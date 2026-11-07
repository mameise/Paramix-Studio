#include "../../src/core/avr.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static u16 fl[AVR_FLASH_WORDS];
int main(int argc, char **argv)
{
  FILE *f = fopen(argv[1], "rb"); memset(fl, 0xFF, sizeof fl); size_t n = fread(fl, 1, sizeof fl, f); fclose(f);
  ab_load(fl, (n + 1) / 2); ab_reset();
  for (int ms = 0; ms < 30; ms++) ab_run(16000);
  for (int i = 0; i < 96; i++) printf("%02x", A_D[i]); printf("\n");
  for (int i = 0x100; i < 0x140; i++) printf("%02x", A_D[i]); printf("\n");
}
