/* Synthetic saves and optional regression against a local Tropical ROM. */
#include "feld_test_support.h"
static void pet_fixture(u32 t){
  fixture(); g_cfg.flags=F_TAMA; g_cfg.version=6; g_cfg.tama_off=t; g_cfg.level_off=0x7C;
  g_cfg.user_start=0xD00000; g_cfg.user_end=SET_A;
  const u32 table=t==0x114u?0xACBB8u:t==0x108u?0x9954Cu:0x94214u;
  const u32 record=t==0x114u?0xDCE000u:0xDEE000u;
  const u32 hook=t==0x114u?0x48A50u:t==0x108u?0x43E8Cu:0x41E84u;
  for(u32 i=0;i<3;i++) sl_w32(host_flash+table+4u*i,0x60000000u+record+i*0x4000u);
  sl_w32(host_flash+hook+0x18u,0x4F7FF1B2u);
  sl_w32(host_flash+hook+0x1Cu,0xD00Bu);
  t_put(4,4001); t_put(6,4002); t_put(0xC,25);
  g_set[t+0x18]=(5u<<2)|0xA0u; g_set[t+0x19]=0xBF; /* default, upper bits kept */
  g_set[t+0x1C]=2; g_set[t+0x1D]=3; g_set[t+0x10]=7;
  stamp(g_set); memcpy(host_flash+SET_A,g_set,PAGE); memcpy(host_flash+SET_B,g_set,PAGE);
  u8 *p=host_flash+g_cfg.user_start; memset(p,0x5A,PAGE);
  sl_w32(p+8,1); p[12]=0xA1; p[13]=0x0F; p[14]=0xA2; p[15]=0x0F;
  p[16]=255; p[0x10A]=0xA2; p[0x10B]=0x0F;
  g_cfg.cat_n=3;
  const u16 ids[3]={1001,4001,4002}; const u8 stages[3]={2,5,5};
  for(u32 k=0;k<3;k++){
    u32 o=g_cfg.cat_off+8+28*k; host_flash[o]=(u8)ids[k]; host_flash[o+1]=(u8)(ids[k]>>8); host_flash[o+2]=2;
    host_flash[o+3]=16; host_flash[o+6]=stages[k]; host_flash[o+7]=0;
    memcpy(host_flash+o+8,"LONG TAMA NAME!!",16);
  }
}
static void saved(void){
  assert(!set_load() && set_ok(host_flash+SET_A));
  assert(!memcmp(host_flash+SET_A,host_flash+SET_B,PAGE));
}
static void fields(u32 t){
  pet_fixture(t); assert(t_alive() && t_layout() && t_color()==16 && t_word(0xC)==25);
  u32 old_fields=g_cfg.nfields; g_cfg.nfields=3; g_cfg.field_col[1]=RGB(90,160,255);
  host_flash[g_cfg.cat_off+8+28+7]=2;
  assert(chr_color(4001,WHITE)==RGB(90,160,255));
  assert(chr_color(1001,WHITE)==WHITE && chr_color(9999,GREY)==GREY);
  g_cfg.nfields=1; assert(chr_color(4001,WHITE)==WHITE); g_cfg.nfields=old_fields;
  u8 before[PAGE],want[PAGE]; memcpy(before,g_set,PAGE); memcpy(want,host_flash+g_cfg.user_start,PAGE);
  assert(t_visual(4001,4001,3,5));
  assert(t_word(4)==4001 && t_word(6)==4001 && t_color()==3 && g_set[t+0x19]==0xA3);
  assert(!memcmp(want,host_flash+g_cfg.user_start,PAGE));
  before[t+6]=0xA1; before[t+0x19]=0xA3; assert(!memcmp(before,g_set,PAGE));
  assert(t_visual(4002,4001,16,5)); assert(t_word(4)==4002 && t_word(6)==4001 && t_color()==16);
  assert(!memcmp(want,host_flash+g_cfg.user_start,PAGE));
  /* Absent, different and duplicate ghosts are unrelated to the active pet. */
  memset(host_flash+g_cfg.user_start,0xFF,PAGE); assert(t_visual(4001,4002,2,5));
  memset(host_flash+g_cfg.user_start,0x5A,PAGE);
  memcpy(host_flash+g_cfg.user_start+PAGE,host_flash+g_cfg.user_start,PAGE);
  assert(t_visual(4002,4001,14,5));
  for(u32 i=0;i<PAGE*2;i++) assert(host_flash[g_cfg.user_start+i]==0x5A);
  memcpy(before,g_set,PAGE);
  assert(!t_visual(4001,4001,17,5) && !t_visual(4001,4001,0,1));
  assert(!t_visual(0,4001,0,5) && !t_visual(4001,65535,0,5));
  assert(!memcmp(before,g_set,PAGE));
  pet_fixture(t); memset(g_set+t+0x20,0xFF,0x10); memset(g_set+t+0x48,0xFF,4);
  g_set[t+0x14]=255; g_set[t+0x15]=g_set[t+0x16]=g_set[t+0x17]=255;
  g_set[t-0xB]=g_set[t-0xC]=255; t_stage_set(2);
  assert(t_stage()==2 && g_set[t+0x18]==0xA8 && g_set[t+0x14]==0x10);
  for(u32 i=0x20;i<=0x2D;i++) assert(!g_set[t+i]);
  for(u32 i=0x48;i<=0x4B;i++) assert(!g_set[t+i]);
  assert(!g_set[t+0x15] && !g_set[t+0x16] && !g_set[t+0x17] && !g_set[t-0xB] && !g_set[t-0xC]);
  pet_fixture(t); g_set[t+0x14]=0xFF; g_set[t-8]=0xFF; g_set[t-3]=0xFF;
  u32 heal=t==0x104?0x180:t==0x108?0x184:0x198;
  memset(g_set+heal,0xFF,8); g_set[t+0x40]=g_set[t+0x41]=255;
  t_care(8); assert(g_set[t+0x14]==0xEF && g_set[t+0x10]==7 && g_set[t+0x1C]==2 && g_set[t+0x1D]==3);
  t_care(1); assert(g_set[t+0x1C]==6 && g_set[t+0x1D]==3 && g_set[t+0x10]==7);
  assert(!g_set[t+0x40] && !g_set[t+0x41]);
  assert(g_set[t-8]==0xFD && g_set[t-3]==0xFD);
  for(u32 i=4;i<8;i++) assert(!g_set[heal+i]);
  t_care(2); assert(g_set[t+0x1D]==20 && g_set[t-8]==0xF9 && g_set[t-3]==0xF9);
  t_care(4); assert(!g_set[t+0x10]); for(u32 i=0;i<4;i++) assert(!g_set[heal+i]);
}
static void body_stages(u32 t){
  for(u32 bs=2;bs<=5;bs++) for(u32 es=2;es<=5;es++){
    pet_fixture(t);
    host_flash[g_cfg.cat_off+8+6]=(u8)bs;
    host_flash[g_cfg.cat_off+8+28*2+6]=(u8)es;
    u32 expected=bs==es?4002:1001;
    assert(chr_body_eyes(1001,t_word(6))==expected);
    assert(t_visual(1001,chr_body_eyes(1001,t_word(6)),t_color(),bs));
    assert(t_word(4)==1001 && t_word(6)==expected && t_stage()==bs);
    assert(rd16(g_cfg.user_start+14)==4002 && rd16(g_cfg.user_start+0x10A)==4002); /* ghosts untouched */
    assert(host_flash[g_cfg.user_start+16]==255); /* color preserved */
  }
  pet_fixture(t); t_put(6,9999); assert(chr_body_eyes(1001,t_word(6))==1001); /* unknown eyes */
  pet_fixture(t); host_flash[g_cfg.cat_off+8+28*2+2]=1;
  assert(chr_body_eyes(4001,t_word(6))==4001); /* an item cannot validate eyes */
}
static void appearance_cancel(u32 t){
  const char *seq[]={"ac","abcc","abbccc","abbbcccc"};
  for(u32 i=0;i<4;i++){
    pet_fixture(t); u8 settings[PAGE], own[PAGE];
    memcpy(settings,g_set,PAGE); memcpy(own,host_flash+g_cfg.user_start,PAGE);
    keys(seq[i]); appearance_edit(); assert(!g_seq[g_pos]);
    assert(!memcmp(settings,g_set,PAGE) && !memcmp(settings,host_flash+SET_A,PAGE));
    assert(!memcmp(settings,host_flash+SET_B,PAGE) && !memcmp(own,host_flash+g_cfg.user_start,PAGE));
  }
}
static void appearance_hints(void){
  pet_fixture(0x104); u32 body=t_id(), eyes=t_word(6); u8 before[PAGE]; memcpy(before,g_set,PAGE);
  keys("b"); assert(appearance_character(1,&body,&eyes,t_color()));
  u32 green=0; for(u32 y=111;y<118;y++) for(u32 x=0;x<128;x++) green+=g_fb[y*128+x]==RGB(120,240,140);
  assert(!green && eyes==4002); /* custom, same-stage eyes are not the original pair */
  keys("nb"); assert(appearance_character(1,&body,&eyes,t_color()));
  green=0; for(u32 y=111;y<118;y++) for(u32 x=0;x<128;x++) green+=g_fb[y*128+x]==RGB(120,240,140);
  assert(green && eyes==body && !memcmp(before,g_set,PAGE));
}
static void neon_pet(void){
  pet_fixture(0x114u);
  g_cfg.flags=F_APPS|F_NEON_EDIT|F_NEON_TAMA;
  g_cfg.level_off=0;
  g_cfg.user_start=NEON_SET_FIRST; g_cfg.user_end=0xF00000u;
  const u32 record=0xE4E000u, table=0xAE954u, hook=0x4902Cu;
  sl_w32(host_flash+table,0x60E4E000u);
  sl_w32(host_flash+table+4u,0x60D8F000u);
  sl_w32(host_flash+table+8u,0x60E52000u);
  sl_w32(host_flash+hook,0x2190F241u);
  sl_w32(host_flash+hook+0x14u,0x681202E4u);
  sl_w32(host_flash+hook+0x18u,0x4F7FF1B2u);
  sl_w32(host_flash+hook+0x1Cu,0xD00Bu);
  sl_w32(host_flash+record,123u);
  sl_w32(host_flash+record+4u,0u-123u);
  sl_w32(host_flash+record+0x10Cu,0x2BA4u);
  host_flash[record+0x10Au]=0xA1; host_flash[record+0x10Bu]=0x0F;
  sl_w32(g_set+NEON_SET_GENERATION,40u);
  stamp(g_set); memcpy(host_flash+NEON_SET_FIRST,g_set,PAGE);
  u8 before[PAGE]; memcpy(before,g_set,PAGE);
  assert(tama_ok() && !set_load() && t_active_record()==record);
  assert(!t_visual(4001,1001,1,5) && !memcmp(before,g_set,PAGE));
  keys("nbnbpbbb"); appearance_edit();
  assert(!g_seq[g_pos] && t_id()==4002 && t_word(6)==4001 && t_color()==15);
  assert(t_stage()==5 && t_word(0xCu)==25 && t_active_record()==record);
  assert(!memcmp(before,host_flash+NEON_SET_FIRST,PAGE));
  assert(set_crc_ok(host_flash+NEON_SET_FIRST+PAGE) && neon_set_generation(g_set)==41u);
  assert(rd32(record+0x10Cu)==T_REBUILD_PENDING && rd16(record+0x10Au)==4001u);
  assert(neon_set_generation(host_flash+SET_A)==0u && neon_set_generation(host_flash+SET_B)==0u);
  g_set[0x128]=0xF0; g_set[0x150]=0x5A;
  t_care(15u);
  assert(g_set[0x130]==6u && g_set[0x131]==20u && g_set[0x124]==0u && g_set[0x128]==0xE0);
  assert(g_set[0x150]==0x5A);
}
static void ui(void){
  pet_fixture(0x104); u8 before[PAGE]; memcpy(before,host_flash+SET_A,PAGE);
  keys("...n...c"); tama_number(0); assert(!g_seq[g_pos] && !memcmp(before,host_flash+SET_A,PAGE));
  keys("aanbbbc"); /* age: step 100 -> 125, save, stay; no extra C consumed */
  tama_number(0); assert(g_seq[g_pos]=='c'); saved(); assert(t_word(0xC)==125);
  keys("nbnbpbbb"); appearance_edit(); saved();
  assert(t_word(4)==4002 && t_word(6)==4001 && t_color()==15 && t_stage()==5);
  /* Back through each step, then cancel Body: no partial save or own-Tama writes. */
  u8 own[PAGE]; memcpy(own,host_flash+g_cfg.user_start,PAGE); memcpy(before,host_flash+SET_A,PAGE);
  keys("abbbcccc"); appearance_edit();
  assert(!g_seq[g_pos] && !memcmp(before,host_flash+SET_A,PAGE) && !memcmp(own,host_flash+g_cfg.user_start,PAGE));
  keys("abbbbb"); appearance_edit(); saved();
  assert(t_word(4)==1001 && t_word(6)==1001 && t_stage()==2 && t_color()==15);
  /* Same-stage eye list cannot leave Baby via A; color wraps both ways. */
  keys("babnbbb"); appearance_edit(); saved();
  assert(t_word(4)==1001 && t_word(6)==1001 && t_color()==16);
  keys("bbnbbb"); appearance_edit(); saved(); assert(t_color()==0); /* max wraps to min */
  keys("bbpbbb"); appearance_edit(); saved(); assert(t_color()==16); /* min wraps to max */
  /* Long character names stay in the 24-byte name buffer, never right[8]. */
  char name[24]; tama_value(1,name); assert(!strcmp(name,"LONG TAMA NAME!!"));
  assert(!strcmp(tama_rows[0],"CARE"));
  tama_value(2,name); assert(!strcmp(name,"125"));
  assert(!strcmp(tama_rows[1],"APPEARANCE") && !strcmp(tama_rows[3],"PREVIEW"));
  keys("...c"); tama_menu(); assert(!g_seq[g_pos]);
  keys("...p...b...c...c"); tama_menu(); assert(!g_seq[g_pos]); /* current preview, back, back */
  keys("...c"); care_menu(); assert(!g_seq[g_pos]);
  /* Select clean only, review/cancel, review/save, remain in Care, back. */
  g_set[0x118]=0xFF; stamp(g_set); memcpy(host_flash+SET_A,g_set,PAGE);
  keys("nnnbnnnnbcbbbc"); care_menu(); saved();
  assert(g_set[0x118]==0xEF && !g_care_mask && !g_seq[g_pos]);
  t_put(0xC,0); keys("pbbb"); tama_number(0); saved(); assert(t_word(0xC)==999);
  keys("nbbb"); tama_number(0); saved(); assert(!t_word(0xC));
  g_set[0x120]=0; keys("pbbb"); tama_number(1); saved(); assert(g_set[0x120]==6);
  g_set[0x121]=20; keys("nbbb"); tama_number(2); saved(); assert(!g_set[0x121]);
  memcpy(before,host_flash+SET_A,PAGE);
  keys("bbpccc"); appearance_edit(); assert(!g_seq[g_pos] && !memcmp(before,host_flash+SET_A,PAGE));
}
static void real_appearance(const char *file,u32 cfg){
  FILE *f=fopen(file,"rb"); assert(f && fread(host_flash,1,0x1000000,f)==0x1000000); fclose(f);
  memcpy((void*)&g_cfg,host_flash+cfg,sizeof g_cfg);
  assert(!set_load() && t_alive() && g_cfg.tama_off==0x114u && t_stage()==5);
  for(u32 field=0;field<3u;field++){
    u32 row=0xAED4Cu+8u*field, table=rd32(row)-0x10000000u+XIP_DEC;
    progress_chars(field);
    assert(g_chrn==BOOK_FILE_COUNT && g_chrn==rd32(row+4u));
    for(u32 i=0;i<g_chrn;i++) assert(rd16(g_cfg.cat_off+8u+28u*g_chr[i])==rd16(table+2u*i));
  }
  u32 record=t_active_record();
  assert(record==0xDCE000u);
  u8 *before=malloc(0x1000000); assert(before); memcpy(before,host_flash,0x1000000);
  u32 body=t_id(), eyes=t_word(6), color=t_color();
  keys("bbbbb"); appearance_edit(); saved();
  assert(!g_seq[g_pos] && t_id()==body && t_word(6)==eyes && t_color()==color);
  assert(!memcmp(before+SET_A,host_flash+SET_A,PAGE)); /* re-save an edit from v27 */
  assert(rd32(record+0x10Cu)==T_REBUILD_PENDING);
  assert(!memcmp(before,host_flash,record+0x10Cu));
  assert(!memcmp(before+record+0x110u,host_flash+record+0x110u,SET_A-record-0x110u));
  assert(!memcmp(before+SET_B+PAGE,host_flash+SET_B+PAGE,0x1000000-SET_B-PAGE));
  free(before); puts("PASS: re-saving the appearance queues a rebuild only for the active Tama");
}
static void progress_unlocks(void){
  pet_fixture(0x114u); g_cfg.nfields=6;
  g_cfg.cat_n=6u*BOOK_FILE_COUNT;
  for(u32 i=0;i<g_cfg.cat_n;i++){
    u32 a=g_cfg.cat_off+8u+28u*i, id=4001u+i;
    host_flash[a]=(u8)id; host_flash[a+1]=(u8)(id>>8);
    host_flash[a+2]=2; host_flash[a+3]=4; host_flash[a+6]=5;
    host_flash[a+7]=(u8)(1u+i/BOOK_FILE_COUNT);
    memcpy(host_flash+a+8,"TAMA",4);
  }
  u8 before[PAGE]; memcpy(before,g_set,PAGE);
  assert(progress_fields()==3u && progress_apply(0,3,0)==66u);
  keys("nbbbbccc"); progress_menu();
  assert(!g_seq[g_pos] && !memcmp(before,host_flash+SET_A,PAGE));
  keys("nbbbbbbcc"); progress_menu(); saved();
  assert(!g_seq[g_pos] && sl_r32(g_set+0x94u)==1u && progress_apply(0,3,0)==65u);
  keys("nbnbnbbbcc"); progress_menu(); saved();
  assert(!g_seq[g_pos] && sl_r32(g_set+0x98u)==0x3FFFFFu);
  keys("nbpbbbcc"); progress_menu(); saved();
  assert(!g_seq[g_pos] && progress_apply(0,3,0)==0 && !sl_r32(g_set+BOOK_EXTRA_OFF));
  g_cfg.flags|=F_BOOK6;
  assert(progress_fields()==6u && progress_apply(0,6,0)==66u);
  keys("nbpbbbcc"); progress_menu(); saved();
  assert(!g_seq[g_pos] && progress_apply(0,6,0)==0);
  for(u32 i=0;i<3;i++){
    sl_w32(before+0x94u+4u*i,0x3FFFFFu);
    sl_w32(before+BOOK_EXTRA_OFF+4u*i,0x3FFFFFu);
  }
  stamp(before);
  assert(!memcmp(before,host_flash+SET_A,PAGE));
  sl_w32(g_set+0xA4u,3u); sl_w32(g_set+BOOK_EXTRA_OFF+12u,3u);
  stamp(g_set); memcpy(host_flash+SET_A,g_set,PAGE); memcpy(host_flash+SET_B,g_set,PAGE);
  memcpy(before,g_set,PAGE);
  keys("nbbbbccc"); progress_menu();
  assert(!g_seq[g_pos] && !memcmp(before,host_flash+SET_A,PAGE));
  keys("nbbbbbbcc"); progress_menu(); saved();
  before[0x94u]&=0xFEu; before[0xA4u]&=0xFEu;
  stamp(before); assert(!g_seq[g_pos] && !memcmp(before,host_flash+SET_A,PAGE));
  keys("nbbnnnbbbbcc"); progress_menu(); saved();
  before[BOOK_EXTRA_OFF]&=0xFEu; before[BOOK_EXTRA_OFF+12u]&=0xFEu;
  stamp(before); assert(!g_seq[g_pos] && !memcmp(before,host_flash+SET_A,PAGE));
  g_cfg.tama_off=0x104u; assert(progress_flag(2,0,4068)==0x88u*8u+68u);
  g_cfg.tama_off=0x108u; assert(progress_flag(2,21,4090)==0x90u*8u+21u);
}
static void progress_level(void){
  pet_fixture(0x114u);
  g_cfg.cat_n=0; /* Level is available even without the Tama File catalog. */
  u8 before[PAGE]; memcpy(before,g_set,PAGE);
  keys("bncc"); progress_menu();
  assert(!g_seq[g_pos] && !memcmp(before,host_flash+SET_A,PAGE));
  keys("bnbbc"); progress_menu(); saved();
  assert(!g_seq[g_pos] && lv_get()==1u);
  before[g_cfg.level_off]=1u; stamp(before);
  assert(!memcmp(before,host_flash+SET_A,PAGE));
}
static void progress_colors(void){
  char name[24], right[8]; u16 color;
  progress_root_row(0,name,right,&color); assert(color==GOLD);
  progress_root_row(1,name,right,&color); assert(color==WHITE);
  progress_action_row(0,name,right,&color); assert(color==WHITE);
  progress_action_row(1,name,right,&color); assert(color==RGB(104,224,211));
  progress_action_row(2,name,right,&color); assert(color==ORANGE);
  list_draw("PROGRESS",2,0,0,progress_root_row,"B SELECT  C BACK");
  u16 title[7u*128u]; memcpy(title,g_fb+2u*128u,sizeof title);
  rect(0,2,128,9,BG); ctext(2,"PROGRESS",GREY,1);
  assert(!memcmp(title,g_fb+2u*128u,sizeof title));
}
static void mission_unlocks(u32 tama,u32 shift){
  pet_fixture(tama);
  g_cfg.money_off=0xB4u+shift;
  money_put(12345);
  g_set[0xB8u+shift]=0x58; g_set[0xB9u+shift]=0x02; /* already 600 meals */
  g_set[0xDEu+shift]=0x80; /* preserve unrelated bitmap bits */
  for(u32 at=0xECu;at<=0xF4u;at+=4u) sl_w32(g_set+at+shift,0x80000002u);
  stamp(g_set); memcpy(host_flash+SET_A,g_set,PAGE); memcpy(host_flash+SET_B,g_set,PAGE);
  u8 before[PAGE]; memcpy(before,g_set,PAGE);
  keys("pbbbccc"); progress_menu();
  assert(!g_seq[g_pos] && !memcmp(before,host_flash+SET_A,PAGE));
  keys("pbbbbbcc"); progress_menu(); saved();
  for(u32 at=0xECu;at<=0xF4u;at+=4u) sl_w32(before+at+shift,0x80000003u);
  stamp(before);
  assert(!g_seq[g_pos] && !memcmp(before,host_flash+SET_A,PAGE));
  keys("pbpbbbcc"); progress_menu(); saved();
  assert(!g_seq[g_pos] && !mission_apply(0,MISSION_COUNT,0) && money_get()==12345);
  assert(g_set[0xBAu+shift]==0 && g_set[0xBBu+shift]==0); /* already completed snack counter untouched */
  assert(g_set[0xBCu+shift]==100 && g_set[0xC3u+shift]==3);
  assert(sl_r32(g_set+0xD4u+shift)==99999u); /* Earn Points lifetime counter only */
  assert(g_set[0xDCu+shift]==255 && g_set[0xDEu+shift]==0x83);
  assert(g_set[0xDFu+shift]==255 && g_set[0xE0u+shift]==15);
  for(u32 at=0xECu;at<=0xF4u;at+=4u) assert(sl_r32(g_set+at+shift)==0x803FFFFFu);
  for(u32 at=4u;at<PAGE;at++)
    if(at<0xB8u+shift || at>=0xF8u+shift) assert(g_set[at]==before[at]);
  memcpy(before,g_set,PAGE);
  keys("pbpbbcc"); progress_menu();
  assert(!g_seq[g_pos] && !memcmp(before,host_flash+SET_A,PAGE));
  keys("pbbbccc"); progress_menu();
  assert(!g_seq[g_pos] && !memcmp(before,host_flash+SET_A,PAGE));
  keys("pbbbbbcc"); progress_menu(); saved();
  before[0xB8u+shift]=0xF3; before[0xB9u+shift]=1; /* 499 of 500 */
  for(u32 at=0xECu;at<=0xF4u;at+=4u) before[at+shift]&=0xFEu;
  stamp(before);
  assert(!g_seq[g_pos] && !memcmp(before,host_flash+SET_A,PAGE));
  assert(mission_apply(0,MISSION_COUNT,0)==1u && money_get()==12345u);
  mission_reset(13); assert(sl_r32(g_set+0xD4u+shift)==99998u && money_get()==12345u);
  mission_reset(16); assert(g_set[0xDCu+shift]==255 && g_set[0xDDu+shift]==255 && g_set[0xDEu+shift]==0x80);
  assert(mission_apply(0,MISSION_COUNT,0)==3u);
}
int main(int argc,char **argv){
  assert(argc==2 || argc==4); g_dir=argv[1]; host_flash=malloc(0x1000000); assert(host_flash);
  fields(0x104); fields(0x108); fields(0x114);
  body_stages(0x104); body_stages(0x108); body_stages(0x114);
  appearance_cancel(0x104); appearance_cancel(0x108); appearance_cancel(0x114); appearance_hints(); neon_pet(); ui(); progress_level(); progress_colors(); progress_unlocks();
  mission_unlocks(0x104u,0); mission_unlocks(0x108u,0); mission_unlocks(0x114u,12u);
  if(argc==4) real_appearance(argv[2],(u32)strtoul(argv[3],0,0));
  free(host_flash);
  puts("Tama editing: layouts, visuals, care, cancel and UI passed"); return 0;
}
