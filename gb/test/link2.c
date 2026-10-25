/* Two Game Boys over GBEmu's link (gb_link.h), on the PC: interleaved a scanline at a time, frames between them
 * delayed by LAT scanlines. link2 <rom> <save0|-> <save1|-> <script0> <script1> <frames> <outdir>
 * script: lines "frame keys" (keys: Peanut joypad bits pressed, 1 A 2 B 4 SELECT 8 START 16 R 32 L 64 U 128 D),
 * "frame shot". Saves the cartridge RAM of both at the end (out/save0.sav, save1.sav). Test only. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
static uint8_t *g_rom; static size_t g_rom_n;
#define PGB_ROM g_rom
#define ENABLE_SOUND 0
#define PEANUT_GB_HIGH_LCD_ACCURACY 1
#define PGB_SERIAL_WAIT
#include "../src/core/peanut_gb.h"
static struct gb_s G[2];
static uint8_t cram[2][0x8000];
static uint8_t fb[2][144][160];
static int cur;                       /* which one runs */
static long lines[2];                 /* scanlines run (time) */
#define LAT (getenv("LAT") ? atoi(getenv("LAT")) : 20)
typedef struct { long at; int to; uint8_t k, b; } Msg;
static Msg q[4096]; static int qn;
static long now_lines(void) { return lines[cur]; }
struct GlState; static struct GlState *gl_cur;
#define GL_S (*gl_cur)
static void gl_got(uint8_t kind, uint8_t b);
static void send_to(int to, uint8_t k, uint8_t b) { if (getenv("DBG")) fprintf(stderr, "%ld gb%d -> %c %02x\n", lines[cur], cur, k, b); if (qn < 4096) { q[qn].at = lines[cur] + LAT; q[qn].to = to; q[qn].k = k; q[qn].b = b; qn++; } }
static void poll_cur(void)
{
  for (int i = 0; i < qn; i++) if (q[i].to == cur && q[i].at <= lines[cur]) { Msg m = q[i]; memmove(&q[i], &q[i + 1], (size_t)(qn - i - 1) * sizeof(Msg)); qn--; i--; gl_got(m.k, m.b); }
}
static int g_partner = 1;
#define GL_SEND(k, b) send_to(1 - cur, (k), (b))
#define GL_POLL() poll_cur()
#define GL_NOW() ((uint32_t)(lines[cur] * 1000 / 9198))
#define GL_PARTNER() g_partner
#include "../src/app/gb_link.h"
static GlState gls[2];
static uint8_t rom_rd(struct gb_s *gb, const uint_fast32_t a) { (void)gb; return a < g_rom_n ? g_rom[a] : 0xFF; }
static uint8_t cram_rd(struct gb_s *gb, const uint_fast32_t a) { return cram[gb == &G[1]][a & 0x7FFF]; }
static void cram_wr(struct gb_s *gb, const uint_fast32_t a, const uint8_t v) { cram[gb == &G[1]][a & 0x7FFF] = v; }
static void err(struct gb_s *gb, const enum gb_error_e e, const uint16_t a) { fprintf(stderr, "gb%d error %d at %04x\n", gb == &G[1], e, a); }
static void lcd(struct gb_s *gb, const uint8_t px[160], const uint_fast8_t line) { int i = gb == &G[1]; for (int x = 0; x < 160; x++) fb[i][line][x] = px[x] & 3; }
static void shot(const char *dir, int i, int f)
{
  char p[512]; snprintf(p, sizeof p, "%s/g%d_%05d.pgm", dir, i, f); FILE *o = fopen(p, "wb"); fprintf(o, "P5 160 144 255\n");
  static const uint8_t sh[4] = { 0xE0, 0xA8, 0x60, 0x18 };
  for (int y = 0; y < 144; y++) for (int x = 0; x < 160; x++) fputc(sh[fb[i][y][x]], o); fclose(o);
}
typedef struct { int f; int keys; int shot; int pa, pv; } Ev;
static int load_script(const char *fn, Ev *ev, int max)
{
  int n = 0; if (!strcmp(fn, "-")) return 0; FILE *f = fopen(fn, "r"); char l[128];
  while (n < max && fgets(l, sizeof l, f)) { int fr; char w[32], a[32], v[32]; int k = sscanf(l, "%d %31s %31s %31s", &fr, w, a, v); if (k < 2) continue; ev[n].f = fr; ev[n].pa = 0;
    if (!strcmp(w, "peek") && k >= 3) { ev[n].pa = (int)strtol(a, 0, 16); ev[n].pv = -1; ev[n].shot = 0; ev[n].keys = -1; n++; continue; }
    if (!strcmp(w, "poke") && k == 4) { ev[n].pa = (int)strtol(a, 0, 16); ev[n].pv = (int)strtol(v, 0, 16); ev[n].shot = 0; ev[n].keys = -1; n++; continue; }
    ev[n].shot = !strcmp(w, "shot"); ev[n].keys = ev[n].shot ? 0 : (int)strtol(w, 0, 0); n++; }
  fclose(f); return n;
}
static uint8_t *rd(const char *f, size_t *n) { FILE *x = fopen(f, "rb"); if (!x) return 0; fseek(x, 0, 2); *n = (size_t)ftell(x); fseek(x, 0, 0); uint8_t *p = malloc(*n); if (fread(p, 1, *n, x) != *n) {} fclose(x); return p; }
int main(int argc, char **argv)
{
  g_rom = rd(argv[1], &g_rom_n);
  for (int i = 0; i < 2; i++) {
    memset(cram[i], 0xFF, sizeof cram[i]);
    if (strcmp(argv[2 + i], "-")) { size_t n; uint8_t *s = rd(argv[2 + i], &n); if (s) { memcpy(cram[i], s, n > 0x8000 ? 0x8000 : n); free(s); } }
    if (gb_init(&G[i], rom_rd, cram_rd, cram_wr, err, 0) != GB_INIT_NO_ERROR) { printf("init\n"); return 1; }
    gb_init_lcd(&G[i], lcd); gb_init_serial(&G[i], gl_tx, gl_rx);
    G[i].direct.joypad = 0xFF;
  }
  static Ev ev[2][4096]; int ne[2]; ne[0] = load_script(argv[4], ev[0], 4096); ne[1] = load_script(argv[5], ev[1], 4096);
  int frames = atoi(argv[6]); const char *out = argv[7];
  if (getenv("SOLO")) g_partner = 0;
  int fr[2] = { 0, 0 }, ei[2] = { 0, 0 };
  while (fr[0] < frames || fr[1] < frames) {
    for (int i = 0; i < 2; i++) {
      if (fr[i] >= frames) continue;
      cur = i; gl_cur = &gls[i];
      uint8_t ly = G[i].hram_io[IO_LY];
      G[i].gb_frame = false;
      long guard = 0;
      while (G[i].hram_io[IO_LY] == ly && !G[i].gb_frame && guard++ < 200000) __gb_step_cpu(&G[i]);
      lines[i]++;
      if (G[i].gb_frame || (G[i].hram_io[IO_LY] == 0 && ly == 153)) {
        fr[i]++;
        gl_tick();
        while (ei[i] < ne[i] && ev[i][ei[i]].f <= fr[i]) { Ev *e = &ev[i][ei[i]++]; if (e->pa && e->pv < 0) printf("gb%d f%d peek %04x = %02x\n", i, fr[i], e->pa, __gb_read(&G[i], (uint16_t)e->pa)); else if (e->pa) __gb_write(&G[i], (uint16_t)e->pa, (uint8_t)e->pv); else if (e->shot) shot(out, i, fr[i]); else G[i].direct.joypad = (uint8_t)~e->keys; }
      }
    }
  }
  for (int i = 0; i < 2; i++) { char p[512]; snprintf(p, sizeof p, "%s/save%d.sav", out, i); FILE *o = fopen(p, "wb"); fwrite(cram[i], 1, 0x8000, o); fclose(o);
    printf("gb%d frames %d tx %u rx %u\n", i, fr[i], gls[i].n_tx, gls[i].n_rx); }
  return 0;
}
