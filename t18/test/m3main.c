/* T18 core on a Cortex-M3 in Unicorn (test bed for the translator).
 * Memory as on the Paradise: guest SRAM at 0x18000000, guest flash at 0x60000000 (uncached XIP).
 * Commands come through g_mb: 1 run <us>, 2 buttons <mask>, 3 hash, 4 picture, 5 set time. */
#include "../src/core/t18soc.h"
#include <string.h>
#define FW ((uint8_t *)0x60000000u)
uint8_t t18_flash_rd(uint32_t a) { return FW[a & 0x7FFFFF]; }
#ifdef T18_FF_FAST
extern uint8_t t18_fff[128];   /* a written block is no longer empty (the device maps a spare block then) */
void t18_flash_prog(uint32_t a, uint8_t b) { FW[a & 0x7FFFFF] &= b; if (b != 0xFF) t18_fff[(a >> 16) & 127] = 0; }
#else
void t18_flash_prog(uint32_t a, uint8_t b) { FW[a & 0x7FFFFF] &= b; }
#endif
void t18_flash_erase(uint32_t a, uint32_t size) { memset(FW + (a & 0x7FFFFF), 0xFF, size); }
static uint32_t g_t[3600 / 4];
#ifndef NPOOL
#define NPOOL 12
#endif
static uint8_t g_pool[NPOOL][T18_HOLE_PG] __attribute__((aligned(4))); static int g_pool_n;
static uint8_t *hole_new(void) { if (g_pool_n >= NPOOL) return 0; uint8_t *p = g_pool[g_pool_n++]; memset(p, 0, T18_HOLE_PG); return p; }
static void hole_free(void) { g_pool_n = 0; }
#ifdef T18_PRAM_WR
/* as on the Paradise: from the flash window, interrupts off (m3run.py checks where PRAM writes come from) */
__attribute__((section(".xiptext"), noinline, used)) void t18_pram_w16(uint16_t *p, uint32_t v)
{
    uint32_t pm;
    __asm__ volatile("mrs %0, primask\n cpsid i" : "=r"(pm) :: "memory");
    *(volatile uint16_t *)p = (uint16_t)v;
    __asm__ volatile("dsb\n isb\n msr primask, %0" :: "r"(pm) : "memory");
}
#endif
volatile uint32_t g_mb[16];
uint32_t g_pic_src, g_pic_len, g_pics;
static void pres(void *u, uint32_t src, uint32_t len) { (void)u; g_pic_src = src; g_pic_len = len; g_pics++; }
/* wait for the next command: g_mb[9] = sequence from the driver, g_mb[8] counts idle turns (4 instructions each) */
__attribute__((noinline, naked)) void mb_spin(void) {
    __asm__ volatile("1: ldr r1, [r0, #36]\n cmp r1, r2\n add r3, r3, #1\n beq 1b\n str r3, [r0, #32]\n bx lr\n");
}
static uint32_t g_seq;
static void mb_wait(void) {
    register volatile uint32_t *r0 __asm__("r0") = g_mb; register uint32_t r2 __asm__("r2") = g_seq; register uint32_t r3 __asm__("r3") = 0;
    __asm__ volatile("bl mb_spin" : "+r"(r0), "+r"(r2), "+r"(r3) : : "r1", "lr", "memory", "cc");
    g_seq = g_mb[9];
}
#ifdef T18_CLOCK_AFTER
/* test: like the app, the time given with "time" is put back when the firmware loads its RTC */
static int g_tm[6]; uint32_t g_cb_n; static int g_pend;   /* like the app: 0.5 s later (the firmware sets its day counter after the RTC) */
static void clock_after(void *t) { (void)t; g_cb_n++; if (g_tm[0]) g_pend = 1; }
#endif
static uint32_t fnv(const uint8_t *p, uint32_t n, uint32_t h) { for (uint32_t i = 0; i < n; i++) h = (h ^ p[i]) * 16777619u; return h; }
int main(void)
{
    tg18_t *T = (tg18_t *)g_t;
    if (tg18_struct_size() > sizeof g_t) { g_mb[15] = tg18_struct_size(); for (;;) mb_wait(); }
    t18_sram = (uint8_t *)0x18000000u; t18_hole_new = hole_new; t18_hole_free = hole_free;
    for (int i = 0; i < 128; i++) t18_fblk[i] = FW + (i << 16);
#if defined(T18_FF_FAST) || defined(T18_EMPTY_SLOW)
    {                                /* as on the device: empty blocks have no pointer (FF_FAST: the translator answers 0xFF) */
      for (int i = 0; i < 128; i++) { int e = 1; for (uint32_t k = 0; k < 0x10000u && e; k += 4) if (*(const uint32_t *)(FW + (i << 16) + k) != 0xFFFFFFFFu) e = 0;
        if (e) { t18_fblk[i] = 0;
#ifdef T18_FF_FAST
          { extern uint8_t t18_fff[128]; t18_fff[i] = 1; }
#endif
        } } }
#endif
    tg18_init(T, FW, 0x800000);
    tg18_set_present(T, pres, 0);
    for (;;) {
        mb_wait();
        switch (g_mb[0]) {
        case 1:
#ifdef T18_CLOCK_AFTER
            for (uint32_t us = g_mb[1]; us; ) { uint32_t k = us > 50000u ? 50000u : us; tg18_run_us(T, k); us -= k;
                if (g_pend && ++g_pend > 11) { g_pend = 0; tg18_set_time(T, g_tm[0], g_tm[1], g_tm[2], g_tm[3], g_tm[4], g_tm[5]); } }
#else
            tg18_run_us(T, g_mb[1]);
#endif
            break;
        case 2: tg18_set_buttons(T, (int)g_mb[1]); break;
        case 4: {
#ifdef T18_JIT
            extern uint32_t t18_jit_stat[8], t18_interp[2];
            for (int i = 0; i < 6; i++) g_mb[1 + i] = t18_jit_stat[i];
            g_mb[11] = t18_jit_stat[6]; { extern uint32_t t18_jit_used(void); g_mb[12] = t18_jit_used(); }
            g_mb[13] = t18_jit_stat[7]; { extern uint32_t t18_jit_bytes; g_mb[14] = t18_jit_bytes; }
            g_mb[7] = t18_interp[0]; g_mb[10] = t18_interp[1];
#endif
            break; }
        case 3: { arm7_t *c = tg18_cpu(T); g_mb[2] = (uint32_t)c->cycles; g_mb[3] = (uint32_t)(c->cycles >> 32); g_mb[4] = c->r[15]; { uint32_t h = 2166136261u; for (uint32_t o = 0; o < T18_SRAM_SIZE; o += 256) h = fnv(t18_sram_rp(o), 256, h); g_mb[5] = h; } g_mb[7] = (uint32_t)t18_oom | (uint32_t)g_pool_n << 8; break; }
#ifdef T18_JIT_VERIFY
        case 6: { extern int t18_verify_on; t18_verify_on = 1; break; }
#endif
        case 7: {                    /* load state + SRAM put by the driver at 0x61000000 - 0x40000 (g_mb[1] = state length) */
            const uint8_t *b = (const uint8_t *)0x60FC0000u; uint32_t n = g_mb[1];
            g_mb[2] = (uint32_t)tg18_load_state(T, b, n);
            for (uint32_t q = 0; q < T18_SRAM_SIZE; q += 4) t18_sram_wr(q, *(const uint32_t *)(b + n + q), 4);
            break; }
        case 5: tg18_set_time(T, (int)g_mb[1], (int)g_mb[2], (int)g_mb[3], (int)g_mb[4], (int)g_mb[5], (int)g_mb[6]);
#ifdef T18_CLOCK_AFTER
            for (int i = 0; i < 6; i++) g_tm[i] = (int)g_mb[1 + i];
            tg18_set_rtc_cb(T, clock_after);
#endif
            break;
        }
        if (g_mb[0] != 4) { g_mb[1] = g_pic_src; g_mb[6] = g_pics; }
    }
}
#ifdef T18_JIT_VERIFY
/* translation differs from the interpreter: report and stop (g_mb[0] = 0xBAD) */
volatile uint32_t g_vf[80];
void t18_verify_fail(uint32_t key, const arm7_t *b, const arm7_t *j, const arm7_t *r)
{
    g_vf[0] = 0xBAD; g_vf[1] = key;
    for (int i = 0; i < 16; i++) { g_vf[2 + i] = b->r[i]; g_vf[18 + i] = j->r[i]; g_vf[34 + i] = r->r[i]; }
    g_vf[50] = b->cpsr; g_vf[51] = j->cpsr; g_vf[52] = r->cpsr;
    g_vf[53] = (uint32_t)j->cycles; g_vf[54] = (uint32_t)r->cycles; g_vf[55] = (uint32_t)b->cycles;
    for (;;) __asm__ volatile("bkpt #1");
}
#endif
/* minimal runtime */
void *memset(void *d, int c, unsigned n) { uint8_t *p = d; while (n && ((uintptr_t)p & 3)) { *p++ = (uint8_t)c; n--; } uint32_t w = (uint8_t)c * 0x01010101u; while (n >= 4) { *(uint32_t *)p = w; p += 4; n -= 4; } while (n--) *p++ = (uint8_t)c; return d; }
void *memcpy(void *d, const void *s, unsigned n) { uint8_t *p = d; const uint8_t *q = s; if (!(((uintptr_t)p | (uintptr_t)q) & 3)) while (n >= 16) { uint32_t a = ((const uint32_t *)q)[0], b = ((const uint32_t *)q)[1], c = ((const uint32_t *)q)[2], e = ((const uint32_t *)q)[3]; ((uint32_t *)p)[0] = a; ((uint32_t *)p)[1] = b; ((uint32_t *)p)[2] = c; ((uint32_t *)p)[3] = e; p += 16; q += 16; n -= 16; } while (n--) *p++ = *q++; return d; }
void *memmove(void *d, const void *s, unsigned n) { uint8_t *p = d; const uint8_t *q = s; if (p < q) while (n--) *p++ = *q++; else { p += n; q += n; while (n--) *--p = *--q; } return d; }
int memcmp(const void *a, const void *b, unsigned n) { const uint8_t *p = a, *q = b; if (!(((uintptr_t)p | (uintptr_t)q) & 3)) while (n >= 4 && *(const uint32_t *)p == *(const uint32_t *)q) { p += 4; q += 4; n -= 4; } for (; n; n--, p++, q++) if (*p != *q) return *p - *q; return 0; }
extern uint32_t __bss_start, __bss_end, __data_start, __data_end, __data_load, __stack_top;
void Reset_Handler(void) {
    for (uint32_t *p = &__bss_start; p < &__bss_end; p++) *p = 0;
    for (uint32_t *d = &__data_start, *s = &__data_load; d < &__data_end; ) *d++ = *s++;
    main();
}
__attribute__((section(".vectors"), used)) const void *g_vec[2] = { (void *)&__stack_top, Reset_Handler };   /* stack in the hole, as on the Paradise */
