/* batt.h - 1.6.9 test17 (switch BAT_SHOW): the battery voltage, as the Paradise firmware measures it.
 * Found in the firmware (and the Paradise emulator capybara): SAR ADC at 0x4003A000, channel 4 (P0.6, ADP bits 13..15),
 * ADM bit 15 on and bit 0 starts a conversion, the result in ADR bits 6..15 (10 bits). The firmware takes
 * volts = sample * 0xD33300 / 2^32 (3.3 V for 1024) and switches off below 2.2 V ("replace the battery").
 * Header only. Author: mameise */
#ifndef BATT_H
#define BATT_H
#include "io.h"
#define BAT_ADP REG(0x4003A000u)
#define BAT_ADM REG(0x4003A004u)
#define BAT_ADR REG(0x4003A008u)
/* millivolts, 0 = no reading (8 conversions, each waited for at most about 2 ms) */
__attribute__((noinline, unused)) static u32 bat_mv(void)
{
  REG(0x45000100u) |= 1u << 12;                     /* SAR ADC clock */
  BAT_ADP = (BAT_ADP & ~0xE000u) | 0x8000u;         /* channel 4 */
  BAT_ADM |= 0x8000u;                               /* on */
  u32 sum = 0, n = 0;
  for (int i = 0; i < 8; i++) {
    BAT_ADM |= 1u;
    u32 t = 0; while (!(BAT_ADM & 2u) && ++t < 100000u) { }
    if (t >= 100000u) break;
    sum += (BAT_ADR >> 6) & 0x3FFu; n++;
  }
  return n ? sum * 3300u / (n * 1024u) : 0u;
}
/* 2 x AAA: 3.0 V and more full, 2.2 V the Paradise switches off */
static inline u32 bat_pct(u32 mv) { return mv >= 3000u ? 100u : mv <= 2200u ? 0u : (mv - 2200u) * 100u / 800u; }
/* "2.84V 80%" (or "?" without a reading) into s (12 bytes) */
__attribute__((noinline, unused)) static void bat_text(char *s)
{
  u32 mv = bat_mv(), k = 0;
  if (!mv) { s[0] = '?'; s[1] = 0; return; }
#ifdef BAT_SMOOTH
  /* 1.6.13 test7: steadier (the reading moved with the load, e.g. while scrolling a menu): v = f v + (1 - f) new,
   * f = 7/8, in 1/16 mV; the first reading is taken as it is */
  { static u32 g_bat16; u32 n = mv << 4; g_bat16 = g_bat16 ? (g_bat16 * 7u + n + 4u) >> 3 : n; mv = (g_bat16 + 8u) >> 4; }
#endif
  u32 p = bat_pct(mv);
  s[k++] = (char)('0' + mv / 1000u); s[k++] = '.'; s[k++] = (char)('0' + (mv / 100u) % 10u); s[k++] = (char)('0' + (mv / 10u) % 10u); s[k++] = 'V'; s[k++] = ' ';
  if (p >= 100u) s[k++] = '1';
  if (p >= 10u) s[k++] = (char)('0' + (p / 10u) % 10u);
  s[k++] = (char)('0' + p % 10u); s[k++] = '%'; s[k] = 0;
}
#ifdef BAT_GUARD
/* test21 (switch BAT_GUARD): the Paradise firmware switches itself off below 2.2 V; under an emulator it ran on until
 * the power broke down, perhaps in the middle of a flash write (a lost save). Read every 20 s: two readings in a row
 * below BAT_LOW_MV and the app saves everything once and switches off (nothing is written any more). */
#ifndef BAT_LOW_MV
#define BAT_LOW_MV 2210u   /* test23: 2.21 V (was 2.25 V), the Paradise itself goes off at 2.2 V */
#endif
__attribute__((noinline, unused, minsize)) static int bat_is_low(void)
{
  REG(0x45000100u) |= 1u << 12; BAT_ADP = (BAT_ADP & ~0xE000u) | 0x8000u; BAT_ADM |= 0x8000u;
  u32 s = 0;
  for (int i = 0; i < 4; i++) { BAT_ADM |= 1u; u32 t = 0; while (!(BAT_ADM & 2u)) if (++t > 100000u) return 0; s += (BAT_ADR >> 6) & 0x3FFu; }
  return s < BAT_LOW_MV * 4096u / 3300u;
}
#ifdef BAT_PIC
/* test23 (switch BAT_PIC): the Paradise's own "battery empty" picture (sprite 53 of the ROM) before switching off, as
 * the Paradise does. The Studio puts it at 0xFFF000 (last sector of the rescue block): "BAT1", byte count, 16 colours
 * (RGB565), then one byte per run: colour << 4 | (length - 1), 128 x 128 pixels row by row. Shown for about 3 s (the caller is at full clock). */
#include "hw.h"
#ifndef BAT_PIC_ATTR
#define BAT_PIC_ATTR __attribute__((noinline, unused, minsize))
#endif
BAT_PIC_ATTR static void bat_pic(void)
{
  const volatile u32 *h = (const volatile u32 *)0x60FFF000u;
  u32 n = h[1];
  if (h[0] != 0x31544142u || n > 4000u) return;
  u16 pal[16], row[128]; u32 x = 0, y = 0;
  for (u32 i = 0; i < 8u; i++) { u32 w = h[2 + i]; pal[2 * i] = (u16)w; pal[2 * i + 1] = (u16)(w >> 16); }
  hw_px_begin(0, 0, 128, 128);
  for (u32 i = 0; i < n && y < 128u; i++) {
    u32 b = (h[10 + (i >> 2)] >> ((i & 3u) * 8u)) & 0xFFu, k = (b & 15u) + 1u;
    while (k--) { row[x++] = pal[b >> 4]; if (x == 128u) { hw_px_row(row, 128); x = 0; y++; } }
  }
  hw_px_end();
  hw_backlight(1);
  REG(0xE000EDFCu) |= 1u << 24; REG(0xE0001000u) |= 1u;           /* cycle counter: about 3 s at 96 MHz */
  for (u32 c0 = REG(0xE0001004u); REG(0xE0001004u) - c0 < 288000000u; ) hw_wdt_feed();
}
#define BAT_PIC_SHOW() bat_pic()
#else
#define BAT_PIC_SHOW() ((void)0)
#endif
/* called every second or so: 1 once the battery is low */
__attribute__((noinline, unused, minsize)) static int bat_guard_tick(void)
{
  static u8 c, n;
  if (++c < 20u) return 0;
  c = 0; n = bat_is_low() ? (u8)(n + 1u) : 0u;
  return n >= 2u;
}
#endif
#endif
