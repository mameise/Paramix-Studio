/* T7 on the Paradise: the part that does not touch the hardware (screen layout, clock,
 * figure menu, saved state). Built for the device and for the PC test (host/). */
#ifndef T7APP_H
#define T7APP_H
#include <stdint.h>

/* Settings block, filled in by the Studio (magic "PMX7"), at 0x140 in the program. */
#define T7_FIG_MAX 16
typedef struct { uint32_t off, len; char name[16]; } T7Fig;
typedef struct {
  uint32_t magic, version, rom_off, rom_len, save_off, nfig;
  T7Fig fig[T7_FIG_MAX];
  uint32_t flags;                          /* bit 0: English, bit 1: clock from the Paradise at every start */
  uint32_t wall_cnt, wall_sod, wall_ymd;   /* written by the Studio on the device: seconds counter, local time and date then (0 = none) */
} T7Cfg;
#define T7_MAGIC 0x37584D50u   /* "PMX7" */

/* Settings on the device (wheel, short press). Stored with the clock note in their own sector. */
typedef struct {
  uint8_t bg, ink, g1, g2, size, pixel, grid, pixvis, ghost, sound, light, sync, lang, r0, r1, r2;
  uint8_t pw, r3, r4, r5;   /* 1.6.12 test1 (T7_PW): power saving while the screen is off, 0 normal, 1 strong (12 MHz),
                               2 maximal (12 MHz and deep sleep between the seconds); others (older settings: 0xFF) = PW_DEF */
} T7Set;
#define PW_DEF 1u
#ifdef T7_FIXED
/* 1.6.12 test4: power save maximal and the JIT always (tested over night: the call still rings and lights the screen) */
static inline int app_pw(void) { return 2; }
#else
static inline int app_pw(void) { extern T7Set g_set; return g_set.pw <= 2u ? g_set.pw : (int)PW_DEF; }
#endif
extern T7Set g_set;
/* 1.6.6: light = time without a key until the screen dims (0..6: 10 s .. 10 min, 7 = never),
 * r0 = dim level (1, 5, 10, 25, 50 %), r2 = time until the screen goes off (0 = never, 1..7 = 10 s .. 10 min,
 * always longer than the dim time), r1: bits 0..3 = 2 (this layout). Dimmed = darker colours (the backlight PWM had no effect on the device). */
#define T7_T_N 7
static inline uint32_t t7_t_ms(int i) { static const uint32_t t[T7_T_N] = { 10000u, 20000u, 30000u, 60000u, 120000u, 300000u, 600000u }; return t[i % T7_T_N]; }
static inline uint32_t app_dim_ms(void) { return g_set.light >= T7_T_N ? 0u : t7_t_ms(g_set.light); }
static inline uint32_t app_off_ms(void) { return g_set.r2 ? t7_t_ms(g_set.r2 - 1) : 0u; }
#ifdef BL_PWM
/* 1.6.8 (test45): layout 3. r0 = dim level 1..50 % (10 steps), r1 bits 4..7 = the light in general:
 * 0 = 100 %, 1..10 = 5, 10, 20 .. 90 %. Dimmed is never brighter than the light in general. */
#define T7_DIM_N 10
#define T7_BL_N 11
static inline int app_bl_pct(void) { static const uint8_t p[T7_BL_N] = { 100, 5, 10, 20, 30, 40, 50, 60, 70, 80, 90 }; return p[((g_set.r1 >> 4) & 15) % T7_BL_N]; }
static inline int app_dim_pct(void) { static const uint8_t p[T7_DIM_N] = { 1, 2, 3, 5, 10, 15, 20, 30, 40, 50 }; int d = p[g_set.r0 % T7_DIM_N], b = app_bl_pct(); return d < b ? d : b; }
#else
static inline int app_dim_pct(void) { static const uint8_t p[5] = { 1, 5, 10, 25, 50 }; return p[g_set.r0 % 5]; }
#endif
void app_dim(int pct);
int app_bell(void);                                       /* 1 = the bell icon is on (the pet calls) */
#define T7_STORE_LEN 64u

/* Platform (device or PC test) */
int  plat_store_load(uint8_t *b, uint32_t n);              /* bytes of the settings sector (0 = empty) */
void plat_store_write(const uint8_t *b, uint32_t n);
uint32_t plat_rtc(void);
#ifdef T7_JIT
/* 1.6.12 test2: r3 bit 0 = JIT off (0xFF of older settings: on) */
#ifdef T7_FIXED
static inline int app_jit(void) { return 1; }
#else
static inline int app_jit(void) { extern T7Set g_set; return g_set.r3 == 0xFFu || !(g_set.r3 & 1u); }
#endif
int  plat_jit(int on);                                     /* switch, -1: the second part is missing */
#endif
int  plat_pw_pct(void);                                   /* T7_PW: % asleep * 1000 + % at 12 MHz, since the start */
#ifdef T7_VOL
/* 1.6.12 test3: g_set.sound = volume 0 off, 1 quiet (as before), 2 medium, 3 loud (test5: maximal 1/2 was quieter than loud, gone) */
#define T7_VOL_N 4
void plat_vol(int v);
#endif
void plat_bat(char *s);                                    /* BAT_SHOW: "2.84V 80%" (12 bytes) */                                   /* seconds counter, keeps running while switched off */
void plat_rect(int x0, int y0, int x1, int y1);            /* start a rectangle on the screen */
void plat_row(const uint16_t *px, int n);                  /* pixels of the rectangle, row by row */
void plat_rect_end(void);
const uint8_t *plat_fig_ptr(int i);                        /* figure data (flash) */

/* App */
void app_init(const volatile T7Cfg *cfg);
void app_render(int force);                                /* draw what changed */
void app_text_screen(const char *line1, const char *line2, int progress, int total);   /* simple message */
int  app_keys(uint32_t buttons, uint32_t now_ms);          /* buttons A=1 B=2 C=4 wheel=8 -> mask for the game (menu takes them) */
void app_tick(uint32_t now_ms);                            /* figure change in progress */
void app_clock(uint32_t rtc, int guard);                   /* keep the clock on the seconds counter (every 2 s) */
int  app_catchup(uint32_t elapsed_s, void (*progress)(int done, int total));   /* after a pause: let the hours pass */
uint32_t app_save(uint8_t *buf, uint32_t cap);             /* state for the flash (0 = too small) */
int  app_load(const uint8_t *buf, uint32_t len, uint32_t *saved_rtc);
int  app_menu_open(void);
int  app_lang(void);                                       /* 0 German, 1 English */
void app_setup_fill(uint32_t rtc);                         /* every 0.5 s: the game asks for the time, fill it in */
void app_store(void);                                      /* settings and clock note into the flash (on leaving) */
extern uint32_t g_para_time, g_para_cnt;                   /* the Paradise's time at the start (platform), 0 = unknown */
int  app_clock_hard(uint32_t rtc);                         /* settings flag 2: game clock = the Paradise's time (1 = done) */
#endif
