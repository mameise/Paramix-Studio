/* ARM7TDMI (ARMv4T) interpreter for T18 on the Paradise. Instruction semantics taken unchanged
 * from the T18Emu core (arm7.c); memory goes through the T18 map: guest SRAM 0xF8000000 (128 KB)
 * directly, guest flash 0x20000000 in 64 KB blocks, the rest through the SoC callbacks.
 * Author: mameise */
#include "t18cpu.h"
#include <string.h>
#define memcpy __builtin_memcpy     /* small copies inline (also in freestanding builds) */

#define N_BIT 31
#define Z_BIT 30
#define C_BIT 29
#define V_BIT 28
#define FLAG_N (1u<<31)
#define FLAG_Z (1u<<30)
#define FLAG_C (1u<<29)
#define FLAG_V (1u<<28)

#define M_USR 0x10
#define M_FIQ 0x11
#define M_IRQ 0x12
#define M_SVC 0x13
#define M_ABT 0x17
#define M_UND 0x1B
#define M_SYS 0x1F

static uint16_t cond_table[16];
static void build_cond(void) {
    for (int f = 0; f < 16; f++) {
        int n = (f >> 3) & 1, z = (f >> 2) & 1, cc = (f >> 1) & 1, v = f & 1;
        uint16_t m = 0;
        int r[16];
        r[0] = z; r[1] = !z; r[2] = cc; r[3] = !cc; r[4] = n; r[5] = !n; r[6] = v; r[7] = !v;
        r[8] = cc && !z; r[9] = !cc || z; r[10] = n == v; r[11] = n != v;
        r[12] = !z && (n == v); r[13] = z || (n != v); r[14] = 1; r[15] = 0;
        for (int k = 0; k < 16; k++) if (r[k]) m |= 1u << k;
        cond_table[f] = m;
    }
}


void arm7_init(arm7_t *c) {
    memset(c, 0, sizeof(*c));
    c->cyc_mult = 1;
    build_cond();
}

/* ---- memory ---- */
uint8_t *t18_sram;                 /* guest 0xF8000000, 128 KB (see t18cpu.h for the hole) */
uint8_t *t18_hole_pg[T18_HOLE_N + 1];
uint8_t *(*t18_hole_new)(void);
void (*t18_hole_free)(void);
int t18_oom;
const uint8_t *t18_fblk[128];      /* guest flash 0x20000000 in 64 KB blocks, 0 = erased */
const uint32_t t18_zero_pg[T18_HOLE_PG / 4];   /* 1.6.8: also read by j_chk (CHK_HOLE) */
const uint8_t *t18_sram_rp(uint32_t o) {
    if (!t18_hole(o)) return t18_sram + o;
    const uint8_t *p = t18_hole_pg[(o - T18_HOLE_LO) / T18_HOLE_PG];
    return (p ? p : (const uint8_t *)t18_zero_pg) + (o & (T18_HOLE_PG - 1));
}
uint8_t *t18_sram_wp(uint32_t o, int make) {
    if (!t18_hole(o)) return t18_sram + o;
    uint8_t **pp = &t18_hole_pg[(o - T18_HOLE_LO) / T18_HOLE_PG];
    if (!*pp && make) { *pp = t18_hole_new ? t18_hole_new() : 0; if (!*pp) t18_oom = 1; }
    return *pp ? *pp + (o & (T18_HOLE_PG - 1)) : 0;
}
uint32_t t18_sram_rd(uint32_t o, int size) {
    uint32_t v = 0;
    for (int i = 0; i < size; i++) v |= (uint32_t)*t18_sram_rp(o + (uint32_t)i) << (8 * i);
    return v;
}
void t18_sram_wr(uint32_t o, uint32_t v, int size) {
    for (int i = 0; i < size; i++) {
        uint8_t b = (uint8_t)(v >> (8 * i));
        uint8_t *p = t18_sram_wp(o + (uint32_t)i, b != 0);
        if (p) *p = b;
    }
}
void t18_sram_clear(void) {
    if (!T18_HOLE_N) { memset(t18_sram, 0, T18_SRAM_SIZE); return; }
    memset(t18_sram, 0, T18_HOLE_LO);
    memset(t18_sram + T18_HOLE_HI, 0, T18_SRAM_SIZE - T18_HOLE_HI);
    for (uint32_t i = 0; i < T18_HOLE_N; i++) t18_hole_pg[i] = 0;
    if (t18_hole_free) t18_hole_free();
    t18_oom = 0;
}
static inline const uint8_t *mp_r(uint32_t a, uint32_t n) {
    uint32_t o = a - 0xF8000000u;
    if (o < T18_SRAM_SIZE - (n - 1)) {
        if (!t18_hole_n(o, n)) return t18_sram + o;
        return n <= 4 ? t18_sram_rp(o) : 0;          /* aligned: within one page */
    }
    o = a - 0x20000000u;
    if (o < 0x800000u && (o & 0xFFFFu) <= 0x10000u - n) { const uint8_t *b = t18_fblk[o >> 16]; if (b) return b + (o & 0xFFFFu); }
    return 0;
}
static void watch_log(uint32_t o);
static inline uint8_t *mp_w(arm7_t *c, uint32_t a, uint32_t n) {
    uint32_t o = a - 0xF8000000u;
    if (o >= T18_SRAM_SIZE - (n - 1)) return 0;
    if (t18_hole_n(o, n)) return 0;                  /* the slow path makes the page for a non-zero value */
    if (__builtin_expect(c->watch, 0)) watch_log(o);
#ifdef T18_WMAP
    { extern uint8_t t18_wmap[512]; t18_wmap[o >> 8] = 1; }
#endif
    if (a + n > c->note_lo && a < c->note_hi && c->note_fn) c->note_fn(c->ctx, a);
#ifdef T18_JIT
    if (t18_jit_code_line(o)) t18_jit_written(c, a, n);
#endif
    return t18_sram + o;
}
#ifdef T18_ACNT
void t18_acnt(uint32_t a, uint32_t pc, int wr);
#define ACNT(a, w) do { if (((a) - 0xF8000000u >= T18_SRAM_SIZE || (a) - 0xF8000000u >= T18_ACNT_LIM) && (a) != c->last_pc) t18_acnt(a, c->last_pc, w); } while (0)
#else
#define ACNT(a, w) do { } while (0)
#endif
#ifdef T18_IDLE_MEMO
static void w_rlog(uint32_t a, uint32_t n, const uint8_t *m);
static void rm_add(uint32_t a, uint32_t n, uint32_t v);
#define RLOG(c, a, n, m) do { if (__builtin_expect((c)->watch, 0)) w_rlog(a, n, m); } while (0)
#else
#define RLOG(c, a, n, m) do { } while (0)
#endif
static inline uint32_t rd32(arm7_t *c, uint32_t a) {
    ACNT(a, 0);
    const uint8_t *m = mp_r(a & ~3u, 4); uint32_t v;
    if (m) { RLOG(c, a & ~3u, 4, m); memcpy(&v, m, 4); } else { v = c->rd(c->ctx, a & ~3u, 4); c->attn = c->irq_line | c->fiq_line | c->halted; }
    uint32_t rot = (a & 3) * 8;
    return rot ? (v >> rot) | (v << (32 - rot)) : v;
}
static inline uint32_t rd32a(arm7_t *c, uint32_t a) {
    const uint8_t *m = mp_r(a & ~3u, 4); uint32_t v;
    if (m) { RLOG(c, a & ~3u, 4, m); memcpy(&v, m, 4); return v; }
    v = c->rd(c->ctx, a & ~3u, 4); c->attn = c->irq_line | c->fiq_line | c->halted; return v;
}
static inline uint32_t rd16(arm7_t *c, uint32_t a) {
    ACNT(a, 0);
    const uint8_t *m = mp_r(a & ~1u, 2);
    if (m) { RLOG(c, a & ~1u, 2, m); uint16_t v; memcpy(&v, m, 2); return v; }
    uint32_t v = c->rd(c->ctx, a & ~1u, 2) & 0xFFFF; c->attn = c->irq_line | c->fiq_line | c->halted; return v;
}
static inline uint32_t rd8(arm7_t *c, uint32_t a) {
    ACNT(a, 0);
    const uint8_t *m = mp_r(a, 1);
    if (m) { RLOG(c, a, 1, m); return *m; }
    uint32_t v = c->rd(c->ctx, a, 1) & 0xFF; c->attn = c->irq_line | c->fiq_line | c->halted; return v;
}
static inline void wr32(arm7_t *c, uint32_t a, uint32_t v) {
    ACNT(a, 1);
    a &= ~3u; uint8_t *m = mp_w(c, a, 4);
    if (m) { memcpy(m, &v, 4); return; }
    c->wr(c->ctx, a, v, 4); c->attn = c->irq_line | c->fiq_line | c->halted;
}
static inline void wr16(arm7_t *c, uint32_t a, uint32_t v) {
    ACNT(a, 1);
    a &= ~1u; uint8_t *m = mp_w(c, a, 2);
    if (m) { uint16_t h = (uint16_t)v; memcpy(m, &h, 2); return; }
    c->wr(c->ctx, a, v & 0xFFFF, 2); c->attn = c->irq_line | c->fiq_line | c->halted;
}
static inline void wr8(arm7_t *c, uint32_t a, uint32_t v) {
    ACNT(a, 1);
    uint8_t *m = mp_w(c, a, 1);
    if (m) { *m = (uint8_t)v; return; }
    c->wr(c->ctx, a, v & 0xFF, 1); c->attn = c->irq_line | c->fiq_line | c->halted;
}
/* block transfer straight into memory when the whole block is in SRAM or one flash block */
static inline uint8_t *blk_ptr(arm7_t *c, uint32_t a, uint32_t len, int wr) {
    if (a & 3) return 0;
    if (wr) {
        if (c->watch) return 0;
        uint32_t o = a - 0xF8000000u;
        if (o >= T18_SRAM_SIZE || T18_SRAM_SIZE - o < len || t18_hole_n(o, len)) return 0;
        if (c->note_hi > c->note_lo && a + len > c->note_lo && a < c->note_hi) return 0;
#ifdef T18_JIT
        if (t18_jit_code_range(o, len)) return 0;
#endif
        return t18_sram + o;
    }
    return (uint8_t *)mp_r(a, len);
}
uint32_t arm7_read32(arm7_t *c, uint32_t a) { return rd32a(c, a); }
uint16_t arm7_read16(arm7_t *c, uint32_t a) { return (uint16_t)rd16(c, a); }
uint8_t arm7_read8(arm7_t *c, uint32_t a) { return (uint8_t)rd8(c, a); }
void arm7_write32(arm7_t *c, uint32_t a, uint32_t v) { wr32(c, a, v); }
void arm7_write16(arm7_t *c, uint32_t a, uint32_t v) { wr16(c, a, v); }
void arm7_write8(arm7_t *c, uint32_t a, uint8_t v) { wr8(c, a, v); }

/* ---- modes ---- */
static uint32_t *spsr_ptr(arm7_t *c, uint32_t mode) {
    switch (mode) {
    case M_FIQ: return &c->spsr_fiq;
    case M_IRQ: return &c->spsr_irq;
    case M_SVC: return &c->spsr_svc;
    case M_ABT: return &c->spsr_abt;
    case M_UND: return &c->spsr_und;
    default: return 0;
    }
}
static uint32_t *bank2(arm7_t *c, uint32_t mode) {
    switch (mode) {
    case M_IRQ: return c->bank_irq;
    case M_SVC: return c->bank_svc;
    case M_ABT: return c->bank_abt;
    case M_UND: return c->bank_und;
    default: return 0;
    }
}

void arm7_set_mode(arm7_t *c, uint32_t nm) {
    uint32_t om = c->cpsr & 0x1F;
    nm &= 0x1F;
    if (om == nm) return;
    /* store current */
    uint32_t *sp;
    if (om == M_FIQ) {
        for (int i = 0; i < 7; i++) c->bank_fiq[i] = c->r[8 + i];
    } else {
        for (int i = 0; i < 5; i++) c->bank_usr[i] = c->r[8 + i];
        uint32_t *b = bank2(c, om);
        if (b) { b[0] = c->r[13]; b[1] = c->r[14]; }
        else { c->bank_usr[5] = c->r[13]; c->bank_usr[6] = c->r[14]; }
    }
    if ((sp = spsr_ptr(c, om))) *sp = c->spsr;
    /* load new */
    if (nm == M_FIQ) {
        for (int i = 0; i < 7; i++) c->r[8 + i] = c->bank_fiq[i];
    } else {
        for (int i = 0; i < 5; i++) c->r[8 + i] = c->bank_usr[i];
        uint32_t *b = bank2(c, nm);
        if (b) { c->r[13] = b[0]; c->r[14] = b[1]; }
        else { c->r[13] = c->bank_usr[5]; c->r[14] = c->bank_usr[6]; }
    }
    if ((sp = spsr_ptr(c, nm))) c->spsr = *sp; else c->spsr = c->cpsr;
    c->cpsr = (c->cpsr & ~0x1Fu) | nm;
}

static inline void set_cpsr(arm7_t *c, uint32_t v) {
    arm7_set_mode(c, v & 0x1F);
    c->cpsr = v;
}

void arm7_reset(arm7_t *c, uint32_t pc) {
    c->cpsr = M_SVC | ARM7_I | ARM7_F;
    c->spsr = 0;
    c->r[15] = pc;
    c->halted = 0;
}

void arm7_exception(arm7_t *c, uint32_t mode, uint32_t vec, uint32_t ret) {
    uint32_t old = c->cpsr;
    arm7_set_mode(c, mode);
    c->spsr = old;
    c->r[14] = ret;
    c->cpsr &= ~ARM7_T;
    c->cpsr |= ARM7_I;
    if (mode == M_FIQ) c->cpsr |= ARM7_F;
    c->r[15] = c->vector_base + vec;
}

/* ---- ALU helpers ---- */
#define GETC() ((c->cpsr >> 29) & 1)

static inline uint32_t shift_imm(arm7_t *c, uint32_t v, int type, int amt, int *carry) {
    switch (type) {
    case 0: /* LSL */
        if (amt == 0) { *carry = GETC(); return v; }
        *carry = (v >> (32 - amt)) & 1; return v << amt;
    case 1: /* LSR */
        if (amt == 0) { *carry = v >> 31; return 0; }
        *carry = (v >> (amt - 1)) & 1; return v >> amt;
    case 2: /* ASR */
        if (amt == 0) { *carry = v >> 31; return (uint32_t)((int32_t)v >> 31); }
        *carry = (v >> (amt - 1)) & 1; return (uint32_t)((int32_t)v >> amt);
    default: /* ROR / RRX */
        if (amt == 0) { *carry = v & 1; return (v >> 1) | ((uint32_t)GETC() << 31); }
        *carry = (v >> (amt - 1)) & 1; return (v >> amt) | (v << (32 - amt));
    }
}
static inline uint32_t shift_reg(arm7_t *c, uint32_t v, int type, uint32_t amt, int *carry) {
    amt &= 0xFF;
    if (amt == 0) { *carry = GETC(); return v; }
    switch (type) {
    case 0:
        if (amt < 32) { *carry = (v >> (32 - amt)) & 1; return v << amt; }
        *carry = amt == 32 ? (v & 1) : 0; return 0;
    case 1:
        if (amt < 32) { *carry = (v >> (amt - 1)) & 1; return v >> amt; }
        *carry = amt == 32 ? (v >> 31) : 0; return 0;
    case 2:
        if (amt < 32) { *carry = (v >> (amt - 1)) & 1; return (uint32_t)((int32_t)v >> amt); }
        *carry = v >> 31; return (uint32_t)((int32_t)v >> 31);
    default:
        amt &= 31;
        if (amt == 0) { *carry = v >> 31; return v; }
        *carry = (v >> (amt - 1)) & 1; return (v >> amt) | (v << (32 - amt));
    }
}

static inline void set_nz(arm7_t *c, uint32_t r) {
    c->cpsr = (c->cpsr & 0x3FFFFFFF) | (r & FLAG_N) | (r ? 0 : FLAG_Z);
}
static inline void set_nzc(arm7_t *c, uint32_t r, int carry) {
    c->cpsr = (c->cpsr & 0x1FFFFFFF) | (r & FLAG_N) | (r ? 0 : FLAG_Z) | ((uint32_t)carry << 29);
}
static inline uint32_t add_flags(arm7_t *c, uint32_t a, uint32_t b, uint32_t cin) {
    uint64_t r = (uint64_t)a + b + cin;
    uint32_t res = (uint32_t)r;
    uint32_t v = ((~(a ^ b)) & (a ^ res)) >> 31;
    c->cpsr = (c->cpsr & 0x0FFFFFFF) | (res & FLAG_N) | (res ? 0 : FLAG_Z) | ((uint32_t)(r >> 32) << 29) | (v << 28);
    return res;
}
static inline uint32_t sub_flags(arm7_t *c, uint32_t a, uint32_t b, uint32_t cin) {
    /* a - b - !cin, carry = no borrow */
    uint64_t r = (uint64_t)a + (uint64_t)(~b) + cin;
    uint32_t res = (uint32_t)r;
    uint32_t v = ((a ^ b) & (a ^ res)) >> 31;
    c->cpsr = (c->cpsr & 0x0FFFFFFF) | (res & FLAG_N) | (res ? 0 : FLAG_Z) | ((uint32_t)(r >> 32) << 29) | (v << 28);
    return res;
}

static inline void set_pc_arm(arm7_t *c, uint32_t v) { c->r[15] = v & ~3u; }

/* ---- ARM execution ---- */
static int exec_arm(arm7_t *c, uint32_t op) {
    uint32_t pc = c->r[15];           /* already advanced by 4: address of next insn */
    /* when reading r15 as operand: current insn + 8 = pc + 4 */
#define RREG(n) ((n) == 15 ? pc + 4 : c->r[n])
    uint32_t cond = op >> 28;
    if (cond != 14) {
        if (!((cond_table[c->cpsr >> 28] >> cond) & 1)) return 1;
    }

    switch ((op >> 25) & 7) {
    case 0:
    case 1: {
        /* BX */
        if ((op & 0x0FFFFFF0) == 0x012FFF10) {
            uint32_t t = c->r[op & 15];
            if (t & 1) { c->cpsr |= ARM7_T; c->r[15] = t & ~1u; }
            else { c->cpsr &= ~ARM7_T; c->r[15] = t & ~3u; }
            return 3;
        }
        if (!(op & (1u << 25)) && (op & 0x90) == 0x90) {
            /* multiply / swap / halfword */
            uint32_t sh = (op >> 5) & 3;
            if (sh == 0) {
                if ((op & 0x0FC000F0) == 0x00000090) { /* MUL/MLA */
                    int rd = (op >> 16) & 15, rn = (op >> 12) & 15, rs = (op >> 8) & 15, rm = op & 15;
                    uint32_t r = c->r[rm] * c->r[rs];
                    if (op & (1u << 21)) r += c->r[rn];
                    c->r[rd] = r;
                    if (op & (1u << 20)) set_nz(c, r);
                    return 3;
                }
                if ((op & 0x0F8000F0) == 0x00800090) { /* long mul */
                    int rdhi = (op >> 16) & 15, rdlo = (op >> 12) & 15, rs = (op >> 8) & 15, rm = op & 15;
                    uint64_t r;
                    if (op & (1u << 22)) r = (uint64_t)((int64_t)(int32_t)c->r[rm] * (int64_t)(int32_t)c->r[rs]);
                    else r = (uint64_t)c->r[rm] * c->r[rs];
                    if (op & (1u << 21)) r += ((uint64_t)c->r[rdhi] << 32) | c->r[rdlo];
                    c->r[rdlo] = (uint32_t)r; c->r[rdhi] = (uint32_t)(r >> 32);
                    if (op & (1u << 20)) c->cpsr = (c->cpsr & 0x3FFFFFFF) | ((uint32_t)(r >> 32) & FLAG_N) | (r ? 0 : FLAG_Z);
                    return 4;
                }
                if ((op & 0x0FB00FF0) == 0x01000090) { /* SWP */
                    int rn = (op >> 16) & 15, rd = (op >> 12) & 15, rm = op & 15;
                    uint32_t a = c->r[rn];
                    if (op & (1u << 22)) { uint32_t t = rd8(c, a); wr8(c, a, c->r[rm]); c->r[rd] = t; }
                    else { uint32_t t = rd32(c, a); wr32(c, a, c->r[rm]); c->r[rd] = t; }
                    return 4;
                }
                goto undef;
            }
            /* halfword transfers */
            {
                int p = (op >> 24) & 1, u = (op >> 23) & 1, i = (op >> 22) & 1, w = (op >> 21) & 1, l = (op >> 20) & 1;
                int rn = (op >> 16) & 15, rd = (op >> 12) & 15;
                uint32_t off = i ? (((op >> 4) & 0xF0) | (op & 15)) : c->r[op & 15];
                uint32_t base = RREG(rn);
                uint32_t addr = p ? (u ? base + off : base - off) : base;
                uint32_t val = 0;
                if (l) {
                    if (sh == 1) val = rd16(c, addr);
                    else if (sh == 2) val = (uint32_t)(int32_t)(int8_t)rd8(c, addr);
                    else val = (uint32_t)(int32_t)(int16_t)rd16(c, addr);
                } else {
                    if (sh == 1) wr16(c, addr, rd == 15 ? pc + 8 : c->r[rd]);
                }
                if (!p) { addr = u ? base + off : base - off; c->r[rn] = addr; }
                else if (w) c->r[rn] = addr;
                if (l) { c->r[rd] = val; if (rd == 15) c->r[15] &= ~3u; }
                return l ? 3 : 2;
            }
        }
        /* MRS / MSR */
        if ((op & 0x0FBF0FFF) == 0x010F0000) {
            c->r[(op >> 12) & 15] = (op & (1u << 22)) ? c->spsr : c->cpsr;
            return 1;
        }
        if ((op & 0x0DB0F000) == 0x0120F000) {
            uint32_t v;
            if (op & (1u << 25)) { uint32_t imm = op & 0xFF, rot = ((op >> 8) & 15) * 2; v = rot ? (imm >> rot) | (imm << (32 - rot)) : imm; }
            else v = c->r[op & 15];
            uint32_t mask = 0;
            if (op & (1u << 16)) mask |= 0x000000FF;
            if (op & (1u << 17)) mask |= 0x0000FF00;
            if (op & (1u << 18)) mask |= 0x00FF0000;
            if (op & (1u << 19)) mask |= 0xFF000000;
            if (op & (1u << 22)) {
                if ((c->cpsr & 0x1F) != M_USR && (c->cpsr & 0x1F) != M_SYS)
                    c->spsr = (c->spsr & ~mask) | (v & mask);
            } else {
                if ((c->cpsr & 0x1F) == M_USR) mask &= 0xFF000000;
                set_cpsr(c, (c->cpsr & ~mask) | (v & mask));
            }
            return 1;
        }
        /* data processing */
        {
            int opc = (op >> 21) & 15, s = (op >> 20) & 1;
            int rn = (op >> 16) & 15, rd = (op >> 12) & 15;
            uint32_t op2; int carry = GETC();
            int cyc = 1;
            uint32_t a;
            if (op & (1u << 25)) {
                uint32_t imm = op & 0xFF, rot = ((op >> 8) & 15) * 2;
                if (rot) { op2 = (imm >> rot) | (imm << (32 - rot)); carry = op2 >> 31; }
                else op2 = imm;
                a = RREG(rn);
            } else {
                int rm = op & 15, type = (op >> 5) & 3;
                if (op & 0x10) {
                    /* register shift: pc reads +12 */
                    uint32_t vm = rm == 15 ? pc + 8 : c->r[rm];
                    op2 = shift_reg(c, vm, type, c->r[(op >> 8) & 15], &carry);
                    a = rn == 15 ? pc + 8 : c->r[rn];
                    cyc = 2;
                } else {
                    op2 = shift_imm(c, RREG(rm), type, (op >> 7) & 31, &carry);
                    a = RREG(rn);
                }
            }
            uint32_t res = 0;
            int wb = 1;
            switch (opc) {
            case 0: res = a & op2; if (s) set_nzc(c, res, carry); break;
            case 1: res = a ^ op2; if (s) set_nzc(c, res, carry); break;
            case 2: res = s ? sub_flags(c, a, op2, 1) : a - op2; break;
            case 3: res = s ? sub_flags(c, op2, a, 1) : op2 - a; break;
            case 4: res = s ? add_flags(c, a, op2, 0) : a + op2; break;
            case 5: res = s ? add_flags(c, a, op2, GETC()) : a + op2 + GETC(); break;
            case 6: res = s ? sub_flags(c, a, op2, GETC()) : a - op2 - !GETC(); break;
            case 7: res = s ? sub_flags(c, op2, a, GETC()) : op2 - a - !GETC(); break;
            case 8: res = a & op2; set_nzc(c, res, carry); wb = 0; break;
            case 9: res = a ^ op2; set_nzc(c, res, carry); wb = 0; break;
            case 10: sub_flags(c, a, op2, 1); wb = 0; break;
            case 11: add_flags(c, a, op2, 0); wb = 0; break;
            case 12: res = a | op2; if (s) set_nzc(c, res, carry); break;
            case 13: res = op2; if (s) set_nzc(c, res, carry); break;
            case 14: res = a & ~op2; if (s) set_nzc(c, res, carry); break;
            case 15: res = ~op2; if (s) set_nzc(c, res, carry); break;
            }
            if (wb) {
                c->r[rd] = res;
                if (rd == 15) {
                    if (s) {
                        uint32_t sp = c->spsr;
                        set_cpsr(c, sp);
                        c->r[15] = (c->cpsr & ARM7_T) ? (res & ~1u) : (res & ~3u);
                    } else c->r[15] = res & ~3u;
                    return cyc + 2;
                }
            } else if (rd == 15 && s) {
                /* TSTP etc. (legacy) - restore cpsr */
                set_cpsr(c, c->spsr);
            }
            return cyc;
        }
    }
    case 2:
    case 3: {
        /* single data transfer */
        if ((op & (1u << 25)) && (op & 0x10)) goto undef;
        int p = (op >> 24) & 1, u = (op >> 23) & 1, b = (op >> 22) & 1, w = (op >> 21) & 1, l = (op >> 20) & 1;
        int rn = (op >> 16) & 15, rd = (op >> 12) & 15;
        uint32_t off;
        if (op & (1u << 25)) { int cy; off = shift_imm(c, c->r[op & 15], (op >> 5) & 3, (op >> 7) & 31, &cy); }
        else off = op & 0xFFF;
        uint32_t base = RREG(rn);
        uint32_t addr = p ? (u ? base + off : base - off) : base;
        uint32_t val = 0;
        if (l) val = b ? rd8(c, addr) : rd32(c, addr);
        else {
            uint32_t sv = rd == 15 ? pc + 8 : c->r[rd];
            if (b) wr8(c, addr, sv); else wr32(c, addr, sv);
        }
        if (!p) c->r[rn] = u ? base + off : base - off;
        else if (w) c->r[rn] = addr;
        if (l) {
            c->r[rd] = val;
            if (rd == 15) {
                /* ARMv4T: LDR pc does not interwork; bit0 ignored */
                c->r[15] = val & ~3u;
                return 5;
            }
            return 3;
        }
        return 2;
    }
    case 4: {
        /* block transfer */
        int p = (op >> 24) & 1, u = (op >> 23) & 1, s = (op >> 22) & 1, w = (op >> 21) & 1, l = (op >> 20) & 1;
        int rn = (op >> 16) & 15;
        uint32_t list = op & 0xFFFF;
        int n = __builtin_popcount(list);
        uint32_t base = c->r[rn];
        uint32_t start;
        if (n == 0) { list = 0x8000; n = 16; } /* ARMv4 quirk: transfers r15, base += 0x40 */
        if (u) start = p ? base + 4 : base;
        else start = p ? base - 4 * n : base - 4 * n + 4;
        uint32_t newbase = u ? base + 4 * n : base - 4 * n;
        int usermode = s && !(l && (list & 0x8000));
        uint32_t oldmode = c->cpsr & 0x1F;
        if (usermode) arm7_set_mode(c, M_SYS);
        uint32_t a = start;
        if (l) {
            if (w) c->r[rn] = newbase;
            for (int i = 0; i < 16; i++) if (list & (1u << i)) {
                uint32_t v = rd32a(c, a); a += 4;
                c->r[i] = v;
            }
            if (usermode) { arm7_set_mode(c, oldmode); }
            if (list & 0x8000) {
                if (s) {
                    set_cpsr(c, c->spsr);
                    c->r[15] &= (c->cpsr & ARM7_T) ? ~1u : ~3u;
                } else c->r[15] &= ~3u;
            }
        } else {
            int first = 1;
            for (int i = 0; i < 16; i++) if (list & (1u << i)) {
                uint32_t v = i == 15 ? pc + 8 : c->r[i];
                if (i == rn && !first) v = newbase;
                wr32(c, a, v); a += 4;
                first = 0;
            }
            if (usermode) arm7_set_mode(c, oldmode);
            if (w) c->r[rn] = newbase;
        }
        return n + 2;
    }
    case 5: {
        int32_t off = (int32_t)(op << 8) >> 6;
        if (op & (1u << 24)) c->r[14] = pc;
        c->r[15] = pc + 4 + off;
        return 3;
    }
    case 6:
        goto undef; /* coprocessor transfer */
    case 7:
        if (op & (1u << 24)) {
            arm7_exception(c, M_SVC, 0x08, pc);
            return 3;
        }
        goto undef;
    }
undef:
    arm7_exception(c, M_UND, 0x04, pc);
    c->fault++;
    return 3;
#undef RREG
}

/* ---- Thumb execution ---- */
#ifdef T18_RAW_LABEL
uint32_t t18_raw_pc = 1u;
volatile uint32_t t18_raw_n, t18_raw_lo, t18_raw_hi;
uint16_t t18_raw_v[4];
static __attribute__((noinline)) void raw_note(uint32_t a, uint32_t v)
{
    if (!t18_raw_n || a < t18_raw_lo) t18_raw_lo = a;
    if (!t18_raw_n || a > t18_raw_hi) t18_raw_hi = a;
    v &= 0xFFFFu;
    int k = 0;
    while (k < 4 && t18_raw_v[k] && t18_raw_v[k] != v) k++;
    if (k < 4) t18_raw_v[k] = (uint16_t)v; else t18_raw_v[t18_raw_n & 3u] = (uint16_t)v;
    t18_raw_n++;
}
#endif
static inline __attribute__((always_inline)) int exec_thumb(arm7_t *c, uint32_t op) {
    uint32_t pc = c->r[15]; /* next insn address; reading pc gives pc+2 */
    switch (op >> 11) {
    case 0: case 1: case 2: { /* shift imm */
        int rd = op & 7, rs = (op >> 3) & 7, amt = (op >> 6) & 31, carry;
        uint32_t r = shift_imm(c, c->r[rs], op >> 11, amt, &carry);
        c->r[rd] = r; set_nzc(c, r, carry);
        return 1;
    }
    case 3: { /* add/sub */
        int rd = op & 7, rs = (op >> 3) & 7;
        uint32_t v = (op & (1u << 10)) ? ((op >> 6) & 7) : c->r[(op >> 6) & 7];
        if (op & (1u << 9)) c->r[rd] = sub_flags(c, c->r[rs], v, 1);
        else c->r[rd] = add_flags(c, c->r[rs], v, 0);
        return 1;
    }
    case 4: { int rd = (op >> 8) & 7; c->r[rd] = op & 0xFF; set_nz(c, c->r[rd]); return 1; }
    case 5: { int rd = (op >> 8) & 7; sub_flags(c, c->r[rd], op & 0xFF, 1); return 1; }
    case 6: { int rd = (op >> 8) & 7; c->r[rd] = add_flags(c, c->r[rd], op & 0xFF, 0); return 1; }
    case 7: { int rd = (op >> 8) & 7; c->r[rd] = sub_flags(c, c->r[rd], op & 0xFF, 1); return 1; }
    case 8: {
        if (!(op & (1u << 10))) { /* ALU ops */
            int rd = op & 7, rs = (op >> 3) & 7, carry;
            uint32_t a = c->r[rd], b = c->r[rs], r;
            switch ((op >> 6) & 15) {
            case 0: r = a & b; c->r[rd] = r; set_nz(c, r); break;
            case 1: r = a ^ b; c->r[rd] = r; set_nz(c, r); break;
            case 2: r = shift_reg(c, a, 0, b, &carry); c->r[rd] = r; set_nzc(c, r, carry); return 2;
            case 3: r = shift_reg(c, a, 1, b, &carry); c->r[rd] = r; set_nzc(c, r, carry); return 2;
            case 4: r = shift_reg(c, a, 2, b, &carry); c->r[rd] = r; set_nzc(c, r, carry); return 2;
            case 5: c->r[rd] = add_flags(c, a, b, GETC()); break;
            case 6: c->r[rd] = sub_flags(c, a, b, GETC()); break;
            case 7: r = shift_reg(c, a, 3, b, &carry); c->r[rd] = r; set_nzc(c, r, carry); return 2;
            case 8: set_nz(c, a & b); break;
            case 9: c->r[rd] = sub_flags(c, 0, b, 1); break;
            case 10: sub_flags(c, a, b, 1); break;
            case 11: add_flags(c, a, b, 0); break;
            case 12: r = a | b; c->r[rd] = r; set_nz(c, r); break;
            case 13: r = a * b; c->r[rd] = r; set_nz(c, r); return 3;
            case 14: r = a & ~b; c->r[rd] = r; set_nz(c, r); break;
            case 15: r = ~b; c->r[rd] = r; set_nz(c, r); break;
            }
            return 1;
        }
        if (!(op & (1u << 9)) || 1) {
            int sub = (op >> 8) & 3;
            if (op & (1u << 11)) break;
            if ((op >> 10) == 0x11) {
                /* hi register ops / BX */
                int rd = (op & 7) | ((op >> 4) & 8), rs = ((op >> 3) & 15);
                uint32_t vs = rs == 15 ? pc + 2 : c->r[rs];
                switch (sub) {
                case 0: {
                    uint32_t vd = rd == 15 ? pc + 2 : c->r[rd];
                    c->r[rd] = vd + vs;
                    if (rd == 15) { c->r[15] &= ~1u; return 3; }
                    return 1;
                }
                case 1: { uint32_t vd = rd == 15 ? pc + 2 : c->r[rd]; sub_flags(c, vd, vs, 1); return 1; }
                case 2:
                    c->r[rd] = vs;
                    if (rd == 15) { c->r[15] &= ~1u; return 3; }
                    return 1;
                case 3:
                    if (op & 0x80) c->r[14] = pc | 1; /* BLX (v5) - harmless */
                    if (vs & 1) c->r[15] = vs & ~1u;
                    else { c->cpsr &= ~ARM7_T; c->r[15] = vs & ~3u; }
                    return 3;
                }
            }
        }
        break;
    }
    case 9: { /* ldr pc-relative */
        int rd = (op >> 8) & 7;
        c->r[rd] = rd32a(c, ((pc + 2) & ~3u) + (op & 0xFF) * 4);
        return 3;
    }
    case 10: case 11: {
        int rd = op & 7, rb = (op >> 3) & 7, ro = (op >> 6) & 7;
        uint32_t a = c->r[rb] + c->r[ro];
        switch ((op >> 9) & 7) {
        case 0: wr32(c, a, c->r[rd]); return 2;
        case 1: wr16(c, a, c->r[rd]); return 2;
        case 2: wr8(c, a, c->r[rd]); return 2;
        case 3: c->r[rd] = (uint32_t)(int32_t)(int8_t)rd8(c, a); return 3;
        case 4: c->r[rd] = rd32(c, a); return 3;
        case 5: c->r[rd] = rd16(c, a); return 3;
        case 6: c->r[rd] = rd8(c, a); return 3;
        case 7: c->r[rd] = (uint32_t)(int32_t)(int16_t)rd16(c, a); return 3;
        }
        break;
    }
    case 12: { int rd = op & 7, rb = (op >> 3) & 7; wr32(c, c->r[rb] + ((op >> 6) & 31) * 4, c->r[rd]); return 2; }
    case 13: { int rd = op & 7, rb = (op >> 3) & 7; c->r[rd] = rd32(c, c->r[rb] + ((op >> 6) & 31) * 4); return 3; }
    case 14: { int rd = op & 7, rb = (op >> 3) & 7; wr8(c, c->r[rb] + ((op >> 6) & 31), c->r[rd]); return 2; }
    case 15: { int rd = op & 7, rb = (op >> 3) & 7; c->r[rd] = rd8(c, c->r[rb] + ((op >> 6) & 31)); return 3; }
    case 16: { int rd = op & 7, rb = (op >> 3) & 7;
#ifdef T18_RAW_LABEL
        if (c->r[15] == t18_raw_pc + 2u) raw_note(c->r[rb] + ((op >> 6) & 31) * 2, c->r[rd]);
#endif
        wr16(c, c->r[rb] + ((op >> 6) & 31) * 2, c->r[rd]); return 2; }
    case 17: { int rd = op & 7, rb = (op >> 3) & 7; c->r[rd] = rd16(c, c->r[rb] + ((op >> 6) & 31) * 2); return 3; }
    case 18: { int rd = (op >> 8) & 7; wr32(c, c->r[13] + (op & 0xFF) * 4, c->r[rd]); return 2; }
    case 19: { int rd = (op >> 8) & 7; c->r[rd] = rd32(c, c->r[13] + (op & 0xFF) * 4); return 3; }
    case 20: { int rd = (op >> 8) & 7; c->r[rd] = ((pc + 2) & ~3u) + (op & 0xFF) * 4; return 1; }
    case 21: { int rd = (op >> 8) & 7; c->r[rd] = c->r[13] + (op & 0xFF) * 4; return 1; }
    case 22: case 23: {
        if ((op & 0xFF00) == 0xB000) {
            uint32_t v = (op & 0x7F) * 4;
            if (op & 0x80) c->r[13] -= v; else c->r[13] += v;
            return 1;
        }
        if ((op & 0xF600) == 0xB400) {
            int l = (op >> 11) & 1, rbit = (op >> 8) & 1;
            uint32_t list = op & 0xFF;
            int n = __builtin_popcount(list) + rbit;
            if (!l) {
                uint32_t a = c->r[13] - 4 * n;
                c->r[13] = a;
                uint8_t *m = blk_ptr(c, a, 4u * n, 1);
                if (m) {
                    for (uint32_t b = list; b; b &= b - 1) { memcpy(m, &c->r[__builtin_ctz(b)], 4); m += 4; }
                    if (rbit) memcpy(m, &c->r[14], 4);
                } else {
                    for (int i = 0; i < 8; i++) if (list & (1u << i)) { wr32(c, a, c->r[i]); a += 4; }
                    if (rbit) wr32(c, a, c->r[14]);
                }
            } else {
                uint32_t a = c->r[13];
                const uint8_t *m = blk_ptr(c, a, 4u * n, 0);
                if (m) {
                    for (uint32_t b = list; b; b &= b - 1) { memcpy(&c->r[__builtin_ctz(b)], m, 4); m += 4; }
                    a += 4u * (n - rbit);
                    if (rbit) { uint32_t v; memcpy(&v, m, 4); c->r[15] = v & ~1u; a += 4; }
                } else {
                    for (int i = 0; i < 8; i++) if (list & (1u << i)) { c->r[i] = rd32a(c, a); a += 4; }
                    if (rbit) { c->r[15] = rd32a(c, a) & ~1u; a += 4; }
                }
                c->r[13] = a;
            }
            return n + 2;
        }
        break;
    }
    case 24: case 25: {
        int rb = (op >> 8) & 7, l = (op >> 11) & 1;
        uint32_t list = op & 0xFF;
        uint32_t a = c->r[rb];
        if (!list) { /* v4 quirk */
            if (l) c->r[15] = rd32a(c, a) & ~1u; else wr32(c, a, pc + 4);
            c->r[rb] = a + 0x40; return 3;
        }
        int n = __builtin_popcount(list);
        uint8_t *m;
        if (!(list & (1u << rb)) && (m = blk_ptr(c, a, 4u * n, !l)) != 0) {   /* PiEmu: direkt */
            if (l) for (uint32_t b = list; b; b &= b - 1) { memcpy(&c->r[__builtin_ctz(b)], m, 4); m += 4; }
            else for (uint32_t b = list; b; b &= b - 1) { memcpy(m, &c->r[__builtin_ctz(b)], 4); m += 4; }
            c->r[rb] = a + 4u * n;
            return n + 2;
        }
        if (l) {
            for (int i = 0; i < 8; i++) if (list & (1u << i)) { c->r[i] = rd32a(c, a); a += 4; }
            if (!(list & (1u << rb))) c->r[rb] = a;
        } else {
            uint32_t nb = a + 4 * n; int first = 1;
            for (int i = 0; i < 8; i++) if (list & (1u << i)) {
                wr32(c, a, (i == rb && !first) ? nb : c->r[i]); a += 4; first = 0;
            }
            c->r[rb] = a;
        }
        return n + 2;
    }
    case 26: case 27: {
        uint32_t cond = (op >> 8) & 15;
        if (cond == 15) { arm7_exception(c, M_SVC, 0x08, pc); return 3; }
        if (cond == 14) break;
        if ((cond_table[c->cpsr >> 28] >> cond) & 1) {
            int32_t off = (int32_t)(int8_t)(op & 0xFF) * 2;
            c->r[15] = pc + 2 + off;
            return 3;
        }
        return 1;
    }
    case 28: {
        int32_t off = ((int32_t)(op << 21) >> 20);
        c->r[15] = pc + 2 + off;
        return 3;
    }
    case 29: break;
    case 30: {
        int32_t off = ((int32_t)(op << 21) >> 9);
        c->r[14] = pc + 2 + off;
        return 1;
    }
    case 31: {
        uint32_t t = c->r[14] + ((op & 0x7FF) << 1);
        c->r[14] = pc | 1;
        c->r[15] = t & ~1u;
        return 3;
    }
    }
    arm7_exception(c, M_UND, 0x04, pc);
    c->fault++;
    return 3;
}


#ifdef T18_PROF
void t18_prof(uint32_t pc, uint32_t thumb);
#endif
#ifdef T18_JIT
uint32_t t18_interp[2];              /* interpreted instructions: Thumb, ARM */
#endif
/* ---- idle loops ----
 * The firmware polls in a tight main loop. Now and then one round is run watched: from the
 * current pc until the same pc comes back. If the round ends with the same registers, leaves
 * SRAM as it was, writes no device and reads only registers that hold still until the next
 * event, every further round does exactly the same. Whole rounds up to the target are then
 * skipped (only the cycles move on); the rest runs normally, so interrupts come at the same
 * instruction as without skipping. */
#ifndef T18_IDLE_DEFAULT
#define T18_IDLE_DEFAULT 1
#endif
int t18_idle_on = T18_IDLE_DEFAULT;
#ifdef T18_IDLE_CNT
uint32_t t18_idle_cnt[4];   /* tries at the idle pc / elsewhere, master cycles stepped in them */
#endif
int t18_jit_off;                     /* 1: interpreter only (for tests) */
#ifdef T18_IDLE_PASS
uint64_t t18_run_tgt;               /* target of the current run (t18_jit_link: idle pc tried in this run) */
#endif
#ifndef W_LOG
#define W_LOG 32   /* test43 fix: 64 did not fit the hole any more; 32 measured the same */
#endif
#ifdef T18_IDLE_DEEP
/* 1.6.8 test: also longer loops (the main loop of the pet screens, about 1200 instructions a round, calls
 * included); the round ends at the first return to the start pc with the same registers */
#define W_MAXI T18_IDLE_DEEP
#else
#define W_MAXI 600
#endif
static uint32_t w_off[W_LOG], w_old[W_LOG]; static int w_n, w_bad;
uint32_t t18_w_bad_addr;
static arm7_rd_fn w_rd0; static arm7_wr_fn w_wr0; static arm7_t *w_c;
static void watch_log(uint32_t o) {
    o &= ~3u;
    for (int i = 0; i < w_n; i++) if (w_off[i] == o) return;
    if (w_n == W_LOG) { w_bad = 1; t18_w_bad_addr = 0xEEEEEEEE; return; }
    w_off[w_n] = o; memcpy(&w_old[w_n], t18_sram_rp(o), 4); w_n++;
}
static uint32_t w_rd(void *ctx, uint32_t a, int size) {
    if (!w_c->io_stable || !w_c->io_stable(ctx, a)) { w_bad = 1; t18_w_bad_addr = a; }
#ifdef T18_IDLE_MEMO
    { uint32_t v = w_rd0(ctx, a, size); rm_add(a, (uint32_t)size | 0x10u, v); return v; }
#else
    return w_rd0(ctx, a, size);
#endif
}
static void w_wr(void *ctx, uint32_t a, uint32_t v, int size) { w_bad = 1; t18_w_bad_addr = a | 1; w_wr0(ctx, a, v, size); }
#ifdef T18_IDLE_MEMO
/* 1.6.8 (test43): the last idle round that could be skipped, with every value it read (memory and the
 * registers that hold still). In the living room the round was stepped through again ~900 times a second
 * (single steps, a quarter of the time). Same start (pc, registers) and the same values to read now means
 * the same round: it is skipped without stepping. Reads of what the round wrote itself do not match and
 * only cost the memo. */
#ifndef RM_N
#define RM_N 48
#endif
static uint32_t rm_a[RM_N], rm_v[RM_N]; static uint8_t rm_n8[RM_N]; static int rm_n, rm_over;   /* rm_over also: not logging in this try */
static uint32_t rm_pc, rm_cpsr, rm_r[16], rm_round, rm_ok, rm_mult, rm_fgen;
extern uint32_t t18_flash_gen;
static void rm_add(uint32_t a, uint32_t n, uint32_t v) {
    if (rm_over) return;
    for (int i = 0; i < rm_n; i++) if (rm_a[i] == a && rm_n8[i] == n) return;
    if (rm_n == RM_N) { rm_over = 1; return; }
    rm_a[rm_n] = a; rm_n8[rm_n] = (uint8_t)n; rm_v[rm_n] = v; rm_n++;
}
static void w_rlog(uint32_t a, uint32_t n, const uint8_t *m) {
    if (rm_over || a - 0x20000000u < 0x800000u) return;  /* the flash: checked as a whole (t18_flash_gen) */
    uint32_t v = 0; memcpy(&v, m, n); rm_add(a, n, v);
}
#endif
#ifdef T18_JIT
void t18_jit_idle_pc(arm7_t *c, uint32_t pc);
#endif
static void idle_try(arm7_t *c, uint64_t target) {
    uint32_t r0[16], cpsr0 = c->cpsr, p0 = c->r[15];
    uint64_t start = c->cycles;
    int ok = 0, at_pc = p0 == c->idle_pc;
#ifdef T18_IDLE_CNT
    extern uint32_t t18_idle_cnt[4]; t18_idle_cnt[at_pc ? 0 : 1]++; uint64_t ic_c0 = c->cycles;
#endif
    memcpy(r0, c->r, sizeof r0);
#ifdef T18_IDLE_MEMO
    if (rm_ok && rm_pc == p0 && rm_cpsr == cpsr0 && rm_mult == c->cyc_mult && rm_fgen == t18_flash_gen
        && !memcmp(rm_r, r0, sizeof r0) && c->cycles < target
        && (target - c->cycles) / rm_round >= 1u) {
        int same = 1;
        for (int i = 0; i < rm_n && same; i++) {
            uint32_t v = 0, n = rm_n8[i] & 7u;
            if (rm_n8[i] & 0x10u) v = c->rd(c->ctx, rm_a[i], (int)n);      /* a register that holds still */
            else { const uint8_t *m = mp_r(rm_a[i], n); if (!m) { same = 0; break; } memcpy(&v, m, n); }
            if (v != rm_v[i]) same = 0;
        }
        if (same && !c->attn) {
            uint64_t k = (target - c->cycles) / rm_round;
            c->cycles += k * rm_round; c->idle_cycles += k * rm_round; c->idle_skipped += k * rm_round;
            c->idle_win = target; c->idle_fail = 0; c->idle_next = c->cycles + 400000u;
            return;
        }
    }
    rm_n = 0; rm_over = !at_pc;                         /* only rounds at the known idle pc are kept */
#endif
    w_n = 0; w_bad = 0; w_c = c; w_rd0 = c->rd; w_wr0 = c->wr;
    c->rd = w_rd; c->wr = w_wr; c->watch = 1;
#ifdef T18_IDLE_CAP
    /* 1.6.8: a try away from the known idle pc stops after T18_IDLE_CAP instructions (every idle round found in
     * the test scripts had fewer than 128; the egg and call scenes tried 600 single steps about 50 times a second
     * without ever finding one) */
    extern int t18_new_off;
    int wmax = at_pc || t18_new_off ? W_MAXI : T18_IDLE_CAP;
#else
    int wmax = W_MAXI;
#endif
    for (int n = 0; n < wmax && !c->attn && c->cycles < target && !w_bad; n++) {
#ifdef T18_IDLE_RUN
        /* test14: one instruction through arm7_run (in the PRAM) instead of arm7_step (in the flash window:
         * on the device a quarter of the time while the pet calls) */
        arm7_run(c, c->cycles + 1u);
#else
        arm7_step(c);
#endif
#ifdef T18_IDLE_DBG
        { extern unsigned long long t18_idle_steps; t18_idle_steps++; }
#endif
#ifdef T18_IDLE_DEEP
        if (c->r[15] == p0 && c->cpsr == cpsr0 && !memcmp(c->r, r0, sizeof r0)) { ok = 1; break; }
#else
        if (c->r[15] == p0) { ok = 1; break; }
#endif
    }
    c->rd = w_rd0; c->wr = w_wr0; c->watch = 0;
    if (ok && !w_bad && c->cpsr == cpsr0 && !memcmp(c->r, r0, sizeof r0) && c->cycles > start) {
        for (int i = 0; i < w_n; i++) if (memcmp(&w_old[i], t18_sram_rp(w_off[i]), 4)) { ok = 0;
#ifdef T18_IDLE_DBG
            t18_w_bad_addr = 0xF8000000u + w_off[i];
#endif
            break; }
    } else ok = 0;
#ifdef T18_IDLE_DBG
    { extern void t18_idle_dbg(int why, uint32_t p0, arm7_t *c, const uint32_t *r0);
      t18_idle_dbg(ok ? 0 : w_bad ? 2 : c->attn ? 5 : c->cycles >= target ? 6 : c->r[15] != p0 ? 1 : c->cpsr != cpsr0 ? 3 : memcmp(c->r, r0, sizeof r0) ? 4 : 7, p0, c, r0); }
#endif
#ifdef T18_IDLE_CNT
    t18_idle_cnt[at_pc ? 2 : 3] += (uint32_t)(c->cycles - ic_c0);
#endif
    c->idle_win = target;                               /* idle_pc: once per run */
#if defined(T18_IDLE_DISARM) && defined(T18_JIT)
    /* 1.6.8: the known idle pc stays a stop for the translated code (no links into it), also while the
     * firmware is busy there (the pet calling: 7000 trips through the dispatcher a second). After
     * T18_IDLE_DISARM failed tries in a row it is given up; the tries elsewhere find it again when it is idle. */
    { static uint32_t pcfail;
      if (ok) pcfail = 0;
      else if (at_pc && !c->attn && c->cycles < target && ++pcfail >= T18_IDLE_DISARM && !t18_new_off) { pcfail = 0; c->idle_pc = 0; t18_jit_idle_pc(c, 0); } }
#endif
#ifdef T18_IDLE_MEMO
    rm_ok = 0;
    if (ok && !rm_over && c->cycles - start < 0x7FFFFFFFu) { rm_ok = 1; rm_pc = p0; rm_cpsr = cpsr0; memcpy(rm_r, r0, sizeof r0); rm_round = (uint32_t)(c->cycles - start);
        rm_mult = c->cyc_mult; rm_fgen = t18_flash_gen; }
#endif
    if (ok) {
        uint64_t round = c->cycles - start, k = c->cycles < target ? (target - c->cycles) / round : 0;
        c->cycles += k * round; c->idle_cycles += k * round; c->idle_skipped += k * round;
        c->idle_fail = 0; c->idle_next = c->cycles + 400000u;
        if (p0 != c->idle_pc) {
            c->idle_pc = p0;
#ifdef T18_JIT
            t18_jit_idle_pc(c, p0);
#endif
        }
    } else if (!at_pc) {
        if (c->cycles >= target || c->attn) c->idle_next = c->cycles + 1000u;   /* cut short: try again soon */
        else {
#ifdef T18_IDLE_BACKOFF
            /* test14: a loop that is not idle (the pet's sprite loop while it calls) was tried every 5 ms, each
             * time up to 600 single steps (on the device from the flash window: a quarter of the time). Now up to
             * every 85 ms; a known idle loop is still tried every time (idle_pc). */
            if (c->idle_fail < T18_IDLE_BACKOFF) c->idle_fail++;
#else
            if (c->idle_fail < 8) c->idle_fail++;
#endif
            c->idle_next = c->cycles + (2000u << c->idle_fail);
        }
    }
}
#ifdef T18_PROF_GPC
/* test: the interpreter's code lies between these (exec_arm, arm7_run with exec_thumb; checked in the map) */
const uintptr_t t18_interp_lo = (uintptr_t)exec_arm, t18_interp_hi = (uintptr_t)idle_try;
#endif
#ifdef T18_KEY_NOIDLE
/* 1.6.13 test11: after a key changed, no idle loops are skipped for a while (t18soc.c sets it): skipped, the game saw
 * a key pressed in the second half of its half-second step only one step later (about 1 s instead of 0.2 s) */
uint64_t t18_idle_hold;
#endif
static inline int idle_due(arm7_t *c, uint64_t target) {
    if (!t18_idle_on || c->attn) return 0;
#ifdef T18_KEY_NOIDLE
    if (c->cycles < t18_idle_hold) return 0;
#endif
#ifdef T18_IDLE_RUN
    if (c->watch) return 0;                             /* stepping inside idle_try */
#endif
    if (c->r[15] == c->idle_pc) return c->idle_win != target;
    return c->cycles >= c->idle_next;
}

/* Run until cycles >= target. Interrupts are taken before each instruction. */
int64_t arm7_run(arm7_t *c, uint64_t target) {
    uint64_t start = c->cycles;
    /* the clock (cyc_mult) may change inside the run: read it per instruction */
    c->attn = c->irq_line | c->fiq_line | c->halted;
#ifdef T18_JIT
    uint64_t jt_until = 0;
#endif

    while (c->cycles < target) {
        if (__builtin_expect(c->attn, 0)) {
            if (c->fiq_line && !(c->cpsr & ARM7_F)) {
                c->halted = 0;
                arm7_exception(c, M_FIQ, 0x1C, c->r[15] + 4);
            } else if (c->irq_line && !(c->cpsr & ARM7_I)) {
                c->halted = 0;
                arm7_exception(c, M_IRQ, 0x18, c->r[15] + 4);   /* lr_irq = next instruction + 4 */
            }
            if (c->halted) { c->idle_cycles += target - c->cycles; c->cycles = target; break; }
            c->attn = c->irq_line | c->fiq_line;
        }
        if (idle_due(c, target)) { idle_try(c, target); continue; }
#ifdef T18_JIT
        /* stop at the next idle check, so a chained loop comes back to it */
#ifdef T18_IDLE_RUN
        if (!t18_jit_off && c->cycles >= jt_until && !c->watch) {
#else
        if (!t18_jit_off && c->cycles >= jt_until) {
#endif
            uint64_t t = !t18_idle_on || c->idle_next <= c->cycles || target < c->idle_next ? target : c->idle_next;
#ifdef T18_SEQ_FAST
            extern uint32_t t18_jseq, t18_jstuck;
            uint32_t pcs = c->r[15];
            if (pcs == t18_jseq && pcs != t18_jstuck) t18_jseq = pcs + ((c->cpsr & ARM7_T) ? 2u : 4u);
            else
#endif
            {
#ifdef T18_IDLE_PASS
            { extern uint64_t t18_run_tgt; t18_run_tgt = target; }
#endif
            int r = t18_jit_try(c, t);
            if (r == 1) continue;
            if (r == 2) jt_until = t;                   /* too little left for a block: interpret up to there */
            }
        }
#endif
        uint32_t pc = c->r[15];
        c->last_pc = pc;
        int cyc;
#if defined(T18_JIT) && !defined(T18_NO_INTERP_CNT)
        t18_interp[(c->cpsr & ARM7_T) ? 0 : 1]++;
#endif
#ifdef T18_IPC_HIST   /* test only: which guest pcs the interpreter runs (pc, count) x 1024 at 0x20008000 */
        { volatile uint32_t *h = (volatile uint32_t *)0x20008000u + 2u * ((pc >> 1) & 1023u);
          if (h[0] == pc) h[1]++; else if (!h[1] || --h[1] == 0) { h[0] = pc; h[1] = 1; } }
#endif
#ifdef T18_PROF
        t18_prof(pc, c->cpsr & ARM7_T);
#endif
        if (c->cpsr & ARM7_T) { c->r[15] = pc + 2; cyc = exec_thumb(c, rd16(c, pc)); }
        else { c->r[15] = pc + 4; cyc = exec_arm(c, rd32a(c, pc)); }
#ifdef T18_CYC32
        c->cycles += (uint32_t)cyc * c->cyc_mult;      /* 1.6.8: 32-bit product (cyc is small) */
#else
        c->cycles += (uint64_t)cyc * c->cyc_mult;
#endif
    }
    return (int64_t)(c->cycles - start);
}
/* one instruction (the JIT uses it for what it does not translate) */
int arm7_step(arm7_t *c) {
    uint32_t pc = c->r[15];
    c->last_pc = pc;
    int cyc;
    if (c->cpsr & ARM7_T) { c->r[15] = pc + 2; cyc = exec_thumb(c, rd16(c, pc)); }
    else { c->r[15] = pc + 4; cyc = exec_arm(c, rd32a(c, pc)); }
    c->cycles += (uint64_t)cyc * c->cyc_mult;
    return cyc;
}

void arm7_save(arm7_t *c, arm7_state_t *s) {
    memset(s, 0, sizeof(*s));
    memcpy(s->r, c->r, sizeof(s->r)); s->cpsr = c->cpsr; s->spsr = c->spsr;
    memcpy(s->bank_usr, c->bank_usr, sizeof(s->bank_usr)); memcpy(s->bank_fiq, c->bank_fiq, sizeof(s->bank_fiq));
    memcpy(s->bank_irq, c->bank_irq, 8); memcpy(s->bank_svc, c->bank_svc, 8); memcpy(s->bank_abt, c->bank_abt, 8); memcpy(s->bank_und, c->bank_und, 8);
    s->spsr_fiq = c->spsr_fiq; s->spsr_irq = c->spsr_irq; s->spsr_svc = c->spsr_svc; s->spsr_abt = c->spsr_abt; s->spsr_und = c->spsr_und;
    s->cycles = c->cycles; s->halted = c->halted;
}
void arm7_load(arm7_t *c, const arm7_state_t *s) {
    memcpy(c->r, s->r, sizeof(s->r)); c->cpsr = s->cpsr; c->spsr = s->spsr;
    memcpy(c->bank_usr, s->bank_usr, sizeof(s->bank_usr)); memcpy(c->bank_fiq, s->bank_fiq, sizeof(s->bank_fiq));
    memcpy(c->bank_irq, s->bank_irq, 8); memcpy(c->bank_svc, s->bank_svc, 8); memcpy(c->bank_abt, s->bank_abt, 8); memcpy(c->bank_und, s->bank_und, 8);
    c->spsr_fiq = s->spsr_fiq; c->spsr_irq = s->spsr_irq; c->spsr_svc = s->spsr_svc; c->spsr_abt = s->spsr_abt; c->spsr_und = s->spsr_und;
    c->cycles = s->cycles; c->halted = s->halted;
}

