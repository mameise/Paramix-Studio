/* The Paradise's own clock, read from its SRAM when an emulator starts (before the emulator uses
 * the SRAM). The Paradise keeps the time as a base (days, seconds of the day) plus its seconds
 * counter 0x45000304; next to it the six fields it shows (year, month, day, hour, minute, second,
 * u16 each). Where this lies differs between the editions, so it is searched: a place counts only
 * if base plus counter give the time of day the fields show (chance 1 in 86400). The fields may
 * lag the counter by a few seconds; that is added. Same method as TamaParadise (uhrstellen).
 * Only for the emulators' start; header only, shared by TamaEmu, T7Emu and T18Emu. */
#ifndef PARACLOCK_H
#define PARACLOCK_H
#include <stdint.h>

/* days since 1970-01-01 */
static inline int32_t para_days(int32_t y, int32_t m, int32_t d)
{
  y -= m <= 2; int32_t era = (y >= 0 ? y : y - 399) / 400; int32_t yoe = y - era * 400;
  int32_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1, doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + doe - 719468;
}
/* Search [lo, hi) (byte offsets in the SRAM at 0x18000000) for the clock, skip [skip_lo, skip_hi).
 * Returns local time as seconds since 1970 (0 = not found). cnt: the seconds counter now. Must not
 * use anything but registers and the stack: it runs before the emulator sets up its memory. */
__attribute__((noinline, unused)) static uint32_t para_clock(uint32_t cnt, uint32_t lo, uint32_t hi, uint32_t skip_lo, uint32_t skip_hi)
{
  const volatile uint16_t *h = (const volatile uint16_t *)0x18000000u;
  uint32_t best = 0, best_lag = 600;
  if (lo < 0x10) lo = 0x10;
  for (uint32_t o = lo; o + 12 <= hi; o += 2) {
    if (o + 12 > skip_lo && o - 0x10 < skip_hi) continue;
    uint32_t y = h[o / 2];
    if (y < 2000u || y > 2100u) continue;
    uint32_t mo = h[o / 2 + 1], d = h[o / 2 + 2], hh = h[o / 2 + 3], mi = h[o / 2 + 4], se = h[o / 2 + 5];
    if (mo < 1u || mo > 12u || d < 1u || d > 31u || hh > 23u || mi > 59u || se > 59u) continue;
    uint32_t days = h[(o - 0x10) / 2] | (uint32_t)h[(o - 0x10) / 2 + 1] << 16;
    uint32_t sek = h[(o - 0x0C) / 2] | (uint32_t)h[(o - 0x0C) / 2 + 1] << 16;
    if (sek >= 86400u || days >= 40000u) continue;
    uint32_t tod = (sek + cnt % 86400u) % 86400u;               /* days * 86400 drops out */
    uint32_t shown = hh * 3600u + mi * 60u + se;
    uint32_t lag = (tod + 86400u - shown) % 86400u;            /* fields behind the clock by this much */
    if (lag < best_lag) {
      best_lag = lag;
      best = (uint32_t)para_days((int32_t)y, (int32_t)mo, (int32_t)d) * 86400u + shown + lag;
    }
  }
  return best;
}
#endif
