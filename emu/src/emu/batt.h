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
  u32 p = bat_pct(mv);
  s[k++] = (char)('0' + mv / 1000u); s[k++] = '.'; s[k++] = (char)('0' + (mv / 100u) % 10u); s[k++] = (char)('0' + (mv / 10u) % 10u); s[k++] = 'V'; s[k++] = ' ';
  if (p >= 100u) s[k++] = '1';
  if (p >= 10u) s[k++] = (char)('0' + (p / 10u) % 10u);
  s[k++] = (char)('0' + p % 10u); s[k++] = '%'; s[k] = 0;
}
#endif
