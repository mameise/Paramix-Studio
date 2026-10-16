/* Lean core for colour Tamagotchis (S1C33), derived from tamaemu
 * (github.com/maragotchi/tamaemu, GPL-3.0). For Paradise apps without an
 * operating system: no stdio, no floating point, fixed memory blocks. */
#ifndef TC_H
#define TC_H
#include <stdint.h>
#include <stdbool.h>
/* Only needed at startup: placed at the end of the image, the translator takes the space afterwards */
#if defined(__arm__) && !defined(TC_HOST)
#define TC_INIT __attribute__((section(".initcode"), noinline))
/* at the very end: writes the fixed routines to the start of the area */
#define TC_INIT_LAST __attribute__((section(".initcode_z"), noinline))
#else
#define TC_INIT
#define TC_INIT_LAST
#endif
#include <stddef.h>
#ifdef TC_HOST
#include <string.h>
#else
void *memset(void *d, int c, size_t n);
void *memcpy(void *d, const void *s, size_t n);
#endif

typedef struct DeviceProfile {
    uint32_t rom_base, rom_size;
    uint32_t a0ram_base, a0ram_size;
    uint32_t ivram_base, ivram_size;
    uint32_t dstram_base, dstram_size;
    uint32_t io_base, io_end, io_size;
    uint32_t lcd_cmd_addr, lcd_data_addr;
    bool     flash_top_boot;
    bool     debug_strap_high;
    bool     has_sleep_flag;
    uint32_t sleep_flag;
    uint32_t ttbr_reset;
    uint32_t osc3_hz;
    uint8_t  ir_gpio_rx_bit;
    bool     ir_gpio;
} DeviceProfile;

#define A0RAM_MAX   0x00008000u
#define IVRAM_MAX   0x00003000u
#define DSTRAM_MAX  0x00001000u
#define IORAM_MAX   0x00002000u
#define GRAM_W 128
#define GRAM_H 128

typedef struct Lcd {
    uint16_t (*gram)[GRAM_W];   /* 128x128, located in fast PRAM */
    uint8_t  cmd;
    int      argc;
    uint8_t  args[16];
    int xs, xe, ys, ye, cx, cy;
    uint8_t  madctl, colmod;
    bool     sleep_out, disp_on, inverted;
    uint8_t  pix_buf[3];
    int      pix_n;
    /* changed area since the last fetch */
    int      dx0, dy0, dx1, dy1;
} Lcd;

typedef struct Cmu {
    uint8_t  unlocked;
    uint32_t sccr, pllc, opt;
    uint32_t osc3_hz, mclk_hz;
} Cmu;

typedef struct T16 { uint16_t counter; uint8_t a_fired; uint32_t accum; } T16;

/* Writable flash sectors live in RAM (overlay), the rest is read
 * directly from the Paradise flash (XIP). */
#define OVL_SLOTS 8
#define OVL_SIZE  0x2000u        /* one 8 KB parameter sector per slot */
typedef struct Overlay {
    uint32_t base[OVL_SLOTS];   /* ROM offset of the sector, 0xFFFFFFFF = free */
    uint8_t *mem[OVL_SLOTS];
    int      n;
} Overlay;

typedef struct Emu {
    uint32_t r[16];
    uint32_t sp, alr, ahr, ttbr, idir, dbbr;
    uint32_t pc;
    /* Translator (tc_jit.c): fields near the start so the generated code
     * reaches them with short offsets. */
    uint32_t nzcv;             /* flags in ARM format, C = not guest C */
    int32_t  jit_left;         /* remaining cycles (r10) */
    uint32_t jit_bias;         /* exit request from a helper */
    uint32_t jit_a0;           /* target address of a0ram */
    uint32_t jit_h[9];         /* target addresses of the helpers */
    uint32_t jit_flush;        /* flush the cache at the next stop */
    int      flash_busy;       /* kept up front: the translator reads ROM directly */
    bool     flash_autoselect; /* as long as both are zero */
    uint32_t jit_rom[128];     /* ROM per 64 KB as target address (0 = helper), see jit_rom_update */
    uint32_t jit_par[8];       /* parameter block per 8 KB (overlay or original) */
    uint32_t jit_parb;         /* offset of the parameter block */
    uint8_t  jit_map[128];     /* a0ram pages (256 B) with translated code */
    uint32_t jit_slot[600];    /* jump targets of the exits (chaining without changing code) */
    uint32_t jit_itab[2 * 256];/* jump targets: (guest pc, code|1), direct mapped */
    uint8_t  f_n, f_z, f_v, f_c, f_ie;
    uint8_t  il;
    uint32_t psr_rest;
    int      ext_count;
    uint32_t ext_val[2];
    bool     halted, halt_hard;
    uint64_t wake_at;
    uint64_t cycles;
    uint64_t evt;              /* next stop of the fast core */
    bool     stopped;
    uint32_t stop_pc, stop_code;
    DeviceProfile dev;
    const uint8_t *rom;        /* original in flash (uncached window) */
    const uint8_t *rom_fast;   /* only for tc_cpu.c (reference on the host) */
    uint32_t rom_fast_len;
    /* ROM in 64 KB blocks: rom_src = data (0 = erased, reads 0xFF),
     * rom_blk = fast read pointer (0 = via mem_read, e.g. parameter block) */
    const uint8_t *rom_src[128];
    const uint8_t *rom_blk[128];
    uint32_t par_base;         /* 64 KB with 8 KB parameter sectors */
    /* Instruction window: host pointer for pc in [fw_lo, fw_lo+fw_len) */
    const uint8_t *fw_ptr; uint32_t fw_lo, fw_len;
    Overlay  ovl;
    uint8_t  a0ram[A0RAM_MAX];
    uint8_t  ivram[IVRAM_MAX];
    uint8_t  dstram[DSTRAM_MAX];
    uint8_t  ioram[IORAM_MAX];
    Cmu      cmu;
    Lcd      lcd;
    T16      t16[6];
    uint32_t pending_irq;
    uint8_t  pending_level;
    bool     wake_req;
    uint64_t last_tick;
    uint8_t  btn_mask;
    uint32_t rtc_cyc, rtc_irq_cyc;
    int      flash_state;
    bool     flash_dirty;
    uint16_t flash_op_data;
    bool     flash_toggle;
    uint32_t flash_lost_writes;  /* write attempts without a free slot */
    uint64_t lcd_writes;
    uint32_t ovl_blk[4];         /* 64 KB blocks outside the parameter block with a written copy (bit per block) */
    uint32_t flash_big_ops;      /* erases and writes in 64 KB sectors (for the display) */
    /* Blocks without their own storage (erased in the original): written 8 KB parts live in
     * spare pages (device). pg_blk: bit per block, pg_map: page index per 8 KB (0xFF = erased). */
    const uint8_t *pg_base;
    uint32_t pg_blk[4];
    uint8_t  pg_map[1024];
} Emu;

/* cpu */
void cpu_reset(Emu *e);
void cpu_step(Emu *e);
/* mem */
uint8_t  mem_read8 (Emu *e, uint32_t a);
uint16_t mem_read16(Emu *e, uint32_t a);
uint32_t mem_read32(Emu *e, uint32_t a);
void mem_write8 (Emu *e, uint32_t a, uint8_t v);
void mem_write16(Emu *e, uint32_t a, uint16_t v);
void mem_write32(Emu *e, uint32_t a, uint32_t v);
void tc_fetch_window(Emu *e, uint32_t pc);
/* periph */
uint8_t  periph_read8 (Emu *e, uint32_t a);
uint32_t periph_read32(Emu *e, uint32_t a);
void periph_write8 (Emu *e, uint32_t a, uint8_t v);
void periph_write32(Emu *e, uint32_t a, uint32_t v);
void periph_tick(Emu *e, uint32_t cycles);
uint32_t periph_next(Emu *e);
void periph_rtc_advance(Emu *e, uint32_t n);
void periph_rtc_set(Emu *e, uint32_t t);   /* local time, seconds since 1970 */
void periph_buttons(Emu *e, uint8_t mask);
void periph_init(Emu *e);
/* lcd */
void lcd_cmd (Emu *e, uint8_t c);
void lcd_data(Emu *e, uint8_t d);
/* device */
void tc_profile_plus_color(DeviceProfile *d);
void tc_profile_idl(DeviceProfile *d);
void tc_rom_linear(Emu *e, const uint8_t *rom);
void tc_init(Emu *e, const uint8_t *rom_xip, uint16_t (*gram)[GRAM_W], uint8_t *ovl_pool, int slots);
/* Runs until cycles >= ziel; returns true if stopped (error). */
bool tc_run(Emu *e, uint64_t ziel);
void tc_exec(Emu *e, uint64_t ziel);
/* translator */
void jit_init(Emu *e, uint8_t *buf, uint32_t tgt, uint32_t size);
void jit_exec(Emu *e, uint64_t ziel);
void jit_flush_all(Emu *e);
uint32_t periph_tone_hz(Emu *e);   /* guest tone in Hz, 0 = silent */
/* All overlay slots in use: write one back and free it (device). */
extern int (*tc_ovl_evict)(Emu *e);
/* Erase a 64 KB sector of the guest in the backing store (host image or Paradise flash).
 * Returns 1 if done. Without it, such erases are lost as before. */
extern int (*tc_big_erase)(Emu *e, uint32_t base);
void tc_ovl_free(Emu *e, int i);              /* release a slot without writing it back */
void tc_blk_update(Emu *e, uint32_t blk);     /* fast pointers of a 64 KB block after a change */
void jit_rom_update(Emu *e, uint32_t off);   /* ROM offset: recompute the entry */
extern uint32_t (*jit_tgt_of)(const uint8_t *p);   /* host pointer -> target address (test bench) */
extern int jit_enabled;
/* Platform: enter generated code, or sync the instruction cache */
uint32_t jit_enter(Emu *e, uint32_t code);
extern volatile uint32_t jit_dbg[44];
void jit_stage_setup(uint8_t *stage, uint32_t size);
extern uint32_t jit_st_copy;
extern void (*jit_bad_hook)(Emu *e);
void jit_sync(uint32_t lo, uint32_t hi);
void tc_exec(Emu *e, uint64_t ziel);
#endif
