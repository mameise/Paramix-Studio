/* TPRV v1: 24-byte header then 72-byte records. Only references/checksums
 * into the existing asset archive; no copied sprite library, firmware calls,
 * initialized game RAM, or flash writes. Uses the item decoder's RAM buffers. */
#define TP_MAGIC 0x56525054u
static int tp_range(u32 a,u32 n){ return a>=0x111000u && n && n<=0x1000000u && a<=0x1000000u-n; }
static void tp_align(u32 skip,u32 n){ for(u32 i=0;i<n;i++) g_pic[i]=g_pic[i+skip]; }
static int tp_load(u32 a,u32 n,u32 sum){
  if(!tp_range(a,n) || n>PIC_MAX-4u) return 0;
  u32 skip=a&3u, base=a&~3u;
  copy_words(base,n+skip); tp_align(skip,n);
  if(pic_sum(n)==sum) return 1;
  copy_bytes(a,n); if(pic_sum(n)==sum) return 1;
  copy_xip(base,n+skip); tp_align(skip,n); return pic_sum(n)==sum;
}
static u32 tp_header(void){
  if(!g_cfg.cat_off || g_cfg.cat_n>2048u || g_cfg.cat_off>0x1000000u-(12u+40u*g_cfg.cat_n+24u)) return 0;
  u32 x=g_cfg.cat_off+8u+28u*g_cfg.cat_n;
  if(x>0x1000000u-(4u+12u*g_cfg.cat_n+24u) || rd32(x)!=EXT_MAGIC) return 0;
  x+=4u+12u*g_cfg.cat_n;
  if(rd32(x)!=TP_MAGIC || rd32(x+4)!=1u || rd32(x+8)!=72u) return 0;
  u32 n=rd32(x+12); if(!n || n>256u || x>0x1000000u-(24u+72u*n)) return 0;
  return x;
}
static u32 tp_record(u32 h,u32 id){
  u32 n=rd32(h+12u); for(u32 i=0;i<n;i++){ u32 a=h+24u+72u*i; if(rd16(a)==id) return a; } return 0;
}
/* Strict byte/word RLE: every source read and output run must fit. */
static int tp_decode(u32 n,u32 cap,u32 flags){
  u32 i=0,o=0, xr=flags&0x80u?0x53u:0u;
#define TP_BYTE(k) ((u8)(g_pic[(k)]^xr))
  if(!(flags&0x60u)){
    if(n!=cap) return 0; for(;o<cap;o++) g_spr[o]=TP_BYTE(o); return 1;
  }
  if((flags&0x60u)==0x60u) return 0;
  if(xr && !(flags&0x40u)){
    int lz4=spr_legacy_lz4(g_spr,g_pic,n,cap);
    if(lz4>=0) return lz4;
  }
  while(o<cap){
    u32 run,lit,unit=flags&0x40u?4u:1u;
    if(i+unit>n) return 0;
    if(unit==4u){ u32 c=TP_BYTE(i)|((u32)TP_BYTE(i+1)<<8)|((u32)TP_BYTE(i+2)<<16)|((u32)TP_BYTE(i+3)<<24); i+=4;
      lit=c>>31; run=lit?(c&0x0FFFFFFFu):c; if(!run || run>(cap-o)/4u) return 0; run*=4u;
    } else { u32 c=TP_BYTE(i++); lit=c>>7; run=c&127u; if(!run || run>cap-o) return 0; }
    u32 src=lit?run:unit; if(src>n-i) return 0;
    for(u32 j=0;j<run;j++) g_spr[o++]=TP_BYTE(i+(lit?j:j%unit)); i+=src;
  }
#undef TP_BYTE
  return o==cap;
}
static int tp_signed(u32 a){ u32 v=rd16(a); return v&0x8000u?(int)v-65536:(int)v; }
static int tp_part(u32 q,int x0,int y0,const u16 *tint){
  u32 addr=rd32(q), n=rd16(q+4), need=rd16(q+6), pal=rd32(q+8);
  u32 w=fl(q+12)[0], h=fl(q+13)[0], flags=fl(q+14)[0], tr=fl(q+15)[0];
  if(!w || !h || w>64u || h>64u || (flags&3u)!=2u || need!=w*h/2u || need>sizeof(g_spr)) return 0;
  if(!tp_load(addr,n,rd16(q+16)) || !tp_decode(n,need,flags) || !tp_load(pal,32,rd16(q+18))) return 0;
  u16 colors[16];
  for(u32 i=0;i<16u;i++){ colors[i]=(u16)pr16(i*2u); if(tint && i>=2u && i<=7u && colors[i]) colors[i]=tint[i-2u]; }
  for(u32 y=0;y<h;y++) for(u32 x=0;x<w;x++){
    u32 bit=(y*w+x)*4u, v=(g_spr[bit>>3]>>(bit&7u))&15u;
    if((flags&4u) && v==tr) continue;
    int X=x0+(int)x,Y=y0+(int)y;
    if(X>=32 && X<96 && Y>=34 && Y<98) g_fb[Y*128+X]=colors[v];
  }
  return 1;
}
static int tama_preview(u32 body,u32 eyes,u32 color){
  u32 h=tp_header(),b=0,e=0; u16 tint[6]; const u16 *colors=0;
  rect(32,34,96,98,BG);
  if(h){ b=tp_record(h,body); e=tp_record(h,eyes); }
  if(!b || !e || color>16u) goto unavailable;
  if(color<16u){
    if(!tp_load(rd32(h+16),512,rd32(h+20))) goto unavailable;
    for(u32 i=0;i<6u;i++) tint[i]=(u16)pr16(color*32u+4u+i*2u); colors=tint;
  }
  /* The native appearance override replaces only the eyes. The body's
   * neutral pose supplies layer placement and its own mouth. */
  int ex=tp_signed(b+64),ey=tp_signed(b+66),mx=tp_signed(b+68),my=tp_signed(b+70);
  if(ex<-64 || ex>64 || ey<-64 || ey>64 || mx<-64 || mx>64 || my<-64 || my>64) goto unavailable;
  /* Eyes retain their own palette. Some faces store outline/iris colors in
   * slots 2..7 too; those are not body-tint slots (white made them disappear).
   * Mouth sprites do contain body-tone patches, so they still inherit tint. */
  if(!tp_part(b+4,32,34,colors) || !tp_part(e+24,32+ex,50+ey,0) || !tp_part(b+44,32+mx,50+my,colors)) goto unavailable;
  return 1;
unavailable:
  rect(8,34,120,98,BG);
  ctext(59,!h?"NO PREVIEW INDEX":!b || !e?"NO CHARACTER DATA":"PREVIEW READ ERROR",GREY,1);
  ctext(73,!h?"REBUILD MENU DATA":!b || !e?"UNSUPPORTED ASSET":"CHECK ROM DATA",GREY,1); return 0;
}
