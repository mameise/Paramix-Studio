/* TG18 (Meets/On) SoC model for the Paradise, from the T18Emu core. */
#ifndef T18SOC_H
#define T18SOC_H
#include <stdint.h>
#include "t18cpu.h"

#define TG18_W 128
#define TG18_H 128
#define TG18_FLASH_SIZE 0x800000
#define TG18_MASTER_HZ  96000000u
#define TG18_BTN_A 1
#define TG18_BTN_B 2
#define TG18_BTN_C 4

typedef struct tg18 tg18_t;
/* a finished picture: src = guest address of the buffer (RGB565, 128 wide), len in bytes */
typedef void (*t18_present_fn)(void *user, uint32_t src, uint32_t len);

/* platform: the guest's SPI flash (8 MB) */
uint8_t t18_flash_rd(uint32_t a);
void t18_flash_prog(uint32_t a, uint8_t b);      /* program: bits only go from 1 to 0 */
void t18_flash_erase(uint32_t a, uint32_t size); /* a aligned to size (4, 32 or 64 KB) */

uint32_t tg18_struct_size(void);
void tg18_init(tg18_t *t, const uint8_t *rom, uint32_t len);
void tg18_reset(tg18_t *t);
void tg18_run_cycles(tg18_t *t, uint64_t target);
void tg18_run_us(tg18_t *t, uint32_t us);
uint64_t tg18_cycles(tg18_t *t);
void tg18_set_buttons(tg18_t *t, int mask);
void tg18_set_present(tg18_t *t, t18_present_fn fn, void *user);
#ifndef PARADISE
const uint16_t *tg18_picture(tg18_t *t);       /* PC: the last finished picture */
#endif
int tg18_frame_count(tg18_t *t);
int tg18_picture_count(tg18_t *t);
uint32_t tg18_tone_hz(tg18_t *t);
int tg18_backlight(tg18_t *t);

void tg18_set_time(tg18_t *t, int y, int mo, int d, int h, int mi, int s);
void tg18_get_time(tg18_t *t, int *out6);
extern int tg18_uhr_wecken;
void tg18_advance_offline(tg18_t *t, int64_t seconds);

uint32_t tg18_state_size(tg18_t *t);
int tg18_save_state(tg18_t *t, uint8_t *buf, uint32_t len);
int tg18_load_state(tg18_t *t, const uint8_t *buf, uint32_t len);
int tg18_flash_dirty(tg18_t *t);
void tg18_flash_clean(tg18_t *t);

int tg18_power(tg18_t *t);
void tg18_set_addrs(tg18_t *t, uint32_t idle_addr, uint32_t softrtc, uint32_t dma_tab);
void tg18_get_addrs(tg18_t *t, uint32_t *out3);
void tg18_set_param_addr(tg18_t *t, uint32_t a);
int tg18_attention(tg18_t *t);
uint32_t tg18_sysclk(tg18_t *t);
uint32_t tg18_boot_count(tg18_t *t);
arm7_t *tg18_cpu(tg18_t *t);
int tg18_sf_fast(void *ctx, uint32_t a, uint32_t *v, int wr);   /* SPI flash registers, word access (1.6.6) */
void tg18_set_trace(tg18_t *t, int on);
int tg18_param_get16(tg18_t *t, uint32_t off);
int tg18_param_set16(tg18_t *t, uint32_t off, int v);
void tg18_set_clock_mult(tg18_t *t, int mult);
int tg18_clock_mult(tg18_t *t);
void tg18_set_keep_awake(tg18_t *t, int on);
uint32_t tg18_idle_addr(tg18_t *t);
void tg18_set_cpu_boost(tg18_t *t, int k);
int tg18_next_wake_s(tg18_t *t);
#ifdef T18_JIT
void t18_jit_reset(void);
#endif
#endif
