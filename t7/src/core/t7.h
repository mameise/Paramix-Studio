/*
 * t7.h - V7 emulator core (6502), urspruenglich fuer Pebble
 *
 * Based on asterick/tamago (JavaScript implementation):
 *   https://github.com/asterick/tamago
 *
 * Hardware: GPLB52320A (8-bit 6502-family) @ 4 MHz
 * Display:  64x32 pixels, 4 grayscale levels (512 bytes DRAM)
 *
 * Memory map (16-bit address space, banked):
 *   $0000-$0FFF  Work RAM (1.5 KB, mirrored)
 *   $1000-$2FFF  Display RAM (512 bytes, mirrored)
 *   $3000-$3FFF  CPU control registers
 *   $4000-$BFFF  Banked ROM (32 KB window, 20 banks total = 640 KB)
 *   $C000-$FFFF  Static ROM (16 KB, always mapped from bank 0)
 *
 * The 640 KB ROM lives in Pebble flash as a resource. To save RAM we
 * only keep the currently-selected bank (32 KB) mirrored in memory.
 * Static ROM (bank 0, first 16 KB) is loaded once at boot.
 */

#ifndef T7_H
#define T7_H

#include "pebble_compat.h"

// Compile-time profile switch. Set to 1 to enable per-opcode / per-read /
// per-write counters (logged once a minute from main.c). Adds ~10-15%
// CPU overhead — keep OFF in production.
#define T7_PROFILE 0

// Display dimensions (native V7 hardware).
//
// The display is 48 pixels wide × 31 pixels high — this comes from the
// HTML template in the JS reference (canvas width=48 height=31). Although
// the hardware has 64 segments × 32 commons, the addressing scheme in
// _dram only gives meaningful pixels in the first 48 of each row; the
// remaining 16 are aliased into the start of the next row's bytes.
// The JS emulator handles this by drawing into a 48-px-wide canvas — the
// 16 "extra" pixels per row spill into the next row and get overwritten.
// In our C port we just clamp to 48 explicitly.
#define T7_LCD_WIDTH    48
#define T7_LCD_HEIGHT   31

// 4 grayscale levels per pixel (2 bits each)
#define T7_PALETTE_SIZE 4

// CPU clock rate (Hz)
#define T7_CLOCK_RATE   4000000

// Memory region sizes
#define T7_WRAM_SIZE    0x600   // 1.5 KB work RAM
#define T7_DRAM_SIZE    0x200   // 512 B display RAM
#define T7_CPUREG_SIZE  0x100   // 256 B control registers
#define T7_ROM_BANK_SIZE 0x8000 // 32 KB per bank
#define T7_STATIC_ROM_SIZE 0x4000  // 16 KB static (high) ROM

// Status flag bits in P register
#define FLAG_C  0x01  // Carry
#define FLAG_Z  0x02  // Zero
#define FLAG_I  0x04  // Interrupt disable
#define FLAG_D  0x08  // Decimal mode
#define FLAG_B  0x10  // Break (software)
#define FLAG_U  0x20  // Unused (always 1)
#define FLAG_V  0x40  // Overflow
#define FLAG_N  0x80  // Negative

// Button bitmask (V7 has 3 buttons: A, B, C plus reset)
#define T7_BTN_A     0x01
#define T7_BTN_B     0x02
#define T7_BTN_C     0x04
#define T7_BTN_RESET 0x08

// Forward declaration for the system state
typedef struct t7_system t7_system_t;

// Public API ---------------------------------------------------------------

// Initialize the emulator. Must be called once at startup. Returns false on
// failure (e.g. ROM resource missing).
bool t7_init(void);
/* After t7_init: 1 = Tamagotchi Friends ROM (768 KB), 0 = Tama-Go. */
int      t7_is_friends(void);
uint16_t t7_rtc_sec_addr(void);

// Reset the CPU (jumps to the reset vector). Memory and EEPROM are preserved.
void t7_reset(void);

// Run the emulator for the given number of CPU cycles. Returns the actual
// number of cycles consumed (may differ slightly due to instruction lengths).
uint32_t t7_step_cycles(uint32_t target_cycles);

// Run a single instruction. Returns the cycle count of that instruction.
uint8_t t7_step_one(void);

// Read the current display state. Pixels are 2 bits each (0..3 = grayscale
// level). `out` must be at least T7_LCD_WIDTH * T7_LCD_HEIGHT bytes.
void t7_get_display(uint8_t *out);

// Number of status icons surrounding the main display. Layout (matching
// asterick/tamago):
//   Top row:    0 dashboard, 1 food,    2 trash,  3 globe, 4 user
//   Bottom row: 5 comments,  6 medkit,  7 heart,  8 book,  9 bell
#define T7_ICON_COUNT  10

// Read the 10 status icon values into `out` (length T7_ICON_COUNT).
// Each byte is a 2-bit intensity (0 = off, 1..3 = visible grayscale).
void t7_get_icons(uint8_t *out);

// --- Figur / SPI-Trace (Reverse-Engineering, Etappe 1) ---
void    t7_figure_set(uint8_t id);
uint8_t t7_figure_get(void);
void    t7_spi_set_ret(uint8_t v);
void    t7_spitrace_arm(void);
void    t7_irtrace_arm(void);
int     t7_spitrace_count(void);
void    t7_spitrace_get(int i, uint8_t *kind, uint8_t *reg, uint8_t *val, uint16_t *rep);

// Set the button state. `mask` is a bitmask of T7_BTN_* constants.
// Bits set = button pressed.
void t7_set_buttons(uint8_t mask);

// IR-BLE-Bridge: 32-Byte-Paket fuer die naechste Empfangsanfrage einspeisen
// (umgeht das IR-Physical-Layer). Aus BLE-Callback oder Test-Trigger aufrufen.
void t7_ir_bridge_rx(const uint8_t *pkt32);
void t7_ir_bridge_enable(int on);          // Bridge ein/aus (TX-Completion-Fake)
int  t7_ir_bridge_tx_poll(uint8_t *out32);  // 1 = neues Sende-Paket in out32
uint32_t t7_ir_activity(void);              // steigt im IR-Verbindungsmenue
int  t7_ir_tx_pending(void);                // 1 = Firmware wartet auf Sende-Completion
void t7_ir_tx_complete(void);               // Sende-Completion ausloesen (zeitverzoegert)

// Tear down the emulator and free resources.
void t7_release(void);

// Debug: return current CPU state for logging. Fills the passed-in pointers
// (any of which may be NULL) with the current register values.
void t7_debug_get_state(uint16_t *pc, uint8_t *a, uint8_t *x, uint8_t *y,
                            uint8_t *s, uint8_t *bank);

// Debug: return whether any dram bytes are non-zero. If the ROM hasn't
// touched the display at all, all dram = 0 and the screen appears uniform.
bool t7_debug_dram_dirty(void);

// Debug: count how many DRAM bytes are non-zero (0..512).
uint16_t t7_debug_dram_nonzero_count(void);

// State serialization for save/restore. Returns total size needed when
// `buf` is NULL or `bufsize` is too small; otherwise the number of bytes
// actually written.
uint32_t t7_serialize_state(uint8_t *buf, uint32_t bufsize);
bool t7_deserialize_state(const uint8_t *buf, uint32_t bufsize);

// Direct WRAM read/write. Used by the RTC sync code to poke the
// emulator's internal time counters. `addr` must be in the 0x0000-0x0FFF
// WRAM range — addresses outside that range are silently ignored.
uint8_t t7_ram_read(uint16_t addr);
void    t7_ram_write(uint16_t addr, uint8_t val);

// Profiling — snapshot + reset the hot-path counters. Used by main.c
// to print a per-minute profile of where emulator time is going.
// Returns the snapshot via out-params. Pass NULL to skip retrieval.
typedef struct {
  uint32_t opcodes;
  uint32_t reads_fast;
  uint32_t reads_io;
  uint32_t writes_fast;
  uint32_t writes_io;
  uint32_t writes_dropped;
  uint32_t irqs;
  uint32_t nmis;
  uint32_t irq_entries;
  uint32_t nmi_entries;
} t7_profile_snapshot_t;

void t7_profile_snapshot_and_reset(t7_profile_snapshot_t *out);

#endif // T7_H
