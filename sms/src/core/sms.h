/* SMSEmu: the Sega Master System (Z80, VDP mode 4, PSG registers, Sega mapper). Author: mameise.
 * Header only, one machine. The includer defines before including:
 *   SMS_LINE(y, lb)  a finished line: y 0..191, lb 256 colour numbers (0..31, see g_sms.pal565)
 * and sets g_sms.rom / g_sms.rom_len (and g_sms.cram, cram_len for the cartridge RAM) before sms_reset().
 * The ROM is read in place (on the Paradise: the flash window). */
#ifndef SMS_H
#define SMS_H
#include <stdint.h>

typedef struct {
  const uint8_t *rom; uint32_t rom_len, banks;      /* banks of 16 KB (power of two, mask banks - 1) */
  uint8_t *cram; uint32_t cram_len; int cram_dirty;  /* cartridge RAM (battery), up to 32 KB */
  const uint8_t *rp[64];                             /* read pages of 1 KB */
  uint8_t map[4];                                    /* FFFC..FFFF */
  uint8_t io_ctrl, pad, pause_in;                    /* pad: buttons held, active high (SMS_UP...) */
  /* VDP */
  uint8_t reg[16], status, latch, first, code, rbuf, vint, lint, vs_frame;
  uint16_t addr; int lcount; int line;
  uint8_t cramv[64]; uint16_t pal565[32];
  uint8_t gg, gg_latch;                              /* Game Gear: 12 bit colours in two bytes, START on port 0 */
  /* PSG */
  uint16_t tone[3]; uint8_t vol[4], noise, psg_latch, psg_on;
  uint32_t frames;
} Sms;
#define SMS_UP 1u
#define SMS_DOWN 2u
#define SMS_LEFT 4u
#define SMS_RIGHT 8u
#define SMS_B1 16u
#define SMS_B2 32u
#define SMS_PAUSE 64u

static Sms g_sms;
static uint8_t g_ram[0x2000] __attribute__((aligned(4)));
static uint8_t g_vram[0x4000] __attribute__((aligned(4)));
static uint32_t g_tc[512 * 8 * 2];                   /* tiles decoded: per row 8 colour numbers (two words) */
static uint32_t g_spread[256];                       /* 8 bits to the low bit of 8 nibbles (bit 7 = first dot) */
static uint8_t g_empty[0x400];
static inline uint8_t SMS_RD(uint32_t a) { return g_sms.rp[a >> 10][a & 0x3FFu]; }
static inline void sms_wr(uint32_t a, uint8_t v);
static uint8_t sms_in(uint32_t p);
static void sms_out(uint32_t p, uint8_t v);
#define Z_RD(a) SMS_RD(a)
#define Z_WR(a, v) sms_wr((a), (v))
#define Z_IN(p) sms_in(p)
#define Z_OUT(p, v) sms_out((p), (v))
#include "z80.h"
static Z80 g_cpu;

static void sms_pages(int slot)
{
  Sms *s = &g_sms;
  if (slot == 2 && (s->map[0] & 8u) && s->cram) {
    uint8_t *c = s->cram + ((s->map[0] & 4u) ? 0x4000u : 0u);
    for (int p = 0; p < 16; p++) s->rp[32 + p] = c + p * 0x400;
    return;
  }
  uint32_t b = s->map[1 + slot] & (s->banks - 1u), o = b * 0x4000u;
  for (int p = slot == 0 ? 1 : 0; p < 16; p++) s->rp[slot * 16 + p] = o + p * 0x400u < s->rom_len ? s->rom + o + p * 0x400u : g_empty;
}
static void sms_mapper(uint32_t r, uint8_t v)
{
  Sms *s = &g_sms;
  s->map[r] = v;
  if (r == 0) sms_pages(2); else sms_pages((int)r - 1);
}

static inline void sms_wr(uint32_t a, uint8_t v)
{
  if (a >= 0xC000u) { g_ram[a & 0x1FFFu] = v; if (a >= 0xFFFCu) sms_mapper(a & 3u, v); }
  else if (a >= 0x8000u && (g_sms.map[0] & 8u) && g_sms.cram) {
    uint32_t o = (a & 0x3FFFu) + ((g_sms.map[0] & 4u) ? 0x4000u : 0u);
    if (o < g_sms.cram_len && g_sms.cram[o] != v) { g_sms.cram[o] = v; g_sms.cram_dirty |= 1 << (o >> 12); }
  }
}

/* ---- VDP ---- */
static void vdp_irq(void)
{
  Sms *s = &g_sms;
  g_cpu.irq = (uint8_t)(((s->status & 0x80u) && (s->reg[1] & 0x20u)) || (s->lint && (s->reg[0] & 0x10u)));
}
static void tc_row(uint32_t a)
{
  const uint8_t *v = &g_vram[a & 0x3FFCu];
  uint32_t t = (a & 0x3FFCu) >> 2, b0 = v[0], b1 = v[1], b2 = v[2], b3 = v[3];
  /* first four dots in the first word (byte 0 = first dot), next four in the second */
  uint32_t lo = 0, hi = 0;
  uint32_t s0 = g_spread[b0], s1 = g_spread[b1], s2 = g_spread[b2], s3 = g_spread[b3];
  uint32_t n = s0 | s1 << 1 | s2 << 2 | s3 << 3;   /* 8 nibbles: nibble k = dot k */
  lo = (n & 0xFu) | ((n >> 4) & 0xFu) << 8 | ((n >> 8) & 0xFu) << 16 | ((n >> 12) & 0xFu) << 24;
  hi = ((n >> 16) & 0xFu) | ((n >> 20) & 0xFu) << 8 | ((n >> 24) & 0xFu) << 16 | ((n >> 28) & 0xFu) << 24;
  g_tc[t * 2] = lo; g_tc[t * 2 + 1] = hi;
}
static void pal_gg(uint32_t i)   /* Game Gear: ----BBBB GGGGRRRR, 4 bits each */
{
  uint32_t lo = g_sms.cramv[i * 2u], hi = g_sms.cramv[i * 2u + 1u], r = lo & 15u, g = lo >> 4, b = hi & 15u;
  g_sms.pal565[i] = (uint16_t)((r << 1 | r >> 3) << 11 | (g << 2 | g >> 2) << 5 | (b << 1 | b >> 3));
}
static void pal_set(uint32_t i, uint8_t v)
{
  static const uint8_t c5[4] = { 0, 10, 21, 31 }, c6[4] = { 0, 21, 42, 63 };
  g_sms.cramv[i] = v;
  g_sms.pal565[i] = (uint16_t)(c5[v & 3u] << 11 | c6[(v >> 2) & 3u] << 5 | c5[(v >> 4) & 3u]);
}
static void vdp_data_w(uint8_t v)
{
  Sms *s = &g_sms;
  s->first = 0;
  if (s->code == 3) {
    if (s->gg) { uint32_t a = s->addr & 63u; if (!(a & 1u)) s->gg_latch = v; else { s->cramv[a - 1u] = s->gg_latch; s->cramv[a] = v; pal_gg(a >> 1); } }
    else pal_set(s->addr & 31u, v);
  }
  else { uint32_t a = s->addr & 0x3FFFu; if (g_vram[a] != v) { g_vram[a] = v; tc_row(a); } }
  s->rbuf = v;
  s->addr = (uint16_t)((s->addr + 1u) & 0x3FFFu);
}
static uint8_t vdp_data_r(void)
{
  Sms *s = &g_sms; uint8_t r = s->rbuf;
  s->first = 0; s->rbuf = g_vram[s->addr & 0x3FFFu]; s->addr = (uint16_t)((s->addr + 1u) & 0x3FFFu);
  return r;
}
static void vdp_ctrl_w(uint8_t v)
{
  Sms *s = &g_sms;
  if (!s->first) { s->first = 1; s->latch = v; s->addr = (uint16_t)((s->addr & 0x3F00u) | v); return; }
  s->first = 0;
  s->code = v >> 6; s->addr = (uint16_t)((v & 0x3Fu) << 8 | s->latch);
  if (s->code == 0) { s->rbuf = g_vram[s->addr]; s->addr = (uint16_t)((s->addr + 1u) & 0x3FFFu); }
  else if (s->code == 2) { s->reg[v & 15u] = s->latch; vdp_irq(); }
}
static uint8_t vdp_status_r(void)
{
  Sms *s = &g_sms; uint8_t r = (uint8_t)(s->status | 0x1Fu);
  s->status = 0; s->lint = 0; s->first = 0; vdp_irq();
  return r;
}
static uint8_t vdp_vcount(void)
{
  int l = g_sms.line;
  return (uint8_t)(l <= 0xDA ? l : l - 6);
}

/* ---- PSG (only the registers: the Paradise plays one tone from them) ---- */
static void psg_w(uint8_t v)
{
  Sms *s = &g_sms;
  if (v & 0x80u) {
    s->psg_latch = (uint8_t)((v >> 4) & 7u);
    uint32_t ch = s->psg_latch >> 1;
    if (s->psg_latch & 1u) s->vol[ch] = v & 15u;
    else if (ch < 3) s->tone[ch] = (uint16_t)((s->tone[ch] & 0x3F0u) | (v & 15u));
    else s->noise = v & 7u;
  } else {
    uint32_t ch = s->psg_latch >> 1;
    if (s->psg_latch & 1u) s->vol[ch] = v & 15u;
    else if (ch < 3) s->tone[ch] = (uint16_t)((s->tone[ch] & 15u) | (v & 0x3Fu) << 4);
    else s->noise = v & 7u;
  }
  s->psg_on = 1;
}

/* ---- ports ---- */
static uint8_t sms_in(uint32_t p)
{
  p &= 0xFFu;
  if (p < 0x40u) {
    if (g_sms.gg && p < 0x08u) {   /* Game Gear: START (pressed = 0), export, NTSC; the link port idle */
      static const uint8_t lk[8] = { 0, 0x7F, 0xFF, 0x00, 0xFF, 0x00, 0xFF, 0xFF };
      return p ? lk[p] : (uint8_t)(((g_sms.pad & SMS_PAUSE) ? 0 : 0x80u) | 0x40u);
    }
    return 0xFF;
  }
  if (p < 0x80u) return (p & 1u) ? (uint8_t)(((228 - g_cpu.cyc) * 171 / 228) & 0xFF) : vdp_vcount();
  if (p < 0xC0u) return (p & 1u) ? vdp_status_r() : vdp_data_r();
  if (!(p & 1u)) { uint32_t k = g_sms.pad; return (uint8_t)~(k & 0x3Fu); }
  /* port DD: second pad not there, reset key up; TH lines as set by port 3F (export console) */
  uint8_t c = g_sms.io_ctrl, r = 0x3F;
  r |= (c & 0x02u) ? 0x40u : (c & 0x20u) ? 0x40u : 0;
  r |= (c & 0x08u) ? 0x80u : (c & 0x80u) ? 0x80u : 0;
  return r;
}
static void sms_out(uint32_t p, uint8_t v)
{
  p &= 0xFFu;
  if (p < 0x40u) { if (p & 1u) g_sms.io_ctrl = v; return; }
  if (p < 0x80u) { psg_w(v); return; }
  if (p < 0xC0u) { if (p & 1u) vdp_ctrl_w(v); else vdp_data_w(v); }
}


/* ---- one line: background and sprites into 256 colour numbers ---- */
static uint8_t g_lb[256 + 16] __attribute__((aligned(4)));
static uint8_t g_sm[256 + 16];
static void render_line(int y)
{
  Sms *s = &g_sms; uint8_t *r = s->reg;
  uint8_t back = (uint8_t)(16u + (r[7] & 15u));
  if (!(r[1] & 0x40u)) { for (int x = 0; x < 256; x++) g_lb[x] = back; SMS_LINE(y, g_lb); return; }
  /* background: 33 tiles from the column under the first dot, then shifted by the fine scroll */
  static uint32_t bg[66 + 2];
  uint32_t hs = ((r[0] & 0x40u) && y < 16) ? 0 : r[8];
  uint32_t bx0 = (256u - hs) & 255u, col0 = bx0 >> 3, off0 = bx0 & 7u;
  uint32_t nt = (uint32_t)(r[2] & 0x0Eu) << 10;
  uint32_t vy = (uint32_t)y + s->vs_frame; if (vy >= 224u) vy -= 224u;
  for (int k = 0; k < 33; k++) {
    uint32_t col = (col0 + (uint32_t)k) & 31u, row = vy;
    if ((r[0] & 0x80u) && k - (off0 ? 1 : 0) >= 24) row = (uint32_t)y;
    const uint8_t *e = &g_vram[nt + ((row >> 3) * 32u + col) * 2u];
    uint32_t t = e[0] | (uint32_t)(e[1] & 1u) << 8, fl = e[1], ty = row & 7u;
    if (fl & 4u) ty = 7u - ty;
    uint32_t lo = g_tc[(t * 8u + ty) * 2u], hi = g_tc[(t * 8u + ty) * 2u + 1u];
    if (fl & 2u) { uint32_t x = __builtin_bswap32(hi); hi = __builtin_bswap32(lo); lo = x; }
    uint32_t add = ((fl & 8u) ? 0x10101010u : 0u);
    if (fl & 0x10u) {   /* priority over sprites where the dot is not colour 0 */
      uint32_t ml = lo | lo >> 1 | lo >> 2 | lo >> 3, mh = hi | hi >> 1 | hi >> 2 | hi >> 3;
      lo |= (ml & 0x01010101u) << 6; hi |= (mh & 0x01010101u) << 6;
    }
    bg[k * 2] = lo | add; bg[k * 2 + 1] = hi | add;
  }
  const uint8_t *src = (const uint8_t *)bg + off0;
  uint32_t *d = (uint32_t *)g_lb;
  if (!off0) { for (int k = 0; k < 64; k++) d[k] = bg[k]; }
  else for (int x = 0; x < 256; x++) g_lb[x] = src[x];
  /* sprites: the first eight on this line, the earlier one wins */
  uint32_t sat = (uint32_t)(r[5] & 0x7Eu) << 7, h = (r[1] & 2u) ? 16u : 8u, zoom = r[1] & 1u, pb = (r[6] & 4u) ? 256u : 0u;
  uint32_t hh = h << zoom;
  int n = 0, any = 0;
  uint8_t idx[8];
  for (int i = 0; i < 64; i++) {
    uint32_t sy = g_vram[sat + (uint32_t)i];
    if (sy == 0xD0u) break;
    int ly = y - (int)sy - 1; if (ly < -16) ly += 256;
    if (ly < 0 || ly >= (int)hh) continue;
    if (n == 8) { s->status |= 0x40u; break; }
    idx[n++] = (uint8_t)i;
  }
  for (int j = 0; j < n; j++) {
    int i = idx[j];
    uint32_t sy = g_vram[sat + (uint32_t)i];
    int ly = y - (int)sy - 1; if (ly < -16) ly += 256;
    ly >>= zoom;
    int sx = g_vram[sat + 0x80u + (uint32_t)i * 2u]; if (r[0] & 8u) sx -= 8;
    uint32_t t = g_vram[sat + 0x81u + (uint32_t)i * 2u]; if (h == 16u) t &= 0xFEu;
    t = (t + pb + (uint32_t)(ly >> 3)) & 511u;
    const uint8_t *px = (const uint8_t *)&g_tc[(t * 8u + (uint32_t)(ly & 7)) * 2u];
    if (!any) { for (int x = 0; x < 256; x++) g_sm[x] = 0; any = 1; }
    for (int k = 0; k < (8 << zoom); k++) {
      int x = sx + k; if (x < 0 || x >= 256) continue;
      uint32_t c = px[k >> zoom]; if (!c) continue;
      if (g_sm[x]) { s->status |= 0x20u; continue; }
      g_sm[x] = 1;
      uint8_t b = g_lb[x];
      if ((b & 0x40u) ) continue;
      g_lb[x] = (uint8_t)(c | 16u);
    }
  }
  if (r[0] & 0x20u) for (int x = 0; x < 8; x++) g_lb[x] = back;
  for (int k = 0; k < 64; k++) d[k] &= 0x1F1F1F1Fu;
  SMS_LINE(y, g_lb);
}

/* ---- the machine ---- */
static void sms_reset(void)
{
  Sms *s = &g_sms;
  const uint8_t *rom = s->rom; uint32_t len = s->rom_len; uint8_t *cr = s->cram; uint32_t cl = s->cram_len; uint8_t gg = s->gg;
  uint8_t *p = (uint8_t *)s; for (unsigned i = 0; i < sizeof *s; i++) p[i] = 0;
  s->rom = rom; s->rom_len = len; s->cram = cr; s->cram_len = cl; s->gg = gg;
  s->banks = 1; while (s->banks * 0x4000u < len) s->banks <<= 1;
  for (int i = 0; i < 256; i++) { uint32_t v = 0; for (int k = 0; k < 8; k++) if (i & (0x80 >> k)) v |= 1u << (k * 4); g_spread[i] = v; }
  for (unsigned i = 0; i < sizeof g_ram; i++) g_ram[i] = 0;
  for (unsigned i = 0; i < sizeof g_vram; i++) g_vram[i] = 0;
  for (unsigned i = 0; i < sizeof g_tc / 4; i++) g_tc[i] = 0;
  for (unsigned i = 0; i < sizeof g_empty; i++) g_empty[i] = 0xFF;
  for (int i = 0; i < 32; i++) pal_set((uint32_t)i, 0);
  s->rp[0] = rom;
  s->map[0] = 0; s->map[1] = 0; s->map[2] = 1; s->map[3] = 2;
  sms_pages(0); sms_pages(1); sms_pages(2);
  for (int i = 48; i < 64; i++) s->rp[i] = g_ram + (i & 7) * 0x400;
  static const uint8_t r0[11] = { 0x36, 0x80, 0xFF, 0xFF, 0xFF, 0xFF, 0xFB, 0x00, 0x00, 0x00, 0xFF };
  for (int i = 0; i < 11; i++) s->reg[i] = r0[i];
  s->lcount = 0xFF;
  s->io_ctrl = 0xFF;
  for (int i = 0; i < 4; i++) s->vol[i] = 15;
  z80_tables(); z80_reset(&g_cpu);
  g_cpu.sp.w = 0xDFF0; g_cpu.im = 1;
}

/* one frame (NTSC: 262 lines of 228 cycles); draw: lines go to SMS_LINE */
static void sms_frame(int draw)
{
  Sms *s = &g_sms;
  static uint32_t pause_old;
  if ((s->pad & SMS_PAUSE) && !pause_old && !s->gg) g_cpu.nmi = 1;
  pause_old = s->pad & SMS_PAUSE;
  s->vs_frame = s->reg[9];
  for (int y = 0; y < 262; y++) {
    s->line = y;
    if (y < 192 && draw) render_line(y);
    if (y <= 192) { if (--s->lcount < 0) { s->lcount = s->reg[10]; s->lint = 1; vdp_irq(); } }
    else s->lcount = s->reg[10];
    if (y == 193) { s->status |= 0x80u; vdp_irq(); }
    z80_run(&g_cpu, 228);
  }
  s->frames++;
}

#endif
