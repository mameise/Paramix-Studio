/* PC test of the Paramix menu: cc -DHOST host.c main.c -o menu_host
 * menu_host <flash_in.bin> <flash_out.bin> <image_folder> "<sequence>"
 * Sequence: n (wheel forward), p (wheel back), a, b, c. One PPM image after each screen update. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>
typedef unsigned int u32; typedef unsigned short u16; typedef unsigned char u8;
unsigned char *host_flash;
static const char *g_seq; static int g_pos; static const char *g_dir; static int g_frame; static const char *g_out;
static jmp_buf g_jb;
void hw_init(void){}
void hw_wdt_feed(void){}
u32 hw_buttons(void){ return 0; }
void hw_push(const u16* fb, u32 stride, u32 x0, u32 y0, u32 x1, u32 y1){
  (void)stride;(void)x0;(void)y0;(void)x1;(void)y1;
  char p[512]; snprintf(p,sizeof p,"%s/%03d.ppm",g_dir,g_frame++);
  FILE *f=fopen(p,"wb"); fprintf(f,"P6 128 128 255\n");
  for(int i=0;i<128*128;i++){ u16 c=fb[i]; u8 r=(u8)((c>>11)<<3), g=(u8)(((c>>5)&63)<<2), b=(u8)((c&31)<<3); fputc(r,f); fputc(g,f); fputc(b,f); }
  fclose(f);
}
void hw_back_to_paradise(void){ printf("BACK TO PARADISE\n"); longjmp(g_jb,1); }
void host_exit(const char *why){ printf("ENDE %s\n",why); longjmp(g_jb,1); }
void hw_tone(u32 hz){(void)hz;} void hw_backlight(int on){(void)on;} void hw_bl_pwm_off(void){}
void flash_prog4k(u32 addr, const void *src){ memcpy(host_flash+(addr&0xFFFFFF),src,0x1000); }
void flash_erase4k(u32 addr){ memset(host_flash+(addr&0xFFFFFF),0xFF,0x1000); }
void flash_read(u32 addr, void *dst, u32 len){ memcpy(dst,host_flash+(addr&0xFFFFFF),len); }
int flash_same(u32 addr, const void *src, u32 len){ return !memcmp(host_flash+(addr&0xFFFFFF),src,len); }
int flash_update4k(u32 addr, const void *src){ if(flash_same(addr,src,0x1000)) return 0; flash_prog4k(addr,src); return 1; }
int host_input(int *step){
  for(;;){
    char c=g_seq[g_pos]; if(!c){ printf("FOLGE ZU ENDE\n"); longjmp(g_jb,1); }
    g_pos++;
    switch(c){ case 'n': *step=1; return 1; case 'p': *step=-1; return 1; case 'a': return 10; case 'b': return 2; case 'c': return 3; default: break; }
  }
}
void host_main(void);
int main(int argc,char**argv){
  if(argc<5){ fprintf(stderr,"menu_host in out dir folge\n"); return 1; }
  host_flash=malloc(0x1000000); FILE *f=fopen(argv[1],"rb"); fread(host_flash,1,0x1000000,f); fclose(f);
  g_out=argv[2]; g_dir=argv[3]; g_seq=argv[4];
  if(!setjmp(g_jb)) host_main();
  f=fopen(g_out,"wb"); fwrite(host_flash,1,0x1000000,f); fclose(f);
  printf("%d Bilder\n",g_frame); return 0;
}
