/* ArduboyEmu AB_JIT: the AVR program translated block by block into Thumb-2 for the Paradise's Cortex-M3 (code in
 * the SRAM: a cache, emptied when full). Included at the end of avr.c (uses its I/O, timers and interrupts).
 *
 * Registers of the translated code: r4 = the data space D (the context lies just before it), r5 = cycles left until
 * the next event, r6 = fz (0 exactly when Z), r7 = fc (C, 0/1), r8 = fn (bit 7 N), r9 = fv (bit 7 V), r10 = fh
 * (bit 4 H), r11 = I and T (bits 7, 6). r0..r3, r12, lr are free. A block ends at a jump, a branch, a skip, a call,
 * a return or after JB_MAX instructions; it subtracts its cycles and goes to the dispatcher with the next pc in r0.
 * The dispatcher leaves for C when no cycles are left. Flags are worked out only where something later reads them
 * (in the block, or at its end: C, Z, N, V; H only for an instruction that reads it in the same block).
 * Rare instructions (I/O, SREG, sleep ...) run in C (jit_op) with the flags handed over in the context.
 * Author: mameise. License: GPL-3.0-or-later. */

#ifndef T_F
#define T_F 64u
#endif
/* ---- the context before D: g_mem.jit[i] lies at D - 4 * (64 - i) ---- */
enum { J_XLATE = 1, J_OP, J_LD, J_ST, J_MAP, J_CODE1, J_FLASH, J_PC, J_DIRTY, J_IN, J_OUT, J_BIT, J_SPDR,
       J_LEFT = 56, J_FZ, J_FC, J_FN, J_FV, J_FH, J_SR, J_SPARE };
#define JOFF(i) (4u * (64u - (i)))   /* offset below D */
#define JC (g_mem.jit)

#ifndef JIT_CACHE
#define JIT_CACHE 0x11C00u           /* bytes of translated code (71 KB; its last 9 KB are also ab_scratch) */
#endif
#define JB_MAX 40u
static u16 g_map[AVR_FLASH_WORDS];   /* pc -> code (in halfwords from g_code, 0: not translated) */
static u16 g_code[JIT_CACHE / 2] __attribute__((aligned(4)));
static u16 *g_ep, *g_cend;           /* emit pointer, end of the cache */
static u32 g_stub_enter, g_stub_disp, g_stub_exit;   /* halfword offsets in g_code */
static u32 g_hold, g_flushes;
#define PEND_N 512u
static u16 g_pend_at[PEND_N], g_pend_pc[PEND_N]; static u32 g_npend, g_blk_pc, g_blk_at;   /* AB_CHAIN */

/* ---------------- Thumb-2 encoder ---------------- */
static void H1(u32 x) { if (g_ep < g_cend) *g_ep = (u16)x; g_ep++; }
static void H2(u32 a, u32 b) { H1(a); H1(b); }
#define LDRB_I 0xF890u
#define STRB_I 0xF880u
#define LDRH_I 0xF8B0u
#define STRH_I 0xF8A0u
#define LDR_I 0xF8D0u
#define STR_I 0xF8C0u
static void mem_i(u32 op, u32 rt, u32 rn, u32 imm)
{
  if (rt < 8u && rn < 8u) {   /* the 16-bit forms (half the code) */
    if ((op == LDRB_I || op == STRB_I) && imm < 32u) { H1((op == LDRB_I ? 0x7800u : 0x7000u) | (imm << 6) | (rn << 3) | rt); return; }
    if ((op == LDRH_I || op == STRH_I) && imm < 64u && !(imm & 1u)) { H1((op == LDRH_I ? 0x8800u : 0x8000u) | ((imm >> 1) << 6) | (rn << 3) | rt); return; }
  }
  H2(op | rn, (rt << 12) | (imm & 0xFFFu));
}
static void ldr_ctx(u32 rt, u32 i) { H2(0xF850u | 4u, (rt << 12) | 0xC00u | JOFF(i)); }   /* ldr rt, [r4, #-off] */
static void str_ctx(u32 rt, u32 i) { H2(0xF840u | 4u, (rt << 12) | 0xC00u | JOFF(i)); }
#define LDRB_R 0xF810u
#define STRB_R 0xF800u
#define LDRH_R 0xF830u
static void mem_r(u32 op, u32 rt, u32 rn, u32 rm, u32 sh) { H2(op | rn, (rt << 12) | (sh << 4) | rm); }
static void dp_r_w(u32 op, u32 s, u32 rd, u32 rn, u32 rm, u32 ty, u32 sh);
#define DP_AND 0xEA00u
#define DP_BIC 0xEA20u
#define DP_ORR 0xEA40u
#define DP_EOR 0xEA80u
#define DP_ADD 0xEB00u
#define DP_ADC 0xEB40u
#define DP_SUB 0xEBA0u
#define SH_LSL 0u
#define SH_LSR 1u
#define SH_ASR 2u
static void dp_r(u32 op, u32 s, u32 rd, u32 rn, u32 rm, u32 ty, u32 sh)
{
  /* 16-bit forms where the flags they set do not matter (no flag test follows them) */
  if (!s && !sh && rd < 8u && rn < 8u && rm < 8u) {
    if (op == DP_ADD) { H1(0x1800u | (rm << 6) | (rn << 3) | rd); return; }
    if (op == DP_SUB) { H1(0x1A00u | (rm << 6) | (rn << 3) | rd); return; }
    if (rd == rn && (op == DP_AND || op == DP_EOR || op == DP_ORR || op == DP_BIC)) { H1(0x4000u | ((op == DP_AND ? 0u : op == DP_EOR ? 1u : op == DP_ORR ? 12u : 14u) << 6) | (rm << 3) | rd); return; }
  }
  dp_r_w(op, s, rd, rn, rm, ty, sh);
}
static void dp_r_w(u32 op, u32 s, u32 rd, u32 rn, u32 rm, u32 ty, u32 sh) { H2(op | (s << 4) | rn, ((sh >> 2) << 12) | (rd << 8) | ((sh & 3u) << 6) | (ty << 4) | rm); }
static void mov_r(u32 rd, u32 rm) { H1(0x4600u | ((rd & 8u) << 4) | (rm << 3) | (rd & 7u)); }
static void movs_sh(u32 rd, u32 rm, u32 ty, u32 sh) { dp_r(DP_ORR, 1, rd, 15, rm, ty, sh); }
static void mov_sh(u32 rd, u32 rm, u32 ty, u32 sh) { dp_r(DP_ORR, 0, rd, 15, rm, ty, sh); }
#define DI_AND 0xF000u
#define DI_ORR 0xF040u
#define DI_EOR 0xF080u
#define DI_ADD 0xF100u
#define DI_SUB 0xF1A0u
static void dp_i(u32 op, u32 s, u32 rd, u32 rn, u32 imm8) { H2(op | (s << 4) | rn, (rd << 8) | (imm8 & 255u)); }
static void tst_i(u32 rn, u32 imm8) { dp_i(DI_AND, 1, 15, rn, imm8); }
static void cmp_i(u32 rn, u32 imm8) { dp_i(DI_SUB, 1, 15, rn, imm8); }
static void movw(u32 rd, u32 v) { if (rd < 8u && v < 256u) { H1(0x2000u | (rd << 8) | v); return; } H2(0xF240u | (((v >> 11) & 1u) << 10) | ((v >> 12) & 15u), (((v >> 8) & 7u) << 12) | (rd << 8) | (v & 255u)); }
static void movt(u32 rd, u32 v) { H2(0xF2C0u | (((v >> 11) & 1u) << 10) | ((v >> 12) & 15u), (((v >> 8) & 7u) << 12) | (rd << 8) | (v & 255u)); }
static void ubfx(u32 rd, u32 rn, u32 lsb, u32 w) { H2(0xF3C0u | rn, ((lsb >> 2) << 12) | (rd << 8) | ((lsb & 3u) << 6) | (w - 1u)); }
/* usat rd, #12, rn: addresses beyond the 4 KB land on its last byte (not on the registers) */
static void sat12(u32 rd, u32 rn) { H2(0xF380u | rn, (rd << 8) | 12u); }
static void uxtb(u32 rd, u32 rm) { if (rd < 8u && rm < 8u) { H1(0xB2C0u | (rm << 3) | rd); return; } H2(0xFA5Fu, 0xF080u | (rd << 8) | rm); }
static void sxtb(u32 rd, u32 rm) { H2(0xFA4Fu, 0xF080u | (rd << 8) | rm); }
static void mul(u32 rd, u32 rn, u32 rm) { H2(0xFB00u | rn, 0xF000u | (rd << 8) | rm); }
static void subw(u32 rd, u32 rn, u32 imm) { H2(0xF2A0u | rn | (((imm >> 11) & 1u) << 10), (((imm >> 8) & 7u) << 12) | (rd << 8) | (imm & 255u)); }
static void addw(u32 rd, u32 rn, u32 imm) { H2(0xF200u | rn | (((imm >> 11) & 1u) << 10), (((imm >> 8) & 7u) << 12) | (rd << 8) | (imm & 255u)); }
static void subs5(u32 n) { while (n > 255u) { H1(0x3D00u | 255u); n -= 255u; } if (n) H1(0x3D00u | n); }   /* subs r5, #n */
static void blx(u32 rm) { H1(0x4780u | (rm << 3)); }
static void bx(u32 rm) { H1(0x4700u | (rm << 3)); }
/* branches: to a halfword index in g_code */
static void bcc_w(u32 cc, u32 to)   /* b<cc>.w, +-1 MB */
{
  s32 off = (s32)(to - (u32)(g_ep - g_code) - 2u) * 2;
  u32 s = (off >> 20) & 1u, j2 = (off >> 19) & 1u, j1 = (off >> 18) & 1u;
  H2(0xF000u | (s << 10) | (cc << 6) | ((off >> 12) & 0x3Fu), 0x8000u | (j1 << 13) | (j2 << 11) | ((off >> 1) & 0x7FFu));
}
static void b_w(u32 to)
{
  s32 off = (s32)(to - (u32)(g_ep - g_code) - 2u) * 2;
  u32 s = (off >> 24) & 1u, i1 = (off >> 23) & 1u, i2 = (off >> 22) & 1u, j1 = (~(i1 ^ s)) & 1u, j2 = (~(i2 ^ s)) & 1u;
  H2(0xF000u | (s << 10) | ((off >> 12) & 0x3FFu), 0x9000u | (j1 << 13) | (j2 << 11) | ((off >> 1) & 0x7FFu));
}
#define CC_EQ 0u
#define CC_NE 1u
#define CC_CS 2u
#define CC_CC 3u
#define CC_LE 13u
/* a forward conditional branch (16 bit) filled in later */
static u16 *bcc_fwd(u32 cc) { u16 *p = g_ep; H1(0xD000u | (cc << 8)); return p; }
static void fix_fwd(u16 *p) { if (p >= g_cend) return; s32 off = (s32)((g_ep - p) - 2); *p |= (u16)(off & 255); }
static u16 *b_fwd(void) { u16 *p = g_ep; H1(0xE000u); return p; }
static void fix_b(u16 *p) { if (p >= g_cend) return; s32 off = (s32)((g_ep - p) - 2); *p |= (u16)(off & 0x7FFu); }
#define R_D 4u
#define R_LEFT 5u
#define R_FZ 6u
#define R_FC 7u
#define R_FN 8u
#define R_FV 9u
#define R_FH 10u
#define R_SR 11u

/* ---------------- the stubs at the start of the cache ---------------- */
static void stubs(void)
{
  g_ep = g_code + 2;   /* offset 0 means "not translated" */
  /* enter: push {r3-r11, lr}; r4 = D; load r5..r11; r0 = pc */
  g_stub_enter = (u32)(g_ep - g_code);
  H2(0xE92Du, 0x4FF8u);
  movw(R_D, (u32)(uintptr_t)A_D & 0xFFFFu); movt(R_D, (u32)(uintptr_t)A_D >> 16);
  subw(12, R_D, JOFF(J_LEFT)); H2(0xE890u | 12u, 0x0FE0u);   /* ldm r12, {r5-r11} */
  ldr_ctx(0, J_PC);
  /* dispatch: r0 = pc */
  g_stub_disp = (u32)(g_ep - g_code);
  H1(0x2D00u);                                  /* cmp r5, #0 */
  u16 *pe = bcc_fwd(CC_LE);
  ldr_ctx(1, J_MAP); mem_r(LDRH_R, 2, 1, 0, 1); /* ldrh r2, [r1, r0, lsl #1] */
  H1(0x2A00u); u16 *px = bcc_fwd(CC_EQ);        /* cmp r2, #0; beq translate */
  ldr_ctx(1, J_CODE1); dp_r(DP_ADD, 0, 1, 1, 2, SH_LSL, 1); bx(1);
  fix_fwd(px);
  ldr_ctx(12, J_XLATE); blx(12);                /* r0 = pc again */
  b_w(g_stub_disp);
  fix_fwd(pe);
  /* exit: pc in r0 */
  g_stub_exit = (u32)(g_ep - g_code);
  str_ctx(0, J_PC);
  subw(12, R_D, JOFF(J_LEFT)); H2(0xE880u | 12u, 0x0FE0u);   /* stm r12, {r5-r11} */
  H2(0xE8BDu, 0x8FF8u);                                       /* pop {r3-r11, pc} */
}
static void jit_flush(void)
{
  for (u32 i = 0; i < AVR_FLASH_WORDS; i++) g_map[i] = 0;
  g_cend = g_code + JIT_CACHE / 2;
  g_npend = 0; g_blk_pc = 0xFFFFFFFFu;
  stubs();
  g_flushes++;
}

/* ---------------- what each instruction reads and sets of the flags ---------------- */
#define FC 1u
#define FZ 2u
#define FN 4u
#define FV 8u
#define FH 16u
#define FALL 31u
enum { K_HELP, K_ADD, K_ADC, K_SUB, K_SBC, K_CP, K_CPC, K_AND, K_OR, K_EOR, K_MOV, K_MOVW, K_LDI, K_SUBI, K_SBCI, K_CPI,
  K_ANDI, K_ORI, K_COM, K_NEG, K_SWAP, K_INC, K_DEC, K_ASR, K_LSR, K_ROR, K_ADIW, K_SBIW, K_MUL, K_LDS, K_STS, K_LDD,
  K_STD, K_LD, K_ST, K_LPM, K_PUSH, K_POP, K_BST, K_BLD, K_INSP, K_OUTSP, K_NOP, K_IN, K_OUT, K_BITIO, K_CLI,
  /* block ends */
  K_RJMP, K_JMP, K_RCALL, K_CALL, K_RET, K_IJMP, K_ICALL, K_BR, K_SBRC, K_SBRS, K_CPSE, K_SKIPIO, K_RETI, K_END };
typedef struct { u16 pc, w, w2, len; u8 k, a, b, c, use, def, live, cyc; } JIns;

static u32 two_w(u32 w) { return (w & 0xFE0Fu) == 0x9000u || (w & 0xFE0Fu) == 0x9200u || (w & 0xFE0Cu) == 0x940Cu; }
/* classify; a, b, c operands (see each kind) */
static void jdec(u32 pc, JIns *x)
{
  u32 w = A_FLASH[pc], w2 = pc + 1 < AVR_FLASH_WORDS ? A_FLASH[pc + 1] : 0;
  u32 d5 = (w >> 4) & 31u, r5 = (w & 15u) | ((w >> 5) & 16u), d4 = 16u + ((w >> 4) & 15u), K8 = ((w >> 4) & 0xF0u) | (w & 15u);
  x->pc = (u16)pc; x->w = (u16)w; x->w2 = (u16)w2; x->len = two_w(w) ? 2 : 1; x->k = K_HELP; x->a = x->b = x->c = 0;
  x->use = FALL; x->def = FALL; x->cyc = 1;
#define S(k_, a_, b_, u_, d_, cy_) do { x->k = (k_); x->a = (u8)(a_); x->b = (u8)(b_); x->use = (u_); x->def = (d_); x->cyc = (cy_); } while (0)
  switch (w >> 12) {
    case 0x0:
      if (w == 0) { S(K_NOP, 0, 0, 0, 0, 1); return; }
      switch ((w >> 10) & 3u) {
        case 0:
          if ((w & 0xFF00u) == 0x0100u) { S(K_MOVW, ((w >> 4) & 15u) * 2u, (w & 15u) * 2u, 0, 0, 1); return; }
          return;   /* MULS, MULSU, FMUL...: in C */
        case 1: S(K_CPC, d5, r5, FC | FZ, FALL, 1); return;
        case 2: S(K_SBC, d5, r5, FC | FZ, FALL, 1); return;
        default: S(K_ADD, d5, r5, 0, FALL, 1); return;
      }
    case 0x1:
      switch ((w >> 10) & 3u) {
        case 0: S(K_CPSE, d5, r5, 0, 0, 1); return;
        case 1: S(K_CP, d5, r5, 0, FALL, 1); return;
        case 2: S(K_SUB, d5, r5, 0, FALL, 1); return;
        default: S(K_ADC, d5, r5, FC, FALL, 1); return;
      }
    case 0x2:
      switch ((w >> 10) & 3u) {
        case 0: S(K_AND, d5, r5, 0, FZ | FN | FV, 1); return;
        case 1: S(K_EOR, d5, r5, 0, FZ | FN | FV, 1); return;
        case 2: S(K_OR, d5, r5, 0, FZ | FN | FV, 1); return;
        default: S(K_MOV, d5, r5, 0, 0, 1); return;
      }
    case 0x3: S(K_CPI, d4, K8, 0, FALL, 1); return;
    case 0x4: S(K_SBCI, d4, K8, FC | FZ, FALL, 1); return;
    case 0x5: S(K_SUBI, d4, K8, 0, FALL, 1); return;
    case 0x6: S(K_ORI, d4, K8, 0, FZ | FN | FV, 1); return;
    case 0x7: S(K_ANDI, d4, K8, 0, FZ | FN | FV, 1); return;
    case 0x8: case 0xA: {
      u32 q = (w & 7u) | ((w >> 7) & 0x18u) | ((w >> 8) & 0x20u), st = (w >> 9) & 1u, y = (w >> 3) & 1u;
      S(st ? K_STD : K_LDD, d5, q, st ? FC | FZ | FN | FV : 0, 0, 2); x->c = (u8)(y ? 28 : 30);   /* a store may leave for C */
      return;
    }
    case 0x9:
      switch ((w >> 9) & 7u) {
        case 0: case 1: {
          u32 st = (w >> 9) & 1u, m = w & 15u;
          if (m == 0) { if (!st && w2 >= 0x100u) S(K_LDS, d5, 0, 0, 0, 2); else if (st && w2 >= 0x100u) S(K_STS, d5, 0, 0, 0, 2); else if (w2 < 0x20u) S(st ? K_STS : K_LDS, d5, 0, 0, 0, 2); return; }
          if (!st && (m == 4 || m == 5 || m == 6 || m == 7)) { S(K_LPM, d5, (m & 1u), 0, 0, 3); return; }
          if (m == 15) { S(st ? K_PUSH : K_POP, d5, 0, 0, 0, 2); return; }
          /* LD / ST: c = pointer register, b: 0 plain, 1 post increment, 2 pre decrement */
          { u32 p = 0, mode = 0;
            switch (m) { case 1: p = 30; mode = 1; break; case 2: p = 30; mode = 2; break; case 9: p = 28; mode = 1; break; case 10: p = 28; mode = 2; break;
                         case 12: p = 26; break; case 13: p = 26; mode = 1; break; case 14: p = 26; mode = 2; break; default: return; }
            S(st ? K_ST : K_LD, d5, mode, st ? FC | FZ | FN | FV : 0, 0, 2); x->c = (u8)p; return; }
        }
        case 2:
          switch (w & 15u) {
            case 0: S(K_COM, d5, 0, 0, FC | FZ | FN | FV, 1); return;
            case 1: S(K_NEG, d5, 0, 0, FALL, 1); return;
            case 2: S(K_SWAP, d5, 0, 0, 0, 1); return;
            case 3: S(K_INC, d5, 0, 0, FZ | FN | FV, 1); return;
            case 5: S(K_ASR, d5, 0, 0, FC | FZ | FN | FV, 1); return;
            case 6: S(K_LSR, d5, 0, 0, FC | FZ | FN | FV, 1); return;
            case 7: S(K_ROR, d5, 0, FC, FC | FZ | FN | FV, 1); return;
            case 10: S(K_DEC, d5, 0, 0, FZ | FN | FV, 1); return;
            case 12: case 13: case 14: case 15: {
              u32 k = ((((w >> 4) & 31u) << 1 | (w & 1u)) << 16 | w2) & 0x3FFFu;
              x->k = (w & 2u) ? K_CALL : K_JMP; x->use = 0; x->def = 0; x->cyc = (w & 2u) ? 4 : 3; x->c = 0; x->w2 = (u16)k; return;
            }
            case 8:
              if (w == 0x9508u) { x->k = K_RET; x->use = 0; x->def = 0; x->cyc = 4; return; }
              if (w == 0x9518u) { x->k = K_RETI; x->use = 0; x->def = 0; x->cyc = 4; return; }
              if (w == 0x95C8u || w == 0x95D8u) { S(K_LPM, 0, 0, 0, 0, 3); return; }   /* LPM r0, Z */
              if (w == 0x94F8u) { S(K_CLI, 0, 0, 0, 0, 1); return; }
              if ((w & 0xFF0Fu) == 0x9408u && !(w & 0x80u) && ((w >> 4) & 7u) < 6u) return;   /* SEC ... : in C */
              return;
            case 9:
              if (w == 0x9409u || w == 0x9419u) { x->k = K_IJMP; x->use = 0; x->def = 0; x->cyc = 2; return; }
              if (w == 0x9509u || w == 0x9519u) { x->k = K_ICALL; x->use = 0; x->def = 0; x->cyc = 3; return; }
              return;
            default: return;
          }
        case 3: S((w & 0x0100u) ? K_SBIW : K_ADIW, 24u + ((w >> 3) & 6u), ((w >> 2) & 0x30u) | (w & 15u), 0, FC | FZ | FN | FV, 2); return;
        case 4: case 5:
          if (w & 0x0100u) { x->k = K_SKIPIO; x->cyc = 1; x->use = 0; x->def = 0; }   /* SBIC, SBIS: in C, then a skip */
          else { x->k = K_BITIO; x->cyc = 2; x->use = 0; x->def = 0; }              /* SBI, CBI */
          return;
        default: S(K_MUL, d5, r5, 0, FC | FZ, 2); return;
      }
    case 0xB: {
      u32 a = 0x20u + ((w & 15u) | ((w >> 5) & 0x30u));
      if (a == SPL || a == SPH) { S((w & 0x0800u) ? K_OUTSP : K_INSP, d5, a, 0, 0, 1); return; }
      if (a != SREG) { S((w & 0x0800u) ? K_OUT : K_IN, d5, a, 0, 0, 1); return; }   /* I/O without the flags */
      return;   /* SREG: in C */
    }
    case 0xC: { s32 k = (s32)(w << 20) >> 20; x->k = K_RJMP; x->use = 0; x->def = 0; x->cyc = 2; x->w2 = (u16)((pc + 1 + (u32)k) & 0x3FFFu); return; }
    case 0xD: { s32 k = (s32)(w << 20) >> 20; x->k = K_RCALL; x->use = 0; x->def = 0; x->cyc = 3; x->w2 = (u16)((pc + 1 + (u32)k) & 0x3FFFu); return; }
    case 0xE: S(K_LDI, d4, K8, 0, 0, 1); return;
    default:
      if ((w & 0x0800u) == 0) {   /* BRBS / BRBC: a = flag bit, b = 1 when "clear" */
        s32 k = (s32)(w << 22) >> 25; u32 b = w & 7u;
        static const u8 U[8] = { FC, FZ, FN, FV, FN | FV, FH, 0, 0 };
        x->k = K_BR; x->a = (u8)b; x->b = (u8)((w >> 10) & 1u); x->use = U[b]; x->def = 0; x->cyc = 1; x->w2 = (u16)((pc + 1 + (u32)k) & 0x3FFFu);
        return;
      }
      { u32 m = 1u << (w & 7u);
        switch ((w >> 9) & 3u) {
          case 0: S(K_BLD, d5, m, 0, 0, 1); return;
          case 1: S(K_BST, d5, m, 0, 0, 1); return;
          case 2: S(K_SBRC, d5, m, 0, 0, 1); return;
          default: S(K_SBRS, d5, m, 0, 0, 1); return;
        }
      }
  }
#undef S
}
static int ends_block(u32 k) { return k >= K_RJMP; }

/* ---------------- C helpers called from translated code ---------------- */
#define CTX_SR() ((JC[J_SR] & 0xC0u) | (JC[J_FC] & 1u) | (JC[J_FZ] ? 0u : 2u) | ((JC[J_FN] >> 5) & 4u) | ((JC[J_FV] >> 4) & 8u) | (((JC[J_FN] ^ JC[J_FV]) >> 3) & 16u) | ((JC[J_FH] << 1) & 32u))
static void ctx_srset(u32 x) { JC[J_SR] = x & 0xC0u; JC[J_FC] = x & 1u; JC[J_FZ] = (x & 2u) ? 0u : 1u; JC[J_FN] = (x & 4u) << 5; JC[J_FV] = (x & 8u) << 4; JC[J_FH] = (x & 32u) >> 1; }
static u32 g_jlim;   /* the event cycle of this run (A_CYC = g_jlim - left) */
#define JCYC() (g_jlim - JC[J_LEFT])
/* SREG written (I may come on): returns 1 when an interrupt is due now (leave for C, one instruction first) */
static u32 sr_write(u32 v)
{
  u32 on = v & ~JC[J_SR] & I_FLAG; ctx_srset(v);
  if (on && irq_now()) { g_hold = 1; return 1; }
  return 0;
}
static u32 j_rd(u32 a)
{
  if (a >= 0x100u) return a < AVR_DATA ? A_D[a] : 0u;
  if (a < 0x20u) return A_D[a];
  if (a == SREG) return CTX_SR();
  A_CYC = JCYC(); return io_rd(a);
}
static u32 j_wr(u32 a, u32 v)
{
  if (a >= 0x100u) { if (a < AVR_DATA) A_D[a] = (u8)v; JC[J_DIRTY] = 1; return 0; }
  if (a < 0x20u) { A_D[a] = (u8)v; return 0; }
  if (a == SREG) return sr_write(v);
  A_CYC = JCYC(); return io_wr(a, v) ? 1u : 0u;
}
/* loads and stores of the pointer instructions that hit I/O (the fast path is in the code) */
__attribute__((used)) static u32 jit_ld(u32 a) { return j_rd(a & 0xFFFFu); }
__attribute__((used)) static u32 jit_st(u32 a, u32 v) { return j_wr(a & 0xFFFFu, v & 255u); }
/* I/O without the flags (left is in the context for the time) */
__attribute__((used)) static u32 jit_in(u32 a) { A_CYC = JCYC(); return io_rd(a); }
__attribute__((used)) static u32 jit_out(u32 a, u32 v) { A_CYC = JCYC(); return io_wr(a, v) ? 1u : 0u; }
/* the display's byte (the most frequent I/O by far): as io_wr(SPDR) */
__attribute__((used)) static u32 jit_spdr(u32 v)
{
  JC[J_DIRTY] = 1; A_D[SPDR] = (u8)v;
  if (!(A_D[SPCR] & 0x40u)) return 0;
  if (A_D[PORTD] & 0x10u) lcd_data(v); else lcd_cmd(v);
  A_D[SPSR] |= 0x80u;
  return (A_D[SPCR] & 0x80u) != 0;
}
/* SBI, CBI, SBIC, SBIS (the word): bit 0 leave for C, bit 1 skip */
__attribute__((used)) static u32 jit_bit(u32 w)
{
  u32 a = 0x20u + ((w >> 3) & 31u), m = 1u << (w & 7u), pin = a >= PINB && a <= PORTF && (a - PINB) % 3u == 0u, fl = a >= TIFR0 && a <= TIFR4;
  A_CYC = JCYC();
  switch ((w >> 8) & 3u) {
    case 0: return (!pin && !fl && io_wr(a, io_rd(a) & ~m)) ? 1u : 0u;
    case 2: return io_wr(a, (pin || fl) ? m : io_rd(a) | m) ? 1u : 0u;
    case 1: return (io_rd(a) & m) ? 0u : 2u;
    default: return (io_rd(a) & m) ? 2u : 0u;
  }
}
/* one rare instruction at pc, flags in the context. Returns bit 0: leave for C after it, bit 1: the condition (skip) */
__attribute__((used)) static u32 jit_op(u32 pc)
{
  u32 w = A_FLASH[pc], w2 = A_FLASH[(pc + 1) & 0x3FFFu], r = 0;
  u32 d5 = (w >> 4) & 31u, r5 = (w & 15u) | ((w >> 5) & 16u);
  switch (w >> 12) {
    case 0x0: {   /* MULS, MULSU, FMUL, FMULS, FMULSU */
      u32 d = 16u + ((w >> 4) & 15u), rr = 16u + (w & 15u), res, f = 0;
      if ((w & 0xFF00u) == 0x0200u) res = (u32)((s32)(int8_t)A_D[d] * (s32)(int8_t)A_D[rr]);
      else {
        d = 16u + ((w >> 4) & 7u); rr = 16u + (w & 7u);
        switch (w & 0x88u) {
          case 0x00: res = (u32)((s32)(int8_t)A_D[d] * (s32)A_D[rr]); break;
          case 0x08: res = (u32)A_D[d] * A_D[rr]; f = 1; break;
          case 0x80: res = (u32)((s32)(int8_t)A_D[d] * (s32)(int8_t)A_D[rr]); f = 1; break;
          default: res = (u32)((s32)(int8_t)A_D[d] * (s32)A_D[rr]); f = 1; break;
        }
      }
      res &= 0xFFFFu; JC[J_FC] = res >> 15; if (f) res = (res << 1) & 0xFFFFu;
      JC[J_FZ] = res; A_D[0] = (u8)res; A_D[1] = (u8)(res >> 8);
      return 0;
    }
    case 0x9:
      if ((w & 0xFE0Fu) == 0x9000u) { A_D[d5] = (u8)j_rd(w2); return 0; }
      if ((w & 0xFE0Fu) == 0x9200u) return j_wr(w2, A_D[d5]);
      if ((w & 0xFF0Fu) == 0x9408u) {   /* BSET / BCLR */
        u32 m = 1u << ((w >> 4) & 7u), v = CTX_SR();
        return sr_write((w & 0x80u) ? v & ~m : v | m);
      }
      if (w == 0x9588u) { if (A_D[SMCR] & 1u) { g_sleep = 1; return 1; } return 0; }
      if (w == 0x95A8u) { if (WDT_ON) { A_CYC = JCYC(); wdt_restart(); } return 0; }
      if ((w & 0xFC00u) == 0x9800u) {   /* CBI, SBI, SBIC, SBIS */
        u32 a = 0x20u + ((w >> 3) & 31u), m = 1u << (w & 7u), pin = a >= PINB && a <= PORTF && (a - PINB) % 3u == 0u, fl = a >= TIFR0 && a <= TIFR4;
        A_CYC = JCYC();
        switch ((w >> 8) & 3u) {
          case 0: if (!pin && !fl) r = io_wr(a, io_rd(a) & ~m) ? 1u : 0u; return r;
          case 2: r = io_wr(a, (pin || fl) ? m : io_rd(a) | m) ? 1u : 0u; return r;
          case 1: return (io_rd(a) & m) ? 0u : 2u;
          default: return (io_rd(a) & m) ? 2u : 0u;
        }
      }
      if ((w & 0xFC00u) == 0x9400u && (w & 15u) == 1u) {   /* NEG (rarely needs all flags) */
        u32 a = A_D[d5], res = (0u - a); A_D[d5] = (u8)res;
        JC[J_FC] = (res >> 8) & 1u; JC[J_FZ] = res & 255u; JC[J_FN] = res; JC[J_FV] = a & res; JC[J_FH] = a ^ res; return 0;
      }
      return 0;
    case 0xB: {
      u32 a = 0x20u + ((w & 15u) | ((w >> 5) & 0x30u));
      if (w & 0x0800u) {
        u32 v = A_D[d5];
        if (a == SREG) return sr_write(v);
        A_CYC = JCYC(); return io_wr(a, v) ? 1u : 0u;
      }
      if (a == SREG) {   /* IN rd, SREG: also the anchor of the busy waiting (see avr.c) */
        A_D[d5] = (u8)CTX_SR();
        if (AB_SPIN) {
          u32 sp = A_D[SPL] | (u32)A_D[SPH] << 8;
          if (pc == g_anc_pc && sp == g_anc_sp) {
            if (!JC[J_DIRTY]) {
              u32 cyc = JCYC(), to = g_jlim; if (g_tread && to - cyc > 512u) to = cyc + 512u;
              if ((s32)(to - cyc) > 1) JC[J_LEFT] = g_jlim - to;
            }
            JC[J_DIRTY] = 0; g_tread = 0;
          } else if (JC[J_DIRTY]) { g_anc_pc = pc; g_anc_sp = sp; JC[J_DIRTY] = 0; g_tread = 0; }
        }
        return 0;
      }
      A_CYC = JCYC(); A_D[d5] = io_rd(a); return 0;
    }
    case 0xF:
      if ((w & 0xFE08u) == 0xFA00u) { JC[J_SR] = (JC[J_SR] & ~T_F) | ((A_D[d5] & (1u << (w & 7u))) ? T_F : 0u); return 0; }
      return 0;
  }
  (void)r5;
  return 0;
}

/* ---------------- the translator ---------------- */
static JIns g_jb[JB_MAX + 1];
/* emit: load AVR register d into host register rt */
static void ldr_avr(u32 rt, u32 d) { mem_i(LDRB_I, rt, R_D, d); }
static void str_avr(u32 rt, u32 d) { mem_i(STRB_I, rt, R_D, d); }
static u32 g_acc;   /* cycles of the block not yet taken from r5 */
static void flush_cyc(void) { subs5(g_acc); g_acc = 0; }
/* call a C helper with the flags handed over (r0 = arg) */
static void sync_out(void) { subw(12, R_D, JOFF(J_LEFT)); H2(0xE880u | 12u, 0x0FE0u); }
static void sync_in(void) { subw(12, R_D, JOFF(J_LEFT)); H2(0xE890u | 12u, 0x0FE0u); }
static void call_ctx(u32 h) { ldr_ctx(12, h); blx(12); }
/* leave the block towards pc: straight to its code when it is there and cycles are left (AB_CHAIN), else through
 * the dispatcher. A jump to a block not yet there is noted and set straight when that block is made. */
#define CC_GT 12u
static void exit_to(u32 pc, u32 cyc)
{
  if (cyc) subs5(cyc); else H1(0x2D00u);   /* the flags for the test */
  u32 at = pc == g_blk_pc ? g_blk_at : g_map[pc];
  if (at) { bcc_w(CC_GT, at); movw(0, pc); b_w(g_stub_disp); return; }
  if (g_npend < PEND_N) { g_pend_at[g_npend] = (u16)(g_ep - g_code); g_pend_pc[g_npend++] = (u16)pc; }
  movw(0, pc); b_w(g_stub_disp); H2(0xF3AFu, 0x8000u);   /* nop.w: room for the patch */
}
static void chain_fix(u32 pc, u32 at)
{
  u16 *keep = g_ep;
  for (u32 i = 0; i < g_npend; ) {
    if (g_pend_pc[i] != pc) { i++; continue; }
    g_ep = g_code + g_pend_at[i]; bcc_w(CC_GT, at); movw(0, pc); b_w(g_stub_disp);
    g_pend_at[i] = g_pend_at[g_npend - 1]; g_pend_pc[i] = g_pend_pc[--g_npend];
  }
  g_ep = keep;
}
static void exit_c(u32 pc) { movw(0, pc); b_w(g_stub_exit); }
/* flags from a result in r2 (operands in r0, r1), only those wanted */
static void fl_add(u32 want, int sticky)
{
  if (want & FC) ubfx(R_FC, 2, 8, 1);
  if (want & FZ) { if (sticky) { uxtb(3, 2); dp_r(DP_ORR, 0, R_FZ, R_FZ, 3, 0, 0); } else uxtb(R_FZ, 2); }
  if (want & FN) mov_r(R_FN, 2);
  if (want & FH) { dp_r(DP_EOR, 0, R_FH, 0, 1, 0, 0); dp_r(DP_EOR, 0, R_FH, R_FH, 2, 0, 0); }
}
static void fl_v_add(void) { dp_r(DP_EOR, 0, 3, 0, 2, 0, 0); dp_r(DP_EOR, 0, 12, 1, 2, 0, 0); dp_r(DP_AND, 0, R_FV, 3, 12, 0, 0); }
static void fl_v_sub(void) { dp_r(DP_EOR, 0, 3, 0, 1, 0, 0); dp_r(DP_EOR, 0, 12, 0, 2, 0, 0); dp_r(DP_AND, 0, R_FV, 3, 12, 0, 0); }
/* address of a data access in r0 (16 bit): r2 = the byte (load), or store r2; slow path for 0x20..0xFF */
static void data_ld(void)
{
  subw(1, 0, 0x20); cmp_i(1, 0xE0); u16 *slow = bcc_fwd(CC_CC);
  sat12(1, 0); mem_r(LDRB_R, 2, R_D, 1, 0); u16 *done = b_fwd();
  fix_fwd(slow);
  H1(0x9000u | (0u << 8) | 0); /* str r0, [sp] (keep the address) */
  sync_out(); call_ctx(J_LD); mov_r(2, 0); sync_in(); H1(0x9800u); /* ldr r0, [sp] */
  fix_b(done);
}
/* store r2 at the address in r0; status in r0 bit 0 (only on the slow path; fast path r0 = address >= 0x20) */
static u16 *data_st(void)
{
  subw(1, 0, 0x20); cmp_i(1, 0xE0); u16 *slow = bcc_fwd(CC_CC);
  sat12(1, 0); mem_r(STRB_R, 2, R_D, 1, 0);
  /* dirty when below the stack pointer (data, not stack) */
  mem_i(LDRH_I, 3, R_D, SPL); dp_r(DP_SUB, 1, 15, 0, 3, 0, 0); u16 *nd = bcc_fwd(CC_CS); str_ctx(R_D, J_DIRTY); fix_fwd(nd);
  u16 *done = b_fwd();
  fix_fwd(slow);
  H1(0x9000u); sync_out(); mov_r(1, 2); call_ctx(J_ST); sync_in();
  tst_i(0, 1); u16 *brk = bcc_fwd(CC_NE); H1(0x9800u);
  fix_b(done);
  return brk;   /* the caller lets it leave for C (after the instruction) */
}
/* pointer register p (26, 28, 30) into r0, with pre decrement / post increment written back after */
static void ptr_get(u32 p, u32 mode) { mem_i(LDRH_I, 0, R_D, p); if (mode == 2) { subw(0, 0, 1); H2(0xFA1Fu, 0xF080u | (0u << 8) | 0u); mem_i(STRH_I, 0, R_D, p); } }
static void ptr_post(u32 p, u32 mode) { if (mode == 1) { mem_i(LDRH_I, 3, R_D, p); addw(3, 3, 1); mem_i(STRH_I, 3, R_D, p); } }
/* push the return address (2 bytes) */
static void push_pc(u32 ret)
{
  mem_i(LDRH_I, 0, R_D, SPL); movw(2, ret);
  sat12(1, 0); mem_r(STRB_R, 2, R_D, 1, 0);
  subw(1, 0, 1); sat12(1, 1); mov_sh(2, 2, SH_LSR, 8); mem_r(STRB_R, 2, R_D, 1, 0);
  subw(0, 0, 2); mem_i(STRH_I, 0, R_D, SPL);
}
/* pop a return address into r0 */
static void pop_pc(void)
{
  mem_i(LDRH_I, 0, R_D, SPL);
  addw(1, 0, 1); sat12(1, 1); mem_r(LDRB_R, 2, R_D, 1, 0);
  addw(1, 0, 2); sat12(1, 1); mem_r(LDRB_R, 3, R_D, 1, 0);
  addw(0, 0, 2); mem_i(STRH_I, 0, R_D, SPL);
  dp_r(DP_ORR, 0, 0, 3, 2, SH_LSL, 8); ubfx(0, 0, 0, 14);
}
/* one block from pc; returns the code's halfword offset (0: no room) */
static u32 xlate(u32 pc)
{
  u32 n = 0;
  while (n < JB_MAX) {
    JIns *x = &g_jb[n]; jdec(pc, x); n++;
    if (ends_block(x->k)) break;
    pc = (pc + x->len) & 0x3FFFu;
    if (pc == 0) break;
  }
  /* which flags are read later: at the end C, Z, N, V (H only inside a block) */
  u32 live = FC | FZ | FN | FV;
  for (u32 i = n; i-- > 0; ) { g_jb[i].live = (u8)live; live = (live & ~g_jb[i].def) | g_jb[i].use; }
  u16 *start = g_ep; g_acc = 0; g_blk_pc = g_jb[0].pc; g_blk_at = (u32)(start - g_code);
  for (u32 i = 0; i < n; i++) {
    JIns *x = &g_jb[i]; u32 want = x->def & x->live, next = (x->pc + x->len) & 0x3FFFu;
    g_acc += x->cyc;
    switch (x->k) {
      case K_NOP: break;
      case K_LDI: movw(0, x->b); str_avr(0, x->a); break;
      case K_MOV: ldr_avr(0, x->b); str_avr(0, x->a); break;
      case K_MOVW: mem_i(LDRH_I, 0, R_D, x->b); mem_i(STRH_I, 0, R_D, x->a); break;
      case K_ADD: case K_ADC:
        ldr_avr(0, x->a); ldr_avr(1, x->b); dp_r(DP_ADD, 0, 2, 0, 1, 0, 0);
        if (x->k == K_ADC) dp_r(DP_ADD, 0, 2, 2, R_FC, 0, 0);
        str_avr(2, x->a); if (want & FV) fl_v_add(); fl_add(want, 0); break;
      case K_SUB: case K_SBC: case K_CP: case K_CPC: case K_SUBI: case K_SBCI: case K_CPI: {
        int imm = x->k == K_SUBI || x->k == K_SBCI || x->k == K_CPI, carry = x->k == K_SBC || x->k == K_CPC || x->k == K_SBCI;
        int store = x->k == K_SUB || x->k == K_SBC || x->k == K_SUBI || x->k == K_SBCI;
        ldr_avr(0, x->a); if (imm) movw(1, x->b); else ldr_avr(1, x->b);
        dp_r(DP_SUB, 0, 2, 0, 1, 0, 0); if (carry) dp_r(DP_SUB, 0, 2, 2, R_FC, 0, 0);
        if (store) str_avr(2, x->a);
        if (want & FV) fl_v_sub(); fl_add(want, carry); break;
      }
      case K_AND: case K_OR: case K_EOR: case K_ANDI: case K_ORI:
        ldr_avr(0, x->a);
        if (x->k == K_ANDI || x->k == K_ORI) dp_i(x->k == K_ANDI ? DI_AND : DI_ORR, 0, 2, 0, x->b);
        else { ldr_avr(1, x->b); dp_r(x->k == K_AND ? DP_AND : x->k == K_OR ? DP_ORR : DP_EOR, 0, 2, 0, 1, 0, 0); }
        str_avr(2, x->a);
        if (want & FZ) mov_r(R_FZ, 2); if (want & FN) mov_r(R_FN, 2); if (want & FV) movw(R_FV, 0); break;
      case K_COM:
        ldr_avr(0, x->a); dp_i(DI_EOR, 0, 2, 0, 0xFF); str_avr(2, x->a);
        if (want & FC) movw(R_FC, 1); if (want & FZ) mov_r(R_FZ, 2); if (want & FN) mov_r(R_FN, 2); if (want & FV) movw(R_FV, 0); break;
      case K_NEG:
        ldr_avr(1, x->a); movw(0, 0); dp_r(DP_SUB, 0, 2, 0, 1, 0, 0); str_avr(2, x->a);
        if (want & FV) fl_v_sub(); fl_add(want, 0); break;
      case K_SWAP:
        ldr_avr(0, x->a); mov_sh(1, 0, SH_LSR, 4); dp_r(DP_ORR, 0, 2, 1, 0, SH_LSL, 4); str_avr(2, x->a); break;
      case K_INC: case K_DEC:
        ldr_avr(0, x->a); if (x->k == K_INC) addw(2, 0, 1); else subw(2, 0, 1); uxtb(2, 2); str_avr(2, x->a);
        if (want & FV) { if (x->k == K_INC) dp_r(DP_BIC, 0, R_FV, 2, 0, 0, 0); else dp_r(DP_BIC, 0, R_FV, 0, 2, 0, 0); }
        if (want & FZ) mov_r(R_FZ, 2); if (want & FN) mov_r(R_FN, 2); break;
      case K_ASR: case K_LSR: case K_ROR:
        ldr_avr(0, x->a);
        if (x->k == K_LSR) mov_sh(2, 0, SH_LSR, 1);
        else if (x->k == K_ASR) { dp_i(DI_AND, 0, 2, 0, 0x80); dp_r(DP_ORR, 0, 2, 2, 0, SH_LSR, 1); }
        else { mov_sh(2, 0, SH_LSR, 1); dp_r(DP_ORR, 0, 2, 2, R_FC, SH_LSL, 7); }
        str_avr(2, x->a);
        dp_i(DI_AND, 0, R_FC, 0, 1);   /* C always (ROR reads it next) */
        if (want & FZ) mov_r(R_FZ, 2); if (want & FN) mov_r(R_FN, 2);
        if (want & FV) dp_r(DP_EOR, 0, R_FV, 2, R_FC, SH_LSL, 7);
        break;
      case K_ADIW: case K_SBIW:
        mem_i(LDRH_I, 0, R_D, x->a);
        if (x->k == K_ADIW) addw(2, 0, x->b); else subw(2, 0, x->b);
        H2(0xFA1Fu, 0xF080u | (2u << 8) | 2u);   /* uxth r2, r2 */
        mem_i(STRH_I, 2, R_D, x->a);
        if (want & FZ) mov_r(R_FZ, 2); if (want & FN) mov_sh(R_FN, 2, SH_LSR, 8);
        if (want & FC) { if (x->k == K_ADIW) dp_r(DP_BIC, 0, 3, 0, 2, 0, 0); else dp_r(DP_BIC, 0, 3, 2, 0, 0, 0); ubfx(R_FC, 3, 15, 1); }
        if (want & FV) { if (x->k == K_ADIW) dp_r(DP_BIC, 0, 3, 2, 0, 0, 0); else dp_r(DP_BIC, 0, 3, 0, 2, 0, 0); mov_sh(R_FV, 3, SH_LSR, 8); }
        break;
      case K_MUL:
        ldr_avr(0, x->a); ldr_avr(1, x->b); mul(2, 0, 1); mem_i(STRH_I, 2, R_D, 0);
        if (want & FC) ubfx(R_FC, 2, 15, 1); if (want & FZ) mov_r(R_FZ, 2); break;
      case K_LDS: case K_STS:
        if (x->w2 >= 0x100u || x->w2 < 0x20u) {
          u32 ad = x->w2 & 0xFFFu;
          if (x->k == K_LDS) { mem_i(LDRB_I, 0, R_D, ad); str_avr(0, x->a); }
          else { ldr_avr(0, x->a); mem_i(STRB_I, 0, R_D, ad); if (x->w2 >= 0x100u) str_ctx(R_D, J_DIRTY); }
          break;
        }
        goto help;
      case K_LDD: case K_STD:
        flush_cyc();
        mem_i(LDRH_I, 0, R_D, x->c); if (x->b) addw(0, 0, x->b);
        if (x->k == K_LDD) { data_ld(); str_avr(2, x->a); }
        else { ldr_avr(2, x->a); u16 *brk = data_st(); u16 *ok = b_fwd(); fix_fwd(brk); exit_c(next); fix_b(ok); }
        break;
      case K_LD: case K_ST:
        flush_cyc();
        ptr_get(x->c, x->b);
        if (x->k == K_LD) { data_ld(); str_avr(2, x->a); ptr_post(x->c, x->b); }
        else { ldr_avr(2, x->a); u16 *brk = data_st(); ptr_post(x->c, x->b); u16 *ok = b_fwd(); fix_fwd(brk); ptr_post(x->c, x->b); exit_c(next); fix_b(ok); }
        break;
      case K_LPM:
        mem_i(LDRH_I, 0, R_D, 30); ubfx(1, 0, 0, 15); ldr_ctx(3, J_FLASH); mem_r(LDRB_R, 2, 3, 1, 0);
        str_avr(2, x->a);
        if (x->b) { addw(0, 0, 1); mem_i(STRH_I, 0, R_D, 30); }
        break;
      case K_PUSH:
        mem_i(LDRH_I, 0, R_D, SPL); sat12(1, 0); ldr_avr(2, x->a); mem_r(STRB_R, 2, R_D, 1, 0);
        subw(0, 0, 1); mem_i(STRH_I, 0, R_D, SPL); break;
      case K_POP:
        mem_i(LDRH_I, 0, R_D, SPL); addw(0, 0, 1); mem_i(STRH_I, 0, R_D, SPL); sat12(1, 0); mem_r(LDRB_R, 2, R_D, 1, 0); str_avr(2, x->a); break;
      case K_INSP: mem_i(LDRB_I, 0, R_D, x->b); str_avr(0, x->a); break;
      case K_IN:
        if (x->b == SPSR || (x->b >= PINB && x->b <= PINF && (x->b - PINB) % 3u == 0u)) { mem_i(LDRB_I, 0, R_D, x->b); str_avr(0, x->a); break; }   /* kept current in D */
        flush_cyc(); str_ctx(R_LEFT, J_LEFT); movw(0, x->b); call_ctx(J_IN); str_avr(0, x->a); break;
      case K_OUT:
        if (x->b == SPDR) { ldr_avr(0, x->a); call_ctx(J_SPDR); tst_i(0, 1); { u16 *ok = bcc_fwd(CC_EQ); flush_cyc(); exit_c(next); fix_fwd(ok); } break; }
        flush_cyc(); str_ctx(R_LEFT, J_LEFT); movw(0, x->b); ldr_avr(1, x->a); call_ctx(J_OUT);
        tst_i(0, 1); { u16 *ok = bcc_fwd(CC_EQ); exit_c(next); fix_fwd(ok); } break;
      case K_BITIO: flush_cyc(); str_ctx(R_LEFT, J_LEFT); movw(0, x->w); call_ctx(J_BIT);
        tst_i(0, 1); { u16 *ok = bcc_fwd(CC_EQ); exit_c(next); fix_fwd(ok); } break;
      case K_CLI: dp_i(DI_AND, 0, R_SR, R_SR, 0x7F); break;
      case K_OUTSP: ldr_avr(0, x->a); mem_i(STRB_I, 0, R_D, x->b); break;
      case K_BST:
        ldr_avr(0, x->a); tst_i(0, x->b); dp_i(DI_AND, 0, R_SR, R_SR, 0xBF);
        { u16 *z = bcc_fwd(CC_EQ); dp_i(DI_ORR, 0, R_SR, R_SR, 0x40); fix_fwd(z); }
        break;
      case K_BLD:
        ldr_avr(0, x->a); dp_i(DI_AND, 0, 0, 0, (~x->b) & 0xFFu); tst_i(R_SR, 0x40);
        { u16 *z = bcc_fwd(CC_EQ); dp_i(DI_ORR, 0, 0, 0, x->b); fix_fwd(z); }
        str_avr(0, x->a); break;
      /* ---- block ends ---- */
      case K_RJMP: case K_JMP: exit_to(x->w2, g_acc); g_acc = 0; break;
      case K_RCALL: case K_CALL: push_pc(next); exit_to(x->w2, g_acc); g_acc = 0; break;
      case K_RET: pop_pc(); subs5(g_acc); g_acc = 0; b_w(g_stub_disp); break;
      case K_RETI: pop_pc(); subs5(g_acc); g_acc = 0; dp_i(DI_ORR, 0, R_SR, R_SR, 0x80); str_ctx(R_D, J_SPARE); b_w(g_stub_exit); break;   /* C: one instruction, then the interrupts */
      case K_IJMP: case K_ICALL:
        if (x->k == K_ICALL) push_pc(next);
        mem_i(LDRH_I, 0, R_D, 30); ubfx(0, 0, 0, 14); subs5(g_acc); g_acc = 0; b_w(g_stub_disp); break;
      case K_BR: {
        u32 c = g_acc - x->cyc;   /* not taken: 1 cycle, taken: 2 */
        u16 *tk;
        switch (x->a) {
          case 0: H1(0x2F00u); tk = bcc_fwd(x->b ? CC_EQ : CC_NE); break;              /* C: cmp r7, #0 */
          case 1: H1(0x2E00u); tk = bcc_fwd(x->b ? CC_NE : CC_EQ); break;              /* Z: cmp r6, #0 */
          case 2: tst_i(R_FN, 0x80); tk = bcc_fwd(x->b ? CC_EQ : CC_NE); break;
          case 3: tst_i(R_FV, 0x80); tk = bcc_fwd(x->b ? CC_EQ : CC_NE); break;
          case 4: dp_r(DP_EOR, 0, 0, R_FN, R_FV, 0, 0); tst_i(0, 0x80); tk = bcc_fwd(x->b ? CC_EQ : CC_NE); break;
          case 5: tst_i(R_FH, 0x10); tk = bcc_fwd(x->b ? CC_EQ : CC_NE); break;
          case 6: tst_i(R_SR, 0x40); tk = bcc_fwd(x->b ? CC_EQ : CC_NE); break;
          default: tst_i(R_SR, 0x80); tk = bcc_fwd(x->b ? CC_EQ : CC_NE); break;
        }
        exit_to(next, c + 1);
        fix_fwd(tk); exit_to(x->w2, c + 2); g_acc = 0; break;
      }
      case K_SBRC: case K_SBRS: case K_CPSE: {
        u32 c = g_acc - x->cyc, l2 = two_w(A_FLASH[next]) ? 2u : 1u;
        u16 *sk;
        if (x->k == K_CPSE) { ldr_avr(0, x->a); ldr_avr(1, x->b); dp_r(DP_SUB, 1, 15, 0, 1, 0, 0); sk = bcc_fwd(CC_EQ); }
        else { ldr_avr(0, x->a); tst_i(0, x->b); sk = bcc_fwd(x->k == K_SBRC ? CC_EQ : CC_NE); }
        exit_to(next, c + 1);
        fix_fwd(sk); exit_to((next + l2) & 0x3FFFu, c + 1 + l2); g_acc = 0; break;
      }
      case K_SKIPIO: {
        u32 c = g_acc - x->cyc, l2 = two_w(A_FLASH[next]) ? 2u : 1u;
        subs5(c); g_acc = 0;
        u32 ia = 0x20u + ((x->w >> 3) & 31u), sk_set = (x->w & 0x0200u) != 0;   /* SBIS: skip when set */
        u16 *sk;
        if (ia == SPSR || (ia >= PINB && ia <= PINF && (ia - PINB) % 3u == 0u)) {
          mem_i(LDRB_I, 0, R_D, ia); tst_i(0, 1u << (x->w & 7u)); sk = bcc_fwd(sk_set ? CC_NE : CC_EQ);
        } else { str_ctx(R_LEFT, J_LEFT); movw(0, x->w); call_ctx(J_BIT); tst_i(0, 2); sk = bcc_fwd(CC_NE); }
        exit_to(next, 1);
        fix_fwd(sk); exit_to((next + l2) & 0x3FFFu, 1 + l2); break;
      }
      default:
      help:
        flush_cyc();
        sync_out(); movw(0, x->pc); call_ctx(J_OP); sync_in();
        tst_i(0, 1); { u16 *ok = bcc_fwd(CC_EQ); exit_c(next); fix_fwd(ok); }
        break;
    }
    if (g_ep >= g_cend - 64) return 0;
  }
  if (!ends_block(g_jb[n - 1].k)) { u32 nx = (g_jb[n - 1].pc + g_jb[n - 1].len) & 0x3FFFu; exit_to(nx, g_acc); g_acc = 0; }
  if (g_ep >= g_cend - 8) return 0;
  return (u32)(start - g_code);
}
/* called by the dispatcher: translate the block at pc (empty the cache when full), returns pc */
__attribute__((used)) static u32 jit_xlate(u32 pc)
{
  pc &= 0x3FFFu;
  u32 at = xlate(pc);
  if (!at) { jit_flush(); at = xlate(pc); }
  g_map[pc] = (u16)at; g_blk_pc = 0xFFFFFFFFu;
  chain_fix(pc, at);
#ifdef __arm__
  __asm__ volatile("dsb\n isb" ::: "memory");
#endif
  return pc;
}

static void jit_init(void)
{
  JC[J_XLATE] = (u32)(uintptr_t)jit_xlate; JC[J_OP] = (u32)(uintptr_t)jit_op; JC[J_LD] = (u32)(uintptr_t)jit_ld; JC[J_ST] = (u32)(uintptr_t)jit_st;
  JC[J_IN] = (u32)(uintptr_t)jit_in; JC[J_OUT] = (u32)(uintptr_t)jit_out; JC[J_BIT] = (u32)(uintptr_t)jit_bit; JC[J_SPDR] = (u32)(uintptr_t)jit_spdr;
  JC[J_MAP] = (u32)(uintptr_t)g_map; JC[J_CODE1] = (u32)(uintptr_t)g_code | 1u; JC[J_FLASH] = (u32)(uintptr_t)A_FLASH;
  jit_flush();
}
u8 *ab_scratch(void) { jit_flush(); return (u8 *)g_code + JIT_CACHE - AB_SCRATCH; }
void ab_load(const u16 *flash, u32 words)
{
  A_FLASH = flash; g_words = words;
  jit_init();
}

#ifdef AB_JIT_HOST
u16 *jit_code(u32 *n) { *n = (u32)(g_ep - g_code); return g_code; }
u32 jit_x(u32 pc) { return jit_xlate(pc); }
u32 jit_at(u32 pc) { return g_map[pc]; }
#endif
AVR_FAST void ab_run(u32 cycles)
{
  u32 end = A_CYC + cycles, pc = g_pc, lim, v;
  pins_upd();   /* the buttons */
#ifdef AB_JIT_HOST
  void (*enter)(void) = 0; (void)enter; return;
#else
  void (*enter)(void) = (void (*)(void))((uintptr_t)(g_code + g_stub_enter) | 1u);
#endif
  for (;;) {
    g_rst = 0; ev_sync(); if (g_rst) pc = 0;
    if (g_hold) g_hold = 0;   /* after SEI, RETI: one more instruction first */
    else if ((A_D[SREG] & I_FLAG) && (v = irq_now())) {
      irq_ack(v);
      if (g_sleep) { g_sleep = 0; A_CYC += 4; }
      { u32 s = A_D[SPL] | (u32)A_D[SPH] << 8;
        A_D[s & 0xFFFu] = (u8)pc; s--; A_D[s & 0xFFFu] = (u8)(pc >> 8); s--; A_D[SPL] = (u8)s; A_D[SPH] = (u8)(s >> 8); }
      A_D[SREG] &= ~I_FLAG; pc = v * 2u; A_CYC += 5;
    }
    if ((s32)(A_CYC - end) >= 0) break;
    lim = ev_next(); lim = lim < end - A_CYC ? A_CYC + lim : end;
    if (g_sleep) { A_CYC = lim; continue; }
    g_jlim = lim; JC[J_LEFT] = lim - A_CYC; JC[J_PC] = pc; ctx_srset(A_D[SREG]);
    enter();
    A_CYC = g_jlim - JC[J_LEFT]; A_D[SREG] = (u8)CTX_SR(); pc = JC[J_PC] & 0x3FFFu;
    if (JC[J_SPARE]) { JC[J_SPARE] = 0; g_hold = 1; }
  }
  g_pc = pc;
}
