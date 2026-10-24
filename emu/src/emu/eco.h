/* eco.h - 1.6.9 test22 (switch CLK_ECO): a lower clock while the screen is off, to save the batteries.
 * The Paradise runs at 96 MHz; the old toys need a fraction of it when nothing is drawn. Every 500 ms the core's own
 * cycle counter (DWT CYCCNT, it stands while the core sleeps in WFI) tells how busy the emulation was: below 35 % one
 * step down (half the clock), above 80 % one step up, down to 12 MHz (AHB / 8). If the emulation falls behind anyway,
 * straight back to full speed and for 5 minutes one step less deep. Full speed at once
 * for a key, the screen on, a tone or link traffic. Header only. Author: mameise */
#ifndef ECO_H
#define ECO_H
#include "hw.h"
#ifndef ECO_SH
#define ECO_SH 3u          /* lowest: 96 MHz >> 3 = 12 MHz */
#endif
#ifndef ECO_LAG
#define ECO_LAG 25         /* ms behind: too slow (the apps drop time only beyond 50 ms) */
#endif
#define ECO_DEMCR REG(0xE000EDFCu)
#define ECO_DWT_CTRL REG(0xE0001000u)
#define ECO_CYC REG(0xE0001004u)
#ifdef ECO_DEBUG
__attribute__((used)) volatile u32 g_eco_dbg[4];   /* tests: ups, last lag, last busy, want_low drops */
#endif
static u32 g_eco_hold, g_eco_t0, g_eco_c0, g_eco_on, g_eco_max = ECO_SH, g_eco_max_until;
/* too slow: full speed, wait longer before the next try. Also called from the apps' catch-up loops. */
__attribute__((noinline, unused, minsize)) static void eco_up(u32 now, u32 core_hz)
{
  /* this step was too slow: back to full speed at once, then at most one step less deep for 5 minutes */
#ifdef ECO_DEBUG
  g_eco_dbg[0]++;
#endif
  if (g_hw_clk_sh) { g_eco_max = g_hw_clk_sh - 1u; g_eco_max_until = now + 300000u; hw_clk_shift(0, core_hz); g_eco_hold = now + 2000u; }
  g_eco_t0 = now; g_eco_c0 = ECO_CYC;
}
#define ECO_CHECK(now, lag, hz) do { if (g_hw_clk_sh && (int)(lag) > ECO_LAG) eco_up((now), (hz)); } while (0)
#ifdef ECO_RTC
/* test27 (switch ECO_RTC): the millisecond tick against the Paradise's seconds counter (crystal). Every 4 whole
 * seconds at one clock step: if fewer than 3800 ms were counted, the missing ms are returned (the program adds them
 * to its tick, the emulation catches up) and that clock step is not used again until the next start. */
static u32 g_er_s, g_er_ms, g_er_n, g_er_sh;
__attribute__((used)) volatile u32 g_eco_bad, g_eco_fix[2];   /* clock steps found wrong; corrections, ms added */
__attribute__((noinline, unused, minsize)) static u32 eco_rtc(u32 now, u32 core_hz)
{
  u32 s = REG(0x45000304u), add = 0;
  if (s == g_er_s) return 0;
  if (s - g_er_s == 1u && g_er_sh == g_hw_clk_sh) {
    if (!g_er_n) { g_er_n = 1; g_er_ms = now; }       /* a window starts at a change of the second */
    else if (++g_er_n >= 5u) {
      u32 d = now - g_er_ms;
      if (d + 200u < 4000u) {
        add = 4000u - d; g_eco_fix[0]++; g_eco_fix[1] += add;
        if (g_hw_clk_sh) { g_eco_bad |= 1u << g_hw_clk_sh; hw_clk_shift(0, core_hz); }
      }
      g_er_n = 1; g_er_ms = now + add; g_er_sh = g_hw_clk_sh;
    }
  } else { g_er_n = 0; g_er_sh = g_hw_clk_sh; }      /* first call, a second missed or another clock step: wait */
  g_er_s = s;
  return add;
}
#define ECO_BAD(sh) ((g_eco_bad >> (sh)) & 1u)
#else
#define ECO_BAD(sh) 0u
#endif
/* want_low: screen off, no key, no tone, no link; lag: how many ms the emulation is behind now; core_hz: SysTick base */
__attribute__((noinline, unused, minsize)) static void eco_poll(u32 now, int want_low, int lag, u32 core_hz)
{
  u32 sh = g_hw_clk_sh;
  if (!g_eco_on) { ECO_DEMCR |= 1u << 24; ECO_DWT_CTRL |= 1u; g_eco_on = 1; g_eco_t0 = now; g_eco_c0 = ECO_CYC; }
#ifdef ECO_DEBUG
  if (!want_low && sh) g_eco_dbg[3]++;
  if (lag > (int)g_eco_dbg[1]) g_eco_dbg[1] = (u32)lag;
#endif
  if (!want_low) { if (sh) hw_clk_shift(0, core_hz); g_eco_hold = now + 1000u; g_eco_t0 = now; g_eco_c0 = ECO_CYC; return; }
  if (lag > ECO_LAG) { eco_up(now, core_hz); return; }
  if (now - g_eco_t0 < 500u) return;
  u32 cyc = ECO_CYC - g_eco_c0, avail = (core_hz >> sh) / 1000u * (now - g_eco_t0), busy = avail >= 100u ? cyc / (avail / 100u) : 100u;
  g_eco_t0 = now; g_eco_c0 = ECO_CYC;
  if (busy > 80u && sh) { hw_clk_shift(sh - 1u, core_hz); g_eco_hold = now + 2000u; return; }
  if (g_eco_max < ECO_SH && (int)(now - g_eco_max_until) >= 0) g_eco_max++;
  if (busy < 35u && sh < g_eco_max && !ECO_BAD(sh + 1u) && (int)(now - g_eco_hold) >= 0) hw_clk_shift(sh + 1u, core_hz);
}
#endif
