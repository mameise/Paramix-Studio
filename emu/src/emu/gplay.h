/* 1.7.2 test14 (GPLAY): play time in DOOM, Wolfenstein, Game Boy and Master System earns Tamapoints for the Paradise.
 * Also Pokemon mini and Arduboy (1.7.3). Time counts between two key presses that lie at most 60 s apart (the Paradise's seconds counter 0x45000304, it runs
 * in every program). Going back, 10 points per whole minute go into the money of the settings block 0xEFE000/0xEFF000
 * (both copies, new CRC, as the menu sets Tamapoints), when the menu's settings block has the flag F_GPLAY (64): the
 * Studio sets it with the checkbox in the Menu tab, only in a ROM with the Paradise. The rest of a minute stays. */
#ifndef GPLAY_H
#define GPLAY_H
#include <stdint.h>
extern uint32_t g_gp_secs, g_gp_last;
#define GP_CNT (*(volatile uint32_t *)0x45000304u)
#define GP_RATE 10u
#define GP_MAX 99999u
#define GP_F 64u
static inline void gp_key(uint32_t b)
{
  if (!b) return;
  uint32_t s = GP_CNT, d = s - g_gp_last;
  if (g_gp_last && d <= 60u) g_gp_secs += d;
  g_gp_last = s;
}
#ifdef GP_POINTS   /* the part that writes (with paraback.h: flash_read, flash_update4k, pb_crc) */
static inline uint32_t gp_u32(const uint8_t *b, uint32_t o) { return b[o] | (uint32_t)b[o + 1] << 8 | (uint32_t)b[o + 2] << 16 | (uint32_t)b[o + 3] << 24; }
/* b: 4 KB buffer. Returns the points added. */
__attribute__((noinline, unused, minsize)) PB_ATTR static uint32_t para_points(uint8_t *b)
{
  uint32_t add = g_gp_secs / 60u * GP_RATE, mo = 0;
  if (!add) return 0;
  for (uint32_t p = 0x90000u; p < 0x110000u && !mo; p += 0x1000u) {
    flash_read(p + 0x140u, b, 264u);
    if (gp_u32(b, 0) == 0x444C4546u && gp_u32(b, 4) >= 9u && (gp_u32(b, 36) & GP_F)) mo = gp_u32(b, 260);
  }
  if (!mo || mo + 4u > 0x1000u) return 0;
  flash_read(0xEFE000u, b, 0x1000u);
  uint32_t c = pb_crc(b);
  if (pb_u16(b, 0) != c || pb_u16(b, 2) != (~c & 0xFFFFu)) return 0;
  uint32_t m = gp_u32(b, mo), n = m + add;
  if (m > GP_MAX) return 0;   /* not a money field as we know it */
  if (n > GP_MAX) n = GP_MAX;
  b[mo] = (uint8_t)n; b[mo + 1] = (uint8_t)(n >> 8); b[mo + 2] = (uint8_t)(n >> 16); b[mo + 3] = (uint8_t)(n >> 24);
  c = pb_crc(b);
  pb_w16(b, 0, c); pb_w16(b, 2, ~c & 0xFFFFu);
  flash_update4k(0xEFE000u, b);
  flash_update4k(0xEFF000u, b);
  g_gp_secs %= 60u;
  return n - m;
}
#endif
#endif
