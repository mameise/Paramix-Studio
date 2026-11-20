/* Tama File flags in the complete, CRC-prefixed settings page.
 * Land/Water/Sky index by the suffix of the character ID. Jade and later
 * have three 32-bit words indexed by the ROM's field and list order. */
#define BOOK_EXTRA_OFF 0xF80u
#define BOOK_FILE_COUNT 22u
#define BOOK_NEON_OFF 0xF98u
#define NEON_BOOK_ROSTERS 0xB0A28u
#define NEON_BOOK_UNLOCKED 0x94u
#define NEON_BOOK_VIEWED 0xA4u

static int progress_neon_book(void){ return (g_cfg.flags&F_NEON_EDIT)!=0; }

static u32 progress_neon_start(void){
#ifndef FELD_CLEAN
  if(g_cfg.flags&F_BOOK6){
    u32 version=rd32(g_cfg.stub_off+0x304u);
    if(version==3u) return 6u;
    if(version==4u) return 3u;
  }
#endif
  return 0;
}
static int progress_imported(u32 field){
  u32 start=progress_neon_start();
  return start && field>=start;
}
static u32 progress_count(u32 field){
  if(progress_neon_book()) return field==0u?22u:21u;
  if(progress_imported(field)) return field==progress_neon_start()+1u?38u:24u;
  return BOOK_FILE_COUNT;
}
static const char *progress_field_name(u32 field){
  u32 start=progress_neon_start();
  if(start && field>=start){
    static const char *const names[]={"COSMIC","NEW TAMA","TAMA PLANET"};
    return names[field-start];
  }
  if(start==6u && nfields()<6u && field>=3u){
    static const char *const names[]={"LAND","WATER","SKY"};
    return names[field-3u];
  }
  return fname(field);
}
static u16 progress_field_color(u32 field){
  return (progress_imported(field) || field>=nfields())?PROGRESS_COLOR:fcol(field);
}

static u32 progress_fields(void){
  if(progress_neon_book()) return 3u;
  u32 start=progress_neon_start();
  if(start) return start+3u;
  u32 n=nfields();
  return (g_cfg.flags&F_BOOK6) || n<3u ? n : 3u;
}
static void progress_chars(u32 field){
  g_chrn=0;
  if(progress_neon_book()){
    const u32 entry=NEON_BOOK_ROSTERS+8u*field, count=progress_count(field);
    u32 roster=rd32(entry)-0x10000000u+XIP_DEC;
    if(rd32(entry+4u)!=count || roster<XIP_DEC || roster>NEON_BOOK_ROSTERS-2u*count) return;
    for(u32 i=0;i<count;i++){
      int k=cat_find(rd16(roster+2u*i));
      if(k<0 || fl(g_cfg.cat_off+8u+28u*(u32)k+7u)[0]!=field+7u){ g_chrn=0; return; }
      g_chr[g_chrn++]=(u16)k;
    }
    return;
  }
  u32 tag=progress_imported(field)?7u+field-progress_neon_start():field+1u;
  for(u32 i=0;i<g_cfg.cat_n && g_chrn<256u;i++){
    u32 a=g_cfg.cat_off+8u+28u*i;
    if(fl(a+2u)[0]==2u && fl(a+6u)[0]>=3u && fl(a+7u)[0]==tag)
      g_chr[g_chrn++]=(u16)i;
  }
}
static u32 progress_flag(u32 field,u32 index,u32 id){
  if(progress_neon_book()) return (NEON_BOOK_UNLOCKED+4u*field)*8u+index;
  if(progress_imported(field)) return (BOOK_NEON_OFF+8u*(field-progress_neon_start()))*8u+index;
  u32 bit=id%1000u, off=0x88u;
  if(g_cfg.tama_off!=0x104u){
    bit=index;
    off=g_cfg.tama_off==0x114u?0x94u:0x88u;
    off=field<3u?off+4u*field:BOOK_EXTRA_OFF+4u*(field-3u);
  }
  return off*8u+bit;
}
static int progress_bit(u32 bit,int unlock){
  u8 *p=&g_set[bit>>3]; u8 mask=(u8)(1u<<(bit&7u));
  if(*p&mask) return 0;
  if(unlock) *p|=mask;
  return 1;
}
static void progress_clear_bit(u32 bit){ g_set[bit>>3]&=(u8)~(1u<<(bit&7u)); }
static u32 progress_apply(u32 first,u32 end,int unlock){
  u32 missing=0;
  for(u32 field=first;field<end;field++){
    progress_chars(field);
    if(g_chrn!=progress_count(field)) continue;
    for(u32 i=0;i<g_chrn;i++){
      u32 id=rd16(g_cfg.cat_off+8u+28u*g_chr[i]);
      missing+=(u32)progress_bit(progress_flag(field,i,id),unlock);
    }
  }
  return missing;
}
static int progress_root_row(u32 i,char *name,char *right,u16 *col){
  static const char *const names[]={"PLANET LEVEL","TAMA FILE","MISSIONS"};
  const char *p=names[i]; while((*name++=*p++)){}
  if(i==0u) utoa_(lv_get(),right);
  else right[0]=0;
  *col=i==0u?GOLD:WHITE; return 1;
}
static int progress_action_row(u32 i,char *name,char *right,u16 *col){
  static const char *const names[]={"ONE TAMA FILE","ONE FIELD","ALL FIELDS"};
  static const u16 colors[]={WHITE,RGB(104,224,211),ORANGE};
  const char *p=names[i]; while((*name++=*p++)){}
  right[0]=0; *col=colors[i]; return 1;
}
static u8 g_progress_regions[9];
static int progress_region_row(u32 i,char *name,char *right,u16 *col){
  u32 field=g_progress_regions[i];
  const char *p=progress_field_name(field); while((*name++=*p++)){}
  right[0]=0; *col=progress_field_color(field); return 1;
}
static u32 g_progress_field;
static int progress_char_row(u32 i,char *name,char *right,u16 *col){
  u32 id=rd16(g_cfg.cat_off+8u+28u*g_chr[i]);
  chr_name(g_chr[i],name);
  int missing=progress_bit(progress_flag(g_progress_field,i,id),0);
  const char *p=missing?"LOCKED":"OPEN"; while((*right++=*p++)){}
  *col=missing?GREY:progress_field_color(g_progress_field); return 1;
}
static int progress_choose(const char *title,u32 n,u32 sel,int (*row)(u32,char *,char *,u16 *)){
  for(;;){
    u32 top=n<=10u || sel<5u?0u:sel+5u>=n?n-10u:sel-5u;
    list_draw(title,n,sel,top,row,"B SELECT  C BACK"); wait_release();
    int k=menu_key();
    if(k==K_BACK) return -1;
    if(k==K_OK) return (int)sel;
    if(k==1 || k==K_A) sel=wrap_step(sel,k==K_A?1:g_step,0,n-1u);
  }
}
static int progress_confirm(const char *name,u32 count){
  if(!count){ message("NO LOCKED FILES","",""); return 0; }
  rect(0,0,128,128,BG);
  menu_title(15,"UNLOCK TAMA FILES"); ctext(37,name,WHITE,1);
  char n[5]; utoa_(count,n); ctext(57,n,WHITE,2);
  ctext(100,"B SAVE  C CANCEL",GREY,1); show();
  return yes_no();
}
static int progress_reset_confirm(const char *title,const char *name,const char *detail){
  rect(0,0,128,128,BG);
  menu_title(15,title); ctext(39,name,WHITE,1); ctext(63,detail,ORANGE,1);
  ctext(100,"B SAVE  C CANCEL",GREY,1); show();
  return yes_no();
}
static void progress_files_menu(void){
  if(!progress_neon_book() && !t_layout()){ message("TAMA SAVE FORMAT","NOT SUPPORTED",""); return; }
  u32 nf=progress_fields(), nr=0;
  for(u32 i=0;i<nf;i++){
    progress_chars(i);
    if(g_chrn==progress_count(i)) g_progress_regions[nr++]=(u8)i;
  }
  if(!nr){ message("NO TAMA FILES","",""); return; }
  u32 sel=0;
  for(;;){
    int action=progress_choose("TAMA FILE",3u,sel,progress_action_row);
    if(action<0) return;
    sel=(u32)action;
    if(action==2){
      if(progress_confirm("ALL FIELDS",progress_apply(0,nf,0))){
        progress_apply(0,nf,1); set_write("TAMA FILES SAVED");
      }
      continue;
    }
    int field=progress_choose("CHOOSE FIELD",nr,0,progress_region_row);
    if(field<0) continue;
    u32 region=g_progress_regions[field];
    if(action==1){
      if(progress_confirm(progress_field_name(region),progress_apply(region,region+1u,0))){
        progress_apply(region,region+1u,1); set_write("TAMA FILES SAVED");
      }
      continue;
    }
    progress_chars(region);
    g_progress_field=region;
    int index=progress_choose(progress_field_name(region),g_chrn,0,progress_char_row);
    if(index<0) continue;
    u32 i=(u32)index, id=rd16(g_cfg.cat_off+8u+28u*g_chr[i]);
    char name[24]; chr_name(g_chr[i],name);
    u32 bit=progress_flag(region,i,id);
    if(progress_bit(bit,0)){
      if(progress_confirm(name,1)){ progress_bit(bit,1); set_write("TAMA FILE SAVED"); }
    } else if(progress_reset_confirm("LOCK TAMA FILE?",name,"HIDE THIS ENTRY")){
      u32 viewed=bit+8u*(progress_neon_book()?NEON_BOOK_VIEWED-NEON_BOOK_UNLOCKED:
                         progress_imported(region)?24u:region>=3u?12u:16u);
      progress_clear_bit(bit); progress_clear_bit(viewed);
      set_write("TAMA FILE LOCKED");
    }
  }
}

/* Native mission records occupy the same settings range on Land/Water/Sky
 * and Jade Forest. Tropical/Glacier insert 12 bytes before this range. */
static const struct { const char *name; u16 target; u8 offset; } missions[]={
  {"FEED MEALS",500,0xB8}, {"FEED SNACKS",500,0xBA}, {"CLEAN TAMAS",100,0xBC},
  {"PLAY WINS",100,0xBE}, {"CLEAN POOP",100,0xC0}, {"COLLECT POOP",1000,0xC2},
  {"FAST COLLECTIONS",10,0xC4}, {"VISIT PLANETS",7,0xC6}, {"PUT TO SLEEP",50,0xC8},
  {"WAKE TAMAS",50,0xCA}, {"CONNECTIONS",100,0xCC}, {"SEND GIFTS",50,0xCE},
  {"MINIGAME WINS",100,0xD0}, {"EARN POINTS",0,0xD4}, {"BREEDING",50,0xD8},
  {"EGG HUNT WINS",50,0xDA}, {"EGG FOOD TYPES",18,0xDC}, {"CHEF MEALS",12,0xDF},
  {"LAND ADULTS",16,0xE1}, {"WATER ADULTS",16,0xE3}, {"SKY ADULTS",16,0xE5},
  {"TAMA COLORS",16,0xE7},
};
#define MISSION_COUNT (sizeof(missions)/sizeof(missions[0]))
#define MISSION_EARNED 13u
static u32 mission_shift(void){ return g_cfg.tama_off==0x114u?12u:0u; }
static u32 mission_flag(u32 index){ return (0xF4u+mission_shift())*8u+index; }
static u32 mission_apply(u32 first,u32 end,int unlock){
  u32 count=0, shift=mission_shift();
  for(u32 i=first;i<end;i++){
    u32 bit=mission_flag(i);
    if(!progress_bit(bit,0)) continue;
    count++;
    if(!unlock) continue;
    u32 off=missions[i].offset+shift, target=missions[i].target;
    if(i==MISSION_EARNED){
      /* A lifetime earnings counter used only by Earn Points, separate from the wallet. */
      u32 earned=0; for(u32 j=0;j<4u;j++) earned|=(u32)g_set[off+j]<<(8u*j);
      if(earned<99999u) for(u32 j=0;j<4u;j++) g_set[off+j]=(u8)(99999u>>(8u*j));
    } else if(i>=16u){
      for(u32 j=0;j<target;j++) progress_bit(off*8u+j,1);
    } else if((g_set[off]|((u32)g_set[off+1u]<<8))<target){
      g_set[off]=(u8)target; g_set[off+1u]=(u8)(target>>8);
    }
    /* Set all three native flags so the game sees a completed mission without a pending reward. */
    for(u32 word=0;word<3u;word++) progress_bit(bit-32u*word,1);
  }
  return count;
}
static int mission_action_row(u32 i,char *name,char *right,u16 *col){
  const char *s=i==0u?"ONE MISSION":"ALL MISSIONS"; while((*name++=*s++)){}
  right[0]=0; *col=i==0u?WHITE:ORANGE; return 1;
}
static int mission_row(u32 i,char *name,char *right,u16 *col){
  const char *s=missions[i].name; while((*name++=*s++)){}
  int pending=progress_bit(mission_flag(i),0);
  s=pending?"TODO":"DONE"; while((*right++=*s++)){}
  *col=pending?WHITE:GREY; return 1;
}
static void mission_confirm(u32 first,u32 end,const char *name,u32 (*apply)(u32,u32,int)){
  u32 count=apply(first,end,0);
  if(!count){ message("ALREADY COMPLETE","NO NEW MISSIONS",""); return; }
  char n[5];
  rect(0,0,128,128,BG); menu_title(10,"COMPLETE MISSIONS"); ctext(28,name,WHITE,1);
  utoa_(count,n); ctext(48,n,WHITE,2);
  ctext(75,"TO COMPLETE",GREY,1);
  ctext(113,"B SAVE  C CANCEL",GREY,1); show();
  if(yes_no()){ apply(first,end,1); set_write("MISSIONS SAVED"); }
}
static void mission_reset(u32 index){
  u32 bit=mission_flag(index), off=missions[index].offset+mission_shift();
  u32 target=missions[index].target;
  if(index>=16u){
    /* Count all bits in this record, but keep spare bits beyond the unlock mask. */
    u32 count=0, bits=(target+7u)&~7u;
    for(u32 i=0;i<bits;i++) count+=!progress_bit(off*8u+i,0);
    for(u32 i=target;i && count>=target;){
      u32 seen=off*8u+--i;
      if(!progress_bit(seen,0)){ progress_clear_bit(seen); count--; }
    }
  } else {
    u32 size=index==MISSION_EARNED?4u:2u, count=0;
    if(index==MISSION_EARNED) target=99999u;
    for(u32 i=0;i<size;i++) count|=(u32)g_set[off+i]<<(8u*i);
    if(count>=target) for(u32 i=0;i<size;i++) g_set[off+i]=(u8)((target-1u)>>(8u*i));
  }
  for(u32 word=0;word<3u;word++) progress_clear_bit(bit-32u*word);
}
static void missions_menu(void){
  if(!t_layout() || g_cfg.money_off!=0xB4u+mission_shift()){
    message("MISSION SAVE FORMAT","NOT SUPPORTED",""); return;
  }
  u32 sel=0, mission=0;
  for(;;){
    int action=progress_choose("MISSIONS",2u,sel,mission_action_row);
    if(action<0) return;
    sel=(u32)action;
    if(sel==1u) mission_confirm(0,MISSION_COUNT,"ALL MISSIONS",mission_apply);
    else {
      int picked=progress_choose("CHOOSE MISSION",MISSION_COUNT,mission,mission_row);
      if(picked<0) continue;
      mission=(u32)picked;
      const char *name=missions[mission].name;
      if(progress_bit(mission_flag(mission),0)) mission_confirm(mission,mission+1u,name,mission_apply);
      else if(progress_reset_confirm("RESET MISSION?",name,"RETURN TO TODO")){
        mission_reset(mission); set_write("MISSION RESET");
      }
    }
  }
}
static void progress_menu(void){
  if(set_check_msg()) return;
  u32 sel=0;
  for(;;){
    int action=progress_choose("PROGRESS",3u,sel,progress_root_row);
    if(action<0) return;
    sel=(u32)action;
    if(sel==0u) level_edit();
    else if(sel==1u) progress_files_menu();
    else missions_menu();
  }
}

#ifndef FELD_CLEAN
/* Offsets within the CRC-prefixed Neon save, in firmware mission order.
 * Negative widths describe the number of bytes in a discovery bitset. */
static const struct { u8 offset; signed char width; } neon_mission_records[]={
  {0xC4,2}, {0xC6,2}, {0xC8,2}, {0xCA,2}, {0xCC,2}, {0xCE,2}, {0xD0,2}, {0xD2,-1},
  {0xD4,2}, {0xD6,2}, {0xD8,2}, {0xDA,2}, {0xDC,2}, {0xE0,4}, {0xE4,2}, {0xE6,2},
  {0xE8,-3}, {0xEB,-2}, {0xED,-2}, {0xEF,-2}, {0xF1,-2}, {0xF3,-2},
};
_Static_assert(sizeof(neon_mission_records)/sizeof(neon_mission_records[0])==MISSION_COUNT,
               "Neon mission records and names must use the same order");
#define NEON_LEVEL_OFF 0x88u
#define NEON_MISSION_COMPLETE_OFF 0xF8u
#define NEON_MISSION_VIEWED_OFF 0xFCu
#define NEON_MISSION_CLAIMED_OFF 0x100u
#define NEON_CURRENT_PLANET_OFF 0x207u

static void neon_mission_set(u32 i,int complete){
  u32 target=i==MISSION_EARNED?99999u:missions[i].target;
  u32 off=neon_mission_records[i].offset;
  int width=neon_mission_records[i].width;
  u32 count=0;
  if(width<0){
    u32 start=off*8u, bits=(u32)(-width)*8u;
    for(u32 n=0;n<bits;n++) count+=!progress_bit(start+n,0);
    if(complete){
      for(u32 n=0;n<bits && count<target;n++) count+=progress_bit(start+n,1);
    } else {
      for(u32 n=bits;n && count>=target;){
        u32 bit=start+--n;
        if(!progress_bit(bit,0)){ progress_clear_bit(bit); count--; }
      }
    }
  } else {
    for(int n=0;n<width;n++) count|=(u32)g_set[off+(u32)n]<<(8u*(u32)n);
    if(complete ? count<target : count>=target){
      u32 value=complete?target:target-1u;
      for(int n=0;n<width;n++) g_set[off+(u32)n]=(u8)(value>>(8u*(u32)n));
    }
  }
  /* A manual completion carries no payout and produces no native notification. */
  const u32 flags[]={NEON_MISSION_COMPLETE_OFF,NEON_MISSION_VIEWED_OFF,NEON_MISSION_CLAIMED_OFF};
  for(u32 n=0;n<3u;n++){
    u32 bit=8u*flags[n]+i;
    if(complete) progress_bit(bit,1);
    else progress_clear_bit(bit);
  }
}
static u32 neon_mission_apply(u32 first,u32 end,int unlock){
  u32 count=0;
  for(u32 i=first;i<end;i++){
    if(!progress_bit(NEON_MISSION_COMPLETE_OFF*8u+i,0)) continue;
    count++;
    if(unlock) neon_mission_set(i,1);
  }
  return count;
}
static const char *neon_mission_name(u32 i){
  static const char *const planets[]={"COSMIC TAMAS","NEW TAMA KINDS","TAMA PLANET KINDS"};
  return i>=18u && i<=20u?planets[i-18u]:missions[i].name;
}
static int neon_mission_row(u32 i,char *name,char *right,u16 *col){
  const char *s=neon_mission_name(i); while((*name++=*s++)){}
  int pending=progress_bit(NEON_MISSION_COMPLETE_OFF*8u+i,0);
  s=pending?"TODO":"DONE"; while((*right++=*s++)){}
  *col=pending?WHITE:GREY; return 1;
}
static void neon_missions_menu(void){
  u32 sel=0, mission=0;
  for(;;){
    int action=progress_choose("MISSIONS",2u,sel,mission_action_row);
    if(action<0) return;
    sel=(u32)action;
    if(sel==1u) mission_confirm(0,MISSION_COUNT,"ALL MISSIONS",neon_mission_apply);
    else {
      int picked=progress_choose("CHOOSE MISSION",MISSION_COUNT,mission,neon_mission_row);
      if(picked<0) continue;
      mission=(u32)picked;
      const char *name=neon_mission_name(mission);
      if(progress_bit(NEON_MISSION_COMPLETE_OFF*8u+mission,0))
        mission_confirm(mission,mission+1u,name,neon_mission_apply);
      else if(progress_reset_confirm("RESET MISSION?",name,"RETURN TO TODO")){
        neon_mission_set(mission,0); set_write("MISSION RESET");
      }
    }
  }
}
#define NEON_PLANET_UNLOCK_OFF 0x89u
#define NEON_PLANET_UNLOCK_MASK 7u
static const char *const neon_planets[]={"COSMIC PLANET","NEW TAMA PLANET","TAMA PLANET"};
#define NEON_PLANET_COUNT (sizeof(neon_planets)/sizeof(neon_planets[0]))

static int neon_planet_unlocked(u32 planet){ return (g_set[NEON_PLANET_UNLOCK_OFF] & (1u<<planet))!=0; }

static int neon_planet_row(u32 i,char *name,char *right,u16 *col){
  const char *s=neon_planets[i]; while((*name++=*s++)){}
  int current=i==g_set[NEON_CURRENT_PLANET_OFF], unlocked=neon_planet_unlocked(i);
  s=unlocked ? (current ? "HERE" : "OPEN") : "LOCKED";
  while((*right++=*s++)){}
  *col=current ? GOLD : unlocked ? WHITE : GREY;
  return 1;
}

static int neon_planet_access_row(u32 i,char *name,char *right,u16 *col){
  if(i<NEON_PLANET_COUNT) return neon_planet_row(i,name,right,col);
  const char *s="UNLOCK ALL"; while((*name++=*s++)){}
  right[0]=0; *col=ORANGE;
  return 1;
}

static void neon_planet_access_menu(void){
  u32 sel=0;
  for(;;){
    int picked=progress_choose("PLANET ACCESS",NEON_PLANET_COUNT+1u,sel,neon_planet_access_row);
    if(picked<0) return;
    sel=(u32)picked;
    if(sel==NEON_PLANET_COUNT){
      if((g_set[NEON_PLANET_UNLOCK_OFF]&NEON_PLANET_UNLOCK_MASK)==NEON_PLANET_UNLOCK_MASK) continue;
      if(!progress_reset_confirm("UNLOCK ALL?","ALL THREE PLANETS","")) continue;
      g_set[NEON_PLANET_UNLOCK_OFF]|=NEON_PLANET_UNLOCK_MASK;
      set_write("PLANETS UNLOCKED");
      continue;
    }
    int unlocked=neon_planet_unlocked(sel);
    if(unlocked && sel==g_set[NEON_CURRENT_PLANET_OFF]){
      message("CURRENT PLANET","MOVE TAMA FIRST","");
      continue;
    }
    if(!progress_reset_confirm(unlocked?"LOCK PLANET?":"UNLOCK PLANET?",neon_planets[sel],"")) continue;
    g_set[NEON_PLANET_UNLOCK_OFF]^=(u8)(1u<<sel);
    set_write(unlocked?"PLANET LOCKED":"PLANET UNLOCKED");
  }
}

static void neon_planet_menu(void){
  u32 current=g_set[NEON_CURRENT_PLANET_OFF];
  u32 sel=current<NEON_PLANET_COUNT?current:0u;
  for(;;){
    int picked=progress_choose("CURRENT PLANET",NEON_PLANET_COUNT,sel,neon_planet_row);
    if(picked<0 || (u32)picked==current) return;
    sel=(u32)picked;
    u32 level=g_set[NEON_LEVEL_OFF]&15u;
    if(!neon_planet_unlocked(sel)){
      message("PLANET LOCKED","USE PLANET ACCESS","");
      continue;
    }
    if(!progress_reset_confirm("CHANGE PLANET?",neon_planets[sel],
                               level==4u?"GALAXY LV.4 TO LV.5":"MOVE CURRENT TAMA")) continue;
    g_set[NEON_CURRENT_PLANET_OFF]=(u8)sel;
    /* Match the native switch's three resets and the Lv.4 move milestone. */
    for(u32 off=0x162u;off<0x168u;off++) g_set[off]=0;
    if(level==4u) g_set[NEON_LEVEL_OFF]=0x45u;
    set_write("PLANET CHANGED");
    return;
  }
}

static int neon_progress_root_row(u32 i,char *name,char *right,u16 *col){
  static const char *const names[]={"GALAXY LEVEL","MISSIONS","TAMA FILE","CURRENT PLANET","PLANET ACCESS"};
  const char *s=names[i]; while((*name++=*s++)){}
  if(i==0u) utoa_(g_set[NEON_LEVEL_OFF]&15u,right);
  else right[0]=0;
  *col=i==0u?GOLD:WHITE; return 1;
}
static void neon_progress_menu(void){
  if(set_check_msg()) return;
  u32 sel=0;
  for(;;){
    int action=progress_choose("PROGRESS",5u,sel,neon_progress_root_row);
    if(action<0) return;
    sel=(u32)action;
    if(sel==0u) level_edit_at(NEON_LEVEL_OFF,"GALAXY LEVEL",1);
    else if(sel==1u) neon_missions_menu();
    else if(sel==2u) progress_files_menu();
    else if(sel==3u) neon_planet_menu();
    else neon_planet_access_menu();
  }
}
#endif
