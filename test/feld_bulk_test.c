#include "feld_test_support.h"
int main(int argc,char **argv){
  assert(argc==2); g_dir=argv[1]; host_flash=malloc(0x1000000); assert(host_flash);
  u8 before[PAGE];
  fixture(); memcpy(before,g_set,PAGE);
  assert(bulk_items(1,15,0)==2 && !memcmp(before,g_set,PAGE));
  assert(bulk_items(1,15,1)==2);
  assert(count_get(0)==3 && count_get(1)==15 && count_get(2)==7 && count_get(3)==2);
  assert(list_len()==5 && list_id(4)==5003 && count_get(4)==15 && list_id(5)==0);
  assert(bulk_items(1,1,1)==0); /* existing counts remain unchanged */
  assert(bulk_items(0,1,1)==4 && count_get(2)==7); /* fixed untouched */
  assert(bulk_items(0,1,1)==0);
  assert(bulk_items(0,15,1)==4 && count_get(0)==15 && count_get(3)==15);
  memcpy(before,g_set,PAGE);
  assert(!bulk_items(1,0,1) && !bulk_items(0,16,1) && !memcmp(before,g_set,PAGE));
  fixture(); assert(bulk_items(0,9,1)==2 && !count_get(1) && list_len()==4);

  /* Full bag still allows restoration of the same zero-count ID, but rejects
   * adding a new ID without partially restoring other entries. */
  fixture(); for(u32 i=4;i<640;i++){ list_set(i,10000+i); count_set(i,1); }
  memcpy(before,g_set,PAGE);
  assert(bulk_items(1,5,0)==641 && bulk_items(1,5,1)==641 && !memcmp(before,g_set,PAGE));
  list_set(639,5003); count_set(639,0);
  assert(bulk_items(1,5,1)==2 && count_get(639)==5 && count_get(1)==5);
  fixture(); for(u32 i=4;i<639;i++){ list_set(i,10000+i); count_set(i,1); }
  assert(bulk_items(1,15,1)==2 && list_len()==640 && list_id(639)==5003 && count_get(639)==15);

  /* The batch path must not truncate the catalog to the picker's 512 IDs. */
  fixture(); memset(g_set+g_cfg.list_off,0,1280); memset(g_set+g_cfg.count_off,0,320);
  g_cfg.cat_n=513;
  for(u32 k=0;k<513;k++){
    u32 o=g_cfg.cat_off+8+28*k, id=5001+k;
    host_flash[o]=(u8)id; host_flash[o+1]=(u8)(id>>8); host_flash[o+2]=0;
  }
  assert(bulk_items(1,6,1)==513 && list_len()==513 && list_id(512)==5513 && count_get(512)==6);

  /* Real UI navigation: cancel count, cancel confirmation, discard changes. */
  fixture(); memcpy(before,g_set,PAGE); run_items("nbcc"); assert(!g_dirty && !memcmp(before,g_set,PAGE));
  fixture(); run_items("nbbcc"); assert(!g_dirty && !memcmp(before,g_set,PAGE));
  fixture(); run_items("nbnbbcc");
  assert(g_dirty && count_get(1)==2 && !memcmp(host_flash+SET_A,before,PAGE));
  /* Apply and save: CRC, mirrored page, restored ID and appended ID. */
  fixture(); run_items("nbnbbcbb");
  assert(set_ok(host_flash+SET_A) && !memcmp(host_flash+SET_A,host_flash+SET_B,PAGE));
  assert(!set_load() && count_get(0)==3 && count_get(1)==2 && count_get(2)==7 && count_get(4)==2);
  /* SET OWNED COUNTS row, 15 maximum, A wrap, lower bound, and old item row. */
  fixture(); run_items("nnbpbbcbb");
  assert(!set_load() && count_get(0)==15 && count_get(3)==15);
  fixture(); run_items("nnbnnnnnnnnnnnnnnbbcbb");
  assert(!set_load() && count_get(0)==15 && !count_get(1) && count_get(2)==7 && count_get(3)==15);
  fixture(); run_items("nnbnnnnnnnnnnnnnnabbcbb");
  assert(!set_load() && count_get(0)==1 && count_get(3)==1);
  fixture(); run_items("nnnbnbcbb");
  assert(!set_load() && count_get(0)==4 && !count_get(1) && count_get(2)==7);
  /* Neon keeps IDs after deletion, has holes, and reserves quantities in use. */
  fixture(); g_cfg.flags=F_NEON_EDIT|F_BAG;
  for(u32 k=0;k<4;k++) host_flash[g_cfg.cat_off+8+28*k+5]=(u8)((k==1?5:10)<<4);
  list_set(1,0); list_set(638,5002);
  g_set[g_cfg.count_off+320]=2; /* two units of slot zero are in use */
  memcpy(before,g_set,PAGE);
  assert(bulk_items(1,15,0)==2 && !memcmp(before,g_set,PAGE));
  assert(bulk_items(1,15,1)==2);
  assert(list_id(638)==5002 && count_get(638)==5);
  assert(list_id(1)==5003 && count_get(1)==10 && count_get(2)==7 && count_get(3)==2);
  assert(bulk_items(0,1,1)==3 && count_get(0)==2 && count_get(638)==1 && count_reserved(0)==2);
  puts("PASS: batch planning, capacity, quantities, cancellation, navigation and mirrored save CRC");
  free(host_flash); return 0;
}
