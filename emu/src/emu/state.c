/* Save and restore state.
 * 1. Written flash sectors of the guest go back into its image in the
 *    Paradise flash (that is where the Tamagotchi's save lives).
 * 2. The run state (CPU, RAM, peripherals, screen) is packed into the
 *    backup area; the next start resumes from there. */
#include "hw.h"
#include "flash.h"
#include "../core/tc.h"
#include "state.h"
#ifdef EMU_OVL
#define ST_OVL __attribute__((section(".ovltext")))   /* 1.6.13 test10: the save when leaving, from the overlay */
#else
#define ST_OVL
#endif

#define MAGIC 0x53584D50u   /* "PMXS" */
#define SVER  3u   /* 2: Emu fields rearranged for the translator (v0.7), 3: 64 KB sectors and spare pages (v0.9.2) */
static u8 *g_sec;            /* 4 KB buffer: first overlay slot (empty when saving) */

/* ---- Spare pages (v0.9.2) ----
 * The guest also writes 64 KB sectors that are empty in its firmware (item stores and others).
 * The Studio does not store empty blocks, so these written 8 KB parts go into spare pages
 * behind the packed firmware. A table in the first 4 KB says which page holds which part.
 * Order when writing: page first, then table. A table that does not check out counts as empty. */
#define PG_MAX 64
#define PG_MAGIC 0x50584D50u   /* "PMXP" */
#ifndef PT_PAD
#define PT_PAD 4   /* host test: 4096, so the 4 KB read stays inside the variable */
#endif
static struct { u32 magic, n, chk, pad; u16 vp[PG_MAX]; u8 tail[PT_PAD]; } g_pt;
static u32 g_pg_tab, g_pg_data, g_pg_n;
u32 st_n_pages, st_n_free;
__attribute__((noinline)) static u32 pt_chk(void){ u32 x=g_pt.magic ^ (g_pt.n*0x9E3779B1u); for(u32 i=0;i<PG_MAX;i++) x=(x<<5|x>>27) ^ g_pt.vp[i]; return x; }
__attribute__((noinline)) static void pt_save(void){
  g_pt.magic=PG_MAGIC; g_pt.n=g_pg_n; g_pt.chk=pt_chk();
  /* The DMA reads 4 KB from the table: what follows it in RAM is ignored when reading back. */
  if(g_pg_n && !flash_same(g_pg_tab, &g_pt, 16u+2u*PG_MAX)) flash_prog4k(g_pg_tab, &g_pt);
}
__attribute__((noinline)) static void pt_count(void){ u32 n=0; for(u32 i=0;i<g_pg_n;i++) if(g_pt.vp[i]!=0xFFFFu) n++; st_n_pages=n; st_n_free=g_pg_n-n; }
static int blk_mapped(const StCfg *c, u32 blk){ return c->map[blk & 127u] != 0xFFu; }

TC_INIT void st_pages(Emu *e, const StCfg *c)
{
  u32 lo=(c->guest_off + c->guest_size + 0x1FFFu) & ~0x1FFFu, hi=c->spare_end & ~0x1FFFu;
  g_pg_n=0;
  if(hi >= lo + 0x4000u){ g_pg_tab=lo; g_pg_data=lo+0x2000u; g_pg_n=(hi-g_pg_data)>>13; if(g_pg_n>PG_MAX) g_pg_n=PG_MAX; }
  flash_read(g_pg_tab, &g_pt, 16u+2u*PG_MAX);
  if(!g_pg_n || g_pt.magic!=PG_MAGIC || g_pt.n!=g_pg_n || g_pt.chk!=pt_chk())
    for(u32 i=0;i<PG_MAX;i++) g_pt.vp[i]=0xFFFFu;
  e->pg_base=(const uint8_t*)XIP_PTR(g_pg_data);
  for(u32 i=0;i<4;i++) e->pg_blk[i]=0;
  for(u32 i=0;i<1024;i++) e->pg_map[i]=0xFFu;
  u32 nb=e->dev.rom_size>>16; if(nb>128) nb=128;
  for(u32 b=0;b<nb;b++) if(!blk_mapped(c,b) && (b<<16)-e->par_base>=0x10000u) e->pg_blk[b>>5] |= 1u<<(b&31u);
  for(u32 i=0;i<g_pg_n;i++){ u32 v=g_pt.vp[i];
    if(v<1024u && ((e->pg_blk[v>>8]>>((v>>3)&31u))&1u) && e->pg_map[v]==0xFFu) e->pg_map[v]=(u8)i; else g_pt.vp[i]=0xFFFFu; }
  pt_count();
}

/* Address in the Paradise flash of a stored block (0 = none). */
__attribute__((noinline)) static u32 blk_addr(const StCfg *c, u32 off)
{
  u32 k=c->map[(off>>16)&127];
  if(k==0xFE) k=off>>16;
  if(k==0xFF) return 0;
  return c->guest_off + (k<<16) + (off & 0xFFFFu);
}
__attribute__((noinline)) static int all_ff(const u8 *p){ const u32 *w=(const u32*)p; for(u32 i=0;i<OVL_SIZE/4u;i++) if(w[i]!=0xFFFFFFFFu) return 0; return 1; }

/* Write an 8 KB part back. step: at most one 4 KB write. Returns the number of flash writes. */
static int wb_part(Emu *e, const StCfg *c, u32 base, const u8 *mem, int step)
{
  u32 a=blk_addr(c, base);
  if(!a){
    u32 v=(base>>13)&1023u, i=e->pg_map[v];
    if(!((e->pg_blk[v>>8]>>((v>>3)&31u))&1u)) return 0;
    if(all_ff(mem)){                                   /* erased: release the page */
      if(i==0xFFu) return 0;
      g_pt.vp[i]=0xFFFFu; e->pg_map[v]=0xFFu; pt_save(); pt_count(); return 1;
    }
    if(i==0xFFu){                                      /* new page */
      for(i=0;i<g_pg_n && g_pt.vp[i]!=0xFFFFu;i++){}
      if(i>=g_pg_n){ e->flash_lost_writes++; return 0; }
      a=g_pg_data + (i<<13);
      flash_update4k(a, mem); flash_update4k(a+0x1000u, mem+0x1000u);
      g_pt.vp[i]=(u16)v; e->pg_map[v]=(u8)i; pt_save(); pt_count();
      return 3;
    }
    a=g_pg_data + (i<<13);
  }
  if(step){
    for(u32 h=0;h<2;h++) if(!flash_same(a+h*0x1000u, mem+h*0x1000u, 0x1000u)){ flash_prog4k(a+h*0x1000u, mem+h*0x1000u); return 1; }
    return 0;
  }
  return flash_update4k(a, mem) + flash_update4k(a+0x1000u, mem+0x1000u);
}

/* Erase a 64 KB sector of the guest (hook tc_big_erase). */
static const StCfg *g_evcfg;
int st_big_erase(Emu *e, uint32_t base)
{
  const StCfg *c=g_evcfg; if(!c) return 0;
  u32 a=blk_addr(c, base & ~0xFFFFu);
  if(a){
    for(u32 s=0;s<16u;s++) if(!flash_isff(a+s*0x1000u, 0x1000u)) flash_erase4k(a+s*0x1000u);
    return 1;
  }
  u32 v0=((base>>16)&127u)*8u; int ch=0;
  for(u32 v=v0; v<v0+8u; v++){ u32 i=e->pg_map[v]; if(i!=0xFFu){ g_pt.vp[i]=0xFFFFu; e->pg_map[v]=0xFFu; ch=1; } }
  if(ch){ pt_save(); pt_count(); }
  return 1;
}

/* ---- Writing back the guest flash ---- */
ST_OVL int st_flush_guest(Emu *e, const StCfg *c)
{
  int n=0;
  if(e->flash_busy || e->flash_state) return 0;
  for(int i=0;i<e->ovl.n;i++){
    if(e->ovl.base[i]==0xFFFFFFFFu) continue;
    n+=wb_part(e, c, e->ovl.base[i], e->ovl.mem[i], 0);
    tc_ovl_free(e, i);                     /* slot free, reads go to flash again */
  }
  e->ovl.n=0;
  e->fw_len=0; e->fw_lo=0xFFFFFFFFu;
  return n;
}

/* v0.8.22: write back step by step. At most one changed 4 KB part per call; the slots
 * stay in use (no copying again). Returns 1 when everything in flash is already up to date. */
u32 st_n_step;
int st_flush_step(Emu *e, const StCfg *c)
{
  static int cur;
  if(e->flash_busy || e->flash_state) return 0;
  int total=e->ovl.n;
  for(int t=0;t<total;t++){
    int i=(cur+t)%total;
    if(e->ovl.base[i]==0xFFFFFFFFu) continue;
    if(wb_part(e, c, e->ovl.base[i], e->ovl.mem[i], 1)){ st_n_step++; cur=i; return 0; }
  }
  return 1;
}

/* All slots in use: write one (round robin) to the image and free it.
 * Costs one flash write; the guest only sees a short pause. */
void st_init(const StCfg *c, u8 *buf4k){ g_evcfg=c; g_sec=buf4k; }
u32 st_n_evict;                            /* counter for the speed display */
int st_evict(Emu *e)
{
  static int next;
  if(!g_evcfg) return 0;
  for(int t=0;t<OVL_SLOTS;t++){
    int i=(next+t)%OVL_SLOTS;
    if(!e->ovl.mem[i] || e->ovl.base[i]==0xFFFFFFFFu) continue;
    wb_part(e, g_evcfg, e->ovl.base[i], e->ovl.mem[i], 0);
    st_n_evict++;
    tc_ovl_free(e, i);
    e->fw_len=0; e->fw_lo=0xFFFFFFFFu;
    next=i+1;
    return 1;
  }
  return 0;
}

/* ---- packed data stream (PackBits) ---- */
typedef struct { u32 addr, n, sec, total; } Wr;
ST_OVL static void w_flush(Wr *w){ if(!w->n) return; for(u32 i=w->n;i<0x1000u;i++) g_sec[i]=0xFF; flash_update4k(w->addr+w->sec*0x1000u, g_sec); w->sec++; w->n=0; }
ST_OVL static void w_byte(Wr *w, u8 b){ g_sec[w->n++]=b; w->total++; if(w->n==0x1000u) w_flush(w); }
ST_OVL static void w_block(Wr *w, const u8 *p, u32 len)
{
  u32 i=0;
  while(i<len){
    u32 r=1; while(i+r<len && r<130 && p[i+r]==p[i]) r++;
    if(r>=3){ w_byte(w,(u8)(0x80+r-3)); w_byte(w,p[i]); i+=r; continue; }
    u32 s=i, l=0;
    while(i<len && l<128){
      u32 q=1; while(i+q<len && q<3 && p[i+q]==p[i]) q++;
      if(q>=3) break;
      i++; l++;
    }
    w_byte(w,(u8)(l-1)); for(u32 k=0;k<l;k++) w_byte(w,p[s+k]);
  }
}
typedef struct { const volatile u8 *p, *end; int bad; } Rd;
static u8 r_byte(Rd *r){ if(r->p>=r->end){ r->bad=1; return 0; } return *r->p++; }
static void r_block(Rd *r, u8 *d, u32 len)
{
  u32 i=0;
  while(i<len && !r->bad){
    u8 c=r_byte(r);
    if(c&0x80){ u32 n=(u32)(c-0x80)+3; u8 v=r_byte(r); for(u32 k=0;k<n && i<len;k++) d[i++]=v; }
    else { u32 n=(u32)c+1; for(u32 k=0;k<n && i<len;k++) d[i++]=r_byte(r); }
  }
}

typedef struct { u32 magic, ver, key, rtc, bytes, sectors, chk; } Hdr;
static u32 st_key(const StCfg *c){ return (u32)sizeof(Emu) ^ (c->device<<24) ^ (c->guest_size<<4) ^ 0x5A17u; }
static u32 rtc_now(void){ return REG(0x45000304u); }

void st_drop(const StCfg *c){ if(!flash_isff(c->save_off, 0x1000u)) flash_erase4k(c->save_off); }

ST_OVL void st_save(Emu *e, const StCfg *c, u16 (*gram)[128])
{
  st_flush_guest(e, c);
  /* invalidate the old header first */
  for(u32 i=0;i<0x1000u;i++) g_sec[i]=0xFF;
  flash_update4k(c->save_off, g_sec);
  Wr w={ c->save_off+0x1000u, 0, 0, 0 };
  w_block(&w, (const u8*)e, sizeof(Emu));
  w_block(&w, (const u8*)gram, 128u*128u*2u);
  w_flush(&w);
  Hdr h={ MAGIC, SVER, st_key(c), rtc_now(), w.total, w.sec, 0 };
  h.chk = h.magic ^ h.ver ^ h.key ^ h.rtc ^ h.bytes ^ h.sectors;
  for(u32 i=0;i<0x1000u;i++) g_sec[i]=0xFF;
  u8 *hp=(u8*)&h; for(u32 i=0;i<sizeof h;i++) g_sec[i]=hp[i];
  flash_prog4k(c->save_off, g_sec);
}

/* Returns 1 if a state was loaded. The caller sets the pointers in Emu. */
TC_INIT int st_load(Emu *e, const StCfg *c, u16 (*gram)[128], u32 *pause_s)
{
  Hdr h; flash_read(c->save_off, &h, sizeof h);
  if(h.magic!=MAGIC || h.ver!=SVER || h.key!=st_key(c)) return 0;
  if(h.chk != (h.magic ^ h.ver ^ h.key ^ h.rtc ^ h.bytes ^ h.sectors)) return 0;
  if(h.sectors > 0x4F) return 0;
  Rd r={ XIP_PTR(c->save_off + 0x1000u), 0, 0 };
  r.end = r.p + h.bytes;
  r_block(&r, (u8*)e, sizeof(Emu));
  r_block(&r, (u8*)gram, 128u*128u*2u);
  if(r.bad) return 0;
  u32 now=rtc_now();
  *pause_s = (now>=h.rtc) ? now-h.rtc : 0;
  return 1;
}
