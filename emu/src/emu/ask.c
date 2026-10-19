/* 1.6.6: "reset?" question when the wheel is pressed in the first 3 s after a resumed start (TamaEmu,
 * T18Emu). Before, the session was thrown away at once, a press meant for the menu lost the game.
 * A = yes, B or the wheel = no.
 * 1.6.8 (OPT_MENU): settings on the device with a short press on the wheel (volume, brightness, screen off),
 * drawn like the menu of T7Emu and V1Emu: its 5x7 font, its colours (green screen, dark ink), title, line,
 * the chosen line dark. */
#include "hw.h"
#include "ask.h"
static const char FK[] = " %\'-./0123456789:?ABCDEFGHIJKLMNOPQRSTUVWXYZ";
static const u8 FG[] = {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x19,0x19,0x02,0x04,0x08,0x13,0x13,0x04,0x04,0x08,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x1f,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x0c,0x0c,0x01,0x01,0x02,0x04,0x08,0x10,0x10,0x0e,0x11,0x13,0x15,0x19,0x11,0x0e,0x04,0x0c,0x04,0x04,0x04,0x04,0x0e,0x0e,0x11,0x01,0x02,0x04,0x08,0x1f,0x1e,0x01,0x01,0x0e,0x01,0x01,0x1e,0x02,0x06,0x0a,0x12,0x1f,0x02,0x02,0x1f,0x10,0x1e,0x01,0x01,0x11,0x0e,0x0e,0x10,0x10,0x1e,0x11,0x11,0x0e,0x1f,0x01,0x02,0x04,0x08,0x08,0x08,0x0e,0x11,0x11,0x0e,0x11,0x11,0x0e,0x0e,0x11,0x11,0x0f,0x01,0x01,0x0e,0x00,0x04,0x04,0x00,0x04,0x04,0x00,0x0e,0x11,0x01,0x02,0x04,0x00,0x04,0x0e,0x11,0x11,0x1f,0x11,0x11,0x11,0x1e,0x11,0x11,0x1e,0x11,0x11,0x1e,0x0e,0x11,0x10,0x10,0x10,0x11,0x0e,0x1e,0x11,0x11,0x11,0x11,0x11,0x1e,0x1f,0x10,0x10,0x1e,0x10,0x10,0x1f,0x1f,0x10,0x10,0x1e,0x10,0x10,0x10,0x0e,0x11,0x10,0x17,0x11,0x11,0x0f,0x11,0x11,0x11,0x1f,0x11,0x11,0x11,0x0e,0x04,0x04,0x04,0x04,0x04,0x0e,0x07,0x02,0x02,0x02,0x02,0x12,0x0c,0x11,0x12,0x14,0x18,0x14,0x12,0x11,0x10,0x10,0x10,0x10,0x10,0x10,0x1f,0x11,0x1b,0x15,0x15,0x11,0x11,0x11,0x11,0x19,0x15,0x13,0x11,0x11,0x11,0x0e,0x11,0x11,0x11,0x11,0x11,0x0e,0x1e,0x11,0x11,0x1e,0x10,0x10,0x10,0x0e,0x11,0x11,0x11,0x15,0x12,0x0d,0x1e,0x11,0x11,0x1e,0x14,0x12,0x11,0x0f,0x10,0x10,0x0e,0x01,0x01,0x1e,0x1f,0x04,0x04,0x04,0x04,0x04,0x04,0x11,0x11,0x11,0x11,0x11,0x11,0x0e,0x11,0x11,0x11,0x11,0x11,0x0a,0x04,0x11,0x11,0x11,0x15,0x15,0x15,0x0a,0x11,0x11,0x0a,0x04,0x0a,0x11,0x11,0x11,0x11,0x0a,0x04,0x04,0x04,0x04,0x1f,0x01,0x02,0x04,0x08,0x10,0x1f};
static const u8 *ask_glyph(char c) { if (c >= 'a' && c <= 'z') c = (char)(c - 32); for (int i = 0; FK[i]; i++) if (FK[i] == c) return FG + 7 * i; return FG; }
static int slen(const char *s) { int n = 0; while (s && s[n]) n++; return n; }
/* one text line from y0 (7 rows, scale 1 or 2); x0 < 0: centred */
static void ask_line(u16 *row, int y, int y0, const char *s, u16 fg, int x0, int sc)
{
  if (!s || y < y0 || y >= y0 + 7 * sc) return;
  int n = slen(s);
  if (x0 < 0) x0 = (128 - n * 6 * sc + sc) / 2;
  int gy = (y - y0) / sc;
  for (int i = 0; i < n; i++) {
    u8 g = ask_glyph(s[i])[gy];
    for (int cx = 0; cx < 5 * sc; cx++) if (g & (0x10 >> (cx / sc))) { int x = x0 + i * 6 * sc + cx; if (x >= 0 && x < 128) row[x] = fg; }
  }
}
static void ask_release(void) { for (u32 t = 0; hw_buttons() && t < 50000000u; t++) if ((t & 0xFFFFu) == 0) hw_wdt_feed(); }
#ifdef OPT_MENU
/* 1.6.8 (test41): the keys bounce; one press of B counted once or twice. A state counts only when it held for
 * 20 samples about 1 ms apart. */
static void ask_ms(void) { for (volatile u32 i = 0; i < 16000u; i++) { } hw_wdt_feed(); }
static u32 ask_stable(void)
{
  u32 v = hw_buttons();
  for (int n = 0; n < 20; ) { ask_ms(); u32 w = hw_buttons(); if (w == v) n++; else { v = w; n = 0; } }
  return v;
}
static void ask_release_db(void) { while (ask_stable()) { } }
static u32 ask_press_db(void) { u32 b; while (!(b = ask_stable())) { } return b; }
#endif
/* colours of the T7Emu menu with its default screen colour: background, 90 %, 60 %, ink */
#define C_BG 0xAE66u
#define C_LINE 0x6BE4u
#define C_INK 0x0000u
int hw_ask_reset(int en)
{
  const char *l1 = en ? "RESET?" : "ZURUECKSETZEN?", *l2 = en ? "A YES" : "A JA", *l3 = en ? "B NO" : "B NEIN";
  u16 row[128];
  hw_px_begin(0, 0, 128, 128);
  for (int y = 0; y < 128; y++) {
    for (int x = 0; x < 128; x++) row[x] = C_BG;
    ask_line(row, y, 36, l1, C_INK, -1, 1); ask_line(row, y, 60, l2, C_INK, -1, 1); ask_line(row, y, 76, l3, C_INK, -1, 1);
    hw_px_row(row, 128);
  }
  hw_px_end();
  hw_backlight(1);
  ask_release();   /* the wheel let go first */
  int r = 0;
  for (u32 t = 0; ; t++) {
    if ((t & 0xFFFFu) == 0) hw_wdt_feed();
    u32 b = hw_buttons();
    if (b & BTN_A) { r = 1; break; }
    if (b & (BTN_B | BTN_WHEEL)) { r = 0; break; }
  }
  ask_release();
  return r;
}

#ifdef OPT_MENU
/* Settings: o holds them as the flags of the program (bits 16-17 volume, 18-19 brightness, 20-22 screen off).
 * A next line, B change, C or the wheel: close. Returns the new bits. */
static const char *const VOL_DE[4] = { "Normal", "Leise", "Laut", "Aus" }, *const VOL_EN[4] = { "Normal", "Quiet", "Loud", "Off" };
/* 1.6.8 (test45): the light in 11 steps, bits 24-27 = step + 1 (0 = the old bits 18-19: 100, 75, 50, 25 %).
 * Step 0 = 100 %, then 5, 10, 20 .. 90 %: B goes up, after 100 % it starts at 5 % again. */
static const u8 OPT_LV[11] = { 100, 5, 10, 20, 30, 40, 50, 60, 70, 80, 90 };
static u32 opt_step(u32 o) { u32 n = (o >> 24) & 15u; if (n && n <= 11u) return n - 1u; { static const u8 m[4] = { 0, 8, 6, 3 }; return m[(o >> 18) & 3u]; } }
u32 opt_light_pct(u32 o) { return OPT_LV[opt_step(o)]; }
static const char *const OFF_DE[8] = { "Auto", "10 s", "20 s", "30 s", "1 Min", "2 Min", "5 Min", "10 Min" };
static const char *const OFF_EN[8] = { "Auto", "10 s", "20 s", "30 s", "1 min", "2 min", "5 min", "10 min" };
#define MY 22
u32 hw_opts_menu(u32 o, int en)
{
  int sel = 0;
  ask_release_db();
  for (;;) {
    u32 v = (o >> 16) & 3u, d = opt_step(o), s = (o >> 20) & 7u;
    char dt[6]; { u32 p = OPT_LV[d], n = 0; if (p >= 100u) dt[n++] = '1'; if (p >= 10u) dt[n++] = (char)('0' + (p / 10u) % 10u); dt[n++] = (char)('0' + p % 10u); dt[n++] = '%'; dt[n] = 0; }
    const char *lab[3] = { en ? "Volume" : "Lautstaerke", en ? "Brightness" : "Helligkeit", en ? "Screen off" : "Bild aus nach" };
    const char *val[3] = { (en ? VOL_EN : VOL_DE)[v], dt, (en ? OFF_EN : OFF_DE)[s] };
    u16 row[128];
    hw_px_begin(0, 0, 128, 128);
    for (int y = 0; y < 128; y++) {
      int my = y - MY;
      u16 bg = C_BG;
      int k = (my - 12) / 10, in = my >= 12 && k >= 0 && k < 3 && (my - 12) % 10 < 9;   /* the chosen line: rows y-1 .. y+7 */
      if (in && k == sel) bg = C_INK;
      for (int x = 0; x < 128; x++) row[x] = (my == 10) ? C_LINE : bg;
      ask_line(row, my, 1, en ? "SETTINGS" : "EINSTELLUNGEN", C_INK, 2, 1);
      for (int i = 0; i < 3; i++) {
        u16 fg = i == sel ? C_BG : C_INK; int yy = 13 + i * 10;
        ask_line(row, my, yy, lab[i], fg, 2, 1);
        ask_line(row, my, yy, val[i], fg, 128 - 2 - 6 * slen(val[i]), 1);
      }
      hw_px_row(row, 128);
    }
    hw_px_end();
    u32 b = ask_press_db();
    ask_release_db();
    if (b & (BTN_C | BTN_WHEEL)) return o;
    if (b & BTN_A) sel = (sel + 1) % 3;
    if (b & BTN_B) {
      if (sel == 0) o = (o & ~(3u << 16)) | (((v + 1) & 3u) << 16);
      else if (sel == 1) {
        o = (o & ~((3u << 18) | (15u << 24))) | ((((d + 1u) % 11u) + 1u) << 24);
#ifdef BL_PWM
        hw_bl_set(opt_light_pct(o));   /* 1.6.8: seen right away */
#endif
      }
      else o = (o & ~(7u << 20)) | (((s + 1) & 7u) << 20);
    }
  }
}
#endif
