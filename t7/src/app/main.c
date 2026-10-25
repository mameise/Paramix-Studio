/* T7 (V7 6502 core, after asterick/tamago, BSD) on the Tamagotchi Paradise.
 * Started like TamaEmu by the stub (A+C on the home screen, or autostart).
 * Long press on the wheel: save and back to the Paradise. Short press: figure menu.
 * Author: mameise */
#include "../../../emu/src/emu/hw.h"
#ifdef CLK_ECO
#include "../../../emu/src/emu/eco.h"
#endif
#include "../../../emu/src/emu/flash.h"
#include "../../../emu/src/emu/paraclock.h"
#ifdef PARA_BACK
#include "../../../emu/src/emu/paraback.h"
#endif
#include "../core/t7.h"
#include "../core/t7_sound.h"
#include "t7app.h"
#ifdef T7_JSR_HLE
extern uint8_t g_t7_dark;
#endif
#ifdef T7_LINK
#include "../../../emu/src/emu/plink.h"
#endif

void t7_eeprom_flush(void);
void eeprom_load(uint32_t at);
void eeprom_store(uint32_t at, uint8_t *sec4k);
int  eeprom_dirty(void);
uint32_t eeprom_seq(void);
#ifdef T7_SESS_EESEQ
/* 1.6.9 test16: a session notes which EEPROM state was in the flash when it was written ("T7EQ" + sequence at the end of
 * its sector). Battery out while playing: the automatic session can be up to 30 minutes old, its RAM (inventory,
 * progress) won over the EEPROM the game had written since and the game wrote the old state back (items bought in a
 * figure shop vanished, after an early session the whole game was new). Now a session whose EEPROM is no longer the
 * one in the flash is not used: the game starts from its EEPROM, as the toy after a battery change. */
#define EQ_MAGIC 0x51453754u   /* "T7EQ" */
static void sess_mark(uint8_t *sec) { uint32_t v[2] = { EQ_MAGIC, eeprom_seq() }; for (int k = 0; k < 2; k++) for (int b = 0; b < 4; b++) sec[0xFF8 + k * 4 + b] = (uint8_t)(v[k] >> (8 * b));
#ifdef ECO_RTC
  { extern volatile u32 g_eco_bad, g_eco_fix[2]; uint32_t w[2] = { g_eco_bad | g_eco_fix[0] << 8, g_eco_fix[1] };   /* test27: for a dump (tick checks) */
    for (int k = 0; k < 2; k++) for (int b = 0; b < 4; b++) sec[0xFF0 + k * 4 + b] = (uint8_t)(w[k] >> (8 * b)); }
#endif
}
static int sess_eq_ok(const uint8_t *sec)
{
  uint32_t m = sec[0xFF8] | sec[0xFF9] << 8 | sec[0xFFA] << 16 | (uint32_t)sec[0xFFB] << 24, q = sec[0xFFC] | sec[0xFFD] << 8 | sec[0xFFE] << 16 | (uint32_t)sec[0xFFF] << 24;
  return m != EQ_MAGIC || q == eeprom_seq();   /* older sessions without the note: as before */
}
#endif
void app_wheel_short(void);

__attribute__((section(".pmxcfg"), used))
volatile const T7Cfg g_cfg = { T7_MAGIC, 1, 0x940000u, 0xA0000u, 0xC0000u, 0, {{0, 0, {0}}}, 0, 0, 0, 0 };

/* Emulated clock: 200000 cycles per 66 ms like the T7Emu app (game and animation tempo there were
 * set by ear against the original). The game clock is kept right by app_clock. */
#define T7_RATE 3030300u
const uint8_t *g_rom_xip;
uint32_t g_rom_size, g_rom_off;
static uint8_t g_sec[0x1000] __attribute__((aligned(4)));   /* the flash DMA takes words */
__attribute__((used)) volatile uint32_t g_t7st[4];
#ifdef T7_COUNT
volatile uint32_t g_t7_ninstr;
#endif   /* emulated ms, dropped ms (for measuring) */
#define SESSION (g_cfg.save_off)
#define EEPROM  (g_cfg.save_off + 0x1000u)
#define SETTINGS (g_cfg.save_off + 0x5000u)

/* Settings and the clock note: own sector (stays when the session is dropped) */
int plat_store_load(uint8_t *b, uint32_t n)
{
  const volatile uint8_t *p = XIP_PTR(SETTINGS);
  for (u32 i = 0; i < n; i++) b[i] = p[i];
  return b[0] == 'T' && b[1] == '7';
}
void plat_store_write(const uint8_t *b, uint32_t n)
{
  const volatile uint8_t *p = XIP_PTR(SETTINGS);
  u32 same = 1; for (u32 i = 0; i < n; i++) if (p[i] != b[i]) { same = 0; break; }
  if (same) return;
  static uint8_t sec[0x1000] __attribute__((aligned(4)));
  for (u32 i = 0; i < 0x1000u; i++) sec[i] = i < n ? b[i] : 0xFF;
  hw_tone(0);
  flash_update4k(SETTINGS, sec);
}

/* ---- time: SysTick 1 ms ---- */
static volatile uint32_t g_ms;
#ifdef T7_WHEEL_RAW
/* 1.6.12 test4: ms of the tick only, never set ahead. The wheel's long press is timed with it: g_ms is moved on after
 * deep sleep and by the clock check (eco_rtc), up to a second at once; a short press right after waking counted as
 * long and left the program */
static volatile uint32_t g_ms_raw;
#define WHEEL_NOW() g_ms_raw
#else
#define WHEEL_NOW() now
#endif
#define SYST_CSR REG(0xE000E010u)
#define SYST_RVR REG(0xE000E014u)
#define SYST_CVR REG(0xE000E018u)
#define CORE_HZ 96000000u
#define XIP_BASE_REG REG(0x4002F004u)
#define XIP_CTRL_REG REG(0x4002F000u)
#ifdef T7_TONE_Q
/* 1.6.9 test15: the tone on the real clock, as T18Emu (T18_TONE_Q). The emulation runs in steps of 2 ms, but after
 * drawing or a flash write it catches up several steps at once: the notes of a melody (about 26 ms each) came within
 * a millisecond and were swallowed or cut. Tone changes are queued with the game time and played TQ_LAT ms after the
 * first of a sequence; late by more than 200 ms the timing starts anew. */
#define TQ_N 32u
#define TQ_LAT 80u
static uint32_t tq_t[TQ_N]; static uint16_t tq_hz[TQ_N]; static uint32_t tq_h, tq_w, tq_g0, tq_r0, tq_on, tq_cur, tq_quiet;
static void tq_put(uint32_t hz, uint32_t gms) {
#ifdef T7_TONE_ISR
  __asm__ volatile("cpsid i" ::: "memory");
#endif
  if (tq_w - tq_h >= TQ_N) tq_h++; tq_t[tq_w % TQ_N] = gms; tq_hz[tq_w % TQ_N] = (uint16_t)(hz > 0xFFFFu ? 0xFFFFu : hz); tq_w++;
#ifdef T7_TONE_ISR
  __asm__ volatile("cpsie i" ::: "memory");
#endif
}
static int tq_busy(void) { return tq_h != tq_w || tq_cur; }
__attribute__((noinline, used)) static void tq_pump(void)
{
  uint32_t now = g_ms;
  if (tq_h == tq_w) { if (tq_on && !tq_cur && now - tq_quiet > 1000u) tq_on = 0; return; }
  if (!tq_on) { tq_on = 1; tq_g0 = tq_t[tq_h % TQ_N]; tq_r0 = now + TQ_LAT; }
  while (tq_h != tq_w) {
    uint32_t i = tq_h % TQ_N, due = tq_r0 + (tq_t[i] - tq_g0);
    int32_t late = (int32_t)(now - due);
    if (late < 0) break;
    if (late > 200) { tq_g0 = tq_t[i]; tq_r0 = now; }
    tq_cur = tq_hz[i]; hw_tone(tq_cur); if (!tq_cur) tq_quiet = now; tq_h++;
  }
}
#ifdef T7_TONE_ISR
/* 1.6.12 test4: the queue is played from the 1 ms tick. From the main loop a note change waited for drawing and the
 * emulation (up to some 10 ms), the notes of a melody (about 26 ms) came unevenly */
static volatile uint8_t tq_isr_on = 1;
#define TQ_TICK() do { if (tq_isr_on) tq_pump(); } while (0)
#define TQ_MAIN() do { } while (0)
#else
#define TQ_TICK() do { } while (0)
#define TQ_MAIN() tq_pump()
#endif
#else
#define TQ_TICK() do { } while (0)
#endif
#ifdef T7_KEYQ
/* 1.6.9 test13: quick presses were lost (the loop reads the keys once per pass, a pass with drawing and catching up
 * takes longer than a quick tap). The 1 ms tick counts each press (A P0.9, B P0.11, C P0.10, pressed = 0); the
 * emulation gets every counted press for at least KQ_HOLD ms of its time, then KQ_GAP ms released (test14: the
 * firmware needs about 120 and 80 ms, on the PC shorter taps were lost also without STOP). */
#define KQ_HOLD 120u
#define KQ_GAP 100u
#define KQ_DEB 30u   /* test15: a press counts only after the key was up this long (contacts bounce: 1 press gave 2) */
static volatile uint8_t g_kin[3]; static uint8_t g_kout[3], g_kprev; static uint32_t g_kup[3];
__attribute__((used)) void systick_c(void) {
  g_ms++;
#ifdef T7_WHEEL_RAW
  g_ms_raw++;
#endif
  TQ_TICK();
  uint32_t d = REG(0x40018000u);
  uint8_t k = (uint8_t)((d & (1u << 9) ? 0 : 1) | (d & (1u << 11) ? 0 : 2) | (d & (1u << 10) ? 0 : 4)), e = (uint8_t)(k & ~g_kprev), r = (uint8_t)(~k & g_kprev);
  g_kprev = k;
  for (int i = 0; i < 3; i++) {
    if (r & (1u << i)) g_kup[i] = g_ms;                                             /* went up */
    if ((e & (1u << i)) && g_ms - g_kup[i] >= KQ_DEB) g_kin[i]++;                    /* down after a real release */
  }
}
static void kq_clear(void) { for (int i = 0; i < 3; i++) g_kout[i] = g_kin[i]; }
/* one 2 ms step of emulated time: the keys the game sees (phys: held now) */
static uint8_t kq_step(uint8_t phys)
{
  static uint8_t ph[3], tm[3];
  uint8_t out = 0;
  for (int i = 0; i < 3; i++) {
    uint8_t bit = (uint8_t)(1u << i);
    if (ph[i] == 0 && g_kin[i] != g_kout[i]) { g_kout[i]++; ph[i] = 1; tm[i] = 0; }
    if (ph[i] == 1) { out |= bit; if (++tm[i] >= KQ_HOLD / 2u && !(phys & bit)) { ph[i] = 2; tm[i] = 0; } }
    else if (ph[i] == 2) { if (++tm[i] >= KQ_GAP / 2u) ph[i] = 0; }
  }
  return out;
}
#else
__attribute__((used)) void systick_c(void) { g_ms++;
#ifdef T7_WHEEL_RAW
  g_ms_raw++;
#endif
  TQ_TICK(); }
#endif
__attribute__((naked)) void SysTick_Handler(void) { __asm__ volatile("b systick_c\n"); }

uint32_t plat_rtc(void) { return REG(0x45000304u); }
#ifdef BAT_SHOW
#include "../../../emu/src/emu/batt.h"
void plat_bat(char *s) { bat_text(s); }
#ifdef T7_VOL
void plat_vol(int v) { g_hw_vol5 = (u32)v; }
#endif
#endif
const uint8_t *plat_fig_ptr(int i) { return (const uint8_t *)(0x60000000u + g_cfg.fig[i].off); }
#ifdef FAST_LCD
void plat_rect(int x0, int y0, int x1, int y1) { hw_px_begin((u32)x0, (u32)y0, (u32)x1, (u32)y1); }
void plat_row(const uint16_t *px, int n) { hw_px_row(px, (u32)n); }
void plat_rect_end(void) { hw_px_end(); }
#else
static int r_x0, r_x1, r_y;
void plat_rect(int x0, int y0, int x1, int y1) { r_x0 = x0; r_x1 = x1; r_y = y0; (void)y1; }
void plat_row(const uint16_t *px, int n) { (void)n; hw_push(px - r_x0, 0, (u32)r_x0, (u32)r_y, (u32)r_x1, (u32)r_y + 1u); r_y++; }
void plat_rect_end(void) {}
#endif


#ifdef T7_PW
/* ---- 1.6.12 test1: power saving while the screen is off (setting "Power save", app_pw()) ----
 * The chip (SNC7330 datasheet): 96 MHz from the PLL about 12 mA, 48 MHz 9 mA (the PLL runs on when the AHB divider
 * slows the core), 12 MHz from the internal RC with the PLL off 1.6 mA, deep sleep 0.4 mA (all clocks off but the RTC,
 * the RAM stays). 1 strong: screen off, no tone, no link: the system clock from the IHRC (12 MHz), PLL and crystal off.
 * 2 maximal: in addition deep sleep whenever the emulation has caught up; the RTC (1 s, set by the Paradise) or a key
 * (GPIO0 P0.8..11, both edges) wakes it; the seconds the RTC counted are added to the ms tick. With the screen on
 * everything is as before (PLL, 96 MHz). Key held while asleep: seen at the next RTC second at the latest. */
#define OSC_CTRL REG(0x45000000u)
#define OSC_STAT REG(0x45000008u)
#define SYS_CLKCFG REG(0x4500000Cu)
#define SYS_AHBCP REG(0x45000010u)
#define ISO_CTRL REG(0x45000200u)
#define PMU_CTRL REG(0x45000300u)
#define RTC_C REG(0x45000304u)
#define GP0(o) REG(0x40018000u + (o))
#define NVIC_ISER0 REG(0xE000E100u)
#define NVIC_ICER0 REG(0xE000E180u)
#define NVIC_ICPR0 REG(0xE000E280u)
#define PW_KEYS 0xF00u                     /* P0.8 wheel, P0.9 A, P0.10 C, P0.11 B */
#define PW_IRQS ((1u << 2) | (1u << 26))   /* RTC, GPIO0 */
static int g_pw_low;                       /* running from the IHRC */
static uint32_t g_pw_t0, g_pw_slept, g_pw_lowms, g_pw_lowat, g_pw_rtc, g_pw_rtcms;   /* statistics: start, seconds asleep, ms at 12 MHz */
__attribute__((used)) void pw_rtc_irq(void) { PMU_CTRL = (PMU_CTRL & ~0x47u) | 0x10u; }   /* RTC_FLAG: write 1 */
__attribute__((used)) void pw_gpio_irq(void) { GP0(0x20) = PW_KEYS; }
static void pw_full(void)
{
  if (!g_pw_low) return;
  OSC_CTRL |= 1u << 4;                                          /* crystal on (the PLL's source, as hw_init sets it) */
  for (u32 t = 0; !(OSC_STAT & (1u << 4)) && t < 2000000u; t++) {}
  OSC_CTRL |= 1u << 15;                                         /* PLL on */
  for (u32 t = 0; !(OSC_STAT & (1u << 6)) && t < 2000000u; t++) {}
  SYS_CLKCFG = 4u;                                              /* system clock from the PLL */
  for (u32 t = 0; ((SYS_CLKCFG >> 4) & 7u) != 4u && t < 2000000u; t++) {}
  SYST_RVR = CORE_HZ / 1000u - 1u; SYST_CVR = 0;
  g_pw_low = 0; g_pw_lowms += g_ms - g_pw_lowat;
}
static void pw_low(void)
{
  if (g_pw_low) return;
#ifdef CLK_ECO
  hw_clk_shift(0, CORE_HZ);
#endif
  SYS_AHBCP = 0;
  SYS_CLKCFG = 0;                                               /* system clock from the IHRC (12 MHz) */
  for (u32 t = 0; ((SYS_CLKCFG >> 4) & 7u) != 0u && t < 2000000u; t++) {}
  if (((SYS_CLKCFG >> 4) & 7u) != 0u) { SYS_CLKCFG = 4u; return; }   /* did not switch: stay as it was */
  OSC_CTRL &= ~((1u << 15) | (1u << 4));                        /* PLL and crystal off */
  SYST_RVR = 12000000u / 1000u - 1u; SYST_CVR = 0;
  g_pw_low = 1; g_pw_lowat = g_ms;
}
/* deep sleep until the next RTC second or a key; returns the seconds that passed */
static uint32_t pw_sleep(void)
{
  uint32_t iso = ISO_CTRL;
  if (!(iso & 0x80u) || (iso & 7u) != 0u) { __asm__ volatile("wfi"); return 0; }   /* the RTC is not on 1 s: no deep sleep */
  ISO_CTRL = iso & ~8u;                                         /* RTC_WKP 0: the RTC interrupt wakes */
  GP0(0x0C) &= ~PW_KEYS; GP0(0x10) |= PW_KEYS; GP0(0x20) = PW_KEYS; GP0(0x18) |= PW_KEYS;   /* edges, both */
  NVIC_ICPR0 = PW_IRQS; NVIC_ISER0 = PW_IRQS;
  uint32_t r0 = RTC_C, csr = SYST_CSR;
  SYST_CSR = 0;                                                 /* the tick stops in deep sleep anyway */
  PMU_CTRL = (PMU_CTRL & ~0x47u) | 2u;                          /* DSLEEP_EN */
  __asm__ volatile("dsb\n wfi\n isb" ::: "memory");
  PMU_CTRL = PMU_CTRL & ~0x47u;
  NVIC_ICER0 = PW_IRQS; GP0(0x18) &= ~PW_KEYS; GP0(0x20) = PW_KEYS;
  ISO_CTRL = (ISO_CTRL & ~8u) | (iso & 8u);
  SYST_CVR = 0; SYST_CSR = csr;
  uint32_t n = RTC_C - r0;
  if (n > 3600u) n = 0;
  g_pw_slept += n;
  return n;
}
int plat_pw_pct(void)   /* asleep * 1000 + at 12 MHz (asleep included): two percentages since the start */
{
  uint32_t ms = g_ms - g_pw_t0; if (!ms) return 0;
  uint32_t lo = g_pw_lowms + (g_pw_low ? g_ms - g_pw_lowat : 0);
  uint32_t a = (uint32_t)((uint64_t)g_pw_slept * 100000u / ms), b = (uint32_t)((uint64_t)lo * 100u / ms);
  return (int)((a > 100u ? 100u : a) * 1000u + (b > 100u ? 100u : b));
}
#endif

static void leave(int save)
{
#ifdef T7_PW
  pw_full();
#endif
#ifdef CLK_ECO
  hw_clk_shift(0, CORE_HZ);
#endif
#ifdef T7_TONE_ISR
  tq_isr_on = 0;
#endif
  hw_tone(0);
  REG(0x20000FF8u) = 0;                  /* start counter of the home hooks */
  REG(0x20000FFCu) = 0x4C584D50u;        /* "PMXL": back on purpose, the autostart stays off */
#ifdef T7_SESS_EESEQ
  t7_eeprom_flush(); eeprom_store(EEPROM, g_sec);   /* the pet's own save first, the session notes its state */
  if (save) {
    uint32_t n = app_save(g_sec, sizeof g_sec);
    if (n && n <= 0xFF0u) { for (uint32_t i = n; i < 0x1000u; i++) g_sec[i] = 0xFF; sess_mark(g_sec); flash_update4k(SESSION, g_sec); }
  }
#else
  if (save) {
    uint32_t n = app_save(g_sec, sizeof g_sec);
    if (n) { for (uint32_t i = n; i < 0x1000u; i++) g_sec[i] = 0xFF; flash_update4k(SESSION, g_sec); }
  }
  t7_eeprom_flush(); eeprom_store(EEPROM, g_sec);   /* the pet's own save always */
#endif
  app_store();                                       /* settings and the clock note (for the next time setting) */
#ifdef PARA_BACK
  para_back_fix(g_sec);                              /* 1.6.8: the Paradise skips the time spent here */
#endif
  hw_bl_pwm_off();
  for (u32 t = 0; hw_buttons() && t < 20000000u; t++) if ((t & 0xFFFFu) == 0) hw_wdt_feed();
  XIP_BASE_REG = 0x60011000u; XIP_CTRL_REG = 3u;
  hw_back_to_paradise();
}
static void drop_session(void) { if (!flash_isff(SESSION, 0x1000u)) flash_erase4k(SESSION); }
#ifdef SESS_AUTO
#ifndef SESS_T1
#define SESS_T1 600000u    /* screen dark: 10 minutes after the last time at the earliest */
#define SESS_T2 1800000u   /* otherwise every 30 minutes */
#endif
/* 1.6.8: the run state now and then, without leaving (battery empty or out while playing): when the screen goes
 * dark (10 minutes after the last time at the earliest), otherwise every 30 minutes after 10 s without a key */
static uint32_t g_sess_ms;
static void sess_auto(uint32_t now)
{
  hw_tone(0);
#ifdef T7_SESS_EESEQ
  t7_eeprom_flush(); if (eeprom_dirty()) eeprom_store(EEPROM, g_sec);
  uint32_t n = app_save(g_sec, sizeof g_sec);
  if (n && n <= 0xFF0u) { for (uint32_t i = n; i < 0x1000u; i++) g_sec[i] = 0xFF; sess_mark(g_sec); flash_update4k(SESSION, g_sec); }
#else
  uint32_t n = app_save(g_sec, sizeof g_sec);
  if (n) { for (uint32_t i = n; i < 0x1000u; i++) g_sec[i] = 0xFF; flash_update4k(SESSION, g_sec); }
  t7_eeprom_flush(); if (eeprom_dirty()) eeprom_store(EEPROM, g_sec);
#endif
  g_sess_ms = now;
}
#endif

#ifdef T7_LINK
/* 1.6.9: visits between two Paradise (Tama-Go: CONNECTION, TAMA-GO, VISIT): each infrared packet the game sends goes
 * as it is to the other one ('T' frames of 32 bytes, as the T7Emu's IR bridge), and comes in there through the
 * bridge's receive hook. Only taken while the game is in its connection screen (else an old packet would wait). */
static uint32_t g_lk_act, g_lk_ms;
static void lk_got(u8 ty, const u8 *d, int n) { if (ty == 'T' && n == 32 && g_ms - g_lk_ms < 1500u) t7_ir_bridge_rx(d); }
static void lk_tick(uint32_t now)
{
  uint32_t a = t7_ir_activity(); if (a != g_lk_act) { g_lk_act = a; g_lk_ms = now; }
  uint8_t p[32]; if (t7_ir_tx_take(p)) plink_send('T', p, 32);
  plink_poll(now, now - g_lk_ms < 1500u ? 1u : 0u, lk_got);
}
#endif
static void prog(int done, int total) { hw_wdt_feed(); app_text_screen(app_lang() ? "Catching up" : "Zeit holt auf", 0, done, total); }

extern u32 __bss_start, __bss_end;
#ifdef T7_JIT
#include "../core/t7_jit.h"
/* 1.6.12 test2: the translator (second part t7x.bin, in the flash at the save area + 0x8000) into the SRAM behind the
 * data, the rest of the SRAM up to the stack for the translated code. Without a matching second part: no JIT. */
#define T7X_MAGIC 0x31583754u   /* "T7X1" */
static int g_x_ok;
int plat_jit(int on) { if (!g_x_ok) return -1; g_t7j_on = on ? 1u : 0u; return on; }
static void x_load(void)
{
  extern u32 __x_start, __x_end;
  const volatile u32 *h = (const volatile u32 *)XIP_PTR(g_cfg.save_off + 0x8000u);
  u32 n = (u32)(&__x_end - &__x_start), sum = 0;
  if (h[0] != T7X_MAGIC || h[1] != (u32)T7X_ID || h[2] != n * 4u) return;
  for (u32 i = 0; i < n; i++) { u32 v = h[4 + i]; (&__x_start)[i] = v; sum += v; }
  if (sum != h[3]) return;
  __asm__ volatile("dsb\n isb" ::: "memory");
  u8 *buf = (u8 *)(((u32)&__x_end + 7u) & ~7u);
  t7_jit_init(buf, 0x1801F000u - (u32)buf);
  g_x_ok = g_t7j_on != 0;
  plat_jit(app_jit());
}
#endif
void Reset_Handler(void)
{
  __asm__ volatile("cpsid i");
  /* the Paradise's clock, before its SRAM is overwritten (the stack is in the top 4 KB) */
  uint32_t cnt = REG(0x45000304u), para = para_clock(cnt, 0, 0x1F000u, 0, 0);
  if (!para) para = para_mailbox(cnt);   /* 1.6.8: started from the menu */
#ifdef PARA_FLASH
  if (!para) para = para_flash(cnt);   /* 1.6.9 test37: the base in the Paradise's settings page */
#endif
#ifdef PARA_BACK
  para_leave_mark(cnt, para);            /* 1.6.8: the Paradise does not live meanwhile */
#endif
  __asm__ volatile("" ::: "memory");
  for (u32 *p = &__bss_start; p < &__bss_end; p++) *p = 0;
  { extern u32 __data_start, __data_end, __data_load;
    const u32 *src = &__data_load; for (u32 *p = &__data_start; p < &__data_end; p++) *p = *src++; }
  g_para_time = para; g_para_cnt = cnt;
  hw_init();
  g_rom_off = g_cfg.rom_off; g_rom_size = g_cfg.rom_len;
  XIP_BASE_REG = 0x60000000u | g_rom_off;     /* ROM in the cached window from 0x10000000 */
  XIP_CTRL_REG = 3u;
  g_rom_xip = (const uint8_t *)0x10000000u;
  app_init(&g_cfg);
  app_render(1);
  eeprom_load(EEPROM);
  t7_init();
#ifdef T7_JIT
  x_load();
#endif
  t7_snd_config(T7_RATE, 16000);
  t7_snd_capture(0);
#ifndef T7_NOSND
  t7_snd_set_mode(1);                          /* original sound: timers on, tone read for the buzzer */
#ifdef T7_TONE_EV
  t7_snd_tone_events(T7_RATE / 1000u);
#endif
#endif
#ifdef T7_LINK
  plink_init();
#endif
  SYST_RVR = CORE_HZ / 1000u - 1u; SYST_CVR = 0; SYST_CSR = 7;
  __asm__ volatile("cpsie i");

  uint32_t saved = 0, restored = 0;
  { const volatile uint8_t *s = XIP_PTR(SESSION);
    for (u32 i = 0; i < 0x1000u; i++) g_sec[i] = s[i]; }
#ifdef T7_SESS_EESEQ
  if (!sess_eq_ok(g_sec)) { drop_session(); for (u32 i = 0; i < 4u; i++) g_sec[i] = 0xFF; }   /* the EEPROM is newer */
#endif
  if (app_load(g_sec, sizeof g_sec, &saved)) {
    restored = 1;
#ifdef SESS_ONCE
    /* 1.6.8: the session is used once. Power gone without leaving (battery out): the next start begins from the
     * toy's own save (EEPROM, written every minute) instead of an old session that would win over it */
    drop_session();
#endif
    if (!app_clock_hard(plat_rtc())) app_catchup(plat_rtc(), prog);
  }
  app_render(1);

#ifdef T7_PW
  g_pw_t0 = g_ms;
#endif
  uint32_t emu_ms = g_ms, last_draw = g_ms, last_clock = g_ms, last_fill = g_ms, t_start = g_ms, wheel_ms = 0, last_in = g_ms, quiet_since = g_ms;
  int bl = 1, lock = 1, dimmed = 0;
  for (;;) {
    hw_wdt_feed();
#ifdef T7_TONE_Q
    TQ_MAIN();
#endif
    uint32_t now = g_ms;
    u32 b = hw_buttons();
    if (lock) { if (b) b = 0; else lock = 0; }         /* wait until A+C from the start are released */
#ifdef T7_PW
    /* 1.6.12 test1: screen off, no menu, no tone, no link: the low power modes */
    int pw_quiet = !bl && !b && !app_menu_open() && !g_hw_tone_hz
#ifdef T7_TONE_Q
      && !tq_busy()
#endif
#ifdef T7_LINK
      && !(g_pl.last_rx && now - g_pl.last_rx < 30000u)
#endif
      ;
    if (app_pw() && pw_quiet) pw_low(); else pw_full();
    { uint32_t r = RTC_C; if (r != g_pw_rtc) { g_pw_rtc = r; g_pw_rtcms = now; } }   /* when the RTC last stepped */
#endif
#ifdef CLK_ECO
    /* test22: screen off, no key, no tone, no link traffic for 30 s: a lower clock (eco.h) */
#ifdef ECO_RTC
    { u32 a = eco_rtc(now, CORE_HZ); if (a) { __asm__ volatile("cpsid i" ::: "memory"); g_ms += a; __asm__ volatile("cpsie i" ::: "memory"); now = g_ms; } }   /* test27 */
#endif
#ifdef T7_PW
    if (!g_pw_low)
#endif
    eco_poll(now, !bl && !b && !g_hw_tone_hz && !app_menu_open()
#ifdef T7_LINK
      && !(g_pl.last_rx && now - g_pl.last_rx < 30000u)
#endif
      , (int)(now - emu_ms), CORE_HZ);
#endif
    /* wheel: within 3 s after resuming = drop the session; long = leave; short = menu */
    if (b & BTN_WHEEL) {
      if (restored && now - t_start < 3000u) {
        /* 1.6.6: ask first (a press meant for the menu threw the session away) */
        app_text_screen(app_lang() ? "Reset?" : "Zuruecksetzen?", app_lang() ? "A yes   B no" : "A ja   B nein", 0, 0);
        while (hw_buttons()) hw_wdt_feed();
        u32 k; do { hw_wdt_feed(); k = hw_buttons(); } while (!(k & (BTN_A | BTN_B | BTN_WHEEL)));
        while (hw_buttons()) hw_wdt_feed();
        if (k & BTN_A) { drop_session(); leave(0); }
        restored = 0; wheel_ms = 0; last_in = g_ms; app_render(1); continue;
      }
      if (!wheel_ms) wheel_ms = WHEEL_NOW() ? WHEEL_NOW() : 1;
      else if (WHEEL_NOW() - wheel_ms > 1500u) leave(1);
    } else if (wheel_ms) { if (WHEEL_NOW() - wheel_ms < 1000u && bl) app_wheel_short(); wheel_ms = 0; }
    if (b) { last_in = now;
      if (!bl) {
#ifdef T7_PW
        pw_full();
#endif
        bl = 1; hw_backlight(1); app_render(1); while (hw_buttons()) hw_wdt_feed();
#ifdef T7_KEYQ
        kq_clear();
#endif
        continue; }
      if (dimmed) { dimmed = 0;
        app_dim(0);
        while (hw_buttons()) hw_wdt_feed();
#ifdef T7_KEYQ
        kq_clear();
#endif
        continue; }   /* the key only wakes */
    }
#ifdef T7_BELL_WAKE
    /* test9: the pet calls (bell icon comes on): screen on, as after a key */
    { static int bell0; int bell = app_bell();
      if (bell && !bell0) { last_in = now;
        if (!bl) { bl = 1;
#ifdef T7_PW
          pw_full();
#endif
#ifdef CLK_ECO
          hw_clk_shift(0, CORE_HZ);   /* test22: full speed before the panel wakes */
#endif
          hw_backlight(1); app_render(1); }
        if (dimmed) { dimmed = 0; app_dim(0); } }
      bell0 = bell; }
#endif
#ifdef T7_KEYQ
    int menu0 = app_menu_open();
    uint8_t gk = (uint8_t)app_keys(b, now);
    int kq = !lock && !menu0 && !app_menu_open() && bl && !dimmed;   /* a key that closed the menu does not reach the game */
    if (!kq) { kq_clear(); t7_set_buttons(gk); }
#else
    t7_set_buttons((uint8_t)app_keys(b, now));
#endif
    app_tick(now);
    /* emulate the time that passed, in steps of 2 ms (keys and sound stay fine-grained) */
    if ((int32_t)(now - emu_ms) > 3000) { g_t7st[1] += now - 3000u - emu_ms; emu_ms = now - 3000u; }   /* far behind: the clock is set by app_clock */
    uint32_t t0 = g_ms;
#ifdef T7_JSR_HLE
    g_t7_dark = (uint8_t)(!bl && !app_menu_open());   /* 1.6.12 test1: screen off: the game's sprite drawing is left out */
#endif
    while ((int32_t)(now - emu_ms) >= 2 && g_ms - t0 < 30u) {
#ifdef T7_KEYQ
      if (kq) t7_set_buttons(kq_step(g_kprev));
#endif
#ifdef T7_DARK_STEP
      /* 1.6.12 test1: screen off: up to 50 ms at once (fewer passes of this loop; the same emulated time) */
      uint32_t st = 2;
      if (!bl && !kq) { st = (now - emu_ms) & ~1u; if (st > 50u) st = 50u; }
      t7_step_cycles((T7_RATE / 500u) * (st / 2u));
      emu_ms += st; g_t7st[0] += st;
#else
      t7_step_cycles(T7_RATE / 500u);
      emu_ms += 2; g_t7st[0] += 2;
#endif
#ifdef CLK_ECO
      ECO_CHECK(g_ms, g_ms - emu_ms, CORE_HZ);
#endif
#ifdef T7_LINK
      lk_tick(g_ms);
#endif
      static uint32_t hz_now;
#ifdef T7_TONE_EV
      /* 1.6.12 test4: the tone changes as the core sees them (every ms of game time, a new pitch only when it stays
       * 4 ms, silent when the envelope has died away), with their time; before: a look every 2 ms, glitches between
       * the notes and notes that rang on after their envelope */
      { uint64_t cn = t7_snd_cycles(), ec; uint32_t ehz;
        while (t7_snd_tone_peek(&ec, &ehz)) {
          t7_snd_tone_pop();
          if (!g_set.sound) ehz = 0;
          if (ehz == hz_now) continue;
          uint32_t back = (uint32_t)((cn - ec) / (T7_RATE / 1000u));
#ifdef T7_PW
          if (ehz) pw_full();   /* the buzzer's timer runs on the PLL clock */
#endif
          hz_now = ehz; tq_put(ehz, emu_ms - (back > 1000u ? 1000u : back));
        } }
      TQ_MAIN();
#elif defined(T7_TONE_Q)
      uint8_t lv; uint32_t hz = t7_snd_tone_hz(&lv);
      if (!g_set.sound) hz = 0;
#ifdef T7_PW
      if (hz && hz != hz_now) pw_full();   /* the buzzer's timer runs on the PLL clock */
#endif
      if (hz != hz_now) { hz_now = hz; tq_put(hz, emu_ms); }
      TQ_MAIN();
#else
      uint8_t lv; uint32_t hz = t7_snd_tone_hz(&lv);
      if (!g_set.sound) hz = 0;
      if (hz != hz_now) { hz_now = hz; hw_tone(hz); }
#endif
    }
#ifdef T7_TONE_Q
    TQ_MAIN();
#endif
    if (now - last_draw >= 33u) { last_draw = now; if (bl) app_render(0); }
    if (now - last_fill >= 500u) { last_fill = now; app_setup_fill(plat_rtc()); }
    if (now - last_clock >= 2000u) { last_clock = now; app_clock(plat_rtc(), !restored && now - t_start < 90000u); }
    /* 1.6.6: dimmed after the dim time, dark after the off time (both from the menu, without a key; the game runs on) */
    if (bl && !app_menu_open()) {
      uint32_t idle = now - last_in;
      if (app_off_ms() && idle > app_off_ms()) { bl = 0; if (dimmed) app_dim(0); dimmed = 0; hw_backlight(0);
#ifdef SESS_AUTO
        if (now - g_sess_ms > SESS_T1) sess_auto(now);
#endif
      }
      else if (!dimmed && app_dim_ms() && idle > app_dim_ms()) { dimmed = 1;
        app_dim(app_dim_pct()); }                                         /* darker colours: the backlight PWM had no effect */
    }
#ifdef SESS_AUTO
    if (now - g_sess_ms > SESS_T2 && now - last_in > 10000u && !app_menu_open()) sess_auto(now);
#endif
    /* the pet's save: write it once things are quiet (1 minute after the last key) */
    if (b) quiet_since = now;
    if (now - quiet_since > 60000u) { t7_eeprom_flush(); if (eeprom_dirty()) { hw_tone(0); eeprom_store(EEPROM, g_sec); } quiet_since = now; }
#ifdef T7_EE_SOON
    /* test16: what the game wrote into its EEPROM (a purchase, a figure item) goes to the flash 5 s later when no key
     * was pressed for 2 s, not only after a minute without keys (battery out before that: lost). The game writes its
     * EEPROM seldom (on the PC: at the setup and at events, not while idle), so the flash is not worn. */
    { static uint32_t ee_t;
      if (now - ee_t >= 5000u) { ee_t = now; t7_eeprom_flush(); if (eeprom_dirty() && now - last_in > 2000u) { hw_tone(0); eeprom_store(EEPROM, g_sec); } } }
#endif
    /* caught up: sleep until the next tick, once the tick is known to arrive */
    { static uint32_t g0 = 0xFFFFFFFFu; static int tick_ok;
      if (g0 == 0xFFFFFFFFu) g0 = g_ms;
      if (!tick_ok && g_ms != g0) tick_ok = 1;
#ifdef T7_WFI_FIX
      /* test26: the emulation runs in steps of 2 ms; one ms behind it waits for the next tick as well. Before, the
       * loop spun through that ms without sleeping (half of all the time, a battery drain) */
#ifdef T7_PW
      if (tick_ok && (int32_t)(g_ms - emu_ms) < 2 && app_pw() == 2 && pw_quiet && g_pw_low && !hw_buttons()) {
        /* 1.6.12 test1: deep sleep until the next RTC second; the ms tick then continues from the time of the last
         * RTC step (the awake time since then is in it already) */
        if (pw_sleep()) { uint32_t r = RTC_C, t = g_pw_rtcms + (r - g_pw_rtc) * 1000u;
          __asm__ volatile("cpsid i" ::: "memory"); if ((int32_t)(t - g_ms) > 0) g_ms = t; __asm__ volatile("cpsie i" ::: "memory");
          g_pw_rtc = r; g_pw_rtcms = t; }
      } else
#endif
      if (tick_ok && (int32_t)(g_ms - emu_ms) < 2) __asm__ volatile("wfi"); }
#else
      if (tick_ok && emu_ms == g_ms) __asm__ volatile("wfi"); }
#endif
  }
}

__attribute__((naked)) static void Default_Handler(void) { __asm__ volatile("1: b 1b\n"); }
__attribute__((naked)) static void Irq_Off(void) { __asm__ volatile("cpsid i\n bx lr\n"); }
__attribute__((section(".vectors"), used))
const void *const g_vectors[16 + 64] = { (void *)0x18020000u, (void *)Reset_Handler,
  [2 ... 10] = (void *)Default_Handler, [11] = (void *)Irq_Off, [12 ... 13] = (void *)Default_Handler, [14] = (void *)Irq_Off,
  [15] = (void *)SysTick_Handler, [16 ... 79] = (void *)Irq_Off
#ifdef T7_PW
  , [16 + 2] = (void *)pw_rtc_irq, [16 + 26] = (void *)pw_gpio_irq
#endif
#ifdef T7_LINK
  , [16 + 37] = (void *)plink_irq
#endif
};
