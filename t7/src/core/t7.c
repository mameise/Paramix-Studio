/*
 * t7.c - top-level emulator integration
 *
 * Provides the public API declared in t7.h.
 */

#include "t7_internal.h"
#include "t7_eeprom.h"
#include "t7_sound.h"
#ifdef T7_FASTRUN
uint32_t t7_cpu_run(uint32_t budget);
#endif

// ----- Global state definitions ------------------------------------------
//
// Declared extern in t7_internal.h. Living as plain globals (rather
// than fields of a g_sys struct) lets the compiler resolve addresses at
// compile time and skip a pointer-indirection on every hot-path memory
// access. ~52 KB total, all in .bss.

cpu6502_t g_cpu;
uint8_t   g_wram[T7_WRAM_SIZE];
uint8_t   g_dram[T7_DRAM_SIZE];
uint8_t   g_cpureg[T7_CPUREG_SIZE];

#if T7_PROFILE
// Profiling counters — see t7_internal.h. Plain global to let the
// linker resolve direct addresses (one less indirection vs a struct
// member access through a pointer).
t7_profile_t g_t7_profile;
#endif
uint8_t  *g_rom_bank_buf = NULL;   // malloc'd in t7_init
uint8_t  *g_static_rom   = NULL;   // malloc'd in t7_init
uint16_t  g_irq_vectors[16];
uint8_t   g_irq_pending_any;
uint8_t   g_rom_bank_id;
bool      g_rom_loaded;
uint8_t   g_keys;
ResHandle g_rom_resource;
bool      g_initialised;

// ----- Init / Release -----------------------------------------------------

bool t7_init(void)
{
  if (g_initialised) {
    APP_LOG(APP_LOG_LEVEL_WARNING, "t7_init: already initialised");
    return true;
  }

  // Allocate the big ROM caches on the heap. They have to live somewhere
  // and putting them in .bss pushes us over Pebble's 16-bit virtual-size
  // limit (65535 bytes). The heap on Emery has plenty of room.
#ifdef T7_NOMALLOC
  /* Paradise: no heap. The banks are read straight from the mapped flash, only the
   * static ROM is copied (16 KB in RAM). */
  { static uint8_t s_static[T7_STATIC_ROM_SIZE]; static uint8_t s_bank1[256];
    g_static_rom = s_static; g_rom_bank_buf = s_bank1; }
#else
  g_rom_bank_buf = (uint8_t *)malloc(T7_ROM_BANK_SIZE);
  g_static_rom   = (uint8_t *)malloc(T7_STATIC_ROM_SIZE);
#endif
#ifndef T7_NOMALLOC
  if (!g_rom_bank_buf || !g_static_rom) {
    APP_LOG(APP_LOG_LEVEL_ERROR,
            "t7_init: out of memory (bank=%p static=%p)",
            g_rom_bank_buf, g_static_rom);
    if (g_rom_bank_buf) { free(g_rom_bank_buf); g_rom_bank_buf = NULL; }
    if (g_static_rom)   { free(g_static_rom);   g_static_rom   = NULL; }
    return false;
  }
#endif

  memset(&g_cpu, 0, sizeof(g_cpu));
  memset(g_wram, 0, sizeof(g_wram));
  memset(g_dram, 0, sizeof(g_dram));
  memset(g_cpureg, 0, sizeof(g_cpureg));
#ifndef T7_NOMALLOC
  memset(g_rom_bank_buf, 0, T7_ROM_BANK_SIZE);
#endif
  memset(g_static_rom,   0, T7_STATIC_ROM_SIZE);
  g_keys = 0xFF;
  g_rom_bank_id = 0xFF;
  g_rom_loaded = false;

  if (!t7_load_static_rom()) return false;
  t7_set_rom_bank(0);
  t7_eeprom_init();      // load save data from persist
  t7_cpu_reset();

  g_initialised = true;
  APP_LOG(APP_LOG_LEVEL_INFO,
          "t7_init: ready, PC=$%04x", g_cpu.pc);
  return true;
}

void t7_release(void)
{
  // Flush any pending EEPROM writes back to persist before we tear down.
  // Without this, save data written between the last STOP-after-WRITE
  // and app exit would be lost.
  t7_eeprom_flush();
#ifndef T7_NOMALLOC
  if (g_rom_bank_buf) { free(g_rom_bank_buf); g_rom_bank_buf = NULL; }
  if (g_static_rom)   { free(g_static_rom);   g_static_rom   = NULL; }
#endif
  g_initialised = false;
}

void t7_reset(void)
{
  if (!g_initialised) return;
  t7_cpu_reset();
}

// ----- Stepping -----------------------------------------------------------

uint8_t T7_HOT t7_step_one(void)
{
  if (!g_initialised) return 0;
#ifdef T7_FASTRUN
  return (uint8_t)t7_cpu_run(1);
#else
  return t7_cpu_step();
#endif
}

static uint32_t s_tbh_accumulator = 0;
#define TBH_RATE (T7_CLOCK_RATE / 2)
#ifdef T7_VIDEO_CYCLES
/* Paradise (as the ESP32 port): video IRQ at 60 Hz of emulated time, independent of how
 * often and with how many cycles this is called. */
#define VIDEO_HZ 60
#define VIDEO_PERIOD_CYCLES (T7_CLOCK_RATE / VIDEO_HZ)
static uint32_t s_video_accumulator = 0;
uint32_t T7_HOT t7_step_cycles(uint32_t target_cycles)
{
  if (!g_initialised) return 0;
  uint32_t spent = 0;
  while (spent < target_cycles) {
    uint32_t step = target_cycles - spent;
    uint32_t to_video = VIDEO_PERIOD_CYCLES - s_video_accumulator;
    uint32_t to_tbh   = TBH_RATE - s_tbh_accumulator;
    if (to_video < step) step = to_video;
    if (to_tbh   < step) step = to_tbh;
    if (step == 0) step = 1;
    uint32_t did = 0;
#ifdef T7_FASTRUN
    did = t7_cpu_run(step);
#else
    if (g_snd_on) {
      while (did < step) {
        uint32_t c = t7_cpu_step();
        did += c;
        if ((g_snd_next -= (int32_t)c) <= 0) t7_snd_service();
      }
    } else {
      while (did < step) did += t7_cpu_step();
    }
#endif
    spent += did;
    s_video_accumulator += did;
    if (s_video_accumulator >= VIDEO_PERIOD_CYCLES) {
      s_video_accumulator -= VIDEO_PERIOD_CYCLES;
      t7_fire_irq(10);
      t7_fire_nmi(6);
    }
    s_tbh_accumulator += did;
    if (s_tbh_accumulator >= TBH_RATE) {
      s_tbh_accumulator -= TBH_RATE;
      t7_fire_irq(13);
    }
  }
  if (g_snd_on) t7_snd_flush();
  return spent;
}
#else


// "Video" IRQ/NMI fire rate.
//
// On real V7 hardware these are driven by the LCD controller, which
// runs at roughly 60 Hz. We step the emulator 20× per second, so we need
// to fire 3 sub-frames worth of NMI/IRQ per call to match the rate the
// ROM expects.
//
// Symptoms when this is wrong: the Tama's clock runs slow because the
// game counts video frames to measure real time (e.g. "60 frames = 1
// second"). At 20 Hz it would clock at 1/3 speed.
#define VIDEO_FIRES_PER_FRAME 3

uint32_t T7_HOT t7_step_cycles(uint32_t target_cycles)
{
  if (!g_initialised) return 0;

  // Split the requested cycles into VIDEO_FIRES_PER_FRAME chunks so the
  // ROM sees the video interrupt at roughly the hardware rate.
  uint32_t per_chunk = target_cycles / VIDEO_FIRES_PER_FRAME;
  uint32_t spent = 0;

  for (int chunk = 0; chunk < VIDEO_FIRES_PER_FRAME; chunk++) {
    t7_fire_irq(10);
    t7_fire_nmi(6);

    uint32_t chunk_target = (chunk == VIDEO_FIRES_PER_FRAME - 1)
                              ? target_cycles   // last chunk takes the rest
                              : (spent + per_chunk);
    if (g_snd_on) {
      while (spent < chunk_target) {
        uint32_t c = t7_cpu_step();
        spent += c;
        if ((g_snd_next -= (int32_t)c) <= 0) t7_snd_service();
      }
    } else {
      while (spent < chunk_target) {
        spent += t7_cpu_step();
      }
    }
  }

  if (g_snd_on) t7_snd_flush();

  // 2 Hz timer (IRQ 13) — the "TBH" counter that drives the game's
  // real-time clock. Independent of the video IRQ rate.
  s_tbh_accumulator += spent;
  while (s_tbh_accumulator >= TBH_RATE) {
    t7_fire_irq(13);
    s_tbh_accumulator -= TBH_RATE;
  }
  return spent;
}
#endif

// ----- Display ------------------------------------------------------------

static const uint16_t LCD_ROW_OFFSETS[31] = {
  0x0C0, 0x0CC, 0x0D8, 0x0E4,
  0x0F0, 0x0FC, 0x108, 0x114,
  0x120, 0x12C, 0x138, 0x144,
  0x150, 0x15C, 0x168, 0x174,
  0x0B4, 0x0A8, 0x09C, 0x090,
  0x084, 0x078, 0x06C, 0x060,
  0x054, 0x048, 0x03C, 0x030,
  0x024, 0x018, 0x00C,
};

void t7_get_display(uint8_t *out)
{
  if (!g_initialised || !out) return;
  for (int y = 0; y < T7_LCD_HEIGHT; y++) {
    uint16_t base = LCD_ROW_OFFSETS[y] % T7_DRAM_SIZE;
    for (int x = 0; x < T7_LCD_WIDTH; x += 4) {
      uint8_t d = g_dram[base++];
      out[y * T7_LCD_WIDTH + x + 0] = (d >> 6) & 0x3;
      out[y * T7_LCD_WIDTH + x + 1] = (d >> 4) & 0x3;
      out[y * T7_LCD_WIDTH + x + 2] = (d >> 2) & 0x3;
      out[y * T7_LCD_WIDTH + x + 3] = (d >> 0) & 0x3;
    }
  }
}

// Icon extraction matches the refresh_simple loop in t7emu/main.js:
//   var a=4, b=0, g=0;
//   while (g<10) { glyph = (dram[a] >> b) & 3; if ((b-=2)<0){b=6;a++;} ... }
//
// Walking the loop manually gives the table below. Two bits per icon,
// four icons per byte, 10 icons total across 4 bytes (DRAM[4..7]) — but
// only the first 2 bits of DRAM[4] and first 2 bits of DRAM[7] are used.
void t7_get_icons(uint8_t *out)
{
  if (!g_initialised || !out) return;
  out[0] = (g_dram[4] >> 0) & 0x3;   // dashboard
  out[1] = (g_dram[5] >> 6) & 0x3;   // food
  out[2] = (g_dram[5] >> 4) & 0x3;   // trash
  out[3] = (g_dram[5] >> 2) & 0x3;   // globe
  out[4] = (g_dram[5] >> 0) & 0x3;   // user
  out[5] = (g_dram[6] >> 6) & 0x3;   // comments
  out[6] = (g_dram[6] >> 4) & 0x3;   // medkit
  out[7] = (g_dram[6] >> 2) & 0x3;   // heart
  out[8] = (g_dram[6] >> 0) & 0x3;   // book
  out[9] = (g_dram[7] >> 6) & 0x3;   // bell
}

// ----- Buttons ------------------------------------------------------------

void t7_set_buttons(uint8_t mask)
{
  if (!g_initialised) return;
  uint8_t lo = ~mask & 0x0F;
  // Obere Bits MUESSEN 0 bleiben (wie in der Referenz: this.keys = 0xF).
  // Frueher stand hier "| 0xF0", was die Figur-Bits (Bit 5-7 in PortA) komplett
  // maskiert hat -> die Firmware konnte eine gesteckte Figur nie erkennen.
  g_keys = lo;
}

// ----- WRAM direct access (used by RTC sync) ------------------------------

uint8_t t7_ram_read(uint16_t addr)
{
  if (!g_initialised) return 0;
  if (addr >= T7_WRAM_SIZE) return 0;
  return g_wram[addr];
}

void t7_ram_write(uint16_t addr, uint8_t val)
{
  if (!g_initialised) return;
  if (addr >= T7_WRAM_SIZE) return;
  g_wram[addr] = val;
}

// ----- Debug --------------------------------------------------------------

void t7_debug_get_state(uint16_t *pc, uint8_t *a, uint8_t *x, uint8_t *y,
                            uint8_t *s, uint8_t *bank)
{
  if (!g_initialised) return;
  if (pc)   *pc   = g_cpu.pc;
  if (a)    *a    = g_cpu.a;
  if (x)    *x    = g_cpu.x;
  if (y)    *y    = g_cpu.y;
  if (s)    *s    = g_cpu.s;
  if (bank) *bank = g_rom_bank_id;
}

bool t7_debug_dram_dirty(void)
{
  if (!g_initialised) return false;
  for (int i = 0; i < T7_DRAM_SIZE; i++) if (g_dram[i]) return true;
  return false;
}

uint16_t t7_debug_dram_nonzero_count(void)
{
  if (!g_initialised) return 0;
  uint16_t n = 0;
  for (int i = 0; i < T7_DRAM_SIZE; i++) if (g_dram[i]) n++;
  return n;
}

// ----- Save state ---------------------------------------------------------

#define T7_SAVE_MAGIC   0x54414D47
#define T7_SAVE_VERSION 1
#define T7_SAVE_SIZE                                                     \
  (4 + 4 + sizeof(cpu6502_t) +                                               \
   T7_CPUREG_SIZE + T7_WRAM_SIZE + T7_DRAM_SIZE + 1 + 1)

uint32_t t7_serialize_state(uint8_t *buf, uint32_t bufsize)
{
  if (!buf || bufsize < T7_SAVE_SIZE) return T7_SAVE_SIZE;
  if (!g_initialised) return 0;
  uint8_t *p = buf;
  uint32_t magic = T7_SAVE_MAGIC;
  memcpy(p, &magic, 4); p += 4;
  *p++ = T7_SAVE_VERSION;
  *p++ = 0; *p++ = 0; *p++ = 0;
  memcpy(p, &g_cpu, sizeof(cpu6502_t)); p += sizeof(cpu6502_t);
  memcpy(p, g_cpureg, T7_CPUREG_SIZE); p += T7_CPUREG_SIZE;
  memcpy(p, g_wram, T7_WRAM_SIZE);     p += T7_WRAM_SIZE;
  memcpy(p, g_dram, T7_DRAM_SIZE);     p += T7_DRAM_SIZE;
  *p++ = g_rom_bank_id;
  *p++ = g_keys;
  return (uint32_t)(p - buf);
}

bool t7_deserialize_state(const uint8_t *buf, uint32_t bufsize)
{
  if (!buf || bufsize < T7_SAVE_SIZE || !g_initialised) return false;
  const uint8_t *p = buf;
  uint32_t magic;
  memcpy(&magic, p, 4); p += 4;
  if (magic != T7_SAVE_MAGIC) return false;
  uint8_t version = *p++;
  if (version != T7_SAVE_VERSION) return false;
  p += 3;
  memcpy(&g_cpu, p, sizeof(cpu6502_t)); p += sizeof(cpu6502_t);
  memcpy(g_cpureg, p, T7_CPUREG_SIZE); p += T7_CPUREG_SIZE;
  memcpy(g_wram, p, T7_WRAM_SIZE);     p += T7_WRAM_SIZE;
  memcpy(g_dram, p, T7_DRAM_SIZE);     p += T7_DRAM_SIZE;
  uint8_t saved_bank = *p++;
  g_keys = *p++;
  g_irq_pending_any = (g_cpureg[0x73] | g_cpureg[0x74]) != 0;
  t7_set_rom_bank(saved_bank);
  t7_snd_resync();
  return true;
}

void t7_profile_snapshot_and_reset(t7_profile_snapshot_t *out)
{
#if T7_PROFILE
  if (out) {
    out->opcodes        = g_t7_profile.opcodes;
    out->reads_fast     = g_t7_profile.reads_fast;
    out->reads_io       = g_t7_profile.reads_io;
    out->writes_fast    = g_t7_profile.writes_fast;
    out->writes_io      = g_t7_profile.writes_io;
    out->writes_dropped = g_t7_profile.writes_dropped;
    out->irqs           = g_t7_profile.irqs;
    out->nmis           = g_t7_profile.nmis;
    out->irq_entries    = g_t7_profile.irq_entries;
    out->nmi_entries    = g_t7_profile.nmi_entries;
  }
  memset(&g_t7_profile, 0, sizeof(g_t7_profile));
#else
  if (out) memset(out, 0, sizeof(*out));
#endif
}
