#include <stddef.h>
#ifdef LIBC_WORD
/* 1.7.3 test21: small (-Os) with a word loop when aligned, instead of the unrolled byte loops of -O2 (room in TamaEmu) */
void *memset(void *d, int c, size_t n){ unsigned char *p=d; unsigned w=(unsigned char)c*0x01010101u;
  if(!((unsigned)p&3u)) for(;n>=4;n-=4,p+=4) *(volatile unsigned*)p=w;
  while(n--) *p++=(unsigned char)c; return d; }
void *memcpy(void *d, const void *s, size_t n){ unsigned char *p=d; const unsigned char *q=s;
  if(!(((unsigned)p|(unsigned)q)&3u)) for(;n>=4;n-=4,p+=4,q+=4) *(volatile unsigned*)p=*(const unsigned*)q;
  while(n--) *p++=*q++; return d; }
#else
void *memset(void *d, int c, size_t n){ unsigned char *p=d; while(n--) *p++=(unsigned char)c; return d; }
void *memcpy(void *d, const void *s, size_t n){ unsigned char *p=d; const unsigned char *q=s; while(n--) *p++=*q++; return d; }
#endif
void *memmove(void *d, const void *s, size_t n){ unsigned char *p=d; const unsigned char *q=s;
  if(p<q){ while(n--) *p++=*q++; } else { p+=n; q+=n; while(n--) *--p=*--q; } return d; }
int memcmp(const void *a, const void *b, size_t n){ const unsigned char *p=a,*q=b; for(;n;n--,p++,q++) if(*p!=*q) return *p-*q; return 0; }
#ifdef LIBC_ALIAS
/* test23: one copy routine instead of four (each wrapper had its own inlined copy loop) */
void __aeabi_memcpy(void *d, const void *s, size_t n) __attribute__((alias("memcpy")));
void __aeabi_memcpy4(void *d, const void *s, size_t n) __attribute__((alias("memcpy")));
void __aeabi_memcpy8(void *d, const void *s, size_t n) __attribute__((alias("memcpy")));
#else
void __aeabi_memcpy(void *d, const void *s, size_t n){ memcpy(d,s,n); }
void __aeabi_memcpy4(void *d, const void *s, size_t n){ memcpy(d,s,n); }
void __aeabi_memcpy8(void *d, const void *s, size_t n){ memcpy(d,s,n); }
#endif
void __aeabi_memset(void *d, size_t n, int c){ memset(d,c,n); }
void __aeabi_memset4(void *d, size_t n, int c){ memset(d,c,n); }
void __aeabi_memclr(void *d, size_t n){ memset(d,0,n); }
void __aeabi_memclr4(void *d, size_t n){ memset(d,0,n); }
void __aeabi_memclr8(void *d, size_t n){ memset(d,0,n); }
