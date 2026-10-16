/* Peripherals based on tamaemu periph.c, without infrared/NFC/sound protocols and
 * without floating point. Time comes from counted cycles. */
#include "tc.h"
#define CMU_BASE 0x301B00u
#define IOB(a) e->ioram[(a) - e->dev.io_base]
#define IR_BASE  0x300B10u
#define IR_RXDATA (IR_BASE + 1u)
#define IR_STAT  (IR_BASE + 2u)
#define IR_LAST  (IR_BASE + 7u)
#define SIO_RX   0x301700u
#define SIO_STAT 0x301714u

static uint32_t ioram_r32(Emu *e, uint32_t a)
{
    uint8_t *p = e->ioram + (a - e->dev.io_base);
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static void ioram_w32(Emu *e, uint32_t a, uint32_t v)
{
    uint8_t *p = e->ioram + (a - e->dev.io_base);
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

static void cmu_recalc(Emu *e)
{
    Cmu *c = &e->cmu;
    uint32_t oscsel  = (c->sccr >> 2) & 3;
    uint32_t osc3div = (c->sccr >> 8) & 7;
    uint32_t mclkdiv = (c->sccr >> 12) & 1;
    uint32_t pllindiv = (c->sccr >> 20) & 0xF;
    uint32_t pllpowr = c->pllc & 1;
    uint32_t plln    = (c->pllc >> 4) & 0xF;
    uint32_t f;
    if (oscsel == 1) f = 32768u;
    else if (oscsel == 3 && pllpowr) f = c->osc3_hz / (pllindiv + 1) * (plln + 1);
    else f = c->osc3_hz >> osc3div;
    if (mclkdiv) f >>= 1;
    c->mclk_hz = f ? f : 1;
}

static uint32_t mclk(Emu *e) { return e->cmu.mclk_hz ? e->cmu.mclk_hz : e->dev.osc3_hz; }

static void cmu_write32(Emu *e, uint32_t a, uint32_t v)
{
    Cmu *c = &e->cmu;
    uint32_t off = a - CMU_BASE;
    if (off == 0x24) { c->unlocked = ((v & 0xFF) == 0x96); ioram_w32(e, a, v); return; }
    if (off <= 0x14 && !c->unlocked) return;
    ioram_w32(e, a, v);
    switch (off) {
    case 0x08: c->sccr = v; cmu_recalc(e); break;
    case 0x0C: c->pllc = v; cmu_recalc(e); break;
    case 0x14: c->opt = v; break;
    default: break;
    }
}

static void itc_scan(Emu *e);

void periph_write32(Emu *e, uint32_t a, uint32_t v)
{
    if (a - CMU_BASE <= 0x24) { cmu_write32(e, a, v); return; }
    if (a == 0x301900) { if (v & 1) IOB(0x301900) &= (uint8_t)~1u; return; }
    ioram_w32(e, a, v);
}

uint32_t periph_read32(Emu *e, uint32_t a)
{
    if (a == SIO_STAT) return ioram_r32(e, a) & ~0x40u;
    if (a == SIO_RX) { uint32_t b = ioram_r32(e, a); IOB(IR_STAT) &= (uint8_t)~0x01u; return b; }
    return ioram_r32(e, a);
}

#ifdef IOLOG
extern int g_iolog;
#endif
void periph_write8(Emu *e, uint32_t a, uint8_t v)
{
#ifdef IOLOG
    if (g_iolog) printf("IO %.4f %06x <- %02x pc=%08x\n", (double)e->cycles / e->dev.osc3_hz, a, v, e->pc);
#endif
    if (a - CMU_BASE <= 0x27) {
        uint32_t word = a & ~3u;
        uint32_t cur = ioram_r32(e, word);
        int sh = (a & 3) * 8;
        cmu_write32(e, word, (cur & ~(0xFFu << sh)) | ((uint32_t)v << sh));
        return;
    }
    if (a >= 0x300280 && a <= 0x30028F) { e->ioram[a - e->dev.io_base] &= (uint8_t)~v; itc_scan(e); return; }
    if (a == 0x3002A9) { e->ioram[a - e->dev.io_base] &= (uint8_t)~v; itc_scan(e); return; }
    if (a == 0x301900) { if (v & 1) e->ioram[a - e->dev.io_base] &= (uint8_t)~1u; return; }
    if (a == 0x300544 && (v & 0x06)) {     /* battery measurement: immediate, healthy */
        e->ioram[a - e->dev.io_base] = v;
        IOB(0x300540) = 0x50; IOB(0x300541) = 0x03;
        IOB(0x300548) = 0x50; IOB(0x300549) = 0x03;
        IOB(0x300287) |= 2;
        itc_scan(e);
        return;
    }
    if (a >= 0x300780 && a <= 0x3007AF) {
        int x = (int)((a - 0x300780) / 8);
        int off = (int)((a - 0x300780) % 8);
        if (off == 6) {
            if (v & 2) { e->t16[x].counter = 0; e->t16[x].accum = 0; }
            e->ioram[a - e->dev.io_base] = v & (uint8_t)~2u;
            return;
        }
        if (off == 4) {
            e->t16[x].counter = (uint16_t)((e->t16[x].counter & 0xFF00) | v);
            e->t16[x].a_fired = 0;
            e->ioram[a - e->dev.io_base] = v;
            return;
        }
        if (off == 5) {
            e->t16[x].counter = (uint16_t)((e->t16[x].counter & 0x00FF) | ((uint16_t)v << 8));
            e->ioram[a - e->dev.io_base] = v;
            return;
        }
    }
    e->ioram[a - e->dev.io_base] = v;
    if ((a >= 0x300270 && a <= 0x30027F) || a == 0x3002A6) itc_scan(e);
}

uint8_t periph_read8(Emu *e, uint32_t a)
{
    if (a == 0x300380) {
        uint8_t p06 = e->dev.debug_strap_high ? 0x00 : 0x40;   /* charger, no debug */
        uint8_t v = (uint8_t)((0x07u | p06) & ~(e->btn_mask & 7));
        if (e->dev.ir_gpio) v |= (uint8_t)(1u << e->dev.ir_gpio_rx_bit);  /* IR idles high */
        return v;
    }
    if (a == IR_RXDATA) { uint8_t v = e->ioram[a - e->dev.io_base]; IOB(IR_STAT) &= (uint8_t)~0x01u; return v; }
    if (a == IR_STAT) return (uint8_t)(e->ioram[a - e->dev.io_base] | 0x02u);
    return e->ioram[a - e->dev.io_base];
}

void periph_buttons(Emu *e, uint8_t mask)
{
    uint8_t newly = mask & (uint8_t)~e->btn_mask & 7;
    e->btn_mask = mask & 7;
    if (newly) { IOB(0x300280) |= newly; itc_scan(e); }
}

static const uint32_t presc_div[8] = {1, 2, 4, 16, 64, 256, 1024, 4096};

static void t16_set_flag(Emu *e, int x, int is_a)
{
    uint32_t freg = 0x300282 + (uint32_t)(x / 2);
    int bit = (x & 1) ? (is_a ? 7 : 6) : (is_a ? 3 : 2);
    IOB(freg) |= (uint8_t)(1u << bit);
}

/* Like the counting loop in the original, but in jumps instead of cycle by cycle. */
static void t16_run(Emu *e, int x, uint32_t clocks)
{
    uint32_t base = 0x300780 + (uint32_t)x * 8;
    uint16_t cra = (uint16_t)(IOB(base) | (IOB(base + 1) << 8));
    uint16_t crb = (uint16_t)(IOB(base + 2) | (IOB(base + 3) << 8));
    T16 *t = &e->t16[x];
    uint32_t c = t->counter;
    while (clocks) {
        uint32_t to_b = (crb > c) ? (uint32_t)crb - c : 1u;
        if (clocks < to_b) {
            c += clocks; clocks = 0;
            if (cra && !t->a_fired && c >= cra) { t16_set_flag(e, x, 1); t->a_fired = 1; }
            break;
        }
        c += to_b; clocks -= to_b;
        if (cra && !t->a_fired && c >= cra) t16_set_flag(e, x, 1);
        t16_set_flag(e, x, 0);
        c = 0; t->a_fired = 0;
        if (crb && clocks >= crb) {          /* whole periods: flags are already set */
            if (cra && cra <= crb) t16_set_flag(e, x, 1);
            clocks %= crb;
        }
    }
    t->counter = (uint16_t)c;
    IOB(base + 4) = (uint8_t)c;
    IOB(base + 5) = (uint8_t)(c >> 8);
}

static void itc_consider(Emu *e, uint32_t vec, uint8_t lvl)
{
    e->wake_req = true;
    if (lvl > e->pending_level || !e->pending_irq) { e->pending_irq = vec; e->pending_level = lvl; }
}

static void itc_scan(Emu *e)
{
    static const uint8_t vec_b[6] = {30, 34, 38, 42, 46, 50};
    e->pending_irq = 0; e->pending_level = 0; e->wake_req = false;
    {
        uint8_t act = IOB(0x300270) & IOB(0x300280) & 0x0F;
        for (int p = 0; p < 4; p++) {
            if (!(act & (1u << p))) continue;
            uint8_t lr = IOB(0x300260 + (uint32_t)(p / 2));
            uint8_t lvl = (p & 1) ? (lr >> 4) & 7 : lr & 7;
            itc_consider(e, 16u + (uint32_t)p, lvl);
        }
    }
    for (int x = 0; x < 6; x++) {
        uint32_t en = 0x300272 + (uint32_t)(x / 2), fl = 0x300282 + (uint32_t)(x / 2);
        uint8_t act = IOB(en) & IOB(fl);
        int b_bit = (x & 1) ? 6 : 2, a_bit = (x & 1) ? 7 : 3;
        uint8_t lvl_reg = IOB(0x300266 + (uint32_t)(x / 2));
        uint8_t lvl = (x & 1) ? (lvl_reg >> 4) & 7 : lvl_reg & 7;
        if (act & (1u << b_bit)) itc_consider(e, vec_b[x], lvl);
        if (act & (1u << a_bit)) itc_consider(e, vec_b[x] + 1u, lvl);
    }
    if (IOB(0x300277) & IOB(0x300287) & 4) { uint8_t lvl = (IOB(0x30026A) >> 4) & 7; itc_consider(e, 65, lvl ? lvl : 1); }
    if (IOB(0x300277) & IOB(0x300287) & 2) { uint8_t lvl = IOB(0x30026A) & 7; itc_consider(e, 64, lvl ? lvl : 1); }
    if (IOB(0x300277) & IOB(0x300287) & 0x08u) itc_consider(e, 68, 4);
    {
        uint8_t act = IOB(0x300276) & IOB(0x300286);
        for (int b = 3; b <= 5; b++)
            if (act & (uint8_t)(1u << b)) itc_consider(e, (uint32_t)(57 + b), 4);
    }
}

static uint8_t bcd_inc(uint8_t v, uint8_t wrap_after)
{
    v++;
    if ((v & 0xF) > 9) v = (uint8_t)((v & 0xF0) + 0x10);
    if (v > wrap_after) return 0xFF;
    return v;
}
static const uint8_t month_days[13] = {0,0x31,0x28,0x31,0x30,0x31,0x30,0x31,0x31,0x30,0x31,0x30,0x31};

static void rtc_count_second(Emu *e)
{
    uint8_t v = bcd_inc(IOB(0x301910), 0x59);
    IOB(0x301910) = (v == 0xFF) ? 0 : v; if (v != 0xFF) return;
    v = bcd_inc(IOB(0x301914), 0x59);
    IOB(0x301914) = (v == 0xFF) ? 0 : v; if (v != 0xFF) return;
    v = bcd_inc(IOB(0x301918), 0x23);
    IOB(0x301918) = (v == 0xFF) ? 0 : v; if (v != 0xFF) return;
    IOB(0x301928) = (uint8_t)((IOB(0x301928) + 1) % 7);
    uint8_t mon = IOB(0x301920);
    int mi = ((mon >> 4) & 1) * 10 + (mon & 0xF);
    uint8_t dim = (mi >= 1 && mi <= 12) ? month_days[mi] : 0x31;
    if (mi == 2 && ((IOB(0x301924) & 0xF) % 4 == 0)) dim = 0x29;
    v = bcd_inc(IOB(0x30191C), dim);
    IOB(0x30191C) = (v == 0xFF) ? 1 : v; if (v != 0xFF) return;
    v = bcd_inc(IOB(0x301920), 0x12);
    IOB(0x301920) = (v == 0xFF) ? 1 : v; if (v != 0xFF) return;
    v = bcd_inc(IOB(0x301924), 0x99);
    IOB(0x301924) = (v == 0xFF) ? 0 : v;
}

/* Set the guest clock to local time t (seconds since 1970), e.g. the Paradise's time at the start.
 * The weekday moves by the days between the old date and the new one. */
static uint8_t to_bcd(uint32_t v) { return (uint8_t)(((v / 10u) << 4) | (v % 10u)); }
static uint32_t from_bcd(uint8_t v) { return (uint32_t)(v >> 4) * 10u + (v & 15u); }
static int32_t days_from(int32_t y, int32_t m, int32_t d)
{
    y -= m <= 2; int32_t era = (y >= 0 ? y : y - 399) / 400, yoe = y - era * 400;
    int32_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1, doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}
void periph_rtc_set(Emu *e, uint32_t t)
{
    if ((IOB(0x301908) & 2) || (IOB(0x30190C) & 1)) return;     /* stopped or being set by the game */
    uint32_t days = t / 86400u, sod = t % 86400u;
    uint32_t z = days + 719468u, era = z / 146097u, doe = z - era * 146097u;
    uint32_t yoe = (doe - doe / 1460u + doe / 36524u - doe / 146096u) / 365u, doy = doe - (365u * yoe + yoe / 4u - yoe / 100u);
    uint32_t mp = (5u * doy + 2u) / 153u, d = doy - (153u * mp + 2u) / 5u + 1u, m = mp < 10u ? mp + 3u : mp - 9u, y = yoe + era * 400u + (m <= 2u);
    uint32_t oy = 2000u + from_bcd(IOB(0x301924)), om = from_bcd(IOB(0x301920)), od = from_bcd(IOB(0x30191C)), wd;
    if (om >= 1u && om <= 12u && od >= 1u && od <= 31u && IOB(0x301928) < 7u) {
        int32_t diff = (int32_t)days - days_from((int32_t)oy, (int32_t)om, (int32_t)od);
        wd = (uint32_t)(((int32_t)IOB(0x301928) + diff % 7 + 7) % 7);
    } else wd = (days + 4u) % 7u;                                   /* 1970-01-01 was a Thursday (0 = Sunday) */
    IOB(0x301910) = to_bcd(sod % 60u); IOB(0x301914) = to_bcd((sod / 60u) % 60u); IOB(0x301918) = to_bcd(sod / 3600u);
    IOB(0x30191C) = to_bcd(d); IOB(0x301920) = to_bcd(m); IOB(0x301924) = to_bcd(y % 100u); IOB(0x301928) = (uint8_t)wd;
    e->rtc_cyc = 0;
}

/* Advance the guest clock by n seconds (after a pause). */
void periph_rtc_advance(Emu *e, uint32_t n)
{
    if ((IOB(0x301908) & 2) || (IOB(0x30190C) & 1)) return;
    while (n--) rtc_count_second(e);
}

static void rtc_tick(Emu *e, uint32_t cycles)
{
    uint32_t hz = mclk(e);
    if (!(IOB(0x301908) & 2) && !(IOB(0x30190C) & 1)) {
        e->rtc_cyc += cycles;
        while (e->rtc_cyc >= hz) { e->rtc_cyc -= hz; rtc_count_second(e); }
    }
    uint32_t sel = (IOB(0x301904) >> 2) & 3;
    uint64_t p = sel == 0 ? hz / 64 : sel == 1 ? hz : sel == 2 ? (uint64_t)hz * 60 : (uint64_t)hz * 3600;
    e->rtc_irq_cyc += cycles;
    if (e->rtc_irq_cyc >= p) {
        e->rtc_irq_cyc -= (uint32_t)(p > 0xFFFFFFFFu ? 0xFFFFFFFFu : p);
        IOB(0x301900) |= 1;
        if (IOB(0x301904) & 1) IOB(0x300287) |= 4;
    }
}

void periph_tick(Emu *e, uint32_t cycles)
{
    rtc_tick(e, cycles);
    for (int x = 0; x < 6; x++) {
        uint8_t c = IOB(0x300780 + (uint32_t)x * 8 + 6);
        if (!(c & 1)) continue;
        T16 *t = &e->t16[x];
        uint8_t ck = IOB(0x3007E0 + (uint32_t)x * 2);
        if (!(ck & 8)) continue;
        t->accum += cycles;
        uint32_t div = presc_div[ck & 7];
        uint32_t clocks = t->accum / div;
        t->accum -= clocks * div;
        if (clocks) t16_run(e, x, clocks);
    }
    itc_scan(e);
}

/* Cycles until the next peripheral event (timers, clock). */
uint32_t periph_next(Emu *e)
{
    uint64_t best = 0xFFFFFFFFu;
    uint32_t hz = mclk(e);
    if ((IOB(0x301904) & 1) && (IOB(0x300277) & 4)) {
        uint32_t sel = (IOB(0x301904) >> 2) & 3;
        uint64_t p = sel == 0 ? hz / 64 : sel == 1 ? hz : sel == 2 ? (uint64_t)hz * 60 : (uint64_t)hz * 3600;
        if (p > e->rtc_irq_cyc && p - e->rtc_irq_cyc < best) best = p - e->rtc_irq_cyc;
        else if (p <= e->rtc_irq_cyc) best = 1;
    }
    for (int x = 0; x < 6; x++) {
        uint8_t c = IOB(0x300780 + (uint32_t)x * 8 + 6);
        if (!(c & 1)) continue;
        uint8_t ck = IOB(0x3007E0 + (uint32_t)x * 2);
        if (!(ck & 8)) continue;
        uint8_t en = IOB(0x300272 + (uint32_t)(x / 2));
        int ea = (en >> ((x & 1) ? 7 : 3)) & 1, eb = (en >> ((x & 1) ? 6 : 2)) & 1;
        if (!ea && !eb) continue;
        uint32_t base = 0x300780 + (uint32_t)x * 8;
        uint32_t cra = (uint32_t)(IOB(base) | (IOB(base + 1) << 8));
        uint32_t crb = (uint32_t)(IOB(base + 2) | (IOB(base + 3) << 8));
        T16 *t = &e->t16[x];
        uint32_t cnt = t->counter;
        uint32_t n = (crb > cnt) ? crb - cnt : 1u;
        if (!eb) n = 0xFFFFFu;
        if (ea && cra && !t->a_fired && cra > cnt && cra - cnt < n) n = cra - cnt;
        if (n == 0xFFFFFu) continue;
        uint64_t cyc = (uint64_t)n * presc_div[ck & 7];
        cyc = cyc > t->accum ? cyc - t->accum : 1;
        if (cyc < best) best = cyc;
    }
    return best ? (uint32_t)best : 1u;
}

/* Guest sound: T16 channel 0 drives the buzzer. If it runs and its output
 * toggles (compare A below B), the frequency is clock / divider / (B+1). */
uint32_t periph_tone_hz(Emu *e)
{
    uint8_t c = IOB(0x300786), ck = IOB(0x3007E0);
    if (!(c & 1) || !(ck & 8)) return 0;
    uint32_t cra = (uint32_t)(IOB(0x300780) | (IOB(0x300781) << 8));
    uint32_t crb = (uint32_t)(IOB(0x300782) | (IOB(0x300783) << 8));
    if (!cra || cra >= crb) return 0;
    uint32_t hz = mclk(e) / presc_div[ck & 7] / (crb + 1);
    return (hz >= 20 && hz <= 12000) ? hz : 0;
}

void periph_init(Emu *e)
{
    e->cmu.osc3_hz = e->dev.osc3_hz;
    e->cmu.mclk_hz = 0;
}
