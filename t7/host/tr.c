#include "../src/core/t7_internal.h"
#include <stdio.h>
void host_set_rom(unsigned char *p, size_t n); uint8_t t7_dbg_p(void); uint32_t t7_cpu_run(uint32_t);
static unsigned char *rd(const char *f, size_t *n){ FILE*x=fopen(f,"rb"); fseek(x,0,2); *n=ftell(x); fseek(x,0,0); unsigned char*p=malloc(*n); if(fread(p,1,*n,x)){} fclose(x); return p; }
int main(int c,char**v){ size_t n; unsigned char*rm=rd(v[1],&n); host_set_rom(rm,n); t7_init(); long N=atol(v[2]);
  for(long i=0;i<N;i++){
#ifdef T7_FASTRUN
    uint32_t cy=t7_cpu_run(1);
#else
    uint32_t cy=t7_cpu_step();
#endif
    printf("%ld %04x %02x %02x %02x %02x %02x %u m48=%02x %02x\n",i,g_cpu.pc,g_cpu.a,g_cpu.x,g_cpu.y,g_cpu.s,t7_dbg_p(),cy,t7_read_page[0][0x48],t7_read_page[0][0x49]); }
}
