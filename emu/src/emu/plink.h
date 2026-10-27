/* plink.h - 1.6.9: link to another Paradise over the UART on the contacts (as T18Emu's T18_LINK).
 * UART1 0x4000B000, IRQ 37, 460800 8N1 like the flash program. Frames: A5 type len data sum (sum = type + len + data,
 * low byte), 5A as start byte is taken too (from the PC). Every second a hello 'H' (ignored by the other side).
 * Two Paradise plugged together at the top (their connector crosses TX and RX).
 * Header only: include once in the program's main.c, put plink_irq at vector 16 + 37, call plink_init once and
 * plink_poll in the main loop. Author: mameise */
#ifndef PLINK_H
#define PLINK_H
#include "io.h"
#ifndef PL_ATTR
#define PL_ATTR __attribute__((noinline))
#endif
#ifndef PL_INIT_ATTR
#define PL_INIT_ATTR PL_ATTR
#endif

#define PL_U1 0x4000B000u
#define PL_DATA REG(PL_U1 + 0x00)
#define PL_IER  REG(PL_U1 + 0x04)
#define PL_FCR  REG(PL_U1 + 0x08)
#define PL_LCR  REG(PL_U1 + 0x0C)
#define PL_LSR  REG(PL_U1 + 0x14)
#define PL_FDR  REG(PL_U1 + 0x28)
#define PL_CTRL REG(PL_U1 + 0x30)

typedef void (*plink_fn)(u8 ty, const u8 *d, int n);
static struct {
  u8 rx[256]; volatile u8 rh, rt;
  u8 st, ty, len, pos, sum, on;
  u8 f[64];
  u32 hello, last_rx;
} g_pl;

__attribute__((used)) static void plink_irq(void)
{
  while (PL_LSR & 1u) { u8 b = (u8)PL_DATA; u8 nt = (u8)(g_pl.rt + 1u); if (nt != g_pl.rh) { g_pl.rx[g_pl.rt] = b; g_pl.rt = nt; } }
}
PL_INIT_ATTR static void plink_init(void)
{
  g_pl.rh = g_pl.rt = 0; g_pl.st = 0; g_pl.hello = 0; g_pl.last_rx = 0;
  PL_IER = 0;
  PL_LCR = 0x83; PL_DATA = 0x1A; PL_IER = 0x00; PL_LCR = 0x03; PL_FDR = 0x100; PL_FCR = 0x07; PL_CTRL = 0xC1;   /* 460800 8N1 */
  while (PL_LSR & 1u) (void)PL_DATA;
  PL_IER = 0x01;                                     /* received data */
  REG(0xE000E104u) = 1u << (37 - 32);                /* NVIC: UART1 */
  g_pl.on = 1;
}
PL_ATTR static void plink_send(u8 ty, const u8 *d, int n)
{
  if (!g_pl.on) return;
  u8 h[3] = { 0xA5, ty, (u8)n }, s = (u8)(ty + n);
  for (int i = 0; i < n + 4; i++) {
    u8 v = i < 3 ? h[i] : i < n + 3 ? d[i - 3] : s;
    if (i >= 3 && i < n + 3) s = (u8)(s + v);
    for (u32 w = 0; !(PL_LSR & 0x20u); w++) if (w > 20000u) { g_pl.on = 0; return; }   /* no UART: off, the game runs on */
    PL_DATA = v;
  }
}
/* flags for the hello: bit 0 = in the connection screen (ready) */
PL_ATTR static void plink_poll(u32 now, u8 flags, plink_fn fn)
{
  if (!g_pl.on) return;
  while (g_pl.rh != g_pl.rt) {
    u8 b = g_pl.rx[g_pl.rh]; g_pl.rh = (u8)(g_pl.rh + 1u);
    switch (g_pl.st) {
    case 0: if (b == 0xA5 || b == 0x5A) g_pl.st = 1; break;
    case 1: g_pl.ty = b; g_pl.sum = b; g_pl.st = 2; break;
    case 2: if (b > 64) { g_pl.st = 0; break; } g_pl.len = b; g_pl.pos = 0; g_pl.sum = (u8)(g_pl.sum + b); g_pl.st = b ? 3 : 4; break;
    case 3: g_pl.f[g_pl.pos++] = b; g_pl.sum = (u8)(g_pl.sum + b); if (g_pl.pos >= g_pl.len) g_pl.st = 4; break;
    default:
      g_pl.st = 0;
      if (b != g_pl.sum) break;                      /* noise on an open contact */
      g_pl.last_rx = now ? now : 1u;
      if (g_pl.ty != 'H' && g_pl.ty != 'P') fn(g_pl.ty, g_pl.f, g_pl.len);
      break;
    }
  }
  if (now - g_pl.hello >= 1000u) { g_pl.hello = now; plink_send('H', &flags, 1); }
}
#endif
