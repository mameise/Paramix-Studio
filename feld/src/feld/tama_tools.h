/* Paradise settings offsets, relative to Cfg.tama_off. App settings buffers
 * omit the four CRC bytes; these offsets address the complete settings page.
 * gen1/Jade/gen3 have the same pet layout. No downloaded app code is included. */
static u32 t_word(u32 rel){ u32 o=g_cfg.tama_off+rel; return g_set[o]|(g_set[o+1]<<8); }
static void t_put(u32 rel,u32 v){ u32 o=g_cfg.tama_off+rel; g_set[o]=(u8)v; g_set[o+1]=(u8)(v>>8); }
static u32 t_color(void){ u32 v=g_set[g_cfg.tama_off+0x19u]&31u; return v==31u?16u:v; }
static int t_alive(void){ return t_word(4)!=0 && t_word(4)!=0xFFFFu && t_stage()>=2u && t_stage()<=5u; }
static int t_layout(void){ return g_cfg.tama_off==0x104u || g_cfg.tama_off==0x108u || g_cfg.tama_off==0x114u; }
static void t_event_clear(u32 code){
  u32 h=g_cfg.tama_off+0x1Cu, bit=1u<<(code&7u);
  g_set[h-0x26u+(code>>3)]&=(u8)~bit;
  g_set[h-0x21u+(code>>3)]&=(u8)~bit;
}
/* Bits: food, joy, heal, clean. Preserve unrelated event/condition bits. */
static void t_care(u32 mask){
  u32 t=g_cfg.tama_off;
  if(mask&1u){
    g_set[t+0x1Cu]=6; t_event_clear(0x11);
    if(t_stage()<=5u){
      g_set[t+0x40u]=g_set[t+0x41u]=0;
      u32 o=t==0x104u?0x184u:t==0x108u?0x188u:0x19Cu;
      for(u32 i=0;i<4u;i++) g_set[o+i]=0;
    }
  }
  if(mask&2u){ g_set[t+0x1Du]=20; t_event_clear(0x12); }
  if(mask&4u){
    g_set[t+0x10u]=0; t_event_clear(0x0A); t_event_clear(0x0B);
    u32 o=t==0x104u?0x180u:t==0x108u?0x184u:0x198u;
    for(u32 i=0;i<4u;i++) g_set[o+i]=0;
  }
  if(mask&8u) g_set[t+0x14u]&=(u8)~0x10u;
}
static void t_stage_set(u32 stage){
  if(stage==t_stage()) return;
  u32 t=g_cfg.tama_off;
  g_set[t+0x18u]=(u8)((g_set[t+0x18u]&~0x1Cu)|(stage<<2));
  for(u32 i=0x48u;i<=0x4Bu;i++) g_set[t+i]=0;
  g_set[t+0x14u]&=0x10u;
  g_set[t+0x15u]=g_set[t+0x16u]=g_set[t+0x17u]=0;
  g_set[t-0xBu]=g_set[t-0xCu]=0;
  for(u32 i=0x20u;i<=0x2Du;i++) g_set[t+i]=0;
  static const u8 events[4]={1,19,20,28};
  for(u32 i=0;i<4u;i++) t_event_clear(events[i]);
}
/* The own-Tama file has body/eyes at +C/+E, color at +10, and an
 * additional eye ID at +10A. Refuse ambiguous matches (e.g. visiting ghosts)
 * rather than changing several characters with the same appearance. Preflight
 * the whole search before the first write. Other pages remain byte-for-byte. */
static int t_visual(u32 body,u32 eyes,u32 color,u32 stage){
  if(!t_layout() || !t_alive() || color>16u || stage<2u || stage>5u) return 0;
  u32 oldbody=t_word(4), oldeyes=t_word(6), at=0, n=0;
  if(body!=oldbody || eyes!=oldeyes || color!=t_color()){
    for(u32 a=g_cfg.user_start;a+PAGE<=g_cfg.user_end && a<SET_A;a+=PAGE){
      u32 g=rd32(a+8u);
      if(g>255u || rd16(a+12u)!=oldbody || rd16(a+14u)!=oldeyes || rd16(a+0x10Au)!=oldeyes) continue;
      u32 c=fl(a+0x10u)[0];
      if(c!=(t_color()==16u?255u:t_color())) continue;
      at=a; n++;
    }
    if(n!=1u) return 0;
    flash_read(at,g_page,PAGE);
    g_page[12]=(u8)body; g_page[13]=(u8)(body>>8);
    g_page[14]=(u8)eyes; g_page[15]=(u8)(eyes>>8);
    g_page[0x10]=(u8)(color==16u?255u:color);
    g_page[0x10A]=(u8)eyes; g_page[0x10B]=(u8)(eyes>>8);
    flash_prog4k(at,g_page);
    if(!flash_same(at,g_page,PAGE)) error_screen(40);
  }
  t_put(4,body); t_put(6,eyes); t_stage_set(stage);
  u32 o=g_cfg.tama_off+0x19u;
  g_set[o]=(u8)((g_set[o]&~31u)|(color==16u?31u:color));
  return 1;
}
