/* Tama File flags in the complete, CRC-prefixed settings page.
 * Land/Water/Sky index by the suffix of the character ID. Jade and later
 * have three 32-bit words indexed by the ROM's field and list order. */
#define BOOK_EXTRA_OFF 0xF80u
#define BOOK_FILE_COUNT 22u

static u32 progress_fields(void){
  u32 n=nfields();
  return (g_cfg.flags&F_BOOK6) || n<3u ? n : 3u;
}
static void progress_chars(u32 field){
  g_chrn=0;
  for(u32 i=0;i<g_cfg.cat_n && g_chrn<256u;i++){
    u32 a=g_cfg.cat_off+8u+28u*i;
    if(fl(a+2u)[0]==2u && fl(a+6u)[0]>=3u && fl(a+7u)[0]==field+1u)
      g_chr[g_chrn++]=(u16)i;
  }
}
static u32 progress_flag(u32 field,u32 index,u32 id){
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
static u32 progress_apply(u32 first,u32 end,int unlock){
  u32 missing=0;
  for(u32 field=first;field<end;field++){
    progress_chars(field);
    if(g_chrn!=BOOK_FILE_COUNT) continue;
    for(u32 i=0;i<g_chrn;i++){
      u32 id=rd16(g_cfg.cat_off+8u+28u*g_chr[i]);
      missing+=(u32)progress_bit(progress_flag(field,i,id),unlock);
    }
  }
  return missing;
}
static int progress_root_row(u32 i,char *name,char *right,u16 *col){
  const char *p=i==0u?"PLANET LEVEL":"TAMA FILE"; while((*name++=*p++)){}
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
static u8 g_progress_regions[NF_MAX];
static int progress_region_row(u32 i,char *name,char *right,u16 *col){
  u32 field=g_progress_regions[i];
  const char *p=fname(field); while((*name++=*p++)){}
  right[0]=0; *col=fcol(field); return 1;
}
static u32 g_progress_field;
static int progress_char_row(u32 i,char *name,char *right,u16 *col){
  u32 id=rd16(g_cfg.cat_off+8u+28u*g_chr[i]);
  chr_name(g_chr[i],name);
  int missing=progress_bit(progress_flag(g_progress_field,i,id),0);
  const char *p=missing?"LOCKED":"OPEN"; while((*right++=*p++)){}
  *col=missing?GREY:fcol(g_progress_field); return 1;
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
static void progress_files_menu(void){
  if(!t_layout()){ message("TAMA SAVE FORMAT","NOT SUPPORTED",""); return; }
  u32 nf=progress_fields(), nr=0;
  for(u32 i=0;i<nf;i++){
    progress_chars(i);
    if(g_chrn==BOOK_FILE_COUNT) g_progress_regions[nr++]=(u8)i;
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
      if(progress_confirm(fname(region),progress_apply(region,region+1u,0))){
        progress_apply(region,region+1u,1); set_write("TAMA FILES SAVED");
      }
      continue;
    }
    progress_chars(region);
    g_progress_field=region;
    int index=progress_choose(fname(region),g_chrn,0,progress_char_row);
    if(index<0) continue;
    u32 i=(u32)index, id=rd16(g_cfg.cat_off+8u+28u*g_chr[i]);
    char name[24]; chr_name(g_chr[i],name);
    u32 bit=progress_flag(region,i,id);
    if(progress_confirm(name,(u32)progress_bit(bit,0))){
      progress_bit(bit,1); set_write("TAMA FILE SAVED");
    }
  }
}
static void progress_menu(void){
  if(set_check_msg()) return;
  u32 sel=0;
  for(;;){
    int action=progress_choose("PROGRESS",2u,sel,progress_root_row);
    if(action<0) return;
    sel=(u32)action;
    if(sel==0u) level_edit();
    else progress_files_menu();
  }
}
