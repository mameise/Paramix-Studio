/* PC test of the Paradise T7 app logic (t7app.c): screen, clock, save, catching up, figures.
 * happ <rom> [figure...]   commands on stdin:
 *   w <s>        run s seconds (rtc counts along, app_clock every 2 s)
 *   slow <s>     run s seconds, but the rtc counts twice as fast (emulation too slow)
 *   p A|B|C|W    press (W = wheel short = figure menu)
 *   poke <h> <m> <s>   set the game clock
 *   t            print the game clock
 *   save <f> / load <f> [pause_s]   save state / load it after a pause (catch up)
 *   s <file.ppm> screenshot of the 128x128 screen
 */
#include "../src/core/t7.h"
#include "../src/app/t7app.h"
#include <stdio.h>
void host_set_rom(unsigned char *p, size_t n);
void app_wheel_short(void);
static unsigned char *rd(const char *f, size_t *n) { FILE *x = fopen(f, "rb"); fseek(x, 0, 2); *n = ftell(x); fseek(x, 0, 0); unsigned char *p = malloc(*n); if (fread(p, 1, *n, x)) {} fclose(x); return p; }
static uint16_t fb[128][128]; static int rx0, rx1, ry;
static uint32_t g_rtc = 1000000, g_ms;
static T7Cfg cfg = { T7_MAGIC, 1, 0, 0, 0, 0, {{0}}, 0 };
static const uint8_t *figs[T7_FIG_MAX];
uint32_t plat_rtc(void) { return g_rtc; }
void plat_vol(int v) { (void)v; }
void plat_bat(char *s) { const char *t = "2.84V 80%"; int i = 0; while ((s[i] = t[i])) i++; }
void plat_rect(int x0, int y0, int x1, int y1) { rx0 = x0; rx1 = x1; ry = y0; (void)y1; }
void plat_row(const uint16_t *px, int n) { for (int i = 0; i < n && rx0 + i < 128; i++) fb[ry][rx0 + i] = px[i]; ry++; }
void plat_rect_end(void) {}
const uint8_t *plat_fig_ptr(int i) { return figs[i]; }
static uint8_t store[T7_STORE_LEN]; static int have_store;
int plat_store_load(uint8_t *b, uint32_t n) { if (!have_store) return 0; memcpy(b, store, n); return 1; }
void plat_store_write(const uint8_t *b, uint32_t n) { memcpy(store, b, n); have_store = 1; printf("store: bg=%d size=%d pixel=%d note=%d\n", b[4], b[8], b[9], b[32] == 'T'); }
static int keys;
static void run(double s, int rtcx)
{
  int n = (int)(s * 500 + 0.5);
  for (int i = 0; i < n; i++) {
    g_ms += 2;
    t7_set_buttons((uint8_t)app_keys((uint32_t)keys, g_ms));
    app_tick(g_ms);
    t7_step_cycles(T7_CLOCK_RATE / 500);
    if (g_ms % 1000 == 0) g_rtc += (uint32_t)rtcx;
    if (g_ms % 500 == 0) app_setup_fill(g_rtc);
    if (g_ms % 2000 == 0) app_clock(g_rtc, 0);
    if (g_ms % 34 == 0) app_render(0);
  }
}
static void shot(const char *f)
{
  app_render(0);
  FILE *o = fopen(f, "wb"); fprintf(o, "P6 128 128 255\n");
  for (int y = 0; y < 128; y++) for (int x = 0; x < 128; x++) { uint16_t p = fb[y][x]; fputc((p >> 11) << 3, o); fputc(((p >> 5) & 63) << 2, o); fputc((p & 31) << 3, o); }
  fclose(o);
}
static void prog(int d, int t) { if (d == t) printf("caught up %d hours\n", t); }
int main(int c, char **v)
{
  size_t n; unsigned char *rom = rd(v[1], &n); host_set_rom(rom, n);
  for (int i = 2; i < c && i - 2 < T7_FIG_MAX; i++) {
    size_t fn; figs[i - 2] = rd(v[i], &fn); cfg.fig[i - 2].len = (uint32_t)fn;
    const char *b = strrchr(v[i], '_'); b = b ? b + 1 : v[i]; int k = 0;
    while (b[k] && b[k] != '.' && k < 15) { cfg.fig[i - 2].name[k] = b[k]; k++; }
    cfg.nfig++;
  }
  app_init(&cfg);
  t7_init();
  t7_snd_config(T7_CLOCK_RATE, 16000); t7_snd_capture(0); t7_snd_set_mode(1);
  app_render(1);
  char l[256];
  while (fgets(l, sizeof l, stdin)) {
    char cmd[16], a[200]; double x = 0; int k = sscanf(l, "%15s %199s %lf", cmd, a, &x);
    if (k < 1) continue;
    if (!strcmp(cmd, "w")) run(atof(a), 1);
    else if (!strcmp(cmd, "slow")) run(atof(a), 2);
    else if (!strcmp(cmd, "p")) {
      if (a[0] == 'W') { app_wheel_short(); run(0.3, 1); }
      else { keys = a[0] == 'A' ? 1 : a[0] == 'B' ? 2 : 4; run(0.2, 1); keys = 0; run(k > 2 ? x : 0.8, 1); }
    }
    else if (!strcmp(cmd, "poke")) { int h, m, s2; sscanf(l, "%*s %d %d %d", &h, &m, &s2); t7_ram_write(0x306, h); t7_ram_write(0x307, m); t7_ram_write(t7_rtc_sec_addr(), s2); }
    else if (!strcmp(cmd, "t")) printf("game %02d:%02d:%02d  day %d.%d  rtc %u fig %d species %02x\n", t7_ram_read(0x306), t7_ram_read(0x307), t7_ram_read(t7_rtc_sec_addr()), t7_ram_read(0x305), t7_ram_read(0x304), g_rtc, t7_figure_get(), t7_ram_read(0x22));
    else if (!strcmp(cmd, "save")) { static uint8_t b[4096]; uint32_t m = app_save(b, sizeof b); FILE *o = fopen(a, "wb"); fwrite(b, 1, m, o); fclose(o); printf("saved %u bytes\n", m); }
    else if (!strcmp(cmd, "load")) {
      size_t m; unsigned char *b = rd(a, &m); uint32_t sv;
      g_rtc += (uint32_t)x;
      t7_init();
      printf("load %s\n", app_load(b, (uint32_t)m, &sv) ? "ok" : "FAILED");
      if (app_clock_hard(g_rtc)) printf("clock from the Paradise\n"); else app_catchup(g_rtc, prog);
    }
    else if (!strcmp(cmd, "s")) shot(a);
    else if (!strcmp(cmd, "para")) {   /* para <y> <m> <d> <h> <mi>: the Paradise's time now, clock flag on */
      int y, mo, d, h, mi; sscanf(l, "%*s %d %d %d %d %d", &y, &mo, &d, &h, &mi);
      int yy = y - (mo <= 2), era = yy / 400, yoe = yy - era * 400, doy = (153 * (mo + (mo > 2 ? -3 : 9)) + 2) / 5 + d - 1, doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
      g_para_time = (uint32_t)(era * 146097 + doe - 719468) * 86400u + (uint32_t)(h * 3600 + mi * 60); g_para_cnt = g_rtc; cfg.flags |= 2u;
    }
    else if (!strcmp(cmd, "wall")) { int h, m, d, mo; sscanf(l, "%*s %d %d %d %d", &h, &m, &d, &mo); cfg.wall_cnt = g_rtc; cfg.wall_sod = (uint32_t)(h * 3600 + m * 60); cfg.wall_ymd = (2026u << 16) | (uint32_t)(mo << 8 | d); }
    else if (!strcmp(cmd, "store")) app_store();
    else if (!strcmp(cmd, "set")) { extern T7Set g_set; int sz = 0, px = 0, gr = -1; sscanf(l, "%*s %d %d %d", &sz, &px, &gr); g_set.size = (uint8_t)sz; g_set.pixel = (uint8_t)px; if (gr >= 0) g_set.grid = (uint8_t)gr; app_dim(1); app_dim(0); }   /* set <size> <pixel> [grid] */
    else if (!strcmp(cmd, "reboot")) { t7_init(); app_init(&cfg); app_render(1); }
    else if (!strcmp(cmd, "wb")) { unsigned ad, v; sscanf(l, "%*s %x %x", &ad, &v); t7_ram_write((uint16_t)ad, (uint8_t)v); }   /* test30 */
    else if (!strcmp(cmd, "dump")) { FILE *o = fopen(a, "wb"); for (int i = 0; i < 0x600; i++) fputc(t7_ram_read((uint16_t)i), o); fclose(o); }
    else if (!strcmp(cmd, "q")) break;
    fflush(stdout);
  }
  return 0;
}
