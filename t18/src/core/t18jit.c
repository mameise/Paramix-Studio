/* T18 translator: guest code of the ARM7TDMI (Thumb and ARM) into Cortex-M3 Thumb-2 code.
 *
 * Most Thumb-1 instructions are the same halfword on the M3, flags included, so ALU code is
 * copied. The rest is rewritten:
 *   host r0..r7   guest r0..r7
 *   host r8       guest sp as a host address (guest + 0x20000000: SRAM 0xF8000000 -> 0x18000000)
 *   host r9       arm7_t *  (guest r8..r12, r14 live there)
 *   host r10      cycle budget left (CPU cycles), counted down at the exits
 *   host r11 r12 lr   scratch (lr also holds the guest flags around an address check)
 *   APSR NZCV     guest NZCV
 * Blocks are found by key = pc | 1 for Thumb, pc for ARM; exits hand the key on in r11.
 * Loads and stores check for SRAM (and alignment) inline; everything else goes through a helper
 * with the exact cycle count of the moment, like the interpreter. A block is only entered when
 * the budget covers all of its cycles, so interrupts come at the same instruction as in the
 * interpreter: the result is the same, bit for bit. What is not translated runs in the
 * interpreter (mode changes, coprocessor, rare forms).
 * Author: mameise */
#ifdef T18_JIT
#include "t18cpu.h"
#include "t2enc.h"
#include <stddef.h>
#include <string.h>

#ifndef T18_JIT_BUF
#define T18_JIT_BUF 16384            /* bytes of translated code */
#endif
#ifndef JH_BITS
#define JH_BITS 9
#endif
#define JH_N (1 << JH_BITS)          /* block table */
#define J_MAXI 48                    /* guest instructions per block */
#ifndef J_HOT
#define J_HOT 2                      /* translate on this start */
#endif
#ifndef T18_BXLR_SHORT
#define T18_BXLR_SHORT 0
#endif
#ifndef JT_BITS
#define JT_BITS 9
#endif
#define JT_N (1 << JT_BITS)          /* cache for indirect jumps (index: see j_ind) */
#define JT_STR2(x) #x
#define JT_STR(x) JT_STR2(x)
#if JT_BITS == 8
#define JT_SB 7
#define JT_SH 25
#elif JT_BITS == 9
#define JT_SB 8
#define JT_SH 24
#elif JT_BITS == 10
#define JT_SB 9
#define JT_SH 23
#endif

_Static_assert(offsetof(arm7_t, r[13]) == 52 && offsetof(arm7_t, r[14]) == 56 && offsetof(arm7_t, r[15]) == 60, "arm7_t layout");
_Static_assert(offsetof(arm7_t, cpsr) == 64, "arm7_t layout");

/* glue: the code buffer branches to it with b/bl (+-16 MB); both are in the PRAM */
#define GLUE
_Static_assert(T18_JIT_BUF <= 65536, "block offsets are 16 bit");
#ifdef T18_JC_LINKED
extern uint16_t __jc_start[], __jc_end[];         /* the linker puts the buffer over the start code */
#define JC __jc_start
#define JC_END ((char *)__jc_end)
#else
static uint16_t JC[T18_JIT_BUF / 2] __attribute__((aligned(4), section(".jitbuf")));
#define JC_END ((char *)JC + sizeof JC)
#endif
uint16_t *J;
#ifdef T18_HALVES
/* test (1.6.8, not taken: in the M3 test bed 10..25 % more host instructions, the hot code of a scene
 * is bigger than half the buffer and the halves throw each other out): the buffer in two halves. When
 * the half being filled is full, only the other (older) half is thrown away; the code of the last half stays. Links from the kept half into the dropped one become
 * "bl j_chain" again, the tables forget the dropped blocks. */
static char *j_lim;                  /* end of the half being filled */
static int j_half;
static uint16_t *j_fill[2];          /* end of the code in each half */
#define JMID ((uint16_t *)(((uintptr_t)JC + ((uintptr_t)JC_END - (uintptr_t)JC) / 2) & ~3u))
#define J_LIMIT (j_lim ? j_lim : JC_END)
#else
#define J_LIMIT JC_END
#endif
typedef struct { uint32_t key; uint16_t off, maxcyc; uint8_t hot, none, hot2, len; } jent_t;   /* len: guest bytes / 2 (255 = more) */   /* off: halfwords into JC + 1, 0 = none */
#define ECODE(e) (JC + (e)->off - 1)
#ifdef T18_JIT_VERIFY
#ifndef T18_VERIFY_AT0
#define T18_VERIFY_AT0 1
#endif
#ifndef T18_VERIFY_N
#define T18_VERIFY_N 20
#endif
int t18_verify_on = T18_VERIFY_AT0;
uint32_t tg18_struct_size(void);
void t18_verify_fail(uint32_t key, const arm7_t *before, const arm7_t *jit, const arm7_t *ref);
#endif
#if defined(T18_JH_SPLIT)
/* 1.6.6: 512 entries (with a bigger code buffer 256 were too few: blocks kept pushing each other out and
 * were interpreted again). No room in the hole for all: the second half lies in the Paradise's RAM. */
static jent_t JHa[JH_N / 2];
#define JHb ((jent_t *)T18_JH_SPLIT)
#define JHE(i) ((i) < JH_N / 2 ? &JHa[i] : &JHb[(i) - JH_N / 2])
#define JH_CLEAR() do { memset(JHa, 0, sizeof JHa); memset(JHb, 0, sizeof(jent_t) * (JH_N / 2)); } while (0)
#elif defined(T18_JH_SECT)
static jent_t JH[JH_N] __attribute__((section(T18_JH_SECT)));   /* elsewhere (test: other RAM) */
#define JHE(i) (&JH[i])
#define JH_CLEAR() memset(JH, 0, sizeof JH)
#else
static jent_t JH[JH_N];
#define JHE(i) (&JH[i])
#define JH_CLEAR() memset(JH, 0, sizeof JH)
#endif
#ifdef T18_JC_LINKED
extern uint32_t __jt_start[];        /* key, code: at the end of the PRAM (written through JW) */
#define JT __jt_start
#define JT_BYTES (JT_N * 8)
#else
static uint32_t JT[JT_N * 2];        /* key, code */
#define JT_BYTES sizeof JT
#endif
static void jt_put(uint32_t *p, uint32_t v) { JW((uint16_t *)p, v & 0xFFFFu); JW((uint16_t *)p + 1, v >> 16); }
#ifdef T18_SEQ_FAST
/* 1.6.8: arm7_run checks "pc == j_seq and not stuck" itself (no call for each interpreted instruction of a
 * run); j_seq is 1 (no pc) while a fallback or tail decides */
uint32_t t18_jseq, t18_jstuck;
#define j_seq t18_jseq
#define j_stuck t18_jstuck
#define SEQ_OFF() (j_seq = 1u)
static uint32_t j_fallback, j_tail;
#else
static uint32_t j_seq, j_fallback, j_stuck, j_tail;
#define SEQ_OFF() ((void)0)
#endif
static uint32_t j_idle_key;          /* never linked into and never inside a block: the idle check sits there */
static arm7_t *JCPU;
#ifdef T18_MEM_HELPER
static int j_compact;
#ifdef T18_TIERUP
static int j_inl;                         /* 1.6.8 (T18_TIERUP): this translation inline, also when compact */
#else
#define j_inl 0
#endif
int t18_new_off;                          /* test: 1 = the changes of 1.6.8 off (C held when T18Emu starts) */
static int j_hot_v = J_HOT;               /* 1.6.8: J_HOT 4; 8 with t18_new_off */
#endif
static uint64_t J_CYC0; static int32_t J_BUD0; static uint32_t J_MULT;
uint32_t t18_jit_stat[8], t18_jit_bytes, t18_jit_fr[4];
#ifdef T18_KSTAT
uint32_t t18_kstat[2][20];
#endif
#if defined(T18_SLOW_HIST) && defined(T18_SLOW_HIST_AT)
#define t18_slow_hist ((uint32_t *)T18_SLOW_HIST_AT)   /* test11: in the Paradise's RAM (30 entries) */
#define SLOW_HIST_N 30
#elif defined(T18_SLOW_HIST)
#define SLOW_HIST_N 64
uint32_t t18_slow_hist[128];
#endif            /* blocks, entries, stuck, flushes, slow accesses, links, lookups, shortcuts */

/* ------------------------------------------------------------------ glue */
/* enter(c, code|1, budget) -> budget left; c->r[15] and the T bit from the exit key */
GLUE __attribute__((naked, used)) int32_t j_enter(arm7_t *c, const void *code, int32_t budget)
{
    __asm__ volatile(
        "push {r4-r11, lr}\n"
        "sub sp, #4\n"
        "mov r9, r0\n"
        "mov r10, r2\n"
        "mov lr, r1\n"
        "ldr r12, [r9, #64]\n"
        "msr apsr_nzcvq, r12\n"
        "ldr r8, [r9, #52]\n"
        "add r8, r8, #0x20000000\n"
        "ldmia r9, {r0-r7}\n"
        "bx lr\n");
}
/* exit: r11 = key of the next instruction (pc | 1 for Thumb) */
GLUE __attribute__((naked, used)) void t18_jit_exit(void)
{
    __asm__ volatile(
        "stmia r9, {r0-r7}\n"
        "bic r12, r11, #1\n"
        "str r12, [r9, #60]\n"
        "sub r8, r8, #0x20000000\n"
        "str r8, [r9, #52]\n"
        "mrs r12, apsr\n"
        "ldr r0, [r9, #64]\n"
        "bic r0, r0, #0xF0000000\n"
        "bic r0, r0, #0x20\n"
        "and r12, r12, #0xF0000000\n"
        "orr r0, r0, r12\n"
        "and r12, r11, #1\n"
        "orr r0, r0, r12, lsl #5\n"
        "str r0, [r9, #64]\n"
        "mov r0, r10\n"
        "add sp, #4\n"
        "pop {r4-r11, pc}\n");
}
uint32_t t18_link_pc, t18_link_cyc, t18_link_go;
/* exits prepared by the helpers below (key in t18_link_pc, cycles to take off in t18_link_cyc) */
GLUE __attribute__((naked, used)) void t18_jit_exit_link(void)
{
    __asm__ volatile(
        "ldr r12, =t18_link_cyc\n"
        "ldr r12, [r12]\n"
        "sub r10, r10, r12\n"
        "ldr r11, =t18_link_pc\n"
        "ldr r11, [r11]\n"
        "b t18_jit_exit\n"
        ".ltorg\n");
}
GLUE __attribute__((naked, used)) void t18_jit_exit_link_fl(void)
{
    __asm__ volatile(
        "msr apsr_nzcvq, lr\n"
        "b t18_jit_exit_link\n");
}
/* take t18_link_cyc off the budget, then go on at t18_link_go */
GLUE __attribute__((naked, used)) void t18_jit_sub_go(void)
{
    __asm__ volatile(
        "ldr r12, =t18_link_cyc\n"
        "ldr r12, [r12]\n"
        "sub r10, r10, r12\n"
        "ldr r12, =t18_link_go\n"
        "ldr r12, [r12]\n"
        "bx r12\n"
        ".ltorg\n");
}
uint32_t t18_jit_link(uint16_t *data, int32_t bud);
uint32_t t18_jit_lookup(uint32_t key, int32_t bud);
uint32_t t18_jh_slow(uint32_t *frame, int32_t bud, uint32_t addr);
/* static exit: "bl j_chain; .word key": link to the target block (patches the bl) */
GLUE __attribute__((naked, used)) void j_chain(void)
{
    __asm__ volatile(
        "push {r0-r3, r12, lr}\n"
        "mrs r12, apsr\n"
        "push {r12}\n"
        "bic r0, lr, #1\n"
        "mov r1, r10\n"
        "ldr r12, =t18_jit_link\n"
        "blx r12\n"
        "mov r11, r0\n"
        "pop {r12}\n"
        "msr apsr_nzcvq, r12\n"
        "pop {r0-r3, r12, lr}\n"
        "bx r11\n"
        ".ltorg\n");
}
/* indirect exit: r11 = key. j_indbx: r11 = BX target (bit 0 = Thumb), made into a key first. */
GLUE __attribute__((naked, used)) void j_indbx(void)
{
    __asm__ volatile(
        "mrs r12, apsr\n"
        "str r12, [r9, %[sv]]\n"
        "tst r11, #1\n"
        "it eq\n"
        "biceq r11, r11, #3\n"
        "b j_ind_key\n"
        : : [sv] "i"(offsetof(arm7_t, jsave)));
}
GLUE __attribute__((naked, used)) void j_ind(void)
{
    __asm__ volatile(
        "mrs r12, apsr\n"
        "str r12, [r9, %[sv]]\n"
        ".global j_ind_key\n"
#ifdef T18_JT_MUL
        /* 1.6.8 (test43): multiplicative hash; the old one (key ^ key >> 10) put 4 of the 10 busiest return
         * addresses of the egg scene into one set (100000 misses a second) */
        "j_ind_key: movw lr, #0x79B1\n"
        "movt lr, #0x9E37\n"
        "mul lr, r11, lr\n"
#else
        "j_ind_key: eor lr, r11, r11, lsr #10\n"
#endif
#ifdef T18_JT2
        /* 1.6.8: two ways per set (the egg scene has 137 targets, but direct mapped 63000 misses a second) */
#ifdef T18_JT_MUL
        "lsr lr, lr, #" JT_STR(JT_SH) "\n"
#else
        "ubfx lr, lr, #1, #" JT_STR(JT_SB) "\n"
#endif
        "ldr r12, [r9, %[jt]]\n"
        "add r12, r12, lr, lsl #4\n"
        "ldr lr, [r12]\n"
        "cmp lr, r11\n"
        "beq 2f\n"
        "ldr lr, [r12, #8]!\n"
        "cmp lr, r11\n"
        "bne 1f\n"
        "2: ldr r12, [r12, #4]\n"
#else
        "ubfx lr, lr, #1, #" JT_STR(JT_BITS) "\n"
        "ldr r12, [r9, %[jt]]\n"
        "add r12, r12, lr, lsl #3\n"
        "ldr lr, [r12]\n"
        "cmp lr, r11\n"
        "bne 1f\n"
        "ldr r12, [r12, #4]\n"
#endif
        "ldr lr, [r9, %[sv]]\n"
        "msr apsr_nzcvq, lr\n"
        "bx r12\n"
        "1: ldr lr, [r9, %[sv]]\n"
        "msr apsr_nzcvq, lr\n"
        "push {r0-r3, r12, lr}\n"
        "mrs r12, apsr\n"
        "push {r12}\n"
        "mov r0, r11\n"
        "mov r1, r10\n"
        "ldr r2, =t18_jit_lookup\n"
        "blx r2\n"
        "mov r11, r0\n"
        "pop {r12}\n"
        "msr apsr_nzcvq, r12\n"
        "pop {r0-r3, r12, lr}\n"
        "bx r11\n"
        : : [sv] "i"(offsetof(arm7_t, jsave)), [jt] "i"(offsetof(arm7_t, jtab)));
}
#if defined(T18_JIT_VERIFY) || defined(T18_NO_CHAIN)
GLUE __attribute__((naked, used)) void j_indbx_plain(void)
{
    __asm__ volatile(
        "mrs r12, apsr\n"
        "tst r11, #1\n"
        "it eq\n"
        "biceq r11, r11, #3\n"
        "msr apsr_nzcvq, r12\n"
        "b t18_jit_exit\n");
}
#endif
#ifdef T18_HOLE_FAST
_Static_assert(T18_HOLE_LO == 0xE600u && T18_HOLE_HI == 0x14400u && T18_HOLE_PG == 512u, "j_slow: hole constants");
#endif
#ifdef T18_FF_FAST
uint8_t t18_fff[128];                     /* 1: guest flash block empty and not being written (platform keeps it) */
const uint32_t t18_ffw = 0xFFFFFFFFu;
#endif
#ifdef T18_SF_ASM
/* for j_slow, in this order: SPI flash answer byte, status (0xC0150004) and control (0xC0150000) registers */
struct { uint8_t *o; uint32_t *r, *c; } t18_sfp;
uint32_t *tg18_sf_regp(void *ctx);
uint32_t *tg18_sf_ctlp(void *ctx);
uint8_t *tg18_sf_outp(void *ctx);
#endif
/* memory slow path: "push {lr}; bl j_slow; .word info; .word key" (lr may hold the guest flags) */
GLUE __attribute__((naked, used)) void j_slow(void)
{
    __asm__ volatile(
        "push {r0-r7, lr}\n"
#ifdef T18_MEM_HELPER
        /* the guest flags stay as they were (j_chkfN: flags live around the access) */
        "mrs r0, apsr\n"
        "str r0, [r9, %[sv]]\n"
#endif
        /* loads from the flash map: here, without C */
        "ldr r12, [lr, #-1]\n"
#ifdef T18_HOLE_FAST
        /* 0xD0000064 (SPI busy, always 0) is polled very often: answer here, first */
        "movw r0, #0x0064\n"
        "movt r0, #0xD000\n"
        "bic r1, r11, #3\n"
        "cmp r1, r0\n"
        "bne 14f\n"
        "tst r12, #0x80\n"
        "beq 9f\n"
        "movs r0, #0\n"
        "b 4f\n"
        "14:\n"
        /* 1.6.6: the guest's bytes in the hole (pages of 512 bytes): aligned loads and stores here, without C.
         * A store into a page not made yet goes to C (it makes the page). */
        "movw r1, #0xE600\n"
        "movt r1, #0xF800\n"
        "sub r0, r11, r1\n"
        "cmp r0, #0x5E00\n"
        "bhs 6f\n"
        "ubfx r1, r12, #4, #2\n"
        "movs r2, #1\n"
        "lsls r2, r2, r1\n"
        "subs r2, #1\n"
        "tst r0, r2\n"
        "bne 9f\n"
        "lsrs r2, r0, #9\n"
        "ldr r3, =t18_hole_pg\n"
        "ldr r3, [r3, r2, lsl #2]\n"
        "ubfx r0, r0, #0, #9\n"
        "tst r12, #0x80\n"
        "beq 8f\n"
        "cmp r3, #0\n"
        "beq 7f\n"
        "add r3, r3, r0\n"
        "b 1f\n"
        "7: movs r0, #0\n"                            /* page not made: reads 0 */
        "b 4f\n"
        "8: cmp r3, #0\n"
        "beq 9f\n"
        "add r3, r3, r0\n"
        "and r0, r12, #15\n"
        "cmp r0, #8\n"
        "ite lo\n"
        "ldrlo r0, [sp, r0, lsl #2]\n"
        "ldrhs r0, [r9, r0, lsl #2]\n"
        "cmp r1, #1\n"
        "beq 11f\n"
        "bhi 12f\n"
        "strb r0, [r3]\n"
        "b 13f\n"
        "11: strh r0, [r3]\n"
        "b 13f\n"
        "12: str r0, [r3]\n"
        "b 13f\n"
        "6:\n"
#ifdef T18_SF_ASM
        /* 1.6.8: the SPI flash registers in word accesses here, without the C slow path: loads of the
         * control (0xC0150000), status (0xC0150004, busy bit 3 always clear) and answer (0xC015000C),
         * stores to the control (chip select) and to 0xC0150008 (send a byte). The firmware reads its
         * pictures byte by byte through them. */
        "bic r1, r11, #3\n"
        "movw r0, #0x0000\n"
        "movt r0, #0xC015\n"
        "subs r0, r1, r0\n"
        "cmp r0, #12\n"
        "bhi 21f\n"
        "and r2, r12, #0x30\n"
        "cmp r2, #0x20\n"
        "bne 21f\n"
        "tst r11, #3\n"
        "bne 21f\n"
        "ldr r2, =t18_sfp\n"
        "ldr r3, [r2]\n"
        "cbz r3, 21f\n"
        "tst r12, #0x80\n"
        "beq 23f\n"
        "cmp r0, #8\n"                               /* loads */
        "beq 21f\n"
        "bhi 22f\n"
        "ldr r1, [r2, #8]\n"                         /* control */
        "ldr r2, [r2, #4]\n"                         /* status */
        "cbz r0, 24f\n"
        "ldr r0, [r2]\n"
        "bic r0, r0, #8\n"
        "b 4f\n"
        "24: ldr r0, [r1]\n"
        "b 4f\n"
        "22: ldrb r0, [r3]\n"
        "b 4f\n"
        "23: ldr r2, =tg18_sf_put\n"                 /* stores */
        "cmp r0, #8\n"
        "beq 25f\n"
        "cmp r0, #0\n"
        "bne 21f\n"
        "ldr r2, =tg18_sf_ctl\n"
        "25: and r0, r12, #15\n"
        "cmp r0, #8\n"
        "ite lo\n"
        "ldrlo r1, [sp, r0, lsl #2]\n"
        "ldrhs r1, [r9, r0, lsl #2]\n"
        "ldr r0, [r9, %[cx]]\n"
        "push {r12, lr}\n"
        "blx r2\n"
        "pop {r12, lr}\n"
        "b 13f\n"
        "21:\n"
#endif
#endif
        "tst r12, #0x80\n"
        "beq 9f\n"
#ifndef T18_HOLE_FAST
        /* 0xD0000064 (SPI busy, always 0) is polled very often: answer here */
        "movw r0, #0x0064\n"
        "movt r0, #0xD000\n"
        "bic r1, r11, #3\n"
        "cmp r1, r0\n"
        "bne 5f\n"
        "movs r0, #0\n"
        "b 4f\n"
#endif
        "5: sub r0, r11, #0x20000000\n"
        "cmp r0, #0x800000\n"
        "bhs 9f\n"
        "ubfx r1, r12, #4, #2\n"
        "movs r2, #1\n"
        "lsls r2, r2, r1\n"
        "subs r2, #1\n"
        "tst r0, r2\n"
        "bne 9f\n"
        "lsrs r2, r0, #16\n"
        "ldr r3, =t18_fblk\n"
        "ldr r3, [r3, r2, lsl #2]\n"
#ifdef T18_FF_FAST
        /* test11: a block that is empty in the firmware reads 0xFF (the firmware looks there very often) */
        "cmp r3, #0\n"
        "bne 17f\n"
        "ldr r3, =t18_fff\n"
        "ldrb r3, [r3, r2]\n"
        "cmp r3, #0\n"
        "beq 9f\n"
        "ldr r3, =t18_ffw\n"
        "b 1f\n"
        "17:\n"
#else
        "cbz r3, 9f\n"
#endif
        "uxth r0, r0\n"
        "add r3, r3, r0\n"
        "1: cmp r1, #1\n"
        "beq 2f\n"
        "bhi 3f\n"
        "ldrb r0, [r3]\n"
        "tst r12, #0x40\n"
        "it ne\n"
        "sxtbne r0, r0\n"
        "b 4f\n"
        "2: ldrh r0, [r3]\n"
        "tst r12, #0x40\n"
        "it ne\n"
        "sxthne r0, r0\n"
        "b 4f\n"
        "3: ldr r0, [r3]\n"
        "4: and r1, r12, #15\n"
        "cmp r1, #8\n"
        "ite lo\n"
        "strlo r0, [sp, r1, lsl #2]\n"
        "strhs r0, [r9, r1, lsl #2]\n"                 /* r8..r14 live in arm7_t */
        "13: bic r2, lr, #1\n"
        "lsrs r12, r12, #20\n"
#ifdef T18_MEM_HELPER
        "cmp r12, #0xF00\n"                           /* 0xF00 + n: the join is n halfwords after the data */
        "bhs 15f\n"
#endif
        "sub r12, r2, r12, lsl #1\n"
        "orr r12, r12, #1\n"
#ifdef T18_MEM_HELPER
        "b 16f\n"
        "15: sub r12, r12, #0xF00\n"
        "add r12, r2, r12, lsl #1\n"
        "orr r12, r12, #1\n"
        "16:\n"
#endif
#ifdef T18_MEM_HELPER
        "ldr r0, [r9, %[sv]]\n"
        "msr apsr_nzcvq, r0\n"
#endif
        "pop {r0-r7}\n"
        "add sp, #4\n"
        "pop {lr}\n"
        "bx r12\n"
        "9: mov r0, sp\n"
        "mov r1, r10\n"
        "mov r2, r11\n"
        "ldr r3, =t18_jh_slow\n"
        "blx r3\n"
        "mov r12, r0\n"
#ifdef T18_MEM_HELPER
        "ldr r0, [r9, %[sv]]\n"
        "msr apsr_nzcvq, r0\n"
#endif
        "pop {r0-r7}\n"
        "add sp, #4\n"
        "pop {lr}\n"
        "bx r12\n"
        ".ltorg\n" : : [sv] "i"(offsetof(arm7_t, jsave)), [cx] "i"(offsetof(arm7_t, ctx)));
}

#ifdef T18_MEM_HELPER
#ifdef T18_CHK_FLASH
/* 1.6.8: aligned loads from the guest flash (pictures) straight from the block pointer, without j_slow.
 * In: r11 address, lr data (info: bit 7 load). Out on a hit: r12 host address, back behind the data
 * (FL: restores the guest flags first). Else falls through with r11, lr as they were. */
#define CHK_FLASH(SH, FL) \
        "sub r12, r11, #0x20000000\n" \
        "cmp r12, #0x800000\n" \
        "bhs 7f\n" \
        "tst r11, #((1 << " #SH ") - 1)\n" \
        "bne 7f\n" \
        "push {lr}\n" \
        "ldr lr, [lr, #-1]\n" \
        "tst lr, #0x80\n" \
        "beq 6f\n" \
        "lsr lr, r12, #16\n" \
        "ldr r12, =t18_fblk\n" \
        "ldr r12, [r12, lr, lsl #2]\n" \
        "cmp r12, #0\n" \
        "beq 6f\n" \
        "ubfx lr, r11, #0, #16\n" \
        "add r12, r12, lr\n" \
        FL \
        "pop {lr}\n" \
        "add lr, lr, #8\n" \
        "bx lr\n" \
        ".ltorg\n" \
        "6: pop {lr}\n" \
        "7:\n"
#else
#define CHK_FLASH(SH, FL) ""
#endif
#ifdef T18_CHK_HOLE
/* 1.6.8: aligned loads and stores of the guest's bytes in the hole (pages of 512 bytes) without j_slow,
 * when the page is made. Same contract as CHK_FLASH; r11 is rebuilt before falling through. */
#define CHK_HOLE(SH, FL) \
        "sub r12, r11, #0xF8000000\n" \
        "sub r12, r12, #0xE600\n" \
        "cmp r12, #0x5E00\n" \
        "bhs 9f\n" \
        "tst r11, #((1 << " #SH ") - 1)\n" \
        "bne 9f\n" \
        "push {lr}\n" \
        "lsr lr, r12, #9\n" \
        "ubfx r12, r12, #0, #9\n" \
        "ldr r11, =t18_hole_pg\n" \
        "ldr r11, [r11, lr, lsl #2]\n" \
        "cmp r11, #0\n" \
        "beq 8f\n" \
        "10: add r12, r12, r11\n" \
        FL \
        "pop {lr}\n" \
        "add lr, lr, #8\n" \
        "bx lr\n" \
        "8: ldr r11, [sp]\n" \
        "ldr r11, [r11, #-1]\n" \
        "tst r11, #0x80\n" \
        "itt ne\n" \
        "ldrne r11, =t18_zero_pg\n" \
        "bne 10b\n" \
        "add r12, r12, lr, lsl #9\n" \
        "add r12, r12, #0xE600\n" \
        "add r11, r12, #0xF8000000\n" \
        "pop {lr}\n" \
        "b 9f\n" \
        ".ltorg\n" \
        "9:\n"
_Static_assert(T18_HOLE_LO == 0xE600u && T18_HOLE_HI == 0x14400u && T18_HOLE_PG == 512u, "CHK_HOLE: constants");
#else
#define CHK_HOLE(SH, FL) ""
#endif
/* 1.6.6: the SRAM check of a load/store as a shared routine instead of 30 bytes in every block:
 * "bl j_chkN; .word info; .word key; ldr/str rt, [r12]" (flags not live, lr free). r11 = guest address.
 * In range: r12 = host address, back behind the data. Else the slow path, which comes back behind the
 * load/store (info: 0xF00 + halfwords from the data to there). N: 0 byte, 1 halfword, 2 word. */
#define J_CHK(N, SH) \
GLUE __attribute__((naked, used)) void j_chk##N(void) \
{ \
    __asm__ volatile( \
        "add r12, r11, #0x08000000\n" \
        ".if " #SH "\n ror r12, r12, #" #SH "\n .endif\n" \
        "cmp r12, #(0x20000 >> " #SH ")\n" \
        "bhs 1f\n" \
        "sub r12, r12, #(0xE600 >> " #SH ")\n" \
        "cmp r12, #(0x5E00 >> " #SH ")\n" \
        "blo 1f\n" \
        "add r12, r11, #0x20000000\n" \
        "add lr, lr, #8\n" \
        "bx lr\n" \
        "1:\n" \
        CHK_HOLE(SH, "") \
        CHK_FLASH(SH, "") \
        "push {lr}\n" \
        "b j_slow\n"); \
}
J_CHK(0, 0)
J_CHK(1, 1)
J_CHK(2, 2)
/* the same when the guest flags are live: they are kept in arm7_t.jsave around the compares */
#define J_CHKF(N, SH) \
GLUE __attribute__((naked, used)) void j_chkf##N(void) \
{ \
    __asm__ volatile( \
        "mrs r12, apsr\n" \
        "str r12, [r9, %[sv]]\n" \
        "add r12, r11, #0x08000000\n" \
        ".if " #SH "\n ror r12, r12, #" #SH "\n .endif\n" \
        "cmp r12, #(0x20000 >> " #SH ")\n" \
        "bhs 1f\n" \
        "sub r12, r12, #(0xE600 >> " #SH ")\n" \
        "cmp r12, #(0x5E00 >> " #SH ")\n" \
        "blo 1f\n" \
        "add r12, r11, #0x20000000\n" \
        "ldr r11, [r9, %[sv]]\n" \
        "msr apsr_nzcvq, r11\n" \
        "add lr, lr, #8\n" \
        "bx lr\n" \
        "1:\n" \
        CHK_HOLE(SH, "ldr r11, [r9, %[sv]]\n msr apsr_nzcvq, r11\n") \
        CHK_FLASH(SH, "ldr r11, [r9, %[sv]]\n msr apsr_nzcvq, r11\n") \
        "ldr r12, [r9, %[sv]]\n" \
        "msr apsr_nzcvq, r12\n" \
        "push {lr}\n" \
        "b j_slow\n" : : [sv] "i"(offsetof(arm7_t, jsave))); \
}
J_CHKF(0, 0)
J_CHKF(1, 1)
J_CHKF(2, 2)
_Static_assert(T18_HOLE_LO == 0xE600u && T18_HOLE_HI == 0x14400u && T18_SRAM_SIZE == 0x20000u, "j_chk: constants");
#endif
/* helpers: cycles as the interpreter would have them */
static inline void j_sync(int32_t bud, uint32_t before) { JCPU->cycles = J_CYC0 + (uint64_t)(uint32_t)(J_BUD0 - bud + (int32_t)before) * J_MULT; }
/* info: 0..3 rt, 4..5 size (0 byte 1 half 2 word), 6 sign, 7 load, 8 flags live, 9..19 cycles before,
 * 20..31 halfwords from the data back to the join point. Second word: key of the instruction
 * (ARM: the next instruction is 4 further). */
#ifdef T18_JIT_VERIFY
static uint32_t t18_vf_st;
#endif
int tg18_sf_fast(void *ctx, uint32_t a, uint32_t *v, int wr);
/* where the block goes on after a slow access: info bits 20..31 halfwords back from the data, or
 * (T18_MEM_HELPER) 0xF00 + n: n halfwords after it */
static inline const uint16_t *jh_join(const uint16_t *data, uint32_t info)
{
    uint32_t b = info >> 20;
#ifdef T18_MEM_HELPER
    if (b >= 0xF00u) return data + (b - 0xF00u);
#endif
    return data - b;
}
__attribute__((used)) uint32_t t18_jh_slow(uint32_t *frame, int32_t bud, uint32_t a)
{
    /* the data words after the bl are only halfword aligned: LDRD/LDM on them fault on the device (1.6.3) */
    const volatile uint16_t *data = (const volatile uint16_t *)(frame[8] & ~1u);
    uint32_t info = data[0] | (uint32_t)data[1] << 16, key = data[2] | (uint32_t)data[3] << 16;
    uint32_t rt = info & 15, sz = (info >> 4) & 3, before = (info >> 9) & 0x7FF;
#ifdef T18_SLOW_HIST
#ifdef T18_SLOW_HIST_EXACT
    { uint32_t k = (a & ~3u) | (info >> 7 & 1u);
#else
    { uint32_t k = (a & ~0xFFu) | (info >> 7 & 1u);                  /* 256-byte pages, bit 0: load */
#endif
      int i = 0; while (i < SLOW_HIST_N && t18_slow_hist[2 * i] && t18_slow_hist[2 * i] != k) i++;
      if (i < SLOW_HIST_N) { t18_slow_hist[2 * i] = k; t18_slow_hist[2 * i + 1]++; } }
#endif
#ifdef T18_SF_FAST
    if ((a & 0xFFFFFFE3u) == 0xC0150000u && sz == 2) {
        uint32_t v = (info & 0x80) ? 0 : (rt < 8 ? frame[rt] : JCPU->r[rt]);
        if (tg18_sf_fast(JCPU->ctx, a, &v, !(info & 0x80))) {
            t18_jit_stat[4]++;
            if (info & 0x80) { if (rt < 8) frame[rt] = v; else JCPU->r[rt] = v; }
            return (uint32_t)(uintptr_t)jh_join((const uint16_t *)data, info) | 1u;
        }
    }
#endif
#ifdef T18_IO_FAST
    if ((info & 0x80) && sz == 2 && !(a & 3) && !t18_new_off) {
        extern int tg18_io_fast(void *ctx, uint32_t a, uint32_t *v);
        uint32_t v;
        if (tg18_io_fast(JCPU->ctx, a, &v)) {
            t18_jit_stat[4]++;
            if (rt < 8) frame[rt] = v; else JCPU->r[rt] = v;
            return (uint32_t)(uintptr_t)jh_join((const uint16_t *)data, info) | 1u;
        }
    }
#endif

    j_sync(bud, before);
    JCPU->last_pc = key & ~1u;
    t18_jit_stat[4]++;
    if (a >= 0xC0000000u && a < 0xE0000000u) {
        /* device registers: straight to the SoC */
        if (info & 0x80) {
            uint32_t v;
            if (sz == 2) { v = JCPU->rd(JCPU->ctx, a & ~3u, 4); if (a & 3) v = (v >> (8 * (a & 3))) | (v << (32 - 8 * (a & 3))); }
            else if (sz == 1) { v = JCPU->rd(JCPU->ctx, a & ~1u, 2) & 0xFFFF; if (info & 0x40) v = (uint32_t)(int32_t)(int16_t)v; }
            else { v = JCPU->rd(JCPU->ctx, a, 1) & 0xFF; if (info & 0x40) v = (uint32_t)(int32_t)(int8_t)v; }
            if (rt < 8) frame[rt] = v; else JCPU->r[rt] = v;
        } else {
            uint32_t v = rt < 8 ? frame[rt] : JCPU->r[rt];
#ifdef T18_JIT_VERIFY
            t18_vf_st++;
#endif
            if (sz == 2) JCPU->wr(JCPU->ctx, a & ~3u, v, 4);
            else if (sz == 1) JCPU->wr(JCPU->ctx, a & ~1u, v & 0xFFFF, 2);
            else JCPU->wr(JCPU->ctx, a, v & 0xFF, 1);
        }
    } else if (info & 0x80) {
        uint32_t v;
        if (sz == 2) { v = arm7_read32(JCPU, a & ~3u); if (a & 3) v = (v >> (8 * (a & 3))) | (v << (32 - 8 * (a & 3))); }
        else if (sz == 1) { v = arm7_read16(JCPU, a & ~1u); if (info & 0x40) v = (uint32_t)(int32_t)(int16_t)v; }
        else { v = arm7_read8(JCPU, a); if (info & 0x40) v = (uint32_t)(int32_t)(int8_t)v; }
        if (rt < 8) frame[rt] = v; else JCPU->r[rt] = v;
    } else {
        uint32_t v = rt < 8 ? frame[rt] : JCPU->r[rt];
#ifdef T18_JIT_VERIFY
        t18_vf_st++;
#endif
        if (sz == 2) arm7_write32(JCPU, a, v);
        else if (sz == 1) arm7_write16(JCPU, a, v);
        else arm7_write8(JCPU, a, (uint8_t)v);
    }
    if (JCPU->cyc_mult != J_MULT) {
        /* clock changed: the rest of this instruction and everything after it at the new rate */
        J_CYC0 = JCPU->cycles; J_BUD0 = bud - (int32_t)before; J_MULT = JCPU->cyc_mult;
        goto out;
    }
    if ((JCPU->irq_line && !(JCPU->cpsr & ARM7_I)) || (JCPU->fiq_line && !(JCPU->cpsr & ARM7_F)) || JCPU->halted) {
    out:
        t18_link_pc = key + ((key & 1) ? 2u : 4u);
        t18_link_cyc = before + ((info & 0x80) ? 3u : 2u);
        return (uint32_t)(uintptr_t)((info & 0x100) ? (void *)t18_jit_exit_link_fl : (void *)t18_jit_exit_link) | 1u;
    }
    return (uint32_t)(uintptr_t)jh_join((const uint16_t *)data, info) | 1u;
}

/* ------------------------------------------------------------------ emission basics */
#define RC 9            /* arm7_t * */
#define RB 10           /* budget */
#define S1 11
#define S2 12
#define SLR 14
#define FN 8u
#define FZ 4u
#define FC 2u
#define FV 1u
typedef struct { uint16_t *at, *at2; uint32_t kind, a, b, c; } jfix_t;    /* stubs placed after the block (at2: a second branch, lo) */
#ifndef JF_MAX
#define JF_MAX (J_MAXI + 8)              /* one per instruction at most */
#endif
static jfix_t JF[JF_MAX]; static int JFN;

static inline const uint16_t *fn16(void (*f)(void)) { return (const uint16_t *)((uintptr_t)f & ~1u); }
static void E_b(const uint16_t *to) { uint16_t *at = J; J += 2; t2_b_at(at, to); }
static void E_bl(const uint16_t *to) { uint16_t *at = J; J += 2; t2_bl_at(at, to); }
static void E_word(uint32_t w) { E16(w & 0xFFFF); E16(w >> 16); }
static uint8_t cond_reads(int cond)
{
    static const uint8_t t[16] = { FZ, FZ, FC, FC, FN, FN, FV, FV, FC | FZ, FC | FZ, FN | FV, FN | FV, FN | FZ | FV, FN | FZ | FV, 0, 0 };
    return t[cond & 15];
}

/* guest register into a host register (returns the host register); pcval = value read as pc */
static int get_reg(int r, int tmp, uint32_t pcval)
{
    if (r < 8) return r;
    if (r == 13) { t2_addi(tmp, 8, 0xE0000000u); return tmp; }
    if (r == 15) { t2_mov32(tmp, pcval); return tmp; }
    t2_ldst_i(1, 2, 0, tmp, RC, 4u * (uint32_t)r); return tmp;
}
static void set_reg(int r, int src)
{
    if (r < 8) { t2_mov(r, src); return; }
    if (r == 13) { t2_addi(8, src, 0x20000000u); return; }
    t2_ldst_i(0, 2, 0, src, RC, 4u * (uint32_t)r);
}
/* static exit: take the cycles off, then link to the target (key) */
static void emit_exit_plain(uint32_t key);
static void emit_exit_key(uint32_t key, uint32_t cyc)
{
    if (cyc) t2_addi(RB, RB, 0u - cyc);
#if defined(T18_JIT_VERIFY) || defined(T18_NO_CHAIN)
    emit_exit_plain(key); return;
#endif
    E_bl(fn16(j_chain));
    E_word(key);
}
/* indirect exit: target in host register r. mode 0: Thumb (key = r | 1), 1: BX (mode from bit 0), 2: ARM (key = r & ~3) */
static void emit_exit_reg(int r, uint32_t cyc, int mode)
{
    if (cyc) t2_addi(RB, RB, 0u - cyc);
#if defined(T18_JIT_VERIFY) || defined(T18_NO_CHAIN)
    if (mode == 1) { t2_mov(S1, r); E_b(fn16(j_indbx_plain)); return; }
    if (mode == 0) t2_dpimm(T2_ORR, 0, S1, r, 1);
    else t2_dpimm(T2_BIC, 0, S1, r, 3);
    E_b(fn16(t18_jit_exit)); return;
#endif
    if (mode == 1) { t2_mov(S1, r); E_b(fn16(j_indbx)); return; }
    if (mode == 0) t2_dpimm(T2_ORR, 0, S1, r, 1);
    else t2_dpimm(T2_BIC, 0, S1, r, 3);
    E_b(fn16(j_ind));
}
static void emit_exit_plain(uint32_t key)
{
    t2_mov32(S1, key);
    E_b(fn16(t18_jit_exit));
}
/* load/store through r11 (guest address) into/from host rt (r0..r7). Fast path: SRAM, aligned. */
static void emit_mem(uint32_t key, int load, int size, int sign, int rt, uint32_t before, int flive)
{
#ifdef T18_JIT_EXACT
    flive = 1;
#endif
#ifndef T18_MEM_HELPER_SIZES
#define T18_MEM_HELPER_SIZES 7u              /* bit per access size (byte, halfword, word) that uses the routine */
#endif
#ifdef T18_MEM_HELPER
    if (((T18_MEM_HELPER_SIZES >> size) & 1u) && j_compact && !j_inl) {
        static void (*const chk[2][3])(void) = { { j_chk0, j_chk1, j_chk2 }, { j_chkf0, j_chkf1, j_chkf2 } };
        E_bl(fn16(chk[flive ? 1 : 0][size]));
        uint16_t *data = J; J += 4;
        if (rt < 8) t2_ldst_i(load, size, sign, rt, S2, 0);
        else if (load) { t2_ldst_i(1, size, sign, S1, S2, 0); t2_ldst_i(0, 2, 0, S1, RC, 4u * (uint32_t)rt); }
        else { t2_ldst_i(1, 2, 0, S1, RC, 4u * (uint32_t)rt); t2_ldst_i(0, size, 0, S1, S2, 0); }
        uint32_t fwd = (uint32_t)(J - data);
        uint32_t info = (uint32_t)rt | (uint32_t)size << 4 | (uint32_t)sign << 6 | (uint32_t)load << 7 | before << 9 | (0xF00u + fwd) << 20;
        uint16_t *keep = J; J = data; E_word(info); E_word(key); J = keep;
        return;
    }
#endif
    if (flive) t2_mrs(SLR);
    t2_addi(S2, S1, 0x08000000u);                                   /* offset in SRAM */
    int sh = size == 2 ? 2 : size;
    if (size) t2_shi(3, S2, S2, sh);                                /* misaligned -> large */
    t2_cmpi(S2, t2_modimm(T18_SRAM_SIZE >> sh));
    uint16_t *br = J, *br2 = 0; J += 2;
    if (T18_HOLE_HI > T18_HOLE_LO) {                                /* not the hole (the emulator's own data is there) */
        t2_addi(S2, S2, 0u - (T18_HOLE_LO >> sh));
        t2_cmpi(S2, t2_modimm((T18_HOLE_HI - T18_HOLE_LO) >> sh));
        br2 = J; J += 2;
    }
    t2_addi(S2, S1, 0x20000000u);                                   /* host address */
    if (rt < 8) t2_ldst_i(load, size, sign, rt, S2, 0);
    else if (load) { t2_ldst_i(1, size, sign, S1, S2, 0); t2_ldst_i(0, 2, 0, S1, RC, 4u * (uint32_t)rt); }   /* r8..r14 live in arm7_t */
    else { t2_ldst_i(1, 2, 0, S1, RC, 4u * (uint32_t)rt); t2_ldst_i(0, size, 0, S1, S2, 0); }
    uint16_t *join = J;
    if (flive) t2_msr(SLR);
    uint32_t info = (uint32_t)rt | (uint32_t)size << 4 | (uint32_t)sign << 6 | (uint32_t)load << 7 | (uint32_t)flive << 8 | before << 9;
    JF[JFN++] = (jfix_t){ br, br2, 1, info, (uint32_t)(uintptr_t)join, key };
}
/* MSR CPSR_c from a block: "mrs lr; push {lr}; bl j_msr; .word info; .word key; msr lr", value in r11 */
uint32_t t18_jh_msr(uint32_t *frame, int32_t bud, uint32_t v);
GLUE __attribute__((naked, used)) void j_msr(void)
{
    __asm__ volatile(
        "push {r0-r7, lr}\n"
        "mov r0, sp\n"
        "mov r1, r10\n"
        "mov r2, r11\n"
        "ldr r3, =t18_jh_msr\n"
        "blx r3\n"
        "mov r12, r0\n"
        "pop {r0-r7}\n"
        "add sp, #4\n"
        "pop {lr}\n"
        "bx r12\n"
        ".ltorg\n");
}
#ifdef T18_MSR_STAT
uint32_t t18_msr_why[6];
#endif
__attribute__((used)) uint32_t t18_jh_msr(uint32_t *frame, int32_t bud, uint32_t v)
{
    const volatile uint16_t *data = (const volatile uint16_t *)(frame[8] & ~1u);   /* halfword aligned, see t18_jh_slow */
    uint32_t info = data[0] | (uint32_t)data[1] << 16, key = data[2] | (uint32_t)data[3] << 16, before = (info >> 9) & 0x7FF;
    arm7_t *c = JCPU;
    j_sync(bud, before);
    c->cpsr &= ~ARM7_T;                                             /* stale after Thumb blocks; this is ARM code */
    uint32_t nc = (c->cpsr & ~0xFFu) | (v & 0xFFu);
    if ((c->cpsr & 0x1F) == 0x10) nc = c->cpsr;                    /* user mode: control bits stay */
#ifdef T18_MSR_STAT
    { extern uint32_t t18_msr_why[6]; t18_msr_why[0]++; if ((nc ^ c->cpsr) & 0x3F) t18_msr_why[1]++; else if ((c->irq_line && !(nc & ARM7_I)) || (c->fiq_line && !(nc & ARM7_F))) t18_msr_why[2]++; t18_msr_why[3] = nc; t18_msr_why[4] = c->cpsr; }
#endif
    if ((nc ^ c->cpsr) & 0x3F) {
        /* mode or T changes: the interpreter does this instruction */
        t18_link_pc = key; t18_link_cyc = before;
        return (uint32_t)(uintptr_t)t18_jit_exit_link_fl | 1u;
    }
    c->cpsr = nc;
    if ((c->irq_line && !(nc & ARM7_I)) || (c->fiq_line && !(nc & ARM7_F))) {
        t18_link_pc = key + 4; t18_link_cyc = before + 1;
        return (uint32_t)(uintptr_t)t18_jit_exit_link_fl | 1u;
    }
    return (uint32_t)(uintptr_t)(data + 4) | 1u;                    /* behind the two data words */
}
static void emit_slow(const jfix_t *f)
{
    t2_bcc_at(f->at, J, 2);                                          /* bhs here */
    if (f->at2) t2_bcc_at(f->at2, J, 3);                             /* blo here (the hole) */
    E16(0xB500u);                                                    /* push {lr} */
    E_bl(fn16(j_slow));
    uint16_t *data = J;
    uint32_t back = (uint32_t)(data - (uint16_t *)(uintptr_t)f->b);
    E_word(f->a | back << 20);
    E_word(f->c);
}
static int modimm_up(uint32_t *v)
{
    for (uint32_t x = *v; x < *v + 512; x++) if (t2_modimm(x) >= 0) { *v = x; return t2_modimm(x); }
    return -1;
}
/* block frame: guard before, stubs after */
static uint16_t *g_guard, *g_gfail, *g_spfail;
/* guest sp range in which host r8 (sp + 0x20000000) may be used directly: above the hole with 1 KB to
 * spare below (pushes), 4 KB below the top (sp + offset). t18_jit_try checks it before entering;
 * blocks that use r8 check it again, as they may be reached through links. */
#define T18_SP_LO (T18_HOLE_HI + 0x400u)
#define T18_SP_HI (T18_SRAM_SIZE - 0x1000u)
/* The guard keeps the flags even when the block overwrites them first: when it fails, the
 * interpreter goes on from here and the flags must be the guest's (an interrupt may save them). */
#ifdef T18_GFAIL_SHARED
/* 1.6.8: the guard's way out shared by all blocks instead of 16 bytes in each: "it lt; bllt j_gfail_a"
 * (budget) and "it hs; blhs j_gfail_b" (sp). r12 = the guest flags, lr = behind the bl; the key comes
 * from the block's header, j_gf_d[] bytes before lr. */
uint32_t j_gf_d[2];
#ifndef T18_JHDR
#error T18_GFAIL_SHARED needs T18_JHDR
#endif
#define J_GFAIL(N, I) \
GLUE __attribute__((naked, used)) void j_gfail_##N(void) \
{ \
    __asm__ volatile( \
        "msr apsr_nzcvq, r12\n" \
        "ldr r12, =j_gf_d\n" \
        "ldr r12, [r12, #" #I "]\n" \
        "bic lr, lr, #1\n" \
        "sub r12, lr, r12\n" \
        "ldrh r11, [r12]\n" \
        "ldrh r12, [r12, #2]\n" \
        "orr r11, r11, r12, lsl #16\n" \
        "b t18_jit_exit\n" \
        ".ltorg\n"); \
}
J_GFAIL(a, 0)
J_GFAIL(b, 4)
static uint16_t *g_start;
#endif
#ifdef T18_LOOPFAST
/* 1.6.8: a jump back to the block's own start (a loop) takes its cycles, compares the budget and goes on at
 * the body (after the guard and the sp check) instead of through the whole start. Only when the flags are not
 * read at the start and sp did not move before the jump. The body starts with a nop: dropping the block (idle
 * switch, tier up) turns it into "b start", so the loops go through the start again. Header bits 10..15 of
 * the guest length: the nop's place in halfwords from the start (0 = none). */
static uint16_t *g_lbody, *g_lstart;
#define HDR_GL(v) ((v) & 0x3FFu)
static void loop_nop_drop(uint16_t *code, uint32_t gl)
{
    uint32_t bo = gl >> 10;
    if (bo) JW(code + bo, 0xE000u | ((0u - (bo + 2u)) & 0x7FFu));       /* b code */
}
#else
#define HDR_GL(v) (v)
#define loop_nop_drop(code, gl) ((void)0)
#endif
static void block_begin(int livein, int usesp)
{
    JFN = 0;
#ifdef T18_GFAIL_SHARED
    g_start = J;
    t2_mrs(S2);
    g_guard = J; J += 2;
    E16(0xBFB8u);                                                    /* it lt */
    E_bl(fn16(j_gfail_a));
    j_gf_d[0] = (uint32_t)((char *)J - (char *)(g_start - 4));
    g_gfail = 0; g_spfail = 0;
    if (usesp) {
#ifdef T18_SPCHK_LD
        /* 1.6.8: the bound from arm7_t (8 bytes) instead of four adds (16 bytes) */
        t2_ldst_i(1, 2, 0, S1, RC, (uint32_t)offsetof(arm7_t, jsplo));
        t2_dpreg(T2_SUB, 0, S1, 8, S1, 0, 0);
#else
        t2_addi(S1, 8, 0u - (0x18000000u + T18_SP_LO));
#endif
        t2_cmpi(S1, t2_modimm(T18_SP_HI - T18_SP_LO));
        E16(0xBF28u);                                                /* it hs */
        E_bl(fn16(j_gfail_b));
        j_gf_d[1] = (uint32_t)((char *)J - (char *)(g_start - 4));
    }
    if (livein) t2_msr(S2);
    return;
#endif
    t2_mrs(SLR);
    g_guard = J; J += 2;
    g_gfail = J; J += 2;
    g_spfail = 0;
    if (usesp) {
        t2_addi(S2, 8, 0u - (0x18000000u + T18_SP_LO));
        t2_cmpi(S2, t2_modimm(T18_SP_HI - T18_SP_LO));
        g_spfail = J; J += 2;
    }
    if (livein) t2_msr(SLR);
}
static uint32_t block_end(uint32_t key, int livein, uint32_t maxcyc)
{
    (void)livein;
#ifdef T18_LOOPFAST
    int lmi;
#ifdef T18_OVERSHOOT
    { uint32_t one = 1; lmi = modimm_up(&one); }
#else
    { uint32_t lm = maxcyc + 2; lmi = modimm_up(&lm); }
#endif
#endif
    for (int k = 0; k < JFN; k++) {
        jfix_t *f = &JF[k];
#ifdef T18_LOOPFAST
        if (f->kind >= 300) {
            if (f->kind >= 400) t2_b_at(f->at, J); else t2_bcc_at(f->at, J, (int)(f->kind - 300));
            if (f->b) t2_addi(RB, RB, 0u - f->b);
            t2_cmpi(RB, lmi);
            { uint16_t *at = J; J += 2; t2_bcc_at(at, g_lbody, 10); }       /* bge body */
            E_b(g_lstart);                                               /* budget used up: through the start */
            continue;
        }
#endif
        if (f->kind >= 100) { t2_bcc_at(f->at, J, (int)(f->kind - 100)); emit_exit_key(f->a, f->b); }
        else emit_slow(f);
    }
    uint16_t *keep = J;
    uint32_t m = maxcyc + 2;
    int mi = modimm_up(&m);
#ifdef T18_OVERSHOOT
    /* 1.6.8: a block runs whenever budget is left (an event comes up to one block late, about 1 us) instead
     * of the rest of the run being interpreted */
    { uint32_t one = 1; (void)mi; J = g_guard; t2_cmpi(RB, modimm_up(&one)); }
#else
    J = g_guard; t2_cmpi(RB, mi);
#endif
    J = keep;
#ifdef T18_GFAIL_SHARED
    (void)key;
    return m;
#endif
    t2_bcc_at(g_gfail, J, 11);                                       /* lt */
    if (g_spfail) t2_bcc_at(g_spfail, J, 2);                         /* hs: sp outside the range */
    t2_msr(SLR);
    emit_exit_plain(key);
    return m;
}

/* ------------------------------------------------------------------ Thumb */
enum { K_COPY, K_HI, K_LDRPC, K_MEM, K_SPMEM, K_ADDPC, K_ADDSP, K_SPADJ, K_PUSH, K_POP, K_LDM, K_STM, K_BCC, K_B, K_BL };
typedef struct { uint32_t pc; uint32_t op; uint16_t op2; uint8_t kind, cyc, fr, fw; } jins_t;
static jins_t I[J_MAXI + 1];
static uint8_t live_after[J_MAXI + 1];

static int t_decode(arm7_t *c, uint32_t pc, jins_t *d)
{
    uint32_t op = arm7_read16(c, pc);
    d->pc = pc; d->op = op; d->op2 = 0; d->fr = 0; d->fw = 0; d->cyc = 1;
    switch (op >> 11) {
    case 0: case 1: case 2:
        d->kind = K_COPY; d->fw = FN | FZ;
        if ((op >> 11) != 0 || ((op >> 6) & 31)) d->fw |= FC;
        return 1;
    case 3: d->kind = K_COPY; d->fw = FN | FZ | FC | FV; return 1;
    case 4: d->kind = K_COPY; d->fw = FN | FZ; return 1;
    case 5: case 6: case 7: d->kind = K_COPY; d->fw = FN | FZ | FC | FV; return 1;
    case 8:
        if (!(op & (1u << 10))) {
            int alu = (op >> 6) & 15;
            d->kind = K_COPY;
            switch (alu) {
            case 2: case 3: case 4: case 7: d->fw = FN | FZ; d->cyc = 2; break;   /* C kept when the amount is 0 */
            case 5: case 6: d->fr = FC; d->fw = FN | FZ | FC | FV; break;
            case 9: case 10: case 11: d->fw = FN | FZ | FC | FV; break;
            case 13: d->fw = FN | FZ; d->cyc = 3; break;
            default: d->fw = FN | FZ; break;
            }
            return 1;
        }
        if ((op >> 10) == 0x11) {
            int sub = (op >> 8) & 3, rd = (op & 7) | ((op >> 4) & 8);
#ifdef T18_SPCHK_MOD
            if (rd == 13 && sub != 1 && !t18_new_off) return 0;           /* add/mov sp, rX: the interpreter (sp checked after it) */
#endif
            d->kind = K_HI;
            if (sub == 1) d->fw = FN | FZ | FC | FV;
            if (sub == 3 || ((sub == 0 || sub == 2) && rd == 15)) d->cyc = 3;
            return 1;
        }
        return 0;
    case 9: d->kind = K_LDRPC; d->cyc = 3; return 1;
    case 10: case 11: d->kind = K_MEM; d->cyc = ((op >> 9) & 7) <= 2 ? 2 : 3; return 1;
    case 12: case 14: case 16: d->kind = K_MEM; d->cyc = 2; return 1;
    case 13: case 15: case 17: d->kind = K_MEM; d->cyc = 3; return 1;
    case 18: d->kind = K_SPMEM; d->cyc = 2; return 1;
    case 19: d->kind = K_SPMEM; d->cyc = 3; return 1;
    case 20: d->kind = K_ADDPC; return 1;
    case 21: d->kind = K_ADDSP; return 1;
    case 22: case 23:
        if ((op & 0xFF00) == 0xB000) { d->kind = K_SPADJ; return 1; }
        if ((op & 0xF600) == 0xB400) {
            int n = __builtin_popcount(op & 0xFF) + ((op >> 8) & 1);
            d->kind = (op & 0x800) ? K_POP : K_PUSH; d->cyc = (uint8_t)(n + 2);
            return 1;
        }
        return 0;
    case 24: case 25: {
        int rb = (op >> 8) & 7, list = op & 0xFF;
        if (!list || (list & (1 << rb))) return 0;                  /* quirks: the interpreter does them */
        d->kind = (op & 0x800) ? K_LDM : K_STM; d->cyc = (uint8_t)(__builtin_popcount(list) + 2);
        return 1;
    }
    case 26: case 27: {
        int cond = (op >> 8) & 15;
        if (cond >= 14) return 0;
        d->kind = K_BCC; d->fr = cond_reads(cond); return 1;
    }
    case 28: d->kind = K_B; d->cyc = 3; return 1;
    case 30: {
        uint32_t op2 = arm7_read16(c, pc + 2);
        if ((op2 >> 11) != 31) return 0;
        d->op2 = (uint16_t)op2; d->kind = K_BL; d->cyc = 4; return 1;
    }
    }
    return 0;
}

static int translate_thumb(arm7_t *c, uint32_t pc, jent_t *e)
{
    int n = 0;
    uint32_t a = pc;
#ifdef T18_BLK_MAXCYC
    uint32_t bcyc = 0;
#endif
    while (n < J_MAXI) {
        jins_t *d = &I[n];
        if (n && (a | 1u) == j_idle_key) break;
#ifdef T18_BLK_MAXCYC
        /* 1.6.8 test: shorter blocks, so less is interpreted at the end of a run (before an interrupt) */
        if (bcyc >= T18_BLK_MAXCYC) break;
#endif
        if (!t_decode(c, a, d)) break;
#ifdef T18_BLK_MAXCYC
        bcyc += d->cyc;
#endif
        n++;
        if (d->kind == K_BL) { a += 4; break; }
        a += 2;
        if (d->kind == K_B) break;
        if (d->kind == K_HI) { int sub = (d->op >> 8) & 3, rd = (d->op & 7) | ((d->op >> 4) & 8); if (sub == 3 || ((sub == 0 || sub == 2) && rd == 15)) break; }
        if (d->kind == K_POP && (d->op & 0x100)) break;
    }
    if (n == 0) return 0;
    uint8_t live = 0xF;
    for (int i = n - 1; i >= 0; i--) {
        live_after[i] = live;
        live = (uint8_t)((live & ~I[i].fw) | I[i].fr);
        if (I[i].kind == K_BCC) live = 0xF;
    }
    int livein = live != 0, usesp = 0;
#ifdef T18_SPCHK_MOD
    /* 1.6.8: only blocks that move sp check it. sp is in range when translated code starts (t18_jit_try) and
     * moves only in blocks that check it at their start; within one block it moves less than the 1 KB to spare,
     * so a block that only reads or writes [sp, #imm] needs no check of its own. */
    for (int i = 0; i < n; i++) {
        int k = I[i].kind, op = (int)I[i].op;
        (void)op; if (k == K_PUSH || k == K_POP || k == K_SPADJ) usesp = 1;
        if (t18_new_off && k == K_SPMEM) usesp = 1;
    }
#else
    for (int i = 0; i < n; i++) if (I[i].kind == K_SPMEM || I[i].kind == K_PUSH || I[i].kind == K_POP) usesp = 1;
#endif
    if ((char *)J + n * 96 + 136 > J_LIMIT) return -1;
#ifdef T18_JHDR
    J += 4;                                                          /* header: key, guest length, host length */
#endif
    uint16_t *start = J;
    block_begin(livein, usesp);
    uint32_t cyc = 0, maxcyc = 0;
    int ended = 0;
#ifdef T18_LOOPFAST
    int spmod = 0; uint32_t own = pc | 1u;
    g_lbody = 0; g_lstart = start;
    if (!livein && own != j_idle_key) {
        int loops = 0;
        for (int i = 0; i < n; i++) {
            if (I[i].kind == K_BCC) { int32_t off = (int32_t)(int8_t)(I[i].op & 0xFF) * 2; if (((I[i].pc + 4 + (uint32_t)off) | 1u) == own) loops = 1; }
            if (I[i].kind == K_B) { int32_t off = ((int32_t)(I[i].op << 21) >> 20); if (((I[i].pc + 4 + (uint32_t)off) | 1u) == own) loops = 1; }
        }
        if (loops) { g_lbody = J; E16(0xBF00u); }                     /* nop: becomes "b start" when dropped */
    }
#endif
#ifdef T18_KSTAT
    extern uint32_t t18_kstat[2][20]; uint16_t *kj = J; int kk = 16;
    t18_kstat[0][18] += (uint32_t)(J - start); t18_kstat[1][18]++;
#endif
    for (int i = 0; i < n && !ended; i++) {
        const jins_t *d = &I[i];
        uint32_t op = d->op, ipc = d->pc;
        int fl = live_after[i] != 0;
#ifdef T18_KSTAT
        t18_kstat[0][kk] += (uint32_t)(J - kj); t18_kstat[1][kk]++; kj = J; kk = d->kind;
#endif
#ifdef T18_LOOPFAST
        if (d->kind == K_PUSH || d->kind == K_POP || d->kind == K_SPADJ
            || (d->kind == K_HI && ((op & 7) | ((op >> 4) & 8)) == 13 && ((op >> 8) & 3) != 1)) spmod = 1;
#endif
        switch (d->kind) {
        case K_COPY: E16(op); cyc += d->cyc; break;
        case K_HI: {
            int sub = (op >> 8) & 3, rd = (op & 7) | ((op >> 4) & 8), rs = (op >> 3) & 15;
            if (sub == 0) {
                if (rd == 13 && rs < 8) { t2_dpreg(T2_ADD, 0, 8, 8, rs, 0, 0); cyc += 1; break; }
                int x = get_reg(rd, S1, ipc + 4), y = get_reg(rs, S2, ipc + 4);
                int t = rd < 8 ? rd : S1;
                t2_dpreg(T2_ADD, 0, t, x, y, 0, 0);
                if (rd == 15) { cyc += 3; emit_exit_reg(t, cyc, 0); ended = 1; break; }
                set_reg(rd, t); cyc += 1;
            } else if (sub == 1) {
                int x = get_reg(rd, S1, ipc + 4), y = get_reg(rs, S2, ipc + 4);
                t2_dpreg(T2_SUB, 1, 15, x, y, 0, 0); cyc += 1;
            } else if (sub == 2) {
                int y = get_reg(rs, S2, ipc + 4);
                if (rd == 15) { cyc += 3; emit_exit_reg(y, cyc, 0); ended = 1; break; }
                set_reg(rd, y); cyc += 1;
            } else {
                int y = get_reg(rs, S2, ipc + 4);
                if (op & 0x80) { t2_mov32(S1, (ipc + 2) | 1); set_reg(14, S1); }
                cyc += 3; emit_exit_reg(y, cyc, 1); ended = 1;
            }
            break;
        }
        case K_LDRPC: {
            uint32_t la = ((ipc + 4) & ~3u) + (op & 0xFF) * 4;
            t2_mov32((op >> 8) & 7, arm7_read32(c, la)); cyc += 3; break;
        }
        case K_MEM: {
            int rd = op & 7, rb = (op >> 3) & 7, load, size, sign = 0;
            uint32_t f = op >> 11;
            if (f == 10 || f == 11) {
                int ro = (op >> 6) & 7, k = (op >> 9) & 7;
                t2_dpreg(T2_ADD, 0, S1, rb, ro, 0, 0);
                static const int8_t L[8] = { 0, 0, 0, 1, 1, 1, 1, 1 }, Z[8] = { 2, 1, 0, 0, 2, 1, 0, 1 }, G[8] = { 0, 0, 0, 1, 0, 0, 0, 1 };
                load = L[k]; size = Z[k]; sign = G[k];
            } else {
                uint32_t imm = (op >> 6) & 31;
                if (f == 12 || f == 13) { size = 2; imm *= 4; } else if (f == 14 || f == 15) size = 0; else { size = 1; imm *= 2; }
                load = f & 1;
                t2_addi(S1, rb, imm);
            }
            emit_mem(ipc | 1u, load, size, sign, rd, cyc, fl);
            cyc += d->cyc;
            break;
        }
        case K_SPMEM: t2_ldst_i((op >> 11) & 1, 2, 0, (op >> 8) & 7, 8, (op & 0xFF) * 4u); cyc += d->cyc; break;
        case K_ADDPC: t2_mov32((op >> 8) & 7, ((ipc + 4) & ~3u) + (op & 0xFF) * 4u); cyc += 1; break;
        case K_ADDSP: { int rd = (op >> 8) & 7; t2_addi(rd, 8, (op & 0xFF) * 4u); t2_addi(rd, rd, 0xE0000000u); cyc += 1; break; }
        case K_SPADJ: { uint32_t v = (op & 0x7F) * 4u; t2_addi(8, 8, (op & 0x80) ? 0u - v : v); cyc += 1; break; }
        case K_PUSH: {
            uint32_t list = op & 0xFF;
            if (op & 0x100) { t2_ldst_i(1, 2, 0, S1, RC, 56); list |= 1u << S1; }
            if (__builtin_popcount(list) == 1) { int r = __builtin_ctz(list); E32(0xF840u | 8u, (uint32_t)r << 12 | 0xD04u); }   /* str r, [r8, #-4]! */
            else t2_stmdb(8, list, 1);
            cyc += d->cyc; break;
        }
        case K_POP: {
            uint32_t list = op & 0xFF;
            if (op & 0x100) list |= 1u << S1;
            if (__builtin_popcount(list) == 1) { int r = __builtin_ctz(list); E32(0xF850u | 8u, (uint32_t)r << 12 | 0xB04u); }   /* ldr r, [r8], #4 */
            else t2_ldmia(8, list, 1);
            cyc += d->cyc;
            if (op & 0x100) { emit_exit_reg(S1, cyc, 0); ended = 1; }
            break;
        }
        case K_LDM: case K_STM: {
            /* SRAM only, else the interpreter does this instruction */
            int rb = (op >> 8) & 7; uint32_t list = op & 0xFF;
            if (fl) t2_mrs(SLR);
            t2_addi(S2, rb, 0x08000000u + 4u * (uint32_t)(__builtin_popcount(list) - 1)); t2_shi(3, S2, S2, 2); t2_cmpi(S2, t2_modimm(T18_SRAM_SIZE >> 2));   /* last word in SRAM */
            uint16_t *br = J, *br2 = 0; J += 2;
            if (T18_HOLE_HI > T18_HOLE_LO) {                        /* no word in the hole: last word not in [LO, HI + 4 (n - 1)) */
                uint32_t lim = (T18_HOLE_HI - T18_HOLE_LO + 4u * (uint32_t)(__builtin_popcount(list) - 1)) >> 2;
                int mi = modimm_up(&lim);
                t2_addi(S2, S2, 0u - (T18_HOLE_LO >> 2)); t2_cmpi(S2, mi);
                br2 = J; J += 2;
            }
            t2_addi(S2, rb, 0x20000000u);
            if (__builtin_popcount(list) == 1) {
                int r = __builtin_ctz(list);
                E32((d->kind == K_LDM ? 0xF850u : 0xF840u) | (uint32_t)S2, (uint32_t)r << 12 | 0xB04u);   /* ldr/str r, [r12], #4 */
            } else if (d->kind == K_LDM) t2_ldmia(S2, list, 1); else t2_stmia(S2, list, 1);
            t2_addi(rb, S2, 0xE0000000u);
            if (fl) t2_msr(SLR);
            uint16_t *skip = J; J += 2;
            t2_bcc_at(br, J, 2);
            if (br2) t2_bcc_at(br2, J, 3);
            if (fl) t2_msr(SLR);
            if (cyc) t2_addi(RB, RB, 0u - cyc);
            emit_exit_plain(ipc | 1u);
            t2_b_at(skip, J);
            cyc += d->cyc;
            break;
        }
        case K_BCC: {
            int cond = (op >> 8) & 15;
            int32_t off = (int32_t)(int8_t)(op & 0xFF) * 2;
            uint32_t tgt = ipc + 4 + (uint32_t)off;
            uint16_t *br = J; J += 2;
#ifdef T18_LOOPFAST
            if (g_lbody && !spmod && (tgt | 1u) == own) JF[JFN++] = (jfix_t){ br, 0, 300u + (uint32_t)cond, tgt | 1u, cyc + 3, 0 };
            else
#endif
            JF[JFN++] = (jfix_t){ br, 0, 100u + (uint32_t)cond, tgt | 1u, cyc + 3, 0 };
            if (cyc + 3 > maxcyc) maxcyc = cyc + 3;
            cyc += 1;
            break;
        }
        case K_B: {
            int32_t off = ((int32_t)(op << 21) >> 20);
            cyc += 3;
#ifdef T18_LOOPFAST
            if (g_lbody && !spmod && ((ipc + 4 + (uint32_t)off) | 1u) == own) { uint16_t *br = J; J += 2; JF[JFN++] = (jfix_t){ br, 0, 400u, own, cyc, 0 }; ended = 1; break; }
#endif
            emit_exit_key((ipc + 4 + (uint32_t)off) | 1u, cyc); ended = 1; break;
        }
        case K_BL: {
            int32_t hi = ((int32_t)(op << 21) >> 9);
            uint32_t lr = ipc + 4 + (uint32_t)hi;
            uint32_t tgt = (lr + ((d->op2 & 0x7FFu) << 1)) & ~1u;
            t2_mov32(S1, (ipc + 4) | 1); set_reg(14, S1);
            cyc += 4; emit_exit_key(tgt | 1u, cyc); ended = 1; break;
        }
        }
        if (cyc > maxcyc) maxcyc = cyc;
    }
#ifdef T18_KSTAT
    t18_kstat[0][kk] += (uint32_t)(J - kj); t18_kstat[1][kk]++; kj = J;
#endif
    if (!ended) emit_exit_key((I[n - 1].pc + (I[n - 1].kind == K_BL ? 4u : 2u)) | 1u, cyc);
#ifdef T18_KSTAT
    uint16_t *ke = J;
#endif
    uint32_t m = block_end(pc | 1u, livein, maxcyc);
#ifdef T18_KSTAT
    t18_kstat[0][19] += (uint32_t)(J - kj); t18_kstat[1][19]++; (void)ke;
#endif
    if ((char *)J > J_LIMIT) return -1;
    e->off = (uint16_t)(start - JC + 1); e->maxcyc = (uint16_t)m;
    { uint32_t gl = (I[n - 1].pc - I[0].pc) / 2u + 2u; e->len = (uint8_t)(gl > 255u ? 255u : gl);
#ifdef T18_JHDR
#ifdef T18_LOOPFAST
      if (gl > 0x3FFu) gl = 0x3FFu;
      if (g_lbody) gl |= (uint32_t)(g_lbody - start) << 10;
#endif
      JW(start - 4, e->key & 0xFFFFu); JW(start - 3, e->key >> 16); JW(start - 2, gl > 0xFFFFu ? 0xFFFFu : gl); JW(start - 1, (uint32_t)(J - start));
#endif
    }
    t18_jit_stat[0]++; t18_jit_stat[7] += (uint32_t)n; t18_jit_bytes += (uint32_t)((char *)J - (char *)start);
    return 1;
}

/* ------------------------------------------------------------------ ARM */
/* An ARM instruction: what it does, its cycles, flags read/written. 0 = not translated. */
enum { A_DP, A_LS, A_LSH, A_LDM, A_STM, A_B, A_BL, A_BX, A_MUL, A_MULL, A_MRS, A_MSR, A_XRET };
static int a_decode(arm7_t *c, uint32_t pc, jins_t *d)
{
    uint32_t op = arm7_read32(c, pc);
    d->pc = pc; d->op = op; d->fr = 0; d->fw = 0; d->cyc = 1;
    uint32_t cond = op >> 28;
    if (cond == 15) return 0;
    if (cond != 14) d->fr = cond_reads((int)cond);
    switch ((op >> 25) & 7) {
    case 0: case 1:
        if ((op & 0x0FFFFFF0) == 0x012FFF10) { d->kind = A_BX; d->cyc = 3; return 1; }
        if (!(op & (1u << 25)) && (op & 0x90) == 0x90) {
            uint32_t sh = (op >> 5) & 3;
            if (sh == 0) {
                if ((op & 0x0FC000F0) == 0x00000090 && !(op & (1u << 20))) {   /* MUL/MLA without S */
                    int rd = (op >> 16) & 15, rn = (op >> 12) & 15, rs = (op >> 8) & 15, rm = op & 15;
                    if (rd == 13 || rd == 15 || rs == 13 || rs == 15 || rm == 13 || rm == 15) return 0;
                    if ((op & (1u << 21)) && (rn == 13 || rn == 15)) return 0;
                    d->kind = A_MUL; d->cyc = 3; return 1;
                }
                if ((op & 0x0F8000F0) == 0x00800090 && !(op & (1u << 20))) {   /* UMULL/SMULL/UMLAL/SMLAL without S */
                    int hi = (op >> 16) & 15, lo = (op >> 12) & 15, rs = (op >> 8) & 15, rm = op & 15;
                    if (hi == lo || hi == 13 || hi == 15 || lo == 13 || lo == 15 || rs == 13 || rs == 15 || rm == 13 || rm == 15) return 0;
                    if ((op & (1u << 21)) && (rs > 7 || rm > 7)) return 0;
                    d->kind = A_MULL; d->cyc = 4; return 1;
                }
                return 0;
            }
            /* halfword transfers: low rd, base low or sp, no pc */
            {
                int l = (op >> 20) & 1, rn = (op >> 16) & 15, rd = (op >> 12) & 15, i = (op >> 22) & 1;
                if (rd == 13 || rd == 15 || (rd > 7 && rn == rd) || rn == 15 || (rn > 7 && rn != 13 && (!(op & (1u << 24)) || (op & (1u << 21)))) || (!i && (op & 15) > 7)) return 0;
                if (!l && sh != 1) return 0;
                d->kind = A_LSH; d->cyc = l ? 3 : 2; return 1;
            }
        }
        if ((op & 0x0FBF0FFF) == 0x010F0000) {                              /* MRS */
            int rd = (op >> 12) & 15;
            if (rd == 13 || rd == 15) return 0;
            if (!(op & (1u << 22))) d->fr |= FN | FZ | FC | FV;
            d->kind = A_MRS; d->cyc = 1; return 1;
        }
        if ((op & 0x0DB0F000) == 0x0120F000) {                              /* MSR: only CPSR_c */
            if ((op & (1u << 22)) || ((op >> 16) & 15) != 1) return 0;
            if (!(op & (1u << 25)) && ((op & 15) == 13 || (op & 15) == 15)) return 0;
            d->kind = A_MSR; d->cyc = 1; return 1;
        }
        {
            int opc = (op >> 21) & 15, s = (op >> 20) & 1, rd = (op >> 12) & 15;
            if (opc >= 8 && opc <= 11 && !s) return 0;
#ifdef T18_SPCHK_MOD
            /* sp moves in translated code only a little (see translate_thumb): add/sub sp, sp, #up to 1 KB */
            if (rd == 13 && !(opc >= 8 && opc <= 11) && !t18_new_off) {
                uint32_t rot = ((op >> 8) & 15) * 2, v = op & 0xFF; v = rot ? (v >> rot) | (v << (32 - rot)) : v;
                if (!(op & (1u << 25)) || ((op >> 16) & 15) != 13 || (opc != 2 && opc != 4) || s || v > 1024u) return 0;
            }
#endif
            if (rd == 15 && s) return 0;                                  /* exception return */
            if (rd == 15 && opc >= 8 && opc <= 11) return 0;
            int reg_shift = !(op & (1u << 25)) && (op & 0x10);
            if (reg_shift && ((op & 15) == 15 || ((op >> 16) & 15) == 15)) return 0;
            if (s) {
                if (opc == 2 || opc == 3 || opc == 4 || opc == 5 || opc == 6 || opc == 7 || opc == 10 || opc == 11) d->fw = FN | FZ | FC | FV;
                else {
                    d->fw = FN | FZ;
                    if (op & (1u << 25)) {
                        uint32_t rot = ((op >> 8) & 15) * 2, v = op & 0xFF; v = rot ? (v >> rot) | (v << (32 - rot)) : v;
                        if (rot) { if (v < 256) return 0; d->fw |= FC; }
                    } else if (reg_shift || ((op >> 7) & 31) || ((op >> 5) & 3)) d->fw |= FC;
                }
            }
            if (opc == 5 || opc == 6 || opc == 7) d->fr |= FC;
            if (!(op & (1u << 25)) && !reg_shift && ((op >> 5) & 3) == 3 && !((op >> 7) & 31)) d->fr |= FC;   /* RRX */
            d->kind = A_DP; d->cyc = (uint8_t)(reg_shift ? 2 : 1);
            if (rd == 15) d->cyc = (uint8_t)(d->cyc + 2);
            return 1;
        }
    case 2: case 3: {
        if ((op & (1u << 25)) && (op & 0x10)) return 0;
        int l = (op >> 20) & 1, rn = (op >> 16) & 15, rd = (op >> 12) & 15;
        if ((op & (1u << 25)) && ((op & 15) > 7)) return 0;
        if (rn == 15 && (!l || !(op & (1u << 24)) || (op & (1u << 21)) || (op & (1u << 25)))) return 0;
        if (rd == 15 && !(l && rn == 13 && !(op & (1u << 22)))) return 0;
        if (rd == 13) return 0;
        if (rd > 7 && rd != 15 && rn == rd) return 0;
        if (rn > 7 && rn != 13 && rn != 15 && (!(op & (1u << 24)) || (op & (1u << 21)))) return 0;   /* high base: no writeback */
        if (!l && rd == rn && (!(op & (1u << 24)) || (op & (1u << 21)))) return 0;
        if (rn == 13 && !(op & (1u << 25)) && (op & 0xFFF) > 255 && (!(op & (1u << 23)) || !(op & (1u << 24)) || (op & (1u << 21)))) return 0;
        d->kind = A_LS; d->cyc = l ? (rd == 15 ? 5 : 3) : 2; return 1;
    }
    case 4: {
        int s = (op >> 22) & 1, rn = (op >> 16) & 15, p = (op >> 24) & 1, u = (op >> 23) & 1, l = (op >> 20) & 1, w = (op >> 21) & 1;
        uint32_t list = op & 0xFFFF;
#ifdef T18_XRET
        /* 1.6.8: a conditional return from an exception (ldmcc ..., {..., pc}^, at the start of the firmware's
         * interrupt handlers, mostly not taken): when its condition holds the block leaves just before it and
         * the interpreter does it; else the block goes on. Before, it split the handler into one piece per
         * dispatcher trip. */
        if (s && l && (list & 0x8000) && (op >> 28) != 14 && !t18_new_off) { d->kind = A_XRET; d->cyc = 1; return 1; }
#endif
        if (s || rn != 13 || !w || !list) return 0;
        if (l ? !(p == 0 && u == 1) : !(p == 1 && u == 0)) return 0;    /* pop: ldmia sp!, push: stmdb sp! */
        if (list & (1u << 13)) return 0;
        if (!l && (list & 0x8000)) return 0;
#ifdef T18_STM_SPLIT
        /* 1.6.8: up to 6 of r8..r12, r14 in two parts (push {r4-r10, lr} in the flash read routine) */
        if (__builtin_popcount(list & 0x5F00) > ((list & 0x8000) || t18_new_off ? 3 : 6)) return 0;
#else
        if (__builtin_popcount(list & 0x5F00) > 3) return 0;              /* r8..r12, r14 into r11, r12, lr */
#endif
        if ((list & 0x8000) && (list & 0x5F00)) return 0;
        d->kind = l ? A_LDM : A_STM; d->cyc = (uint8_t)(__builtin_popcount(list) + 2);
        return 1;
    }
    case 5: d->kind = (op & (1u << 24)) ? A_BL : A_B; d->cyc = 3; return 1;
    }
    return 0;
}
/* ARM operand 2 for data processing into a host register when it is not a plain immediate.
 * Returns -1 when the instruction was emitted as immediate form instead. */
static int a_op2_reg(uint32_t op, uint32_t pc8, int s, int *type, int *amt)
{
    int rm = op & 15;
    int r = get_reg(rm, S2, pc8);
    if (op & 0x10) {
        /* register shift: shift into r12 (flags if s: then the ALU op keeps that carry) */
        int rs = get_reg((op >> 8) & 15, SLR, pc8);
        static const int sh[4] = { 0, 1, 2, 3 };   /* LSL LSR ASR ROR (register) */
        uint32_t t = (op >> 5) & 3;
        E32(0xFA00u | (uint32_t)sh[t] << 5 | (uint32_t)s << 4 | (uint32_t)r, 0xF000u | (uint32_t)S2 << 8 | (uint32_t)rs);
        *type = 0; *amt = 0;
        return S2;
    }
    *type = (op >> 5) & 3; *amt = (op >> 7) & 31;
    return r;
}
static int translate_arm(arm7_t *c, uint32_t pc, jent_t *e)
{
#ifdef T18_NO_ARM
    (void)c; (void)pc; (void)e; return 0;
#endif
    int n = 0;
    uint32_t a = pc;
#ifdef T18_BLK_MAXCYC
    uint32_t bcyc = 0;
#endif
    while (n < J_MAXI) {
        jins_t *d = &I[n];
        if (n && a == j_idle_key) break;
#ifdef T18_BLK_MAXCYC
        if (bcyc >= T18_BLK_MAXCYC) break;
#endif
        if (!a_decode(c, a, d)) break;
#ifdef T18_BLK_MAXCYC
        bcyc += d->cyc;
#endif
        n++; a += 4;
        uint32_t op = d->op, cond = op >> 28;
        if (cond == 14 && (d->kind == A_B || d->kind == A_BL || d->kind == A_BX)) break;
        if (cond == 14 && d->kind == A_DP && ((op >> 12) & 15) == 15) break;
        if (cond == 14 && d->kind == A_LDM && (op & 0x8000)) break;
        if (cond == 14 && d->kind == A_LS && ((op >> 12) & 15) == 15) break;
    }
    if (n == 0) return 0;
    uint8_t live = 0xF;
    for (int i = n - 1; i >= 0; i--) {
        live_after[i] = live;
        uint32_t cond = I[i].op >> 28;
        if (cond != 14) live = (uint8_t)(live | I[i].fr);                /* may not run: flags pass through */
        else live = (uint8_t)((live & ~I[i].fw) | I[i].fr);
        if (I[i].kind == A_B || I[i].kind == A_BL || I[i].kind == A_BX) live = 0xF;
    }
    int livein = live != 0, usesp = 0;
    for (int i = 0; i < n; i++) if ((I[i].kind == A_LS || I[i].kind == A_LDM || I[i].kind == A_STM) && ((I[i].op >> 16) & 15) == 13) usesp = 1;
#ifdef T18_SPCHK_MOD
    for (int i = 0; i < n; i++) if (I[i].kind == A_DP && ((I[i].op >> 12) & 15) == 13) usesp = 1;
#endif
    if ((char *)J + n * 112 + 136 > J_LIMIT) return -1;
#ifdef T18_JHDR
    J += 4;
#endif
    uint16_t *start = J;
    block_begin(livein, usesp);
#ifdef T18_LOOPFAST
    g_lbody = 0;
#endif
    uint32_t cyc = 0, maxcyc = 0;
    int ended = 0;
    for (int i = 0; i < n && !ended; i++) {
        const jins_t *d = &I[i];
        uint32_t op = d->op, ipc = d->pc, pc8 = ipc + 8;
        uint32_t cond = op >> 28;
        int fl = live_after[i] != 0;
        /* condition: skip over the code (counts 1 cycle); the executed path pays the rest */
        uint16_t *skipbr = 0;
        if (cond != 14) { skipbr = J; J += 2; }
        uint32_t cyc_exec = cyc + d->cyc;                                /* cycles once it ran */
        int term = 0;                                                     /* unconditional end of block */
        switch (d->kind) {
        case A_DP: {
            int opc = (op >> 21) & 15, s = (op >> 20) & 1, rn = (op >> 16) & 15, rd = (op >> 12) & 15;
            int dst = rd < 8 ? rd : S1;
            if (rd == 15) dst = S1;
            int x = (opc == 13 || opc == 15) ? -1 : get_reg(rn, S1, pc8);
            if (op & (1u << 25)) {
                uint32_t rot = ((op >> 8) & 15) * 2, v = op & 0xFF; v = rot ? (v >> rot) | (v << (32 - rot)) : v;
                int mi = t2_modimm(v);
                static const int8_t T2OP[16] = { T2_AND, T2_EOR, T2_SUB, T2_RSB, T2_ADD, T2_ADC, T2_SBC, -1, T2_AND, T2_EOR, T2_SUB, T2_ADD, T2_ORR, T2_ORR, T2_BIC, T2_ORN };
                if (opc == 7 || mi < 0) {                                  /* RSC, or not encodable: value in r12 */
                    t2_mov32(S2, v);
                    if (opc == 7) { t2_dpreg(T2_SBC, s, dst, S2, x, 0, 0); }
                    else {
                        static const int8_t R[16] = { T2_AND, T2_EOR, T2_SUB, T2_RSB, T2_ADD, T2_ADC, T2_SBC, -1, T2_AND, T2_EOR, T2_SUB, T2_ADD, T2_ORR, T2_ORR, T2_BIC, T2_ORN };
                        int o = R[opc];
                        if (opc == 13) t2_dpreg(T2_ORR, s, dst, 15, S2, 0, 0);
                        else if (opc == 15) t2_dpreg(T2_ORN, s, dst, 15, S2, 0, 0);
                        else if (opc >= 8 && opc <= 11) t2_dpreg(o, 1, 15, x, S2, 0, 0);
                        else t2_dpreg(o, s, dst, x, S2, 0, 0);
                    }
                } else {
                    int o = T2OP[opc];
                    if (opc == 13) t2_dpimm(T2_ORR, s, dst, 15, mi);          /* MOV */
                    else if (opc == 15) t2_dpimm(T2_ORN, s, dst, 15, mi);     /* MVN */
                    else if (opc >= 8 && opc <= 11) t2_dpimm(o, 1, 15, x, mi);
                    else t2_dpimm(o, s, dst, x, mi);
                }
            } else {
                int type, amt;
                int y = a_op2_reg(op, pc8 + ((op & 0x10) ? 4u : 0u), s, &type, &amt);
                if ((op & 0x10) && rn == 15) x = get_reg(15, S1, pc8 + 4);
                static const int8_t R[16] = { T2_AND, T2_EOR, T2_SUB, T2_RSB, T2_ADD, T2_ADC, T2_SBC, -1, T2_AND, T2_EOR, T2_SUB, T2_ADD, T2_ORR, T2_ORR, T2_BIC, T2_ORN };
                if (opc == 7) {                                          /* RSC: op2 - rn - !C */
                    if (y != S2) { t2_dpreg(T2_ORR, 0, S2, 15, y, type, amt); y = S2; type = 0; amt = 0; }
                    else if (type || amt) { t2_dpreg(T2_ORR, 0, S2, 15, S2, type, amt); type = 0; amt = 0; }
                    t2_dpreg(T2_SBC, s, dst, S2, x, 0, 0);
                }
                else if (opc == 13) t2_dpreg(T2_ORR, s, dst, 15, y, type, amt);
                else if (opc == 15) t2_dpreg(T2_ORN, s, dst, 15, y, type, amt);
                else if (opc >= 8 && opc <= 11) t2_dpreg(R[opc], 1, 15, x, y, type, amt);
                else t2_dpreg(R[opc], s, dst, x, y, type, amt);
            }
            if (opc >= 8 && opc <= 11) break;
            if (rd == 15) {
                if (cond != 14) t2_addi(RB, RB, 0u - (cyc_exec - cyc - 1));
                emit_exit_reg(S1, cond != 14 ? cyc + 1 : cyc_exec, 2);
                term = 1; break;
            }
            if (rd >= 8) set_reg(rd, S1);
            break;
        }
        case A_MUL: {
            int rd = (op >> 16) & 15, rn = (op >> 12) & 15, rs = (op >> 8) & 15, rm = op & 15;
            int a = get_reg(rm, S1, pc8), b = get_reg(rs, S2, pc8), dst = rd < 8 ? rd : S1;
            if (op & (1u << 21)) {
                int n = get_reg(rn, SLR, pc8);
                E32(0xFB00u | (uint32_t)a, (uint32_t)n << 12 | (uint32_t)dst << 8 | (uint32_t)b);   /* MLA */
            } else E32(0xFB00u | (uint32_t)a, 0xF000u | (uint32_t)dst << 8 | (uint32_t)b);      /* MUL */
            if (rd >= 8) set_reg(rd, S1);
            break;
        }
        case A_MULL: {
            int hi = (op >> 16) & 15, lo = (op >> 12) & 15, rs = (op >> 8) & 15, rm = op & 15;
            int sgn = (op >> 22) & 1, acc = (op >> 21) & 1;
            int a = get_reg(rm, SLR, pc8), b = get_reg(rs, S2, pc8);
            int dl = lo < 8 ? lo : S1, dh = hi < 8 ? hi : S2;
            if (acc) { if (lo >= 8) get_reg(lo, S1, pc8); if (hi >= 8) get_reg(hi, S2, pc8); }
            E32((acc ? (sgn ? 0xFBC0u : 0xFBE0u) : (sgn ? 0xFB80u : 0xFBA0u)) | (uint32_t)a, (uint32_t)dl << 12 | (uint32_t)dh << 8 | (uint32_t)b);
            if (lo >= 8) set_reg(lo, S1);
            if (hi >= 8) set_reg(hi, S2);
            break;
        }
        case A_MRS: {
            int rd = (op >> 12) & 15;
            if (op & (1u << 22)) t2_ldst_i(1, 2, 0, S2, RC, (uint32_t)offsetof(arm7_t, spsr));
            else {
                t2_mrs(S1);
                t2_ldst_i(1, 2, 0, S2, RC, (uint32_t)offsetof(arm7_t, cpsr));
                t2_shi(1, S1, S1, 28);
                E32(0xF360u | (uint32_t)S1, 7u << 12 | (uint32_t)S2 << 8 | 31u);                   /* BFI S2, S1, #28, #4 */
                t2_dpimm(T2_BIC, 0, S2, S2, 0x20);                                                 /* T in arm7_t may be stale: ARM here */
            }
            if (rd < 8) t2_mov(rd, S2); else set_reg(rd, S2);
            break;
        }
        case A_MSR: {
            if (op & (1u << 25)) { uint32_t rot = ((op >> 8) & 15) * 2, v = op & 0xFF; t2_mov32(S1, rot ? (v >> rot) | (v << (32 - rot)) : v); }
            else { int r = get_reg(op & 15, S1, pc8); t2_mov(S1, r); }
            t2_mrs(SLR);
            E16(0xB500u);                                                /* push {lr} */
            E_bl(fn16(j_msr));
            E_word(0x100u | cyc << 9);
            E_word(ipc);
            t2_msr(SLR);
            break;
        }
        case A_LS: case A_LSH: {
            int l = (op >> 20) & 1, rn = (op >> 16) & 15, rd = (op >> 12) & 15;
            int p = (op >> 24) & 1, u = (op >> 23) & 1, w = (op >> 21) & 1;
            int size, sign = 0;
            uint32_t imm = 0; int reg_off = -1, type = 0, amt = 0;
            if (d->kind == A_LS) {
                size = (op & (1u << 22)) ? 0 : 2;
                if (op & (1u << 25)) { reg_off = op & 15; type = (op >> 5) & 3; amt = (op >> 7) & 31; }
                else imm = op & 0xFFF;
            } else {
                uint32_t sh = (op >> 5) & 3;
                size = sh == 1 ? 1 : sh == 2 ? 0 : 1; sign = sh != 1;
                if (op & (1u << 22)) imm = ((op >> 4) & 0xF0) | (op & 15); else reg_off = op & 15;
            }
            if (rn == 15) {                                              /* literal */
                uint32_t la = pc8 + (u ? imm : 0u - imm);
                uint32_t v = size == 2 ? arm7_read32(c, la) : size == 1 ? arm7_read16(c, la) : arm7_read8(c, la);
                if (sign) v = size == 1 ? (uint32_t)(int32_t)(int16_t)v : (uint32_t)(int32_t)(int8_t)v;
                if (rd < 8) t2_mov32(rd, v); else { t2_mov32(S1, v); set_reg(rd, S1); }
                break;
            }
            if (rn == 13 && reg_off < 0 && size == 2) {
                /* sp based: host r8, always SRAM */
                int idx = p ? (w ? 1 : 0) : 2;                           /* offset, pre with writeback, post */
                int t = rd < 8 ? rd : S1;                                /* pc and r8..r14 go through r11 */
                if (!l && rd >= 8) get_reg(rd, S1, pc8);
                if (idx == 0) {
                    if (u && imm < 4096) t2_ldst_i(l, 2, 0, t, 8, imm);
                    else { E32((l ? 0xF850u : 0xF840u) | 8u, (uint32_t)t << 12 | 0x800u | (u ? 0x200u : 0) | 0x400u | (imm & 0xFF)); }
                } else {
                    E32((l ? 0xF850u : 0xF840u) | 8u, (uint32_t)t << 12 | 0x800u | (idx == 1 ? 0x400u : 0) | (u ? 0x200u : 0) | 0x100u | (imm & 0xFF));
                }
                if (l && rd >= 8 && rd != 15) set_reg(rd, S1);
                if (rd == 15) {
                    if (cond != 14) t2_addi(RB, RB, 0u - (cyc_exec - cyc - 1));
                    emit_exit_reg(S1, cond != 14 ? cyc + 1 : cyc_exec, 2);   /* ARMv4: ldr pc stays ARM */
                    term = 1;
                }
                break;
            }
            /* address into r11 */
            int b = get_reg(rn, S1, pc8);
            int off_r = -1;
            if (reg_off >= 0) off_r = get_reg(reg_off, S2, pc8);
            if (p) {
                if (off_r >= 0) t2_dpreg(u ? T2_ADD : T2_SUB, 0, S1, b, off_r, type, amt);
                else t2_addi(S1, b, u ? imm : 0u - imm);
            } else t2_mov(S1, b);
            /* writeback / post index: new base, before the access (the access uses r11) */
            if (!p || w) {
                int nb = rn == 13 ? 8 : rn;
                if (rn == 13) {
                    if (off_r >= 0) t2_dpreg(u ? T2_ADD : T2_SUB, 0, 8, 8, off_r, type, amt);
                    else t2_addi(8, 8, u ? imm : 0u - imm);
                } else if (p) t2_mov(nb, S1);
                else if (off_r >= 0) t2_dpreg(u ? T2_ADD : T2_SUB, 0, nb, nb, off_r, type, amt);
                else t2_addi(nb, nb, u ? imm : 0u - imm);
                if (rn == rd && l) { /* loaded value wins: nothing more to do */ }
            }
            emit_mem(ipc, l, size, sign, rd, cyc, fl);
            break;
        }
        case A_LDM: case A_STM: {
            uint32_t list = op & 0xFFFF, hl = 0;
#ifdef T18_STM_SPLIT
            if (__builtin_popcount(list & 0x5F00) > 3) {
                /* two parts: the 3 highest registers above the others (push them first, pop them last) */
                uint32_t hi = 0, lo; int m = 0;
                for (int r = 14; r >= 8 && m < 3; r--) if (list & (1u << r)) { hi |= 1u << r; m++; }
                lo = list & ~hi;
                for (int part = 0; part < 2; part++) {
                    uint32_t pl = (d->kind == A_STM) == (part == 0) ? hi : lo, ph = 0;
                    int sc2[3] = { S1, S2, SLR }, k2 = 0, mp[16];
                    for (int r = 0; r < 15; r++) {
                        if (!(pl & (1u << r))) continue;
                        if (r < 8) { ph |= 1u << r; mp[r] = r; } else { mp[r] = sc2[k2++]; ph |= 1u << mp[r]; }
                    }
                    if (d->kind == A_STM) {
                        for (int r = 8; r < 15; r++) if (pl & (1u << r)) get_reg(r, mp[r], 0);
                        if (__builtin_popcount(ph) == 1) { int r = __builtin_ctz(ph); E32(0xF840u | 8u, (uint32_t)r << 12 | 0xD04u); }
                        else t2_stmdb(8, ph, 1);
                    } else {
                        if (__builtin_popcount(ph) == 1) { int r = __builtin_ctz(ph); E32(0xF850u | 8u, (uint32_t)r << 12 | 0xB04u); }
                        else t2_ldmia(8, ph, 1);
                        for (int r = 8; r < 15; r++) if (pl & (1u << r)) set_reg(r, mp[r]);
                    }
                }
                break;
            }
#endif
            int sc[3] = { S1, S2, SLR }, k = 0, map[16];
            for (int r = 0; r < 16; r++) {
                if (!(list & (1u << r))) continue;
                if (r < 8) { hl |= 1u << r; map[r] = r; }
                else if (r == 15) { hl |= 1u << S1; map[r] = S1; }
                else { map[r] = sc[k++]; hl |= 1u << map[r]; }
            }
            (void)k;
            if (d->kind == A_STM) {
                for (int r = 8; r < 15; r++) if (list & (1u << r)) get_reg(r, map[r], 0);
                if (__builtin_popcount(hl) == 1) { int r = __builtin_ctz(hl); E32(0xF840u | 8u, (uint32_t)r << 12 | 0xD04u); }
                else t2_stmdb(8, hl, 1);
            } else {
                if (__builtin_popcount(hl) == 1) { int r = __builtin_ctz(hl); E32(0xF850u | 8u, (uint32_t)r << 12 | 0xB04u); }
                else t2_ldmia(8, hl, 1);
                for (int r = 8; r < 15; r++) if (list & (1u << r)) set_reg(r, map[r]);
                if (list & 0x8000) {
                    if (cond != 14) t2_addi(RB, RB, 0u - (cyc_exec - cyc - 1));
                    emit_exit_reg(S1, cond != 14 ? cyc + 1 : cyc_exec, 2);
                    term = 1;
                }
            }
            break;
        }
        case A_B: case A_BL: {
            int32_t off = (int32_t)(op << 8) >> 6;
            if (d->kind == A_BL) { t2_mov32(S1, ipc + 4); set_reg(14, S1); }
            if (cond != 14) t2_addi(RB, RB, 0u - (cyc_exec - cyc - 1));
            emit_exit_key((pc8 + (uint32_t)off) & ~3u, cond != 14 ? cyc + 1 : cyc_exec);
            term = 1;
            break;
        }
#ifdef T18_XRET
        case A_XRET:
            emit_exit_key(ipc, cyc);                                      /* the interpreter does it (j_stuck) */
            term = 1;
            break;
#endif
        case A_BX: {
            int y = get_reg(op & 15, S2, pc8);
            if (cond != 14) t2_addi(RB, RB, 0u - (cyc_exec - cyc - 1));
            emit_exit_reg(y, cond != 14 ? cyc + 1 : cyc_exec, 1);
            term = 1;
            break;
        }
        }
        if (cond != 14) {
            /* executed path: pay the extra cycles (exits above paid theirs) */
            if (!term && d->cyc > 1) t2_addi(RB, RB, 0u - (uint32_t)(d->cyc - 1));
            t2_bcc_at(skipbr, J, (int)(cond ^ 1));
            if (cyc_exec > maxcyc) maxcyc = cyc_exec;
            cyc += 1;
        } else {
            cyc = cyc_exec;
            if (term) ended = 1;
        }
        if (cyc > maxcyc) maxcyc = cyc;
    }
    if (!ended) emit_exit_key(I[n - 1].pc + 4, cyc);
    uint32_t m = block_end(pc, livein, maxcyc);
    if ((char *)J > J_LIMIT) return -1;
    e->off = (uint16_t)(start - JC + 1); e->maxcyc = (uint16_t)m;
    { uint32_t gl = (I[n - 1].pc - I[0].pc) / 2u + 2u; e->len = (uint8_t)(gl > 255u ? 255u : gl);
#ifdef T18_JHDR
      JW(start - 4, e->key & 0xFFFFu); JW(start - 3, e->key >> 16); JW(start - 2, gl > 0xFFFFu ? 0xFFFFu : gl); JW(start - 1, (uint32_t)(J - start));
#endif
    }
    t18_jit_stat[0]++; t18_jit_stat[7] += (uint32_t)n; t18_jit_bytes += (uint32_t)((char *)J - (char *)start);
    return 1;
}

/* ------------------------------------------------------------------ dispatcher */
#ifdef T18_MEM_HELPER
/* 1.6.6: compact loads/stores (shared check routine, half the code) only while the code buffer overflows
 * often; the inline check is faster when everything fits */
#ifndef T18_COMPACT_ON
#define T18_COMPACT_ON 8000000u              /* guest cycles between two flushes: below = compact */
#endif
#ifndef T18_COMPACT_OFF
#define T18_COMPACT_OFF 60000000u            /* above = inline again */
#endif
#endif
static inline jent_t *lookup(uint32_t key, int make);
static void compact_note_n(uint32_t n)
{
#ifdef T18_MEM_HELPER
    if (J && JCPU) {
        static uint64_t last;
        uint64_t d = (JCPU->cycles - last) * n; last = JCPU->cycles;
        if (d < T18_COMPACT_ON) j_compact = 1; else if (d > T18_COMPACT_OFF) j_compact = 0;
    }
#else
    (void)n;
#endif
}
#define compact_note() compact_note_n(1)
static void flush(void)
{
    compact_note();
#ifdef T18_FLUSH_KEEP
    /* 1.6.6: the keys stay; blocks that had code are translated again at their next start instead of
     * being interpreted J_HOT times first */
    if (!J) JH_CLEAR();
    else for (uint32_t i = 0; i < JH_N; i++) { jent_t *x = JHE(i); if (x->off) { x->off = 0; x->none = 0; x->hot = j_hot_v - 1; } }
#else
    JH_CLEAR();
#endif
    for (uint32_t i = 0; i < JT_BYTES / 4; i++) if (JT[i]) jt_put(&JT[i], 0);
    J = JC;
#ifdef T18_HALVES
    j_half = 0; j_lim = (char *)JMID; j_fill[0] = JC; j_fill[1] = JMID;
#endif
    t18_jit_stat[3]++;
}
#ifdef T18_HALVES
static uint16_t *t2_b_target(const uint16_t *at)
{
    uint32_t h1 = at[0], h2 = at[1];
    if ((h1 & 0xF800u) != 0xF000u || (h2 & 0xD000u) != 0x9000u) return 0;
    uint32_t sg = (h1 >> 10) & 1, i1 = (~(((h2 >> 13) & 1) ^ sg)) & 1, i2 = (~(((h2 >> 11) & 1) ^ sg)) & 1;
    int32_t off = (int32_t)(sg << 24 | i1 << 23 | i2 << 22 | (h1 & 0x3FFu) << 12 | (h2 & 0x7FFu) << 1);
    off = (off << 7) >> 7;
    return (uint16_t *)((const char *)at + 4 + off);
}
/* drop half h (0: JC..JMID, 1: JMID..end) */
#ifdef T18_HALVES_DBG
uint32_t t18_hdbg[8];
#endif
static void half_drop(int h)
{
    uint16_t *lo = h ? JMID : JC, *hi = h ? (uint16_t *)JC_END : JMID;
    uint16_t *ko = h ? JC : JMID, *ke = j_fill[h ^ 1];
    /* links in the kept half that jump into the dropped one: "bl j_chain" again (checked against the table) */
    for (uint16_t *p = ko; p + 4 <= ke; p++) {
        uint16_t *t = t2_b_target(p);
        if (!t || t < lo || t >= hi) continue;
        uint32_t key = (uint32_t)p[2] | (uint32_t)p[3] << 16;
        /* a link: the target is a block start (header before it holds the key), or a block start that was
         * replaced by "bl j_chain; .word key" (idle pc; maybe linked on to b.w). The table may already point elsewhere. */
        if (!key) continue;
        int ok = (t - 4 >= lo && ((uint32_t)t[-4] | (uint32_t)t[-3] << 16) == key)
              || (t + 4 <= hi && (t[0] & 0xF800u) == 0xF000u && (t[1] & 0x9000u) == 0x9000u && ((uint32_t)t[2] | (uint32_t)t[3] << 16) == key);
        if (!ok) {
#ifdef T18_HALVES_DBG
            extern uint32_t t18_hdbg[8];
            if (!t18_hdbg[0]++) { t18_hdbg[1] = (uint32_t)(uintptr_t)p; t18_hdbg[2] = key; t18_hdbg[3] = (uint32_t)(uintptr_t)t; t18_hdbg[4] = (uint32_t)t[-4] | (uint32_t)t[-3] << 16; t18_hdbg[5] = (uint32_t)t[0] | (uint32_t)t[1] << 16; t18_hdbg[6] = (uint32_t)t[2] | (uint32_t)t[3] << 16; }
#endif
            continue;
        }
#ifdef T18_HALVES_DBG
        { extern uint32_t t18_hdbg[8]; t18_hdbg[7]++; }
#endif
        t2_bl_at(p, fn16(j_chain));
        p += 3;
    }
    /* the entries of the dropped blocks go (left with their keys they filled the table: new keys took the
     * places of keys still counting up to J_HOT, which then never got hot). The others are put again
     * where a lookup finds them (linear probing, no gaps on the way). */
    for (uint32_t i = 0; i < JH_N; i++) { jent_t *x = JHE(i); if (x->off && ECODE(x) >= lo && ECODE(x) < hi) *x = (jent_t){ 0 }; }
    for (uint32_t i = 0; i < JH_N; i++) {
        jent_t *x = JHE(i);
        if (!x->key) continue;
        jent_t t = *x; *x = (jent_t){ 0 };
        uint32_t g = (t.key * 0x9E3779B1u) >> (32 - JH_BITS);
        for (int k = 0; k < 8; k++, g = (g + 1) & (JH_N - 1)) { jent_t *y = JHE(g); if (!y->key) { *y = t; break; } }
    }
    for (uint32_t i = 0; i < JT_N; i++) { uint16_t *c = (uint16_t *)(JT[2 * i + 1] & ~1u); if (JT[2 * i] && c >= lo && c < hi) jt_put(&JT[2 * i], 0); }
    j_fill[h] = lo;
}
#endif
void t18_jit_reset(void)
{
    flush(); j_seq = 0;
#ifdef T18_SF_ASM
    t18_sfp.o = 0;
#endif
}
uint32_t t18_jit_compact(void) { return (uint32_t)j_compact; }
uint32_t t18_jit_used(void)
{
#ifdef T18_HALVES
    if (J && j_lim) return (uint32_t)(((char *)(j_half ? j_fill[0] : J) - (char *)JC) + ((char *)(j_half ? J : j_fill[1]) - (char *)JMID));
#endif
    return J ? (uint32_t)((char *)J - (char *)JC) : 0;
}
int t18_jit_code_line(uint32_t o) { (void)o; return 0; }
int t18_jit_code_range(uint32_t o, uint32_t n) { (void)o; (void)n; return 0; }
void t18_jit_written(arm7_t *c, uint32_t a, uint32_t n) { (void)c; (void)a; (void)n; }

static inline jent_t *lookup(uint32_t key, int make)
{
    uint32_t h = (key * 0x9E3779B1u) >> (32 - JH_BITS);
    jent_t *spare = 0;
#ifndef T18_JH_PROBE
#define T18_JH_PROBE 8
#endif
    for (int k = 0; k < T18_JH_PROBE; k++, h = (h + 1) & (JH_N - 1)) {
        jent_t *e = JHE(h);
        if (e->key == key) return e;
        if (!e->key) { if (!make) return 0; e->key = key; return e; }
        if (!spare && !e->off) spare = e;
    }
    if (!make) return 0;
    /* full: take a slot without code (code of a dropped entry stays valid until the next flush) */
    if (!spare) spare = JHE((key * 0x9E3779B1u) >> (32 - JH_BITS));
    *spare = (jent_t){ 0 }; spare->key = key;
    return spare;
}
static int j_flushed;
#ifdef T18_REHOT
/* test10: a block that was translated before (bit set in a small table that a flush does not clear) is
 * translated again on its first start after a flush, instead of being interpreted J_HOT times first.
 * 1024 bits in the Paradise's RAM; cleared when 600 bits came in since the last clear (cold code). */
#define RH ((uint32_t *)T18_REHOT)
static uint32_t rh_n;
static inline uint32_t rh_bit(uint32_t key) { return (key * 0x9E3779B1u) >> 22; }
static inline int rh_get(uint32_t key) { uint32_t b = rh_bit(key); return (RH[b >> 5] >> (b & 31)) & 1u; }
static inline void rh_set(uint32_t key)
{
    uint32_t b = rh_bit(key);
    if ((RH[b >> 5] >> (b & 31)) & 1u) return;
    if (++rh_n > 600u) { memset(RH, 0, 128); rh_n = 1; }
    RH[b >> 5] |= 1u << (b & 31);
}
#endif
/* translated block for the key, translating it when hot. 0 = none. */
static jent_t *block_for(arm7_t *c, uint32_t key, int count)
{
    jent_t *e = lookup(key, count);
    if (!e) return 0;
    if (e->off) return e;
#ifdef T18_REHOT
    if (!count || e->none || (++e->hot < j_hot_v && !rh_get(key))) return 0;
#else
    if (!count || e->none || ++e->hot < j_hot_v) return 0;
#endif
#ifdef T18_NO_SRAMCODE
    if (key >= 0xF8000000u) { e->none = 1; return 0; }
#endif
#ifdef T18_HALVES
    uint16_t *j0 = J;
#endif
#ifdef T18_TIERUP
    j_inl = e->hot2 == 254;
#endif
    int r = (key & 1) ? translate_thumb(c, key & ~1u, e) : translate_arm(c, key, e);
    t18_jit_fr[key >= 0xF8000000u ? 2 : 3]++;
    if (r < 0) {
        t18_jit_fr[0]++;
#ifdef T18_HALVES
        if (j_lim) {
            int o = j_half ^ 1;
            j_fill[j_half] = j0;                                         /* without the unfinished block */
            compact_note_n(2);                                           /* half the buffer: count as half a flush */
            half_drop(o);
            j_half = o; J = o ? JMID : JC; j_lim = o ? JC_END : (char *)JMID;
            t18_jit_stat[3]++;
            j_flushed = 1; e = lookup(key, 1); if (!e) return 0;
            r = (key & 1) ? translate_thumb(c, key & ~1u, e) : translate_arm(c, key, e);
        }
        if (r < 0)
#endif
        { flush(); j_flushed = 1; e = lookup(key, 1); if (!e) return 0;
        r = (key & 1) ? translate_thumb(c, key & ~1u, e) : translate_arm(c, key, e); }
    }
#ifdef T18_TIERUP
    /* hot2: samples while the block has compact accesses; 255 = inline already */
    if (r > 0) e->hot2 = (uint8_t)(j_compact && !j_inl ? 0 : 255);
    j_inl = 0;
#endif
    if (r <= 0) { e->none = 1; return 0; }
#ifdef T18_REHOT
    rh_set(key);
#endif
    return e;
}
/* from the code: static exit (data = key after the bl) */
void t18_jit_idle_pc(arm7_t *c, uint32_t pc)
{
    j_idle_key = pc | ((c->cpsr & ARM7_T) ? 1u : 0u);
    t18_jit_stat[2]++;
    t18_jit_fr[1]++;
#ifdef T18_JHDR
    /* 1.6.6: the firmware switches between idle loops many times a second; a full flush each time made it
     * translate the same code again and again. Every block (each has a header) that holds the new idle pc
     * gets a link to its own key at its start: the next entry translates it anew, up to the idle pc. */
    if (J) {
        uint32_t ipc = pc & ~1u;
#ifdef T18_HALVES
        for (int h = 0; h < 2; h++) {
        uint16_t *pe = !j_lim ? (h ? JC : J) : (h == j_half ? J : j_fill[h]);
        for (uint16_t *p = h ? JMID : JC; p + 4 <= pe; ) {
#else
        for (uint16_t *p = JC; p + 4 <= J; ) {
#endif
            uint32_t key = (uint32_t)p[0] | (uint32_t)p[1] << 16, gl = p[2], hs = p[3];
            uint16_t *code = p + 4;
            if (key && ipc - (key & ~1u) < 2u * HDR_GL(gl)) {
                for (uint32_t i = 0; i < JH_N; i++) { jent_t *x = JHE(i); if (x->off && ECODE(x) == code) { x->off = 0; x->none = 0; x->hot = j_hot_v - 1; } }
                uint16_t *keep = J; J = code;
                E_bl(fn16(j_chain)); E_word(key);
                J = keep;
                loop_nop_drop(code, gl);
                JW(p, 0); JW(p + 1, 0);                                  /* dead: not looked at again */
            }
            p = code + hs;
        }
#ifdef T18_HALVES
        }
#endif
        return;
    }
#endif
#ifdef T18_IDLE_PART
    /* 1.6.6 test: the firmware switches between idle loops many times a second (pet screen); a full
     * flush each time made it translate the same code again and again. Only the blocks that hold the
     * new idle pc are dropped: their start becomes a link to their own key (the next entry translates
     * anew, up to the idle pc, or leaves to the interpreter there). */
    if (J) {
        uint32_t ipc = pc & ~1u;
        for (uint32_t i = 0; i < JH_N; i++) {
            jent_t *e = JHE(i);
            if (!e->off) continue;
            uint32_t a0 = e->key & ~1u, n = e->len == 255u ? 0x10000u : 2u * e->len;
            if (ipc - a0 >= n) continue;
            uint16_t *keep = J; J = ECODE(e);
            E_bl(fn16(j_chain)); E_word(e->key);
            J = keep;
            e->off = 0; e->none = 0; e->hot = j_hot_v - 1;
        }
        return;
    }
#endif
    if (J) flush();
}
#ifdef T18_LK_DBG   /* test only: why a link or lookup went out (key, reason, count) x 512 at 0x20008000 */
static void lk_note(uint32_t key, uint32_t r) { volatile uint32_t *h = (volatile uint32_t *)0x20008000u; uint32_t i = ((key * 0x9E3779B1u) >> 23) * 3;
  for (int k = 0; k < 64; k++, i = (i + 3) % (512 * 3)) { if (h[i] == key && h[i + 1] == r) { h[i + 2]++; return; } if (!h[i]) { h[i] = key; h[i + 1] = r; h[i + 2] = 1; return; } } }
#define LK(k, r) lk_note(k, r)
#else
#define LK(k, r) ((void)0)
#endif
__attribute__((used)) uint32_t t18_jit_link(uint16_t *data, int32_t bud)
{
    uint32_t key = (uint32_t)data[0] | (uint32_t)data[1] << 16;
    (void)bud;
    j_flushed = 0;
    if (key == j_idle_key) {
#ifdef T18_IDLE_PASS
        /* 1.6.8: the idle pc is tried once per run; on later passes in the same run the block is entered
         * without a link (a busy loop through the idle pc went through the dispatcher every 190 cycles) */
        extern uint64_t t18_run_tgt;
        if (JCPU->idle_win == t18_run_tgt && !t18_new_off) {
            jent_t *e = block_for(JCPU, key, 1);
            if (e && !j_flushed) return (uint32_t)(uintptr_t)ECODE(e) | 1u;
        }
#endif
        LK(key, 5); t18_link_pc = key; t18_link_cyc = 0; return (uint32_t)(uintptr_t)t18_jit_exit_link | 1u;
    }
    jent_t *e = block_for(JCPU, key, 1);
    if (e && !j_flushed) {
        t2_b_at(data - 2, ECODE(e));                                  /* the bl becomes b.w to the block */
        t18_jit_stat[5]++;
        return (uint32_t)(uintptr_t)ECODE(e) | 1u;
    }
    LK(key, e ? 4 : 3);
    t18_link_pc = key; t18_link_cyc = 0;
    return (uint32_t)(uintptr_t)t18_jit_exit_link | 1u;
}
__attribute__((used)) uint32_t t18_jit_lookup(uint32_t key, int32_t bud)
{
    (void)bud;
    uint32_t extra = 0;
    j_flushed = 0;
    if (T18_BXLR_SHORT && !(key & 1) && arm7_read32(JCPU, key) == 0xE12FFF1Eu) {
        /* an ARM routine that only returns (bx lr): do it here */
        uint32_t lr = JCPU->r[14];
        key = (lr & 1) ? lr : (lr & ~3u); extra = 3; t18_jit_stat[7]++;
    }
#ifdef T18_IDLE_PASS
    extern uint64_t t18_run_tgt;
    jent_t *e = key == j_idle_key && (JCPU->idle_win != t18_run_tgt || t18_new_off) ? 0 : block_for(JCPU, key, 1);
    if (key == j_idle_key && e && !j_flushed) { t18_jit_stat[6]++; return (uint32_t)(uintptr_t)ECODE(e) | 1u; }   /* no table entry */
#else
    jent_t *e = key == j_idle_key ? 0 : block_for(JCPU, key, 1);
#endif
    if (e && !j_flushed) {
        t18_jit_stat[6]++; LK(key, 7);
        if (extra) { t18_link_cyc = extra; t18_link_go = (uint32_t)(uintptr_t)ECODE(e) | 1u; return (uint32_t)(uintptr_t)t18_jit_sub_go | 1u; }
#ifdef T18_JT2
#ifdef T18_JT_MUL
        uint32_t *jt = &JT[((key * 0x9E3779B1u) >> JT_SH) * 4];
#else
        uint32_t *jt = &JT[(((key ^ (key >> 10)) >> 1) & (JT_N / 2 - 1)) * 4];
#endif
        if (jt[0]) { uint32_t k0 = jt[0], c0 = jt[1]; jt_put(&jt[2], 0); jt_put(&jt[3], c0); jt_put(&jt[2], k0); }   /* the older one to way 2 */
#else
        uint32_t *jt = &JT[(((key ^ (key >> 10)) >> 1) & (JT_N - 1)) * 2];
#endif
        jt_put(&jt[0], 0); jt_put(&jt[1], (uint32_t)(uintptr_t)ECODE(e) | 1u); jt_put(&jt[0], key);   /* the key last */
        return (uint32_t)(uintptr_t)ECODE(e) | 1u;
    }
    LK(key, key == j_idle_key ? 6 : e ? 2 : 1);
    t18_link_pc = key; t18_link_cyc = extra;
    return (uint32_t)(uintptr_t)t18_jit_exit_link | 1u;
}

#ifdef T18_TRY_STAT
uint32_t t18_try_why[20];    /* calls, stuck, sp, seq, nobl, none, budget, entered, target; 10+r: seq after r */
static int tw_last;
#ifdef T18_TRY_PCS
#define t18_try_pcs ((uint32_t *)0x20004000u)   /* pc, reason, count (M3 only, RAM20=0x10000) */
static uint32_t tw_pc;
static void tw_note(int r) { uint32_t h = ((tw_pc * 0x9E3779B1u) >> 22) * 3; for (int k = 0; k < 64; k++, h = (h + 3) % (1024 * 3)) { if (t18_try_pcs[h] == tw_pc && t18_try_pcs[h + 1] == (uint32_t)r) { t18_try_pcs[h + 2]++; return; } if (!t18_try_pcs[h]) { t18_try_pcs[h] = tw_pc; t18_try_pcs[h + 1] = (uint32_t)r; t18_try_pcs[h + 2] = 1; return; } } }
#define TW(i) ((i) == 3 ? t18_try_why[10 + tw_last]++ : (i) ? (tw_last = (i)) : 0, t18_try_why[i]++, tw_note(i))
#else
#define TW(i) ((i) == 3 ? t18_try_why[10 + tw_last]++ : (i) ? (tw_last = (i)) : 0, t18_try_why[i]++)
#endif
#else
#define TW(i) ((void)0)
#endif
#ifdef T18_TIERUP
/* 1.6.8: hot blocks without the shared check routine. SysTick notes the interrupted host pc when it lies in
 * the code buffer (t18_tu_pc). A block that was hit T18_TU_N times while it had compact loads/stores is
 * dropped the way the idle switch drops blocks (its start links to its own key) and at its next start
 * translated again with the checks inline: faster, a little bigger. */
#ifndef T18_TU_N
#define T18_TU_N 3
#endif
volatile uint32_t t18_tu_pc;
uint32_t t18_tu_n;                         /* blocks made inline (for measuring) */
__attribute__((section(".xiptext.tu_note"), noinline)) static void tu_note(uint32_t hpc)
{
    const uint16_t *q = (const uint16_t *)(hpc & ~1u);
    if (!J || q < JC || q >= J) return;
    for (uint16_t *p = JC; p + 4 <= J; ) {
        uint16_t *code = p + 4, *end = code + p[3];
        if (q < end) {
            uint32_t key = (uint32_t)p[0] | (uint32_t)p[1] << 16;
            if (q < code || !key) return;
            jent_t *e = lookup(key, 0);
            if (!e || !e->off || ECODE(e) != code || e->hot2 >= T18_TU_N) return;
            if (++e->hot2 < T18_TU_N) return;
            uint16_t *keep = J; J = code;
            E_bl(fn16(j_chain)); E_word(key);
            J = keep;
            loop_nop_drop(code, p[2]);
            JW(p, 0); JW(p + 1, 0);
            e->off = 0; e->none = 0; e->hot = (uint8_t)(j_hot_v - 1); e->hot2 = 254;
            t18_tu_n++;
            return;
        }
        p = end;
    }
}
#endif
/* Called before each interpreted instruction. 1: translated code ran. */
int t18_jit_try(arm7_t *c, uint64_t target)
{
#ifdef T18_TIERUP
    if (t18_tu_pc) { uint32_t h = t18_tu_pc; t18_tu_pc = 0; if (!t18_new_off) tu_note(h); }
#endif
    uint32_t pc = c->r[15], thumb = (c->cpsr & ARM7_T) ? 1u : 0u, len = thumb ? 2u : 4u;
    static int consts_ok = -1;                                       /* the constants the blocks compare with */
    if (consts_ok < 0) {
        consts_ok = t2_modimm(T18_SP_HI - T18_SP_LO) >= 0;
        for (int sh = 0; sh <= 2; sh++) if (t2_modimm(T18_SRAM_SIZE >> sh) < 0 || (T18_HOLE_HI > T18_HOLE_LO && t2_modimm((T18_HOLE_HI - T18_HOLE_LO) >> sh) < 0)) consts_ok = 0;
    }
    if (!consts_ok) return 0;
#ifdef T18_TRY_PCS
    tw_pc = pc | thumb;
#endif
    TW(0);
    if (pc == j_stuck) { TW(1); j_stuck = 1; j_seq = pc + len; j_fallback = 1; SEQ_OFF(); return 0; }
    int start = (pc != j_seq || j_fallback) && !j_tail;
    j_fallback = 0; j_tail = 0;
    if (c->r[13] - 0xF8000000u - T18_SP_LO >= T18_SP_HI - T18_SP_LO) { TW(2); j_seq = pc + len; return 0; }
#ifdef T18_TRY_SEQ
    /* 1.6.6: in the middle of an interpreted run (no jump got here) no block is looked up: blocks are made
     * where jumps land, the next jump finds them */
    if (!start && J) { TW(3); j_seq = pc + len; return 0; }
#endif
    if (!J) flush();
    JCPU = c; c->jtab = JT;
#ifdef T18_SPCHK_LD
    c->jsplo = 0x18000000u + T18_SP_LO;
#endif
#ifdef T18_SF_ASM
    if (t18_new_off) j_hot_v = 8;
    if (!t18_sfp.o && !t18_new_off) { t18_sfp.r = tg18_sf_regp(c->ctx); t18_sfp.c = tg18_sf_ctlp(c->ctx); t18_sfp.o = tg18_sf_outp(c->ctx); }
#endif
    uint32_t key = pc | thumb;
    jent_t *e = block_for(c, key, start);
    if (!e) { e = lookup(key, 0); j_seq = pc + len; if (e && e->none) { TW(5); j_fallback = 1; SEQ_OFF(); } else TW(4); return 0; }
    /* only when the budget covers the whole block (interrupts as in the interpreter) */
    if (target <= c->cycles) { TW(8); j_seq = pc + len; return 0; }
    uint64_t left = target - c->cycles;
    uint32_t mult = c->cyc_mult;
    int64_t bud = left >= 0x3FFFFFFFu ? 0x3FFFFFFF : (int64_t)(((uint32_t)left + mult - 1) / mult);   /* 32-bit divide */
    if (bud > 0x3FFFFFFF) bud = 0x3FFFFFFF;
#ifdef T18_OVERSHOOT
    if (bud <= 0) { TW(6); j_tail = 1; SEQ_OFF(); return 2; }
#else
    if (bud < (int64_t)e->maxcyc) { TW(6); j_tail = 1; SEQ_OFF(); return 2; }
#endif
    TW(7);   /* the rest of this run: interpreter */
#ifdef T18_JIT_VERIFY
    static arm7_t snap; static uint8_t *const SRAMCOPY = (uint8_t *)0x30000000u;
    uint32_t slow0 = t18_jit_stat[4], st0 = t18_vf_st;
    int verify = t18_verify_on && e->hot2 < T18_VERIFY_N;
    static uint8_t *const SOCCOPY = (uint8_t *)0x30040000u, *const SOCJIT = (uint8_t *)0x30050000u;
    uint32_t socn = tg18_struct_size();
    if (verify) { snap = *c; memcpy(SRAMCOPY, t18_sram, T18_SRAM_SIZE); memcpy(SOCCOPY, c->ctx, socn); }
#endif
    J_CYC0 = c->cycles; J_BUD0 = (int32_t)bud; J_MULT = mult;
    c->last_pc = pc;
    t18_jit_stat[1]++;
    int32_t (*volatile enter)(arm7_t *, const void *, int32_t) = j_enter;   /* far: the glue sits next to the code buffer */
    int32_t rest = enter(c, (const void *)((uintptr_t)ECODE(e) | 1u), (int32_t)bud);
    c->cycles = J_CYC0 + (uint64_t)(uint32_t)(J_BUD0 - rest) * J_MULT;
#ifdef T18_JIT_VERIFY
    if (verify && (t18_jit_stat[4] == slow0 || T18_VERIFY_IO)) {
        e->hot2++;
        static arm7_t res; res = *c;
        static uint8_t *const SRAMJIT = (uint8_t *)0x30020000u;
        memcpy(SRAMJIT, t18_sram, T18_SRAM_SIZE); memcpy(SOCJIT, c->ctx, socn);
        memcpy(c->ctx, SOCCOPY, socn);
        *c = snap; memcpy(t18_sram, SRAMCOPY, T18_SRAM_SIZE);
        int guard = 0;
        while (c->cycles < res.cycles && guard++ < 200) arm7_step(c);
        int bad = c->cycles != res.cycles || memcmp(c->r, res.r, sizeof c->r) || ((c->cpsr ^ res.cpsr) & 0xF000003Fu) || memcmp(t18_sram, SRAMJIT, T18_SRAM_SIZE);
        if (bad) { t18_verify_fail(e->key, &snap, &res, c); }
        memcpy(c->ctx, SOCJIT, socn);
        *c = res; memcpy(t18_sram, SRAMJIT, T18_SRAM_SIZE);
    }
#endif
    c->attn = c->irq_line | c->fiq_line | c->halted;
    j_seq = 0;
    if (c->r[15] == pc && c->cycles == J_CYC0) { j_stuck = pc; t18_jit_stat[2]++; }
    return 1;
}
#endif
