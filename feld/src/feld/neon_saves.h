/* Neon owns the format inside the live region. A slot is a sparse image of
 * that region, including all five settings sectors, and never changes its
 * edition, planet, or generation to enter a slot. */
#define NEON_ACTIVE_MAGIC 0x5641534Eu /* NSAV */

static int neon_active(void){
  u32 page=g_cfg.slot_off-PAGE;
  u32 slot=rd32(page+8u);
  if(rd32(page)!=NEON_ACTIVE_MAGIC || rd32(page+4u)!=~NEON_ACTIVE_MAGIC ||
     slot>=g_cfg.nslots || rd32(page+12u)!=~slot) return -1;
  return (int)slot;
}

static int neon_set_active(u32 slot){
  if(neon_active()==(int)slot) return 1;
  for(u32 i=0;i<PAGE;i++) g_out[i]=0xFF;
  u32 *record=(u32*)g_out;
  record[0]=NEON_ACTIVE_MAGIC; record[1]=~NEON_ACTIVE_MAGIC;
  record[2]=slot; record[3]=~slot;
  flash_prog4k(g_cfg.slot_off-PAGE,g_out);
  return neon_active()==(int)slot;
}

static int neon_restore_source(u32 journal,u32 source){
  if(restore(source,0) || !neon_set_active(source) || !switch_journal_end(journal)) return 0;
  erase_page(slot_base(source));
  return 1;
}

static void neon_recover(u32 journal,u32 source){
  while(!neon_restore_source(journal,source))
    message("RECOVERY INCOMPLETE","BACKUP IS KEPT","B / C TO RETRY");
  message("SWITCH INTERRUPTED","ORIGINAL SAVE BACK","");
  leave();
}

static int neon_save_switch(u32 source,u32 target,int has){
  if(set_load()){
    message("SAVE UNAVAILABLE","FINISH GAME SETUP","AND TRY AGAIN");
    return 0;
  }
  if(has && restore_check(target)){
    message("SAVE UNREADABLE","CURRENT SAVE KEPT","");
    return 0;
  }
  rect(0,0,128,128,BG); ctext(8,"PLEASE WAIT",WHITE,1); ctext(20,"DO NOT SWITCH OFF",GREY,1); show();
  if(stash(source)){
    message("BACKUP FAILED","CURRENT SAVE KEPT","");
    return 0;
  }
  u32 journal=switch_journal_begin(source);
  if(!journal){
    message("NO RECOVERY SPACE","CURRENT SAVE KEPT","");
    return 0;
  }
  if(restore_loaded(target,!has) || !neon_set_active(target) || !switch_journal_end(journal)){
    neon_recover(journal,source);
    return 0;
  }
  if(has) erase_page(slot_base(target));
  return 1;
}

static const char *g_neon_field[NF_MAX];
static void neon_scan_saves(void){
  for(u32 i=0;i<g_cfg.nslots;i++){
    g_neon_field[i]=0;
    int f=-1;
    if((int)i==g_act){
      if(!set_load()) f=field_of(g_set);
    } else if(slot_valid(i)){
      u32 b=slot_base(i), np=rd32(b+8u), bytes=rd32(b+12u), pos=0, latest=0;
      for(u32 j=0;j<np;j++){
        u32 addr=rd16(b+32u+j*4u)<<12, n=rd16(b+34u+j*4u);
        if(!n || n>sizeof g_cmp || n>bytes-pos) break;
        if(slot_settings_page(addr)){
          slot_get(i,pos,n,g_cmp);
          if(!lz_unpack(g_cmp,n,g_page) && set_ok(g_page)){
            u32 generation=neon_set_generation(g_page);
            if(generation>=latest){ latest=generation; f=field_of(g_page); }
          }
        }
        pos+=n;
      }
    }
    if(f>=0) g_neon_field[i]=fname((u32)f);
  }
}
static int neon_save_row(u32 i,char *name,char *right,u16 *col){
  save_label(i,name);
  int active=(int)i==g_act;
  if(active){
    char *p=name;
    while(*p) p++;
    const char *live=" LIVE";
    while((*p++=*live++)){}
  }
  const char *status=g_neon_field[i];
  if(!status) status=active?"ACTIVE":slot_valid(i)?"CONTINUE":"NEW GAME";
  while((*right++=*status++)){}
  *col=active?GOLD:WHITE;
  return 1;
}

static void neon_saves_menu(void){
  g_act=neon_active();
  if(g_act<0){ message("SAVE SLOTS DAMAGED","CURRENT SAVE KEPT",""); return; }
  neon_scan_saves();
  u32 target=(u32)g_act;
  for(;;){
    int picked=progress_choose("SAVES",g_cfg.nslots,target,neon_save_row);
    if(picked<0) return;
    target=(u32)picked;
    if(target==(u32)g_act) continue;
    int has=slot_valid(target);
    if(!has && !page_ff(slot_base(target))){
      message("SAVE UNREADABLE","CURRENT SAVE KEPT","");
      continue;
    }
    char label[16]; save_label(target,label);
    if(!progress_reset_confirm(has?"SWITCH SAVE?":"START NEW GAME?",label,
                              has?"LOAD THIS SAVE":"USE NATIVE SETUP")) continue;
    if(neon_save_switch((u32)g_act,target,has)) leave();
  }
}
