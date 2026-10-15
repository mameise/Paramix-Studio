/* Same key script on t7_cpu_step and on t7_cpu_run (build twice, -DT7_FASTRUN): prints a hash of
 * the whole state after every 2 ms step; the two outputs must be equal. runcmp <rom> <script> */
#include "../src/core/t7_internal.h"
#include "../src/core/t7_sound.h"
#include <stdio.h>
void host_set_rom(unsigned char *p, size_t n);
static unsigned char *rd(const char *f, size_t *n){ FILE*x=fopen(f,"rb"); fseek(x,0,2); *n=ftell(x); fseek(x,0,0); unsigned char*p=malloc(*n); if(fread(p,1,*n,x)){} fclose(x); return p; }
static uint32_t h(const uint8_t *p, int n, uint32_t s){ while(n--) s = s*16777619u ^ *p++; return s; }
int main(int c,char**v){ size_t n; unsigned char*rm=rd(v[1],&n); host_set_rom(rm,n); if(!t7_init()) return 1;
  if (c>3) t7_snd_set_mode(1);
  FILE*f=fopen(v[2],"r"); char l[128]; long step=0;
  while(fgets(l,sizeof l,f)){ char k=0; double s=0; if(sscanf(l,"%c %lf",&k,&s)<2) continue;
    t7_set_buttons(k=='A'?1:k=='B'?2:k=='C'?4:0);
    int m=(int)(s*500+0.5);
    for(int i=0;i<m;i++){ t7_step_cycles(8000); uint8_t buf[4096]; uint32_t sz=t7_serialize_state(buf,sizeof buf);
      uint32_t hh=h(buf,sz,2166136261u); printf("%ld %08x pc=%04x a=%02x x=%02x y=%02x s=%02x w=%08x d=%08x r=%08x\n",step++,hh,g_cpu.pc,g_cpu.a,g_cpu.x,g_cpu.y,g_cpu.s,h(g_wram,0x600,1),h(g_dram,0x200,1),h(g_cpureg,256,1)); } }
  return 0; }
