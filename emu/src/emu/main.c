/* Paramix emulator app for the Tamagotchi Paradise: runs a colour
 * Tamagotchi core (tamaemu, GPL-3.0) directly on the device.
 * Long press on the wheel: save state and return to the Paradise. */
#include "hw.h"
#include "../core/tc.h"
#include "state.h"
uint32_t jit_used(void);

/* Settings, filled in by the ROM builder (magic "PMXC"). */
typedef struct { u32 magic, version, guest_off, guest_size, save_off, device; u8 map[128]; u32 spare_end; } PmxCfg;
/* version 3 (v0.9.2): spare_end = end of the guest area; the room behind the packed firmware holds spare pages */
__attribute__((section(".pmxcfg"), used))
volatile const PmxCfg g_cfg = { 0x43584D50u, 1, 0x940000u, 0x400000u, 0xC0000u, 0,
  { [0 ... 63] = 0xFE, [64 ... 127] = 0xFF }, 0 };   /* 0xFE: linear (old layout) */

static uint16_t g_gram[GRAM_H][GRAM_W];
static Emu g_emu;
#define NSLOT 2
static uint8_t g_pool[NSLOT * OVL_SIZE];
#ifdef OVL_RAM20
/* v0.9.1: extra slots for the guest's modified flash sectors in RAM at 0x20004000 (64 KB on the device,
 * the first 16 KB stay free). With 8 slots instead of 2, eviction mid-game is almost never needed. */
#define R20_SLOTS 6
#define R20_POOL 0x20004000u
static void ovl_extra(Emu *e){ for(int i=0;i<R20_SLOTS && NSLOT+i<OVL_SLOTS;i++) e->ovl.mem[NSLOT+i]=(uint8_t*)(R20_POOL+(u32)i*OVL_SIZE); }
#endif
static int g_bl=1;                        /* backlight on (guest display visible) */
extern uint8_t __jit_start[], __jit_end[], __initcode_start[], __pdata_start[];
static u16 g_row[128];
static StCfg g_sc;
static u32 g_restored;
static u32 g_still;                       /* ms without a change to the guest frame */
__attribute__((section(".stats"), used)) static volatile u32 g_st[16];   /* measurements, located at 0x18000000 */

/* ---- Time: SysTick as millisecond counter ---- */
static volatile u32 g_ms;
#define SYST_CSR REG(0xE000E010u)
#define SYST_RVR REG(0xE000E014u)
#define SYST_CVR REG(0xE000E018u)
typedef int32_t s32;
#define CORE_HZ 96000000u
static void systick_init(void){ SYST_RVR = CORE_HZ/1000u - 1u; SYST_CVR = 0; SYST_CSR = 7; }

/* XIP window: 1 MB cached from 0x10000000, base freely selectable. */
#define XIP_BASE_REG REG(0x4002F004u)
#define XIP_CTRL_REG REG(0x4002F000u)
#define MPU_TYPE REG(0xE000ED90u)
#define MPU_CTRL REG(0xE000ED94u)
#define MPU_RNR  REG(0xE000ED98u)
#define MPU_RBAR REG(0xE000ED9Cu)
#define MPU_RASR REG(0xE000EDA0u)
#define SHCSR    REG(0xE000ED24u)

/* Check PRAM against the app image in flash (uncached window, confirmed on
 * the device) and replace any differences. On the device, v0.8.3 hit an
 * "undefined instruction" fault at a valid address (0x41A6): suspected that
 * PRAM did not match the image. */
#define APP_FLASH 0x600A0000u
static volatile u32 g_zero;
static u32 g_pc_n, g_pc_first, g_pc_last, g_pc_rt;
static u32 pram_check(u32 end, int fix){
  volatile u32 *dst=(volatile u32*)g_zero;
  volatile const u32 *src=(volatile const u32*)APP_FLASH;
  if(src[0]!=dst[0] || src[1]!=dst[1]) return 0xFFFFFFFFu;   /* window does not read the image */
  u32 n=0;
  for(u32 i=0;i<end/4u;i++){
    u32 v=src[i];
    if(dst[i]!=v){ if(!g_pc_n && !n) g_pc_first=i*4u; g_pc_last=i*4u; n++; if(fix) dst[i]=v; }
  }
  if(n && fix){ __asm__ volatile("dsb\n isb" ::: "memory"); }
  return n;
}
/* Startup display: until the guest has sent a frame, two rows of numbers (hex)
 * are shown at the bottom. Shows on the device how far startup gets. */
#if !defined(CLEAN) || defined(MESS)
static const u16 g_font[16]={0x7B6F,0x2C97,0x73E7,0x73CF,0x5BC9,0x79CF,0x79EF,0x7249,0x7BEF,0x7BCF,0x7BED,0x6BAE,0x7927,0x6B6E,0x79E7,0x79E4};
static void diag_line(u32 y0, const u32 *v, const u8 *n, int cnt){
  for(u32 y=0;y<10;y++){
    for(u32 x=0;x<128;x++) g_row[x]=0x0010u;
    u32 cx=2;
    for(int k=0;k<cnt;k++){
      for(int d=n[k]-1; d>=0; d--){
        u32 h=(v[k]>>(4*d))&15u, bits=g_font[h], ry=y/2;
        for(u32 c=0;c<3;c++) if(ry<5 && ((bits>>(14-(ry*3+c)))&1u)){ g_row[cx+2*c]=0xFFFFu; g_row[cx+2*c+1]=0xFFFFu; }
        cx+=8;
      }
      cx+=6;
    }
    hw_push(g_row, 0, 0, y0+y, 128, y0+y+1);
  }
}
#endif
#ifndef CLEAN
static volatile u32 g_stage, g_guest_drawn, g_loops;
static void diag(Emu *e, u32 emu_ms){
  if(g_guest_drawn) return;
  u32 a[3]={g_stage, g_ms, emu_ms}; static const u8 na[3]={1,5,5};
  u32 b[3]={e->pc, (u32)(e->cycles>>20), g_loops}; static const u8 nb[3]={7,4,3};
  diag_line(104, a, na, 3); diag_line(116, b, nb, 3);
  { u32 c[4]={g_pc_n, g_pc_first, g_pc_last, g_pc_rt}; static const u8 nc[4]={3,4,4,3}; diag_line(32, c, nc, 4); }
}
/* Fault screen: on a CPU fault (F) or when the main loop stalls for 3 s (E),
 * four rows: kind and pc of the location, lr, CFSR and HFSR, BFAR and guest pc. */
static u32 g_loops_seen, g_loops_ms;
static u32 g_r9;
static volatile u32 g_probe, g_probe_hit;   /* memory probe with caught bus fault */
static void fault_show(u32 kind, const u32 *fr){
  { u32 q[3]={9, g_r9, g_r9==g_emu.jit_a0}; static const u8 nq[3]={1,8,1}; diag_line(32, q, nq, 3); }
  u32 a[2]={kind, fr[6]}; static const u8 na[2]={1,8};
  u32 b[1]={fr[5]}; static const u8 nb[1]={8};
  u32 c[2]={REG(0xE000ED28u), REG(0xE000ED2Cu)}; static const u8 nc[2]={8,4};
  u32 d[2]={ (REG(0xE000ED28u) & 0x80u) ? REG(0xE000ED34u) : REG(0xE000ED38u), g_emu.pc}; static const u8 nd[2]={8,7};
  diag_line(68, a, na, 2); diag_line(80, b, nb, 1); diag_line(92, c, nc, 2); diag_line(104, d, nd, 2);
  { u32 pc=fr[6] & ~3u; u32 x[2]={0,0}; static const u8 nx[2]={8,8};
    if(pc < 0x10000u){ x[0]=*(volatile const u32*)(g_zero+pc); x[1]=*(volatile const u32*)(APP_FLASH+pc); }
    diag_line(20, x, nx, 2);
    /* y=8 stays free for the 5 row (memory test) */ }
}
/* The JIT entry guard has triggered (display since v0.8.10):
 * y=68 A <kind 1=sp, 2=r10/r11> <code> <return value>, y=80 <sp before> <sp difference> <mask of r4..r11 changed>,
 * y=92 last three codes, y=104 guest pc now and at entry,
 * y=20/32/44/56: r4 r5 / r6 r7 / r8 r9 / r10 r11 AFTER the block (lower 6 digits),
 * y=116: r10 r11 BEFORE (y=8 stays the write test row).
 */
static void leave(void);
static void jit_bad(Emu *e){
  g_stage=8;                                         /* E screen off, otherwise it covers this display */
  volatile u32 *d=jit_dbg; u32 k=(d[5]-1u)&7u, mask=0;
  for(u32 i=0;i<8;i++) if(d[28+i]!=d[36+i]) mask |= 1u<<i;   /* Bit 0 = r4 ... Bit 7 = r11 */
  u32 a[4]={0xA, d[4], d[2]&0xFFFFu, d[3]&0xFFFFFu}; static const u8 na[4]={1,1,4,5};
  u32 b[3]={d[0], (d[1]-d[0])&0xFFFFu, mask}; static const u8 nb[3]={8,4,2};
  u32 c[3]={d[9+2*((k-2)&7u)]&0xFFFFu, d[9+2*((k-1)&7u)]&0xFFFFu, d[9+2*k]&0xFFFFu}; static const u8 nc[3]={4,4,4};
  u32 f[2]={e->pc, d[8+2*k]}; static const u8 nf[2]={7,7};
  static const u8 n6[2]={6,6};
  u32 r45[2]={d[36]&0xFFFFFFu, d[37]&0xFFFFFFu}, r67[2]={d[38]&0xFFFFFFu, d[39]&0xFFFFFFu};
  u32 r89[2]={d[40]&0xFFFFFFu, d[41]&0xFFFFFFu}, rab[2]={d[42]&0xFFFFFFu, d[43]&0xFFFFFFu};
  diag_line(68, a, na, 4); diag_line(80, b, nb, 3); diag_line(92, c, nc, 3); diag_line(104, f, nf, 2);
  diag_line(20, r45, n6, 2); diag_line(32, r67, n6, 2); diag_line(44, r89, n6, 2); diag_line(56, rab, n6, 2);
  { u32 v2[2]={d[34]&0xFFFFFFu, d[35]&0xFFFFFFu};
    diag_line(116, v2, n6, 2); }      /* before: r10 r11; y=8 stays the write test */
  for(;;){ hw_wdt_feed(); if(hw_buttons()&BTN_WHEEL) leave(); }
}
__attribute__((used)) void fault_c(u32 *fr, u32 r9){
  if(g_probe){ g_probe_hit=1; g_probe=0; fr[6]+=2u; REG(0xE000ED28u)=REG(0xE000ED28u); REG(0xE000ED2Cu)=REG(0xE000ED2Cu); return; }
  g_r9=r9;
  fault_show(0xF, fr);
  for(u32 t=0;;t++){ hw_wdt_feed(); if(hw_buttons()&BTN_WHEEL){ if(t>2000000u) leave(); } else t=0; }
}
#else
/* Release build (v0.9.0): no diagnostics. On a CPU fault, sound off and wait;
 * a long press on the wheel returns to the Paradise (without saving, the state is unreliable). */
static volatile u32 g_stage, g_guest_drawn;
#define diag(e,m) ((void)0)
static void leave(void);
static void leave_nosave(void);
__attribute__((used)) void fault_c(u32 *fr, u32 r9){
  (void)fr; (void)r9; hw_tone(0);
  for(u32 t=0;;t++){ hw_wdt_feed(); if(hw_buttons()&BTN_WHEEL){ if(t>2000000u) leave_nosave(); } else t=0; }
}
#endif
#ifdef PROF
/* v0.8.21: one sample per ms: where was the CPU (pc) at the interrupt? 256-byte buckets in PRAM. */
static u16 g_prof[256]; static u32 g_prof_other;
#endif
__attribute__((used)) void systick_c(u32 *fr, u32 r9){
  g_ms++;
#if defined(PROF) && defined(MESS) && defined(CLEAN)
  { u32 pc=fr[6]; if(pc<0x10000u){ if(g_prof[pc>>8]<0xFFFFu) g_prof[pc>>8]++; } else g_prof_other++; }
#endif
#ifndef CLEAN
#ifdef PROF
  if(g_stage==7){ u32 pc=fr[6]; if(pc<0x10000u){ if(g_prof[pc>>8]<0xFFFFu) g_prof[pc>>8]++; } else g_prof_other++; }
#endif
  if(g_stage==7 && !g_guest_drawn){
    if(g_loops!=g_loops_seen){ g_loops_seen=g_loops; g_loops_ms=g_ms; }
    else if(g_ms-g_loops_ms>3000u && (g_ms & 511u)==0){ g_r9=r9; fault_show(0xE, fr); }
  }
#else
  (void)fr; (void)r9;
#endif
}
__attribute__((naked)) void SysTick_Handler(void){ __asm__ volatile("mrs r0, msp\n mov r1, r9\n b systick_c\n"); }
__attribute__((naked)) static void Fault_Handler(void){ __asm__ volatile("mrs r0, msp\n mov r1, r9\n b fault_c\n"); }
#ifdef LCD_ROWS
extern uint8_t lcd_rall;
#define LCD_ALL() (lcd_rall=1)
#else
#define LCD_ALL() ((void)0)
#endif
static void push_dirty(Emu *e){
  Lcd *l=&e->lcd;
  if(l->dx1 < l->dx0 || l->dy1 < l->dy0){ g_still+=40; return; }
  g_still=0;
  int x0=l->dx0, y0=l->dy0, x1=l->dx1+1, y1=l->dy1+1;
  if(x1>128) x1=128; if(y1>128) y1=128;
  l->dx0=1000; l->dy0=1000; l->dx1=-1; l->dy1=-1;
  if(x0>=x1 || y0>=y1) return;
  g_guest_drawn=1;
  int on = l->sleep_out && l->disp_on;
  int bgr = l->madctl & 0x08, inv = l->inverted;
#ifdef LCD_ROWS
  /* v0.8.19: if the changes are scattered (two characters in different corners), send only the changed
   * pieces of each row instead of the whole bounding rectangle. Cost roughly estimated in bytes. */
  { extern uint8_t lcd_rx0[], lcd_rx1[], lcd_rall;
    int rows=0;
    if(!lcd_rall){
      u32 cr=(u32)(x1-x0)*(u32)(y1-y0)*2u + 30u, cs=0;
      for(int y=y0;y<y1;y++) if(lcd_rx0[y]<=lcd_rx1[y]) cs += (u32)(lcd_rx1[y]-lcd_rx0[y]+1)*2u + 30u;
      rows = cs < cr;
    }
    if(rows){
      for(int y=y0;y<y1;y++){
        if(lcd_rx0[y]>lcd_rx1[y]) continue;
        int a=lcd_rx0[y], b=lcd_rx1[y]+1; if(b>128) b=128;
        for(int x=a;x<b;x++){
          u16 p = on ? l->gram[y][x] : 0;
          if(on && inv) p=(u16)~p;
          if(bgr) p=(u16)(((p&0x1F)<<11)|(p&0x07E0)|(p>>11));
          g_row[x]=p;
        }
        hw_px_begin((u32)a,(u32)y,(u32)b,(u32)y+1u); hw_px_row(g_row + a, (u32)(b-a)); hw_px_end();
      }
    }
    if(lcd_rall){ for(int y=0;y<128;y++){ lcd_rx0[y]=255; lcd_rx1[y]=0; } lcd_rall=0; }
    else for(int y=y0;y<y1;y++){ lcd_rx0[y]=255; lcd_rx1[y]=0; }
    if(rows) return; }
#endif
#ifdef FAST_LCD
  hw_px_begin((u32)x0,(u32)y0,(u32)x1,(u32)y1);
#endif
  for(int y=y0;y<y1;y++){
    for(int x=x0;x<x1;x++){
      u16 p = on ? l->gram[y][x] : 0;
      if(on && inv) p=(u16)~p;
      if(bgr) p=(u16)(((p&0x1F)<<11)|(p&0x07E0)|(p>>11));
      g_row[x]=p;
    }
#ifdef FAST_LCD
    hw_px_row(g_row + x0, (u32)(x1-x0));
#else
    hw_push(g_row - 0 , 0, x0, y, x1, y+1);  /* stride 0: one row */
#endif
  }
#ifdef FAST_LCD
  hw_px_end();
#endif
}

static void leave_go(int save){
  hw_tone(0);
  REG(0x20000FF8u)=0;                    /* start counter of the home hooks */
  REG(0x20000FFCu)=0x4C584D50u;          /* "PMXL": back on purpose, the autostart of the stub stays off (v0.9.4) */
  if(save)
#ifdef DBGLEAVE
  g_st[15]=0x1EA0;
#endif
  st_save(&g_emu, &g_sc, g_gram);
#ifdef DBGLEAVE
  g_st[15]=0x1EA1;
  for(;;) hw_wdt_feed();
#endif
  /* Only restart once the wheel is released: otherwise the Paradise sees a
   * pressed button while booting. Screen off as a sign that the save is done. */
#ifdef BL_SLEEP
  hw_bl_pwm_off();                        /* do not put the display to sleep: the firmware restarts right away */
#else
  hw_backlight(0);
#endif
  for(u32 t=0; hw_buttons() && t<20000000u; t++) if((t & 0xFFFFu)==0) hw_wdt_feed();
  MPU_CTRL = 0u; __asm__ volatile("dsb\n isb" ::: "memory");   /* start the firmware without protection */
  XIP_BASE_REG = 0x60011000u; XIP_CTRL_REG = 3u;
  hw_back_to_paradise();
}
/* v0.9.2: if guest writes were lost, the guest may be stuck: do not keep that state */
static void leave(void){ leave_go(g_emu.flash_lost_writes==0u); }
static void leave_nosave(void){ leave_go(0); }

/* ROM blocks: packed from guest_off, the first 16 are in the cached window */
TC_INIT static void setup_rom(Emu *e){
  for(u32 i=0;i<128;i++){
    u32 k=g_cfg.map[i];
    if(k==0xFE) k=i;                       /* old layout: linear */
    if(k==0xFF || (i<<16)>=e->dev.rom_size){ e->rom_src[i]=0; e->rom_blk[i]=0; continue; }
    e->rom_src[i]=(const uint8_t*)(0x60000000u + g_cfg.guest_off + (k<<16));
#ifdef ROM_CACHED
    e->rom_blk[i]= k<16 ? (const uint8_t*)(0x10000000u + (k<<16)) : e->rom_src[i];
#else
    /* On the device: after changing the base, the cached window sometimes returns
     * stale lines (suspected, v0.8.4). So everything goes through the uncached window. */
    e->rom_blk[i]= e->rom_src[i];
#endif
  }
  e->rom_blk[e->par_base>>16]=0;
}

/* Self test on the device: does the CPU see code that was newly written to PRAM?
 * The translator (JIT) keeps writing code to PRAM and executing it. If instruction
 * fetch returns stale contents (cache), the guest runs into nowhere.
 * Result (row y=68): C a b c d e  (expected 11 22 33 44 55).
 * a: first call, b: after overwriting, c/d/e: after overwriting and
 * a nop sled of 1/4/8 KB (evicts a cache). */
static u32 g_ct_ok;
#ifndef CLEAN
#define CT_X    0xFF00u
static u32 ct_call(u32 v){
  volatile u16 *x=(volatile u16*)(g_zero+CT_X);
  x[0]=(u16)(0x2000u | (v & 0xFFu)); x[1]=0x4770u;    /* movs r0,#v ; bx lr */
  __asm__ volatile("dsb\n isb" ::: "memory");
  return 0;
}
static u32 ct_run(void){ u32 (*f)(void)=(u32(*)(void))(g_zero+CT_X+1u); return f(); }
static void ct_sled(u32 n){
  u32 base=((u32)__jit_start + 0xFFu) & ~0xFFu;   /* free space, never executed before */
  if(base + n > CT_X) n = CT_X - base;
  volatile u16 *s=(volatile u16*)(g_zero+base);
  for(u32 i=0;i<n/2u-1u;i++){ s[i]=0xBF00u; }
  s[n/2u-1u]=0x4770u;
  __asm__ volatile("dsb\n isb" ::: "memory");
  void (*f)(void)=(void(*)(void))(g_zero+base+1u); f();
}
static void code_test(void){
  u32 r[5];
  ct_call(0x11); r[0]=ct_run();
  ct_call(0x22); r[1]=ct_run();
  ct_call(0x33); ct_sled(0x400);  r[2]=ct_run();
  ct_call(0x44); ct_sled(0x1000); r[3]=ct_run();
  ct_call(0x55); ct_sled(0x2000); r[4]=ct_run();
  g_ct_ok = r[0]==0x11 && r[1]==0x22 && r[2]==0x33 && r[3]==0x44 && r[4]==0x55;
  u32 v[6]={0xC,r[0],r[1],r[2],r[3],r[4]}; static const u8 nv[6]={1,2,2,2,2,2};
  diag_line(44, v, nv, 6);
}
#endif

/* Write protection for the program code in PRAM (MPU). On the device with v0.8.6, code
 * at 0x5612 was overwritten (fault F at 0x5610). With the protection, any stray
 * write raises a fault immediately: the fault screen then shows the writing
 * location (pc) and the target (MMFAR instead of BFAR).
 * Display y=56: M <MPU_TYPE> <1 = protection on>. */
static void mpu_guard(void){
  u32 t=MPU_TYPE, on=0;
  u32 end=(u32)__pdata_start & ~0xFFFu;             /* up to the data, rounded to 4 KB */
#ifndef NOMPU
  if(((t>>8)&0xFFu) >= 2u && end >= 0x8000u && end <= 0x10000u){
    const u32 ro=(6u<<24) | (1u<<17);                 /* read only, normal memory */
    MPU_RNR=0; MPU_RBAR=0x00000000u; MPU_RASR=ro | (14u<<1) | 1u;          /* 0..0x8000 */
    u32 n=(end-0x8000u)/0x1000u;                      /* active 4 KB parts from 0x8000 */
    MPU_RNR=1; MPU_RBAR=0x00008000u; MPU_RASR=ro | (((0xFFu<<n)&0xFFu)<<8) | (14u<<1) | (n?1u:0u);
    __asm__ volatile("dsb\n isb" ::: "memory");
    SHCSR |= 1u<<16;                                  /* MemManage as its own fault */
    MPU_CTRL = 5u;                                    /* on, background map as without MPU */
    __asm__ volatile("dsb\n isb" ::: "memory");
    on=1;
  }
#endif
#ifndef CLEAN
  u32 v[3]={0xD /* M is not in the font: D as in data protection */, t, on}; static const u8 nv[3]={1,8,1};
  diag_line(56, v, nv, 3);
#else
  (void)on;
#endif
}

/* Is a firmware DMA still running after the restart and writing to SRAM?
 * At the very start (before .bss), fill SRAM up to just below sp with a pattern,
 * wait a bit (feeding the watchdog), then count differences. 0x20000000..0x20000400 is read only.
 * Display y=8: 5 <count> <first offset from 0x18000000> <1 if 0x20000000.. has changed>. */
#ifndef CLEAN
__attribute__((noinline)) static u32 sram_watch(u32 *first, u32 *c2){
  u32 sp; __asm__ volatile("mov %0, sp" : "=r"(sp));
  volatile u32 *p=(volatile u32*)0x18000000u; u32 n=(sp-0x200u-0x18000000u)/4u;
  /* only read and compare 0x20000000 (contents belong to the stub/firmware) */
  volatile u32 *q=(volatile u32*)0x20000000u; const u32 m=0x400u/4u; u32 snap=0;
  for(u32 i=0;i<n;i++) p[i]=0xA5C30000u ^ i;
  for(u32 i=0;i<m;i++) snap += q[i] * (i*2u+1u);
  /* wait; the firmware watchdog is still running (v0.8.8 reset the Tama here) */
  for(u32 t=0;t<8000000u;t++){ if((t & 0xFFFu)==0){ REG(0x4000800Cu)=0x5AFA55AAu; REG(0x4000900Cu)=0x5AFA55AAu; } __asm__ volatile("nop"); }
  u32 c=0, f=0, k=0, snap2=0;
  for(u32 i=0;i<n;i++) if(p[i]!=(0xA5C30000u ^ i)){ if(!c) f=i*4u; c++; }
  for(u32 i=0;i<m;i++) snap2 += q[i] * (i*2u+1u);
  k = snap!=snap2;
  *first=f; *c2=k; return c;
}
static u32 g_sw_n, g_sw_f, g_sw_k;
#endif
static u32 g_ovl;                         /* speed display on (short wheel press) */
/* Timing (v0.8.20): cycles from SysTick (g_ms * 96000 + elapsed part of the current ms).
 * MESS: release build plus only the speed display (short wheel press), for measuring on the device. */
#if !defined(CLEAN) || defined(MESS)
static u32 g_cy_emu, g_cy_disp;
static u32 cyc_now(void){ u32 m, v; do { m=g_ms; v=SYST_CVR; } while(m!=g_ms); return m*96000u + (95999u - v); }
#endif
extern uint32_t jit_st_flush, jit_st_blocks;
extern u32 st_n_evict;
static u32 g_n_gflash;                    /* counter: guest save written to flash */
/* Measure the size of the RAM at 0x20000000 (v0.8.15): read and write from 0x20001000 in 2 KB steps.
 * A bus fault on read is caught in the fault handler (g_probe); that marks the end.
 * Result in KB (0x20000000 up to the first missing step), at most 64. */
#ifndef CLEAN
static u32 g_ram20;
__attribute__((noinline)) static u32 probe_rd(u32 a){ u32 v; __asm__ volatile("ldr %0, [%1]\n nop" : "=l"(v) : "l"(a) : "memory"); return v; }
static void ram20_size(void){
  u32 kb=4;
  for(u32 a=0x20001000u; a<0x20010000u; a+=0x800u){
    g_probe_hit=0; g_probe=1; u32 old=probe_rd(a); g_probe=0;
    if(g_probe_hit) break;
    volatile u32 *p=(volatile u32*)a; *p=0xC3A55A3Cu ^ a; __asm__ volatile("dsb" ::: "memory");
    u32 ok = *p==(0xC3A55A3Cu ^ a); *p=old;
    if(!ok) break;
    kb += 2;
  }
  g_ram20=kb;
}
#ifdef PROBE
/* Memory probe (v0.8.13, build with -DPROBE): answers three questions on the device without JIT.
 * y=20  1 <a> <b>  Mirroring: write a marker to PRAM at 0xE000/0xFF80, search the whole SRAM
 *                 (0x18000000..0x1801FFFF) and 0x20000000..0x20000FFF; a hit only counts if the location
 *                 also shows the changed marker. Address (lower 5 digits, 8xxxx = 0x20000xxx; 0 = nowhere).
 * y=32  2 <a> <b>  Reverse: marker in SRAM at 0x1801E000/0x1801FC00, searched in PRAM 0xD000..0xFFFF.
 * y=44  4 <x> <y>      Is code in SRAM (0x1801E800) or at 0x20000E00 executable? 66 = yes (return value).
 * Then it halts; holding the wheel for 2 s returns. */
static u32 probe_exec(u32 at){
  volatile u16 *x=(volatile u16*)at; x[0]=0x2066u; x[1]=0x4770u;     /* movs r0,#0x66 ; bx lr */
  __asm__ volatile("dsb\n isb" ::: "memory");
  u32 (*f)(void)=(u32(*)(void))(at|1u); return f();
}
static void probe_all(void){
  static const u8 n5[4]={1,5,5,5};
  u32 sp; __asm__ volatile("mov %0, sp" : "=r"(sp));
  { static const u32 pa[2]={0xE000u,0xFF80u}; u32 r[3]={1,0,0}; static const u8 n3[3]={1,5,5};
    for(u32 k=0;k<2;k++){ volatile u32 *pp=(volatile u32*)(g_zero+pa[k]); u32 mk=0x6D4B5A5Au ^ (k*0x1111u);
      *pp=mk; __asm__ volatile("dsb" ::: "memory");
      for(u32 a=0x18000000u; a<0x18020000u && !r[k+1]; a+=4) if(*(volatile u32*)a==mk){
        *pp=~mk; __asm__ volatile("dsb" ::: "memory");
        if(*(volatile u32*)a==~mk) r[k+1]=a&0xFFFFFu; else *pp=mk; }
      if(!r[k+1]) for(u32 a=0x20000000u; a<0x20001000u && !r[k+1]; a+=4) if(*(volatile u32*)a==*pp){
        u32 o=*pp; *pp=~o; __asm__ volatile("dsb" ::: "memory");
        if(*(volatile u32*)a==~o) r[k+1]=0x80000u|(a&0xFFFu); else *pp=o; } }
    diag_line(20, r, n3, 3); }
  { static const u32 sa[2]={0x1801E000u,0x1801FC00u}; u32 r[3]={2,0,0}; static const u8 n3[3]={1,5,5};
    for(u32 k=0;k<2;k++){ if(sa[k] >= sp-0x40u) { r[k+1]=0xFFFFFu; continue; }
      volatile u32 *sp_=(volatile u32*)sa[k]; u32 mk=0x3C7EA5A5u ^ (k*0x2222u); *sp_=mk; __asm__ volatile("dsb" ::: "memory");
      for(u32 a=0xD000u; a<0x10000u && !r[k+1]; a+=4) if(*(volatile u32*)(g_zero+a)==mk){
        *sp_=~mk; __asm__ volatile("dsb" ::: "memory");
        if(*(volatile u32*)(g_zero+a)==~mk) r[k+1]=a; else *sp_=mk; } }
    diag_line(32, r, n3, 3); }
  { u32 r[3]={4, 0xEE, 0xEE}; static const u8 n2[3]={1,2,2};
    diag_line(44, r, n2, 3);                   /* EE = not run yet (if the call hangs, EE stays) */
    r[1]=probe_exec(0x1801E800u); diag_line(44, r, n2, 3);
    r[2]=probe_exec(0x20000E00u); diag_line(44, r, n2, 3); }
  for(u32 t=0;;t++){ hw_wdt_feed(); if(hw_buttons()&BTN_WHEEL){ if(t>2000000u) leave(); } else t=0; }
}
#endif
/* PRAM write test (v0.8.12): word pattern in 64 bytes at 0xFF40, then write one byte (offset 0..3) or one
 * halfword (0, 2) into word 8 and read everything back. Per test: number of wrong bytes OUTSIDE
 * the target, plus 0x80 if the target itself is wrong. Display y=8: W b0 b1 b2 b3 h0 h2 (00 = correct). */
static void pram_wtest(void){
  volatile u32 *w=(volatile u32*)(g_zero+0xFF40u); volatile u8 *b=(volatile u8*)w; volatile u16 *h=(volatile u16*)w;
  u32 r[6]; u8 exp[64];
  for(u32 t=0;t<6;t++){
    for(u32 i=0;i<16;i++){ u32 v=0x11223344u ^ (i*0x01010101u) ^ (t<<28); w[i]=v;
      exp[4*i]=(u8)v; exp[4*i+1]=(u8)(v>>8); exp[4*i+2]=(u8)(v>>16); exp[4*i+3]=(u8)(v>>24); }
    __asm__ volatile("dsb" ::: "memory");
    u32 lo, n;
    if(t<4){ lo=32u+t; n=1; b[lo]=0xA5u; exp[lo]=0xA5u; }
    else { lo=32u+(t-4u)*2u; n=2; h[lo/2u]=0xA55Au; exp[lo]=0x5Au; exp[lo+1]=0xA5u; }
    __asm__ volatile("dsb" ::: "memory");
    u32 bad=0, tbad=0;
    for(u32 i=0;i<16;i++){ u32 v=w[i];
      for(u32 k=0;k<4;k++){ u32 j=4*i+k; if((u8)(v>>(8*k))!=exp[j]){ if(j>=lo && j<lo+n) tbad=1; else bad++; } } }
    r[t]=(bad>0x7Fu?0x7Fu:bad) | (tbad?0x80u:0u);
  }
  u32 v[7]={0x3 /* no W in the font: 3 as in third probe */, r[0],r[1],r[2],r[3],r[4],r[5]}; static const u8 nv[7]={1,2,2,2,2,2,2};
  diag_line(8, v, nv, 7);
}
#endif

extern u32 __bss_start, __bss_end, __ram_end;
void Reset_Handler(void){
  __asm__ volatile("cpsid i");
  u32 swf=0, swk=0, swn=0;
#ifndef NOSRAMWATCH
  swn=sram_watch(&swf, &swk);
#endif
  for(u32* p=&__bss_start; p<&__bss_end; p++) *p=0;
  { extern u32 __data_start, __data_end, __data_load;     /* initial data values into SRAM */
    const u32 *src=&__data_load; for(u32* p=&__data_start; p<&__data_end; p++) *p=*src++; }
#ifndef CLEAN
  g_sw_n=swn; g_sw_f=swf; g_sw_k=swk;
#else
  (void)swn; (void)swf; (void)swk;
#endif
  g_sc.guest_off=g_cfg.guest_off; g_sc.guest_size=g_cfg.guest_size; g_sc.save_off=g_cfg.save_off; g_sc.device=g_cfg.device; g_sc.map=g_cfg.map; g_sc.spare_end = g_cfg.version>=3u ? g_cfg.spare_end : 0u;
  hw_init();
#ifndef CLEAN
  g_pc_n=pram_check((u32)__pdata_start, 1);
#else
  pram_check((u32)__pdata_start, 1);
#endif   /* after flashctl_init: code up to the data */
  /* Sign on the device: screen dark blue as soon as the display is set up. If the
   * Paradise screen stays after A+C, startup never got this far. */
#ifdef CLEAN
  for(u32 x=0;x<128;x++) g_row[x]=0;        /* black until the guest draws */
#else
  for(u32 x=0;x<128;x++) g_row[x]=0x0010u;
#endif
  for(u32 y=0;y<128;y++) hw_push(g_row, 0, 0, y, 128, y+1);
  /* Guest ROM: map the first MB cached, the rest uncached. */
  g_stage=1; diag(&g_emu,0);
#ifndef CLEAN
  pram_wtest();
#ifdef RAM20
  ram20_size();                           /* harmful in Capybara (slows down), so off */
#endif
#ifdef PROBE
  probe_all();
#endif
#ifndef NOSRAMWATCH
  { u32 v[4]={5, g_sw_n>0xFFFFu?0xFFFFu:g_sw_n, g_sw_f&0xFFFFFu, g_sw_k>0xFFFu?0xFFFu:g_sw_k}; static const u8 nv[4]={1,4,5,3}; diag_line(8, v, nv, 4); }
#endif
#ifdef JIT_FRESHBUF
  g_ct_ok=1;                              /* experiment: execute nothing in the JIT area beforehand */
#else
  code_test();
#endif
#else
  g_ct_ok=1;
#endif
  mpu_guard();
#ifdef NOJIT
  jit_enabled=0;                          /* test build: interpreter only */
#else
  if(!g_ct_ok) jit_enabled=0;             /* new code is not seen: run without JIT */
#endif
  XIP_BASE_REG = 0x60000000u | g_cfg.guest_off;
  XIP_CTRL_REG = 3u;
  Emu *e=&g_emu;
  g_stage=2; diag(e,0);
  /* device: 0 Plus Color (4 MB), 1 iD L, 2 P's, 3 4U+, 4 4U (all 8 MB, same map as the iD L; the 4U runs at 30 MHz) */
  if(g_cfg.device>=1){ tc_profile_idl(&e->dev); if(g_cfg.device>=3) e->dev.osc3_hz=30000000u; }
  else tc_profile_plus_color(&e->dev);
  tc_init(e, 0, g_gram, g_pool, NSLOT);
  tc_ovl_evict = st_evict; tc_big_erase = st_big_erase; st_init(&g_sc, g_pool);
#ifdef OVL_RAM20
  ovl_extra(e);
#endif
  setup_rom(e);
  cpu_reset(e);
  g_stage=3; diag(e,0);
  { u32 pause=0;
    if(st_load(e, &g_sc, g_gram, &pause)){
      g_restored=1+pause;
      /* reset pointers: everything that points into this run */
      e->lcd.gram=g_gram;
      for(int i=0;i<OVL_SLOTS;i++){ e->ovl.base[i]=0xFFFFFFFFu; e->ovl.mem[i]= i<NSLOT ? g_pool+(u32)i*OVL_SIZE : 0; }
#ifdef OVL_RAM20
      ovl_extra(e);
#endif
      e->ovl.n=0; for(int i=0;i<4;i++) e->ovl_blk[i]=0;
      e->rom=0; e->rom_fast=0; e->rom_fast_len=0;
      e->fw_len=0; e->fw_lo=0xFFFFFFFFu;
      setup_rom(e);
      if(pause > 400u*86400u) pause=400u*86400u;
      periph_rtc_advance(e, pause);
      e->lcd.dx0=0; e->lcd.dy0=0; e->lcd.dx1=127; e->lcd.dy1=127; LCD_ALL();
    }
  }
  st_pages(e, &g_sc);                     /* spare pages for written empty blocks (after a restore as well) */
  /* startup code is done: the translator gets its space as well */
  g_stage=4; diag(e,0);
#if defined(JIT_STAGE)
  /* v0.8.16: buffer back in PRAM (large, fast); code is generated in SRAM (3 KB after .bss) and copied by
   * a loop running in SRAM. This way no PRAM code writes to PRAM any more. */
  { u32 lo=((u32)&__ram_end + 31u) & ~31u;
    jit_stage_setup((uint8_t*)lo, 0xC00u);
    jit_init(e, __initcode_start, (u32)__initcode_start, (u32)(__jit_end - __initcode_start)); }
#elif defined(JIT_SRAMBUF)
  /* v0.8.14: translated code in SRAM between .bss and the stack (2 KB stack). On the device, code in
   * SRAM is executable (probe v0.8.13: 66). Writing to PRAM while the CPU fetches instructions from PRAM
   * is suspected of corrupting the fetched instructions. This way nothing writes to PRAM any more. */
  { u32 lo=((u32)&__ram_end + 31u) & ~31u, hi=0x18020000u - 0x800u;
    jit_init(e, (uint8_t*)lo, lo, hi-lo); }
#elif defined(JIT_FRESHBUF)
  /* Experiment v0.8.11: JIT area only in the never executed part (from __jit_start), not over the
   * startup code. Smaller, but it never held code the CPU had already fetched. */
  jit_init(e, __jit_start, (u32)__jit_start, (u32)(__jit_end - __jit_start));
#else
  jit_init(e, __initcode_start, (u32)__initcode_start, (u32)(__jit_end - __initcode_start));
#endif
#if defined(JIT_GUARD) && !defined(CLEAN)
  jit_bad_hook = jit_bad;
#endif
  g_stage=5; diag(e,0);
  systick_init();
  __asm__ volatile("cpsie i");
  g_stage=6; diag(e,0);
  u32 emu_ms=g_ms, last_push=g_ms, wheel_ms=0, dropped=0, idle=0, busy_ms=0;
  for(;;){
    hw_wdt_feed();
#ifndef CLEAN
    if(!g_guest_drawn && (++g_loops & 0x3FFu)==1){ g_stage=7; diag(e, emu_ms); }
#endif
#ifdef DETERM
    u32 now=emu_ms+1;
#else
    u32 now=g_ms;
#endif
    if((s32)(now-emu_ms) > 100){                                  /* too far behind: drop time */
      u32 d=now-emu_ms-100; dropped += d; emu_ms = now-100;
      { static u32 lost; lost += d; if(lost>=1000u){ periph_rtc_advance(e, lost/1000u); lost%=1000u; } }   /* guest clock stays correct anyway */
    }
    if(emu_ms == now){                     /* caught up: sleep until the next tick */
      idle += SYST_CVR;
      /* Only sleep once the SysTick interrupt is known to arrive. Otherwise poll the
       * counter directly (COUNTFLAG) so the loop never gets stuck in wfi. */
      { static u32 g0=0xFFFFFFFFu; static int tick_ok;
        if(g0==0xFFFFFFFFu) g0=g_ms;
        if(!tick_ok && g_ms!=g0) tick_ok=1;
        if(tick_ok) __asm__ volatile("wfi");
        else { u32 m=g_ms; if((SYST_CSR & 0x10000u) && g_ms==m) g_ms++; } }
      continue;
    }
    /* one millisecond of guest time */
    u32 mhz = e->cmu.mclk_hz ? e->cmu.mclk_hz : e->dev.osc3_hz;
    /* v0.9.3: carry the rest of the division. With the 32768 Hz clock (the guest sleeping)
     * mhz/1000 gave 32 instead of 32.768 cycles per ms: the guest clock lost 2.3 percent. */
    static u32 cy_rest;
    u32 cy_ms; { u32 t = mhz + cy_rest; cy_ms = t / 1000u; cy_rest = t % 1000u; }
#if defined(CLEAN) && !defined(MESS)
    u32 tr_stop=tc_run(e, e->cycles + cy_ms);
#else
    u32 pc0=cyc_now();
    u32 tr_stop=tc_run(e, e->cycles + cy_ms);
    g_cy_emu += cyc_now()-pc0;
#endif
    if(tr_stop){ /* core stopped: show the error and wait */
      { volatile u32* st=g_st; st[0]=0x53544F50u; st[6]=e->stop_pc; st[7]=e->stop_code; st[2]=(u32)e->cycles; }
      for(int y=0;y<128;y++) for(int x=0;x<128;x++) g_gram[y][x]=(u16)0xF800;
      e->lcd.dx0=0; e->lcd.dy0=0; e->lcd.dx1=127; e->lcd.dy1=127; LCD_ALL();
      e->lcd.sleep_out=1; e->lcd.disp_on=1; e->lcd.madctl=0;
      push_dirty(e);
      for(;;){ hw_wdt_feed(); if(hw_buttons()&BTN_WHEEL) leave_nosave(); }
    }
    emu_ms++; busy_ms++;
    /* v0.9.3: keep the guest clock on the Paradise's own seconds counter (crystal). If the
     * millisecond tick runs slow, or busy scenes (shop, flash writes) make the emulator drop
     * time, the missing seconds are added to the guest clock. */
    static u32 run_ms; run_ms++;          /* ms the guest really ran (dropped ms not counted) */
    if((run_ms & 1023u)==0u){
      static u32 r0, m0, have; static s32 owe;
      u32 r = REG(0x45000304u);
      if(!have){ r0=r; m0=run_ms; have=1; }
      else if(r - r0 >= 30u){
        u32 dr = r - r0, dm = run_ms - m0;
        if(dr <= dm/1000u + 600u && dm/1000u <= dr + 5u){      /* plausible: no jump of the counter */
          owe += (s32)(dr*1000u) - (s32)dm;
          if(owe >= 1000){ u32 n=(u32)owe/1000u; periph_rtc_advance(e, n); owe -= (s32)(n*1000u); }
          if(owe < -2000) owe = -2000;                            /* tick fast: nothing to take back */
        }
        r0=r; m0=run_ms;
      }
    }
    /* Guest save into the Paradise flash: writing briefly stalls everything.
     * So only once nothing has been written for 3 s AND the screen has been still
     * for a moment (no game running), at the latest after 2 minutes. */
    { static u32 quiet; static int pend;
      if(e->flash_dirty){ e->flash_dirty=false; quiet=0; pend=1; }
      #ifdef GFLUSH_STEP
      /* v0.8.22: after 3 s without new writes, at most one changed 4 KB part to flash every 250 ms,
       * instead of everything at once (that caused stalls of half a second and more). */
#ifdef GFLUSH_DARK
      /* v0.9.1: only write while the screen is dark (then the pause goes unnoticed);
       * after 10 minutes at the latest anyway, in case it never goes dark */
      else if(pend && ++quiet>3000u && (quiet % 250u)==0u && (!g_bl || quiet>600000u) && !e->flash_busy && !e->flash_state){ if(st_flush_step(e, &g_sc)){ pend=0; g_n_gflash++; } } }
#else
      else if(pend && ++quiet>3000u && (quiet % 250u)==0u && !e->flash_busy && !e->flash_state){ if(st_flush_step(e, &g_sc)){ pend=0; g_n_gflash++; } } }
#endif
#else
      else if(pend && ++quiet>3000u && (g_still>600u || quiet>120000u) && !e->flash_busy && !e->flash_state){ st_flush_guest(e, &g_sc); pend=0; g_n_gflash++; } }
#endif
    u32 b=hw_buttons();
    { static int lock=1;                    /* wait for the start buttons (A+C) to be released */
      g_st[14]=b | ((u32)lock<<8);
      if(lock){ if(b) b=0; else lock=0; } }
    /* v0.9.5: guest screen dark right after the start (its own screen saver ran out while the clock
     * caught up, often after the autostart): one short press of C wakes it, like the user would.
     * Only once, only after resuming a session, in the first 8 s, while dark and nothing is pressed. */
    { static u32 t0, wk_ms; static int wk;
      if(!t0) t0=now?now:1;
      if(wk==0){ if(g_restored && !g_bl && !b && now-t0>1000u) { wk=1; wk_ms=now; } else if(now-t0>8000u) wk=2; }
      if(wk==1){ if(now-wk_ms<150u) b|=BTN_C; else wk=2; } }
    periph_buttons(e,(uint8_t)(b&7));
    /* v0.9.2: wheel within the first 3 s after resuming a saved session: throw the session away
     * and go back to the Paradise. The next start begins fresh from the Tamagotchi's own save. */
    { static u32 t_start; if(!t_start) t_start=now?now:1;
      if(g_restored && (b&BTN_WHEEL) && now-t_start<3000u){ st_drop(&g_sc); leave_nosave(); } }
    #ifndef NOWHEEL
    if(b&BTN_WHEEL){ if(!wheel_ms) wheel_ms=now?now:1; else if(now-wheel_ms>1500) leave(); }
#if defined(CLEAN) && !defined(MESS)
    else wheel_ms=0;
#else
    else { if(wheel_ms && now-wheel_ms<1000u){ g_ovl^=1u; if(!g_ovl){ e->lcd.dx0=0; e->lcd.dy0=0; e->lcd.dx1=127; e->lcd.dy1=59; LCD_ALL(); } } wheel_ms=0; }
#endif
#endif
#ifdef LCD_SKIP
    /* v0.8.19: if emulation lags behind (more than 20 ms), only one frame every 80 ms instead of every 40 ms */
    if(emu_ms-last_push >= (((s32)(g_ms-emu_ms) > 20) ? 80u : 40u)){ last_push=emu_ms;
#if defined(CLEAN) && !defined(MESS)
      push_dirty(e); }
#else
      u32 c0=cyc_now(); push_dirty(e); g_cy_disp += cyc_now()-c0; }
#endif
#else
    if(emu_ms-last_push>=40){ last_push=emu_ms; push_dirty(e); }
#endif
    /* Speed display (short wheel press): top row, every 0.5 s.
     * 5 <speed in % of real time, decimal> <JIT buffer used in 256 B> <flushes per s> <idle in %, decimal>
     * 6 <translated blocks per s> <evicted flash sectors per s> <save parts to flash per s> */
#if !defined(CLEAN) || defined(MESS)
    { static u32 t0, e0, d0, f0, i0, b0, v0, g0, ce0, cd0;
      if(now-t0>=500u){
        if(g_ovl && t0){ u32 dt=now-t0, de=(emu_ms-e0)-(dropped-d0); u32 pct=de*100u/(dt?dt:1u); if(pct>999u) pct=999u;
          u32 bcd=((pct/100u)<<8)|(((pct/10u)%10u)<<4)|(pct%10u); u32 fl=(jit_st_flush-f0)*2u; if(fl>0xFFu) fl=0xFFu;
          u32 ip=(idle-i0)/(dt*960u+1u); if(ip>99u) ip=99u; ip=((ip/10u)<<4)|(ip%10u);
          u32 v[5]={5, bcd, jit_used()>>8, fl, ip}; static const u8 nv[5]={1,3,2,2,2}; diag_line(0, v, nv, 5);
          u32 nb=(jit_st_blocks-b0)*2u, ne=(st_n_evict-v0)*2u, nf=(st_n_step-g0)*2u; if(nb>0xFFFu) nb=0xFFFu; if(ne>0xFFu) ne=0xFFu; if(nf>0xFFu) nf=0xFFu;
          u32 w[4]={6, nb, ne, nf}; static const u8 nw[4]={1,3,2,2}; diag_line(10, w, nw, 4);
          /* 7 <emulation %> <display output %> <rest %> of CPU time (decimal) */
          u32 tot=dt*960u+1u, pe_=(g_cy_emu-ce0)/tot, pd=(g_cy_disp-cd0)/tot; if(pe_>99u) pe_=99u; if(pd>99u) pd=99u;
          u32 rest = 100u>pe_+pd ? 100u-pe_-pd : 0u; if(rest>99u) rest=99u;
          u32 z[4]={7, ((pe_/10u)<<4)|(pe_%10u), ((pd/10u)<<4)|(pd%10u), ((rest/10u)<<4)|(rest%10u)}; static const u8 nz[4]={1,2,2,2};
          diag_line(20, z, nz, 4);
#ifdef PROF
          /* 8/9: the four most hit 256-byte locations (address, percent). A: share in the JIT buffer
           * (from __initcode_start), share outside PRAM (SRAM copy loop and others). */
          { u32 tot2=g_prof_other; for(u32 i=0;i<256;i++) tot2+=g_prof[i]; if(!tot2) tot2=1;
            u32 top[4]={0,0,0,0}, tv[4]={0,0,0,0};
            for(u32 i=0;i<256;i++){ u32 v=g_prof[i]; for(u32 k=0;k<4;k++) if(v>tv[k]){ for(u32 m=3;m>k;m--){ tv[m]=tv[m-1]; top[m]=top[m-1]; } tv[k]=v; top[k]=i; break; } }
            u32 jit=0; for(u32 i=((u32)__initcode_start)>>8;i<256;i++) jit+=g_prof[i];
            #define PCT(x) ({ u32 _p=(x)*100u/tot2; if(_p>99u) _p=99u; ((_p/10u)<<4)|(_p%10u); })
            u32 a8[5]={8, top[0]<<8, PCT(tv[0]), top[1]<<8, PCT(tv[1])}; static const u8 n8[5]={1,4,2,4,2};
            u32 a9[5]={9, top[2]<<8, PCT(tv[2]), top[3]<<8, PCT(tv[3])};
            u32 aa[3]={0xA, PCT(jit), PCT(g_prof_other)}; static const u8 na_[3]={1,2,2};
            diag_line(30, a8, n8, 5); diag_line(40, a9, n8, 5); diag_line(50, aa, na_, 3);
            for(u32 i=0;i<256;i++) g_prof[i]=0; g_prof_other=0; }
#endif
          }
        t0=now; e0=emu_ms; d0=dropped; f0=jit_st_flush; i0=idle; b0=jit_st_blocks; v0=st_n_evict; g0=st_n_step; ce0=g_cy_emu; cd0=g_cy_disp; } }
#endif
#ifdef TONE_FIX
    /* Guest sound to the buzzer. Short gaps (under 30 ms of guest time) are bridged,
     * otherwise the timer keeps restarting and it crackles. */
    { static u32 tone, offc; u32 t=periph_tone_hz(e);
      if(t){ offc=0; if(t!=tone){ tone=t; hw_tone(t); } }
      else if(tone && ++offc>=30u){ tone=0; hw_tone(0); } }
#else
    { static u32 tone; u32 t=periph_tone_hz(e);                 /* guest sound to the buzzer */
      if(t!=tone){ tone=t;
#ifndef NOTONE
        hw_tone(t);
#endif
      } }
#endif
    { static u32 dark; int bl=g_bl;                            /* guest display off: backlight off */
      int on = e->lcd.sleep_out && e->lcd.disp_on;
#ifdef BL_BLACK
      /* v0.8.16: the guest often just turns its screen black instead of off. Every 0.5 s check
       * whether the whole frame is black; if so, treat it as "off". */
      { static u32 bt; static int black;
        if(now-bt>=500u){ bt=now; u16 z = e->lcd.inverted ? 0xFFFFu : 0u; black=1;
          const u16 *g=&g_gram[0][0]; for(u32 i=0;i<GRAM_W*GRAM_H;i++) if(g[i]!=z){ black=0; break; } }
        if(black) on=0; }
#endif
      if(on){ dark=0; if(!bl){ bl=1; hw_backlight(1); e->lcd.dx0=0; e->lcd.dy0=0; e->lcd.dx1=127; e->lcd.dy1=127; LCD_ALL(); } }
      else if(bl && ++dark>500u){ bl=0; hw_backlight(0); }
      g_bl=bl; }
    { volatile u32* st=g_st; st[0]=0x53544154u; st[1]=now; st[2]=(u32)e->cycles; st[3]=(u32)(e->cycles>>32);
      st[4]=e->cmu.mclk_hz; st[5]=e->flash_lost_writes; st[6]=e->pc; st[7]=e->halted; st[8]=jit_used();
      st[9]=emu_ms; st[10]=dropped; st[11]=idle; st[12]=busy_ms; st[13]=g_restored; }
  }
}

__attribute__((naked)) static void Default_Handler(void){ __asm__ volatile("1: b 1b\n"); }
__attribute__((naked)) static void Irq_Off(void){ __asm__ volatile("cpsid i\n bx lr\n"); }
__attribute__((section(".vectors"), used))
const void* const g_vectors[16+64] = { (void*)0x18020000u, (void*)Reset_Handler,
  [2 ... 14] = (void*)Default_Handler, [3 ... 6] = (void*)Fault_Handler, [11] = (void*)Irq_Off, [14] = (void*)Irq_Off,
  [15] = (void*)SysTick_Handler, [16 ... 79] = (void*)Irq_Off };
