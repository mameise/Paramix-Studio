/* ArduboyEmu: the Arduboy on the Tamagotchi Paradise, a further program of the Paramix menu. Bring your own games
 * (.hex). Core: own ATmega32U4 emulation (../core/avr.c).
 * Screen: the Arduboy's 128 x 64 at 1:1, 3:2 or 2:1 height (setting PICTURE); greys that games make by switching two
 * pictures come out as grey. Keys: set in the Studio (tab Arduboy). A+B+C together: settings; wheel held 2 s: save and
 * back to the Paradise. Sound: the speaker's tone on the Paradise's buzzer. Save: the EEPROM (1 KB) goes to the flash a
 * few seconds after the game wrote it, and on leaving; on leaving the whole state too, the next start continues there.
 * Author: mameise */
#include "../../../emu/src/emu/hw.h"
#include "../../../emu/src/emu/exitkey.h"
#include "../../../emu/src/emu/flash.h"
#define PARA_ATTR
#include "../../../emu/src/emu/paraclock.h"
#ifdef PARA_BACK
#include "../../../emu/src/emu/paraback.h"
#ifdef GPLAY
#define GP_POINTS
#include "../../../emu/src/emu/gplay.h"
#endif
#endif
#include "../core/avr.h"
#include "font.h"

/* ---- settings block at 0x140, filled in by the Studio ----
 * keys[i]: Arduboy buttons (bit 0 UP, 1 DOWN, 2 LEFT, 3 RIGHT, 4 A, 5 B) for the Paradise input i: 0 A, 1 B, 2 C,
 * 3 wheel press, 4 wheel up, 5 wheel down, 6 A+C, 7 B+C, 8 A+B. flags: bit 0 English, bit 1 wheel the other way round.
 * save_off: 4 KB settings of the device, 4 KB EEPROM (1 KB used), then the state (up to 8 KB). */
#define NIN 9
typedef struct { u32 magic, version, rom_off, rom_len, save_off, flags; char name[16]; u8 keys[12]; } AbCfg;
#define AB_MAGIC 0x50584241u   /* "ABXP" */
__attribute__((section(".pmxcfg"), used))
volatile const AbCfg g_cfg = { AB_MAGIC, 1, 0, 0, 0, 0, {0}, { AB_LEFT, AB_B, AB_RIGHT, AB_A, AB_UP, AB_DOWN, 0, 0, 0 } };
#define SETTINGS (g_cfg.save_off)
#define EEP_AT (g_cfg.save_off + 0x1000u)
#define STATE_AT (g_cfg.save_off + 0x2000u)
#define STATE_MAX 0x2000u
#define XIP_BASE_REG REG(0x4002F004u)
#define XIP_CTRL_REG REG(0x4002F000u)
#define ROMWIN ((const u16 *)0x10000000u)

/* ---- time: SysTick 1 ms; the wheel's encoder is read here too ---- */
static volatile u32 g_ms;
static volatile s32 g_enc_acc; static u32 g_enc_last;
static const signed char QD[16] = { 0, -1, 1, 0, 1, 0, 0, -1, -1, 0, 0, 1, 0, 1, -1, 0 };
#define GPIO2 0x4001A000u
#define SYST_CSR REG(0xE000E010u)
#define SYST_RVR REG(0xE000E014u)
#define SYST_CVR REG(0xE000E018u)
#define CORE_HZ 96000000u
__attribute__((used)) void systick_c(void)
{
  g_ms++;
  u32 n = REG(GPIO2) & 3u;
  if (n != g_enc_last) { g_enc_acc += QD[(g_enc_last << 2) | n]; g_enc_last = n; }
}
__attribute__((naked)) void SysTick_Handler(void) { __asm__ volatile("b systick_c\n"); }
void abort(void) { for (;;) hw_wdt_feed(); }

/* ---- settings on the device ---- */
typedef struct { u8 magic[4]; u8 pal, dim, lang, dimto, off, r0, size, mute; } AbSet;   /* mute: 0 = sound on */
#ifndef AB_SIZE0
#define AB_SIZE0 0   /* 1.7.3 test10: picture size SMALL by default (1:1, not stretched); 1 = MEDIUM as before */
#endif
static AbSet g_set = { {'A','B','S','1'}, 0, 7, 0, 6, 3, 0x80, AB_SIZE0, 0 };
/* background, dot: white, amber, cyan (as the OLED), paper (black on white) */
static const u16 PAL[4][2] = { { 0x0000u, 0xFFFFu }, { 0x0000u, 0xFDA0u }, { 0x0000u, 0x5FFFu }, { 0xEF5Du, 0x10A2u } };
static const char *const PAL_NM[2][4] = { { "WEISS", "GELB", "BLAU", "PAPIER" }, { "WHITE", "AMBER", "CYAN", "PAPER" } };
static const char *const SIZE_NM[2][3] = { { "KLEIN", "MITTEL", "GROSS" }, { "SMALL", "MEDIUM", "LARGE" } };
#define T_N 7
static const u32 T_MS[T_N] = { 10000u, 20000u, 30000u, 60000u, 120000u, 300000u, 600000u };
static const char *const T_NM[2][T_N + 1] = { { "10 S", "20 S", "30 S", "1 MIN", "2 MIN", "5 MIN", "10 MIN", "NIE" }, { "10 S", "20 S", "30 S", "1 MIN", "2 MIN", "5 MIN", "10 MIN", "NEVER" } };
#define LV_N 11
#define DIM2_N 10
static const u8 LV_PCT[LV_N] = { 100u, 5u, 10u, 20u, 30u, 40u, 50u, 60u, 70u, 80u, 90u };
static const u8 DIM2_PCT[DIM2_N] = { 1u, 2u, 3u, 5u, 10u, 15u, 20u, 30u, 40u, 50u };
static int en(void) { return g_set.lang ? g_set.lang == 2u : (int)(g_cfg.flags & 1u); }
static u16 BG, INK, OFF;
static u16 g_lut[3];   /* dot off, grey, on (with the game's contrast) */
static u32 g_contrast = 0x100u;
static int g_dimmed, g_redraw;
static u16 mix(u16 a, u16 b, int p, int q)   /* a + (b - a) * p / q */
{
  int r = (a >> 11) + (((b >> 11) - (a >> 11)) * p) / q, g = ((a >> 5) & 63) + ((((b >> 5) & 63) - ((a >> 5) & 63)) * p) / q;
  int bl = (a & 31) + (((b & 31) - (a & 31)) * p) / q;
  return (u16)((r << 11) | (g << 5) | bl);
}
static void lut(void)
{
  const u16 *p = PAL[g_set.pal & 3u];
  u32 c = 128u + (A_LCD.contrast >> 1);   /* the game's contrast: 128..255 of the full dot */
  g_lut[0] = p[0]; g_lut[2] = mix(p[0], p[1], (int)c, 255); g_lut[1] = mix(p[0], p[1], (int)c, 510);
  g_contrast = A_LCD.contrast;
}
static void colours(void)
{
  const u16 *p = PAL[g_set.pal & 3u];
  BG = p[0]; INK = p[1]; OFF = mix(p[0], p[1], 1, 2);
  lut();
#ifdef BL_PWM
  { u32 l = LV_PCT[(g_set.r0 & 15u) % LV_N], d = DIM2_PCT[g_set.dimto % DIM2_N];
    hw_bl_set(g_dimmed ? (d < l ? d : l) : l); }
#endif
  g_redraw = 1;
}
/* sector buffer (4 KB) and state buffer: the end of the fast core's cache (ab_scratch empties it), so the cache is
 * bigger; only used between runs */
static void set_load(void)
{
  const volatile u8 *p = XIP_PTR(SETTINGS); AbSet t; u8 *d = (u8 *)&t;
  for (u32 i = 0; i < sizeof t; i++) d[i] = p[i];
  if (t.magic[0] == 'A' && t.magic[1] == 'B' && t.magic[2] == 'S' && t.magic[3] == '1' && t.pal < 4u && t.dim <= T_N && t.off <= T_N && t.lang < 3u && t.size < 3u) g_set = t;
  colours();
}
static void set_store(void)
{
  u8 *sec = ab_scratch();
  for (u32 i = 0; i < 0x1000u; i++) sec[i] = 0xFF;
  const u8 *s = (const u8 *)&g_set; for (u32 i = 0; i < sizeof g_set; i++) sec[i] = s[i];
  flash_update4k(SETTINGS, sec);
}

/* ---- EEPROM (the games' saves, 1 KB) ---- */
static u32 g_tone_now;
static void tone(u32 hz)
{
#ifndef AB_TONE_ALL
  /* as GB_TONE_RESTART: every new pitch starts the buzzer anew (a new pitch in the running timer stayed silent on the
   * device); changes below about 3 % (measuring) are left out, so it does not start anew every 20 ms */
  if (hz && g_tone_now) { u32 d = hz > g_tone_now ? hz - g_tone_now : g_tone_now - hz; if (d * 32u < g_tone_now) return; }
#endif
  if (hz != g_tone_now) { if (hz && g_tone_now) hw_tone(0); g_tone_now = hz; hw_tone(hz); }
}
static void eep_load(void) { const volatile u8 *p = XIP_PTR(EEP_AT); for (u32 i = 0; i < AVR_EEP; i++) A_EEP[i] = p[i]; }
static void eep_store(void)
{
  u8 *sec = ab_scratch();
  for (u32 i = 0; i < 0x1000u; i++) sec[i] = i < AVR_EEP ? A_EEP[i] : 0xFFu;
  tone(0); flash_update4k(EEP_AT, sec);
  A_EEP_DIRTY = 0;
}

/* ---- screen ---- */
static u8 g_snap[2][1024] __attribute__((aligned(4)));   /* the last two full pictures */
static u32 g_si, g_nsnap, g_nsnap_shown;
void ab_frame(void)
{
  u32 *d = (u32 *)g_snap[g_si ^= 1u]; const u32 *s = (const u32 *)A_GD;
  for (int i = 0; i < 256; i++) d[i] = s[i];
  g_nsnap++;
}
static u8 g_src[64][128] __attribute__((aligned(4)));   /* the Arduboy's rows as shown: 0 off, 1 grey, 2 on */
static u16 g_row[128] __attribute__((aligned(4)));
static int g_dirty_lo = 64, g_dirty_hi = -1;   /* changed source rows */
static int oh(void) { return g_set.size == 0 ? 64 : g_set.size == 1 ? 96 : 128; }
static void src_dirty_all(void) { for (int y = 0; y < 64; y++) for (int x = 0; x < 128; x++) g_src[y][x] = 0xFF; }
/* one row of the Arduboy's picture: 0, 1 (grey) or 2 per dot */
static void src_row(int y, const u8 *a, const u8 *b, u8 *o)
{
  if (!A_LCD.on) { for (int x = 0; x < 128; x++) o[x] = 0; return; }
  if (A_LCD.allon) { for (int x = 0; x < 128; x++) o[x] = 2; return; }
  int ry = (A_LCD.flip_y ? y : 63 - y) + A_LCD.start; ry &= 63;
  const u8 *pa = a + (ry >> 3) * 128, *pb = b + (ry >> 3) * 128; u32 sh = (u32)ry & 7u, inv = A_LCD.invert ? 1u : 0u;
  if (A_LCD.flip_x) for (int x = 0; x < 128; x++) o[x] = (u8)((((pa[x] >> sh) & 1u) ^ inv) + (((pb[x] >> sh) & 1u) ^ inv));
  else for (int x = 0; x < 128; x++) o[x] = (u8)((((pa[127 - x] >> sh) & 1u) ^ inv) + (((pb[127 - x] >> sh) & 1u) ^ inv));
}
static void frame_build(void)
{
  const u8 *a, *b;
  if (g_nsnap != g_nsnap_shown && g_nsnap >= 2u) { a = g_snap[g_si]; b = g_snap[g_si ^ 1u]; g_nsnap_shown = g_nsnap; }
  else { a = A_GD; b = A_GD; }
  if (A_LCD.contrast != g_contrast) { lut(); g_redraw = 1; }
  static u8 t[128] __attribute__((aligned(4)));
  for (int y = 0; y < 64; y++) {
    src_row(y, a, b, t);
    u32 *f = (u32 *)g_src[y], d = 0; const u32 *w = (const u32 *)t;
    for (int x = 0; x < 32; x++) { d |= f[x] ^ w[x]; f[x] = w[x]; }
    if (d) { if (y < g_dirty_lo) g_dirty_lo = y; if (y > g_dirty_hi) g_dirty_hi = y; }
  }
}
/* output row r of the shown height h from the source rows */
static void out_px(int r, int h)
{
  if (h == 64) { const u8 *f = g_src[r]; for (int x = 0; x < 128; x++) g_row[x] = g_lut[f[x]]; return; }
  if (h == 128) { const u8 *f = g_src[r >> 1]; for (int x = 0; x < 128; x++) g_row[x] = g_lut[f[x]]; return; }
  int k = r / 3, m = r % 3; const u8 *p = g_src[2 * k], *q = g_src[2 * k + 1];   /* 2 rows to 3: a, (a + b) / 2, b */
  if (m == 0) for (int x = 0; x < 128; x++) g_row[x] = g_lut[p[x]];
  else if (m == 2) for (int x = 0; x < 128; x++) g_row[x] = g_lut[q[x]];
  else for (int x = 0; x < 128; x++) g_row[x] = g_lut[(p[x] + q[x] + 1u) >> 1];
}
static void push(int all)
{
  int h = oh(), oy = (128 - h) / 2;
  if (g_redraw) { g_redraw = 0; all = 1; }
  if (all && oy) {   /* the bars */
    for (int x = 0; x < 128; x++) g_row[x] = BG;
    hw_px_begin(0, 0, 128, (u32)oy); for (int y = 0; y < oy; y++) hw_px_row(g_row, 128); hw_px_end();
    hw_px_begin(0, (u32)(oy + h), 128, 128); for (int y = oy + h; y < 128; y++) hw_px_row(g_row, 128); hw_px_end();
  }
  int lo = all ? 0 : g_dirty_lo, hi = all ? 63 : g_dirty_hi;
  g_dirty_lo = 64; g_dirty_hi = -1;
  if (lo > hi) return;
  int r0 = h == 64 ? lo : h == 128 ? 2 * lo : (lo >> 1) * 3, r1 = h == 64 ? hi : h == 128 ? 2 * hi + 1 : (hi >> 1) * 3 + 2;
  hw_px_begin(0, (u32)(oy + r0), 128, (u32)(oy + r1 + 1));
  for (int r = r0; r <= r1; r++) { out_px(r, h); hw_px_row(g_row, 128); }
  hw_px_end();
}

/* ---- the settings menu (A+B+C): A next line, B change, C close ---- */
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
enum { M_PAL, M_SIZE, M_SND, M_LIGHT, M_DIM, M_DIMTO, M_OFF, M_RESET, M_LANG, M_N };
static int g_items[M_N], g_nitems;
static void menu_items(void)
{
  g_nitems = 0;
  for (int k = 0; k < M_N; k++) { if (k == M_DIMTO && g_set.dim >= T_N) continue; g_items[g_nitems++] = k; }
  if (g_msel >= g_nitems) g_msel = g_nitems - 1;
}
static void pct(char *o, int v) { int n = 0; if (v >= 100) o[n++] = '1'; if (v >= 10) o[n++] = (char)('0' + (v / 10) % 10); o[n++] = (char)('0' + v % 10); o[n++] = '%'; o[n] = 0; }
static int g_reset_ask;
static void menu_line(int it, const char **a, char *b)
{
  int e = en(); const char *v = 0; b[0] = 0;
  switch (it) {
    case M_PAL: *a = e ? "COLOUR" : "FARBE"; v = PAL_NM[e][g_set.pal & 3u]; break;
    case M_SIZE: *a = e ? "PICTURE" : "BILD"; v = SIZE_NM[e][g_set.size % 3u]; break;
    case M_SND: *a = e ? "SOUND" : "TON"; v = g_set.mute ? (e ? "OFF" : "AUS") : (e ? "ON" : "AN"); break;
    case M_LIGHT: *a = e ? "LIGHT" : "LICHT"; pct(b, LV_PCT[(g_set.r0 & 15u) % LV_N]); break;
    case M_DIM: *a = e ? "DIM AFTER" : "DIMMEN NACH"; v = T_NM[e][g_set.dim <= T_N ? g_set.dim : T_N]; break;
    case M_DIMTO: *a = e ? "DIM TO" : "DIMMEN AUF"; pct(b, DIM2_PCT[g_set.dimto % DIM2_N]); break;
    case M_OFF: *a = e ? "SCREEN OFF" : "BILD AUS NACH"; v = T_NM[e][g_set.off <= T_N ? g_set.off : T_N]; break;
    case M_RESET: *a = e ? "RESTART GAME" : "SPIEL NEU"; v = g_reset_ask ? (e ? "SURE? B" : "SICHER? B") : ""; break;
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
    text(4, 118, en() ? "A NEXT B SET C OK" : "A ZEILE B SETZEN C OK", OFF, g_row, y);
    hw_px_row(g_row, 128);
  }
  hw_px_end();
}
static int g_set_dirty;
static void ab_fresh(void);
static void menu_close(void)
{
  g_menu = 0; g_reset_ask = 0; if (g_set_dirty) { set_store(); g_set_dirty = 0; }
  src_dirty_all(); frame_build(); push(1);
}
static void menu_key(u32 k)
{
  if (k & BTN_A) { g_msel = (g_msel + 1) % g_nitems; g_reset_ask = 0; }
  if (k & BTN_B) {
    switch (g_items[g_msel]) {
      case M_PAL: g_set.pal = (u8)((g_set.pal + 1u) & 3u); break;
      case M_SIZE: g_set.size = (u8)((g_set.size + 1u) % 3u); break;
      case M_SND: g_set.mute ^= 1u; break;
      case M_LIGHT: g_set.r0 = (u8)(0x80u | (((g_set.r0 & 15u) + 1u) % LV_N)); break;
      case M_DIM: g_set.dim = (u8)((g_set.dim + 1u) % (T_N + 1u)); if (g_set.dim < T_N && g_set.off < T_N && g_set.off <= g_set.dim) g_set.off = (u8)(g_set.dim + 1u); break;
      case M_DIMTO: g_set.dimto = (u8)((g_set.dimto + 1u) % DIM2_N); break;
      case M_OFF: do g_set.off = (u8)((g_set.off + 1u) % (T_N + 1u)); while (g_set.off < T_N && g_set.dim < T_N && g_set.off <= g_set.dim); break;
      case M_RESET: if (g_reset_ask) { g_reset_ask = 0; ab_fresh(); menu_close(); return; } g_reset_ask = 1; break;
      default: g_set.lang = (u8)(en() ? 1u : 2u); break;
    }
    if (g_items[g_msel] != M_RESET) g_set_dirty = 1;
    colours(); menu_items();
  }
  if (k & BTN_C) { menu_close(); return; }
  menu_draw();
}
static void message(const char *a, const char *b)
{
  hw_px_begin(0, 0, 128, 128);
  for (int y = 0; y < 128; y++) { for (int x = 0; x < 128; x++) g_row[x] = BG; text(4, 50, a, INK, g_row, y); text(4, 62, b, INK, g_row, y); hw_px_row(g_row, 128); }
  hw_px_end();
}

/* ---- keys: the Paradise inputs to the Arduboy's buttons (map from the Studio) ---- */
static u32 g_wheel_until[2];   /* wheel up / down held until (ms) */
static u32 ab_keys(u32 b, u32 now)
{
  u32 in = 0;
  int ac = (b & (BTN_A | BTN_C)) == (BTN_A | BTN_C), bc = (b & (BTN_B | BTN_C)) == (BTN_B | BTN_C), ab = (b & (BTN_A | BTN_B)) == (BTN_A | BTN_B);
  u32 used = 0;   /* a pair held: its two keys count as the pair only (if the pair has a meaning) */
  if (ac && g_cfg.keys[6]) { in |= 1u << 6; used |= BTN_A | BTN_C; }
  if (bc && g_cfg.keys[7]) { in |= 1u << 7; used |= BTN_B | BTN_C; }
  if (ab && g_cfg.keys[8]) { in |= 1u << 8; used |= BTN_A | BTN_B; }
  u32 r = b & ~used;
  if (r & BTN_A) in |= 1u; if (r & BTN_B) in |= 2u; if (r & BTN_C) in |= 4u; if (b & BTN_WHEEL) in |= 8u;
  if ((s32)(g_wheel_until[0] - now) > 0) in |= 16u;
  if ((s32)(g_wheel_until[1] - now) > 0) in |= 32u;
  u32 k = 0;
  for (int i = 0; i < NIN; i++) if (in & (1u << i)) k |= g_cfg.keys[i];
  return k;
}

/* ---- the game ---- */
static void ab_fresh(void) { A_KEYS = 0; ab_reset(); }

/* ---- the whole state on leaving (wheel held), read once at the next start ---- */
#include "../../../emu/src/emu/pstate.h"
#ifdef SESS_FLAG
#define NOSESS ((g_cfg.flags & 0x1000u) != 0u)   /* 1.7.3 test7: the Studio's "remember on leaving" off: only the game's own save */
#else
#define NOSESS 0
#endif
#define STATE_N 0x1400u
static int g_running;
static u32 st_tag(void) { return 0x41420001u ^ (u32)STATE_N ^ g_cfg.rom_len ^ (u32)&A_D ^ (u32)ab_run; }
static void state_store(void)
{
  if (!g_running) return;
  u8 *sec = ab_scratch(), *st = sec + 0x1000u;
  for (u32 i = 0; i < STATE_N; i++) st[i] = 0;
  if (!ab_state_save(st, STATE_N)) return;
  StPart pt[1] = { { st, STATE_N } };
  st_store(STATE_AT, STATE_MAX, st_tag(), pt, 1, sec);
}
static int state_load(void)
{
  if (NOSESS) { st_drop(STATE_AT); return 0; }
  u8 *st = ab_scratch() + 0x1000u;
  StPart pt[1] = { { st, STATE_N } };
  int ok = st_load(STATE_AT, STATE_MAX, st_tag(), pt, 1) && ab_state_load(st, STATE_N);
  st_drop(STATE_AT);
  return ok;
}

/* ---- leaving ---- */
static void leave(void)
{
  tone(0); hw_tone(0);
  if (g_set_dirty) set_store();
  REG(0x20000FF8u) = 0;
  REG(0x20000FFCu) = 0x4C584D50u;        /* "PMXL": back on purpose, the autostart stays off */
  if (g_running && !NOSESS) { message(en() ? "SAVING" : "SPEICHERN", ""); state_store(); }
  if (A_EEP_DIRTY) eep_store();
#ifdef PARA_BACK
#ifdef GPLAY
  para_points((uint8_t *)ab_scratch());   /* play time to Tamapoints */
#endif
  para_back_fix(ab_scratch());
#endif
  hw_bl_pwm_off();
  for (u32 t = 0; hw_buttons() && t < 20000000u; t++) if ((t & 0xFFFFu) == 0) hw_wdt_feed();
  XIP_BASE_REG = 0x60011000u; XIP_CTRL_REG = 3u;
  hw_back_to_paradise();
}

__attribute__((used)) volatile u32 g_abst[4];   /* ms run, pictures pushed, ms dropped, last slice in ms (for the tests) */
extern u32 __bss_start, __bss_end;
void Reset_Handler(void)
{
  __asm__ volatile("cpsid i");
  u32 cnt = REG(0x45000304u), para = para_clock(cnt, 0, 0x1F000u, 0, 0);
  if (!para) para = para_mailbox(cnt);
#ifdef PARA_BACK
  para_leave_mark(cnt, para);
#endif
  __asm__ volatile("" ::: "memory");
  for (u32 *p = &__bss_start; p < &__bss_end; p++) *p = 0;
  { extern u32 __data_start, __data_end, __data_load;
    const u32 *src = &__data_load; for (u32 *p = &__data_start; p < &__data_end; p++) *p = *src++; }
  (void)para;
  hw_init();
  hw_tone(0);
  XIP_BASE_REG = 0x60000000u | g_cfg.rom_off;     /* the game in the cached window from 0x10000000 */
  XIP_CTRL_REG = 3u;
  REG(GPIO2 + 0x04) &= ~3u; g_enc_last = REG(GPIO2) & 3u;
  set_load();
  ab_load(ROMWIN, (g_cfg.rom_len + 1u) / 2u);
  eep_load();
  ab_reset();
  state_load();
  A_EEP_DIRTY = 0;
  g_running = 1;
  src_dirty_all();
  frame_build(); push(1);
  SYST_RVR = CORE_HZ / 1000u - 1u; SYST_CVR = 0; SYST_CSR = 7;
  __asm__ volatile("cpsie i");

  u32 last_in = g_ms, wheel_ms = 0, eep_ms = 0, done_ms = g_ms, last_push = g_ms, mk0 = 0, tone_ms = g_ms, tone_cyc = A_CYC;
  int bl = 1, lock = 1, menu_lock = 0;
  for (;;) {
    hw_wdt_feed();
    u32 now = g_ms;
    u32 b = hw_buttons();
    if (lock) { if (b) b = 0; else lock = 0; }
    /* wheel steps: one step = 4 phases; each step holds up or down for 150 ms */
    { s32 a = g_enc_acc;
      if (a >= 4 || a <= -4) {
        __asm__ volatile("cpsid i"); g_enc_acc -= a >= 4 ? 4 : -4; __asm__ volatile("cpsie i");
        int up = (a > 0) ^ ((g_cfg.flags & 2u) != 0);
        u32 *u = &g_wheel_until[up ? 0 : 1]; *u = ((s32)(*u - now) > 0 ? *u : now) + 150u;
        last_in = now;
        if (g_menu) { g_msel = up ? (g_msel + g_nitems - 1) % g_nitems : (g_msel + 1) % g_nitems; menu_draw(); }
      } }
    if (b) {
      last_in = now;
      if (!bl || g_dimmed) {                            /* the key only wakes */
        bl = 1; hw_backlight(1); if (g_dimmed) { g_dimmed = 0; colours(); }
        if (g_menu) menu_draw(); else push(1);
        while (hw_buttons()) hw_wdt_feed();
        done_ms = g_ms;
        continue;
      }
    }
    /* wheel held 2 s: save and back */
    if (b & BTN_WHEEL) { if (!wheel_ms) wheel_ms = now ? now : 1; else if (EXIT_NOW(now - wheel_ms, 2000u)) leave(); } else wheel_ms = 0;
    /* A+B+C: settings */
    if ((b & (BTN_A | BTN_B | BTN_C)) == (BTN_A | BTN_B | BTN_C) && !g_menu && !menu_lock) {
      tone(0); A_KEYS = 0; g_menu = 1; menu_lock = 1; g_msel = 0; g_mtop = 0; menu_items(); menu_draw(); mk0 = BTN_A | BTN_B | BTN_C;
    }
    if (!(b & (BTN_A | BTN_B | BTN_C))) menu_lock = 0;
    if (g_menu) {
      u32 k = b & (BTN_A | BTN_B | BTN_C), nk = k & ~mk0; mk0 = k; if (nk) menu_key(nk);
      done_ms = g_ms;
      __asm__ volatile("wfi");
      continue;
    }
    if (!bl) { tone(0); done_ms = g_ms; __asm__ volatile("wfi"); continue; }   /* screen off: the game waits (battery) */
    A_KEYS = ab_keys(b, now);
    /* the Arduboy runs 16000 cycles per millisecond */
    if ((s32)(now - done_ms) > 0) {
      if ((s32)(now - done_ms) > 50) { g_abst[2] += now - done_ms - 50u; done_ms = now - 50u; }   /* too far behind: drop the time */
      u32 t0 = g_ms;
      ab_run(16000u);
      done_ms++; g_abst[0]++; g_abst[3] = g_ms - t0;
      if (now - tone_ms >= 20u) {
        u32 hz = ab_tone(A_CYC - tone_cyc); tone_ms = now; tone_cyc = A_CYC;
        tone(!g_set.mute && hz >= 40u && hz <= 12000u ? hz : 0u);
      }
      if (now - last_push >= 33u) { last_push = now; frame_build(); push(0); g_abst[1]++; }
    }
    if (A_EEP_DIRTY && !eep_ms) eep_ms = now ? now : 1;
    if (eep_ms && now - eep_ms > 3000u && now - last_in > 1000u) { eep_ms = 0; eep_store(); }
    if (bl && !g_dimmed && g_set.dim < T_N && now - last_in > T_MS[g_set.dim]) { g_dimmed = 1; colours(); push(1); }
    if (bl && g_set.off < T_N && now - last_in > T_MS[g_set.off]) { bl = 0; tone(0); hw_backlight(0); if (A_EEP_DIRTY) { eep_ms = 0; eep_store(); } }
    if ((s32)(g_ms - done_ms) <= 0) __asm__ volatile("wfi");
  }
}

__attribute__((naked)) static void Default_Handler(void) { __asm__ volatile("1: b 1b\n"); }
__attribute__((naked)) static void Irq_Off(void) { __asm__ volatile("cpsid i\n bx lr\n"); }
__attribute__((section(".vectors"), used))
const void *const g_vectors[16 + 64] = { (void *)0x18020000u, (void *)Reset_Handler,
  [2 ... 10] = (void *)Default_Handler, [11] = (void *)Irq_Off, [12 ... 13] = (void *)Default_Handler, [14] = (void *)Irq_Off,
  [15] = (void *)SysTick_Handler, [16 ... 79] = (void *)Irq_Off };
