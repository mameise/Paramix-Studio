/* Host: boot the V7 ROM, press keys, save screenshots. hrun <rom> [figure]; stdin: p A|B|C [s] | w s | s file | q */
#include "../src/core/t7.h"
#include <stdio.h>
#include "../src/core/t7_internal.h"
void host_set_rom(unsigned char *p, size_t n);
void figure_rom_set_active(const unsigned char* p, unsigned int n);
static unsigned char *rd(const char *f, size_t *n){ FILE*x=fopen(f,"rb"); fseek(x,0,2); *n=ftell(x); fseek(x,0,0); unsigned char*p=malloc(*n); if(fread(p,1,*n,x)){} fclose(x); return p; }
static void frames(double s){ int n=(int)(s*100+0.5); for(int i=0;i<n;i++) t7_step_cycles(40000); }
static void shot(const char *f){ uint8_t d[48*31], ic[10]; t7_get_display(d); t7_get_icons(ic);
  static const int pal[4]={0xE0,0xA0,0x60,0x20}; FILE*o=fopen(f,"wb"); fprintf(o,"P6 192 140 255\n");
  for(int y=0;y<140;y++) for(int x=0;x<192;x++){ int v=0xF0;
    if(y>=8&&y<132){ int yy=(y-8)/4, xx=x/4; if(yy<31) v=pal[d[yy*48+xx]]; }
    else { int k=x/(192/5)+(y>=132?5:0); v=ic[k]?pal[ic[k]]:0xF0; }
    fputc(v,o); fputc(v,o); fputc(v,o);} fclose(o); }
int main(int c, char **v){ size_t n; unsigned char *rom=rd(v[1],&n); host_set_rom(rom,n);
  if(c>2){ size_t fn; unsigned char*f=rd(v[2],&fn); figure_rom_set_active(f,(unsigned)fn); t7_figure_set(1); }
  if(!t7_init()){ printf("init failed\n"); return 1; }
  char l[256]; while(fgets(l,sizeof l,stdin)){ char cmd[16],a[200]; double x=0; int k=sscanf(l,"%15s %199s %lf",cmd,a,&x); if(k<1) continue;
    if(!strcmp(cmd,"p")){ t7_set_buttons(a[0]=='A'?1:a[0]=='B'?2:4); frames(0.2); t7_set_buttons(0); frames(k>2?x:0.8); }
    else if(!strcmp(cmd,"w")) frames(atof(a));
    else if(!strcmp(cmd,"s")) shot(a);
    else if(!strcmp(cmd,"t")) printf("time %02d:%02d:%02d day %d.%d\n", t7_ram_read(0x306),t7_ram_read(0x307),t7_ram_read(t7_rtc_sec_addr()),t7_ram_read(0x305),t7_ram_read(0x304));
    else if(!strcmp(cmd,"poke")){ t7_ram_write((uint16_t)strtol(a,0,16),(uint8_t)x); }
    else if(!strcmp(cmd,"d")){ FILE*o=fopen(a,"wb"); fwrite(g_wram,1,T7_WRAM_SIZE,o); fclose(o); }
    else if(!strcmp(cmd,"q")) break; fflush(stdout); }
  return 0; }
