/* zexdoc / zexall on the SMSEmu Z80 (CP/M: BDOS print at 5, end at 0). Test only. */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
static uint8_t mem[65536]; static int done;
struct Z80;
#define Z_RD(a) mem[a]
#define Z_WR(a, v) (mem[a] = (v))
#define Z_IN(p) 0xFF
static void bdos(void);
#define Z_OUT(p, v) do { if (((p) & 0xFF) == 0) bdos(); else done = 1; } while (0)
#include "../src/core/z80.h"
static Z80 cpu;
static void bdos(void)
{
  Z80 *z = &cpu;
  if (z->bc.b.l == 2) putchar(z->de.b.l);
  else if (z->bc.b.l == 9) for (uint16_t a = z->de.w; mem[a] != '$'; a++) putchar(mem[a]);
  fflush(stdout);
}
int main(int argc, char **argv)
{
  FILE *f = fopen(argv[1], "rb"); size_t n = fread(mem + 0x100, 1, 0xFE00, f); fclose(f); (void)n;
  mem[0] = 0xD3; mem[1] = 0x01;                 /* OUT (1),A: end */
  mem[5] = 0xD3; mem[6] = 0x00; mem[7] = 0xC9;  /* OUT (0),A; RET */
  z80_tables(); z80_reset(&cpu); cpu.pc = 0x100; cpu.sp.w = 0xF000;
  unsigned long long cyc = 0;
  while (!done) { z80_run(&cpu, 100000); cyc += 100000; }
  printf("\ncycles %llu\n", cyc);
  return 0;
}
