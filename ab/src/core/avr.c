/* ArduboyEmu: ATmega32U4 core and the Arduboy's hardware (see avr.h). Author: mameise. License: GPL-3.0-or-later. */
#include "avr.h"
#include <stdint.h>

#ifndef AVR_FAST
#define AVR_FAST __attribute__((optimize("O2")))
#endif

AbMem g_mem __attribute__((aligned(8)));
u8 A_EEP[AVR_EEP] __attribute__((aligned(4)));
u8 A_GD[1024] __attribute__((aligned(4)));
#if !AB_JIT
u32 A_OP[AVR_FLASH_WORDS + 4];   /* behind the end: O_BAD, which wraps the pc */
#endif
const u16 *A_FLASH;
static u32 g_words;
u32 A_CYC, A_KEYS, A_FRAMES;
int A_EEP_DIRTY;
AvrLcd A_LCD;
#ifdef AB_COUNT
u32 A_INS, A_OPC[96], A_PCH[16384];
#endif

/* ------------------------------------------------------------------ decoding
 * One u32 per program word: the handler (low 16 bits: its index, or on the Paradise its address in PRAM) and the
 * operands in the high 16 bits (f = o >> 16):
 *   two registers: d | r << 5 (CPSE: | skip of a two-word instruction << 10); register and byte: d | K << 8;
 *   jumps and branches: the target word; SBRC/SBRS/BLD/BST: d | skip2 << 5 | bit mask << 8;
 *   SBI/CBI/SBIC/SBIS: bit mask | (I/O address - 0x20) << 8 | skip2 << 13; LDS/STS: d, the address in the next slot. */
enum {
  O_NOP, O_MOVW, O_MULS, O_MULSU, O_FMUL, O_FMULS, O_FMULSU, O_CPC, O_SBC, O_ADD, O_CPSE, O_CP, O_SUB, O_ADC, O_AND,
  O_EOR, O_OR, O_MOV, O_CPI, O_SBCI, O_SUBI, O_ORI, O_ANDI, O_LDDY, O_LDDZ, O_STDY, O_STDZ, O_LDS, O_LDZP, O_LDMZ,
  O_LPMZ, O_LPMZP, O_LDYP, O_LDMY, O_LDX, O_LDXP, O_LDMX, O_POP, O_STS, O_STZP, O_STMZ, O_STYP, O_STMY, O_STX, O_STXP,
  O_STMX, O_PUSH, O_COM, O_NEG, O_SWAP, O_INC, O_ASR, O_LSR, O_ROR, O_DEC, O_BSET, O_BCLR, O_RET, O_RETI, O_SLEEP,
  O_WDR, O_LPM0, O_IJMP, O_ICALL, O_JMP, O_CALL, O_ADIW, O_SBIW, O_CBI, O_SBIC, O_SBI, O_SBIS, O_MUL, O_IN, O_OUT,
  O_RJMP, O_RCALL, O_LDI, O_BLD, O_BST, O_SBRC, O_SBRS, O_INSREG, O_OUTSREG,
  O_BREQ, O_BRNE, O_BRCS, O_BRCC, O_BRMI, O_BRPL, O_BRVS, O_BRVC, O_BRLT, O_BRGE, O_BRHS, O_BRHC, O_BRTS, O_BRTC,
  O_BRIE, O_BRID, O_BAD, O_N
};
static int two_words(u32 w) { return (w & 0xFE0Fu) == 0x9000u || (w & 0xFE0Fu) == 0x9200u || (w & 0xFE0Cu) == 0x940Cu; }
#define F(op, f) ((u32)(op) | ((u32)(f) << 16))
static u32 decode(u32 pc)
{
  u32 w = A_FLASH[pc], w2 = pc + 1 < AVR_FLASH_WORDS ? A_FLASH[pc + 1] : 0, sk2 = pc + 1 < AVR_FLASH_WORDS && two_words(w2);
  u32 d5 = (w >> 4) & 31u, r5 = (w & 15u) | ((w >> 5) & 16u), d4 = 16u + ((w >> 4) & 15u), K8 = ((w >> 4) & 0xF0u) | (w & 15u);
#define DR(op, d, r) F(op, (d) | (r) << 5)
#define DK(op, d, k) F(op, (d) | (k) << 8)
  switch (w >> 12) {
    case 0x0:
      if (w == 0) return O_NOP;
      switch ((w >> 10) & 3u) {
        case 0:
          if ((w & 0xFF00u) == 0x0100u) return DR(O_MOVW, ((w >> 4) & 15u) * 2u, (w & 15u) * 2u);
          if ((w & 0xFF00u) == 0x0200u) return DR(O_MULS, d4, 16u + (w & 15u));
          if ((w & 0xFF00u) == 0x0300u) {
            u32 d = 16u + ((w >> 4) & 7u), r = 16u + (w & 7u);
            switch (w & 0x88u) { case 0x00: return DR(O_MULSU, d, r); case 0x08: return DR(O_FMUL, d, r); case 0x80: return DR(O_FMULS, d, r); default: return DR(O_FMULSU, d, r); }
          }
          return O_BAD;
        case 1: return DR(O_CPC, d5, r5);
        case 2: return DR(O_SBC, d5, r5);
        default: return DR(O_ADD, d5, r5);
      }
    case 0x1:
      switch ((w >> 10) & 3u) { case 0: return DR(O_CPSE, d5, r5) | sk2 << 26; case 1: return DR(O_CP, d5, r5); case 2: return DR(O_SUB, d5, r5); default: return DR(O_ADC, d5, r5); }
    case 0x2:
      switch ((w >> 10) & 3u) { case 0: return DR(O_AND, d5, r5); case 1: return DR(O_EOR, d5, r5); case 2: return DR(O_OR, d5, r5); default: return DR(O_MOV, d5, r5); }
    case 0x3: return DK(O_CPI, d4, K8);
    case 0x4: return DK(O_SBCI, d4, K8);
    case 0x5: return DK(O_SUBI, d4, K8);
    case 0x6: return DK(O_ORI, d4, K8);
    case 0x7: return DK(O_ANDI, d4, K8);
    case 0x8: case 0xA: {
      u32 q = (w & 7u) | ((w >> 7) & 0x18u) | ((w >> 8) & 0x20u), st = (w >> 9) & 1u, y = (w >> 3) & 1u;
      return DK(st ? (y ? O_STDY : O_STDZ) : (y ? O_LDDY : O_LDDZ), d5, q);
    }
    case 0x9:
      switch ((w >> 9) & 7u) {
        case 0:   /* 1001 000d */
          switch (w & 15u) {
            case 0: return F(O_LDS, d5);
            case 1: return F(O_LDZP, d5); case 2: return F(O_LDMZ, d5);
            case 4: case 6: return F(O_LPMZ, d5); case 5: case 7: return F(O_LPMZP, d5);
            case 9: return F(O_LDYP, d5); case 10: return F(O_LDMY, d5);
            case 12: return F(O_LDX, d5); case 13: return F(O_LDXP, d5); case 14: return F(O_LDMX, d5);
            case 15: return F(O_POP, d5);
            default: return O_BAD;
          }
        case 1:
          switch (w & 15u) {
            case 0: return F(O_STS, d5);
            case 1: return F(O_STZP, d5); case 2: return F(O_STMZ, d5);
            case 9: return F(O_STYP, d5); case 10: return F(O_STMY, d5);
            case 12: return F(O_STX, d5); case 13: return F(O_STXP, d5); case 14: return F(O_STMX, d5);
            case 15: return F(O_PUSH, d5);
            default: return O_BAD;
          }
        case 2:   /* 1001 010x */
          switch (w & 15u) {
            case 0: return F(O_COM, d5); case 1: return F(O_NEG, d5); case 2: return F(O_SWAP, d5);
            case 3: return F(O_INC, d5); case 5: return F(O_ASR, d5); case 6: return F(O_LSR, d5);
            case 7: return F(O_ROR, d5); case 10: return F(O_DEC, d5);
            case 12: case 13: case 14: case 15: {
              u32 k = ((((w >> 4) & 31u) << 1 | (w & 1u)) << 16 | w2) & 0x3FFFu;
              return F((w & 2u) ? O_CALL : O_JMP, k);
            }
            case 8:
              if ((w & 0xFF0Fu) == 0x9408u) return F((w & 0x80u) ? O_BCLR : O_BSET, 1u << ((w >> 4) & 7u));
              switch (w) {
                case 0x9508: return O_RET; case 0x9518: return O_RETI; case 0x9588: return O_SLEEP; case 0x9598: return O_NOP;
                case 0x95A8: return O_WDR; case 0x95C8: case 0x95D8: return O_LPM0; case 0x95E8: case 0x95F8: return O_NOP;
              }
              return O_BAD;
            case 9:
              if (w == 0x9409u || w == 0x9419u) return O_IJMP;
              if (w == 0x9509u || w == 0x9519u) return O_ICALL;
              return O_BAD;
            default: return O_BAD;
          }
        case 3:
          return DK((w & 0x0100u) ? O_SBIW : O_ADIW, 24u + ((w >> 3) & 6u), ((w >> 2) & 0x30u) | (w & 15u));
        case 4: case 5: {
          u32 a = (w >> 3) & 31u, m = 1u << (w & 7u), set = (w >> 9) & 1u, sk = (w >> 8) & 1u;
          return F(set ? (sk ? O_SBIS : O_SBI) : (sk ? O_SBIC : O_CBI), m | a << 8 | sk2 << 13);
        }
        default: return DR(O_MUL, d5, r5);
      }
    case 0xB: {
      u32 a = 0x20u + ((w & 15u) | ((w >> 5) & 0x30u));
      if (a == 0x5Fu) return F((w & 0x0800u) ? O_OUTSREG : O_INSREG, d5);
      return DK((w & 0x0800u) ? O_OUT : O_IN, d5, a);
    }
    case 0xC: { s32 k = (s32)(w << 20) >> 20; return F(O_RJMP, (pc + 1 + (u32)k) & 0x3FFFu); }
    case 0xD: { s32 k = (s32)(w << 20) >> 20; return F(O_RCALL, (pc + 1 + (u32)k) & 0x3FFFu); }
    case 0xE: return DK(O_LDI, d4, K8);
    default: {
      if ((w & 0x0800u) == 0) {   /* BRBS / BRBC: one handler per flag and sense */
        s32 k = (s32)(w << 22) >> 25; u32 t = (pc + 1 + (u32)k) & 0x3FFFu, b = w & 7u, clr = (w >> 10) & 1u;
        static const u8 BR[8] = { O_BRCS, O_BREQ, O_BRMI, O_BRVS, O_BRLT, O_BRHS, O_BRTS, O_BRIE };
        return F(BR[b] + clr, t);
      }
      u32 m = 1u << (w & 7u);
      switch ((w >> 9) & 3u) {
        case 0: return DK(O_BLD, d5, m);
        case 1: return DK(O_BST, d5, m);
        case 2: return DK(O_SBRC, d5 | sk2 << 5, m);
        default: return DK(O_SBRS, d5 | sk2 << 5, m);
      }
    }
  }
}

/* ------------------------------------------------------------------ I/O registers (data addresses) */
#define PINB 0x23
#define DDRB 0x24
#define PORTB 0x25
#define PINC 0x26
#define DDRC 0x27
#define PORTC 0x28
#define PIND 0x29
#define DDRD 0x2A
#define PORTD 0x2B
#define PINE 0x2C
#define DDRE 0x2D
#define PORTE 0x2E
#define PINF 0x2F
#define DDRF 0x30
#define PORTF 0x31
#define TIFR0 0x35
#define TIFR1 0x36
#define TIFR3 0x38
#define TIFR4 0x39
#define EECR 0x3F
#define EEDR 0x40
#define EEARL 0x41
#define EEARH 0x42
#define TCCR0A 0x44
#define TCCR0B 0x45
#define TCNT0 0x46
#define OCR0A 0x47
#define OCR0B 0x48
#define PLLCSR 0x49
#define SPCR 0x4C
#define SPSR 0x4D
#define SPDR 0x4E
#define SMCR 0x53
#define MCUSR 0x54
#define SPL 0x5D
#define SPH 0x5E
#define SREG 0x5F
#define WDTCSR 0x60
#define TIMSK0 0x6E
#define TIMSK1 0x6F
#define TIMSK3 0x71
#define TIMSK4 0x72
#define ADCL 0x78
#define ADCH 0x79
#define ADCSRA 0x7A
#define TCCR1A 0x80
#define TCCR3A 0x90
#define TCNT4 0xBE
#define TC4H 0xBF
#define TCCR4A 0xC0
#define TCCR4B 0xC1
#define TCCR4C 0xC2
#define TCCR4D 0xC3
#define OCR4A 0xCF
#define OCR4B 0xD0
#define OCR4C 0xD1
#define OCR4D 0xD2
#define PLLFRQ 0x52
#define I_FLAG 0x80u

/* ------------------------------------------------------------------ timers
 * The counter is a position ph in one period P (up: P = top + 1, count = ph; both ways: P = 2 top, count goes up then
 * down). Kept at cycle `base`; brought up to date (flags set) when read or changed and at each event. */
typedef struct {
  u32 base, ph, P, top, sh, mul, dual, tov_ok, temp, ocr[4];
  u8 tifr, timsk, t16;
} Tm;
static Tm g_t[4];   /* timer 0, 1, 3, 4 */
/* AB_EVC: each timer's next event (cycle) is kept until something changes it (I/O write, flag change, interrupt);
 * only a timer whose event came is brought up to date (at the latest every 4M cycles) */
#ifndef AB_EVC
#define AB_EVC 1
#endif
static u32 g_tat[4], g_tv;   /* g_tv bit i: g_tat[i] holds */
static const u8 T_TIFR[4] = { TIFR0, TIFR1, TIFR3, TIFR4 }, T_TIMSK[4] = { TIMSK0, TIMSK1, TIMSK3, TIMSK4 };
/* flag bits of the compare channels and overflow */
static const u8 T_FB[4][4] = { { 2, 4, 0, 0 }, { 2, 4, 8, 0 }, { 2, 4, 8, 0 }, { 0x40, 0x20, 0x80, 0 } };
static const u8 T_TOV[4] = { 1, 1, 1, 4 };
#define STOP 255u
static u32 tm_count(const Tm *t) { return t->dual && t->ph > t->top ? t->P - t->ph : t->ph; }
static u32 tm_dist(const Tm *t, u32 e) { return (e + t->P - t->ph - 1u) % t->P + 1u; }   /* ticks until position e */
static void tm_flags(Tm *t, u32 n, u8 *fl)
{
  if (!n) return;
  u32 f = 0;
  for (int c = 0; c < 3; c++) {
    u32 v = t->ocr[c], b = T_FB[t - g_t][c];
    if (!b || v > t->top) continue;
    if (tm_dist(t, v) <= n) f |= b;
    else if (t->dual && v && v < t->top && tm_dist(t, t->P - v) <= n) f |= b;
  }
  if (t->tov_ok && tm_dist(t, 0) <= n) f |= T_TOV[t - g_t];
  *fl |= (u8)f;
}
static void tm_sync(int i)
{
  Tm *t = &g_t[i];
  if (t->sh == STOP) { t->base = A_CYC; return; }
  u32 el = A_CYC - t->base, n = (el * t->mul) >> t->sh;
  if (!n) return;
  t->base += t->mul == 1u ? n << t->sh : ((n << t->sh) + t->mul - 1u) / t->mul;
  { u8 *fl = &A_D[T_TIFR[i]]; u32 o = *fl; tm_flags(t, n, fl); if (*fl != o) g_tv &= ~(1u << i); }
  t->ph = (t->ph + n % t->P) % t->P;
}
/* cycles until the next flag that would start an interrupt (0xFFFFFFFF: none) */
static u32 tm_next(int i)
{
  Tm *t = &g_t[i];
  u32 m = A_D[T_TIMSK[i]] & ~A_D[T_TIFR[i]];
  if (t->sh == STOP || !m) return 0xFFFFFFFFu;
  u32 best = 0xFFFFFFFFu;
  for (int c = 0; c < 3; c++) {
    u32 v = t->ocr[c], b = T_FB[i][c];
    if (!(m & b) || v > t->top) continue;
    u32 dd = tm_dist(t, v); if (dd < best) best = dd;
    if (t->dual && v && v < t->top) { dd = tm_dist(t, t->P - v); if (dd < best) best = dd; }
  }
  if ((m & T_TOV[i]) && t->tov_ok) { u32 dd = tm_dist(t, 0); if (dd < best) best = dd; }
  if (best == 0xFFFFFFFFu) return best;
  /* cycles: the tick `best` comes at base + best * 2^sh / mul */
  u32 at = t->base + (t->mul == 1u ? best << t->sh : ((best << t->sh) + t->mul - 1u) / t->mul);
  u32 d = at - A_CYC;
  return (s32)d < 0 ? 0u : d;   /* already due (the clock moved on since the last sync) */
}
static const u8 PS0[8] = { STOP, 0, 3, 6, 8, 10, STOP, STOP };
static void tm_cfg(int i)
{
  Tm *t = &g_t[i]; u32 cnt = tm_count(t), wgm, top = 0xFF, dual = 0, tov = 1;
  t->mul = 1;
  if (i == 0) {
    wgm = (A_D[TCCR0A] & 3u) | ((A_D[TCCR0B] >> 1) & 4u); t->sh = PS0[A_D[TCCR0B] & 7u];
    switch (wgm) { case 1: dual = 1; break; case 2: top = t->ocr[0]; tov = top == 0xFF; break; case 5: dual = 1; top = t->ocr[0]; break; case 7: top = t->ocr[0]; break; }
  } else if (i < 3) {
    u32 a = i == 1 ? TCCR1A : TCCR3A;
    wgm = (A_D[a] & 3u) | ((A_D[a + 1] >> 1) & 12u); t->sh = PS0[A_D[a + 1] & 7u];
    static const u16 TOPS[16] = { 0xFFFF, 0xFF, 0x1FF, 0x3FF, 0, 0xFF, 0x1FF, 0x3FF, 1, 0, 1, 0, 1, 0xFFFF, 1, 0 };
    top = TOPS[wgm]; if (!top) top = t->ocr[0]; else if (top == 1) top = t->ocr[3];   /* 0: OCRnA, 1: ICRn */
    dual = (wgm >= 1 && wgm <= 3) || (wgm >= 8 && wgm <= 11);
    tov = wgm == 0 || (wgm >= 5 && wgm <= 7) || wgm >= 14 || dual || top == 0xFFFF;
  } else {
    u32 cs = A_D[TCCR4B] & 15u; t->sh = cs ? cs - 1u : STOP;
    u32 tm = (A_D[PLLFRQ] >> 4) & 3u; static const u8 MUL[4] = { 1, 6, 4, 3 };   /* PLL 96 MHz /1, /1.5, /2 */
    if (tm && (A_D[PLLCSR] & 2u)) { t->mul = MUL[tm]; if (tm == 2) t->mul = 4; }
    top = t->ocr[3] ? t->ocr[3] : 0x3FF; dual = (A_D[TCCR4D] & 3u) == 1u;
  }
  if (top == 0) top = 1;
  t->top = top; t->dual = dual; t->tov_ok = tov; t->P = dual ? 2u * top : top + 1u;
  t->ph = cnt % t->P;
}

/* ------------------------------------------------------------------ display (SSD1306) */
static u32 g_col, g_page, g_c0 = 0, g_c1 = 127, g_p0 = 0, g_p1 = 7, g_amode = 2, g_cmd_n, g_cmd[8];
static u32 lcd_args(u32 c)
{
  switch (c) {
    case 0x81: case 0x20: case 0xA8: case 0xD3: case 0xD5: case 0xD9: case 0xDA: case 0xDB: case 0x8D: return 1;
    case 0x21: case 0x22: case 0xA3: return 2;
    case 0x26: case 0x27: return 6;
    case 0x29: case 0x2A: return 5;
    default: return 0;
  }
}
#ifdef AB_TRACE
#include <stdio.h>
#endif
/* boot: as the Arduboy's boot loader leaves the display (on, rows across); rst: the display's own reset (pin PD7) */
static void lcd_reset(int boot)
{
  A_LCD.on = (u8)boot; A_LCD.invert = 0; A_LCD.contrast = boot ? 0xCF : 0x7F; A_LCD.flip_x = (u8)boot; A_LCD.flip_y = (u8)boot; A_LCD.allon = 0; A_LCD.start = 0;
  g_col = g_page = 0; g_c0 = 0; g_c1 = 127; g_p0 = 0; g_p1 = 7; g_amode = boot ? 0 : 2; g_cmd_n = 0;
}
static void lcd_cmd(u32 c)
{
#ifdef AB_TRACE
  printf("cmd %02x\n", c);
#endif
  if (g_cmd_n) {
    g_cmd[g_cmd_n++] = c;
    if (g_cmd_n <= lcd_args(g_cmd[0])) return;
    switch (g_cmd[0]) {
      case 0x81: A_LCD.contrast = (u8)g_cmd[1]; break;
      case 0x20: g_amode = g_cmd[1] & 3u; break;
      case 0x21: g_c0 = g_cmd[1] & 127u; g_c1 = g_cmd[2] & 127u; g_col = g_c0; break;
      case 0x22: g_p0 = g_cmd[1] & 7u; g_p1 = g_cmd[2] & 7u; g_page = g_p0; break;
    }
    g_cmd_n = 0; return;
  }
  if (lcd_args(c)) { g_cmd[0] = c; g_cmd_n = 1; return; }
  if (c <= 0x0F) { g_col = (g_col & 0xF0u) | c; return; }
  if (c <= 0x1F) { g_col = (g_col & 0x0Fu) | ((c & 7u) << 4); return; }
  if (c >= 0x40 && c <= 0x7F) { A_LCD.start = (u8)(c & 63u); return; }
  if (c >= 0xB0 && c <= 0xB7) { g_page = c & 7u; return; }
  switch (c) {
    case 0xAE: A_LCD.on = 0; break; case 0xAF: A_LCD.on = 1; break;
    case 0xA6: A_LCD.invert = 0; break; case 0xA7: A_LCD.invert = 1; break;
    case 0xA4: A_LCD.allon = 0; break; case 0xA5: A_LCD.allon = 1; break;
    case 0xA0: A_LCD.flip_x = 0; break; case 0xA1: A_LCD.flip_x = 1; break;
    case 0xC0: A_LCD.flip_y = 0; break; case 0xC8: A_LCD.flip_y = 1; break;
  }
}
/* the program may want each full picture (greys made by switching pictures) */
__attribute__((weak)) void ab_frame(void) {}
static inline __attribute__((always_inline)) void lcd_data(u32 v)
{
#ifdef AB_TRACE
  { static u32 n, nz; n++; if (v) nz++; if ((n & 0xFFF) == 0) printf("data %u nonzero %u cyc %u\n", n, nz, A_CYC); }
#endif
  u32 col = g_col;
  A_GD[(g_page & 7u) * 128u + (col & 127u)] = (u8)v;
  if (__builtin_expect(g_amode == 0 && col < g_c1, 1)) { g_col = col + 1u; return; }
  if (g_amode == 2) { g_col = (col + 1u) & 127u; return; }
  if (g_amode == 0) {
    if (g_col >= g_c1) { g_col = g_c0; if (g_page >= g_p1) { g_page = g_p0; A_FRAMES++; ab_frame(); } else g_page++; }
    else g_col++;
  } else {
    if (g_page >= g_p1) { g_page = g_p0; if (g_col >= g_c1) { g_col = g_c0; A_FRAMES++; ab_frame(); } else g_col++; }
    else g_page++;
  }
}

/* ------------------------------------------------------------------ the rest of the hardware */
/* AB_SND2: the pitch from the times of the pin changes (not their number in the window), a change of both speaker pins
 * at once counts once, and sound made by PWM samples on OC4A (ATMlib) from the rising edges of the samples */
#ifndef AB_SND2
#define AB_SND2 1
#endif
static u32 g_tt0, g_tt1, g_pe, g_pt0, g_pt1, g_pl, g_pd;
static u32 g_lfsr = 0xACE1u, g_tog, g_wdt_at, g_sleep, g_spm, g_pc, g_rst;
/* AB_SPIN: busy waiting (a loop that only asks the clock, as millis() and micros() do, and changes nothing) is skipped
 * up to the next event. Anchor: "in rd, SREG" (the start of millis() and micros()). An iteration from the anchor back to
 * it, at the same stack depth, that wrote nothing but its own stack (and the call into the anchor's function: 32
 * bytes above it) and no I/O, changes nothing but the time. */
#ifndef AB_SPIN
#define AB_SPIN 1
#endif
#if AB_JIT
#define g_dirty (g_mem.jit[9])   /* J_DIRTY of jit.inc */
static u32 g_anc_pc = 0xFFFFFFFFu, g_anc_sp, g_tread;
#else
static u32 g_anc_pc = 0xFFFFFFFFu, g_anc_sp, g_dirty = 1, g_tread;
#endif
static u32 rnd(void) { g_lfsr ^= g_lfsr << 13; g_lfsr ^= g_lfsr >> 17; g_lfsr ^= g_lfsr << 5; return g_lfsr; }
#define WDT_ON (A_D[WDTCSR] & 0x48u)
static void wdt_restart(void)
{
  u32 p = (A_D[WDTCSR] & 7u) | ((A_D[WDTCSR] >> 2) & 8u); if (p > 9u) p = 9u;
  g_wdt_at = A_CYC + (16000u * 16u << p);   /* 16 ms << WDP */
}
static int tm_of(u32 a, u32 *reg)
{
  if (a >= TCCR0A && a <= OCR0B) { *reg = a; return 0; }
  if (a >= 0x80 && a <= 0x8D) { *reg = a; return 1; }
  if (a >= 0x90 && a <= 0x9D) { *reg = a; return 2; }
  if (a == TCNT4 || a == TC4H || (a >= TCCR4A && a <= 0xC4) || (a >= OCR4A && a <= OCR4D)) { *reg = a; return 3; }
  return -1;
}
static u32 pins(u32 port, u32 ext)
{
  u32 ddr = A_D[port - 1];
  return (A_D[port] & ddr) | (ext & ~ddr);
}
static u8 io_rd(u32 a)
{
  u32 r;
#ifdef AB_IOSTAT
  { extern u32 A_IOR[256]; A_IOR[a & 255u]++; }
#endif
  switch (a) {
    case PINB: case PINC: case PIND: case PINE: case PINF: return A_D[a];   /* pins_upd */
    case PLLCSR: return (u8)((A_D[PLLCSR] & ~1u) | ((A_D[PLLCSR] >> 1) & 1u));
    case SPSR: return A_D[SPSR];
    case SPDR: A_D[SPSR] &= 0x3Fu; return 0;
    case ADCL: return A_D[ADCL];
    case EECR: return A_D[EECR] & ~2u;
  }
  int i = tm_of(a, &r);
  if (i >= 0) {
    tm_sync(i); Tm *t = &g_t[i];
    if (i == 0) { if (a == TCNT0) { g_tread = 1; return (u8)tm_count(t); } return A_D[a]; }
    if (i < 3) {
      u32 o = a - (i == 1 ? 0x80u : 0x90u);
      if (o == 4) { u32 c = tm_count(t); t->temp = c >> 8; g_tread = 1; return (u8)c; }
      if (o == 5) return (u8)t->temp;
      if (o == 6) { t->temp = t->ocr[3] >> 8; return (u8)t->ocr[3]; }
      if (o == 7) return (u8)t->temp;
      if (o >= 8 && o <= 13) { u32 v = t->ocr[(o - 8) >> 1]; return (u8)(o & 1u ? v >> 8 : v); }
      return A_D[a];
    }
    if (a == TCNT4) { u32 c = tm_count(t); A_D[TC4H] = (u8)(c >> 8); g_tread = 1; return (u8)c; }
    if (a >= OCR4A && a <= OCR4D) { static const u8 SL[4] = { 0, 1, 3, 2 }; u32 x = t->ocr[SL[a - OCR4A]]; A_D[TC4H] = (u8)(x >> 8); return (u8)x; }
    return A_D[a];
  }
  return A_D[a];
}
/* returns 1 when an interrupt may now be due (the run loop looks again) */
#ifdef AB_IOSTAT
u32 A_IOW[256], A_IOR[256];
#endif
/* the pins as the program reads them, kept in the data space (the fast core reads them there) */
static void pins_upd(void)
{
  u32 m = 0xFFu; if (A_KEYS & AB_UP) m &= ~0x80u; if (A_KEYS & AB_RIGHT) m &= ~0x40u; if (A_KEYS & AB_LEFT) m &= ~0x20u; if (A_KEYS & AB_DOWN) m &= ~0x10u;
  A_D[PINB] = (u8)pins(PORTB, (A_KEYS & AB_B) ? 0xEFu : 0xFFu); A_D[PINC] = (u8)pins(PORTC, 0xFFu);
  A_D[PIND] = (u8)pins(PORTD, 0xFFu); A_D[PINE] = (u8)pins(PORTE, (A_KEYS & AB_A) ? 0xBFu : 0xFFu); A_D[PINF] = (u8)pins(PORTF, m);
}
static int io_wr0(u32 a, u32 v);
static int io_wr(u32 a, u32 v)
{
  int r = io_wr0(a, v);
  if (a >= PINB && a <= PORTF) pins_upd();
  else if (!(a >= SPCR && a <= SPDR) && !(a >= SPL && a <= SREG)) g_tv = 0;
  return r;
}
static int io_wr0(u32 a, u32 v)
{
  u32 r, old;
#ifdef AB_IOSTAT
  A_IOW[a & 255u]++;
#endif
  if (a != SPL && a != SPH && a != SREG) g_dirty = 1;
  switch (a) {
    case PINB: case PINC: case PIND: case PINE: case PINF: a += 2; v = A_D[a] ^ v; /* fall through: toggle */
    case PORTB: case PORTD: case PORTE: case PORTF: case PORTC:
      old = A_D[a]; A_D[a] = (u8)v;
      if (a == PORTD && (old & ~v & A_D[DDRD] & 0x80u)) lcd_reset(0);   /* display reset pin low */
      if (a == PORTC) { u32 x = (old ^ v) & A_D[DDRC] & 0xC0u;
#if AB_SND2
        if (x) { if (!g_tog) g_tt0 = A_CYC; g_tog++; g_tt1 = A_CYC; }
#else
        g_tog += (x >> 7) + ((x >> 6) & 1u);
#endif
      }
      return 0;
    case TIFR0: case TIFR1: case TIFR3: case TIFR4:
      { int i = a == TIFR0 ? 0 : a == TIFR1 ? 1 : a == TIFR3 ? 2 : 3; tm_sync(i); A_D[a] &= (u8)~v; return 1; }
    case TIMSK0: case TIMSK1: case TIMSK3: case TIMSK4:
      { int i = a == TIMSK0 ? 0 : a == TIMSK1 ? 1 : a == TIMSK3 ? 2 : 3; tm_sync(i); A_D[a] = (u8)v; return 1; }
    case SREG: old = A_D[SREG]; A_D[SREG] = (u8)v; return (v & ~old & I_FLAG) != 0;
    case SPDR:
      A_D[SPDR] = (u8)v;
      if (A_D[SPCR] & 0x40u) {
        if (A_D[PORTD] & 0x10u) lcd_data(v); else lcd_cmd(v);   /* the display is always selected (CS not used) */
        A_D[SPSR] |= 0x80u;
        return (A_D[SPCR] & 0x80u) != 0;
      }
      return 0;
    case SPSR: A_D[SPSR] = (u8)((A_D[SPSR] & 0xFEu) | (v & 1u)); return 0;
    case EECR:
      A_D[EECR] = (u8)(v & 0x3Fu);
      if (v & 1u) A_D[EEDR] = A_EEP[(A_D[EEARL] | A_D[EEARH] << 8) & (AVR_EEP - 1u)];
      if ((v & 2u) && (v & 4u || g_spm)) {
        u32 ad = (A_D[EEARL] | A_D[EEARH] << 8) & (AVR_EEP - 1u), md = (v >> 4) & 3u, nv = md == 0 ? A_D[EEDR] : md == 1 ? 0xFFu : (A_EEP[ad] & A_D[EEDR]);
        if (A_EEP[ad] != nv) { A_EEP[ad] = (u8)nv; A_EEP_DIRTY = 1; }
        A_D[EECR] &= ~6u;
      }
      g_spm = (v & 4u) != 0;
      return (v & 8u) != 0;
    case ADCSRA:
      A_D[ADCSRA] = (u8)(v & ~0x10u) | (A_D[ADCSRA] & 0x10u & ~v);
      if (v & 0x40u) { u32 x = rnd() & 0x3FFu; A_D[ADCL] = (u8)x; A_D[ADCH] = (u8)(x >> 8); A_D[ADCSRA] = (u8)((A_D[ADCSRA] & ~0x40u) | 0x10u); return (v & 8u) != 0; }
      return 0;
    case WDTCSR:
      A_D[WDTCSR] = (u8)((v & 0x7Fu) | (A_D[WDTCSR] & 0x80u & ~v));
      if (WDT_ON) wdt_restart();
      return 1;
    case SMCR: A_D[SMCR] = (u8)v; return 0;
    case MCUSR: A_D[MCUSR] = (u8)(A_D[MCUSR] & v); return 0;
    case PLLCSR: A_D[PLLCSR] = (u8)(v & 0xFEu); tm_sync(3); tm_cfg(3); return 0;
    case PLLFRQ: A_D[PLLFRQ] = (u8)v; tm_sync(3); tm_cfg(3); return 0;
  }
  int i = tm_of(a, &r);
  if (i >= 0) {
    tm_sync(i); Tm *t = &g_t[i];
    if (i == 0) {
      if (a == TCNT0) { t->ph = v % t->P; return 1; }
      A_D[a] = (u8)v;
      if (a == OCR0A) t->ocr[0] = v; else if (a == OCR0B) t->ocr[1] = v;
      tm_cfg(0); return 1;
    }
    if (i < 3) {
      u32 o = a - (i == 1 ? 0x80u : 0x90u);
      if (o == 5 || o == 7 || o == 9 || o == 11 || o == 13) { t->temp = v; return 0; }
      u32 w = (t->temp << 8) | v;
      if (o == 4) { t->ph = w % t->P; return 1; }
      if (o == 6) t->ocr[3] = w; else if (o >= 8) t->ocr[(o - 8) >> 1] = w; else A_D[a] = (u8)v;
      tm_cfg(i); return 1;
    }
    u32 w = ((A_D[TC4H] & 3u) << 8) | v;
    if (a == TC4H) { A_D[TC4H] = (u8)(v & 3u); return 0; }
    if (a == TCNT4) { t->ph = w % t->P; return 1; }
    if (a == OCR4A) {
      t->ocr[0] = w;
#if AB_SND2
      if (w > g_pl + 8u) { if (g_pd != 1u) { if (!g_pe) g_pt0 = A_CYC; g_pe++; g_pt1 = A_CYC; } g_pd = 1; }
      else if (w + 8u < g_pl) g_pd = 2;
      g_pl = w;
#endif
    } else if (a == OCR4B) t->ocr[1] = w;
    else if (a == OCR4C) t->ocr[3] = w; else if (a == OCR4D) t->ocr[2] = w;   /* D has the third flag, C is the top */
    else A_D[a] = (u8)v;
    tm_cfg(3); return 1;
  }
  A_D[a] = (u8)v;
  return 0;
}
/* the interrupt to run now (vector number), 0: none */
static u32 irq_now(void)
{
  u32 f;
  if ((A_D[WDTCSR] & 0xC0u) == 0xC0u) return 12;
  if ((f = A_D[TIFR1] & A_D[TIMSK1] & 0x2Fu)) return f & 0x20u ? 16 : f & 2u ? 17 : f & 4u ? 18 : f & 8u ? 19 : 20;
  if ((f = A_D[TIFR0] & A_D[TIMSK0] & 7u)) return f & 2u ? 21 : f & 4u ? 22 : 23;
  if ((A_D[SPSR] & 0x80u) && (A_D[SPCR] & 0x80u)) return 24;
  if ((A_D[ADCSRA] & 0x18u) == 0x18u) return 29;
  if (A_D[EECR] & 8u) return 30;
  if ((f = A_D[TIFR3] & A_D[TIMSK3] & 0x2Fu)) return f & 0x20u ? 31 : f & 2u ? 32 : f & 4u ? 33 : f & 8u ? 34 : 35;
  if ((f = A_D[TIFR4] & A_D[TIMSK4] & 0xE4u)) return f & 0x40u ? 38 : f & 0x20u ? 39 : f & 0x80u ? 40 : 41;
  return 0;
}
#ifdef AB_IOSTAT
u32 A_IRQ[64];
#endif
static void irq_ack(u32 v)
{
#ifdef AB_IOSTAT
  A_IRQ[v & 63u]++;
#endif
  g_tv &= v >= 38 ? ~8u : v >= 31 ? ~4u : v >= 21 ? ~1u : v >= 16 ? ~2u : ~0u;
  switch (v) {
    case 12: A_D[WDTCSR] &= ~0x80u; break;
    case 16: A_D[TIFR1] &= ~0x20u; break; case 17: A_D[TIFR1] &= ~2u; break; case 18: A_D[TIFR1] &= ~4u; break; case 19: A_D[TIFR1] &= ~8u; break; case 20: A_D[TIFR1] &= ~1u; break;
    case 21: A_D[TIFR0] &= ~2u; break; case 22: A_D[TIFR0] &= ~4u; break; case 23: A_D[TIFR0] &= ~1u; break;
    case 24: A_D[SPSR] &= ~0x80u; break; case 29: A_D[ADCSRA] &= ~0x10u; break;
    case 31: A_D[TIFR3] &= ~0x20u; break; case 32: A_D[TIFR3] &= ~2u; break; case 33: A_D[TIFR3] &= ~4u; break; case 34: A_D[TIFR3] &= ~8u; break; case 35: A_D[TIFR3] &= ~1u; break;
    case 38: A_D[TIFR4] &= ~0x40u; break; case 39: A_D[TIFR4] &= ~0x20u; break; case 40: A_D[TIFR4] &= ~0x80u; break; case 41: A_D[TIFR4] &= ~4u; break;
  }
}
static void ev_sync(void)
{
#if AB_EVC
  for (int i = 0; i < 4; i++) if (!(g_tv & (1u << i)) || (s32)(A_CYC - g_tat[i]) >= 0) { tm_sync(i); g_tv &= ~(1u << i); }
#else
  for (int i = 0; i < 4; i++) tm_sync(i);
#endif
  if (WDT_ON && (s32)(A_CYC - g_wdt_at) >= 0) {
    if (A_D[WDTCSR] & 0x40u) { A_D[WDTCSR] |= 0x80u; wdt_restart(); }
    else { ab_reset(); A_D[MCUSR] |= 8u; }
  }
}
static u32 ev_next(void)
{
  u32 b = 0xFFFFFFFFu;
#if AB_EVC
  for (int i = 0; i < 4; i++) {
    if (!(g_tv & (1u << i))) { u32 n = tm_next(i); g_tat[i] = A_CYC + (n < 0x400000u ? n : 0x400000u); g_tv |= 1u << i; }
    u32 n = g_tat[i] - A_CYC; if ((s32)n < 0) n = 0; if (n < b) b = n;
  }
#else
  for (int i = 0; i < 4; i++) { u32 n = tm_next(i); if (n < b) b = n; }
#endif
  if (WDT_ON) { u32 n = g_wdt_at - A_CYC; if ((s32)n < 0) n = 0; if (n < b) b = n; }
  return b;
}

/* ------------------------------------------------------------------ program */
#if !AB_JIT
static void *const *g_T;   /* the handlers (filled in by the first ab_run(0)) */
#if defined(__arm__) && !defined(AB_INDEX)
#define AB_ADDR 1          /* the handler's address in the op (PRAM is below 64 KB) */
#else
#define AB_ADDR 0
#endif
static u32 op_of(u32 o) { u32 i = o & 0xFFFFu; return AB_ADDR ? (o & 0xFFFF0000u) | ((u32)(uintptr_t)g_T[i] & 0xFFFFu) : o; }
void ab_load(const u16 *flash, u32 words)
{
  if (!g_T) ab_run(0);
  A_FLASH = flash; g_words = words;
  for (u32 pc = 0; pc < AVR_FLASH_WORDS; pc++) A_OP[pc] = op_of(pc < words ? decode(pc) : O_BAD);
  for (u32 pc = 0; pc + 1 < words; pc++) {   /* LDS, STS: the address in the next slot */
    u32 w = flash[pc];
    if ((w & 0xFE0Fu) == 0x9000u || (w & 0xFE0Fu) == 0x9200u) A_OP[pc + 1] = op_of(F(O_BAD, flash[pc + 1]));
  }
  for (u32 pc = AVR_FLASH_WORDS; pc < AVR_FLASH_WORDS + 4; pc++) A_OP[pc] = op_of(O_BAD);
}
#endif
void ab_reset(void)
{
  for (u32 i = 0; i < AVR_DATA; i++) A_D[i] = 0;
  A_D[SPL] = (u8)(AVR_DATA - 1u); A_D[SPH] = (u8)((AVR_DATA - 1u) >> 8);
  A_D[PLLFRQ] = 0x04; A_D[0x61] = 0; A_D[MCUSR] = 1;
  for (int i = 0; i < 4; i++) { Tm *t = &g_t[i]; for (u32 k = 0; k < sizeof *t; k++) ((u8 *)t)[k] = 0; t->base = A_CYC; }
  g_t[3].ocr[3] = 0xFF;
  for (int i = 0; i < 4; i++) tm_cfg(i);
  lcd_reset(1);
  g_sleep = 0; g_spm = 0; g_pc = 0; g_rst = 1; g_dirty = 1; g_anc_pc = 0xFFFFFFFFu; g_tv = 0; pins_upd();
}

#if !AB_JIT
/* ------------------------------------------------------------------ the CPU
 * Flags are kept apart and put together only when SREG is read: fc = C (0/1), fz = 0 exactly when Z, fn bit 7 = N,
 * fv bit 7 = V, fh bit 4 = H, S = N ^ V; sr keeps I and T. left: cycles until the next event (lim). */
#define T_F 64u
#define R(i) D[i]
#define RW(i) (D[i] | (u32)D[(i) + 1] << 8)
#define SP_GET() (D[SPL] | (u32)D[SPH] << 8)
#define SP_SET(v) do { u32 _v = (v); D[SPL] = (u8)_v; D[SPH] = (u8)(_v >> 8); } while (0)
#define PUSHB(v) do { u32 _s = SP_GET(); D[_s < 0x1000u ? _s : 0xFFFu] = (u8)(v); if (_s > g_anc_sp + 32u) g_dirty = 1; SP_SET(_s - 1u); } while (0)
#define POPB() ({ u32 _s = SP_GET() + 1u; SP_SET(_s); D[_s < 0x1000u ? _s : 0xFFFu]; })
#define PUSHPC(p) do { PUSHB(p); PUSHB((p) >> 8); } while (0)
#define POPPC() ({ u32 _h = POPB(); u32 _l = POPB(); ((_h << 8) | _l) & 0x3FFFu; })

AVR_FAST void ab_run(u32 cycles)
{
  static void *const T[O_N] = {
    &&nop, &&movw, &&muls, &&mulsu, &&fmul, &&fmuls, &&fmulsu, &&cpc, &&sbc, &&add, &&cpse, &&cp, &&sub, &&adc, &&and_,
    &&eor, &&or_, &&mov, &&cpi, &&sbci, &&subi, &&ori, &&andi, &&lddy, &&lddz, &&stdy, &&stdz, &&lds, &&ldzp, &&ldmz,
    &&lpmz, &&lpmzp, &&ldyp, &&ldmy, &&ldx, &&ldxp, &&ldmx, &&pop, &&sts, &&stzp, &&stmz, &&styp, &&stmy, &&stx, &&stxp,
    &&stmx, &&push, &&com, &&neg, &&swap, &&inc, &&asr, &&lsr, &&ror, &&dec, &&bset, &&bclr, &&ret, &&reti, &&sleep,
    &&wdr, &&lpm0, &&ijmp, &&icall, &&jmp, &&call, &&adiw, &&sbiw, &&cbi, &&sbic, &&sbi, &&sbis, &&mul, &&in, &&out,
    &&rjmp, &&rcall, &&ldi, &&bld, &&bst, &&sbrc, &&sbrs, &&insreg, &&outsreg,
    &&breq, &&brne, &&brcs, &&brcc, &&brmi, &&brpl, &&brvs, &&brvc, &&brlt, &&brge, &&brhs, &&brhc, &&brts, &&brtc,
    &&brie, &&brid, &&bad };
  if (!cycles) { g_T = T; return; }
  u8 *D = A_D; __asm__("" : "+r"(D));   /* one base register for the data space */
  u32 end = A_CYC + cycles, pc = g_pc, lim, sr, fc, fz, fn, fv, fh, o, d, a, b, v, res;
  s32 left;
  const u32 *ip;
  pins_upd();   /* the buttons */
  for (;;) {
    g_rst = 0; ev_sync(); if (g_rst) pc = 0;
    if ((D[SREG] & I_FLAG) && (v = irq_now())) {
      irq_ack(v);
      if (g_sleep) { g_sleep = 0; A_CYC += 4; }
      PUSHPC(pc); D[SREG] &= ~I_FLAG; pc = v * 2u; A_CYC += 5;
    }
    if ((s32)(A_CYC - end) >= 0) break;
    lim = ev_next(); lim = lim < end - A_CYC ? A_CYC + lim : end;
    if (g_sleep) { A_CYC = lim; continue; }
    left = (s32)(lim - A_CYC);
    v = D[SREG]; sr = v & 0xC0u; fc = v & 1u; fz = (v & 2u) ? 0u : 1u; fn = (v & 4u) << 5; fv = (v & 8u) << 4; fh = (v & 32u) >> 1;
    ip = A_OP + pc;
#define SRG() ((sr & 0xC0u) | fc | (fz ? 0u : 2u) | ((fn >> 5) & 4u) | ((fv >> 4) & 8u) | (((fn ^ fv) >> 3) & 16u) | ((fh << 1) & 32u))
#define SRSET(x) do { u32 _x = (x); sr = _x & 0xC0u; fc = _x & 1u; fz = (_x & 2u) ? 0u : 1u; fn = (_x & 4u) << 5; fv = (_x & 8u) << 4; fh = (_x & 32u) >> 1; } while (0)
#define CYC() (lim - (u32)left)
#define SAVE() do { A_CYC = CYC(); } while (0)
#define BREAK_AFTER() do { lim -= (u32)left; left = 0; } while (0)          /* leave after this instruction */
#define ONE_MORE(n) do { lim = lim - (u32)left + (n) + 1u; left = (s32)(n) + 1; } while (0)
#if AB_ADDR
#define GO() goto *(void *)(uintptr_t)(o & 0xFFFFu)
#else
#define GO() goto *T[o & 255u]
#endif
#ifdef AB_COUNT
#define CNT A_INS++; A_OPC[(AB_ADDR ? 0 : o & 255u)]++; A_PCH[PCNOW & 0x3FFFu]++;
#else
#define CNT
#endif
#define NEXT(n) do { CNT left -= (s32)(n); if (left <= 0) goto leave; o = *ip; GO(); } while (0)
/* straight on: the event check waits for the next jump (a block of straight code is short) */
#ifdef AB_SINGLE
#define STEP(n) do { ip++; NEXT(n); } while (0)   /* tests: one instruction per ab_run(1) */
#else
#define STEP(n) do { CNT left -= (s32)(n); o = *++ip; GO(); } while (0)
#endif
#define D5 ((o >> 16) & 31u)
#define R5 ((o >> 21) & 31u)
#define KH (o >> 24)
#define T14 (o >> 16)
#define RD_DATA(ad, dst) do { u32 _a = (ad); if (_a >= 0x100u) dst = D[_a < 0x1000u ? _a : 0xFFFu];   /* as the fast core: beyond the 4 KB on its last byte */ else if (_a < 0x20u) dst = D[_a]; else if (_a == SREG) dst = SRG(); else { SAVE(); dst = io_rd(_a); } } while (0)
#define WR_DATA(ad, val) do { u32 _a = (ad); if (_a >= 0x100u) { D[_a < 0x1000u ? _a : 0xFFFu] = (u8)(val); if (_a < SP_GET() || _a > g_anc_sp + 32u) g_dirty = 1; } \
    else if (_a < 0x20u) D[_a] = (u8)(val); else if (_a == SREG) { u32 _v = (val); if ((_v & ~sr & I_FLAG) && irq_now()) ONE_MORE(2u); SRSET(_v); } \
    else { SAVE(); if (io_wr(_a, (val))) BREAK_AFTER(); } } while (0)
#define JUMP(t) (ip = A_OP + (t))
#define PCNOW ((u32)(ip - A_OP))
    o = *ip; GO();
  nop: STEP(1);
  movw: d = D5; a = R5; D[d] = D[a]; D[d + 1] = D[a + 1]; STEP(1);
  muls: res = (u32)((s32)(int8_t)R(D5) * (s32)(int8_t)R(R5)) & 0xFFFFu; goto mulf;
  mulsu: res = (u32)((s32)(int8_t)R(D5) * (s32)R(R5)) & 0xFFFFu; goto mulf;
  mul: res = (u32)R(D5) * R(R5);
  mulf: D[0] = (u8)res; D[1] = (u8)(res >> 8); fc = res >> 15; fz = res; STEP(2);
  fmul: res = (u32)R(D5) * R(R5); goto fmulf;
  fmuls: res = (u32)((s32)(int8_t)R(D5) * (s32)(int8_t)R(R5)) & 0xFFFFu; goto fmulf;
  fmulsu: res = (u32)((s32)(int8_t)R(D5) * (s32)R(R5)) & 0xFFFFu;
  fmulf: fc = res >> 15; res = (res << 1) & 0xFFFFu; fz = res; D[0] = (u8)res; D[1] = (u8)(res >> 8); STEP(2);
#define ADDF(a_, b_, r_) do { fc = (r_) >> 8; fz = (r_) & 0xFFu; fn = (r_); fv = ((a_) ^ (r_)) & ((b_) ^ (r_)); fh = (a_) ^ (b_) ^ (r_); } while (0)
#define SUBF(a_, b_, r_) do { fc = ((r_) >> 8) & 1u; fz = (r_) & 0xFFu; fn = (r_); fv = ((a_) ^ (b_)) & ((a_) ^ (r_)); fh = (a_) ^ (b_) ^ (r_); } while (0)
#define SBCF(a_, b_, r_) do { fc = ((r_) >> 8) & 1u; fz |= (r_) & 0xFFu; fn = (r_); fv = ((a_) ^ (b_)) & ((a_) ^ (r_)); fh = (a_) ^ (b_) ^ (r_); } while (0)
  add: d = D5; a = R(d); b = R(R5); res = a + b; D[d] = (u8)res; ADDF(a, b, res); STEP(1);
  adc: d = D5; a = R(d); b = R(R5); res = a + b + fc; D[d] = (u8)res; ADDF(a, b, res); STEP(1);
  sub: d = D5; a = R(d); b = R(R5); res = a - b; D[d] = (u8)res; SUBF(a, b, res); STEP(1);
  sbc: d = D5; a = R(d); b = R(R5); res = a - b - fc; D[d] = (u8)res; SBCF(a, b, res); STEP(1);
  cp: a = R(D5); b = R(R5); res = a - b; SUBF(a, b, res); STEP(1);
  cpc: a = R(D5); b = R(R5); res = a - b - fc; SBCF(a, b, res); STEP(1);
  cpi: a = R(D5); b = KH; res = a - b; SUBF(a, b, res); STEP(1);
  subi: d = D5; a = R(d); b = KH; res = a - b; D[d] = (u8)res; SUBF(a, b, res); STEP(1);
  sbci: d = D5; a = R(d); b = KH; res = a - b - fc; D[d] = (u8)res; SBCF(a, b, res); STEP(1);
  cpse: if (R(D5) == R(R5)) { a = (o >> 26) & 1u; ip += 2u + a; NEXT(2u + a); } STEP(1);
  and_: d = D5; res = R(d) & R(R5); goto logic;
  eor: d = D5; res = R(d) ^ R(R5); goto logic;
  or_: d = D5; res = R(d) | R(R5); goto logic;
  andi: d = D5; res = R(d) & KH; goto logic;
  ori: d = D5; res = R(d) | KH;
  logic: D[d] = (u8)res; fz = res; fn = res; fv = 0; STEP(1);
  mov: D[D5] = D[R5]; STEP(1);
  ldi: D[D5] = (u8)KH; STEP(1);
  lddy: a = RW(28) + KH; goto ldd;
  lddz: a = RW(30) + KH;
  ldd: RD_DATA(a & 0xFFFFu, v); D[D5] = (u8)v; STEP(2);
  stdy: a = RW(28) + KH; goto std_;
  stdz: a = RW(30) + KH;
  std_: v = D[D5]; WR_DATA(a & 0xFFFFu, v); STEP(2);
  lds: a = ip[1] >> 16; RD_DATA(a, v); D[D5] = (u8)v; ip += 2; NEXT(2);
  sts: a = ip[1] >> 16; v = D[D5]; WR_DATA(a, v); ip += 2; NEXT(2);
#define PTR_LD(rp, pre, post) do { a = RW(rp); if (pre) a = (a - 1u) & 0xFFFFu; RD_DATA(a, v); if (post) a = (a + 1u) & 0xFFFFu; if ((pre) || (post)) { D[rp] = (u8)a; D[rp + 1] = (u8)(a >> 8); } D[D5] = (u8)v; } while (0)
#define PTR_ST(rp, pre, post) do { a = RW(rp); if (pre) a = (a - 1u) & 0xFFFFu; v = D[D5]; WR_DATA(a, v); if (post) a = (a + 1u) & 0xFFFFu; if ((pre) || (post)) { D[rp] = (u8)a; D[rp + 1] = (u8)(a >> 8); } } while (0)
  ldzp: PTR_LD(30, 0, 1); STEP(2);
  ldmz: PTR_LD(30, 1, 0); STEP(2);
  ldyp: PTR_LD(28, 0, 1); STEP(2);
  ldmy: PTR_LD(28, 1, 0); STEP(2);
  ldx: PTR_LD(26, 0, 0); STEP(2);
  ldxp: PTR_LD(26, 0, 1); STEP(2);
  ldmx: PTR_LD(26, 1, 0); STEP(2);
  stzp: PTR_ST(30, 0, 1); STEP(2);
  stmz: PTR_ST(30, 1, 0); STEP(2);
  styp: PTR_ST(28, 0, 1); STEP(2);
  stmy: PTR_ST(28, 1, 0); STEP(2);
  stx: PTR_ST(26, 0, 0); STEP(2);
  stxp: PTR_ST(26, 0, 1); STEP(2);
  stmx: PTR_ST(26, 1, 0); STEP(2);
  lpmz: a = RW(30); D[D5] = (u8)(A_FLASH[(a >> 1) & 0x3FFFu] >> ((a & 1u) * 8u)); STEP(3);
  lpmzp: a = RW(30); D[D5] = (u8)(A_FLASH[(a >> 1) & 0x3FFFu] >> ((a & 1u) * 8u)); a = (a + 1u) & 0xFFFFu; D[30] = (u8)a; D[31] = (u8)(a >> 8); STEP(3);
  lpm0: a = RW(30); D[0] = (u8)(A_FLASH[(a >> 1) & 0x3FFFu] >> ((a & 1u) * 8u)); STEP(3);
  push: PUSHB(D[D5]); STEP(2);
  pop: D[D5] = (u8)POPB(); STEP(2);
  com: d = D5; res = ~R(d) & 0xFFu; D[d] = (u8)res; fc = 1; fv = 0; fn = fz = res; STEP(1);
  neg: d = D5; a = R(d); res = 0u - a; D[d] = (u8)res; SUBF(0u, a, res); STEP(1);
  swap: d = D5; a = R(d); D[d] = (u8)((a >> 4) | (a << 4)); STEP(1);
  inc: d = D5; a = R(d); res = (a + 1u) & 0xFFu; D[d] = (u8)res; fv = res & ~a; fn = fz = res; STEP(1);
  dec: d = D5; a = R(d); res = (a - 1u) & 0xFFu; D[d] = (u8)res; fv = a & ~res; fn = fz = res; STEP(1);
  asr: d = D5; a = R(d); res = (a >> 1) | (a & 0x80u); goto shf;
  lsr: d = D5; a = R(d); res = a >> 1; goto shf;
  ror: d = D5; a = R(d); res = (a >> 1) | (fc << 7);
  shf: D[d] = (u8)res; fc = a & 1u; fn = fz = res; fv = res ^ (fc << 7); STEP(1);
  bset: v = SRG(); a = (o >> 16) & 255u; if ((a & ~v & I_FLAG) && irq_now()) ONE_MORE(1u); SRSET(v | a); STEP(1);
  bclr: v = SRG(); SRSET(v & ~(o >> 16)); STEP(1);
  bst: sr = (sr & ~T_F) | ((D[D5] & KH) ? T_F : 0u); STEP(1);
  bld: d = D5; if (sr & T_F) D[d] |= (u8)KH; else D[d] &= (u8)~KH; STEP(1);
  ret: JUMP(POPPC()); NEXT(4);
  reti: JUMP(POPPC()); sr |= I_FLAG; if (irq_now()) ONE_MORE(4u); NEXT(4);
  sleep: ip++; if (D[SMCR] & 1u) { g_sleep = 1; BREAK_AFTER(); } NEXT(1);
  wdr: if (WDT_ON) { SAVE(); wdt_restart(); } STEP(1);
  ijmp: JUMP(RW(30) & 0x3FFFu); NEXT(2);
  icall: a = PCNOW + 1u; PUSHPC(a); JUMP(RW(30) & 0x3FFFu); NEXT(3);
  jmp: JUMP(T14); NEXT(3);
  call: a = PCNOW + 2u; PUSHPC(a); JUMP(T14); NEXT(4);
  rjmp: JUMP(T14); NEXT(2);
  rcall: a = PCNOW + 1u; PUSHPC(a); JUMP(T14); NEXT(3);
  adiw: d = D5; a = RW(d); res = (a + KH) & 0xFFFFu; D[d] = (u8)res; D[d + 1] = (u8)(res >> 8);
    fz = res; fn = res >> 8; fc = ((a & ~res) >> 15) & 1u; fv = (res & ~a) >> 8; STEP(2);
  sbiw: d = D5; a = RW(d); res = (a - KH) & 0xFFFFu; D[d] = (u8)res; D[d + 1] = (u8)(res >> 8);
    fz = res; fn = res >> 8; fc = ((res & ~a) >> 15) & 1u; fv = (a & ~res) >> 8; STEP(2);
#define IOA (((o >> 24) & 31u) + 0x20u)
#define IOM ((o >> 16) & 255u)
#define IS_PIN(a_) ((a_) >= PINB && (a_) <= PORTF && ((a_) - PINB) % 3u == 0u)
#define IS_FLAG(a_) ((a_) >= TIFR0 && (a_) <= TIFR4)
  cbi: a = IOA; SAVE();
    if (!IS_PIN(a) && !IS_FLAG(a)) { v = io_rd(a) & ~IOM; if (io_wr(a, v)) BREAK_AFTER(); }
    STEP(2);
  sbi: a = IOA; SAVE();
    v = (IS_PIN(a) || IS_FLAG(a)) ? IOM : io_rd(a) | IOM;
    if (io_wr(a, v)) BREAK_AFTER();
    STEP(2);
  sbic: a = IOA; SAVE(); v = io_rd(a); if (!(v & IOM)) { a = (o >> 29) & 1u; ip += 2u + a; NEXT(2u + a); } STEP(1);
  sbis: a = IOA; SAVE(); v = io_rd(a); if (v & IOM) { a = (o >> 29) & 1u; ip += 2u + a; NEXT(2u + a); } STEP(1);
  sbrc: if (!(D[D5] & KH)) { a = (o >> 21) & 1u; ip += 2u + a; NEXT(2u + a); } STEP(1);
  sbrs: if (D[D5] & KH) { a = (o >> 21) & 1u; ip += 2u + a; NEXT(2u + a); } STEP(1);
  in: SAVE(); D[D5] = io_rd(KH); STEP(1);
  out: SAVE(); if (io_wr(KH, D[D5])) BREAK_AFTER(); STEP(1);
  outsreg: v = D[D5]; if ((v & ~sr & I_FLAG) && irq_now()) ONE_MORE(1u); SRSET(v); STEP(1);
  insreg: D[D5] = (u8)SRG();
    if (AB_SPIN) {   /* busy waiting: see g_anc_pc */
      u32 sp = SP_GET(), pcn = PCNOW;
      if (pcn == g_anc_pc && sp == g_anc_sp) {
        if (!g_dirty) {
          u32 cyc = CYC(), to = lim; if (g_tread && to - cyc > 512u) to = cyc + 512u;
          if ((s32)(to - cyc) > 1) left = (s32)(lim - to) + 1;   /* after this instruction the time is `to` */
        }
        g_dirty = 0; g_tread = 0;
      } else if (g_dirty) { g_anc_pc = pcn; g_anc_sp = sp; g_dirty = 0; g_tread = 0; }
    }
    STEP(1);
  breq: if (!fz) { JUMP(T14); NEXT(2); } STEP(1);
  brne: if (fz) { JUMP(T14); NEXT(2); } STEP(1);
  brcs: if (fc) { JUMP(T14); NEXT(2); } STEP(1);
  brcc: if (!fc) { JUMP(T14); NEXT(2); } STEP(1);
  brmi: if (fn & 0x80u) { JUMP(T14); NEXT(2); } STEP(1);
  brpl: if (!(fn & 0x80u)) { JUMP(T14); NEXT(2); } STEP(1);
  brvs: if (fv & 0x80u) { JUMP(T14); NEXT(2); } STEP(1);
  brvc: if (!(fv & 0x80u)) { JUMP(T14); NEXT(2); } STEP(1);
  brlt: if ((fn ^ fv) & 0x80u) { JUMP(T14); NEXT(2); } STEP(1);
  brge: if (!((fn ^ fv) & 0x80u)) { JUMP(T14); NEXT(2); } STEP(1);
  brhs: if (fh & 0x10u) { JUMP(T14); NEXT(2); } STEP(1);
  brhc: if (!(fh & 0x10u)) { JUMP(T14); NEXT(2); } STEP(1);
  brts: if (sr & T_F) { JUMP(T14); NEXT(2); } STEP(1);
  brtc: if (!(sr & T_F)) { JUMP(T14); NEXT(2); } STEP(1);
  brie: if (sr & I_FLAG) { JUMP(T14); NEXT(2); } STEP(1);
  brid: if (!(sr & I_FLAG)) { JUMP(T14); NEXT(2); } STEP(1);
  bad: JUMP((PCNOW + 1u) & 0x3FFFu); NEXT(1);
  leave:
    A_CYC = CYC(); D[SREG] = (u8)SRG(); pc = PCNOW;
  }
  g_pc = pc;
}
#undef SAVE

#endif
/* ------------------------------------------------------------------ sound */
u32 ab_tone(u32 cycles)
{
  u32 t = g_tog; g_tog = 0;
  /* timer 3 toggling OC3A (PC6) in CTC mode */
  if ((A_D[TCCR3A] & 0xC0u) == 0x40u && (A_D[DDRC] & 0x40u) && g_t[2].sh != STOP)
    return (16000000u / (2u << g_t[2].sh)) / (g_t[2].top + 1u);
  /* timer 4 toggling OC4A (PC7) */
  if ((A_D[TCCR4A] & 0xC2u) == 0x40u && (A_D[DDRC] & 0x80u) && g_t[3].sh != STOP)   /* not PWM4A: there 01 is PWM on both pins */
    return (16000000u / (2u << g_t[3].sh)) * g_t[3].mul / (g_t[3].top + 1u);
#if AB_SND2
  u32 e = g_pe; g_pe = 0;
  if (t >= 3u && g_tt1 != g_tt0) return (t - 1u) * 8000000u / (g_tt1 - g_tt0);   /* two changes per period */
  if (e >= 2u && e < 250u && g_pt1 != g_pt0 && (A_D[TCCR4A] & 0xC2u) > 0x02u) return (e - 1u) * 16000000u / (g_pt1 - g_pt0);
  return 0;
#else
  if (t < 4u || !cycles) return 0;
  cycles /= 1000u; if (!cycles) return 0;
  return t * 8000u / cycles;
#endif
}

/* ------------------------------------------------------------------ state */
#define ST_ID 0x31534241u   /* "ABS1" */
u32 ab_state_save(u8 *buf, u32 size)
{
  struct { void *p; u32 n; } P[] = { { A_D, AVR_DATA }, { A_GD, 1024 }, { &A_LCD, sizeof A_LCD }, { g_t, sizeof g_t },
    { &g_pc, 4 }, { &A_CYC, 4 }, { &g_col, 4 }, { &g_page, 4 }, { &g_c0, 4 }, { &g_c1, 4 }, { &g_p0, 4 }, { &g_p1, 4 },
    { &g_amode, 4 }, { &g_cmd_n, 4 }, { g_cmd, sizeof g_cmd }, { &g_lfsr, 4 }, { &g_wdt_at, 4 }, { &g_sleep, 4 } };
  u32 n = 4; if (size < 4) return 0;
  buf[0] = 'A'; buf[1] = 'B'; buf[2] = 'S'; buf[3] = '1';
  for (u32 i = 0; i < sizeof P / sizeof P[0]; i++) { if (n + P[i].n > size) return 0; for (u32 k = 0; k < P[i].n; k++) buf[n + k] = ((u8 *)P[i].p)[k]; n += P[i].n; }
  return n;
}
int ab_state_load(const u8 *buf, u32 size)
{
  struct { void *p; u32 n; } P[] = { { A_D, AVR_DATA }, { A_GD, 1024 }, { &A_LCD, sizeof A_LCD }, { g_t, sizeof g_t },
    { &g_pc, 4 }, { &A_CYC, 4 }, { &g_col, 4 }, { &g_page, 4 }, { &g_c0, 4 }, { &g_c1, 4 }, { &g_p0, 4 }, { &g_p1, 4 },
    { &g_amode, 4 }, { &g_cmd_n, 4 }, { g_cmd, sizeof g_cmd }, { &g_lfsr, 4 }, { &g_wdt_at, 4 }, { &g_sleep, 4 } };
  u32 n = 4, tot = 4;
  for (u32 i = 0; i < sizeof P / sizeof P[0]; i++) tot += P[i].n;
  if (size < tot || buf[0] != 'A' || buf[1] != 'B' || buf[2] != 'S' || buf[3] != '1') return 0;
  for (u32 i = 0; i < sizeof P / sizeof P[0]; i++) { for (u32 k = 0; k < P[i].n; k++) ((u8 *)P[i].p)[k] = buf[n + k]; n += P[i].n; }
  g_tv = 0; pins_upd();
  return 1;
}
#if !AB_JIT
u8 *ab_scratch(void) { static u8 b[AB_SCRATCH] __attribute__((aligned(4))); return b; }
#endif
u32 ab_pc(void) { return g_pc; }
void ab_set_pc(u32 p) { g_pc = p; }
void ab_wake(void) { g_sleep = 0; }
u32 ab_tm(int i, int c) { return c < 4 ? g_t[i].ocr[c] : c == 4 ? g_t[i].top : g_t[i].sh; }
#if AB_JIT
#include "jit.inc"
#endif
