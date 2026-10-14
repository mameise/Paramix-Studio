/* Memory map, based on tamaemu mem.c. ROM reads directly from the Paradise flash,
 * written sectors are kept as a copy in RAM. */
#include "tc.h"
#define DEV const DeviceProfile *dev = &e->dev
#define FLASH_BUSY_READS 3

/* Sector for a ROM offset (top 64 KB: 8 KB sectors, otherwise 64 KB). */
static void sector_of(Emu *e, uint32_t off, uint32_t *base, uint32_t *size)
{
    DEV;
    if (dev->flash_top_boot && off >= dev->rom_size - 0x10000) { *base = off & ~0x1FFFu; *size = 0x2000; }
    else if (!dev->flash_top_boot && off < 0x10000)          { *base = off & ~0x1FFFu; *size = 0x2000; }
    else                                                      { *base = off & ~0xFFFFu; *size = 0x10000; }
}

/* Pointer to the current byte (overlay or original). Erased
 * blocks read 0xFF. */
static const uint8_t rom_ff[4] = { 0xFF, 0xFF, 0xFF, 0xFF };
/* Start of the stored 8 KB part (0 = erased). */
static inline const uint8_t *rom_chunk(Emu *e, uint32_t base)
{
    uint32_t b = (base >> 16) & 127u;
    if ((e->pg_blk[b >> 5] >> (b & 31u)) & 1u) {
        uint8_t k = e->pg_map[(base >> 13) & 1023u];
        return k == 0xFFu ? 0 : e->pg_base + (uint32_t)k * 0x2000u;
    }
    const uint8_t *p = e->rom_src[b];
    return p ? p + (base & 0xE000u) : 0;
}
static inline const uint8_t *rom_src_ptr(Emu *e, uint32_t off)
{
    uint32_t b = (off >> 16) & 127u;
    if ((e->pg_blk[b >> 5] >> (b & 31u)) & 1u) {
        const uint8_t *c = rom_chunk(e, off & ~0x1FFFu);
        return c ? c + (off & 0x1FFFu) : rom_ff;
    }
    const uint8_t *p = e->rom_src[b];
    return p ? p + (off & 0xFFFFu) : rom_ff;
}
/* Slow path: parameter block, written copies, spare pages, erased blocks. */
__attribute__((noinline)) static const uint8_t *rom_slow(Emu *e, uint32_t off)
{
    for (int i = 0; i < e->ovl.n; i++)
        if (off - e->ovl.base[i] < OVL_SIZE) return e->ovl.mem[i] + (off - e->ovl.base[i]);
    return rom_src_ptr(e, off);
}
/* rom_blk is set only for blocks without written copies (see tc_blk_update). */
static inline const uint8_t *rom_ptr(Emu *e, uint32_t off)
{
    const uint8_t *b = e->rom_blk[(off >> 16) & 127u];
    return b ? b + (off & 0xFFFFu) : rom_slow(e, off);
}

/* Get a writable copy of the sector (create it if needed). Only
 * 8 KB sectors fit in a slot; writes to larger sectors are lost. */
int (*tc_ovl_evict)(Emu *e);
static uint8_t *rom_wptr(Emu *e, uint32_t off)
{
    for (int i = 0; i < e->ovl.n; i++)
        if (off - e->ovl.base[i] < OVL_SIZE) return e->ovl.mem[i] + (off - e->ovl.base[i]);
    uint32_t base, size; sector_of(e, off, &base, &size);
    if (size > OVL_SIZE) {
        /* 64 KB sector: kept in 8 KB parts like the parameter sectors (v0.9.2). Before, these
         * writes were lost and the guest could hang or crash (for example when buying an item). */
        if (!tc_big_erase) { e->flash_lost_writes++; return 0; }
        base = off & ~(OVL_SIZE - 1u);
    }
    for (int pass = 0; pass < 2; pass++) {
    if (pass && !(tc_ovl_evict && tc_ovl_evict(e))) break;
    for (int i = 0; i < OVL_SLOTS; i++)
        if (e->ovl.mem[i] && e->ovl.base[i] == 0xFFFFFFFFu) {
            e->ovl.base[i] = base;
            { const uint8_t *c = rom_chunk(e, base);
              if (c) memcpy(e->ovl.mem[i], c, OVL_SIZE); else memset(e->ovl.mem[i], 0xFF, OVL_SIZE); }
            if (i >= e->ovl.n) e->ovl.n = i + 1;
            if (base - e->par_base >= 0x10000u) {
                uint32_t b = (base >> 16) & 127u;
                e->ovl_blk[b >> 5] |= 1u << (b & 31u);
                tc_blk_update(e, b);
            }
#ifdef TC_JIT
            else jit_rom_update(e, base);
#endif
            return e->ovl.mem[i] + (off - base);
        }
    }
    e->flash_lost_writes++;
    return 0;
}

int (*tc_big_erase)(Emu *e, uint32_t base);
/* Fast read pointers of a 64 KB block: direct only while no written copy exists. */
__attribute__((noinline)) void tc_blk_update(Emu *e, uint32_t b)
{
    b &= 127u;
    if ((b << 16) - e->par_base < 0x10000u) return;
    int busy = (int)(((e->ovl_blk[b >> 5] | e->pg_blk[b >> 5]) >> (b & 31u)) & 1u);
    e->rom_blk[b] = busy ? 0 : e->rom_src[b];
    e->fw_len = 0; e->fw_lo = 0xFFFFFFFFu;
#ifdef TC_JIT
    jit_rom_update(e, b << 16);
    e->jit_flush = 1;                      /* translated code may have read this block */
#endif
}
__attribute__((noinline)) void tc_ovl_free(Emu *e, int i)
{
    uint32_t base = e->ovl.base[i];
    if (base == 0xFFFFFFFFu) return;
    e->ovl.base[i] = 0xFFFFFFFFu;
    e->fw_len = 0; e->fw_lo = 0xFFFFFFFFu;
    if (base - e->par_base < 0x10000u) {
#ifdef TC_JIT
        jit_rom_update(e, base);
#endif
        return;
    }
    uint32_t b = (base >> 16) & 127u;
    for (int k = 0; k < e->ovl.n; k++) if (e->ovl.base[k] != 0xFFFFFFFFu && ((e->ovl.base[k] >> 16) & 127u) == b) return;
    e->ovl_blk[b >> 5] &= ~(1u << (b & 31u));
    tc_blk_update(e, b);
}
static void flash_erase_sector(Emu *e, uint32_t off)
{
    uint32_t base, size; sector_of(e, off, &base, &size);
    uint8_t *p = 0;
    if (size > OVL_SIZE) {
        /* drop the written copies of this sector, then erase the backing store */
        for (int i = 0; i < e->ovl.n; i++) if (e->ovl.base[i] - base < size) tc_ovl_free(e, i);
        e->flash_big_ops++;
        if (!(tc_big_erase && tc_big_erase(e, base))) e->flash_lost_writes++;
        tc_blk_update(e, base >> 16);
    } else p = rom_wptr(e, base);
    if (p) memset(p, 0xFF, size);
    e->flash_dirty = true;
    e->flash_busy = FLASH_BUSY_READS; e->fw_len = 0; e->fw_lo = 0xFFFFFFFFu;
    e->flash_op_data = 0xFFFF;
    e->flash_toggle = false;
}

static uint16_t flash_status(Emu *e)
{
    e->flash_toggle = !e->flash_toggle;
    uint16_t s = 0;
    if (!((e->flash_op_data >> 7) & 1)) s |= 0x80;
    if (e->flash_toggle) s |= 0x40;
    if (--e->flash_busy <= 0) e->flash_busy = 0;
    return s;
}

static void flash_write(Emu *e, uint32_t a, uint32_t v, int size)
{
    DEV;
    uint32_t off = a - dev->rom_base;
    uint8_t d = (uint8_t)v;
    if (e->flash_state == 3) {
        uint16_t final = 0;
        for (int i = 0; i < size; i++) {
            uint32_t o = (off + (uint32_t)i) & (dev->rom_size - 1);
            uint8_t *p = rom_wptr(e, o);
            uint8_t nb = p ? (*p &= (uint8_t)(v >> (8 * i))) : rom_ptr(e, o)[0];
            final |= (uint16_t)nb << (8 * i);
        }
        e->flash_dirty = true;
        e->flash_state = 0;
        e->flash_busy = FLASH_BUSY_READS; e->fw_len = 0; e->fw_lo = 0xFFFFFFFFu;
        e->flash_op_data = size >= 2 ? final : (uint16_t)(0xFF00 | final);
        e->flash_toggle = false;
        return;
    }
    if (d == 0xF0) { e->flash_state = 0; e->flash_autoselect = false; return; }
    switch (e->flash_state) {
    case 0: if (d == 0xAA) e->flash_state = 1; break;
    case 1: e->flash_state = (d == 0x55) ? 2 : 0; break;
    case 2:
        if (d == 0xA0) e->flash_state = 3;
        else if (d == 0x80) e->flash_state = 4;
        else if (d == 0x90) { e->flash_autoselect = true; e->flash_state = 0; e->fw_len = 0; e->fw_lo = 0xFFFFFFFFu; }
        else e->flash_state = 0;
        break;
    case 4: e->flash_state = (d == 0xAA) ? 5 : 0; break;
    case 5: e->flash_state = (d == 0x55) ? 6 : 0; break;
    case 6:
        if (d == 0x30) flash_erase_sector(e, off);
        e->flash_state = 0;      /* chip erase (0x10) is ignored */
        break;
    default: e->flash_state = 0; break;
    }
}

static uint8_t flash_read8(Emu *e, uint32_t a)
{
    DEV;
    if (e->flash_busy) return (uint8_t)flash_status(e);
    if (e->flash_autoselect) {
        uint32_t w = (a - dev->rom_base) >> 1;
        if ((w & 0xFF) == 0) return (a & 1) ? 0x00 : 0x01;
        return (a & 1) ? 0x22 : 0xF6;
    }
    return *rom_ptr(e, a - dev->rom_base);
}

#ifdef LCDRD_DEBUG
extern uint32_t g_lcdrd, g_lcdrd_pc[16]; extern int g_np;
#endif
uint8_t mem_read8(Emu *e, uint32_t a)
{
    DEV;
#ifdef LCDRD_DEBUG
    if (a == dev->lcd_cmd_addr || a == dev->lcd_data_addr) { g_lcdrd++; int k; for (k = 0; k < g_np && g_lcdrd_pc[k] != e->pc; k++); if (k == g_np && g_np < 16) g_lcdrd_pc[g_np++] = e->pc; }
#endif
    if (a - dev->rom_base < dev->rom_size) {
        if (e->flash_busy || e->flash_autoselect) return flash_read8(e, a);
        return *rom_ptr(e, a - dev->rom_base);
    }
    if (a - dev->a0ram_base < dev->a0ram_size)  return e->a0ram[a - dev->a0ram_base];
    if (a - dev->io_base < dev->io_size) return periph_read8(e, a);
    if (a - dev->ivram_base < dev->ivram_size)  return e->ivram[a - dev->ivram_base];
    if (a - dev->dstram_base < dev->dstram_size) return e->dstram[a - dev->dstram_base];
    return 0;
}

void mem_write8(Emu *e, uint32_t a, uint8_t v)
{
    DEV;
    if (a - dev->a0ram_base < dev->a0ram_size) { e->a0ram[a - dev->a0ram_base] = v; return; }
    if (a == dev->lcd_cmd_addr)  { e->lcd_writes++; lcd_cmd(e, v);  return; }
    if (a == dev->lcd_data_addr) { e->lcd_writes++; lcd_data(e, v); return; }
    if (a - dev->io_base < dev->io_size) { periph_write8(e, a, v); return; }
    if (a - dev->ivram_base < dev->ivram_size)  { e->ivram[a - dev->ivram_base] = v; return; }
    if (a - dev->dstram_base < dev->dstram_size) { e->dstram[a - dev->dstram_base] = v; return; }
    if (a - dev->rom_base < dev->rom_size) { flash_write(e, a, v, 1); return; }
}

uint16_t mem_read16(Emu *e, uint32_t a)
{
    DEV;
    if (a - dev->rom_base < dev->rom_size - 1) {
        if (e->flash_busy) return flash_status(e);
        if (e->flash_autoselect)
            return (uint16_t)(flash_read8(e, a) | (flash_read8(e, a + 1) << 8));
        const uint8_t *p = rom_ptr(e, a - dev->rom_base);
        return (uint16_t)(p[0] | (p[1] << 8));
    }
    if (a - dev->a0ram_base < dev->a0ram_size - 1) {
        const uint8_t *p = e->a0ram + (a - dev->a0ram_base);
        return (uint16_t)(p[0] | (p[1] << 8));
    }
    return (uint16_t)(mem_read8(e, a) | (mem_read8(e, a + 1) << 8));
}

uint32_t mem_read32(Emu *e, uint32_t a)
{
    DEV;
    if (a - dev->rom_base < dev->rom_size - 3) {
        if (e->flash_busy || e->flash_autoselect)
            return (uint32_t)mem_read16(e, a) | ((uint32_t)mem_read16(e, a + 2) << 16);
        const uint8_t *p = rom_ptr(e, a - dev->rom_base);
        return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
    }
    if (a - dev->a0ram_base < dev->a0ram_size - 3) {
        const uint8_t *p = e->a0ram + (a - dev->a0ram_base);
        return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
    }
    if (a - dev->io_base < dev->io_size - 3) return periph_read32(e, a);
    return (uint32_t)mem_read16(e, a) | ((uint32_t)mem_read16(e, a + 2) << 16);
}

void mem_write16(Emu *e, uint32_t a, uint16_t v)
{
    DEV;
    if (a - dev->a0ram_base < dev->a0ram_size - 1) {
        e->a0ram[a - dev->a0ram_base] = (uint8_t)v;
        e->a0ram[a - dev->a0ram_base + 1] = (uint8_t)(v >> 8);
        return;
    }
    if (a - dev->rom_base < dev->rom_size - 1) { flash_write(e, a, v, 2); return; }
    mem_write8(e, a, (uint8_t)v);
    mem_write8(e, a + 1, (uint8_t)(v >> 8));
}

void mem_write32(Emu *e, uint32_t a, uint32_t v)
{
    DEV;
    if (a - dev->a0ram_base < dev->a0ram_size - 3) {
        uint8_t *p = e->a0ram + (a - dev->a0ram_base);
        p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
        return;
    }
    if (a - dev->io_base < dev->io_size - 3) { periph_write32(e, a, v); return; }
    if (a - dev->rom_base < dev->rom_size - 3) {
        flash_write(e, a, v & 0xFFFF, 2);
        flash_write(e, a + 2, v >> 16, 2);
        return;
    }
    mem_write16(e, a, (uint16_t)v);
    mem_write16(e, a + 2, (uint16_t)(v >> 16));
}

/* Set the instruction window for fast fetch (or clear it if none fits). */
void tc_fetch_window(Emu *e, uint32_t pc)
{
    DEV;
    e->fw_len = 0; e->fw_lo = 0xFFFFFFFFu;
    if (pc - dev->rom_base < dev->rom_size && !e->flash_busy && !e->flash_autoselect) {
        /* 64 KB window of the block */
        const uint8_t *b = e->rom_blk[(pc - dev->rom_base) >> 16];
        if (!b) return;
        e->fw_ptr = b; e->fw_lo = pc & ~0xFFFFu; e->fw_len = 0x10000u; return; }
    if (pc - dev->a0ram_base < dev->a0ram_size) { e->fw_ptr = e->a0ram; e->fw_lo = dev->a0ram_base; e->fw_len = dev->a0ram_size; return; }
    if (pc - dev->ivram_base < dev->ivram_size) { e->fw_ptr = e->ivram; e->fw_lo = dev->ivram_base; e->fw_len = dev->ivram_size; return; }
    if (pc - dev->dstram_base < dev->dstram_size) { e->fw_ptr = e->dstram; e->fw_lo = dev->dstram_base; e->fw_len = dev->dstram_size; return; }
}
