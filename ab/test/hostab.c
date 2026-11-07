/* PC test of the ArduboyEmu core: hostab game.hex seconds [KEYS="t:mask,..."] [SHOTS="t,..."] [OUT=dir]
 * Writes the display as PGM pictures (128 x 64) at the shot times, prints frames, sound and timing. */
#include "../src/core/avr.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
static u16 g_fl[AVR_FLASH_WORDS];
extern u32 ab_pc(void);
static u32 g_hist[AVR_FLASH_WORDS];
static int hexval(const char *s, int n) { int v = 0; for (int i = 0; i < n; i++) { int c = s[i]; v = v * 16 + (c <= '9' ? c - '0' : (c | 32) - 'a' + 10); } return v; }
static u32 load_hex(const char *fn)
{
  FILE *f = fopen(fn, "r"); if (!f) { perror(fn); exit(1); }
  u8 *b = (u8 *)g_fl; memset(b, 0xFF, sizeof g_fl);
  char l[600]; u32 hi = 0, top = 0;
  while (fgets(l, sizeof l, f)) {
    if (l[0] != ':') continue;
    int n = hexval(l + 1, 2), ad = hexval(l + 3, 4), ty = hexval(l + 7, 2);
    if (ty == 0) for (int i = 0; i < n; i++) { u32 a = hi + ad + i; if (a < sizeof g_fl) { b[a] = (u8)hexval(l + 9 + 2 * i, 2); if (a + 1 > top) top = a + 1; } }
    else if (ty == 2) hi = hexval(l + 9, 4) << 4;
    else if (ty == 4) hi = hexval(l + 9, 4) << 16;
  }
  fclose(f);
  return (top + 1) / 2;
}
static void shot(const char *out, int k)
{
  char fn[512]; snprintf(fn, sizeof fn, "%s/shot%02d.pgm", out, k);
  FILE *f = fopen(fn, "wb"); fprintf(f, "P5 128 64 255\n");
  for (int y = 0; y < 64; y++) for (int x = 0; x < 128; x++) {
    int on = (A_GD[(y >> 3) * 128 + x] >> (y & 7)) & 1;
    if (A_LCD.invert) on ^= 1;
    if (!A_LCD.on) on = 0;
    fputc(on ? 255 : 0, f);
  }
  fclose(f);
}
int main(int argc, char **argv)
{
  u32 words = load_hex(argv[1]); double secs = atof(argv[2]);
  const char *ks = getenv("KEYS"), *ss = getenv("SHOTS"), *out = getenv("OUT"); if (!out) out = ".";
  double kt[64]; u32 km[64]; int nk = 0; double st[64]; int ns = 0;
  if (ks) { const char *p = ks; while (*p && nk < 64) { kt[nk] = atof(p); const char *c = strchr(p, ':'); km[nk++] = (u32)atoi(c + 1); p = strchr(c, ','); if (!p) break; p++; } }
  if (ss) { const char *p = ss; while (*p && ns < 64) { st[ns++] = atof(p); p = strchr(p, ','); if (!p) break; p++; } }
  ab_load(g_fl, words); ab_reset();
  for (u32 i = 0; i < AVR_EEP; i++) A_EEP[i] = 0xFF;
  clock_t c0 = clock(); int si = 0; u32 tones = 0, lastf = 0;
  u32 ms_n = (u32)(secs * 1000);
  for (u32 ms = 0; ms < ms_n; ms++) {
    double t = ms / 1000.0; A_KEYS = 0; for (int i = 0; i < nk; i++) if (t >= kt[i]) A_KEYS = km[i];
    if (getenv("CALLER")) { u32 c = strtoul(getenv("CALLER"), 0, 16); for (u32 e = A_CYC + 16000; (s32)(A_CYC - e) < 0; ) { if (ab_pc() == c) { u32 sp = A_D[0x5D] | A_D[0x5E] << 8; g_hist[((A_D[sp + 1] << 8) | A_D[sp + 2]) & 0x3FFF]++; } ab_run(1); } } else
    if (getenv("PCS")) for (int q = 0; q < 16; q++) { ab_run(1000); g_hist[ab_pc() & 0x3FFF]++; } else ab_run(16000);
    if (ms % 50 == 49) { u32 hz = ab_tone(800000); if (hz) tones++; }
    while (si < ns && t >= st[si]) shot(out, si++);
    if (ms % 1000 == 999) { printf("t=%u s frames %u (+%u)\n", (ms + 1) / 1000, A_FRAMES, A_FRAMES - lastf); lastf = A_FRAMES; }
  }
  while (si < ns) shot(out, si++);
  if (getenv("PCS") || getenv("CALLER")) { for (int k = 0; k < 8; k++) { u32 b = 0, bi = 0; for (u32 i = 0; i < AVR_FLASH_WORDS; i++) if (g_hist[i] > b) { b = g_hist[i]; bi = i; } printf("pc %04x (byte %05x) %u\n", bi, bi * 2, b); g_hist[bi] = 0; } }
  double el = (double)(clock() - c0) / CLOCKS_PER_SEC;
  printf("frames %u, tone windows %u, host %.2f s for %.1f s\n", A_FRAMES, tones, el, secs);
  return 0;
}
