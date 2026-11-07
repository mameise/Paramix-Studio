/* lockstep against simavr on a game: gcc -O1 -DAB_SINGLE -o lock lock.c ../src/core/avr.c -lsimavr -lelf && ./lock game.hex steps
 * (interrupts and I/O reads differ between the two: the simulator is followed there) */
/* lockstep: game hex in our core and simavr, instruction by instruction */
#include <simavr/sim_avr.h>
#include <simavr/sim_core.h>
#include <simavr/sim_hex.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/core/avr.h"
extern u32 ab_pc(void); extern void ab_set_pc(u32); extern void ab_wake(void);
static u16 fl[AVR_FLASH_WORDS];
int main(int argc, char **argv)
{
  uint32_t sz, base; uint8_t *code = read_ihex_file(argv[1], &sz, &base);
  long steps = atol(argv[2]);
  memset(fl, 0xFF, sizeof fl); memcpy(fl, code, sz);
  avr_t *a = avr_make_mcu_by_name("atmega32u4"); a->frequency = 16000000; avr_init(a);
  avr_loadcode(a, code, sz, base);
  ab_load(fl, (sz + 1) / 2); ab_reset();
  long syncs = 0, irqs = 0; unsigned lastpc = 0;
  static char hist[64][160]; int hn = 0;
  for (long st = 0; st < steps; st++) {
    unsigned pc0 = a->pc / 2;
    u16 w = fl[pc0];
    unsigned spa = a->data[0x5D] | a->data[0x5E] << 8, spb = A_D[0x5D] | A_D[0x5E] << 8, ourpc = ab_pc();
    avr_run(a);
    ab_run(1);
    snprintf(hist[hn++ & 63], 160, "st %ld pc %04x/%04x op %04x sp %04x/%04x r24 %02x/%02x -> %04x/%04x sreg %02x", st, pc0, ourpc, w, spa, spb, a->data[24], A_D[24], a->pc/2, ab_pc(), A_D[0x5F]);
    unsigned p1 = a->pc / 2, p2 = ab_pc();
    int isld = (w & 0xF800) == 0xB000 || (w & 0xFE0F) == 0x9000 || (w & 0xFC00) == 0x9000 || (w & 0xD200) == 0x8000;
    if (p1 != p2) {
      int skio = (w & 0xFD00) == 0x9900; if (p1 < 0x56 || p2 < 0x56 || skio || p1 == pc0 || p2 == pc0) { /* one took an interrupt: follow the simulator */
        for (int i = 0; i < 32; i++) A_D[i] = a->data[i];
        for (int i = 0x100; i < 0xB00; i++) A_D[i] = a->data[i];
        A_D[0x5D] = a->data[0x5D]; A_D[0x5E] = a->data[0x5E];
        { unsigned s1 = 0; for (int i = 0; i < 8; i++) if (a->sreg[i]) s1 |= 1 << i; A_D[0x5F] = s1; }
        ab_wake(); ab_set_pc(p1); A_CYC = (u32)a->cycle; irqs++; continue;
      }
      for (int k = 0; k < 40; k++) puts(hist[(hn + 24 + k) & 63]);
      printf("step %ld: pc sim %04x ours %04x (from %04x op %04x) cyc sim %llu ours %u\n", st, p1, p2, pc0, w, (unsigned long long)a->cycle, A_CYC); break; }
    for (int i = 0; i < 32; i++) if (a->data[i] != A_D[i]) {
      if (isld) { A_D[i] = a->data[i]; syncs++; }
      else { for (int k = 0; k < 40; k++) puts(hist[(hn + 24 + k) & 63]); printf("step %ld pc %04x op %04x: r%d sim %02x ours %02x\n", st, pc0, w, i, a->data[i], A_D[i]); st = steps; break; }
    }
    if (0 && a->cycle != A_CYC) { printf("cycles step %ld pc %04x op %04x sim %llu ours %u\n", st, pc0, w, (unsigned long long)a->cycle, A_CYC); break; }
    if (0) { for (int k = 0; k < 40; k++) puts(hist[(hn + 24 + k) & 63]); printf("SP differs at step %ld\n", st); break; }
    lastpc = pc0;
  }
  printf("irqs %ld syncs %ld cyc sim %llu ours %u\n", irqs, syncs, (unsigned long long)a->cycle, A_CYC);
  return 0;
}
