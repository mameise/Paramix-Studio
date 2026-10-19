/* V1Emu: the first Tamagotchis and the Digital Monster (Epson E0C6S46/E0C6S48, 4-bit) on the
 * Tamagotchi Paradise. Core: TamaLIB by Jean-Christophe Rona (GPL v2 or later), see ../core.
 * Started from the Paramix menu like the other further programs.
 * Keys: A, B, C as on the toy; wheel short = tap (vibration sensor of the Angel); wheel long = save
 * and back; wheel within 3 s after a resumed start = start over (asks first).
 * The screen: 32x16 dots four times enlarged, the eight icons above and below.
 * Author: mameise */
#include "../../../emu/src/emu/hw.h"
#include "../../../emu/src/emu/flash.h"
#include "../core/tamalib.h"
#include "../core/hal.h"
#include "../core/cpu.h"
#define PARA_ATTR
#include "../../../emu/src/emu/paraclock.h"
#ifdef PARA_BACK
#include "../../../emu/src/emu/paraback.h"
#endif

int hw_ask_reset(int en);
typedef int s32;

/* Settings block, filled in by the Studio (magic "PMX1"), at 0x140 in the program.
 * save_off: session (4 KB), then the settings of the device (4 KB). icons: the eight LCD icons of this toy
 * (16x16, bit 15 left), used when icons_ok is 1 (the Studio has a set for this ROM), else our own pictograms. */
typedef struct { u32 magic, version, rom_off, rom_len, save_off, flags; char name[16]; u32 icons_ok; u16 icons[8][16]; } V1Cfg;
#define V1_MAGIC 0x31584D50u   /* "PMX1" */
__attribute__((section(".pmxcfg"), used))
volatile const V1Cfg g_cfg = { V1_MAGIC, 1, 0, 0, 0, 0, {0}, 0, {{0}} };

#define SESSION (g_cfg.save_off)
#define SETTINGS (g_cfg.save_off + 0x1000u)
#include "font.h"
#define ST_MAGIC 0x54533156u   /* "V1ST" */

/* ---- time: SysTick 1 ms ---- */
static volatile u32 g_ms;
#define SYST_CSR REG(0xE000E010u)
#define SYST_RVR REG(0xE000E014u)
#define SYST_CVR REG(0xE000E018u)
#define CORE_HZ 96000000u
__attribute__((used)) void systick_c(void) { g_ms++; }
__attribute__((naked)) void SysTick_Handler(void) { __asm__ volatile("b systick_c\n"); }

/* ---- HAL for TamaLIB: virtual time in us, moved on by the core's own waits ---- */
static u32 g_vt;
static u8 g_mat[16][32], g_icon[8], g_dirty = 1;
static u32 g_freq, g_snd_on, g_sound = 1, g_tone_now, g_tone_since, g_tone_cut;
static void *h_malloc(u32_t s) { (void)s; return 0; }
static void h_free(void *p) { (void)p; }
static void h_halt(void) {}
static bool_t h_log_on(log_level_t l) { (void)l; return 0; }
static void h_log(log_level_t l, char *b, ...) { (void)l; (void)b; }
static void h_sleep_until(timestamp_t ts) { if ((s32)(ts - g_vt) > 0) g_vt = ts; }
static timestamp_t h_ts(void) { return g_vt; }
static void h_update(void) {}
static u32 g_lcd_vt;   /* 1.6.8: toy time of the last change on its LCD (a picture is shown once it holds still) */
static void h_mat(u8_t x, u8_t y, bool_t v) { if (x < 32 && y < 16 && g_mat[y][x] != v) { g_mat[y][x] = v; g_dirty = 1; g_lcd_vt = g_vt; } }
static void h_icon(u8_t i, bool_t v) { if (i < 8 && g_icon[i] != v) { g_icon[i] = v; g_dirty = 1; g_lcd_vt = g_vt; } }   /* test46: Digimon too (core hw.c: its icon segments) */
/* 1.6.8 test26: the buzzer only on a change (the game switches it on and off very often), and never longer
 * than 3 s in one go (no tone of these toys is that long; a stuck tone stops) */
static void snd_apply(void)
{
  u32 hz = g_snd_on && g_sound && g_freq && !g_tone_cut ? g_freq / 10u : 0u;
  if (hz == g_tone_now) return;
  if (hz && !g_tone_now) g_tone_since = g_ms;
  g_tone_now = hz; hw_tone(hz);
}
static void h_freq(u32_t f) { g_freq = f; if (g_snd_on) snd_apply(); }
static void h_play(bool_t en) { if (en && !g_snd_on) g_tone_cut = 0; g_snd_on = en; snd_apply(); }
static int h_handler(void) { return 0; }
static hal_t g_hal_v1 = { h_malloc, h_free, h_halt, h_log_on, h_log, h_sleep_until, h_ts, h_update, h_mat, h_icon, h_freq, h_play, h_handler };

/* ---- settings on the device (wheel short), like T7Emu ---- */
typedef struct { u8 magic[4]; u8 pal, grid, shadow, sound, dim, lang, icons, ghost, dimto, off, sync, r0; } V1Set;
#ifdef BL_PWM
static V1Set g_set = { {'V','1','S','2'}, 0, 1, 1, 1, 7, 0, 1, 4, 6, 3, 1, 0x80 };
#else
static V1Set g_set = { {'V','1','S','2'}, 0, 1, 1, 1, 7, 0, 1, 4, 1, 3, 1, 0 };
#endif   /* ghost: percent 0..20 */
static const u16 PAL[6][2] = { { 0xB5B2u, 0x18E3u }, { 0xC618u, 0x0000u }, { 0xFFFFu, 0x0000u }, { 0xEF34u, 0x4A00u }, { 0x9E7Fu, 0x000Fu }, { 0xFDF9u, 0x780Fu } };
static const char *const PAL_NM[2][6] = { { "LCD", "GRAU", "WEISS", "GELB", "BLAU", "PINK" }, { "LCD", "GREY", "WHITE", "YELLOW", "BLUE", "PINK" } };
#define T_N 7   /* 10 s ... 10 min, index T_N = never */
static const u32 T_MS[T_N] = { 10000u, 20000u, 30000u, 60000u, 120000u, 300000u, 600000u };
static const char *const T_NM[2][T_N + 1] = { { "10 S", "20 S", "30 S", "1 MIN", "2 MIN", "5 MIN", "10 MIN", "NIE" }, { "10 S", "20 S", "30 S", "1 MIN", "2 MIN", "5 MIN", "10 MIN", "NEVER" } };
static const u8 DIM_PCT[4] = { 20u, 40u, 60u, 80u };
#ifdef BL_PWM
/* 1.6.8 (test45): r0 bit 7 = this layout, bits 0-3 = the light in general (0 = 100 %, then 5, 10, 20 .. 90 %),
 * dimto = dim level 1 .. 50 % (10 steps) */
#define LV_N 11
#define DIM2_N 10
static const u8 LV_PCT[LV_N] = { 100u, 5u, 10u, 20u, 30u, 40u, 50u, 60u, 70u, 80u, 90u };
static const u8 DIM2_PCT[DIM2_N] = { 1u, 2u, 3u, 5u, 10u, 15u, 20u, 30u, 40u, 50u };
#endif
static int en(void) { return g_set.lang ? g_set.lang == 2u : (int)(g_cfg.flags & 1u); }
static u16 mix(u16 a, u16 b, int p)   /* p percent of b */
{
  int r = ((a >> 11) * (100 - p) + (b >> 11) * p) / 100, g = (((a >> 5) & 63) * (100 - p) + ((b >> 5) & 63) * p) / 100, bl = ((a & 31) * (100 - p) + (b & 31) * p) / 100;
  return (u16)((r << 11) | (g << 5) | bl);
}
static u16 BG, INK, OFF, SHD;
static int g_dimmed;
static void colours(void)
{
  u32 k = g_set.pal < 6u ? g_set.pal : 0u;
  BG = PAL[k][0]; INK = PAL[k][1]; OFF = mix(BG, INK, g_set.ghost); SHD = g_set.shadow ? mix(BG, INK, 8) : BG;
#ifdef BL_PWM
  { u32 l = LV_PCT[(g_set.r0 & 15u) % LV_N], d = DIM2_PCT[g_set.dimto % DIM2_N];
    hw_bl_set(g_dimmed ? (d < l ? d : l) : l); }   /* 1.6.8: the light, as the Paradise does it */
#else
  if (g_dimmed) { int p = 100 - DIM_PCT[g_set.dimto & 3u]; BG = mix(BG, 0, p); INK = mix(INK, 0, p); OFF = mix(OFF, 0, p); SHD = mix(SHD, 0, p); }
#endif
}
static void set_load(void)
{
  const volatile u8 *p = XIP_PTR(SETTINGS); V1Set t; u8 *d = (u8 *)&t;
  for (u32 i = 0; i < sizeof t; i++) d[i] = p[i];
  if (t.magic[0] == 'V' && t.magic[1] == '1' && t.magic[2] == 'S' && t.magic[3] == '2' && t.pal < 6u && t.dim <= T_N && t.off <= T_N && t.lang < 3u) { g_set = t; if (g_set.ghost > 20u) g_set.ghost = 4u; }
  else if (t.magic[0] == 'V' && t.magic[1] == '1' && t.magic[2] == 'S' && t.magic[3] == 'E' && t.pal < 6u) {   /* test23..25 */
    static const u8 OLD_OFF[5] = { 2, 3, 4, 5, T_N };
    g_set.pal = t.pal; g_set.grid = t.grid & 1u; g_set.shadow = t.shadow & 1u; g_set.sound = t.sound & 1u; g_set.off = OLD_OFF[t.dim < 5u ? t.dim : 1u];
  }
#ifdef BL_PWM
  if (!(g_set.r0 & 0x80u)) { static const u8 m[4] = { 6, 8, 9, 9 }; g_set.dimto = m[g_set.dimto & 3u]; g_set.r0 = 0x80u; }   /* was 20, 40, 60, 80 % */
#endif
  colours();
}
static void set_store(void)
{
  static u8 sec[0x1000] __attribute__((aligned(4)));
  const volatile u8 *p = XIP_PTR(SETTINGS); const u8 *s = (const u8 *)&g_set; u32 same = 1;
  for (u32 i = 0; i < sizeof g_set; i++) if (p[i] != s[i]) { same = 0; break; }
  if (same) return;
  for (u32 i = 0; i < sizeof sec; i++) sec[i] = i < sizeof g_set ? s[i] : 0xFF;
  hw_tone(0); g_tone_now = 0; flash_update4k(SETTINGS, sec);
}
static u16 g_row[128] __attribute__((aligned(4)));
/* Eight simple pictograms of our own (16x16), one per icon position */
static const u16 g_ico[8][16] = {
  { 0x0000,0x0000,0x2494,0x2494,0x2494,0x3FFC,0x1FF8,0x0FF0,0x0180,0x0180,0x0180,0x0180,0x0180,0x0180,0x07E0,0x0000 },  /* fork: food */
  { 0x0000,0x07E0,0x0FF0,0x1FF8,0x1FF8,0x1FF8,0x1FF8,0x0FF0,0x07E0,0x03C0,0x03C0,0x07E0,0x07E0,0x03C0,0x0000,0x0000 },  /* bulb: light */
  { 0x0000,0x07E0,0x1818,0x2004,0x2664,0x4662,0x4002,0x4002,0x4812,0x4422,0x23C4,0x2004,0x1818,0x07E0,0x0000,0x0000 },  /* face: game */
  { 0x0000,0x0180,0x0180,0x0180,0x0180,0x0180,0x7FFE,0x7FFE,0x0180,0x0180,0x0180,0x0180,0x0180,0x0180,0x0000,0x0000 },  /* cross: medicine */
  { 0x0000,0x0080,0x0180,0x03C0,0x07E0,0x0FF0,0x1FF8,0x1FF8,0x3FFC,0x3FFC,0x3FFC,0x1FF8,0x1FF8,0x07E0,0x0000,0x0000 },  /* drop: bath */
  { 0x0000,0x0000,0x0018,0x0018,0x0198,0x0198,0x1998,0x1998,0x1998,0x1998,0x1998,0x1998,0x7FFE,0x0000,0x0000,0x0000 },  /* bars: status */
  { 0x0000,0x0040,0x00E0,0x01F0,0x03F8,0x07FC,0x00E0,0x00E0,0x00E0,0x00E0,0x00E0,0x00E0,0x00E0,0x01F0,0x0000,0x0000 },  /* arrow: discipline */
  { 0x0000,0x0180,0x03C0,0x07E0,0x0FF0,0x0FF0,0x0FF0,0x0FF0,0x1FF8,0x1FF8,0x3FFC,0x7FFE,0x0000,0x0180,0x0000,0x0000 },  /* bell: attention */
};
#define ROT ((g_cfg.flags & 32u) != 0u)
static void draw(int all)
{
  if (!g_dirty && !all) return;
  g_dirty = 0;
  hw_px_begin(0, 0, 128, 128);   /* end exclusive */
  int gap = g_set.grid ? 3 : 4;  /* grid: one line between the dots */
  for (int y = 0; y < 128; y++) {
    for (int x = 0; x < 128; x++) g_row[x] = BG;
    if (y >= 34 && y < 98) {                                   /* the dots, four times enlarged */
      int r = (y - 34) >> 2, sub = (y - 34) & 3;
      const u8 *mr = ROT ? g_mat[15 - r] : g_mat[r];   /* Digimon: the LCD sits the other way round (flags bit 5) */
      if (sub < gap) for (int c = 0; c < 32; c++) { u16 v = (ROT ? mr[31 - c] : mr[c]) ? INK : SHD; u16 *p = g_row + c * 4; p[0] = p[1] = p[2] = v; if (gap == 4) p[3] = v; }
    } else if (g_set.icons) {
      int top = y < 34, iy = top ? y - 9 : y - 103;            /* icons 16 px high at y 9 and 103 */
      if (iy >= 0 && iy < 16) for (int k = 0; k < 4; k++) {
        int i = top ? k : 4 + k, on = (g_cfg.flags & 64u) ? g_icon[i] : g_icon[ROT ? 7 - i : i];   /* the picture of place i, lit by the toy's icon (turned: 7 - i); Digimon: core hw.c numbers its icons by place */
        u16 bits = g_cfg.icons_ok ? g_cfg.icons[i][iy] : g_ico[i][iy], col = on ? INK : OFF;
        if (!on && !g_set.ghost) continue;
        for (int b = 0; b < 16; b++) if (bits & (0x8000u >> b)) g_row[8 + k * 32 + b] = col;
      }
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
enum { M_PAL, M_GRID, M_SHD, M_ICO, M_GHOST, M_SND, M_LIGHT, M_DIM, M_DIMTO, M_OFF, M_SYNC, M_LANG, M_N };   /* shake: turn the wheel (1.6.8 test30) */
static int g_items[M_N], g_nitems;
static void menu_items(void)
{
  g_nitems = 0;
  for (int k = 0; k < M_N; k++) {
    if (k == M_GHOST && !g_set.icons) continue;
    if (k == M_DIMTO && g_set.dim >= T_N) continue;
#ifndef BL_PWM
    if (k == M_LIGHT) continue;
#endif
    if (k == M_SYNC && !(g_cfg.flags & 8u)) continue;   /* only where V1Emu knows the toy's clock */
    g_items[g_nitems++] = k;
  }
  if (g_msel >= g_nitems) g_msel = g_nitems - 1;
}
static void pct(char *o, int v) { int n = 0; if (v >= 100) o[n++] = '1'; if (v >= 10) o[n++] = (char)('0' + (v / 10) % 10); o[n++] = (char)('0' + v % 10); o[n++] = '%'; o[n] = 0; }
static void menu_line(int it, const char **a, char *b)
{
  int e = en(); const char *v = 0; b[0] = 0;
  const char *on = e ? "ON" : "AN", *off = e ? "OFF" : "AUS";
  switch (it) {
    case M_PAL: *a = e ? "COLOUR" : "FARBE"; v = PAL_NM[e][g_set.pal % 6u]; break;
    case M_GRID: *a = e ? "GRID" : "RASTER"; v = g_set.grid ? on : off; break;
    case M_SHD: *a = e ? "SHADOW" : "SCHATTEN"; v = g_set.shadow ? on : off; break;
    case M_ICO: *a = e ? "ICONS" : "SYMBOLE"; v = g_set.icons ? on : off; break;
    case M_GHOST: *a = e ? "ICONS UNLIT" : "SYMBOLE AUS"; if (g_set.ghost) pct(b, g_set.ghost); else v = e ? "HIDDEN" : "UNSICHTB."; break;
    case M_SND: *a = e ? "SOUND" : "TON"; v = g_set.sound ? on : off; break;
    case M_DIM: *a = e ? "DIM AFTER" : "DIMMEN NACH"; v = T_NM[e][g_set.dim <= T_N ? g_set.dim : T_N]; break;
#ifdef BL_PWM
    case M_LIGHT: *a = e ? "LIGHT" : "LICHT"; pct(b, LV_PCT[(g_set.r0 & 15u) % LV_N]); break;
    case M_DIMTO: *a = e ? "DIM TO" : "DIMMEN AUF"; pct(b, DIM2_PCT[g_set.dimto % DIM2_N]); break;
#else
    case M_DIMTO: *a = e ? "DIM TO" : "DIMMEN AUF"; pct(b, DIM_PCT[g_set.dimto & 3u]); break;
#endif
    case M_OFF: *a = e ? "SCREEN OFF" : "BILD AUS NACH"; v = T_NM[e][g_set.off <= T_N ? g_set.off : T_N]; break;
    case M_SYNC: *a = e ? "CLOCK" : "UHR"; v = g_set.sync ? "AUTO" : (e ? "GAME" : "SPIEL"); break;
    default: *a = e ? "LANGUAGE" : "SPRACHE"; v = e ? "ENGLISH" : "DEUTSCH"; break;
  }
  if (v) { int n = 0; while (v[n] && n < 11) { b[n] = v[n]; n++; } b[n] = 0; }
}
#define ROWS 9
static void menu_draw(void)
{
  if (g_msel < g_mtop) g_mtop = g_msel;
  if (g_msel >= g_mtop + ROWS) g_mtop = g_msel - ROWS + 1;
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
    if (g_mtop > 0 && y >= 4 && y < 8) for (int x = 118 - (y - 4); x <= 118 + (y - 4); x++) g_row[x] = OFF;              /* more above */
    if (g_mtop + ROWS < g_nitems && y >= 4 && y < 8) for (int x = 108 - (7 - y); x <= 108 + (7 - y); x++) g_row[x] = OFF; /* more below */
    text(4, 118, en() ? "A NEXT B SET C OK" : "A ZEILE B SETZEN C OK", OFF, g_row, y);
    hw_px_row(g_row, 128);
  }
  hw_px_end();
}
static u32 g_tap_until, g_enc;
static int g_set_dirty;
static void menu_close(u32 now, int tap)
{
  (void)now; (void)tap; g_menu = 0;
  if (g_set_dirty) { set_store(); g_set_dirty = 0; }
  draw(1);
}
static void menu_key(u32 k, u32 now)
{
  if (k & BTN_A) g_msel = (g_msel + 1) % g_nitems;
  if (k & BTN_B) {
    switch (g_items[g_msel]) {
      case M_PAL: g_set.pal = (u8)((g_set.pal + 1u) % 6u); break;
      case M_GRID: g_set.grid ^= 1u; break;
      case M_SHD: g_set.shadow ^= 1u; break;
      case M_ICO: g_set.icons ^= 1u; break;
      case M_GHOST: g_set.ghost = (u8)(g_set.ghost >= 20u ? 0u : g_set.ghost + 1u); break;   /* 1% steps up to 20% */
      case M_SND: g_set.sound ^= 1u; g_sound = g_set.sound; snd_apply(); break;
      case M_DIM: g_set.dim = (u8)((g_set.dim + 1u) % (T_N + 1u)); if (g_set.dim < T_N && g_set.off < T_N && g_set.off <= g_set.dim) g_set.off = (u8)(g_set.dim + 1u); break;
#ifdef BL_PWM
      case M_LIGHT: g_set.r0 = (u8)(0x80u | (((g_set.r0 & 15u) + 1u) % LV_N)); colours(); break;
      case M_DIMTO: g_set.dimto = (u8)((g_set.dimto + 1u) % DIM2_N); break;
#else
      case M_DIMTO: g_set.dimto = (u8)((g_set.dimto + 1u) & 3u); break;
#endif
      case M_OFF: do g_set.off = (u8)((g_set.off + 1u) % (T_N + 1u)); while (g_set.off < T_N && g_set.dim < T_N && g_set.off <= g_set.dim); break;
      case M_SYNC: g_set.sync ^= 1u; break;
      default: g_set.lang = (u8)(en() ? 1u : 2u); break;
    }
    g_set_dirty = 1; colours(); menu_items();
  }
  if (k & (BTN_C | BTN_WHEEL)) { menu_close(now, 0); return; }
  menu_draw();
}

/* ---- program and session ---- */
static u12_t g_prog[8192];
static u8 g_sec[0x1000] __attribute__((aligned(4)));
static u32 crc_rom(void) { u32 h = 2166136261u; for (u32 i = 0; i < 8192u; i++) h = (h ^ g_prog[i]) * 16777619u; return h; }
static void save_session(void)
{
  for (u32 i = 0; i < sizeof g_sec; i++) g_sec[i] = 0xFF;
  u32 *w = (u32 *)g_sec; w[0] = ST_MAGIC; w[1] = crc_rom(); w[2] = g_vt;
  u32 n = cpu_v1_save(g_sec + 16); w[3] = n;
  hw_tone(0); g_tone_now = 0;
  flash_update4k(SESSION, g_sec);
}
static int load_session(void)
{
  const volatile u8 *s = XIP_PTR(SESSION);
  for (u32 i = 0; i < sizeof g_sec; i++) g_sec[i] = s[i];
  u32 *w = (u32 *)g_sec;
  if (w[0] != ST_MAGIC || w[1] != crc_rom() || w[3] == 0 || w[3] > sizeof g_sec - 16) return 0;
  g_vt = w[2];
  cpu_v1_load(g_sec + 16);
  return 1;
}
#ifdef SESS_ONCE
static u32 g_sess_ms;
#endif
static void drop_session(void) { if (!flash_isff(SESSION, 0x1000u)) flash_erase4k(SESSION); }
static void leave(int save)
{
  hw_tone(0);
  set_store();
  REG(0x20000FF8u) = 0;                  /* start counter of the home hooks */
  REG(0x20000FFCu) = 0x4C584D50u;        /* "PMXL": back on purpose, the autostart stays off */
  if (save) save_session();
#ifdef PARA_BACK
  para_back_fix(g_sec);                  /* 1.6.8: the Paradise skips the time spent here */
#endif
  hw_bl_pwm_off();
  for (u32 t = 0; hw_buttons() && t < 20000000u; t++) if ((t & 0xFFFFu) == 0) hw_wdt_feed();
  hw_back_to_paradise();
}

/* ---- the toy's clock (1.6.8 test26) ----
 * Tamagotchi P1, P2, Angel and Morino keep the time at RAM nibble 0x10: seconds (BCD, ones then tens),
 * minutes (BCD), hours (binary, 0..23). The Studio sets flags bit 3 for these ROMs. Bit 1: take the
 * Paradise's time (per program, tab Emulator). The time away is not played through. On a fresh start
 * the clock first has to be set in the game (Angel: or it never comes down); V1Emu does that with the
 * toy's own keys (B, B, A, C) and then writes the time. */
static u32 g_para_time, g_para_cnt;
static int clock_on(void) { return (g_cfg.flags & 10u) == 10u && g_set.sync && g_para_time; }
/* where the toy keeps its clock: the Tamagotchis at 0x10 (hours binary 0..23), Digimon V1/V2 at 0x1A (1.6.8 test36:
 * hours 0..11 at 0x1E, 0x1F = PM; 0x46 was a second counter of the game, not the clock). Both stand until the
 * time was set once in the game. */
#define DIGI ((g_cfg.flags & 64u) != 0u)
#define CB (DIGI ? 0x1Au : 0x10u)
#define NEEDSET 1
static void clock_write(void)
{
  u32 t = g_para_time + (REG(0x45000304u) - g_para_cnt), d = t % 86400u, h = d / 3600u, m = d / 60u % 60u, sec = d % 60u;
  cpu_v1_poke(CB, (u4_t)(sec % 10u)); cpu_v1_poke(CB + 1, (u4_t)(sec / 10u));
  cpu_v1_poke(CB + 2, (u4_t)(m % 10u)); cpu_v1_poke(CB + 3, (u4_t)(m / 10u));
  if (DIGI) { cpu_v1_poke(CB + 4, (u4_t)(h % 12u ? h % 12u : 12u)); cpu_v1_poke(CB + 5, (u4_t)(h >= 12u)); }   /* 1..12, 0 = never set */
  else { cpu_v1_poke(CB + 4, (u4_t)(h & 15u)); cpu_v1_poke(CB + 5, (u4_t)(h >> 4)); }
}
static u32 clock_sec(void) { return cpu_v1_peek(CB) | (u32)cpu_v1_peek(CB + 1) << 4; }
/* keys the program presses itself (Tamagotchis): B, B, B, A, C (wake, clock, clock setting, one hour so the clock
 * starts, set), 3 s after the start; then the time is written and checked (a second try if the clock stands) */
#define NSET (DIGI ? 2 : 5)
static const u32 SETKEYS_T[5] = { BTN_B, BTN_B, BTN_B, BTN_A, BTN_C };
static const u32 SETKEYS_D[2] = { BTN_B, BTN_C };   /* Digimon: B shows the clock setting, C takes it (then the clock runs) */
#define SETKEYS (DIGI ? SETKEYS_D : SETKEYS_T)
static int g_auto_k = -1; static u32 g_auto_at, g_auto_b, g_auto_until;

static void keys(u32 b)
{
  static u32 last = 0xFFFFFFFFu;
  if (b == last) return;
  last = b;
  thw_set_button(BTN_LEFT, (b & BTN_A) ? BTN_STATE_PRESSED : BTN_STATE_RELEASED);
  thw_set_button(BTN_MIDDLE, (b & BTN_B) ? BTN_STATE_PRESSED : BTN_STATE_RELEASED);
  thw_set_button(BTN_RIGHT, (b & BTN_C) ? BTN_STATE_PRESSED : BTN_STATE_RELEASED);
  thw_set_button(BTN_TAP, (b & 0x100u) ? BTN_STATE_PRESSED : BTN_STATE_RELEASED);
}

extern u32 __bss_start, __bss_end;
void Reset_Handler(void)
{
  __asm__ volatile("cpsid i");
  /* the Paradise's clock, before its SRAM is overwritten; started from the menu: its mailbox */
  u32 cnt = REG(0x45000304u), para = para_clock(cnt, 0, 0x1F000u, 0, 0);
  if (!para) para = para_mailbox(cnt);
#ifdef PARA_BACK
  para_leave_mark(cnt, para);            /* 1.6.8: the Paradise does not live meanwhile */
#endif
  __asm__ volatile("" ::: "memory");
  for (u32 *p = &__bss_start; p < &__bss_end; p++) *p = 0;
  { extern u32 __data_start, __data_end, __data_load;
    const u32 *src = &__data_load; for (u32 *p = &__data_start; p < &__data_end; p++) *p = *src++; }
  g_para_time = para; g_para_cnt = cnt;
  hw_init();
  hw_tone(0);
  /* the ROM: 12-bit words, two bytes each, high byte first */
  { const volatile u8 *r = XIP_PTR(g_cfg.rom_off); u32 n = g_cfg.rom_len / 2u; if (n > 8192u) n = 8192u;
    for (u32 i = 0; i < n; i++) g_prog[i] = (u12_t)(((u32)r[2 * i] << 8 | r[2 * i + 1]) & 0xFFFu); }
  tamalib_register_hal(&g_hal_v1);
  { extern int thw_digi, cpu_kmask_fix; thw_digi = DIGI; cpu_kmask_fix = DIGI; }   /* test46: Digimon takes its keys by interrupt */   /* 1.6.8: Digimon's own LCD wiring */
  tamalib_init(g_prog, 0, 1000000u);
  int restored = load_session();
  set_load(); g_sound = g_set.sound;
  u32 clk_chk = 0, clk_s0 = 0xFFu, clk_try = 0;
  if (clock_on()) {
    if (restored) { clock_write(); clk_chk = 2500u; }
    else if (NEEDSET) { g_auto_k = 0; g_auto_at = 3000u; }
  }
  draw(1);
  SYST_RVR = CORE_HZ / 1000u - 1u; SYST_CVR = 0; SYST_CSR = 7;
  __asm__ volatile("cpsie i");

  g_enc = REG(0x4001A000u) & 3u;
  u32 emu_ms = g_ms, last_draw = g_ms, t_start = g_ms, wheel_ms = 0, last_in = g_ms, mk0 = 0;
  int bl = 1, lock = 1;
  for (;;) {
    hw_wdt_feed();
    u32 now = g_ms;
    u32 b = hw_buttons();
    if (lock) { if (b) b = 0; else lock = 0; }         /* wait until the keys of the start are released */
    if (b & BTN_WHEEL) {
      if (restored && now - t_start < 3000u) {
        int yes = hw_ask_reset(en());
        if (yes) { drop_session(); leave(0); }
        restored = 0; draw(1); continue;
      }
      if (!wheel_ms) wheel_ms = now ? now : 1;
      else if (now - wheel_ms > 1500u) leave(1);
    } else if (wheel_ms) { if (now - wheel_ms < 1000u) { if (g_menu) menu_key(BTN_WHEEL, now); else { g_menu = 1; g_msel = 0; g_mtop = 0; menu_items(); menu_draw(); } } wheel_ms = 0; }
    /* turning the wheel shakes the toy (vibration sensor of the Angel and others, K03): any step of the encoder */
    { u32 n = REG(0x4001A000u) & 3u; if (n != g_enc) { g_enc = n; if (!g_menu) g_tap_until = now + 120u; last_in = now; } }
    if (b) {
      last_in = now;
      if (!bl || g_dimmed) {                            /* the key only wakes */
        bl = 1; hw_backlight(1); if (g_dimmed) { g_dimmed = 0; colours(); }
        if (g_menu) menu_draw(); else draw(1);
        while (hw_buttons()) hw_wdt_feed();
        continue;
      }
    }
    /* clock setting by the program itself on a fresh start (one key every 400 ms, held 150 ms) */
    if (g_auto_k >= 0 && now - t_start >= g_auto_at) {
      /* Digimon reads its keys only twice a second: held 600 ms, 1.2 s apart; a valid time (hour 1..12) before B and C */
      if (g_auto_k == 0 && DIGI) clock_write();
      if (g_auto_k < NSET) { g_auto_b = SETKEYS[g_auto_k++]; g_auto_until = now + (DIGI ? 600u : 150u); g_auto_at = now - t_start + (DIGI ? 1200u : 400u); }
      else { clock_write(); g_auto_k = -1; clk_chk = now - t_start + 2500u; clk_s0 = 0xFFu; }
    }
    /* does the toy's clock run? (a clock never set in the game stands still) If not: set it with the toy's keys, twice at most */
    if (clk_chk) {
      if (clk_s0 == 0xFFu && now - t_start >= clk_chk - 2000u) clk_s0 = clock_sec();
      if (now - t_start >= clk_chk) { clk_chk = 0; if (clock_sec() == clk_s0 && NEEDSET && clk_try++ < 2) { g_auto_k = 0; g_auto_at = now - t_start + 100u; } }
    }
    u32 ab = (g_auto_b && (s32)(g_auto_until - now) > 0) ? g_auto_b : 0u;
    if (g_menu) {                                       /* the toy runs on, its keys go to the menu */
      u32 k = b & (BTN_A | BTN_B | BTN_C), nk = k & ~mk0; mk0 = k;
      if (nk) menu_key(nk, now);
      keys(ab);
    } else { mk0 = b & (BTN_A | BTN_B | BTN_C); keys((b & (BTN_A | BTN_B | BTN_C)) | ab | ((s32)(g_tap_until - now) > 0 ? 0x100u : 0u)); }
    /* run the toy up to now (at most 50 ms behind) */
    if ((s32)(now - emu_ms) > 50) emu_ms = now - 50u;
    u32 t0 = g_ms;
    while ((s32)(now - emu_ms) > 0 && g_ms - t0 < 20u) {
      u32 target = g_vt + 1000u;
      while ((s32)(target - g_vt) > 0) { u32 v0 = g_vt; tamalib_step(); if (g_vt == v0) g_vt += 31u; }
      emu_ms++;
    }
    if (g_tone_now && now - g_tone_since > 3000u) { g_tone_cut = 1; snd_apply(); }   /* a stuck tone stops */
    /* 1.6.8: draw a picture only once the toy has finished it (no change for 8 ms of toy time, at the latest after
     * 150 ms): drawn in the middle of an update, the clock looked broken and icons flickered */
    { static u32 dirty_ms; if (!g_dirty) dirty_ms = 0; else if (!dirty_ms) dirty_ms = now ? now : 1;
      if (bl && !g_menu && now - last_draw >= 33u && (g_vt - g_lcd_vt >= 8000u || (dirty_ms && now - dirty_ms > 150u))) { last_draw = now; draw(0); } }
    /* dim, then screen off, without a key */
    if (bl && !g_dimmed && g_set.dim < T_N && now - last_in > T_MS[g_set.dim]) { g_dimmed = 1; colours(); if (g_menu) menu_draw(); else draw(1); }
    if (bl && g_set.off < T_N && now - last_in > T_MS[g_set.off]) { bl = 0; hw_backlight(0);
#ifdef SESS_ONCE
      if (g_auto_k < 0 && !clk_chk) { save_session(); g_sess_ms = now; }
#endif
    }
#ifdef SESS_ONCE
    /* 1.6.8: the toy has no save of its own; the session is written every 15 minutes (quiet for 10 s) and when the
     * screen goes off, so a battery change takes back at most that much */
    if (now - g_sess_ms > 900000u && now - last_in > 10000u && g_auto_k < 0 && !clk_chk && !g_menu) { save_session(); g_sess_ms = now; }
#endif
    if (emu_ms == g_ms) __asm__ volatile("wfi");
  }
}

__attribute__((naked)) static void Default_Handler(void) { __asm__ volatile("1: b 1b\n"); }
__attribute__((naked)) static void Irq_Off(void) { __asm__ volatile("cpsid i\n bx lr\n"); }
__attribute__((section(".vectors"), used))
const void *const g_vectors[16 + 64] = { (void *)0x18020000u, (void *)Reset_Handler,
  [2 ... 10] = (void *)Default_Handler, [11] = (void *)Irq_Off, [12 ... 13] = (void *)Default_Handler, [14] = (void *)Irq_Off,
  [15] = (void *)SysTick_Handler, [16 ... 79] = (void *)Irq_Off };
