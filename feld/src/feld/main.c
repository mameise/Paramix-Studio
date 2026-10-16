/* Paramix menu for the Paradise (field switch, items, Tamapoints, app launch).
 *
 * Started by the stub (A+C on the home screen), runs from PRAM.
 * Everything that depends on the edition (ID page, fields, factory settings,
 * location of bag and money, item catalog) lives in the settings block
 * g_cfg and is filled in by the Studio when it installs the menu. The
 * defaults here are for Land.
 *
 * Switching from field cur to field tgt:
 *  1. Compress the save (all used pages from user_start on) into slot[cur]
 *     and verify it by reading it back.
 *  2. Rewrite the save area: contents of slot[tgt], or, if the field has
 *     never run, the factory settings pages for tgt.
 *  3. Set the ID page to tgt.
 *  4. Free slot[tgt], restart.
 * If power fails between 1 and 4, the old save is still in slot[cur] and the
 * ID still points to cur: on the next start the program offers to restore it.
 *
 * Items and Tamapoints: the settings block 0xEFE000 (copy at 0xEFF000) holds
 * money (u32), the item list (u16 per slot, ends at 0) and the counts
 * (4 bits per slot). Protected by CRC-16/ARC over bytes 4 to 4096 (at 0,
 * inverse at 2). Same as in the TamaParadise app: counts 1 to 15 only, new
 * items go into the first free slot, nothing is deleted. */
#include "io.h"
#include "hw.h"
#include "flash.h"
#include "font.h"

#define FELD_MAGIC 0x444C4546u      /* "FELD" */
#define SLOT_MAGIC 0x544C5346u      /* "FSLT" */
#define CAT_MAGIC  0x54414350u      /* "PCAT" */
#define PAGE 0x1000u
#define NPAGE_MAX 500u
#define SET_A 0xEFE000u
#define SET_B 0xEFF000u
#define F_FIELDS 1u
#define F_BAG    2u

#define APPS_MAX 6
typedef struct { u16 off; u8 val; u8 pad; } Patch;
typedef struct {
  u32 magic, version;
  u32 slot_off, slot_size, nslots;
  u32 user_start, user_end, id_page;
  u32 stub_off;                      /* stub: word +8 = program source (XIP), 0 = none */
  u32 flags;
  u32 id_c; u16 id_h0, id_h1;        /* ID page: footer u16 h0, u16 h1, C-id, ~(C-id) */
  u32 nfields;
  u32 field_id[4];
  char field_name[4][12];
  u32 field_col[4];
  Patch fac_common[16];              /* factory settings: off 0 = end of list */
  Patch fac_field[4][4];
  u32 money_off, list_off, count_off;
  u32 cat_off, cat_n;                /* catalog: 8 byte header, then 28 bytes per entry (u16 id, u8 fixed, u8 len, u16 image, u16 0, 20 chars) */
  u32 field_byte;                    /* offset of the field in the settings block (Land 439, Jade 443, Tropical 499) */
  u32 pack_off;                      /* image table in flash (for the item pictures), 0 = none */
  /* from version 4: fields 5 and 6 (six fields: Land, Water, Sky next to Tropical, Glacier, Forest) */
  u32 six_off;                       /* offset of the field set byte in the settings block, 0 = none */
  u32 field_id2[2];
  char field_name2[2][12];
  u32 field_col2[2];
  Patch fac_field2[2][4];
  u32 slot_off2, slot_n1;            /* slots slot_n1.. lie at slot_off2 (0 = all at slot_off) */
  u8 six_map[8];                     /* field of (set byte * 4 + field byte), 0xFF = none */
  /* from version 5 (1.6.0): more programs (each a full PRAM image in the flash, started through the
   * stub entry at +0x0C), and the name of the stub's own program ("" = by its kind) */
  char name0[12];
  u32 napps;
  struct { u32 src; char name[12]; } apps[APPS_MAX];
} Cfg;

#ifdef HOST
#define CFG_QUAL
#else
#define CFG_QUAL const
#endif
__attribute__((section(".pmxcfg"), used))
volatile CFG_QUAL Cfg g_cfg = { FELD_MAGIC, 5, 0x830000u, 0x58000u, 3, 0xD4A000u, 0x1000000u, 0xD49000u, 0xB0000u,
  F_FIELDS|F_BAG, 0x70169E2Cu, 1, 4, 3, {1,2,3,0}, {"LAND","WATER","SKY",""},
  {0x5647u, 0x2BDFu, 0x7E9Fu, 0xFD45u},
  {{8,0x11,0},{16,0xc0,0},{17,0xa8,0},{168,1,0},{576,0x3f,0},{577,0x1f,0},{1856,1,0}},
  {{{4,0x0c,0},{170,2,0},{439,0,0}}, {{4,4,0},{170,1,0},{439,1,0}}, {{4,4,0},{170,1,0},{439,2,0}}},
  0xb4u, 0x240u, 0x740u, 0, 0, 439u, 0, 0, {0,0}, {"",""}, {0,0}, {{{0,0,0}}}, 0, 0, {0}, "", 0, {{0,""}} };
#define XIP_DEC 0x11000u
#include <stddef.h>
_Static_assert(offsetof(Cfg,flags)==36 && offsetof(Cfg,id_c)==40 && offsetof(Cfg,nfields)==48 && offsetof(Cfg,field_name)==68
  && offsetof(Cfg,field_col)==116 && offsetof(Cfg,fac_common)==132 && offsetof(Cfg,fac_field)==196
  && offsetof(Cfg,money_off)==260 && offsetof(Cfg,cat_n)==276 && offsetof(Cfg,field_byte)==280 && offsetof(Cfg,six_off)==288 && offsetof(Cfg,field_name2)==300
  && offsetof(Cfg,field_col2)==324 && offsetof(Cfg,fac_field2)==332 && offsetof(Cfg,slot_off2)==364 && offsetof(Cfg,six_map)==372 && sizeof(Cfg)==492 && offsetof(Cfg,name0)==380 && offsetof(Cfg,napps)==392 && offsetof(Cfg,apps)==396, "settings block layout");
#define NF_MAX 6u

/* Saved field (header page at the start of its slot). res[0] = POOL_MAGIC (1.2.12): the
 * packed data lies in the pages dpg[0..res[1]-1] of the shared pool (all slot pages except
 * the header pages), so one field can use the room the other fields do not need. Without
 * it (older menus) the data follows the header in the field's own slot. */
#define POOL_MAGIC 0x4C4F4F50u      /* "POOL" */
#define DPG_MAX 1024u
typedef struct {
  u32 magic, field, npages, bytes, sum, res[3];
  struct { u16 page, clen; } e[NPAGE_MAX];
  u16 dpg[DPG_MAX];
} SlotHdr;
_Static_assert(sizeof(SlotHdr)<=4096u, "slot header fits one page");

#define GPIO2 0x4001A000u

static u16 g_fb[128*128];
static u8  g_page[PAGE];             /* uncompressed page */
static u8  g_cmp[PAGE+PAGE/8+16];    /* one compressed page */
static u8  g_out[PAGE];              /* write buffer */
static SlotHdr g_hdr;
static u16 g_head[4096], g_prev[4096];
static u8  g_ff[PAGE];               /* empty page: erase = write an empty page */
static u8  g_set[PAGE];              /* settings block being edited */

#ifdef HOST
#include "host.h"
#else
#define FLASH_BASE ((volatile u8*)0x60000000u)
#endif
static u32 rd32(u32 a){ return *(const volatile u32*)(FLASH_BASE + a); }
static u32 rd16(u32 a){ return *(const volatile u16*)(FLASH_BASE + a); }
static const volatile u8* fl(u32 a){ return (const volatile u8*)(FLASH_BASE + a); }

/* ---------------------------------------------------------------- Display */
#define RGB(r,g,b) ((u16)((((r)>>3)<<11)|(((g)>>2)<<5)|((b)>>3)))
static void rect(int x0,int y0,int x1,int y1,u16 c){
  if(x0<0)x0=0; if(y0<0)y0=0; if(x1>128)x1=128; if(y1>128)y1=128;
  for(int y=y0;y<y1;y++) for(int x=x0;x<x1;x++) g_fb[y*128+x]=c;
}
static int glyph(int c){ return c<32 ? 92+c : c-32; }
static int slen(const char *s){ int n=0; while(s[n]) n++; return n; }
static int text_w(const char *s, int sc){ int n=slen(s); return n ? n*6*sc-sc : 0; }
static void textn(int x,int y,const char *s,int n,u16 c,int sc){
  for(int i=0;i<n && s[i];i++,x+=6*sc){
    int ch=(u8)s[i]; if(ch>=0x60 && ch!='x') ch-=32;
    const u8 *g=g_font[glyph(ch)&127];
    for(int r=0;r<7;r++) for(int k=0;k<5;k++) if(g[r]&(16>>k)) rect(x+k*sc,y+r*sc,x+k*sc+sc,y+r*sc+sc,c);
  }
}
static void text(int x,int y,const char *s,u16 c,int sc){ textn(x,y,s,999,c,sc); }
static void ctext(int y,const char *s,u16 c,int sc){ text((128-text_w(s,sc))/2,y,s,c,sc); }
static void show(void){ hw_push(g_fb,128,0,0,128,128); }
static void utoa_(u32 v, char *s){ char t[12]; int n=0; do{ t[n++]=(char)('0'+v%10u); v/=10u; }while(v); for(int i=0;i<n;i++) s[i]=t[n-1-i]; s[n]=0; }

#define BG    RGB(16,16,40)
#define WHITE RGB(255,255,255)
#define GREY  RGB(140,140,160)
#define RED   RGB(255,80,80)
#define GOLD  RGB(255,200,60)
#define ORANGE RGB(255,170,40)
#define PINK  RGB(255,120,200)

static u16 fcol(u32 i){ return (u16)(i<4u ? g_cfg.field_col[i] : g_cfg.field_col2[i-4u]); }
static const char *fname(u32 i){ return i<4u ? (const char*)g_cfg.field_name[i] : (const char*)g_cfg.field_name2[i-4u]; }
static u32 fid(u32 i){ return i<4u ? g_cfg.field_id[i] : g_cfg.field_id2[i-4u]; }
static u32 nfields(void){ return g_cfg.nfields>NF_MAX ? NF_MAX : g_cfg.nfields; }

static void progress(const char *what, u32 n, u32 tot){
  rect(0,40,128,100,BG);
  ctext(44,what,WHITE,1);
  rect(14,62,114,72,GREY); rect(15,63,113,71,BG);
  u32 w = tot ? (98u*n)/tot : 0; if(w>98u) w=98u;
  rect(15,63,15+(int)w,71,RGB(40,120,255));
  hw_push(g_fb,128,0,40,128,100);
}

/* ---------------------------------------------------------------- Buttons */
static const signed char g_qd[16]={0,-1,1,0, 1,0,0,-1, -1,0,0,1, 0,1,-1,0};
static u32 g_enc_last; static int g_enc_acc;
static u32 g_btn_last;
#define SYST_CSR REG(0xE000E010u)
#define SYST_RVR REG(0xE000E014u)
#define SYST_CVR REG(0xE000E018u)
static u32 g_tick_last, g_ms, g_sub, g_enc_t;
static void clock_start(void){ SYST_RVR=0xFFFFFFu; SYST_CVR=0; SYST_CSR=5u; g_tick_last=SYST_CVR; }
static void clock_poll(void){
  u32 c=SYST_CVR, d=(g_tick_last-c)&0xFFFFFFu; g_tick_last=c;
  g_sub+=d; while(g_sub>=96000u){ g_sub-=96000u; g_ms++; }
}
#define K_A    10
#define K_OK    2
#define K_BACK  3
/* returns: wheel steps (+n/-n, 1..9), K_OK (B or wheel), K_BACK (C), K_A (A), 0 nothing.
 * The wheel counts once it has been still for 25 ms after a movement, so it does
 * not matter how many edges one detent produces. Fast turning gives several steps. */
static int g_step;
#ifdef HOST
static int input(void){ return host_input(&g_step); }
#else
static int input(void){
  hw_wdt_feed(); clock_poll();
  u32 n=REG(GPIO2) & 3u;
  if(n!=g_enc_last){ g_enc_acc+=g_qd[(g_enc_last<<2)|n]; g_enc_last=n; g_enc_t=g_ms; }
  else if(g_enc_acc && g_ms-g_enc_t>=25u){
    int a=g_enc_acc<0 ? -g_enc_acc : g_enc_acc; int st=(a+2)/4; if(st<1) st=1; if(st>9) st=9;
    g_step = g_enc_acc>0 ? st : -st; g_enc_acc=0; return 1; }
  u32 b=hw_buttons(), dn=b & ~g_btn_last; g_btn_last=b;
  if(dn & BTN_A) return K_A;
  if(dn & (BTN_B|BTN_WHEEL)) return K_OK;
  if(dn & BTN_C) return K_BACK;
  return 0;
}
#endif
#ifdef HOST
static void delay_loop(u32 n){ (void)n; }
#else
static void delay_loop(u32 n){ for(volatile u32 i=0;i<n;i++){ if(!(i&0xFFFu)) hw_wdt_feed(); } }
#endif
#ifdef HOST
static void wait_release(void){}
#else
static void wait_release(void){
  for(u32 k=0;k<400000u;k++){ if(!hw_buttons()) { delay_loop(200000u); break; } hw_wdt_feed(); }
  g_btn_last=hw_buttons();
}
#endif
static int yes_no(void){ wait_release(); for(;;){ int k=input(); if(k==K_OK) return 1; if(k==K_BACK) return 0; } }

/* ---------------------------------------------------------------- Compression
 * LZSS per page: flag byte for 8 entries (bit 1 = reference), reference =
 * 2 bytes ((distance-1)<<4 | (length-3)), distance 1..4096, length 3..17; if
 * the length field is 15, one more byte follows that is added to length 18 (up to 273).
 * Saves are two thirds zeros, hence the long lengths. */
#define LZ_MAX 273u
static u32 lz_hash(const u8 *p){ return ((u32)p[0]*1089u + (u32)p[1]*33u + p[2]) & 4095u; }
static u32 lz_pack(const u8 *in, u8 *out){
  for(u32 i=0;i<4096u;i++) g_head[i]=0xFFFFu;
  u32 i=0, o=0, fpos=0, bit=8;
  while(i<PAGE){
    if(bit==8){ fpos=o++; out[fpos]=0; bit=0; }
    u32 best=0, boff=0;
    if(i+3<=PAGE){
      u32 j=g_head[lz_hash(in+i)], tries=0;
      while(j!=0xFFFFu && tries<48u){
        u32 l=0; while(l<LZ_MAX && i+l<PAGE && in[j+l]==in[i+l]) l++;
        if(l>best){ best=l; boff=i-j; if(l==LZ_MAX) break; }
        j=g_prev[j]; tries++;
      }
    }
    u32 step = best>=3u ? best : 1u;
    if(best>=3u){
      u32 ln=best-3u;
      out[fpos]|=(u8)(1u<<bit);
      if(ln<15u){ u32 v=((boff-1u)<<4)|ln; out[o++]=(u8)v; out[o++]=(u8)(v>>8); }
      else { u32 v=((boff-1u)<<4)|15u; out[o++]=(u8)v; out[o++]=(u8)(v>>8); out[o++]=(u8)(ln-15u); }
    } else out[o++]=in[i];
    bit++;
    for(u32 k=0;k<step;k++,i++){
      if(i+3<=PAGE){ u32 h=lz_hash(in+i); g_prev[i]=g_head[h]; g_head[h]=(u16)i; }
    }
  }
  return o;
}
static int lz_unpack(const volatile u8 *in, u32 n, u8 *out){
  u32 i=0, o=0;
  while(o<PAGE){
    if(i>=n) return 1;
    u32 f=in[i++];
    for(u32 b=0;b<8u && o<PAGE;b++){
      if(f & (1u<<b)){
        if(i+2>n) return 2;
        u32 v=in[i] | ((u32)in[i+1]<<8); i+=2;
        u32 off=(v>>4)+1u, len=(v&15u)+3u;
        if((v&15u)==15u){ if(i>=n) return 2; len+=in[i++]; }
        if(off>o || o+len>PAGE) return 3;
        for(u32 k=0;k<len;k++,o++) out[o]=out[o-off];
      } else { if(i>=n) return 4; out[o++]=in[i++]; }
    }
  }
  return i==n ? 0 : 5;
}

/* ---------------------------------------------------------------- Flash */
/* Erase by programming an empty page: the program command also erases on the
 * device (confirmed), a plain erase has not been tested. */
static void erase_page(u32 a){ flash_prog4k(a, g_ff); }
static int page_ff(u32 a){ const volatile u32 *p=(const volatile u32*)(FLASH_BASE+a); for(u32 i=0;i<PAGE/4;i++) if(p[i]!=0xFFFFFFFFu) return 0; return 1; }
static u32 crc16arc(const u8 *b, u32 from, u32 to){
  u32 c=0;
  for(u32 i=from;i<to;i++){ c^=b[i]; for(u32 k=0;k<8u;k++) c = (c&1u) ? (c>>1)^0xA001u : c>>1; }
  return c & 0xFFFFu;
}
static void stamp(u8 *p){ u32 c=crc16arc(p,4,PAGE); p[0]=(u8)c; p[1]=(u8)(c>>8); p[2]=(u8)~c; p[3]=(u8)(~c>>8); }

/* Fields: index 0..nfields-1, ID g_cfg.field_id[i] */
static u32 cur_id(void){ return rd32(g_cfg.id_page); }
static int field_index(u32 id){ for(u32 i=0;i<nfields();i++) if(fid(i) && fid(i)==id) return (int)i; return -1; }
/* The active field is stored in the save (settings block), not in the ID:
 * verified on the emulator (fresh Land ROM with ID Water, field byte Land: egg in Land). */
static int field_now(void){
  flash_read(SET_A, g_page, PAGE);
  u32 c=crc16arc(g_page,4,PAGE);
  if((g_page[0]|(g_page[1]<<8))==c && (g_page[2]|(g_page[3]<<8))==(~c&0xFFFFu) && g_cfg.field_byte<PAGE){
    u32 f=g_page[g_cfg.field_byte];
    /* six fields: the set byte (0 or 1) picks the second three */
    if(g_cfg.six_off && g_cfg.six_off<PAGE && f<4u){
      u32 m=g_cfg.six_map[(g_page[g_cfg.six_off]==1u ? 4u : 0u) + f];
      if(m<nfields()) return (int)m;
    } else if(f<nfields()) return (int)f; }
  return field_index(cur_id());
}
static u32 slot_base(u32 i){
  if(g_cfg.slot_off2 && g_cfg.slot_n1 && i>=g_cfg.slot_n1) return g_cfg.slot_off2 + (i-g_cfg.slot_n1)*g_cfg.slot_size;
  return g_cfg.slot_off + i*g_cfg.slot_size; }
static u32 spp(void){ return g_cfg.slot_size/PAGE - 1u; }            /* pool pages per slot */
static u32 pool_n(void){ return g_cfg.nslots*spp(); }
static u32 pool_addr(u32 k){ return slot_base(k/spp()) + (k%spp()+1u)*PAGE; }
static int slot_pool(u32 b){ return rd32(b+20)==POOL_MAGIC; }
static int slot_valid(u32 i){
  u32 b=slot_base(i);
  if(rd32(b)!=SLOT_MAGIC || rd32(b+4)!=i+1u) return 0;   /* slot ID = field number + 1 */
  u32 np=rd32(b+8), by=rd32(b+12);
  if(np>NPAGE_MAX) return 0;
  if(!slot_pool(b)) return by<=g_cfg.slot_size-PAGE;
  u32 nd=rd32(b+24);
  if(nd>DPG_MAX || by>nd*PAGE) return 0;
  for(u32 k=0;k<nd;k++) if(rd16(b+offsetof(SlotHdr,dpg)+2u*k)>=pool_n()) return 0;
  return 1;
}
/* Read n bytes of the packed data of slot i from position pos. */
static void slot_get(u32 i, u32 pos, u32 n, u8 *dst){
  u32 b=slot_base(i); int pool=slot_pool(b);
  while(n){
    u32 pg=pos/PAGE, o=pos%PAGE, c=PAGE-o; if(c>n) c=n;
    u32 a = pool ? pool_addr(rd16(b+offsetof(SlotHdr,dpg)+2u*pg)) : b+PAGE+pg*PAGE;
    for(u32 k=0;k<c;k++) dst[k]=fl(a+o)[k];
    dst+=c; pos+=c; n-=c;
  }
}
/* Pool pages taken by the saved fields other than skip. */
static u8 g_used[512];
static void pool_mark(u32 skip){
  for(u32 k=0;k<sizeof g_used;k++) g_used[k]=0;
  for(u32 j=0;j<g_cfg.nslots;j++){
    if(j==skip || !slot_valid(j)) continue;
    u32 b=slot_base(j);
    if(slot_pool(b)){ u32 nd=rd32(b+24); for(u32 k=0;k<nd;k++){ u32 x=rd16(b+offsetof(SlotHdr,dpg)+2u*k); g_used[x>>3]|=(u8)(1u<<(x&7u)); } }
    else { u32 nd=(rd32(b+12)+PAGE-1u)/PAGE; for(u32 k=0;k<nd;k++){ u32 x=j*spp()+k; g_used[x>>3]|=(u8)(1u<<(x&7u)); } }
  }
}
/* Next free pool page for slot fi (its own pages first), marks it. 0xFFFF = full. */
static u32 pool_take(u32 fi){
  u32 n=pool_n(); if(n>sizeof(g_used)*8u) n=sizeof(g_used)*8u;
  for(u32 r=0;r<n;r++){
    u32 x=(fi*spp()+r)%n;
    if(!(g_used[x>>3]&(1u<<(x&7u)))){ g_used[x>>3]|=(u8)(1u<<(x&7u)); return x; }
  }
  return 0xFFFFu;
}
static void write_id(u32 id){
  u32 *w=(u32*)g_out;
  for(u32 i=0;i<PAGE/4;i++) w[i]=0xFFFFFFFFu;
  w[0]=id;
  ((u16*)g_out)[4084/2]=g_cfg.id_h0; ((u16*)g_out)[4086/2]=g_cfg.id_h1;
  w[4088/4]=g_cfg.id_c-id; w[4092/4]=~(g_cfg.id_c-id);
  flash_prog4k(g_cfg.id_page, g_out);
}
/* Factory settings page of a fresh field: zeros, the measured bytes, checksum. */
static void factory_page(u32 i, u8 *p){
  for(u32 k=0;k<PAGE;k++) p[k]=0;
  for(u32 k=0;k<16u && g_cfg.fac_common[k].off;k++) p[g_cfg.fac_common[k].off & 0xFFFu]=g_cfg.fac_common[k].val;
  const volatile Patch *fp = i<4u ? g_cfg.fac_field[i] : g_cfg.fac_field2[i-4u];
  for(u32 k=0;k<4u && fp[k].off;k++) p[fp[k].off & 0xFFFu]=fp[k].val;
  stamp(p);
}

static int stash(u32 fi){
  u32 b=slot_base(fi), cap=g_cfg.slot_size-PAGE;
  u32 tot=0; for(u32 a=g_cfg.user_start;a<g_cfg.user_end;a+=PAGE) if(!page_ff(a)) tot++;
  if(tot>NPAGE_MAX) return 10;
  erase_page(b);                            /* invalidate the header first */
  for(u32 i=0;i<sizeof(g_hdr)/4;i++) ((u32*)&g_hdr)[i]=0xFFFFFFFFu;
  pool_mark(fi);
  u32 np=0, by=0, sum=0, on=0, nd=0, done=0;
  (void)cap;
  for(u32 a=g_cfg.user_start;a<g_cfg.user_end;a+=PAGE){
    if(page_ff(a)) continue;
    flash_read(a, g_page, PAGE);
    u32 n=lz_pack(g_page, g_cmp);
    g_hdr.e[np].page=(u16)(a>>12); g_hdr.e[np].clen=(u16)n; np++;
    for(u32 k=0;k<n;k++){ g_out[on++]=g_cmp[k]; sum+=g_cmp[k];
      if(on==PAGE){ u32 x=pool_take(fi); if(x==0xFFFFu || nd>=DPG_MAX) return 11;
        flash_prog4k(pool_addr(x),g_out); g_hdr.dpg[nd++]=(u16)x; on=0; } }
    by+=n;
    progress("SAVING", ++done, tot*2u);
  }
  if(on){ u32 x=pool_take(fi); if(x==0xFFFFu || nd>=DPG_MAX) return 11;
    for(u32 k=on;k<PAGE;k++) g_out[k]=0xFF; flash_prog4k(pool_addr(x),g_out); g_hdr.dpg[nd++]=(u16)x; }
  /* check against the pages just written (the header is not there yet) */
  u32 off=0;
  for(u32 i=0;i<np;i++){
    u32 a=(u32)g_hdr.e[i].page<<12, n=g_hdr.e[i].clen;
    for(u32 k=0;k<n;k++){ u32 p=off+k; g_cmp[k]=fl(pool_addr(g_hdr.dpg[p/PAGE])+p%PAGE)[0]; }
    if(lz_unpack(g_cmp, n, g_page)) return 12;
    if(!flash_same(a, g_page, PAGE)) return 13;
    off+=n;
    progress("CHECKING", tot+i+1u, tot*2u);
  }
  g_hdr.magic=SLOT_MAGIC; g_hdr.field=fi+1u; g_hdr.npages=np; g_hdr.bytes=by; g_hdr.sum=sum;
  g_hdr.res[0]=POOL_MAGIC; g_hdr.res[1]=nd; g_hdr.res[2]=0;
  { const u8 *h=(const u8*)&g_hdr; for(u32 k=0;k<PAGE;k++) g_out[k]= k<sizeof(g_hdr) ? h[k] : 0xFF; }
  flash_prog4k(b, g_out);
  if(!slot_valid(fi)) return 14;
  return 0;
}

static int restore(u32 fi, int fresh){
  u32 b=slot_base(fi), np=0, off=0, ix=0;
  if(!fresh){
    np=rd32(b+8);
    u32 sum=0, by=rd32(b+12);
    for(u32 p=0;p<by;p+=PAGE){ u32 c=by-p<PAGE?by-p:PAGE; slot_get(fi,p,c,g_page); for(u32 k=0;k<c;k++) sum+=g_page[k]; }
    if(sum!=rd32(b+16)) return 20;
  }
  u32 tot=(g_cfg.user_end-g_cfg.user_start)/PAGE, done=0;
  /* Two passes: first everything except the settings block, then the block itself.
   * It holds the field; as long as it is old, the switch counts as not done and after
   * an interruption the menu offers to bring back the old save. */
  for(u32 pass=0;pass<2u;pass++){
    u32 o2=off, ix2=0;
    for(u32 a=g_cfg.user_start;a<g_cfg.user_end;a+=PAGE){
      int set = (a==SET_A || a==SET_B), want=0;
      if(fresh){
        if(set){ if(pass) { factory_page(fi,g_page); want=1; } else continue; }
      } else if(ix2<np && rd16(b+32+ix2*4)==(a>>12)){
        u32 n=rd16(b+34+ix2*4);
        if(set==(int)pass){ slot_get(fi,o2,n,g_cmp); if(lz_unpack(g_cmp, n, g_page)) return 21; want=1; }
        o2+=n; ix2++;
        if(!want) continue;
      } else if(set!=(int)pass) continue;
      if(want){ if(!flash_same(a,g_page,PAGE)) flash_prog4k(a,g_page); if(!flash_same(a,g_page,PAGE)) return 22; }
      else if(!page_ff(a)){ erase_page(a); if(!page_ff(a)) return 23; }
      if(!pass && !(++done & 7u)) progress("LOADING", done, tot);
    }
    if(!fresh && ix2!=np) return 24;
  }
  (void)ix;
  return 0;
}

/* ---------------------------------------------------------------- Screens */
static void leave(void){
  rect(0,0,128,128,0); show();
  hw_back_to_paradise();
}
static void error_screen(int code){
  rect(0,0,128,128,BG);
  ctext(30,"ERROR",RED,2);
  char s[12]="CODE 00"; s[5]=(char)('0'+code/10); s[6]=(char)('0'+code%10);
  ctext(56,s,WHITE,1);
  if(code==10||code==11) ctext(68,"SAVE TOO BIG",WHITE,1);   /* the field stays as it is */
  ctext(84,"B OR C: BACK",GREY,1);
  show(); wait_release();
  for(;;){ int k=input(); if(k==K_OK||k==K_BACK) leave(); }
}
static void message(const char *l1, const char *l2, const char *l3){
  rect(0,0,128,128,BG);
  ctext(34,l1,WHITE,1); ctext(48,l2,WHITE,1); ctext(62,l3,WHITE,1);
  ctext(100,"C BACK",GREY,1);
  show(); wait_release();
  for(;;){ int k=input(); if(k==K_OK||k==K_BACK) return; }
}
static void done_screen(const char *what, u16 col){
  rect(0,0,128,128,BG);
  ctext(40,"DONE",WHITE,2);
  ctext(66,what,col,1);
  show();
  delay_loop(6000000u);
  leave();
}

/* Program in the stub? 0 = none, 1 = DOOM, 2 = TamaEmu, 3 = other, 4 = T7Emu */
static u32 g_app;
static void app_detect(void){
  u32 src=rd32(g_cfg.stub_off+8u);
  if(src<0x10000000u || src>=0x11000000u){ g_app=0; return; }
  u32 off=src-0x10000000u+XIP_DEC;
  u32 m=rd32(off+0x140u);
  g_app = m==0x43584D50u ? 2u : m==0x37584D50u ? 4u : m==0x38584D50u ? 5u : 3u;
  for(u32 i=0x140u;i<0x400u;i+=4u) if(rd32(off+i)==0x434D4450u){ g_app=1u; break; }
}
static const char *const g_appname[6]={"","DOOM","TAMAEMU","PROGRAM","T7EMU","T18EMU"};
static void start_app(void){
  rect(0,0,128,128,0); show();
#ifdef HOST
  host_exit("APP");
#endif
  ((void(*)(void))(0x10000000u + g_cfg.stub_off - XIP_DEC + 1u))();
  for(;;){}
}
/* All programs: 0 = the stub's own (if any), then the ones of the settings block (version 5) */
static u32 g_napp, g_appsrc[APPS_MAX+1];
static char g_appnm[APPS_MAX+1][13];
static void apps_detect(void){
  g_napp=0;
  if(g_app){
    const volatile char *n=g_cfg.version>=5u && g_cfg.name0[0] ? g_cfg.name0 : g_appname[g_app];
    for(int k=0;k<12;k++){ g_appnm[0][k]=n[k]; if(!n[k]) break; } g_appnm[0][12]=0;
    g_appsrc[0]=0; g_napp=1;
  }
  /* the stub must have the entry at +0x0C (cpsid i; mov r4, r0) */
  if(g_cfg.version<5u || rd32(g_cfg.stub_off+0x0Cu)!=0x4604B672u) return;
  for(u32 i=0;i<g_cfg.napps && i<APPS_MAX;i++){
    u32 s=g_cfg.apps[i].src; if(s<0x10000000u || s>=0x11000000u) continue;
    for(int k=0;k<12;k++){ g_appnm[g_napp][k]=g_cfg.apps[i].name[k]; } g_appnm[g_napp][12]=0;
    g_appsrc[g_napp++]=s;
  }
}
static void start_app_i(u32 i){
  if(!g_appsrc[i]){ start_app(); return; }
  rect(0,0,128,128,0); show();
#ifdef HOST
  host_exit(g_appnm[i]);
#endif
  ((void(*)(u32))(0x10000000u + g_cfg.stub_off - XIP_DEC + 0x0Cu + 1u))(g_appsrc[i]);
  for(;;){}
}

/* Large menu entry: name in large text, state in small text below. */
static void big_entry(int y, int sel, const char *name, const char *st, u16 c, u16 stc){
  if(sel){ rect(3,y,125,y+28,c); rect(5,y+2,123,y+26,BG); }
  text(9,y+4,name,c,2);
  text(9,y+19,st,stc,1);
}

/* ---------------------------------------------------------------- Fields */
static int g_cur;                     /* index of the active field, -1 = not a field in the list */

static void field_switch(u32 tgt, int has){
  rect(0,0,128,128,BG); ctext(8,"PLEASE WAIT",WHITE,1); ctext(20,"DO NOT SWITCH OFF",GREY,1); show();
  int e=stash((u32)g_cur);
  if(e){ erase_page(slot_base((u32)g_cur)); error_screen(e); }
  e=restore(tgt,!has);
  if(e) error_screen(e);
  if(fid(tgt)){ write_id(fid(tgt)); if(cur_id()!=fid(tgt)) error_screen(30); }
  if(field_now()!=(int)tgt) error_screen(31);
  erase_page(slot_base(tgt));
  done_screen(fname(tgt), fcol(tgt));
}
static int field_confirm(u32 tgt, int has){
  rect(0,0,128,128,BG);
  ctext(14,"SWITCH TO",WHITE,1);
  ctext(30,fname(tgt),fcol(tgt),2);
  ctext(54, has ? "SAVED GAME" : "FIELD STARTS", WHITE,1);
  ctext(66, has ? "WILL BE LOADED" : "FROM SCRATCH", WHITE,1);
  ctext(92,"B = YES",WHITE,1);
  ctext(106,"C = NO",GREY,1);
  show();
  return yes_no();
}
static void big_entry(int y, int sel, const char *name, const char *st, u16 c, u16 stc);
/* Program list (more than one program). Returns on C unless top. */
static void apps_menu(int top){
  u32 n=g_napp, sel=0;
  for(;;){
    rect(0,0,128,128,BG);
    int y0 = n>=4u ? 1 : 12;
    if(n<4u) ctext(2,"PROGRAMS",WHITE,1);
    for(u32 i=0;i<n;i++){
      if(n>4u){
        int y=2+(int)i*16;
        if(i==sel){ rect(1,y,127,y+15,ORANGE); rect(2,y+1,126,y+14,BG); }
        text(5,y+4,g_appnm[i],ORANGE,1);
        text(123-text_w("START",1),y+4,"START",GREY,1);
      } else big_entry(y0+(int)i*29, i==sel, g_appnm[i], "START", ORANGE, GREY);
    }
    ctext(119,"B OK   C BACK",GREY,1);
    show(); wait_release();
    for(;;){
      int k=input();
      if(k==1){ sel=(u32)((int)sel + (g_step>0?1:-1) + (int)n) % n; break; }
      if(k==K_A){ sel=(sel+1u)%n; break; }
      if(k==K_BACK){ if(top) leave(); return; }
      if(k==K_OK) start_app_i(sel);
    }
  }
}
/* Field list. with_app: program as the last entry (when there is no main menu).
 * Returns when C is pressed and there is a main menu. */
static void field_menu(int with_app, int top){
  int has[NF_MAX]; u32 nf=nfields(); for(u32 i=0;i<nf;i++) has[i]=slot_valid(i);
  u32 na = with_app ? (g_napp>1u ? 1u : g_napp) : 0u;
  u32 n = nf + na;
  u32 sel = na ? nf : (u32)g_cur;
  for(;;){
    rect(0,0,128,128,BG);
    int y0 = n>=4u ? 1 : 12;
    if(n<4u) ctext(2, top ? "PARAMIX" : "FIELD", WHITE,1);
    for(u32 i=0;i<n;i++){
      const char *nm = i<nf ? fname(i) : g_napp>1u ? "PROGRAMS" : g_appnm[0];
      const char *st = i>=nf ? (g_napp>1u ? "CHOOSE" : "START") : ((int)i==g_cur) ? "ACTIVE" : has[i] ? "SAVED" : "NEW";
      u16 c = i<nf ? fcol(i) : ORANGE, stc = (i<nf && (int)i==g_cur) ? WHITE : GREY;
      if(n>4u){                       /* six fields: compact rows */
        int y=2+(int)i*16;
        if(i==sel){ rect(1,y,127,y+15,c); rect(2,y+1,126,y+14,BG); }
        text(5,y+4,nm,c,1);
        text(123-text_w(st,1),y+4,st,stc,1);
      } else big_entry(y0+(int)i*29, i==sel, nm, st, c, stc);
    }
    ctext(119,"B OK   C BACK",GREY,1);
    show(); wait_release();
    for(;;){
      int k=input();
      if(k==1){ sel=(u32)((int)sel + (g_step>0?1:-1) + (int)n) % n; break; }
      if(k==K_A){ sel=(sel+1u)%n; break; }
      if(k==K_BACK){ if(top) leave(); return; }
      if(k==K_OK){
        if(sel>=nf){ if(g_napp>1u) apps_menu(0); else start_app_i(0); break; }
        if((int)sel==g_cur){ if(top) leave(); return; }
        if(field_confirm(sel,has[sel])) field_switch(sel,has[sel]);
        break;
      }
    }
  }
}

/* ---------------------------------------------------------------- Items and Tamapoints */
static int set_load(void){
  flash_read(SET_A, g_set, PAGE);
  u32 c=crc16arc(g_set,4,PAGE);
  if((g_set[0]|(g_set[1]<<8))!=c || (g_set[2]|(g_set[3]<<8))!=(~c&0xFFFFu)) return 1;
  u32 y=g_set[0x1c]|(g_set[0x1d]<<8), m=g_set[0x1e]|(g_set[0x1f]<<8), d=g_set[0x20]|(g_set[0x21]<<8);
  if(y<2020u || y>2035u || m<1u || m>12u || d<1u || d>31u) return 2;   /* nothing saved yet */
  if(g_cfg.list_off+1280u>PAGE || g_cfg.count_off+320u>PAGE || g_cfg.money_off+4u>PAGE) return 3;
  return 0;
}
static int set_check_msg(void){
  int e=set_load();
  if(e==1){ message("SAVE DATA","IS DAMAGED","NOTHING CHANGED"); return 1; }
  if(e==2){ message("NO SAVE YET","PLAY A LITTLE","AND TRY AGAIN"); return 1; }
  if(e){ message("SAVE FORMAT","UNKNOWN",""); return 1; }
  return 0;
}
static void set_write(const char *what){
  rect(0,0,128,128,BG); ctext(40,"SAVING",WHITE,1); ctext(54,"DO NOT SWITCH OFF",GREY,1); show();
  stamp(g_set);
  flash_prog4k(SET_A, g_set);
  flash_prog4k(SET_B, g_set);
  if(!flash_same(SET_A,g_set,PAGE) || !flash_same(SET_B,g_set,PAGE)) error_screen(40);
  done_screen(what, GOLD);
}
static u32 money_get(void){ u32 o=g_cfg.money_off; return g_set[o]|(g_set[o+1]<<8)|(g_set[o+2]<<16)|((u32)g_set[o+3]<<24); }
static void money_put(u32 v){ u32 o=g_cfg.money_off; g_set[o]=(u8)v; g_set[o+1]=(u8)(v>>8); g_set[o+2]=(u8)(v>>16); g_set[o+3]=(u8)(v>>24); }
static u32 list_id(u32 i){ u32 o=g_cfg.list_off+2u*i; return g_set[o]|(g_set[o+1]<<8); }
static void list_set(u32 i, u32 id){ u32 o=g_cfg.list_off+2u*i; g_set[o]=(u8)id; g_set[o+1]=(u8)(id>>8); }
static u32 count_get(u32 i){ u8 b=g_set[g_cfg.count_off+i/2u]; return (i&1u) ? (b>>4) : (b&15u); }
static void count_set(u32 i, u32 v){ u32 o=g_cfg.count_off+i/2u; u8 b=g_set[o]; g_set[o]=(u8)((i&1u) ? ((b&0x0F)|(v<<4)) : ((b&0xF0)|(v&15u))); }
static u32 list_len(void){ u32 n=0; while(n<640u && list_id(n)) n++; return n; }

/* Catalog in flash: 28 bytes per entry, sorted by ID */
static int cat_find(u32 id){
  int lo=0, hi=(int)g_cfg.cat_n-1;
  while(lo<=hi){ int m=(lo+hi)/2; u32 v=rd16(g_cfg.cat_off+8u+28u*(u32)m); if(v==id) return m; if(v<id) lo=m+1; else hi=m-1; }
  return -1;
}
static int cat_fixed(u32 id){ int k=cat_find(id); return k>=0 && fl(g_cfg.cat_off+8u+28u*(u32)k+2u)[0]; }
static char g_nm[24];
static const char *item_name(u32 id){
  int k=cat_find(id);
  if(k>=0){ u32 o=g_cfg.cat_off+8u+28u*(u32)k; u32 n=fl(o+3)[0]; if(n>20u) n=20u; if(n){ for(u32 i=0;i<n;i++) g_nm[i]=(char)fl(o+8+i)[0]; g_nm[n]=0; return g_nm; } }
  char d[8]; utoa_(id,d); g_nm[0]='I'; g_nm[1]='T'; g_nm[2]='E'; g_nm[3]='M'; g_nm[4]=' '; int i=0; while(d[i]){ g_nm[5+i]=d[i]; i++; } g_nm[5+i]=0;
  return g_nm;
}
/* Item picture from the ROM image table (same format as in the Studio: core.js spriteDecode).
 * First palette, transparent pixels stay background.
 * The image entry is first copied to RAM with aligned 32-bit reads (the way the flash
 * app reads whole dumps on the device) and checked against the sum the Studio stored in
 * the catalog. Only a matching copy is decoded, so bad data can never hang the menu.
 * A picture that cannot be drawn leaves a short code in the box. */
#define PIC_MAX 12288u
static u8 g_spr[16384];
static u32 g_picw[PIC_MAX/4u + 1u];
static u8 *const g_pic = (u8*)g_picw;
static void copy_words(u32 a, u32 n){            /* flash a (4-aligned) -> g_pic[0..n) */
  const volatile u32 *s=(const volatile u32*)(FLASH_BASE + a);
  for(u32 i=0;i<(n+3u)/4u;i++) g_picw[i]=s[i];
}
static u32 pr16(u32 o){ return g_pic[o]|(g_pic[o+1]<<8); }
static u32 pr32(u32 o){ return g_pic[o]|(g_pic[o+1]<<8)|(g_pic[o+2]<<16)|((u32)g_pic[o+3]<<24); }
static u32 spr_rle(u32 src, u32 ln, int x, int word, u8 *out, u32 cap){
  u32 i=0, o=0;
#define SB(k) ((u8)(g_pic[src+(k)] ^ (x?0x53u:0u)))
  if(!word){
    while(i<ln && o<cap){ u32 c=SB(i); i++; if(!c) break;
      if(c&0x80u){ u32 n=c&0x7Fu; for(u32 k=0;k<n && o<cap && i+k<ln;k++) out[o++]=SB(i+k); i+=n; }
      else { u8 v=SB(i); i++; for(u32 k=0;k<c && o<cap;k++) out[o++]=v; } }
  } else {
    while(i+4<=ln && o<cap){ u32 c=SB(i)|(SB(i+1)<<8)|(SB(i+2)<<16)|((u32)SB(i+3)<<24); i+=4; if(!c) break;
      if(c&0x80000000u){ u32 n=(c&0x0FFFFFFFu)*4u; for(u32 k=0;k<n && o<cap && i+k<ln;k++) out[o++]=SB(i+k); i+=n; }
      else { for(u32 r=0;r<c && o<cap;r++) for(u32 k=0;k<4u && o<cap;k++) out[o++]=SB(i+k); i+=4; } }
  }
#undef SB
  return o;
}
static void pic_code(int cy, const char *c){ ctext(cy-3, c, GREY, 1); }
static void hex4(char *t, u32 v){ const char *h="0123456789ABCDEF"; for(int i=0;i<4;i++) t[i]=h[(v>>(12-4*i))&15u]; }
static void pic_hex(int cy, u32 a, u32 b){
  char t[12]; hex4(t,a); t[4]=' '; hex4(t+5,b); t[9]=0;
  ctext(cy+8, t, GREY, 1);
}
static void copy_bytes(u32 a, u32 n){ for(u32 i=0;i<n;i++) g_pic[i]=fl(a+i)[0]; }
/* Read through the cached XIP window: point it at the picture, copy, point it back.
 * The firmware's own code is not running (the menu runs from PRAM). */
static void copy_xip(u32 a, u32 n){
#ifdef HOST
  copy_words(a, n);
#else
  u32 base=REG(0x4002F004u), ctrl=REG(0x4002F000u);
  REG(0x4002F004u)=0x60000000u | (a & 0x00FFF000u);
  REG(0x4002F000u)=3u;
  const volatile u32 *s=(const volatile u32*)(0x10000000u + (a & 0xFFFu));
  for(u32 i=0;i<(n+3u)/4u;i++) g_picw[i]=s[i];
  REG(0x4002F004u)=base; REG(0x4002F000u)=ctrl;
#endif
}
static u32 pic_sum(u32 n){ u32 s=0; for(u32 i=0;i<n;i++) s+=g_pic[i]; s&=0xFFFFu; return s ? s : 1u; }
#define EXT_MAGIC 0x54584550u   /* "PEXT": per item address, length, sum, first word of the picture */
static void draw_item_pic(u32 id, int cx, int cy, int box){
  int k=cat_find(id);
  if(!g_cfg.pack_off){ pic_code(cy,"NO PICS"); return; }
  if(k<0){ pic_code(cy,"P1"); return; }
  u32 co=g_cfg.cat_off+8u+28u*(u32)k, pic=rd16(co+4u), want=rd16(co+6u);
  u32 xo=g_cfg.cat_off+8u+28u*g_cfg.cat_n, e, end=0;
  if(rd32(xo)==EXT_MAGIC){
    /* Studio 1.0.4 and later: the picture's address and size are in the catalog */
    u32 q=xo+4u+12u*(u32)k;
    e=rd32(q); end=rd16(q+4u); want=rd16(q+6u);
    if(!e || !end || end>PIC_MAX){ pic_code(cy,"P7"); return; }
    /* three ways to read, the first whose sum matches wins:
       W = 32-bit reads in the 0x60000000 window, B = byte reads there,
       X = cached XIP window at 0x10000000 pointed at the picture (menu runs from PRAM) */
    u32 sW, sB=0, sX=0;
    copy_words(e, end); sW=pic_sum(end);
    if(sW!=want){ copy_bytes(e, end); sB=pic_sum(end); }
    if(sW!=want && sB!=want){ copy_xip(e, end); sX=pic_sum(end); }
    if(sW!=want && sB!=want && sX!=want){
      char t[12];
      ctext(cy-12,"RD",GREY,1);
      hex4(t,sW); t[4]=' '; hex4(t+5,want); t[9]=0; ctext(cy-2,t,GREY,1);
      hex4(t,sB); t[4]=' '; hex4(t+5,sX); t[9]=0; ctext(cy+8,t,GREY,1);
      copy_words(e,4u); hex4(t,pr16(0)); t[4]=' '; hex4(t+5,rd32(q+8u)&0xFFFFu); t[9]=0; ctext(cy+18,t,GREY,1);
      return;
    }
  } else {
    u32 P=g_cfg.pack_off, n=rd32(P)/4u; if(pic>=n){ pic_code(cy,"P2"); return; }
    e=P+rd32(P+4u*pic);
    if(e&3u){ pic_code(cy,"P6"); return; }
  }
  if(!end) copy_words(e, 64u);                                   /* header */
  u32 flags=g_pic[4], bpp=g_pic[5], sw=g_pic[8], sh=g_pic[9], iw=g_pic[12], ih=g_pic[13];
  u32 tr=pr16(16), po=pr16(18), px=pr16(20);
  if(bpp>3u || !sw || !sh){ pic_code(cy,"P3"); return; }
  if(!iw) iw=1; if(!ih) ih=1;
  u32 cnt=iw*ih, bits=1u<<bpp, mask=(1u<<bits)-1u, npx=sw*sh, need=(npx*bits+7u)/8u;
  if(need>sizeof(g_spr)){ pic_code(cy,"P4"); return; }
  if(!end){
    u32 hdr=px+8u*cnt; if(hdr>PIC_MAX || po+2u*(mask+1u)>PIC_MAX){ pic_code(cy,"P5"); return; }
    copy_words(e, hdr);                                          /* header and sub-image table */
    end=hdr;
    for(u32 c=0;c<cnt;c++){
      u32 x;
      if(flags & 0x60u){ u32 off=pr32(px+8u*c)&0x7FFFFFFFu, ln=pr32(px+8u*c+4u); x=px+off+((pr32(px+8u*c)&0x80000000u)?need:ln); }
      else x=px+(c+1u)*need;
      if(x>end) end=x;
    }
    u32 pend=po+2u*(mask+1u); if(pend>end) end=pend;
    if(end>PIC_MAX){ pic_code(cy,"P5"); return; }
    copy_words(e, end);                                          /* whole entry */
    if(want && pic_sum(end)!=want){ pic_code(cy,"RD"); pic_hex(cy,pic_sum(end),want); return; }
  }
  /* every offset used below must lie inside the copied entry */
  if(px+8u*cnt>end || po+2u*(mask+1u)>end){ pic_code(cy,"P5"); return; }
  int W=(int)(sw*iw), H=(int)(sh*ih), sc=1, dn=1;
  while(W/dn>box || H/dn>box) dn*=2;
  if(dn==1 && W*2<=box && H*2<=box) sc=2;
  int x0=cx-(W/dn*sc)/2, y0=cy-(H/dn*sc)/2;
  int xr=(flags&0x80u)!=0;
  for(u32 c=0;c<cnt;c++){
    if(flags & 0x60u){
      u32 off=pr32(px+8u*c), ln=pr32(px+8u*c+4u), unc=off&0x80000000u; off&=0x7FFFFFFFu;
      if(unc){ for(u32 q=0;q<need;q++) g_spr[q]=(u8)(g_pic[px+off+q]^(xr?0x53u:0u)); }
      else { u32 got=spr_rle(px+off, ln, xr, (flags&0x40u)!=0, g_spr, need); for(u32 q=got;q<need;q++) g_spr[q]=0; }
    } else { for(u32 q=0;q<need;q++) g_spr[q]=(u8)(g_pic[px+c*need+q]^(xr?0x53u:0u)); }
    int gx=(int)(c%iw)*(int)sw, gy=(int)(c/iw)*(int)sh;
    for(u32 q=0;q<npx;q++){
      int x=gx+(int)(q%sw), y=gy+(int)(q/sw);
      if((x%dn) || (y%dn)) continue;
      u32 bit=q*bits, v=(g_spr[bit>>3]>>(bit&7u))&mask;
      if((flags&4u) && v==tr) continue;
      u16 col=(u16)pr16(po+2u*v);
      int X=x0+x/dn*sc, Y=y0+y/dn*sc;
      rect(X,Y,X+sc,Y+sc,col);
    }
  }
}
static const char *group_name(u32 id){ switch(id/1000u){ case 5: return "FOOD"; case 6: return "SNACKS"; case 7: return "TOYS"; case 8: return "GOODS"; default: return "OTHER"; } }

/* Draw a list: title, 10 rows from y=12, footer. */
static void list_draw(const char *title, u32 n, u32 sel, u32 top, int (*row)(u32 i, char *name, char *right, u16 *col), const char *foot){
  rect(0,0,128,128,BG);
  ctext(2,title,WHITE,1);
  for(u32 r=0;r<10u && top+r<n;r++){
    u32 i=top+r; int y=12+(int)r*10;
    char nm[24], rt[8]; u16 c=WHITE; nm[0]=0; rt[0]=0;
    row(i,nm,rt,&c);
    if(i==sel) rect(0,y-1,128,y+8,RGB(60,60,110));
    int rw=text_w(rt,1);
    int maxc = (122-rw)/6;
    textn(3,y,nm,maxc,c,1);
    text(125-rw,y,rt,GREY,1);
  }
  if(n>10u){ int h=100*10/(int)n; if(h<6) h=6; int y=12+(int)(100-h)*(int)top/(int)(n-10u); rect(126,y,128,y+h,GREY); }
  ctext(118,foot,GREY,1);
  show();
}

/* Bag list: index -> slot (only items that are not fixed), entry 0 = add */
static u16 g_bag[640]; static u32 g_bagn;
static void bag_build(void){ g_bagn=0; u32 n=list_len(); for(u32 i=0;i<n;i++) if(!cat_fixed(list_id(i))) g_bag[g_bagn++]=(u16)i; }
static int bag_row(u32 i, char *name, char *right, u16 *col){
  if(i==0){ const char *s="+ ADD ITEM"; int k=0; while(s[k]){ name[k]=s[k]; k++; } name[k]=0; *col=GOLD; return 0; }
  u32 slot=g_bag[i-1]; const char *s=item_name(list_id(slot)); int k=0; while(s[k] && k<22){ name[k]=s[k]; k++; } name[k]=0;
  right[0]='x'; utoa_(count_get(slot), right+1);
  return 0;
}
static u16 g_cat[512]; static u32 g_catn;
static void cat_build(void){ g_catn=0; for(u32 k=0;k<g_cfg.cat_n && g_catn<512u;k++){ u32 o=g_cfg.cat_off+8u+28u*k; if(!fl(o+2)[0]) g_cat[g_catn++]=(u16)rd16(o); } }
static int cat_row(u32 i, char *name, char *right, u16 *col){
  u32 id=g_cat[i]; const char *s=item_name(id); int k=0; while(s[k] && k<22){ name[k]=s[k]; k++; } name[k]=0;
  u32 n=list_len(); for(u32 j=0;j<n;j++) if(list_id(j)==id){ right[0]='x'; utoa_(count_get(j), right+1); *col=GREY; break; }
  return 0;
}
/* Set the count (1..15). Returns the new count, or 0 if cancelled. */
static u32 count_edit(u32 id, u32 v){
  if(v<1u) v=1u; if(v>15u) v=15u;
  for(;;){
    rect(0,0,128,128,BG);
    const char *nm=item_name(id); int L=slen(nm);
    if(L<=20) ctext(4,nm,WHITE,1); else { textn(4,2,nm,20,WHITE,1); textn(4,11,nm+20,20,WHITE,1); }
    rect(30,20,98,84,RGB(40,40,70));
    draw_item_pic(id,64,52,64);
    char s[8]; s[0]='x'; utoa_(v,s+1); ctext(90,s,GOLD,2);
    ctext(108,"WHEEL: COUNT",GREY,1);
    ctext(119,"B OK   C BACK",GREY,1);
    show(); wait_release();
    for(;;){
      int k=input();
      if(k==1){ int nv=(int)v+(g_step>0?1:-1); if(nv<1) nv=1; if(nv>15) nv=15; v=(u32)nv; break; }
      if(k==K_A){ v = v>=15u ? 1u : v+1u; break; }
      if(k==K_OK) return v;
      if(k==K_BACK) return 0;
    }
  }
}
static int g_dirty;
static void cat_pick(void){
  cat_build();
  if(!g_catn){ message("NO ITEM LIST","IN THIS ROM",""); return; }
  u32 sel=0, top=0;
  for(;;){
    if(sel<top) top=sel; if(sel>=top+10u) top=sel-9u;
    char t[24]; const char *g=group_name(g_cat[sel]); int k=0; const char *a="ADD: "; while(a[k]){ t[k]=a[k]; k++; } int j=0; while(g[j]){ t[k++]=g[j++]; } t[k]=0;
    list_draw(t, g_catn, sel, top, cat_row, "A NEXT GROUP  C BACK");
    wait_release();
    for(;;){
      int c=input();
      if(c==1){ int s=(int)sel+g_step; if(s<0) s=0; if(s>=(int)g_catn) s=(int)g_catn-1; sel=(u32)s; break; }
      if(c==K_A){ u32 gr=g_cat[sel]/1000u; u32 s=sel; while(s<g_catn && g_cat[s]/1000u==gr) s++; sel = s<g_catn ? s : 0u; break; }
      if(c==K_BACK) return;
      if(c==K_OK){
        u32 id=g_cat[sel], n=list_len(), slot=n;
        for(u32 i=0;i<n;i++) if(list_id(i)==id){ slot=i; break; }
        if(slot>=640u){ message("BAG IS FULL","",""); return; }
        u32 v=count_edit(id, slot<n ? count_get(slot) : 1u);
        if(v){ list_set(slot,id); count_set(slot,v); g_dirty=1; return; }
        break;
      }
    }
  }
}
static void items_menu(void){
  if(set_check_msg()) return;
  g_dirty=0;
  u32 sel=0, top=0;
  for(;;){
    bag_build();
    u32 n=g_bagn+1u; if(sel>=n) sel=n-1u;
    if(sel<top) top=sel; if(sel>=top+10u) top=sel-9u;
    list_draw("ITEMS", n, sel, top, bag_row, "B EDIT   C BACK");
    wait_release();
    for(;;){
      int c=input();
      if(c==1){ int s=(int)sel+g_step; if(s<0) s=0; if(s>=(int)n) s=(int)n-1; sel=(u32)s; break; }
      if(c==K_A){ sel=(sel+1u)%n; break; }
      if(c==K_OK){
        if(sel==0){ cat_pick(); }
        else { u32 slot=g_bag[sel-1]; u32 v=count_edit(list_id(slot), count_get(slot)); if(v && v!=count_get(slot)){ count_set(slot,v); g_dirty=1; } }
        break;
      }
      if(c==K_BACK){
        if(!g_dirty) return;
        rect(0,0,128,128,BG); ctext(34,"SAVE CHANGES?",WHITE,1); ctext(60,"THE TAMA RESTARTS",GREY,1);
        ctext(92,"B = YES",WHITE,1); ctext(106,"C = NO",GREY,1); show();
        if(yes_no()) set_write("ITEMS SAVED");
        return;
      }
    }
  }
}
static void points_menu(void){
  if(set_check_msg()) return;
  u32 v=money_get(); if(v>99999u) v=99999u;
  u8 dg[5]; { u32 t=v; for(int i=4;i>=0;i--){ dg[i]=(u8)(t%10u); t/=10u; } }
  int pos=0;
  for(;;){
    rect(0,0,128,128,BG);
    ctext(10,"TAMAPOINTS",GOLD,1);
    for(int i=0;i<5;i++){
      int x=14+i*21; char s[2]={(char)('0'+dg[i]),0};
      if(i==pos) rect(x-2,40,x+17,68,RGB(60,60,110));
      text(x,43,s,i==pos?GOLD:WHITE,3);
    }
    ctext(80,"WHEEL: DIGIT",GREY,1);
    ctext(92,"B NEXT  A BACK",GREY,1);
    ctext(106,"C CANCEL",GREY,1);
    show(); wait_release();
    for(;;){
      int k=input();
      if(k==1){ dg[pos]=(u8)((dg[pos]+10+(g_step>0?1:-1))%10); break; }
      if(k==K_A){ if(pos>0) pos--; break; }
      if(k==K_BACK) return;
      if(k==K_OK){
        if(pos<4){ pos++; break; }
        u32 nv=0; for(int i=0;i<5;i++) nv=nv*10u+dg[i];
        if(nv==money_get()) return;
        rect(0,0,128,128,BG); ctext(24,"SET TAMAPOINTS TO",WHITE,1);
        char s[8]; utoa_(nv,s); ctext(44,s,GOLD,2); ctext(70,"THE TAMA RESTARTS",GREY,1);
        ctext(92,"B = YES",WHITE,1); ctext(106,"C = NO",GREY,1); show();
        if(yes_no()){ money_put(nv); set_write("TAMAPOINTS SAVED"); }
        return;
      }
    }
  }
}

/* ---------------------------------------------------------------- Main menu */
static void main_menu(void){
  int fields = (g_cfg.flags & F_FIELDS) && g_cur>=0 && g_cfg.nfields>=2u;
  u32 kinds[4], n=0;                   /* 0 field, 1 items, 2 Tamapoints, 3 program */
  if(fields) kinds[n++]=0;
  kinds[n++]=1; kinds[n++]=2;
  if(g_napp) kinds[n++]=3;
  u32 sel = g_napp ? n-1u : 0u;
  for(;;){
    int ok=set_load()==0;
    rect(0,0,128,128,BG);
    int y0 = n>=4u ? 1 : 12;
    if(n<4u) ctext(2,"PARAMIX",WHITE,1);
    for(u32 i=0;i<n;i++){
      int y=y0+(int)i*29; char st[16];
      switch(kinds[i]){
        case 0: big_entry(y,i==sel,"FIELD",fname((u32)g_cur),fcol((u32)g_cur),WHITE); break;
        case 1: { u32 c=0; if(ok){ u32 L=list_len(); for(u32 j=0;j<L;j++) if(!cat_fixed(list_id(j))) c++; utoa_(c,st); int k=slen(st); const char *a=" IN BAG"; for(int q=0;a[q];q++) st[k++]=a[q]; st[k]=0; } else { st[0]='-'; st[1]=0; }
                  big_entry(y,i==sel,"ITEMS",st,PINK,GREY); } break;
        case 2: if(ok) utoa_(money_get(),st); else { st[0]='-'; st[1]=0; }
                big_entry(y,i==sel,"POINTS",st,GOLD,GREY); break;
        default: big_entry(y,i==sel,g_napp>1u ? "PROGRAMS" : g_appnm[0],g_napp>1u ? "CHOOSE" : "START",ORANGE,GREY); break;
      }
    }
    ctext(119,"B OK   C BACK",GREY,1);
    show(); wait_release();
    for(;;){
      int k=input();
      if(k==1){ sel=(u32)((int)sel + (g_step>0?1:-1) + (int)n) % n; break; }
      if(k==K_A){ sel=(sel+1u)%n; break; }
      if(k==K_BACK) leave();
      if(k==K_OK){
        switch(kinds[sel]){ case 0: field_menu(0,0); break; case 1: items_menu(); break; case 2: points_menu(); break; default: if(g_napp>1u) apps_menu(0); else start_app_i(0); }
        break;
      }
    }
  }
}

static void run(void){
  rect(0,0,128,128,BG); show();
#ifndef HOST
  REG(GPIO2+0x04) &= ~3u; g_enc_last=REG(GPIO2) & 3u;
  clock_start();
#endif
  app_detect();
  apps_detect();
  g_cur = field_now();
  int fields = (g_cfg.flags & F_FIELDS) && g_cur>=0 && g_cfg.nfields>=2u;
  /* interrupted switch? */
  if(fields && slot_valid((u32)g_cur)){
    rect(0,0,128,128,BG);
    ctext(14,"SWITCH WAS",RED,1); ctext(26,"INTERRUPTED",RED,1);
    ctext(48,"RESTORE SAVE OF",WHITE,1); ctext(60,fname((u32)g_cur),fcol((u32)g_cur),1); ctext(72,"?",WHITE,1);
    ctext(96,"B = YES",WHITE,1); ctext(108,"C = NO",GREY,1);
    show();
    if(!yes_no()) leave();
    rect(0,0,128,128,BG); show();
    int e=restore((u32)g_cur,0); if(e) error_screen(e);
    erase_page(slot_base((u32)g_cur)); done_screen(fname((u32)g_cur), fcol((u32)g_cur));
  }
  if(g_cfg.flags & F_BAG) main_menu();
  if(fields) field_menu(1,1);
  if(g_napp>1u) apps_menu(1);
  if(g_napp) start_app_i(0);
  leave();
}

#ifdef HOST
void host_main(void){
  for(u32 i=0;i<PAGE;i++) g_ff[i]=0xFF;
  for(u32 p=0x90000u;p<0x110000u;p+=0x1000u) if(rd32(p+0x140u)==FELD_MAGIC){
    u8 *d=(u8*)&g_cfg; for(u32 k=0;k<sizeof(Cfg);k++) d[k]=fl(p+0x140u+k)[0]; break; }
  run();
}
#else
extern u32 __bss_start, __bss_end;
void Reset_Handler(void){
  __asm__ volatile("cpsid i");
  for(u32* p=&__bss_start; p<&__bss_end; p++) *p=0;
  for(u32 i=0;i<PAGE;i++) g_ff[i]=0xFF;
  hw_init();
  run();
  for(;;){}
}
__attribute__((naked)) static void Default_Handler(void){ __asm__ volatile("1: b 1b\n"); }
__attribute__((naked)) static void Irq_Off(void){ __asm__ volatile("cpsid i\n bx lr\n"); }
__attribute__((section(".vectors"), used))
const void* const g_vectors[16+64] = { (void*)0x1801F000u, (void*)Reset_Handler,
  [2 ... 15] = (void*)Default_Handler, [11] = (void*)Irq_Off, [14] = (void*)Irq_Off, [15] = (void*)Irq_Off,
  [16 ... 79] = (void*)Irq_Off };
#endif
