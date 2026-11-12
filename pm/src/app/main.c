/* PokeMiniEmu: the Pokemon mini on the Tamagotchi Paradise. Core: PokeMini by JustBurn (GPL 3 or later), see ../core.
 * Started from the Paramix menu like the other further programs. Bring your own games (.min); the BIOS is the free one
 * of PokeMini unless the Studio put your own in.
 * Screen: 96 x 64 grown to 128 x 85 (3 dots to 4, mixed), with the grey shades of the real LCD (the core's analog LCD).
 * Keys: set in the Studio (tab Pokemon mini), stored in the settings block. A+B+C together: settings (also POWER and
 * sound); wheel held 2 s: save and back to the Paradise. Sound: the Paradise's buzzer plays the Pokemon mini's tone.
 * Save: the EEPROM (the games' saves) goes to the flash a few seconds after the game wrote it, and on leaving; on
 * leaving the whole state too, the next start continues there.
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
#include "PokeMini.h"
#include "Hardware.h"
#include "font.h"
typedef int s32;

/* ---- settings block at 0x140, filled in by the Studio ----
 * keys[i]: Pokemon mini buttons (bit 0 A, 1 B, 2 C, 3 UP, 4 DOWN, 5 LEFT, 6 RIGHT, 7 POWER, 8 SHAKE) for the Paradise
 * input i: 0 A, 1 B, 2 C, 3 wheel press, 4 wheel up, 5 wheel down, 6 A+C, 7 B+C, 8 A+B.
 * flags: bit 0 English, bit 1 wheel the other way round, bit 2 own BIOS at bios_off.
 * save_off: 4 KB settings of the device, 8 KB EEPROM, then the state (up to 36 KB). */
#define NIN 9
typedef struct { u32 magic, version, rom_off, rom_len, save_off, flags; char name[16]; u16 keys[12]; u32 bios_off; } PmCfg;
#define PM_MAGIC 0x50584D50u   /* "PMXP" */
__attribute__((section(".pmxcfg"), used))
volatile const PmCfg g_cfg = { PM_MAGIC, 1, 0, 0, 0, 0, {0}, { 0x020, 0x002, 0x040, 0x001, 0x008, 0x010, 0x004, 0x000, 0x100 }, 0 };
#define SETTINGS (g_cfg.save_off)
#define EEP_AT (g_cfg.save_off + 0x1000u)
#define STATE_AT (g_cfg.save_off + 0x3000u)
#define STATE_MAX 0x9000u
#define STATE_N 0x7400u
#define XIP_BASE_REG REG(0x4002F004u)
#define XIP_CTRL_REG REG(0x4002F000u)
#define ROMWIN ((const u8 *)0x10000000u)
void pm_set_rom(const uint8_t *rom, uint32_t len);
void pm_set_time(int y, int mo, int d, int h, int mi, int s);
uint32_t pm_state_save(uint8_t *buf, uint32_t size);
int pm_state_load(uint8_t *buf, uint32_t size);
void pm_audio_now(int *hz, int *vol);
extern int PokeMini_EEPROMWritten;

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
typedef struct { u8 magic[4]; u8 pal, dim, lang, dimto, off, r0, skip, mute; } PmSet;   /* mute: 0 = sound on */
static PmSet g_set = { {'P','M','S','1'}, 0, 7, 0, 6, 3, 0x80, 0, 0 };
/* light to dark: grey green (as the real LCD), grey, yellow, blue */
static const u16 PAL[4][2] = { { 0xB6B1u, 0x1903u }, { 0xE71Cu, 0x10A2u }, { 0xFFF4u, 0x2100u }, { 0xDF5Fu, 0x0846u } };
static const char *const PAL_NM[2][4] = { { "LCD", "GRAU", "GELB", "BLAU" }, { "LCD", "GREY", "YELLOW", "BLUE" } };
#define T_N 7
static const u32 T_MS[T_N] = { 10000u, 20000u, 30000u, 60000u, 120000u, 300000u, 600000u };
static const char *const T_NM[2][T_N + 1] = { { "10 S", "20 S", "30 S", "1 MIN", "2 MIN", "5 MIN", "10 MIN", "NIE" }, { "10 S", "20 S", "30 S", "1 MIN", "2 MIN", "5 MIN", "10 MIN", "NEVER" } };
#define LV_N 11
#define DIM2_N 10
static const u8 LV_PCT[LV_N] = { 100u, 5u, 10u, 20u, 30u, 40u, 50u, 60u, 70u, 80u, 90u };
static const u8 DIM2_PCT[DIM2_N] = { 1u, 2u, 3u, 5u, 10u, 15u, 20u, 30u, 40u, 50u };
static int en(void) { return g_set.lang ? g_set.lang == 2u : (int)(g_cfg.flags & 1u); }
static u16 BG, INK, OFF;
static u16 g_lut[256];   /* LCD intensity (0 light .. 255 dark) to colour */
static int g_dimmed, g_redraw;
static u16 mix(u16 a, u16 b, int p, int q)   /* a + (b - a) * p / q */
{
  int r = (a >> 11) + (((b >> 11) - (a >> 11)) * p) / q, g = ((a >> 5) & 63) + ((((b >> 5) & 63) - ((a >> 5) & 63)) * p) / q;
  int bl = (a & 31) + (((b & 31) - (a & 31)) * p) / q;
  return (u16)((r << 11) | (g << 5) | bl);
}
static void colours(void)
{
  const u16 *p = PAL[g_set.pal & 3u];
  for (int v = 0; v < 256; v++) g_lut[v] = mix(p[0], p[1], v, 255);
  BG = p[0]; INK = p[1]; OFF = g_lut[150];
#ifdef BL_PWM
  { u32 l = LV_PCT[(g_set.r0 & 15u) % LV_N], d = DIM2_PCT[g_set.dimto % DIM2_N];
    hw_bl_set(g_dimmed ? (d < l ? d : l) : l); }
#endif
  g_redraw = 1;
}
static u8 g_sec[0x1000] __attribute__((aligned(4)));
static void set_load(void)
{
  const volatile u8 *p = XIP_PTR(SETTINGS); PmSet t; u8 *d = (u8 *)&t;
  for (u32 i = 0; i < sizeof t; i++) d[i] = p[i];
  if (t.magic[0] == 'P' && t.magic[1] == 'M' && t.magic[2] == 'S' && t.magic[3] == '1' && t.pal < 4u && t.dim <= T_N && t.off <= T_N && t.lang < 3u) g_set = t;
  colours();
}
static void set_store(void)
{
  for (u32 i = 0; i < sizeof g_sec; i++) g_sec[i] = 0xFF;
  const u8 *s = (const u8 *)&g_set; for (u32 i = 0; i < sizeof g_set; i++) g_sec[i] = s[i];
  flash_update4k(SETTINGS, g_sec);
}

/* ---- EEPROM (the games' saves, 8 KB) ---- */
static u32 g_tone_now;
static void tone(u32 hz) { if (hz != g_tone_now) { if (hz && g_tone_now) hw_tone(0); g_tone_now = hz; hw_tone(hz); } }
static void eep_load(void)
{
  const volatile u8 *p = XIP_PTR(EEP_AT); int any = 0;
  for (u32 i = 0; i < 0x2000u; i++) { u8 v = p[i]; EEPROM[i] = v; if (v != 0xFF) any = 1; }
  (void)any;   /* a fresh area (all 0xFF) is a fresh EEPROM, as PokeMini formats it */
}
static void eep_store(void)
{
  for (u32 o = 0; o < 0x2000u; o += 0x1000u) {
    for (u32 i = 0; i < 0x1000u; i++) g_sec[i] = EEPROM[o + i];
    tone(0); flash_update4k(EEP_AT + o, g_sec);
  }
  PokeMini_EEPROMWritten = 0;
}

/* ---- screen: 96 x 64 to 128 x 85 (3 dots to 4: a, (a+2b)/3, (2b+c)/3, c), from y 21 ---- */
#define OY 21
#define OH 85
static u8 g_fb[OH][128] __attribute__((aligned(4)));   /* intensity 0..255 */
static u16 g_row[128] __attribute__((aligned(4)));
static int g_dirty_lo = OH, g_dirty_hi = -1;
static u8 g_hl[3][128];   /* the three source rows of a group, grown across */
static void grow_row(const u8 *s, u8 *d)
{
  for (int k = 0; k < 32; k++, s += 3, d += 4) {
    u32 a = s[0], b = s[1], c = s[2];
    d[0] = (u8)a; d[1] = (u8)((a + 2u * b) / 3u); d[2] = (u8)((2u * b + c) / 3u); d[3] = (u8)c;
  }
}
static void out_row(int r, const u8 *v)
{
  if (r >= OH) return;
  u32 *f = (u32 *)g_fb[r], d = 0; const u32 *w = (const u32 *)v;
  for (int x = 0; x < 32; x++) { d |= f[x] ^ w[x]; f[x] = w[x]; }
  if (d) { if (r < g_dirty_lo) g_dirty_lo = r; if (r > g_dirty_hi) g_dirty_hi = r; }
}
#ifdef PM_FASTLCD
/* the LCD memory (8 pages of 96 columns, one bit per dot) of the frame before the shown one: dots on in one of the two
 * frames are grey (games make their grey by switching dots every frame) */
static u8 g_prev[8 * 96], g_src[96 * 64] __attribute__((aligned(4)));
static void lcd_snap(void) { for (int p = 0; p < 8; p++) for (int x = 0; x < 96; x++) g_prev[p * 96 + x] = LCDData[p * 256 + x]; }
#ifdef PM_PRC_MIX
int pm_prc_img;
static u8 g_cur[8 * 96] __attribute__((aligned(4)));
static int g_pend;   /* a new picture not shown yet */
/* a new picture: the one before becomes g_prev, the new one g_cur */
static void lcd_take(void) { for (int i = 0; i < 8 * 96; i++) g_prev[i] = g_cur[i]; for (int p = 0; p < 8; p++) for (int x = 0; x < 96; x++) g_cur[p * 96 + x] = LCDData[p * 256 + x]; }
#define MIX_SRC g_cur
#define MIX_STRIDE 96
#else
#define MIX_SRC LCDData
#define MIX_STRIDE 256
#endif
static void lcd_mix(void)
{
  if (!MinxLCD.DisplayOn) { for (int i = 0; i < 96 * 64; i++) g_src[i] = 0; return; }
  u8 lv[3]; int p0 = MinxLCD.Pixel0Intensity, p1 = MinxLCD.Pixel1Intensity;
  lv[0] = (u8)p0; lv[1] = (u8)((p0 + p1) / 2); lv[2] = (u8)p1;
  u32 inv = MinxLCD.InvAllPix & 1u, set = MinxLCD.SetAllPix & 1u;
  for (int y = 0; y < 64; y++) {
    int yp = (y + MinxLCD.StartLine) & 63; if (MinxLCD.RowOrder) yp = 63 - yp;
    const u8 *c = MIX_SRC + (yp >> 3) * MIX_STRIDE, *q = g_prev + (yp >> 3) * 96; u32 sh = (u32)yp & 7u; u8 *d = g_src + y * 96;
    for (int x = 0; x < 96; x++) { u32 a = (((c[x] >> sh) & 1u) ^ inv) | set, b = (((q[x] >> sh) & 1u) ^ inv) | set; d[x] = lv[a + b]; }
  }
}
#endif
static void frame_grow(void)
{
#ifdef PM_FASTLCD
  const u8 *src = g_src;
#else
  const u8 *src = LCDPixelsA;
#endif
  static u8 t[128] __attribute__((aligned(4)));
  for (int g = 0; g < 22; g++) {   /* rows 3g..3g+2 to 4g..4g+3 (the last group has row 63 only) */
    int n = g < 21 ? 3 : 1;
    for (int i = 0; i < n; i++) grow_row(src + (3 * g + i) * 96, g_hl[i]);
    out_row(4 * g, g_hl[0]);
    if (n < 3) break;
    for (int x = 0; x < 128; x++) t[x] = (u8)((g_hl[0][x] + 2u * g_hl[1][x]) / 3u);
    out_row(4 * g + 1, t);
    for (int x = 0; x < 128; x++) t[x] = (u8)((2u * g_hl[1][x] + g_hl[2][x]) / 3u);
    out_row(4 * g + 2, t);
    out_row(4 * g + 3, g_hl[2]);
  }
}
static void push(int all)
{
  if (g_redraw) { g_redraw = 0; all = 1; }
  int lo = all ? 0 : g_dirty_lo, hi = all ? OH - 1 : g_dirty_hi;
  if (all) {   /* the bars */
    for (int x = 0; x < 128; x++) g_row[x] = BG;
    hw_px_begin(0, 0, 128, OY); for (int y = 0; y < OY; y++) hw_px_row(g_row, 128); hw_px_end();
    hw_px_begin(0, OY + OH, 128, 128); for (int y = OY + OH; y < 128; y++) hw_px_row(g_row, 128); hw_px_end();
  }
  if (lo > hi) return;
  hw_px_begin(0, (u32)(OY + lo), 128, (u32)(OY + hi + 1));
  for (int y = lo; y <= hi; y++) { const u8 *f = g_fb[y]; for (int x = 0; x < 128; x++) g_row[x] = g_lut[f[x]]; hw_px_row(g_row, 128); }
  hw_px_end();
  g_dirty_lo = OH; g_dirty_hi = -1;
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
enum { M_POW, M_PAL, M_SND, M_LIGHT, M_DIM, M_DIMTO, M_OFF, M_RESET, M_LANG, M_N };
static u32 g_power_until;   /* menu line POWER: pressed for 200 ms after the menu closes */
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
    case M_POW: *a = e ? "PRESS POWER" : "POWER DRUECKEN"; v = "B"; break;
    case M_PAL: *a = e ? "COLOUR" : "FARBE"; v = PAL_NM[e][g_set.pal & 3u]; break;
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
static void pm_fresh(void);
static void pause_end(void);
static void menu_close(void) { pause_end(); g_menu = 0; g_reset_ask = 0; if (g_set_dirty) { set_store(); g_set_dirty = 0; } push(1); }
static void menu_key(u32 k)
{
  if (k & BTN_A) { g_msel = (g_msel + 1) % g_nitems; g_reset_ask = 0; }
  if (k & BTN_B) {
    switch (g_items[g_msel]) {
      case M_POW: g_power_until = g_ms + 200u; menu_close(); return;
      case M_PAL: g_set.pal = (u8)((g_set.pal + 1u) & 3u); break;
      case M_SND: g_set.mute ^= 1u; break;
      case M_LIGHT: g_set.r0 = (u8)(0x80u | (((g_set.r0 & 15u) + 1u) % LV_N)); break;
      case M_DIM: g_set.dim = (u8)((g_set.dim + 1u) % (T_N + 1u)); if (g_set.dim < T_N && g_set.off < T_N && g_set.off <= g_set.dim) g_set.off = (u8)(g_set.dim + 1u); break;
      case M_DIMTO: g_set.dimto = (u8)((g_set.dimto + 1u) % DIM2_N); break;
      case M_OFF: do g_set.off = (u8)((g_set.off + 1u) % (T_N + 1u)); while (g_set.off < T_N && g_set.dim < T_N && g_set.off <= g_set.dim); break;
      case M_RESET: if (g_reset_ask) { g_reset_ask = 0; pm_fresh(); menu_close(); return; } g_reset_ask = 1; break;
      default: g_set.lang = (u8)(en() ? 1u : 2u); break;
    }
    if (g_items[g_msel] != M_RESET && g_items[g_msel] != M_POW) g_set_dirty = 1;
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

/* ---- keys: the Paradise inputs to the Pokemon mini's buttons (map from the Studio) ---- */
static u32 g_wheel_until[2];   /* wheel up / down held until (ms) */
static u32 pm_keys(u32 b, u32 now)
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
  u32 pm = 0;
  for (int i = 0; i < NIN; i++) if (in & (1u << i)) pm |= g_cfg.keys[i];
  if ((s32)(g_power_until - now) > 0) pm |= 0x80u;
  return pm;
}
static u32 g_pm_keys;
static void keys_set(u32 k)
{
  u32 ch = k ^ g_pm_keys; if (!ch) return;
  for (int i = 0; i < 9; i++) if (ch & (1u << i)) MinxIO_Keypad((u8)(MINX_KEY_A + i), (k >> i) & 1u);   /* A, B, C, up, down, left, right, power, shake */
  g_pm_keys = k;
}

/* ---- the clock of the game from the Paradise's ---- */
static u32 g_para;
static void time_from_para(void)
{
  if (!g_para) return;
  u32 s = g_para, days = s / 86400u, rem = s % 86400u;
  s32 z = (s32)days + 719468, era = z / 146097; u32 doe = (u32)(z - era * 146097);
  u32 yoe = (doe - doe / 1460u + doe / 36524u - doe / 146096u) / 365u; s32 y = (s32)yoe + era * 400;
  u32 doy = doe - (365u * yoe + yoe / 4u - yoe / 100u), mp = (5u * doy + 2u) / 153u, d = doy - (153u * mp + 2u) / 5u + 1u, m = mp < 10u ? mp + 3u : mp - 9u;
  if (m <= 2u) y++;
  pm_set_time((int)y, (int)m, (int)d, (int)(rem / 3600u), (int)(rem / 60u % 60u), (int)(rem % 60u));
}
static void bios_load(void)
{
  if ((g_cfg.flags & 4u) && g_cfg.bios_off) { const volatile u8 *p = XIP_PTR(g_cfg.bios_off); for (u32 i = 0; i < 4096u; i++) PM_BIOS[i] = p[i]; PokeMini_FreeBIOS = 0; }
  else PokeMini_LoadFreeBIOS();
}
/* the game anew (settings menu RESTART GAME): the EEPROM stays */
static void pm_fresh(void) { keys_set(0); PokeMini_Reset(1); time_from_para(); }

/* ---- the Pokemon mini's clock (its seconds counter) keeps running while the game waits (settings, screen off, back
 * in the Paradise): the seconds of the Paradise's counter (it runs always) are added when the game goes on ---- */
#define PARA_SECS REG(0x45000304u)
static u32 g_pause_cnt;
static void rtc_add(u32 secs) { if (secs && secs < 400u * 86400u && (PMR_SEC_CTRL & 1u)) MinxTimers.SecTimerCnt = (MinxTimers.SecTimerCnt + secs) & 0xFFFFFFu; }
static void pause_begin(void) { if (!g_pause_cnt) g_pause_cnt = PARA_SECS | 0x80000000u; }
static void pause_end(void) { if (g_pause_cnt) { rtc_add((PARA_SECS - g_pause_cnt) & 0x7FFFFFFFu); g_pause_cnt = 0; } }

/* ---- the whole state on leaving (wheel held), read once at the next start ---- */
#include "../../../emu/src/emu/pstate.h"
#ifdef SESS_FLAG
#define NOSESS ((g_cfg.flags & 0x1000u) != 0u)   /* 1.7.3 test7: the Studio's "remember on leaving" off: only the game's own save */
#else
#define NOSESS 0
#endif
static u8 g_st[STATE_N] __attribute__((aligned(4)));
static int g_running;
static u32 st_tag(void) { return 0x504D0001u ^ (u32)STATE_N ^ g_cfg.rom_len ^ (u32)&PM_RAM ^ (u32)PokeMini_EmulateFrame; }
static u32 g_st_cnt;   /* the Paradise's seconds counter when the state was stored */
static void state_store(void)
{
  if (!g_running) return;
  pause_end();
  for (u32 i = 0; i < STATE_N; i++) g_st[i] = 0;
  if (!pm_state_save(g_st, STATE_N)) return;
  g_st_cnt = PARA_SECS;
  StPart pt[2] = { { g_st, STATE_N }, { &g_st_cnt, 4 } };
  st_store(STATE_AT, STATE_MAX, st_tag(), pt, 2, g_sec);
}
static int state_load(void)
{
  if (NOSESS) { st_drop(STATE_AT); return 0; }
  StPart pt[2] = { { g_st, STATE_N }, { &g_st_cnt, 4 } };
  int ok = st_load(STATE_AT, STATE_MAX, st_tag(), pt, 2) && pm_state_load(g_st, STATE_N);
  st_drop(STATE_AT);
  if (ok) rtc_add(PARA_SECS - g_st_cnt);   /* the time away */
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
  if (PokeMini_EEPROMWritten) eep_store();
#ifdef PARA_BACK
#ifdef GPLAY
  para_points((uint8_t *)g_sec);   /* play time to Tamapoints */
#endif
  para_back_fix(g_sec);
#endif
  hw_bl_pwm_off();
  for (u32 t = 0; hw_buttons() && t < 20000000u; t++) if ((t & 0xFFFFu) == 0) hw_wdt_feed();
  XIP_BASE_REG = 0x60011000u; XIP_CTRL_REG = 3u;
  hw_back_to_paradise();
}

__attribute__((used)) volatile u32 g_pmst[4];   /* frames run, frames pushed, frames dropped, last frame in ms (for the tests) */
extern u32 __bss_start, __bss_end;
void Reset_Handler(void)
{
  __asm__ volatile("cpsid i");
  u32 cnt = REG(0x45000304u), para = para_clock(cnt, 0, 0x1F000u, 0, 0);
  if (!para) para = para_mailbox(cnt);
#ifdef PARA_FLASH
  if (!para) para = para_flash(cnt);
#endif
#ifdef PARA_BACK
  para_leave_mark(cnt, para);
#endif
  __asm__ volatile("" ::: "memory");
  for (u32 *p = &__bss_start; p < &__bss_end; p++) *p = 0;
  { extern u32 __data_start, __data_end, __data_load;
    const u32 *src = &__data_load; for (u32 *p = &__data_start; p < &__data_end; p++) *p = *src++; }
  g_para = para;
  hw_init();
  hw_tone(0);
  XIP_BASE_REG = 0x60000000u | g_cfg.rom_off;     /* the game in the cached window from 0x10000000 */
  XIP_CTRL_REG = 3u;
  REG(GPIO2 + 0x04) &= ~3u; g_enc_last = REG(GPIO2) & 3u;
  set_load();
  if (!PokeMini_Create(0, 0)) { message("NO MEMORY", ""); for (u32 t = 0; t < 30000000u; t++) if ((t & 0xFFFFu) == 0) hw_wdt_feed(); leave(); }
  PokeMini_LCDMode = LCDMODE_ANALOG;
  bios_load();
  pm_set_rom(ROMWIN, g_cfg.rom_len);
  eep_load();
  PokeMini_Reset(1);
  if (!state_load()) time_from_para();
  PokeMini_EEPROMWritten = 0;
  g_running = 1;
  push(1);
  SYST_RVR = CORE_HZ / 1000u - 1u; SYST_CVR = 0; SYST_CSR = 7;
  __asm__ volatile("cpsie i");

  u32 last_in = g_ms, wheel_ms = 0, eep_ms = 0, next_us = 0, last_push = g_ms, mk0 = 0, t_us0 = g_ms;
  int bl = 1, lock = 1, menu_lock = 0, drawn = 0;
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
        bl = 1; hw_backlight(1); pause_end(); if (g_dimmed) { g_dimmed = 0; colours(); }
        if (g_menu) menu_draw(); else push(1);
        while (hw_buttons()) hw_wdt_feed();
        t_us0 = g_ms; next_us = 0;
        continue;
      }
    }
    /* wheel held 2 s: save and back */
    if (b & BTN_WHEEL) { if (!wheel_ms) wheel_ms = now ? now : 1; else if (EXIT_NOW(now - wheel_ms, 2000u)) leave(); } else wheel_ms = 0;
    /* A+B+C: settings */
    if ((b & (BTN_A | BTN_B | BTN_C)) == (BTN_A | BTN_B | BTN_C) && !g_menu && !menu_lock) {
      tone(0); keys_set(0); pause_begin(); g_menu = 1; menu_lock = 1; g_msel = 1; g_mtop = 0; menu_items(); menu_draw(); mk0 = BTN_A | BTN_B | BTN_C;
    }
    if (!(b & (BTN_A | BTN_B | BTN_C))) menu_lock = 0;
    if (g_menu) {
      u32 k = b & (BTN_A | BTN_B | BTN_C), nk = k & ~mk0; mk0 = k; if (nk) menu_key(nk);
      t_us0 = g_ms; next_us = 0;
      __asm__ volatile("wfi");
      continue;
    }
    if (!bl) { tone(0); t_us0 = g_ms; next_us = 0; __asm__ volatile("wfi"); continue; }   /* screen off: the game waits (battery) */
    keys_set(pm_keys(b, now));
    /* one Pokemon mini frame every 1/72 s (13889 us) */
    u32 el_us = (now - t_us0) * 1000u;
    if ((s32)(el_us - next_us) >= 0) {
      if ((s32)(el_us - next_us) > 100000) { g_pmst[2] += (el_us - next_us) / 13889u; next_us = el_us; }   /* too far behind: drop the time */
      u32 t0 = g_ms;
      PokeMini_EmulateFrame();
      g_pmst[0]++; g_pmst[3] = g_ms - t0;
      next_us += 13889u;
      if (next_us > 1000000000u) { t_us0 += next_us / 1000u; next_us %= 1000u; }
      { int hz = 0, vol = 0; pm_audio_now(&hz, &vol); tone(!g_set.mute && vol && hz >= 40 && hz <= 12000 ? (u32)hz : 0u); }
      drawn ^= 1;
#ifdef PM_FASTLCD
#ifdef PM_PRC_MIX
      if (pm_prc_img) { lcd_take(); g_pend = 1; }
      if (g_pend && now - last_push >= 25u) { g_pend = 0; last_push = now; lcd_mix(); frame_grow(); push(0); g_pmst[1]++; }
#else
      if (!drawn) lcd_snap();
      else if (now - last_push >= 25u) { last_push = now; lcd_mix(); frame_grow(); push(0); g_pmst[1]++; }
#endif
#else
      if (drawn && now - last_push >= 25u) { last_push = now; frame_grow(); push(0); g_pmst[1]++; }
#endif
    }
    if (PokeMini_EEPROMWritten && !eep_ms) eep_ms = now ? now : 1;
    if (eep_ms && now - eep_ms > 3000u && now - last_in > 1000u) { eep_ms = 0; eep_store(); }
    if (bl && !g_dimmed && g_set.dim < T_N && now - last_in > T_MS[g_set.dim]) { g_dimmed = 1; colours(); push(1); }
    if (bl && g_set.off < T_N && now - last_in > T_MS[g_set.off]) { bl = 0; tone(0); hw_backlight(0); pause_begin(); if (PokeMini_EEPROMWritten) { eep_ms = 0; eep_store(); } }
    if ((s32)(next_us - (g_ms - t_us0) * 1000u) > 0) __asm__ volatile("wfi");
  }
}

__attribute__((naked)) static void Default_Handler(void) { __asm__ volatile("1: b 1b\n"); }
__attribute__((naked)) static void Irq_Off(void) { __asm__ volatile("cpsid i\n bx lr\n"); }
__attribute__((section(".vectors"), used))
const void *const g_vectors[16 + 64] = { (void *)0x18020000u, (void *)Reset_Handler,
  [2 ... 10] = (void *)Default_Handler, [11] = (void *)Irq_Off, [12 ... 13] = (void *)Default_Handler, [14] = (void *)Irq_Off,
  [15] = (void *)SysTick_Handler, [16 ... 79] = (void *)Irq_Off };
