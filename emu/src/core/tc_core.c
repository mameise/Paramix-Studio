#include "tc.h"

TC_INIT void tc_profile_idl(DeviceProfile *d)
{
    memset(d, 0, sizeof *d);
    d->rom_base = 0x02000000u; d->rom_size = 0x00800000u;
    d->a0ram_base = 0x00000000u; d->a0ram_size = 0x00008000u;
    d->ivram_base = 0x00080000u; d->ivram_size = 0x00003000u;
    d->dstram_base = 0x00084000u; d->dstram_size = 0x00001000u;
    d->io_base = 0x00300000u; d->io_end = 0x00302000u; d->io_size = 0x2000u;
    d->lcd_cmd_addr = 0x00600000u; d->lcd_data_addr = 0x00600001u;
    d->flash_top_boot = false;
    d->debug_strap_high = false;
    d->has_sleep_flag = true; d->sleep_flag = 0x00000FD8u;
    d->ttbr_reset = 0x02400000u;
    d->osc3_hz = 18432000u;
}

TC_INIT void tc_profile_plus_color(DeviceProfile *d)
{
    memset(d, 0, sizeof *d);
    d->rom_base = 0x00C00000u; d->rom_size = 0x00400000u;
    d->a0ram_base = 0x00000000u; d->a0ram_size = 0x00008000u;
    d->ivram_base = 0x00080000u; d->ivram_size = 0x00003000u;
    d->dstram_base = 0x00084000u; d->dstram_size = 0x00001000u;
    d->io_base = 0x00300000u; d->io_end = 0x00302000u; d->io_size = 0x2000u;
    d->lcd_cmd_addr = 0x00600000u; d->lcd_data_addr = 0x00600001u;
    d->flash_top_boot = true;
    d->debug_strap_high = true;
    d->has_sleep_flag = true; d->sleep_flag = 0x00000368u;
    d->ttbr_reset = 0x00C00000u;
    d->osc3_hz = 20000000u;
    d->ir_gpio = true; d->ir_gpio_rx_bit = 5;
}

TC_INIT void tc_init(Emu *e, const uint8_t *rom_xip, uint16_t (*gram)[GRAM_W], uint8_t *ovl_pool, int slots)
{
    DeviceProfile d = e->dev;
    memset(e, 0, sizeof *e);
    e->dev = d;
    e->rom = rom_xip;
    e->rom_fast = rom_xip; e->rom_fast_len = 0;
    e->par_base = d.flash_top_boot ? d.rom_size - 0x10000u : 0;
    if (rom_xip) tc_rom_linear(e, rom_xip);
    e->lcd.gram = gram;
    for (int i = 0; i < OVL_SLOTS; i++) { e->ovl.base[i] = 0xFFFFFFFFu; e->ovl.mem[i] = 0; }
    for (int i = 0; i < slots && i < OVL_SLOTS; i++) e->ovl.mem[i] = ovl_pool + (uint32_t)i * OVL_SIZE;
    e->ovl.n = 0;
    e->lcd.xe = 131; e->lcd.ye = 161;
    e->lcd.dx0 = 0; e->lcd.dy0 = 0; e->lcd.dx1 = 127; e->lcd.dy1 = 127;
    e->fw_len = 0; e->fw_lo = 0xFFFFFFFFu;
    periph_init(e);
    cpu_reset(e);
}

uint32_t tc_halt_steps;
/* ROM is contiguous from rom (fill the block tables). */
void tc_rom_linear(Emu *e, const uint8_t *rom)
{
    for (uint32_t i = 0; i < 128; i++) {
        e->rom_src[i] = (i << 16) < e->dev.rom_size ? rom + (i << 16) : 0;
        e->rom_blk[i] = e->rom_src[i];
    }
    e->rom_blk[e->par_base >> 16] = 0;
}

bool tc_run(Emu *e, uint64_t ziel)
{
    while (e->cycles < ziel) {
        if (e->stopped) return true;
        if (e->halted) {
            if (e->pending_irq || e->wake_req || (!e->halt_hard && e->cycles >= e->wake_at)) {
                e->halted = false;
            } else {
                /* Sleeping CPU: fast forward time in steps instead of cycle by cycle. */
                uint64_t step = ziel - e->cycles;
#ifdef TC_JIT
                { uint32_t nx = periph_next(e); if (step > nx) step = nx; tc_halt_steps++; }
#else
                if (step > 1024) step = 1024;
#endif
                if (!e->halt_hard && e->wake_at > e->cycles && e->wake_at - e->cycles < step) step = e->wake_at - e->cycles;
                if (!step) step = 1;
                e->cycles += step;
                periph_tick(e, (uint32_t)(e->cycles - e->last_tick));
                e->last_tick = e->cycles;
                continue;
            }
        }
#ifdef TC_JIT
        if (jit_enabled) { jit_exec(e, ziel); continue; }
#endif
        tc_exec(e, ziel);
    }
    return e->stopped;
}
