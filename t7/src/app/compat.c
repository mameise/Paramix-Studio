/* The platform functions the V7 core expects (pebble_compat.h), on the Paradise:
 * ROM from the flash (cached XIP window), EEPROM pages in RAM, written to the flash
 * by eeprom_store(). Author: mameise */
#include "../core/pebble_compat.h"
#include "../../../emu/src/emu/io.h"
#include "../../../emu/src/emu/flash.h"

extern const uint8_t *g_rom_xip;      /* main.c: start of the ROM in the cached window */
extern uint32_t g_rom_size, g_rom_off;
static uint8_t g_ee[16][256] __attribute__((aligned(4)));   /* test27: the flash DMA takes words (was at ...2: pages 2 bytes off) */
static uint16_t g_have, g_dirty;

void tama_log(const char *fmt, ...) { (void)fmt; }
ResHandle resource_get_handle(int id) { (void)id; return 1; }
size_t resource_size(ResHandle h) { (void)h; return g_rom_size; }
size_t resource_load_byte_range(ResHandle h, size_t off, uint8_t *out, size_t len)
{
  (void)h;
  if (off >= g_rom_size) return 0;
  if (off + len > g_rom_size) len = g_rom_size - off;
  const volatile uint8_t *s = XIP_PTR(g_rom_off + off);     /* uncached: right after setting the window */
  for (size_t i = 0; i < len; i++) out[i] = s[i];
  return len;
}
const uint8_t *t7_compat_rom_mapped(void) { return g_rom_xip; }
void t7_compat_set_rom_path(const char *p) { (void)p; }
void t7_compat_set_eeprom_path(const char *p) { (void)p; }

/* EEPROM: 16 pages of 256 bytes, keys 100..115. In flash: one 4 KB sector,
 * behind it a byte per page that says whether the page was ever written. */
bool persist_exists(uint32_t key) { return key >= 100 && key < 116 && (g_have >> (key - 100)) & 1u; }
int persist_read_data(uint32_t key, void *buf, size_t n)
{
  if (!persist_exists(key)) return -1;
  if (n > 256) n = 256;
  uint8_t *d = buf; for (size_t i = 0; i < n; i++) d[i] = g_ee[key - 100][i];
  return (int)n;
}
status_t persist_write_data(uint32_t key, const void *buf, size_t n)
{
  if (key < 100 || key >= 116) return -1;
  if (n > 256) n = 256;
  const uint8_t *s = buf; int ch = 0;
  for (size_t i = 0; i < n; i++) if (g_ee[key - 100][i] != s[i]) { g_ee[key - 100][i] = s[i]; ch = 1; }
  if (ch || !persist_exists(key)) { g_have |= (uint16_t)(1u << (key - 100)); g_dirty = 1; }
  return S_SUCCESS;
}
status_t persist_delete(uint32_t key) { if (key >= 100 && key < 116 && persist_exists(key)) { g_have &= (uint16_t)~(1u << (key - 100)); g_dirty = 1; } return S_SUCCESS; }
void persist_flush_to_disk(void) {}
int persist_page_count(void) { return 16; }
int persist_page_size(void) { return 256; }
int persist_backup_page(int p, uint8_t *o) { for (int i = 0; i < 256; i++) o[i] = g_ee[p][i]; return persist_exists(100u + (uint32_t)p); }
void persist_restore_page(int p, const uint8_t *in) { for (int i = 0; i < 256; i++) g_ee[p][i] = in[i]; g_have |= (uint16_t)(1u << p); g_dirty = 1; }

/* Flash: two slots of 8 KB (header sector, page sector) at at+0 and at+0x2000. A new state
 * goes into the other slot (pages first, header last), so a power cut always leaves one
 * complete slot. The newer valid slot wins. */
#define EE_MAGIC 0x45453754u   /* "T7EE" */
static uint32_t g_seq; static int g_slot = -1;
static uint32_t rd32(const volatile uint8_t *h) { return h[0] | (h[1] << 8) | (h[2] << 16) | ((uint32_t)h[3] << 24); }
static uint32_t sum_pages(const volatile uint8_t *d) { uint32_t s = 0x7E7E; for (int i = 0; i < 4096; i++) s = s * 31u + d[i]; return s; }
#ifdef T7_EE_SHIFT
/* test27: up to test26 g_ee could lie at an address ...2; the flash DMA starts at the word below, so the page sector
 * holds 2 other bytes, then the pages without their last 2 bytes (the header sum is over the right pages).
 * Such a slot is taken 2 bytes on and written again correctly at the next store. */
static uint32_t sum_pages2(const volatile uint8_t *d) { uint32_t s = 0x7E7E; for (int i = 2; i < 4096; i++) s = s * 31u + d[i]; return s * 31u * 31u; }
static uint8_t g_shift[2];
#endif
static int slot_ok(uint32_t at, uint32_t *seq)
{
  const volatile uint8_t *h = XIP_PTR(at);
  if (rd32(h) != EE_MAGIC) return 0;
  if (rd32(h + 12) != sum_pages(XIP_PTR(at + 0x1000u))) {
#ifdef T7_EE_SHIFT
    if (rd32(h + 12) != sum_pages2(XIP_PTR(at + 0x1000u))) return 0;
    *seq = rd32(h + 4);
    return 2;
#else
    return 0;
#endif
  }
  *seq = rd32(h + 4);
  return 1;
}
void eeprom_load(uint32_t at)
{
  uint32_t s0 = 0, s1 = 0; int a = slot_ok(at, &s0), b = slot_ok(at + 0x2000u, &s1);
#ifdef T7_EE_SHIFT
  g_shift[0] = a == 2; g_shift[1] = b == 2;
#endif
  g_have = 0; g_dirty = 0; g_slot = -1; g_seq = 0;
  if (!a && !b) return;
  g_slot = (a && (!b || (int32_t)(s0 - s1) > 0)) ? 0 : 1;
  uint32_t base = at + (uint32_t)g_slot * 0x2000u;
  g_seq = g_slot ? s1 : s0;
  const volatile uint8_t *h = XIP_PTR(base);
  g_have = (uint16_t)(h[8] | (h[9] << 8));
  const volatile uint8_t *d = XIP_PTR(base + 0x1000u);
  for (int p = 0; p < 16; p++) for (int i = 0; i < 256; i++) g_ee[p][i] = d[p * 256 + i];
#ifdef T7_EE_SHIFT
  if (g_shift[g_slot]) { uint8_t *e = &g_ee[0][0]; for (int i = 0; i < 4094; i++) e[i] = d[i + 2]; e[4094] = e[4095] = 0; g_dirty = 1; }
#endif
}
int eeprom_dirty(void) { return g_dirty; }
uint32_t eeprom_seq(void) { return g_slot < 0 ? 0u : g_seq; }   /* test16: which stored state is in the flash */
void eeprom_store(uint32_t at, uint8_t *sec4k)
{
  if (!g_dirty) return;
  int slot = g_slot == 0 ? 1 : 0;
  uint32_t base = at + (uint32_t)slot * 0x2000u, seq = g_seq + 1u;
  for (int i = 0; i < 0x1000; i++) sec4k[i] = 0xFF;
  flash_update4k(base, sec4k);                       /* old header of this slot away */
  flash_update4k(base + 0x1000u, &g_ee[0][0]);
  uint32_t s = sum_pages(&g_ee[0][0]);
  uint32_t v[4] = { EE_MAGIC, seq, g_have, s };
  for (int k = 0; k < 4; k++) for (int b = 0; b < 4; b++) sec4k[k * 4 + b] = (uint8_t)(v[k] >> (8 * b));
  flash_update4k(base, sec4k);
  g_slot = slot; g_seq = seq; g_dirty = 0;
}
