/* Synthetic saves only. Run: node test/feld_bulk_test.js */
#define HOST
#define BAG_DEL
#define main menu_host_driver
#define g_out host_output_file
#include "../feld/src/feld/host.c"
#undef main
#undef g_out
#include "../feld/src/feld/main.c"
#include <assert.h>

static void fixture(void){
  memset(host_flash,0,0x1000000);
  memset(g_set,0,PAGE);
  g_cfg.cat_off=0x10000; g_cfg.cat_n=4;
  g_cfg.list_off=0x240; g_cfg.count_off=0x740;
  for(u32 k=0;k<4;k++){
    u32 o=g_cfg.cat_off+8+28*k, id=5001+k;
    host_flash[o]=(u8)id; host_flash[o+1]=(u8)(id>>8);
  }
  host_flash[g_cfg.cat_off+8+28*3+2]=1; /* fixed item */
  list_set(0,5001); count_set(0,3);
  list_set(1,5002); count_set(1,0);
  list_set(2,5004); count_set(2,7);
  list_set(3,9001); count_set(3,2); /* owned ID absent from catalog */
  g_set[0x1c]=0xea; g_set[0x1d]=7; /* 2026 */
  g_set[0x1e]=1; g_set[0x20]=1;
  stamp(g_set);
  memcpy(host_flash+SET_A,g_set,PAGE);
  memcpy(host_flash+SET_B,g_set,PAGE);
  g_dirty=0;
}
static void keys(const char *s){ g_seq=s; g_pos=0; }
static void run_items(const char *s){
  keys(s);
  int jump=setjmp(g_jb); if(!jump) items_menu(); assert(jump!=2);
  assert(g_seq[g_pos]==0);
}
