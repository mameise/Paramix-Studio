/* Minimal C library for the WAD converter in the browser (WebAssembly) */
#include <stddef.h>
#include <stdarg.h>
extern unsigned char __heap_base;
static unsigned long heap_top;
void *malloc(size_t n){
  if(!heap_top) heap_top=(unsigned long)&__heap_base;
  heap_top=(heap_top+7)&~7ul;
  unsigned long p=heap_top; heap_top+=n;
  unsigned long need=(heap_top+65535)>>16, have=__builtin_wasm_memory_size(0);
  if(need>have && __builtin_wasm_memory_grow(0, need-have)==(size_t)-1) return 0;
  return (void*)p;
}
void free(void *p){ (void)p; }
void heap_reset(void){ heap_top=0; }
void *memcpy(void *d, const void *s, size_t n){ unsigned char *a=d; const unsigned char *b=s; while(n--) *a++=*b++; return d; }
void *memmove(void *d, const void *s, size_t n){ unsigned char *a=d; const unsigned char *b=s; if(a<b) while(n--) *a++=*b++; else { a+=n; b+=n; while(n--) *--a=*--b; } return d; }
void *memset(void *d, int c, size_t n){ unsigned char *a=d; while(n--) *a++=(unsigned char)c; return d; }
int memcmp(const void *x, const void *y, size_t n){ const unsigned char *a=x,*b=y; for(;n;n--,a++,b++) if(*a!=*b) return *a-*b; return 0; }
int strncmp(const char *a, const char *b, size_t n){ for(;n;n--,a++,b++){ if(*a!=*b) return (unsigned char)*a-(unsigned char)*b; if(!*a) return 0; } return 0; }
size_t strlen(const char *s){ size_t n=0; while(s[n]) n++; return n; }
char *strncpy(char *d, const char *s, size_t n){ size_t i=0; for(;i<n&&s[i];i++) d[i]=s[i]; for(;i<n;i++) d[i]=0; return d; }
int printf(const char *f, ...){ (void)f; return 0; }
int snprintf(char *b, size_t n, const char *f, ...){
  va_list ap; va_start(ap,f); size_t o=0;
#define PUT(c) do{ if(o+1<n) b[o]=(c); o++; }while(0)
  while(*f){
    if(*f!='%'){ PUT(*f++); continue; }
    f++; int zero=0, w=0; if(*f=='0'){ zero=1; f++; } while(*f>='0'&&*f<='9') w=w*10+(*f++-'0');
    char t=*f++; unsigned v; int neg=0; char tmp[16]; int k=0; unsigned base=10;
    if(t=='d'){ int x=va_arg(ap,int); if(x<0){neg=1; v=(unsigned)-x;} else v=(unsigned)x; }
    else if(t=='X'||t=='x'){ v=va_arg(ap,unsigned); base=16; }
    else if(t=='s'){ const char *s=va_arg(ap,const char*); while(*s) PUT(*s++); continue; }
    else { PUT(t); continue; }
    do{ unsigned dgt=v%base; tmp[k++]= dgt<10 ? '0'+dgt : (t=='X' ? 'A' : 'a')+dgt-10; v/=base; } while(v);
    if(neg) PUT('-');
    for(int p=k; p<w; p++) PUT(zero?'0':' ');
    while(k) PUT(tmp[--k]);
  }
  if(n) b[o<n?o:n-1]=0;
  va_end(ap); return (int)o;
}
