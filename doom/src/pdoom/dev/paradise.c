/* DOOM for the Tamagotchi Paradise: platform layer (startup, time, buttons and wheel, display, sound, flash).
 * Core: next-hack nRF52840 Doom (GBADoom, PrBoom), GPL. This file: mameise, GPL. */
#include "io.h"
#include "hw.h"
#include "flash.h"
#include <stdint.h>
#include <string.h>
#include "graphics.h"
#include "pwm_audio.h"
#include "nrf.h"
#include "keyboard.h"

#define PRAMF __attribute__((section(".pram_text"), noinline))
static volatile unsigned int g_stage, g_fl_words;
void plat_log_raw(unsigned int w); void plat_log(unsigned int type, unsigned int val);
void plat_stage(unsigned int st);
#define R20B  __attribute__((section(".ram20_bss")))

/* ---- Settings, filled in by the ROM builder ----
 * PMXC: required by the stub (like for the emulator), contents do not matter here.
 * PDMC: DOOM. Button bits see below. */
typedef struct { u32 magic, version, guest_off, guest_size, save_off, device; u8 map[128]; } PmxCfg;
__attribute__((section(".pmxcfg"), used)) volatile const PmxCfg g_cfg = { 0x43584D50u, 1, 0, 0, 0, 0, {0} };
typedef struct {
  u32 magic, version;
  u32 wad_off, wad_len;            /* WAD in flash (offset), 4 byte length before the WAD */
  u32 region_off, region_len;      /* area for precomputed data (the core writes it itself) */
  u32 save_off;                    /* 2 x 4 KB: save, settings */
  u32 code_off;                    /* program part in the flash window */
  u32 detent;                      /* encoder steps per detent (2 or 4) */
  u32 turn_tics;                   /* turn duration per detent in game tics */
  u32 sound;                       /* 1 = sound on */
  u32 invert;                      /* 1 = wheel reversed */
  /* from version 2 (KEYMAP): key mapping as button masks (BTN_A 1, BTN_B 2, BTN_C 4, BTN_WHEEL 8) */
  u32 k_fire, k_fwd, k_back, k_use, k_mod;   /* k_mod: hold + wheel = switch weapon, hold + press wheel = sound */
  u32 run_dbl;                     /* 1 = double tap forward toggles run */
} PdmCfg;
__attribute__((section(".pdmcfg"), used)) volatile const PdmCfg g_dcfg =
  { 0x434D4450u, 2, 0x830004u, 0, 0xCA0000u, 0xA8000u, 0x10E000u, 0xB1000u, 1, 3, 1, 1,   /* v0.2.4: wheel reversed by default */
    BTN_A, BTN_B, BTN_C, BTN_WHEEL, BTN_C, 1 };

#define XIP_BASE_REG REG(0x4002F004u)
#define XIP_CTRL_REG REG(0x4002F000u)
#define SYST_CSR REG(0xE000E010u)
#define SYST_RVR REG(0xE000E014u)
#define SYST_CVR REG(0xE000E018u)
#define GPIO2 0x4001A000u

/* ---- Time ---- */
static volatile u32 g_ms;
/* v0.1.5: time also works without the timer interrupt. The counter bit (COUNTFLAG) is polled on every call;
 * the larger of the two counters wins. If the interrupt never comes, DOOM still runs. */
static volatile u32 g_poll_ms, g_diag_t;
static volatile u32 g_frames, g_last_frame, g_in_push, g_beep;
PRAMF void plat_line(u32 row, const u32 *v, const u8 *nd, u32 cnt);
PRAMF static u32 now_ms(void){
  if(SYST_CSR & 0x10000u) g_poll_ms++;
  return g_ms > g_poll_ms ? g_ms : g_poll_ms;
}
PRAMF unsigned int plat_micros(void){
  u32 m=now_ms(), v=SYST_CVR;
  REG(0x4000800Cu)=0x5AFA55AAu; REG(0x4000900Cu)=0x5AFA55AAu;
#ifdef DIAG_OVL
  /* Sign of life without the interrupt: while no frame has come, line 3 every second after 8 s */
  if(!g_frames && m>8000u && m-g_diag_t>=1000u){ g_diag_t=m;
    u32 pm; __asm__ volatile("mrs %0, primask" : "=r"(pm));
    u32 vv[5]={3, g_ms & 0xFFFFu, g_poll_ms & 0xFFFFu, pm, (SYST_CSR & 7u)}; static const u8 n[5]={1,4,4,1,1}; plat_line(3, vv, n, 5); }
#endif
  return m*1000u + (95999u - v)/96u; }
PRAMF void delay(unsigned int ms){ u32 t=now_ms(); while(now_ms()-t<ms){ } }

/* ---- Rotary encoder (polled 1000 times per s) ---- */
static volatile u32 g_enc_last; static volatile int g_enc_acc, g_enc_det;
__attribute__((section(".pram_rodata"))) static const signed char g_qd[16]={0,-1,1,0, 1,0,0,-1, -1,0,0,1, 0,1,-1,0};
/* ---- PC speaker sounds (DP lumps): 140 steps per s ---- */
static volatile const uint8_t *g_snd; static volatile u32 g_snd_n, g_snd_t;
static volatile u32 g_snd_mode;   /* v0.2.6: 0 all on, 1 music off, 2 all off (C + press wheel) */
PRAMF static u32 snd_hz(u32 v){ /* 175 Hz * 2^((v-1)/24), like the dividers of the DOS version */
  static const uint16_t base[24]={17500,18013,18541,19084,19643,20219,20811,21421,22049,22695,23360,24044,24749,25474,26220,26989,27780,28594,29431,30294,31181,32095,33036,34004};
  u32 k=v-1u; return ((u32)base[k%24u] << (k/24u)) / 100u; }

#ifdef MUS_PLAY
/* ---- v0.2.5: music (MUS lumps from the WAD) on the buzzer: one voice, the most recently struck note still sounding.
 * Drums (channel 15) are dropped. Everything 2 octaves higher (the buzzer cannot do bass), the rest folded into 200..5000 Hz. */
static const uint16_t g_note_hz[128]={262,277,294,311,330,349,370,392,208,220,233,247,262,277,294,311,330,349,370,392,208,220,233,247,262,277,294,311,330,349,370,392,208,220,233,247,262,277,294,311,330,349,370,392,415,440,466,494,523,554,587,622,659,698,740,784,831,880,932,988,1047,1109,1175,1245,1319,1397,1480,1568,1661,1760,1865,1976,2093,2217,2349,2489,2637,2794,2960,3136,3322,3520,3729,3951,4186,4435,4699,4978,2637,2794,2960,3136,3322,3520,3729,3951,4186,4435,4699,4978,2637,2794,2960,3136,3322,3520,3729,3951,4186,4435,4699,4978,2637,2794,2960,3136,3322,3520,3729,3951,4186,4435,4699,4978,2637,2794,2960,3136};
static volatile const uint8_t *g_mus_start, *g_mus_pos, *g_mus_end;
static volatile u32 g_mus_on, g_mus_loop, g_mus_delay, g_mus_acc, g_mus_stamp, g_mus_vol=1;
static volatile int g_mus_cur=-1;
static uint16_t g_mus_st[128];     /* 0 = off, otherwise time stamp of the note on */
static uint8_t g_mus_chn[16];      /* last note per channel (for "all off") */
PRAMF static u32 mus_rd(void){ if(g_mus_pos>=g_mus_end){ g_mus_on=0; return 0; } return *g_mus_pos++; }
PRAMF static void mus_alloff(void){ for(u32 i=0;i<128u;i++) g_mus_st[i]=0; }
PRAMF static void mus_tick(void){
  if(g_mus_delay){ g_mus_delay--; if(g_mus_delay) return; }
  for(u32 guard=0; guard<64u && g_mus_on; guard++){
    u32 b=mus_rd(), t=(b>>4)&7u, ch=b&15u, x;
    switch(t){
      case 0: x=mus_rd()&127u; if(ch!=15u) g_mus_st[x]=0; break;
      case 1: x=mus_rd(); if(x&0x80u){ u32 v=mus_rd(); if(!v && ch!=15u){ g_mus_st[x&127u]=0; break; } }
              if(ch!=15u){ g_mus_stamp++; if(!(g_mus_stamp & 0xFFFFu)) g_mus_stamp=1; g_mus_st[x&127u]=(uint16_t)g_mus_stamp; g_mus_chn[ch]=(uint8_t)(x&127u); } break;
      case 2: mus_rd(); break;
      case 3: x=mus_rd(); if(x==10u || x==11u) mus_alloff(); break;
      case 4: mus_rd(); mus_rd(); break;
      case 6: mus_alloff(); if(g_mus_loop){ g_mus_pos=g_mus_start; g_mus_delay=1; return; } g_mus_on=0; return;
      default: break;
    }
    if(b & 0x80u){ u32 d=0; do{ x=mus_rd(); d=(d<<7)|(x&127u); } while((x & 0x80u) && g_mus_on); if(d){ g_mus_delay=d; return; } }
  }
}
PRAMF static void mus_ms(void){
  if(!g_mus_on) { if(g_mus_cur>=0 && !g_snd){ hw_tone(0); g_mus_cur=-1; } return; }
  g_mus_acc+=140u; if(g_mus_acc<1000u) return; g_mus_acc-=1000u;
  mus_tick();
  if(g_snd) return;                         /* game sounds take priority */
  int best=-1; u32 bs=0;
  if(g_mus_vol && !g_snd_mode) for(u32 i=0;i<128u;i++){ u32 s2=g_mus_st[i]; if(s2){ u32 age=(g_mus_stamp-s2)&0xFFFFu; if(best<0 || age<bs){ best=(int)i; bs=age; } } }
  if(best!=g_mus_cur){ g_mus_cur=best; hw_tone(best>=0 ? g_note_hz[best] : 0); }
}
#endif
/* ---- Wheel button, sampled every millisecond (v0.4.1) ----
 * DOOM reads the keys only once per frame. Quick taps on the wheel (looking for secret doors)
 * then fell between two frames, the releases were missed and the taps added up to a long press
 * (back to the Paradise). The timer now follows the button on its own: a change counts after
 * 8 ms without bouncing, every press is counted, the long press measures from the last press. */
static volatile u32 g_wh_down, g_wh_cand, g_wh_t0, g_wh_press;
PRAMF static void wheel_poll(u32 ms, u32 down){
  if(down==g_wh_down){ g_wh_cand=0; return; }
  if(!g_wh_cand){ g_wh_cand=ms|1u; return; }
  if((int)(ms-g_wh_cand)<8) return;
  g_wh_cand=0; g_wh_down=down;
  if(down){ g_wh_t0=ms|1u; g_wh_press++; } else g_wh_t0=0;
}
PRAMF void plat_hang(u32 *fr);
PRAMF void systick_c(u32 *fr){
  g_ms++;
  wheel_poll(g_ms, (REG(0x40018000u) & (1u<<8)) ? 0u : 1u);
#ifdef DIAG_OVL
  /* If execution hangs (no frame for 3 s, not in the middle of a frame output): show where it was interrupted */
  if(g_ms>6000u && g_ms-g_last_frame>3000u && (g_ms & 1023u)==0){ plat_hang(fr); hw_tone(880); g_beep=40; }
  if(g_beep && !--g_beep && !g_snd) hw_tone(0);
#endif
  REG(0x4000800Cu)=0x5AFA55AAu; REG(0x4000900Cu)=0x5AFA55AAu;
  u32 n=REG(GPIO2) & 3u;
  if(n!=g_enc_last){ g_enc_acc+=g_qd[(g_enc_last<<2)|n]; g_enc_last=n;
    int d=(int)g_dcfg.detent; if(d<1) d=2;
    while(g_enc_acc>=d){ g_enc_acc-=d; g_enc_det++; }
    while(g_enc_acc<=-d){ g_enc_acc+=d; g_enc_det--; } }
  if(g_snd && ++g_snd_t>=7u){ g_snd_t=0;           /* about 140 per s */
    if(g_snd_n){ u32 v=*g_snd++; g_snd_n--; hw_tone(v ? snd_hz(v) : 0); }
    else { g_snd=0; hw_tone(0);
#ifdef MUS_PLAY
      g_mus_cur=-2;   /* restart the music afterwards */
#endif
    } }
#ifdef MUS_PLAY
  mus_ms();
#endif
}
__attribute__((naked, section(".pram_text"))) void SysTick_Handler(void){ __asm__ volatile("tst lr, #4\n ite eq\n mrseq r0, msp\n mrsne r0, psp\n b systick_c\n"); }

/* ---- Sound ---- */
soundChannel_t soundChannels[MAX_CHANNELS];
extern int W_CheckNumForName(const char *name);
extern const void *W_CacheLumpNum(int lump);
extern const char *plat_sfx_name(int id);
void plat_sound_mode_next(void){
  g_snd_mode = (g_snd_mode + 1u) % 3u;
  __asm__ volatile("cpsid i");
  if(g_snd_mode==2u){ g_snd=0; g_snd_n=0; hw_tone(0); }
#ifdef MUS_PLAY
  g_mus_cur=-2;
#endif
  __asm__ volatile("cpsie i");
}
void plat_sound(int id){
  if(!g_dcfg.sound || g_snd_mode==2u) return;
  const char *nm=plat_sfx_name(id); if(!nm) return;
  if((nm[0]=='D'||nm[0]=='d') && (nm[1]=='S'||nm[1]=='s')) nm+=2;   /* v0.2.4: names here already come as DSxxxx */
  char b[9]={'D','P',0}; for(int i=0;i<6 && nm[i];i++){ char c=nm[i]; if(c>='a'&&c<='z') c-=32; b[2+i]=c; b[3+i]=0; }
  int l=W_CheckNumForName(b); if(l<0) return;
  const uint8_t *p=(const uint8_t*)W_CacheLumpNum(l);
  u32 cnt=p[2] | (p[3]<<8);
  __asm__ volatile("cpsid i"); g_snd_n=cnt; g_snd_t=7; g_snd=p+4; __asm__ volatile("cpsie i");
}

#ifdef MUS_PLAY
extern const char *plat_mus_name(int handle);
void plat_music_play(int handle, int looping){
  __asm__ volatile("cpsid i"); g_mus_on=0; __asm__ volatile("cpsie i");
  if(!g_dcfg.sound) return;
  const char *nm=plat_mus_name(handle); if(!nm) return;
  int l=W_CheckNumForName(nm); if(l<0) return;
  const uint8_t *p=(const uint8_t*)W_CacheLumpNum(l);
  if(p[0]!='M' || p[1]!='U' || p[2]!='S') return;
  u32 len=p[4] | (p[5]<<8), st=p[6] | (p[7]<<8);
  __asm__ volatile("cpsid i");
  mus_alloff(); g_mus_start=p+st; g_mus_pos=p+st; g_mus_end=p+st+len; g_mus_loop=(u32)looping;
  g_mus_delay=0; g_mus_acc=0; g_mus_cur=-2; g_mus_on=1;
  __asm__ volatile("cpsie i");
}
void plat_music_stop(void){ __asm__ volatile("cpsid i"); g_mus_on=0; mus_alloff(); __asm__ volatile("cpsie i"); }
void plat_music_pause(int p){ static u32 was; __asm__ volatile("cpsid i"); if(p){ was=g_mus_on; g_mus_on=0; } else if(was && g_mus_start){ g_mus_on=1; g_mus_cur=-2; } __asm__ volatile("cpsie i"); }
void plat_music_volume(int v){ g_mus_vol = v>0; g_mus_cur=-2; }
#endif
void muteSound(void){ __asm__ volatile("cpsid i"); g_snd=0; g_snd_n=0; __asm__ volatile("cpsie i"); hw_tone(0); }
void updateSound(void){}
void SetBacklight(int x){ (void)x; }
void disableWirelessAudio(void){}
void restoreWirelessAudio(void){}

/* ---- Display ---- */
R20B displayData_t displayData;
#ifdef SINGLE_FB
__attribute__((aligned(8))) static uint8_t g_fb[PDOOM_W * PDOOM_H];
#endif
plat_qspi_t plat_qspi;
static u16 g_line[128];
PRAMF void plat_line(u32 row, const u32 *v, const u8 *nd, u32 cnt);
PRAMF void startDisplayRefresh(uint8_t b){
  const uint8_t *fb=displayData.displayFrameBuffer[b];
  volatile uint16_t *pal=displayData.pPalette;
  g_frames++; g_last_frame=g_ms;
  if(!pal) return;
  g_in_push=1;
#ifdef DIAG_OVL
#ifndef SIM_NODIAG
  if(g_frames==1){
    /* v0.2.0: diagnostics page before the first frame, waits for B (at most 60 s) */
    extern void plat_diag_info(unsigned int *o);
    u32 sp; __asm__ volatile("mov %0, sp" : "=r"(sp));
    u32 di[8]; plat_diag_info(di);
    { u32 v[4]={0xF, di[0], di[1], sp}; static const u8 n[4]={1,2,2,8}; plat_line(0, v, n, 4); }
    { u32 v[4]={pal[0], pal[1], pal[2], pal[3]}; static const u8 n[4]={4,4,4,4}; plat_line(1, v, n, 4); }
    { u32 v[4]={pal[4], pal[5], pal[6], pal[7]}; static const u8 n[4]={4,4,4,4}; plat_line(2, v, n, 4); }
    { u32 v[2]={di[3], di[5]}; static const u8 n[2]={8,6}; plat_line(3, v, n, 2); }
    { const volatile u8 *pb=(const volatile u8*)di[3]; u32 v[6]={0,0,0,0,0,0};
      if(di[3]) for(u32 i=0;i<6;i++) v[i]=pb[i]; static const u8 n[6]={2,2,2,2,2,2}; plat_line(4, v, n, 6); }
    { const volatile u32 *pw=(const volatile u32*)(di[3] & ~3u); u32 v[2]={0,0};
      if(di[3]){ v[0]=pw[0]; v[1]=pw[1]; } static const u8 n[2]={8,8}; plat_line(5, v, n, 2); }
    { extern int zone_min_free; u32 v[2]={0xA, (u32)(-zone_min_free)}; static const u8 n[2]={1,6}; plat_line(6, v, n, 2); }
    { u32 nz0=0, nz1=0; for(u32 i=0;i<128u*128u;i++){ if(displayData.displayFrameBuffer[0][i]) nz0++; if(displayData.displayFrameBuffer[1][i]) nz1++; }
      u32 v[4]={b, nz0, nz1, (u32)fb}; static const u8 n[4]={1,4,4,8}; plat_line(7, v, n, 4); }
    { const volatile u8 *cm=(const volatile u8*)di[4]; u32 v[6]={0,0,0,0,0,0};
      if(di[4]) for(u32 i=0;i<6;i++) v[i]=cm[0x40+i]; static const u8 n[6]={2,2,2,2,2,2}; plat_line(8, v, n, 6); }
    { u32 v[3]={di[4], di[6], di[7]}; static const u8 n[3]={8,1,4}; plat_line(9, v, n, 3); }
    { u32 v[1]={0xB}; static const u8 n[1]={1}; plat_line(10, v, n, 1); }
    u32 t0=now_ms();
    while((hw_buttons() & BTN_B)==0 && now_ms()-t0<3000u){ REG(0x4000800Cu)=0x5AFA55AAu; REG(0x4000900Cu)=0x5AFA55AAu; }
    t0=now_ms(); while((hw_buttons() & BTN_B) && now_ms()-t0<5000u){ REG(0x4000800Cu)=0x5AFA55AAu; REG(0x4000900Cu)=0x5AFA55AAu; }
  }
#endif
#endif
#ifdef FRAME_ROW
  /* v0.1.7: output through the same path as the number lines (hw_px_row, one window per line) */
  for(u32 y=0;y<128;y++){
    for(u32 x=0;x<128;x++){ u16 c=pal[fb[x]]; g_line[x]=(u16)((c>>8)|(c<<8)); }
    hw_px_begin(0,y,128,y+1); hw_px_row(g_line,128); hw_px_end();
    fb+=128;
  }
#else
  hw_px_begin(0,0,128,128);
  for(u32 y=0;y<128;y++){
    for(u32 x=0;x<128;x++) g_line[x]=pal[fb[x]];
    hw_px_raw((const u8*)g_line, 256u);
    fb+=128;
  }
  hw_px_end();
#endif
#ifdef DIAG_OVL
  /* v0.1.3: bottom line: frames, seconds, palette color 0, color 1, first frame byte */
  { u32 nz=0; const uint8_t *f2=displayData.displayFrameBuffer[b]; for(u32 i=0;i<128u*128u;i+=4u) if(*(const u32*)(f2+i)) nz++;
    u32 v[5]={g_frames & 0xFFFu, (g_ms/1000u) & 0xFFFu, nz, pal[1], f2[64*128+64]}; static const u8 n[5]={3,3,4,4,2};
    plat_line(11, v, n, 5); }
#endif
  g_in_push=0;
}

/* ---- Exit ---- */
PRAMF void plat_leave(void){
  hw_tone(0);
  __asm__ volatile("cpsid i");
  SYST_CSR=0;
  REG(0x20000FF8u)=0;
  hw_bl_pwm_off();
  for(u32 t=0; hw_buttons() && t<20000000u; t++) if((t & 0xFFFFu)==0) hw_wdt_feed();
  XIP_BASE_REG=0x60011000u; XIP_CTRL_REG=3u;
  __asm__ volatile("dsb\n isb");
  { volatile u32 sum=0; for(u32 i=0;i<0x4000u;i++) sum+=*(volatile u32*)(0x10080000u+4u*i); (void)sum; }
  hw_back_to_paradise();
}

/* ---- Buttons ----
 * B forward, C back, A fire, turn wheel left/right, short wheel press door/use,
 * hold A + turn wheel: switch weapon, B twice quickly: run on/off,
 * A+C together: menu, B+C together: map, wheel 2 s: back to the Paradise. */
#define KEY_SPEED 0x0400
extern void plat_toggle_run(void); void plat_sound_mode_next(void);
#ifdef KEYMAP
/* v0.4.0: mapping from g_dcfg (configurable in the studio). Default as before: A fire, B forward,
 * C back, press wheel use, hold C + wheel weapon, hold C + press wheel sound, 2x B run.
 * Fixed: A+C menu, B+C map, wheel 2 s back to the Paradise.
 * The hold key (k_mod) only does its own action after 120 ms and only while the wheel
 * has not been turned, otherwise every weapon switch would also be a step back. */
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
  __asm__ volatile("cpsid i");
  wheel_poll(now, (b & BTN_WHEEL) ? 1u : 0u);
  np=g_wh_press-wh_seen; wh_seen+=np; wdown=g_wh_down; wt0=g_wh_t0;
  __asm__ volatile("cpsie i");
  if(np){ if(b & km){ det_seen=1; plat_sound_mode_next(); } else if(ku==BTN_WHEEL) use_left=3; }
  if(wdown && wt0 && now-wt0>2000u) plat_leave();
  int det; __asm__ volatile("cpsid i"); det=g_enc_det; g_enc_det=0; __asm__ volatile("cpsie i");
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
#else
void getKeys(pkey_t *keys){
  static u32 wheel_t0, a_t0, b_last, b_prev, use_left, turn_left, menu_hold, chg_left, chg_dir;
  static int turn_dir, det_seen;
  u32 now=now_ms(), b=hw_buttons(); pkey_t k=0;
  /* Wheel pressed: short = use, long = exit */
#ifdef SND_TOGGLE
  /* v0.2.6: hold C + press wheel: cycle sound (all on, music off, all off) */
  static u32 wheel_c;
  if((b & BTN_WHEEL) && !wheel_t0 && (b & BTN_C)){ wheel_c=1; det_seen=1; plat_sound_mode_next(); }
#endif
  if(b & BTN_WHEEL){ if(!wheel_t0) wheel_t0=now|1u; else if(now-wheel_t0>2000u) plat_leave(); }
  else {
#ifdef SND_TOGGLE
    if(wheel_c){ wheel_c=0; wheel_t0=0; } else
#endif
    { if(wheel_t0 && now-wheel_t0<800u) use_left=3; wheel_t0=0; } }
  /* Turns */
  int det; __asm__ volatile("cpsid i"); det=g_enc_det; g_enc_det=0; __asm__ volatile("cpsie i");
  if(g_dcfg.invert) det=-det;
  if(det){
#ifdef CHG_WITH_C
    if(b & BTN_C){ chg_left=2; chg_dir = det>0 ? 1u : 0u; det_seen=1; }   /* v0.2.4: hold C + wheel: weapon */
#else
    if(b & BTN_A){ chg_left=2; chg_dir = det>0 ? 1u : 0u; det_seen=1; }
#endif
    else { if((det>0) != (turn_dir>0)) turn_left=0; turn_dir = det>0 ? 1 : -1; turn_left += (u32)(det>0?det:-det) * g_dcfg.turn_tics; }
  }
  /* Combinations */
  int ac = (b & BTN_A) && (b & BTN_C), bc = (b & BTN_B) && (b & BTN_C);
  if(ac){ menu_hold++; }
  else menu_hold=0;
  if(ac){ if(menu_hold<=2) k|=KEY_MENU; }
  else if(bc){ k|=KEY_MAP; }
  else {
    if(b & BTN_B) k|=KEY_UP;
#ifdef CHG_WITH_C
    /* C: back only after 120 ms and only while the wheel has not been turned (otherwise weapon switch) */
    if((b & BTN_C) && !det_seen){ if(!a_t0) a_t0=now|1u; if(now-a_t0>=120u) k|=KEY_DOWN; }
    if(b & BTN_A) k|=KEY_FIRE;
#else
    if(b & BTN_C) k|=KEY_DOWN;
    if((b & BTN_A) && !det_seen){ if(!a_t0) a_t0=now|1u; if(now-a_t0>=40u) k|=KEY_FIRE; }
#endif
  }
#ifdef CHG_WITH_C
  if(!(b & BTN_C)){ a_t0=0; det_seen=0; }
#else
  if(!(b & BTN_A)){ a_t0=0; det_seen=0; }
#endif
  /* B twice quickly: toggle run */
  if((b & BTN_B) && !b_last){ if(b_prev && now-b_prev<300u){ plat_toggle_run(); b_prev=0; } else b_prev=now; }
  b_last = b & BTN_B;
  if(use_left){ use_left--; k|=KEY_USE; }
  if(turn_left){ turn_left--; k |= turn_dir>0 ? KEY_RIGHT : KEY_LEFT; }
  if(chg_left){ chg_left--; k |= chg_dir ? KEY_CHGW : (KEY_CHGW|KEY_USE); }
  *keys=k;
}
#endif

/* ---- Flash for the precomputed data (addresses in the uncached window 0x6...) ---- */
PRAMF static void fl_erase(u32 off){ __asm__ volatile("cpsid i"); flash_erase4k(off); __asm__ volatile("cpsie i"); }
PRAMF static void fl_prog(u32 off, const void *p, u32 n){ __asm__ volatile("cpsid i"); flash_prog(off, p, n); __asm__ volatile("cpsie i"); }
/* Check and if needed rewrite the whole sector (the method the emulator uses on the device) */
#define g_sect ((u32*)displayData.displayFrameBuffer[1])   /* v0.1.4: fallback buffer in the second framebuffer */
PRAMF static void fl_rmw(u32 off, u32 word){
  u32 base=off & 0x00FFF000u;
  for(u32 i=0;i<1024;i++) g_sect[i]=*(volatile u32*)(0x60000000u+base+4u*i);
  g_sect[(off & 0xFFFu)>>2] &= word;
  __asm__ volatile("cpsid i"); flash_prog4k(base, g_sect); __asm__ volatile("cpsie i");
}
u32 plat_flash_fixes; extern u32 g_in_fault;
void plat_flash_erase4k(uint32_t a){
  u32 off=a & 0x00FFF000u; g_last_frame=g_ms; fl_erase(off);
  for(u32 i=0;i<1024;i++) if(*(volatile u32*)(0x60000000u+off+4u*i)!=0xFFFFFFFFu){
    for(u32 k=0;k<1024;k++) g_sect[k]=0xFFFFFFFFu;
    __asm__ volatile("cpsid i"); flash_prog4k(off, g_sect); __asm__ volatile("cpsie i"); plat_flash_fixes++; break; }
}
void plat_flash_word(uint32_t a, uint32_t w){
  static u32 buf; u32 off=a & 0x00FFFFFCu;
  g_last_frame=g_ms;   /* v0.2.4: writing flash (level start) is not a hang, no beep */
  if((++g_fl_words & 1023u)==0) plat_stage(g_stage);
  u32 want=*(volatile u32*)(0x60000000u+off) & w;
  buf=w; fl_prog(off, &buf, 4u);
  if(*(volatile u32*)(0x60000000u+off)!=want && !g_in_fault){ fl_rmw(off, w); plat_flash_fixes++; }
}
uint32_t qspiFlashGetSize(void){ return 0x1000000u; }
void qspiFlashErasePage4k(uint32_t a){ fl_erase(a & 0x00FFF000u); }
void qspiFlashProgram(uint32_t a, const void *b, uint32_t s){
  const u8 *p=(const u8*)b; a&=0x00FFFFFFu;
  while(s){ u32 n=256u-(a & 255u); if(n>s) n=s; fl_prog(a, p, n); a+=n; p+=n; s-=n; } }
void _putchar(char c){ (void)c; }

/* ---- Diagnostics display (v0.1.1): lines of hex digits, runs entirely from PRAM ---- */
__attribute__((section(".pram_rodata"))) static const u16 g_font[16]={0x7B6F,0x2C97,0x73E7,0x73CF,0x5BC9,0x79CF,0x79EF,0x7249,0x7BEF,0x7BCF,0x7BED,0x6BAE,0x7927,0x6B6E,0x79E7,0x79E4};
static u16 g_drow[128];
#ifdef CLEAN
#define BG 0x0000u   /* v0.2.6: release build, black instead of blue */
#else
#define BG 0x0010u
#endif
PRAMF void plat_line(u32 row, const u32 *v, const u8 *nd, u32 cnt){
#ifdef CLEAN
  (void)row; (void)v; (void)nd; (void)cnt; return;   /* v0.2.6: no more number lines */
#endif
  for(u32 y=0;y<10;y++){
    u32 ry = y<8u ? (y*5u)/8u : 9u;
    for(u32 x=0;x<128;x++) g_drow[x]=0x0010u;
    u32 cx=1;
    for(u32 k=0;k<cnt;k++){
      for(int d=(int)nd[k]-1; d>=0; d--){
        u32 bits=g_font[(v[k]>>(4u*(u32)d))&15u];
        if(cx+6<128 && ry<5) for(u32 c=0;c<3;c++) if((bits>>(14-(ry*3+c)))&1u){ g_drow[cx+2*c]=0xFFFFu; g_drow[cx+2*c+1]=0xFFFFu; }
        cx+=8;
      }
      cx+=4;
    }
    hw_px_begin(0,row*10u+y,128,row*10u+y+1); hw_px_row(g_drow,128); hw_px_end();
  }
}
extern u32 plat_flash_fixes;
PRAMF void plat_stage(unsigned int st){
  g_stage=st;
#ifdef CLEAN
  /* v0.2.6: a loading bar at the bottom instead of numbers, until the first frame (first start writes about 32000 words) */
  if(!g_frames){ u32 w=(g_fl_words*128u)>>15; if(w>128u) w=128u;
    for(u32 x=0;x<128;x++) g_drow[x]= x<w ? 0xA514u : 0x2104u;
    for(u32 y=124;y<126;y++){ hw_px_begin(0,y,128,y+1); hw_px_row(g_drow,128); hw_px_end(); } }
  return;
#endif
  u32 v[3]={st, g_fl_words, plat_flash_fixes}; static const u8 n[3]={2,6,4};
  plat_line(0, v, n, 3);
}

/* Hang display: H <pc> / <lr> / frames, stage */
static u32 g_hang_logged;
PRAMF void plat_hang(u32 *fr){
  if(g_hang_logged<6u){ g_hang_logged++; plat_log(5, g_frames); plat_log_raw(fr[6]); plat_log_raw(fr[5]); }
  { u32 v[2]={0x8, fr[6]}; static const u8 n[2]={1,8}; plat_line(8, v, n, 2); }
  { u32 v[2]={0xA, fr[5]}; static const u8 n[2]={1,8}; plat_line(9, v, n, 2); }
  { u32 v[5]={0xB, g_frames & 0xFFFu, g_stage, g_in_push, (u32)fr}; static const u8 n[5]={1,3,2,1,8}; plat_line(10, v, n, 5); }
}

/* ---- v0.1.8: event log in flash (0x108000 to 0x10DFFF, empty in the ROM) ----
 * Word = <type 4 bit><value 12 bit><ms 16 bit>. Type 1 checkpoint (source line), 2 PLOG number, 3 I_Error (line),
 * 4 fault (next word pc), 5 hang (next word pc), F restart. On the next start with A+C the
 * latest entries are shown (until a key is pressed). */
#define LOG_BASE 0x60108000u
#define LOG_N    6144u
static u32 g_log_i;
void plat_log_raw(u32 w){
#ifdef CLEAN
  (void)w; return;   /* v0.2.6: no event log */
#endif
  if(g_log_i>=LOG_N) return;
  plat_flash_word(LOG_BASE + 4u*g_log_i, w); g_log_i++;
}
void plat_log(u32 type, u32 val){ plat_log_raw((type<<28) | ((val & 0xFFFu)<<16) | (g_ms & 0xFFFFu)); }
void plat_plog(int n){ plat_log(2, (u32)n); }
static void log_show_previous(void){
#ifdef CLEAN
  return;
#endif
  u32 n=0; while(n<LOG_N && *(volatile u32*)(LOG_BASE+4u*n)!=0xFFFFFFFFu) n++;
  g_log_i=n;
  if(!n) return;
  /* v0.1.9: pages of 11 entries, A pages back (older), B or C starts DOOM */
  u32 page=0;
  while(hw_buttons() & (BTN_A|BTN_C)){ REG(0x4000800Cu)=0x5AFA55AAu; REG(0x4000900Cu)=0x5AFA55AAu; }
  for(;;){
    for(u32 y=0;y<128;y++){ for(u32 x=0;x<128;x++) g_drow[x]=BG; hw_px_begin(0,y,128,y+1); hw_px_row(g_drow,128); hw_px_end(); }
    u32 end = n > 11u*page ? n-11u*page : 0u;
    u32 first = end>11u ? end-11u : 0u;
    { u32 v[3]={0xC, n, page}; static const u8 nn[3]={1,4,2}; plat_line(0, v, nn, 3); }
    for(u32 k=first;k<end;k++){ u32 v[1]={*(volatile u32*)(LOG_BASE+4u*k)}; static const u8 nn[1]={8}; plat_line(1u+(k-first), v, nn, 1); }
    u32 b;
    for(;;){ REG(0x4000800Cu)=0x5AFA55AAu; REG(0x4000900Cu)=0x5AFA55AAu; b=hw_buttons(); if(b & (BTN_A|BTN_B|BTN_C)) break; }
    while(hw_buttons()){ REG(0x4000800Cu)=0x5AFA55AAu; REG(0x4000900Cu)=0x5AFA55AAu; }
    if((b & BTN_A) && first>0u) { page++; continue; }
    if(b & BTN_A) { page=0; continue; }
    break;
  }
}

/* Checkpoints from the core (lprintf) and errors (I_Error): line 1 <counter> <line> <file id> <ms interrupt> <ms polled>.
 * The file id is the sum of the characters of the file name (16 bit). */
static volatile u32 g_marks;
PRAMF static u32 fhash(const char *f){ u32 h=0; while(*f) h+=(u8)*f++; return h & 0xFFFFu; }
PRAMF void plat_mark(int line, const char *file){
  REG(0x4000800Cu)=0x5AFA55AAu; REG(0x4000900Cu)=0x5AFA55AAu;
  g_marks++;
  plat_log(1, (u32)line);
  u32 v[5]={g_marks & 0xFFu, (u32)line & 0xFFFu, fhash(file), g_ms & 0xFFFFu, g_poll_ms & 0xFFFFu}; static const u8 n[5]={2,3,4,4,4};
  plat_line(2, v, n, 5);
}
PRAMF void plat_error(int line, const char *file){
  plat_log(3, (u32)line);
  u32 v[3]={0xE, (u32)line & 0xFFFu, fhash(file)}; static const u8 n[3]={1,3,4};
  plat_line(4, v, n, 3);   /* only show it and keep running (the core also reports harmless things, e.g. missing WIMINUS) */
}


/* v0.2.1: alias test of RAM 0x20000000..0x2000FFFF in 1 KB blocks. Each block gets a marker at +0x200,
 * then we read back which marker is there. Line 1/2: for each 4 KB block the number (4 KB) of the marker read,
 * line 3: number of wrong 1 KB blocks, first wrong block, the marker read there, check word. */
PRAMF static void ram20_alias_test(void){
  static u32 sv[64]; u32 rb[64];
  __asm__ volatile("cpsid i");
  for(u32 i=0;i<64u;i++) sv[i]=*(volatile u32*)(0x20000200u+0x400u*i);
  for(u32 i=0;i<64u;i++){ *(volatile u32*)(0x20000200u+0x400u*i)=0xC0DE0000u|i; __asm__ volatile("dsb"); }
  for(u32 i=0;i<64u;i++) rb[i]=*(volatile u32*)(0x20000200u+0x400u*i);
  for(int i=63;i>=0;i--) *(volatile u32*)(0x20000200u+0x400u*(u32)i)=sv[i];
  __asm__ volatile("dsb\n cpsie i");
  u32 bad=0, fb=0, fv=0;
  for(u32 i=0;i<64u;i++) if(rb[i]!=(0xC0DE0000u|i)){ if(!bad){ fb=i; fv=rb[i]; } bad++; }
  u32 a[8], c[8];
  for(u32 i=0;i<8u;i++){ u32 r=rb[4u*i]; a[i]=((r>>16)==0xC0DEu) ? ((r & 0x3Fu)>>2) : 0xEu; }
  for(u32 i=0;i<8u;i++){ u32 r=rb[32u+4u*i]; c[i]=((r>>16)==0xC0DEu) ? ((r & 0x3Fu)>>2) : 0xEu; }
  static const u8 n8[8]={1,1,1,1,1,1,1,1};
  plat_line(5, a, n8, 8); plat_line(6, c, n8, 8);
  { u32 v[4]={bad, fb, fv & 0xFFu, fv>>16}; static const u8 n[4]={2,2,2,4}; plat_line(7, v, n, 4); }
  { u32 v[1]={0xB}; static const u8 n[1]={1}; plat_line(8, v, n, 1); }
  for(u32 t=0;t<0x4000000u;t++){ REG(0x4000800Cu)=0x5AFA55AAu; REG(0x4000900Cu)=0x5AFA55AAu; if(hw_buttons() & BTN_B) break; }
  for(u32 t=0;t<0x800000u && (hw_buttons() & BTN_B);t++){ REG(0x4000800Cu)=0x5AFA55AAu; REG(0x4000900Cu)=0x5AFA55AAu; }
}
/* ---- Start ---- */
extern u32 __data_load, __data_start, __data_end, __bss_start, __bss_end;
extern u32 __r20_load, __r20_start, __r20_end, __r20b_start, __r20b_end;
extern void D_DoomMain(void); extern void Z_Init(void); extern void InitGlobals(void);
__attribute__((section(".pram_text"), noinline, used)) void Reset_Handler(void){
  __asm__ volatile("cpsid i");
  REG(0xE000ED14u) &= ~(1u<<3);   /* v0.2.3: UNALIGN_TRP off (plain LDR/STR may be unaligned) */
#ifdef UA_FIX
  REG(0xE000ED24u) |= (1u<<18);    /* UsageFault on its own (vector 6) */
#endif
  hw_init();
  for(u32 y=0;y<128;y++){ for(u32 x=0;x<128;x++) g_drow[x]=BG; hw_px_begin(0,y,128,y+1); hw_px_row(g_drow,128); hw_px_end(); }
  log_show_previous();
#ifdef ALIAS_TEST
  ram20_alias_test();
#endif
  for(u32 y=0;y<128;y++){ for(u32 x=0;x<128;x++) g_drow[x]=BG; hw_px_begin(0,y,128,y+1); hw_px_row(g_drow,128); hw_px_end(); }
  plat_stage(1);
  plat_log(0xF, 0);
  /* v0.1.2: the window stays at the firmware base (0x60011000), our program part sits in it at 0x100A0000
   * (flash 0x0B1000). No switching, so no stale lines in the cache. */
  XIP_BASE_REG = 0x60011000u; XIP_CTRL_REG = 3u;
  __asm__ volatile("dsb\n isb");
  u32 bad0=0; for(u32 i=0;i<0x4000u;i++) if(*(volatile u32*)(0x100A0000u+4u*i)!=*(volatile u32*)(0x600B1000u+4u*i)) bad0++;
#ifdef XIP_FLUSH
  /* The window cache keeps stale lines even after the base is changed: reading 64 KB evicts everything */
  { volatile u32 sum=0; for(u32 i=0;i<0x4000u;i++) sum+=*(volatile u32*)(0x10040000u+4u*i); (void)sum; }
#endif
  { u32 bad=0; for(u32 i=0;i<0x4000u;i++) if(*(volatile u32*)(0x100A0000u+4u*i)!=*(volatile u32*)(0x600B1000u+4u*i)) bad++;
    u32 v[3]={2, bad0, bad}; static const u8 n[3]={2,5,5}; plat_line(1, v, n, 3); }
  { const u32 *s=&__data_load; for(u32 *p=&__data_start;p<&__data_end;p++) *p=*s++; }
  for(u32 *p=&__bss_start;p<&__bss_end;p++) *p=0;
  { const u32 *s=&__r20_load; for(u32 *p=&__r20_start;p<&__r20_end;p++) *p=*s++; }
  for(u32 *p=&__r20b_start;p<&__r20b_end;p++) *p=0;
#ifdef SINGLE_FB
  displayData.displayFrameBuffer[0]=g_fb; displayData.displayFrameBuffer[1]=g_fb;
#endif
  __asm__ volatile("dsb\n isb");
  plat_stage(3);
  for(u32 i=0;i<4;i++){ REG(0xE000E180u+4u*i)=0xFFFFFFFFu; REG(0xE000E280u+4u*i)=0xFFFFFFFFu; }   /* all IRQs off, nothing pending */
  REG(GPIO2+0x04) &= ~3u; g_enc_last=REG(GPIO2) & 3u;
  SYST_RVR=96000u-1u; SYST_CVR=0; SYST_CSR=7;
  __asm__ volatile("cpsie i");
  /* wait until the start keys (A+C) are released */
  for(u32 t=0; (hw_buttons() & (BTN_A|BTN_C)) && t<3000u; t++) delay(1);
  plat_stage(4); Z_Init(); plat_stage(5); InitGlobals(); plat_stage(6); D_DoomMain();
  plat_leave();
}

/* v0.1.9: separate stack for fault handling, in case the broken stack itself is the cause */
__attribute__((section(".bss"), aligned(8))) u32 g_fault_stack[256];
__attribute__((naked, section(".pram_text"))) static void Fault_Handler(void){
  __asm__ volatile("mrs r0, msp\n mov r1, lr\n ldr r2, =g_fault_stack+1024\n mov sp, r2\n b plat_fault\n .ltorg\n"); }
static int fr_ok(u32 *fr){ u32 a=(u32)fr;
  return ((a>=0x18000000u && a<=0x18020000u-32u) || (a>=0x20001000u && a<=0x20010000u-32u)) && !(a&3u); }
u32 g_in_fault;
/* Fault screen: F <type> <pc>, <lr>, <CFSR> <HFSR>, <address>, stage. Wheel 2 s: return to normal mode and from there back to the Paradise. */

#ifdef UA_FIX
/* v0.2.3: emulate unaligned LDRD/STRD/LDM/STM (always a fault on Cortex-M3) in the UsageFault and keep running.
 * The first 8 locations go into the event log (type 6, counter, then pc) so they can be fixed in the source. */
static u32 g_ua_n, g_ua_pc[8];
static int ua_rd(u32 *fr, u32 *r48, u32 n, u32 *v){
  if(n<4u){ *v=fr[n]; return 1; } if(n<12u){ *v=r48[n-4u]; return 1; } if(n==12u){ *v=fr[4]; return 1; } if(n==14u){ *v=fr[5]; return 1; } return 0; }
static int ua_wr(u32 *fr, u32 *r48, u32 n, u32 v){
  if(n<4u){ fr[n]=v; return 1; } if(n<12u){ r48[n-4u]=v; return 1; } if(n==12u){ fr[4]=v; return 1; } if(n==14u){ fr[5]=v; return 1; } return 0; }
static u32 ua_ld(u32 a){ const volatile u8 *p=(const volatile u8*)a; return (u32)p[0] | ((u32)p[1]<<8) | ((u32)p[2]<<16) | ((u32)p[3]<<24); }
static void ua_st(u32 a, u32 v){ volatile u8 *p=(volatile u8*)a; p[0]=(u8)v; p[1]=(u8)(v>>8); p[2]=(u8)(v>>16); p[3]=(u8)(v>>24); }
__attribute__((used, section(".pram_text"))) u32 ua_fix(u32 *fr, u32 exc, u32 *r48){
  (void)exc;
  if(!(REG(0xE000ED28u) & (1u<<24))) return 0;
  if(fr[7] & 0x0600FC00u) return 0;              /* inside an IT block: do not emulate */
  u32 pc=fr[6]; u32 h1=*(const volatile u16*)pc, h2=*(const volatile u16*)(pc+2u), len=4;
  u32 rn, base;
  if((h1 & 0xFE40u)==0xE840u){                    /* LDRD/STRD (immediate) */
    rn=h1 & 15u; u32 P=(h1>>8)&1u, U=(h1>>7)&1u, W=(h1>>5)&1u, L=(h1>>4)&1u, rt=h2>>12, rt2=(h2>>8)&15u, imm=(h2 & 0xFFu)<<2;
    if(!ua_rd(fr,r48,rn,&base)) return 0;
    u32 off = U ? base+imm : base-imm, a = P ? off : base, v1, v2;
    if(L){ v1=ua_ld(a); v2=ua_ld(a+4u); if(!ua_wr(fr,r48,rt,v1) || !ua_wr(fr,r48,rt2,v2)) return 0; }
    else { if(!ua_rd(fr,r48,rt,&v1) || !ua_rd(fr,r48,rt2,&v2)) return 0; ua_st(a,v1); ua_st(a+4u,v2); }
    if(W && !ua_wr(fr,r48,rn,off)) return 0;
  } else if((h1 & 0xFF80u)==0xE880u || (h1 & 0xFF80u)==0xE900u){   /* LDM/STM IA or DB */
    rn=h1 & 15u; u32 W=(h1>>5)&1u, L=(h1>>4)&1u, db=(h1 & 0xFF80u)==0xE900u, list=h2 & 0xDFFFu, cnt=0;
    if(!ua_rd(fr,r48,rn,&base)) return 0;
    for(u32 i=0;i<16u;i++) if(list & (1u<<i)) cnt++;
    if(list & 0x8000u) return 0;
    u32 a = db ? base-4u*cnt : base;
    for(u32 i=0;i<15u;i++) if(list & (1u<<i)){
      if(L){ if(!ua_wr(fr,r48,i,ua_ld(a))) return 0; } else { u32 v; if(!ua_rd(fr,r48,i,&v)) return 0; ua_st(a,v); }
      a+=4u; }
    if(W && !(L && (list & (1u<<rn)))) { if(!ua_wr(fr,r48,rn, db ? base-4u*cnt : base+4u*cnt)) return 0; }
  } else if((h1 & 0xF000u)==0xC000u){              /* 16 bit LDMIA/STMIA */
    len=2; rn=(h1>>8)&7u; u32 L=(h1>>11)&1u, list=h1 & 0xFFu, cnt=0;
    if(!ua_rd(fr,r48,rn,&base)) return 0;
    u32 a=base;
    for(u32 i=0;i<8u;i++) if(list & (1u<<i)){
      if(L){ if(!ua_wr(fr,r48,i,ua_ld(a))) return 0; } else { u32 v; if(!ua_rd(fr,r48,i,&v)) return 0; ua_st(a,v); }
      a+=4u; cnt++; }
    if(!(L && (list & (1u<<rn)))) ua_wr(fr,r48,rn,base+4u*cnt);
  } else return 0;
  fr[6]=pc+len;
  REG(0xE000ED28u)=(1u<<24);
  { u32 k; for(k=0;k<g_ua_n && k<8u;k++) if(g_ua_pc[k]==pc) break;
    if(k==g_ua_n && k<8u){ g_ua_pc[k]=pc; g_ua_n++; plat_log(6, g_ua_n); plat_log_raw(pc); } }
  return 1;
}
__attribute__((naked, section(".pram_text"))) static void Usage_Handler(void){
  __asm__ volatile(
    "mrs r0, msp\n mov r1, lr\n push {r1, r4-r11}\n add r2, sp, #4\n bl ua_fix\n cmp r0, #0\n pop {r1, r4-r11}\n"
    "beq 1f\n bx r1\n"
    "1: mrs r0, msp\n ldr r2, =g_fault_stack+1024\n mov sp, r2\n b plat_fault\n .ltorg\n");
}
#endif
__attribute__((used, section(".pram_text"))) void plat_fault(u32 *fr, u32 exc){
  hw_tone(0);
  g_in_fault=1;
  u32 ipsr; __asm__ volatile("mrs %0, ipsr" : "=r"(ipsr));
  /* first everything that works without reading the stack: type 4 with exception number, then msp, CFSR, HFSR, BFAR, MMFAR, exc_return */
  plat_log(4, ipsr & 0x1FFu); plat_log_raw((u32)fr); plat_log_raw(REG(0xE000ED28u)); plat_log_raw(REG(0xE000ED2Cu));
  plat_log_raw(REG(0xE000ED38u)); plat_log_raw(REG(0xE000ED34u)); plat_log_raw(exc);
  int frgood=fr_ok(fr);
  if(!frgood){ static u32 dummy[8]; fr=dummy; }
  plat_log_raw(fr[6]); plat_log_raw(fr[5]);
  u32 cfsr=REG(0xE000ED28u), hfsr=REG(0xE000ED2Cu);
  u32 adr = (cfsr & 0x80u) ? REG(0xE000ED34u) : REG(0xE000ED38u);
  { u32 v[3]={0xF, ipsr & 0xFFu, fr[6]}; static const u8 n[3]={1,2,8}; plat_line(3, v, n, 3); }
  { u32 v[2]={0xA, fr[5]}; static const u8 n[2]={1,8}; plat_line(4, v, n, 2); }
  { u32 v[3]={0xC, cfsr, hfsr >> 16}; static const u8 n[3]={1,8,4}; plat_line(5, v, n, 3); }
  { u32 v[2]={0xD, adr}; static const u8 n[2]={1,8}; plat_line(6, v, n, 2); }
  { u32 v[3]={0xE, g_stage, g_fl_words}; static const u8 n[3]={1,2,6}; plat_line(7, v, n, 3); }
  for(u32 t=0;;t++){ hw_wdt_feed(); if(hw_buttons() & BTN_WHEEL){ if(t>2000000u) break; } else t=0; }
  if(!frgood) plat_leave();
  /* back to thread mode, straight into plat_leave */
  fr[6]=((u32)plat_leave) & ~1u; fr[7]=0x01000000u;
  REG(0xE000ED28u)=cfsr; REG(0xE000ED2Cu)=hfsr;
  __asm__ volatile("mov lr, %0\n bx lr\n" :: "r"(exc));
}
/* v0.1.5: unexpected interrupt (e.g. end of flash DMA, IRQ 58): disable only this one in the NVIC.
 * Before, this was "cpsid i": after that the timer (SysTick) stopped too, and DOOM waited forever for the next tick. */
__attribute__((used, section(".pram_text"))) void irq_off_c(void){
  u32 ipsr; __asm__ volatile("mrs %0, ipsr" : "=r"(ipsr));
  u32 n=(ipsr & 0x1FFu);
  if(n>=16u){ n-=16u; REG(0xE000E180u + 4u*(n>>5)) = 1u<<(n&31u); REG(0xE000E280u + 4u*(n>>5)) = 1u<<(n&31u); }
}
__attribute__((naked, section(".pram_text"))) static void Irq_Off(void){ __asm__ volatile("b irq_off_c\n"); }
__attribute__((naked, section(".pram_text"))) static void Exc_Off(void){ __asm__ volatile("bx lr\n"); }
__attribute__((section(".vectors"), used))
const void* const g_vectors[16+64] = { (void*)0x18020000u, (void*)Reset_Handler,
  [2 ... 14] = (void*)Fault_Handler, [11] = (void*)Exc_Off, [14] = (void*)Exc_Off,
#ifdef UA_FIX
  [6] = (void*)Usage_Handler,
#endif
  [15] = (void*)SysTick_Handler, [16 ... 79] = (void*)Irq_Off };

/* ---- Reading from flash: aligned words (single uncached bytes are about 8x slower) ---- */
void plat_fcopy(void *dst, const void *src, unsigned int n){
  u32 s=(u32)src; u8 *d=(u8*)dst;
  if(s - 0x60000000u >= 0x01000000u){ memcpy(dst, src, n); return; }
  if(!n) return;
  const volatile u32 *w=(const volatile u32*)(s & ~3u); u32 sh=s & 3u;
  u32 v=*w++ >> (8u*sh);
  u32 k=4u-sh;
  while(n && k){ *d++=(u8)v; v>>=8; n--; k--; }
  if(!(((u32)d) & 3u)){ u32 *dw=(u32*)d; while(n>=4u){ *dw++=*w++; n-=4u; } d=(u8*)dw; }
  else while(n>=4u){ u32 x=*w++; d[0]=(u8)x; d[1]=(u8)(x>>8); d[2]=(u8)(x>>16); d[3]=(u8)(x>>24); d+=4; n-=4u; }
  if(n){ u32 x=*w; while(n--){ *d++=(u8)x; x>>=8; } }
}
/* Copy into PRAM: only from code outside PRAM and with interrupts off (otherwise instruction fetch gets corrupted) */
void plat_pram_copy(void *d, const void *s, unsigned n){
  __asm__ volatile("cpsid i");
  plat_fcopy(d, s, n);
  __asm__ volatile("dsb\n isb\n cpsie i");
}
/* Flats: 4 slots of 4 KB in PRAM, the least recently used one is replaced */
#define NFLAT 2   /* v0.1.4: in RAM at 0x2000xxxx instead of PRAM (writing to PRAM is unreliable on the device) */
__attribute__((section(".ram20_bss"), aligned(4))) static uint8_t g_flat[NFLAT][4096];
static int g_flat_lump[NFLAT] = { -1, -1 };
static u32 g_flat_use[NFLAT], g_flat_clock;
const uint8_t *plat_flat(int lump, const uint8_t *src){
  g_flat_clock++;
  for(int i=0;i<NFLAT;i++) if(g_flat_lump[i]==lump){ g_flat_use[i]=g_flat_clock; return g_flat[i]; }
  int o=0; for(int i=1;i<NFLAT;i++) if(g_flat_use[i]<g_flat_use[o]) o=i;
  plat_pram_copy(g_flat[o], src, 4096);
  g_flat_lump[o]=lump; g_flat_use[o]=g_flat_clock;
  return g_flat[o];
}
