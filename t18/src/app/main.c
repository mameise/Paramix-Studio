/* T18 (Meets/On, TG18 firmware, ARM7) on the Tamagotchi Paradise.
 * Started like TamaEmu by the stub (A+C on the home screen). The user's own firmware is put into
 * the Paradise flash by the Studio; none is part of this program.
 * Wheel: long press = save and back to the Paradise. Short press: speed display on/off.
 *
 * Memory (the RAM at 0x20000000 has only 4 KB, which belong to the Paradise firmware):
 *   PRAM 0x00000000 (64 KB): hot code; after the start the translated code (from __jc_start on, over
 *     the start code). The PRAM is only written with no instruction fetch from it at the same time
 *     (t18_pram_w16 runs from the flash window).
 *   SRAM 0x18000000 (128 KB): the guest SRAM 1:1, except for the hole 0x1800E600..0x18014400, which
 *     holds this program's stack and data (see t18cpu.h).
 *   Flash window 0x10000000 (base 0x60011000): code that is not hot, at flash T18_XIP_OFF.
 * Author: mameise */
#include "../../../emu/src/emu/hw.h"
#include "../../../emu/src/emu/ask.h"
#include "../../../emu/src/emu/flash.h"
#include "../../../emu/src/emu/paraclock.h"
#ifdef PARA_BACK
#define PB_ATTR __attribute__((section(".xiptext")))   /* 1.6.8: only when leaving; the PRAM is for translated code (para_leave_mark stays in the PRAM: it runs before the window is set up) */
#include "../../../emu/src/emu/paraback.h"
#endif
#include "../core/t18soc.h"
#include "t18app.h"
#include <string.h>
#include <stdint.h>

#define COLD __attribute__((section(".xiptext"), noinline))

__attribute__((section(".pmxcfg"), used))
volatile const T18Cfg g_cfg = { .magic = T18_MAGIC, .version = 1, .save_off = 0xC0000u };

#define SAVE (g_cfg.save_off)
#ifdef HW_OPTS
static uint32_t g_scr_ms, g_key_ms; static int g_scr_dark;   /* 1.6.8: screen off after (Studio) */
#endif
#define XIP_BASE_REG REG(0x4002F004u)
#define XIP_CTRL_REG REG(0x4002F000u)

/* ---- time: SysTick 1 ms ---- */
static volatile uint32_t g_ms;
#define SYST_CSR REG(0xE000E010u)
#define SYST_RVR REG(0xE000E014u)
#define SYST_CVR REG(0xE000E018u)
#define CORE_HZ 96000000u
#ifdef T18_PROF_OVL
/* Test build: where the time goes. Each ms the interrupted PC goes into a bucket: PRAM in 64 bytes
 * (1024), flash window 0x100DF000 in 1 KB (64), SRAM code, rest. Counters in the Paradise's RAM (cleared
 * when it restarts): no room in the hole. */
#define g_pb ((volatile uint16_t *)0x20000400u)
#define PB_N 1090u
static volatile uint32_t g_pflag, g_pn, g_ppres, g_pwait;
#ifdef T18_PROF_GPC
static uint32_t g_tg[3600 / 4];                /* (defined again below) */
#endif
__attribute__((used)) void systick_c(uint32_t *fr)
{
  g_ms++;
  uint32_t pc = fr[6], i;
#ifdef T18_TIERUP
  { extern volatile uint32_t t18_tu_pc; extern uint32_t __jc_start[], __jc_end[];
    if (pc - (uint32_t)__jc_start < (uint32_t)__jc_end - (uint32_t)__jc_start) t18_tu_pc = pc; }
#endif
#ifndef T18_PROF_LITE
  if (pc < 0x10000u) i = pc >> 6; else if (pc - 0x100DF000u < 0x10000u) i = 1024u + ((pc - 0x100DF000u) >> 10); else if (pc - 0x18000000u < 0x20000u) i = 1088u; else i = 1089u;
  if (g_pb[i] < 0xFFFFu) g_pb[i]++;
#elif defined(T18_PROF_PC)
  /* test13: coarse PC buckets in the Paradise's RAM: 0 code buffer, 1..32 PRAM in 2 KB, 33..48 flash
   * window in 4 KB, 49 SRAM, 50 rest */
  { extern uint16_t __jc_start[], __jc_end[];
#ifdef T18_PROF_PC_LO
    /* test15: fine buckets of 128 bytes from T18_PROF_PC_LO (48 of them), 48 code buffer, 49 other PRAM, 50 rest */
    if (pc >= (uint32_t)__jc_start && pc < (uint32_t)__jc_end) i = 48;
#ifndef T18_PROF_PC_SH
#define T18_PROF_PC_SH 7
#endif
    else if (pc - T18_PROF_PC_LO < (48u << T18_PROF_PC_SH)) i = (pc - T18_PROF_PC_LO) >> T18_PROF_PC_SH;
    else if (pc < 0x10000u) i = 49u; else i = 50u;
#else
    if (pc >= (uint32_t)__jc_start && pc < (uint32_t)__jc_end) i = 0;
    else if (pc < 0x10000u) i = 1u + (pc >> 11);
    else if (pc - 0x100DF000u < 0x10000u) i = 33u + ((pc - 0x100DF000u) >> 12);
    else if (pc - 0x18000000u < 0x20000u) i = 49u; else i = 50u;
#endif
    ((volatile uint32_t *)T18_PROF_PC)[i]++;
#ifdef T18_PROF_GPC
    /* 1.6.8 test: while the interpreter runs (exec_arm .. idle_try), which guest code (64-byte regions, 6 places) */
    extern const uintptr_t t18_interp_lo, t18_interp_hi;
    if (pc - (t18_interp_lo & ~1u) < t18_interp_hi - t18_interp_lo) {
      volatile uint32_t *gp = (volatile uint32_t *)0x20000FD0u;
      uint32_t g = (((arm7_t *)tg18_cpu((tg18_t *)g_tg))->last_pc & ~0x3Fu) | 1u;
      int k, m = 0;
      for (k = 0; k < 6; k++) { if (gp[2 * k] == g) { gp[2 * k + 1]++; break; } if (gp[2 * k + 1] < gp[2 * m + 1]) m = k; }
      if (k == 6) { gp[2 * m] = g; gp[2 * m + 1] = 1; }
    }
#endif
  }
#else
  (void)pc; (void)i;
#endif
  g_pn++; if (g_pflag & 1u) g_ppres++; if (g_pflag & 2u) g_pwait++;
}
__attribute__((naked)) void SysTick_Handler(void) { __asm__ volatile("tst lr, #4\n ite eq\n mrseq r0, msp\n mrsne r0, psp\n b systick_c\n"); }
#elif defined(T18_TIERUP)
/* 1.6.8: the interrupted pc, when it lies in the code buffer, marks a hot block (t18jit.c, tu_note) */
extern volatile uint32_t t18_tu_pc; extern uint32_t __jc_start[], __jc_end[];
__attribute__((used)) void systick_c(uint32_t *fr)
{
  g_ms++;
  uint32_t pc = fr[6];
  if (pc - (uint32_t)__jc_start < (uint32_t)__jc_end - (uint32_t)__jc_start) t18_tu_pc = pc;
}
__attribute__((naked)) void SysTick_Handler(void) { __asm__ volatile("tst lr, #4\n ite eq\n mrseq r0, msp\n mrsne r0, psp\n b systick_c\n"); }
#else
__attribute__((used)) void systick_c(void) { g_ms++; }
__attribute__((naked)) void SysTick_Handler(void) { __asm__ volatile("b systick_c\n"); }
#endif
static uint32_t rtc_now(void) { return REG(0x45000304u); }   /* Paradise seconds counter (runs while off) */

static uint32_t g_tg[3600 / 4];                /* tg18_t */
#define T ((tg18_t *)g_tg)
__attribute__((used)) volatile uint32_t g_t18st[8];   /* emulated ms, dropped ms, flash writes, map writes, -, lost writes (for measuring) */
static uint32_t g_para, g_para_cnt;           /* Paradise local time at the start (seconds since 1970, 0 = not found) and its counter then */
extern uint32_t __stack_bottom;

/* ---- pages for the guest's bytes in the hole ---- */
#ifndef NPOOL
#define NPOOL 12
#endif
static uint8_t g_pool[NPOOL][T18_HOLE_PG] __attribute__((aligned(4)));
static uint32_t g_pool_n;
static uint8_t *hole_new(void) { if (g_pool_n >= NPOOL) return 0; uint8_t *p = g_pool[g_pool_n++]; memset(p, 0, T18_HOLE_PG); return p; }
static void hole_free(void) { g_pool_n = 0; }

/* ---- the code buffer in the PRAM: written from the flash window with interrupts off ---- */
/* T18_PRAM_WR_SRAM (on): the writer runs from the SRAM, as the copy loop of TamaEmu (proven on the device).
 * Without: from the flash window. */
#ifdef T18_PRAM_WR_SRAM
#define PW_SECT ".ramfunc"
#else
#define PW_SECT ".xiptext"
#endif
__attribute__((section(PW_SECT), noinline, used)) void t18_pram_w16(uint16_t *p, uint32_t v)
{
  uint32_t pm;
  __asm__ volatile("mrs %0, primask\n cpsid i" : "=r"(pm) :: "memory");
#ifdef T18_PRAM_HALF
  *(volatile uint16_t *)p = (uint16_t)v;
#else
  /* 1.6.2: whole words only. On the device a halfword write merges with the halfword written last
   * (somewhere else), not with what is in the word: the other half of the word got wrong (E1). */
  volatile uint32_t *w = (volatile uint32_t *)((uint32_t)p & ~3u);
  uint32_t o = *w;
  *w = ((uint32_t)p & 2u) ? (o & 0xFFFFu) | (v << 16) : (o & 0xFFFF0000u) | (v & 0xFFFFu);
#endif
  __asm__ volatile("dsb\n isb\n msr primask, %0" :: "r"(pm) : "memory");
}

/* ---- errors: red picture with a code, wheel = back to the Paradise (nothing saved) ---- */
static const uint16_t DIG[17] = { 0x7B6F, 0x2C97, 0x73E7, 0x73CF, 0x5BC9, 0x79CF, 0x79EF, 0x7249, 0x7BEF, 0x7BCF, 0x52A5 /* % */,
                                  0x2BED, 0x6BAE, 0x3923, 0x6B6E, 0x79E7, 0x79E4 /* A..F */ };
static void glyph_row(uint16_t *row, int x0, int ch, int y, uint16_t fg)
{
  if (y < 0 || y >= 10) return;
  for (int cx = 0; cx < 6; cx++) if ((DIG[ch] >> (14 - ((y / 2) * 3 + cx / 2))) & 1) row[x0 + cx] = fg;
}
/* How far the start got (shown on the red screen): 1 hw_init, 2 flash part ok, 3 map, 4 SoC, 5 session,
 * 6 clock, 7 main loop, 8 first picture. */
__attribute__((used)) volatile uint32_t g_stage;
/* Rows of 8 hex digits under "E<code>": n values. */
static void rows_draw(uint32_t code, const uint32_t *v, int n)
{
  uint16_t row[128];
  int two = n > 8, rows = two ? (n + 1) / 2 : n;
  int top = two ? 2 : n > 1 ? 14 : 40, step = two ? 14 : 14, r0 = two ? 16 : n > 1 ? 32 : 70, pitch = two ? 7 : 8;
  hw_px_begin(0, 0, 128, 128);
  for (int y = 0; y < 128; y++) {
    for (int x = 0; x < 128; x++) row[x] = 0xA000;
    if (y >= top && y < top + 10) { glyph_row(row, 52, 15 /* E */, y - top, 0xFFFF); glyph_row(row, 60, (int)(code & 15), y - top, 0xFFFF); }
    for (int k = 0; k < n; k++) {
      int y0 = r0 + (two ? k / 2 : k) * step, x0 = two ? 4 + (k & 1) * 64 : 32;
      if (y < y0 || y >= y0 + 10) continue;
      for (int i = 0; i < 8; i++) { uint32_t d = (v[k] >> (28 - 4 * i)) & 15; glyph_row(row, x0 + i * pitch, d < 10 ? (int)d : (int)d + 1, y - y0, 0xFFFF); }
    }
    (void)rows;
    hw_px_row(row, 128);
  }
  hw_px_end();
  hw_backlight(1);
}
#ifdef PARA_BACK
static uint8_t c_buf[0x1000] __attribute__((aligned(4)));
#endif
__attribute__((noreturn, section(".xiptext"), noinline)) static void fatal_rows(uint32_t code, const uint32_t *v, int n)
{
  __asm__ volatile("cpsid i");
  hw_tone(0);
#ifdef T18_QUIET_FATAL
  (void)code; (void)v; (void)n;                 /* release: no code screen, straight back to the Paradise */
#else
  rows_draw(code, v, n);
  for (u32 t = 0; ; t++) {                       /* wheel released, then pressed */
    hw_wdt_feed();
    static int up;
    if (!(hw_buttons() & BTN_WHEEL)) up = 1; else if (up && t > 200000u) break;
  }
#endif
  REG(0x20000FF8u) = 0;
  REG(0x20000FFCu) = 0x4C584D50u;               /* "PMXL": the autostart stays off */
#ifdef PARA_BACK
  para_back_fix(c_buf);
#endif
  for (u32 t = 0; hw_buttons() && t < 20000000u; t++) if ((t & 0xFFFFu) == 0) hw_wdt_feed();
  XIP_BASE_REG = 0x60011000u; XIP_CTRL_REG = 3u;
  hw_back_to_paradise();
  for (;;) {}
}
__attribute__((noreturn)) static void fatal(uint32_t code, uint32_t info) { fatal_rows(code, &info, 1); }
/* E1: two columns. PC LR / CFSR fault address / stage guest PC / r0 r1 / r2 r3 / r12 xPSR /
 * SP EXC_RETURN / HFSR translator (blocks, bit 31 = interpreter only) */
extern uint32_t t18_jit_stat[8];
#if defined(T18_XIP_PROBE) || defined(T18_RAM_PROBE)
#define T18_PROBE_FAULT
volatile uint32_t g_probe;                /* 1: a probe read is running, 2: it faulted */
extern const uint16_t probe_ldr[], probe_str[];
#endif
__attribute__((used)) void fault_c(uint32_t *fr, uint32_t exc)
{
#ifdef T18_PROBE_FAULT
  if (g_probe == 1u) {
    uint32_t c = REG(0xE000ED28u);
    if ((c & 0x400u) || fr[6] == ((uint32_t)probe_ldr & ~1u) || fr[6] == ((uint32_t)probe_str & ~1u)) {
      if (!(c & 0x400u)) { fr[6] += 2u; fr[0] = 0xDEADBEEFu; }
      g_probe = 2u; REG(0xE000ED28u) = c; REG(0xE000ED2Cu) = REG(0xE000ED2Cu);
      return;
    }
  }
#endif
  uint32_t cfsr = REG(0xE000ED28u), fa = (cfsr & 0x8000u) ? REG(0xE000ED38u) : (cfsr & 0x80u) ? REG(0xE000ED34u) : 0;
  uint32_t v[16] = { fr[6], fr[5], cfsr, fa, g_stage, g_tg[15], fr[0], fr[1], fr[2], fr[3], fr[4], fr[7],
                     (uint32_t)fr, exc, REG(0xE000ED2Cu), t18_jit_stat[0] | ((uint32_t)t18_jit_off << 31) };
  fatal_rows(1, v, 16);
}
__attribute__((naked)) static void Fault_Handler(void)
{
  __asm__ volatile("tst lr, #4\n ite eq\n mrseq r0, msp\n mrsne r0, psp\n mov r1, lr\n b fault_c\n");
}

/* ---- guest flash: block map, one 4 KB sector held in RAM while the guest writes ---- */
static uint16_t g_map[128];
static uint8_t g_spare_used[T18_SPARE_MAX];
static uint8_t c_buf[0x1000] __attribute__((aligned(4)));   /* in the SRAM: the flash controller reads it by DMA */
static uint32_t c_sec = 0xFFFFFFFFu;
static uint8_t c_dirty;

static void flash_put(uint32_t phys, const uint8_t *src) { flash_update4k(phys, src); }
/* 1.6.4: guest flash through the cached window (base 0x60011000) instead of the uncached one
 * (0x60000000, about 1.5 us per access on the device: pictures from the flash made the pet screen
 * 10 times slower). A block the guest has written in this session (or a spare) is read uncached,
 * the cache would not see the new data. Switch T18_FLASH_UNCACHED: always uncached as before. */
static uint8_t g_wblk[128];
static uint32_t g_ncached;              /* blocks read through the cache (shown under the speed) */
/* flash offset the window starts at: 0x11000 (the Paradise's base) or T18_HOT_BASE (hot window) */
static uint32_t g_wbase = 0x11000u;
/* 1.6.6: a block can be read through the cache when the window (1 MB) shows it, apart from the flash part */
static int in_window(uint32_t phys)
{
  uint32_t o = phys - g_wbase, x = T18_XIP_OFF - 0x11000u;
  return phys >= g_wbase && o + 0x10000u <= 0x100000u && !(o < x + T18_XIP_MAX && o + 0x10000u > x);
}
static const uint8_t *blk_xip(uint32_t b)
{
  if (g_map[b] == 0xFFFFu) return 0;
  uint32_t phys = (uint32_t)g_map[b] << 16;
#ifndef T18_FLASH_UNCACHED
  if (!g_wblk[b] && in_window(phys)) return (const uint8_t *)(0x10000000u + phys - g_wbase);
#endif
  return (const uint8_t *)(0x60000000u + phys);
}
#ifdef T18_SEC_SPARE
/* 1.6.8 (test45): spares by the 4 KB sector. A typical install has only 6 spare blocks, but the firmware writes
 * a few sectors in many empty blocks (first start 5 blocks, a photo and the diary 2 more): the 7th block was lost
 * and opening the diary hung (white picture). Now an empty guest block gets single sectors from a pool: pool
 * sector k = spare block k >> 4, sector k & 15 (spare blocks already used whole are skipped). Tables in the
 * Paradise's RAM (no room in the hole): s_idx[guest block] = row + 1, row[sector] = pool sector + 1. Saved in the
 * map sector behind the block map ("T8SS"). */
#define S_IDX ((uint8_t *)0x20000010u)            /* 128 bytes, to 0x20000090 */
#define S_ROWS 19u
static uint8_t *s_row(uint32_t r) { return (uint8_t *)(r < 7u ? 0x20000090u + 16u * r : 0x20000F00u + 16u * (r - 7u)); }
#define S_MAGIC 0x53533854u                        /* "T8SS" */
#define S_OFF (4u + 256u + T18_SPARE_MAX)          /* in the map sector, behind magic, block map, spares in use */
/* Paradise flash of a guest sector, 0 = none */
static uint32_t s_phys(uint32_t sec)
{
  uint32_t i = S_IDX[(sec >> 16) & 127u];
  if (!i) return 0;
  uint32_t k = s_row(i - 1u)[(sec >> 12) & 15u];
  if (!k) return 0;
  k--;
  return ((uint32_t)g_cfg.spare[k >> 4] << 16) | ((k & 15u) << 12);
}
static uint32_t s_lost;
/* a pool sector for the guest sector (0 = none left) */
static uint32_t s_alloc(uint32_t sec)
{
  uint32_t b = (sec >> 16) & 127u, i = S_IDX[b];
  if (!i) {
    for (i = 1; i <= S_ROWS; i++) { int fr = 1; for (uint32_t q = 0; q < 128u && fr; q++) if (S_IDX[q] == i) fr = 0; if (fr) break; }
    if (i > S_ROWS) return 0;
    memset(s_row(i - 1u), 0, 16); S_IDX[b] = (uint8_t)i;
  }
  uint32_t n = g_cfg.nspare < T18_SPARE_MAX ? g_cfg.nspare : T18_SPARE_MAX;
  for (uint32_t k = 0; k < n * 16u && k < 254u; k++) {
    if (g_spare_used[k >> 4]) continue;
    int fr = 1;
    for (uint32_t r = 0; r < S_ROWS && fr; r++) { int on = 0; for (uint32_t q = 0; q < 128u; q++) if (S_IDX[q] == r + 1u) { on = 1; break; }
      if (on) for (uint32_t j = 0; j < 16u; j++) if (s_row(r)[j] == k + 1u) { fr = 0; break; } }
    if (!fr) continue;
    s_row(i - 1u)[(sec >> 12) & 15u] = (uint8_t)(k + 1u);
    return ((uint32_t)g_cfg.spare[k >> 4] << 16) | ((k & 15u) << 12);
  }
  return 0;
}
#endif
#ifdef T18_FF_FAST
extern uint8_t t18_fff[128];
#ifdef T18_SEC_SPARE
#define FFF_SET(b) (t18_fff[b] = (uint8_t)(g_map[b] == 0xFFFFu && !S_IDX[b] && c_sec >> 16 != (b)))
#else
#define FFF_SET(b) (t18_fff[b] = (uint8_t)(g_map[b] == 0xFFFFu && c_sec >> 16 != (b)))
#endif
#else
#define FFF_SET(b) ((void)0)
#endif
static void fblk_set(uint32_t b)
{
  FFF_SET(b);
  if (c_sec >> 16 == b) { t18_fblk[b] = 0; return; }   /* reads through t18_flash_rd */
  t18_fblk[b] = blk_xip(b);
}
COLD static void map_save(uint8_t *s)                 /* s: 4 KB to build the sector in */
{
  memset(s, 0xFF, 0x1000);
  uint32_t m = 0x504D3854u; memcpy(s, &m, 4);
  memcpy(s + 4, g_map, sizeof g_map); memcpy(s + 4 + sizeof g_map, g_spare_used, sizeof g_spare_used);
#ifdef T18_SEC_SPARE
  { uint32_t sm = S_MAGIC; memcpy(s + S_OFF, &sm, 4); memcpy(s + S_OFF + 4, S_IDX, 128);
    for (uint32_t r = 0; r < S_ROWS; r++) memcpy(s + S_OFF + 132u + 16u * r, s_row(r), 16); }
#endif
  hw_tone(0);
  flash_put(SAVE + T18_MAP_SEC, s);
  g_t18st[3]++;
}
COLD static void map_load(void)
{
  const volatile uint8_t *p = XIP_PTR(SAVE + T18_MAP_SEC);
  uint32_t m = (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
  if (m == 0x504D3854u) {
    for (uint32_t i = 0; i < sizeof g_map; i++) ((uint8_t *)g_map)[i] = p[4 + i];
    for (uint32_t i = 0; i < sizeof g_spare_used; i++) g_spare_used[i] = p[4 + sizeof g_map + i];
#ifdef T18_SEC_SPARE
    memset(S_IDX, 0, 128); for (uint32_t r = 0; r < S_ROWS; r++) memset(s_row(r), 0, 16);
    if (p[S_OFF] == 0x54u && p[S_OFF + 1] == 0x38u && p[S_OFF + 2] == 0x53u && p[S_OFF + 3] == 0x53u) {
      for (uint32_t i = 0; i < 128u; i++) { uint8_t v = p[S_OFF + 4 + i]; S_IDX[i] = v <= S_ROWS ? v : 0; }
      for (uint32_t r = 0; r < S_ROWS; r++) for (uint32_t j = 0; j < 16u; j++) s_row(r)[j] = p[S_OFF + 132u + 16u * r + j];
    }
#endif
  } else {
#ifdef T18_SEC_SPARE
    memset(S_IDX, 0, 128); for (uint32_t r = 0; r < S_ROWS; r++) memset(s_row(r), 0, 16);
#endif
    for (int i = 0; i < 128; i++) g_map[i] = g_cfg.map[i];
    memset(g_spare_used, 0, sizeof g_spare_used);
  }
  for (uint32_t b = 0; b < 128; b++) g_wblk[b] = g_map[b] != g_cfg.map[b];
#ifndef T18_FLASH_UNCACHED
  /* 1.6.5: the cached window must show the same as the flash; where it does not (the window may not
   * reach that far on the device), the block is read uncached */
  g_ncached = 0;
  for (uint32_t b = 0; b < 128; b++) {
    if (g_map[b] == 0xFFFFu || g_wblk[b]) continue;
    uint32_t phys = (uint32_t)g_map[b] << 16, ok = (uint32_t)in_window(phys);
    static const uint16_t at[4] = { 0x0000u, 0x4000u, 0x8000u, 0xFFF0u };
    for (int k = 0; k < 4 && ok; k++)
      for (uint32_t i = 0; i < 16u && ok; i += 4u)
        if (*(const volatile uint32_t *)(0x10000000u + phys - g_wbase + at[k] + i) != *(const volatile uint32_t *)(0x60000000u + phys + at[k] + i)) ok = 0;
    if (!ok) g_wblk[b] = 1; else g_ncached++;
  }
#endif
  for (uint32_t b = 0; b < 128; b++) { t18_fblk[b] = blk_xip(b); FFF_SET(b); }
}

#ifdef T18_XIP_PROBE
/* Test build: where does the cached window (0x10000000) reach on the device, and does a new base work?
 * Runs from the SRAM with interrupts off; a read that faults returns 0xDEADBEEF. Screen E7. */
#endif
#ifdef T18_PROBE_FAULT
__attribute__((section(".ramfunc"), naked, noinline)) static uint32_t probe_rd(uint32_t a)
{
  __asm__ volatile(".global probe_ldr\n .thumb_func\n probe_ldr: ldr r0, [r0]\n dsb\n bx lr\n");
}
__attribute__((section(".ramfunc"), naked, noinline)) static void probe_wr(uint32_t a, uint32_t v)
{
  __asm__ volatile(".global probe_str\n .thumb_func\n probe_str: str r1, [r0]\n dsb\n bx lr\n");
}
__attribute__((section(".ramfunc"), noinline)) static uint32_t prd(uint32_t a, int *f)
{
  g_probe = 1u; uint32_t v = probe_rd(a); __asm__ volatile("dsb\n isb" ::: "memory");
  if (g_probe == 2u) *f = 1; g_probe = 0; return v;
}
__attribute__((section(".ramfunc"), noinline)) static void pwr(uint32_t a, uint32_t v, int *f)
{
  g_probe = 1u; probe_wr(a, v); __asm__ volatile("dsb\n isb" ::: "memory");
  if (g_probe == 2u) *f = 1; g_probe = 0;
}
#endif
#ifdef T18_RAM_PROBE
/* Test build: is there more RAM than known (PRAM over 64 KB, SRAM over 128 KB, RAM at 0x20000000 over
 * 4 KB)? Per address: F read faults, E write faults, 0 not writable, 2 writes show at the alias
 * (same memory), 1 RAM of its own. From the SRAM with interrupts off (PRAM writes). Screen E9. */
__attribute__((section(".ramfunc"), noinline)) static uint32_t ram_probe1(uint32_t a, uint32_t alias)
{
  int f = 0;
  uint32_t a0 = prd(alias, &f), o = prd(a, &f);
  if (f) return 15u;
  pwr(a, 0x5AA5C33Cu, &f);
  if (f) return 14u;
  uint32_t v = prd(a, &f), a1 = prd(alias, &f);
  pwr(a, o, &f);
  if (a1 != a0) pwr(alias, a0, &f);
  return v != 0x5AA5C33Cu ? 0u : a1 != a0 ? 2u : 1u;
}
static const uint32_t g_rp[12][2] = {
  { 0x0001F800u, 0xF800u }, { 0x0002F800u, 0xF800u }, { 0x0003F800u, 0xF800u }, { 0x0007F800u, 0xF800u },
  { 0x18020100u, 0x18000100u }, { 0x18030100u, 0x18000100u }, { 0x18040100u, 0x18000100u }, { 0x1807F100u, 0x18000100u },
  { 0x20001900u, 0x20000900u }, { 0x20002900u, 0x20000900u }, { 0x20004900u, 0x20000900u }, { 0x20008900u, 0x20000900u } };
COLD static void ram_probe(void)
{
  uint32_t v[3] = { 0, 0, 0 };
  for (int i = 0; i < 12; i++) v[i >> 2] |= ram_probe1(g_rp[i][0], g_rp[i][1]) << (28 - 4 * (i & 3));
  rows_draw(9, v, 3);
  for (u32 up = 0; ; ) { hw_wdt_feed(); if (!(hw_buttons() & BTN_WHEEL)) { if (up == 2) break; up = 1; } else if (up) up = 2; }
}
#endif
#ifdef T18_XIP_PROBE
/* 16 bytes at window offset w against the flash at fl: 1 same, 0 not, F fault, E both all FF */
__attribute__((section(".ramfunc"), noinline)) static uint32_t pcmp(uint32_t w, uint32_t fl)
{
  int f = 0, same = 1, ff = 1;
  for (uint32_t i = 0; i < 16u; i += 4u) {
    uint32_t a = prd(0x10000000u + w + i, &f), b = *(const volatile uint32_t *)(0x60000000u + fl + i);
    if (f) return 15u;
    if (a != b) same = 0;
    if (a != 0xFFFFFFFFu || b != 0xFFFFFFFFu) ff = 0;
  }
  return ff ? 14u : (uint32_t)same;
}
__attribute__((section(".ramfunc"), noinline)) static void xip_flush(uint32_t at)
{
  volatile uint32_t sum = 0; int f = 0;
  for (uint32_t i = 0; i < 0x40000u; i += 4u) { sum += prd(0x10000000u + at + 4u * i, &f); if (f) break; }
  (void)sum;
}
COLD static void xip_probe_a(uint32_t *v, uint32_t gmin, uint32_t gmax)
{
  for (int i = 0; i < 16; i++) {           /* window offset i MB + 0x8000 against flash 0x11000 + that */
    uint32_t w = (uint32_t)i * 0x100000u + 0x8000u;
    v[i >> 3] |= pcmp(w, 0x11000u + w) << (28 - 4 * (i & 7));
  }
  v[2] = gmin; v[3] = gmax;
  int f = 0;
  for (int i = 0; i < 8; i++) { f = 0; v[8 + i] = prd(0x4002F000u + 4u * (uint32_t)i, &f); if (f) v[8 + i] = 0xDEADBEEFu; }
}
__attribute__((section(".ramfunc"), noinline)) static void xip_probe_b(uint32_t *v, uint32_t gmin)
{
  /* new base at the lowest firmware block: count blocks the window shows right */
  uint32_t ob = XIP_BASE_REG;
  XIP_BASE_REG = 0x60000000u + gmin; XIP_CTRL_REG = 3u; __asm__ volatile("dsb\n isb" ::: "memory");
  v[7] = XIP_BASE_REG;
  xip_flush(0x100000u);
  uint32_t ok = 0, n = 0, fl = 0;
  for (uint32_t b = 0; b < 128; b++) {
    if (g_map[b] == 0xFFFFu) continue;
    uint32_t phys = (uint32_t)g_map[b] << 16; if (phys < gmin) continue;
    n++;
    uint32_t r = 1;
    static const uint16_t at[4] = { 0x0000u, 0x4000u, 0x8000u, 0xFFF0u };
    for (int k = 0; k < 4 && r == 1u; k++) { uint32_t c = pcmp(phys - gmin + at[k], phys + at[k]); if (c == 15u) { fl++; r = 0; } else if (c == 0u) r = 0; }
    ok += r;
  }
  v[4] = ok << 16 | n; v[5] = fl;
  { int f2 = 0; v[6] = prd(0x10000000u + 0x8000u, &f2); }
  XIP_BASE_REG = ob; XIP_CTRL_REG = 3u; __asm__ volatile("dsb\n isb" ::: "memory");
  xip_flush(0x100000u);
}
#endif
COLD static void c_write_back(void)
{
  if (!c_dirty) return;
  uint32_t sec = c_sec, b = sec >> 16;
  int fresh = 0;
  if (g_map[b] == 0xFFFFu) {
#ifdef T18_SEC_SPARE
    uint32_t sp = s_phys(sec);
    if (!sp) {
      int ff = 1; for (int k = 0; k < 0x1000 && ff; k++) if (c_buf[k] != 0xFF) ff = 0;
      if (ff) { c_dirty = 0; return; }
      sp = s_alloc(sec);
      if (!sp) { g_t18st[5]++; s_lost++; c_dirty = 0; return; }   /* no room: the write is lost */
      hw_tone(0); flash_put(sp, c_buf); c_dirty = 0; g_t18st[2]++;
      map_save(c_buf); flash_read(sp, c_buf, 0x1000);              /* the buffer is the scratch for the map sector */
      return;
    }
    hw_tone(0); flash_put(sp, c_buf); c_dirty = 0; g_t18st[2]++;
    return;
#else
    int ff = 1; for (int k = 0; k < 0x1000 && ff; k++) if (c_buf[k] != 0xFF) ff = 0;
    if (ff) { c_dirty = 0; return; }
    int k = 0; while (k < (int)g_cfg.nspare && k < T18_SPARE_MAX && g_spare_used[k]) k++;
    if (k >= (int)g_cfg.nspare || k >= T18_SPARE_MAX) { g_t18st[5]++; c_dirty = 0; return; }   /* no room: the write is lost */
    g_spare_used[k] = 1; g_map[b] = g_cfg.spare[k]; fresh = 1;
#endif
  }
  uint32_t phys = ((uint32_t)g_map[b] << 16) | (sec & 0xFFFFu);
  g_wblk[b] = 1;                                  /* from now on uncached (fblk_set when the buffer goes) */
  hw_tone(0);
  flash_put(phys, c_buf);
  c_dirty = 0; g_t18st[2]++;
  if (fresh) { map_save(c_buf); flash_read(phys, c_buf, 0x1000); }   /* the buffer is the scratch for the map sector */
}
COLD static void c_get(uint32_t sec)
{
  if (c_sec == sec) return;
  c_write_back();
  uint32_t old = c_sec;
  c_sec = sec; c_dirty = 0;
  const uint8_t *x = blk_xip(sec >> 16);
  if (x) flash_read(((uint32_t)g_map[sec >> 16] << 16) | (sec & 0xFFFFu), c_buf, 0x1000);
#ifdef T18_SEC_SPARE
  else if (s_phys(sec)) flash_read(s_phys(sec), c_buf, 0x1000);
#endif
  else memset(c_buf, 0xFF, 0x1000);
  if (old != 0xFFFFFFFFu) fblk_set(old >> 16);
  fblk_set(sec >> 16);
}
/* the buffer is needed for something else: write it back and let it go */
COLD static void c_release(void)
{
  c_write_back();
  uint32_t old = c_sec; c_sec = 0xFFFFFFFFu;
  if (old != 0xFFFFFFFFu) fblk_set(old >> 16);
}

uint8_t t18_flash_rd(uint32_t a)
{
  a &= 0x7FFFFFu;
  if (c_sec == (a & ~0xFFFu)) return c_buf[a & 0xFFFu];
  const uint8_t *x = blk_xip(a >> 16);
#ifdef T18_SEC_SPARE
  if (!x && S_IDX[a >> 16]) { uint32_t sp = s_phys(a); return sp ? *(const volatile uint8_t *)(0x60000000u + sp + (a & 0xFFFu)) : 0xFF; }
#endif
  return x ? ((const volatile uint8_t *)x)[a & 0xFFFFu] : 0xFF;
}
#ifdef T18_FLUSH_IDLE
static uint32_t g_cw_ms;                /* last guest flash write (test10: written back 3 s later) */
#define CW_NOTE() (g_cw_ms = g_ms)
#else
#define CW_NOTE() ((void)0)
#endif
COLD void t18_flash_prog(uint32_t a, uint8_t v)
{
  a &= 0x7FFFFFu; CW_NOTE();
  c_get(a & ~0xFFFu);
  uint8_t o = c_buf[a & 0xFFFu], n = o & v;
  if (n != o) { c_buf[a & 0xFFFu] = n; c_dirty = 1; }
}
COLD void t18_flash_erase(uint32_t a, uint32_t size)
{
  a &= 0x7FFFFFu; CW_NOTE();
  for (uint32_t s = a & ~0xFFFu; s < a + size; s += 0x1000u) {
    if (c_sec == s) { memset(c_buf, 0xFF, 0x1000); c_dirty = 1; continue; }
#ifdef T18_SEC_SPARE
    if (g_map[s >> 16] == 0xFFFFu) { uint32_t sp = s_phys(s); if (sp && !flash_isff(sp, 0x1000u)) { hw_tone(0); flash_erase4k(sp); g_t18st[2]++; } continue; }
#endif
    if (g_map[s >> 16] == 0xFFFFu) continue;
    uint32_t phys = ((uint32_t)g_map[s >> 16] << 16) | (s & 0xFFFFu);
    if (!flash_isff(phys, 0x1000u)) { hw_tone(0); flash_erase4k(phys); g_t18st[2]++; if (!g_wblk[s >> 16]) { g_wblk[s >> 16] = 1; fblk_set(s >> 16); } }
  }
}

/* ---- screen ---- */
static volatile uint32_t g_pres_src, g_pres_len, g_pres_n;
static void present(int force);
static int g_lcd_on;
/* 1.6.4: the picture goes to the panel right when the firmware has finished it (before it starts the
 * next one), else it was read half drawn: built up from the top, black behind texts.
 * Switch T18_PRESENT_LATE: in the main loop as before. */
static void on_present(void *u, uint32_t src, uint32_t len)
{
  (void)u; g_pres_src = src; g_pres_len = len; g_pres_n++;
#if !defined(T18_PRESENT_LATE) && !defined(T18_PRESENT_IDLE)
  if (g_lcd_on) present(0);
#endif
}
/* one picture row (128 pixels) from guest SRAM; a row that touches the hole is copied */
static const uint16_t *guest_row(uint32_t a)
{
  static uint16_t tmp[128];
  uint32_t o = a - 0xF8000000u;
  if (o >= T18_SRAM_SIZE || T18_SRAM_SIZE - o < 256u) return 0;
  if (!t18_hole_n(o, 256u)) return (const uint16_t *)(t18_sram + o);
  uint8_t *d = (uint8_t *)tmp;
  for (uint32_t i = 0; i < 256u; i++) d[i] = *t18_sram_rp(o + i);
  return tmp;
}
static uint32_t g_speed = 100;
/* one line of the overlay at y0: glyph indices (DIG) */
COLD static void ovl_line(int y0, const char *s, int n)
{
  static uint16_t row[8 * 8];
  int w = n * 8;
  hw_px_begin(0, (u32)y0, (u32)w, (u32)y0 + 12u);
  for (int y = 0; y < 12; y++) {
    for (int x = 0; x < w; x++) {
      int c = x / 8, cx = (x % 8) / 2, cy = (y - 1) / 2, on = 0;
      if (y >= 1 && y < 11 && cx < 3) on = (DIG[(int)s[c]] >> (14 - (cy * 3 + cx))) & 1;
      row[x] = on ? 0xFFFF : 0x0000;
    }
    hw_px_row(row, (u32)w);
  }
  hw_px_end();
}
/* speed in % and below "C" with the number of firmware blocks read through the cache (1.6.5) */
COLD static void draw_overlay(void)
{
  char s[8]; uint32_t v = g_speed > 999 ? 999 : g_speed; int n = 0;
  if (v >= 100) s[n++] = (char)(v / 100);
  if (v >= 10) s[n++] = (char)(v / 10 % 10);
  s[n++] = (char)(v % 10); s[n++] = 10;
  ovl_line(0, s, n);
  n = 0; v = g_ncached > 999 ? 999 : g_ncached; s[n++] = 13 /* C */;
  if (v >= 100) s[n++] = (char)(v / 100);
  if (v >= 10) s[n++] = (char)(v / 10 % 10);
  s[n++] = (char)(v % 10);
  ovl_line(12, s, n);
}

#ifdef T18_PROF_OVL
/* every 5 s the counts are taken over (the screen shows the last 5 s) */
static uint32_t g_pv[16], g_pt, g_js0[8];
static uint32_t bcd(uint32_t v) { uint32_t r = 0; for (int k = 0; k < 8; k++) { r |= (v % 10u) << (4 * k); v /= 10u; } return r; }
COLD static void prof_tick(void)
{
  if (++g_pt < 5u) return;
  g_pt = 0;
  uint32_t n = g_pn ? g_pn : 1u, jc = 0, xw = 0;
#ifndef T18_PROF_LITE
  extern uint16_t __jc_start[], __jc_end[];
  uint32_t j0 = (uint32_t)__jc_start >> 6, j1 = (uint32_t)__jc_end >> 6;
  for (uint32_t i = j0; i < j1; i++) jc += g_pb[i];
  for (uint32_t i = 1024u; i < 1088u; i++) xw += g_pb[i];
#endif
  uint32_t *v = g_pv;
  v[0] = bcd(g_speed); v[1] = bcd(jc * 100u / n); v[2] = bcd(xw * 100u / n);
  v[3] = bcd((t18_jit_stat[0] - g_js0[0]) / 5u); v[4] = bcd((t18_jit_stat[3] - g_js0[3]) / 5u);
  v[5] = bcd((t18_jit_stat[4] - g_js0[4]) / 5u);
  for (int k = 0; k < 8; k++) g_js0[k] = t18_jit_stat[k];
#ifndef T18_PROF_LITE
  for (int t = 0; t < 5; t++) {          /* the 5 biggest buckets (the code buffer counts as one): address and % */
    uint32_t m = 0, mv = 0;
    for (uint32_t i = 0; i < PB_N; i++) { uint32_t c = (i >= j0 && i < j1) ? 0u : g_pb[i]; if (c > mv) { mv = c; m = i; } }
    v[6 + 2 * t] = m < 1024u ? m << 6 : m < 1088u ? 0x100DF000u + ((m - 1024u) << 10) : m == 1088u ? 0x18000000u : 0xFFFFFFFFu;
    v[7 + 2 * t] = bcd(mv * 1000u / n);   /* per mille */
    g_pb[m] = 0;
  }
#else
  { extern uint32_t t18_jit_fr[4], t18_interp[2]; extern uint32_t t18_jit_used(void); static uint32_t i0; uint32_t in = t18_interp[0] + t18_interp[1];
    v[6] = bcd((in - i0) / 5000u); i0 = in; v[7] = bcd(t18_jit_fr[0]); v[8] = bcd(t18_jit_fr[1]); v[9] = bcd(t18_jit_used() / 1024u); v[10] = bcd(g_pn);
#ifdef T18_PROF_COMPACT
    { extern uint32_t t18_jit_compact(void); extern int t18_new_off; v[10] = t18_jit_compact() | (t18_new_off ? 0x10u : 0u); }   /* test3: 1 = compact loads/stores; test9: 1x = C held */
#endif
    }
#ifdef T18_PROF_PC
  /* test13: the 3 biggest PC buckets apart from the code buffer (start address, per mille); v15 = code buffer */
  { volatile uint32_t *h = (volatile uint32_t *)T18_PROF_PC; static const uint8_t at[6] = { 1, 2, 11, 12, 13, 14 };
#ifdef T18_PROF_PC_LO
    v[15] = bcd(h[48] * 1000u / n);
    for (int t = 0; t < 3; t++) {
      int m = -1; uint32_t mv = 0;
      for (int i = 0; i < 51; i++) if (i != 48 && h[i] > mv) { mv = h[i]; m = i; }
      v[at[2 * t]] = m < 0 ? 0 : m < 48 ? T18_PROF_PC_LO + ((uint32_t)m << T18_PROF_PC_SH) : m == 49 ? 0x0000FFFFu : 0xFFFFFFFFu;
#else
    v[15] = bcd(h[0] * 1000u / n);
    for (int t = 0; t < 3; t++) {
      int m = -1; uint32_t mv = 0;
      for (int i = 1; i < 51; i++) if (h[i] > mv) { mv = h[i]; m = i; }
      v[at[2 * t]] = m < 0 ? 0 : m <= 32 ? (uint32_t)(m - 1) << 11 : m <= 48 ? 0x100DF000u + ((uint32_t)(m - 33) << 12) : m == 49 ? 0x18000000u : 0xFFFFFFFFu;
#endif
      v[at[2 * t + 1]] = bcd(mv * 1000u / n);
      if (m >= 0) h[m] = 0;
    }
    for (int i = 0; i < 51; i++) h[i] = 0;
#ifdef T18_PROF_GPC
    { volatile uint32_t *gp = (volatile uint32_t *)0x20000FD0u; int a = -1, b = -1;
      for (int k = 0; k < 6; k++) { if (a < 0 || gp[2 * k + 1] > gp[2 * a + 1]) { b = a; a = k; } else if (b < 0 || gp[2 * k + 1] > gp[2 * b + 1]) b = k; }
      v[7] = gp[2 * a] & ~1u; v[8] = bcd(gp[2 * a + 1] * 1000u / n); v[10] = gp[2 * b] & ~1u;
      for (int k = 0; k < 12; k++) gp[k] = 0; }
#endif
  }
#endif
#if defined(T18_SLOW_HIST) && defined(T18_SLOW_HIST_AT)
  /* test11: the 3 most used 256-byte pages of the slow path (address, bit 0 = load) and their count per s */
  { uint32_t *h = (uint32_t *)T18_SLOW_HIST_AT; static const uint8_t at[6] = { 1, 2, 11, 12, 13, 14 };
    for (int t = 0; t < 3; t++) {
      int m = -1; uint32_t mv = 0;
      for (int i = 0; i < 30; i++) if (h[2 * i] && h[2 * i + 1] > mv) { mv = h[2 * i + 1]; m = i; }
      v[at[2 * t]] = m < 0 ? 0 : h[2 * m]; v[at[2 * t + 1]] = bcd(mv / 5u);
      if (m >= 0) h[2 * m + 1] = 0;
    }
    uint32_t used = 0; for (int i = 0; i < 30; i++) if (h[2 * i]) used++;
    v[15] = bcd(used);
    for (int i = 0; i < 60; i++) h[i] = 0; }
#endif
#endif
  __asm__ volatile("cpsid i");
#ifndef T18_PROF_LITE
  for (uint32_t i = 0; i < PB_N; i++) g_pb[i] = 0;
#endif
  g_pn = g_ppres = g_pwait = 0;
  __asm__ volatile("cpsie i");
}
COLD static void prof_show(void) { rows_draw(8, g_pv, 16); }
#endif
/* send the picture: only rows that changed since the last time (the SPI to the panel is slow) */
#ifdef T18_ROWSUM_RAM20
#define g_row_sum ((uint32_t *)0x20000D00u)   /* test builds: the Paradise's RAM (no room in the hole) */
#else
static uint32_t g_row_sum[128];
#endif
static int g_ovl;
static void present(int force)
{
  if (g_stage < 8) g_stage = 8;
#ifdef T18_PROF_OVL
  if (g_ovl == 2) return;
  g_pflag |= 1u;
#endif
  uint32_t src = g_pres_src, len = g_pres_len;
  if (!len) return;
  uint32_t rows = len / 256u; if (rows > 128u) rows = 128u;
  int y0 = -1;
  for (uint32_t y = 0; y <= rows; y++) {
    int changed = 0;
    if (y < rows) {
      const uint16_t *p = guest_row(src + y * 256u);
      if (!p) return;
      uint32_t sum = 0x9E3779B9u;
      for (int i = 0; i < 128; i += 2) sum = (sum << 5 | sum >> 27) ^ ((uint32_t)p[i] | (uint32_t)p[i + 1] << 16);
      changed = force || sum != g_row_sum[y];
      g_row_sum[y] = sum;
    }
    if (changed && y0 < 0) y0 = (int)y;
    if (!changed && y0 >= 0) {
      hw_px_begin(0, (u32)y0, 128, y);
      for (uint32_t k = (uint32_t)y0; k < y; k++) hw_px_row(guest_row(src + k * 256u), 128);
      hw_px_end();
      y0 = -1;
    }
  }
  if (g_ovl == 1) draw_overlay();
#ifdef T18_PROF_OVL
  g_pflag &= ~1u;
#endif
}
#ifdef T18_PRESENT_STABLE
/* a check value of the firmware's picture (every 3rd pixel) */
static uint32_t frame_hash(void)
{
  uint32_t src = g_pres_src, len = g_pres_len, rows = len / 256u, h = 0x811C9DC5u;
  if (rows > 128u) rows = 128u;
  for (uint32_t y = 0; y < rows; y++) {
    const uint16_t *p = guest_row(src + y * 256u);
    if (!p) return 0;
    for (int i = 0; i < 128; i += 3) h = (h ^ p[i]) * 16777619u;
  }
  return h;
}
#endif
/* ---- clock note and session ---- */
typedef struct { uint32_t magic, rtc, tm[6], flags; } Note;
COLD static int note_load(Note *n)
{
  const volatile uint8_t *p = XIP_PTR(SAVE + T18_NOTE_SEC);
  for (uint32_t i = 0; i < sizeof *n; i++) ((uint8_t *)n)[i] = p[i];
  return n->magic == 0x544E3854u;
}
COLD static void note_save(void)
{
  Note n; n.magic = 0x544E3854u; n.rtc = rtc_now(); n.flags = 0;
  int tm[6]; tg18_get_time(T, tm); for (int i = 0; i < 6; i++) n.tm[i] = (uint32_t)tm[i];
  memset(c_buf, 0xFF, 0x1000); memcpy(c_buf, &n, sizeof n);
  hw_tone(0); flash_put(SAVE + T18_NOTE_SEC, c_buf);
}
COLD static void set_time_secs(int64_t s)
{
  int64_t days = s / 86400, rem = s % 86400, z = days + 719468, era = (z >= 0 ? z : z - 146096) / 146097;
  int64_t doe = z - era * 146097, yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365, y = yoe + era * 400;
  int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100), mp = (5 * doy + 2) / 153, d = doy - (153 * mp + 2) / 5 + 1, m = mp + (mp < 10 ? 3 : -9);
  tg18_set_time(T, (int)(y + (m <= 2)), (int)m, (int)d, (int)(rem / 3600), (int)(rem / 60 % 60), (int)(rem % 60));
}
#ifdef T18_CLOCK_AFTER
/* 1.6.8: a new game asks for date and time first and then loads the battery RTC with them, over the Paradise's time
 * set at the start. With "clock from the Paradise" on, the Paradise's time is put back right then (the game keeps
 * its date through the day counter, which tg18_set_time moves along). */
static uint32_t g_clk_pend;              /* g_ms + 500 when the Paradise's time is due again (0 = not) */
static void clock_after_set(void *t)
{
  (void)t;
  /* half a second later: right after the RTC the firmware sets its own day counter from the date it was given */
  if ((g_cfg.flags & T18F_CLOCK) && g_para) g_clk_pend = (g_ms + 500u) | 1u;
}
static void clock_after_poll(void)
{
  if (g_clk_pend && (int32_t)(g_ms - g_clk_pend) >= 0) { g_clk_pend = 0; set_time_secs((int64_t)g_para + (int64_t)(rtc_now() - g_para_cnt)); }
}
#endif
#define SESS_MAGIC 0x53533854u
COLD static uint32_t sess_sum(void)
{
  uint32_t s = 0x811C9DC5u;
  for (int i = 0; i < 128; i++) s = (s ^ g_cfg.map[i]) * 16777619u;
  return s;
}
/* the guest SRAM sector by sector (the hole part as the guest sees it), the state header last */
COLD static void session_save(void)
{
  uint32_t n = tg18_state_size(T);
  if (n + 16 > 0x1000u) return;
  c_release();
  hw_tone(0);
  for (uint32_t o = 0; o < T18_SRAM_SIZE; o += 0x1000u) {
    hw_wdt_feed();
    const uint8_t *src = t18_sram + o;
    if (t18_hole_n(o, 0x1000u)) { for (uint32_t i = 0; i < 0x1000u; i++) c_buf[i] = *t18_sram_rp(o + i); src = c_buf; }
    flash_put(SAVE + T18_SESS_SEC + 0x1000u + o, src);
  }
  uint32_t *h = (uint32_t *)c_buf;
  memset(c_buf, 0xFF, 0x1000);
  tg18_save_state(T, c_buf + 16, 0x1000u - 16);
  h[0] = SESS_MAGIC; h[1] = n; h[2] = sess_sum(); h[3] = rtc_now();
  flash_put(SAVE + T18_SESS_SEC, c_buf);   /* header last: a half written session stays invalid */
}
#ifdef SESS_AUTO
#ifndef SESS_T1
#define SESS_T1 600000u    /* screen dark: 10 minutes after the last time at the earliest */
#define SESS_T2 1800000u   /* otherwise every 30 minutes */
#endif
static uint32_t g_sess_ms, g_sess_key;
#endif
#ifdef HW_OPTS
static const uint16_t OPT_SM[8] = { 0, 10, 20, 30, 60, 120, 300, 600 };   /* screen off after, seconds */
#ifdef T18_TONE_Q
/* 1.6.8 (test43): the tone in real time. The game plays its melodies in game time; below 100 % (a call is
 * busy) they dragged. Tone changes are queued with the game time and played on the real clock, 300 ms after
 * the first one of a sequence: up to that much falling behind the melody keeps its tempo. Late by more than
 * 200 ms anyway: the timing starts anew from there (no notes squeezed together). */
#define TQ_N 32u   /* a power of two; 32 changes are more than 300 ms of any melody */
#define TQ_LAT 300u
static uint32_t tq_t[TQ_N]; static uint16_t tq_hz[TQ_N]; static uint32_t tq_h, tq_w, g_gms, tq_g0, tq_r0, tq_on, tq_cur, tq_quiet;
static void tq_put(uint32_t hz) { if (tq_w - tq_h >= TQ_N) tq_h++; tq_t[tq_w % TQ_N] = g_gms; tq_hz[tq_w % TQ_N] = (uint16_t)(hz > 0xFFFFu ? 0xFFFFu : hz); tq_w++; }
static void tq_pump(void)
{
  uint32_t now = g_ms;
  if (tq_h == tq_w) { if (tq_on && !tq_cur && now - tq_quiet > 1000u) tq_on = 0; return; }
  if (!tq_on) { tq_on = 1; tq_g0 = tq_t[tq_h % TQ_N]; tq_r0 = now + TQ_LAT; }
  while (tq_h != tq_w) {
    uint32_t i = tq_h % TQ_N, due = tq_r0 + (tq_t[i] - tq_g0);
    int32_t late = (int32_t)(now - due);
    if (late < 0) break;
    if (late > 200) { tq_g0 = tq_t[i]; tq_r0 = now; }
    tq_cur = tq_hz[i]; hw_tone(tq_cur); if (!tq_cur) tq_quiet = now; tq_h++;
  }
}
#endif
COLD static void opt_apply(uint32_t f) { g_hw_vol = (0x0312u >> (((f >> 16) & 3u) * 4u)) & 15u; g_hw_dim = (f >> 18) & 3u; g_scr_ms = OPT_SM[(f >> 20) & 7u] * 1000u;
#ifdef BL_PWM
  hw_bl_set(opt_light_pct(f));   /* 1.6.8: the light, not the colours */
#endif
}
#ifdef OPT_MENU
/* 1.6.8: settings changed on the device (wheel short), in the free sector behind the session: magic, the Studio's
 * bits when they were changed, the bits, check. The Studio's bits win again once they are changed there. */
#define OPT_SEC (SAVE + 0x23000u)
#define OPT_MAGIC 0x4F584D50u   /* "PMXO" */
static uint32_t g_opt;
COLD static uint32_t opt_studio(void) { return g_cfg.flags & 0xF7F0000u; }
COLD static void opt_load(void)
{
  const volatile uint32_t *s = (const volatile uint32_t *)XIP_PTR(OPT_SEC);
  uint32_t a = s[0], b = s[1], c = s[2], d = s[3];
  g_opt = (a == OPT_MAGIC && b == opt_studio() && d == (a ^ b ^ c) && !(c & ~0xF7F0000u)) ? c : opt_studio();
}
COLD static void opt_store(void)
{
  c_release(); hw_tone(0);
  uint32_t *h = (uint32_t *)c_buf; memset(c_buf, 0xFF, 0x1000);
  h[0] = OPT_MAGIC; h[1] = opt_studio(); h[2] = g_opt; h[3] = h[0] ^ h[1] ^ h[2];
  flash_put(OPT_SEC, c_buf);
}
#endif
#endif
COLD static void session_drop(void) { if (!flash_isff(SAVE + T18_SESS_SEC, 16)) flash_erase4k(SAVE + T18_SESS_SEC); }
COLD static int session_load(uint32_t *saved_rtc)
{
  const volatile uint8_t *p = XIP_PTR(SAVE + T18_SESS_SEC);
  uint32_t h[4];
  for (uint32_t i = 0; i < 16u; i++) ((uint8_t *)h)[i] = p[i];
  if (h[0] != SESS_MAGIC || h[1] != tg18_state_size(T) || h[2] != sess_sum()) return 0;
  for (uint32_t o = 0; o < T18_SRAM_SIZE; o += 0x1000u) {
    uint32_t at = SAVE + T18_SESS_SEC + 0x1000u + o;
    if (!t18_hole_n(o, 0x1000u)) { flash_read(at, t18_sram + o, 0x1000u); continue; }
    const volatile uint8_t *q = XIP_PTR(at);
    for (uint32_t i = 0; i < 0x1000u; i++) t18_sram_wr(o + i, q[i], 1);
  }
  c_release();
  for (uint32_t i = 0; i < 0x1000u; i++) c_buf[i] = p[i];
  if (tg18_load_state(T, c_buf + 16, h[1])) return 0;
  *saved_rtc = h[3];
  return 1;
}

COLD static void leave(int save)
{
  hw_tone(0);
  REG(0x20000FF8u) = 0;                  /* start counter of the home hooks */
  REG(0x20000FFCu) = 0x4C584D50u;        /* "PMXL": back on purpose, the autostart stays off */
  c_release();
  if (save) { note_save(); session_save(); }
#ifdef PARA_BACK
  para_back_fix(c_buf);                  /* 1.6.8: the Paradise skips the time spent here */
#endif
  hw_bl_pwm_off();
  for (u32 t = 0; hw_buttons() && t < 20000000u; t++) if ((t & 0xFFFFu) == 0) hw_wdt_feed();
  XIP_BASE_REG = 0x60011000u; XIP_CTRL_REG = 3u;
  hw_back_to_paradise();
}

/* Start: memory, firmware map, the SoC, the session; the clock. Returns 1 when a session was resumed. */
COLD static int app_start(void)
{
  t18_sram = (uint8_t *)0x18000000u; t18_hole_new = hole_new; t18_hole_free = hole_free;
  map_load();
#ifdef T18_XIP_PROBE
  { uint32_t v[16] = { 0 }, gmin = 0xFFFFFFFFu, gmax = 0;
    for (int b = 0; b < 128; b++) if (g_map[b] != 0xFFFFu) { uint32_t p = (uint32_t)g_map[b] << 16; if (p < gmin) gmin = p; if (p > gmax) gmax = p; }
    xip_probe_a(v, gmin, gmax); xip_probe_b(v, gmin);
    rows_draw(7, v, 16);
    for (u32 t = 0, up = 0; ; t++) { hw_wdt_feed(); if (!(hw_buttons() & BTN_WHEEL)) { if (up == 2) break; up = 1; } else if (up) up = 2; }
    for (u32 t = 0; t < 2000000u; t++) if ((t & 0xFFFFu) == 0) hw_wdt_feed(); }
#endif
  g_stage = 3;
#ifdef T18_TEST_KEYS
  /* test builds only (1.6.8): a key held from the start of the Paradise menu would switch these on by mistake */
  t18_jit_off = (hw_buttons() & BTN_B) != 0;          /* B held at the start: no translator (test) */
  { extern int t18_new_off; t18_new_off = (hw_buttons() & BTN_C) != 0; }   /* C held: 1.6.8 changes off (test) */
#ifdef OPT_MENU
  g_ovl = 1;                                          /* the wheel opens the settings: speed always shown */
#endif
#endif
  if (tg18_struct_size() > sizeof g_tg) fatal(5, tg18_struct_size());
  tg18_init(T, 0, 0);
  tg18_set_addrs(T, g_cfg.idle_addr, g_cfg.softrtc, g_cfg.dma_tab);
#ifdef HW_OPTS
  /* 1.6.8: volume (bits 16-17: 0 normal, 1 quiet, 2 loud, 3 off), brightness (18-19), screen off after (20-22) */
#ifdef OPT_MENU
  opt_load(); opt_apply(g_opt);
#else
  opt_apply(g_cfg.flags);
#endif
#endif
  tg18_set_present(T, on_present, 0);
  g_stage = 4;

#ifdef T18_CLOCK_AFTER
  tg18_set_rtc_cb(T, clock_after_set);   /* 1.6.8: the game's own clock setting does not overwrite the Paradise's time */
#endif
  uint32_t saved_rtc = 0, restored = (uint32_t)session_load(&saved_rtc);
#ifdef SESS_ONCE
  if (restored) session_drop();   /* 1.6.8: used once; power gone without leaving: next start from the toy's own save */
#endif
  g_stage = 5;
  uint32_t now_rtc = rtc_now();
  if ((g_cfg.flags & T18F_CLOCK) && g_para) {
    /* the Paradise's time, as it is now: nothing is caught up */
    set_time_secs((int64_t)g_para + (int64_t)(now_rtc - g_para_cnt));
  } else if (restored) {
    uint32_t gap = now_rtc - saved_rtc;
    if (gap < 400u * 86400u) tg18_advance_offline(T, (int64_t)gap);
  } else {
    Note n;
    if (note_load(&n)) set_time_secs((int64_t)para_days((int32_t)n.tm[0], (int32_t)n.tm[1], (int32_t)n.tm[2]) * 86400 + n.tm[3] * 3600 + n.tm[4] * 60 + n.tm[5] + (int64_t)(now_rtc - n.rtc));
    else if (g_cfg.wall_ymd) set_time_secs((int64_t)para_days((int32_t)(g_cfg.wall_ymd >> 16), (int32_t)(g_cfg.wall_ymd >> 8 & 0xFF), (int32_t)(g_cfg.wall_ymd & 0xFF)) * 86400 + g_cfg.wall_sod + (int64_t)(now_rtc - g_cfg.wall_cnt));
    else tg18_set_time(T, 2026, 1, 1, 12, 0, 0);
  }
  g_stage = 6;
  return (int)restored;
}

static void main_loop(int restored)
{
  uint32_t emu_ms = g_ms, t_start = g_ms, wheel_ms = 0, last_pres = 0, sp_t = g_ms, sp_emu = 0;
#ifdef HW_OPTS
  g_key_ms = g_ms;
#endif
  int bl = -1, lock = 1;
  g_stage = 7;
  for (;;) {
    hw_wdt_feed();
    uint32_t now = g_ms;
    u32 b = hw_buttons();
    if (lock) { if (b) b = 0; else lock = 0; }         /* wait until A+C from the start are released */
    if (b & BTN_WHEEL) {
      if (restored && now - t_start < 3000u) {            /* wheel right after a resumed start: drop the session? */
        if (hw_ask_reset((g_cfg.flags & T18F_EN) != 0)) { session_drop(); leave(0); }
        restored = 0; wheel_ms = 0; lock = 1; present(1); continue;   /* 1.6.6: asked first */
      }
      if (!wheel_ms) wheel_ms = now ? now : 1;
      else if (now - wheel_ms > 1500u) leave(1);
#ifdef OPT_MENU
    } else if (wheel_ms) {           /* 1.6.8: wheel short: settings (the game stands still meanwhile) */
      if (now - wheel_ms < 1000u) { uint32_t n = hw_opts_menu(g_opt, (g_cfg.flags & T18F_EN) != 0);
        if (n != g_opt) { g_opt = n; opt_apply(n); opt_store(); }
        g_scr_dark = 0; g_key_ms = g_ms; lock = 1; present(1); }
      wheel_ms = 0; }
#elif defined(T18_PROF_OVL)
    } else if (wheel_ms) { if (now - wheel_ms < 1000u) { g_ovl = (g_ovl + 1) % 3; if (g_ovl == 2) prof_show(); else present(1); } wheel_ms = 0; }
#elif defined(T18_NO_SPEED)
    } else if (wheel_ms) { wheel_ms = 0; }        /* release: no speed display */
#else
    } else if (wheel_ms) { if (now - wheel_ms < 1000u) { g_ovl = !g_ovl; present(1); } wheel_ms = 0; }
#endif
#ifdef HW_OPTS
    /* screen off after a while without a key (set in the Studio): the first key only wakes it */
    { static int swallow;
      if (b) { if (g_scr_dark) { g_scr_dark = 0; swallow = 1; } g_key_ms = now; }
      if (swallow) { if (b) b = 0; else swallow = 0; }
      if (g_scr_ms && now - g_key_ms > g_scr_ms) g_scr_dark = 1; }
#endif
    tg18_set_buttons(T, (int)(b & 7u));
#ifdef T18_CLOCK_AFTER
    clock_after_poll();
#endif
    /* emulate the time that passed in steps of 4 ms; far behind: drop time (the clock stays right) */
    if ((int32_t)(now - emu_ms) > 250) {
      static uint32_t lost;
      uint32_t d = now - 250u - emu_ms; g_t18st[1] += d; emu_ms += d; lost += d;
      if (lost >= 1000u) { tg18_advance_offline(T, (int64_t)(lost / 1000u)); lost %= 1000u; }   /* the game clock keeps up */
    }
    uint32_t t0 = g_ms;
    while ((int32_t)(now - emu_ms) >= 4 && g_ms - t0 < 40u) {
      tg18_run_us(T, 4000);
      emu_ms += 4; g_t18st[0] += 4; sp_emu += 4;
#ifdef T18_TONE_Q
      g_gms += 4;
#endif
#ifdef T18_PRESENT_IDLE
#ifndef T18_PRESENT_MAXMS
#define T18_PRESENT_MAXMS 1500u
#endif
      /* 1.6.6: the panel scans the firmware's buffer all the time; slower than the real thing we would show
       * it half drawn (built up from the top, black behind texts). A picture goes out only once the firmware
       * waits again (idle loop or halt: it has finished drawing), at most 30 a second; if it never waits,
       * every 1.5 s. */
      { static uint64_t ic0, cy0; static uint32_t pt;
        arm7_t *c = tg18_cpu(T);
        uint64_t ic = c->idle_cycles, cy = c->cycles;
        uint32_t tn = g_ms;
        int waited = (ic - ic0) * 4u > cy - cy0;                  /* waited for a quarter of these 4 ms or more */
#ifdef T18_PRESENT_STABLE
        /* test9: a long stretch without waiting (leaving a menu) is the firmware drawing a new screen;
         * shown only once the picture holds still for 250 ms, at the latest after 5 s */
        { static uint32_t ht, h_last;
          if (g_lcd_on && g_pres_len && waited && tn - pt >= 33u) { pt = tn; ht = tn; present(0); }
          else if (g_lcd_on && g_pres_len && tn - ht >= 250u) {
            ht = tn; uint32_t h = frame_hash();
            if ((h == h_last && tn - pt >= 250u) || tn - pt >= 5000u) { pt = tn; present(0); }
            h_last = h; } }
#else
        if (g_lcd_on && g_pres_len && ((waited && tn - pt >= 33u) || tn - pt >= T18_PRESENT_MAXMS)) { pt = tn; present(0); }
#endif
        ic0 = ic; cy0 = cy; }
#endif
      static uint32_t hz_now;
      uint32_t hz = tg18_tone_hz(T);
#ifdef T18_TONE_Q
      if (hz != hz_now) { hz_now = hz; tq_put(hz); }
      tq_pump();
#else
      if (hz != hz_now) { hz_now = hz; hw_tone(hz); }
#endif
    }
#ifdef T18_TONE_Q
    tq_pump();
#endif
#ifdef T18_FLUSH_IDLE
    /* the guest's last written sector stayed in the buffer until leaving: lost when the power went */
    if (c_dirty && g_ms - g_cw_ms > 3000u) c_write_back();
#endif
    if (t18_oom) fatal(2, g_pool_n);                    /* the guest's bytes in the hole need more pages */
    if (__stack_bottom != 0x5354414Bu) fatal(3, 0);     /* stack ran into the data */
    int on = tg18_power(T) && tg18_backlight(T) > 0;
#ifdef HW_OPTS
    if (g_scr_dark) on = 0;
#endif
    if (on != bl) { bl = on; g_lcd_on = on; hw_backlight(on); if (on) present(1);
#ifdef SESS_AUTO
      if (!on && now - g_sess_ms > SESS_T1) { note_save(); session_save(); g_sess_ms = now; }
#endif
    }
#ifdef SESS_AUTO
    /* 1.6.8: the run state now and then, without leaving (battery empty or out while playing): when the screen goes
     * dark (10 minutes after the last time at the earliest), otherwise every 30 minutes after 10 s without a key */
    if (b) g_sess_key = now;
    if (now - g_sess_ms > SESS_T2 && now - g_sess_key > 10000u) { note_save(); session_save(); g_sess_ms = now; }
#endif
#ifdef T18_PRESENT_LATE
    if (on && g_pres_n != last_pres && now - t0 < 1000u) { last_pres = g_pres_n; present(0); }
#else
    (void)last_pres;
#endif
    if (now - sp_t >= 1000u) { g_speed = sp_emu * 100u / (now - sp_t); sp_t = now; sp_emu = 0; if (g_ovl == 1 && on) draw_overlay();
#ifdef T18_PROF_OVL
      prof_tick();
#endif
    }
    /* caught up: sleep until the next tick */
    { static uint32_t g0 = 0xFFFFFFFFu; static int tick_ok;
      if (g0 == 0xFFFFFFFFu) g0 = g_ms;
      if (!tick_ok && g_ms != g0) tick_ok = 1;
#ifdef T18_PROF_OVL
      if (tick_ok && (int32_t)(g_ms - emu_ms) < 4) { g_pflag |= 2u; __asm__ volatile("wfi"); g_pflag &= ~2u; } }
#else
      if (tick_ok && (int32_t)(g_ms - emu_ms) < 4) __asm__ volatile("wfi"); }
#endif
  }
}

#ifndef T18_BUILD
#define T18_BUILD 1u
#endif
__attribute__((section(".xipmagic"), used)) const uint32_t g_xip_id[2] = { T18_XIP_MAGIC, T18_BUILD };
extern u32 __bss_start, __bss_end, __xip_magic[];
#ifdef T18_HOTWIN
/* from the SRAM: the code in the window must not run while the base moves */
__attribute__((section(".ramfunc"), noinline)) static void hot_window(void)
{
  XIP_BASE_REG = 0x60000000u + T18_HOT_BASE; XIP_CTRL_REG = 3u;
  __asm__ volatile("dsb\n isb" ::: "memory");
  volatile u32 sum = 0;
  for (u32 o = 0; o < 0x100000u; o += 16u) sum += *(volatile u32 *)(0x10000000u + o);   /* every line once */
  (void)sum;
  __asm__ volatile("dsb\n isb" ::: "memory");
  if (__xip_magic[0] != T18_XIP_MAGIC || __xip_magic[1] != T18_BUILD) {   /* not as expected: back */
    XIP_BASE_REG = 0x60011000u; XIP_CTRL_REG = 3u; __asm__ volatile("dsb\n isb" ::: "memory");
    for (u32 o = 0; o < 0x100000u; o += 16u) sum += *(volatile u32 *)(0x10000000u + o);
    return;
  }
  g_wbase = T18_HOT_BASE;
}
#endif
void Reset_Handler(void)
{
  __asm__ volatile("cpsid i");
  /* the Paradise's clock, before anything of it is overwritten (the hole is ours from now on) */
  uint32_t cnt = REG(0x45000304u);
  uint32_t para = para_clock(cnt, 0, T18_SRAM_SIZE, T18_HOLE_LO, T18_HOLE_HI);
  if (!para) para = para_mailbox(cnt);   /* 1.6.8: started from the menu */
#ifdef PARA_BACK
  para_leave_mark(cnt, para);            /* 1.6.8: the Paradise does not live meanwhile */
#endif
  __asm__ volatile("" ::: "memory");
  for (u32 *p = &__bss_start; p < &__bss_end; p++) *p = 0;
  { extern u32 __data_start, __data_end, __data_load;
    const u32 *src = &__data_load; for (u32 *p = &__data_start; p < &__data_end; p++) *p = *src++; }
  __stack_bottom = 0x5354414Bu;
  g_para = para; g_para_cnt = cnt;
#ifdef T18_PROF_OVL
#ifndef T18_PROF_LITE
  for (u32 i = 0; i < 0x224u; i++) REG(0x20000400u + 4u * i) = 0;
#endif
#endif
#if defined(T18_SLOW_HIST) && defined(T18_SLOW_HIST_AT)
  for (u32 i = 0; i < 60u; i++) REG(T18_SLOW_HIST_AT + 4u * i) = 0;
#endif
#ifdef T18_PROF_PC
  for (u32 i = 0; i < 51u; i++) REG(T18_PROF_PC + 4u * i) = 0;
#ifdef T18_PROF_GPC
  for (u32 i = 0; i < 12u; i++) REG(0x20000FD0u + 4u * i) = 0;
#endif
#endif
  REG(0xE000ED14u) &= ~((1u << 3) | (1u << 4));      /* no trap on unaligned access or division by 0 */
  __asm__ volatile("dsb\n isb" ::: "memory");
  hw_init();
  g_stage = 1;
  /* flash window: the Paradise firmware's base; our part of the code is in it */
  XIP_BASE_REG = 0x60011000u; XIP_CTRL_REG = 3u;
  __asm__ volatile("dsb\n isb" ::: "memory");
  { volatile u32 sum = 0; for (u32 i = 0; i < 0x4000u; i++) sum += *(volatile u32 *)(0x10040000u + 4u * i); (void)sum; }   /* old lines out */
  if (__xip_magic[0] != T18_XIP_MAGIC || __xip_magic[1] != T18_BUILD) fatal(4, __xip_magic[1]);   /* the flash part is missing or from another build */
  g_stage = 2;
  /* code written into the PRAM where code already ran (the start code) must run as written */
  { extern void hw_init(void); uint16_t *p = (uint16_t *)((uint32_t)&hw_init & ~3u);
    t18_pram_w16(p, 0x205Au); t18_pram_w16(p + 1, 0x4770u);
    uint32_t r1 = ((uint32_t (*)(void))((uint32_t)p | 1u))();
    t18_pram_w16(p, 0x20A5u);
    uint32_t r2 = ((uint32_t (*)(void))((uint32_t)p | 1u))();
    if (r1 != 0x5Au || r2 != 0xA5u) { uint32_t v[2] = { r1, r2 }; fatal_rows(6, v, 2); } }
#ifdef T18_RAM_PROBE
  ram_probe();
#endif
#ifdef T18_HOTWIN
  /* hot window: the same flash part lies at T18_XIP_HOT, so the code in the window stays the same when the
   * base moves; the rest of the window then shows the hot firmware blocks (old lines out first) */
  if ((g_cfg.flags & T18F_HOTWIN) && *(const volatile uint32_t *)(0x60000000u + T18_XIP_HOT) == T18_XIP_MAGIC
      && *(const volatile uint32_t *)(0x60000000u + T18_XIP_HOT + 4u) == T18_BUILD) hot_window();
#endif
  int restored = app_start();
  SYST_RVR = CORE_HZ / 1000u - 1u; SYST_CVR = 0; SYST_CSR = 7;
  __asm__ volatile("cpsie i");
  main_loop(restored);
}

__attribute__((naked)) static void Irq_Off(void) { __asm__ volatile("cpsid i\n bx lr\n"); }
extern u32 __stack_top;
__attribute__((section(".vectors"), used))
const void *const g_vectors[16 + 64] = { (void *)&__stack_top, (void *)Reset_Handler,
  [2] = (void *)Fault_Handler, [3 ... 6] = (void *)Fault_Handler, [7 ... 10] = (void *)Fault_Handler, [11] = (void *)Irq_Off,
  [12 ... 13] = (void *)Fault_Handler, [14] = (void *)Irq_Off, [15] = (void *)SysTick_Handler, [16 ... 79] = (void *)Irq_Off };
