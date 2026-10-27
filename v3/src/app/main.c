/* V3Emu: the Tamagotchi Connection V3 (Sunplus SPLB32) on the Tamagotchi Paradise. Core ported from BrickEmuPy
 * by azya52 (CC0), see ../core. Started from the Paramix menu like the other further programs.
 * Keys: A, B, C as on the toy (left, middle, right); wheel short = settings; wheel long = save and back; wheel
 * within 3 s after a resumed start = start over (asks first).
 * The screen: the 32 x 30 dots four times enlarged. The toy's own save (EEPROM, 1 KB) goes to the flash a few
 * seconds after the game wrote it (two slots, a power cut leaves one whole).
 * Author: mameise */
#include "../../../emu/src/emu/hw.h"
#include "../../../emu/src/emu/exitkey.h"
#include "../../../emu/src/emu/flash.h"
#include "../core/v3cpu.h"
#ifdef V3_CARE
#include "../../../emu/src/emu/ask.h"
#endif
#define PARA_ATTR
#include "../../../emu/src/emu/paraclock.h"
#ifdef PARA_BACK
#include "../../../emu/src/emu/paraback.h"
#endif

int hw_ask_reset(int en);
typedef int s32;

/* Settings block at 0x140, filled in by the Studio: the block of V1Emu (magic "PMX1", same layout), so the Studio keeps
 * places, saves and backups as for V1Emu; it tells the two apart by the ROM (512 KB here). The icons are not used.
 * save_off: session (4 KB), then 4 KB with the settings of the device and the toy's EEPROM. */
typedef struct { u32 magic, version, rom_off, rom_len, save_off, flags; char name[16]; u32 icons_ok; u16 icons[8][16]; } V3Cfg;
#define V3_MAGIC 0x31584D50u   /* "PMX1" */
__attribute__((section(".pmxcfg"), used))
volatile const V3Cfg g_cfg = { V3_MAGIC, 1, 0, 0, 0, 0, {0}, 0, {{0}} };

#define SESSION (g_cfg.save_off)
#define SETTINGS (g_cfg.save_off + 0x1000u)
#define EE_AT 0x100u                   /* in the settings sector: "V3EE", sequence, sum, 4 bytes free, then the 1 KB */
#define XIP_BASE_REG REG(0x4002F004u)
#define XIP_CTRL_REG REG(0x4002F000u)
#include "font.h"
#ifdef CLK_ECO
#include "../../../emu/src/emu/eco.h"
#endif
#ifdef V3_LINK
/* 1.6.9 test20: infrared between two Paradise over the contacts (plink, as T7Emu and T18Emu). The core turns the
 * toy's IR pulses into bytes and back (CON_V3_IR of BrickEmuPy, which links two emulators the same way over a
 * serial line); here they go as 'V' frames to the other Paradise. */
#include "../../../emu/src/emu/plink.h"
static void lk_got(u8 ty, const u8 *d, int n) { if (ty == 'V') v3_ir_rx(d, n); }
#endif
#define ST_MAGIC 0x54533356u   /* "V3ST" */

/* ---- time: SysTick 1 ms ---- */
static volatile u32 g_ms;
#define SYST_CSR REG(0xE000E010u)
#define SYST_RVR REG(0xE000E014u)
#define SYST_CVR REG(0xE000E018u)
#define CORE_HZ 96000000u
__attribute__((used)) void systick_c(void) { g_ms++; }
__attribute__((naked)) void SysTick_Handler(void) { __asm__ volatile("b systick_c\n"); }
#ifdef V3_PW
/* ---- 1.6.13 test12: power saving with the screen dark (as T7Emu, T18Emu and TamaEmu) ----
 * Screen dark, no key, tone, menu or link traffic: the chip runs from its internal 12 MHz RC with PLL and crystal off
 * and goes to deep sleep once the emulation has caught up; the RTC (1 s) or a key wakes it, the ms tick continues from
 * the RTC. The toy's time is then run in one go (it sleeps most of the time), never dropped: its clock stays right. */
#define OSC_CTRL REG(0x45000000u)
#define OSC_STAT REG(0x45000008u)
#define SYS_CLKCFG REG(0x4500000Cu)
#define SYS_AHBCP REG(0x45000010u)
#define ISO_CTRL REG(0x45000200u)
#define PMU_CTRL REG(0x45000300u)
#define GP0(o) REG(0x40018000u + (o))
#define NVIC_ISER0 REG(0xE000E100u)
#define NVIC_ICER0 REG(0xE000E180u)
#define NVIC_ICPR0 REG(0xE000E280u)
#define PW_KEYS 0xF00u                     /* P0.8 wheel, P0.9 A, P0.10 C, P0.11 B */
#define PW_IRQS ((1u << 2) | (1u << 26))   /* RTC, GPIO0 */
static int g_pw_low;
static u32 g_pw_rtc, g_pw_rtcms, g_pw_skip;
static volatile u8 g_pw_rtcw;              /* the RTC woke the chip (its interrupt ran) */
static u8 g_pw_kw;                         /* woken by a key before the next RTC second: that part of a second is not counted yet */
__attribute__((used, noinline)) void pw_rtc_irq(void) { PMU_CTRL = (PMU_CTRL & ~0x47u) | 0x10u; g_pw_rtcw = 1; }   /* RTC_FLAG: write 1 */
__attribute__((used, noinline)) void pw_gpio_irq(void) { GP0(0x20) = PW_KEYS; }
__attribute__((noinline, minsize)) static void pw_full(void)
{
  if (!g_pw_low) return;
  OSC_CTRL |= 1u << 4;                                          /* crystal on (the PLL's source, as hw_init sets it) */
  for (u32 t = 0; !(OSC_STAT & (1u << 4)) && t < 2000000u; t++) {}
  OSC_CTRL |= 1u << 15;                                         /* PLL on */
  for (u32 t = 0; !(OSC_STAT & (1u << 6)) && t < 2000000u; t++) {}
  SYS_CLKCFG = 4u;                                              /* system clock from the PLL */
  for (u32 t = 0; ((SYS_CLKCFG >> 4) & 7u) != 4u && t < 2000000u; t++) {}
  SYST_RVR = CORE_HZ / 1000u - 1u; SYST_CVR = 0;
  g_pw_low = 0;
}
__attribute__((noinline, minsize)) static void pw_low(void)
{
  if (g_pw_low) return;
  SYS_AHBCP = 0;
  SYS_CLKCFG = 0;                                               /* system clock from the IHRC (12 MHz) */
  for (u32 t = 0; ((SYS_CLKCFG >> 4) & 7u) != 0u && t < 2000000u; t++) {}
  if (((SYS_CLKCFG >> 4) & 7u) != 0u) { SYS_CLKCFG = 4u; return; }   /* did not switch: stay as it was */
  OSC_CTRL &= ~((1u << 15) | (1u << 4));                        /* PLL and crystal off */
  SYST_RVR = 12000000u / 1000u - 1u; SYST_CVR = 0;
  g_pw_low = 1;
}
/* deep sleep until the next RTC second or a key; returns the seconds that passed */
__attribute__((noinline, minsize)) static u32 pw_sleep(void)
{
  u32 iso = ISO_CTRL;
  if (!(iso & 0x80u) || (iso & 7u) != 0u) { __asm__ volatile("wfi"); return 0; }   /* the RTC is not on 1 s: no deep sleep */
  ISO_CTRL = iso & ~8u;                                         /* RTC_WKP 0: the RTC interrupt wakes */
  GP0(0x0C) &= ~PW_KEYS; GP0(0x10) |= PW_KEYS; GP0(0x20) = PW_KEYS; GP0(0x18) |= PW_KEYS;   /* edges, both */
  NVIC_ICPR0 = PW_IRQS; NVIC_ISER0 = PW_IRQS;
  u32 r0 = REG(0x45000304u), csr = SYST_CSR;
  SYST_CSR = 0;
  g_pw_rtcw = 0;
  PMU_CTRL = (PMU_CTRL & ~0x47u) | 2u;                          /* DSLEEP_EN */
  __asm__ volatile("dsb\n wfi\n isb" ::: "memory");
  PMU_CTRL = PMU_CTRL & ~0x47u;
  NVIC_ICER0 = PW_IRQS; GP0(0x18) &= ~PW_KEYS; GP0(0x20) = PW_KEYS;
  ISO_CTRL = (ISO_CTRL & ~8u) | (iso & 8u);
  SYST_CVR = 0; SYST_CSR = csr;
  /* woken by the RTC: wait until the counter (32 kHz domain) shows the new second (as T18Emu test3) */
  u32 r = REG(0x45000304u);
  if (g_pw_rtcw) for (u32 t = 0; r == r0 && t < 200000u; t++) r = REG(0x45000304u);
  u32 n = r - r0;
  if (!n) g_pw_kw = 1;
  return n > 3600u ? 0u : n;
}
#endif

/* ---- settings on the device (wheel short), like V1Emu ---- */
typedef struct { u8 magic[4]; u8 pal, grid, shadow, sound, dim, lang, dimto, off, r0, noicons, clk, ghost; } V3Set;   /* noicons: 0 = icons shown (V3_ICONS) */
/* test27 (V3_SETX, like the T7Emu): grid 0..100 % (old 1 = 100), shadow = pixel view 0..20 % (old 1 = 8), ghost = icons
 * that are off 0..30 % (0x80 + value; 0 = 18 as before), clk 0 = as the Studio says, 1 = Auto, 2 = game */
static V3Set g_set = { {'V','3','S','1'}, 0, 1, 1, 1, 7, 0, 6, 3, 0x80, 0, {0} };
static const u16 PAL[6][2] = { { 0xB5B2u, 0x18E3u }, { 0xC618u, 0x0000u }, { 0xFFFFu, 0x0000u }, { 0xEF34u, 0x4A00u }, { 0x9E7Fu, 0x000Fu }, { 0xFDF9u, 0x780Fu } };
static const char *const PAL_NM[2][6] = { { "LCD", "GRAU", "WEISS", "GELB", "BLAU", "PINK" }, { "LCD", "GREY", "WHITE", "YELLOW", "BLUE", "PINK" } };
#define T_N 7   /* 10 s ... 10 min, index T_N = never */
static const u32 T_MS[T_N] = { 10000u, 20000u, 30000u, 60000u, 120000u, 300000u, 600000u };
static const char *const T_NM[2][T_N + 1] = { { "10 S", "20 S", "30 S", "1 MIN", "2 MIN", "5 MIN", "10 MIN", "NIE" }, { "10 S", "20 S", "30 S", "1 MIN", "2 MIN", "5 MIN", "10 MIN", "NEVER" } };
#define LV_N 11
#define DIM2_N 10
static const u8 LV_PCT[LV_N] = { 100u, 5u, 10u, 20u, 30u, 40u, 50u, 60u, 70u, 80u, 90u };
static const u8 DIM2_PCT[DIM2_N] = { 1u, 2u, 3u, 5u, 10u, 15u, 20u, 30u, 40u, 50u };
#ifdef V3_SETX
static int gridp(void) { return g_set.grid == 1u ? 100 : g_set.grid <= 100u ? g_set.grid : 100; }
static int shdp(void) { return g_set.shadow == 1u ? 8 : g_set.shadow <= 20u ? g_set.shadow : 8; }
static int clk_auto(void) { return g_set.clk == 1u ? 1 : g_set.clk == 2u ? 0 : (int)((g_cfg.flags >> 1) & 1u); }   /* toy's clock from the Paradise */
static int ghp(void) { return (g_set.ghost & 0x80u) && (g_set.ghost & 0x7Fu) <= 30u ? g_set.ghost & 0x7F : 18; }
#endif
static u8 g_clk_diag;   /* V3_CLK_RETRY: 0 waiting, 1 written, 2 no time from the Paradise, 3 the toy's clock stands, 4 written but off */
static int en(void) { return g_set.lang ? g_set.lang == 2u : (int)(g_cfg.flags & 1u); }
static u16 mix(u16 a, u16 b, int p)   /* p percent of b */
{
  int r = ((a >> 11) * (100 - p) + (b >> 11) * p) / 100, g = (((a >> 5) & 63) * (100 - p) + ((b >> 5) & 63) * p) / 100, bl = ((a & 31) * (100 - p) + (b & 31) * p) / 100;
  return (u16)((r << 11) | (g << 5) | bl);
}
static u16 BG, INK, OFF, SHD, GHO, INKG, SHDG;
static int g_dimmed;
static void colours(void)
{
  u32 k = g_set.pal < 6u ? g_set.pal : 0u;
#ifdef V3_SETX
  BG = PAL[k][0]; INK = PAL[k][1]; OFF = mix(BG, INK, 40); GHO = mix(BG, INK, ghp()); SHD = mix(BG, INK, shdp());
  INKG = mix(INK, BG, gridp()); SHDG = mix(SHD, BG, gridp());   /* the gap between the dots */
#else
  BG = PAL[k][0]; INK = PAL[k][1]; OFF = mix(BG, INK, 40); GHO = mix(BG, INK, 18); SHD = g_set.shadow ? mix(BG, INK, 8) : BG;
#endif
#ifdef BL_PWM
  { u32 l = LV_PCT[(g_set.r0 & 15u) % LV_N], d = DIM2_PCT[g_set.dimto % DIM2_N];
    hw_bl_set(g_dimmed ? (d < l ? d : l) : l); }
#endif
}
static void set_load(void)
{
  const volatile u8 *p = XIP_PTR(SETTINGS); V3Set t; u8 *d = (u8 *)&t;
  for (u32 i = 0; i < sizeof t; i++) d[i] = p[i];
  if (t.magic[0] == 'V' && t.magic[1] == '3' && t.magic[2] == 'S' && t.magic[3] == '1' && t.pal < 6u && t.dim <= T_N && t.off <= T_N && t.lang < 3u) g_set = t;
  colours();
}
static u8 g_sec[0x1000] __attribute__((aligned(4)));
static u32 g_tone_now;

/* ---- the toy's EEPROM (HT24LC08, 1 KB), in the settings sector behind the settings, with a sequence number; the
 * session holds a copy too (if the sector is broken by a power cut while written, the session's copy is taken) ---- */
#define EE_MAGIC 0x45453356u
static u32 g_ee_seq; static int g_ee_ok;
static u32 ee_sum(const volatile u8 *d) { u32 s = 0x3E3E; for (u32 i = 0; i < V3_EE_SIZE; i++) s = s * 31u + d[i]; return s; }
static u32 rd32(const volatile u8 *h) { return h[0] | h[1] << 8 | h[2] << 16 | (u32)h[3] << 24; }
static void ee_load(void)
{
  const volatile u8 *h = XIP_PTR(SETTINGS + EE_AT);
  g_ee_ok = rd32(h) == EE_MAGIC && rd32(h + 8) == ee_sum(h + 16);
  if (!g_ee_ok) return;
  g_ee_seq = rd32(h + 4);
  u8 *e = v3_eeprom(); for (u32 i = 0; i < V3_EE_SIZE; i++) e[i] = h[16 + i];
}
/* settings and EEPROM together into the sector (only if something changed) */
static void sec_store(int ee_new)
{
  if (ee_new) g_ee_seq++;
  for (u32 i = 0; i < sizeof g_sec; i++) g_sec[i] = 0xFF;
  const u8 *s = (const u8 *)&g_set; for (u32 i = 0; i < sizeof g_set; i++) g_sec[i] = s[i];
  u32 v[3] = { EE_MAGIC, g_ee_seq, ee_sum(v3_eeprom()) };
  for (int k = 0; k < 3; k++) for (int b = 0; b < 4; b++) g_sec[EE_AT + k * 4 + b] = (u8)(v[k] >> (8 * b));
  const u8 *e = v3_eeprom(); for (u32 i = 0; i < V3_EE_SIZE; i++) g_sec[EE_AT + 16 + i] = e[i];
  hw_tone(0); g_tone_now = 0;
  flash_update4k(SETTINGS, g_sec);   /* writes only if different */
  g_ee_ok = 1;
}
static void set_store(void) { sec_store(v3_eeprom_dirty(1)); }
static void ee_store(void) { sec_store(1); }

/* ---- screen: 32 x 30 dots, 4 x 4 pixels each (3 x 3 with the grid), from y 4 ---- */
#include "../core/v3lcd.inc"
static u16 g_row[128] __attribute__((aligned(4)));
static u8 g_pix[30][32];
#ifdef V3_ICONS
/* 1.6.9 test19: the ten icons around the screen, lit by the toy's LCD segments (bit b of byte n, as the face of
 * BrickEmuPy names them "n_b"): top row left to right, then the bottom row. The pictures are mameise's V3 icons
 * (20 x 20, icons_v3.h from gen/icons.py; the second one is the T7Emu icon). With the icons the dots are three times enlarged (96 x 90). */
#include "icons_v3.h"
static const u8 ICO_SEG[10][2] = { { 0, 7 }, { 1, 0 }, { 1, 1 }, { 1, 2 }, { 1, 3 }, { 6, 3 }, { 6, 2 }, { 6, 1 }, { 6, 0 }, { 5, 7 } };
static u8 g_ico[10];
static int icons_on(void) { return !g_set.noicons; }
#define ICO_R0 2    /* icon rows 2..18 are drawn (17 px): top at y 0, bottom at y 111 */
#define ICO_H 17
static void icon_row(int y)
{
  int top = y < ICO_H, iy = (top ? y : y - 111) + ICO_R0;
  if (!top && y < 111) return;
  for (int k = 0; k < 5; k++) {
    int i = top ? k : 5 + k, x0 = k * 128 / 5 + 2;
    u16 col = g_ico[i] ? INK : GHO; u32 bits = g_icons[i][iy];
    for (int b = 0; b < 20; b++) if (bits & (0x80000u >> b)) g_row[x0 + b] = col;
  }
}
#endif
static u32 g_lcd_ms;
/* the LCD RAM to dots; 1 if something changed */
static u32 g_vcopy[V3_DPRAM / 4]; static int g_vmode = -1;
static int lcd_take(void)
{
  int mode, ch = 0; const u8 *v = v3_vram(&mode);
  { const u32 *w = (const u32 *)v; int same = mode == g_vmode;   /* 1.6.9: only when the LCD RAM changed */
    for (u32 i = 0; i < V3_DPRAM / 4; i++) if (w[i] != g_vcopy[i]) { g_vcopy[i] = w[i]; same = 0; }
    if (same) return 0; g_vmode = mode; }
  for (int y = 0; y < 30; y++) for (int x = 0; x < 32; x++) {
    int k = V3_LCD[y][x]; u8 on = (u8)(mode == 2 || (mode == 1 && ((v[k >> 3] >> (k & 7)) & 1)));
    if (g_pix[y][x] != on) { g_pix[y][x] = on; ch = 1; }
  }
#ifdef V3_ICONS
  for (int i = 0; i < 10; i++) { u8 on = (u8)(mode == 2 || (mode == 1 && ((v[ICO_SEG[i][0]] >> ICO_SEG[i][1]) & 1))); if (g_ico[i] != on) { g_ico[i] = on; ch = 1; } }
#endif
  return ch;
}
static void draw(void)
{
  hw_px_begin(0, 0, 128, 128);
#ifdef V3_ICONS
  if (icons_on()) {
    int gap = g_set.grid ? 2 : 3;
    for (int y = 0; y < 128; y++) {
      for (int x = 0; x < 128; x++) g_row[x] = BG;
      if (y >= 19 && y < 109) {
        int r = (y - 19) / 3, sub = (y - 19) % 3;
#ifdef V3_SETX
        for (int c = 0; c < 32; c++) { int on = g_pix[r][c]; u16 v = on ? INK : SHD, w = on ? INKG : SHDG; u16 *p = g_row + 16 + c * 3; if (sub < 2) { p[0] = p[1] = v; p[2] = w; } else p[0] = p[1] = p[2] = w; }
        (void)gap;
#else
        if (sub < gap) for (int c = 0; c < 32; c++) { u16 v = g_pix[r][c] ? INK : SHD; u16 *p = g_row + 16 + c * 3; p[0] = p[1] = v; if (gap == 3) p[2] = v; }
#endif
      } else icon_row(y);
      hw_px_row(g_row, 128);
    }
    hw_px_end();
    return;
  }
#endif
  int gap = g_set.grid ? 3 : 4;
  for (int y = 0; y < 128; y++) {
    for (int x = 0; x < 128; x++) g_row[x] = BG;
    if (y >= 4 && y < 124) {
      int r = (y - 4) >> 2, sub = (y - 4) & 3;
#ifdef V3_SETX
      for (int c = 0; c < 32; c++) { int on = g_pix[r][c]; u16 v = on ? INK : SHD, w = on ? INKG : SHDG; u16 *p = g_row + c * 4; if (sub < 3) { p[0] = p[1] = p[2] = v; p[3] = w; } else p[0] = p[1] = p[2] = p[3] = w; }
      (void)gap;
#else
      if (sub < gap) for (int c = 0; c < 32; c++) { u16 v = g_pix[r][c] ? INK : SHD; u16 *p = g_row + c * 4; p[0] = p[1] = p[2] = v; if (gap == 4) p[3] = v; }
#endif
    }
    hw_px_row(g_row, 128);
  }
  hw_px_end();
}

/* ---- the settings menu (wheel short): A next line, B change, C close ---- */
static int g_menu, g_msel, g_mtop;
static void text(int x, int y, const char *s, u16 col, u16 *row, int ry)
{
  for (; *s; s++, x += 6) {
    int c = (unsigned char)*s; if (c >= 97 && c <= 122) c -= 32; c = (c < 32 || c > 127) ? 0 : c - 32;
    if (ry < y || ry >= y + 7) continue;
    u8 bits = g_font[c][ry - y];
    for (int k = 0; k < 5; k++) if ((bits & (0x10 >> k)) && x + k >= 0 && x + k < 128) row[x + k] = col;
  }
}
enum {
#ifdef BAT_SHOW
  M_BAT,
#endif
  M_PAL, M_GRID, M_SHD,
#ifdef V3_ICONS
  M_ICON,
#ifdef V3_SETX
  M_GHOST,
#endif
#endif
  M_SND, M_LIGHT, M_DIM, M_DIMTO, M_OFF,
#if defined(V3_SETX) && defined(V3_CLOCK)
  M_CLK,
#endif
#ifdef V3_CARE
  M_GP, M_CARE,
#endif
  M_LANG, M_N };
static int g_items[M_N], g_nitems;
#ifdef V3_CARE
/* test31: the toy's RAM (found in the PC run, checked on its status pages): happy 0xA1 and hunger 0xA2 (4 hearts),
 * Gotchi Points 0x154/0x155 (BCD, lowest digits first, the status shows 4 digits) */
static u8 *ram(u32 a);
static u8 g_cared;
static u32 gp_get(void) { u8 l = *ram(0x154), h = *ram(0x155); return (u32)(h >> 4) * 1000u + (h & 15u) * 100u + (l >> 4) * 10u + (l & 15u); }
#endif
static void menu_items(void)
{
  g_nitems = 0;
  for (int k = 0; k < M_N; k++) { if (k == M_DIMTO && g_set.dim >= T_N) continue;
#if defined(V3_SETX) && defined(V3_ICONS)
    if (k == M_GHOST && g_set.noicons) continue;
#endif
#if defined(V3_SETX) && defined(V3_CLOCK)
    if (k == M_CLK && !(g_cfg.flags & 8u)) continue;    /* only when the Studio knows this toy's clock */
#endif
    g_items[g_nitems++] = k; }
  if (g_msel >= g_nitems) g_msel = g_nitems - 1;
}
#ifdef BAT_SHOW
#include "../../../emu/src/emu/batt.h"
static char g_bat[12];
#endif
static void pct(char *o, int v) { int n = 0; if (v >= 100) o[n++] = '1'; if (v >= 10) o[n++] = (char)('0' + (v / 10) % 10); o[n++] = (char)('0' + v % 10); o[n++] = '%'; o[n] = 0; }
static void menu_line(int it, const char **a, char *b)
{
  int e = en(); const char *v = 0; b[0] = 0;
  const char *on = e ? "ON" : "AN", *off = e ? "OFF" : "AUS";
  switch (it) {
#ifdef BAT_SHOW
    case M_BAT: *a = e ? "BATTERY" : "BATTERIE"; v = g_bat; break;
#endif
    case M_PAL: *a = e ? "COLOUR" : "FARBE"; v = PAL_NM[e][g_set.pal % 6u]; break;
#ifdef V3_SETX
    case M_GRID: *a = e ? "GRID" : "RASTER"; pct(b, gridp()); break;
    case M_SHD: *a = e ? "PIXEL VIEW" : "PIXEL-SICHT"; pct(b, shdp()); break;
#else
    case M_GRID: *a = e ? "GRID" : "RASTER"; v = g_set.grid ? on : off; break;
    case M_SHD: *a = e ? "SHADOW" : "SCHATTEN"; v = g_set.shadow ? on : off; break;
#endif
#ifdef V3_ICONS
    case M_ICON: *a = "ICONS"; v = g_set.noicons ? off : on; break;
#ifdef V3_SETX
    case M_GHOST: *a = e ? "ICONS OFF" : "ICONS INAKTIV"; pct(b, ghp()); break;
#endif
#endif
#ifdef V3_VOL
    case M_SND: { static const char *const VN[2][4] = { { "AUS", "NORMAL", "LAUT", "LEISE" }, { "OFF", "NORMAL", "LOUD", "QUIET" } };
      *a = e ? "SOUND" : "TON"; v = VN[e ? 1 : 0][g_set.sound & 3u]; break; }
#else
    case M_SND: *a = e ? "SOUND" : "TON"; v = g_set.sound ? on : off; break;
#endif
    case M_LIGHT: *a = e ? "LIGHT" : "LICHT"; pct(b, LV_PCT[(g_set.r0 & 15u) % LV_N]); break;
    case M_DIM: *a = e ? "DIM AFTER" : "DIMMEN NACH"; v = T_NM[e][g_set.dim <= T_N ? g_set.dim : T_N]; break;
    case M_DIMTO: *a = e ? "DIM TO" : "DIMMEN AUF"; pct(b, DIM2_PCT[g_set.dimto % DIM2_N]); break;
    case M_OFF: *a = e ? "SCREEN OFF" : "BILD AUS NACH"; v = T_NM[e][g_set.off <= T_N ? g_set.off : T_N]; break;
#if defined(V3_SETX) && defined(V3_CLOCK)
#ifdef V3_CLK_RETRY
    case M_CLK: *a = e ? "CLOCK" : "UHR"; v = !clk_auto() ? (e ? "GAME" : "SPIEL") : g_clk_diag == 1 ? "AUTO OK" : g_clk_diag == 2 ? "AUTO -" : g_clk_diag == 3 ? "AUTO !" : g_clk_diag == 4 ? "AUTO ?" : "AUTO"; break;
#else
    case M_CLK: *a = e ? "CLOCK" : "UHR"; v = clk_auto() ? "AUTO" : e ? "GAME" : "SPIEL"; break;
#endif
#endif
#ifdef V3_CARE
    case M_GP: *a = "GOTCHI POINTS"; { u32 n = gp_get(); char t[6]; int k = 0, j = 0; do { t[k++] = (char)('0' + n % 10u); n /= 10u; } while (n); while (k) b[j++] = t[--k]; b[j++] = 'P'; b[j] = 0; } break;
    case M_CARE: *a = e ? "FULL CARE" : "PFLEGE VOLL"; if (g_cared) v = "OK"; break;
#endif
    default: *a = e ? "LANGUAGE" : "SPRACHE"; v = e ? "ENGLISH" : "DEUTSCH"; break;
  }
  if (v) { int n = 0; while (v[n] && n < 11) { b[n] = v[n]; n++; } b[n] = 0; }
}
#define ROWS 9
static void menu_draw(void)
{
  if (g_msel < g_mtop) g_mtop = g_msel;
  if (g_msel >= g_mtop + ROWS) g_mtop = g_msel - ROWS + 1;
#ifdef BAT_SHOW
  bat_text(g_bat);
#endif
  hw_px_begin(0, 0, 128, 128);
  for (int y = 0; y < 128; y++) {
    for (int x = 0; x < 128; x++) g_row[x] = BG;
    text(4, 3, en() ? "SETTINGS" : "EINSTELLUNGEN", INK, g_row, y);
    if (y == 12) for (int x = 0; x < 128; x++) g_row[x] = OFF;
    for (int k = 0; k < ROWS && g_mtop + k < g_nitems; k++) {
      int i = g_mtop + k, ly = 16 + k * 11; const char *a; char b[12]; menu_line(g_items[i], &a, b);
      if (i == g_msel && y >= ly - 2 && y < ly + 9) for (int x = 0; x < 128; x++) g_row[x] = INK;
      u16 c = i == g_msel ? BG : INK;
      text(4, ly, a, c, g_row, y); int w = 0; while (b[w]) w++; text(124 - w * 6, ly, b, c, g_row, y);
    }
    if (g_mtop > 0 && y >= 4 && y < 8) for (int x = 118 - (y - 4); x <= 118 + (y - 4); x++) g_row[x] = OFF;
    if (g_mtop + ROWS < g_nitems && y >= 4 && y < 8) for (int x = 108 - (7 - y); x <= 108 + (7 - y); x++) g_row[x] = OFF;
    text(4, 118, en() ? "A NEXT B SET C OK" : "A ZEILE B SETZEN C OK", OFF, g_row, y);
    hw_px_row(g_row, 128);
  }
  hw_px_end();
}
static int g_set_dirty;
static void menu_close(void) {
#ifdef V3_CARE
  g_cared = 0;
#endif
  g_menu = 0; if (g_set_dirty) { set_store(); g_set_dirty = 0; } draw(); }
static void menu_key(u32 k)
{
  if (k & BTN_A) g_msel = (g_msel + 1) % g_nitems;
  if (k & BTN_B) {
    switch (g_items[g_msel]) {
#ifdef BAT_SHOW
      case M_BAT: break;
#endif
      case M_PAL: g_set.pal = (u8)((g_set.pal + 1u) % 6u); break;
#ifdef V3_SETX
      case M_GRID: g_set.grid = (u8)(gridp() >= 100 ? 0 : gridp() / 25 * 25 + 25); break;
      case M_SHD: g_set.shadow = (u8)(shdp() >= 20 ? 0 : shdp() / 2 * 2 + 2); break;
#else
      case M_GRID: g_set.grid ^= 1u; break;
      case M_SHD: g_set.shadow ^= 1u; break;
#endif
#ifdef V3_ICONS
      case M_ICON: g_set.noicons ^= 1u; break;
#ifdef V3_SETX
      case M_GHOST: g_set.ghost = (u8)(0x80u | (ghp() >= 30 ? 0 : ghp() / 2 * 2 + 2)); break;
#endif
#if defined(V3_SETX) && defined(V3_CLOCK)
      case M_CLK: g_set.clk = clk_auto() ? 2u : 1u; break;
#endif
#endif
#ifdef V3_CARE
      case M_GP: { u32 n = gp_get(); if (hw_num_edit("GOTCHI POINTS", &n, 4, 9999u, en())) { *ram(0x154) = (u8)((n / 10u % 10u) << 4 | n % 10u); *ram(0x155) = (u8)((n / 1000u) << 4 | n / 100u % 10u); } } menu_draw(); return;
      case M_CARE: if (*ram(0xA1) < 4u) *ram(0xA1) = 4; if (*ram(0xA2) < 4u) *ram(0xA2) = 4; g_cared = 1; menu_draw(); return;
#endif
#ifdef V3_VOL
      case M_SND: g_set.sound = (u8)((g_set.sound + 1u) & 3u); break;
#else
      case M_SND: g_set.sound ^= 1u; break;
#endif
      case M_LIGHT: g_set.r0 = (u8)(0x80u | (((g_set.r0 & 15u) + 1u) % LV_N)); break;
      case M_DIM: g_set.dim = (u8)((g_set.dim + 1u) % (T_N + 1u)); if (g_set.dim < T_N && g_set.off < T_N && g_set.off <= g_set.dim) g_set.off = (u8)(g_set.dim + 1u); break;
      case M_DIMTO: g_set.dimto = (u8)((g_set.dimto + 1u) % DIM2_N); break;
      case M_OFF: do g_set.off = (u8)((g_set.off + 1u) % (T_N + 1u)); while (g_set.off < T_N && g_set.dim < T_N && g_set.off <= g_set.dim); break;
      default: g_set.lang = (u8)(en() ? 1u : 2u); break;
    }
    g_set_dirty = 1; colours(); menu_items();
  }
  if (k & (BTN_C | BTN_WHEEL)) { menu_close(); return; }
  menu_draw();
}

/* ---- session: the whole state (CPU, RAM, LCD, timers, EEPROM copy), and which EEPROM slot it belongs to ---- */
static u32 crc_rom(void) { const volatile u8 *r = XIP_PTR(g_cfg.rom_off); u32 h = 2166136261u; for (u32 i = 0; i < 0x8000u; i += 7u) h = (h ^ r[i]) * 16777619u; return h; }
static void save_session(void)
{
  u32 n = v3_state_size();
  if (n + 16u > sizeof g_sec) return;
  if (v3_eeprom_dirty(1) || !g_ee_ok) ee_store();   /* the EEPROM first: the session notes its sequence */
  for (u32 i = 0; i < sizeof g_sec; i++) g_sec[i] = 0xFF;
  u32 *w = (u32 *)g_sec; w[0] = ST_MAGIC; w[1] = crc_rom(); w[2] = g_ee_seq; w[3] = n;
  v3_state_save(g_sec + 16);
  hw_tone(0); g_tone_now = 0;
  flash_update4k(SESSION, g_sec);
}
static int load_session(void)
{
  const volatile u8 *s = XIP_PTR(SESSION);
  for (u32 i = 0; i < sizeof g_sec; i++) g_sec[i] = s[i];
  u32 *w = (u32 *)g_sec;
  if (w[0] != ST_MAGIC || w[1] != crc_rom() || w[3] != v3_state_size()) return 0;
  if (g_ee_ok && w[2] != g_ee_seq) return 0;   /* the game has saved since: start from its EEPROM (as the toy after a battery change) */
  if (!v3_state_load(g_sec + 16, w[3])) return 0;
  if (g_ee_ok) ee_load(); else g_ee_seq = w[2];   /* a broken sector: the session's copy */
  return 1;
}
static u32 g_sess_ms;
__attribute__((used)) volatile u32 g_v3st[2];   /* emulated ms, dropped ms (for the tests) */
#ifdef V3_EE2
/* test21: a used session is not erased but marked used (first word 0): its copy of the EEPROM stays as the second copy
 * for a settings sector that broke while being written (power gone in the middle) */
static void drop_session(void)
{
  const volatile u8 *s = XIP_PTR(SESSION); u32 w0 = rd32(s);
  if (w0 == 0u) return;
  if (w0 == ST_MAGIC) { for (u32 i = 0; i < sizeof g_sec; i++) g_sec[i] = s[i]; g_sec[0] = g_sec[1] = g_sec[2] = g_sec[3] = 0; flash_update4k(SESSION, g_sec); return; }
  if (!flash_isff(SESSION, 0x1000u)) flash_erase4k(SESSION);
}
static int ee_from_session(void)
{
  const volatile u8 *s = XIP_PTR(SESSION); u32 w0 = rd32(s);
  if ((w0 != ST_MAGIC && w0 != 0u) || rd32(s + 4) != crc_rom() || rd32(s + 12) != v3_state_size()) return 0;
  for (u32 i = 0; i < sizeof g_sec; i++) g_sec[i] = s[i];
  const u8 *e = v3_state_ee(g_sec + 16); u8 *d = v3_eeprom(); for (u32 i = 0; i < V3_EE_SIZE; i++) d[i] = e[i];
  g_ee_seq = rd32(s + 8); return 1;
}
#else
static void drop_session(void) { if (!flash_isff(SESSION, 0x1000u)) flash_erase4k(SESSION); }
#endif
static void leave(int save)
{
#ifdef V3_PW
  pw_full();
#endif
#ifdef CLK_ECO
  hw_clk_shift(0, CORE_HZ);
#endif
  hw_tone(0);
  set_store();
  REG(0x20000FF8u) = 0;
  REG(0x20000FFCu) = 0x4C584D50u;        /* "PMXL": back on purpose, the autostart stays off */
  if (v3_eeprom_dirty(1)) ee_store();
  if (save) save_session();
#ifdef PARA_BACK
  para_back_fix(g_sec);
#endif
  hw_bl_pwm_off();
  for (u32 t = 0; hw_buttons() && t < 20000000u; t++) if ((t & 0xFFFFu) == 0) hw_wdt_feed();
  XIP_BASE_REG = 0x60011000u; XIP_CTRL_REG = 3u;
  hw_back_to_paradise();
}

#ifdef V3_CLOCK
/* 1.6.9 test20: the toy's clock from the Paradise at every start (flags bit 1, the Studio's tick; bit 3: the Studio
 * knows this toy's clock). Found on the host: seconds 0x81 (ones) 0x82 (tens), minutes 0x83 0x84, hour 0x85 (1..12,
 * 0 = never set) and 0x86 (1 = PM), day 0x13D (ones) 0x13E (tens), month 0x13F (1..12). The clock stands until it was set once in the game, so
 * only a running clock is written; then it is checked and written again if it is off (three times at most). */
static u32 g_para_time, g_para_cnt;
static u8 *ram(u32 a) { return v3_ram() + (a - 0x40u); }
static int clk_running_val(void) { return ram(0x81)[0] + 10 * ram(0x82)[0]; }
static void civil(u32 days, u32 *mo, u32 *d)   /* days since 1970 -> month, day */
{
  s32 z = (s32)days + 719468, era = z / 146097, doe = z - era * 146097, yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  s32 doy = doe - (365 * yoe + yoe / 4 - yoe / 100), mp = (5 * doy + 2) / 153;
  *d = (u32)(doy - (153 * mp + 2) / 5 + 1); *mo = (u32)(mp < 10 ? mp + 3 : mp - 9);
}
static u32 para_now(void) { return g_para_time + (REG(0x45000304u) - g_para_cnt); }
static void clock_write(void)
{
  u32 t = para_now(), d = t % 86400u, h = d / 3600u, m = d / 60u % 60u, sec = d % 60u, mo, dy;
  ram(0x81)[0] = (u8)(sec % 10u); ram(0x82)[0] = (u8)(sec / 10u); ram(0x83)[0] = (u8)(m % 10u); ram(0x84)[0] = (u8)(m / 10u);
  ram(0x85)[0] = (u8)(h % 12u ? h % 12u : 12u); ram(0x86)[0] = (u8)(h >= 12u);
  civil(t / 86400u, &mo, &dy); ram(0x13D)[0] = (u8)(dy % 10u); ram(0x13E)[0] = (u8)(dy / 10u); ram(0x13F)[0] = (u8)mo;   /* test23: the day as two digits */
}
static int clock_ok(void)
{
  u32 t = para_now(), want = t % 86400u / 60u, h = ram(0x85)[0] % 12u + (ram(0x86)[0] ? 12u : 0u), m = ram(0x83)[0] + 10u * ram(0x84)[0];
  u32 diff = (h * 60u + m + 1440u - want) % 1440u;
  return diff <= 1u || diff >= 1439u;
}
#endif
extern u32 __bss_start, __bss_end;
#ifdef V3_TONE_Q
/* test21: tone in real time (as TamaEmu, T18Emu): changes with the toy's time, played on the real clock 250 ms after the
 * first of a sequence; more than 200 ms late: anew from there. A tone always at the full clock. */
#define TQ_N 32u
#define TQ_LAT 250u
static u32 tq_t[TQ_N]; static u16 tq_hz[TQ_N]; static u32 tq_h, tq_w, tq_g0, tq_r0, tq_on, tq_last;
static void tq_put(u32 hz, u32 gms) { if (tq_w - tq_h >= TQ_N) tq_h++; tq_t[tq_w % TQ_N] = gms; tq_hz[tq_w % TQ_N] = (u16)(hz > 0xFFFFu ? 0xFFFFu : hz); tq_w++; }
__attribute__((noinline)) static void tq_pump(void)
{
  u32 now = g_ms;
  while (tq_h != tq_w) {
    u32 i = tq_h % TQ_N;
    if (!tq_on) { tq_on = 1; tq_g0 = tq_t[i]; tq_r0 = now + TQ_LAT; }
    s32 late = (s32)(now - (tq_r0 + (tq_t[i] - tq_g0)));
    if (late < 0) return;
    if (late > 200) { tq_g0 = tq_t[i]; tq_r0 = now; }
    u32 hz = tq_hz[i];
#ifdef V3_PW
    if (hz) pw_full();
#endif
    hw_tone(hz); tq_h++; tq_last = now;
  }
  if (!g_hw_tone_hz && now - tq_last > 1000u) tq_on = 0;
}
#endif
#ifdef V3_VOL
/* test21: volume as in the other emulators: sound 0 off, 1 normal (the old "on"), 2 loud, 3 quiet */
static const u8 VOL_OF[4] = { 0, 2, 3, 1 };
#endif
void Reset_Handler(void)
{
  __asm__ volatile("cpsid i");
  u32 cnt = REG(0x45000304u), para = para_clock(cnt, 0, 0x1F000u, 0, 0);
  if (!para) para = para_mailbox(cnt);
#if defined(PARA_FLASH) && !defined(PARA_FLASH_LATE)
  if (!para) para = para_flash(cnt);   /* 1.6.9 test37 */
#endif
#ifdef PARA_BACK
  para_leave_mark(cnt, para);
#endif
  __asm__ volatile("" ::: "memory");
  for (u32 *p = &__bss_start; p < &__bss_end; p++) *p = 0;
  { extern u32 __data_start, __data_end, __data_load;
    const u32 *src = &__data_load; for (u32 *p = &__data_start; p < &__data_end; p++) *p = *src++; }
  (void)para;
#ifdef V3_CLOCK
  g_para_time = para; g_para_cnt = cnt;
#endif
  hw_init();
  hw_tone(0);
#if defined(V3_CLOCK) && defined(PARA_FLASH_LATE)
  /* 1.6.10 test2: neither SRAM nor mailbox: the base in the Paradise's settings page (readable only after hw_init) */
  if (!g_para_time) { u32 c2 = REG(0x45000304u), t2 = para_flash(c2); if (t2) { g_para_time = t2; g_para_cnt = c2; para_mailbox_put(t2, c2); } }
#endif
  XIP_BASE_REG = 0x60000000u | g_cfg.rom_off;     /* the ROM in the cached window from 0x10000000 */
  XIP_CTRL_REG = 3u;
  v3_init((const u8 *)0x10000000u, g_cfg.rom_len);
  ee_load();
#ifdef V3_EE2
  int ee_fix = !g_ee_ok && ee_from_session();
#endif
  int restored = load_session();
  if (!restored) { drop_session(); v3_reset(); }
  set_load();
#ifdef V3_EE2
  if (ee_fix) ee_store();   /* the settings sector written anew from the second copy */
#endif
  lcd_take(); draw();
#ifdef V3_LINK
  plink_init();
#endif
  SYST_RVR = CORE_HZ / 1000u - 1u; SYST_CVR = 0; SYST_CSR = 7;
  __asm__ volatile("cpsie i");

  u32 emu_ms = g_ms, last_draw = g_ms, t_start = g_ms, wheel_ms = 0, last_in = g_ms, mk0 = 0, ee_ms = 0;
  int bl = 1, lock = 1;
#ifdef V3_PW
  u32 pw_hold = 0, pw_tq = 0; int pw_back = 0;   /* ms the run stays behind for a tone while dark; time from dark to catch up */
  g_pw_rtc = REG(0x45000304u); g_pw_rtcms = g_ms;
#endif
  g_sess_ms = g_ms;
#ifdef V3_CALL_WAKE
  int call_wake = 0;
#endif
  for (;;) {
    hw_wdt_feed();
    u32 now = g_ms;
#ifdef BAT_GUARD
    /* test21: battery empty: the EEPROM and the run state saved, then off until new batteries */
    { static u32 bt; if (now - bt >= 1000u) { bt = now; if (bat_guard_tick()) {
#ifdef V3_PW
        pw_full();
#endif
        if (v3_eeprom_dirty(1)) ee_store();
        save_session(); BAT_PIC_SHOW(); hw_backlight(0);
        for (;;) { hw_wdt_feed(); __asm__ volatile("wfi"); } } } }
#endif
    u32 b = hw_buttons();
    if (lock) { if (b) b = 0; else lock = 0; }
#ifdef V3_PW
    int pw_quiet = !bl && !b && !g_tone_now && !g_menu && !wheel_ms && !pw_hold
#ifdef V3_TONE_Q
      && tq_h == tq_w && !g_hw_tone_hz
#endif
#ifdef V3_LINK
      && !(g_pl.last_rx && now - g_pl.last_rx < 30000u)
#endif
      ;
    if (pw_quiet && (s32)(now - emu_ms) < 2500) {   /* after a deep sleep 1 s behind: caught up at 12 MHz */
#ifdef CLK_ECO
      if (!g_pw_low) hw_clk_shift(0, CORE_HZ);
#endif
      pw_low();
    } else pw_full();
    if (pw_quiet) pw_back = 1;
    { u32 r = REG(0x45000304u);                     /* when the RTC last stepped */
      if (r != g_pw_rtc) {
        if (g_pw_kw) { u32 t = g_pw_rtcms + (r - g_pw_rtc) * 1000u;   /* woken by a key: the part of a second slept */
          __asm__ volatile("cpsid i" ::: "memory"); if ((s32)(t - g_ms) > 0) g_ms = t; __asm__ volatile("cpsie i" ::: "memory");
          now = g_ms; g_pw_kw = 0; }
        g_pw_rtc = r; g_pw_rtcms = now; } }
#endif
#ifdef CLK_ECO
    /* test22: screen off, no key, no tone, no link traffic for 30 s: a lower clock */
#ifdef V3_PW
    if (!g_pw_low) {
#endif
#ifdef ECO_RTC
    (void)eco_rtc(now, CORE_HZ);   /* test27: a clock step whose ms tick runs slow is not used again */
#endif
    eco_poll(now, !bl && !b && !g_tone_now && !g_menu
#ifdef V3_LINK
      && !(g_pl.last_rx && now - g_pl.last_rx < 30000u)
#endif
      , (int)(now - emu_ms), CORE_HZ);
#ifdef V3_PW
    }
#endif
#endif
    if (b & BTN_WHEEL) {
      if (restored && now - t_start < 3000u) {
        int yes = hw_ask_reset(en());
        if (yes) { drop_session(); leave(0); }
        restored = 0; draw(); continue;
      }
      if (!wheel_ms) wheel_ms = now ? now : 1;
      else if (EXIT_NOW(now - wheel_ms, 1500u)) leave(1);
    } else if (wheel_ms) { if (now - wheel_ms < 1000u) { if (g_menu) menu_key(BTN_WHEEL); else { g_menu = 1;
#ifdef BAT_SHOW
        g_msel = 1;
#else
        g_msel = 0;
#endif
        g_mtop = 0; menu_items(); menu_draw(); } } wheel_ms = 0; }
    if (b) {
      last_in = now;
      if (!bl || g_dimmed) {                            /* the key only wakes */
        bl = 1; hw_backlight(1); if (g_dimmed) { g_dimmed = 0; colours(); }
#ifdef V3_PW
        pw_full(); lcd_take();
#endif
        if (g_menu) menu_draw(); else draw();
        while (hw_buttons()) hw_wdt_feed();
        continue;
      }
    }
    if (g_menu) { u32 k = b & (BTN_A | BTN_B | BTN_C), nk = k & ~mk0; mk0 = k; if (nk) menu_key(nk); v3_set_keys(0); }
    else { mk0 = b & (BTN_A | BTN_B | BTN_C); v3_set_keys(((b & BTN_A) ? 1u : 0u) | ((b & BTN_B) ? 2u : 0u) | ((b & BTN_C) ? 4u : 0u)); }
    /* run the toy up to now (at most 50 ms behind), 1 ms at a time */
#ifdef V3_PW
    /* dark: up to 5 s behind (the toy's time is run later, at once); after the screen comes on that is caught up first
     * (20 ms of run between the passes); a tone while dark: real time from there (pw_hold), so it sounds as it should */
    s32 lim = pw_back ? 5000 : 50;
    if ((s32)(now - emu_ms) > lim) { g_v3st[1] += now - (u32)lim - emu_ms; emu_ms = now - (u32)lim; }
    u32 t0 = g_ms, end = pw_hold ? now - pw_hold : now;
    while ((s32)(end - emu_ms) > 0 && g_ms - t0 < (pw_quiet ? 40u : 20u)) {
#else
    if ((s32)(now - emu_ms) > 50) { g_v3st[1] += now - 50u - emu_ms; emu_ms = now - 50u; }
    u32 t0 = g_ms;
    while ((s32)(now - emu_ms) > 0 && g_ms - t0 < 20u) {
#endif
      v3_run((uint64_t)(V3_CLOCK / 1000u) * V3_TICKS);
      emu_ms++; g_v3st[0]++;
#ifdef CLK_ECO
      ECO_CHECK(g_ms, g_ms - emu_ms, CORE_HZ);
#endif
#ifdef V3_LINK
      { u8 tx[32]; int n = v3_ir_tx(tx, sizeof tx); if (n) plink_send('V', tx, n); }
      plink_poll(g_ms, 0, lk_got);
#endif
      u32 hz = g_set.sound ? v3_tone_hz() : 0u;
#ifdef V3_VOL
      g_hw_vol = VOL_OF[g_set.sound & 3u];
#endif
#ifdef V3_TONE_Q
      if (hz != g_tone_now) { g_tone_now = hz; tq_put(hz, emu_ms); }
#else
      if (hz != g_tone_now) { g_tone_now = hz; hw_tone(hz); }
#endif
#ifdef V3_CALL_WAKE
      if (hz && !bl) call_wake = 1;   /* test21: the toy calls while dark: the screen comes on (as the other emulators) */
#endif
#ifdef V3_PW
      if (hz && !bl && !pw_hold && (s32)(now - emu_ms) > 1) { pw_hold = now - emu_ms; pw_full(); break; }
#endif
    }
#ifdef V3_TONE_Q
    tq_pump();
#endif
#ifdef V3_CALL_WAKE
    if (call_wake) { call_wake = 0;
      if (!bl) { bl = 1; last_in = now; hw_backlight(1); if (g_dimmed) { g_dimmed = 0; colours(); }
#ifdef V3_PW
        pw_full(); lcd_take();
#endif
        draw(); } }
#endif
#ifdef V3_PW
    if (g_tone_now) pw_tq = emu_ms;
    if (pw_hold && (bl || emu_ms - pw_tq > 2000u)) pw_hold = 0;   /* 2 s of the toy's time without a tone */
    if (pw_back && bl && (s32)(now - emu_ms) <= 1) pw_back = 0;
#endif
#ifdef V3_CLOCK
    /* the toy's clock: seconds read at 1 s and 2.5 s after the start; running: write the Paradise's time */
    { static int st = 0, s1, tries; static u32 t0; u32 a = now - t_start;
#ifdef V3_SETX
      if ((g_cfg.flags & 8u) && clk_auto() && g_para_time && st >= 0) {
#else
      if ((g_cfg.flags & 10u) == 10u && g_para_time && st >= 0) {
#endif
#ifdef V3_CLK_RETRY
        /* test37: a clock that stands (the toy started from its own save, it asks for the time) is looked at again every
         * 5 s for 10 minutes: once it was set in the game, the Paradise's time is written (reported: not set now and then) */
        if (st == 0 && a >= t0 + 1000u) { s1 = clk_running_val(); st = 1; }
        else if (st == 1 && a >= t0 + 2500u) {
          if (clk_running_val() != s1 && ram(0x85)[0] >= 1u && ram(0x85)[0] <= 12u) { clock_write(); st = 2; t0 = a; g_clk_diag = 1; }
          else if (a < 600000u) { st = 0; t0 = a + 2500u; g_clk_diag = 3; } else st = -1;
        }
        else if (st >= 2 && a >= t0 + (u32)(st - 1) * 2500u) { if (clock_ok()) { st = -1; g_clk_diag = 1; } else if (tries++ < 3) { clock_write(); st++; } else { st = -1; g_clk_diag = 4; } }
#else
        if (st == 0 && a >= 1000u) { s1 = clk_running_val(); st = 1; }
        else if (st == 1 && a >= 2500u) { if (clk_running_val() != s1 && ram(0x85)[0] >= 1u && ram(0x85)[0] <= 12u) { clock_write(); st = 2; } else st = -1; }
        else if (st >= 2 && a >= 2500u + (u32)(st - 1) * 2500u) { if (clock_ok()) st = -1; else if (tries++ < 3) { clock_write(); st++; } else st = -1; }
#endif
      }
#ifdef V3_CLK_RETRY
      if (!g_para_time) g_clk_diag = 2;
#endif
    }
#endif
    if (v3_eeprom_dirty(1)) ee_ms = now ? now : 1;    /* the game wrote its EEPROM */
    if (ee_ms && now - ee_ms > 3000u && now - last_in > 2000u) { ee_ms = 0; ee_store(); }
#ifdef V3_PW
    { static u32 lt; if (bl && now - lt >= 8u) { lt = now; if (lcd_take()) g_lcd_ms = now; } }   /* dark: nothing to show */
#else
    { static u32 lt; if (now - lt >= 8u) { lt = now; if (lcd_take()) g_lcd_ms = now; } }
#endif
    if (bl && !g_menu && g_lcd_ms && now - last_draw >= 33u) { last_draw = now; g_lcd_ms = 0; draw(); }
    if (bl && !g_dimmed && g_set.dim < T_N && now - last_in > T_MS[g_set.dim]) { g_dimmed = 1; colours(); if (g_menu) menu_draw(); else draw(); }
    if (bl && g_set.off < T_N && now - last_in > T_MS[g_set.off]) { bl = 0; hw_backlight(0); save_session(); g_sess_ms = now; }
    /* the run state every 15 minutes (quiet for 10 s) and when the screen goes off */
    if (now - g_sess_ms > 900000u && now - last_in > 10000u && !g_menu) { save_session(); g_sess_ms = now; }
#ifdef V3_PW
    if (emu_ms == g_ms) {
      if (pw_quiet && g_pw_low && !hw_buttons() && !ee_ms) {
        /* deep sleep until the next RTC second or a key; the ms tick then continues from the time of the last RTC step */
        if (pw_sleep()) { u32 r = REG(0x45000304u), t = g_pw_rtcms + (r - g_pw_rtc) * 1000u;
          __asm__ volatile("cpsid i" ::: "memory"); if ((s32)(t - g_ms) > 0) g_ms = t; __asm__ volatile("cpsie i" ::: "memory");
          g_pw_rtc = r; g_pw_rtcms = t; }
      } else __asm__ volatile("wfi");
    }
#else
    if (emu_ms == g_ms) __asm__ volatile("wfi");
#endif
  }
}

__attribute__((naked)) static void Default_Handler(void) { __asm__ volatile("1: b 1b\n"); }
__attribute__((naked)) static void Irq_Off(void) { __asm__ volatile("cpsid i\n bx lr\n"); }
__attribute__((section(".vectors"), used))
const void *const g_vectors[16 + 64] = { (void *)0x18020000u, (void *)Reset_Handler,
  [2 ... 10] = (void *)Default_Handler, [11] = (void *)Irq_Off, [12 ... 13] = (void *)Default_Handler, [14] = (void *)Irq_Off,
  [15] = (void *)SysTick_Handler, [16 ... 79] = (void *)Irq_Off
#ifdef V3_LINK
  , [16 + 37] = (void *)plink_irq
#endif
#ifdef V3_PW
  , [16 + 2] = (void *)pw_rtc_irq, [16 + 26] = (void *)pw_gpio_irq
#endif
};
