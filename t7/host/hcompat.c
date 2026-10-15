/* Host build of the Paradise T7 app glue: ROM from a file, EEPROM pages in memory. */
#include "../src/core/pebble_compat.h"
#include <stdarg.h>
static unsigned char *g_rom; static size_t g_rom_n;
static unsigned char g_pages[16][256]; static unsigned char g_has[16];
void host_set_rom(unsigned char *p, size_t n){ g_rom=p; g_rom_n=n; }
void tama_log(const char *fmt, ...){ if(!getenv("T7LOG")) return; va_list a; va_start(a,fmt); vfprintf(stderr,fmt,a); va_end(a); }
ResHandle resource_get_handle(int id){ (void)id; return g_rom?1:0; }
size_t resource_size(ResHandle h){ (void)h; return g_rom_n; }
size_t resource_load_byte_range(ResHandle h, size_t off, uint8_t *out, size_t len){ (void)h; if(off>=g_rom_n) return 0; if(off+len>g_rom_n) len=g_rom_n-off; memcpy(out,g_rom+off,len); return len; }
const uint8_t *t7_compat_rom_mapped(void){ return g_rom; }
bool persist_exists(uint32_t key){ return key>=100 && key<116 && g_has[key-100]; }
int persist_read_data(uint32_t key, void *buf, size_t n){ if(!persist_exists(key)) return -1; if(n>256) n=256; memcpy(buf,g_pages[key-100],n); return (int)n; }
status_t persist_write_data(uint32_t key, const void *buf, size_t n){ if(key<100||key>=116) return -1; if(n>256) n=256; memcpy(g_pages[key-100],buf,n); g_has[key-100]=1; return S_SUCCESS; }
status_t persist_delete(uint32_t key){ if(key>=100&&key<116) g_has[key-100]=0; return S_SUCCESS; }
void persist_flush_to_disk(void){}
int persist_page_count(void){ return 16; }
int persist_page_size(void){ return 256; }
int persist_backup_page(int p, uint8_t *o){ memcpy(o,g_pages[p],256); return g_has[p]; }
void persist_restore_page(int p, const uint8_t *i){ memcpy(g_pages[p],i,256); g_has[p]=1; }
void t7_compat_set_rom_path(const char *p){ (void)p; }
void t7_compat_set_eeprom_path(const char *p){ (void)p; }
