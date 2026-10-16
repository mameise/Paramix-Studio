/* TG18 system emulator - GPEL3101A peripheral models (from the T18Emu core), for the Paradise:
 * no Bluetooth module, no IR link, no sound samples (only the tone), registers in a small table,
 * SRAM and flash through the T18 memory map. PARADISE: device build (picture straight from the
 * buffer, flash writes through the platform).
 * Author: mameise */
#include "t18soc.h"
#include <string.h>
#define memcpy __builtin_memcpy     /* small copies inline (also in freestanding builds) */
#ifndef PARADISE
#include <stdlib.h>
#include <stdio.h>
#endif

#ifndef TG18_TRACE
#define TG18_TRACE 0
#endif

/* device registers: open hash, about 150 are used */
#define REG_N 256                 /* reg_hash gives 8 bits */
typedef struct { uint32_t a, v; } regent_t;

typedef struct {
    int cs;             /* chip select active */
    int state;          /* bytes into command */
    uint8_t cmd;
    uint32_t addr;
    uint8_t out;        /* response byte */
    uint8_t status;     /* SR1 */
    uint8_t status2;
    int wel;
    int dummy;
    int powerdown;
} spiflash_t;

typedef struct {
    uint64_t base_cyc;   /* cycle at which cnt was base_cnt */
    uint32_t base_cnt;
    uint64_t next_ovf;   /* cycle of next overflow, ~0 if none */
} tmr_t;

#define CPU_HZ_DEFAULT 96000000u   /* master time base: 96 MHz ticks */

struct tg18 {
    arm7_t cpu;
    uint32_t param_addr;     /* game Param struct (for attention detection), 0 = unknown */
    int last_attention;
    int trace;
    /* ---- everything from here to st_end is saved in state ---- */
    uint32_t st_begin;
    uint32_t cpu_hz;         /* master tick rate (constant 96 MHz) */
    uint32_t sys_hz;         /* current system (CPU/peripheral) clock */
    tmr_t tmr[6];
    uint32_t irq_pending;   /* bit n-1 = IRQ n asserted */
    uint32_t fiq_pending;   /* bit n-1 = FIQ n */
    uint64_t next_event;
    /* 32768 Hz time base */
    uint64_t rtc_cyc0;       /* cycle reference */
    uint64_t rtc_tick0;      /* 32k ticks at rtc_cyc0 */
    uint64_t next_half;      /* cycle of next half-second boundary */
    /* RTC macro (battery backed, 0xC0090000 indirect) */
    uint8_t rtcm[256];
    int64_t rtcm_off;        /* macro counter = rtcm_off + ticks */
    uint8_t rtcm_load[6];
    uint64_t rtcm_last_sec;
    /* SoC RTC (0xC0040000) */
    int64_t soc_off;         /* soc seconds = (soc_off + ticks)/32768 */
    /* ADC */
    uint64_t adc_done;
    uint16_t adc_value;
    regent_t regs[REG_N];
    int nreg;
    spiflash_t sf;
    int flash_dirty;
    /* ST7735-style LCD on i80 bus */
    uint8_t lcd_cmd, lcd_nparam, lcd_param[16];
    uint16_t lcd_xs, lcd_xe, lcd_ys, lcd_ye, lcd_x, lcd_y;
    uint8_t lcd_madctl, lcd_on, lcd_hibyte, lcd_half;
    uint32_t lcd_pixels;
    uint64_t lcd_next;       /* next refresh event */
    uint32_t lcd_period;     /* cycles per refresh */
    int frames;
    int buttons;
    int power;               /* 1 = SoC powered */
    /* UART (IrDA) 0xC0060000 */
    uint8_t ur_rx[64]; uint32_t ur_rx_r, ur_rx_w;      /* hardware RX FIFO (16 deep used) */
    uint64_t ur_tx_busy_until;                          /* cycle when TX shift register is free */
    uint32_t ur_tx_fifo_n;                              /* bytes waiting in TX FIFO */
    uint64_t ur_next_rx;                                /* cycle when next byte may enter RX FIFO */
    uint64_t ur_tx_evt;                                 /* next TX FIFO state change (room / complete) */
    uint32_t ir_tx_count;
    uint32_t boot_count;
    /* audio synthesis (PWM on timer E) */
    uint64_t aud_cyc;        /* emulated cycle up to which audio was produced */
    uint32_t aud_phase;      /* 16.16 phase within PWM period (in ticks) */
    uint32_t st_end;
#ifndef PARADISE
#endif
    uint8_t spi_rx[8]; int spi_rxn;
    uint32_t clock_mult;     /* game clock speed (1 = real time), not saved */
    /* finished-picture tracking: the firmware composes straight into the buffer
       the panel scans, top to bottom, over several refreshes. The panel only gets
       a picture once its pass is complete. */
#ifndef PARADISE
    uint16_t pres[128 * 162];
#endif
    uint32_t fb_src, fb_len, fb_last;
    uint64_t fb_last_cyc, fb_pres_cyc;
    int fb_dirty, fb_valid;
    uint32_t pres_seq;       /* counts finished pictures */
    int fb_boot;             /* just powered on: nothing shown before the first full picture */
    uint64_t fb_boot_cyc;
    uint32_t fb_pass_lo;     /* first address of the pass being drawn */
#ifndef PARADISE
    uint8_t fb_raw[128 * 162 / 8];   /* pixel last written as a colour-table number */
#endif
    uint32_t fb_pc[4]; uint8_t fb_pc_raw[4]; int fb_pc_n;   /* storing code, classified */
    uint32_t idle_addr;      /* home-screen idle seconds (sleep after 30), 0 = unknown */
    uint32_t softrtc;        /* firmware's own clock struct (+0x2C = day counter), 0 = unknown */
    uint32_t dma_tab;        /* firmware table of DMA channels in use (4 pointers, 4 busy bytes), 0 = unknown */
    int keep_awake;
    int cpu_boost;           /* CPU runs this many times faster than its clock setting (1 = real) */
    uint64_t ir_last_activity;
    t18_present_fn present; void *present_user;
};

#define tlog(...) do { } while (0)

#define REG_EMPTY 1u           /* register addresses are word aligned, so 1 marks a free slot */
static void reg_clear(tg18_t *t) { for (int k = 0; k < REG_N; k++) { t->regs[k].a = REG_EMPTY; t->regs[k].v = 0; } t->nreg = 0; }
static inline uint32_t reg_hash(uint32_t a) { return (a * 0x9E3779B1u) >> 24; }   /* REG_N = 256 */
static uint32_t *reg(tg18_t *t, uint32_t a) {
    a &= ~3u;
    uint32_t h = reg_hash(a);
    for (int k = 0; k < REG_N; k++, h = (h + 1) & (REG_N - 1)) {
        regent_t *e = &t->regs[h];
        if (e->a == a) return &e->v;
        if (e->a == REG_EMPTY) {
            if (t->nreg >= REG_N - 16) break;
            e->a = a; e->v = 0; t->nreg++; return &e->v;
        }
    }
    static uint32_t dummy; dummy = 0; return &dummy;
}
/* read without taking a slot (unknown registers read as 0) */
static uint32_t reg_get(tg18_t *t, uint32_t a) {
    a &= ~3u;
    uint32_t h = reg_hash(a);
    for (int k = 0; k < REG_N; k++, h = (h + 1) & (REG_N - 1)) {
        regent_t *e = &t->regs[h];
        if (e->a == a) return e->v;
        if (e->a == REG_EMPTY) return 0;
    }
    return 0;
}
#define R(a) (*reg(t, (a)))

/* ---------------- SPI flash (GD25Q64C) ---------------- */
static void sf_cs(tg18_t *t, int on) {
    spiflash_t *s = &t->sf;
    if (on && !s->cs) { s->state = 0; }
    if (!on && s->cs) {
        /* end of command */
        if (s->cmd == 0x06) s->wel = 1;
        else if (s->cmd == 0x04) s->wel = 0;
        else if ((s->cmd == 0x20 || s->cmd == 0x52 || s->cmd == 0xD8) && s->state >= 4 && s->wel) {
            uint32_t sz = s->cmd == 0x20 ? 0x1000 : s->cmd == 0x52 ? 0x8000 : 0x10000;
            uint32_t a = s->addr & ~(sz - 1) & (TG18_FLASH_SIZE - 1);
            t18_flash_erase(a, sz); t->flash_dirty = 1; s->wel = 0;
            if (t->trace) tlog(t, "SF erase %06x %x", a, sz);
        } else if ((s->cmd == 0x60 || s->cmd == 0xC7) && s->wel) {
            /* chip erase - refuse to wipe firmware: only allowed in emulator? ignore */
            s->wel = 0;
        } else if (s->cmd == 0x02) { s->wel = 0; }
        else if (s->cmd == 0x01) { s->wel = 0; }
        else if (s->cmd == 0xB9) s->powerdown = 1;
        else if (s->cmd == 0xAB) s->powerdown = 0;
    }
    s->cs = on;
}

static uint8_t sf_xfer(tg18_t *t, uint8_t b) {
    spiflash_t *s = &t->sf;
    uint8_t out = 0xFF;
    if (!s->cs) return 0xFF;
    if (s->state == 0) { s->cmd = b; s->state = 1; s->addr = 0; return 0xFF; }
    switch (s->cmd) {
    case 0x9F: { static const uint8_t id[3] = {0xC8, 0x40, 0x17}; out = id[(s->state - 1) % 3]; break; }
    case 0x90: case 0xAB:
        if (s->state >= 4) { out = ((s->state - 4) & 1) ? 0x16 : 0xC8; if (s->cmd == 0xAB) out = 0x16; }
        break;
    case 0x05: out = (uint8_t)(s->status | (s->wel ? 2 : 0)); break;
    case 0x35: out = s->status2; break;
    case 0x01: if (s->state == 1) s->status = b & 0xFC; else if (s->state == 2) s->status2 = b; break;
    case 0x03: case 0x0B: case 0x02: case 0x20: case 0x52: case 0xD8: {
        if (s->state <= 3) { s->addr = (s->addr << 8) | b; break; }
        if (s->cmd == 0x0B && s->state == 4) break; /* dummy */
        if (s->cmd == 0x03 || s->cmd == 0x0B) {
            out = t18_flash_rd(s->addr & (TG18_FLASH_SIZE - 1)); s->addr++;
        } else if (s->cmd == 0x02 && s->wel) {
            uint32_t a = s->addr & (TG18_FLASH_SIZE - 1);
            t18_flash_prog(a, b); t->flash_dirty = 1;
            s->addr = (s->addr & ~0xFFu) | ((s->addr + 1) & 0xFF);
        }
        break;
    }
    default:
        break;
    }
    s->state++;
    return out;
}


static void rtc_half_event(tg18_t *t);
static void ur_tx_sched(tg18_t *t);
static void ur_irq(tg18_t *t);
static void rtcm_alarm_poll(tg18_t *t);
static void power_on(tg18_t *t);
static void power_off(tg18_t *t);
static void lcd_refresh_event(tg18_t *t);
static void adc_irq(tg18_t *t);
/* ---------------- interrupt controller (0xD0100000) ---------------- */
static const int tmr_irqn[6] = {8, 10, 17, 25, 0, 0};

/* 1.6.6: mask bit (32-n) belongs to interrupt n (pending bit n-1): the mask bit reversed */
static inline uint32_t rbit32(uint32_t v) {
#if defined(__arm__) && defined(__ARM_ARCH_7M__)
    uint32_t r; __asm__("rbit %0, %1" : "=r"(r) : "r"(v)); return r;
#else
    v = ((v >> 1) & 0x55555555u) | ((v & 0x55555555u) << 1); v = ((v >> 2) & 0x33333333u) | ((v & 0x33333333u) << 2);
    v = ((v >> 4) & 0x0F0F0F0Fu) | ((v & 0x0F0F0F0Fu) << 4); v = ((v >> 8) & 0x00FF00FFu) | ((v & 0x00FF00FFu) << 8);
    return (v >> 16) | (v << 16);
#endif
}
static void intc_update(tg18_t *t) {
    uint32_t mask = R(0xD0100030);        /* bit (32-n) set = masked */
    uint32_t en = t->irq_pending & ~rbit32(mask);
    int glob = !(R(0xD0100038) & 1);
    t->cpu.irq_line = glob && en;
    uint32_t fmask = R(0xD0100034);
    uint32_t fen = 0;
    for (int n = 1; n <= 4; n++) if ((t->fiq_pending >> (n - 1)) & 1) if (!((fmask >> (4 - n)) & 1)) fen |= 1u << (n - 1);
    t->cpu.fiq_line = glob && fen;
    if (t->cpu.irq_line || t->cpu.fiq_line) t->cpu.halted = 0;
}
static uint32_t intc_vec(tg18_t *t, int fiq) {
    if (fiq) {
        uint32_t fmask = R(0xD0100034);
        for (int n = 1; n <= 4; n++) if ((t->fiq_pending >> (n - 1)) & 1) if (!((fmask >> (4 - n)) & 1)) return n;
        return 0;
    }
    uint32_t en = t->irq_pending & ~rbit32(R(0xD0100030));
    return en ? __builtin_ctz(en) + 1 : 0;
}
static void set_irq(tg18_t *t, int n, int on) {
    if (n <= 0) return;
    uint32_t b = 1u << (n - 1);
    uint32_t old = t->irq_pending;
    if (on) t->irq_pending |= b; else t->irq_pending &= ~b;
    if (old != t->irq_pending) intc_update(t);
}
static void set_fiq(tg18_t *t, int n, int on) {
    uint32_t b = 1u << (n - 1);
    uint32_t old = t->fiq_pending;
    if (on) t->fiq_pending |= b; else t->fiq_pending &= ~b;
    if (old != t->fiq_pending) intc_update(t);
}

/* ---------------- timers (0xC0020000, 6 x 0x20) ---------------- */
static uint32_t sysclk(tg18_t *t) { return t->sys_hz; }
static uint32_t tmr_rate(tg18_t *t, uint32_t ctrl) {
    switch (ctrl & 7) {
    case 0: return sysclk(t) / 2;
    case 1: return sysclk(t) / 256;
    case 2: return 32768;
    case 3: return 8192;
    case 4: return 4096;
    default: return 0;
    }
}
#define TREG(i, o) R(0xC0020000u + (i) * 0x20u + (o))
static uint32_t tmr_count(tg18_t *t, int i, uint64_t now) {
    tmr_t *m = &t->tmr[i];
    uint32_t ctrl = TREG(i, 0);
    if (!(ctrl & 0x2000)) return m->base_cnt & 0xFFFF;
    uint32_t rate = tmr_rate(t, ctrl);
    if (!rate) return m->base_cnt & 0xFFFF;
    uint64_t ticks = (now - m->base_cyc) * (uint64_t)rate / t->cpu_hz;
    uint32_t pre = TREG(i, 8) & 0xFFFF;
    uint32_t period = 0x10000 - pre;
    uint64_t c = m->base_cnt + ticks;
    if (c > 0xFFFF) c = pre + ((c - 0x10000) % period);
    return (uint32_t)c;
}
static void tmr_sched(tg18_t *t, int i) {
    tmr_t *m = &t->tmr[i];
    uint32_t ctrl = TREG(i, 0);
    m->next_ovf = ~0ull;
    uint32_t rate = tmr_rate(t, ctrl);
    if (!(ctrl & 0x2000) || !rate) return;
    uint64_t ticks = 0x10000 - (m->base_cnt & 0xFFFF);
    m->next_ovf = m->base_cyc + (ticks * t->cpu_hz + rate - 1) / rate;
    if (m->next_ovf < t->next_event) t->next_event = m->next_ovf;
}
static void tmr_sync(tg18_t *t, int i) {
    uint64_t now = t->cpu.cycles;
    t->tmr[i].base_cnt = tmr_count(t, i, now);
    t->tmr[i].base_cyc = now;
}
static void tmr_irq(tg18_t *t, int i) {
    uint32_t ctrl = TREG(i, 0);
    int on = (ctrl & 0xC000) == 0xC000;
    int fiq_sel = (R(0xD00000A8) == (uint32_t)i) && !((R(0xD0100034) >> 2) & 1);
    if (fiq_sel) { set_fiq(t, 2, on); set_irq(t, tmr_irqn[i], 0); }
    else { set_irq(t, tmr_irqn[i], on); }
}
static void tmr_event(tg18_t *t, int i) {
    tmr_t *m = &t->tmr[i];
    uint32_t pre = TREG(i, 8) & 0xFFFF;
    /* overflow happened at m->next_ovf */
    uint64_t at = m->next_ovf;
    m->base_cnt = pre; m->base_cyc = at;
    TREG(i, 0) |= 0x8000;
    tmr_irq(t, i);
    tmr_sched(t, i);
}
static void events_recalc(tg18_t *t) {
    t->next_event = ~0ull;
    if (t->next_half < t->next_event) t->next_event = t->next_half;
    if (t->adc_done < t->next_event) t->next_event = t->adc_done;
    if (t->lcd_next < t->next_event) t->next_event = t->lcd_next;
    if (t->ur_tx_evt < t->next_event) t->next_event = t->ur_tx_evt;
    for (int i = 0; i < 6; i++) if (t->tmr[i].next_ovf < t->next_event) t->next_event = t->tmr[i].next_ovf;
}
static void events_run(tg18_t *t) {
    uint64_t now = t->cpu.cycles;
    int again = 1;
    while (again) {
        again = 0;
        for (int i = 0; i < 6; i++) if (t->tmr[i].next_ovf <= now) { tmr_event(t, i); again = 1; }
        if (t->next_half <= now) { rtc_half_event(t); again = 1; }
        if (t->lcd_next <= now) { lcd_refresh_event(t); again = 1; }
        if (t->ur_tx_evt <= now) { t->ur_tx_evt = ~0ull; ur_tx_sched(t); ur_irq(t); again = 1; }
        if (t->adc_done <= now) {
            t->adc_done = ~0ull; if (t->trace) tlog(t, "ADC done");
            R(0xC00C0008) = t->adc_value;
            R(0xC00C0004) = (R(0xC00C0004) & ~0x4030u) | 0x8080u;
            adc_irq(t); again = 1;
        }
    }
    events_recalc(t);
}

static int tmr_io_rd(tg18_t *t, uint32_t a, uint32_t *v) {
    if (a < 0xC0020000 || a >= 0xC00200C0) return 0;
    int i = (a - 0xC0020000) >> 5, o = a & 0x1F;
    if (o == 0x10) { *v = tmr_count(t, i, t->cpu.cycles); return 1; }
    return 0;
}
static void tmr_io_wr(tg18_t *t, uint32_t a, uint32_t v, uint32_t old) {
    if (a < 0xC0020000 || a >= 0xC00200C0) return;
    int i = (a - 0xC0020000) >> 5, o = a & 0x1F;
    uint32_t *p = reg(t, a);
    if (o == 0) {
        /* bit15 write-1-clear */
        uint32_t nv = (v & ~0x8000u) | (old & 0x8000u);
        if (v & 0x8000) nv &= ~0x8000u;
        /* sync with old control */
        *p = old; tmr_sync(t, i);
        *p = nv;
        if (!(old & 0x2000) && (nv & 0x2000)) { t->tmr[i].base_cnt = TREG(i, 8) & 0xFFFF; t->tmr[i].base_cyc = t->cpu.cycles; }
        tmr_sched(t, i); events_recalc(t);
        tmr_irq(t, i);
    } else if (o == 8) {
        if (!(TREG(i, 0) & 0x2000)) t->tmr[i].base_cnt = v & 0xFFFF;
    } else if (o == 0x10) {
        tmr_sync(t, i); t->tmr[i].base_cnt = v & 0xFFFF; tmr_sched(t, i); events_recalc(t);
    }
}


/* ---------------- 32768 Hz time base, RTCs ---------------- */
/* Both RTCs count this time base. With a clock multiplier it simply runs faster:
   the firmware still sees every half second, second and minute, only sooner. */
static inline uint64_t rtc_rate(tg18_t *t) { return 32768ull * (t->clock_mult ? t->clock_mult : 1); }
static uint64_t ticks32k(tg18_t *t, uint64_t now) {
    return t->rtc_tick0 + (now - t->rtc_cyc0) * rtc_rate(t) / t->cpu_hz;
}
static uint64_t cyc_of_tick(tg18_t *t, uint64_t tick) {
    uint64_t r = rtc_rate(t);
    return t->rtc_cyc0 + ((tick - t->rtc_tick0) * (uint64_t)t->cpu_hz + r - 1) / r;
}
/* move the reference point up to now (on a whole tick), keeps the products small */
static void rtc_rebase(tg18_t *t) {
    uint64_t now = t->cpu.cycles;
    if (now - t->rtc_cyc0 < (uint64_t)t->cpu_hz) return;
    uint64_t tk = ticks32k(t, now);
    uint64_t c = cyc_of_tick(t, tk);
    if (c > now) c = now;
    t->rtc_cyc0 = c; t->rtc_tick0 = tk;
}
static void rtc_sched(tg18_t *t) {
    uint64_t tk = ticks32k(t, t->cpu.cycles);
    uint64_t soc = (uint64_t)(t->soc_off + (int64_t)tk);
    uint64_t nb = (soc / 16384 + 1) * 16384;       /* next half second in soc time */
    t->next_half = cyc_of_tick(t, tk + (nb - soc));
    if (t->next_half < t->next_event) t->next_event = t->next_half;
}
static uint64_t soc_secs(tg18_t *t) {
    return (uint64_t)(t->soc_off + (int64_t)ticks32k(t, t->cpu.cycles)) / 32768ull;
}
static void soc_rtc_irq(tg18_t *t) {
    uint32_t fl = R(0xC0040054), en = R(0xC0040058);
    set_irq(t, 1, (fl & en) != 0);
}
static uint32_t rtc_alarm_hit(tg18_t *t, uint64_t s) {
    if (!((R(0xC0040050) & 0x100) || (R(0xC0040058) & 0x100))) return 0;
    uint32_t sec = s % 60, min = (s / 60) % 60, hr = (s / 3600) % 24;
    return (sec == (R(0xC0040010) & 0x3F) && min == (R(0xC0040014) & 0x3F) && hr == (R(0xC0040018) & 0x1F)) ? 0x100 : 0;
}
static void rtc_half_event(tg18_t *t) {
    uint64_t soc = (uint64_t)(t->soc_off + (int64_t)ticks32k(t, t->next_half));
    uint64_t halves = (soc + 8) / 16384;           /* boundary just crossed */
    uint32_t fl = 0x1;                             /* half second */
    if ((halves & 1) == 0) {
        uint64_t s = halves / 2;
        fl |= 0x2;
        if (s % 60 == 0) fl |= 0x4;
        if (s % 3600 == 0) fl |= 0x8;
        if (s % 86400 == 0) fl |= 0x10;
        fl |= rtc_alarm_hit(t, s);                 /* alarm: sec/min/hour match */
    }
    R(0xC0040054) |= fl;
    soc_rtc_irq(t);
    /* RTC macro: alarm interrupts reported through regs 0x40/0x50 */
    rtcm_alarm_poll(t);
    rtc_sched(t);
}

static uint64_t rtcm_counter(tg18_t *t) {
    return (uint64_t)(t->rtcm_off + (int64_t)ticks32k(t, t->cpu.cycles)) & 0xFFFFFFFFFFFFull;
}
static uint8_t rtcm_read(tg18_t *t, uint8_t a) {
    if (a >= 0x30 && a <= 0x35) return (uint8_t)(rtcm_counter(t) >> ((a - 0x30) * 8));
    if (a == 0x00) return t->rtcm[0] & ~0x10;      /* never busy */
    return t->rtcm[a];
}
static void rtcm_irq(tg18_t *t);
static void rtcm_write(tg18_t *t, uint8_t a, uint8_t v) {
    t->rtcm[a] = v;
    if (a == 0x40 || a == 0x50) rtcm_irq(t);
    if (a >= 0x10 && a <= 0x15) {
        t->rtcm_load[a - 0x10] = v;
        if (a == 0x15) {
            uint64_t val = 0;
            for (int i = 5; i >= 0; i--) val = (val << 8) | t->rtcm_load[i];
            t->rtcm_off = (int64_t)val - (int64_t)ticks32k(t, t->cpu.cycles);
            if (t->trace) tlog(t, "RTCM set counter %llx", (unsigned long long)val);
        }
    }
}


/* ---- RTC macro alarm (regs 0x20..0x25, flags 0x40, enable 0x50) ---- */
static uint64_t rtcm_alarm_sec(tg18_t *t) {
    uint64_t v = 0;
    for (int i = 5; i >= 0; i--) v = (v << 8) | t->rtcm[0x20 + i];
    return v >> 15;
}
static void rtcm_irq(tg18_t *t) {
    if (t->power) set_irq(t, 3, (t->rtcm[0x40] & t->rtcm[0x50]) != 0);
}
/* seconds (macro counter) at cycle 'cyc' */
static uint64_t rtcm_sec_at(tg18_t *t, uint64_t cyc) {
    return (uint64_t)(t->rtcm_off + (int64_t)ticks32k(t, cyc)) >> 15;
}
/* cycle at which macro counter reaches second s */
static uint64_t rtcm_cyc_of_sec(tg18_t *t, uint64_t s) {
    int64_t tick = (int64_t)(s << 15) - t->rtcm_off;
    if (tick < (int64_t)t->rtc_tick0) return t->cpu.cycles;
    return cyc_of_tick(t, (uint64_t)tick);
}
/* next second > from_sec whose time-of-day matches the alarm */
static uint64_t rtcm_next_alarm_sec(tg18_t *t, uint64_t from_sec) {
    uint64_t a = rtcm_alarm_sec(t) % 86400, f = from_sec % 86400;
    uint64_t d = (a + 86400 - f) % 86400;
    if (d == 0) d = 86400;
    return from_sec + d;
}
static void rtcm_alarm_fire(tg18_t *t) {
    t->rtcm[0x40] |= 0x06;
    if (t->trace) tlog(t, "RTC alarm fired (power=%d en=%02x)", t->power, t->rtcm[0x50]);
    if (t->power) rtcm_irq(t);
    else if (t->rtcm[0x50] & 0x06) power_on(t);
}
/* called every half second while powered: detect alarm crossing */
static void rtcm_alarm_poll(tg18_t *t) {
    uint64_t s = rtcm_sec_at(t, t->cpu.cycles);
    if (s != t->rtcm_last_sec) {
        if ((t->rtcm[0x50] & 0x06) && t->rtcm_last_sec) {
            uint64_t n = rtcm_next_alarm_sec(t, t->rtcm_last_sec);
            if (n <= s) rtcm_alarm_fire(t);
        }
        t->rtcm_last_sec = s;
    }
}
static void off_run(tg18_t *t, uint64_t target) {
    if (t->buttons) { power_on(t); return; }
    uint64_t now = t->cpu.cycles;
    if (t->rtcm[0x50] & 0x06) {
        uint64_t s = rtcm_sec_at(t, now);
        uint64_t n = rtcm_next_alarm_sec(t, s);
        uint64_t c = rtcm_cyc_of_sec(t, n);
        if (c <= target) {
            t->cpu.cycles = c > now ? c : now;
            t->aud_cyc = t->cpu.cycles;
            t->rtcm_last_sec = n;
            rtcm_alarm_fire(t);
            return;
        }
    }
    t->cpu.cycles = target;
    t->aud_cyc = target;
}

/* ---------------- ADC (0xC00C0000) ---------------- */
static void adc_irq(tg18_t *t) {
    uint32_t r4 = R(0xC00C0004);
    set_irq(t, 29, (r4 & 0x8000) && (r4 & 0x40));
}


/* ---------------- LCD (ST7735S via i80, 0xD0500140/16C) ---------------- */
static void lcd_command(tg18_t *t, uint8_t c) {
    t->lcd_cmd = c; t->lcd_nparam = 0; t->lcd_half = 0;
    if (c == 0x2C) { t->lcd_x = t->lcd_xs; t->lcd_y = t->lcd_ys; }
    else if (c == 0x29) t->lcd_on = 1;
    else if (c == 0x28) t->lcd_on = 0;
}
static void lcd_pixel(tg18_t *t, uint16_t px) {
    (void)px;
    t->lcd_pixels++;
    if (++t->lcd_x > t->lcd_xe) {
        t->lcd_x = t->lcd_xs;
        if (++t->lcd_y > t->lcd_ye) { t->lcd_y = t->lcd_ys; t->frames++; }
    }
}
static void lcd_data(tg18_t *t, uint32_t v, int bus8) {
    uint8_t c = t->lcd_cmd;
    if (c == 0x2C) {
        if (bus8) {
            if (!t->lcd_half) { t->lcd_hibyte = (uint8_t)v; t->lcd_half = 1; }
            else { lcd_pixel(t, (uint16_t)((t->lcd_hibyte << 8) | (v & 0xFF))); t->lcd_half = 0; }
        } else lcd_pixel(t, (uint16_t)v);
        return;
    }
    if (t->lcd_nparam < 16) t->lcd_param[t->lcd_nparam] = (uint8_t)v;
    t->lcd_nparam++;
    if (c == 0x2A && t->lcd_nparam == 4) { t->lcd_xs = (t->lcd_param[0] << 8) | t->lcd_param[1]; t->lcd_xe = (t->lcd_param[2] << 8) | t->lcd_param[3]; }
    if (c == 0x2B && t->lcd_nparam == 4) { t->lcd_ys = (t->lcd_param[0] << 8) | t->lcd_param[1]; t->lcd_ye = (t->lcd_param[2] << 8) | t->lcd_param[3]; }
    if (c == 0x36 && t->lcd_nparam == 1) t->lcd_madctl = (uint8_t)v;
}


static void lcd_irq(tg18_t *t) {
    set_irq(t, 27, (R(0xD050018C) & R(0xD0500188)) != 0);
}
/* copy the firmware's buffer as the picture to show */
/* The firmware composes in two ways. The full picture converts every layer
   pixel through its colour table; the small rectangle compositor (used e.g. for
   menu titles and the name bubble) stores the colour-table number itself. As a
   rule the full picture follows at once and covers it, but not always - then a
   blue block stayed visible. The rectangle compositor is recognised by its code
   (a call of the per-pixel routine, then strh r0,[r5]; adds r6,#1 - found once
   in every firmware, at different addresses), and pixels it wrote keep showing
   what was there before until the full picture draws them. */
#ifndef PARADISE
static int fb_is_raw_pc(tg18_t *t, uint32_t pc) {
    for (int i = 0; i < t->fb_pc_n; i++) if (t->fb_pc[i] == pc) return t->fb_pc_raw[i];
    int raw = 0;
    if (pc >= 0xF8000004u && pc < 0xF8000000u + T18_SRAM_SIZE - 4) {
        uint32_t o = pc - 0xF8000000u;
        uint16_t h = (uint16_t)t18_sram_rd(o, 2), n = (uint16_t)t18_sram_rd(o + 2, 2);
        uint16_t b1 = (uint16_t)t18_sram_rd(o - 4, 2), b2 = (uint16_t)t18_sram_rd(o - 2, 2);
        raw = h == 0x8028 && n == 0x3601 && (b1 & 0xF800) == 0xF000 && (b2 & 0xF800) == 0xF800;
    }
    int k = t->fb_pc_n < 4 ? t->fb_pc_n++ : 3;
    t->fb_pc[k] = pc; t->fb_pc_raw[k] = (uint8_t)raw;
    return raw;
}
#endif
static void fb_present(tg18_t *t) {
#ifndef PARADISE
    uint32_t n = t->fb_len / 2;
    for (uint32_t i = 0; i < n; i++) {
        uint32_t a = t->fb_src + i * 2;
        uint16_t v = arm7_read16(&t->cpu, a);
        int raw = i < sizeof t->fb_raw * 8 && (t->fb_raw[i >> 3] >> (i & 7) & 1);
        if (!raw) t->pres[i] = v;
        else if (!t->fb_valid) t->pres[i] = 0;
    }
#endif
    t->fb_dirty = 0; t->fb_valid = 1; t->fb_pres_cyc = t->cpu.cycles; t->pres_seq++;
    if (t->present) t->present(t->present_user, t->fb_src, t->fb_len);
}
/* a store into the scanned buffer (called before it lands). A picture is shown
   once drawing pauses (see lcd_refresh_event); passes drawn back to back, like a
   label composed just before the full scene, stay hidden. Should drawing never
   pause, a pass boundary (addresses go back) shows the picture after 0.7 s. */
static void fb_note(void *ctx, uint32_t a) {
    tg18_t *t = (tg18_t *)ctx;
#ifndef PARADISE
    uint32_t px = (a - t->fb_src) >> 1;
    if (px < sizeof t->fb_raw * 8) {
        if (fb_is_raw_pc(t, t->cpu.last_pc)) t->fb_raw[px >> 3] |= (uint8_t)(1u << (px & 7));
        else t->fb_raw[px >> 3] &= (uint8_t)~(1u << (px & 7));
    }
#endif
    if (a < t->fb_last || !t->fb_dirty) t->fb_pass_lo = a;       /* a new pass begins */
    if (t->fb_boot) {
        /* waking up: the firmware first draws bits (e.g. the name label) into a
           black buffer; the panel stays dark until one pass has covered the whole
           picture from the top line to the bottom line */
        uint32_t line = t->fb_len / 128 ? t->fb_len / 128 : 256;
        if (t->fb_pass_lo < t->fb_src + 2 * line && a >= t->fb_src + t->fb_len - 2 * line) t->fb_boot = 0;
    } else if (a < t->fb_last && t->fb_dirty && t->cpu.cycles - t->fb_pres_cyc > (uint64_t)t->cpu_hz * 7 / 10) fb_present(t);
    t->fb_last = a; t->fb_dirty = 1; t->fb_last_cyc = t->cpu.cycles;
}
static void lcd_refresh_event(tg18_t *t) {
    t->lcd_next += t->lcd_period;
    uint32_t ctl = R(0xD0500140);
    if (((ctl >> 4) & 0xF) == 0xD && (ctl & 1)) {
        uint32_t src = R(0xD050033C);
        int w = (R(0xD0500154) & 0x3FF) + 1, h = (R(0xD0500144) & 0x3FF) + 1;
        uint32_t len = (uint32_t)(w * h * 2);
        if (len > 128u * 162u * 2u) len = 128u * 162u * 2u;
        if (t->fb_boot && t->cpu.cycles - t->fb_boot_cyc > (uint64_t)t->cpu_hz * 3) t->fb_boot = 0;   /* safety */
#ifdef PARADISE
        /* Paradise: no store watching (translated code writes the buffer directly). The
           panel gets the buffer like the real LCD controller scans it, at most 30 times a
           second; the platform skips pictures that did not change. */
        t->fb_src = src; t->fb_len = len; t->fb_boot = 0;
        if (t->cpu.cycles - t->fb_pres_cyc >= (uint64_t)t->cpu_hz / 30) fb_present(t);
        if (0) {
#else
        if (src != t->fb_src || len != t->fb_len || !t->fb_valid) {
#endif
            t->fb_src = src; t->fb_len = len;
            t->cpu.note_lo = src; t->cpu.note_hi = src + len; t->cpu.note_fn = fb_note;
            if (t->fb_boot) {
#ifndef PARADISE
                memset(t->pres, 0, sizeof t->pres);
#endif
                t->fb_valid = 1;
            }
            else fb_present(t);
        } else if (t->fb_boot) {
            /* keep the dark picture */
        } else if (t->fb_dirty && t->cpu.cycles - t->fb_last_cyc > t->cpu_hz / 125) {
            fb_present(t);                         /* quiet for 8 ms: drawing finished */
        }
        /* (the panel itself is not modelled: the finished picture goes out by fb_present) */
        lcd_command(t, 0x2C);
        R(0xD050018C) |= 0x2040;
        lcd_irq(t);
    }
}


/* ---------------- audio: timer E PWM -> speaker ---------------- */
#define AUD_TMR 4
static int aud_pwm_on(tg18_t *t) {
    uint32_t ctrl = TREG(AUD_TMR, 0), pwm = TREG(AUD_TMR, 4);
    return (ctrl & 0x2000) && (pwm & 0x8000) && tmr_rate(t, ctrl);
}
/* no samples on the Paradise: the buzzer gets the tone's frequency (tg18_tone_hz) */
static void aud_catchup(tg18_t *t) { t->aud_cyc = t->cpu.cycles; }
uint32_t tg18_tone_hz(tg18_t *t) {
    if (!t->power || !aud_pwm_on(t)) return 0;
    uint32_t pre = TREG(AUD_TMR, 8) & 0xFFFF, period = 0x10000 - pre;
    if (period < 2) return 0;
    return tmr_rate(t, TREG(AUD_TMR, 0)) / period;
}


/* ---------------- UART / IrDA (0xC0060000) ---------------- */
static uint32_t ur_byte_cycles(tg18_t *t) {
    uint32_t div = R(0xC006000C);
    if (div < 16) div = t->sys_hz / 9600;
    return (uint32_t)((uint64_t)div * 10 * t->cpu_hz / t->sys_hz);   /* 10 bits per byte, in master ticks */
}
static int ur_rx_count(tg18_t *t) { return (int)(t->ur_rx_w - t->ur_rx_r); }
static uint32_t ur_status(tg18_t *t);
static void ur_irq(tg18_t *t) {
    uint32_t ctl = R(0xC0060008);
    int on = (ctl & 0x8000) && ur_rx_count(t) > 0;
    if ((ctl & 0x4000) && (ur_status(t) & 0x4000)) on = 1;
    set_irq(t, 14, on);
}
/* move bytes from the incoming link queue into the RX FIFO at line rate */
static void ur_feed(tg18_t *t) {
    uint64_t now = t->cpu.cycles;
    (void)now;      /* no IR link on the Paradise: nothing arrives */
    ur_irq(t);
}
static uint32_t ur_status(tg18_t *t) {
    uint64_t now = t->cpu.cycles;
    uint32_t st = 0;
    if (ur_rx_count(t) == 0) st |= 0x10;
    /* TX FIFO model: busy until last byte shifted out */
    uint64_t bc = ur_byte_cycles(t);
    uint64_t pending = t->ur_tx_busy_until > now ? (t->ur_tx_busy_until - now + bc - 1) / bc : 0;
    if (pending >= 16) st |= 0x20;
    else st |= 0x4000;                           /* TX FIFO has room */
    if (pending == 0) st |= 0x80 | 0x40;         /* TX empty / idle */
    if (ur_rx_count(t) > 0 && (R(0xC0060008) & 0x8000)) st |= 0x8000;
    return st;
}
/* schedule an event when the TX FIFO gains room or completes, if TX interrupts are enabled */
static void ur_tx_sched(tg18_t *t) {
    uint64_t now = t->cpu.cycles, bc = ur_byte_cycles(t);
    t->ur_tx_evt = ~0ull;
    if (!(R(0xC0060008) & 0x4000) || t->ur_tx_busy_until <= now) return;
    uint64_t room = t->ur_tx_busy_until > 15 * bc ? t->ur_tx_busy_until - 15 * bc : 0;
    uint64_t at = room > now ? room : t->ur_tx_busy_until;
    t->ur_tx_evt = at;
    if (at < t->next_event) t->next_event = at;
}
static void ur_tx(tg18_t *t, uint8_t b) {
    uint64_t now = t->cpu.cycles, bc = ur_byte_cycles(t);
    uint64_t start = t->ur_tx_busy_until > now ? t->ur_tx_busy_until : now;
    t->ur_tx_busy_until = start + bc;
    (void)b;
    t->ir_tx_count++;
    t->ir_last_activity = now;
    ur_tx_sched(t);
}


/* ---------------- clock tree ---------------- */
static void tmr_sync(tg18_t *t, int i);
static void tmr_sched(tg18_t *t, int i);
static void events_recalc(tg18_t *t);
/* master ticks per CPU cycle at system clock hz, divided by the boost */
static uint32_t cpu_cost(tg18_t *t, uint32_t hz) {
    uint32_t m = t->cpu_hz / hz;
    uint32_t b = t->cpu_boost > 1 ? (uint32_t)t->cpu_boost : 1u;
    m = m / b;
    return m < 1 ? 1 : m;
}
static void clk_update(tg18_t *t) {
    uint32_t hz;
    if (R(0xD000001C) & 0x8000) {
        uint32_t pll = (R(0xD000005C) & 0xFF) * 6000000u;
        uint32_t div = (R(0xD00000AC) & 0x7F) + 1;
        if (!pll) pll = 96000000u;
        hz = pll / div;
    } else hz = 12000000u;
    if (hz < 1000000u) hz = 1000000u;
    if (hz > 96000000u) hz = 96000000u;
    if (hz == t->sys_hz) return;
    for (int i = 0; i < 6; i++) tmr_sync(t, i);
    t->sys_hz = hz;
    t->cpu.cyc_mult = cpu_cost(t, hz);
    for (int i = 0; i < 6; i++) tmr_sched(t, i);
    events_recalc(t);
    if (t->trace) tlog(t, "SYSCLK %u Hz (mult %d)", hz, t->cpu.cyc_mult);
}

/* ---------------- MMIO ---------------- */
/* ---------------- DMA controller (0xD0300000, 4 channels of 0x40) ----------------
   +0 control (bit0 start, bits12-13 unit 1/2/4 bytes), +4 source, +8 destination,
   +0xC count of units; +0x1FC status, bit n = channel n done (write 1 to clear),
   IRQ 24. The 2018 firmwares (JP v030/v031) program the SPI flash this way: the
   flash controller sends command and address by itself (0xC0150010 low byte,
   0xC0150014) and the DMA feeds the data register. The transfer happens at once. */
static uint32_t io_rd(void *ctx, uint32_t a, int size);
static void io_wr(void *ctx, uint32_t a, uint32_t v, int size);
static int is_io(uint32_t a) { return a >= 0xC0000000u && a < 0xE0000000u; }
static uint32_t dma_rd(tg18_t *t, uint32_t a, int w) {
    if (is_io(a)) return io_rd(t, a, w);
    return w == 4 ? arm7_read32(&t->cpu, a) : w == 2 ? arm7_read16(&t->cpu, a) : arm7_read8(&t->cpu, a);
}
static void dma_wr(tg18_t *t, uint32_t a, uint32_t v, int w) {
    if (is_io(a)) { io_wr(t, a, v, w); return; }
    if (w == 4) arm7_write32(&t->cpu, a, v);
    else if (w == 2) { arm7_write8(&t->cpu, a, (uint8_t)v); arm7_write8(&t->cpu, a + 1, (uint8_t)(v >> 8)); }
    else arm7_write8(&t->cpu, a, (uint8_t)v);
}
static void dma_run(tg18_t *t, int ch) {
    uint32_t base = 0xD0300000u + (uint32_t)ch * 0x40u;
    uint32_t ctrl = R(base), src = R(base + 4), dst = R(base + 8), cnt = R(base + 0xC) & 0xFFFFFFu;
    int w = 1 << ((ctrl >> 12) & 3); if (w > 4) w = 4;
    int sf_tx = dst == 0xC0150008u, sf_rx = src == 0xC015000Cu;
    if (sf_tx || sf_rx) {
        uint32_t ad = R(0xC0150014);
        sf_cs(t, 1);
        sf_xfer(t, (uint8_t)R(0xC0150010));
        sf_xfer(t, (uint8_t)(ad >> 16)); sf_xfer(t, (uint8_t)(ad >> 8)); sf_xfer(t, (uint8_t)ad);
        if ((R(0xC0150010) & 0xFF) == 0x0B) sf_xfer(t, 0);
    }
    for (uint32_t i = 0; i < cnt; i++) {
        uint32_t v;
        if (sf_rx) { v = 0; for (int k = 0; k < w; k++) v |= (uint32_t)sf_xfer(t, 0xFF) << (8 * k); }
        else v = dma_rd(t, src, w);
        if (sf_tx) { for (int k = 0; k < w; k++) sf_xfer(t, (uint8_t)(v >> (8 * k))); }
        else dma_wr(t, dst, v, w);
        if (!is_io(src)) src += (uint32_t)w;
        if (!is_io(dst)) dst += (uint32_t)w;
    }
    if (sf_tx || sf_rx) sf_cs(t, 0);
    R(base) = ctrl & ~1u;
    R(0xD03001FC) |= 1u << ch;
    set_irq(t, 24, 1);
}

/* memory that is not direct: SRAM above 128 KB, erased flash, everything else unused */
static uint32_t mem_rd(tg18_t *t, uint32_t a, int size) {
    (void)t;
    uint32_t o = a - 0xF8000000u;
    if (o < T18_SRAM_SIZE && o + (uint32_t)size <= T18_SRAM_SIZE) return t18_sram_rd(o, size);
    if (a - 0x20000000u < 0x800000u) {
        /* flash outside the map (erased, or a block the platform holds in RAM while writing) */
        uint32_t v = 0;
        for (int i = 0; i < size; i++) v |= (uint32_t)t18_flash_rd(a + (uint32_t)i) << (8 * i);
        return v;
    }
    return 0;
}
static uint32_t io_rd(void *ctx, uint32_t a, int size) {
    tg18_t *t = (tg18_t *)ctx;
    uint32_t v;
    if (!is_io(a)) return mem_rd(t, a, size);
    switch (a & ~3u) {
    case 0xD000003C: v = (R(0xD000001C) & 0x8000) ? 2 : 1; break;
    case 0xD0000064: v = 0; break;
    case 0xC0150004: v = R(a & ~3u) & ~8u; break;
    case 0xC015000C: v = t->sf.out; break;
    case 0xD0100028: v = intc_vec(t, 0); break;
    case 0xC0090010: v = 1; break;
    case 0xC0060000:
        ur_feed(t);
        if (ur_rx_count(t) > 0) { v = t->ur_rx[t->ur_rx_r & 63]; t->ur_rx_r++; } else v = 0;
        ur_feed(t);
        break;
    case 0xC0060010: ur_feed(t); v = ur_status(t) | (R(0xC0060010) & 0x0F00); break;
    case 0xC008000C: v = (R(0xC008000C) & ~7u) | (uint32_t)(t->spi_rxn > 7 ? 7 : t->spi_rxn); break;
    case 0xC0080010:
        if (t->spi_rxn > 0) { v = t->spi_rx[0]; memmove(t->spi_rx, t->spi_rx + 1, (size_t)(--t->spi_rxn)); } else v = 0;
        break;
    case 0xC0080014: v = (R(0xC0080014) & ~4u) | (t->spi_rxn ? 4u : 0u); break;
    case 0xC0090014: v = R(0xC0090014); break;
    case 0xC0040000: v = soc_secs(t) % 60; break;
    case 0xC0040004: v = (soc_secs(t) / 60) % 60; break;
    case 0xC0040008: v = (soc_secs(t) / 3600) % 24; break;
    case 0xC004005C: v = 0; break;
    case 0xC0000020: v = (R(0xC0000020) & ~0xE0u) | ((t->buttons & 1) ? 0x20 : 0) | ((t->buttons & 2) ? 0x40 : 0) | ((t->buttons & 4) ? 0x80 : 0); break;
    case 0xD010002C: v = intc_vec(t, 1); break;
    default:
        if (!tmr_io_rd(t, a & ~3u, &v)) v = reg_get(t, a);
        break;
    }
    if (t->trace) tlog(t, "R %08x = %08x pc=%08x", a, v, t->cpu.last_pc);
    if (size == 1) v >>= (a & 3) * 8;
    else if (size == 2) v >>= (a & 2) * 8;
    return v;
}

/* 1.6.6: the SPI flash registers in a word access, straight from the translator's slow path (the firmware
 * reads its pictures byte by byte through them, about 20000 a second on the pet screen). Same as io_rd /
 * io_wr for these registers. 1 = done. */
int tg18_sf_fast(void *ctx, uint32_t a, uint32_t *v, int wr) {
    tg18_t *t = (tg18_t *)ctx;
    if (t->trace) return 0;
    uint32_t *p = reg(t, a);
    if (!wr) {
        switch (a) {
        case 0xC0150004: *v = *p & ~8u; return 1;
        case 0xC015000C: *v = t->sf.out; return 1;
        case 0xC0150000: case 0xC0150008: case 0xC0150010: case 0xC0150014: case 0xC0150018: case 0xC015001C: *v = *p; return 1;
        }
        return 0;
    }
    uint32_t old = *p; *p = *v;
    switch (a) {
    case 0xC0150000: if ((*p ^ old) & 0x40) sf_cs(t, (*p >> 6) & 1); return 1;
    case 0xC0150008: t->sf.out = sf_xfer(t, (uint8_t)*v); return 1;
    case 0xC0150004: case 0xC015000C: case 0xC0150010: case 0xC0150014: case 0xC0150018: case 0xC015001C: return 1;   /* command, address, length: kept for the DMA */
    }
    *p = old; return 0;
}
/* for the idle skip: 1 when reading a gives the same value (and changes nothing) until the next event */
static int io_stable(void *ctx, uint32_t a) {
    (void)ctx;
    if (!is_io(a)) return 1;
    a &= ~3u;
    switch (a) {
    case 0xC015000C: case 0xD0100028: case 0xD010002C: case 0xC0060000: case 0xC0060010:
    case 0xC008000C: case 0xC0080010: case 0xC0080014:
    case 0xC0040000: case 0xC0040004: case 0xC0040008:
        return 0;
    }
    if (a >= 0xC0020000u && a < 0xC00200C0u && (a & 0x1F) == 0x10) return 0;   /* timer counters */
    return 1;
}
static void io_wr(void *ctx, uint32_t a, uint32_t v, int size) {
    tg18_t *t = (tg18_t *)ctx;
    if (!is_io(a)) {
        uint32_t o = a - 0xF8000000u;
        if (o < T18_SRAM_SIZE && o + (uint32_t)size <= T18_SRAM_SIZE) t18_sram_wr(o, v, size);   /* the hole */
        return;
    }
    uint32_t *p = reg(t, a & ~3u);
    uint32_t old = *p;
    if (size == 4) *p = v;
    else if (size == 2) { int sh = (a & 2) * 8; *p = (*p & ~(0xFFFFu << sh)) | ((v & 0xFFFF) << sh); }
    else { int sh = (a & 3) * 8; *p = (*p & ~(0xFFu << sh)) | ((v & 0xFF) << sh); }
    if (t->trace) tlog(t, "W %08x = %08x pc=%08x", a, v, t->cpu.last_pc);
    if ((a & ~0x1Fu) == 0xC0020080u) { uint32_t nv = *p; *p = old; aud_catchup(t); *p = nv; }
    tmr_io_wr(t, a & ~3u, *p, old);
    switch (a & ~3u) {
    case 0xD0100030: case 0xD0100034: case 0xD0100038: intc_update(t); break;
    case 0xD00000A8: for (int i = 0; i < 4; i++) tmr_irq(t, i); break;
    case 0xD0000078: if (!(v & 1)) power_off(t); break;
    case 0xD000001C: case 0xD000005C: case 0xD00000AC: clk_update(t); break;
    case 0xC0060000: ur_tx(t, (uint8_t)v); break;
    case 0xC0060008: ur_tx_sched(t); ur_irq(t); break;
    case 0xC0080008: {
        uint8_t in = 0x00;   /* Bluetooth module: not on the Paradise */
        if (t->spi_rxn < 8) t->spi_rx[t->spi_rxn++] = in;
        break;
    }
    case 0xC009000C:
        if (v & 1) rtcm_write(t, (uint8_t)R(0xC0090004), (uint8_t)R(0xC0090008));
        else if (v & 2) R(0xC0090014) = rtcm_read(t, (uint8_t)R(0xC0090004));
        *p = 0;
        break;
    case 0xC0040000: case 0xC0040004: case 0xC0040008: {
        uint64_t s = soc_secs(t);
        uint64_t sec = s % 60, mn = (s / 60) % 60, hr = (s / 3600) % 24, day = s / 86400;
        if ((a & ~3u) == 0xC0040000) sec = v % 60;
        if ((a & ~3u) == 0xC0040004) mn = v % 60;
        if ((a & ~3u) == 0xC0040008) hr = v % 24;
        uint64_t ns = day * 86400 + hr * 3600 + mn * 60 + sec;
        uint64_t tk = ticks32k(t, t->cpu.cycles);
        uint64_t frac = (uint64_t)(t->soc_off + (int64_t)tk) % 32768;
        t->soc_off = (int64_t)(ns * 32768 + frac) - (int64_t)tk;
        rtc_sched(t); events_recalc(t);
        break;
    }
    case 0xC0040054: *p = old & ~v; soc_rtc_irq(t); break;
    case 0xC0040058: soc_rtc_irq(t); break;
    case 0xC00C0004:
        if ((v & 0x8000)) *p &= ~0x8000u;          /* ack */
        *p &= ~0x30u;
        if ((v & 0x4000) && !(old & 0x4000)) { t->adc_done = t->cpu.cycles + t->cpu_hz / 50000; events_recalc(t); }
        adc_irq(t);
        break;
    case 0xD050018C: *p = old & ~v; lcd_irq(t); break;
    case 0xD0500188: lcd_irq(t); break;
    case 0xD0500140: {
        uint32_t op = (v >> 4) & 0xF;
        if (op == 0x8) lcd_command(t, (uint8_t)R(0xD050016C));
        else if (op == 0xA) lcd_data(t, R(0xD050016C), (v & 0x2000) != 0);
        else if (op != 0xD && op != 0 && t->trace) tlog(t, "LCD ctrl op %x v=%08x", op, v);
        break;
    }
    case 0xC0150000:
        if ((*p ^ old) & 0x40) sf_cs(t, (*p >> 6) & 1);
        break;
    case 0xD0300000: case 0xD0300040: case 0xD0300080: case 0xD03000C0:
        if (v & 1) dma_run(t, (int)((a >> 6) & 3));
        break;
    case 0xD03001FC:
        *p = old & ~v;
        set_irq(t, 24, (*p & 0xF) != 0);
        break;
    case 0xC0150008:
        t->sf.out = sf_xfer(t, (uint8_t)v);
        break;
    }
}

/* The firmware keeps one flag byte per button in three consecutive SRAM bytes;
   code refers to them together everywhere, so in the literal pools the three
   addresses stand close to each other (about 120 times in every image). The
   home-screen idle counter lies 15 bytes before them: it counts seconds without
   input and at 30 the device goes to sleep. */
#ifndef PARADISE
static uint32_t find_idle(const uint8_t *rom, uint32_t len) {
    uint32_t n = len / 4, best = 0; int bestc = 0;
    static uint32_t cand[64]; static int cc[64]; int nc = 0;
    for (uint32_t i = 0; i + 8 < n; i++) {
        uint32_t w; memcpy(&w, rom + i * 4, 4);
        if (w < 0xF8000000u || w >= 0xF8000000u + T18_SRAM_SIZE) continue;
        int h1 = 0, h2 = 0;
        for (int k = 1; k < 8; k++) { uint32_t x; memcpy(&x, rom + (i + k) * 4, 4); if (x == w + 1) h1 = 1; if (x == w + 2) h2 = 1; }
        if (!h1 || !h2) continue;
        int j; for (j = 0; j < nc; j++) if (cand[j] == w) break;
        if (j == nc) { if (nc == 64) continue; cand[nc] = w; cc[nc++] = 0; }
        if (++cc[j] > bestc) { bestc = cc[j]; best = w; }
    }
    return bestc >= 40 ? best - 0x0F : 0;
}
#else
static uint32_t find_idle(const uint8_t *rom, uint32_t len) { (void)rom; (void)len; return 0; }   /* the Studio finds it */
#endif
/* rom: the firmware as the CPU sees it (for the address searches), len its size.
 * The SRAM (t18_sram) and the flash map (t18_fblk) are set up by the platform. */
void tg18_init(tg18_t *t, const uint8_t *rom, uint32_t len) {
    memset(t, 0, sizeof *t);
    reg_clear(t);
    arm7_init(&t->cpu);
    t->cpu.ctx = t; t->cpu.rd = io_rd; t->cpu.wr = io_wr; t->cpu.io_stable = io_stable;
    t->fb_valid = 0; t->fb_dirty = 0; t->fb_last = 0;
    t->cpu.vector_base = 0x2000000C;
    t->cpu_hz = CPU_HZ_DEFAULT;
    t->sys_hz = 12000000u;
    t->rtc_cyc0 = 0; t->rtc_tick0 = 0;
    /* battery RTC starts at 2018-01-01 00:00 (firmware epoch day 0 = 2007-12-31) */
    t->rtcm_off = (int64_t)3654 * 86400 * 32768;
    if (!rom) len = 0;                        /* the platform sets the addresses (tg18_set_addrs) */
    t->idle_addr = len ? find_idle(rom, len) : 0;
    /* The firmware reads only hour/minute/second from the SoC RTC and counts the days
       itself (struct +0x2C, one up when the hour goes from 23 to 0). Its read function
       keeps 0xC0040000 and the struct address next to each other in the literal pool. */
    for (uint32_t i = 0; i + 8 <= len && i < 0x20000u; i += 4) {
        uint32_t a0, a1; memcpy(&a0, rom + i, 4); memcpy(&a1, rom + i + 4, 4);
        if (a0 == 0xC0040000u && a1 >= 0xF8008000u && a1 < 0xF8010000u && a1 != 0xF800128Cu && (a1 & 3) == 0) {
            t->softrtc = a1; break;
        }
    }
    /* the DMA driver's literal pool: 0xD0300000, 0x3210 (priorities), then its channel table */
    for (uint32_t i = 0; i + 12 <= len && i < 0x400000u; i += 4) {
        uint32_t a0, a1, a2; memcpy(&a0, rom + i, 4); memcpy(&a1, rom + i + 4, 4); memcpy(&a2, rom + i + 8, 4);
        if (a0 == 0xD0300000u && a1 == 0x3210u && a2 >= 0xF8000000u && a2 < 0xF8020000u) { t->dma_tab = a2; break; }
    }
    tg18_reset(t);
}
uint32_t tg18_struct_size(void) { return (uint32_t)sizeof(tg18_t); }
void tg18_set_present(tg18_t *t, t18_present_fn fn, void *user) { t->present = fn; t->present_user = user; }
#ifndef PARADISE
const uint16_t *tg18_picture(tg18_t *t) { return t->pres; }
#endif

static void soc_reset(tg18_t *t) {
    uint64_t now = t->cpu.cycles;
    t->fb_boot = 1; t->fb_boot_cyc = now; t->fb_dirty = 0; t->fb_last = 0; t->fb_valid = 0;
    t->fb_pc_n = 0;
#ifndef PARADISE
    memset(t->fb_raw, 0, sizeof t->fb_raw);
#endif
    t18_sram_clear();
#ifdef T18_JIT
    t18_jit_reset();
#endif
    reg_clear(t);
    memset(&t->sf, 0, sizeof t->sf);
    memset(t->tmr, 0, sizeof t->tmr);
    for (int i = 0; i < 6; i++) t->tmr[i].next_ovf = ~0ull;
    t->irq_pending = t->fiq_pending = 0;
    t->cpu_hz = CPU_HZ_DEFAULT;
    t->sys_hz = 12000000u; t->cpu.cyc_mult = cpu_cost(t, t->sys_hz);
    t->adc_done = ~0ull; t->adc_value = 0x7800;
    t->soc_off = 0;
    t->lcd_period = t->cpu_hz / 60; t->lcd_next = now + t->lcd_period;
    t->lcd_on = 0; t->lcd_cmd = 0;
    t->ur_tx_evt = ~0ull; t->ur_tx_busy_until = 0; t->ur_rx_r = t->ur_rx_w = 0;
    t->aud_cyc = now; t->aud_phase = 0;
    t->next_event = ~0ull;
    rtc_sched(t);
    events_recalc(t);
    t->cpu.irq_line = t->cpu.fiq_line = 0;
    t->cpu.halted = 0;
    arm7_reset(&t->cpu, 0x20000048);
    t->power = 1;
    t->boot_count++;
}

/* power the SoC off: CPU stops, only the battery-backed RTC keeps running */
static void power_off(tg18_t *t) {
    if (!t->power) return;
    t->power = 0;
    t->lcd_on = 0;
    t->cpu.irq_line = t->cpu.fiq_line = 0;
    if (t->trace) tlog(t, "POWER OFF at %llu", (unsigned long long)t->cpu.cycles);
}
static void power_on(tg18_t *t) {
    if (t->power) return;
    if (t->trace) tlog(t, "POWER ON at %llu", (unsigned long long)t->cpu.cycles);
    soc_reset(t);
}

void tg18_reset(tg18_t *t) { t->power = 0; soc_reset(t); }

arm7_t *tg18_cpu(tg18_t *t) { return &t->cpu; }
int tg18_frame_count(tg18_t *t) { return t->frames; }
void tg18_set_trace(tg18_t *t, int on) { t->trace = on; }

/* run until cycle target, servicing events */
static void off_run(tg18_t *t, uint64_t target);
void tg18_run_cycles(tg18_t *t, uint64_t target) {
    rtc_rebase(t);
    /* keep awake: hold the idle counter below the sleep limit, as if a button had
       been touched a few seconds ago. Everything that happens earlier still does. */
    while (t->cpu.cycles < target) {
        if (!t->power) { off_run(t, target); continue; }
        /* checked on every event, so it also holds when the clock runs fast */
        if (t->keep_awake && t->idle_addr) {
            uint32_t io = t->idle_addr - 0xF8000000u, ic = t18_sram_rd(io, 1);
            if (ic >= 25 && ic < 0x80) t18_sram_wr(io, 15, 1);
        }
        uint64_t stop = target < t->next_event ? target : t->next_event;
        if (stop > t->cpu.cycles) arm7_run(&t->cpu, stop);
        if (t->cpu.halted && t->cpu.cycles < stop) t->cpu.cycles = stop;
        if (t->cpu.cycles >= t->next_event) events_run(t);
    }
}
void tg18_run_us(tg18_t *t, uint32_t us) {
    tg18_run_cycles(t, t->cpu.cycles + (uint64_t)us * t->cpu_hz / 1000000u);
}
uint64_t tg18_cycles(tg18_t *t) { return t->cpu.cycles; }

void tg18_set_buttons(tg18_t *t, int mask) { t->buttons = mask; }

/* ---------------- state ---------------- */
#define ST_MAGIC 0x54313853u  /* 'S81T' */
#define ST_VERSION 1u
#include <stddef.h>
static uint32_t st_len(void) { return (uint32_t)(offsetof(tg18_t, st_end) - offsetof(tg18_t, st_begin)); }
uint32_t tg18_state_size(tg18_t *t) { (void)t; return 12 + sizeof(arm7_state_t) + st_len(); }
int tg18_save_state(tg18_t *t, uint8_t *buf, uint32_t len) {
    if (len < tg18_state_size(t)) return -1;
    uint32_t h[3] = {ST_MAGIC, ST_VERSION, st_len()};
    memcpy(buf, h, 12);
    arm7_state_t cs; arm7_save(&t->cpu, &cs);
    memcpy(buf + 12, &cs, sizeof cs);
    memcpy(buf + 12 + sizeof cs, &t->st_begin, st_len());
    return (int)tg18_state_size(t);
}
int tg18_load_state(tg18_t *t, const uint8_t *buf, uint32_t len) {
    uint32_t h[3];
    if (len < 12) return -1;
    memcpy(h, buf, 12);
    if (h[0] != ST_MAGIC || h[1] != ST_VERSION || h[2] != st_len() || len < tg18_state_size(t)) return -2;
    arm7_state_t cs; memcpy(&cs, buf + 12, sizeof cs);
    arm7_load(&t->cpu, &cs);
    memcpy(&t->st_begin, buf + 12 + sizeof cs, st_len());
    t->cpu.cyc_mult = cpu_cost(t, t->sys_hz ? t->sys_hz : 12000000u);
    intc_update(t);
    t->fb_valid = 0; t->fb_dirty = 0; t->fb_last = 0;
    t->fb_pc_n = 0;
#ifndef PARADISE
    memset(t->fb_raw, 0, sizeof t->fb_raw);
#endif
    /* Up to 1.7 the DMA was not emulated: the 2018 firmwares (JP v030/v031) took a
       channel for every flash write and never got it back. Transfers now finish at
       once, so none is ever running here: give all channels back. */
    if (t->dma_tab) for (uint32_t i = 0; i < 0x14; i += 4) t18_sram_wr(t->dma_tab - 0xF8000000u + i, 0, 4);
#ifdef T18_JIT
    t18_jit_reset();
#endif
    R(0xD03001FC) = 0; set_irq(t, 24, 0);
    return 0;
}
int tg18_flash_dirty(tg18_t *t) { return t->flash_dirty; }
void tg18_flash_clean(tg18_t *t) { t->flash_dirty = 0; }
/* ---------------- host helpers ---------------- */
/* days since firmware epoch (day 0 = 2007-12-31) for a civil date */
static int64_t days_from_civil(int y, int m, int d) {
    y -= m <= 2;
    int64_t era = (y >= 0 ? y : y - 399) / 400;
    int64_t yoe = y - era * 400;
    int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    int64_t unix_days = era * 146097 + doe - 719468;       /* days since 1970-01-01 */
    return unix_days - 13878;                               /* 2007-12-31 is unix day 13878 */
}
extern int tg18_uhr_wecken;
/* set the battery RTC to local wall time. If the SoC is running, its RTC is adjusted too. */
void tg18_set_time(tg18_t *t, int y, int mo, int d, int h, int mi, int s) {
    int64_t days = days_from_civil(y, mo, d);
    int64_t secs = days * 86400 + h * 3600 + mi * 60 + s;
    int64_t tk = (int64_t)ticks32k(t, t->cpu.cycles);
    int64_t old = t->rtcm_off + tk;
    t->rtcm_off = (secs << 15) - tk;
    t->rtcm_last_sec = (uint64_t)secs;
    /* the firmware's own day counter follows only midnight: move it by the days changed */
    if (t->softrtc) {
        int64_t oldday = (old >> 15) / 86400, newday = secs / 86400;
        uint32_t dc = t->softrtc + 0x2C - 0xF8000000u;
        int32_t v = (int32_t)t18_sram_rd(dc, 4);
        if (v > 0 && v < 200000 && newday != oldday) { v += (int32_t)(newday - oldday); t18_sram_wr(dc, (uint32_t)v, 4); }
    }
    if (t->power) {
        /* shift the SoC clock by the same amount so the running game sees the new time */
        int64_t delta = (secs << 15) - old;
        t->soc_off += delta;
        rtc_sched(t); events_recalc(t);
    } else if (tg18_uhr_wecken && (t->rtcm[0x50] & 0x06)) {
        /* Asleep, the console waits for an alarm at a time of day. After a jump that
           time is gone (forward: until tomorrow, back: up to the old time), and the Tama
           stands still meanwhile. Wake it the normal way so it sets its next alarm. */
        rtcm_alarm_fire(t);
    }
}
int tg18_uhr_wecken = 1;
/* read battery RTC as local wall time: y,mo,d,h,mi,s */
void tg18_get_time(tg18_t *t, int *out6) {
    int64_t secs = (int64_t)(rtcm_counter(t) >> 15);
    int64_t days = secs / 86400, rem = secs % 86400;
    int64_t z = days + 13878 + 719468;
    int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    int64_t doe = z - era * 146097;
    int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int64_t y = yoe + era * 400;
    int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    int64_t mp = (5 * doy + 2) / 153;
    int64_t d = doy - (153 * mp + 2) / 5 + 1;
    int64_t m = mp + (mp < 10 ? 3 : -9);
    out6[0] = (int)(y + (m <= 2)); out6[1] = (int)m; out6[2] = (int)d;
    out6[3] = (int)(rem / 3600); out6[4] = (int)(rem / 60 % 60); out6[5] = (int)(rem % 60);
}
/* let offline time pass (app was not running): advance the RTC and fire a pending alarm */
void tg18_advance_offline(tg18_t *t, int64_t seconds) {
    if (seconds <= 0) return;
    t->rtcm_off += seconds << 15;
    if (t->power) {
        t->soc_off += seconds << 15;
        rtc_sched(t); events_recalc(t);
    } else if (t->rtcm[0x50] & 0x06) {
        t->rtcm_last_sec = rtcm_sec_at(t, t->cpu.cycles);
        rtcm_alarm_fire(t);
    }
}
int tg18_power(tg18_t *t) { return t->power; }
/* addresses tg18_init finds in the firmware, when the platform found them before (no scan) */
void tg18_set_addrs(tg18_t *t, uint32_t idle_addr, uint32_t softrtc, uint32_t dma_tab) { t->idle_addr = idle_addr; t->softrtc = softrtc; t->dma_tab = dma_tab; }
void tg18_get_addrs(tg18_t *t, uint32_t *out3) { out3[0] = t->idle_addr; out3[1] = t->softrtc; out3[2] = t->dma_tab; }
void tg18_set_param_addr(tg18_t *t, uint32_t a) { t->param_addr = a; }
/* attention call: low nibble of the game's call flags (Param+0x0C) */
int tg18_attention(tg18_t *t) {
    if (!t->param_addr) return 0;
    if (!t->power) return t->last_attention;
    uint32_t a = t->param_addr + 0x0C;
    if (a < 0xF8000000u || a >= 0xF8000000u + T18_SRAM_SIZE - 3) return 0;
    uint32_t v = t18_sram_rd(a - 0xF8000000u, 4);
    t->last_attention = (v & 0x0F) != 0;
    return t->last_attention;
}
/* backlight brightness 0..255 from timer B PWM */
int tg18_backlight(tg18_t *t) {
    if (!t->power) return 0;
    uint32_t ctrl = TREG(1, 0), pwm = TREG(1, 4);
    if (!(ctrl & 0x2000) || !(pwm & 0x8000)) return 255;
    uint32_t pre = TREG(1, 8) & 0xFFFF, cmp = TREG(1, 0xC) & 0xFFFF;
    uint32_t period = 0x10000 - pre;
    if (!period) return 255;
    uint32_t high = cmp > pre ? 0x10000 - cmp : period;
    int lvl = (int)(high * 255 / period);
    return lvl;
}
uint32_t tg18_sysclk(tg18_t *t) { return t->sys_hz; }
uint32_t tg18_boot_count(tg18_t *t) { return t->boot_count; }

/* 16 bit value in the game's Param block (e.g. +0x20 = Gotchi points); -1 if unknown */
int tg18_param_get16(tg18_t *t, uint32_t off) {
    if (!t->param_addr) return -1;
    return arm7_read8(&t->cpu, t->param_addr + off) | (arm7_read8(&t->cpu, t->param_addr + off + 1) << 8);
}
int tg18_param_set16(tg18_t *t, uint32_t off, int v) {
    if (!t->param_addr) return -1;
    arm7_write8(&t->cpu, t->param_addr + off, (uint8_t)v);
    arm7_write8(&t->cpu, t->param_addr + off + 1, (uint8_t)(v >> 8));
    return 0;
}

/* game clock speed: 1 = real time, up to 600. Only the clocks run faster;
   CPU, picture and sound keep their normal pace. */
void tg18_set_clock_mult(tg18_t *t, int mult) {
    if (mult < 1) mult = 1;
    if (mult > 600) mult = 600;
    if ((uint32_t)mult == (t->clock_mult ? t->clock_mult : 1)) return;
    /* exact rebase: tick count at this very cycle stays the same */
    uint64_t now = t->cpu.cycles;
    uint64_t tk = ticks32k(t, now);
    t->rtc_cyc0 = cyc_of_tick(t, tk); if (t->rtc_cyc0 > now) t->rtc_cyc0 = now;
    t->rtc_tick0 = tk;
    t->clock_mult = (uint32_t)mult;
    if (t->power) { rtc_sched(t); events_recalc(t); }
}
int tg18_clock_mult(tg18_t *t) { return t->clock_mult ? (int)t->clock_mult : 1; }

void tg18_set_keep_awake(tg18_t *t, int on) { t->keep_awake = on ? 1 : 0; }
uint32_t tg18_idle_addr(tg18_t *t) { return t->idle_addr; }
int tg18_picture_count(tg18_t *t) { return (int)t->pres_seq; }
void tg18_set_cpu_boost(tg18_t *t, int k) {
    if (k < 1) k = 1;
    if (k > 8) k = 8;
    t->cpu_boost = k;
    t->cpu.cyc_mult = cpu_cost(t, t->sys_hz ? t->sys_hz : 12000000u);
}

/* Game seconds until the battery RTC alarm wakes the sleeping device, -1 if none.
   (The phone can sleep meanwhile and only needs to be awake at that moment.) */
int tg18_next_wake_s(tg18_t *t) {
    if (t->power || !(t->rtcm[0x50] & 0x06)) return -1;
    uint64_t s = rtcm_sec_at(t, t->cpu.cycles);
    uint64_t n = rtcm_next_alarm_sec(t, s);
    if (n < s) return 0;
    return (n - s > 0x7FFFFFFF) ? 0x7FFFFFFF : (int)(n - s);
}
