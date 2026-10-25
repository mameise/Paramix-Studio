/* Paramix: an emulator's whole state in the flash (1.6.11 test2, GBEmu and SMSEmu). Stored on leaving, read once at the
 * next start and dropped right after reading, so a power cut later starts from the game's own save as the real
 * hardware would. Layout: header (magic "PMST", tag, length, sum) and the parts one after the other; the sectors behind
 * the first are written first, the one with the header last (a cut while storing leaves no valid state).
 * tag: anything that changes with the program build or the game (a state of another build is not taken).
 * Header only. Author: mameise */
#ifndef PSTATE_H
#define PSTATE_H
typedef struct { void *p; u32 n; } StPart;
#define ST_MAGIC 0x54534D50u   /* "PMST" */
#define ST_HEAD 16u

/* byte i of the parts behind the header */
static inline u8 st_byte(const StPart *pt, int np, u32 i)
{
  for (int k = 0; k < np; k++) { if (i < pt[k].n) return ((const u8 *)pt[k].p)[i]; i -= pt[k].n; }
  return 0xFF;
}
static u32 st_len(const StPart *pt, int np) { u32 n = 0; for (int k = 0; k < np; k++) n += pt[k].n; return n; }

/* sec: 4 KB buffer. Returns 1 when stored. */
static int st_store(u32 at, u32 max, u32 tag, const StPart *pt, int np, u8 *sec)
{
  u32 len = st_len(pt, np), tot = ST_HEAD + len;
  if (tot > max) return 0;
  u32 sum = 0x5157u;
  for (int k = 0; k < np; k++) { const u8 *p = (const u8 *)pt[k].p; for (u32 i = 0; i < pt[k].n; i++) sum = sum * 31u + p[i]; }
  u32 ns = (tot + 0xFFFu) >> 12;
  for (u32 s = ns; s-- > 0; ) {                 /* the last sector first, the header sector at the end */
    hw_wdt_feed();
    for (u32 i = 0; i < 0x1000u; i++) {
      u32 o = (s << 12) + i;
      u8 v = 0xFF;
      if (o < ST_HEAD) { u32 w = o < 4 ? ST_MAGIC : o < 8 ? tag : o < 12 ? len : sum; v = (u8)(w >> (8 * (o & 3u))); }
      else if (o < tot) v = st_byte(pt, np, o - ST_HEAD);
      sec[i] = v;
    }
    flash_update4k(at + (s << 12), sec);
  }
  return 1;
}
static u32 st_rd32(const volatile u8 *p) { return p[0] | (u32)p[1] << 8 | (u32)p[2] << 16 | (u32)p[3] << 24; }
/* Returns 1 when a state of this tag was read into the parts (sizes must match). */
static int st_load(u32 at, u32 max, u32 tag, const StPart *pt, int np)
{
  const volatile u8 *f = XIP_PTR(at);
  u32 len = st_len(pt, np);
  if (st_rd32(f) != ST_MAGIC || st_rd32(f + 4) != tag || st_rd32(f + 8) != len || ST_HEAD + len > max) return 0;
  u32 sum = 0x5157u;
  for (u32 i = 0; i < len; i++) { sum = sum * 31u + f[ST_HEAD + i]; if ((i & 0xFFFu) == 0) hw_wdt_feed(); }
  if (sum != st_rd32(f + 12)) return 0;
  u32 o = ST_HEAD;
  for (int k = 0; k < np; k++) { u8 *p = (u8 *)pt[k].p; for (u32 i = 0; i < pt[k].n; i++) p[i] = f[o++]; }
  return 1;
}
/* the header away: the state is used once */
static void st_drop(u32 at) { if (st_rd32(XIP_PTR(at)) != 0xFFFFFFFFu) flash_erase4k(at); }
#endif
