/* Paramix Wolf3D: what the platform (PC test runner or the Paradise) gives the game. Author: mameise. */
#ifndef PMX_PLAT_H
#define PMX_PLAT_H
#include <stdint.h>

uint32_t plat_ms (void);                       /* milliseconds since start */
void     plat_wait (uint32_t ms);              /* sleep (the Paradise: wfi until then) */
uint32_t plat_keys (void);                     /* PMX_K_* held now */
int      plat_wheel (void);                    /* wheel steps since the last call (+ one way, - the other) */
void     plat_lcd (const uint8_t *fb, int y0, int y1, const uint16_t *lut);   /* rows y0..y1-1 of the 128-wide screen */
void     plat_tone (uint32_t hz);              /* buzzer, 0 = off */
const uint8_t *plat_pack (void);               /* the data pack (W3DP), fast (cached) */
const uint8_t *plat_pack_far (void);           /* the same pack through a slower path, for offsets >= plat_pack_win() */
uint32_t plat_pack_win (void);                 /* how much of the pack the fast pointer reaches */
int      plat_settings_read (void *buf, int len);
void     plat_settings_write (const void *buf, int len);
void     plat_back (void);                     /* leave to the Paradise (does not return) */
void     plat_save_at (uint32_t off, void *dst, int len);           /* read the save area (60 KB) */
void     plat_save_page (uint32_t off, const void *src, int len);   /* write one 4 KB page (off 4 KB aligned; rest 0xFF) */
#define  plat_save_read(buf, len) plat_save_at (0, (buf), (len))
void     plat_fatal (const char *s);

#endif
