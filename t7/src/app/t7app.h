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
  uint32_t flags;
  uint32_t wall_cnt, wall_sod, wall_ymd;   /* written by the Studio on the device: seconds counter, local time and date then (0 = none) */
} T7Cfg;
#define T7_MAGIC 0x37584D50u   /* "PMX7" */

/* Settings on the device (wheel, short press). Stored with the clock note in their own sector. */
typedef struct {
  uint8_t bg, ink, g1, g2, size, pixel, grid, pixvis, ghost, sound, light, sync, lang, r0, r1, r2;
} T7Set;
extern T7Set g_set;
static inline uint32_t app_light_ms(void) { static const uint32_t t[5] = { 30000u, 60000u, 120000u, 300000u, 0u }; return t[g_set.light % 5]; }
#define T7_STORE_LEN 64u

/* Platform (device or PC test) */
int  plat_store_load(uint8_t *b, uint32_t n);              /* bytes of the settings sector (0 = empty) */
void plat_store_write(const uint8_t *b, uint32_t n);
uint32_t plat_rtc(void);                                   /* seconds counter, keeps running while switched off */
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
#endif
