/* T7 on the Paradise: screen layout, settings menu, clock and saved state.
 * No hardware access here (see main.c); the PC test in host/ uses the same file.
 * Author: mameise */
#include "t7app.h"
#ifdef BL_PWM
void hw_bl_set(unsigned int pct);
#endif
#include "../core/t7.h"
#include "font.h"
#include "icons.h"
#ifdef T7_FR_ICONS
#include "icons_friends.h"
extern uint32_t g_rom_size;
#endif

void figure_rom_set_active(const unsigned char *p, unsigned int n);

/* ---------------------------------------------------------------- screen
 * 128x128. Icons 20x20 in a row at the top (y 2) and at the bottom (y 106).
 * Game screen 48x31: size 0 twice as big (96x62), size 1 as wide as the screen (128x83,
 * cells of 2 or 3 pixels). Menu and messages use the whole area between the icons. */
#define SCR 128
#define ICON_TOP 2
#define ICON_BOT 106
#define MX 0
#define MY 22
#define MW 128
#define MH 84
#define LCDW T7_LCD_WIDTH
#define LCDH T7_LCD_HEIGHT
static const volatile T7Cfg *g_cfg;
static int GX, GY, GW, GH;                      /* game screen on the display */
static uint8_t g_cx[SCR], g_gx[SCR];            /* per display column: game column, last column of the cell */
static uint8_t g_ry[LCDH + 1];                  /* first display row of each game row */
static uint16_t g_pal[4], g_grid, g_off, g_ghostc, g_mpal[4];
static uint8_t g_disp[LCDW * LCDH], g_prev[LCDW * LCDH];
static uint8_t g_ic[T7_ICON_COUNT], g_icp[T7_ICON_COUNT];
static uint16_t g_row[SCR];
static int g_full = 1;

static uint16_t mix(uint16_t a, uint16_t b, int pa)   /* pa percent of a, rest b */
{
  int r = (((a >> 11) & 31) * pa + ((b >> 11) & 31) * (100 - pa) + 50) / 100;
  int g = (((a >> 5) & 63) * pa + ((b >> 5) & 63) * (100 - pa) + 50) / 100;
  int c = ((a & 31) * pa + (b & 31) * (100 - pa) + 50) / 100;
  return (uint16_t)((r << 11) | (g << 5) | c);
}
/* Settings. Colour tables and the grey, grid, pixel and icon values as in the ESP32 version. */
#define BG_N 14
static const uint16_t PAL_BG[BG_N] = { 0xFFFF, 0xAE66, 0x9DE1, 0xB79A, 0xFD80, 0xB71F, 0x4C67, 0x469A, 0xFF08, 0xFC06, 0xFD99, 0xC51F, 0x0000, 0xF800 };
static const char *const BG_DE[BG_N] = { "Weiss", "Gruen", "GB-Gruen", "Mint", "Amber", "Blau", "Dunkelgruen", "Tuerkis", "Gelb", "Orange", "Rosa", "Lila", "Schwarz", "Rot" };
static const char *const BG_EN[BG_N] = { "White", "Green", "GB green", "Mint", "Amber", "Blue", "Dark green", "Turquoise", "Yellow", "Orange", "Pink", "Purple", "Black", "Red" };
#ifdef BL_PWM
static const T7Set SET_DEF = { 1, 0, 90, 70, 0, 0, 0, 3, 3, 1, 2, 1, 0xFF, 3, 3, 5 };
#else
static const T7Set SET_DEF = { 1, 0, 90, 70, 0, 0, 0, 3, 3, 1, 2, 1, 0xFF, 1, 2, 5 };
#endif
T7Set g_set = { 1, 0, 90, 70, 0, 0, 0, 3, 3, 1, 2, 1, 0xFF, 1, 2, 5 };
static int g_lang;                                /* 0 German, 1 English */
static int g_dimp;                                /* dimmed: colours at this percent, 0 = normal */
int app_lang(void) { return g_lang; }
static const char *tr(const char *de, const char *en) { return g_lang ? en : de; }

static void geometry(void)
{
  if (g_set.size) { GW = SCR; GH = 83; GX = 0; GY = MY + 1; }
  else { GW = LCDW * 2; GH = LCDH * 2; GX = (SCR - GW) / 2; GY = MY + (MH - GH) / 2; }
  for (int c = 0; c < LCDW; c++) {
    int x0 = c * GW / LCDW, x1 = (c + 1) * GW / LCDW;
#ifdef T7_PIXSOFT
    for (int x = x0; x < x1; x++) { g_cx[x] = (uint8_t)c; g_gx[x] = x == x1 - 1; }   /* test13: a gap in every cell, in the cell's own colour (draw_row) */
#else
    for (int x = x0; x < x1; x++) { g_cx[x] = (uint8_t)c; g_gx[x] = x == x1 - 1 && (!g_set.size || x1 - x0 >= 3); }   /* large: gap only in 3 pixel cells, dots stay 2 wide */
#endif
  }
  for (int r = 0; r <= LCDH; r++) g_ry[r] = (uint8_t)(r * GH / LCDH);
}
static void palette(void)
{
  if (g_set.bg >= BG_N) g_set.bg = 1;
  if (g_set.ink > BG_N) g_set.ink = 0;
  if (g_set.g1 > 100) g_set.g1 = 90;
  if (g_set.g2 > 100) g_set.g2 = 70;
  if (g_set.grid > 100) g_set.grid = 0;
  if (g_set.pixvis > 10) g_set.pixvis = 3;
  if (g_set.ghost > 10) g_set.ghost = 3;
  g_lang = g_set.lang == 0xFF ? (int)(g_cfg->flags & 1u) : (g_set.lang & 1);
  uint16_t bg = PAL_BG[g_set.bg];
  uint16_t auto_ink = g_set.bg == 12 ? 0xFFFF : 0x0000;               /* on black: white */
  uint16_t ink = g_set.ink ? PAL_BG[g_set.ink - 1] : auto_ink;
  const int pct[4] = { 100, g_set.g1, g_set.g2, 0 };
  for (int i = 0; i < 4; i++) { g_pal[i] = mix(bg, ink, pct[i]); g_mpal[i] = mix(bg, auto_ink, i == 0 ? 100 : i == 1 ? 90 : i == 2 ? 60 : 0); }
  g_grid = mix(bg, mix(bg, ink, 76), 100 - g_set.grid);              /* full grid: 24 percent towards the ink */
  g_off = mix(bg, ink, 100 - g_set.pixvis);                           /* pixels that are off, slightly visible */
  g_ghostc = mix(bg, ink, 100 - g_set.ghost);                         /* icons that are off */
  if (g_dimp) {                                                       /* dimmed (screen left on) */
    for (int i = 0; i < 4; i++) { g_pal[i] = mix(g_pal[i], 0, g_dimp); g_mpal[i] = mix(g_mpal[i], 0, g_dimp); }
    g_grid = mix(g_grid, 0, g_dimp); g_off = mix(g_off, 0, g_dimp); g_ghostc = mix(g_ghostc, 0, g_dimp);
  }
  geometry();
}
static int icon_x(int i) { return (i % 5) * SCR / 5 + (SCR / 5 - 20) / 2; }
static void draw_icon(int i, int on)
{
  int x0 = icon_x(i), y0 = i < 5 ? ICON_TOP : ICON_BOT;
  uint16_t c = on ? g_pal[3] : g_set.ghost ? g_ghostc : g_pal[0];
  plat_rect(x0, y0, x0 + 20, y0 + 20);
  for (int y = 0; y < 20; y++) {
#ifdef T7_FR_ICONS
    uint32_t b = g_rom_size == 0xC0000u ? g_icons_fr[i][y] : g_icons[i][y];   /* Friends ROM (768 KB): its own icons */
#else
    uint32_t b = g_icons[i][y];
#endif
    for (int x = 0; x < 20; x++) g_row[x] = (b & (0x80000u >> x)) ? c : g_pal[0];
    plat_row(g_row, 20);
  }
  plat_rect_end();
}
static void fill(int x0, int y0, int x1, int y1, uint16_t c)
{
  plat_rect(x0, y0, x1, y1);
  for (int x = 0; x < x1 - x0; x++) g_row[x] = c;
  for (int y = y0; y < y1; y++) plat_row(g_row, x1 - x0);
  plat_rect_end();
}

/* ---------------------------------------------------------------- menu */
static int g_menu, g_sel, g_top;
static int g_fig = -1, g_fig_next = -2;          /* inserted figure, figure waiting to be inserted */
static uint32_t g_fig_at;
static uint8_t g_txt[MH][MW];                     /* menu as levels 0..3 */
static int glyph(int c) { if (c >= 97 && c <= 122) c -= 32; return (c < 32 || c > 127) ? 0 : c - 32; }
static int slen(const char *s) { int n = 0; while (s[n]) n++; return n; }
static void txt(int x, int y, const char *s, uint8_t lv)
{
  for (; *s && x < MW - 4; s++, x += 6) {
    const uint8_t *g = g_font[glyph((unsigned char)*s)];
    for (int r = 0; r < 7; r++) for (int c = 0; c < 5; c++)
      if (g[r] & (0x10 >> c)) { int yy = y + r, xx = x + c; if (yy >= 0 && yy < MH && xx >= 0 && xx < MW) g_txt[yy][xx] = lv; }
  }
}
static void num(char *o, int v, const char *unit)   /* "25%" or "Aus" for 0 */
{
  int n = 0;
  if (!v) { const char *a = tr("Aus", "Off"); while (*a) o[n++] = *a++; o[n] = 0; return; }
  if (v >= 100) o[n++] = (char)('0' + v / 100);
  if (v >= 10) o[n++] = (char)('0' + (v / 10) % 10);
  o[n++] = (char)('0' + v % 10);
  while (*unit) o[n++] = *unit++;
  o[n] = 0;
}
/* Menu: page 0 = settings, page 1 = figures */
static int g_page;
enum { M_FIG, M_BG, M_INK, M_G1, M_G2, M_SIZE, M_PIX, M_GRID, M_PVIS, M_GHOST, M_SND, M_BL, M_LIGHT, M_DIM, M_OFF, M_SYNC,
#ifdef T7_CARE
  M_GP, M_CARE,
#endif
  M_LANG,
#ifdef BAT_SHOW
  M_BAT,   /* test17: the battery (shown, nothing to change) */
#endif
  M_N };
static int g_items[M_N], g_nitems;
#define ROWS 7
static void menu_items(void)
{
  g_nitems = 0;
#ifdef BAT_SHOW
  g_items[g_nitems++] = M_BAT;   /* first: seen when the menu opens */
#endif
  if (g_cfg->nfig) g_items[g_nitems++] = M_FIG;
  for (int k = M_BG; k < M_N; k++) {
#ifdef BAT_SHOW
    if (k == M_BAT) continue;
#endif
#ifndef BL_PWM
    if (k == M_BL) continue;
#endif
    if (k != M_DIM || app_dim_ms()) g_items[g_nitems++] = k;   /* dim level only when dimming */
  }
}
#ifdef T7_CARE
/* test30: Tama-Go's and Friends' state block in the RAM from 0x2F0 (Friends: same places, checked in the PC run) (what it keeps in EEPROM page 0, same offsets; found in the
 * simulation): Gotchi Points 0x320..0x324 (BCD, lowest digits first), hunger 0x326 and happy 0x327 (0..5 hearts) */
int hw_num_edit(const char *title, uint32_t *v, int digits, uint32_t max, int en);
static uint8_t g_cared;   /* "OK" until the menu closes */
static uint32_t gp_get(void)
{
  uint32_t n = 0;
  for (int i = 4; i >= 0; i--) { uint8_t v = t7_ram_read((uint16_t)(0x320 + i)); n = n * 100u + (v >> 4) * 10u + (v & 15u); }
  return n;
}
static void gp_set(uint32_t n)
{
  for (int i = 0; i < 5; i++) { uint32_t d = n % 100u; n /= 100u; t7_ram_write((uint16_t)(0x320 + i), (uint8_t)((d / 10u) << 4 | (d % 10u))); }
}
#endif
static const char *onoff(int v) { return v ? tr("Ein", "On") : tr("Aus", "Off"); }
static void item_text(int it, const char **label, char *val)
{
  static const char *const tDE[T7_T_N] = { "10 s", "20 s", "30 s", "1 Min", "2 Min", "5 Min", "10 Min" }, *const tEN[T7_T_N] = { "10 s", "20 s", "30 s", "1 min", "2 min", "5 min", "10 min" };
  const char *const *tt = g_lang ? tEN : tDE, *never = tr("Nie", "Never");
  const char *b = 0; val[0] = 0;
  switch (it) {
  case M_FIG: *label = tr("Figur", "Figure"); if (g_fig < 0) b = tr("Keine", "None"); else { int c = 0; for (; c < 15 && g_cfg->fig[g_fig].name[c]; c++) val[c] = g_cfg->fig[g_fig].name[c]; val[c] = 0; } break;
  case M_BG: *label = tr("Farbe", "Colour"); b = g_lang ? BG_EN[g_set.bg] : BG_DE[g_set.bg]; break;
  case M_INK: *label = "Tama"; b = g_set.ink ? (g_lang ? BG_EN[g_set.ink - 1] : BG_DE[g_set.ink - 1]) : "Auto"; break;
  case M_G1: *label = tr("Hellgrau", "Light grey"); num(val, g_set.g1, "%"); if (!g_set.g1) { val[0] = '0'; val[1] = '%'; val[2] = 0; } break;
  case M_G2: *label = tr("Dunkelgrau", "Dark grey"); num(val, g_set.g2, "%"); if (!g_set.g2) { val[0] = '0'; val[1] = '%'; val[2] = 0; } break;
  case M_SIZE: *label = tr("Bild", "Screen"); b = g_set.size ? tr("Gross", "Large") : tr("Pixelgenau", "Pixel exact"); break;
  case M_PIX: *label = "Pixel"; b = onoff(g_set.pixel); break;
  case M_GRID: *label = tr("Gitter", "Grid"); num(val, g_set.grid, "%"); break;
  case M_PVIS: *label = tr("Pixel-Sicht", "Pixel view"); num(val, g_set.pixvis, "%"); break;
  case M_GHOST: *label = "Icons"; num(val, g_set.ghost, "%"); break;
  case M_SND: *label = tr("Ton", "Sound"); b = onoff(g_set.sound); break;
#ifdef BL_PWM
  case M_BL: *label = tr("Licht", "Light"); num(val, app_bl_pct(), "%"); break;
#endif
  case M_LIGHT: *label = tr("Dimmen nach", "Dim after"); b = g_set.light >= T7_T_N ? never : tt[g_set.light]; break;
  case M_DIM: *label = tr("Dimmen auf", "Dim to"); num(val, app_dim_pct(), "%"); break;
  case M_OFF: *label = tr("Bild aus nach", "Screen off"); b = g_set.r2 ? tt[(g_set.r2 - 1) % T7_T_N] : never; break;
  case M_SYNC: *label = tr("Uhr", "Clock"); b = g_set.sync ? "Auto" : tr("Spiel", "Game"); break;
  case M_LANG: *label = tr("Sprache", "Language"); b = g_lang ? "English" : "Deutsch"; break;
#ifdef T7_CARE
  case M_GP: { uint32_t v = gp_get(); char t[8]; int n = 0; do { t[n++] = (char)('0' + v % 10u); v /= 10u; } while (v); int j = 0; while (n) val[j++] = t[--n]; val[j++] = 'p'; val[j] = 0; } *label = "Gotchi Points"; break;
  case M_CARE: *label = tr("Pflege voll", "Full care"); if (g_cared) { val[0] = 'O'; val[1] = 'K'; val[2] = 0; } break;
#endif
#ifdef BAT_SHOW
  case M_BAT: *label = tr("Batterie", "Battery"); plat_bat(val); break;
#endif
  }
  if (b) { int n = 0; while (b[n] && n < 15) { val[n] = b[n]; n++; } val[n] = 0; }
}
static void menu_build(void)
{
  for (int y = 0; y < MH; y++) for (int x = 0; x < MW; x++) g_txt[y][x] = 0;
  int n = g_page ? (int)g_cfg->nfig + 1 : g_nitems;
  txt(2, 1, g_page ? tr("FIGUR", "FIGURE") : tr("EINSTELLUNGEN", "SETTINGS"), 3);
  for (int x = 0; x < MW; x++) g_txt[10][x] = 2;
  if (g_sel < g_top) g_top = g_sel;
  if (g_sel >= g_top + ROWS) g_top = g_sel - ROWS + 1;
  for (int k = 0; k < ROWS && g_top + k < n; k++) {
    int i = g_top + k, y = 13 + k * 10;
    uint8_t lv = i == g_sel ? 0 : 3;
    if (i == g_sel) for (int yy = y - 1; yy < y + 8 && yy < MH; yy++) for (int x = 0; x < MW; x++) g_txt[yy][x] = 3;
    if (!g_page) {
      const char *label = ""; char val[17];
      item_text(g_items[i], &label, val);
      txt(2, y, label, lv);
      txt(MW - 2 - 6 * slen(val), y, val, lv);
    } else {
      char nm[17];
      if (i == 0) { const char *a = tr("Keine", "None"); int c = 0; while (a[c]) { nm[c] = a[c]; c++; } nm[c] = 0; }
      else { for (int c = 0; c < 16; c++) nm[c] = g_cfg->fig[i - 1].name[c]; nm[16] = 0; }
      txt(2, y, (i - 1 == g_fig) ? "*" : " ", lv);
      txt(10, y, nm, lv);
    }
  }
  /* scroll marks */
  for (int k = 0; k < 4; k++) for (int x = -k; x <= k; x++) {
    if (g_top > 0) g_txt[2 + k][MW - 14 + x] = 2;            /* more above */
    if (g_top + ROWS < n) g_txt[7 - k][MW - 5 + x] = 2;      /* more below */
  }
  g_full = 2;
}
int app_menu_open(void) { return g_menu; }

static void fig_insert(int i)
{
  if (i < 0) { figure_rom_set_active(0, 0); t7_figure_set(0); g_fig = -1; return; }
  figure_rom_set_active(plat_fig_ptr(i), g_cfg->fig[i].len);
  t7_figure_set(1);
  g_fig = i;
}
static void fig_choose(int i, uint32_t now)
{
  if (i == g_fig) return;
  /* like on the real toy: take the old figure off, wait, put the new one on */
  fig_insert(-1);
  if (i >= 0) { g_fig_next = i; g_fig_at = now + 2500u; }
}
void app_tick(uint32_t now)
{
  if (g_fig_next >= 0 && (int32_t)(now - g_fig_at) >= 0) { fig_insert(g_fig_next); g_fig_next = -2; }
}

static int g_dirty_set;
static void change(int it)
{
  switch (it) {
  case M_BG: g_set.bg = (uint8_t)((g_set.bg + 1) % BG_N); break;
  case M_INK: g_set.ink = (uint8_t)((g_set.ink + 1) % (BG_N + 1)); break;
  case M_G1: g_set.g1 = (uint8_t)(g_set.g1 >= 100 ? 0 : (g_set.g1 / 5) * 5 + 5); break;
  case M_G2: g_set.g2 = (uint8_t)(g_set.g2 >= 100 ? 0 : (g_set.g2 / 5) * 5 + 5); break;
  case M_SIZE: g_set.size ^= 1; break;
  case M_PIX: g_set.pixel ^= 1; break;
  case M_GRID: g_set.grid = (uint8_t)(g_set.grid >= 100 ? 0 : (g_set.grid / 25) * 25 + 25); break;
  case M_PVIS: g_set.pixvis = (uint8_t)(g_set.pixvis >= 10 ? 0 : g_set.pixvis + 1); break;
  case M_GHOST: g_set.ghost = (uint8_t)(g_set.ghost >= 10 ? 0 : g_set.ghost + 1); break;
  case M_SND: g_set.sound ^= 1; break;
  case M_LIGHT:
    g_set.light = (uint8_t)((g_set.light + 1) % (T7_T_N + 1));
    if (g_set.light < T7_T_N && g_set.r2 && g_set.r2 - 1 <= g_set.light) g_set.r2 = (uint8_t)(g_set.light + 2 <= T7_T_N ? g_set.light + 2 : 0);   /* off stays after dimming */
    menu_items();
    break;
#ifdef BL_PWM
  case M_BL: g_set.r1 = (uint8_t)((g_set.r1 & 0x0F) | (((((g_set.r1 >> 4) & 15) + 1) % T7_BL_N) << 4)); hw_bl_set((unsigned int)app_bl_pct()); break;
  case M_DIM: g_set.r0 = (uint8_t)((g_set.r0 + 1) % T7_DIM_N); break;
#else
  case M_DIM: g_set.r0 = (uint8_t)((g_set.r0 + 1) % 5); break;
#endif
  case M_OFF:
    do g_set.r2 = (uint8_t)((g_set.r2 + 1) % (T7_T_N + 1)); while (g_set.r2 && g_set.light < T7_T_N && g_set.r2 - 1 <= g_set.light);
    break;
  case M_SYNC: g_set.sync ^= 1; break;
  case M_LANG: g_set.lang = (uint8_t)(g_lang ^ 1); break;
#ifdef T7_CARE
  case M_GP: { uint32_t v = gp_get(); if (hw_num_edit("GOTCHI POINTS", &v, 6, 999999u, g_lang)) gp_set(v); } break;
  case M_CARE: t7_ram_write(0x326, 5); t7_ram_write(0x327, 5); g_cared = 1; break;
#endif
  }
  g_dirty_set = 1;
  palette();
  /* the screen around the menu in the new colours */
  fill(0, 0, SCR, MY, g_pal[0]); fill(0, MY + MH, SCR, SCR, g_pal[0]);
  for (int i = 0; i < T7_ICON_COUNT; i++) g_icp[i] = 0xFF;
}
static void menu_close(void)
{
  g_menu = 0; g_full = 1;
#ifdef T7_CARE
  g_cared = 0;
#endif
  if (g_dirty_set) { app_store(); g_dirty_set = 0; }
}
int app_keys(uint32_t b, uint32_t now)
{
  static uint32_t last;
  uint32_t press = b & ~last; last = b;
  if (!g_menu) return (int)(b & 7u);
  int n = g_page ? (int)g_cfg->nfig + 1 : g_nitems;
  if (press & 1u) { g_sel = (g_sel + 1) % n; menu_build(); }
  if (press & 2u) {
    if (g_page) { fig_choose(g_sel - 1, now); g_page = 0; g_sel = 0; g_top = 0; menu_build(); }
    else if (g_items[g_sel] == M_FIG) { g_page = 1; g_sel = g_fig + 1; g_top = 0; menu_build(); }
    else { int k = g_items[g_sel]; change(k); menu_build();
#ifdef T7_CARE
      if (k == M_GP) g_full = 1;   /* the number screen covered everything */
#endif
    }
  }
  if (press & 4u) {
    if (g_page) { g_page = 0; g_sel = 0; g_top = 0; menu_build(); }
    else menu_close();
  }
  return 0;
}
/* dimmed after the light time (screen left on) or normal again */
void app_dim(int pct)
{
#ifdef BL_PWM
  hw_bl_set(pct ? (unsigned int)pct : (unsigned int)app_bl_pct());   /* 1.6.8: the light, as the Paradise does it, the colours stay */
  return;
#endif
  if (pct == g_dimp) return;
  g_dimp = pct; palette();
  for (int i = 0; i < T7_ICON_COUNT; i++) g_icp[i] = 0xFF;
  g_full = 1; app_render(1);
}
/* wheel, short press: menu open/close */
void app_wheel_short(void)
{
  if (g_menu) { menu_close(); return; }
#ifdef BAT_SHOW
  g_menu = 1; menu_items(); g_page = 0; g_sel = 1; g_top = 0; menu_build();   /* the battery line is only shown */
#else
  g_menu = 1; menu_items(); g_page = 0; g_sel = 0; g_top = 0; menu_build();
#endif
}

/* ---------------------------------------------------------------- stored settings and clock note
 * Sector layout: "T7S2", the settings (16 bytes), then at 32 "T7CK", seconds counter, game
 * second of the day, month and day then. Older layout "T7ST": bg ink pixel ghost sound light. */
#define CK_MAGIC 0x4B433754u    /* "T7CK" */
static uint32_t g_ck_cnt, g_ck_sod, g_ck_md, g_ck_have;
static void w32(uint8_t *b, uint32_t v) { b[0] = (uint8_t)v; b[1] = (uint8_t)(v >> 8); b[2] = (uint8_t)(v >> 16); b[3] = (uint8_t)(v >> 24); }
static uint32_t r32(const uint8_t *b) { return b[0] | (b[1] << 8) | (b[2] << 16) | ((uint32_t)b[3] << 24); }
static void store_load(void)
{
  uint8_t b[T7_STORE_LEN];
  g_set = SET_DEF;
  if (!plat_store_load(b, sizeof b)) return;
  if (b[0] == 'T' && b[1] == '7' && b[2] == 'S' && b[3] == '2') {
    uint8_t *d = (uint8_t *)&g_set; for (unsigned i = 0; i < sizeof g_set; i++) d[i] = b[4 + i];
  } else if (b[0] == 'T' && b[1] == '7' && b[2] == 'S' && b[3] == 'T') {
    g_set.bg = b[4]; g_set.ink = b[5]; g_set.pixel = b[6] & 1; g_set.sound = b[8] & 1; g_set.light = b[9];
    g_set.r0 = 0; g_set.r1 = 0;
  }
  if ((g_set.r1 & 0x0F) != 2 && (g_set.r1 & 0x0F) != 3) {   /* older layouts: what was "light off after" stays */
    uint8_t off;                                     /* index 0..6 (10 s .. 10 min) or 7 never */
    if ((g_set.r1 & 0x0F) == 1) { static const uint8_t m[7] = { 0, 1, 2, 3, 4, 5, 7 }; off = m[g_set.light % 7]; }   /* test builds 1.6.6 */
    else { static const uint8_t m[5] = { 2, 3, 4, 5, 7 }; off = m[g_set.light % 5]; }                                 /* 30 s, 1, 2, 5 min, never */
    g_set.light = T7_T_N; g_set.r0 = 1; g_set.r2 = off >= T7_T_N ? 0 : (uint8_t)(off + 1); g_set.r1 = 2;
  }
#ifdef BL_PWM
  if ((g_set.r1 & 0x0F) == 2) {                     /* layout 2: dim 1, 5, 10, 25, 50 %, the light always full */
    static const uint8_t m[5] = { 0, 3, 4, 6, 9 }; g_set.r0 = m[g_set.r0 % 5]; g_set.r1 = 3;
  }
  hw_bl_set((unsigned int)app_bl_pct());
#endif
  if (r32(b + 32) == CK_MAGIC) { g_ck_cnt = r32(b + 36); g_ck_sod = r32(b + 40); g_ck_md = r32(b + 44); g_ck_have = 1; }
}
static int tama_sod(void);
static int g_ck_ok(void);
void app_store(void)
{
  uint8_t b[T7_STORE_LEN];
  for (unsigned i = 0; i < sizeof b; i++) b[i] = 0xFF;
  b[0] = 'T'; b[1] = '7'; b[2] = 'S'; b[3] = '2';
  const uint8_t *d = (const uint8_t *)&g_set; for (unsigned i = 0; i < sizeof g_set; i++) b[4 + i] = d[i];
  int obs = tama_sod();
  if (g_ck_ok() && obs >= 0) { g_ck_cnt = plat_rtc(); g_ck_sod = (uint32_t)obs; g_ck_md = (uint32_t)(t7_ram_read(0x0304) << 8 | t7_ram_read(0x0305)); g_ck_have = 1; }
  if (g_ck_have) { w32(b + 32, CK_MAGIC); w32(b + 36, g_ck_cnt); w32(b + 40, g_ck_sod); w32(b + 44, g_ck_md); }
  plat_store_write(b, sizeof b);
}

void app_init(const volatile T7Cfg *cfg)
{
  g_cfg = cfg;
  store_load();
  palette();
  g_full = 1;
}

/* one game row into the display rows of its cell */
static void draw_row(int r, const uint8_t *d)
{
  int p = g_set.pixel;
#ifdef T7_PIXSOFT
  /* 1.6.9 test13: pixel mode. A cell is only 2 or 3 display pixels: a gap in the grid colour left a dot of 1 pixel
   * and the picture was hard to see. Now every cell has its gap (right column, bottom row), drawn as the cell's own
   * colour moved a fifth towards the grid colour: the pixels stay full, a fine grid shows between them. */
  if (p) {
    uint16_t on[4], gap[4];
    for (int v = 0; v < 4; v++) { on[v] = v ? g_pal[v] : g_off; gap[v] = mix(on[v], g_grid, 80); }
    for (int y = g_ry[r]; y < g_ry[r + 1]; y++) {
      int hl = y == g_ry[r + 1] - 1;
      for (int x = 0; x < GW; x++) { int v = d[g_cx[x]] & 3; g_row[x] = (hl || g_gx[x]) ? gap[v] : on[v]; }
      plat_row(g_row, GW);
    }
    return;
  }
#endif
  for (int y = g_ry[r]; y < g_ry[r + 1]; y++) {
    if (p && y == g_ry[r + 1] - 1 && (!g_set.size || g_ry[r + 1] - g_ry[r] >= 3)) { for (int x = 0; x < GW; x++) g_row[x] = g_grid; }
    else for (int x = 0; x < GW; x++) {
      int v = d[g_cx[x]] & 3;
      g_row[x] = (p && g_gx[x]) ? g_grid : (p && !v) ? g_off : g_pal[v];
    }
    plat_row(g_row, GW);
  }
}
void app_render(int force)
{
  if (force) g_full = 1;
  if (g_full == 1) {
    fill(0, 0, SCR, SCR, g_pal[0]);
    for (int i = 0; i < T7_ICON_COUNT; i++) g_icp[i] = 0xFF;
    for (int i = 0; i < LCDW * LCDH; i++) g_prev[i] = 0xFF;
  }
  if (g_menu) {
    if (g_full) {
      plat_rect(MX, MY, MX + MW, MY + MH);
      for (int y = 0; y < MH; y++) { for (int x = 0; x < MW; x++) g_row[x] = g_mpal[g_txt[y][x]]; plat_row(g_row, MW); }
      plat_rect_end();
      for (int i = 0; i < LCDW * LCDH; i++) g_prev[i] = 0xFF;   /* game screen again afterwards */
    }
  } else {
    t7_get_display(g_disp);
    int y = 0;
    while (y < LCDH) {
      int ch = 0;
      for (int x = 0; x < LCDW; x++) if (g_disp[y * LCDW + x] != g_prev[y * LCDW + x]) { ch = 1; break; }
      if (!ch) { y++; continue; }
      int y1 = y;
      while (y1 < LCDH) {
        int c2 = 0;
        for (int x = 0; x < LCDW; x++) if (g_disp[y1 * LCDW + x] != g_prev[y1 * LCDW + x]) { c2 = 1; break; }
        if (!c2) break;
        y1++;
      }
      plat_rect(GX, GY + g_ry[y], GX + GW, GY + g_ry[y1]);
      for (int r = y; r < y1; r++) {
        draw_row(r, &g_disp[r * LCDW]);
        for (int x = 0; x < LCDW; x++) g_prev[r * LCDW + x] = g_disp[r * LCDW + x];
      }
      plat_rect_end();
      y = y1;
    }
  }
  t7_get_icons(g_ic);
  for (int i = 0; i < T7_ICON_COUNT; i++) {
    int on = g_ic[i] != 0;
    if (on != g_icp[i]) { draw_icon(i, on); g_icp[i] = (uint8_t)on; }
  }
  g_full = 0;
}

/* the bell icon (the pet calls) */
int app_bell(void)
{
  uint8_t ic[T7_ICON_COUNT];
  t7_get_icons(ic);
  return ic[9] != 0;
}
/* Full screen message (start up, catching up the time) with a bar */
void app_text_screen(const char *l1, const char *l2, int done, int total)
{
  for (int y = 0; y < MH; y++) for (int x = 0; x < MW; x++) g_txt[y][x] = 0;
  if (l1) txt(4, 20, l1, 3);
  if (l2) txt(4, 32, l2, 3);
  if (total > 0) {
    int w = (MW - 16) * done / total;
    for (int y = 50; y < 58; y++) for (int x = 8; x < MW - 8; x++) g_txt[y][x] = (x - 8 < w) ? 3 : ((y == 50 || y == 57 || x == 8 || x == MW - 9) ? 2 : 0);
  }
  plat_rect(MX, MY, MX + MW, MY + MH);
  for (int y = 0; y < MH; y++) { for (int x = 0; x < MW; x++) g_row[x] = g_mpal[g_txt[y][x]]; plat_row(g_row, MW); }
  plat_rect_end();
  for (int i = 0; i < LCDW * LCDH; i++) g_prev[i] = 0xFF;
  g_full = 1;
}

/* ---------------------------------------------------------------- clock
 * The V7 keeps its time in work RAM (binary): hour 0x306, minute 0x307, second 0x396 (Friends 0x136),
 * day 0x305, month 0x304. The hour may only change through the game itself (it ages the
 * pet and runs its day events), so behind by an hour or more we put the clock just
 * before the full hour and let the game count on. Within the hour, minute and second
 * are simply set. Reference: the Paradise's seconds counter. */
#define RTC_H 0x0306
#define RTC_M 0x0307
#define RTC_S t7_rtc_sec_addr()   /* 0x396, Friends 0x136 */
#define RTC_D 0x0305
#define RTC_MO 0x0304
static uint32_t g_ref_cnt, g_ref_sod, g_have_ref, g_last_obs, g_last_cnt;
static int g_ck_ok(void) { return (int)g_have_ref; }
static int tama_sod(void)
{
  int h = t7_ram_read(RTC_H), m = t7_ram_read(RTC_M), s = t7_ram_read(RTC_S);
  if (h > 23 || m > 59 || s > 59) return -1;
  return h * 3600 + m * 60 + s;
}
static int32_t wrapdiff(int32_t a, int32_t b)    /* a - b on the 24 h circle, -43200..43199 */
{
  int32_t d = (a - b) % 86400; if (d < -43200) d += 86400; if (d >= 43200) d -= 86400; return d;
}
static uint32_t expected(uint32_t rtc) { return (g_ref_sod + (rtc - g_ref_cnt)) % 86400u; }
static void set_ms(int sod) { t7_ram_write(RTC_M, (uint8_t)((sod / 60) % 60)); t7_ram_write(RTC_S, (uint8_t)(sod % 60)); }

void app_clock(uint32_t rtc, int guard)
{
  int obs = tama_sod();
#ifdef T7_GAME_KEEP
  /* test27: with "Clock: Game" too the game's clock is kept on the Paradise's seconds counter while it runs
   * (only its own time, nothing of the Paradise's) */
  if (guard || obs < 0) { g_have_ref = 0; return; }
#else
  if (guard || obs < 0 || !g_set.sync) { g_have_ref = 0; return; }
#endif
  /* the clock stands (the game asks for the time, or it is being set): leave it alone */
  if (g_have_ref && (uint32_t)obs == g_last_obs && rtc != g_last_cnt) { g_have_ref = 0; g_last_cnt = rtc; return; }
  if (!g_have_ref) { g_ref_cnt = rtc; g_ref_sod = (uint32_t)obs; g_have_ref = 1; g_last_obs = (uint32_t)obs; g_last_cnt = rtc; return; }
  /* clock set in the game: new reference */
  int32_t jump = wrapdiff(obs, (int32_t)((g_last_obs + (rtc - g_last_cnt)) % 86400u));
  if (jump > 30 || jump < -30) { g_ref_cnt = rtc; g_ref_sod = (uint32_t)obs; g_last_obs = (uint32_t)obs; g_last_cnt = rtc; return; }
  int exp = (int)expected(rtc);
  int32_t d = wrapdiff(exp, obs);                 /* > 0: the game is behind */
  if (d >= 2 || d <= -2) {
    if (exp / 3600 == obs / 3600) set_ms(exp);
    else if (d > 0) { t7_ram_write(RTC_M, 59); t7_ram_write(RTC_S, 59); }
  }
  int now = tama_sod();
  g_last_obs = (uint32_t)(now < 0 ? obs : now); g_last_cnt = rtc;
}

/* After a pause: one full hour after the other through the game (at most 30, like the
 * phone app), the rest of the time is set directly. Returns the hours run. */
int app_catchup(uint32_t rtc, void (*progress)(int, int))
{
  int obs = tama_sod();
  if (!g_have_ref || obs < 0 || !g_set.sync) return 0;
  int32_t ahead = (int32_t)(rtc - g_last_cnt);    /* seconds since the save */
  if (ahead < 0) { g_ref_cnt = rtc; g_ref_sod = (uint32_t)obs; g_last_obs = (uint32_t)obs; g_last_cnt = rtc; return 0; }   /* counter went back: keep the game time */
  int hours = (int)((obs % 3600 + ahead) / 3600);
  int n = hours > 30 ? 30 : hours;
  for (int k = 0; k < n; k++) {
    if (progress) progress(k, n);
    int h0 = t7_ram_read(RTC_H);
    t7_ram_write(RTC_M, 59); t7_ram_write(RTC_S, 59);
    for (int f = 0; f < 300 && t7_ram_read(RTC_H) == h0; f++) t7_step_cycles(40000);     /* up to 3 s game time */
    for (int f = 0; f < 50; f++) t7_step_cycles(40000);                                  /* let the day events run */
  }
  int exp = (int)expected(rtc);
  if (hours > n) t7_ram_write(RTC_H, (uint8_t)(exp / 3600));   /* beyond 30 hours: skip */
  set_ms(exp);
  int now = tama_sod();
  g_last_obs = (uint32_t)(now < 0 ? exp : now); g_last_cnt = rtc;
  if (progress) progress(n, n);
  return n;
}

/* The game asks for the time (hour 255 while setting it up, after a fresh start). It only
 * takes the time once the hour was changed, so midnight could not be entered. Filled in:
 * the game time noted on leaving (counted on), else the time the Studio wrote along, else
 * just hour 0. Once per request, the player can still change everything. */
static const uint8_t MDAYS[13] = { 0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
static void add_days(uint32_t *y, uint32_t *m, uint32_t *d, uint32_t n)
{
  if (*m < 1 || *m > 12) *m = 1;
  if (*d < 1) *d = 1;
  while (n--) {
    uint32_t len = MDAYS[*m] + (*m == 2 && *y && (*y % 4u) == 0 && ((*y % 100u) || (*y % 400u) == 0));
    if (++*d > len) { *d = 1; if (++*m > 12) { *m = 1; if (*y) ++*y; } }
  }
}
/* The Paradise's own time at the start (seconds since 1970, 0 = not found) and its seconds counter
 * then, read by the platform before anything overwrote it. With settings flag 2 ("clock from the
 * Paradise") the game clock is set to it at every start, nothing is run in between. */
uint32_t g_para_time, g_para_cnt;
static int para_hard(void) { return (g_cfg->flags & 2u) && g_para_time && g_set.sync; }
static void civil(uint32_t days, uint32_t *y, uint32_t *m, uint32_t *d)
{
  uint32_t z = days + 719468u, era = z / 146097u, doe = z - era * 146097u;
  uint32_t yoe = (doe - doe / 1460u + doe / 36524u - doe / 146096u) / 365u, doy = doe - (365u * yoe + yoe / 4u - yoe / 100u);
  uint32_t mp = (5u * doy + 2u) / 153u;
  *d = doy - (153u * mp + 2u) / 5u + 1u; *m = mp < 10u ? mp + 3u : mp - 9u; *y = yoe + era * 400u + (*m <= 2u);
}
static uint32_t para_now(uint32_t rtc) { return g_para_time + (rtc - g_para_cnt); }
int app_clock_hard(uint32_t rtc)
{
  if (!para_hard() || t7_ram_read(RTC_H) > 23) return 0;          /* hour 255: the game asks, app_setup_fill answers */
  uint32_t now = para_now(rtc), sod = now % 86400u, y, m, d;
  civil(now / 86400u, &y, &m, &d);
  t7_ram_write(RTC_MO, (uint8_t)m); t7_ram_write(RTC_D, (uint8_t)d);
  t7_ram_write(RTC_H, (uint8_t)(sod / 3600u)); t7_ram_write(RTC_M, (uint8_t)((sod / 60u) % 60u)); t7_ram_write(RTC_S, (uint8_t)(sod % 60u));
  g_ref_cnt = rtc; g_ref_sod = sod; g_have_ref = 1; g_last_obs = sod; g_last_cnt = rtc;
  return 1;
}
static int g_fill_seen, g_fill_done;
void app_setup_fill(uint32_t rtc)
{
  int h = t7_ram_read(RTC_H);
  /* 1.6.8: once the game's own setup is through (it asked for the time, now it has one), the Paradise's time
   * replaces what was typed in (clock from the Paradise) */
  static int asked, after;
  if (h == 255) { asked = 1; after = 0; }
  else if (h <= 23 && asked && ++after >= 4) { asked = 0; after = 0; app_clock_hard(rtc); }
  if (h <= 23) { g_fill_seen = 0; g_fill_done = 0; return; }
  if (h != 255 || !g_set.sync || g_fill_done) return;
  if (++g_fill_seen < 2) return;                 /* seen twice, half a second apart */
  g_fill_done = 1;
  uint32_t sod, y = 0, m, d, have = 1, el;
  if (para_hard()) { uint32_t now = para_now(rtc); el = now % 86400u; civil(now / 86400u, &y, &m, &d); y = 0; }
  else if (g_ck_have) { el = g_ck_sod + (rtc - g_ck_cnt); m = (g_ck_md >> 8) & 0xFF; d = g_ck_md & 0xFF; }
  else if (g_cfg->wall_ymd) { el = g_cfg->wall_sod + (rtc - g_cfg->wall_cnt); y = g_cfg->wall_ymd >> 16; m = (g_cfg->wall_ymd >> 8) & 0xFF; d = g_cfg->wall_ymd & 0xFF; }
  else { have = 0; el = 0; m = 1; d = 1; }
  if (!have) { t7_ram_write(RTC_H, 0); return; }
  sod = el % 86400u;
  add_days(&y, &m, &d, el / 86400u);
  t7_ram_write(RTC_MO, (uint8_t)m); t7_ram_write(RTC_D, (uint8_t)d);
  t7_ram_write(RTC_H, (uint8_t)(sod / 3600u)); t7_ram_write(RTC_M, (uint8_t)((sod / 60u) % 60u)); t7_ram_write(RTC_S, (uint8_t)(sod % 60u));
}

/* ---------------------------------------------------------------- saved state */
#define SV_MAGIC 0x53533754u   /* "T7SS" */
#define SV_VER 1u
uint32_t app_save(uint8_t *buf, uint32_t cap)
{
  uint32_t n = t7_serialize_state(0, 0);
  if (cap < 40u + n) return 0;
  int fig = g_fig_next >= 0 ? g_fig_next : g_fig;
  w32(buf, SV_MAGIC); w32(buf + 4, SV_VER); w32(buf + 8, plat_rtc());
  w32(buf + 12, g_ref_cnt); w32(buf + 16, g_ref_sod); w32(buf + 20, g_have_ref);
  w32(buf + 24, (uint32_t)fig); w32(buf + 28, n); w32(buf + 32, g_last_cnt);
  t7_serialize_state(buf + 40, n);
  uint32_t s = 0; for (uint32_t i = 0; i < 40u + n; i++) if (i < 36 || i >= 40) s = s * 31u + buf[i];
  w32(buf + 36, s);
  return 40u + n;
}
int app_load(const uint8_t *buf, uint32_t len, uint32_t *saved_rtc)
{
  if (len < 40 || r32(buf) != SV_MAGIC || r32(buf + 4) != SV_VER) return 0;
  uint32_t n = r32(buf + 28);
  if (n != t7_serialize_state(0, 0) || 40u + n > len) return 0;
  uint32_t s = 0; for (uint32_t i = 0; i < 40u + n; i++) if (i < 36 || i >= 40) s = s * 31u + buf[i];
  if (s != r32(buf + 36)) return 0;
  if (!t7_deserialize_state(buf + 40, n)) return 0;
  *saved_rtc = r32(buf + 8);
  g_ref_cnt = r32(buf + 12); g_ref_sod = r32(buf + 16); g_have_ref = r32(buf + 20);
  g_last_cnt = r32(buf + 8);
  { int o = tama_sod(); g_last_obs = (uint32_t)(o < 0 ? 0 : o); }
  int fig = (int32_t)r32(buf + 24);
  if (fig >= 0 && fig < (int)g_cfg->nfig) fig_insert(fig); else fig_insert(-1);
  g_full = 1;
  return 1;
}
