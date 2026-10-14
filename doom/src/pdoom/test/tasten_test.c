
#include <stdio.h>
typedef unsigned int u32; typedef unsigned short pkey_t;
#define BTN_A 1u
#define BTN_B 2u
#define BTN_C 4u
#define BTN_WHEEL 8u
enum { KEY_UP=1, KEY_DOWN=2, KEY_LEFT=4, KEY_RIGHT=8, KEY_FIRE=16, KEY_USE=32, KEY_MENU=64, KEY_MAP=128, KEY_CHGW=256 };
static u32 T, BTN; static volatile int g_enc_det; static int runs, snd, left;
static u32 now_ms(void){ return T; } static u32 hw_buttons(void){ return BTN; }
static void plat_toggle_run(void){ runs++; } static void plat_sound_mode_next(void){ snd++; } static void plat_leave(void){ left++; }
typedef struct { u32 magic, version, a,b,c,d,e,f, detent, turn_tics, sound, invert, k_fire,k_fwd,k_back,k_use,k_mod,run_dbl; } PdmCfg;
static PdmCfg g_dcfg={0,2,0,0,0,0,0,0,1,3,1,0, BTN_A,BTN_B,BTN_C,BTN_WHEEL,BTN_C,1};
#define cpsid
static volatile u32 g_wh_down, g_wh_cand, g_wh_t0, g_wh_press;
static void wheel_poll(u32 ms, u32 down){
  if(down==g_wh_down){ g_wh_cand=0; return; }
  if(!g_wh_cand){ g_wh_cand=ms|1u; return; }
  if((int)(ms-g_wh_cand)<8) return;
  g_wh_cand=0; g_wh_down=down;
  if(down){ g_wh_t0=ms|1u; g_wh_press++; } else g_wh_t0=0;
}
void getKeys(pkey_t *keys){
  static u32 mod_t0, b_last, b_prev, use_left, turn_left, menu_hold, chg_left, chg_dir, wh_seen;
  static int turn_dir, det_seen;
  u32 now=now_ms(), b=hw_buttons(); pkey_t k=0;
  u32 kf=g_dcfg.k_fire, kv=g_dcfg.k_fwd, kz=g_dcfg.k_back, ku=g_dcfg.k_use, km=g_dcfg.k_mod;
  if(g_dcfg.version<2u){ kf=BTN_A; kv=BTN_B; kz=BTN_C; ku=BTN_WHEEL; km=BTN_C; }
  /* Wheel pressed: with hold key = sound, otherwise use (if mapped so); held 2 s from the last press = exit.
   * Presses and the time come from the millisecond timer (wheel_poll); this call only adds a sample
   * in case the timer interrupt does not run. */
  u32 np, wdown, wt0;
  
  wheel_poll(now, (b & BTN_WHEEL) ? 1u : 0u);
  np=g_wh_press-wh_seen; wh_seen+=np; wdown=g_wh_down; wt0=g_wh_t0;
  
  if(np){ if(b & km){ det_seen=1; plat_sound_mode_next(); } else if(ku==BTN_WHEEL) use_left=3; }
  if(wdown && wt0 && now-wt0>2000u) plat_leave();
  int det;  det=g_enc_det; g_enc_det=0; 
  if(g_dcfg.invert) det=-det;
  if(det){
    if(b & km){ chg_left=2; chg_dir = det>0 ? 1u : 0u; det_seen=1; }
    else { if((det>0) != (turn_dir>0)) turn_left=0; turn_dir = det>0 ? 1 : -1; turn_left += (u32)(det>0?det:-det) * g_dcfg.turn_tics; }
  }
  int ac = (b & BTN_A) && (b & BTN_C), bc = (b & BTN_B) && (b & BTN_C);
  if(ac) menu_hold++; else menu_hold=0;
  if(ac){ if(menu_hold<=2) k|=KEY_MENU; }
  else if(bc){ k|=KEY_MAP; }
  else {
    /* pressed, unless the hold key is still in its waiting time */
    u32 eff = b & ~km;
    if((b & km) && !det_seen){ if(!mod_t0) mod_t0=now|1u; if((int)(now-mod_t0)>=120) eff |= km; }   /* v0.4.0: signed, otherwise a step already came in the first tick (now even) */
    if(eff & kv) k|=KEY_UP;
    if(eff & kz) k|=KEY_DOWN;
    if(eff & kf) k|=KEY_FIRE;
    if(ku!=BTN_WHEEL && (eff & ku) && !(b_last & ku)) use_left=3;
  }
  if(!(b & km)){ mod_t0=0; det_seen=0; }
  if(g_dcfg.run_dbl || g_dcfg.version<2u){
    if((b & kv) && !(b_last & kv)){ if(b_prev && now-b_prev<300u){ plat_toggle_run(); b_prev=0; } else b_prev=now; }
  }
  b_last = b;
  if(use_left){ use_left--; k|=KEY_USE; }
  if(turn_left){ turn_left--; k |= turn_dir>0 ? KEY_RIGHT : KEY_LEFT; }
  if(chg_left){ chg_left--; k |= chg_dir ? KEY_CHGW : (KEY_CHGW|KEY_USE); }
  *keys=k;
}
static void tick(u32 b){ BTN=b; T++; wheel_poll(T,(b&BTN_WHEEL)?1u:0u); }
static pkey_t step(u32 b,int det){ BTN=b; g_enc_det=det; pkey_t k; getKeys(&k); for(int i=0;i<10;i++) tick(b); return k; }
/* one frame of 100 ms with a quick tap: up 30 ms, then down; the frame itself only sees "down" */
static pkey_t frame_tap(void){ pkey_t k=0; for(int i=0;i<100;i++) tick(i>=30?BTN_WHEEL:0); getKeys(&k); return k; }
static pkey_t run(u32 b,int n){ pkey_t acc=0; for(int i=0;i<n;i++) acc|=step(b,0); return acc; }
int main(void){
  int bad=0;
#define CHECK(c,m) do{ if(!(c)){ printf("FALSCH %s\n",m); bad++; } else printf("ok %s\n",m); }while(0)
  CHECK(run(BTN_A,3)&KEY_FIRE, "A fires at once");
  run(0,5); CHECK(run(BTN_B,3)&KEY_UP, "B vor");
  run(0,5); CHECK(!(run(BTN_C,5)&KEY_DOWN), "C not back at once");
  CHECK(run(BTN_C,15)&KEY_DOWN, "C nach 120 ms zurueck");
  run(0,5); { pkey_t k=step(BTN_C,1); k|=run(BTN_C,20); CHECK((k&KEY_CHGW) && !(k&KEY_DOWN), "C+wheel weapon, no back"); }
  run(0,5); run(BTN_WHEEL,3); CHECK(run(0,4)&KEY_USE, "Rad kurz benutzen");
  run(0,50); runs=0; run(BTN_B,2); run(0,2); run(BTN_B,2); CHECK(runs==1, "2x B Laufen");
  run(0,50); snd=0; run(BTN_C,2); run(BTN_C|BTN_WHEEL,2); run(BTN_C,2); CHECK(snd==1, "C+Rad druecken Ton");
  run(0,10); CHECK(run(BTN_A|BTN_C,3)&KEY_MENU, "A+C Menue");
  run(0,10); { pkey_t k=step(0,2); k|=run(0,8); CHECK(k&KEY_RIGHT, "Rad dreht"); }
  /* remapped: B fires, A forward, use on C, weapon with A */
  g_dcfg.k_fire=BTN_B; g_dcfg.k_fwd=BTN_A; g_dcfg.k_back=BTN_C; g_dcfg.k_use=BTN_C; g_dcfg.k_mod=BTN_A;
  run(0,50); CHECK(run(BTN_B,3)&KEY_FIRE, "remapped: B fires");
  run(0,5); CHECK(!(run(BTN_A,5)&KEY_UP) && (run(BTN_A,15)&KEY_UP), "umbelegt: A vor nach Wartezeit (Haltetaste)");
  run(0,5); { pkey_t k=step(BTN_A,1); k|=run(BTN_A,20); CHECK((k&KEY_CHGW) && !(k&KEY_UP), "umbelegt: A+Rad Waffe"); }
  run(0,5); { pkey_t k=run(BTN_C,2); k|=run(0,4); CHECK(k&KEY_USE, "umbelegt: C benutzt"); }
  run(0,5); run(BTN_WHEEL,3); CHECK(!(run(0,4)&KEY_USE), "remapped: wheel does not use");
  /* v0.4.1: quick taps between the frames (60 ms down, 40 ms up, frames every 100 ms): no exit, uses come */
  g_dcfg.k_fire=BTN_A; g_dcfg.k_fwd=BTN_B; g_dcfg.k_back=BTN_C; g_dcfg.k_use=BTN_WHEEL; g_dcfg.k_mod=BTN_C;
  run(0,50); left=0; { int uses=0; for(int i=0;i<60;i++){ pkey_t k=frame_tap(); if(k&KEY_USE) uses++; }
    CHECK(left==0, "6 s quick taps: no exit"); CHECK(uses>=20, "quick taps: use comes"); }
  run(0,50); left=0; run(BTN_WHEEL,150); CHECK(left==0, "wheel 1.5 s: still in"); run(BTN_WHEEL,60); CHECK(left>0, "wheel over 2 s: exit");
  printf(bad?"%d wrong\n":"all good\n",bad); return bad;
}
