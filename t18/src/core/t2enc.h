/* Thumb-2 encoder for the T18 translator (Cortex-M3 code). Each function appends to *J. */
#ifndef T2ENC_H
#define T2ENC_H
#include <stdint.h>

extern uint16_t *J;      /* write pointer (halfwords) */
/* every write into the code buffer goes through JW: on the Paradise the buffer is in the PRAM, where a
 * write must not meet an instruction fetch from the PRAM (t18_pram_w16 runs from the flash window) */
#ifdef T18_PRAM_WR
void t18_pram_w16(uint16_t *p, uint32_t v);
#define JW(p, v) t18_pram_w16((p), (uint32_t)(v))
#else
#define JW(p, v) (*(p) = (uint16_t)(v))
#endif
static inline void E16(uint32_t h) { JW(J, h); J++; }
static inline void E32(uint32_t hw1, uint32_t hw2) { JW(J, hw1); JW(J + 1, hw2); J += 2; }

/* modified immediate (ThumbExpandImm): returns i:imm3:imm8 as 12 bits, or -1 */
static inline int t2_modimm(uint32_t v)
{
    if (v < 256) return (int)v;
    uint32_t b = v & 0xFF;
    if (v == (b | b << 16) && b) return (int)(0x100 | b);
    if (v == (b << 8 | b << 24) && b) return (int)(0x200 | b);
    if (v == (b | b << 16 | b << 8 | b << 24) && b) return (int)(0x300 | b);
    /* 1.6.6: the 8 bits straight from the highest and lowest set bit (was a loop over 24 rotations) */
    int hb = 31 - __builtin_clz(v), lb = __builtin_ctz(v);
    if (hb - lb <= 7) {
        uint32_t sh = (uint32_t)hb - 7u;                      /* >= 1 as v >= 256 */
        return (int)(((32u - sh) << 7) | ((v >> sh) & 0x7Fu));
    }
    if (!((v & 1u) && (v >> 31))) return -1;                  /* only a value around bit 0 can still fit */
    for (int rot = 8; rot < 32; rot++) {
        uint32_t x = (v << rot) | (v >> (32 - rot));          /* undo ror(rot) */
        if (x >= 0x80 && x < 0x100) return (int)((rot << 7) | (x & 0x7F));
    }
    return -1;
}
/* data processing (modified immediate): op = 4-bit opcode, s = set flags */
static inline void t2_dpimm(int op, int s, int rd, int rn, int imm12)
{
    E32(0xF000u | (uint32_t)((imm12 >> 11) & 1) << 10 | (uint32_t)op << 5 | (uint32_t)s << 4 | (uint32_t)rn,
        (uint32_t)((imm12 >> 8) & 7) << 12 | (uint32_t)rd << 8 | (uint32_t)(imm12 & 0xFF));
}
enum { T2_AND = 0, T2_BIC = 1, T2_ORR = 2, T2_ORN = 3, T2_EOR = 4, T2_ADD = 8, T2_ADC = 10, T2_SBC = 11, T2_SUB = 13, T2_RSB = 14 };
/* ADDW / SUBW rd, rn, #0..4095 (no flags) */
static inline void t2_addw(int rd, int rn, uint32_t imm)
{
    E32(0xF200u | ((imm >> 11) & 1) << 10 | (uint32_t)rn, ((imm >> 8) & 7) << 12 | (uint32_t)rd << 8 | (imm & 0xFF));
}
static inline void t2_subw(int rd, int rn, uint32_t imm)
{
    E32(0xF2A0u | ((imm >> 11) & 1) << 10 | (uint32_t)rn, ((imm >> 8) & 7) << 12 | (uint32_t)rd << 8 | (imm & 0xFF));
}
/* MOVW / MOVT */
static inline void t2_movw(int rd, uint32_t imm)
{
    E32(0xF240u | ((imm >> 11) & 1) << 10 | ((imm >> 12) & 15), ((imm >> 8) & 7) << 12 | (uint32_t)rd << 8 | (imm & 0xFF));
}
static inline void t2_movt(int rd, uint32_t imm)
{
    E32(0xF2C0u | ((imm >> 11) & 1) << 10 | ((imm >> 12) & 15), ((imm >> 8) & 7) << 12 | (uint32_t)rd << 8 | (imm & 0xFF));
}
static inline void t2_mov32(int rd, uint32_t v)
{
    int m = t2_modimm(v);
    if (m >= 0) { t2_dpimm(2, 0, rd, 15, m); return; }       /* MOV.W (ORR with rn=PC) */
    int n = t2_modimm(~v);
    if (n >= 0) { t2_dpimm(3, 0, rd, 15, n); return; }       /* MVN.W */
    t2_movw(rd, v & 0xFFFF);
    if (v >> 16) t2_movt(rd, v >> 16);
}
/* add rd, rn, #v with any v (no flags); may use rd as temporary only if rd != rn */
static inline void t2_addi(int rd, int rn, uint32_t v)
{
    if (v == 0) { if (rd != rn) E16(0x4600u | (uint32_t)(rd & 8) << 4 | (uint32_t)rn << 3 | (uint32_t)(rd & 7)); return; }
    int m = t2_modimm(v);
    if (m >= 0) { t2_dpimm(T2_ADD, 0, rd, rn, m); return; }
    m = t2_modimm(0u - v);
    if (m >= 0) { t2_dpimm(T2_SUB, 0, rd, rn, m); return; }
    if (v < 4096) { t2_addw(rd, rn, v); return; }
    if (0u - v < 4096) { t2_subw(rd, rn, 0u - v); return; }
    /* the low 12 bits, then 8-bit windows from the top (each one is a modified immediate) */
    uint32_t lo = v & 0xFFFu, hi = v & ~0xFFFu;
    if (lo) { t2_addw(rd, rn, lo); rn = rd; }
    while (hi) {
        int top = 31 - __builtin_clz(hi), sh = top - 7;
        uint32_t part = hi & (0xFFu << sh);
        t2_dpimm(T2_ADD, 0, rd, rn, t2_modimm(part)); rn = rd; hi &= ~part;
    }
}
/* MOV rd, rm (any registers, 16 bit) */
static inline void t2_mov(int rd, int rm) { if (rd != rm) E16(0x4600u | (uint32_t)(rd & 8) << 4 | (uint32_t)rm << 3 | (uint32_t)(rd & 7)); }
/* shifts by immediate, no flags: type 0 LSL 1 LSR 2 ASR 3 ROR */
static inline void t2_shi(int type, int rd, int rm, int n)
{
    E32(0xEA4Fu, (uint32_t)((n >> 2) & 7) << 12 | (uint32_t)rd << 8 | (uint32_t)(n & 3) << 6 | (uint32_t)type << 4 | (uint32_t)rm);
}
/* register-register data processing with optional shift (no flags unless s) */
static inline void t2_dpreg(int op, int s, int rd, int rn, int rm, int type, int n)
{
    E32(0xEA00u | (uint32_t)op << 5 | (uint32_t)s << 4 | (uint32_t)rn, (uint32_t)((n >> 2) & 7) << 12 | (uint32_t)rd << 8 | (uint32_t)(n & 3) << 6 | (uint32_t)type << 4 | (uint32_t)rm);
}
/* CMP.W rn, #imm (modified immediate) */
static inline void t2_cmpi(int rn, int imm12) { t2_dpimm(T2_SUB, 1, 15, rn, imm12); }
/* UBFX rd, rn, #lsb, #width */
static inline void t2_ubfx(int rd, int rn, int lsb, int width)
{
    E32(0xF3C0u | (uint32_t)rn, (uint32_t)((lsb >> 2) & 7) << 12 | (uint32_t)rd << 8 | (uint32_t)(lsb & 3) << 6 | (uint32_t)(width - 1));
}
/* loads and stores, immediate 0..4095: size 0 byte 1 half 2 word; sign for loads */
static inline void t2_ldst_i(int load, int size, int sign, int rt, int rn, uint32_t imm)
{
    uint32_t op = 0xF880u | (uint32_t)size << 5 | (uint32_t)load << 4 | (uint32_t)sign << 8;
    E32(op | (uint32_t)rn, (uint32_t)rt << 12 | (imm & 0xFFF));
}
/* loads and stores, register offset (rn + rm) */
static inline void t2_ldst_r(int load, int size, int sign, int rt, int rn, int rm)
{
    uint32_t op = 0xF800u | (uint32_t)size << 5 | (uint32_t)load << 4 | (uint32_t)sign << 8;
    E32(op | (uint32_t)rn, (uint32_t)rt << 12 | (uint32_t)rm);
}
/* LDMIA.W rn!, {list} / STMDB.W rn!, {list} / LDMIA / STMIA without writeback */
static inline void t2_ldmia(int rn, uint32_t list, int wb) { E32(0xE890u | (uint32_t)wb << 5 | (uint32_t)rn, list); }
static inline void t2_stmia(int rn, uint32_t list, int wb) { E32(0xE880u | (uint32_t)wb << 5 | (uint32_t)rn, list); }
static inline void t2_stmdb(int rn, uint32_t list, int wb) { E32(0xE900u | (uint32_t)wb << 5 | (uint32_t)rn, list); }
/* MRS rd, APSR / MSR APSR_nzcvq, rn */
static inline void t2_mrs(int rd) { E32(0xF3EFu, 0x8000u | (uint32_t)rd << 8); }
static inline void t2_msr(int rn) { E32(0xF380u | (uint32_t)rn, 0x8800u); }
/* PUSH/POP (16 bit, low registers + lr/pc) */
static inline void t2_push(uint32_t list) { E16(0xB400u | (list & 0xFF) | ((list >> 14) & 1) << 8); }
static inline void t2_pop(uint32_t list) { E16(0xBC00u | (list & 0xFF) | ((list >> 15) & 1) << 8); }
/* branches: at = position of the instruction, to = target (halfword pointers) */
static inline void t2_b_at(uint16_t *at, const uint16_t *to)
{
    int32_t off = (int32_t)((const char *)to - ((const char *)at + 4));
    uint32_t s = (uint32_t)(off < 0), imm = (uint32_t)off >> 1;
    uint32_t j1 = ((~(imm >> 22) ^ s) & 1), j2 = ((~(imm >> 21) ^ s) & 1);
    JW(&at[0], 0xF000u | s << 10 | ((imm >> 11) & 0x3FF));
    JW(&at[1], 0x9000u | j1 << 13 | j2 << 11 | (imm & 0x7FF));
}
static inline void t2_bl_at(uint16_t *at, const void *to)
{
    t2_b_at(at, (const uint16_t *)to); JW(&at[1], at[1] | 0x4000u);
}
static inline void t2_bcc_at(uint16_t *at, const uint16_t *to, int cond)
{
    int32_t off = (int32_t)((const char *)to - ((const char *)at + 4));
    uint32_t s = (uint32_t)(off < 0), imm = (uint32_t)off >> 1;
    uint32_t j1 = (imm >> 18) & 1, j2 = (imm >> 17) & 1;
    JW(&at[0], 0xF000u | s << 10 | (uint32_t)cond << 6 | ((imm >> 11) & 0x3F));
    JW(&at[1], 0x8000u | j1 << 13 | j2 << 11 | (imm & 0x7FF));
}
#endif
