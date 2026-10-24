/* PC run of the V3 core: v3run <rom> <seconds> <prefix> [keys "t:mask,..."]
 * env TRACE=n: the first n steps as "n pc a x y sp NVDIZC" (as BrickEmuPy, see the reference ref.py)
 * env SHOTS=ms: a 32x30 PGM every ms. Author: mameise */
#include "../src/core/v3cpu.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static uint16_t g_map[30][32];   /* LCD RAM byte << 3 | bit for each pixel */
#include "../src/core/v3lcd.inc"
static void shot(const char *fn)
{
  int mode; const uint8_t *v = v3_vram(&mode);
  FILE *o = fopen(fn, "wb"); fprintf(o, "P5 32 30 255\n");
  for (int y = 0; y < 30; y++) for (int x = 0; x < 32; x++) { int k = V3_LCD[y][x], on = mode == 2 || (mode == 1 && (v[k >> 3] >> (k & 7) & 1)); fputc(on ? 0 : 255, o); }
  fclose(o);
}
int main(int c, char **v)
{
  FILE *f = fopen(v[1], "rb"); fseek(f, 0, 2); long n = ftell(f); fseek(f, 0, 0); uint8_t *rom = malloc(n); if (fread(rom, 1, n, f)) {} fclose(f);
  v3_init(rom, (uint32_t)n);
  double secs = atof(v[2]); const char *pre = v[3];
  struct { double t; unsigned m; } ks[64]; int nk = 0, ki = 0;
  if (c > 4) { char *s = strdup(v[4]); for (char *p = strtok(s, ","); p && nk < 64; p = strtok(0, ",")) { ks[nk].t = atof(p); ks[nk].m = (unsigned)strtoul(strchr(p, ':') + 1, 0, 0); nk++; } }
  /* env RX="t:hexbytes,...": bytes into the infrared receiver at time t (s); bytes sent are printed as "TX t hex" */
  struct { double t; uint8_t b[32]; int n; } rx[16]; int nrx = 0, ri = 0;
  if (getenv("RX")) { char *s = strdup(getenv("RX")); for (char *p = strtok(s, ","); p && nrx < 16; p = strtok(0, ",")) { rx[nrx].t = atof(p); const char *h = strchr(p, ':') + 1; rx[nrx].n = 0; while (h[0] && h[1] && rx[nrx].n < 32) { unsigned x; sscanf(h, "%2x", &x); rx[nrx].b[rx[nrx].n++] = (uint8_t)x; h += 2; } nrx++; } }
  long tfrom = getenv("TRACEFROM") ? atol(getenv("TRACEFROM")) : 0;
  long tr = getenv("TRACE") ? atol(getenv("TRACE")) : 0, shots = getenv("SHOTS") ? atol(getenv("SHOTS")) : 0;
  FILE *tf = 0; if (tr) { char fn[256]; snprintf(fn, sizeof fn, "%s_trace.txt", pre); tf = fopen(fn, "w"); }
  uint64_t total = (uint64_t)(secs * V3_CLOCK) * V3_TICKS, t = 0, next = 0; long i = 0; int si = 0;
  while (t < total) {
    while (ki < nk && t >= (uint64_t)(ks[ki].t * V3_CLOCK) * V3_TICKS) v3_set_keys(ks[ki++].m);
    while (ri < nrx && t >= (uint64_t)(rx[ri].t * V3_CLOCK) * V3_TICKS) { v3_ir_rx(rx[ri].b, rx[ri].n); ri++; }
    if (tf && i >= tfrom && i < tfrom + tr) { uint8_t r[5], fl[6]; v3_regs(r, fl); fprintf(tf, "%ld %04x %02x %02x %02x %02x %d%d%d%d%d%d\n", i, v3_pc(), r[0], r[1], r[2], r[3], fl[0], fl[1], fl[2], fl[3], fl[4], fl[5]); }
    t += v3_step(); i++;
    { uint8_t o[32]; int n = v3_ir_tx(o, 32); for (int k = 0; k < n; k++) printf("TX %.4f %02x\n", (double)t / V3_TICKS / V3_CLOCK, o[k]); }
    if (shots && t >= next) { char fn[256]; snprintf(fn, sizeof fn, "%s_%03d.pgm", pre, si++); shot(fn); next += (uint64_t)V3_CLOCK * V3_TICKS * (uint64_t)shots / 1000u; }
  }
  char fn[256]; snprintf(fn, sizeof fn, "%s_end.pgm", pre); shot(fn);
  printf("steps %ld pc %04x bank %u tone %u\n", i, v3_pc(), v3_bank(), v3_tone_hz());
  return 0;
}
