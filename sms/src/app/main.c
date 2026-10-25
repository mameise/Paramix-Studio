/* SMSEmu: the Sega Master System on the Tamagotchi Paradise. Core: ../core (own Z80, VDP mode 4, Sega mapper).
 * Started from the Paramix menu like the other further programs. Bring your own ROM (.sms).
 * Screen: 256 x 192 shrunk to 128 x 96 (every 2 x 2 dots mixed), black bars above and below.
 * Keys: set in the Studio (tab Master System), stored in the settings block. A+B+C together: settings (also
 * pause and sound); wheel held 2 s: save and back to the Paradise. Sound: the Paradise's buzzer plays the loudest
 * of the three tone channels. Save: the cartridge RAM (battery save) goes to the flash a few seconds after the game
 * wrote it, and on leaving.
 * Author: mameise */
#include "../../../emu/src/emu/hw.h"
#include "../../../emu/src/emu/flash.h"
#define PARA_ATTR
#include "../../../emu/src/emu/paraclock.h"
#ifdef PARA_BACK
#include "../../../emu/src/emu/paraback.h"
#endif
typedef int s32;

/* ---- settings block at 0x140, filled in by the Studio ----
 * keys[i]: Master System buttons (1 up, 2 down, 4 left, 8 right, 16 button 1, 32 button 2, 64 pause)
 * for the Paradise input i: 0 A, 1 B, 2 C, 3 wheel press, 4 wheel up, 5 wheel down, 6 A+C, 7 B+C, 8 A+B.
 * flags: bit 0 English, bit 1 wheel the other way round.
 * save_off: 4 KB settings of the device, then the cartridge RAM (32 KB). */
#define NIN 9
typedef struct { u32 magic, version, rom_off, rom_len, save_off, flags; char name[16]; u8 keys[12]; } SmsCfg;
#define SMS_MAGIC 0x53584D50u   /* "PMXS" */
__attribute__((section(".pmxcfg"), used))
volatile const SmsCfg g_cfg = { SMS_MAGIC, 1, 0, 0, 0, 2, {0}, { 0x04, 0x10, 0x08, 0x20, 0x01, 0x02, 0x40, 0x00, 0x00 } };
#define SETTINGS (g_cfg.save_off)
#define CRAM_AT (g_cfg.save_off + 0x1000u)
#define CRAM_MAX 0x8000u
#define XIP_BASE_REG REG(0x4002F004u)
#define XIP_CTRL_REG REG(0x4002F000u)
#define ROMWIN ((const u8 *)0x10000000u)

/* ---- screen: lines come from the core while a frame runs; every two lines give one row of 128 ---- */
/* Master System 128 x 96 from y 16; Game Gear (SMS_GG): 160 x 144 to 128 x 115 from y 6 */
static int OY = 16, OH = 96;
#define OH_MAX 115
static void sms_line(int y, const u8 *lb);
#define SMS_LINE(y, lb) sms_line((y), (lb))
#include "../core/sms.h"
#include "font.h"

/* ---- time: SysTick 1 ms; the wheel's encoder is read here too, so no step is lost while a frame runs ---- */
static volatile u32 g_ms;
static u32 g_tone_now;
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
typedef struct { u8 magic[4]; u8 r1, dim, lang, dimto, off, r0, r2, mute; } SmsSet;   /* mute: 0 = sound on */
static SmsSet g_set = { {'S','M','S','1'}, 0, 7, 0, 6, 3, 0x80, 0, 0 };
#define T_N 7
static const u32 T_MS[T_N] = { 10000u, 20000u, 30000u, 60000u, 120000u, 300000u, 600000u };
static const char *const T_NM[2][T_N + 1] = { { "10 S", "20 S", "30 S", "1 MIN", "2 MIN", "5 MIN", "10 MIN", "NIE" }, { "10 S", "20 S", "30 S", "1 MIN", "2 MIN", "5 MIN", "10 MIN", "NEVER" } };
#define LV_N 11
#define DIM2_N 10
static const u8 LV_PCT[LV_N] = { 100u, 5u, 10u, 20u, 30u, 40u, 50u, 60u, 70u, 80u, 90u };
static const u8 DIM2_PCT[DIM2_N] = { 1u, 2u, 3u, 5u, 10u, 15u, 20u, 30u, 40u, 50u };
static int en(void) { return g_set.lang ? g_set.lang == 2u : (int)(g_cfg.flags & 1u); }
#define BG 0x0000u
#define INK 0xFFFFu
#define OFF 0x7BEFu
static int g_dimmed;
static void light(void)
{
#ifdef BL_PWM
  u32 l = LV_PCT[(g_set.r0 & 15u) % LV_N], d = DIM2_PCT[g_set.dimto % DIM2_N];
  hw_bl_set(g_dimmed ? (d < l ? d : l) : l);
#endif
}
static u8 g_sec[0x1000] __attribute__((aligned(4)));
static void set_load(void)
{
  const volatile u8 *p = XIP_PTR(SETTINGS); SmsSet t; u8 *d = (u8 *)&t;
  for (u32 i = 0; i < sizeof t; i++) d[i] = p[i];
  if (t.magic[0] == 'S' && t.magic[1] == 'M' && t.magic[2] == 'S' && t.magic[3] == '1' && t.dim <= T_N && t.off <= T_N && t.lang < 3u) g_set = t;
  light();
}
static void set_store(void)
{
  for (u32 i = 0; i < sizeof g_sec; i++) g_sec[i] = 0xFF;
  const u8 *s = (const u8 *)&g_set; for (u32 i = 0; i < sizeof g_set; i++) g_sec[i] = s[i];
  flash_update4k(SETTINGS, g_sec);
}

/* ---- the cartridge RAM: 4 KB pages that changed go to the flash ---- */
static u8 g_cram[CRAM_MAX] __attribute__((aligned(4)));
static u32 g_cram_seen;   /* pages the game wrote since the last store */
static void cram_load(void)
{
  const volatile u8 *p = XIP_PTR(CRAM_AT);
  for (u32 i = 0; i < CRAM_MAX; i++) g_cram[i] = p[i];
}
static void cram_store(void)
{
  u32 m = (u32)g_sms.cram_dirty; g_sms.cram_dirty = 0; g_cram_seen = 0;
  for (u32 pg = 0; pg < CRAM_MAX / 0x1000u; pg++) {
    if (!(m & (1u << pg))) continue;
    for (u32 i = 0; i < 0x1000u; i++) g_sec[i] = g_cram[pg * 0x1000u + i];
    hw_tone(0); g_tone_now = 0;
    flash_update4k(CRAM_AT + pg * 0x1000u, g_sec);
  }
}

/* ---- sound: the loudest of the three tone channels (the noise channel never) ---- */
static void psg_frame(void)
{
  Sms *s = &g_sms; u32 hz = 0;
  if (!g_set.mute) {
    int best = -1; u32 bv = 15;
    for (int c = 0; c < 3; c++) { u32 v = s->vol[c] & 15u, n = s->tone[c]; if (v < bv && n >= 8u) { bv = v; best = c; } }
    if (best >= 0) { hz = 3579545u / (32u * s->tone[best]); if (hz < 40u || hz > 10000u) hz = 0; }
  }
  /* as GB_TONE_RESTART: every new pitch starts the timer anew; changes below about 1.5 % are left out */
  if (hz && g_tone_now) { u32 d = hz > g_tone_now ? hz - g_tone_now : g_tone_now - hz; if (d * 64u < g_tone_now) return; }
  if (hz != g_tone_now) { if (hz && g_tone_now) hw_tone(0); g_tone_now = hz; hw_tone(hz); }
}
static void psg_quiet(void) { if (g_tone_now) { g_tone_now = 0; hw_tone(0); } }

/* ---- screen ---- */
static u16 g_ha[128] __attribute__((aligned(4)));   /* the even line, two dots mixed into one */
static u16 g_row[128] __attribute__((aligned(4)));
static u32 g_hash[OH_MAX];
static int g_draw, g_win = -1, g_force;
static inline u32 mix2(u32 a, u32 b) { return (a & b) + (((a ^ b) & 0xF7DEu) >> 1); }
static void row_out(int r, u32 h)
{
  if (h == g_hash[r] && !g_force) return;   /* the row as it is on the display */
  g_hash[r] = h;
  if (g_win != r) { if (g_win >= 0) hw_px_end(); hw_px_begin(0, (u32)(OY + r), 128, (u32)(OY + OH)); }
  hw_px_row(g_row, 128);
  g_win = r + 1;
}
#ifdef SMS_GG
/* Game Gear: the 160 x 144 window (from x 48, y 24); 5 dots to 4 across and 5 lines to 4 down: the outer ones as they
 * are, the middle ones mixed from two (as GBEmu, nothing is cut off) */
static u16 g_gl[2][128] __attribute__((aligned(4)));
static void gg_hrow(const u8 *lb, u16 *o)
{
  const u16 *pal = g_sms.pal565;
  for (int k = 0; k < 32; k++, lb += 5, o += 4) {
    u32 p1 = pal[lb[1]], p2 = pal[lb[2]], p3 = pal[lb[3]];
    o[0] = pal[lb[0]]; o[1] = (u16)mix2(p1, p2); o[2] = (u16)mix2(p2, p3); o[3] = pal[lb[4]];
  }
}
static u32 row_hash(void) { u32 h = 2166136261u; const u32 *w = (const u32 *)g_row; for (int x = 0; x < 64; x++) h = (h ^ w[x]) * 16777619u; return h; }
static void gg_line(int y, const u8 *lb)
{
  int g = y / 5, l = y % 5, r = g * 4;
  switch (l) {
    case 0: gg_hrow(lb, g_row); break;
    case 1: gg_hrow(lb, g_gl[0]); return;
    case 2: gg_hrow(lb, g_gl[1]); for (int x = 0; x < 128; x++) g_row[x] = (u16)mix2(g_gl[0][x], g_gl[1][x]); r += 1; break;
    case 3: gg_hrow(lb, g_gl[0]); for (int x = 0; x < 128; x++) g_row[x] = (u16)mix2(g_gl[1][x], g_gl[0][x]); r += 2; break;
    default: gg_hrow(lb, g_row); r += 3; break;
  }
  if (r < OH) row_out(r, row_hash());
}
#endif
static void sms_line(int y, const u8 *lb)
{
  const u16 *pal = g_sms.pal565;
#ifdef SMS_GG
  if (g_sms.gg) { if (y >= 24 && y < 168) gg_line(y - 24, lb + 48); return; }
#endif
  if (!(y & 1)) { for (int x = 0; x < 128; x++) g_ha[x] = (u16)mix2(pal[lb[2 * x]], pal[lb[2 * x + 1]]); return; }
  u32 h = 2166136261u;
  for (int x = 0; x < 128; x += 2) {
    u32 a = mix2(g_ha[x], mix2(pal[lb[2 * x]], pal[lb[2 * x + 1]]));
    u32 b = mix2(g_ha[x + 1], mix2(pal[lb[2 * x + 2]], pal[lb[2 * x + 3]]));
    g_row[x] = (u16)a; g_row[x + 1] = (u16)b;
    h = (h ^ (a | b << 16)) * 16777619u;
  }
  row_out(y >> 1, h);
}
static void bars(void)
{
  for (int x = 0; x < 128; x++) g_row[x] = 0;
  hw_px_begin(0, 0, 128, (u32)OY); for (int y = 0; y < OY; y++) hw_px_row(g_row, 128); hw_px_end();
  hw_px_begin(0, (u32)(OY + OH), 128, 128); for (int y = OY + OH; y < 128; y++) hw_px_row(g_row, 128); hw_px_end();
}
static void redraw_all(void) { bars(); g_force = 1; for (int i = 0; i < OH; i++) g_hash[i] = 0; }

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
enum { M_PAUSE, M_SND, M_LIGHT, M_DIM, M_DIMTO, M_OFF, M_RESET, M_LANG, M_N };
static u32 g_pause_until;   /* menu line PAUSE: pressed for 200 ms after the menu closes */
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
    case M_PAUSE: *a = g_sms.gg ? (e ? "PRESS START" : "START DRUECKEN") : (e ? "PRESS PAUSE" : "PAUSE DRUECKEN"); v = "B"; break;
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
static void menu_close(void) { g_menu = 0; g_reset_ask = 0; if (g_set_dirty) { set_store(); g_set_dirty = 0; } redraw_all(); }
static void menu_key(u32 k)
{
  if (k & BTN_A) { g_msel = (g_msel + 1) % g_nitems; g_reset_ask = 0; }
  if (k & BTN_B) {
    switch (g_items[g_msel]) {
      case M_PAUSE: g_pause_until = g_ms + 200u; menu_close(); return;
      case M_SND: g_set.mute ^= 1u; break;
      case M_LIGHT: g_set.r0 = (u8)(0x80u | (((g_set.r0 & 15u) + 1u) % LV_N)); break;
      case M_DIM: g_set.dim = (u8)((g_set.dim + 1u) % (T_N + 1u)); if (g_set.dim < T_N && g_set.off < T_N && g_set.off <= g_set.dim) g_set.off = (u8)(g_set.dim + 1u); break;
      case M_DIMTO: g_set.dimto = (u8)((g_set.dimto + 1u) % DIM2_N); break;
      case M_OFF: do g_set.off = (u8)((g_set.off + 1u) % (T_N + 1u)); while (g_set.off < T_N && g_set.dim < T_N && g_set.off <= g_set.dim); break;
      case M_RESET: if (g_reset_ask) { g_reset_ask = 0; if (g_sms.cram_dirty) cram_store(); sms_reset(); menu_close(); return; } g_reset_ask = 1; break;
      default: g_set.lang = (u8)(en() ? 1u : 2u); break;
    }
    if (g_items[g_msel] != M_RESET && g_items[g_msel] != M_PAUSE) g_set_dirty = 1;
    light(); menu_items();
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

/* ---- keys: the Paradise inputs to the Master System's buttons (map from the Studio) ---- */
static u32 g_wheel_until[2];   /* wheel up / down held until (ms) */
static u32 sms_keys(u32 b, u32 now)
{
  u32 in = 0;
  int ac = (b & (BTN_A | BTN_C)) == (BTN_A | BTN_C), bc = (b & (BTN_B | BTN_C)) == (BTN_B | BTN_C), ab = (b & (BTN_A | BTN_B)) == (BTN_A | BTN_B);
  u32 used = 0;
  if (ac && g_cfg.keys[6]) { in |= 1u << 6; used |= BTN_A | BTN_C; }
  if (bc && g_cfg.keys[7]) { in |= 1u << 7; used |= BTN_B | BTN_C; }
  if (ab && g_cfg.keys[8]) { in |= 1u << 8; used |= BTN_A | BTN_B; }
  u32 r = b & ~used;
  if (r & BTN_A) in |= 1u; if (r & BTN_B) in |= 2u; if (r & BTN_C) in |= 4u; if (b & BTN_WHEEL) in |= 8u;
  if ((s32)(g_wheel_until[0] - now) > 0) in |= 16u;
  if ((s32)(g_wheel_until[1] - now) > 0) in |= 32u;
  u32 k = 0;
  for (int i = 0; i < NIN; i++) if (in & (1u << i)) k |= g_cfg.keys[i];
  if ((s32)(g_pause_until - now) > 0) k |= SMS_PAUSE;
  return k;
}

#ifdef SMS_STATE
/* ---- 1.6.11 test2: the whole machine on leaving (wheel held), read once at the next start ---- */
#include "../../../emu/src/emu/pstate.h"
#define STATE_AT (g_cfg.save_off + 0x9000u)
#define STATE_MAX 0x7000u
static int g_running;
static u32 st_tag(void) { return 0x534D0001u ^ (u32)&g_sms ^ ((u32)sizeof(Sms) << 8) ^ ((u32)sizeof(Z80) << 20) ^ g_cfg.rom_len ^ (u32)sms_line; }
static int st_parts(StPart *pt)
{
  pt[0].p = &g_cpu; pt[0].n = sizeof g_cpu; pt[1].p = &g_sms; pt[1].n = sizeof g_sms;
  pt[2].p = g_ram; pt[2].n = sizeof g_ram; pt[3].p = g_vram; pt[3].n = sizeof g_vram;
  return 4;
}
static void state_store(void) { StPart pt[4]; int n = st_parts(pt); if (g_running) st_store(STATE_AT, STATE_MAX, st_tag(), pt, n, g_sec); }
static int state_load(void)
{
  StPart pt[4]; int n = st_parts(pt);
  const uint8_t *rom = g_sms.rom; uint32_t rl = g_sms.rom_len; uint8_t *cr = g_sms.cram; uint32_t cl = g_sms.cram_len;
  int ok = st_load(STATE_AT, STATE_MAX, st_tag(), pt, n);
  if (ok) {   /* this program's pointers again, the pages and the decoded tiles from the state */
    g_sms.rom = rom; g_sms.rom_len = rl; g_sms.cram = cr; g_sms.cram_len = cl; g_sms.cram_dirty = 0;
    g_sms.rp[0] = rom; sms_pages(0); sms_pages(1); sms_pages(2);
    for (int i = 48; i < 64; i++) g_sms.rp[i] = g_ram + (i & 7) * 0x400;
    for (uint32_t a = 0; a < 0x4000u; a += 4u) tc_row(a);
  }
  st_drop(STATE_AT);
  return ok;
}
#endif

/* ---- leaving ---- */
static void leave(void)
{
  psg_quiet(); hw_tone(0);
  if (g_set_dirty) set_store();
  REG(0x20000FF8u) = 0;
  REG(0x20000FFCu) = 0x4C584D50u;        /* "PMXL": back on purpose, the autostart stays off */
#ifdef SMS_STATE
  if (g_running) { message(en() ? "SAVING" : "SPEICHERN", ""); state_store(); }
#endif
  if (g_sms.cram_dirty) cram_store();
#ifdef PARA_BACK
  para_back_fix(g_sec);
#endif
  hw_bl_pwm_off();
  for (u32 t = 0; hw_buttons() && t < 20000000u; t++) if ((t & 0xFFFFu) == 0) hw_wdt_feed();
  XIP_BASE_REG = 0x60011000u; XIP_CTRL_REG = 3u;
  hw_back_to_paradise();
}

__attribute__((used)) volatile u32 g_smsst[4];   /* frames run, frames drawn, frames dropped, last frame in ms (for the tests) */
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
  REG(0xE000ED14u) &= ~(1u << 3);                 /* UNALIGN_TRP off */
  hw_init();
  hw_tone(0);
  XIP_BASE_REG = 0x60000000u | g_cfg.rom_off;     /* the ROM in the cached window from 0x10000000 */
  XIP_CTRL_REG = 3u;
  __asm__ volatile("dsb\n isb" ::: "memory");
  REG(GPIO2 + 0x04) &= ~3u; g_enc_last = REG(GPIO2) & 3u;
  set_load();
  {   /* the 512 byte copier header some dumps carry is left out */
    u32 off = (g_cfg.rom_len & 0x3FFFu) == 512u ? 512u : 0u;
    g_sms.rom = ROMWIN + off; g_sms.rom_len = g_cfg.rom_len - off;
  }
  if (g_sms.rom_len < 0x2000u || g_sms.rom_len > 0x100000u) {
    message(en() ? "ROM NOT USABLE" : "ROM GEHT NICHT", "");
    for (u32 t = 0; t < 30000000u; t++) if ((t & 0xFFFFu) == 0) hw_wdt_feed();
    leave();
  }
  g_sms.cram = g_cram; g_sms.cram_len = CRAM_MAX;
#ifdef SMS_GG
  if (g_cfg.flags & 4u) { g_sms.gg = 1; OY = 6; OH = 115; }   /* flags bit 2: Game Gear */
#endif
  cram_load();
  sms_reset();
#ifdef SMS_STATE
  state_load(); g_running = 1;
#endif
  redraw_all();
  SYST_RVR = CORE_HZ / 1000u - 1u; SYST_CVR = 0; SYST_CSR = 7;
  __asm__ volatile("cpsie i");

  u32 last_in = g_ms, wheel_ms = 0, cram_ms = 0, next_ms = g_ms, frac = 0, mk0 = 0;
  int skip_run = 0;
  int bl = 1, lock = 1, menu_lock = 0;
  for (;;) {
    hw_wdt_feed();
    u32 now = g_ms;
    u32 b = hw_buttons();
    if (lock) { if (b) b = 0; else lock = 0; }
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
      if (!bl || g_dimmed) {
        bl = 1; hw_backlight(1); if (g_dimmed) { g_dimmed = 0; light(); }
        if (g_menu) menu_draw(); else redraw_all();
        while (hw_buttons()) hw_wdt_feed();
        continue;
      }
    }
    if (b & BTN_WHEEL) { if (!wheel_ms) wheel_ms = now ? now : 1; else if (now - wheel_ms > 2000u) leave(); } else wheel_ms = 0;
    if ((b & (BTN_A | BTN_B | BTN_C)) == (BTN_A | BTN_B | BTN_C) && !g_menu && !menu_lock) {
      psg_quiet(); g_menu = 1; menu_lock = 1; g_msel = 1; g_mtop = 0; menu_items(); menu_draw(); mk0 = BTN_A | BTN_B | BTN_C; g_sms.pad = 0;
    }
    if (!(b & (BTN_A | BTN_B | BTN_C))) menu_lock = 0;
    if (g_menu) {
      u32 k = b & (BTN_A | BTN_B | BTN_C), nk = k & ~mk0; mk0 = k; if (nk) menu_key(nk);
      next_ms = now;
      __asm__ volatile("wfi");
      continue;
    }
    if (!bl) { psg_quiet(); next_ms = now; __asm__ volatile("wfi"); continue; }   /* screen off: the game waits */
    g_sms.pad = (u8)sms_keys(b, now);
    /* one frame every 16.68 ms (262 lines of 228 cycles at 3.58 MHz); drawn when there is time */
    if ((s32)(now - next_ms) >= 0) {
      if ((s32)(now - next_ms) > 100) { g_smsst[2] += (now - next_ms) / 17u; next_ms = now; }
      int behind = (s32)(now - next_ms) > 0;
      g_draw = skip_run >= 3 || (skip_run >= 1 && !behind);
      skip_run = g_draw ? 0 : skip_run + 1;
      u32 t0 = g_ms;
      sms_frame(g_draw);
      if (g_win >= 0) { hw_px_end(); g_win = -1; }
      if (g_draw) { g_smsst[1]++; g_force = 0; }
      g_smsst[0]++; g_smsst[3] = g_ms - t0;
      psg_frame();
      frac += 683u; next_ms += 16u + frac / 1000u; frac %= 1000u;   /* 16.68 ms */
    }
    if (g_sms.cram_dirty) { if (!cram_ms) cram_ms = now ? now : 1; }
    if (cram_ms && now - cram_ms > 3000u && now - last_in > 1000u) { cram_ms = 0; cram_store(); }
    if (bl && !g_dimmed && g_set.dim < T_N && now - last_in > T_MS[g_set.dim]) { g_dimmed = 1; light(); }
    if (bl && g_set.off < T_N && now - last_in > T_MS[g_set.off]) { bl = 0; hw_backlight(0); if (g_sms.cram_dirty) { cram_ms = 0; cram_store(); } }
    if ((s32)(next_ms - g_ms) > 0) __asm__ volatile("wfi");
  }
}

__attribute__((naked)) static void Default_Handler(void) { __asm__ volatile("1: b 1b\n"); }
__attribute__((naked)) static void Irq_Off(void) { __asm__ volatile("cpsid i\n bx lr\n"); }
__attribute__((section(".vectors"), used))
const void *const g_vectors[16 + 64] = { (void *)0x18020000u, (void *)Reset_Handler,
  [2 ... 10] = (void *)Default_Handler, [11] = (void *)Irq_Off, [12 ... 13] = (void *)Default_Handler, [14] = (void *)Irq_Off,
  [15] = (void *)SysTick_Handler, [16 ... 79] = (void *)Irq_Off
};
