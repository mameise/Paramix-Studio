/* SMSEmu on the PC, for tests only: host <rom> <frames> <outdir> [KEYS="frame:mask,..."] [SHOTS="frame,..."]
 * Writes shot_<frame>.ppm (256 x 192) and small_<frame>.ppm (128 x 96 as the Paradise shows it). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdint.h>
static uint8_t g_img[192][256];
#define SMS_LINE(y, lb) memcpy(g_img[y], (lb), 256)
#include "../core/sms.h"
static uint8_t rom[4 << 20], cram[0x8000];
static void ppm(const char *dir, const char *nm, int f)
{
  char p[512]; snprintf(p, sizeof p, "%s/%s_%05d.ppm", dir, nm, f);
  FILE *o = fopen(p, "wb");
  if (!strcmp(nm, "shot")) {
    fprintf(o, "P6 256 192 255\n");
    for (int y = 0; y < 192; y++) for (int x = 0; x < 256; x++) { uint16_t c = g_sms.pal565[g_img[y][x]]; uint8_t b[3] = { (uint8_t)((c >> 11) << 3), (uint8_t)(((c >> 5) & 63) << 2), (uint8_t)((c & 31) << 3) }; fwrite(b, 1, 3, o); }
  } else {
    fprintf(o, "P6 128 96 255\n");
    for (int y = 0; y < 96; y++) for (int x = 0; x < 128; x++) {
      int r = 0, g = 0, b = 0;
      for (int k = 0; k < 4; k++) { uint16_t c = g_sms.pal565[g_img[y * 2 + (k >> 1)][x * 2 + (k & 1)]]; r += c >> 11; g += (c >> 5) & 63; b += c & 31; }
      uint8_t q[3] = { (uint8_t)(r * 2), (uint8_t)(g), (uint8_t)(b * 2) }; fwrite(q, 1, 3, o); }
  }
  fclose(o);
}
int main(int argc, char **argv)
{
  FILE *f = fopen(argv[1], "rb"); size_t n = fread(rom, 1, sizeof rom, f); fclose(f);
  int frames = atoi(argv[2]); const char *dir = argv[3];
  if ((n & 0x3FFF) == 512) { memmove(rom, rom + 512, n - 512); n -= 512; }
  { size_t L = strlen(argv[1]); g_sms.gg = L > 3 && !strcmp(argv[1] + L - 3, ".gg"); }
  g_sms.rom = rom; g_sms.rom_len = (uint32_t)n; g_sms.cram = cram; g_sms.cram_len = sizeof cram;
  sms_reset();
  const char *keys = getenv("KEYS"), *shots = getenv("SHOTS");
  clock_t t0 = clock();
  for (int fr = 0; fr < frames; fr++) {
    if (keys) { const char *k = keys; while (*k) { int a = atoi(k); const char *c = strchr(k, ':'); if (!c) break; int m = (int)strtol(c + 1, 0, 0); if (a == fr) g_sms.pad = (uint8_t)m; k = strchr(c, ','); if (!k) break; k++; } }
    sms_frame(1);
    if (shots) { const char *k = shots; while (*k) { if (atoi(k) == fr) { ppm(dir, "shot", fr); ppm(dir, "small", fr); } k = strchr(k, ','); if (!k) break; k++; } }
  }
  double s = (double)(clock() - t0) / CLOCKS_PER_SEC;
  printf("%d frames in %.2f s (%.0f fps), pc %04X\n", frames, s, frames / s, g_cpu.pc);
  return 0;
}
