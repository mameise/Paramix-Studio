/* Menu save protocol v1, deliberately separate from the game's playdate link.
 * Same 460800 8N1 UART configuration as emu/plink.h; polled, no IRQ needed.
 * Stop-and-wait, session + sequence, CRC32 on every frame. Empty slots only.
 * Host socket transport exercises the exact same framing and import code. */
static int sl_cancel;
#ifdef HOST
#include <unistd.h>
#include <poll.h>
#include <time.h>
static int sl_fd=-1;
static u32 sl_test_counter=100;
static u32 sl_counter(void){ return sl_test_counter; }
static u32 sl_now(void){ struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t); return (u32)(t.tv_sec*1000u+t.tv_nsec/1000000u); }
static int sl_byte(u8 *b,u32 ms){
  struct pollfd p={sl_fd,POLLIN,0};
  if(sl_fd<0) { sl_cancel=1; return 0; }
  if(poll(&p,1,(int)ms)<=0) return 0;
  if(read(sl_fd,b,1)==1) return 1;
  sl_cancel=1; return 0;
}
static int sl_put(u8 b){ return sl_fd>=0 && write(sl_fd,&b,1)==1; }
static void sl_open(void){}
static void sl_close(void){}
#else
static u32 sl_counter(void){ return REG(0x45000304u); }
static u32 sl_now(void){ clock_poll(); return g_ms; }
static int sl_byte(u8 *b,u32 ms){
  u32 t=sl_now();
  do { hw_wdt_feed(); if(hw_buttons()&BTN_C){ sl_cancel=1; return 0; }
    if(REG(UART+0x14)&1u){ *b=(u8)REG(UART); return 1; }
  } while(sl_now()-t<ms);
  return 0;
}
static int sl_put(u8 b){
  u32 t=sl_now(); while(!(REG(UART+0x14)&0x20u)){
    hw_wdt_feed(); if(sl_now()-t>100u) return 0;
  }
  REG(UART)=b; return 1;
}
static void sl_open(void){
  REG(UART+4)=0; REG(UART+12)=0x83; REG(UART)=0x1A; REG(UART+4)=0;
  REG(UART+12)=3; REG(UART+0x28)=0x100; REG(UART+8)=7; REG(UART+0x30)=0xC1;
  while(REG(UART+0x14)&1u) (void)REG(UART);
}
static void sl_close(void){
  u32 t=sl_now(); while(!(REG(UART+0x14)&0x40u) && sl_now()-t<100u) hw_wdt_feed();
  REG(UART+4)=0; REG(UART+0x30)=0;
}
#endif
enum { SL_ROM=1,SL_PREPARE,SL_WAIT,SL_PEER,SL_HEADER,SL_DATA,SL_VERIFY,SL_COMMIT,SL_FINISH };
static u32 sl_ui_phase,sl_ui_start,sl_ui_paint,sl_ui_done,sl_ui_total;
static int sl_ui_receiving;
#ifdef HOST
static u32 sl_ui_seen,sl_ui_full_paints;
#endif
static const char *sl_ui_title(void){
  switch(sl_ui_phase){
    case SL_ROM: return "CHECKING ROM";
    case SL_PREPARE: return "PREPARING SAVE";
    case SL_WAIT: return "WAITING FOR PEER";
    case SL_PEER: return "PEER FOUND";
    case SL_HEADER: return sl_ui_receiving?"READING SAVE INFO":"SENDING SAVE INFO";
    case SL_DATA: return sl_ui_receiving?"RECEIVING SAVE":"SENDING SAVE";
    case SL_VERIFY: return "CHECKING SAVE";
    case SL_COMMIT: return "SAVING SLOT";
    default: return sl_ui_receiving?"SAVE RECEIVED":"SAVE SENT";
  }
}
/* Full repaint on every phase transition, partial updates only on this clean
 * transfer screen. Never leave portions of the incoming-save confirmation. */
static void sl_ui_draw(int full){
  if(!sl_ui_phase) return;
  u32 now=sl_now(); sl_ui_paint=now;
  if(full){
    rect(0,0,128,128,BG); ctext(4,sl_ui_receiving?"RECEIVE SAVE":"SEND SAVE",GOLD,1);
    ctext(20,sl_ui_phase<=SL_PREPARE?"PLEASE WAIT":sl_ui_phase==SL_WAIT?"CONNECT BOTH TAMAS":sl_ui_phase==SL_FINISH?"FINISHING LINK":"CONNECTED",GREY,1);
    ctext(118,sl_ui_phase>=SL_COMMIT?"DO NOT DISCONNECT":"C CANCEL",GREY,1);
#ifdef HOST
    sl_ui_full_paints++;
#endif
  }
  rect(0,36,128,112,BG); ctext(44,sl_ui_title(),WHITE,1);
  rect(14,62,114,72,GREY); rect(15,63,113,71,BG);
  char s[20];
  if(sl_ui_total){
    u32 pct=sl_ui_done*100u/sl_ui_total; if(pct>100u) pct=100u;
    rect(15,63,15+(int)(98u*pct/100u),71,RGB(40,120,255));
    utoa_(pct,s); int k=slen(s); s[k++]='%'; s[k]=0; ctext(84,s,GOLD,1);
  } else {
    u32 x=((now-sl_ui_start)/250u)%85u;
    rect(15+(int)x,63,28+(int)x,71,GOLD);
    ctext(84,"WORKING...",GREY,1);
  }
  utoa_((now-sl_ui_start)/1000u,s); int k=slen(s); s[k++]=' '; s[k++]='S'; s[k++]=' '; s[k++]="|/-\\"[((now-sl_ui_start)/500u)%4u]; s[k]=0;
  ctext(103,s,GREY,1);
  if(full) show(); else hw_push(g_fb,128,0,36,128,112);
}
static void sl_ui_set(u32 phase){
  sl_ui_phase=phase; sl_ui_start=sl_now(); sl_ui_done=sl_ui_total=0;
  if(phase==SL_FINISH) sl_ui_done=sl_ui_total=1;
#ifdef HOST
  sl_ui_seen|=1u<<phase;
#endif
  sl_ui_draw(1);
}
static void sl_ui_begin(int receive){
  sl_ui_receiving=receive;
#ifdef HOST
  sl_ui_seen=sl_ui_full_paints=0;
#endif
  sl_ui_set(SL_ROM);
}
static void sl_ui_progress(u32 done,u32 total){
  sl_ui_done=done; sl_ui_total=total;
  if(sl_now()-sl_ui_paint>=150u || (total && done==total)) sl_ui_draw(0);
}
static void sl_ui_pulse(void){ if(sl_ui_phase && sl_now()-sl_ui_paint>=500u) sl_ui_draw(0); }
static int sl_aborted(void){
#ifndef HOST
  if(hw_buttons()&BTN_C) sl_cancel=1;
#endif
  return sl_cancel;
}
static u32 sl_crc(u32 c,u8 b){ c^=b; for(u32 i=0;i<8;i++) c=(c>>1)^((c&1u)?0xEDB88320u:0u); return c; }
static void sl_w32(u8 *p,u32 x){ for(u32 i=0;i<4;i++) p[i]=(u8)(x>>(8u*i)); }
static u32 sl_r32(const u8 *p){ return p[0]|((u32)p[1]<<8)|((u32)p[2]<<16)|((u32)p[3]<<24); }
static u32 sl_session,sl_seq,sl_rx_session,sl_rx_seq;
static u8 sl_rx_type,sl_rx_n,sl_rx[128];
static int sl_frame(u8 type,u32 session,u32 seq,const u8 *data,u32 n){
  if(n>128u) return 0;
  u8 h[14]; sl_w32(h,0x31534D50u); sl_w32(h+4,session); sl_w32(h+8,seq); h[12]=type; h[13]=(u8)n;
  u32 crc=0xFFFFFFFFu;
  for(u32 i=0;i<14u+n;i++){ u8 b=i<14u?h[i]:data[i-14u]; crc=sl_crc(crc,b); if(!sl_put(b)) return 0; }
  crc=~crc; for(u32 i=0;i<4;i++) if(!sl_put((u8)(crc>>(8u*i)))) return 0;
  return 1;
}
static int sl_read(u32 ms){
  u32 start=sl_now(), magic=0, crc; u8 b,h[14];
  for(;;){ u32 elapsed=sl_now()-start; if(elapsed>=ms || !sl_byte(&b,ms-elapsed)) return 0;
    magic=(magic>>8)|((u32)b<<24); if(magic==0x31534D50u) break; }
  sl_w32(h,magic);
  for(u32 i=4;i<14;i++){ u32 e=sl_now()-start; if(e>=ms || !sl_byte(h+i,ms-e)) return 0; }
  if(h[13]>128u) return 0;
  crc=0xFFFFFFFFu; for(u32 i=0;i<14;i++) crc=sl_crc(crc,h[i]);
  for(u32 i=0;i<h[13];i++){ u32 e=sl_now()-start; if(e>=ms || !sl_byte(sl_rx+i,ms-e)) return 0; crc=sl_crc(crc,sl_rx[i]); }
  u8 tail[4]; for(u32 i=0;i<4;i++){ u32 e=sl_now()-start; if(e>=ms || !sl_byte(tail+i,ms-e)) return 0; }
  if(~crc!=sl_r32(tail)) return 0;
  sl_rx_session=sl_r32(h+4); sl_rx_seq=sl_r32(h+8); sl_rx_type=h[12]; sl_rx_n=h[13]; return 1;
}
#ifdef HOST
static int sl_test_drop_ack=-1;
#endif
static void sl_ack(u32 seq,u8 status){
#ifdef HOST
  if((int)seq==sl_test_drop_ack){ sl_test_drop_ack=-1; return; }
#endif
  sl_frame('A',sl_session,seq,&status,1);
}
static int sl_request(u8 type,const u8 *data,u32 n){
  for(u32 retry=0;retry<(type=='O'?90u:12u);retry++){
    if(sl_cancel) return 0;
    if(!sl_frame(type,sl_session,sl_seq,data,n)) return 0;
    u32 t=sl_now();
    while(!sl_cancel){
      u32 elapsed=sl_now()-t; if(elapsed>=1000u || !sl_read(1000u-elapsed)) break;
      if(sl_rx_session==sl_session && sl_rx_seq==sl_seq && sl_rx_type=='A' && sl_rx_n==1u){
        if(sl_rx[0]) return 0; sl_seq++; return 1;
      }
    }
    /* Idle animation is outside frame parsing; never delay byte reception
     * with display writes while a frame is still arriving. */
    sl_ui_pulse();
  }
  return 0;
}
/* Returns next frame without acknowledging it; caller validates/stores first. */
static int sl_next(u8 type,u32 n){
  u32 t=sl_now();
  while(!sl_cancel && sl_now()-t<15000u){
    if(!sl_read(1000u)){ sl_ui_pulse(); continue; }
    if(sl_rx_session!=sl_session) continue;
    if(sl_seq && sl_rx_seq==sl_seq-1u){ sl_ack(sl_rx_seq,0); continue; }
    if(sl_rx_seq==sl_seq && sl_rx_type==type && sl_rx_n==n) return 1;
  }
  return 0;
}
static void sl_accept(void){ sl_ack(sl_seq++,0); }
/* Require matching game resources AND save layout, not just a matching name.
 * Menu/stub changes do not affect this signature. */
static u32 sl_signature(void){
  u32 len=rd32(0x111008u), crc=0xFFFFFFFFu;
  if(len>0xD00000u) return 0;
  for(u32 a=0x111000u;a<0x111010u+len;a++){
    crc=sl_crc(crc,fl(a)[0]); if(!(a&0xFFFu)){
      hw_wdt_feed(); if(sl_aborted()) return 0;
      if(sl_ui_phase==SL_ROM) sl_ui_progress(a-0x111000u,len+16u);
    }
  }
  if(sl_ui_phase==SL_ROM) sl_ui_progress(len+16u,len+16u);
  u32 v[8]={g_cfg.id_c,g_cfg.user_start,g_cfg.user_end,g_cfg.tama_off,g_cfg.field_byte,g_cfg.six_off,g_cfg.list_off,g_cfg.count_off};
  for(u32 k=0;k<sizeof v;k++) crc=sl_crc(crc,((u8*)v)[k]);
  for(u32 k=0;k<8;k++) crc=sl_crc(crc,g_cfg.six_map[k]);
  for(u32 i=0;i<nfields();i++){ u32 f=fid(i); for(u32 k=0;k<4;k++) crc=sl_crc(crc,(u8)(f>>(8u*k))); }
  return ~crc;
}
static int sl_export(u32 src,u32 *fld){
  u32 np; if(source_check(src,&np)) return 0;
  u32 by=0,sum=0,prev=0,sets=0;
  for(u32 i=0;i<np;i++){
    u32 a; if(save_page(src,i,&a) || a<g_cfg.user_start || a>=g_cfg.user_end || (i && a<=prev)) return 0;
    prev=a;
    if(a==SET_A || a==SET_B){ if(!set_ok(g_page)) return 0; sets|=a==SET_A?1u:2u; }
    if(a==SET_A){ int f=field_of(g_page); if(f<0) return 0; *fld=(u32)f; }
    u32 n=lz_pack(g_page,g_cmp); g_hdr.e[i].page=(u16)(a>>12); g_hdr.e[i].clen=(u16)n;
    by+=n; for(u32 k=0;k<n;k++) sum+=g_cmp[k];
    if(sl_aborted()) return 0;
    if(sl_ui_phase==SL_PREPARE) sl_ui_progress(i+1u,np);
  }
  g_hdr.npages=np; g_hdr.bytes=by; g_hdr.sum=sum;
  return sets==3u;
}
static int sl_send(u32 src){
  sl_ui_begin(0);
  u32 fld=0, sig=sl_signature(); if(!sig) return 0;
  sl_ui_set(SL_PREPARE); if(!sl_export(src,&fld)) return 0;
  sl_session=sl_now()^g_hdr.sum^0xAA583197u; sl_seq=0;
  u8 offer[32]; sl_w32(offer,1); sl_w32(offer+4,sig); sl_w32(offer+8,g_hdr.npages);
  sl_w32(offer+12,g_hdr.bytes); sl_w32(offer+16,g_hdr.sum); sl_w32(offer+20,fld); sl_w32(offer+24,src);
  sl_w32(offer+28,sl_counter());
  sl_ui_set(SL_WAIT);
  if(!sl_request('O',offer,sizeof offer)) return 0;
  sl_ui_set(SL_HEADER);
  for(u32 i=0;i<g_hdr.npages;){
    u32 n=g_hdr.npages-i; if(n>32u) n=32u;
    if(!sl_request('H',(u8*)&g_hdr.e[i],n*4u)) return 0; i+=n;
    sl_ui_progress(i,g_hdr.npages);
  }
  sl_ui_set(SL_DATA); u32 sent=0;
  for(u32 i=0;i<g_hdr.npages;i++){
    u32 a; if(save_page(src,i,&a)) return 0;
    u32 n=lz_pack(g_page,g_cmp); if(n!=g_hdr.e[i].clen) return 0;
    for(u32 pos=0;pos<n;pos+=128u){ u32 c=n-pos; if(c>128u) c=128u; if(!sl_request('D',g_cmp+pos,c)) return 0; sent+=c; sl_ui_progress(sent,g_hdr.bytes); }
  }
  sl_ui_set(SL_VERIFY);
  int ok=sl_request('E',0,0); if(ok) sl_ui_set(SL_FINISH); return ok;
}
static int sl_flush(u32 tgt,u32 *nd){
  if(*nd>=DPG_MAX) return 0;
  u32 x=pool_store(tgt,g_out);
  if(x==0xFFFFu || !flash_same(pool_addr(x),g_out,PAGE)) return 0;
  g_hdr.dpg[(*nd)++]=(u16)x; return 1;
}
static int sl_receive(u32 tgt){
  if(tgt>=g_cfg.nslots || (int)tgt==g_act || slot_valid(tgt)) return 0;
  sl_ui_begin(1);
  u32 sig=sl_signature(); if(!sig) return 0;
  sl_ui_set(SL_WAIT);
  u32 t=sl_now();
  while(!sl_cancel && sl_now()-t<90000u){
    if(sl_read(1000u) && sl_rx_type=='O' && sl_rx_seq==0u && sl_rx_n==32u) goto offer;
    sl_ui_pulse();
  }
  return 0;
offer:;
  sl_ui_set(SL_PEER);
  sl_session=sl_rx_session; sl_seq=0;
  u32 np=sl_r32(sl_rx+8),by=sl_r32(sl_rx+12),sum=sl_r32(sl_rx+16),fld=sl_r32(sl_rx+20),src=sl_r32(sl_rx+24);
  u32 from_counter=sl_r32(sl_rx+28),to_counter=sl_counter();
  u32 nd=(by+PAGE-1u)/PAGE;
  if(sl_r32(sl_rx)!=1u || sl_r32(sl_rx+4)!=sig || !np || np>NPAGE_MAX || !by || by>np*sizeof g_cmp || fld>=nfields() || nd>DPG_MAX){
    sl_ack(0,1); return 0;
  }
  rect(0,0,128,128,BG); ctext(14,"RECEIVE SAVE?",WHITE,1);
  char label[16]; save_label(tgt,label); ctext(30,label,GOLD,2);
  ctext(54,fname(fld),fcol(fld),1); save_label(src<9u?src:0u,label); ctext(68,"FROM",GREY,1); ctext(80,label,WHITE,1);
  ctext(98,"B ACCEPT",WHITE,1); ctext(110,"C CANCEL",GREY,1); show();
  if(!yes_no()){ sl_ack(0,1); return 0; }
  /* Repaint BEFORE ACK: the sender is still waiting, so no incoming frame
   * can be lost while the LCD changes from confirmation to transfer. */
  sl_ui_set(SL_HEADER);
  pool_mark(g_cfg.nslots);
  sl_accept();
  for(u32 i=0;i<sizeof g_hdr/4u;i++) ((u32*)&g_hdr)[i]=0xFFFFFFFFu;
  u32 count=0,prev=0,sets=0;
  for(u32 i=0;i<np;){
    u32 n=np-i; if(n>32u) n=32u;
    if(!sl_next('H',n*4u)) return 0;
    for(u32 k=0;k<n*4u;k++) ((u8*)&g_hdr.e[i])[k]=sl_rx[k];
    for(u32 k=i;k<i+n;k++){
      u32 a=(u32)g_hdr.e[k].page<<12, len=g_hdr.e[k].clen;
      if(a<g_cfg.user_start || a>=g_cfg.user_end || (k && a<=prev) || !len || len>sizeof g_cmp) return 0;
      prev=a; count+=len;
    }
    i+=n; sl_ui_progress(i,np);
    if(i==np) sl_ui_set(SL_DATA);
    sl_accept();
  }
  if(count!=by) return 0;
  u32 on=0,used=0,actual=0,received=0;
  for(u32 i=0;i<np;i++){
    u32 n=g_hdr.e[i].clen, a=(u32)g_hdr.e[i].page<<12;
    for(u32 pos=0;pos<n;pos+=128u){
      u32 c=n-pos; if(c>128u) c=128u;
      if(!sl_next('D',c)) return 0;
      for(u32 k=0;k<c;k++) g_cmp[pos+k]=sl_rx[k];
      received+=c; sl_ui_progress(received,by);
      /* Last chunk is acknowledged only after decompressing and storing. */
      if(pos+c<n) sl_accept();
    }
    if(lz_unpack(g_cmp,n,g_page)) return 0;
    if(a==SET_A || a==SET_B){ if(!set_ok(g_page) || field_of(g_page)!=(int)fld) return 0; sets|=a==SET_A?1u:2u; }
    for(u32 k=0;k<n;k++){
      actual+=g_cmp[k]; g_out[on++]=g_cmp[k];
      if(on==PAGE){ if(!sl_flush(tgt,&used)){ sl_ack(sl_seq,1); return 0; } on=0; }
    }
    if(i+1u==np) sl_ui_set(SL_VERIFY);
    sl_accept();
  }
  if(!sl_next('E',0)) return 0;
  if(actual!=sum || sets!=3u){ sl_ack(sl_seq,1); return 0; }
  if(on){
    for(u32 i=on;i<PAGE;i++) g_out[i]=0xFF;
    if(!sl_flush(tgt,&used)){ sl_ack(sl_seq,1); return 0; }
  }
  if(used!=nd){ sl_ack(sl_seq,1); return 0; }
  sl_ui_set(SL_COMMIT);
  g_hdr.magic=SLOT_MAGIC; g_hdr.field=tgt+1u; g_hdr.npages=np; g_hdr.bytes=by; g_hdr.sum=sum;
  g_hdr.res[0]=POOL_MAGIC; g_hdr.res[1]=used; g_hdr.res[2]=0;
  for(u32 i=0;i<PAGE;i++) g_out[i]=i<sizeof g_hdr ? ((u8*)&g_hdr)[i] : 0xFF;
  sl_w32(g_out+SLOT_TIME_OFF,SLOT_TIME_MAGIC); sl_w32(g_out+SLOT_TIME_OFF+4,from_counter);
  sl_w32(g_out+SLOT_TIME_OFF+8,to_counter); sl_w32(g_out+SLOT_TIME_OFF+12,SLOT_TIME_MAGIC^from_counter^to_counter);
  flash_prog4k(slot_base(tgt),g_out);
  if(!flash_same(slot_base(tgt),g_out,PAGE) || !slot_valid(tgt)){ slot_clear(tgt); return 0; }
  sl_ui_set(SL_FINISH);
  sl_accept();
  /* Lost final ACK: keep acknowledging retransmitted E frames briefly. */
  t=sl_now(); while(!sl_cancel && sl_now()-t<3000u){ if(sl_read(1000u) && sl_rx_session==sl_session && sl_rx_seq==sl_seq-1u && sl_rx_type=='E') sl_ack(sl_rx_seq,0); sl_ui_pulse(); }
  return 1;
}
/* Return true only when reception created a new local slot. */
static int save_link_menu(u32 slot,int receive){
  rect(0,0,128,128,BG); ctext(14,receive?"RECEIVE SAVE":"SEND SAVE",WHITE,1);
  ctext(42,"CONNECT BOTH TAMAS",GREY,1); ctext(56,"AT THE TOP",GREY,1);
  ctext(82,"B START",WHITE,1); ctext(102,"C CANCEL",GREY,1); show();
  if(!yes_no()) return 0;
  sl_ui_begin(receive); wait_release();
  sl_cancel=0; sl_open(); int ok=receive?sl_receive(slot):sl_send(slot); sl_close();
  if(ok) message(receive?"SAVE RECEIVED":"SAVE SENT",receive?"SELECT SLOT TO LOAD":"SOURCE IS KEPT","");
  else message("TRANSFER STOPPED","CHECK LINK / ROM",receive?"TARGET NOT LOADED":"SOURCE IS KEPT");
  return receive && ok;
}
