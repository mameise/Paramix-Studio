/* Save and restore state.
 * 1. Written flash sectors of the guest go back into its image in the
 *    Paradise flash (that is where the Tamagotchi's save lives).
 * 2. The run state (CPU, RAM, peripherals, screen) is packed into the
 *    backup area; the next start resumes from there. */
#include "hw.h"
#include "flash.h"
#include "../core/tc.h"
#include "state.h"

#define MAGIC 0x53584D50u   /* "PMXS" */
#define SVER  2u   /* 2: Emu fields rearranged for the translator (v0.7) */
static u8 *g_sec;            /* 4 KB buffer: first overlay slot (empty when saving) */

/* ---- Writing back the guest flash ---- */
static u32 guest_addr(const StCfg *c, u32 off)
{
  u32 k=c->map[(off>>16)&127];
  if(k==0xFE) k=off>>16;
  if(k==0xFF) return 0;
  return c->guest_off + (k<<16) + (off & 0xFFFFu);
}
int st_flush_guest(Emu *e, const StCfg *c)
{
  int n=0;
  if(e->flash_busy || e->flash_state) return 0;
  for(int i=0;i<e->ovl.n;i++){
    if(e->ovl.base[i]==0xFFFFFFFFu) continue;
    u32 a=guest_addr(c, e->ovl.base[i]);
    if(a){ n+=flash_update4k(a, e->ovl.mem[i]); n+=flash_update4k(a+0x1000u, e->ovl.mem[i]+0x1000u); }
    { u32 b=e->ovl.base[i]; e->ovl.base[i]=0xFFFFFFFFu; jit_rom_update(e, b); }   /* slot free, reads go to flash again */
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
  int total=e->ovl.n*2;
  for(int t=0;t<total;t++){
    int k=(cur+t)%total, i=k>>1, h=k&1;
    if(e->ovl.base[i]==0xFFFFFFFFu) continue;
    u32 a=guest_addr(c, e->ovl.base[i]); if(!a) continue;
    a += (u32)h*0x1000u; const u8 *src=e->ovl.mem[i]+(u32)h*0x1000u;
    if(flash_same(a, src, 0x1000u)) continue;
    flash_prog4k(a, src); st_n_step++; cur=k+1;
    return 0;
  }
  return 1;
}

/* All slots in use: write one (round robin) to the image and free it.
 * Costs one flash write; the guest only sees a short pause. */
static const StCfg *g_evcfg;
void st_init(const StCfg *c, u8 *buf4k){ g_evcfg=c; g_sec=buf4k; }
u32 st_n_evict;                            /* counter for the speed display */
int st_evict(Emu *e)
{
  static int next;
  if(!g_evcfg) return 0;
  for(int t=0;t<OVL_SLOTS;t++){
    int i=(next+t)%OVL_SLOTS;
    if(!e->ovl.mem[i] || e->ovl.base[i]==0xFFFFFFFFu) continue;
    u32 b=e->ovl.base[i], a=guest_addr(g_evcfg, b);
    if(a){ flash_update4k(a, e->ovl.mem[i]); flash_update4k(a+0x1000u, e->ovl.mem[i]+0x1000u); }
    st_n_evict++;
    e->ovl.base[i]=0xFFFFFFFFu;
    jit_rom_update(e, b);
    e->fw_len=0; e->fw_lo=0xFFFFFFFFu;
    next=i+1;
    return 1;
  }
  return 0;
}

/* ---- packed data stream (PackBits) ---- */
typedef struct { u32 addr, n, sec, total; } Wr;
static void w_flush(Wr *w){ if(!w->n) return; for(u32 i=w->n;i<0x1000u;i++) g_sec[i]=0xFF; flash_update4k(w->addr+w->sec*0x1000u, g_sec); w->sec++; w->n=0; }
static void w_byte(Wr *w, u8 b){ g_sec[w->n++]=b; w->total++; if(w->n==0x1000u) w_flush(w); }
static void w_block(Wr *w, const u8 *p, u32 len)
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

void st_save(Emu *e, const StCfg *c, u16 (*gram)[128])
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
  Rd r={ (const volatile u8*)(0x60000000u + c->save_off + 0x1000u), 0, 0 };
  r.end = r.p + h.bytes;
  r_block(&r, (u8*)e, sizeof(Emu));
  r_block(&r, (u8*)gram, 128u*128u*2u);
  if(r.bad) return 0;
  u32 now=rtc_now();
  *pause_s = (now>=h.rtc) ? now-h.rtc : 0;
  return 1;
}
