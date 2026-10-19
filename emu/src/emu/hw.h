#ifndef HW_H
#define HW_H
#include "io.h"
#define BTN_A 1u
#define BTN_B 2u
#define BTN_C 4u
#define BTN_WHEEL 8u
#ifndef PARADISE_REBOOT
#define PARADISE_REBOOT 0x100FF568u
#endif
void hw_init(void);
void hw_wdt_feed(void);
u32  hw_buttons(void);
void hw_push(const u16* fb, u32 stride, u32 x0, u32 y0, u32 x1, u32 y1);
void hw_back_to_paradise(void);
void hw_backlight(int on);
void hw_bl_pwm_off(void);
void hw_bl_level(u32 pct);
void hw_bl_test(u32 m, u32 pct);
void hw_tone(u32 hz);              /* 0 = off */
#ifdef HW_OPTS
extern u32 g_hw_vol, g_hw_dim;     /* 1.6.8: volume 0..3 (2 = as before), brightness 0..3 (0 = full) */
#endif
void hw_bl_set(u32 pct);   /* 1.6.8 BL_PWM: light 0..100 % */
#ifdef FAST_LCD
void hw_px_begin(u32 x0, u32 y0, u32 x1, u32 y1);
void hw_px_row(const u16* r, u32 n);
void hw_px_end(void);
#endif
#endif
