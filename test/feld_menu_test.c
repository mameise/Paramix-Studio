#include "feld_test_support.h"
#include <sys/socket.h>
#include <sys/wait.h>

static void save_fixture(void){
  fixture();
  u8 set[PAGE]; memcpy(set,g_set,PAGE); memset(host_flash,0xFF,0x1000000);
  g_cfg.nslots=3; g_cfg.slot_off=0x900000; g_cfg.slot_off2=0; g_cfg.slot_size=0x5000;
  g_cfg.user_start=0xEFD000; g_cfg.user_end=0xF00000;
  g_cfg.flags=F_FIELDS|F_SAVES|F_BAG|F_TAMA; g_cfg.six_off=0; g_cfg.field_byte=439;
  g_cfg.nfields=3; g_cfg.tama_off=0x104; g_cfg.money_off=0xB4; g_cfg.level_off=0x7C;
  g_act=0; g_cur=0; g_napp=0;
  for(u32 i=0;i<PAGE;i++) g_ff[i]=0xFF;
  set[g_cfg.field_byte]=0; set[SAVE_OFF]=1; stamp(set);
  sl_w32(set+12,300); sl_w32(set+16,40000); stamp(set);
  memcpy(host_flash+SET_A,set,PAGE); memcpy(host_flash+SET_B,set,PAGE);
  memcpy(g_set,set,PAGE);
  u32 r=0x853C49E6u;
  for(u32 i=0;i<PAGE;i++){ r^=r<<13; r^=r>>17; r^=r<<5; host_flash[g_cfg.user_start+i]=(u8)r; }
  /* A small synthetic asset archive for compatibility hashes. */
  memset(host_flash+0x111000,0,32); sl_w32(host_flash+0x111008,16);
  sl_cancel=0;
  sl_test_counter=100;
}
static void run_saves(const char *s){ keys(s); int jump=setjmp(g_jb); if(!jump) saves_menu(0,0); assert(jump!=2); assert(!g_seq[g_pos]); }
static void points(const char *s){ keys(s); int jump=setjmp(g_jb); if(!jump) points_menu(); assert(jump!=2); assert(!g_seq[g_pos]); }
static void filter_fixture(void){ fixture(); count_set(0,15); }
static void framing(void){
  int fd[2]; assert(!socketpair(AF_UNIX,SOCK_STREAM,0,fd));
  sl_fd=fd[0]; const u8 payload[3]={1,2,3}; assert(sl_frame('D',123,9,payload,3));
  sl_fd=fd[1]; assert(sl_read(100) && sl_rx_session==123 && sl_rx_seq==9 && sl_rx_type=='D' && sl_rx_n==3 && sl_rx[2]==3);
  /* Corrupt a captured frame. */
  sl_fd=fd[0]; assert(sl_frame('D',123,9,payload,3));
  u8 raw[21]; assert(read(fd[1],raw,sizeof raw)==sizeof raw); raw[15]^=1;
  assert(write(fd[0],raw,sizeof raw)==sizeof raw); sl_fd=fd[1]; assert(!sl_read(100));
  /* A repeated sequence is acknowledged but never applied twice. */
  sl_fd=fd[0]; assert(sl_frame('H',456,0,payload,3)); assert(sl_frame('D',456,1,payload,3));
  sl_fd=fd[1]; sl_session=456; sl_seq=1; assert(sl_next('D',3) && sl_rx_seq==1);
  sl_fd=fd[0]; assert(sl_read(100) && sl_rx_type=='A' && sl_rx_seq==0);
  close(fd[0]); close(fd[1]); sl_fd=-1;
}
static void transfer(int compatible,int stored){
  save_fixture();
  if(stored){ assert(!slot_copy(0,2,1)); }
  u8 original[PAGE]; memcpy(original,host_flash+SET_A,PAGE);
  int fd[2]; assert(!socketpair(AF_UNIX,SOCK_STREAM,0,fd));
  fflush(stdout);
  pid_t pid=fork(); assert(pid>=0);
  if(!pid){
    close(fd[0]); sl_fd=fd[1];
    sl_test_counter=1000;
    /* Receiver has its own active save and keeps it throughout. */
    host_flash[SET_A+0x80]=42; stamp(host_flash+SET_A); u8 live[PAGE]; memcpy(live,host_flash+SET_A,PAGE);
    if(!compatible) host_flash[0x111010]^=1;
    if(compatible) sl_test_drop_ack=1; /* force a retransmission of the header */
    keys("b"); int ok=sl_receive(1); assert(ok==compatible);
    assert(!memcmp(host_flash+SET_A,live,PAGE));
    if(compatible){
      u32 phases=(1u<<SL_ROM)|(1u<<SL_WAIT)|(1u<<SL_PEER)|(1u<<SL_HEADER)|(1u<<SL_DATA)|(1u<<SL_VERIFY)|(1u<<SL_COMMIT)|(1u<<SL_FINISH);
      assert((sl_ui_seen&phases)==phases && sl_ui_full_paints>=8u);
      assert(slot_valid(1) && !slot_set(1,g_page) && set_ok(g_page));
      assert(field_of(g_page)==(stored?1:0));
      assert(!slot_clear(2) && !slot_copy(1,2,2));
      assert(!slot_set(2,g_page) && sl_r32(g_page+16)==sl_r32(original+16)-900u);
      assert(!restore(1,0));
      assert(sl_r32(host_flash+SET_A+16)==sl_r32(original+16)-900u);
      assert(set_ok(host_flash+SET_A) && !memcmp(host_flash+SET_A,host_flash+SET_B,PAGE));
    } else assert(!slot_valid(1));
    close(fd[1]); _exit(0);
  }
  close(fd[1]); sl_fd=fd[0]; assert(sl_send(stored?2:0)==compatible);
  if(compatible){
    u32 phases=(1u<<SL_ROM)|(1u<<SL_PREPARE)|(1u<<SL_WAIT)|(1u<<SL_HEADER)|(1u<<SL_DATA)|(1u<<SL_VERIFY)|(1u<<SL_FINISH);
    assert((sl_ui_seen&phases)==phases);
  }
  assert(!memcmp(host_flash+SET_A,original,PAGE));
  int status; assert(waitpid(pid,&status,0)==pid && WIFEXITED(status) && WEXITSTATUS(status)==0);
  close(fd[0]); sl_fd=-1;
}
static void interrupted_transfer(void){
  save_fixture(); u32 fld=0; assert(sl_export(0,&fld));
  u8 live[PAGE]; memcpy(live,host_flash+SET_A,PAGE);
  int fd[2]; assert(!socketpair(AF_UNIX,SOCK_STREAM,0,fd)); fflush(stdout);
  pid_t pid=fork(); assert(pid>=0);
  if(!pid){
    close(fd[0]); sl_fd=fd[1]; keys("b"); assert(!sl_receive(1));
    assert(!slot_valid(1) && !memcmp(live,host_flash+SET_A,PAGE)); close(fd[1]); _exit(0);
  }
  close(fd[1]); sl_fd=fd[0]; sl_session=54321; sl_seq=0;
  u8 offer[32]; sl_w32(offer,1); sl_w32(offer+4,sl_signature()); sl_w32(offer+8,g_hdr.npages);
  sl_w32(offer+12,g_hdr.bytes); sl_w32(offer+16,g_hdr.sum); sl_w32(offer+20,fld); sl_w32(offer+24,0);
  sl_w32(offer+28,sl_counter());
  assert(sl_request('O',offer,32)); assert(sl_request('H',(u8*)g_hdr.e,g_hdr.npages*4u));
  /* Transmit one real data frame, then disconnect before the file is complete. */
  u32 a; assert(!save_page(0,0,&a)); u32 n=lz_pack(g_page,g_cmp); assert(n>128u);
  assert(sl_request('D',g_cmp,128)); close(fd[0]); sl_fd=-1;
  int status; assert(waitpid(pid,&status,0)==pid && WIFEXITED(status) && !WEXITSTATUS(status));
}
static void flash_fixture(void){
  save_fixture(); g_cfg.version=9; g_cfg.stub_off=0xB6000;
  sl_w32(host_flash+g_cfg.stub_off+0x0C,0x4604B672u);
  memset(host_flash+FT_BASE,0xA5,0x10000); sl_w32(host_flash+FT_BASE,0x1801F000u); sl_w32(host_flash+FT_BASE+4,9);
  sl_w32(host_flash+FT_TABLE,0x494E4F53u); sl_w32(host_flash+FT_TABLE+4,0x56454458u);
  sl_w32(host_flash+FT_TABLE+8,0x12); sl_w32(host_flash+FT_TABLE+0x10,0x60FF0000u); sl_w32(host_flash+FT_TABLE+0x14,FT_SIZE);
  u32 crc=0xFFFFFFFFu; for(u32 i=0;i<FT_SIZE;i++) crc=sl_crc(crc,host_flash[FT_BASE+i]); sl_w32(host_flash+FT_TABLE+0x24,crc^0xFFFFFFFFu);
  host_flash_tool_src=0;
}
static void root_pause(const char *seq){ keys(seq); int jump=setjmp(g_jb); if(!jump) main_menu(); assert(jump==2 && !g_seq[g_pos]); }
static void flash_tool_tests(void){
  flash_fixture(); assert(flash_tool_header() && flash_tool_stub() && flash_tool_valid());
  u8 rescue[0x10000],live[PAGE]; memcpy(rescue,host_flash+FT_BASE,sizeof rescue); memcpy(live,host_flash+SET_A,PAGE);
  u32 leaves=host_leave_count;
  keys("c"); flash_tool_menu(); assert(!host_flash_tool_src && host_leave_count==leaves && !g_seq[g_pos]);
  keys("b"); int jump=setjmp(g_jb); if(!jump) flash_tool_menu();
  assert(jump==1 && host_flash_tool_src==0x10FDF000u && host_leave_count==leaves && !g_seq[g_pos]);
  assert(!memcmp(rescue,host_flash+FT_BASE,sizeof rescue) && !memcmp(live,host_flash+SET_A,PAGE));
  flash_fixture(); host_flash[FT_BASE+0x100]^=1; assert(!flash_tool_valid()); keys("c"); flash_tool_menu(); assert(!host_flash_tool_src && !g_seq[g_pos]);
  flash_fixture(); sl_w32(host_flash+FT_TABLE+0x14,0x10000); assert(!flash_tool_header()); keys("c"); flash_tool_menu(); assert(!host_flash_tool_src && !g_seq[g_pos]);
  flash_fixture(); sl_w32(host_flash+FT_BASE+4,0xE001); assert(!flash_tool_header());
  flash_fixture(); sl_w32(host_flash+FT_TABLE+8,0x13); assert(!flash_tool_header());
  flash_fixture(); sl_w32(host_flash+g_cfg.stub_off+0x0C,0); keys("c"); flash_tool_menu(); assert(!host_flash_tool_src && !g_seq[g_pos]);
  flash_fixture(); memset(host_flash+FT_TABLE,0xFF,PAGE); keys("c"); flash_tool_menu(); assert(!host_flash_tool_src && !g_seq[g_pos]);
  /* Without apps/launcher flags/dl_src, root still reaches FLASH TOOL. */
  flash_fixture(); g_cfg.dl_src=0; keys("pbcc"); jump=setjmp(g_jb); if(!jump) main_menu();
  assert(jump==1 && host_leave_count==leaves+1u && !host_flash_tool_src && !g_seq[g_pos]);
  /* Circular viewport: crossing either end keeps adjacent wrapped entries
   * visible and scrolls only one row, rather than switching whole pages. */
  flash_fixture(); root_pause(""); assert(g_fb[12u*128u+3u]==fcol((u32)g_cur));
  u32 title_pixels=0; for(u32 y=1;y<8;y++) for(u32 x=49;x<127;x++) if(g_fb[y*128u+x]==GREY) title_pixels++;
  assert(title_pixels>10u); /* version is actually rendered after PARAMIX */
  root_pause("p"); assert(g_fb[12u*128u+3u]==ORANGE && g_fb[39u*128u+3u]==BG);
  root_pause("pn"); assert(g_fb[39u*128u+3u]==fcol((u32)g_cur)); /* SAVES below wrapped FLASH */
  root_pause("nnnn"); assert(g_fb[93u*128u+3u]==ORANGE); /* one-row shift to FLASH */
  root_pause("nnnnn"); assert(g_fb[93u*128u+3u]==fcol((u32)g_cur)); /* wrap while retaining viewport */
  flash_fixture(); g_cfg.flags=F_BAG; root_pause(""); assert(g_fb[12u*128u+3u]==PINK);
  root_pause("nn"); assert(g_fb[66u*128u+3u]==ORANGE); /* three items fit without scrolling */
  /* All six categories: preserve PROGRAM default, scroll through FLASH and
   * back around to SAVES, then launch from the actual root action. */
  flash_fixture(); g_napp=1; g_appsrc[0]=0; strcpy(g_appnm[0],"TEST APP");
  root_pause(""); assert(g_fb[93u*128u+3u]==ORANGE);
  root_pause("n"); assert(g_fb[93u*128u+3u]==ORANGE && g_fb[103u*128u+126u]==BG);
  root_pause("nn"); assert(g_fb[93u*128u+3u]==fcol((u32)g_cur));
  root_pause("nnp"); assert(g_fb[66u*128u+3u]==ORANGE);
  keys("nnpbb"); jump=setjmp(g_jb); if(!jump) main_menu();
  assert(jump==1 && host_flash_tool_src==0x10FDF000u && !g_seq[g_pos]);
  assert(!memcmp(rescue,host_flash+FT_BASE,sizeof rescue));
}
int main(int argc,char **argv){
  assert(argc==2); g_dir=argv[1]; host_flash=malloc(0x1000000); assert(host_flash);
  flash_tool_tests();
  /* Confirmation/background pixels must never survive a transfer transition. */
  sl_ui_begin(1);
  for(u32 i=0;i<128u*128u;i++) g_fb[i]=RED;
  sl_ui_set(SL_HEADER);
  for(u32 i=0;i<128u*128u;i++) assert(g_fb[i]!=RED);
  assert(g_fb[98u*128u+10u]==BG && g_fb[126u*128u+80u]==BG);
  int pulse_frame=g_frame;
  sl_ui_start=sl_now()-2500u; sl_ui_paint=sl_now()-1000u; sl_ui_pulse();
  assert(g_frame==pulse_frame+1);
  sl_ui_pulse(); assert(g_frame==pulse_frame+1); /* throttled, not a redraw loop */
  sl_ui_phase=0;
  assert(wrap_step(1,-1,1,15)==15 && wrap_step(15,1,1,15)==1 && wrap_step(0,-9,0,3)==3);
  filter_fixture(); g_show_max=0; cat_build(); assert(g_catn==2 && g_cat[0]==5002 && g_cat[1]==5003);
  g_show_max=1; cat_build(); assert(g_catn==3);
  filter_fixture(); keys("b"); assert(count_edit(5001,15,1)==15);
  keys("nb"); assert(count_edit(5001,15,1)==1);
  keys("pb"); assert(count_edit(5001,1,1)==15);
  keys("nb"); assert(count_edit(5001,15,0)==0);
  keys("pb"); assert(count_edit(5001,0,0)==15);
  /* Add picker: last -> filter row -> last, toggle filter on and off. */
  filter_fixture(); keys("ppnbnbnb"); cat_pick(); assert(g_show_max && g_dirty && count_get(0)==1);
  fixture(); count_set(0,15); count_set(1,15); list_set(4,5003); count_set(4,15);
  g_show_max=0; cat_build(); assert(!g_catn); keys("c"); cat_pick();

  fixture(); u8 original[PAGE]; memcpy(original,host_flash+SET_A,PAGE);
  points("bcc"); assert(!memcmp(original,host_flash+SET_A,PAGE));
  points("bbbc"); assert(!set_load() && money_get()==99999 && set_ok(host_flash+SET_A) && !memcmp(host_flash+SET_A,host_flash+SET_B,PAGE));
  fixture(); points("c"); assert(!memcmp(original,host_flash+SET_A,PAGE));

  /* Real input() returns zero until an edge arrives. Idle polling must not
   * repaint/re-enter wait_release(), which otherwise consumes physical keys. */
  fixture(); int frame=g_frame;
  points("...n...n...b...c...c"); /* Third entry edits digits; C exits. */
  assert(g_frame-frame==5);
  frame=g_frame; points("...c"); assert(g_frame-frame==1);
  frame=g_frame; points("...b...c...c"); assert(g_frame-frame==3);

  /* Saved prompt defaults to NO; C also stays. Only explicit YES exits. */
  fixture(); u32 leaves=host_leave_count;
  keys("...b"); done_screen("SAVED",GOLD); assert(host_leave_count==leaves && !g_seq[g_pos]);
  keys("...c"); done_screen("SAVED",GOLD); assert(host_leave_count==leaves && !g_seq[g_pos]);
  keys("...n...b"); int jump=setjmp(g_jb);
  if(!jump) done_screen("SAVED",GOLD);
  assert(jump==1 && host_leave_count==leaves+1u && !g_seq[g_pos]);
  /* Two edits in the same Points session without restarting the device. */
  fixture(); leaves=host_leave_count;
  points("bbb" "nnb" "p" "bbbbb" "b" "b" "c");
  assert(host_leave_count==leaves && !set_load() && money_get()==89999u);
  assert(set_ok(host_flash+SET_A) && !memcmp(host_flash+SET_A,host_flash+SET_B,PAGE));
  /* SET ZERO is second, requires confirmation, saves both mirrors, stays here. */
  fixture(); money_put(12345); stamp(g_set); memcpy(host_flash+SET_A,g_set,PAGE); memcpy(host_flash+SET_B,g_set,PAGE);
  points("nbcc"); assert(!set_load() && money_get()==12345u);
  leaves=host_leave_count; points("nbbbc");
  assert(host_leave_count==leaves && !set_load() && !money_get());
  assert(set_ok(host_flash+SET_A) && !memcmp(host_flash+SET_A,host_flash+SET_B,PAGE));
  points("nbbc"); assert(!set_load() && !money_get()); /* already zero: no redundant save prompt */
  save_fixture(); leaves=host_leave_count;
  keys("nbb"); level_edit(); assert(host_leave_count==leaves && lv_get()==1 && !g_seq[g_pos]);
  keys("bb"); care_full(); assert(host_leave_count==leaves && !g_seq[g_pos]);
  /* Home C exits; submenus return to home first after save-and-stay. */
  save_fixture(); leaves=host_leave_count; keys("nnbbbbcc"); jump=setjmp(g_jb);
  if(!jump) main_menu();
  assert(jump==1 && host_leave_count==leaves+1u && !g_seq[g_pos]);
  assert(!set_load() && money_get()==99999u);

  save_fixture(); frame=g_frame;
  run_saves("...b...c...c"); assert(g_frame-frame==3);
  frame=g_frame; run_saves("...c"); assert(g_frame-frame==1);
  assert(!slot_copy(0,1,1));
  frame=g_frame; run_saves("...n...b...n...b...c...c");
  assert(g_frame-frame==6 && slot_valid(1)); /* clear confirmation cancels */
  saves_scan();
  keys("...p...b"); assert(save_action(0,1)==2); /* SEND TO TAMA */
  assert(!slot_clear(1)); saves_scan();
  keys("...p...b"); assert(save_action(1,0)==g_copy_n+1u); /* RECEIVE */
  run_saves("...n...b...p...b...c...c"); /* open RECEIVE, cancel, exit */
  run_saves("...b...p...b...c...c"); /* open SEND, cancel, exit */

  /* Scrolling/back/cancel must not re-read or decompress stored metadata. */
  save_fixture(); assert(!slot_copy(0,1,1)); assert(!slot_copy(0,2,2));
  g_slot_settings_reads=0;
  run_saves("...n...n...n...p...p...a...a...c");
  assert(g_slot_settings_reads==2 && g_shas[0] && g_shas[1] && g_shas[2]);
  g_slot_settings_reads=0;
  run_saves("nbnbbc"); /* clear cancelled: one initial scan only */
  assert(g_slot_settings_reads==2 && g_shas[1]);
  g_slot_settings_reads=0;
  run_saves("nbnbnbc"); /* clear accepted: refresh status and remaining save */
  assert(g_slot_settings_reads==3 && !g_shas[1] && g_sfld[1]==-1 && !g_snm[1][0] && g_shas[2]);

  save_fixture(); memcpy(original,host_flash+SET_A,PAGE);
  assert(!slot_copy(0,1,2)); assert(slot_valid(1));
  assert(!memcmp(original,host_flash+SET_A,PAGE));
  assert(!slot_set(1,g_page) && field_of(g_page)==2 && g_page[SAVE_OFF]==2 && set_ok(g_page));
  u8 source_header[PAGE]; memcpy(source_header,host_flash+slot_base(1),PAGE);
  assert(!slot_copy(1,2,1)); assert(!memcmp(source_header,host_flash+slot_base(1),PAGE));
  assert(!slot_set(2,g_page) && field_of(g_page)==1 && g_page[SAVE_OFF]==3);
  assert(slot_clear(0) && !memcmp(original,host_flash+SET_A,PAGE));
  assert(!slot_clear(1) && !slot_valid(1)); assert(slot_valid(2));
  assert(!restore(2,0) && field_now()==1);


  /* Rejected copy (capacity, corrupt source) makes no live/source changes. */
  save_fixture(); g_cfg.slot_size=2*PAGE; assert(!stash(1));
  memcpy(source_header,host_flash+slot_base(1),PAGE); assert(slot_copy(1,2,2)==3);
  assert(!slot_valid(2) && !memcmp(source_header,host_flash+slot_base(1),PAGE));
  host_flash[pool_addr(rd16(slot_base(1)+offsetof(SlotHdr,dpg)))]^=1;
  assert(slot_copy(1,2,2)==2 && !slot_valid(2));

  /* Six-field mapping changes both field bytes in the independent copy. */
  save_fixture(); g_cfg.six_off=500; for(u32 i=0;i<8;i++) g_cfg.six_map[i]=0xFF;
  g_cfg.six_map[0]=0; g_cfg.six_map[5]=2;
  host_flash[SET_A+500]=0; host_flash[SET_B+500]=0; stamp(host_flash+SET_A); stamp(host_flash+SET_B);
  assert(!slot_copy(0,1,2)); assert(!slot_set(1,g_page) && g_page[500]==1 && g_page[439]==1 && field_of(g_page)==2);

  /* UI defaults to no copy; cancelling does not create a slot. */
  save_fixture(); run_saves("nbcc"); assert(!slot_valid(1));
  /* Copy active slot to slot 2, select field 2, then switch. */
  save_fixture(); run_saves("nbnbnbbbc");
  assert(!set_load() && field_now()==1 && g_set[SAVE_OFF]==2 && slot_valid(0) && g_act==1 && g_cur==1 && g_shas[1]);
  /* Switching back while staying uses the updated active slot bookkeeping. */
  leaves=host_leave_count; run_saves("pbbbbc");
  assert(host_leave_count==leaves && g_act==0 && g_cur==0 && slot_valid(1) && !slot_valid(0));
  /* Clear defaults to cancel; explicit clear leaves the active save alone. */
  save_fixture(); assert(!slot_copy(0,1,1)); run_saves("nbnbbc"); assert(slot_valid(1));
  run_saves("nbnbnbc"); assert(!slot_valid(1) && field_now()==0);
  /* Active slot clear is refused. */
  run_saves("bnbcc"); assert(field_now()==0);
  framing(); transfer(1,0); transfer(1,1); transfer(0,0); interrupted_transfer();
  puts("PASS: filtering, circular controls, max points, slot copy/clear, UI, CRC/replay and two-peer save transfer");
  free(host_flash); return 0;
}
