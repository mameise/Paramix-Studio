/* PC histogram of the V7 firmware (host). pcprof <rom> <setup.txt-like keys file> <secs> */
#include "../src/core/t7_internal.h"
#include <stdio.h>
void host_set_rom(unsigned char *p, size_t n);
static unsigned char *rd(const char *f, size_t *n){ FILE*x=fopen(f,"rb"); fseek(x,0,2); *n=ftell(x); fseek(x,0,0); unsigned char*p=malloc(*n); if(fread(p,1,*n,x)){} fclose(x); return p; }
static unsigned cnt[65536];
int main(int c,char**v){ size_t n; unsigned char *rm=rd(v[1],&n); host_set_rom(rm,n); if(!t7_init()) return 1;
  double secs=atof(v[2]); long total=(long)(secs*4000000), done=0; long ins=0;
  /* after 5 s, count */
  while(done<total){ uint32_t s=t7_step_cycles(4000); done+=s; }
  for(long k=0;k<(long)(2*4000000);){ uint16_t pc=g_cpu.pc; cnt[pc]++; ins++; uint32_t cy=t7_step_one(); if(!cy) cy=1; k+=cy;
    static uint32_t acc; acc+=cy; if(acc>=66666){acc-=66666; t7_fire_irq(10); t7_fire_nmi(6);} }
  printf("instr in 2 s: %ld (%.2f M/s)\n", ins, ins/2e6);
  for(int r=0;r<15;r++){ int b=0; for(int i=1;i<65536;i++) if(cnt[i]>cnt[b]) b=i; printf("%04x %u bank %d\n",b,cnt[b],g_rom_bank_id); cnt[b]=0; }
}
