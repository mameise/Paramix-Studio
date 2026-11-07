/* CPU check against simavr: random instruction runs (ALU, branches, skips), registers and SREG after each step.
 * gcc -O1 -DAB_SINGLE -o fuzz fuzz.c ../src/core/avr.c -lsimavr -lelf && ./fuzz 3000 */
/* random ALU sequences: our core against simavr */
#include <simavr/sim_avr.h>
#include <simavr/sim_core.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/core/avr.h"
extern u32 ab_pc(void);
static u16 fl[AVR_FLASH_WORDS];
static unsigned rr(void) { return (unsigned)rand(); }
static u16 gen(void)
{
  unsigned d = rr() & 31, r = rr() & 31, K = rr() & 255, d4 = rr() & 15, b = rr() & 7;
  switch (rr() % 40) {
    case 0: return 0x0C00 | (r & 16) << 5 | d << 4 | (r & 15);  /* ADD */
    case 1: return 0x1C00 | (r & 16) << 5 | d << 4 | (r & 15);  /* ADC */
    case 2: return 0x1800 | (r & 16) << 5 | d << 4 | (r & 15);  /* SUB */
    case 3: return 0x0800 | (r & 16) << 5 | d << 4 | (r & 15);  /* SBC */
    case 4: return 0x2000 | (r & 16) << 5 | d << 4 | (r & 15);  /* AND */
    case 5: return 0x2800 | (r & 16) << 5 | d << 4 | (r & 15);  /* OR */
    case 6: return 0x2400 | (r & 16) << 5 | d << 4 | (r & 15);  /* EOR */
    case 7: return 0x1400 | (r & 16) << 5 | d << 4 | (r & 15);  /* CP */
    case 8: return 0x0400 | (r & 16) << 5 | d << 4 | (r & 15);  /* CPC */
    case 9: return 0x2C00 | (r & 16) << 5 | d << 4 | (r & 15);  /* MOV */
    case 10: return 0x5000 | (K & 0xF0) << 4 | d4 << 4 | (K & 15); /* SUBI */
    case 11: return 0x4000 | (K & 0xF0) << 4 | d4 << 4 | (K & 15); /* SBCI */
    case 12: return 0x7000 | (K & 0xF0) << 4 | d4 << 4 | (K & 15); /* ANDI */
    case 13: return 0x6000 | (K & 0xF0) << 4 | d4 << 4 | (K & 15); /* ORI */
    case 14: return 0x3000 | (K & 0xF0) << 4 | d4 << 4 | (K & 15); /* CPI */
    case 15: return 0x9400 | d << 4 | 0; /* COM */
    case 16: return 0x9400 | d << 4 | 1; /* NEG */
    case 17: return 0x9400 | d << 4 | 2; /* SWAP */
    case 18: return 0x9400 | d << 4 | 3; /* INC */
    case 19: return 0x9400 | d << 4 | 5; /* ASR */
    case 20: return 0x9400 | d << 4 | 6; /* LSR */
    case 21: return 0x9400 | d << 4 | 7; /* ROR */
    case 22: return 0x9400 | d << 4 | 10; /* DEC */
    case 23: return 0x9C00 | (r & 16) << 5 | d << 4 | (r & 15); /* MUL */
    case 24: return 0x0200 | d4 << 4 | (rr() & 15); /* MULS */
    case 25: return 0x0300 | (rr() & 0x88) | (rr() & 0x77); /* MULSU FMUL* */
    case 26: return 0x9600 | (rr() & 0xFF); /* ADIW */
    case 27: return 0x9700 | (rr() & 0xFF); /* SBIW */
    case 28: return 0x0100 | (rr() & 0xFF); /* MOVW */
    case 29: return 0xFA00 | d << 4 | b; /* BST */
    case 30: return 0xF800 | d << 4 | b; /* BLD */
    case 31: { static const int B[4] = { 0, 1, 5, 6 }; return 0x9408 | B[rr() & 3] << 4 | ((rr() & 1) << 7); } /* BSET/BCLR of C Z H T */
    case 32: return 0xE000 | (K & 0xF0) << 4 | d4 << 4 | (K & 15); /* LDI */
    case 34: case 35: return 0xF000 | (rr() & 0x400) | (1 << 3) | (rr() & 7);   /* BRBS/BRBC .+1 */
    case 36: return 0xFC00 | (rr() & 0x200) | d << 4 | b;   /* SBRC/SBRS */
    case 37: return 0x1000 | (r & 16) << 5 | d << 4 | (r & 15);  /* CPSE */

    default: return 0x0000;
  }
}
int main(int argc, char **argv)
{
  int seeds = argc > 1 ? atoi(argv[1]) : 200, fails = 0;
  for (int sd = 1; sd <= seeds; sd++) {
    srand(sd);
    int n = 200;
    for (int i = 0; i < n; i++) { fl[i] = gen(); if (fl[i] == 0x9478 || fl[i] == 0x94F8) fl[i] = 0; { unsigned o = fl[i] & 0xfc0f; if ((o == 0x940c || o == 0x940d || o == 0x940e || o == 0x940f || o == 0x9000 || o == 0x9200) && (fl[i] & 0xFE0F) != 0x9000) fl[i] = 0; } if ((fl[i] & 0xFE0F) == 0x9000 && i + 1 < n) { fl[++i] = 0x0100 + (rr() & 0x7FF); } }
    fl[n] = 0xCFFF;   /* rjmp . */
    avr_t *a = avr_make_mcu_by_name("atmega32u4"); avr_init(a);
    avr_loadcode(a, (uint8_t *)fl, (n + 1) * 2, 0);
    ab_load(fl, n + 1); ab_reset();
    unsigned sreg0 = rr() & 0x6F; sreg0 |= (((sreg0 >> 2) ^ (sreg0 >> 3)) & 1) << 4;
    for (int i = 0; i < 32; i++) { u8 v = rr(); a->data[i] = v; A_D[i] = v; }
    a->data[R_SREG] = sreg0; for (int i = 0; i < 8; i++) a->sreg[i] = (sreg0 >> i) & 1; A_D[0x5F] = sreg0;
    for (int i = 0x100; i < 0xB00; i++) { u8 v = rr(); a->data[i] = v; A_D[i] = v; }
    a->pc = 0;
    for (int st = 0; st < n; st++) {
      u16 w = fl[a->pc / 2];
      if (a->pc / 2 >= n) break;
      avr_run(a);
      ab_run(1);
      if (getenv("TR") && sd == atoi(getenv("TR"))) { unsigned s2 = 0; for (int i = 0; i < 8; i++) if (a->sreg[i]) s2 |= 1 << i; printf("  st %d op %04x sreg sim %02x ours %02x pc %x/%x cyc %llu/%u\n", st, w, s2, A_D[0x5F], a->pc/2, ab_pc(), (unsigned long long)a->cycle, A_CYC); }
      unsigned s1 = 0; for (int i = 0; i < 8; i++) if (a->sreg[i]) s1 |= 1 << i;
      int bad = s1 != A_D[0x5F] || a->pc / 2 != ab_pc(); for (int i = 0; i < 32; i++) if (a->data[i] != A_D[i]) bad = 1;
      if (bad) {
        printf("seed %d step %d at %x op %04x: sreg sim %02x ours %02x pc sim %x ours %x\n", sd, st, (unsigned)0, w, s1, A_D[0x5F], a->pc/2, ab_pc());
        for (int i = 0; i < 32; i++) if (a->data[i] != A_D[i]) printf("  r%d sim %02x ours %02x\n", i, a->data[i], A_D[i]);
 fails++; break;
      }
    }
    avr_terminate(a);
    if (fails > 10) break;
  }
  printf("fails %d\n", fails);
  return 0;
}
