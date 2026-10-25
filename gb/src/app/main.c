/* GBEmu: the Game Boy (DMG) on the Tamagotchi Paradise. Core: Peanut-GB by Mahyar Koshkouei (MIT), see ../core.
 * Started from the Paramix menu like the other further programs. Bring your own ROM (.gb).
 * Screen: 160 x 144 shrunk to 128 x 115 (5 dots to 4, mixed, nothing cut off), black bars above and below.
 * Keys: set in the Studio (tab Game Boy), stored in the settings block. A+B+C together: settings (also SELECT and
 * sound); wheel held 2 s: save and back to the Paradise. Sound (GB_SOUND): the Paradise's buzzer plays one channel.
 * Save: the cartridge RAM (battery save) goes to the flash a few seconds after the game wrote it, and on leaving.
 * Author: mameise */
#include "../../../emu/src/emu/hw.h"
#include "../../../emu/src/emu/flash.h"
#define PARA_ATTR
#include "../../../emu/src/emu/paraclock.h"
#ifdef PARA_BACK
#include "../../../emu/src/emu/paraback.h"
#endif
typedef int s32;

/* ---- settings block at 0x140, filled in by the Studio ----
 * keys[i]: Game Boy buttons (Peanut-GB joypad bits: 1 A, 2 B, 4 SELECT, 8 START, 16 RIGHT, 32 LEFT, 64 UP, 128 DOWN)
 * for the Paradise input i: 0 A, 1 B, 2 C, 3 wheel press, 4 wheel up, 5 wheel down, 6 A+C, 7 B+C, 8 A+B.
 * flags: bit 0 English, bit 1 wheel the other way round.
 * save_off: 4 KB settings of the device, then the cartridge RAM (up to 32 KB). */
#define NIN 9
typedef struct { u32 magic, version, rom_off, rom_len, save_off, flags; char name[16]; u8 keys[12]; } GbCfg;
#define GB_MAGIC 0x47584D50u   /* "PMXG" */
__attribute__((section(".pmxcfg"), used))
volatile const GbCfg g_cfg = { GB_MAGIC, 1, 0, 0, 0, 0, {0}, { 0x20, 0x02, 0x10, 0x01, 0x40, 0x80, 0x08, 0x00, 0x00 } };
#define SETTINGS (g_cfg.save_off)
#define CRAM_AT (g_cfg.save_off + 0x1000u)
#define CRAM_MAX 0x8000u
#define XIP_BASE_REG REG(0x4002F004u)
#define XIP_CTRL_REG REG(0x4002F000u)
#define ROMWIN ((const u8 *)0x10000000u)

#define PGB_ROM ROMWIN
#define ENABLE_SOUND 0
#define PEANUT_GB_HIGH_LCD_ACCURACY 0
#ifdef GB_SOUND
static void apu_write(u32 addr, u8 val);
#define PGB_APU_WRITE(a, v) apu_write((u32)(a), (u8)(v))
#endif
#include "../core/peanut_gb.h"
#include "font.h"

/* ---- time: SysTick 1 ms; the wheel's encoder is read here too, so no step is lost while a frame runs ---- */
static volatile u32 g_ms;
static u32 g_tone_now;   /* buzzer frequency now (GB_SOUND) */
static volatile s32 g_enc_acc; static u32 g_enc_last;
static const signed char QD[16] = { 0, -1, 1, 0, 1, 0, 0, -1, -1, 0, 0, 1, 0, 1, -1, 0 };
#define GPIO2 0x4001A000u
#define SYST_CSR REG(0xE000E010u)
#define SYST_RVR REG(0xE000E014u)
#define SYST_CVR REG(0xE000E018u)
#define CORE_HZ 96000000u
__attribute__((used)) void systick_c(void)
{
  g_ms++;
  u32 n = REG(GPIO2) & 3u;
  if (n != g_enc_last) { g_enc_acc += QD[(g_enc_last << 2) | n]; g_enc_last = n; }
}
__attribute__((naked)) void SysTick_Handler(void) { __asm__ volatile("b systick_c\n"); }
void abort(void) { for (;;) hw_wdt_feed(); }

/* ---- settings on the device ---- */
typedef struct { u8 magic[4]; u8 pal, dim, lang, dimto, off, r0, skip, mute; } GbSet;   /* mute: 0 = sound on */
static GbSet g_set = { {'G','B','S','1'}, 0, 7, 0, 6, 3, 0x80, 0, 0 };
/* light to dark: DMG green, pocket grey, light (yellow), blue */
static const u16 PAL[4][4] = {
  { 0x9DE1u, 0x8D61u, 0x3324u, 0x0981u },
  { 0xE71Cu, 0xAD55u, 0x5ACBu, 0x10A2u },
  { 0xFFF4u, 0xCE4Cu, 0x7B46u, 0x2100u },
  { 0xDF5Fu, 0x8D3Cu, 0x3A92u, 0x0846u } };
static const char *const PAL_NM[2][4] = { { "GRUEN", "GRAU", "GELB", "BLAU" }, { "GREEN", "GREY", "YELLOW", "BLUE" } };
#define T_N 7
static const u32 T_MS[T_N] = { 10000u, 20000u, 30000u, 60000u, 120000u, 300000u, 600000u };
static const char *const T_NM[2][T_N + 1] = { { "10 S", "20 S", "30 S", "1 MIN", "2 MIN", "5 MIN", "10 MIN", "NIE" }, { "10 S", "20 S", "30 S", "1 MIN", "2 MIN", "5 MIN", "10 MIN", "NEVER" } };
#define LV_N 11
#define DIM2_N 10
static const u8 LV_PCT[LV_N] = { 100u, 5u, 10u, 20u, 30u, 40u, 50u, 60u, 70u, 80u, 90u };
static const u8 DIM2_PCT[DIM2_N] = { 1u, 2u, 3u, 5u, 10u, 15u, 20u, 30u, 40u, 50u };
static int en(void) { return g_set.lang ? g_set.lang == 2u : (int)(g_cfg.flags & 1u); }
static u16 BG, INK, OFF;
static u16 g_lut[76];   /* mixed brightness 0..75 (25 weights x shade 0..3) to colour */
static int g_dimmed;
static u16 mix(u16 a, u16 b, int p, int q)   /* a + (b - a) * p / q */
{
  int r = (a >> 11) + (((b >> 11) - (a >> 11)) * p) / q, g = ((a >> 5) & 63) + ((((b >> 5) & 63) - ((a >> 5) & 63)) * p) / q;
  int bl = (a & 31) + (((b & 31) - (a & 31)) * p) / q;
  return (u16)((r << 11) | (g << 5) | bl);
}
static void colours(void)
{
  const u16 *p = PAL[g_set.pal & 3u];
  for (int v = 0; v < 76; v++) { int s = v / 25, f = v % 25; g_lut[v] = s >= 3 ? p[3] : mix(p[s], p[s + 1], f, 25); }
  BG = p[0]; INK = p[3]; OFF = p[2];
#ifdef BL_PWM
  { u32 l = LV_PCT[(g_set.r0 & 15u) % LV_N], d = DIM2_PCT[g_set.dimto % DIM2_N];
    hw_bl_set(g_dimmed ? (d < l ? d : l) : l); }
#endif
}
static u8 g_sec[0x1000] __attribute__((aligned(4)));
static void set_load(void)
{
  const volatile u8 *p = XIP_PTR(SETTINGS); GbSet t; u8 *d = (u8 *)&t;
  for (u32 i = 0; i < sizeof t; i++) d[i] = p[i];
  if (t.magic[0] == 'G' && t.magic[1] == 'B' && t.magic[2] == 'S' && t.magic[3] == '1' && t.pal < 4u && t.dim <= T_N && t.off <= T_N && t.lang < 3u) g_set = t;
  colours();
}
static void set_store(void)
{
  for (u32 i = 0; i < sizeof g_sec; i++) g_sec[i] = 0xFF;
  const u8 *s = (const u8 *)&g_set; for (u32 i = 0; i < sizeof g_set; i++) g_sec[i] = s[i];
  flash_update4k(SETTINGS, g_sec);
}

/* ---- the Game Boy ---- */
static struct gb_s g_gb;
static u8 g_cram[CRAM_MAX] __attribute__((aligned(4)));
static u32 g_cram_len; static int g_cram_dirty;
static u8 gb_rom_rd(struct gb_s *gb, const uint_fast32_t a) { (void)gb; return ROMWIN[a]; }
static u8 gb_cram_rd(struct gb_s *gb, const uint_fast32_t a) { (void)gb; return a < CRAM_MAX ? g_cram[a] : 0xFF; }
static void gb_cram_wr(struct gb_s *gb, const uint_fast32_t a, const u8 v) { (void)gb; if (a < CRAM_MAX && g_cram[a] != v) { g_cram[a] = v; g_cram_dirty = 1; } }
static volatile u32 g_err;
static void gb_err(struct gb_s *gb, const enum gb_error_e e, const uint16_t a) { (void)gb; g_err = 0x80000000u | (u32)e << 16 | a; }
static void cram_load(void)
{
  const volatile u8 *p = XIP_PTR(CRAM_AT);
  for (u32 i = 0; i < g_cram_len; i++) g_cram[i] = p[i];
  /* a fresh save area (all 0xFF) is what a new cartridge has too */
}
static void cram_store(void)
{
  for (u32 o = 0; o < g_cram_len; o += 0x1000u) {
    u32 n = g_cram_len - o < 0x1000u ? g_cram_len - o : 0x1000u;
    for (u32 i = 0; i < 0x1000u; i++) g_sec[i] = i < n ? g_cram[o + i] : 0xFF;
    hw_tone(0); g_tone_now = 0;
    flash_update4k(CRAM_AT + o, g_sec);
  }
  g_cram_dirty = 0;
}

#ifdef GB_SOUND
/* ---- sound (GB_SOUND, test37): the Paradise has one square-wave buzzer, so of the Game Boy's four channels the
 * loudest of the two square channels plays (the wave channel when they are quiet; the noise channel never).
 * The registers are kept by the core (hram_io); triggers, length, volume envelope and the sweep of channel 1 are
 * followed here once per Game Boy frame (16.7 ms). ---- */
typedef struct { u8 on, vol, env_dir, env_per, env_t; u16 len_ms; u8 len_on; u16 freq; u8 sw_t; } ApuCh;
static ApuCh g_ch[3];
static u8 *io(u32 a) { return &g_gb.hram_io[a - 0xFF00u]; }
static void apu_write(u32 addr, u8 val)
{
  int c = addr >= 0xFF10u && addr <= 0xFF14u ? 0 : addr >= 0xFF15u && addr <= 0xFF19u ? 1 : addr >= 0xFF1Au && addr <= 0xFF1Eu ? 2 : -1;
  if (addr == 0xFF26u && !(val & 0x80u)) { g_ch[0].on = g_ch[1].on = g_ch[2].on = 0; return; }
  if (c < 0) return;
  u32 base = c == 0 ? 0xFF10u : c == 1 ? 0xFF15u : 0xFF1Au;
  ApuCh *h = &g_ch[c];
  if (addr == base + 4u) {
    h->len_on = (val & 0x40u) != 0;
    if (val & 0x80u) {   /* trigger */
      if (c < 2) { u8 e = *io(base + 2u); h->vol = e >> 4; h->env_dir = (e & 8u) != 0; h->env_per = e & 7u; h->env_t = 0; h->on = (e & 0xF8u) != 0;
        h->len_ms = (u16)((64u - (*io(base + 1u) & 63u)) * 1000u / 256u); }
      else { h->on = (*io(0xFF1Au) & 0x80u) != 0; h->len_ms = (u16)((256u - *io(0xFF1Bu)) * 1000u / 256u); }
      h->freq = (u16)(*io(base + 3u) | (val & 7u) << 8); h->sw_t = 0;
    }
  }
  if (c < 2 && addr == base + 2u && !(val & 0xF8u)) h->on = 0;   /* DAC off */
  if (c == 2 && addr == 0xFF1Au && !(val & 0x80u)) h->on = 0;
}
/* one Game Boy frame (16.74 ms) on: length, envelope (steps of 1/64 s), sweep (steps of 1/128 s); then the buzzer */
static void apu_frame(void)
{
  for (int c = 0; c < 3; c++) {
    ApuCh *h = &g_ch[c]; if (!h->on) continue;
    u32 base = c == 0 ? 0xFF10u : c == 1 ? 0xFF15u : 0xFF1Au;
    if (!h->sw_t || c) h->freq = (u16)(*io(base + 3u) | (*io(base + 4u) & 7u) << 8);   /* vibrato: the game changes the registers */
    if (h->len_on) { if (h->len_ms <= 17u) { h->on = 0; continue; } h->len_ms = (u16)(h->len_ms - 17u); }
    if (c < 2 && h->env_per) { h->env_t++; if (h->env_t >= h->env_per) { h->env_t = 0; if (h->env_dir) { if (h->vol < 15u) h->vol++; } else if (h->vol) h->vol--; } }
    if (c == 0) {   /* sweep: two 128 Hz steps per frame */
      u8 sw = *io(0xFF10u); u32 per = (sw >> 4) & 7u, sh = sw & 7u;
      if (per && sh) for (int k = 0; k < 2; k++) { if (++h->sw_t >= per) { h->sw_t = 1; u32 d = h->freq >> sh, f = (sw & 8u) ? h->freq - d : h->freq + d; if (f > 2047u) { h->on = 0; break; } h->freq = (u16)f; } }
    }
  }
  u32 hz = 0;
  if (!g_set.mute && (*io(0xFF26u) & 0x80u)) {
    u8 pan = *io(0xFF25u); int best = -1; u32 bv = 0;
    for (int c = 0; c < 3; c++) {
      ApuCh *h = &g_ch[c]; if (!h->on || !(pan & (0x11u << c))) continue;
      u32 v = c < 2 ? h->vol : ((*io(0xFF1Cu) >> 5) & 3u) == 1u ? 15u : ((*io(0xFF1Cu) >> 5) & 3u) == 2u ? 7u : ((*io(0xFF1Cu) >> 5) & 3u) == 3u ? 3u : 0u;
      if (c == 2 && best >= 0) continue;   /* the wave channel (mostly the bass) only when both square channels are quiet */
      if (v > bv) { bv = v; best = c; }
    }
    if (best >= 0) { u32 x = g_ch[best].freq & 2047u; hz = best < 2 ? 131072u / (2048u - x) : 65536u / (2048u - x); if (hz < 40u || hz > 10000u) hz = 0; }
  }
#ifdef GB_TONE_RESTART
  /* test39: on the device a new pitch written into the running timer left the buzzer silent (the first notes with
   * silence between them played, later notes did not). Now every new pitch starts the timer anew; changes below
   * about 1.5 % (vibrato) are left out so it does not restart every frame. */
  if (hz && g_tone_now) { u32 d = hz > g_tone_now ? hz - g_tone_now : g_tone_now - hz; if (d * 64u < g_tone_now) return; }
  if (hz != g_tone_now) { if (hz && g_tone_now) hw_tone(0); g_tone_now = hz; hw_tone(hz); }
#else
  if (hz != g_tone_now) { g_tone_now = hz; hw_tone(hz); }
#endif
}
static void apu_quiet(void) { if (g_tone_now) { g_tone_now = 0; hw_tone(0); } }
#else
static void apu_quiet(void) {}
#endif

/* ---- screen: 5 x 5 Game Boy dots to 4 x 4 (weights 4/1, 3/2, 2/3, 1/4), 128 x 115 from y 6 ---- */
#define OY 6
#define OH 115
static u8 g_fb[OH][128] __attribute__((aligned(4)));
static int g_frame_drawn, g_dirty_lo = OH, g_dirty_hi = -1;
#ifdef GB_FAST
/* test38: the same weights four dots at a time (each byte of a word is one dot, sums stay below 256) */
static u32 g_hlw[32], g_accw[2][32];
static u32 g_h5[1024];   /* five dots (2 bits each) to their four shrunk dots */
static void h5_init(void)
{
  for (u32 i = 0; i < 1024u; i++) {
    u32 a = i & 3u, b = (i >> 2) & 3u, c = (i >> 4) & 3u, d = (i >> 6) & 3u, e = i >> 8;
    g_h5[i] = (4u * a + b) | (3u * b + 2u * c) << 8 | (2u * c + 3u * d) << 16 | (d + 4u * e) << 24;
  }
}
static void row_out(int r, const u32 *a)
{
  if (r >= OH) return;
  u32 *f = (u32 *)g_fb[r], d = 0;
  for (int x = 0; x < 32; x++) { d |= f[x] ^ a[x]; f[x] = a[x]; }
  if (d) { if (r < g_dirty_lo) g_dirty_lo = r; if (r > g_dirty_hi) g_dirty_hi = r; }
}
static void lcd_line(struct gb_s *gb, const uint8_t px[160], const uint_fast8_t line)
{
  (void)gb;
  const uint8_t *s = px;
  for (int k = 0; k < 32; k++, s += 5)
    g_hlw[k] = g_h5[(s[0] & 3u) | (s[1] & 3u) << 2 | (s[2] & 3u) << 4 | (s[3] & 3u) << 6 | (s[4] & 3u) << 8];
  int g = line / 5, l = line % 5, r = g * 4;
  u32 *a0 = g_accw[0], *a1 = g_accw[1]; const u32 *h = g_hlw;
  switch (l) {
    case 0: for (int x = 0; x < 32; x++) a0[x] = h[x] << 2; break;
    case 1: for (int x = 0; x < 32; x++) { a0[x] += h[x]; a1[x] = 3u * h[x]; } row_out(r, a0); break;
    case 2: for (int x = 0; x < 32; x++) { a1[x] += 2u * h[x]; a0[x] = 2u * h[x]; } row_out(r + 1, a1); break;
    case 3: for (int x = 0; x < 32; x++) { a0[x] += 3u * h[x]; a1[x] = h[x]; } row_out(r + 2, a0); break;
    default: for (int x = 0; x < 32; x++) a1[x] += h[x] << 2; row_out(r + 3, a1); break;
  }
  if (line == 143) g_frame_drawn = 1;
}
#else
static u8 g_hl[128];          /* the current line, shrunk across (0..15) */
static u8 g_acc[2][128];      /* rows being summed */
static void lcd_line(struct gb_s *gb, const uint8_t px[160], const uint_fast8_t line)
{
  (void)gb;
  const uint8_t *s = px; u8 *h = g_hl;
  for (int k = 0; k < 32; k++, s += 5, h += 4) {
    u32 a = s[0] & 3u, b = s[1] & 3u, c = s[2] & 3u, d = s[3] & 3u, e = s[4] & 3u;
    h[0] = (u8)(4u * a + b); h[1] = (u8)(3u * b + 2u * c); h[2] = (u8)(2u * c + 3u * d); h[3] = (u8)(d + 4u * e);
  }
  int g = line / 5, l = line % 5, r = g * 4;   /* out rows r..r+3 */
  u8 *a0 = g_acc[0], *a1 = g_acc[1];
  switch (l) {
    case 0: for (int x = 0; x < 128; x++) a0[x] = (u8)(4u * g_hl[x]); break;
    case 1: for (int x = 0; x < 128; x++) { a0[x] = (u8)(a0[x] + g_hl[x]); a1[x] = (u8)(3u * g_hl[x]); } if (r < OH) { for (int x = 0; x < 128; x++) if (g_fb[r][x] != a0[x]) { g_fb[r][x] = a0[x]; if (r < g_dirty_lo) g_dirty_lo = r; if (r > g_dirty_hi) g_dirty_hi = r; } } break;
    case 2: for (int x = 0; x < 128; x++) { a1[x] = (u8)(a1[x] + 2u * g_hl[x]); a0[x] = (u8)(2u * g_hl[x]); } r += 1; if (r < OH) { for (int x = 0; x < 128; x++) if (g_fb[r][x] != a1[x]) { g_fb[r][x] = a1[x]; if (r < g_dirty_lo) g_dirty_lo = r; if (r > g_dirty_hi) g_dirty_hi = r; } } break;
    case 3: for (int x = 0; x < 128; x++) { a0[x] = (u8)(a0[x] + 3u * g_hl[x]); a1[x] = g_hl[x]; } r += 2; if (r < OH) { for (int x = 0; x < 128; x++) if (g_fb[r][x] != a0[x]) { g_fb[r][x] = a0[x]; if (r < g_dirty_lo) g_dirty_lo = r; if (r > g_dirty_hi) g_dirty_hi = r; } } break;
    default: for (int x = 0; x < 128; x++) a1[x] = (u8)(a1[x] + 4u * g_hl[x]); r += 3; if (r < OH) { for (int x = 0; x < 128; x++) if (g_fb[r][x] != a1[x]) { g_fb[r][x] = a1[x]; if (r < g_dirty_lo) g_dirty_lo = r; if (r > g_dirty_hi) g_dirty_hi = r; } } break;
  }
  if (line == 143) g_frame_drawn = 1;
}
#endif
static u16 g_row[128] __attribute__((aligned(4)));
static void push(int all)
{
  int lo = all ? 0 : g_dirty_lo, hi = all ? OH - 1 : g_dirty_hi;
  if (all) {   /* the bars */
    for (int x = 0; x < 128; x++) g_row[x] = 0;
    hw_px_begin(0, 0, 128, OY); for (int y = 0; y < OY; y++) hw_px_row(g_row, 128); hw_px_end();
    hw_px_begin(0, OY + OH, 128, 128); for (int y = OY + OH; y < 128; y++) hw_px_row(g_row, 128); hw_px_end();
  }
  if (lo > hi) return;
  hw_px_begin(0, (u32)(OY + lo), 128, (u32)(OY + hi + 1));
  for (int y = lo; y <= hi; y++) { const u8 *f = g_fb[y]; for (int x = 0; x < 128; x++) g_row[x] = g_lut[f[x]]; hw_px_row(g_row, 128); }
  hw_px_end();
  g_dirty_lo = OH; g_dirty_hi = -1;
}

/* ---- the settings menu (A+B+C): A next line, B change, C close ---- */
static int g_menu, g_msel, g_mtop;
static void text(int x, int y, const char *s, u16 col, u16 *row, int ry)
{
  for (; *s; s++, x += 6) {
    int c = (unsigned char)*s; if (c >= 97 && c <= 122) c -= 32; c = (c < 32 || c > 127) ? 0 : c - 32;
    if (ry < y || ry >= y + 7) continue;
    u8 bits = g_font[c][ry - y];
    for (int k = 0; k < 5; k++) if ((bits & (0x10 >> k)) && x + k >= 0 && x + k < 128) row[x + k] = col;
  }
}
enum { M_SEL, M_PAL,
#ifdef GB_SOUND
  M_SND,
#endif
  M_LIGHT, M_DIM, M_DIMTO, M_OFF, M_RESET, M_LANG, M_N };
static u32 g_select_until;   /* menu line SELECT: pressed for 200 ms after the menu closes */
static int g_items[M_N], g_nitems;
static void menu_items(void)
{
  g_nitems = 0;
  for (int k = 0; k < M_N; k++) { if (k == M_DIMTO && g_set.dim >= T_N) continue; g_items[g_nitems++] = k; }
  if (g_msel >= g_nitems) g_msel = g_nitems - 1;
}
static void pct(char *o, int v) { int n = 0; if (v >= 100) o[n++] = '1'; if (v >= 10) o[n++] = (char)('0' + (v / 10) % 10); o[n++] = (char)('0' + v % 10); o[n++] = '%'; o[n] = 0; }
static int g_reset_ask;
static void menu_line(int it, const char **a, char *b)
{
  int e = en(); const char *v = 0; b[0] = 0;
  switch (it) {
    case M_SEL: *a = e ? "PRESS SELECT" : "SELECT DRUECKEN"; v = "B"; break;
    case M_PAL: *a = e ? "COLOUR" : "FARBE"; v = PAL_NM[e][g_set.pal & 3u]; break;
#ifdef GB_SOUND
    case M_SND: *a = e ? "SOUND" : "TON"; v = g_set.mute ? (e ? "OFF" : "AUS") : (e ? "ON" : "AN"); break;
#endif
    case M_LIGHT: *a = e ? "LIGHT" : "LICHT"; pct(b, LV_PCT[(g_set.r0 & 15u) % LV_N]); break;
    case M_DIM: *a = e ? "DIM AFTER" : "DIMMEN NACH"; v = T_NM[e][g_set.dim <= T_N ? g_set.dim : T_N]; break;
    case M_DIMTO: *a = e ? "DIM TO" : "DIMMEN AUF"; pct(b, DIM2_PCT[g_set.dimto % DIM2_N]); break;
    case M_OFF: *a = e ? "SCREEN OFF" : "BILD AUS NACH"; v = T_NM[e][g_set.off <= T_N ? g_set.off : T_N]; break;
    case M_RESET: *a = e ? "RESTART GAME" : "SPIEL NEU"; v = g_reset_ask ? (e ? "SURE? B" : "SICHER? B") : ""; break;
    default: *a = e ? "LANGUAGE" : "SPRACHE"; v = e ? "ENGLISH" : "DEUTSCH"; break;
  }
  if (v) { int n = 0; while (v[n] && n < 11) { b[n] = v[n]; n++; } b[n] = 0; }
}
#define ROWS 9
static void menu_draw(void)
{
  if (g_msel < g_mtop) g_mtop = g_msel;
  if (g_msel >= g_mtop + ROWS) g_mtop = g_msel - ROWS + 1;
  hw_px_begin(0, 0, 128, 128);
  for (int y = 0; y < 128; y++) {
    for (int x = 0; x < 128; x++) g_row[x] = BG;
    text(4, 3, en() ? "SETTINGS" : "EINSTELLUNGEN", INK, g_row, y);
    if (y == 12) for (int x = 0; x < 128; x++) g_row[x] = OFF;
    for (int k = 0; k < ROWS && g_mtop + k < g_nitems; k++) {
      int i = g_mtop + k, ly = 16 + k * 11; const char *a; char b[12]; menu_line(g_items[i], &a, b);
      if (i == g_msel && y >= ly - 2 && y < ly + 9) for (int x = 0; x < 128; x++) g_row[x] = INK;
      u16 c = i == g_msel ? BG : INK;
      text(4, ly, a, c, g_row, y); int w = 0; while (b[w]) w++; text(124 - w * 6, ly, b, c, g_row, y);
    }
    text(4, 118, en() ? "A NEXT B SET C OK" : "A ZEILE B SETZEN C OK", OFF, g_row, y);
    hw_px_row(g_row, 128);
  }
  hw_px_end();
}
static int g_set_dirty;
static void menu_close(void) { g_menu = 0; g_reset_ask = 0; if (g_set_dirty) { set_store(); g_set_dirty = 0; } push(1); }
static void menu_key(u32 k)
{
  if (k & BTN_A) { g_msel = (g_msel + 1) % g_nitems; g_reset_ask = 0; }
  if (k & BTN_B) {
    switch (g_items[g_msel]) {
      case M_SEL: g_select_until = g_ms + 200u; menu_close(); return;
      case M_PAL: g_set.pal = (u8)((g_set.pal + 1u) & 3u); break;
#ifdef GB_SOUND
      case M_SND: g_set.mute ^= 1u; break;
#endif
      case M_LIGHT: g_set.r0 = (u8)(0x80u | (((g_set.r0 & 15u) + 1u) % LV_N)); break;
      case M_DIM: g_set.dim = (u8)((g_set.dim + 1u) % (T_N + 1u)); if (g_set.dim < T_N && g_set.off < T_N && g_set.off <= g_set.dim) g_set.off = (u8)(g_set.dim + 1u); break;
      case M_DIMTO: g_set.dimto = (u8)((g_set.dimto + 1u) % DIM2_N); break;
      case M_OFF: do g_set.off = (u8)((g_set.off + 1u) % (T_N + 1u)); while (g_set.off < T_N && g_set.dim < T_N && g_set.off <= g_set.dim); break;
      case M_RESET: if (g_reset_ask) { g_reset_ask = 0; gb_reset(&g_gb); menu_close(); return; } g_reset_ask = 1; break;
      default: g_set.lang = (u8)(en() ? 1u : 2u); break;
    }
    if (g_items[g_msel] != M_RESET && g_items[g_msel] != M_SEL) g_set_dirty = 1;
    colours(); menu_items();
  }
  if (k & BTN_C) { menu_close(); return; }
  menu_draw();
}
static void message(const char *a, const char *b)
{
  hw_px_begin(0, 0, 128, 128);
  for (int y = 0; y < 128; y++) { for (int x = 0; x < 128; x++) g_row[x] = BG; text(4, 50, a, INK, g_row, y); text(4, 62, b, INK, g_row, y); hw_px_row(g_row, 128); }
  hw_px_end();
}

/* ---- keys: the Paradise inputs to the Game Boy's buttons (map from the Studio) ---- */
static u32 g_wheel_until[2];   /* wheel up / down held until (ms) */
static u32 gb_keys(u32 b, u32 now)
{
  u32 in = 0;
  int ac = (b & (BTN_A | BTN_C)) == (BTN_A | BTN_C), bc = (b & (BTN_B | BTN_C)) == (BTN_B | BTN_C), ab = (b & (BTN_A | BTN_B)) == (BTN_A | BTN_B);
  /* a pair held: its two keys count as the pair only (if the pair has a meaning) */
  u32 used = 0;
  if (ac && g_cfg.keys[6]) { in |= 1u << 6; used |= BTN_A | BTN_C; }
  if (bc && g_cfg.keys[7]) { in |= 1u << 7; used |= BTN_B | BTN_C; }
  if (ab && g_cfg.keys[8]) { in |= 1u << 8; used |= BTN_A | BTN_B; }
  u32 r = b & ~used;
  if (r & BTN_A) in |= 1u; if (r & BTN_B) in |= 2u; if (r & BTN_C) in |= 4u; if (b & BTN_WHEEL) in |= 8u;
  if ((s32)(g_wheel_until[0] - now) > 0) in |= 16u;
  if ((s32)(g_wheel_until[1] - now) > 0) in |= 32u;
  u32 gb = 0;
  for (int i = 0; i < NIN; i++) if (in & (1u << i)) gb |= g_cfg.keys[i];
  if ((s32)(g_select_until - now) > 0) gb |= 4u;
  return gb;
}

#ifdef GB_LINK
/* ---- 1.6.11 test2: link cable between two Paradise (plink over the contacts), e.g. trading in Pokemon ---- */
#include "../../../emu/src/emu/plink.h"
static void gl_send2(u8 k, u8 b) { u8 d[2] = { k, b }; plink_send('G', d, 2); }
static void gl_poll2(void);
#define GL_SEND(k, b) gl_send2((k), (b))
#define GL_POLL() gl_poll2()
#define GL_NOW() g_ms
#define GL_PARTNER() (g_pl.last_rx && g_ms - g_pl.last_rx < 2500u)
#include "gb_link.h"
static void gl_got2(u8 ty, const u8 *d, int n) { if (ty == 'G' && n == 2) gl_got(d[0], d[1]); }
static void gl_poll2(void) { plink_poll(g_ms, 0, gl_got2); }
#endif

#ifdef GB_STATE
/* ---- 1.6.11 test2: the whole Game Boy on leaving (wheel held), read once at the next start ---- */
#include "../../../emu/src/emu/pstate.h"
#define STATE_AT (g_cfg.save_off + 0x9000u)
#define STATE_MAX 0x7000u
static int g_running;
static u32 st_tag(void) { return 0x47420001u ^ (u32)&g_gb ^ ((u32)sizeof(struct gb_s) << 8) ^ g_cfg.rom_len ^ (u32)lcd_line; }
static int st_parts(StPart *pt)
{
  int n = 0; pt[n].p = &g_gb; pt[n++].n = sizeof g_gb;
#ifdef GB_SOUND
  pt[n].p = g_ch; pt[n++].n = sizeof g_ch;
#endif
  return n;
}
static void state_store(void) { StPart pt[2]; int n = st_parts(pt); if (g_running) st_store(STATE_AT, STATE_MAX, st_tag(), pt, n, g_sec); }
static int state_load(void)
{
  StPart pt[2]; int n = st_parts(pt);
  struct gb_s keep = g_gb;                         /* the pointers of this program stay */
  int ok = st_load(STATE_AT, STATE_MAX, st_tag(), pt, n);
  if (ok) {
    g_gb.gb_rom_read = keep.gb_rom_read; g_gb.gb_cart_ram_read = keep.gb_cart_ram_read; g_gb.gb_cart_ram_write = keep.gb_cart_ram_write;
    g_gb.gb_error = keep.gb_error; g_gb.gb_serial_tx = keep.gb_serial_tx; g_gb.gb_serial_rx = keep.gb_serial_rx;
    g_gb.gb_bootrom_read = keep.gb_bootrom_read; g_gb.display.lcd_draw_line = keep.display.lcd_draw_line; g_gb.direct.priv = keep.direct.priv;
  }
  st_drop(STATE_AT);
  return ok;
}
#endif

/* ---- leaving ---- */
static void leave(void)
{
  apu_quiet(); hw_tone(0);
  if (g_set_dirty) set_store();
  REG(0x20000FF8u) = 0;
  REG(0x20000FFCu) = 0x4C584D50u;        /* "PMXL": back on purpose, the autostart stays off */
#ifdef GB_STATE
  if (g_running) { message(en() ? "SAVING" : "SPEICHERN", ""); state_store(); }
#endif
  if (g_cram_dirty) cram_store();
#ifdef PARA_BACK
  para_back_fix(g_sec);
#endif
  hw_bl_pwm_off();
  for (u32 t = 0; hw_buttons() && t < 20000000u; t++) if ((t & 0xFFFFu) == 0) hw_wdt_feed();
  XIP_BASE_REG = 0x60011000u; XIP_CTRL_REG = 3u;
  hw_back_to_paradise();
}

__attribute__((used)) volatile u32 g_gbst[4];   /* frames run, frames drawn, frames dropped, last frame in ms (for the tests) */
extern u32 __bss_start, __bss_end;
void Reset_Handler(void)
{
  __asm__ volatile("cpsid i");
  u32 cnt = REG(0x45000304u), para = para_clock(cnt, 0, 0x1F000u, 0, 0);
  if (!para) para = para_mailbox(cnt);
#ifdef PARA_FLASH
  if (!para) para = para_flash(cnt);   /* 1.6.9 test37 */
#endif
#ifdef PARA_BACK
  para_leave_mark(cnt, para);
#endif
  __asm__ volatile("" ::: "memory");
  for (u32 *p = &__bss_start; p < &__bss_end; p++) *p = 0;
  { extern u32 __data_start, __data_end, __data_load;
    const u32 *src = &__data_load; for (u32 *p = &__data_start; p < &__data_end; p++) *p = *src++; }
  (void)para;
  hw_init();
  hw_tone(0);
  XIP_BASE_REG = 0x60000000u | g_cfg.rom_off;     /* the ROM in the cached window from 0x10000000 */
  XIP_CTRL_REG = 3u;
  REG(GPIO2 + 0x04) &= ~3u; g_enc_last = REG(GPIO2) & 3u;
  set_load();
  enum gb_init_error_e ie = gb_init(&g_gb, gb_rom_rd, gb_cram_rd, gb_cram_wr, gb_err, 0);
  if (ie != GB_INIT_NO_ERROR) {
    message(en() ? "ROM NOT USABLE" : "ROM GEHT NICHT", ie == GB_INIT_CARTRIDGE_UNSUPPORTED ? (en() ? "CARTRIDGE TYPE" : "MODUL-TYP") : (en() ? "CHECKSUM" : "PRUEFSUMME"));
    for (u32 t = 0; t < 30000000u; t++) if ((t & 0xFFFFu) == 0) hw_wdt_feed();
    leave();
  }
  { size_t n = 0; if (gb_get_save_size_s(&g_gb, &n) != 0) n = 0; g_cram_len = n > CRAM_MAX ? CRAM_MAX : (u32)n; }
  cram_load();
#ifdef GB_FAST
  h5_init();
#endif
  gb_init_lcd(&g_gb, lcd_line);
#ifdef GB_LINK
  plink_init(); gb_init_serial(&g_gb, gl_tx, gl_rx);
#endif
#ifdef GB_STATE
  state_load(); g_running = 1;
#endif
  push(1);
  SYST_RVR = CORE_HZ / 1000u - 1u; SYST_CVR = 0; SYST_CSR = 7;
  __asm__ volatile("cpsie i");

  u32 last_in = g_ms, wheel_ms = 0, cram_ms = 0, next_ms = g_ms, last_push = g_ms, mk0 = 0;
  u32 frac = 0;
#ifdef GB_SKIP_ADAPT
  int skip_run = 0;
#endif
  int bl = 1, lock = 1, menu_lock = 0;
  for (;;) {
    hw_wdt_feed();
    u32 now = g_ms;
    u32 b = hw_buttons();
    if (lock) { if (b) b = 0; else lock = 0; }
    /* wheel steps: one step = 4 phases; each step holds up or down for 150 ms */
    { s32 a = g_enc_acc;
      if (a >= 4 || a <= -4) {
        __asm__ volatile("cpsid i"); g_enc_acc -= a >= 4 ? 4 : -4; __asm__ volatile("cpsie i");
        int up = (a > 0) ^ ((g_cfg.flags & 2u) != 0);
        u32 *u = &g_wheel_until[up ? 0 : 1]; *u = ((s32)(*u - now) > 0 ? *u : now) + 150u;
        last_in = now;
        if (g_menu) { g_msel = up ? (g_msel + g_nitems - 1) % g_nitems : (g_msel + 1) % g_nitems; menu_draw(); }
      } }
    if (b) {
      last_in = now;
      if (!bl || g_dimmed) {                            /* the key only wakes */
        bl = 1; hw_backlight(1); if (g_dimmed) { g_dimmed = 0; colours(); }
        if (g_menu) menu_draw(); else push(1);
        while (hw_buttons()) hw_wdt_feed();
        continue;
      }
    }
    /* wheel held 2 s: save and back */
    if (b & BTN_WHEEL) { if (!wheel_ms) wheel_ms = now ? now : 1; else if (now - wheel_ms > 2000u) leave(); } else wheel_ms = 0;
    /* A+B+C: settings */
    if ((b & (BTN_A | BTN_B | BTN_C)) == (BTN_A | BTN_B | BTN_C) && !g_menu && !menu_lock) {
      apu_quiet(); g_menu = 1; menu_lock = 1; g_msel = 1; g_mtop = 0; menu_items(); menu_draw(); mk0 = BTN_A | BTN_B | BTN_C; g_gb.direct.joypad = 0xFF;
    }
    if (!(b & (BTN_A | BTN_B | BTN_C))) menu_lock = 0;
    if (g_menu) {
      u32 k = b & (BTN_A | BTN_B | BTN_C), nk = k & ~mk0; mk0 = k; if (nk) menu_key(nk);
      next_ms = now;
      __asm__ volatile("wfi");
      continue;
    }
    if (!bl) { apu_quiet(); next_ms = now; __asm__ volatile("wfi"); continue; }   /* screen off: the game waits (battery) */
    g_gb.direct.joypad = (u8)~gb_keys(b, now);
    /* one Game Boy frame every 16.74 ms (70224 cycles at 4.194304 MHz); drawn only when there is time */
    if ((s32)(now - next_ms) >= 0) {
      if ((s32)(now - next_ms) > 100) { g_gbst[2] += (now - next_ms) / 17u; next_ms = now; }   /* too far behind: drop the time */
      int late = (s32)(now - next_ms) > 8;
      (void)late; g_gb.direct.frame_skip = 1;   /* every second frame is drawn: the display takes 30 pictures a second anyway */
      g_frame_drawn = 0;
      u32 t0 = g_ms;
      gb_run_frame(&g_gb);
      g_gbst[0]++; g_gbst[3] = g_ms - t0;
#ifdef GB_SOUND
      apu_frame();
#endif
      frac += 740u; next_ms += 16u + frac / 1000u; frac %= 1000u;   /* 16.74 ms */
#ifdef GB_SKIP_ADAPT
      /* test38: normally every second frame is drawn; when the game falls behind (many sprites), up to three in a row
       * are left out, so the game keeps its speed and only the picture gets fewer updates */
      { int behind = (s32)(g_ms - next_ms) > 0, draw = skip_run >= 3 || (skip_run >= 1 && !behind);
        skip_run = draw ? 0 : skip_run + 1; g_gb.display.frame_skip_count = draw; }
#endif
      if (g_frame_drawn) g_gbst[1]++;
      if (g_err) { message(en() ? "GAME BOY ERROR" : "GAME BOY FEHLER", ""); g_err = 0; }
      if (bl && g_frame_drawn && now - last_push >= 30u) { last_push = now; push(0); }
    }
#ifdef GB_LINK
    gl_tick();
#endif
    if (g_cram_dirty && !cram_ms) cram_ms = now ? now : 1;
    if (cram_ms && now - cram_ms > 3000u && now - last_in > 1000u) { cram_ms = 0; cram_store(); }
    if (bl && !g_dimmed && g_set.dim < T_N && now - last_in > T_MS[g_set.dim]) { g_dimmed = 1; colours(); push(1); }
    if (bl && g_set.off < T_N && now - last_in > T_MS[g_set.off]) { bl = 0; hw_backlight(0); if (g_cram_dirty) { cram_ms = 0; cram_store(); } }
    if ((s32)(next_ms - g_ms) > 0) __asm__ volatile("wfi");
  }
}

__attribute__((naked)) static void Default_Handler(void) { __asm__ volatile("1: b 1b\n"); }
__attribute__((naked)) static void Irq_Off(void) { __asm__ volatile("cpsid i\n bx lr\n"); }
__attribute__((section(".vectors"), used))
const void *const g_vectors[16 + 64] = { (void *)0x18020000u, (void *)Reset_Handler,
  [2 ... 10] = (void *)Default_Handler, [11] = (void *)Irq_Off, [12 ... 13] = (void *)Default_Handler, [14] = (void *)Irq_Off,
  [15] = (void *)SysTick_Handler, [16 ... 79] = (void *)Irq_Off
#ifdef GB_LINK
  , [16 + 37] = (void *)plink_irq
#endif
};
