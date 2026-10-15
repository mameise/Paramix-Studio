/* Translator S1C33 -> Thumb-2 for Cortex-M4. Same semantics as tc_fast.c
 * (verified by per-block lockstep runs against tc_exec). Included into the
 * same compilation unit as tc_all.c and uses its static helpers.
 *
 * Register allocation in generated code:
 *   r11 = Emu*   r10 = remaining cycles   r9 = a0ram   r4..r7 survive helpers
 *   r0..r3, r12, lr = free (memory accesses clobber them)
 * Flags live in e->nzcv in ARM format, with C meaning "no borrow"
 * (= not guest C), so CMP/SUB produce them directly. */
#include "tc.h"
#ifdef JIT_RSTAT
#include <stdio.h>
#endif

int jit_enabled = 1;
uint32_t jit_st_cw;

/* ---------- Memory for code and mapping ---------- */
#ifndef JIT_HASH
#define JIT_HASH 1024u
#endif
#define JIT_MAXI 32               /* guest instructions per block */
#define JIT_INTERP 1u             /* marker: use the interpreter */
#ifndef JIT_TICK
#define JIT_TICK 1024u            /* peripheral tick interval of the translator */
#endif

static uint8_t *J_buf;            /* host pointer */

static uint32_t J_tgt, J_size;    /* target address and size */
#ifdef JIT_STAGE
/* v0.8.16: Code is generated in SRAM (J_stage) and copied to PRAM only when complete, by a small
 * copy loop that itself runs from SRAM (interrupts off). On the device, writing to PRAM corrupts
 * instructions if the CPU is fetching code from PRAM at the same time. J_lim caps to the staging buffer. */
static uint8_t *J_stage; static uint32_t J_stage_sz, J_lo, J_lim;
static void (*J_copy)(uint32_t dst, const uint8_t *src, uint32_t n);
uint32_t jit_st_copy;
static void stage_begin(uint32_t lo) { J_lo = lo; J_buf = J_stage - lo; J_lim = lo + J_stage_sz; if (J_lim > J_size) J_lim = J_size; }
static void stage_commit(uint32_t lo, uint32_t hi)
{
    if (hi <= lo) return;
    uint32_t pm; __asm__ volatile("mrs %0, primask\n cpsid i" : "=r"(pm) :: "memory");
    J_copy(J_tgt + lo, J_stage, (hi - lo + 1u) & ~1u);
    __asm__ volatile("msr primask, %0" :: "r"(pm) : "memory");
    jit_st_copy++;
}
#define J_LIMIT J_lim
#else
#define J_LIMIT J_size
#endif
static uint32_t J_pos, J_fixed;   /* fill level, end of the fixed part */
static int J_full;
static uint32_t J_entry, J_exit;  /* entry/exit addresses */
static uint32_t J_chainx, J_plainx, J_idlex;  /* exits with target pc as a literal after the bl */
int jit_idle_on = 1;
#define JIT_CNT 2048
static uint8_t jit_cnt[JIT_CNT];
int jit_hot = 32, jit_interp_run = 1;
static uint32_t jit_prev_pc = 0xFFFFFFF0u;
uint32_t jit_st_patch;              /* skip idle loops until the next event */
uint32_t jit_st_idle;
static uint32_t J_slow[8];        /* shared slow paths */
static uint32_t J_stn, J_slown, J_st[3], J_pushn, J_popn, J_lds[5][7];   /* writes (fast+slow), pushn/popn */
static uint32_t jh_pc[JIT_HASH], jh_code[JIT_HASH];

/* field offsets in Emu */
#define OFF(f) ((uint32_t)offsetof(Emu, f))
#define O_R(i) (OFF(r) + 4u * (uint32_t)(i))

/* ---------- Emission ---------- */
#pragma clang attribute push (__attribute__((minsize)), apply_to = function)
static inline uint32_t HERE(void) { return J_tgt + J_pos; }
static void E16(uint32_t h)
{
    if (J_pos + 2 > J_LIMIT) { J_full = 1; return; }
#ifdef JIT_HWRITE
    *(volatile uint16_t *)(J_buf + J_pos) = (uint16_t)h; J_pos += 2;   /* halfword writes only into PRAM */
#else
    J_buf[J_pos] = (uint8_t)h; J_buf[J_pos + 1] = (uint8_t)(h >> 8); J_pos += 2;
#endif
}
static void E32(uint32_t h1, uint32_t h2) { E16(h1); E16(h2); }
static void P16(uint32_t tgt, uint32_t h) { uint32_t o = tgt - J_tgt; if (o + 2 > J_LIMIT
#ifdef JIT_STAGE
    || o < J_lo
#endif
    ) { J_full = 1; return; } 
#ifdef JIT_HWRITE
    *(volatile uint16_t *)(J_buf + o) = (uint16_t)h; }
#else
    J_buf[o] = (uint8_t)h; J_buf[o + 1] = (uint8_t)(h >> 8); }
#endif

enum { R0, R1, R2, R3, R4, R5, R6, R7, R8, R9, R10, R11, R12, SPR, LR, PCR };
enum { EQ, NE, CS, CC, MI, PL, VS, VC, HI, LS, GE, LT, GT, LE, AL };
enum { AND = 0, BIC = 1, ORR = 2, ORN = 3, EOR = 4, ADD = 8, ADC = 10, SBC = 11, SUB = 13, RSB = 14 };
enum { LSL, LSR, ASR, ROR };
#define NPOOL 7
#define G_SP 16
#define G_ALR 17
#define G_AHR 18
#define NG 19
static const int8_t pool_reg[NPOOL] = { R4, R5, R6, R7, R8, R2, R3 };
static int pool_idx(int r) { for (int i = 0; i < NPOOL; i++) if (pool_reg[i] == r) return i; return 0; }

static int modimm(uint32_t v)
{
    if (v < 256) return (int)v;
    uint32_t b = v & 0xFF;
    if (b && v == (b | (b << 16))) return 0x100 | (int)b;
    b = (v >> 8) & 0xFF;
    if (b && v == ((b << 8) | (b << 24))) return 0x200 | (int)b;
    b = v & 0xFF;
    if (b && v == b * 0x01010101u) return 0x300 | (int)b;
    {   /* 1xxxxxxx rotated right by rot (8..31): rot follows from the highest set bit */
        int lz = __builtin_clz(v);
        if (lz > 23) return -1;
        int rot = lz + 8;
        uint32_t u = (v << rot) | (v >> (32 - rot));
        if (u < 256) return (rot << 7) | (int)(u & 0x7F);
    }
    return -1;
}
static void dpi(int op, int s, int rd, int rn, int imm12)
{
    E32(0xF000u | ((uint32_t)(imm12 >> 11) << 10) | ((uint32_t)op << 5) | ((uint32_t)s << 4) | (uint32_t)rn,
        ((uint32_t)((imm12 >> 8) & 7) << 12) | ((uint32_t)rd << 8) | (uint32_t)(imm12 & 0xFF));
}
static void dpr(int op, int s, int rd, int rn, int rm, int type, int imm5)
{
    E32(0xEA00u | ((uint32_t)op << 5) | ((uint32_t)s << 4) | (uint32_t)rn,
        ((uint32_t)(imm5 >> 2) << 12) | ((uint32_t)rd << 8) | ((uint32_t)(imm5 & 3) << 6) | ((uint32_t)type << 4) | (uint32_t)rm);
}
static void movw_t(int rd, uint32_t v, int top)
{
    E32((top ? 0xF2C0u : 0xF240u) | (((v >> 11) & 1) << 10) | ((v >> 12) & 0xF),
        (((v >> 8) & 7) << 12) | ((uint32_t)rd << 8) | (v & 0xFF));
}
static void mov32(int rd, uint32_t v)
{
    int m = modimm(v);
    if (m >= 0) { dpi(ORR, 0, rd, PCR, m); return; }
    m = modimm(~v);
    if (m >= 0) { dpi(ORN, 0, rd, PCR, m); return; }
    movw_t(rd, v & 0xFFFF, 0);
    if (v >> 16) movw_t(rd, v >> 16, 1);
}
static void movr(int rd, int rm) { E16(0x4600u | ((uint32_t)(rd >> 3) << 7) | ((uint32_t)rm << 3) | (uint32_t)(rd & 7)); }
/* rd = rn OP imm, falling back to r12 */
static void opi(int op, int s, int rd, int rn, uint32_t imm)
{
    int m = modimm(imm);
    if (m >= 0) { dpi(op, s, rd, rn, m); return; }
    mov32(R12, imm);
    dpr(op, s, rd, rn, R12, LSL, 0);
}
/* Load/store with 12-bit offset or register */
enum { STRB_I = 0xF880, STRH_I = 0xF8A0, STR_I = 0xF8C0, LDRB_I = 0xF890, LDRH_I = 0xF8B0, LDR_I = 0xF8D0, LDRSB_I = 0xF990, LDRSH_I = 0xF9B0 };
enum { STRB_R = 0xF800, STRH_R = 0xF820, STR_R = 0xF840, LDRB_R = 0xF810, LDRH_R = 0xF830, LDR_R = 0xF850, LDRSB_R = 0xF910, LDRSH_R = 0xF930 };
static void ldsti(uint32_t base, int rt, int rn, uint32_t imm) { E32(base | (uint32_t)rn, ((uint32_t)rt << 12) | (imm & 0xFFF)); }
static void ldstr(uint32_t base, int rt, int rn, int rm) { E32(base | (uint32_t)rn, ((uint32_t)rt << 12) | (uint32_t)rm); }
static void ldf(int r, uint32_t off) { ldsti(LDR_I, r, R11, off); }
static void stf(uint32_t off, int r) { ldsti(STR_I, r, R11, off); }
static void shreg(int type, int s, int rd, int rn, int rm) { E32(0xFA00u | ((uint32_t)type << 5) | ((uint32_t)s << 4) | (uint32_t)rn, 0xF000u | ((uint32_t)rd << 8) | (uint32_t)rm); }
static void ext16(uint32_t h1, int rd, int rm) { E32(h1, 0xF080u | ((uint32_t)rd << 8) | (uint32_t)rm); }
#define SXTB(d, m) ext16(0xFA4F, d, m)
#define SXTH(d, m) ext16(0xFA0F, d, m)
#define UXTB(d, m) ext16(0xFA5F, d, m)
#define UXTH(d, m) ext16(0xFA1F, d, m)
#ifdef JIT_RSTAT
uint16_t jit_line_map[0x8000];
#define LINEMARK(l) do { if (J_pos / 2 < 0x8000u) jit_line_map[J_pos / 2] = (uint16_t)(l); } while (0)
#else
#define LINEMARK(l) do {} while (0)
#endif
static void mrs_(int rd, int l) { LINEMARK(l); E32(0xF3EF, 0x8000u | ((uint32_t)rd << 8)); }
static void msr_(int rn, int l) { LINEMARK(l); E32(0xF380u | (uint32_t)rn, 0x8800); }
#define mrs(r) mrs_(r, __LINE__)
#define msr(r) msr_(r, __LINE__)
static void it(int cond, uint32_t mask) { E16(0xBF00u | ((uint32_t)cond << 4) | mask); }
#define IT1(c)  it((c), 0x8)
#define ITE(c)  it((c), (((~(uint32_t)(c)) & 1u) << 3) | 4u)
#define ITT(c)  it((c), (((uint32_t)(c) & 1u) << 3) | 4u)

/* Branches */
static void enc_b(uint32_t at, uint32_t to, int link, uint32_t *h1, uint32_t *h2)
{
    int32_t off = (int32_t)(to - (at + 4));
    uint32_t S = ((uint32_t)off >> 24) & 1, I1 = ((uint32_t)off >> 23) & 1, I2 = ((uint32_t)off >> 22) & 1;
    uint32_t J1 = (~I1 ^ S) & 1, J2 = (~I2 ^ S) & 1;
    *h1 = 0xF000u | (S << 10) | (((uint32_t)off >> 12) & 0x3FF);
    *h2 = (link ? 0xD000u : 0x9000u) | (J1 << 13) | (J2 << 11) | (((uint32_t)off >> 1) & 0x7FF);
}
static void enc_bc(uint32_t at, uint32_t to, int cond, uint32_t *h1, uint32_t *h2)
{
    int32_t off = (int32_t)(to - (at + 4));
    uint32_t u = (uint32_t)off;
    *h1 = 0xF000u | (((u >> 20) & 1) << 10) | ((uint32_t)cond << 6) | ((u >> 12) & 0x3F);
    *h2 = 0x8000u | (((u >> 18) & 1) << 13) | (((u >> 19) & 1) << 11) | ((u >> 1) & 0x7FF);
}
static void b_to(uint32_t to) { uint32_t h1, h2; enc_b(HERE(), to, 0, &h1, &h2); E32(h1, h2); }
static void bl_to(uint32_t to) { uint32_t h1, h2; enc_b(HERE(), to, 1, &h1, &h2); E32(h1, h2); }
static void bc_to(int c, uint32_t to) { uint32_t h1, h2; enc_bc(HERE(), to, c, &h1, &h2); E32(h1, h2); }
static void patch_b(uint32_t at, uint32_t to) { uint32_t h1, h2; enc_b(at, to, 0, &h1, &h2); P16(at, h1); P16(at + 2, h2); }
static void patch_bc(uint32_t at, int c, uint32_t to) { uint32_t h1, h2; enc_bc(at, to, c, &h1, &h2); P16(at, h1); P16(at + 2, h2); }

#pragma clang attribute pop
/* ---------- Helpers (called from generated code) ---------- */
static uint32_t jit_req(Emu *e) { if (e->jit_bias) return 0; e->jit_bias = 0x40000000u; return 1; }
static uint32_t jit_codewrite(Emu *e, uint32_t a, uint32_t n)
{
    if (a > A0RAM_MAX - n) return 0;
    if (!(e->jit_map[a >> 8] | e->jit_map[(a + n - 1) >> 8])) return 0;
    jit_cw_note(e, a, n);
    e->jit_flush = 1; jit_st_cw++;
    memset(e->jit_map, 0, sizeof e->jit_map);
    return 1;
}
static uint32_t jit_irq_ready(Emu *e) { return e->pending_irq && e->f_ie && e->pending_level > e->il; }
#ifdef JIT_RSTAT
uint64_t jit_rstat[8][8];
static int rreg(Emu *e, uint32_t a){ if(a<0x8000)return 0; if(a-0x80000u<0x5000u)return 1; if(a-0x300000u<0x2000u)return 2; if(a-0x600000u<2u)return 3; if(a-e->dev.rom_base<e->dev.rom_size)return 4; return 5; }
#define RST(k,a) jit_rstat[k][rreg(e,a)]++
#else
#define RST(k,a) (void)0
#endif
#ifdef JIT_RSTAT
uint64_t jit_romhist[2048];
#endif
uint32_t jh_rd8(Emu *e, uint32_t a)  { RST(0,a);
#ifdef JIT_RSTAT
    if (a - e->dev.rom_base < e->dev.rom_size) jit_romhist[(a - e->dev.rom_base) >> 12]++;
#endif
    return f_rd8(e, a); }
uint32_t jh_rd16(Emu *e, uint32_t a) { RST(1,a); return f_rd16(e, a); }
uint32_t jh_rd32(Emu *e, uint32_t a) { RST(2,a); return f_rd32(e, a); }
static uint32_t jh_after_wr(Emu *e, uint32_t cw, int slow)
{
    if (cw || (slow && jit_irq_ready(e))) return jit_req(e);
    return 0;
}
uint32_t jh_wr8(Emu *e, uint32_t a, uint32_t v)
{
    RST(3,a);
    uint32_t cw = jit_codewrite(e, a, 1);
    uint8_t *p = f_ramp(e, a, 1); if (p) { *p = (uint8_t)v; return jh_after_wr(e, cw, 0); }
    mem_write8(e, a, (uint8_t)v); return jh_after_wr(e, cw, 1);
}
uint32_t jh_wr16(Emu *e, uint32_t a, uint32_t v)
{
    RST(4,a);
    uint32_t cw = jit_codewrite(e, a, 2);
    uint8_t *p = f_ramp(e, a, 2); if (p) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); return jh_after_wr(e, cw, 0); }
    mem_write16(e, a, (uint16_t)v); return jh_after_wr(e, cw, 1);
}
uint32_t jh_wr32(Emu *e, uint32_t a, uint32_t v)
{
    RST(5,a);
    uint32_t cw = jit_codewrite(e, a, 4);
    uint8_t *p = f_ramp(e, a, 4); if (p) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); return jh_after_wr(e, cw, 0); }
    mem_write32(e, a, v); return jh_after_wr(e, cw, 1);
}
/* Byte to LCD data (the most common peripheral access in games) */
uint32_t jh_lcd8(Emu *e, uint32_t a, uint32_t v)
{
    (void)a; RST(3, e->dev.lcd_data_addr);
    e->lcd_writes++; lcd_data(e, (uint8_t)v);
    return 0;
}
uint32_t jh_pushn(Emu *e, uint32_t last)
{
    RST(6,e->sp);
    uint32_t q = 0;
    for (int i = (int)last; i >= 0; i--) { e->sp -= 4; q |= jh_wr32(e, e->sp, e->r[i]); }
    return q;
}
uint32_t jh_popn(Emu *e, uint32_t last)
{
    RST(7,e->sp);
    for (uint32_t i = 0; i <= last; i++) { e->r[i] = f_rd32(e, e->sp); e->sp += 4; }
    return 0;
}
enum { H_RD8, H_RD16, H_RD32, H_WR8, H_WR16, H_WR32, H_PUSHN, H_POPN, H_LCD };
/* Slow paths: read 8u 8s 16u 16s 32, write 8 16 32 */
enum { S_RD8U, S_RD8S, S_RD16U, S_RD16S, S_RD32, S_WR8, S_WR16, S_WR32 };

#pragma clang attribute push (__attribute__((minsize)), apply_to = function)
/* ---------- fixed part: entry, exit, slow paths ----------
 * In generated code APSR always holds the guest flags (C as "no borrow").
 * All routines here leave APSR unchanged (helper calls are wrapped in
 * mrs/msr). */
static uint32_t J_exitn;            /* exit, flags already saved */
static void cbz_fwd(int rn, int nz, uint32_t to)   /* cbz/cbnz forward, target <= 126 bytes */
{
    uint32_t off = to - (HERE() + 4);
    E16((nz ? 0xB900u : 0xB100u) | (((off >> 6) & 1) << 9) | (((off >> 1) & 31) << 3) | (uint32_t)rn);
}
/* Patch cbz/cbnz into a reserved slot */
uint32_t jit_cbz_bad, jit_cbz_bad_stub; int jit_cbz_site;
static void patch_cbz(uint32_t at, int rn, int nz, uint32_t to)
{
    if (to < at + 4 || to - (at + 4) > 126u) { jit_cbz_bad++; if (jit_cbz_site) jit_cbz_bad_stub++; J_full = 1;
#ifdef JIT_RSTAT
        fprintf(stderr, "cbz too far: %u bytes, caller %p\n", to - (at + 4), __builtin_return_address(0));
#endif
        return; }
    uint32_t o = J_pos; J_pos = at - J_tgt; cbz_fwd(rn, nz, to); J_pos = o;
}
static uint32_t hole(void) { uint32_t at = HERE(); E16(0); return at; }
#define LDR_POST(rt, rn) E32(0xF850u | (uint32_t)(rn), ((uint32_t)(rt) << 12) | 0x0B04u)
#define STR_POST(rt, rn) E32(0xF840u | (uint32_t)(rn), ((uint32_t)(rt) << 12) | 0x0B04u)
#define LSRW(rd, rm, n)  dpr(ORR, 0, (rd), PCR, (rm), LSR, (n))
/* Read without helper, without flags: ROM block, ivram, dstram. Address r0,
 * value r1, bx lr. Otherwise falls through to the helper call after it. */
static void fast_rd(Emu *e, uint32_t ld, uint32_t n)
{
    uint32_t sz = e->dev.rom_size; int sh = 0; while ((1u << sh) < sz) sh++;
    if ((1u << sh) != sz || OFF(jit_parb) > 4000u || OFF(flash_autoselect) > 4000u) return;
    uint32_t b_al = 0;
    if (n > 1) {                                  /* aligned only: then it cannot cross a boundary */
        dpi(AND, 0, R1, R0, (int)(n - 1));
        E16(0xB100u | (1u << 3) | R1);            /* cbz r1, +4 */
        b_al = HERE(); E16(0); E16(0);            /* b.w helper (patched later) */
    }
    mov32(R2, e->dev.rom_base);
    dpr(SUB, 0, R2, R0, R2, LSL, 0);              /* r2 = a - rom_base */
    LSRW(R3, R2, sh); uint32_t c_norom = hole();  /* cbnz r3 -> not ROM */
    ldf(R3, OFF(flash_busy));
    ldsti(LDRB_I, R12, R11, OFF(flash_autoselect));
    dpr(ORR, 0, R3, R3, R12, LSL, 0); uint32_t c_busy = hole();   /* cbnz r3 -> helper */
    LSRW(R3, R2, 16);
    dpr(ADD, 0, R3, R11, R3, LSL, 2);
    ldsti(LDR_I, R3, R3, OFF(jit_rom)); uint32_t c_blk = hole();  /* cbz r3 -> parameter block */
    UXTH(R2, R2);
    ldstr(ld, R1, R3, R2);
    E16(0x4770);                                  /* bx lr */
    uint32_t par = HERE();
    ldf(R3, OFF(jit_parb));
    dpr(SUB, 0, R3, R2, R3, LSL, 0);              /* r3 = o - par */
    LSRW(R1, R3, 16); uint32_t c_np = hole();     /* cbnz r1 -> helper */
    LSRW(R3, R3, 13);
    dpr(ADD, 0, R3, R11, R3, LSL, 2);
    ldsti(LDR_I, R3, R3, OFF(jit_par)); uint32_t c_ps = hole();   /* cbz r3 -> helper */
    E32(0xF3C0u | R2, (R2 << 8) | 12u);           /* ubfx r2, r2, #0, #13 */
    ldstr(ld, R1, R3, R2);
    E16(0x4770);                                  /* bx lr */
    uint32_t norom = HERE();
    dpi(SUB, 0, R2, R0, modimm(0x80000u));        /* r2 = a - 0x80000 */
    LSRW(R3, R2, 15); uint32_t c_far = hole();    /* cbnz r3 -> helper */
    LSRW(R3, R2, 12);
    E32(0xE8DF, 0xF000u | R3);                    /* tbb [pc, r3] */
    uint32_t tab = HERE(); E16(0); E16(0); E16(0); E16(0);
    uint32_t iv = HERE();
    dpi(ADD, 0, R3, R9, modimm(0x8000u)); ldstr(ld, R1, R3, R2); E16(0x4770);
    uint32_t ds = HERE();                         /* dstram = a0ram + 0xB000, offset r2 - 0x4000 */
    dpi(ADD, 0, R3, R9, modimm(0x7000u)); ldstr(ld, R1, R3, R2); E16(0x4770);
    uint32_t help = HERE();
    if (b_al) patch_b(b_al, help);
    patch_cbz(c_norom, R3, 1, norom);
    patch_cbz(c_busy, R3, 1, help);
    patch_cbz(c_blk, R3, 0, par);
    patch_cbz(c_np, R1, 1, help);
    patch_cbz(c_ps, R3, 0, help);
    patch_cbz(c_far, R3, 1, help);
    { uint32_t t[8] = { iv, iv, iv, help, ds, help, help, help };
      for (int i = 0; i < 8; i++) { uint32_t o = (t[i] - tab) / 2; uint32_t at = tab + (uint32_t)i - J_tgt;
        if (at < J_LIMIT && !J_full) {
#ifdef JIT_HWRITE
            volatile uint16_t *hp = (volatile uint16_t *)(J_buf + (at & ~1u));
            *hp = (at & 1u) ? (uint16_t)((*hp & 0x00FFu) | (o << 8)) : (uint16_t)((*hp & 0xFF00u) | (o & 0xFFu));
#else
            J_buf[at] = (uint8_t)o;
#endif
        } } }
}
/* pushn (k=0) / popn (k=1) in a0ram without helper. r1 = last register.
 * Otherwise falls through to the helper call after it. */
static void fast_pushpop(int k)
{
    ldf(R0, OFF(sp));
    dpi(ADD, 0, R2, R1, 1); dpr(ORR, 0, R2, PCR, R2, LSL, 2);   /* r2 = 4*(n+1) */
    uint32_t c1, c2, c3 = 0, c4 = 0;
    if (k == 0) {
        dpr(SUB, 0, R3, R0, R2, LSL, 0);          /* new sp */
        LSRW(R2, R3, 15); c1 = hole();
        dpi(SUB, 0, R2, R0, 1); LSRW(R2, R2, 15); c2 = hole();
        dpr(ADD, 0, R2, R11, R3, LSR, 8); ldsti(LDRB_I, R2, R2, OFF(jit_map)); c3 = hole();
        dpi(SUB, 0, R2, R0, 1); dpr(ADD, 0, R2, R11, R2, LSR, 8); ldsti(LDRB_I, R2, R2, OFF(jit_map)); c4 = hole();
        stf(OFF(sp), R3);
        dpr(ADD, 0, R0, R9, R3, LSL, 0);          /* destination in a0ram */
        movr(R2, R11);                            /* e->r[0] */
        uint32_t lp = HERE();
        LDR_POST(R3, R2); STR_POST(R3, R0);
        uint32_t cd = hole(); dpi(SUB, 0, R1, R1, 1); b_to(lp);
        patch_cbz(cd, R1, 0, HERE());
    } else {
        dpr(ADD, 0, R3, R0, R2, LSL, 0);          /* new sp */
        dpi(SUB, 0, R2, R3, 1); LSRW(R2, R2, 15); c1 = hole();
        LSRW(R2, R0, 15); c2 = hole();
        stf(OFF(sp), R3);
        dpr(ADD, 0, R0, R9, R0, LSL, 0);
        movr(R2, R11);
        uint32_t lp = HERE();
        LDR_POST(R3, R0); STR_POST(R3, R2);
        uint32_t cd = hole(); dpi(SUB, 0, R1, R1, 1); b_to(lp);
        patch_cbz(cd, R1, 0, HERE());
    }
    E16(0x4770);                                  /* bx lr */
    uint32_t slow = HERE();
    patch_cbz(c1, R2, 1, slow); patch_cbz(c2, R2, 1, slow);
    if (k == 0) { patch_cbz(c3, R2, 1, slow); patch_cbz(c4, R2, 1, slow); }
}
static void call_helper2(int h, int keep)  /* r0..r2 = arguments, result r0; APSR preserved if keep */
{
    if (keep) mrs(R12);
    E32(0xE92D, 0x500C);                 /* push {r2, r3, r12, lr} */
    ldf(R12, OFF(jit_h) + 4u * (uint32_t)h);
    E16(0x4780 | (R12 << 3));            /* blx r12 */
    E32(0xE8BD, 0x500C);                 /* pop {r2, r3, r12, lr} */
    if (keep) msr(R12);
}
#define call_helper(h) call_helper2(h, 1)
static void emit_fixed(Emu *e)
{
    J_pos = 0;
    /* Entry: jit_enter(e, code|1) */
    J_entry = HERE();
    E32(0xE92D, 0x4FF8);                 /* push {r3-r11, lr} */
    movr(R11, R0);
    ldf(R10, OFF(jit_left));
    ldf(R9, OFF(jit_a0));
    ldf(R12, OFF(nzcv)); msr(R12);
    E16(0x4708);                         /* bx r1 */
    /* Exit: r0 = reason */
    J_exit = HERE();
    mrs(R12); stf(OFF(nzcv), R12);
    J_exitn = HERE();
    stf(OFF(jit_left), R10);
    E32(0xE8BD, 0x8FF8);                 /* pop {r3-r11, pc} */
    /* Exit with chaining: bl here, followed by .word pc. r0 = address of the bl */
    J_chainx = HERE();
    dpi(SUB, 0, R0, LR, 5);
    ldsti(LDR_I, R1, R0, 4);
    stf(OFF(pc), R1);
    b_to(J_exit);
    /* Exit without chaining (cycles used up) */
    J_plainx = HERE();
    dpi(SUB, 0, R0, LR, 1);
    ldsti(LDR_I, R1, R0, 0);
    stf(OFF(pc), R1);
    dpi(ORR, 0, R0, PCR, 0);             /* mov.w r0, #0 (no flags) */
    b_to(J_exit);
    /* Exit from an idle loop (jump to itself, waiting on memory) */
    J_idlex = HERE();
    dpi(SUB, 0, R0, LR, 1);
    ldsti(LDR_I, R1, R0, 0);
    stf(OFF(pc), R1);
    dpi(ORR, 0, R0, PCR, 1);             /* mov.w r0, #1 */
    b_to(J_exit);
    /* All fixed routines called from a block preserve r2/r3
     * (they belong to the register cache): slow paths go through a wrapper
     * push {r2,r3,lr}; bl inner; pop {r2,r3,pc}. */
#define HUELLE(lbl, inner) do { lbl = HERE(); E16(0xB50C); bl_to(inner); E16(0xBD0C); } while (0)
    /* Slow read (inner): address r0 -> value r1 */
    static const int rh[5] = { H_RD8, H_RD8, H_RD16, H_RD16, H_RD32 };
    uint32_t in_rd[5], in_wr[3], in_pp[2];
    for (int k = 0; k < 5; k++) {
        in_rd[k] = HERE();
        { static const uint32_t lo[5] = { LDRB_R, LDRSB_R, LDRH_R, LDRSH_R, LDR_R }, ln[5] = { 1, 1, 2, 2, 4 };
          fast_rd(e, lo[k], ln[k]); }
        movr(R1, R0); movr(R0, R11);
        call_helper(rh[k]);
        if (k == S_RD8S) SXTB(R1, R0);
        else if (k == S_RD16S) SXTH(R1, R0);
        else movr(R1, R0);
        E16(0x4770);                     /* bx lr */
    }
    for (int k = 0; k < 5; k++) HUELLE(J_slow[k], in_rd[k]);
    /* Slow read into a pool register: bl here, returns 4 bytes further (past the ldr) */
    for (int k = 0; k < 5; k++)
        for (int d = 0; d < NPOOL; d++) {
            J_lds[k][d] = HERE();
            E16(0xB500);                         /* push {lr} */
            bl_to(J_slow[k]);
            movr(pool_reg[d], R1);
            E16(0xBC01);                         /* pop {r0} */
            dpi(ADD, 0, R0, R0, 4);              /* add.w r0, r0, #4 (no flags) */
            E16(0x4700);                         /* bx r0 */
        }
    /* Slow write (inner): address r0, value r1. Byte: LCD data directly */
    static const int wh[3] = { H_WR8, H_WR16, H_WR32 };
    for (int k = 0; k < 3; k++) {
        in_wr[k] = HERE();
        if (k == 0) {
            mov32(R2, e->dev.lcd_data_addr);
            dpr(SUB, 0, R2, R0, R2, LSL, 0);
            uint32_t c3 = HERE(); E16(0);             /* cbnz r2 -> generic */
            movr(R2, R1); movr(R1, R0); movr(R0, R11);
            call_helper(H_LCD);
            E16(0x4770);
            patch_cbz(c3, R2, 1, HERE());
        }
        movr(R2, R1); movr(R1, R0); movr(R0, R11);
        call_helper(wh[k]);
        { uint32_t at = HERE(); E16(0); dpi(SUB, 0, R10, R10, modimm(0x40000000u));
          uint32_t o = J_pos; J_pos = at - J_tgt; cbz_fwd(R0, 0, J_tgt + o); J_pos = o; }
        E16(0x4770);                     /* bx lr */
    }
    for (int k = 0; k < 3; k++) HUELLE(J_slow[S_WR8 + k], in_wr[k]);
    /* Byte write when no flag is live afterwards: LCD data without mrs/msr
     * (in T25Emu these are fallbacks into its interpreter) */
    uint32_t in_wrn = HERE();
    {
        mov32(R2, e->dev.lcd_data_addr);
        dpr(SUB, 0, R2, R0, R2, LSL, 0);
        uint32_t c3 = HERE(); E16(0);             /* cbnz r2 -> generic */
        movr(R2, R1); movr(R1, R0); movr(R0, R11);
        call_helper2(H_LCD, 0);
        E16(0x4770);
        patch_cbz(c3, R2, 1, HERE());
        b_to(in_wr[0]);
    }
    HUELLE(J_slown, in_wrn);
    /* Write with fast path: address r0, value r1. Only r0, r1, r12 (no flags) */
    static const uint32_t sop[3] = { STRB_R, STRH_R, STR_R };
    for (int k = 0; k < 3; k++) {
        J_st[k] = HERE();
        movr(R12, R0);
        dpr(ORR, 0, R0, PCR, R0, LSR, 15);            /* lsr.w r0, r0, #15 */
        uint32_t c1 = HERE(); E16(0);                 /* cbnz r0, slow */
        dpr(ADD, 0, R0, R11, R12, LSR, 8);
        ldsti(LDRB_I, R0, R0, OFF(jit_map));
        uint32_t c2 = HERE(); E16(0);                 /* cbnz r0, slow */
        ldstr(sop[k], R1, R9, R12);
        E16(0x4770);                                  /* bx lr */
        uint32_t sl = HERE();
        movr(R0, R12);
        b_to(J_slow[S_WR8 + k]);
        patch_cbz(c1, R0, 1, sl);
        patch_cbz(c2, R0, 1, sl);
    }
    {
        J_stn = HERE();
        movr(R12, R0);
        dpr(ORR, 0, R0, PCR, R0, LSR, 15);
        uint32_t c1 = HERE(); E16(0);
        dpr(ADD, 0, R0, R11, R12, LSR, 8);
        ldsti(LDRB_I, R0, R0, OFF(jit_map));
        uint32_t c2 = HERE(); E16(0);
        ldstr(STRB_R, R1, R9, R12);
        E16(0x4770);
        uint32_t sl = HERE();
        movr(R0, R12);
        b_to(J_slown);
        patch_cbz(c1, R0, 1, sl);
        patch_cbz(c2, R0, 1, sl);
    }
    /* pushn/popn: fast path in a0ram, otherwise helper. r1 = last register */
    for (int k = 0; k < 2; k++) {
        in_pp[k] = HERE();
        fast_pushpop(k);
        movr(R0, R11);
        call_helper(k == 0 ? H_PUSHN : H_POPN);
        { uint32_t at = HERE(); E16(0); dpi(SUB, 0, R10, R10, modimm(0x40000000u));
          uint32_t o = J_pos; J_pos = at - J_tgt; cbz_fwd(R0, 0, J_tgt + o); J_pos = o; }
        E16(0x4770);                     /* bx lr */
    }
    HUELLE(J_pushn, in_pp[0]);
    HUELLE(J_popn, in_pp[1]);
#undef HUELLE
    (void)e;
    J_fixed = J_pos;
}

/* ---------- Decoding ---------- */
enum {
    K_NOP, K_INTERP,
    K_ALU_RR,     /* rd = rd op rs (op 0..7 as in class 1) */
    K_ALU_RX,     /* rd = rs op imm (with ext) */
    K_ALU_I,      /* rd = rd op imm */
    K_SHI,        /* rd = shift(rd, imm) */
    K_SHR,        /* rd = shift(rd, r[rs]&31) */
    K_SWAP, K_MIRROR,
    K_SPADJ,      /* sp += imm (two's complement) */
    K_LD, K_ST,   /* size, sign, base (rs or sp), offset, post-increment */
    K_EXT,        /* rd = sxtb/uxtb/sxth/uxth(rs) */
    K_MLTH, K_MLTUH, K_MLTW, K_MLTUW,
    K_ADC, K_SBC,
    K_BIT,        /* op: 2 btst 3 bclr 4 bset 5 bnot */
    K_LDSR,       /* rd = special register */
    K_STSR,       /* special register = r[rs] */
    K_PUSH, K_POP, K_PUSHN, K_POPN, K_PUSHS, K_POPS, K_CLRF,
    K_JR, K_JP, K_CALL,           /* direct, cc / always / call */
    K_JPR, K_JPREG, K_CALLREG, K_RET
};
enum { SH_SRL, SH_SLL, SH_SRA, SH_RR, SH_RL };
enum { O_ADD, O_SUB, O_CMP, O_MOV, O_AND, O_OR, O_XOR, O_NOT };
#define FN_ 8
#define FZ_ 4
#define FV_ 2
#define FC_ 1

typedef struct DI {
    uint32_t addr, next;       /* start (with ext), next address */
    uint8_t  len;              /* halfwords */
    uint8_t  kind, op, rd, rs, size, sgn, base_sp, post, dly, cond;
    uint32_t imm, target;
    uint8_t  fdef, fuse, fpart, live;
} DI;

static const uint8_t cond_need[16] = {
    [4] = FN_ | FZ_ | FV_, [5] = FN_ | FV_, [6] = FN_ | FV_, [7] = FN_ | FZ_ | FV_,
    [8] = FZ_ | FC_, [9] = FC_, [10] = FC_, [11] = FZ_ | FC_, [12] = FZ_, [13] = FZ_ };
static const uint8_t cond_arm[16] = { [4] = GT, [5] = GE, [6] = LT, [7] = LE, [8] = HI, [9] = CS, [10] = CC, [11] = LS, [12] = EQ, [13] = NE };

static int code_ok(Emu *e, uint32_t a)
{
    return a - e->dev.rom_base < e->dev.rom_size - 2 || a < A0RAM_MAX - 2;
}
static uint32_t code16(Emu *e, uint32_t a)
{
    if (a < A0RAM_MAX - 1) return (uint32_t)(e->a0ram[a] | (e->a0ram[a + 1] << 8));
    const uint8_t *p = rom_ptr(e, a - e->dev.rom_base);
    return (uint32_t)(p[0] | (p[1] << 8));
}

/* One instruction at a. Returns 0 if not readable. */
static int decode(Emu *e, uint32_t a, DI *d)
{
    memset(d, 0, sizeof *d);
    d->addr = a;
    if (!code_ok(e, a)) return 0;
    uint32_t hw = code16(e, a), pc = a;
    int ext_n = 0; uint32_t ext0 = 0, ext1 = 0;
    while ((hw >> 13) == 6) {
        if (ext_n == 0) ext0 = hw & 0x1FFF; else if (ext_n == 1) ext1 = hw & 0x1FFF;
        ext_n++; pc += 2;
        if (ext_n > 2 || !code_ok(e, pc)) { d->kind = K_INTERP; d->len = (uint8_t)((pc - a) / 2 + 1); d->next = pc + 2; return 1; }
        hw = code16(e, pc);
    }
    uint32_t iaddr = pc;
    d->next = pc + 2;
    d->len = (uint8_t)((d->next - a) / 2);
    int rd = hw & 0xF, rs = (hw >> 4) & 0xF;
    d->rd = (uint8_t)rd; d->rs = (uint8_t)rs;
    d->kind = K_INTERP;
    switch (hw >> 13) {
    case 0: {
        int op1 = (hw >> 9) & 0xF, dd = (hw >> 8) & 1;
        if (op1 <= 3) {
            int cnst = (hw >> 4) & 3;
            if (cnst == 0) {
                int op2 = (hw >> 6) & 3;
                switch (op1 * 8 + dd * 4 + op2) {
                case 0: d->kind = K_NOP; break;
                case 8: d->kind = K_PUSHN; break;
                case 9: d->kind = K_POPN; break;
                case 11: d->kind = K_JPR; d->target = iaddr; break;
                case 15: d->kind = K_JPR; d->target = iaddr; d->dly = 1; break;
                case 24: d->kind = K_CALLREG; break;
                case 25: d->kind = K_RET; break;
                case 26: d->kind = K_JPREG; break;
                case 28: d->kind = K_CALLREG; d->dly = 1; break;
                case 29: d->kind = K_RET; d->dly = 1; break;
                case 30: d->kind = K_JPREG; d->dly = 1; break;
                default: break;
                }
            } else if (cnst == 1 && op1 == 0) {
                switch ((hw >> 6) & 7) {
                case 0: d->kind = K_PUSH; break;
                case 1: d->kind = K_POP; break;
                case 2: if ((hw & 0xF) == 2 || (hw & 0xF) == 3) d->kind = K_PUSHS; break;
                case 3: if ((hw & 0xF) == 2 || (hw & 0xF) == 3) d->kind = K_POPS; break;
                case 7: d->kind = K_CLRF; d->fdef = 0xF; break;
                }
            }
        } else {
            uint32_t u8 = hw & 0xFF, target;
            if (ext_n == 0)      target = iaddr + SEXT(u8, 8) * 2;
            else if (ext_n == 1) target = iaddr + ((SEXT(ext0, 13) << 9) | (u8 << 1));
            else                 target = iaddr + (((ext0 & 0x1ff8) << 19) | (ext1 << 9) | (u8 << 1));
            d->target = target; d->dly = (uint8_t)dd;
            if (op1 == 14) d->kind = K_CALL;
            else if (op1 == 15) d->kind = K_JP;
            else { d->kind = K_JR; d->cond = (uint8_t)op1; d->fuse = cond_need[op1]; }
        }
        break;
    }
    case 1: {
        int op1 = (hw >> 10) & 7, op2 = (hw >> 8) & 3;
        if (op2 == 3) {
            static const int8_t m[8] = { SH_SRL, SH_SLL, SH_SRA, SH_SLL, SH_RR, SH_RL, -1, -1 };
            if (m[op1] < 0) break;
            d->kind = K_SHI; d->op = (uint8_t)m[op1]; d->imm = ((hw >> 4) & 0xF) + 16;
            d->fdef = FN_ | FZ_; d->fpart = 1;
            break;
        }
        if (op2 == 2) {
            uint32_t imm = (ext_n == 1) ? ext0 : ((ext0 << 13) | ext1);
            if (ext_n && (op1 == 3 || op1 == 7)) break;
            d->kind = ext_n ? K_ALU_RX : K_ALU_RR; d->op = (uint8_t)op1; d->imm = imm;
            if (op1 <= 2) d->fdef = 0xF;
            else if (op1 >= 4) { d->fdef = FN_ | FZ_ | FV_; d->fpart = 1; }
            break;
        }
        if (ext_n && op2 == 1) break;
        {
            static const uint8_t sz[8] = { 1, 1, 2, 2, 4, 1, 2, 4 }, sg[8] = { 1, 0, 1, 0, 0, 0, 0, 0 };
            d->kind = op1 >= 5 ? K_ST : K_LD; d->size = sz[op1]; d->sgn = sg[op1];
            d->imm = ext_n ? ((ext_n == 1) ? ext0 : ((ext0 << 13) | ext1)) : 0;
            d->post = (op2 == 1) ? sz[op1] : 0;
        }
        break;
    }
    case 2: {
        int op1 = (hw >> 10) & 7;
        uint32_t imm6 = (hw >> 4) & 0x3F, off;
        static const uint8_t sz[8] = { 1, 1, 2, 2, 4, 1, 2, 4 }, sg[8] = { 1, 0, 1, 0, 0, 0, 0, 0 };
        if (ext_n == 0) off = imm6 * sz[op1];
        else if (ext_n == 1) off = (ext0 << 6) | imm6;
        else off = (ext0 << 19) | (ext1 << 6) | imm6;
        d->kind = op1 >= 5 ? K_ST : K_LD; d->size = sz[op1]; d->sgn = sg[op1]; d->imm = off; d->base_sp = 1;
        break;
    }
    case 3: {
        int op1 = (hw >> 10) & 7;
        uint32_t imm6 = (hw >> 4) & 0x3F, imm;
        if (ext_n == 0) imm = (op1 == 0 || op1 == 1) ? imm6 : SEXT(imm6, 6);
        else if (ext_n == 1) { imm = (ext0 << 6) | imm6; if (!(op1 == 0 || op1 == 1)) imm = SEXT(imm, 19); }
        else imm = (ext0 << 19) | (ext1 << 6) | imm6;
        d->kind = K_ALU_I; d->op = (uint8_t)op1; d->imm = imm;
        if (op1 <= 2) d->fdef = 0xF;
        else if (op1 >= 4) { d->fdef = FN_ | FZ_ | FV_; d->fpart = 1; }
        break;
    }
    case 4: {
        int op1 = (hw >> 10) & 7, op2 = (hw >> 8) & 3;
        if (op1 == 0) { d->kind = K_SPADJ; d->imm = (uint32_t)(hw & 0x3FF) * 4; break; }
        if (op1 == 1) { d->kind = K_SPADJ; d->imm = 0u - (uint32_t)(hw & 0x3FF) * 4; break; }
        if (op2 == 2 && op1 == 4) { d->kind = K_SWAP; break; }
        if (op2 == 2 && op1 == 6) { d->kind = K_MIRROR; break; }
        static const uint8_t m[8] = { 0, 0, SH_SRL, SH_SLL, SH_SRA, SH_SLL, SH_RR, SH_RL };
        d->op = m[op1];
        if (op2 == 0) { d->kind = K_SHI; d->imm = (hw >> 4) & 0xF; }
        else d->kind = K_SHR;
        d->fdef = FN_ | FZ_; d->fpart = 1;
        break;
    }
    case 5: {
        int op1 = (hw >> 10) & 7, op2 = (hw >> 8) & 3;
        switch (op1 * 4 + op2) {
        case 0: if (rd == 1 || rd == 2 || rd == 3) { d->kind = K_STSR; d->op = (uint8_t)rd; } break;
        case 1: d->kind = K_EXT; d->op = 0; break;     /* sxtb */
        case 2: d->kind = K_MLTH; break;
        case 4: if (rs == 1 || rs == 2 || rs == 3 || rs == 8 || rs == 10 || rs == 11 || rs == 15) { d->kind = K_LDSR; d->op = (uint8_t)rs; d->imm = iaddr; } break;
        case 5: d->kind = K_EXT; d->op = 1; break;     /* uxtb */
        case 6: d->kind = K_MLTUH; break;
        case 8: case 12: case 16: case 20:
            d->kind = K_BIT; d->op = (uint8_t)op1; d->imm = ext_n ? ((ext_n == 1) ? ext0 : ((ext0 << 13) | ext1)) : 0;
            d->size = (uint8_t)(hw & 7);
            if (op1 == 2) { d->fdef = FZ_; d->fpart = 1; }
            break;
        case 9: d->kind = K_EXT; d->op = 2; break;     /* sxth */
        case 10: d->kind = K_MLTW; break;
        case 13: d->kind = K_EXT; d->op = 3; break;    /* uxth */
        case 14: d->kind = K_MLTUW; break;
        case 17: case 21: d->kind = K_NOP; break;
        case 24: d->kind = K_ADC; d->fdef = 0xF; d->fuse = FC_; break;
        case 28: d->kind = K_SBC; d->fdef = 0xF; d->fuse = FC_; break;
        default: break;
        }
        break;
    }
    default: break;
    }
    return 1;
}
static int is_branch(int k) { return k >= K_JR; }

/* Which flags does the code at t need before it sets them itself? Scans up
 * to the first branch (after that unknown = all). Fixed ROM only: RAM and the
 * parameter sectors may change later. Saves fixing up flags at exits that
 * the target overwrites anyway. */
#ifdef JIT_NOXL
int jit_xlive = 0;
#else
int jit_xlive = 1;
#endif
static uint8_t xl_pg[16]; static int xl_np;   /* RAM pages read by the lookahead */
static int live_fest(Emu *e, uint32_t a)
{
    if (a < A0RAM_MAX - 8u) {                     /* RAM: remember page, a write there flushes */
        for (uint32_t p = a >> 8; p <= (a + 7) >> 8; p++) {
            int q = 0; while (q < xl_np && xl_pg[q] != p) q++;
            if (q == xl_np) { if (xl_np >= (int)sizeof xl_pg) return 0; xl_pg[xl_np++] = (uint8_t)p; }
        }
        return 1;
    }
    if (a < A0RAM_MAX) return 0;
    uint32_t off = a - e->dev.rom_base;
    if (off >= e->dev.rom_size - 4) return 0;
    return off - e->par_base >= 0x10000u + 64u && off + 64u - e->par_base >= 0x10000u + 64u;
}
static uint8_t exit_live(Emu *e, uint32_t t)
{
    uint8_t need = 0, def = 0;
    if (!jit_xlive) return 0xF;
    for (int k = 0; k < 12; k++) {
        DI d;
        if (!live_fest(e, t) || !decode(e, t, &d) || d.kind == K_INTERP) break;
        need |= (uint8_t)(d.fuse & ~def);
        if (is_branch(d.kind)) {
            if (d.dly) {
                DI s2;
                if (!live_fest(e, d.next) || !decode(e, d.next, &s2) || s2.kind == K_INTERP) break;
                need |= (uint8_t)(s2.fuse & ~def); def |= s2.fdef;
                if ((def & 0xF) == 0xF) return need;
            }
            if (d.kind == K_JP || d.kind == K_CALL) { t = d.target; continue; }   /* fixed jump: continue there */
            break;
        }
        def |= d.fdef;
        if ((def & 0xF) == 0xF) return need;
        t = d.next;
    }
    return (uint8_t)((need | ~def) & 0xF);
}
#ifdef JIT_TEST
uint8_t jit_exit_live(Emu *e, uint32_t t) { xl_np = 0; return exit_live(e, t); }
#endif

/* ---------- Code generation ---------- */

/* Register cache: guest registers 0..15, 16 = sp, 17 = alr, 18 = ahr live
 * in r4..r8 during a block (preserved across helper calls).
 * Written back at the end of the block. */
static int8_t rc_g[NPOOL];          /* guest register, -1 free, -2 temp register */
static uint8_t rc_dirty[NPOOL];
static uint32_t rc_use[NPOOL], rc_clock;
static int8_t rc_of[NG];
static uint32_t goff(int g)
{
    if (g < 16) return O_R(g);
    return g == G_SP ? OFF(sp) : g == G_ALR ? OFF(alr) : OFF(ahr);
}
static void rc_reset(void)
{
    for (int i = 0; i < NPOOL; i++) { rc_g[i] = -1; rc_dirty[i] = 0; rc_use[i] = 0; }
    for (int g = 0; g < NG; g++) rc_of[g] = -1;
    rc_clock = 1;
}
static void rc_next(void) { rc_clock++; }      /* new instruction: old entries can be evicted */
static int rc_slot(void)
{
    int best = -1;
    for (int i = 0; i < NPOOL; i++) if (rc_g[i] == -1) return i;
    for (int i = 0; i < NPOOL; i++)
        if (rc_g[i] >= 0 && rc_use[i] < rc_clock && (best < 0 || rc_use[i] < rc_use[best])) best = i;
    if (best < 0) { J_full = 1; return 0; }  /* must not happen */
    int g = rc_g[best];
    if (rc_dirty[best]) stf(goff(g), pool_reg[best]);
    rc_of[g] = -1; rc_g[best] = -1; rc_dirty[best] = 0;
    return best;
}
static int rget(int g, int load, int write)
{
    int i = rc_of[g];
    if (i < 0) {
        i = rc_slot();
        rc_g[i] = (int8_t)g; rc_of[g] = (int8_t)i; rc_dirty[i] = 0;
        if (load) ldf(pool_reg[i], goff(g));
    }
    rc_use[i] = rc_clock;
    if (write) rc_dirty[i] = 1;
    return pool_reg[i];
}
#define RR(g)  rget((g), 1, 0)          /* read */
#define RW(g)  rget((g), 0, 1)          /* write only */
#define RRW(g) rget((g), 1, 1)          /* read and write */
static int tmp_get(void)
{
    int i = rc_slot();
    rc_g[i] = -2; rc_use[i] = 0xFFFFFFFFu;
    return pool_reg[i];
}
static int tmp_get_low(void)          /* temp register r2..r7 (for cbz) */
{
    int i = -1;
    for (int k = 0; k < NPOOL; k++) if (pool_reg[k] != R8 && rc_g[k] == -1) { i = k; break; }
    if (i < 0) {
        for (int k = 0; k < NPOOL; k++)
            if (pool_reg[k] != R8 && rc_g[k] >= 0 && rc_use[k] < rc_clock && (i < 0 || rc_use[k] < rc_use[i])) i = k;
        if (i < 0) { J_full = 1; return R4; }
        int g = rc_g[i];
        if (rc_dirty[i]) stf(goff(g), pool_reg[i]);
        rc_of[g] = -1; rc_dirty[i] = 0;
    }
    rc_g[i] = -2; rc_use[i] = 0xFFFFFFFFu;
    return pool_reg[i];
}
static void tmp_put(int r)
{
    for (int i = 0; i < NPOOL; i++) if (pool_reg[i] == r && rc_g[i] == -2) { rc_g[i] = -1; rc_use[i] = 0; }
}
static void rc_wb(int g)               /* write back a single register, it stays in the cache */
{
    int i = rc_of[g];
    if (i >= 0 && rc_dirty[i]) { stf(goff(g), pool_reg[i]); rc_dirty[i] = 0; }
}
static void rc_drop(int g)             /* discard entry (memory is newer) */
{
    int i = rc_of[g];
    if (i >= 0) { rc_g[i] = -1; rc_dirty[i] = 0; rc_use[i] = 0; rc_of[g] = -1; }
}
static void rc_flush(void)            /* write back everything modified (sets no flags) */
{
    for (int i = 0; i < NPOOL; i++)
        if (rc_g[i] >= 0 && rc_dirty[i]) { stf(goff(rc_g[i]), pool_reg[i]); rc_dirty[i] = 0; }
}

/* Memory access: address in r0. Read -> r1, write from r1. */
static void mem_rd(int size, int sgn)
{
    /* fast path without flags: a < 0x8000 -> ldr from a0ram, otherwise slow path */
    int k = size == 1 ? (sgn ? S_RD8S : S_RD8U) : size == 2 ? (sgn ? S_RD16S : S_RD16U) : S_RD32;
    static const uint32_t ops[5] = { LDRB_R, LDRSB_R, LDRH_R, LDRSH_R, LDR_R };
    dpr(ORR, 0, R1, PCR, R0, LSR, 15);            /* lsr.w r1, r0, #15 */
    E16(0xB100u | (2u << 3) | R1);                /* cbz r1, +4 -> fast path */
    bl_to(J_slow[k]);
    E16(0xE000u | 1u);                            /* b +2 (past the ldr) */
    ldstr(ops[k], R1, R9, R0);
}
/* like mem_rd, result directly in dst (r4..r8): fast path without going through r1 */
static void mem_rd_to(int dst, int size, int sgn, int base)   /* base: address register (r0 = copy only on the slow path) */
{
    if (base != R0) {
        int k = size == 1 ? (sgn ? S_RD8S : S_RD8U) : size == 2 ? (sgn ? S_RD16S : S_RD16U) : S_RD32;
        static const uint32_t ops[5] = { LDRB_R, LDRSB_R, LDRH_R, LDRSH_R, LDR_R };
        dpr(ORR, 0, R1, PCR, base, LSR, 15);          /* lsr.w r1, base, #15 */
        E16(0xB100u | (2u << 3) | R1);                /* cbz r1, past mov and bl -> fast path */
        movr(R0, base);
        bl_to(J_lds[k][pool_idx(dst)]);
        ldstr(ops[k], dst, R9, base);
        return;
    }
    int k = size == 1 ? (sgn ? S_RD8S : S_RD8U) : size == 2 ? (sgn ? S_RD16S : S_RD16U) : S_RD32;
    static const uint32_t ops[5] = { LDRB_R, LDRSB_R, LDRH_R, LDRSH_R, LDR_R };
    dpr(ORR, 0, R1, PCR, R0, LSR, 15);            /* lsr.w r1, r0, #15 */
    E16(0xB100u | (1u << 3) | R1);                /* cbz r1, +2 -> fast path */
    bl_to(J_lds[k][pool_idx(dst)]);               /* slow path, returns past the ldr */
    ldstr(ops[k], dst, R9, R0);
}
static uint32_t J_wlive = 0xF;    /* flags a write must preserve */
static void mem_wr(int size)
{
    bl_to(size == 1 && !J_wlive ? J_stn : J_st[size == 1 ? 0 : size == 2 ? 1 : 2]);
}

#define A_N 0x80000000u
#define A_Z 0x40000000u
#define A_C 0x20000000u
#define A_V 0x10000000u
/* Invert C (guest C = carry, stored as "no borrow"); now only used for adc */
static void fl_inv_c(void)
{
    mrs(R12); dpi(EOR, 0, R12, R12, modimm(A_C)); msr(R12);
}
/* ALU: dst = a OP (b | imm). a, b are ARM registers (already loaded). */
static int imm_keeps_c(uint32_t imm) { int m = modimm(imm); return m >= 0 && m < 0x400; }
static void gen_alu(const DI *d, int op, int ra, int has_imm, uint32_t imm, int rb)
{
    int live = d->live & d->fdef;
    switch (op) {
    case O_ADD: case O_SUB: case O_CMP: {
        int aop = (op == O_ADD) ? ADD : SUB;
        int s = live ? 1 : 0;
        if (op == O_CMP && !live) break;
        int dst = (op == O_CMP) ? PCR : RW(d->rd);
        if (live && op == O_ADD && (d->live & FC_)) {
            /* Guest C after add = no carry. cmp ~b, a gives exactly that as C,
             * and V as for add; N, Z then from the result (tst keeps C, V).
             * Without mrs/msr (fallbacks in T25Emu). */
            if (has_imm) mov32(R12, ~imm); else dpr(ORN, 0, R12, PCR, rb, LSL, 0);   /* r12 = ~b */
            dpr(SUB, 1, PCR, R12, ra, LSL, 0);                                        /* cmp r12, a */
            if (has_imm) opi(ADD, 0, dst, ra, imm); else dpr(ADD, 0, dst, ra, rb, LSL, 0);
            if (d->live & (FN_ | FZ_)) dpr(AND, 1, PCR, dst, dst, LSL, 0);           /* tst */
            break;
        }
        if (has_imm) opi(aop, s, dst, ra, imm); else dpr(aop, s, dst, ra, rb, LSL, 0);
        break;
    }
    case O_MOV: {
        int dst = RW(d->rd);
        if (has_imm) mov32(dst, imm); else movr(dst, rb);
        break;
    }
    default: {
        int lop = op == O_AND ? AND : op == O_OR ? ORR : op == O_XOR ? EOR : ORN;
        int vfix = live && (d->live & FV_);   /* clear V: without mrs/msr, see below */
        int s = live && !vfix ? 1 : 0;
        int dst = RW(d->rd);
        if (op == O_NOT) {
            if (has_imm) { mov32(dst, ~imm); if (s) dpr(AND, 1, PCR, dst, dst, LSL, 0); }   /* tst dst, dst */
            else dpr(ORN, s, dst, PCR, rb, LSL, 0);                                         /* mvns */
        } else if (has_imm) {
            if (!s || imm_keeps_c(imm)) opi(lop, s, dst, ra, imm);
            else { mov32(R12, imm); dpr(lop, 1, dst, ra, R12, LSL, 0); }
        } else dpr(lop, s, dst, ra, rb, LSL, 0);
        if (vfix) {
            if (d->live & FC_) {
                /* save C: r12 = C (adc), cmp r12, #1 sets C = r12 and V = 0, then tst sets N, Z */
                dpi(ORR, 0, R12, PCR, 0);
                dpi(ADC, 0, R12, R12, 0);
                dpi(SUB, 1, PCR, R12, 1);
            } else
                dpi(SUB, 1, PCR, dst, 0);                 /* cmp dst, #0: N, Z, V = 0 */
            if (d->live & FC_) dpr(AND, 1, PCR, dst, dst, LSL, 0);   /* tst dst, dst */
        }
        break;
    }
    }
}

static void gen_shift(const DI *d, int byreg)
{
    int a = RR(d->rd);
    if (byreg) {
        int rs = RR(d->rs);
        dpi(AND, 0, R1, rs, 31);
        a = RRW(d->rd);
        switch (d->op) {
        case SH_SRL: shreg(LSR, 0, a, a, R1); break;
        case SH_SLL: shreg(LSL, 0, a, a, R1); break;
        case SH_SRA: shreg(ASR, 0, a, a, R1); break;
        case SH_RR:  shreg(ROR, 0, a, a, R1); break;
        case SH_RL:  dpi(RSB, 0, R1, R1, 32); shreg(ROR, 0, a, a, R1); break;
        }
    } else {
        int n = (int)d->imm;
        a = RRW(d->rd);
        if (n) switch (d->op) {
        case SH_SRL: dpr(ORR, 0, a, PCR, a, LSR, n); break;
        case SH_SLL: dpr(ORR, 0, a, PCR, a, LSL, n); break;
        case SH_SRA: dpr(ORR, 0, a, PCR, a, ASR, n); break;
        case SH_RR:  dpr(ORR, 0, a, PCR, a, ROR, n); break;
        case SH_RL:  dpr(ORR, 0, a, PCR, a, ROR, 32 - n); break;
        }
    }
    if (d->live & d->fdef) dpr(AND, 1, PCR, a, a, LSL, 0);   /* tst a, a: N, Z; C, V unchanged */
}

/* Address into r0 */
static void addr_of(const DI *d)
{
    int b = RR(d->base_sp ? G_SP : d->rs);
    if (d->imm) opi(ADD, 0, R0, b, d->imm); else movr(R0, b);
}

/* sp -= 4; [sp] = value in r1 */
static void push_r1(void)
{
    int sp = RRW(G_SP);
    dpi(SUB, 0, sp, sp, 4);
    movr(R0, sp);
    mem_wr(4);
}

/* Exits: bl J_chainx with pc after it, becomes b.w target when chained */
/* Exit with fixed target: ldr pc,[r11,#slot] jumps through a slot in
 * jit_slot. Initially it points to the bl J_chainx after it; chaining only
 * rewrites the slot (data), the code stays unchanged. This keeps the
 * instruction cache clean and matters in T25Emu: there, any change to
 * code that has already run discards all translations. */
static Emu *J_e;
static uint32_t J_nslot;
static void exit_direct(uint32_t pc)
{
    if (J_nslot >= 600u || OFF(jit_slot) + 4u * 600u > 4095u) { J_full = 1; return; }
    uint32_t sl = J_nslot++;
    /* ldr.w r12, [r11, #slot]; bx r12. Not ldr pc: T25Emu cannot translate
     * that and falls back to its interpreter every time. */
    E32(0xF8D0u | R11, ((uint32_t)R12 << 12) | (OFF(jit_slot) + 4u * sl));
    E16(0x4700u | (R12 << 3));
    J_e->jit_slot[sl] = HERE() | 1u;
    bl_to(J_chainx); E16(pc & 0xFFFFu); E16(pc >> 16);
}
static void exit_reg(int r)          /* pc from register (r1 or pool); the target checks cycles */
{
    /* without touching flags (no mrs/msr: a fallback in T25Emu) */
    stf(OFF(pc), r);
    E32(0xF3C0u | (uint32_t)r, (0u << 12) | (R0 << 8) | (1u << 6) | 7u);   /* ubfx r0, r, #1, #8 */
    dpr(ADD, 0, R0, R11, R0, LSL, 3);
    ldsti(LDR_I, LR, R0, OFF(jit_itab) + 4);        /* code */
    ldsti(LDR_I, R0, R0, OFF(jit_itab));            /* guest pc */
    dpr(SUB, 0, R0, R0, r, LSL, 0);                 /* r0 = r0 - r */
    E16(0xB900u | R0);                              /* cbnz r0, +0 -> exit */
    E16(0x4700u | (LR << 3));                       /* bx lr */
    mrs(R12);
    stf(OFF(nzcv), R12);
    dpi(ORR, 0, R0, PCR, 0);                        /* mov.w r0, #0 */
    b_to(J_exitn);
}
/* Place the block entry's bail-out (cycles used up) in dead code */
static uint32_t E_cb, E_pc, E_lim = 90;
static void entry_stub_place(void)
{
    if (!E_cb) return;
    if (HERE() - (E_cb + 4) > 126u) return;
    patch_cbz(E_cb, R3, 1, HERE());
    bl_to(J_plainx); E16(E_pc & 0xFFFFu); E16(E_pc >> 16);
    E_cb = 0;
}
static void entry_stub_maybe(void)            /* if it gets too far: place it with a jump over it */
{
    if (!E_cb || HERE() - E_cb < E_lim) return;
    uint32_t at = HERE(); E16(0);
    entry_stub_place();
    P16(at, 0xE000u | (((HERE() - (at + 4)) >> 1) & 0x7FFu));
}
uint32_t jit_st_kb[48], jit_st_kn[48];
static void gen_insn2(Emu *e, const DI *d);
static void gen_insn(Emu *e, const DI *d)
{
    entry_stub_maybe();
    uint32_t h0 = J_pos;
    J_wlive = (uint8_t)(d->live | d->fuse); gen_insn2(e, d); J_wlive = 0xF;
    int k = d->kind; if (k == K_ALU_RR || k == K_ALU_RX || k == K_ALU_I) k = 32 + (d->op) + (d->live & d->fdef ? 8 : 0);
    jit_st_kb[k] += J_pos - h0; jit_st_kn[k]++;
}
static void gen_insn2(Emu *e, const DI *d)
{
    (void)e;
    rc_next();
    switch (d->kind) {
    case K_NOP: break;
    case K_ALU_RR: {
        int a = (d->op == O_MOV || d->op == O_NOT) ? 0 : RR(d->rd);
        int b = RR(d->rs);
        gen_alu(d, d->op, a, 0, 0, b);
        break;
    }
    case K_ALU_RX:
        gen_alu(d, d->op, RR(d->rs), 1, d->imm, 0);
        break;
    case K_ALU_I: {
        int a = (d->op != O_MOV && d->op != O_NOT) ? RR(d->rd) : 0;
        gen_alu(d, d->op, a, 1, d->imm, 0);
        break;
    }
    case K_SHI: gen_shift(d, 0); break;
    case K_SHR: gen_shift(d, 1); break;
    case K_SWAP: case K_MIRROR: {
        int s = RR(d->rs), t = RW(d->rd);
        E32(0xFA90u | (uint32_t)s, (d->kind == K_SWAP ? 0xF080u : 0xF090u) | ((uint32_t)t << 8) | (uint32_t)s);
        break;
    }
    case K_SPADJ: { int sp = RRW(G_SP); opi(ADD, 0, sp, sp, d->imm); break; }
    case K_LD: {
        int base = R0;
        if (d->imm) addr_of(d); else base = RR(d->base_sp ? G_SP : d->rs);
        rc_next();
        { int dst = RW(d->rd); mem_rd_to(dst, d->size, d->sgn, base); }
        if (d->post) { int b = RRW(d->rs); dpi(ADD, 0, b, b, d->post); }
        break;
    }
    case K_ST: {
        addr_of(d);
        movr(R1, RR(d->rd));
        mem_wr(d->size);
        if (d->post) { int b = RRW(d->rs); dpi(ADD, 0, b, b, d->post); }
        break;
    }
    case K_EXT: {
        int s = RR(d->rs), t = RW(d->rd);
        if (d->op == 0) SXTB(t, s); else if (d->op == 1) UXTB(t, s);
        else if (d->op == 2) SXTH(t, s); else UXTH(t, s);
        break;
    }
    case K_MLTH: case K_MLTUH: {
        int a = RR(d->rd), b = RR(d->rs);
        if (d->kind == K_MLTH) { SXTH(R0, a); SXTH(R1, b); } else { UXTH(R0, a); UXTH(R1, b); }
        int t = RW(G_ALR);
        E32(0xFB00u | R0, 0xF000u | ((uint32_t)t << 8) | R1);    /* mul t, r0, r1 */
        break;
    }
    case K_MLTW: case K_MLTUW: {
        int a = RR(d->rd), b = RR(d->rs);
        E32((d->kind == K_MLTW ? 0xFB80u : 0xFBA0u) | (uint32_t)a, (R0 << 12) | (R1 << 8) | (uint32_t)b);
        movr(RW(G_ALR), R0); movr(RW(G_AHR), R1);
        break;
    }
    case K_ADC: case K_SBC: {
        int a = RRW(d->rd), b = RR(d->rs);
        if (d->kind == K_ADC) fl_inv_c();               /* ARM C = guest C for adcs */
        dpr(d->kind == K_ADC ? ADC : SBC, 1, a, a, b, LSL, 0);
        if (d->kind == K_ADC) fl_inv_c();
        break;
    }
    case K_BIT: {
        int b = RR(d->rs);
        if (d->imm) opi(ADD, 0, R0, b, d->imm); else movr(R0, b);
        int t = tmp_get();
        movr(t, R0);
        mem_rd(1, 0);
        uint32_t m = 1u << d->size;
        if (d->op == 2) {
            if (d->live & FZ_) {
                mrs(R12); dpi(BIC, 0, R12, R12, modimm(A_Z));
                dpi(AND, 0, R1, R1, (int)m);            /* r1 &= m (no flags) */
                E16(0xB900u | (1u << 3) | R1);          /* cbnz r1, past the orr */
                dpi(ORR, 0, R12, R12, modimm(A_Z));
                msr(R12);
            }
        } else {
            int op = d->op == 3 ? BIC : d->op == 4 ? ORR : EOR;
            dpi(op, 0, R1, R1, (int)m);
            movr(R0, t);
            mem_wr(1);
        }
        tmp_put(t);
        break;
    }
    case K_LDSR: {
        switch (d->op) {
        case 1: { int s = RR(G_SP); movr(RW(d->rd), s); break; }
        case 2: { int s = RR(G_ALR); movr(RW(d->rd), s); break; }
        case 3: { int s = RR(G_AHR); movr(RW(d->rd), s); break; }
        case 8: ldf(RW(d->rd), OFF(ttbr)); break;
        case 10: ldf(RW(d->rd), OFF(idir)); break;
        case 11: ldf(RW(d->rd), OFF(dbbr)); break;
        default: mov32(RW(d->rd), d->imm); break;
        }
        break;
    }
    case K_STSR: {
        int s = RR(d->rs);
        movr(RW(d->op == 1 ? G_SP : d->op == 2 ? G_ALR : G_AHR), s);
        break;
    }
    case K_PUSH: movr(R1, RR(d->rd)); push_r1(); break;
    case K_POP: {
        int sp = RRW(G_SP);
        movr(R0, sp); dpi(ADD, 0, sp, sp, 4);
        mem_rd(4, 0);
        rc_next();
        movr(RW(d->rd), R1);
        break;
    }
    case K_PUSHN:
        for (int i = 0; i <= d->rd; i++) rc_wb(i);
        rc_wb(G_SP); rc_drop(G_SP);
        mov32(R1, d->rd); bl_to(J_pushn);
        break;
    case K_POPN:
        rc_wb(G_SP); rc_drop(G_SP);
        for (int i = 0; i <= d->rd; i++) rc_drop(i);
        mov32(R1, d->rd); bl_to(J_popn);
        break;
    case K_PUSHS:
        if (d->rd == 3) { movr(R1, RR(G_AHR)); push_r1(); rc_next(); }
        movr(R1, RR(G_ALR)); push_r1();
        break;
    case K_POPS: {
        int sp = RRW(G_SP);
        movr(R0, sp); dpi(ADD, 0, sp, sp, 4);
        mem_rd(4, 0); rc_next(); movr(RW(G_ALR), R1);
        if (d->rd == 3) {
            rc_next();
            sp = RRW(G_SP);
            movr(R0, sp); dpi(ADD, 0, sp, sp, 4);
            mem_rd(4, 0); rc_next(); movr(RW(G_AHR), R1);
        }
        break;
    }
    case K_CLRF:
        mov32(R12, A_C); msr(R12);
        break;
    default: break;
    }
}

/* ---------- Mapping ---------- */
static uint32_t hslot(uint32_t pc) { return ((pc >> 1) ^ (pc >> 11)) & (JIT_HASH - 1); }
static uint32_t lookup(uint32_t pc)
{
    for (uint32_t i = hslot(pc), n = 0; n < JIT_HASH; n++, i = (i + 1) & (JIT_HASH - 1)) {
        if (jh_pc[i] == pc) return jh_code[i];
        if (jh_pc[i] == 0xFFFFFFFFu) return 0;
    }
    return 0;
}
static int insert(uint32_t pc, uint32_t code)
{
    for (uint32_t i = hslot(pc), n = 0; n < JIT_HASH / 2; n++, i = (i + 1) & (JIT_HASH - 1))
        if (jh_pc[i] == 0xFFFFFFFFu) { jh_pc[i] = pc; jh_code[i] = code; return 1; }
    return 0;
}
static uint32_t jit_count, jit_gen;
uint32_t jit_st_insns, jit_st_flush, jit_st_blocks, jit_st_bytes;
void jit_flush_all(Emu *e)
{
    memset(jh_pc, 0xFF, sizeof jh_pc);
    memset(e->jit_itab, 0xFF, sizeof e->jit_itab);
    J_pos = J_fixed; J_full = 0; jit_count = 0; jit_gen++; jit_st_flush++; J_nslot = 0;
#ifndef JIT_KEEPCNT
    memset(jit_cnt, 0, sizeof jit_cnt);   /* KEEPCNT: retranslate hot blocks right after a flush */
#endif
    memset(e->jit_map, 0, sizeof e->jit_map);
    e->jit_flush = 0;
}

/* Store modified registers without changing the cache state (side exit) */
static void rc_store_dirty(void)
{
    for (int i = 0; i < NPOOL; i++)
        if (rc_g[i] >= 0 && rc_dirty[i]) stf(goff(rc_g[i]), pool_reg[i]);
}
#ifndef JIT_SUPER
#define JIT_SUPER 1                /* conditional branches inside a block: side exit instead of block end */
#endif
int jit_super = JIT_SUPER;
/* Translate the block at pc. 0 = no space, JIT_INTERP = interpreter.
 * A block continues past conditional branches (superblock): a taken
 * branch leaves it as a side exit, and the register cache is kept for the
 * fall-through. Cycles are deducted per section so that a side exit costs
 * exactly as much as in the interpreter. */
static uint32_t translate(Emu *e, uint32_t pc)
{
    static DI di[JIT_MAXI + 2];
    static uint8_t side[JIT_MAXI + 2];   /* 1: a section ends here with a conditional branch */
    int n = 0, end_kind = 0;      /* 0 = fall-through, 1 = branch */
    uint32_t a = pc;
    int ram = pc < A0RAM_MAX;
    while (n < JIT_MAXI) {
        DI *d = &di[n];
        side[n] = 0;
        if (!decode(e, a, d) || d->kind == K_INTERP) break;
        if (ram && d->next > A0RAM_MAX - 2) break;
        if (is_branch(d->kind)) {
            int m = 1;
            if (d->dly) {
                DI *s = &di[n + 1];
                side[n + 1] = 0;
                if (!decode(e, d->next, s) || s->kind == K_INTERP || is_branch(s->kind)) break;
                if (ram && s->next > A0RAM_MAX - 2) break;
                m = 2;
            }
            if (jit_super && d->kind == K_JR && n + m < JIT_MAXI - 1) {
                side[n] = 1;                       /* continue on the fall-through */
                n += m;
                a = di[n - 1].next;
                continue;
            }
            n += m;
            end_kind = 1;
            break;
        }
        n++;
        a = d->next;
    }
    if (n == 0) return JIT_INTERP;
    uint32_t next = di[n - 1].next;
    /* Idle loop: the first section jumps to itself, only reads memory
     * and compares. Its result only changes when an interrupt or a
     * peripheral writes something; until then time may fast-forward. Counter
     * loops (registers change) do not qualify. */
    int idle_at = -1;
    {
        int j = 0; while (j < n && !is_branch(di[j].kind)) j++;
        if (j < n && j >= 1 && di[j].kind == K_JR && !di[j].dly && di[j].target == pc) {
            uint32_t bases = 0, dsts = 0; int ok = 1;
            for (int i = 0; i < j && ok; i++) {
                DI *d = &di[i];
                if (d->kind == K_LD && !d->post) { bases |= 1u << (d->base_sp ? 16 : d->rs); dsts |= 1u << d->rd; }
                else if ((d->kind == K_ALU_RR || d->kind == K_ALU_RX || d->kind == K_ALU_I) && d->op == O_CMP) {}
                else if (d->kind == K_BIT && d->op == 2) {}
                else if (d->kind == K_NOP) {}
                else ok = 0;
            }
            if (ok && !(bases & dsts)) idle_at = j;
        }
    }
    /* Flag liveness, backwards; all live at every side exit */
    uint8_t live;
    xl_np = 0;
    if (!end_kind) live = exit_live(e, next);
    else {
        DI *b = is_branch(di[n - 1].kind) || n < 2 ? &di[n - 1] : &di[n - 2];
        if (b->kind == K_JR) live = exit_live(e, b->target) | exit_live(e, next);
        else if (b->kind == K_JP || b->kind == K_CALL) live = exit_live(e, b->target);
        else live = 0xF;
    }
    for (int i = n - 1; i >= 0; i--) {
        int exit_after = side[i] ? !di[i].dly : (i > 0 && side[i - 1] && di[i - 1].dly);
        if (exit_after) live |= exit_live(e, side[i] ? di[i].target : di[i - 1].target);
        di[i].live = live;
        live = (uint8_t)((live & ~di[i].fdef) | di[i].fuse);
    }
    /* Code */
    /* new block in a fresh 64-byte line: otherwise T25Emu discards
     * everything when a line with already executed code is written to */
    J_pos += (0u - HERE()) & 63u;                  /* only advance, write nothing */
    if (J_pos >= J_size) { J_full = 1; return 0; }
#ifdef JIT_STAGE
    stage_begin(J_pos);
#endif
    uint32_t start = HERE(), nslot0 = J_nslot;
    E_lim = 90;
again:
    J_e = e;
    rc_reset();
    /* Entry: cycles used up (left < 0) -> return. The bail-out is placed
     * later in dead code (after an exit), so the normal path runs through
     * without a taken branch. The cost of the whole block is deducted once,
     * and a side exit refunds the rest. */
    dpr(ORR, 0, R3, PCR, R10, ASR, 31);            /* r3 = left < 0 ? -1 : 0 */
    E_cb = HERE(); E16(0);                         /* cbnz r3 -> bail out */
    E_pc = pc;
    uint32_t total = 0, done = 0;
    for (int j = 0; j < n; j++) total += di[j].len;
    opi(SUB, 0, R10, R10, total);
    int i = 0;
    while (i < n) {
        /* Section i..k-1: up to and including the conditional branch (and delay slot) */
        int k = i;
        while (k < n && !side[k] && !(end_kind && is_branch(di[k].kind))) k++;
        int last;                                  /* last instruction of the section */
        if (k < n) last = di[k].dly ? k + 1 : k; else last = n - 1;
        for (int j = i; j <= last; j++) done += di[j].len;
        if (k >= n) {                              /* end without branch */
            for (int j = i; j < n; j++) gen_insn(e, &di[j]);
            rc_flush();
            exit_direct(next);
            break;
        }
        for (int j = i; j < k; j++) gen_insn(e, &di[j]);
        DI *b = &di[k], *sl = b->dly ? &di[k + 1] : 0;
        uint32_t ret = sl ? sl->next : b->next;
        rc_next();
        if (side[k]) {
            int c = cond_arm[b->cond];
            uint32_t skip;
            int t = -1;
            if (sl && sl->fdef) {
                t = tmp_get_low();
                ITE(c); dpi(ORR, 0, t, PCR, 1); dpi(ORR, 0, t, PCR, 0);   /* t = taken */
                gen_insn(e, sl);
                skip = HERE(); E16(0);                                  /* cbz t -> continue */
            } else {
                if (sl) { sl->live |= b->fuse; gen_insn(e, sl); rc_next(); }   /* delay slot keeps flags; the branch reads them afterwards */
                skip = HERE(); E16(0);                                  /* b<!c> -> continue */
            }
            rc_store_dirty();
            if (total > done) opi(ADD, 0, R10, R10, total - done);   /* refund the part not executed */
            if (k == idle_at) { bl_to(J_idlex); E16(b->target & 0xFFFFu); E16(b->target >> 16); }
            else exit_direct(b->target);
            entry_stub_place();
            if (t >= 0) { patch_cbz(skip, t, 0, HERE()); tmp_put(t); }
            else {
                uint32_t off = HERE() - (skip + 4);
                if (off > 254u) { J_full = 1; } else P16(skip, 0xD000u | ((uint32_t)(c ^ 1) << 8) | (off >> 1));
            }
            i = sl ? k + 2 : k + 1;
            if (i >= n) { rc_flush(); exit_direct(next); break; }
            continue;
        }
        switch (b->kind) {
        case K_JR: {
            int c = cond_arm[b->cond];
            if (sl && !sl->fdef) { sl->live |= b->fuse; gen_insn(e, sl); rc_next(); sl = 0; }
            if (sl) {
                int t = tmp_get_low();
                ITE(c); dpi(ORR, 0, t, PCR, 1); dpi(ORR, 0, t, PCR, 0);   /* t = taken */
                gen_insn(e, sl);
                rc_flush();
                E16(0xB900u | (6u << 3) | (uint32_t)t);   /* cbnz t, +12 -> taken (past exit_direct, 14 bytes) */
                tmp_put(t);
            } else {
                rc_flush();
                E16(0xD000u | ((uint32_t)c << 8) | 6u);   /* b<c> +12 -> taken */
            }
            exit_direct(ret);
            if (k == idle_at) { bl_to(J_idlex); E16(b->target & 0xFFFFu); E16(b->target >> 16); }
            else exit_direct(b->target);
            break;
        }
        case K_JP:
            if (sl) gen_insn(e, sl);
            rc_flush();
            exit_direct(b->target);
            break;
        case K_CALL:
            if (sl) gen_insn(e, sl);
            rc_next();
            mov32(R1, ret); push_r1();
            rc_flush();
            exit_direct(b->target);
            break;
        case K_JPR: case K_JPREG: case K_CALLREG: {
            int t = tmp_get();
            int r = RR(b->rd);
            if (b->kind == K_JPR) opi(ADD, 0, t, r, b->target); else movr(t, r);
            if (sl) gen_insn(e, sl);
            rc_next();
            if (b->kind == K_CALLREG) { mov32(R1, ret); push_r1(); }
            rc_flush();
            exit_reg(t);
            tmp_put(t);
            break;
        }
        case K_RET: {
            if (sl) gen_insn(e, sl);
            rc_next();
            int sp = RRW(G_SP);
            movr(R0, sp); dpi(ADD, 0, sp, sp, 4);
            mem_rd(4, 0);
            rc_flush();
            exit_reg(R1);
            break;
        }
        }
        break;
    }
    entry_stub_place();
    if (E_cb && !J_full && E_lim) {                /* bail-out too far away: redo with a tighter limit */
        E_lim = E_lim > 40u ? 40u : 0u;
        J_pos = start - J_tgt; J_nslot = nslot0; E_cb = 0;
        goto again;
    }
    if (E_cb) J_full = 1;
    if (J_full) return 0;
#ifdef JIT_STAGE
    stage_commit(start - J_tgt, J_pos);
#endif
    if (ram) for (uint32_t p = pc >> 8; p <= ((next - 1) >> 8) && p < 128; p++) e->jit_map[p] = 1;
    for (int q = 0; q < xl_np; q++) if (xl_pg[q] < 128) e->jit_map[xl_pg[q]] = 1;
    jit_sync(start, HERE());
    jit_count++; jit_st_blocks++; jit_st_insns += (uint32_t)n; jit_st_bytes += HERE() - start;
    return start;
}

static uint32_t get_block(Emu *e, uint32_t pc)
{
    uint32_t c = lookup(pc);
    if (c) return c;
    c = translate(e, pc);
    if (!c) { jit_flush_all(e); c = translate(e, pc); if (!c) return JIT_INTERP; }
    if (!insert(pc, c)) { jit_flush_all(e); c = translate(e, pc); if (!c) return JIT_INTERP; insert(pc, c); }
    return c;
}

#ifdef JIT_STAGE
/* Copy loop as fixed instructions in SRAM: r0 = destination (PRAM), r1 = source, r2 = length (even). */
static uint16_t J_cpcode[12] __attribute__((aligned(4)));
void jit_stage_setup(uint8_t *stage, uint32_t size)
{
    static const uint16_t c[12] = { 0xF831, 0x3B02,   /* ldrh.w r3, [r1], #2 */
                                    0xF820, 0x3B02,   /* strh.w r3, [r0], #2 */
                                    0x3A02,           /* subs r2, #2 */
                                    0xD1F9,           /* bne start */
                                    0xF3BF, 0x8F4F,   /* dsb */
                                    0xF3BF, 0x8F6F,   /* isb */
                                    0x4770, 0xBF00 }; /* bx lr */
    for (int i = 0; i < 12; i++) J_cpcode[i] = c[i];
    __asm__ volatile("dsb\n isb" ::: "memory");
    J_copy = (void (*)(uint32_t, const uint8_t *, uint32_t))((uint32_t)J_cpcode | 1u);
    J_stage = stage; J_stage_sz = size;
}
#endif
/* ---------- Platform-independent setup ---------- */
TC_INIT_LAST void jit_init(Emu *e, uint8_t *buf, uint32_t tgt, uint32_t size)
{
    J_buf = buf; J_tgt = tgt; J_size = size;
#ifdef JIT_WHOT
    memset(jit_wcnt, 0, sizeof jit_wcnt); memset(jit_nojit, 0, sizeof jit_nojit); jit_wwin = jit_nojit_at = 0;
#endif
#ifdef JIT_STAGE
    stage_begin(0);
    emit_fixed(e);
    stage_commit(0, J_pos);
#else
    emit_fixed(e);
#endif
    jit_flush_all(e);
    jit_sync(J_tgt, J_tgt + J_fixed);
#ifndef TC_HOST
    e->jit_a0 = (uint32_t)e->a0ram;
    e->jit_h[H_RD8] = (uint32_t)jh_rd8; e->jit_h[H_RD16] = (uint32_t)jh_rd16; e->jit_h[H_RD32] = (uint32_t)jh_rd32;
    e->jit_h[H_WR8] = (uint32_t)jh_wr8; e->jit_h[H_WR16] = (uint32_t)jh_wr16; e->jit_h[H_WR32] = (uint32_t)jh_wr32;
    e->jit_h[H_PUSHN] = (uint32_t)jh_pushn; e->jit_h[H_POPN] = (uint32_t)jh_popn; e->jit_h[H_LCD] = (uint32_t)jh_lcd8;
#endif
    e->jit_parb = e->par_base;
    for (uint32_t o = 0; o < e->dev.rom_size && o < 0x800000u; o += 0x2000u) jit_rom_update(e, o);
}
uint32_t (*jit_tgt_of)(const uint8_t *p);
static uint32_t jit_tgt(const uint8_t *p) { return !p ? 0 : jit_tgt_of ? jit_tgt_of(p) : (uint32_t)(uintptr_t)p; }
/* Read pointer for ROM offset off: outside the parameter block, the fast
 * block (64 KB); inside it, the overlay or the original per 8 KB. 0 = helper. */
__attribute__((noinline)) void jit_rom_update(Emu *e, uint32_t off)
{
    if (off >= 0x800000u) return;
    uint32_t b = off & ~0x1FFFu;
    if (b - e->par_base < 0x10000u) {
        const uint8_t *p = 0;
        for (int k = 0; k < e->ovl.n; k++) if (e->ovl.base[k] == b) p = e->ovl.mem[k];
        if (!p && e->rom_src[b >> 16]) p = e->rom_src[b >> 16] + (b & 0xFFFFu);
        e->jit_par[(b - e->par_base) >> 13] = jit_tgt(p);
        e->jit_rom[b >> 16] = 0;
    } else e->jit_rom[b >> 16] = jit_tgt(e->rom_blk[b >> 16]);
#ifdef ROMSTAT
    e->jit_rom[b >> 16] = 0;              /* stats build: all ROM reads go through the helpers (countable) */
#endif
}
uint32_t jit_entry_addr(void) { return J_entry; }
uint32_t jit_used(void) { return J_pos; }
uint32_t jit_lookup_dbg(uint32_t pc) { return lookup(pc); }
#ifndef TC_HOST
#ifdef JIT_GUARD
/* Guard around entry (device debugging v0.8.8): own frame with r3..r11/lr,
 * records sp before and after the block. If the block returns with a wrong sp
 * (without the exit's pop), sp is restored and jit_dbg[4] is set so the
 * caller's registers stay intact and the display can report it.
 * jit_dbg: 0 sp before, 1 sp after, 2 code, 3 return value, 4 error (1 sp, 2 r10/r11),
 * 5 counter (entries), 6/7 r10/r11 before, 8..23 ring of the last 8 entries
 * (guest pc, code), 24 r10 after on error 2, 28..35 r4..r11 before, 36..43 r4..r11 after. */
volatile uint32_t jit_dbg[44];
void (*jit_bad_hook)(Emu *e);
__attribute__((naked, noinline)) static uint32_t jit_tramp(Emu *e, uint32_t code, uint32_t entry)
{
    __asm__ volatile(
        "push {r3-r11, lr}\n"
        "movw r3, #:lower16:jit_dbg\n movt r3, #:upper16:jit_dbg\n"
        "mov r12, sp\n str r12, [r3, #0]\n"
        "str r10, [r3, #24]\n str r11, [r3, #28]\n"
        "add r12, r3, #112\n stmia r12, {r4-r11}\n"
        "blx r2\n"
        "movw r3, #:lower16:jit_dbg\n movt r3, #:upper16:jit_dbg\n"
        "add r12, r3, #144\n stmia r12, {r4-r11}\n"
        "mov r12, sp\n str r12, [r3, #4]\n str r0, [r3, #12]\n"
        "ldr r2, [r3, #0]\n cmp r2, r12\n beq 1f\n"
        "mov sp, r2\n movs r1, #1\n str r1, [r3, #16]\n b 3f\n"
        "1: ldr r2, [r3, #24]\n cmp r2, r10\n bne 2f\n"
        "ldr r2, [r3, #28]\n cmp r2, r11\n beq 3f\n"
        "2: str r10, [r3, #96]\n movs r1, #2\n str r1, [r3, #16]\n"
        "3: pop {r3-r11, pc}\n");
}
uint32_t jit_enter(Emu *e, uint32_t code)
{
    uint32_t k = jit_dbg[5]++ & 7u;
    jit_dbg[8 + 2 * k] = e->pc; jit_dbg[9 + 2 * k] = code; jit_dbg[2] = code;
    uint32_t r = jit_tramp(e, code, J_entry | 1u);
    if (jit_dbg[4] && jit_bad_hook) jit_bad_hook(e);
    return r;
}
#else
uint32_t jit_enter(Emu *e, uint32_t code) { return ((uint32_t (*)(Emu *, uint32_t))(J_entry | 1u))(e, code); }
#endif
void jit_sync(uint32_t lo, uint32_t hi) { (void)lo; (void)hi; __asm__ volatile("dsb\n isb" ::: "memory"); }
#endif

#pragma clang attribute pop
static inline uint32_t pack_fl(Emu *e)
{
    return ((uint32_t)e->f_n << 31) | ((uint32_t)e->f_z << 30) | ((uint32_t)(!e->f_c) << 29) | ((uint32_t)e->f_v << 28);
}
static inline void unpack_fl(Emu *e)
{
    uint32_t f = e->nzcv;
    e->f_n = (uint8_t)(f >> 31); e->f_z = (uint8_t)((f >> 30) & 1); e->f_c = (uint8_t)(!((f >> 29) & 1)); e->f_v = (uint8_t)((f >> 28) & 1);
}

#ifdef JIT_TEST
/* Test mode: run only one block without chaining */
int jit_test_single = 0;
uint32_t jit_test_cost;
void (*jit_test_hook)(Emu *e, uint32_t pc);
#endif

/* Like tc_exec: until cycles >= ziel (target), halt or stop. */
void jit_exec(Emu *e, uint64_t ziel)
{
    int32_t left;
#define JCYC() (e->evt - (uint64_t)(int64_t)left)
#define JNEXT(c) do { uint64_t _n = (e->last_tick + JIT_TICK < ziel) ? e->last_tick + JIT_TICK : ziel; e->evt = _n; left = (int32_t)(_n - (c)); } while (0)
    JNEXT(e->cycles);
    if (e->halted || e->stopped) return;
    for (;;) {
        if (left <= 0) {
            uint64_t cyc = JCYC();
            if (cyc - e->last_tick >= JIT_TICK) {
                e->cycles = cyc;
                periph_tick(e, (uint32_t)(cyc - e->last_tick));
                e->last_tick = cyc;
            }
            if (cyc >= ziel) { e->cycles = cyc; break; }
            JNEXT(cyc);
        }
        if (e->jit_flush) jit_flush_all(e);
        if (e->pending_irq && e->f_ie && e->pending_level > e->il) {
            uint32_t vec = e->pending_irq;
            uint32_t psr = (e->psr_rest & ~0xF1Fu) | e->f_n | ((uint32_t)e->f_z << 1) | ((uint32_t)e->f_v << 2) | ((uint32_t)e->f_c << 3) | ((uint32_t)e->f_ie << 4) | ((uint32_t)(e->il & 0xF) << 8);
            e->sp -= 4; jh_wr32(e, e->sp, e->pc);
            e->sp -= 4; jh_wr32(e, e->sp, psr);
            e->jit_bias = 0;
            e->f_ie = 0;
            e->il = e->pending_level;
            e->pc = f_rd32(e, e->ttbr + vec * 4);
            e->pending_irq = 0;
            jit_prev_pc = 0xFFFFFFF0u;
            if (e->jit_flush) jit_flush_all(e);
        }
        uint32_t pc = e->pc & 0x0FFFFFFFu;
        uint32_t code = lookup(pc);
        if (!code) {
            /* Translate only once the block runs often: rare code stays in the
             * interpreter and does not evict hot blocks from the buffer. */
            uint8_t *cn = &jit_cnt[((pc >> 1) ^ (pc >> 11)) & (JIT_CNT - 1)];
            if (pc > jit_prev_pc && pc - jit_prev_pc <= 6u) code = JIT_INTERP;   /* in the middle of a sequence: keep interpreting */
#ifdef JIT_WHOT
            else if (pc < A0RAM_MAX && jit_nojit_pc(e, pc)) code = JIT_INTERP;   /* code there keeps being overwritten */
#endif
            else if (*cn < jit_hot) { (*cn)++; code = JIT_INTERP; }
            else code = get_block(e, pc);
        }
        if (code == JIT_INTERP) {
            e->cycles = JCYC(); e->pc = pc;
            jit_prev_pc = pc;
            tc_exec(e, e->cycles + jit_interp_run);
            left = (int32_t)(e->evt - e->cycles);
            if (e->halted || e->stopped) break;
            if (e->cycles >= ziel) break;
            JNEXT(e->cycles);
#ifdef JIT_TEST
            if (jit_test_single) { jit_test_cost = 0; break; }
#endif
            continue;
        }
#ifdef JIT_TEST
        if (jit_test_hook) { e->cycles = JCYC(); e->pc = pc; jit_test_hook(e, pc); }
#endif
#ifdef JIT_TEST
        if (!jit_test_single)
#endif
        { uint32_t k = (pc >> 1) & 255u; e->jit_itab[2 * k] = pc; e->jit_itab[2 * k + 1] = code | 1u; }
        e->nzcv = pack_fl(e);
        e->jit_left = left; e->jit_bias = 0;
        uint32_t r = jit_enter(e, code | 1u);
        jit_prev_pc = 0xFFFFFFF0u;
        left = e->jit_left + (int32_t)e->jit_bias; e->jit_bias = 0;
        unpack_fl(e);
#ifdef JIT_TEST
        if (jit_test_single) { e->cycles = JCYC(); jit_test_cost = 1; break; }
#endif
        if (r == 1 && jit_idle_on) {
            /* Idle loop: fast-forward to the next peripheral event */
            uint64_t cyc = JCYC(), t = e->last_tick + periph_next(e);
            if (t > ziel) t = ziel;
            if (t > cyc) { left = (int32_t)(e->evt - t); jit_st_idle++; }
            continue;
        }
        if (r > 1 && jit_enabled != 2) {
            /* Chaining: point the exit's slot (in the ldr before it) at the target block */
            uint32_t t = e->pc & 0x0FFFFFFFu;
            uint32_t c = lookup(t);           /* only chain targets already translated */
            uint32_t o = r - 4 - J_tgt;           /* second half of the ldr.w */
            if (c && c != JIT_INTERP && o < J_size) {
#ifdef JIT_STAGE
                const volatile uint8_t *cb = (const volatile uint8_t *)(uintptr_t)(J_tgt + o);   /* finished code in PRAM */
                uint32_t imm = (uint32_t)(cb[0] | (cb[1] << 8)) & 0xFFFu;
#else
                uint32_t imm = (uint32_t)(J_buf[o] | (J_buf[o + 1] << 8)) & 0xFFFu;
#endif
                uint32_t sl = (imm - OFF(jit_slot)) / 4u;
                if (sl < 600u) { e->jit_slot[sl] = c | 1u; jit_st_patch++; }
            }
        }
    }
    e->cycles = JCYC();
#undef JCYC
#undef JNEXT
}
