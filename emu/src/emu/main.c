/* Paramix emulator app for the Tamagotchi Paradise: runs a colour
 * Tamagotchi core (tamaemu, GPL-3.0) directly on the device.
 * Long press on the wheel: save state and return to the Paradise. */
#include "hw.h"
#include "exitkey.h"
#include "ask.h"
#include "flash.h"
#include "../core/tc.h"
#include "state.h"
#ifdef TC_IRLINK
#define PL_ATTR __attribute__((noinline, minsize))
#ifdef EMU_TONE_Q
#define PL_INIT_ATTR __attribute__((section(".initcode"), noinline, minsize))   /* test20: room for the tone queue */
#endif
#include "plink.h"
#ifdef CLK_ECO
#include "eco.h"
#endif
/* 1.6.9: the guest's infrared bytes to the other Paradise ('E' frames over the contacts) and back */
static u8 g_irq[512], g_irtxn;
static u16 g_irh, g_irt;
int tc_ir_avail(void) { return g_irh != g_irt; }
int tc_ir_in(void) { if (g_irh == g_irt) return -1; u8 b = g_irq[g_irh & 511u]; g_irh++; return b; }
#ifdef LK_SYNC
/* 1.7.1 LK_SYNC: two iD L (or P's) only connect when each answers within about 10 ms of the other's game time. The
 * emulators run on their own: a busy moment (drawing, translating, the flash) put one of them 20 ms or more behind and
 * the visit failed ("FAILED"). Now, while infrared bytes go back and forth, each frame ('F') carries how many game ms
 * the sender has run; the one that is more than LK_SLACK ms ahead waits here for the other (at most 500 ms without a
 * frame from it). Without bytes for 2 s each runs on its own again. */
#define LK_SLACK 3
static volatile u32 g_ms;
static u8 g_irtx[64];                     /* [0..3] game ms run, then the bytes */
static u32 g_lk_run, g_lk_off, g_lk_peer, g_lk_act; static u8 g_lk_on;
__attribute__((minsize)) void tc_ir_out(u8 b) { g_lk_act = g_ms; if (g_irtxn < 60u) g_irtx[4 + g_irtxn++] = b; }   /* a few bytes per ms: sent after each game ms */
__attribute__((noinline, minsize)) static void ir_got(u8 ty, const u8 *d, int n)
{
  if (ty != 'F') return;                       /* our frames carry at least the 4 bytes */
  u32 p; memcpy(&p, d, 4);
  if (!g_lk_on) { g_lk_on = 1; g_lk_off = p - g_lk_run; }   /* a new exchange: both counts from here */
  g_lk_peer = p;
  if (n > 4) g_lk_act = g_ms;
  for (int i = 4; i < n; i++) if ((u16)(g_irt - g_irh) < 512u) { g_irq[g_irt & 511u] = d[i]; g_irt++; }
}
/* after each game ms */
__attribute__((noinline, minsize)) static void ir_link_tick(u32 now)
{
  g_lk_run++;
  if (now - g_lk_act >= 2000u) g_lk_on = 0;
  else { memcpy(g_irtx, &g_lk_run, 4); plink_send('F', g_irtx, 4 + g_irtxn); g_irtxn = 0; }
  do { plink_poll(g_ms, 0, ir_got); hw_wdt_feed(); }
  while (g_lk_on && g_ms - g_pl.last_rx < 500u && (int32_t)(g_lk_run + g_lk_off - g_lk_peer) > LK_SLACK);
}
#else
static u8 g_irtx[60];
__attribute__((minsize)) void tc_ir_out(u8 b) { g_irtx[g_irtxn++] = b; if (g_irtxn >= sizeof g_irtx) { plink_send('E', g_irtx, g_irtxn); g_irtxn = 0; } }
__attribute__((noinline, minsize)) static void ir_got(u8 ty, const u8 *d, int n)
{
  if (ty != 'E') return;
  for (int i = 0; i < n; i++) if ((u16)(g_irt - g_irh) < 512u) { g_irq[g_irt & 511u] = d[i]; g_irt++; }
}
__attribute__((noinline, minsize)) static void ir_link_tick(u32 now) { if (g_irtxn) { plink_send('E', g_irtx, g_irtxn); g_irtxn = 0; } plink_poll(now, 0, ir_got); }
#endif
#endif
#ifdef LK_SYNC
#define PARA_ATTR __attribute__((section(".initcode"), minsize))   /* 1.7.1: start code, small (room for LK_SYNC) */
#else
#define PARA_ATTR __attribute__((section(".initcode")))
#endif
#ifdef EMU_OVL
/* 1.6.13 test10: code that only runs in the settings or when leaving lives in the overlay at the end of the program
 * image, inside the translator's room: ovl_load copies it back from the flash when it is needed (the translator starts
 * over afterwards). The translator gets that much more room. */
#ifdef LK_SYNC
#define OVL_FN __attribute__((section(".ovltext"), noinline, minsize))   /* 1.7.1: settings and leaving, small */
#else
#define OVL_FN __attribute__((section(".ovltext"), noinline))
#endif
#define OVL_RO __attribute__((section(".ovlrodata")))
#define PB_ATTR __attribute__((section(".ovltext")))
#else
#define OVL_FN
#define OVL_RO
#endif
#include "paraclock.h"
#ifdef PARA_BACK
#ifdef EMU_TONE_Q
#define PB_MARK_ATTR __attribute__((section(".initcode"), minsize))   /* test20: only at the start (in the PRAM image) */
#endif
#include "paraback.h"
#ifdef BAT_GUARD
#define BAT_PIC_ATTR OVL_FN __attribute__((minsize))   /* test23: in the overlay, PRAM is full */
#include "batt.h"
static volatile u8 g_bat_go;
#endif
#endif
uint32_t jit_used(void);

/* Settings, filled in by the ROM builder (magic "PMXC"). */
typedef struct { u32 magic, version, guest_off, guest_size, save_off, device; u8 map[128]; u32 spare_end, flags; } PmxCfg;
/* version 4 (1.5.1): flags bit 1 = at every start the guest clock is set to the Paradise's time */
/* version 3 (v0.9.2): spare_end = end of the guest area; the room behind the packed firmware holds spare pages */
__attribute__((section(".pmxcfg"), used))
volatile const PmxCfg g_cfg = { 0x43584D50u, 1, 0x940000u, 0x400000u, 0xC0000u, 0,
  { [0 ... 63] = 0xFE, [64 ... 127] = 0xFF }, 0, 0 };   /* 0xFE: linear (old layout) */
static u32 g_para, g_para_cnt;          /* the Paradise's time at the start (seconds since 1970, 0 = not found), its counter then */
static int para_hard(void) { return g_cfg.version >= 4u && (g_cfg.flags & 2u) && g_para; }

static uint16_t g_gram[GRAM_H][GRAM_W] __attribute__((aligned(4)));   /* also a flash source (para_back_fix): the DMA takes words */
static Emu g_emu;
#define NSLOT 2
static uint8_t g_pool[NSLOT * OVL_SIZE] __attribute__((aligned(4)));   /* flash DMA source */
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
static u32 g_run_ms, g_hold_ms;           /* see clock_sync */

#define SYST_CSR REG(0xE000E010u)
#define SYST_RVR REG(0xE000E014u)
#define SYST_CVR REG(0xE000E018u)
typedef int32_t s32;
#define CORE_HZ 96000000u
static void systick_init(void){ SYST_RVR = CORE_HZ/1000u - 1u; SYST_CVR = 0; SYST_CSR = 7; }
#ifdef EMU_PW
/* ---- 1.6.13 test10: power saving while the guest sleeps (as T7Emu T7_PW and T18Emu T18_PW) ----
 * Screen dark and the guest asleep on its 32 kHz clock (it only counts its time): the chip runs from its internal
 * 12 MHz RC with PLL and crystal off (datasheet: 1.6 mA instead of 9 to 12 mA) and goes to deep sleep (0.4 mA) once
 * the emulation has caught up; the RTC (1 s, set by the Paradise) or a key wakes it, the ms tick continues from the
 * RTC. The guest's sleeping time then passes in a tight loop (pw_steps), never dropped. */
#define OSC_CTRL REG(0x45000000u)
#define OSC_STAT REG(0x45000008u)
#define SYS_CLKCFG REG(0x4500000Cu)
#define SYS_AHBCP REG(0x45000010u)
#define ISO_CTRL REG(0x45000200u)
#define PMU_CTRL REG(0x45000300u)
#define GP0(o) REG(0x40018000u + (o))
#define NVIC_ISER0 REG(0xE000E100u)
#define NVIC_ICER0 REG(0xE000E180u)
#define NVIC_ICPR0 REG(0xE000E280u)
#define PW_KEYS 0xF00u                     /* P0.8 wheel, P0.9 A, P0.10 C, P0.11 B */
#define PW_IRQS ((1u << 2) | (1u << 26))   /* RTC, GPIO0 */
static int g_pw_low;
static u32 g_pw_rtc, g_pw_rtcms, g_pw_skip;
static volatile u8 g_pw_rtcw;              /* the RTC woke the chip (its interrupt ran) */
static u8 g_pw_kw;                         /* woken by a key before the next RTC second: that part of a second is not counted yet */
__attribute__((used, noinline)) void pw_rtc_irq(void) { PMU_CTRL = (PMU_CTRL & ~0x47u) | 0x10u; g_pw_rtcw = 1; }   /* RTC_FLAG: write 1 */
__attribute__((used, noinline)) void pw_gpio_irq(void) { GP0(0x20) = PW_KEYS; }
__attribute__((noinline, minsize)) static void pw_full(void)
{
  if (!g_pw_low) return;
  OSC_CTRL |= 1u << 4;                                          /* crystal on (the PLL's source, as hw_init sets it) */
  for (u32 t = 0; !(OSC_STAT & (1u << 4)) && t < 2000000u; t++) {}
  OSC_CTRL |= 1u << 15;                                         /* PLL on */
  for (u32 t = 0; !(OSC_STAT & (1u << 6)) && t < 2000000u; t++) {}
  SYS_CLKCFG = 4u;                                              /* system clock from the PLL */
  for (u32 t = 0; ((SYS_CLKCFG >> 4) & 7u) != 4u && t < 2000000u; t++) {}
  SYST_RVR = CORE_HZ / 1000u - 1u; SYST_CVR = 0;
  g_pw_low = 0;
}
__attribute__((noinline, minsize)) static void pw_low(void)
{
  if (g_pw_low) return;
  SYS_AHBCP = 0;
  SYS_CLKCFG = 0;                                               /* system clock from the IHRC (12 MHz) */
  for (u32 t = 0; ((SYS_CLKCFG >> 4) & 7u) != 0u && t < 2000000u; t++) {}
  if (((SYS_CLKCFG >> 4) & 7u) != 0u) { SYS_CLKCFG = 4u; return; }   /* did not switch: stay as it was */
  OSC_CTRL &= ~((1u << 15) | (1u << 4));                        /* PLL and crystal off */
  SYST_RVR = 12000000u / 1000u - 1u; SYST_CVR = 0;
  g_pw_low = 1;
}
#ifdef EMU_TONE_Q
/* test20: tone queue (as T18_TONE_Q): changes with their game time, played on the real clock 150 ms after the first of
 * a sequence; late by more than 200 ms the timing starts anew. A tone always at the full clock (the buzzer's timer
 * counts the system clock: at 12 MHz a call began as a long low tone). */
#ifdef EMU_TQ_DROP
#define TQ_N 64u   /* test22: a call melody caught up from seconds behind fits */
static u32 tq_drop;   /* game ms dropped so far: the queue's game time stays continuous */
#else
#define TQ_N 32u
#endif
#define TQ_LAT 250u
static u32 tq_t[TQ_N]; static u16 tq_hz[TQ_N]; static u32 tq_h, tq_w, tq_g0, tq_r0, tq_on, tq_last;
static void pw_full(void);
__attribute__((minsize)) static void tq_put(u32 hz, u32 gms) {
#ifdef EMU_TQ_DROP
  gms -= tq_drop;
#endif
  if (tq_w - tq_h >= TQ_N) tq_h++; tq_t[tq_w % TQ_N] = gms; tq_hz[tq_w % TQ_N] = (u16)(hz > 0xFFFFu ? 0xFFFFu : hz); tq_w++; }
__attribute__((noinline, minsize)) static void tq_pump(void)
{
  u32 now = g_ms;
  while (tq_h != tq_w) {
    u32 i = tq_h % TQ_N;
    if (!tq_on) { tq_on = 1; tq_g0 = tq_t[i]; tq_r0 = now + TQ_LAT; }
    s32 late = (s32)(now - (tq_r0 + (tq_t[i] - tq_g0)));
    if (late < 0) return;
    if (late > 200) { tq_g0 = tq_t[i]; tq_r0 = now; }
    u32 hz = tq_hz[i];
    if (hz) { pw_full();
#ifdef CLK_ECO
      if (g_hw_clk_sh) hw_clk_shift(0, CORE_HZ);   /* the lower eco clock would lower the tone as well */
#endif
    }
    hw_tone(hz); tq_h++; tq_last = now;
  }
  if (!g_hw_tone_hz && now - tq_last > 1000u) tq_on = 0;   /* quiet for a second: the next note starts a new sequence */
}
#endif
/* deep sleep until the next RTC second or a key; returns the seconds that passed */
__attribute__((noinline, minsize)) static u32 pw_sleep(void)
{
  u32 iso = ISO_CTRL;
  if (!(iso & 0x80u) || (iso & 7u) != 0u) { __asm__ volatile("wfi"); return 0; }   /* the RTC is not on 1 s: no deep sleep */
  ISO_CTRL = iso & ~8u;                                         /* RTC_WKP 0: the RTC interrupt wakes */
  GP0(0x0C) &= ~PW_KEYS; GP0(0x10) |= PW_KEYS; GP0(0x20) = PW_KEYS; GP0(0x18) |= PW_KEYS;   /* edges, both */
  NVIC_ICPR0 = PW_IRQS; NVIC_ISER0 = PW_IRQS;
  u32 r0 = REG(0x45000304u), csr = SYST_CSR;
  SYST_CSR = 0;
  g_pw_rtcw = 0;
  PMU_CTRL = (PMU_CTRL & ~0x47u) | 2u;                          /* DSLEEP_EN */
  __asm__ volatile("dsb\n wfi\n isb" ::: "memory");
  PMU_CTRL = PMU_CTRL & ~0x47u;
  NVIC_ICER0 = PW_IRQS; GP0(0x18) &= ~PW_KEYS; GP0(0x20) = PW_KEYS;
  ISO_CTRL = (ISO_CTRL & ~8u) | (iso & 8u);
  SYST_CVR = 0; SYST_CSR = csr;
  /* woken by the RTC: wait until the counter (32 kHz domain) shows the new second (as T18Emu test3) */
  u32 r = REG(0x45000304u);
  if (g_pw_rtcw) for (u32 t = 0; r == r0 && t < 200000u; t++) r = REG(0x45000304u);
  u32 n = r - r0;
  if (!n) g_pw_kw = 1;
  return n > 3600u ? 0u : n;
}
#endif

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
#ifdef APP_SELF
/* test5: where this program's image lies in the flash. A further TamaEmu of the menu has its image right behind its
 * guest area (spare_end), not at 0x0A0000; checking against 0x0A0000 copied the main TamaEmu's settings over its own
 * and the other ROM started (Bernd: iD L and P's, always the one added first). */
static u32 app_flash(void){
  if(g_cfg.version>=3u){ u32 a=g_cfg.spare_end;
    if(a && a<0x1000000u && !(a & 0xFFFFu) && a!=0xA0000u){
      volatile const u32 *c=(volatile const u32*)(0x60000000u + a + 0x140u);
      if(c[0]==0x43584D50u && c[2]==g_cfg.guest_off && c[4]==g_cfg.save_off) return 0x60000000u + a; } }
  return APP_FLASH;
}
#else
#define app_flash() APP_FLASH
#endif
static u32 pram_check(u32 end, int fix){
  volatile u32 *dst=(volatile u32*)g_zero;
  volatile const u32 *src=(volatile const u32*)app_flash();
  if(src[0]!=dst[0] || src[1]!=dst[1]) return 0xFFFFFFFFu;   /* window does not read the image */
  u32 n=0;
  for(u32 i=0;i<end/4u;i++){
    u32 v=src[i];
#ifdef APP_SELF
    if(i>=0x140u/4u && i<(0x140u+sizeof(PmxCfg))/4u) continue;   /* its own settings stay, whatever the image there says */
#endif
    if(dst[i]!=v){ if(!g_pc_n && !n) g_pc_first=i*4u; g_pc_last=i*4u; n++; if(fix) dst[i]=v; }
  }
  if(n && fix){ __asm__ volatile("dsb\n isb" ::: "memory"); }
  return n;
}
#ifdef EMU_OVL
extern uint8_t __ovl_start[], __ovl_end[];
void jit_pram_load(uint32_t dst, const void *src, uint32_t n);
/* the overlay back from this program's image in the flash (the translator wrote its code over it); the translator
 * then starts over */
__attribute__((noinline, minsize)) static void ovl_load(void){ jit_pram_load((u32)__ovl_start, (const void *)(app_flash() + (u32)__ovl_start), (u32)(__ovl_end - __ovl_start)); g_emu.jit_flush = 1; }
#endif
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

#ifdef BAT_GUARD
/* test21: battery empty: everything saved (guest flash, run state), then off until new batteries (BAT_GUARD) */
OVL_FN static void bat_end(void){
  st_save(&g_emu, &g_sc, g_gram);
  BAT_PIC_SHOW();
  hw_backlight(0);
  for(;;){ hw_wdt_feed(); __asm__ volatile("wfi"); }
}
#endif
__attribute__((minsize)) static void leave_go(int save){
#ifdef EMU_PW
  pw_full();
#endif
#ifdef CLK_ECO
  hw_clk_shift(0, CORE_HZ);
#endif
  hw_tone(0);
#ifdef EMU_OVL
  ovl_load();
#endif
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
#ifdef PARA_BACK
  para_back_fix((uint8_t *)g_gram);       /* 1.6.8: the Paradise skips the time spent here */
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
#ifdef EMU_CARE
/* test30: care and Gotchi Points in the settings, per firmware (its reset vector), found in the simulation and checked
 * on the game's status pages: hunger (raised to its full value), happy/love (P's ring, 4U love), Gotchi Points (u32) */
typedef struct { u32 rv; u16 hun, hap, pts, med; u8 hmax, mul, add, mask; } EmuCare;   /* 0x8xxx: dstram 0x84xxx, else a0ram */
/* test32: the happy bar (iD L: friend level, Plus Color: HAPPY!, P's: ring, 4U: love) only as far as the game itself
 * lets it grow, read from each firmware's own code: mul * (medals in med, bits of mask) + add; mask 0 (4U): 100 once
 * the stage byte med is above 2, else 30 */
OVL_RO static const EmuCare CARE[] = {
  { 0x00C2F0ECu, 0x8125u, 0x812Cu, 0x8138u, 0x815Eu, 4, 10, 10, 0x7F },   /* Plus Color v040 */
  { 0x024CA8FEu, 0x8225u, 0x8228u, 0x8238u, 0x826Cu, 4, 20, 19, 0x0F },   /* iD L v050 */
  { 0x024CA942u, 0x8225u, 0x822Cu, 0x8238u, 0x8270u, 4, 20, 19, 0x0F },   /* iD L v050 (2011-02-24) */
  { 0x024CAA72u, 0x8225u, 0x822Cu, 0x8238u, 0x8270u, 4, 20, 19, 0x0F },   /* iD L v053 (English) */
  { 0x024BD18Au, 0x8201u, 0x8208u, 0x8214u, 0x8250u, 4, 20, 19, 0x0F },   /* iD L v054 (15th) */
  { 0x024C14F6u, 0x8201u, 0x8208u, 0x8214u, 0x8250u, 4, 20, 19, 0x0F },   /* iD L v055 (Princess Spacy) */
  { 0x024FDFE6u, 0x12ACu, 0x12B4u, 0x12C0u, 0x12FAu, 4, 4, 4, 0x0F },     /* P's */
  { 0x024F6256u, 0x11A1u, 0x11A8u, 0x11B4u, 0x11E2u, 6, 0, 0, 0 },        /* 4U+ */
  { 0x024C680Au, 0x12A9u, 0x12B0u, 0x12BCu, 0x12EAu, 6, 0, 0, 0 },        /* 4U v001 */
};
OVL_FN static const EmuCare *care_find(Emu *e){ u32 v=mem_read32(e, e->dev.ttbr_reset); for(u32 i=0;i<sizeof CARE/sizeof CARE[0];i++) if(CARE[i].rv==v) return &CARE[i]; return 0; }
OVL_FN static u8 *care_p(Emu *e, u32 a){ return (a & 0x8000u) ? e->dstram+(a & 0x7FFFu) : e->a0ram+a; }
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
static u32 g_btn_ms;                      /* last key (GFLUSH_IDLE) */
#ifdef HW_OPTS
static u32 g_scr_ms; static int g_scr_dark;   /* 1.6.8: screen off after (Studio), off now */
static const u16 OPT_SM[8]={0,10,20,30,60,120,300,600};   /* screen off after, seconds */
#ifdef EMU_DIM
/* 1.6.13 test10: dimming as in T7Emu and T18Emu (bits 12-14 dim after, 8-11 dim to, set on the device): the light goes
 * down to the chosen percent after that time without a key; the first key then only brings the light back */
static u32 g_dim_ms, g_dim_t; static int g_dimd;
#endif
__attribute__((minsize)) static void opt_apply(u32 f){ g_hw_vol=(0x0312u>>(((f>>16)&3u)*4u))&15u; g_hw_dim=(f>>18)&3u; g_scr_ms=OPT_SM[(f>>20)&7u]*1000u;
#ifdef EMU_DIM
  g_dim_ms=OPT_SM[(f>>12)&7u]*1000u; g_dimd=0;
#endif
#ifdef BL_PWM
  hw_bl_set(opt_light_pct(f));   /* 1.6.8: the light, not the colours */
#endif
}
#ifdef OPT_MENU
/* 1.6.8: settings changed on the device (wheel short), in the last sector of the save area: magic, the Studio's
 * bits when they were changed, the bits, check. The Studio's bits win again once they are changed there. */
#define OPT_SEC (g_cfg.save_off+0x4F000u)
#define OPT_MAGIC 0x4F584D50u   /* "PMXO" */
static u32 g_opt, g_optsec[4];
static u32 opt_studio(void){ return (g_cfg.version>=4u ? g_cfg.flags : 0u) & 0xF7F0000u; }
#ifdef EMU_DIM
/* the device-only bits (dim 8-14, language 0-1) stay when the Studio's bits were changed */
static void opt_load(void){ u32 *s=g_optsec; flash_read(OPT_SEC, s, 16);
  int ok = s[0]==OPT_MAGIC && s[3]==(s[0]^s[1]^s[2]) && !(s[2]&~0xF7F7F03u);
  g_opt = ok && s[1]==opt_studio() ? s[2] : opt_studio() | (ok ? s[2] & 0x7F03u : 0u); }
#else
static void opt_load(void){ u32 *s=g_optsec; flash_read(OPT_SEC, s, 16);
  g_opt = (s[0]==OPT_MAGIC && s[1]==opt_studio() && s[3]==(s[0]^s[1]^s[2]) && !(s[2]&~0xF7F0000u)) ? s[2] : opt_studio(); }
#endif
#ifdef ASK_LANG
/* test10: the language of the settings, set on the device (bits 0-1: 0 as the Studio, 1 German, 2 English) */
#define EMU_EN() ((g_opt & 3u) ? (g_opt & 3u) == 2u : (g_cfg.flags & 1u) != 0u)
#endif
OVL_FN static void opt_store(void){ u32 *s=g_optsec; s[0]=OPT_MAGIC; s[1]=opt_studio(); s[2]=g_opt; s[3]=s[0]^s[1]^s[2];
  hw_tone(0); flash_prog4k(OPT_SEC, s); }   /* the DMA takes 4 KB from here: only the first 16 bytes count */
#endif
#endif
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
/* v0.9.3: keep the guest clock on the Paradise's own seconds counter. g_run_ms: ms the guest ran, plus the
 * dropped ones already added in the main loop (1.6.8). Called every 1024 ms. */
#ifdef EMU_DATE_FIX
/* test20: the games keep the date in their own RAM (iD L: year, month, day, hour, minute at dstram 0x27E) and take only
 * the time from the clock chip: with the clock from the Paradise the time was right, the date stayed the game's own
 * (1/2 instead of 10/26). The clock record is found by the time the game copied in (hour and minute as on the chip,
 * a month and a day before them); found exactly once, its month and day are set. */
static u32 g_date_n;
__attribute__((noinline, minsize)) static int date_fix(Emu *e){
  const u8 *io = e->ioram - e->dev.io_base;
  #define BCD(x) (((x) >> 4) * 10u + ((x) & 15u))
  u32 h = BCD(io[0x301918]), mi = BCD(io[0x301914]), d = BCD(io[0x30191C]), mo = BCD(io[0x301920]);
  u8 *b = e->dstram; u32 n = 0, at = 0;
  for (u32 i = 2; i + 1 < e->dev.dstram_size; i++)
    if (b[i] == h && b[i + 1] == mi && b[i - 1] >= 1u && b[i - 1] <= 31u && b[i - 2] >= 1u && b[i - 2] <= 12u) { n++; at = i; }
  if (n != 1) return 0;
  b[at - 2] = (u8)mo; b[at - 1] = (u8)d;
  return 1;
}
#endif
__attribute__((noinline, minsize)) static void clock_sync(Emu *e){
  u32 run_ms = g_run_ms;
#ifdef BAT_GUARD
  if (bat_guard_tick()) g_bat_go = 1;
#endif
#ifdef EMU_DATE_FIX
  if (g_date_n < 150u && para_hard() && g_cfg.device == 1u) {   /* iD L only (checked there) */ g_date_n++; if (date_fix(e)) g_date_n = 150u; }
#endif
  static u32 r0, m0, have; static s32 owe;
  u32 r = REG(0x45000304u);
#ifndef CLOCK_DOUBLE
  if(REG(0x45000200u) & 7u) have=0;   /* 1.6.8 (Sonix SDK): 0x45000304 counts RTC periods, seconds only with timeout 0 (1 s) */
  else
#endif
  if(!have){ r0=r; m0=run_ms; have=1; }
  else if(r - r0 >= 30u){
    u32 dr = r - r0, dm = run_ms - m0;
    if(dr <= dm/1000u + 600u && dm/1000u <= dr + 5u){      /* plausible: no jump of the counter */
      owe += (s32)(dr*1000u) - (s32)dm;
      if(owe >= 1000){ u32 n=(u32)owe/1000u; periph_rtc_advance(e, n); owe -= (s32)(n*1000u); }
#ifndef CLOCK_DOUBLE
      /* 1.6.8: tick fast (the core clock comes from the internal RC, up to 2 %): the guest clock ran ahead.
       * Hold the guest back for the extra time, 1 ms in 16, instead of letting it run fast for good. */
      if(owe <= -1000){ u32 n=(u32)(-owe)/1000u; g_hold_ms += n*1000u; owe += (s32)(n*1000u); }
      if(g_hold_ms > 60000u) g_hold_ms = 60000u;
#else
      if(owe < -2000) owe = -2000;                            /* tick fast: nothing to take back */
#endif
    }
    r0=r; m0=run_ms;
  }
}
#ifdef EMU_PW
static u32 g_cy_rest;                     /* rest of the guest cycles per ms (see the main loop) */
/* one ms of the sleeping guest, as in the main loop: its cycles, its clock */
__attribute__((noinline)) static void pw_step(Emu *e){
  u32 mhz = e->cmu.mclk_hz ? e->cmu.mclk_hz : e->dev.osc3_hz;
  u32 t = mhz + g_cy_rest, cy_ms = t / 1000u; g_cy_rest = t % 1000u;
  tc_run(e, e->cycles + cy_ms);
#ifdef RTC_MS
  if(g_hold_ms && ((g_run_ms + 1u) & 15u)==0u) g_hold_ms--; else periph_rtc_ms(e);
#endif
  if(((++g_run_ms) & 1023u)==0u) clock_sync(e);
}
#endif
/* 1.6.9: everything up to the translator runs once, from the startup area (.initcode, later the translator's):
 * frees PRAM for the infrared link */
#ifdef EMU_OVL
__attribute__((section(".initcode"), noinline, minsize))
#else
__attribute__((section(".initcode"), noinline))
#endif
static void boot(void){
  u32 swf=0, swk=0, swn=0;
  /* the Paradise's clock, before its SRAM is overwritten (the stack is in the top 4 KB) */
  u32 pcnt=REG(0x45000304u), para=para_clock(pcnt, 0, 0x1F000u, 0, 0);
  if(!para) para=para_mailbox(pcnt);   /* 1.6.8: started from the menu */
#ifdef PARA_FLASH
  if(!para) para=para_flash(pcnt);   /* 1.6.9 test37 */
#endif
#ifdef PARA_BACK
  para_leave_mark(pcnt, para);         /* 1.6.8: the Paradise does not live meanwhile */
#endif
  __asm__ volatile("" ::: "memory");
#ifndef NOSRAMWATCH
  swn=sram_watch(&swf, &swk);
#endif
  for(u32* p=&__bss_start; p<&__bss_end; p++) *p=0;
  { extern u32 __data_start, __data_end, __data_load;     /* initial data values into SRAM */
    const u32 *src=&__data_load; for(u32* p=&__data_start; p<&__data_end; p++) *p=*src++; }
  g_para=para; g_para_cnt=pcnt;
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
  /* 1.6.8: 4U/4U+ get an answering NFC chip (flags bit 2 switches it off again) */
  if(g_cfg.device>=3u && g_cfg.device<=4u && !(g_cfg.flags & 4u)) e->dev.nfc_pn512=true;
#ifdef TC_IRLINK
  /* 1.6.9: infrared to another Paradise over the contacts (iD L: session through its context pointer, P's: its mode byte) */
  if(g_cfg.device==1u){ tc_ir_setup(0, 0x84350u); plink_init(); }
  else if(g_cfg.device==2u){ tc_ir_setup(0xDB3u, 0); plink_init(); }
#endif
#ifdef HW_OPTS
  /* 1.6.8: volume (bits 16-17: 0 normal, 1 quiet, 2 loud, 3 off), brightness (18-19), screen off after (20-23) */
#ifdef OPT_MENU
  opt_load(); opt_apply(g_opt);
#else
  opt_apply(g_cfg.version>=4u ? g_cfg.flags : 0u);
#endif
#endif
  tc_init(e, 0, g_gram, g_pool, NSLOT);
  tc_ovl_evict = st_evict; tc_big_erase = st_big_erase; st_init(&g_sc, g_pool);
#ifdef OVL_RAM20
  ovl_extra(e);
#endif
  setup_rom(e);
  cpu_reset(e);
  if(para_hard()) periph_rtc_set(e, g_para + (REG(0x45000304u) - g_para_cnt));   /* fresh start: the Paradise's time too */
  g_stage=3; diag(e,0);
  { u32 pause=0;
    if(st_load(e, &g_sc, g_gram, &pause)){
      g_restored=1+pause;
#ifdef SESS_ONCE
      st_drop(&g_sc);   /* 1.6.8: used once; power gone without leaving: next start from the Tama's own save */
#endif
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
      if(para_hard()) periph_rtc_set(e, g_para + (REG(0x45000304u) - g_para_cnt));   /* the Paradise's time, nothing caught up */
      else { if(pause > 400u*86400u) pause=400u*86400u; periph_rtc_advance(e, pause); }
      e->lcd.dx0=0; e->lcd.dy0=0; e->lcd.dx1=127; e->lcd.dy1=127; LCD_ALL();
    }
  }
  st_pages(e, &g_sc);                     /* spare pages for written empty blocks (after a restore as well) */
}
#if defined(OPT_MENU) && defined(EMU_OVL)
/* 1.6.8: wheel short: settings (the game stands still meanwhile); 1.6.13 test10: from the overlay */
OVL_FN static void menu_run(Emu *e){
#ifdef EMU_CARE
#ifdef ASK_LANG
        const EmuCare *c=care_find(e); int en=EMU_EN();
        const char *cx[2]={ c ? "GOTCHI POINTS" : 0, c ? "PFLEGE VOLL|FULL CARE" : 0 };
        u32 n=hw_opts_menu_xn(g_opt, en, cx, 2);
        if(n & 3u) en=(n & 3u)==2u;   /* the language may have been changed in the menu */
#else
        const EmuCare *c=care_find(e); int en=(int)(g_cfg.flags & 1u);
        const char *cx[2]={ c ? "GOTCHI POINTS" : 0, c ? (en ? "FULL CARE" : "PFLEGE VOLL") : 0 };
        u32 n=hw_opts_menu_xn(g_opt, en, cx, 2);
#endif
        if(n & 0x80000000u){ u8 *p=care_p(e,c->pts);
          if(n & (1u<<28)){ u8 *h=care_p(e,c->hun); if(*h<c->hmax) *h=c->hmax; { u32 m=*care_p(e,c->med), cap; if(c->mask){ m&=c->mask; cap=0; while(m){ cap+=c->mul; m&=m-1u; } cap+=c->add; } else cap=m>2u?100u:30u; h=care_p(e,c->hap); if(*h<cap) *h=(u8)cap; } }
          else { u32 v=p[0]|(u32)p[1]<<8|(u32)p[2]<<16; if(hw_num_edit("GOTCHI POINTS",&v,5,99999u,en)){ p[0]=(u8)v; p[1]=(u8)(v>>8); p[2]=(u8)(v>>16); p[3]=0; } }
          n&=0x0FFFFFFFu; }
#else
        u32 n=hw_opts_menu(g_opt, (int)(g_cfg.flags & 1u));
#endif
        if(n!=g_opt){ g_opt=n; opt_apply(n); opt_store(); }
}
#endif
/* test26 CLK_ECO: the main loop for size (the 64 KB were full); the hot paths (core, JIT, LCD push) stay -O2 */
#ifdef CLK_ECO
__attribute__((minsize))
#endif
void Reset_Handler(void){
  __asm__ volatile("cpsid i");
  boot();
  Emu *e=&g_emu;
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
  g_btn_ms=g_ms;
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
#ifdef BAT_GUARD
    if(g_bat_go){ pw_full();
#ifdef CLK_ECO
      hw_clk_shift(0, CORE_HZ);
#endif
      hw_tone(0); ovl_load(); bat_end(); }
#endif
#ifdef EMU_PW
    /* screen dark, no key, tone or link traffic: 12 MHz from the internal RC, deep sleep once caught up (below). The
     * guest's time runs on behind (nobody sees it); more than 2.5 s behind: full clock until caught up */
    int pw_quiet = !g_bl && !g_hw_tone_hz && !periph_tone_hz(e) && !wheel_ms
#ifdef EMU_TONE_Q
      && tq_h == tq_w
#endif
#ifdef TC_IRLINK
      && !(g_pl.last_rx && now-g_pl.last_rx<30000u) && !g_irtxn
#endif
      && !hw_buttons();
    if(pw_quiet && (s32)(now-emu_ms)<2500){   /* after a deep sleep 1 s behind: caught up at 12 MHz */
#ifdef CLK_ECO
      if(!g_pw_low) hw_clk_shift(0, CORE_HZ);
#endif
      pw_low();
    } else pw_full();
    { u32 r=REG(0x45000304u);                                                      /* when the RTC last stepped */
      if(r!=g_pw_rtc){
        /* after a wake by a key the part of a second slept is still missing: the step comes exactly whole seconds
         * after the last one measured */
        if(g_pw_kw){ u32 t=g_pw_rtcms + (r-g_pw_rtc)*1000u;
          __asm__ volatile("cpsid i" ::: "memory"); if((s32)(t-g_ms)>0) g_ms=t; __asm__ volatile("cpsie i" ::: "memory");
          now=g_ms; g_pw_kw=0; }
        g_pw_rtc=r; g_pw_rtcms=now; } }
    /* the screen dark: the guest's time passes in a tight loop up to the last ms (that one runs the whole loop: keys,
     * light, saves), within 40 ms of real time; up to 5 s behind before time is dropped */
    if(pw_quiet){ u32 t1=g_ms;
      while((s32)(now-emu_ms)>=2 && g_ms-t1<40u && !e->stopped
#ifdef EMU_TONE_Q
            && !periph_tone_hz(e)   /* test20: a tone (a call) begins: out to the main loop, which plays it at full clock */
#endif
            ){ pw_step(e); emu_ms++; g_pw_skip++; } }
    if((s32)(now-emu_ms) > (g_bl ? 100 : 5000)){
#else
    if((s32)(now-emu_ms) > 100){
#endif                                  /* too far behind: drop time */
      u32 d=now-emu_ms-100; dropped += d; emu_ms = now-100;
#ifdef EMU_TQ_DROP
      tq_drop += d;   /* test22: else the note before the jump lasted as long as the jump (the long tone at a call) */
#endif
      { static u32 lost; lost += d; if(lost>=1000u){ periph_rtc_advance(e, lost/1000u); lost%=1000u; } }   /* guest clock stays correct anyway */
#ifndef CLOCK_DOUBLE
      g_run_ms += d;   /* 1.6.8: those ms are paid already, the comparison with the seconds counter below must not add them a second time */
#endif
    }
    if(emu_ms == now){                     /* caught up: sleep until the next tick */
#ifdef EMU_TONE_Q
      tq_pump();
#endif
      idle += SYST_CVR;
      /* Only sleep once the SysTick interrupt is known to arrive. Otherwise poll the
       * counter directly (COUNTFLAG) so the loop never gets stuck in wfi. */
      { static u32 g0=0xFFFFFFFFu; static int tick_ok;
        if(g0==0xFFFFFFFFu) g0=g_ms;
        if(!tick_ok && g_ms!=g0) tick_ok=1;
#ifdef EMU_PW
        if(tick_ok && pw_quiet && g_pw_low && !hw_buttons()){
          /* deep sleep until the next RTC second or a key; the ms tick then continues from the time of the last RTC step */
          if(pw_sleep()){ u32 r=REG(0x45000304u), t=g_pw_rtcms + (r-g_pw_rtc)*1000u;
            __asm__ volatile("cpsid i" ::: "memory"); if((s32)(t-g_ms)>0) g_ms=t; __asm__ volatile("cpsie i" ::: "memory");
            g_pw_rtc=r; g_pw_rtcms=t; }
        } else
#endif
        if(tick_ok) __asm__ volatile("wfi");
        else { u32 m=g_ms; if((SYST_CSR & 0x10000u) && g_ms==m) g_ms++; } }
      continue;
    }
#if !defined(CLOCK_DOUBLE) && !defined(RTC_MS)
    if(g_hold_ms && (emu_ms & 15u)==0u){ g_hold_ms--; emu_ms++; continue; }   /* guest clock ahead: one ms off */
#endif
    /* one millisecond of guest time */
    u32 mhz = e->cmu.mclk_hz ? e->cmu.mclk_hz : e->dev.osc3_hz;
    /* v0.9.3: carry the rest of the division. With the 32768 Hz clock (the guest sleeping)
     * mhz/1000 gave 32 instead of 32.768 cycles per ms: the guest clock lost 2.3 percent. */
#ifdef EMU_PW
    u32 cy_ms; { u32 t = mhz + g_cy_rest; cy_ms = t / 1000u; g_cy_rest = t % 1000u; }
#else
    static u32 cy_rest;
    u32 cy_ms; { u32 t = mhz + cy_rest; cy_ms = t / 1000u; cy_rest = t % 1000u; }
#endif
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
#ifdef CLK_ECO
    ECO_CHECK(g_ms, g_ms-emu_ms, CORE_HZ);
#endif
#ifdef TC_IRLINK
    ir_link_tick(g_ms);
#endif
#ifdef RTC_MS
    /* 1.6.8: the guest's clock counts real milliseconds (not guest cycles). Guest clock ahead of the
     * Paradise's crystal (g_hold_ms from clock_sync): one ms in 16 is not counted. */
    if(g_hold_ms && (emu_ms & 15u)==0u) g_hold_ms--; else periph_rtc_ms(e);
#endif
    /* v0.9.3: keep the guest clock on the Paradise's own seconds counter (crystal). If the
     * millisecond tick runs slow, or busy scenes (shop, flash writes) make the emulator drop
     * time, the missing seconds are added to the guest clock. */
    if(((++g_run_ms) & 1023u)==0u) clock_sync(e);
#ifndef RTC_KEEP_GUEST
    /* 1.6.8: with the clock taken from the Paradise, a time the game sets itself (its first setup, after a reset)
     * is replaced by the Paradise's time once the game has not touched its clock for 2 s */
    { static u32 wr0, wr_ms;
      if(g_tc_rtc_wr!=wr0){ wr0=g_tc_rtc_wr; wr_ms=emu_ms?emu_ms:1; }
      else if(wr_ms && emu_ms-wr_ms>2000u){ wr_ms=0; if(para_hard()) periph_rtc_set(e, g_para + (REG(0x45000304u) - g_para_cnt)); } }
#endif
    /* Guest save into the Paradise flash: writing briefly stalls everything.
     * So only once nothing has been written for 3 s AND the screen has been still
     * for a moment (no game running), at the latest after 2 minutes. */
    { static u32 quiet; static int pend;
#ifdef GFLUSH_IDLE
      static u32 pend_ms;
      if(e->flash_dirty && !pend) pend_ms=now;
#endif
#ifdef EMU_PW
      /* the ms passed in pw_steps count as quiet ones; the next one is a step of 250 */
      if(g_pw_skip){ if(pend){ quiet+=g_pw_skip; quiet-=(quiet+1u)%250u; } g_pw_skip=0; }
#endif
      if(e->flash_dirty){ e->flash_dirty=false; quiet=0; pend=1; }
      #ifdef GFLUSH_STEP
      /* v0.8.22: after 3 s without new writes, at most one changed 4 KB part to flash every 250 ms,
       * instead of everything at once (that caused stalls of half a second and more). */
#ifdef GFLUSH_DARK
      /* v0.9.1: only write while the screen is dark (then the pause goes unnoticed);
       * after 10 minutes at the latest anyway, in case it never goes dark */
#ifdef GFLUSH_IDLE
      /* 1.6.6: with the screen always on it never went dark, and a guest that writes again within 10 minutes
       * was never saved (power gone = save gone). Now also when no key was pressed for 20 s, and at the latest
       * 10 minutes after the first unsaved write. */
      else if(pend && ++quiet>3000u && (quiet % 250u)==0u && (!g_bl || quiet>600000u || now-g_btn_ms>20000u || now-pend_ms>600000u) && !e->flash_busy && !e->flash_state
#ifdef LK_SYNC
        && !g_lk_on   /* not while a visit runs: writing stops the emulator */
#endif
        ){ if(st_flush_step(e, &g_sc)){ pend=0; g_n_gflash++; } } }
#else
      else if(pend && ++quiet>3000u && (quiet % 250u)==0u && (!g_bl || quiet>600000u) && !e->flash_busy && !e->flash_state){ if(st_flush_step(e, &g_sc)){ pend=0; g_n_gflash++; } } }
#endif
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
#ifdef CLK_ECO
    /* test26: screen dark, no key, no tone, no link traffic for 30 s: a lower clock (eco.h) */
#ifdef EMU_PW
    if(!g_pw_low)
#endif
    eco_poll(now, !g_bl && !b && !g_hw_tone_hz && !(g_pl.last_rx && now-g_pl.last_rx<30000u), (int)(now-emu_ms), CORE_HZ);
#endif
    /* v0.9.5: guest screen dark right after the start (its own screen saver ran out while the clock
     * caught up, often after the autostart): one short press of C wakes it, like the user would.
     * Only once, only after resuming a session, in the first 8 s, while dark and nothing is pressed. */
    { static u32 t0, wk_ms; static int wk;
      if(!t0) t0=now?now:1;
      if(wk==0){ if(g_restored && !g_bl && !b && now-t0>1000u) { wk=1; wk_ms=now; } else if(now-t0>8000u) wk=2; }
      if(wk==1){ if(now-wk_ms<150u) b|=BTN_C; else wk=2; } }
#ifdef HW_OPTS
    /* screen off after a while without a key (set in the Studio): the first key only wakes it */
    { static int swallow;
      if(g_scr_dark && b){ g_scr_dark=0; swallow=1; g_btn_ms=now; }
#ifdef EMU_DIM
      if(b){ if(g_dimd && g_bl){ swallow=1; g_btn_ms=now; } g_dim_t=now; }
#endif
      if(swallow){ if(b) b=0; else swallow=0; } }
#endif
    periph_buttons(e,(uint8_t)(b&7));
    if(b) g_btn_ms=now;
    /* v0.9.2: wheel within the first 3 s after resuming a saved session: throw the session away
     * and go back to the Paradise. The next start begins fresh from the Tamagotchi's own save. */
    { static u32 t_start; if(!t_start) t_start=now?now:1;
      if(g_restored && (b&BTN_WHEEL) && now-t_start<3000u){
        /* 1.6.6: ask first (a press meant for the speed display lost the game) */
#ifdef EMU_OVL
        ovl_load();
#endif
#ifdef ASK_LANG
        if(hw_ask_reset(EMU_EN())){
#else
        if(hw_ask_reset((int)(g_cfg.flags & 1u))){
#endif
          st_drop(&g_sc); leave_nosave(); }
        g_restored=0; b=0; e->lcd.dx0=0; e->lcd.dy0=0; e->lcd.dx1=127; e->lcd.dy1=59; LCD_ALL(); } }
    #ifndef NOWHEEL
    if(b&BTN_WHEEL){ if(!wheel_ms) wheel_ms=now?now:1; else if(EXIT_NOW(now-wheel_ms, 1500u)) leave(); }
#if defined(OPT_MENU)
    /* 1.6.8: wheel short: settings (the game stands still meanwhile) */
    else { if(wheel_ms && now-wheel_ms<1000u){
#ifdef EMU_OVL
        ovl_load(); menu_run(e);
#else
#ifdef EMU_CARE
#ifdef ASK_LANG
        const EmuCare *c=care_find(e); int en=EMU_EN();
        const char *cx[2]={ c ? "GOTCHI POINTS" : 0, c ? "PFLEGE VOLL|FULL CARE" : 0 };
        u32 n=hw_opts_menu_xn(g_opt, en, cx, 2);
        if(n & 3u) en=(n & 3u)==2u;   /* the language may have been changed in the menu */
#else
        const EmuCare *c=care_find(e); int en=(int)(g_cfg.flags & 1u);
        const char *cx[2]={ c ? "GOTCHI POINTS" : 0, c ? (en ? "FULL CARE" : "PFLEGE VOLL") : 0 };
        u32 n=hw_opts_menu_xn(g_opt, en, cx, 2);
#endif
        if(n & 0x80000000u){ u8 *p=care_p(e,c->pts);
          if(n & (1u<<28)){ u8 *h=care_p(e,c->hun); if(*h<c->hmax) *h=c->hmax; { u32 m=*care_p(e,c->med), cap; if(c->mask){ m&=c->mask; cap=0; while(m){ cap+=c->mul; m&=m-1u; } cap+=c->add; } else cap=m>2u?100u:30u; h=care_p(e,c->hap); if(*h<cap) *h=(u8)cap; } }
          else { u32 v=p[0]|(u32)p[1]<<8|(u32)p[2]<<16; if(hw_num_edit("GOTCHI POINTS",&v,5,99999u,en)){ p[0]=(u8)v; p[1]=(u8)(v>>8); p[2]=(u8)(v>>16); p[3]=0; } }
          n&=0x0FFFFFFFu; }
#else
        u32 n=hw_opts_menu(g_opt, (int)(g_cfg.flags & 1u));
#endif
        if(n!=g_opt){ g_opt=n; opt_apply(n); opt_store(); }
#endif
        g_scr_dark=0; g_btn_ms=g_ms;
#ifdef EMU_DIM
        g_dim_t=g_ms;
#endif
        e->lcd.dx0=0; e->lcd.dy0=0; e->lcd.dx1=127; e->lcd.dy1=127; LCD_ALL(); } wheel_ms=0; }
#elif defined(CLEAN) && !defined(MESS)
    else wheel_ms=0;
#else
    else { if(wheel_ms && now-wheel_ms<1000u){ g_ovl^=1u; if(!g_ovl){ e->lcd.dx0=0; e->lcd.dy0=0; e->lcd.dx1=127; e->lcd.dy1=59; LCD_ALL(); } } wheel_ms=0; }
#endif
#endif
#ifdef LCD_QUIET
    /* 1.6.8: only show finished pictures. The guest draws a picture in one burst of LCD writes; push once it
     * has written nothing for 2 ms (at most every 40 ms, at the latest after 120 ms), not in the middle of a
     * burst (the picture was seen building up from the top). */
    static u32 lw_last, lw_quiet;
    { u32 lw=(u32)e->lcd_writes; if(lw!=lw_last){ lw_last=lw; lw_quiet=0; } else if(lw_quiet<1000u) lw_quiet++; }
    if(emu_ms-last_push >= (((s32)(g_ms-emu_ms) > 20) ? 80u : 40u) && (lw_quiet>=2u || emu_ms-last_push>=120u)){ last_push=emu_ms;
      push_dirty(e); }
#elif defined(LCD_SKIP)
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
#if defined(TONE_FIX) && defined(EMU_TONE_Q)
    /* test20: as T18Emu and T7Emu: the tone changes go into a queue with the game time and are played on the real
     * clock (tq_pump), so a slow moment of the emulator no longer stretches or swallows notes */
    { static u32 tone, offc; u32 t=periph_tone_hz(e);
#ifdef EMU_TONE_GAP
      /* test22: a pause of 4 ms or more is a pause (the B key: four notes with 8 ms between them, bridged they ran
       * into one sliding tone); shorter ones are still bridged */
      if(t && tone && offc>=4u){ tq_put(0, emu_ms-offc); tone=0; }
#endif
      if(t){ offc=0; if(t!=tone){ tone=t; tq_put(t, emu_ms); } }
      else if(tone && ++offc>=30u){ tone=0; tq_put(0, emu_ms - 29u); } }
    tq_pump();
#elif defined(TONE_FIX)
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
#ifdef HW_OPTS
      if(g_scr_ms && (s32)(now-g_btn_ms)>(s32)g_scr_ms) g_scr_dark=1;   /* signed: right after the menu */
      if(g_scr_dark) on=0;
#endif
#ifdef BL_GUEST_PWM
      /* 1.6.9: the colour Tamas light their screen with T16 channel 2 (PWM, all four models). The P's wakes its LCD
       * every 20 minutes for about 9 s while the screen is dark, without that light (a dark refresh, also a silent
       * reminder now and then); on the Paradise this lit the screen all night. The light now goes on only with the
       * guest's own light, or with a key or a tone in the last second (a call stays visible), and stays on while
       * the LCD stays on. */
      { static u32 tone_ms; static int ok;
        if(periph_tone_hz(e)) tone_ms=now?now:1;
        int gl=(e->ioram[0x300796u-e->dev.io_base] & 1u) && (e->ioram[0x3007E4u-e->dev.io_base] & 8u);
        if(!on) ok=0;
        else if(gl || now-g_btn_ms<1000u || (tone_ms && now-tone_ms<1000u)) ok=1;
        if(!ok) on=0; }
#endif
      if(on){ dark=0; if(!bl){ bl=1; hw_backlight(1); e->lcd.dx0=0; e->lcd.dy0=0; e->lcd.dx1=127; e->lcd.dy1=127; LCD_ALL();
#ifdef EMU_DIM
          g_dim_t=now;   /* the game lit the screen (a call): full light, the dim time starts again */
#endif
        } }
      else if(bl && ++dark>500u){ bl=0; hw_backlight(0); }
      g_bl=bl; }
#ifdef EMU_DIM
    /* signed: after the menu g_dim_t is later than now */
    if(g_dimd && ((s32)(now-g_dim_t)<=(s32)g_dim_ms || !g_bl)){ g_dimd=0; hw_bl_set(opt_light_pct(g_opt)); }   /* a key, the menu, dark */
    else if(!g_dimd && g_dim_ms && g_bl && (s32)(now-g_dim_t)>(s32)g_dim_ms){ g_dimd=1; hw_bl_set(opt_dim_pct(g_opt)); }
#endif
#ifdef SESS_AUTO
#ifndef SESS_T1
#define SESS_T1 600000u    /* screen dark: 10 minutes after the last time at the earliest */
#define SESS_T2 1800000u   /* otherwise every 30 minutes */
#endif
    /* 1.6.8: the run state now and then, without leaving (battery empty or out while playing): when the screen
     * goes dark (10 minutes after the last time at the earliest), otherwise every 30 minutes after 10 s without
     * a key. st_save writes the guest flash back first (its slots are free then) */
    { static u32 last; static int bl0=1;
      int went=bl0 && !g_bl; bl0=g_bl;
      if(((went && now-last>SESS_T1) || (now-last>SESS_T2 && now-g_btn_ms>10000u)) && !e->flash_busy && !e->flash_state){
        hw_tone(0);
#ifdef EMU_OVL
        ovl_load();
#endif
        st_save(e, &g_sc, g_gram); last=now; } }
#endif
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
  [15] = (void*)SysTick_Handler, [16 ... 79] = (void*)Irq_Off
#ifdef TC_IRLINK
  , [16 + 37] = (void*)plink_irq
#endif
#ifdef EMU_PW
  , [16 + 2] = (void*)pw_rtc_irq, [16 + 26] = (void*)pw_gpio_irq
#endif
};
