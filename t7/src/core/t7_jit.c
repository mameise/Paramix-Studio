/*
 * t7_jit.c - 6502 to Thumb-2 translator for the Paradise (Cortex-M3), switch T7_JIT (1.6.12 test2).
 *
 * Straight runs of ROM code (static $C000-$FFFF and the banked $4000-$BFFF) become native code. A block ends with a
 * branch, JMP, JSR or RTS, or stops before anything the interpreter must do itself (IRQ flag CLI/PLP/RTI/BRK, decimal
 * mode, PHP, the idle loop, the JSR shortcuts, the IR hooks, opcodes not handled here, the end of the page). Blocks
 * are found by the physical address of their first byte (page table pointer), so banked code needs no flushing.
 *
 * Exactness (checked on the PC under qemu-arm, runcmp against the interpreter, state hash every 2 ms):
 * - a block runs only when the interpreter would run all of its instructions in this run (cyc + pre < budget) and
 *   no sound event falls inside it (snd > all); its cycles are the same opcode_cycles sums;
 * - the flags are kept as the interpreter keeps them (nzn, nzz, fc, fv); ADC/SBC binary only (blocks run with D = 0);
 * - an I/O write ends the block right after it, with g_snd_next set before the write as the interpreter does;
 *   $0000-$0FFF work RAM, $1000-$2FFF display RAM (fixed), $3xxx I/O, ROM above (writes there do nothing).
 *
 * Registers in a block: r4 A, r5 X, r6 Y, r7 S, r8 nzn, r9 nzz, r10 C, r11 the state block; r0-r3, r12, lr free.
 * Author: mameise
 */
#include "t7_internal.h"
#include "t7_sound.h"
#ifdef T7_JIT
#include "t7_jit.h"

#define T7J_HASH 1024u
#define T7J_MAXI 32u
#ifndef T7J_HOT
#define T7J_HOT 2u
#endif

static uint16_t *g_jbuf;            /* code buffer (halfwords) */
static uint32_t g_jcap, g_jpos;     /* in halfwords */
static uint32_t g_jexit, g_jexit0, g_jtramp, g_jrds, g_jwrs, g_jrts, g_jjsr;  /* the shared exits, entry and memory stubs */
static T7JEnt g_t7j[T7J_HASH];
uint32_t g_t7j_on, g_t7j_stat[4];   /* blocks made, flushes, block runs, refused starts */
static int g_jfull;

/* ---------------------------------------------------------------- Thumb-2 emitter */
static __attribute__((noinline)) void e16(uint32_t h) { if (g_jpos + 1u > g_jcap) { g_jfull = 1; return; } g_jbuf[g_jpos++] = (uint16_t)h; }
static __attribute__((noinline)) void e32(uint32_t a, uint32_t b) { e16(a); e16(b); }
enum { R0, R1, R2, R3, R4, R5, R6, R7, R8, R9, R10, R11, R12, R13, LR, R15 };
#define RA R4
#define RX R5
#define RY R6
#define RST R7                       /* low register: short loads of the state fields */
#define RN R8
#define RZ R9
#define RC R10
#define RS R11
#define OFS(f) ((uint32_t)offsetof(T7J, f))
#define OFS2(f) ((uint32_t)offsetof(T7JEnt, f))
/* data processing, register, shift type t by n */
static __attribute__((noinline)) void dpr32(uint32_t op, int rd, int rn, int rm, int t, int n) { e32(op | (uint32_t)rn, ((uint32_t)(n >> 2) << 12) | ((uint32_t)rd << 8) | ((uint32_t)(n & 3) << 6) | ((uint32_t)t << 4) | (uint32_t)rm); }
#define ADD_R 0xEB00u
#define SUB_R 0xEBA0u
#define SUBS_R 0xEBB0u
#define AND_R 0xEA00u
#define BIC_R 0xEA20u
#define ORR_R 0xEA40u
#define EOR_R 0xEA80u
#define LO(r) ((uint32_t)(r) < 8u)
/* the short (flag setting) forms where they fit; the flags of the host are only used right after a compare */
static __attribute__((noinline)) void dpr(uint32_t op, int rd, int rn, int rm, int t, int n)
{
  if (!t && !n && LO(rd) && LO(rm) && rn != 15 && LO(rn)) {
    if (op == ADD_R) { e16(0x1800u | ((uint32_t)rm << 6) | ((uint32_t)rn << 3) | (uint32_t)rd); return; }
    if (op == SUB_R || op == SUBS_R) { e16(0x1A00u | ((uint32_t)rm << 6) | ((uint32_t)rn << 3) | (uint32_t)rd); return; }
    if (rd == rn) {
      uint32_t o16 = op == AND_R ? 0x4000u : op == EOR_R ? 0x4040u : op == ORR_R ? 0x4300u : op == BIC_R ? 0x4380u : 0u;
      if (o16) { e16(o16 | ((uint32_t)rm << 3) | (uint32_t)rd); return; }
    }
  }
  if (rn == 15 && op == ORR_R && LO(rd) && LO(rm) && n && t <= 1) { e16((t ? 0x0800u : 0x0000u) | ((uint32_t)n << 6) | ((uint32_t)rm << 3) | (uint32_t)rd); return; }
  dpr32(op, rd, rn, rm, t, n);
}
static __attribute__((noinline)) void mov(int rd, int rm) { e16(0x4600u | (((uint32_t)rd & 8u) << 4) | ((uint32_t)rm << 3) | ((uint32_t)rd & 7u)); }
static __attribute__((noinline)) void mvn(int rd, int rm) { dpr(0xEA60u, rd, 15, rm, 0, 0); }   /* ORN rd, pc, rm = MVN */
static __attribute__((noinline)) void lsli(int rd, int rm, int n) { dpr(ORR_R, rd, 15, rm, 0, n); }
static __attribute__((noinline)) void lsri(int rd, int rm, int n) { dpr(ORR_R, rd, 15, rm, 1, n); }
/* data processing, immediate 0..255 */
static __attribute__((noinline)) void dpi32(uint32_t op, int rd, int rn, uint32_t v) { e32(op | (uint32_t)rn, ((uint32_t)rd << 8) | (v & 0xFFu)); }
#define AND_I 0xF000u
#define TST_I 0xF010u
#define MOV_I 0xF04Fu
#define ADD_I 0xF100u
#define ADC_I 0xF140u
#define SUB_I 0xF1A0u
#define SUBS_I 0xF1B0u
static __attribute__((noinline)) void dpi(uint32_t op, int rd, int rn, uint32_t v)
{
  v &= 0xFFu;
  if (LO(rd) && rd == rn) {
    if (op == ADD_I) { e16(0x3000u | ((uint32_t)rd << 8) | v); return; }
    if (op == SUB_I) { e16(0x3800u | ((uint32_t)rd << 8) | v); return; }
  }
  if (op == SUBS_I && rd == 15 && LO(rn)) { e16(0x2800u | ((uint32_t)rn << 8) | v); return; }   /* CMP */
  dpi32(op, rd, rn, v);
}
static __attribute__((noinline)) void movi(int rd, uint32_t v) { if (LO(rd) && v < 256u) { e16(0x2000u | ((uint32_t)rd << 8) | v); return; } dpi(MOV_I, rd, 0, v); }   /* MOVS: never between a compare and its use */
static __attribute__((noinline)) void addw(int rd, int rn, uint32_t v) { e32(0xF200u | (((v >> 11) & 1u) << 10) | (uint32_t)rn, (((v >> 8) & 7u) << 12) | ((uint32_t)rd << 8) | (v & 0xFFu)); }
static __attribute__((noinline)) void movw(int rd, uint32_t v) { e32(0xF240u | ((v >> 12) & 0xFu) | (((v >> 11) & 1u) << 10), (((v >> 8) & 7u) << 12) | ((uint32_t)rd << 8) | (v & 0xFFu)); }
static __attribute__((noinline)) void movt(int rd, uint32_t v) { e32(0xF2C0u | ((v >> 12) & 0xFu) | (((v >> 11) & 1u) << 10), (((v >> 8) & 7u) << 12) | ((uint32_t)rd << 8) | (v & 0xFFu)); }
static __attribute__((noinline)) void mov32(int rd, uint32_t v) { movw(rd, v & 0xFFFFu); if (v >> 16) movt(rd, v >> 16); }
static __attribute__((noinline)) void uxtb(int rd, int rm) { if (LO(rd) && LO(rm)) { e16(0xB2C0u | ((uint32_t)rm << 3) | (uint32_t)rd); return; } e32(0xFA5Fu, 0xF080u | ((uint32_t)rd << 8) | (uint32_t)rm); }
static __attribute__((noinline)) void uxth(int rd, int rm) { if (LO(rd) && LO(rm)) { e16(0xB280u | ((uint32_t)rm << 3) | (uint32_t)rd); return; } e32(0xFA1Fu, 0xF080u | ((uint32_t)rd << 8) | (uint32_t)rm); }
static __attribute__((noinline)) void ldrb_i(int rt, int rn, uint32_t o) { if (LO(rt) && LO(rn) && o < 32u) { e16(0x7800u | (o << 6) | ((uint32_t)rn << 3) | (uint32_t)rt); return; } e32(0xF890u | (uint32_t)rn, ((uint32_t)rt << 12) | (o & 0xFFFu)); }
static __attribute__((noinline)) void strb_i(int rt, int rn, uint32_t o) { if (LO(rt) && LO(rn) && o < 32u) { e16(0x7000u | (o << 6) | ((uint32_t)rn << 3) | (uint32_t)rt); return; } e32(0xF880u | (uint32_t)rn, ((uint32_t)rt << 12) | (o & 0xFFFu)); }
static __attribute__((noinline)) void ldr_i(int rt, int rn, uint32_t o) { if (LO(rt) && LO(rn) && o < 128u && !(o & 3u)) { e16(0x6800u | ((o >> 2) << 6) | ((uint32_t)rn << 3) | (uint32_t)rt); return; } e32(0xF8D0u | (uint32_t)rn, ((uint32_t)rt << 12) | (o & 0xFFFu)); }
static __attribute__((noinline)) void str_i(int rt, int rn, uint32_t o) { if (LO(rt) && LO(rn) && o < 128u && !(o & 3u)) { e16(0x6000u | ((o >> 2) << 6) | ((uint32_t)rn << 3) | (uint32_t)rt); return; } e32(0xF8C0u | (uint32_t)rn, ((uint32_t)rt << 12) | (o & 0xFFFu)); }
static __attribute__((noinline)) void ldrb_r(int rt, int rn, int rm) { if (LO(rt) && LO(rn) && LO(rm)) { e16(0x5C00u | ((uint32_t)rm << 6) | ((uint32_t)rn << 3) | (uint32_t)rt); return; } e32(0xF810u | (uint32_t)rn, ((uint32_t)rt << 12) | (uint32_t)rm); }
static __attribute__((noinline)) void strb_r(int rt, int rn, int rm) { if (LO(rt) && LO(rn) && LO(rm)) { e16(0x5400u | ((uint32_t)rm << 6) | ((uint32_t)rn << 3) | (uint32_t)rt); return; } e32(0xF800u | (uint32_t)rn, ((uint32_t)rt << 12) | (uint32_t)rm); }
static __attribute__((noinline)) void ldr_r2(int rt, int rn, int rm) { e32(0xF850u | (uint32_t)rn, ((uint32_t)rt << 12) | (2u << 4) | (uint32_t)rm); }
static __attribute__((noinline)) void blx(int rm) { e16(0x4780u | ((uint32_t)rm << 3)); }
static __attribute__((noinline)) void cmpi(int rn, uint32_t v) { dpi(SUBS_I, 15, rn, v); }
static __attribute__((noinline)) void call(const void *f) { mov32(R12, (uint32_t)(uintptr_t)f); blx(R12); }
static __attribute__((noinline)) void carry_to(int rd) { movi(rd, 0); dpi(ADC_I, rd, rd, 0); }      /* C of the last SUBS: 1 = no borrow */
static __attribute__((noinline)) void nz(int r) { mov(RN, r); mov(RZ, r); }
static __attribute__((noinline)) void wram(int rd) { ldr_i(rd, RST, OFS(wram)); }
/* branches with fixups */
static __attribute__((noinline)) uint32_t bcond_at(int cond) { uint32_t at = g_jpos; e32(0xF000u | ((uint32_t)cond << 6), 0x8000u); return at; }
static uint32_t b_at(void) { uint32_t at = g_jpos; e32(0xF000u, 0x9000u); return at; }
static uint32_t bl_at(void) { uint32_t at = g_jpos; e32(0xF000u, 0xD000u); return at; }
static uint32_t cbz_at(int rn) { uint32_t at = g_jpos; e16(0xB100u | (uint32_t)rn); return at; }   /* forward, fixed by fixcb */
static __attribute__((noinline)) void fixcb(uint32_t at, uint32_t to) { if (g_jfull) return; uint32_t off = (to - at) * 2u - 4u; g_jbuf[at] = (uint16_t)(g_jbuf[at] | (((off >> 6) & 1u) << 9) | (((off >> 1) & 0x1Fu) << 3)); }
static __attribute__((noinline)) void fix(uint32_t at, uint32_t to)
{
  if (g_jfull || at + 1u >= g_jcap) return;
  int32_t off = ((int32_t)to - (int32_t)at - 2) * 2;
  uint32_t u = (uint32_t)off, s = off < 0 ? 1u : 0u, h1 = g_jbuf[at], h2 = g_jbuf[at + 1];
  if (!(h2 & 0x1000u)) {                 /* T3 conditional: S J2 J1 imm6 imm11 */
    g_jbuf[at] = (uint16_t)((h1 & 0xFBC0u) | (s << 10) | ((u >> 12) & 0x3Fu));
    g_jbuf[at + 1] = (uint16_t)(0x8000u | (((u >> 18) & 1u) << 13) | (((u >> 19) & 1u) << 11) | ((u >> 1) & 0x7FFu));
  } else {                               /* T4: S J1 J2 imm10 imm11 */
    uint32_t i1 = (u >> 23) & 1u, i2 = (u >> 22) & 1u, j1 = (~(i1 ^ s)) & 1u, j2 = (~(i2 ^ s)) & 1u;
    g_jbuf[at] = (uint16_t)(0xF000u | (s << 10) | ((u >> 12) & 0x3FFu));
    g_jbuf[at + 1] = (uint16_t)((h2 & 0x4000u) | 0x9000u | (j1 << 13) | (j2 << 11) | ((u >> 1) & 0x7FFu));
  }
}
enum { C_EQ = 0, C_NE = 1 };

/* ---------------------------------------------------------------- helpers called from blocks */
static uint32_t jrd(uint32_t a) { return t7_io_read((uint16_t)a); }
static void jwr(T7J *s, uint32_t a, uint32_t v, uint32_t cb)
{
  g_snd_next = g_snd_on ? s->snd - (int32_t)cb : s->snd;   /* as the interpreter's WR before an I/O write */
  t7_io_write((uint16_t)a, (uint8_t)v);
}

/* ---------------------------------------------------------------- block parts */
static __attribute__((noinline)) void cycles_r1(uint32_t cyc) { if (cyc < 256u) e16(0x2100u | cyc); else movw(R1, cyc); }
static __attribute__((noinline)) void exit_to(uint32_t pc, uint32_t cyc, uint32_t io)   /* r0 pc, r1 cycles, r2 io, to the exit */
{
  movw(R0, pc);
  if (cyc < 256u) e16(0x2100u | cyc); else movw(R1, cyc);    /* MOVS r1: the host flags are dead here */
  if (io) { movw(R2, io); fix(b_at(), g_jexit); } else fix(b_at(), g_jexit0);
}
/* constant addresses: work RAM (mirrored every 6 pages) and display RAM directly, the rest through the stubs */
static int ram_ofs(uint32_t ad, int *dram)
{
  if (ad < 0x1000u) { *dram = 0; return (int)(((ad >> 8) % 6u) * 0x100u + (ad & 0xFFu)); }
  if (ad < 0x3000u) { *dram = 1; return (int)(((ad >> 8) & 1u) * 0x100u + (ad & 0xFFu)); }
  return -1;
}
static __attribute__((noinline)) void ram_base(int dram) { if (dram) mov32(R3, (uint32_t)(uintptr_t)g_dram); else wram(R3); }
static __attribute__((noinline)) void emit_rd(void) { fix(bl_at(), g_jrds); }         /* r0 = address -> r0 = byte */
static __attribute__((noinline)) void emit_wr(uint32_t pcnext, uint32_t cb, uint32_t c) /* write r1 to address r0; on I/O the stub ends the block */
{
  fix(bl_at(), g_jwrs);
  e16(pcnext); e16(c); e16(cb); e16(0);                    /* data for the stub, skipped on return */
}
static __attribute__((noinline)) void ld_abs(int rt, uint32_t ad)
{
  int d, o = ram_ofs(ad, &d);
  if (o >= 0) { ram_base(d); ldrb_i(rt, R3, (uint32_t)o); return; }
  movw(R0, ad); emit_rd(); if (rt != R0) mov(rt, R0);
}
static __attribute__((noinline)) void st_abs(int rt, uint32_t ad, uint32_t pn, uint32_t cb, uint32_t c)
{
  int d, o = ram_ofs(ad, &d);
  if (o >= 0) { ram_base(d); strb_i(rt, R3, (uint32_t)o); return; }
  movw(R0, ad); if (rt != R1) mov(R1, rt); emit_wr(pn, cb, c);
}
static __attribute__((noinline)) void ea_absi(uint32_t ad, int ri) { movw(R0, ad); dpr(ADD_R, R0, R0, ri, 0, 0); uxth(R0, R0); }
static __attribute__((noinline)) void ea_zpi(uint32_t z, int ri) { dpi(ADD_I, R0, ri, z); uxtb(R0, R0); }
static __attribute__((noinline)) void ea_indx(uint32_t z)
{
  wram(R3);
  dpi(ADD_I, R1, RX, z); uxtb(R1, R1); ldrb_r(R0, R3, R1);
  dpi(ADD_I, R1, R1, 1); uxtb(R1, R1); ldrb_r(R2, R3, R1);
  dpr(ORR_R, R0, R0, R2, 0, 8);
}
static __attribute__((noinline)) void ea_indy(uint32_t z)
{
  wram(R3);
  ldrb_i(R0, R3, z); ldrb_i(R2, R3, (z + 1u) & 0xFFu);
  dpr(ORR_R, R0, R0, R2, 0, 8);
  dpr(ADD_R, R0, R0, RY, 0, 0); uxth(R0, R0);
}

static __attribute__((noinline)) void stubs(void)
{
  g_jtramp = g_jpos;                                 /* tramp(T7J *s, code) */
  e32(0xE92Du, 0x4FF0u);                             /* push {r4-r11, lr} */
  mov(RST, R0);
  ldr_i(RA, RST, OFS(a)); ldr_i(RX, RST, OFS(x)); ldr_i(RY, RST, OFS(y)); ldr_i(RS, RST, OFS(s));
  ldr_i(RN, RST, OFS(nzn)); ldr_i(RZ, RST, OFS(nzz)); ldr_i(RC, RST, OFS(fc));
  e16(0x4708u);                                      /* bx r1 */
  /* exit without I/O: r0 pc, r1 the block's cycles. The next block follows here when it is made and the interpreter
   * would run it now as well (as t7_cpu_run: pc from $4000, pre < budget left, all < sound countdown) */
  g_jexit0 = g_jpos;
  ldr_i(R3, RST, OFS(cyc)); dpr(ADD_R, R3, R3, R1, 0, 0); str_i(R3, RST, OFS(cyc));
  ldr_i(R3, RST, OFS(son)); ldr_i(R2, RST, OFS(snd)); e32(0xFB01u, 0x2213u); str_i(R2, RST, OFS(snd));   /* mls r2, r1, r3, r2 */
  ldr_i(R3, RST, OFS(rem)); dpr(SUB_R, R3, R3, R1, 0, 0); str_i(R3, RST, OFS(rem));
  lsri(R2, R0, 14); uint32_t o1 = bcond_at(C_EQ);
  lsri(R2, R0, 8); ldr_i(R1, RST, OFS(rp)); ldr_r2(R1, R1, R2);
  uint32_t o2 = cbz_at(R1);
  uxtb(R2, R0); dpr(ADD_R, R1, R1, R2, 0, 0);                              /* the key */
  dpr32(EOR_R, R2, R1, R1, 1, 11); lsli(R2, R2, 22); lsri(R2, R2, 22);     /* as t7_jit_get */
  dpr32(ADD_R, R2, R2, R2, 0, 2); ldr_i(R12, RST, OFS(tbl)); dpr32(ADD_R, R12, R12, R2, 0, 2);
  ldr_i(R2, R12, OFS2(key)); e16(0x4280u | (R1 << 3) | R2);               /* cmp r2, r1 */
  uint32_t o3 = bcond_at(C_NE);
  ldr_i(R1, R12, OFS2(code));
  uint32_t o4 = cbz_at(R1);
  ldr_i(R2, R12, OFS2(pre)); e16(0x4280u | (R3 << 3) | R2);               /* cmp r2, r3 */
  uint32_t o5 = bcond_at(10);                                              /* bge */
  ldr_i(R3, RST, OFS(son));
  uint32_t go = cbz_at(R3);
  ldr_i(R2, R12, OFS2(all)); ldr_i(R3, RST, OFS(snd)); e16(0x4280u | (R3 << 3) | R2);
  uint32_t o6 = bcond_at(10);
  fixcb(go, g_jpos);
  e16(0x4708u);                                      /* bx r1 */
  fix(o1, g_jpos); fixcb(o2, g_jpos); fix(o3, g_jpos); fixcb(o4, g_jpos); fix(o5, g_jpos); fix(o6, g_jpos);
  e16(0x2200u);                                      /* movs r2, #0 */
  uint32_t sv = b_at();
  g_jexit = g_jpos;                                  /* exit after an I/O write: r0 pc, r1 cycles, r2 the write's cycles */
  ldr_i(R3, RST, OFS(cyc)); dpr(ADD_R, R3, R3, R1, 0, 0); str_i(R3, RST, OFS(cyc));
  fix(sv, g_jpos);
  str_i(R0, RST, OFS(pc)); str_i(R2, RST, OFS(io));
  str_i(RA, RST, OFS(a)); str_i(RX, RST, OFS(x)); str_i(RY, RST, OFS(y)); str_i(RS, RST, OFS(s));
  str_i(RN, RST, OFS(nzn)); str_i(RZ, RST, OFS(nzz)); str_i(RC, RST, OFS(fc));
  e32(0xE8BDu, 0x8FF0u);                             /* pop {r4-r11, pc} */
  /* read: r0 address -> r0 byte (r1-r3, r12 used) */
  g_jrds = g_jpos;
  lsri(R1, R0, 8); ldr_i(R2, RST, OFS(rp)); ldr_r2(R2, R2, R1);
  uint32_t z = cbz_at(R2);
  uxtb(R3, R0); ldrb_r(R0, R2, R3); e16(0x4770u);  /* bx lr */
  fixcb(z, g_jpos);
  e16(0xB500u); call(jrd); e16(0xBD00u);           /* push {lr} .. pop {pc} */
  /* write: r0 address, r1 byte; lr points at the data after the BL: pc next, cycles, cycles before (halfwords) */
  g_jwrs = g_jpos;
  lsri(R2, R0, 8); ldr_i(R3, RST, OFS(wp)); ldr_r2(R3, R3, R2);
  z = cbz_at(R3);
  uxtb(R2, R0); strb_r(R1, R3, R2);
  uint32_t skip = g_jpos;
  addw(LR, LR, 8); e16(0x4770u);
  fixcb(z, g_jpos);
  lsri(R2, R0, 12); cmpi(R2, 3);
  fix(bcond_at(C_NE), skip);                         /* ROM: nothing happens */
  e32(0xF2AEu, 0x0C01u);                             /* subw r12, lr, #1: the data */
  str_i(R12, RST, OFS(tmp));
  mov(R2, R1); mov(R1, R0); mov(R0, RST);
  e32(0xF8BCu, 0x3004u);                             /* ldrh r3, [r12, #4]: cycles before */
  call(jwr);
  ldr_i(R3, RST, OFS(tmp));
  e16(0x8818u);                                      /* ldrh r0, [r3] */
  e16(0x885Au);                                      /* ldrh r2, [r3, #2] */
  e16(0x8899u);                                      /* ldrh r1, [r3, #4] */
  dpr(ADD_R, R1, R1, R2, 0, 0);
  fix(b_at(), g_jexit);                              /* the interpreter looks at the IRQs (chk) after it */
  /* RTS: r1 cycles */
  g_jrts = g_jpos;
  wram(R3);
  dpi(ADD_I, RS, RS, 1); uxtb(RS, RS); addw(R0, RS, 0x100); ldrb_r(R2, R3, R0);
  dpi(ADD_I, RS, RS, 1); uxtb(RS, RS); addw(R0, RS, 0x100); ldrb_r(R0, R3, R0);
  dpr32(ORR_R, R0, R2, R0, 0, 8); dpi(ADD_I, R0, R0, 1); uxth(R0, R0);
  fix(b_at(), g_jexit0);
  /* JSR: r0 return address - 1, r3 target, r1 cycles */
  g_jjsr = g_jpos;
  wram(R2);
  addw(R12, RS, 0x100); lsri(LR, R0, 8); strb_r(LR, R2, R12); dpi(SUB_I, RS, RS, 1); uxtb(RS, RS);
  addw(R12, RS, 0x100); strb_r(R0, R2, R12); dpi(SUB_I, RS, RS, 1); uxtb(RS, RS);
  mov(R0, R3);
  fix(b_at(), g_jexit0);
}

/* instruction length of the handled opcodes, 0 = not handled */
static uint32_t oplen(uint32_t op)
{
  switch (op) {
  case 0xA9: case 0xA2: case 0xA0: case 0x29: case 0x09: case 0x49: case 0xC9: case 0xE0: case 0xC0: case 0x69: case 0xE9:
  case 0xA5: case 0xA6: case 0xA4: case 0x85: case 0x86: case 0x84: case 0xB5: case 0x95: case 0xB4: case 0x94: case 0xB6: case 0x96:
  case 0x25: case 0x05: case 0x45: case 0xC5: case 0xE4: case 0xC4: case 0x65: case 0xE5: case 0x24: case 0xE6: case 0xC6:
  case 0x35: case 0x15: case 0x55: case 0xD5: case 0x75: case 0xF5: case 0xF6: case 0xD6: case 0x06: case 0x46: case 0x26: case 0x66:
  case 0x16: case 0x56: case 0x36: case 0x76:
  case 0xA1: case 0xB1: case 0x81: case 0x91: case 0x21: case 0x31: case 0x01: case 0x11: case 0x41: case 0x51: case 0xC1: case 0xD1:
  case 0x61: case 0x71: case 0xE1: case 0xF1:
  case 0x10: case 0x30: case 0x50: case 0x70: case 0x90: case 0xB0: case 0xD0: case 0xF0:
    return 2;
  case 0xAD: case 0xAE: case 0xAC: case 0x8D: case 0x8E: case 0x8C: case 0xBD: case 0xB9: case 0x9D: case 0x99: case 0xBC: case 0xBE:
  case 0x2D: case 0x0D: case 0x4D: case 0xCD: case 0xEC: case 0xCC: case 0x6D: case 0xED: case 0x2C: case 0xEE: case 0xCE: case 0xFE: case 0xDE:
  case 0x3D: case 0x39: case 0x1D: case 0x19: case 0x5D: case 0x59: case 0xDD: case 0xD9: case 0x7D: case 0x79: case 0xFD: case 0xF9:
  case 0x0E: case 0x4E: case 0x2E: case 0x6E: case 0x1E: case 0x5E: case 0x3E: case 0x7E:
  case 0x4C: case 0x20:
    return 3;
  case 0xAA: case 0xA8: case 0x8A: case 0x98: case 0xBA: case 0x9A: case 0xE8: case 0xC8: case 0xCA: case 0x88:
  case 0x18: case 0x38: case 0xB8: case 0xD8: case 0x78: case 0xEA: case 0x48: case 0x68: case 0x0A: case 0x4A: case 0x2A: case 0x6A: case 0x60:
    return 1;
  }
  return 0;
}

/* one instruction; returns 0 not done, 1 done, 2 done and the block ends */
static __attribute__((noinline)) int emit_op(uint32_t op, uint32_t b1, uint32_t ad, uint32_t pn, uint32_t cb, uint32_t c)
{
  switch (op) {
  case 0xA9: movi(RA, b1); nz(RA); return 1;
  case 0xA2: movi(RX, b1); nz(RX); return 1;
  case 0xA0: movi(RY, b1); nz(RY); return 1;
  case 0xA5: wram(R3); ldrb_i(RA, R3, b1); nz(RA); return 1;
  case 0xA6: wram(R3); ldrb_i(RX, R3, b1); nz(RX); return 1;
  case 0xA4: wram(R3); ldrb_i(RY, R3, b1); nz(RY); return 1;
  case 0xB5: ea_zpi(b1, RX); wram(R3); ldrb_r(RA, R3, R0); nz(RA); return 1;
  case 0xB4: ea_zpi(b1, RX); wram(R3); ldrb_r(RY, R3, R0); nz(RY); return 1;
  case 0xB6: ea_zpi(b1, RY); wram(R3); ldrb_r(RX, R3, R0); nz(RX); return 1;   /* page 0 is work RAM */
  case 0xAD: ld_abs(RA, ad); nz(RA); return 1;
  case 0xAE: ld_abs(RX, ad); nz(RX); return 1;
  case 0xAC: ld_abs(RY, ad); nz(RY); return 1;
  case 0xBD: ea_absi(ad, RX); emit_rd(); mov(RA, R0); nz(RA); return 1;
  case 0xB9: ea_absi(ad, RY); emit_rd(); mov(RA, R0); nz(RA); return 1;
  case 0xBC: ea_absi(ad, RX); emit_rd(); mov(RY, R0); nz(RY); return 1;
  case 0xBE: ea_absi(ad, RY); emit_rd(); mov(RX, R0); nz(RX); return 1;
  case 0xA1: ea_indx(b1); emit_rd(); mov(RA, R0); nz(RA); return 1;
  case 0xB1: ea_indy(b1); emit_rd(); mov(RA, R0); nz(RA); return 1;
  case 0x85: wram(R3); strb_i(RA, R3, b1); return 1;
  case 0x86: wram(R3); strb_i(RX, R3, b1); return 1;
  case 0x84: wram(R3); strb_i(RY, R3, b1); return 1;
  case 0x95: ea_zpi(b1, RX); wram(R3); strb_r(RA, R3, R0); return 1;
  case 0x94: ea_zpi(b1, RX); wram(R3); strb_r(RY, R3, R0); return 1;
  case 0x96: ea_zpi(b1, RY); wram(R3); strb_r(RX, R3, R0); return 1;
  case 0x8D: st_abs(RA, ad, pn, cb, c); return 1;
  case 0x8E: st_abs(RX, ad, pn, cb, c); return 1;
  case 0x8C: st_abs(RY, ad, pn, cb, c); return 1;
  case 0x9D: ea_absi(ad, RX); mov(R1, RA); emit_wr(pn, cb, c); return 1;
  case 0x99: ea_absi(ad, RY); mov(R1, RA); emit_wr(pn, cb, c); return 1;
  case 0x81: ea_indx(b1); mov(R1, RA); emit_wr(pn, cb, c); return 1;
  case 0x91: ea_indy(b1); mov(R1, RA); emit_wr(pn, cb, c); return 1;
  case 0xAA: mov(RX, RA); nz(RX); return 1;
  case 0xA8: mov(RY, RA); nz(RY); return 1;
  case 0x8A: mov(RA, RX); nz(RA); return 1;
  case 0x98: mov(RA, RY); nz(RA); return 1;
  case 0xBA: mov(RX, RS); nz(RX); return 1;
  case 0x9A: mov(RS, RX); return 1;
  case 0xE8: dpi(ADD_I, RX, RX, 1); uxtb(RX, RX); nz(RX); return 1;
  case 0xC8: dpi(ADD_I, RY, RY, 1); uxtb(RY, RY); nz(RY); return 1;
  case 0xCA: dpi(SUB_I, RX, RX, 1); uxtb(RX, RX); nz(RX); return 1;
  case 0x88: dpi(SUB_I, RY, RY, 1); uxtb(RY, RY); nz(RY); return 1;
  case 0x18: movi(RC, 0); return 1;
  case 0x38: movi(RC, 1); return 1;
  case 0xB8: movi(R0, 0); str_i(R0, RST, OFS(fv)); return 1;
  case 0xD8: return 1;                                         /* CLD: D is 0 in a block already */
  case 0x78: movi(R0, 1); str_i(R0, RST, OFS(fi)); return 1;   /* SEI (as the interpreter: 1) */
  case 0xEA: return 1;
  case 0x48: wram(R3); addw(R0, RS, 0x100); strb_r(RA, R3, R0); dpi(SUB_I, RS, RS, 1); uxtb(RS, RS); return 1;
  case 0x68: dpi(ADD_I, RS, RS, 1); uxtb(RS, RS); wram(R3); addw(R0, RS, 0x100); ldrb_r(RA, R3, R0); nz(RA); return 1;
  case 0x4C: exit_to(ad, cb + c, 0); return 2;
  case 0x20: movw(R0, (pn - 1u) & 0xFFFFu); movw(R3, ad); cycles_r1(cb + c); fix(b_at(), g_jjsr); return 2;
  case 0x60: cycles_r1(cb + c); fix(b_at(), g_jrts); return 2;
  case 0x10: case 0x30: case 0x50: case 0x70: case 0x90: case 0xB0: case 0xD0: case 0xF0: {
    uint32_t tgt = (pn + (uint32_t)(int32_t)(int8_t)b1) & 0xFFFFu;
    int cond;
    cycles_r1(cb + c);                                        /* first: MOVS sets the host flags */
    switch (op) {
    case 0x10: dpi(TST_I, 15, RN, 0x80); cond = C_EQ; break;     /* BPL */
    case 0x30: dpi(TST_I, 15, RN, 0x80); cond = C_NE; break;     /* BMI */
    case 0x50: ldr_i(R2, RST, OFS(fv)); cmpi(R2, 0); cond = C_EQ; break;
    case 0x70: ldr_i(R2, RST, OFS(fv)); cmpi(R2, 0); cond = C_NE; break;
    case 0x90: cmpi(RC, 0); cond = C_EQ; break;
    case 0xB0: cmpi(RC, 0); cond = C_NE; break;
    case 0xD0: cmpi(RZ, 0); cond = C_NE; break;
    default:   cmpi(RZ, 0); cond = C_EQ; break;
    }
    movw(R0, pn);
    e16(cond == C_EQ ? 0xBF08u : 0xBF18u);                    /* IT EQ / IT NE */
    movw(R0, tgt);
    fix(b_at(), g_jexit0);
    return 2;
  }
  }
  /* ALU group with an operand in r0 */
  int m;
  switch (op) {
  case 0x29: case 0x09: case 0x49: case 0xC9: case 0xE0: case 0xC0: case 0x69: case 0xE9: movi(R0, b1); m = 1; break;
  case 0x25: case 0x05: case 0x45: case 0xC5: case 0xE4: case 0xC4: case 0x65: case 0xE5: case 0x24: wram(R3); ldrb_i(R0, R3, b1); m = 1; break;
  case 0x35: case 0x15: case 0x55: case 0xD5: case 0x75: case 0xF5: ea_zpi(b1, RX); wram(R3); ldrb_r(R0, R3, R0); m = 1; break;
  case 0x2D: case 0x0D: case 0x4D: case 0xCD: case 0xEC: case 0xCC: case 0x6D: case 0xED: case 0x2C: ld_abs(R0, ad); m = 1; break;
  case 0x3D: case 0x1D: case 0x5D: case 0xDD: case 0x7D: case 0xFD: ea_absi(ad, RX); emit_rd(); m = 1; break;
  case 0x39: case 0x19: case 0x59: case 0xD9: case 0x79: case 0xF9: ea_absi(ad, RY); emit_rd(); m = 1; break;
  case 0x21: case 0x01: case 0x41: case 0xC1: case 0x61: case 0xE1: ea_indx(b1); emit_rd(); m = 1; break;
  case 0x31: case 0x11: case 0x51: case 0xD1: case 0x71: case 0xF1: ea_indy(b1); emit_rd(); m = 1; break;
  default: m = 0; break;
  }
  if (m) {
    int grp;
    if (op == 0xE0 || op == 0xE4 || op == 0xEC) grp = 10;
    else if (op == 0xC0 || op == 0xC4 || op == 0xCC) grp = 11;
    else if (op == 0x24 || op == 0x2C) grp = 12;
    else grp = (int)(op >> 5);
    switch (grp) {
    case 0: dpr(ORR_R, RA, RA, R0, 0, 0); nz(RA); return 1;
    case 1: dpr(AND_R, RA, RA, R0, 0, 0); nz(RA); return 1;
    case 2: dpr(EOR_R, RA, RA, R0, 0, 0); nz(RA); return 1;
    case 3:                                                    /* ADC: o = a + d + c */
      dpr(ADD_R, R1, RA, R0, 0, 0); dpr(ADD_R, R1, R1, RC, 0, 0);
      dpr(EOR_R, R2, RA, R0, 0, 0); dpr(EOR_R, R3, RA, R1, 0, 0); dpr(BIC_R, R2, R3, R2, 0, 0); dpi(AND_I, R2, R2, 0x80);
      str_i(R2, RST, OFS(fv));
      lsri(RC, R1, 8); uxtb(RA, R1); nz(RA); return 1;
    case 6: dpr(SUBS_R, R1, RA, R0, 0, 0); carry_to(RC); uxtb(R1, R1); nz(R1); return 1;   /* CMP */
    case 7:                                                    /* SBC: o = a - d - !c (signed) */
      dpr(SUB_R, R1, RA, R0, 0, 0); dpr(ADD_R, R1, R1, RC, 0, 0); dpi(SUB_I, R1, R1, 1);
      dpr(EOR_R, R2, RA, R0, 0, 0); dpr(EOR_R, R3, RA, R1, 0, 0); dpr(AND_R, R2, R2, R3, 0, 0); dpi(AND_I, R2, R2, 0x80);
      str_i(R2, RST, OFS(fv));
      mvn(RC, R1); lsri(RC, RC, 31);
      uxtb(RA, R1); nz(RA); return 1;
    case 10: dpr(SUBS_R, R1, RX, R0, 0, 0); carry_to(RC); uxtb(R1, R1); nz(R1); return 1;
    case 11: dpr(SUBS_R, R1, RY, R0, 0, 0); carry_to(RC); uxtb(R1, R1); nz(R1); return 1;
    case 12: dpi(AND_I, R1, R0, 0x40); str_i(R1, RST, OFS(fv)); mov(RN, R0); dpr(AND_R, RZ, RA, R0, 0, 0); return 1;
    }
    return 0;
  }
  /* shifts, INC, DEC */
  int sh = -1, mem = 1;
  switch (op) {
  case 0x0A: sh = 0; mem = 0; break; case 0x4A: sh = 1; mem = 0; break; case 0x2A: sh = 2; mem = 0; break; case 0x6A: sh = 3; mem = 0; break;
  case 0x06: case 0x16: case 0x0E: case 0x1E: sh = 0; break;
  case 0x46: case 0x56: case 0x4E: case 0x5E: sh = 1; break;
  case 0x26: case 0x36: case 0x2E: case 0x3E: sh = 2; break;
  case 0x66: case 0x76: case 0x6E: case 0x7E: sh = 3; break;
  case 0xE6: case 0xF6: case 0xEE: case 0xFE: sh = 4; break;
  case 0xC6: case 0xD6: case 0xCE: case 0xDE: sh = 5; break;
  }
  if (sh < 0) return 0;
  uint32_t mode = op & 0x1Cu;     /* 04 zp, 14 zp,X, 0C abs, 1C abs,X */
  int zp = mode == 0x04u || mode == 0x14u, d = 0, ko = mode == 0x0Cu ? ram_ofs(ad, &d) : -1;
  if (mode == 0x04u) { ko = (int)b1; d = 0; }
  if (ko >= 0) { ram_base(d); ldrb_i(R0, R3, (uint32_t)ko); }
  else if (mem) {
    if (mode == 0x14u) ea_zpi(b1, RX); else if (mode == 0x0Cu) movw(R0, ad); else ea_absi(ad, RX);
    str_i(R0, RST, OFS(tmp));
    if (zp) { wram(R3); ldrb_r(R0, R3, R0); } else emit_rd();
  } else mov(R0, RA);
  switch (sh) {
  case 0: lsri(RC, R0, 7); lsli(R1, R0, 1); uxtb(R1, R1); break;
  case 1: dpi(AND_I, RC, R0, 1); lsri(R1, R0, 1); break;
  case 2: lsli(R1, R0, 1); dpr(ORR_R, R1, R1, RC, 0, 0); lsri(RC, R0, 7); uxtb(R1, R1); break;
  case 3: lsri(R1, R0, 1); dpr(ORR_R, R1, R1, RC, 0, 7); dpi(AND_I, RC, R0, 1); break;
  case 4: dpi(ADD_I, R1, R0, 1); uxtb(R1, R1); break;
  default: dpi(SUB_I, R1, R0, 1); uxtb(R1, R1); break;
  }
  nz(R1);
  if (!mem) mov(RA, R1);
  else if (ko >= 0) strb_i(R1, R3, (uint32_t)ko);
  else if (zp) { ldr_i(R0, RST, OFS(tmp)); wram(R3); strb_r(R1, R3, R0); }
  else { ldr_i(R0, RST, OFS(tmp)); emit_wr(pn, cb, c); }
  return 1;
}

extern const uint8_t *const t7_opcode_cycles;
static int ok_pc(uint32_t pc, uint32_t b3)
{
  b3 &= 1u;
  if (b3 && (pc == 0x60C4u || pc == 0x7926u)) return 0;          /* IR hooks of the Tama-Go */
#ifdef T7_IDLE
  if (pc >= T7_IDLE_PC && pc < T7_IDLE_PC + 0x14u) return 0;      /* the idle loop: the interpreter adds its passes up */
#endif
  return 1;
}

static T7JEnt *translate(T7JEnt *e, uint32_t key, uint32_t pc, uint8_t *const *rp, uint32_t b3)
{
  if (g_jcap - g_jpos < 1200u) { t7_jit_flush(); g_t7j_stat[1]++; }
  uint32_t start = g_jpos, cyc = 0, pre = 0, n = 0, ended = 0;
  const uint8_t *page = rp[pc >> 8];
#ifdef T7J_DEBUG
  { extern int g_t7j_log; if (g_t7j_log==1) printf("TRS pc %04x page %p key %08x bytes %02x %02x %02x bank %u\n", pc, page, key, page[pc&0xFF], page[(pc+1)&0xFF], page[(pc+2)&0xFF], g_rom_bank_id); if (g_t7j_log==1) { for (int q=0;q<12;q++) printf("%02x ", page[(pc+q)&0xFF]); printf("\n"); } }
#endif
  g_jfull = 0;
  while (n < T7J_MAXI && ok_pc(pc, b3)) {
    uint32_t o = pc & 0xFFu, op = page[o], len = oplen(op);
    if (!len || o + len > 0x100u) break;
#ifdef T7J_DEBUG
    { extern uint8_t g_t7j_deny[256]; if (g_t7j_deny[op]) break; }
#endif
    uint32_t b1 = len > 1 ? page[o + 1] : 0, b2 = len > 2 ? page[o + 2] : 0, ad = b1 | (b2 << 8);
    if (op == 0x20 && (ad == 0xD460u || ad == 0xE4CDu)) break;  /* the JSR shortcuts stay with the interpreter */
#ifdef T7_IDLE
    if (op == 0xF0 && ((pc + 2u + (uint32_t)(int32_t)(int8_t)b1) & 0xFFFFu) == T7_IDLE_PC) break;
#endif
    uint32_t c = t7_opcode_cycles[op], pn = (pc + len) & 0xFFFFu, keep = g_jpos;
    int r = emit_op(op, b1, ad, pn, cyc, c);
    if (!r) { g_jpos = keep; break; }
#ifdef T7J_DEBUG
    { extern int g_t7j_log; if (g_t7j_log==3) fprintf(stderr,"O %02x %u\n", op, (unsigned)(g_jpos-keep)*2); }
#endif
    pre = cyc; cyc += c; n++; pc = pn;
    if (r == 2) { ended = 1; break; }
    if (!(pc & 0xFFu)) break;                         /* the next page may be mapped elsewhere */
  }
  if (!ended && n) exit_to(pc, cyc, 0);              /* stop before what the interpreter must do */
  if (!n || g_jfull) { g_jpos = start; e->key = key; e->code = 0; e->cnt = 0x8000u; g_t7j_stat[3]++; return 0; }
  e->key = key; e->code = (uint32_t)(uintptr_t)&g_jbuf[start] | 1u; e->pre = pre; e->all = cyc; e->cnt = 0;
  g_t7j_stat[0]++;
#ifdef T7J_DEBUG
  { extern int g_t7j_log; if (g_t7j_log==3) fprintf(stderr,"T %x %x %u %u\n", key, pc, n, (unsigned)(g_jpos-start)*2); }
#endif
#ifdef T7J_DEBUG
  { uint32_t pc0 = pc; extern int g_t7j_log; if (g_t7j_log==1) { printf("TR start key %08x end %04x n %u cyc %u code:", key, pc0, n, cyc); for (uint32_t k = start; k < g_jpos; k++) printf(" %04x", g_jbuf[k]); printf("\n"); } }
#endif
#if defined(__arm__)
  __asm__ volatile("dsb\n isb" ::: "memory");
#ifndef T7_FREESTANDING
  __builtin___clear_cache((char *)&g_jbuf[start], (char *)&g_jbuf[g_jpos]);
#endif
#endif
  return e;
}

void t7_jit_flush(void)
{
  for (uint32_t i = 0; i < T7J_HASH; i++) { g_t7j[i].key = 0; g_t7j[i].code = 0; g_t7j[i].cnt = 0; }
  g_jpos = 0; g_jfull = 0; stubs();
}
void t7_jit_init(void *buf, uint32_t bytes)
{
  g_jbuf = (uint16_t *)buf; g_jcap = bytes / 2u; g_t7j_on = g_jbuf && g_jcap > 2048u;
  if (g_t7j_on) t7_jit_flush();
}
T7JEnt *t7_jit_get(uint32_t key, uint32_t pc, uint8_t *const *rp, uint32_t b3)
{
  T7JEnt *e = &g_t7j[(key ^ (key >> 11)) & (T7J_HASH - 1u)];
  if (e->key == key) {
    if (e->code) return e;
    if (e->cnt >= 0x8000u) return 0;                 /* nothing to translate there */
    if (++e->cnt < T7J_HOT) return 0;
    return translate(e, key, pc, rp, b3);
  }
  if (!(b3 & 2u)) return 0;                          /* not where a run of code starts (after a jump or a block) */
  if (e->code && e->cnt < 0x10u) { e->cnt++; return 0; }   /* a used block stays for a while against a newcomer */
  e->key = key; e->code = 0; e->cnt = 1;
  return 0;
}
typedef void (*t7j_tramp)(T7J *, uint32_t);
void t7_jit_run(T7J *s, uint32_t code) { s->tbl = g_t7j; ((t7j_tramp)((uint32_t)(uintptr_t)&g_jbuf[g_jtramp] | 1u))(s, code); g_t7j_stat[2]++; }
#endif
