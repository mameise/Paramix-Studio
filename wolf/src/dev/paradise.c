/* Paramix Wolf3D on the Tamagotchi Paradise: started from the Paramix menu like the other further programs.
 * The data pack (made by the Studio from the user's own game files) lies in the flash; the cached flash window
 * (1 MB from 0x10000000) is put on it, anything behind the first MB is read through the uncached window.
 * Settings block "PMXW" at 0x140 (filled in by the Studio): pack, its length, save area, flags, name, keys.
 * Author: mameise. GPL-2.0-or-later. */
#include "../../../emu/src/emu/hw.h"
#include "../../../emu/src/emu/flash.h"
#define PARA_ATTR
#include "../../../emu/src/emu/paraclock.h"
#ifdef PARA_BACK
#include "../../../emu/src/emu/paraback.h"
#endif
#include "../pmx/pmx_plat.h"
#include <string.h>

typedef int s32;
/* keys: masks A 1, B 2, C 4, wheel press 8; fire, forward, back, use, hold key; run with B twice; turn degrees per step
 * flags: bit 0 English, bit 1 wheel the other way round. save_off: 4 KB settings, then the saved game (60 KB). */
typedef struct { u32 magic, version, rom_off, rom_len, save_off, flags; char name[16]; u8 k_fire, k_fwd, k_back, k_use, k_mod, run_dbl, turn_deg, r0; u32 prog_off; } WolfCfg;
#define WOLF_MAGIC 0x57584D50u   /* "PMXW" */
__attribute__((section(".pmxcfg"), used))
volatile const WolfCfg g_cfg = { WOLF_MAGIC, 1, 0, 0, 0, 0, "WOLFENSTEIN 3D", 1, 2, 4, 8, 4, 1, 15, 0, 0 };
#define XIP_BASE_REG REG(0x4002F004u)
#define XIP_CTRL_REG REG(0x4002F000u)
#define WIN ((const u8 *)0x10000000u)
#define WIN_LEN 0x100000u

/* ---- time: SysTick 1 ms; the wheel's encoder and the sound are served here ---- */
static volatile u32 g_ms;
static volatile s32 g_enc_acc; static u32 g_enc_last;
static const signed char QD[16] = { 0, -1, 1, 0, 1, 0, 0, -1, -1, 0, 0, 1, 0, 1, -1, 0 };
#define GPIO2 0x4001A000u
#define SYST_CSR REG(0xE000E010u)
#define SYST_RVR REG(0xE000E014u)
#define SYST_CVR REG(0xE000E018u)
#define CORE_HZ 96000000u
void pmx_sd_service (uint32_t now);
__attribute__((used)) void systick_c(void)
{
  u32 ms = ++g_ms;
  u32 n = REG(GPIO2) & 3u;
  if (n != g_enc_last) { g_enc_acc += QD[(g_enc_last << 2) | n]; g_enc_last = n; }
  pmx_sd_service(ms);
}
__attribute__((naked)) void SysTick_Handler(void) { __asm__ volatile("b systick_c\n"); }

uint32_t plat_ms(void) { return g_ms; }
void plat_wait(uint32_t ms) { u32 t0 = g_ms; hw_wdt_feed(); while (g_ms - t0 < ms) __asm__ volatile("wfi"); }
uint32_t plat_keys(void) { hw_wdt_feed(); return hw_buttons() & 15u; }
int plat_wheel(void)
{
  s32 a = g_enc_acc, d = 0;
  while (a >= 4) { a -= 4; d++; }
  while (a <= -4) { a += 4; d--; }
  if (d) { __asm__ volatile("cpsid i"); g_enc_acc -= 4 * d; __asm__ volatile("cpsie i"); }
  return d;
}
static u16 g_row[128] __attribute__((aligned(4)));
void plat_lcd(const u8 *fb, int y0, int y1, const u16 *lut)
{
  hw_wdt_feed();
  hw_px_begin(0, (u32)y0, 128, (u32)y1);
  for (int y = y0; y < y1; y++) { const u8 *f = fb + y * 128; for (int x = 0; x < 128; x++) g_row[x] = lut[f[x]]; hw_px_row(g_row, 128); }
  hw_px_end();
}
void plat_tone(uint32_t hz) { hw_tone(hz); }
const u8 *plat_pack(void) { return WIN; }
/* the pack beyond the cached first MB: through the uncached window */
const u8 *plat_pack_far(void) { return (const u8 *)(0x60000000u + g_cfg.rom_off); }
uint32_t plat_pack_win(void) { return WIN_LEN; }

static u8 g_sec[0x1000] __attribute__((aligned(4)));
void plat_save_at(uint32_t off, void *dst, int len)
{
  const volatile u8 *p = XIP_PTR(g_cfg.save_off + 0x1000u + off); u8 *d = (u8 *)dst;
  if (off + (uint32_t)len > 0xF000u) return;
  for (int i = 0; i < len; i++) d[i] = p[i];
}
void pmx_sd_halt(void);
void plat_save_page(uint32_t off, const void *src, int len)
{
  if (off + 0x1000u > 0xF000u) return;
  pmx_sd_halt();
  for (int i = 0; i < 0x1000; i++) g_sec[i] = i < len ? ((const u8 *)src)[i] : 0xFF;
  hw_wdt_feed();
  flash_update4k(g_cfg.save_off + 0x1000u + off, g_sec);
}
int plat_settings_read(void *buf, int len)
{
  const volatile u8 *p = XIP_PTR(g_cfg.save_off); u8 *d = (u8 *)buf;
  for (int i = 0; i < len; i++) d[i] = p[i];
  return len;
}
void plat_settings_write(const void *buf, int len)
{
  for (int i = 0; i < 0x1000; i++) g_sec[i] = i < len ? ((const u8 *)buf)[i] : 0xFF;
  pmx_sd_halt();
  flash_update4k(g_cfg.save_off, g_sec);
}

void plat_back(void)
{
  __asm__ volatile("cpsid i");
  SYST_CSR = 0;
  hw_tone(0);
  REG(0x20000FF8u) = 0;
  REG(0x20000FFCu) = 0x4C584D50u;        /* "PMXL": back on purpose, the autostart stays off */
#ifdef PARA_BACK
  para_back_fix(g_sec);
#endif
  hw_bl_pwm_off();
  for (u32 t = 0; hw_buttons() && t < 20000000u; t++) if ((t & 0xFFFFu) == 0) hw_wdt_feed();
  XIP_BASE_REG = 0x60011000u; XIP_CTRL_REG = 3u;
  hw_back_to_paradise();
  for (;;) {}
}

/* ---- a message on the screen, then back ---- */
void pmx_text(int x, int y, const char *s, int color);
u8 *pmx_screen(void);
const u16 *pmx_lut(void);
void plat_fatal(const char *s)
{
  u8 *fb = pmx_screen();
  memset(fb, 0, 128 * 128);
  pmx_text(2, 50, "FEHLER", 15);
  pmx_text(2, 62, s, 15);
  plat_lcd(fb, 0, 128, pmx_lut());
  for (u32 t0 = g_ms; g_ms - t0 < 5000u; ) { hw_wdt_feed(); if (hw_buttons()) break; }
  plat_back();
}
void pmx_error(const char *s) { plat_fatal(s); }

/* newlib bits nobody needs here */
void *_sbrk(int n) { (void)n; return (void *)-1; }
void abort(void) { plat_fatal("ABORT"); for (;;) {} }

void pmx_main(void);
void pmx_cfg_keys(uint32_t fire, uint32_t fwd, uint32_t back, uint32_t use, uint32_t mod, uint32_t run_dbl, uint32_t invert, uint32_t turn_deg, uint32_t english);
extern u32 __bss_start, __bss_end;
void Reset_Handler(void)
{
  __asm__ volatile("cpsid i");
  u32 cnt = REG(0x45000304u), para = para_clock(cnt, 0, 0x1F000u, 0, 0);
  if (!para) para = para_mailbox(cnt);
#ifdef PARA_BACK
  para_leave_mark(cnt, para);
#endif
  __asm__ volatile("" ::: "memory");
  for (u32 *p = &__bss_start; p < &__bss_end; p++) *p = 0;
  { extern u32 __data_start, __data_end, __data_load;      /* start values of .data: in PRAM with the code */
    const u32 *src = &__data_load; for (u32 *p = &__data_start; p < &__data_end; p++) *p = *src++; }
  (void)para;
  REG(0xE000ED14u) &= ~(1u << 3);                  /* UNALIGN_TRP off (as DOOM: plain LDR/STR may be unaligned) */
  hw_init();                                       /* also switches the flash window on again */
  hw_tone(0);
  /* the second part (code for the SRAM behind .bss) lies behind the first 64 KB of the program in the flash
   * (g_cfg.prog_off, set by the Studio). The stub starts us with a system reset; only now (hw_init) can the flash be read. */
  { extern u32 __sc_start, __sc_end, __sc_load;
    const volatile u32 *src = (const volatile u32 *)XIP_PTR(g_cfg.prog_off + (u32)&__sc_load);
    for (u32 *p = &__sc_start; p < &__sc_end; p++) { *p = *src++; }
    __asm__ volatile("dsb\n isb" ::: "memory"); }
  XIP_BASE_REG = 0x60000000u | g_cfg.rom_off;     /* the pack in the cached window from 0x10000000 */
  XIP_CTRL_REG = 3u;
  __asm__ volatile("dsb\n isb" ::: "memory");
  /* the window's cache keeps lines of the old base: reading 64 KB elsewhere in it throws them out (as DOOM) */
  { volatile u32 sum = 0; for (u32 i = 0; i < 0x4000u; i++) sum += *(const volatile u32 *)(0x100C0000u + 4u * i); (void)sum; }
  REG(GPIO2 + 0x04) &= ~3u; g_enc_last = REG(GPIO2) & 3u;
  pmx_cfg_keys(g_cfg.k_fire, g_cfg.k_fwd, g_cfg.k_back, g_cfg.k_use, g_cfg.k_mod, g_cfg.run_dbl, (g_cfg.flags >> 1) & 1u, g_cfg.turn_deg, g_cfg.flags & 1u);
  SYST_RVR = CORE_HZ / 1000u - 1u; SYST_CVR = 0; SYST_CSR = 7;
  __asm__ volatile("cpsie i");
  pmx_main();
  plat_back();
}

/* a fault: "FEHLER" with the address on the screen, a key goes back to the Paradise (instead of hanging) */
static void hex8(char *o, u32 v) { for (int i = 0; i < 8; i++) { u32 d = (v >> (28 - 4 * i)) & 15u; o[i] = (char)(d < 10 ? '0' + d : 'A' + d - 10); } o[8] = 0; }
__attribute__((used)) void fault_c(u32 *sp, u32 lr)
{
  char a[24] = "PC ", b[24] = "LR ", c[24] = "FS ";
  hex8(a + 3, sp[6]); hex8(b + 3, sp[5]); hex8(c + 3, REG(0xE000ED28u));
  (void)lr;
  hw_tone(0);
  u8 *fb = pmx_screen();
  memset(fb, 0, 128 * 128);
  pmx_text(2, 40, "FEHLER", 15); pmx_text(2, 52, a, 15); pmx_text(2, 62, b, 15); pmx_text(2, 72, c, 15);
  plat_lcd(fb, 0, 128, pmx_lut());
  while (hw_buttons()) hw_wdt_feed();
  while (!hw_buttons()) hw_wdt_feed();
  plat_back();
}
__attribute__((naked)) static void Default_Handler(void)
{
  __asm__ volatile("tst lr, #4\n ite eq\n mrseq r0, msp\n mrsne r0, psp\n mov r1, lr\n ldr r2, =0x18020000\n mov sp, r2\n b fault_c\n .ltorg");
}
__attribute__((naked)) static void Irq_Off(void) { __asm__ volatile("cpsid i\n bx lr\n"); }
__attribute__((section(".vectors"), used))
const void *const g_vectors[16 + 64] = { (void *)0x18020000u, (void *)Reset_Handler,
  [2 ... 10] = (void *)Default_Handler, [11] = (void *)Irq_Off, [12 ... 13] = (void *)Default_Handler, [14] = (void *)Irq_Off,
  [15] = (void *)SysTick_Handler, [16 ... 79] = (void *)Irq_Off
};
