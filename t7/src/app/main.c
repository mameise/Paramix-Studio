/* T7 (V7 6502 core, after asterick/tamago, BSD) on the Tamagotchi Paradise.
 * Started like TamaEmu by the stub (A+C on the home screen, or autostart).
 * Long press on the wheel: save and back to the Paradise. Short press: figure menu.
 * Author: mameise */
#include "../../../emu/src/emu/hw.h"
#include "../../../emu/src/emu/flash.h"
#include "../../../emu/src/emu/paraclock.h"
#ifdef PARA_BACK
#include "../../../emu/src/emu/paraback.h"
#endif
#include "../core/t7.h"
#include "../core/t7_sound.h"
#include "t7app.h"

void t7_eeprom_flush(void);
void eeprom_load(uint32_t at);
void eeprom_store(uint32_t at, uint8_t *sec4k);
int  eeprom_dirty(void);
void app_wheel_short(void);

__attribute__((section(".pmxcfg"), used))
volatile const T7Cfg g_cfg = { T7_MAGIC, 1, 0x940000u, 0xA0000u, 0xC0000u, 0, {{0, 0, {0}}}, 0, 0, 0, 0 };

/* Emulated clock: 200000 cycles per 66 ms like the T7Emu app (game and animation tempo there were
 * set by ear against the original). The game clock is kept right by app_clock. */
#define T7_RATE 3030300u
const uint8_t *g_rom_xip;
uint32_t g_rom_size, g_rom_off;
static uint8_t g_sec[0x1000];
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
  static uint8_t sec[0x1000];
  for (u32 i = 0; i < 0x1000u; i++) sec[i] = i < n ? b[i] : 0xFF;
  hw_tone(0);
  flash_update4k(SETTINGS, sec);
}

/* ---- time: SysTick 1 ms ---- */
static volatile uint32_t g_ms;
#define SYST_CSR REG(0xE000E010u)
#define SYST_RVR REG(0xE000E014u)
#define SYST_CVR REG(0xE000E018u)
#define CORE_HZ 96000000u
#define XIP_BASE_REG REG(0x4002F004u)
#define XIP_CTRL_REG REG(0x4002F000u)
__attribute__((used)) void systick_c(void) { g_ms++; }
__attribute__((naked)) void SysTick_Handler(void) { __asm__ volatile("b systick_c\n"); }

uint32_t plat_rtc(void) { return REG(0x45000304u); }
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

static void leave(int save)
{
  hw_tone(0);
  REG(0x20000FF8u) = 0;                  /* start counter of the home hooks */
  REG(0x20000FFCu) = 0x4C584D50u;        /* "PMXL": back on purpose, the autostart stays off */
  if (save) {
    uint32_t n = app_save(g_sec, sizeof g_sec);
    if (n) { for (uint32_t i = n; i < 0x1000u; i++) g_sec[i] = 0xFF; flash_update4k(SESSION, g_sec); }
  }
  t7_eeprom_flush(); eeprom_store(EEPROM, g_sec);   /* the pet's own save always */
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
  uint32_t n = app_save(g_sec, sizeof g_sec);
  if (n) { for (uint32_t i = n; i < 0x1000u; i++) g_sec[i] = 0xFF; flash_update4k(SESSION, g_sec); }
  t7_eeprom_flush(); if (eeprom_dirty()) eeprom_store(EEPROM, g_sec);
  g_sess_ms = now;
}
#endif

static void prog(int done, int total) { hw_wdt_feed(); app_text_screen(app_lang() ? "Catching up" : "Zeit holt auf", 0, done, total); }

extern u32 __bss_start, __bss_end;
void Reset_Handler(void)
{
  __asm__ volatile("cpsid i");
  /* the Paradise's clock, before its SRAM is overwritten (the stack is in the top 4 KB) */
  uint32_t cnt = REG(0x45000304u), para = para_clock(cnt, 0, 0x1F000u, 0, 0);
  if (!para) para = para_mailbox(cnt);   /* 1.6.8: started from the menu */
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
  t7_snd_config(T7_RATE, 16000);
  t7_snd_capture(0);
#ifndef T7_NOSND
  t7_snd_set_mode(1);                          /* original sound: timers on, tone read for the buzzer */
#endif
  SYST_RVR = CORE_HZ / 1000u - 1u; SYST_CVR = 0; SYST_CSR = 7;
  __asm__ volatile("cpsie i");

  uint32_t saved = 0, restored = 0;
  { const volatile uint8_t *s = XIP_PTR(SESSION);
    for (u32 i = 0; i < 0x1000u; i++) g_sec[i] = s[i]; }
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

  uint32_t emu_ms = g_ms, last_draw = g_ms, last_clock = g_ms, last_fill = g_ms, t_start = g_ms, wheel_ms = 0, last_in = g_ms, quiet_since = g_ms;
  int bl = 1, lock = 1, dimmed = 0;
  for (;;) {
    hw_wdt_feed();
    uint32_t now = g_ms;
    u32 b = hw_buttons();
    if (lock) { if (b) b = 0; else lock = 0; }         /* wait until A+C from the start are released */
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
      if (!wheel_ms) wheel_ms = now ? now : 1;
      else if (now - wheel_ms > 1500u) leave(1);
    } else if (wheel_ms) { if (now - wheel_ms < 1000u && bl) app_wheel_short(); wheel_ms = 0; }
    if (b) { last_in = now;
      if (!bl) { bl = 1; hw_backlight(1); app_render(1); while (hw_buttons()) hw_wdt_feed(); continue; }
      if (dimmed) { dimmed = 0;
        app_dim(0);
        while (hw_buttons()) hw_wdt_feed(); continue; }   /* the key only wakes */
    }
#ifdef T7_BELL_WAKE
    /* test9: the pet calls (bell icon comes on): screen on, as after a key */
    { static int bell0; int bell = app_bell();
      if (bell && !bell0) { last_in = now;
        if (!bl) { bl = 1; hw_backlight(1); app_render(1); }
        if (dimmed) { dimmed = 0; app_dim(0); } }
      bell0 = bell; }
#endif
    t7_set_buttons((uint8_t)app_keys(b, now));
    app_tick(now);
    /* emulate the time that passed, in steps of 2 ms (keys and sound stay fine-grained) */
    if ((int32_t)(now - emu_ms) > 3000) { g_t7st[1] += now - 3000u - emu_ms; emu_ms = now - 3000u; }   /* far behind: the clock is set by app_clock */
    uint32_t t0 = g_ms;
    while ((int32_t)(now - emu_ms) >= 2 && g_ms - t0 < 30u) {
      t7_step_cycles(T7_RATE / 500u);
      emu_ms += 2; g_t7st[0] += 2;
      uint8_t lv; uint32_t hz = t7_snd_tone_hz(&lv);
      static uint32_t hz_now;
      if (!g_set.sound) hz = 0;
      if (hz != hz_now) { hz_now = hz; hw_tone(hz); }
    }
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
    /* caught up: sleep until the next tick, once the tick is known to arrive */
    { static uint32_t g0 = 0xFFFFFFFFu; static int tick_ok;
      if (g0 == 0xFFFFFFFFu) g0 = g_ms;
      if (!tick_ok && g_ms != g0) tick_ok = 1;
      if (tick_ok && emu_ms == g_ms) __asm__ volatile("wfi"); }
  }
}

__attribute__((naked)) static void Default_Handler(void) { __asm__ volatile("1: b 1b\n"); }
__attribute__((naked)) static void Irq_Off(void) { __asm__ volatile("cpsid i\n bx lr\n"); }
__attribute__((section(".vectors"), used))
const void *const g_vectors[16 + 64] = { (void *)0x18020000u, (void *)Reset_Handler,
  [2 ... 10] = (void *)Default_Handler, [11] = (void *)Irq_Off, [12 ... 13] = (void *)Default_Handler, [14] = (void *)Irq_Off,
  [15] = (void *)SysTick_Handler, [16 ... 79] = (void *)Irq_Off };
