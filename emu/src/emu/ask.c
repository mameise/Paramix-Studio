/* 1.6.6: "reset?" question when the wheel is pressed in the first 3 s after a resumed start (TamaEmu,
 * T18Emu). Before, the session was thrown away at once, a press meant for the menu lost the game.
 * A = yes, B or the wheel = no. Own little font (3x5, drawn twice as big). */
#include "hw.h"
#include "ask.h"
static const char AK[] = "ABCEIJKNORSTUYZ?";
static const u16 AG[] = { 0x2BED, 0x6BAE, 0x3923, 0x79E7, 0x7497, 0x126A, 0x5BAD, 0x6B6D, 0x7B6F, 0x6BAD, 0x388E, 0x7492, 0x5B6F, 0x5A92, 0x72A7, 0x7282 };
static u16 ask_glyph(char c) { for (int i = 0; AK[i]; i++) if (AK[i] == c) return AG[i]; return 0; }
static void ask_line(u16 *row, int y, int y0, const char *s, u16 fg)
{
  if (y < y0 || y >= y0 + 10) return;
  int n = 0; while (s[n]) n++;
  int x0 = (128 - n * 8 + 2) / 2, gy = (y - y0) / 2;
  for (int i = 0; i < n; i++) {
    u16 g = ask_glyph(s[i]);
    for (int cx = 0; cx < 6; cx++) if ((g >> (14 - (gy * 3 + cx / 2))) & 1) { int x = x0 + i * 8 + cx; if (x >= 0 && x < 128) row[x] = fg; }
  }
}
int hw_ask_reset(int en)
{
  const char *l1 = en ? "RESET?" : "ZURUECKSETZEN?", *l2 = en ? "A YES" : "A JA", *l3 = en ? "B NO" : "B NEIN";
  u16 row[128];
  hw_px_begin(0, 0, 128, 128);
  for (int y = 0; y < 128; y++) {
    for (int x = 0; x < 128; x++) row[x] = 0x0000;
    ask_line(row, y, 36, l1, 0xFFFF); ask_line(row, y, 66, l2, 0x07E0); ask_line(row, y, 86, l3, 0xF800);
    hw_px_row(row, 128);
  }
  hw_px_end();
  hw_backlight(1);
  for (u32 t = 0; hw_buttons() && t < 50000000u; t++) if ((t & 0xFFFFu) == 0) hw_wdt_feed();   /* the wheel let go first */
  int r = 0;
  for (u32 t = 0; ; t++) {
    if ((t & 0xFFFFu) == 0) hw_wdt_feed();
    u32 b = hw_buttons();
    if (b & BTN_A) { r = 1; break; }
    if (b & (BTN_B | BTN_WHEEL)) { r = 0; break; }
  }
  for (u32 t = 0; hw_buttons() && t < 50000000u; t++) if ((t & 0xFFFFu) == 0) hw_wdt_feed();
  return r;
}
