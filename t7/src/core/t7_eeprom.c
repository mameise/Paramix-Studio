/*
 * t7_eeprom.c - emulated 4 KB I²C EEPROM backed by Pebble persist
 *
 * Faithful port of asterick/tamago src/cpu/eeprom.js. The state
 * machine, command set, and bit timing all match exactly so save data
 * written by the JS emulator could in principle be loaded here and vice
 * versa.
 *
 * Persistence layout: the 4 KB EEPROM is split into 16 pages of 256
 * bytes each, stored in Pebble persist keys EEPROM_PERSIST_BASE +
 * page_index. Pebble's persist storage supports up to 256 bytes per key
 * and ~4 KB total per app, which is exactly enough.
 *
 * Pages are loaded on init. Writes are batched: we set a dirty bit per
 * page and only push to persist when t7_eeprom_flush() is called,
 * or when an I²C STOP after a WRITE command commits the data. Persist
 * writes are slow (flash-backed) so batching matters.
 */

#include "t7_eeprom.h"
#include "pebble_compat.h"
#ifndef T7_FREESTANDING
#include <string.h>
#endif

#define EEPROM_BIT_WIDTH    12
#define EEPROM_SIZE         (1 << EEPROM_BIT_WIDTH)        // 4096
#define EEPROM_MASK         (EEPROM_SIZE - 1)
#define EEPROM_ADDR_BYTES   2                              // ceil(12/8)
#define EEPROM_PAGE_SIZE    256
#define EEPROM_PAGE_COUNT   (EEPROM_SIZE / EEPROM_PAGE_SIZE)  // 16
#define EEPROM_PERSIST_BASE 100u   // persist keys 100..115 reserved for us

// State machine — values match JS eeprom.js
typedef enum {
  ST_DISABLED = 0,
  ST_COMMAND  = 1,
  ST_ADDRESS  = 2,
  ST_READ     = 3,
  ST_WRITE    = 4,
} eeprom_state_t;

static uint8_t  s_data[EEPROM_SIZE];   // 4 KB in .bss
static uint16_t s_dirty;               // bit N set ⇒ page N needs flush

// I²C bus state
static eeprom_state_t s_state;
static uint8_t  s_output;
static uint8_t  s_last_clk;
static uint8_t  s_last_data;

// Transfer state
static uint8_t  s_read;        // 8-bit shift register (bit being assembled)
static uint8_t  s_bits_tx;     // bit counter within byte (0..8, 8 = ACK)
static uint16_t s_address;     // current address pointer (12 bits)
static uint8_t  s_addressbyte; // how many address bytes have we received

// ----- Persistence -------------------------------------------------------

void t7_eeprom_init(void)
{
  // Initial bus state: power off, output high (NACK)
  s_state     = ST_DISABLED;
  s_output    = 1;
  s_last_clk  = 0;
  s_last_data = 0;
  s_read      = 0;
  s_bits_tx   = 0;
  s_address   = 0;
  s_addressbyte = 0;
  s_dirty     = 0;

  // Load saved pages from persist. Missing pages stay zero-filled (the
  // Tama firmware treats blank EEPROM as "first boot, run init").
  memset(s_data, 0, sizeof(s_data));
  int loaded_bytes = 0;
  for (int p = 0; p < EEPROM_PAGE_COUNT; p++) {
    uint32_t key = EEPROM_PERSIST_BASE + p;
    if (persist_exists(key)) {
      int n = persist_read_data(key, &s_data[p * EEPROM_PAGE_SIZE],
                                EEPROM_PAGE_SIZE);
      if (n > 0) loaded_bytes += n;
    }
  }
  APP_LOG(APP_LOG_LEVEL_INFO,
          "t7_eeprom: loaded %d bytes from persist",
          loaded_bytes);
}

void t7_eeprom_flush(void)
{
  if (!s_dirty) return;
  int pages_written = 0;
  for (int p = 0; p < EEPROM_PAGE_COUNT; p++) {
    if (s_dirty & (1u << p)) {
      uint32_t key = EEPROM_PERSIST_BASE + p;
      status_t st = persist_write_data(key,
                                       &s_data[p * EEPROM_PAGE_SIZE],
                                       EEPROM_PAGE_SIZE);
      if (st >= 0) {
        s_dirty &= ~(1u << p);
        pages_written++;
      } else {
        APP_LOG(APP_LOG_LEVEL_ERROR,
                "t7_eeprom: persist_write_data key=%u failed (%d)",
                (unsigned)key, (int)st);
        // Leave the dirty bit set so we retry next time.
      }
    }
  }
  if (pages_written > 0) {
    APP_LOG(APP_LOG_LEVEL_INFO,
            "t7_eeprom: flushed %d page(s)", pages_written);
  }
}

void t7_eeprom_wipe(void)
{
  // Clear all 4 KB of EEPROM in-memory + delete all persisted pages.
  // The Tama firmware will see "blank" EEPROM on the next boot and
  // start fresh (egg-select screen).
  memset(s_data, 0, sizeof(s_data));
  s_dirty = 0;
  for (int p = 0; p < EEPROM_PAGE_COUNT; p++) {
    uint32_t key = EEPROM_PERSIST_BASE + p;
    persist_delete(key);
  }
  APP_LOG(APP_LOG_LEVEL_INFO,
          "t7_eeprom: wiped (in-memory + persist)");
}

uint8_t t7_eeprom_output(void)
{
  return s_output;
}

// ----- Bus state machine -------------------------------------------------
//
// Translated 1:1 from eeprom.js update(power, clk, data).

void t7_eeprom_update(uint8_t power, uint8_t clk, uint8_t data)
{
  clk  = clk  ? 1 : 0;
  data = data ? 1 : 0;

  int clk_d  = (int)clk  - (int)s_last_clk;
  int data_d = (int)data - (int)s_last_data;

  s_last_clk  = clk;
  s_last_data = data;

  // No power → chip idle, NACK on the bus.
  if (!power) {
    s_state  = ST_DISABLED;
    s_output = 1;
    return;
  }

  // No bus change → no work.
  if (!clk_d && !data_d) return;

  // Data transition while CLK high = START/STOP condition.
  if (clk && data_d) {
    if (data_d > 0) {
      // STOP condition. The page was marked dirty when we received the
      // last write byte; the actual persist_write_data() flush is
      // deferred to the periodic timer in main.c (every 60s) plus
      // t7_release() on app exit. Persist writes are flash-backed
      // (~10 ms each) and the ROM does dense write bursts during boot
      // and save points — flushing eagerly here drops the emulator
      // throughput by ~50% on heavy-write phases.
      s_state  = ST_DISABLED;
      s_output = 0;
    } else {
      // START condition.
      s_state    = ST_COMMAND;
      s_output   = 0;
      s_bits_tx  = 0;
      s_read     = 0;
    }
  }

  // If we ended up disabled (no active transfer), nothing more to do.
  if (s_state == ST_DISABLED) return;

  if (clk_d > 0) {
    // Rising edge: sample data line into shift register (host → device).
    s_read = (uint8_t)((s_read << 1) | data);
  } else if (clk_d < 0) {
    // Falling edge: drive next output bit (device → host) OR ACK.
    if (s_bits_tx < 8) {
      // Mid-byte: drive output bit if we're in READ mode, else high.
      if (s_state == ST_READ) {
        s_output = ((s_data[s_address] << s_bits_tx) & 0x80) ? 1 : 0;
      } else {
        s_output = 1;
      }
    } else if (s_bits_tx == 8) {
      // Byte boundary: send ACK and process the byte we just received.
      s_output = 0;  // ACK

      switch (s_state) {
        case ST_COMMAND:
          // I²C EEPROM address pattern. 0xA0/0xA1 = Atmel 24Cxx family
          // device address with R/W bit; the chip ignores the middle
          // bits hence the 0xF1 mask.
          switch (s_read & 0xF1) {
            case 0xA0:  // Write command
              s_state       = ST_ADDRESS;
              s_addressbyte = 0;
              s_address     = 0;
              break;
            case 0xA1:  // Read command
              s_state = ST_READ;
              break;
            default:
              s_output = 1;  // NACK
              break;
          }
          break;

        case ST_ADDRESS:
          s_address = (uint16_t)((s_address << 8) | s_read);
          if (++s_addressbyte >= EEPROM_ADDR_BYTES) {
            s_state = ST_WRITE;
            s_address &= EEPROM_MASK;
          }
          break;

        case ST_WRITE: {
          uint16_t addr = s_address & EEPROM_MASK;
          s_data[addr] = s_read;
          // Mark page dirty for the next flush.
          s_dirty |= (1u << (addr / EEPROM_PAGE_SIZE));
          s_address = (s_address + 1) & EEPROM_MASK;
          break;
        }

        case ST_READ:
          // Address pointer auto-increments on read too — matches
          // 24Cxx sequential-read behaviour.
          s_address = (s_address + 1) & EEPROM_MASK;
          break;

        default:
          break;
      }
    }

    s_bits_tx = (uint8_t)((s_bits_tx + 1) % 9);
  }
}
