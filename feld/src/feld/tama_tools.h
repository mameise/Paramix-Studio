/* Paradise pet offsets relative to Cfg.tama_off in the complete settings
 * page. The native SRAM view omits the first four CRC bytes. */
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
  if(g_cfg.flags & F_NEON_TAMA){
    if(mask&1u) g_set[t+0x1Cu]=6;
    if(mask&2u) g_set[t+0x1Du]=20;
    if(mask&4u) g_set[t+0x10u]=0;
    if(mask&8u) g_set[t+0x14u]&=(u8)~0x10u;
    return;
  }
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
  if(g_cfg.flags & F_NEON_TAMA) return;
  for(u32 i=0x48u;i<=0x4Bu;i++) g_set[t+i]=0;
  g_set[t+0x14u]&=0x10u;
  g_set[t+0x15u]=g_set[t+0x16u]=g_set[t+0x17u]=0;
  g_set[t-0xBu]=g_set[t-0xCu]=0;
  for(u32 i=0x20u;i<=0x2Du;i++) g_set[t+i]=0;
  static const u8 events[4]={1,19,20,28};
  for(u32 i=0;i<4u;i++) t_event_clear(events[i]);
}
/* Vanilla firmware uses record zero of its ghost table as a rendered
 * appearance override for the active Tama. Verify the table before using
 * that slot. Other slots and the communication staging records belong to
 * separate features. The table offsets are relative to the flash image. */
#define T_REBUILD_PENDING 0xFF000000u
static u32 t_active_record(void){
  if(g_cfg.flags & F_NEON_TAMA){
    const u32 record=0xE4E000u, table=0xAE954u, hook=0x4902Cu;
    if(record<g_cfg.user_start || record+0x4000u>g_cfg.user_end ||
       rd32(table)!=0x60E4E000u || rd32(table+4u)!=0x60D8F000u ||
       rd32(table+8u)!=0x60E52000u ||
       rd32(hook)!=0x2190F241u || rd32(hook+0x14u)!=0x681202E4u ||
       rd32(hook+0x18u)!=0x4F7FF1B2u || rd16(hook+0x1Cu)!=0xD00Bu) return 0;
    if(rd32(record+0x10Cu)!=T_REBUILD_PENDING &&
       (rd16(record+0x10Au)!=t_word(4) || rd32(record)+rd32(record+4u)!=0u)) return 0;
    return record;
  }
  u32 table, record, hook;
  switch(g_cfg.tama_off){
    case 0x104u: table=0x94214u; record=0xDEE000u; hook=0x41E84u; break;
    case 0x108u: table=0x9954Cu; record=0xDEE000u; hook=0x43E8Cu; break;
    case 0x114u: table=0xACBB8u; record=0xDCE000u; hook=0x48A50u; break;
    default: return 0;
  }
  if(record<g_cfg.user_start || record+0x4000u>g_cfg.user_end ||
     rd32(table)!=0x60000000u+record ||
     rd32(table+4u)!=0x60004000u+record ||
     rd32(table+8u)!=0x60008000u+record ||
     rd32(hook+0x18u)!=0x4F7FF1B2u || rd16(hook+0x1Cu)!=0xD00Bu) return 0;
  return record;
}
static int t_queue_rebuild(u32 record){
  if(rd32(record+0x10Cu)==T_REBUILD_PENDING) return 1;
  /* An out-of-range length makes the game rebuild the active record. The
   * installed patch recognizes this marker and keeps the selected settings. */
  flash_read(record,g_page,PAGE);
  g_page[0x10C]=g_page[0x10D]=g_page[0x10E]=0;
  g_page[0x10F]=(u8)(T_REBUILD_PENDING>>24);
  flash_prog4k(record,g_page);
  return flash_same(record,g_page,PAGE);
}
static int t_visual(u32 body,u32 eyes,u32 color,u32 stage){
  if(!t_layout() || !t_alive() || color>16u || stage<2u || stage>5u ||
     body<1001u || body>4999u || eyes<1001u || eyes>4999u) return 0;
  if(g_cfg.flags & F_NEON_TAMA){
    int b=cat_find(body), e=cat_find(eyes);
    if(b<0 || e<0) return 0;
    u32 bo=g_cfg.cat_off+8u+28u*(u32)b, eo=g_cfg.cat_off+8u+28u*(u32)e;
    if(fl(bo+2u)[0]!=2u || fl(eo+2u)[0]!=2u ||
       fl(bo+6u)[0]!=stage || fl(eo+6u)[0]!=stage) return 0;
  }
  t_put(4,body); t_put(6,eyes); t_stage_set(stage);
  u32 o=g_cfg.tama_off+0x19u;
  g_set[o]=(u8)((g_set[o]&~31u)|(color==16u?31u:color));
  return 1;
}
