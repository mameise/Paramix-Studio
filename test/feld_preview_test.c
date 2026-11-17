#include "feld_test_support.h"
static void put16(u8 *p,u32 v){ p[0]=(u8)v; p[1]=(u8)(v>>8); }
static u32 sum16(const u8 *p,u32 n){ u32 s=0; for(u32 i=0;i<n;i++) s+=p[i]; return (s&65535u)?s&65535u:1u; }
static u32 descriptor(u32 q,u32 at,u32 pal,u32 height,u32 mode,const u8 *raw){
  u8 *p=host_flash+at; u32 need=64u*height/2u,n=0;
  if(mode==0){ memcpy(p,raw,need); n=need; }
  else if(mode==0x20){
    for(u32 i=0;i<need;){ u32 k=need-i; if(k>127) k=127; p[n++]=(u8)(128u+k); memcpy(p+n,raw+i,k); n+=k; i+=k; }
    p[n++]=0;
  } else { sl_w32(p,0x80000000u|need/4u); memcpy(p+4,raw,need); n=need+4; sl_w32(p+n,0); n+=4; }
  for(u32 i=0;i<n;i++) p[i]^=0x53u;
  u8 *d=host_flash+q; sl_w32(d,at); put16(d+4,n); put16(d+6,need); sl_w32(d+8,pal);
  d[12]=64; d[13]=(u8)height; d[14]=(u8)(0x86u|mode); d[15]=0;
  put16(d+16,sum16(p,n)); put16(d+18,sum16(host_flash+pal,32)); return n;
}
static void synthetic(void){
  fixture();
  u32 x=g_cfg.cat_off+12u+40u*g_cfg.cat_n, r=x+24, pal=0x112001, variants=0x113001;
  sl_w32(host_flash+x-4u-12u*g_cfg.cat_n,EXT_MAGIC);
  sl_w32(host_flash+x,TP_MAGIC); sl_w32(host_flash+x+4,1); sl_w32(host_flash+x+8,72); sl_w32(host_flash+x+12,1);
  sl_w32(host_flash+x+16,variants); put16(host_flash+r,4001);
  u8 *p=host_flash+pal; memset(p,0,32); put16(p+4,0xF800); put16(p+6,0x07E0); put16(p+8,0x001F);
  memset(host_flash+variants,0,512);
  for(u32 c=0;c<16;c++){ u8 *v=host_flash+variants+32*c; put16(v+4,0xFFE0); put16(v+6,0x07FF); put16(v+8,0xF81F); }
  sl_w32(host_flash+x+20,sum16(host_flash+variants,512));
  u8 raw[2048]; memset(raw,0x22,sizeof raw); raw[0]=0x52; /* opaque zero-valued palette index 5 */
  descriptor(r+4,0x114001,pal,64,0,raw);
  memset(raw,0,sizeof raw); raw[0]=3; descriptor(r+24,0x115001,pal,32,0x20,raw);
  raw[0]=4; descriptor(r+44,0x116001,pal,32,0x40,raw);
  put16(host_flash+r+64,0); put16(host_flash+r+66,(u16)-16); put16(host_flash+r+68,1); put16(host_flash+r+70,(u16)-15);
  for(u32 i=0;i<16384;i++) g_fb[i]=0x1234;
  assert(tama_preview(4001,4001,16));
  assert(g_fb[34*128+32]==0x07E0 && g_fb[35*128+33]==0x001F && g_fb[36*128+34]==0xF800);
  assert(g_fb[34*128+33]==0 && g_fb[33*128+32]==0x1234 && g_fb[98*128+95]==0x1234);
  assert(tama_preview(4001,4001,0));
  assert(g_fb[34*128+32]==0x07E0 && g_fb[35*128+33]==0xF81F && g_fb[36*128+34]==0xFFE0 && !g_fb[34*128+33]);
  /* No flash mutation while previewing, including bad references/checksums. */
  u8 *snapshot=malloc(0x1000000); assert(snapshot); memcpy(snapshot,host_flash,0x1000000);
  assert(!tama_preview(1234,4001,16) && !tama_preview(4001,4001,17));
  assert(!memcmp(snapshot,host_flash,0x1000000)); free(snapshot);
  host_flash[0x114001]^=1; assert(!tama_preview(4001,4001,16)); host_flash[0x114001]^=1;
  sl_w32(host_flash+r+4,0xFFFFFFFE); assert(!tama_preview(4001,4001,16)); sl_w32(host_flash+r+4,0x114001);
  put16(host_flash+r+28,1); assert(!tama_preview(4001,4001,16));
  /* Strict decoder truncation, oversized runs and word-RLE source reads. */
  g_pic[0]=0x7F; g_pic[1]=1; assert(!tp_decode(2,8,0x20));
  g_pic[0]=0x81; assert(!tp_decode(1,8,0x20));
  memset(g_pic,0,8); g_pic[0]=1; assert(!tp_decode(4,4,0x40));
  assert(!tp_decode(8,8,0x60));
  sl_w32(host_flash+x+12,0xFFFFFFFF); assert(!tp_header());
  g_cfg.cat_off=0xFFFFFFF0; assert(!tp_header());
  puts("Preview: layer placement, ROM palettes, raw/byte/word RLE, XOR, clipping and invalid data passed");
}
int main(int argc,char **argv){
  host_flash=malloc(0x1000000); assert(host_flash);
  if(argc==8){
    FILE *f=fopen(argv[1],"rb"); assert(f && fread(host_flash,1,0x1000000,f)==0x1000000); fclose(f);
    memcpy((void*)&g_cfg,host_flash+strtoul(argv[2],0,0),sizeof g_cfg);
    for(u32 i=0;i<16384;i++) g_fb[i]=BG;
    assert(tama_preview(strtoul(argv[3],0,0),strtoul(argv[4],0,0),strtoul(argv[5],0,0)));
    f=fopen(argv[6],"wb"); assert(f && fwrite(g_fb,2,16384,f)==16384); fclose(f);
    g_dir=argv[7]; show();
  } else { assert(argc==2); g_dir=argv[1]; synthetic(); }
  free(host_flash); return 0;
}
