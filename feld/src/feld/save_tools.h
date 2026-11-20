/* Local slot copies share immutable identical pool pages, committing the header last.
 * No live save or source slot is modified until the normal switch afterwards. */
static int save_page(u32 src,u32 i,u32 *addr){
  if((int)src==g_act){
    u32 j=0;
    for(u32 a=g_cfg.user_start;a<g_cfg.user_end;a+=PAGE) if(!page_ff(a)){
      if(j++==i){ *addr=a; flash_read(a,g_page,PAGE); return 0; }
    }
    return 1;
  }
  u32 b=slot_base(src), pos=0;
  for(u32 k=0;k<i;k++) pos+=rd16(b+34u+4u*k);
  u32 n=rd16(b+34u+4u*i); *addr=rd16(b+32u+4u*i)<<12;
  if(!n || n>sizeof g_cmp) return 1;
  slot_get(src,pos,n,g_cmp);
  if(lz_unpack(g_cmp,n,g_page)) return 1;
  return slot_time(src,*addr,g_page);
}
static int source_check(u32 src,u32 *np){
  if(src>=g_cfg.nslots) return 1;
  *np=0;
  if((int)src==g_act){
    flash_read(SET_A,g_page,PAGE); if(!set_ok(g_page)) return 1;
    for(u32 a=g_cfg.user_start;a<g_cfg.user_end;a+=PAGE) if(!page_ff(a)) (*np)++;
  } else {
    if(!slot_valid(src)) return 1;
    u32 b=slot_base(src), by=rd32(b+12), sum=0, bytes=0;
    *np=rd32(b+8);
    for(u32 p=0;p<by;p+=PAGE){ u32 n=by-p<PAGE?by-p:PAGE; slot_get(src,p,n,g_page); for(u32 k=0;k<n;k++) sum+=g_page[k]; }
    if(sum!=rd32(b+16)) return 1;
    for(u32 i=0;i<*np;i++) bytes+=rd16(b+34u+4u*i);
    if(bytes!=by) return 1;
  }
  return !*np || *np>NPAGE_MAX;
}
static int copy_settings(u32 addr,u32 tgt,u32 fld){
  if(addr!=SET_A && addr!=SET_B) return 0;
  if(!set_ok(g_page) || fld>=nfields()) return 1;
  if(g_cfg.six_off){
    u32 k=0; while(k<8u && g_cfg.six_map[k]!=fld) k++;
    if(k==8u) return 1;
    g_page[g_cfg.six_off]=(u8)(k/4u); g_page[g_cfg.field_byte]=(u8)(k%4u);
  } else g_page[g_cfg.field_byte]=(u8)fld;
  g_page[SAVE_OFF]=(u8)(tgt+1u); stamp(g_page); return 0;
}
static u32 pool_free(u32 tgt){
  pool_mark(tgt); u32 n=pool_n(), free=0;
  if(n>sizeof(g_used)*8u) return 0;
  for(u32 k=0;k<n;k++) if(!(g_used[k>>3]&(1u<<(k&7u)))) free++;
  return free;
}
static int slot_clear(u32 tgt){
  if(tgt>=g_cfg.nslots || (int)tgt==g_act) return 1;
  /* Erase only the header: data pages may physically lie in another slot's
   * region, and become available to the shared pool automatically. */
  erase_page(slot_base(tgt)); return !page_ff(slot_base(tgt));
}
static int slot_copy(u32 src,u32 tgt,u32 fld){
  if(tgt>=g_cfg.nslots || src==tgt || (int)tgt==g_act || slot_valid(tgt) || fld>=nfields()) return 1;
  u32 np; if(source_check(src,&np)) return 2;
  pool_mark(tgt);
  u32 bytes=0, prev=0, sets=0, on=0, need=0;
  /* Exact preflight, including recompressed field/slot settings. */
  for(u32 i=0;i<np;i++){
    u32 a; if(save_page(src,i,&a) || a<g_cfg.user_start || a>=g_cfg.user_end || (i && a<=prev)) return 2;
    prev=a;
    if(copy_settings(a,tgt,fld)) return 2;
    if(a==SET_A) sets|=1u; if(a==SET_B) sets|=2u;
    u32 n=lz_pack(g_page,g_cmp); bytes+=n;
    for(u32 k=0;k<n;k++){ g_out[on++]=g_cmp[k]; if(on==PAGE){ need+=pool_need_chunk(on); on=0; } }
  }
  u32 nd=(bytes+PAGE-1u)/PAGE;
  if(sets!=3u) return 2;
  if(on) need+=pool_need_chunk(on);
  if(nd>DPG_MAX || need>pool_free(tgt) || !stash_capacity(need)) return 3;
  pool_mark(tgt);
  for(u32 k=0;k<sizeof g_hdr/4u;k++) ((u32*)&g_hdr)[k]=0xFFFFFFFFu;
  on=0; u32 used=0, sum=0;
  for(u32 i=0;i<np;i++){
    u32 a; if(save_page(src,i,&a) || copy_settings(a,tgt,fld)) return 2;
    u32 n=lz_pack(g_page,g_cmp); g_hdr.e[i].page=(u16)(a>>12); g_hdr.e[i].clen=(u16)n;
    for(u32 k=0;k<n;k++){
      g_out[on++]=g_cmp[k]; sum+=g_cmp[k];
      if(on==PAGE){
        if(used>=nd) return 3; u32 x=pool_store(tgt,g_out); if(x==0xFFFFu) return 3;
        if(!flash_same(pool_addr(x),g_out,PAGE)) return 4;
        g_hdr.dpg[used++]=(u16)x; on=0;
      }
    }
    progress("COPYING",i+1u,np);
  }
  if(on){
    if(used>=nd) return 3;
    for(u32 k=on;k<PAGE;k++) g_out[k]=0xFF;
    u32 x=pool_store(tgt,g_out); if(x==0xFFFFu) return 3;
    if(!flash_same(pool_addr(x),g_out,PAGE)) return 4;
    g_hdr.dpg[used++]=(u16)x;
  }
  if(used!=nd) return 4;
  g_hdr.magic=SLOT_MAGIC; g_hdr.field=tgt+1u; g_hdr.npages=np; g_hdr.bytes=bytes; g_hdr.sum=sum;
  g_hdr.res[0]=POOL_MAGIC; g_hdr.res[1]=used; g_hdr.res[2]=0;
  for(u32 k=0;k<PAGE;k++) g_out[k]=k<sizeof g_hdr ? ((u8*)&g_hdr)[k] : 0xFF;
  flash_prog4k(slot_base(tgt),g_out);
  if(!flash_same(slot_base(tgt),g_out,PAGE) || !slot_valid(tgt)){ slot_clear(tgt); return 4; }
  return 0;
}
