/* Synthetic shared-pool regression tests; optional user ROM stays local. */
#include "feld_test_support.h"
static void pool_fixture(void){
  fixture(); u8 settings[PAGE]; memcpy(settings,g_set,PAGE); memset(host_flash,0xFF,0x1000000);
  g_cfg.version=6;g_cfg.pool_extra_off=0;g_cfg.pool_extra_pages=0;
  g_cfg.nslots=6; g_cfg.slot_off=0x900000; g_cfg.slot_off2=0; g_cfg.slot_n1=0; g_cfg.slot_size=5*PAGE;
  g_cfg.user_start=0xEF4000; g_cfg.user_end=0xF00000;
  g_cfg.flags=F_FIELDS|F_SAVES; g_cfg.field_byte=439; g_cfg.six_off=0; g_cfg.nfields=3;
  for(u32 i=0;i<3;i++) g_cfg.field_id[i]=i+1;
  g_cfg.id_page=0xD49000; g_act=0; g_cur=0;
  for(u32 i=0;i<PAGE;i++) g_ff[i]=0xFF;
  settings[g_cfg.field_byte]=0; settings[SAVE_OFF]=1; stamp(settings);
  memcpy(host_flash+SET_A,settings,PAGE); memcpy(host_flash+SET_B,settings,PAGE); memcpy(g_set,settings,PAGE);
  u32 r=0x653C49E6u;
  for(u32 a=g_cfg.user_start;a<SET_A;a++) { r^=r<<13; r^=r>>17; r^=r<<5; host_flash[a]=(u8)r; }
}
static u32 used_pages(void){pool_mark(g_cfg.nslots);return pool_n()-pool_available();}
static void check_slot(u32 s){u32 np;assert(!source_check(s,&np));}
static void copies_switches(void){
  pool_fixture();
  for(u32 j=1;j<6;j++) {assert(!slot_copy(0,j,j%3));check_slot(j);}
  assert(used_pages()<24); /* five full saves cannot fit without sharing */
  u8 originals[5][PAGE];
  for(u32 j=1;j<6;j++){assert(!slot_set(j,originals[j-1]));}
  /* Edit only the live save, then back it up: referenced data is immutable. */
  host_flash[g_cfg.user_start]^=0x31;
  assert(stash_capacity(0,0));assert(!stash(0));
  for(u32 j=1;j<6;j++){assert(!slot_set(j,g_page));assert(!memcmp(g_page,originals[j-1],PAGE));check_slot(j);}
  assert(!slot_clear(3));
  for(u32 j=1;j<6;j++) if(j!=3)check_slot(j);
  /* Switch among four occupied slots; target loading releases only its header. */
  for(u32 j=1;j<5;j++) if(j!=3){keys("b");field_switch2(j,1,j%3);assert(g_act==(int)j);assert(!slot_valid(j));}
  for(u32 j=0;j<6;j++)if(j!=(u32)g_act && j!=3)check_slot(j);
}
static void old_duplicates(void){
  pool_fixture(); assert(!stash(1));
  u32 b1=slot_base(1),b2=slot_base(2),nd=rd32(b1+24); assert(nd*2<=pool_n());
  u8 head[PAGE]; flash_read(b1,head,PAGE);sl_w32(head+4,3);
  /* Emulate an old image with physically independent but equal pages. */
  pool_mark(g_cfg.nslots);
  for(u32 k=0;k<nd;k++){
    u32 x=pool_take(2),old=rd16(b1+offsetof(SlotHdr,dpg)+2*k);assert(x!=0xFFFFu);
    flash_read(pool_addr(old),g_page,PAGE);flash_prog4k(pool_addr(x),g_page);
    head[offsetof(SlotHdr,dpg)+2*k]=(u8)x;head[offsetof(SlotHdr,dpg)+2*k+1]=(u8)(x>>8);
  }
  sl_w32(head+SLOT_TIME_OFF,SLOT_TIME_MAGIC);sl_w32(head+SLOT_TIME_OFF+4,100);sl_w32(head+SLOT_TIME_OFF+8,100);
  sl_w32(head+SLOT_TIME_OFF+12,SLOT_TIME_MAGIC^100^100);flash_prog4k(b2,head);
  assert(used_pages()==nd*2);assert(pool_compact());assert(used_pages()==nd);
  check_slot(1);check_slot(2);
  flash_read(b2,g_page,PAGE);
  assert(!memcmp(head,g_page,offsetof(SlotHdr,dpg)));
  assert(!memcmp(head+SLOT_TIME_OFF,g_page+SLOT_TIME_OFF,PAGE-SLOT_TIME_OFF));
  assert(stash_capacity(0,0));assert(!stash(0));check_slot(1);check_slot(2);
}
static void full_pool(void){
  pool_fixture();g_cfg.nslots=3;g_cfg.slot_size=5*PAGE;assert(!stash(1));
  /* Entirely different live data needs new pages; never invalidate old headers. */
  u8 head[PAGE],live[PAGE];flash_read(slot_base(1),head,PAGE);
  for(u32 a=g_cfg.user_start;a<SET_A;a++)host_flash[a]^=0xFF;
  flash_read(SET_A,live,PAGE);assert(!stash_capacity(0,0));assert(stash(0)==11);
  assert(!memcmp(head,host_flash+slot_base(1),PAGE));assert(!memcmp(live,host_flash+SET_A,PAGE));check_slot(1);
}
static void expanded_pool(void){
  pool_fixture();g_cfg.version=11;g_cfg.nslots=3;g_cfg.pool_extra_off=0xFB7000;g_cfg.pool_extra_pages=16;
  assert(pool_n()==28 && pool_addr(12)==0xFB7000);assert(!stash(1));
  u8 head[PAGE];flash_read(slot_base(1),head,PAGE);
  for(u32 a=g_cfg.user_start;a<SET_A;a++)host_flash[a]^=0xFF;
  assert(stash_capacity(0,0));assert(!stash(0));
  u32 extended=0;for(u32 k=0;k<rd32(slot_base(0)+24);k++)extended+=rd16(slot_base(0)+offsetof(SlotHdr,dpg)+2*k)>=12;
  assert(extended && !memcmp(head,host_flash+slot_base(1),PAGE));check_slot(1);
  assert(!slot_clear(1));int old=g_act;g_act=-1;check_slot(0);g_act=old;
}
static void real_rom(const char *file,u32 cfg){
  FILE *f=fopen(file,"rb");assert(f);assert(fread(host_flash,1,0x1000000,f)==0x1000000);fclose(f);
  memset((void*)&g_cfg,0,sizeof g_cfg);memcpy((void*)&g_cfg,host_flash+cfg,608);
  for(u32 i=0;i<PAGE;i++)g_ff[i]=0xFF;
  g_act=(int)host_flash[SET_A+SAVE_OFF]-1;assert(g_act>=0 && (u32)g_act<g_cfg.nslots);
  u32 before=used_pages();assert(stash_capacity((u32)g_act,0));u32 compact=used_pages();
  assert(compact<before);assert(!stash((u32)g_act));
  for(u32 j=0;j<g_cfg.nslots;j++)if(slot_valid(j)){ /* avoid live special case */
    u32 np;int active=g_act;g_act=-1;assert(!source_check(j,&np));g_act=active;
  }
  printf("PASS: user-ROM pool %u -> %u pages after compaction, %u with current backup\n",before,compact,used_pages());
  for(u32 j=0;j<g_cfg.nslots;j++)if(j!=(u32)g_act && !slot_valid(j))assert(!slot_copy((u32)g_act,j,j%nfields()));
  int active=g_act;g_act=-1;
  for(u32 j=0;j<g_cfg.nslots;j++){assert(slot_valid(j));u32 np;assert(!source_check(j,&np));}
  g_act=active;printf("PASS: supplied ROM fills six valid saved copies using %u shared pool pages\n",used_pages());
}
int main(int argc,char **argv){
  assert(argc>=2);g_dir=argv[1];host_flash=malloc(0x1000000);assert(host_flash);
  copies_switches();old_duplicates();full_pool();expanded_pool();
  if(argc==4)real_rom(argv[2],(u32)strtoul(argv[3],0,0));
  free(host_flash);puts("PASS: six-slot sharing, copy-on-write, clear/switch, legacy compaction, and full-pool rejection");return 0;
}
