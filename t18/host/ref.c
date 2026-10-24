/* PC run of the T18 core with the Paradise memory map (reference for the JIT and the device).
 * ref <rom> <script> [csv]
 * script: wait <ms> | press <A|B|C|AB..> [ms] | shot [file] | hash | state <file> | time y m d h mi s */
#include "../src/core/t18soc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>
#include <unistd.h>

static uint8_t *g_flash;
uint8_t t18_flash_rd(uint32_t a) { return g_flash[a & 0x7FFFFF]; }
void t18_flash_prog(uint32_t a, uint8_t b) { g_flash[a & 0x7FFFFF] &= b; }
void t18_flash_erase(uint32_t a, uint32_t size) { memset(g_flash + (a & 0x7FFFFF), 0xFF, size); }

static tg18_t *T;
#ifdef T18_PROF
static uint32_t *g_armh; static int g_profon;
static FILE *g_tr; static uint64_t g_trend, g_trbeg;
void t18_prof(uint32_t pc, uint32_t thumb) {
    if (g_profon) g_armh[(pc >> 2) & 0x3FFFFF]++;
    if (g_tr) { arm7_t *c = tg18_cpu(T); if (c->cycles > g_trend || c->cycles < g_trbeg) return;
        fprintf(g_tr, "%llu", (unsigned long long)c->cycles); for (int i = 0; i < 16; i++) fprintf(g_tr, " %x", c->r[i]); fprintf(g_tr, " %x %lx\n", c->cpsr & 0xF000003Fu, crc32(0, t18_sram, 0x1F000)); }
}
#endif
static int nshot;
#ifdef T18_WMAP
uint8_t t18_wmap[512];
#endif
#ifdef T18_IDLE_DBG
static unsigned g_why[8];
extern uint32_t t18_w_bad_addr;
void t18_idle_dbg(int why, uint32_t p0, arm7_t *c, const uint32_t *r0) {
    g_why[why]++;
    if (c->cycles > 1000000ull && (g_why[why] % 20) == 1) {
        fprintf(stderr, "why %d p0 %08x now %08x bad %08x:", why, p0, c->r[15], t18_w_bad_addr);
        for (int i = 0; i < 16; i++) if (c->r[i] != r0[i]) fprintf(stderr, " r%d %x->%x", i, r0[i], c->r[i]);
        fprintf(stderr, "\n");
    }
}
#endif
#ifdef T18_PSTAT
static uint64_t ps_r[512], ps_w[512]; static uint8_t ps_nz[512];
static uint32_t sp_min[32]={0}, sp_max[32]; static uint32_t sp_hist[512];
void *t18_cur_cpu;
void t18_acnt(uint32_t a, uint32_t pc, int wr) { (void)pc; if (t18_cur_cpu) { arm7_t *cc = t18_cur_cpu; uint32_t m = cc->cpsr & 31, sp = cc->r[13]; if (!sp_min[m] || sp < sp_min[m]) sp_min[m] = sp; if (sp > sp_max[m]) sp_max[m] = sp; if (sp - 0xF8000000u < 0x20000u) sp_hist[(sp - 0xF8000000u) >> 8]++; } uint32_t o = a - 0xF8000000u; if (o < 0x20000u) { if (wr) ps_w[o >> 8]++; else ps_r[o >> 8]++; } }
static void ps_snap(void) { for (int i = 0; i < 512; i++) for (int k = 0; k < 256; k++) { uint32_t o = i * 256 + k; uint8_t v = *t18_sram_rp(o); if (v) { ps_nz[i] = 1; break; } } }
#endif
#if defined(T18_ACNT) && !defined(T18_PSTAT)
#include <stdint.h>
static uint32_t ak[1<<16], av[1<<16]; static uint32_t *kk; static int acn_on;
void t18_acnt(uint32_t a, uint32_t pc, int wr) {
    if (!acn_on) return;
    uint32_t key = (a & 0xFFFFFFFC) | wr; uint32_t h = (key * 2654435761u) >> 16;
    while (ak[h] && ak[h] != key) h = (h + 1) & 0xFFFF;
    ak[h] = key; av[h]++;
    (void)kk;
}
#endif
#ifdef T18_LINK
/* test6: the phone side for the link (as tools/ble/mmphone.py of the Android T18Emu): notifications from the module */
static uint8_t lk_n[4096][24]; static int lk_nl[4096], lk_h, lk_t;
void tg18_link_out(const uint8_t *d, int n) { if (n > 24) n = 24; memcpy(lk_n[lk_t & 4095], d, (size_t)n); lk_nl[lk_t & 4095] = n; lk_t++; }
static void lk_run(int ms) { for (int i = 0; i < ms; i++) tg18_run_us(T, 1000); }
static void lk_send(const uint8_t *d, int n) { int k = 0; for (; k < 20000 && !tg18_ble_from_phone(T, d, n); k++) tg18_run_us(T, 1000); if (k >= 20000) printf("  write not taken (ready %d)\n", tg18_ble_ready(T)); }
/* wait for a notification starting with a, b (ms); returns its index or -1; everything before it is dropped */
static int lk_wait(int a, int b, int ms) {
    for (int i = 0; i <= ms; i++) { while (lk_h < lk_t) { int k = lk_h++ & 4095; if (lk_nl[k] >= 2 && lk_n[k][0] == a && lk_n[k][1] == b) return k; } if (i < ms) tg18_run_us(T, 1000); }
    return -1;
}
static void lk_hex(const char *tag, int k) { printf("%s", tag); if (k < 0) { printf(" none\n"); return; } for (int i = 0; i < lk_nl[k]; i++) printf(" %02X", lk_n[k][i]); printf("\n"); }
/* test7: infrared between two refs (visit of two Paradise): LKR/LKW are pipe fds from the driver; "linkrun <ms>"
 * runs in lockstep, 1 ms at a time, exchanging what each infrared port sent */
static uint8_t ir_o[8192], ir_i[65536]; static int ir_on, ir_ih, ir_it;
void tg18_link_ir_out(uint8_t b) { if (ir_on < (int)sizeof ir_o) ir_o[ir_on++] = b; }
int tg18_link_ir_avail(void) { return ir_ih != ir_it; }
int tg18_link_ir_in(uint8_t *b) { if (ir_ih == ir_it) return 0; *b = ir_i[ir_ih++ & 65535]; return 1; }
static void lk_pairstep(void) {
    static int fr = -2, fw = -1; if (fr == -2) { const char *a = getenv("LKR"), *b = getenv("LKW"); fr = a ? atoi(a) : -1; fw = b ? atoi(b) : -1; }
    if (fr < 0 || fw < 0) return;
    uint16_t n = (uint16_t)ir_on; if (write(fw, &n, 2) != 2 || (n && write(fw, ir_o, n) != n)) exit(3); ir_on = 0;
    uint16_t m; if (read(fr, &m, 2) != 2) exit(3);
    for (int got = 0; got < m;) { uint8_t tmp[512]; int k = read(fr, tmp, m - got > 512 ? 512 : m - got); if (k <= 0) exit(3); for (int i = 0; i < k; i++) ir_i[ir_it++ & 65535] = tmp[i]; got += k; }
}
/* replay session si of a Lauschposten capture: each phone message (F0 8D .. F0 CD) is written, then the device's status
 * F0 0C is awaited, F0 8E 0D asks for its answer F0 0D .. F0 4D */
static void lk_replay(const char *fn, int si) {
    static uint8_t w[3000][24]; static int wl[3000]; int nw = 0, sess = -1, inmsg = 0;
    FILE *f = fopen(fn, "r"); if (!f) { printf("no capture\n"); return; }
    char line[512];
    int start[200], nm = 0;
    while (fgets(line, sizeof line, f)) {
        if (strstr(line, "Benachrichtigung EIN")) { sess++; inmsg = 0; continue; }
        if (sess != si) continue;
        char *q = strstr(line, "<< fff2"); if (!q) continue;
        q = strchr(q, ':'); if (!q) continue; q++;
        uint8_t b[24]; int n = 0; unsigned x; int c;
        while (n < 24 && sscanf(q, " %2x%n", &x, &c) == 1) { b[n++] = (uint8_t)x; q += c; }
        if (n >= 2 && b[0] == 0xF0 && b[1] == 0x8D) { inmsg = 1; start[nm++] = nw; }
        if (n == 3 && b[0] == 0xF0 && b[1] == 0x8E && b[2] == 0x0D) continue;
        if (!inmsg || nw >= 3000) continue;
        memcpy(w[nw], b, (size_t)n); wl[nw++] = n;
        if (n >= 2 && b[0] == 0xF0 && b[1] == 0xCD) inmsg = 0;
    }
    fclose(f); start[nm] = nw;
    printf("session %d: %d messages, %d writes\n", si, nm, nw);
    tg18_ble_connect(T, 1); lk_run(500); printf("  module set up: %d\n", tg18_ble_ready(T));
    for (int m = 0; m < nm; m++) {
        for (int i = start[m]; i < start[m + 1]; i++) { lk_send(w[i], wl[i]); lk_run(2); }
        lk_hex("  status", lk_wait(0xF0, 0x0C, 8000));
        lk_run(700);
        static const uint8_t rq[3] = { 0xF0, 0x8E, 0x0D }; lk_send(rq, 3);
        int h = lk_wait(0xF0, 0x0D, 8000); lk_hex("  answer", h);
        int bytes = 0, e = -1;
        for (int i = 0; i < 20000 && e < 0; i++) { while (lk_h < lk_t) { int k = lk_h++ & 4095; if (lk_nl[k] >= 2 && lk_n[k][0] == 0xF0 && lk_n[k][1] == 0x4D) { e = k; break; } bytes += lk_nl[k]; } if (e < 0) tg18_run_us(T, 1000); }
        printf("  answer data %d bytes\n", bytes); lk_hex("  end", e);
        lk_hex("  status", lk_wait(0xF0, 0x0C, 3000));
    }
    lk_run(2000); tg18_ble_connect(T, 0); lk_run(3000);
}
#endif
static uint32_t fnv(const uint8_t *p, size_t n, uint32_t h) { for (size_t i = 0; i < n; i++) h = (h ^ p[i]) * 16777619u; return h; }
/* hash over guest SRAM 0xF8000000..0xF801FFFF, whatever part is direct */
/* hash over the guest's view of its SRAM (with a hole: its pages, not the host bytes) */
static uint32_t sram_hash(void) { uint32_t h = 2166136261u; for (uint32_t o = 0; o < T18_SRAM_SIZE; o += 256) h = fnv(t18_sram_rp(o), 256, h); return h; }
#if T18_HOLE_HI > T18_HOLE_LO
static uint8_t g_pool[64][T18_HOLE_PG]; static int g_pool_n;
static uint8_t *hole_new(void) { if (g_pool_n >= (getenv("T18_NPOOL") ? atoi(getenv("T18_NPOOL")) : 64)) return 0; uint8_t *p = g_pool[g_pool_n++]; memset(p, 0, T18_HOLE_PG); return p; }
static void hole_free(void) { g_pool_n = 0; }
#endif


#ifdef T18_PANEL_MODEL
/* 1.6.9 (test11): the Paradise's panel on the PC. The core hands the scanned buffer 30 times a second (T18_PRES_PARA);
 * the policy of main.c decides when rows go to the panel (POLICY=old: T18_PRESENT_IDLE+STABLE, POLICY=calm:
 * T18_PRESENT_CALM). pshot writes what the panel shows (backlight off: darkened). */
int t18_new_off; static uint32_t MQ = 16; static int m_xfer; static void m_present(int force); static uint32_t m_ms; static uint32_t m_src, m_len; static uint16_t m_panel[128][128]; static uint32_t m_sum[128]; static int m_on, m_calm;
static void m_present_cb(void *u, uint32_t src, uint32_t len) { (void)u; if (getenv("SRCLOG") && (src != m_src || len != m_len)) fprintf(stderr, "src %08x len %u at %u\n", src, len, m_ms); m_src = src; m_len = len; if (m_xfer && m_on) m_present(0); }
static const uint16_t *m_row(uint32_t a) { uint32_t o = a - 0xF8000000u; static uint16_t tmp[128]; if (o >= T18_SRAM_SIZE - 256u) { memset(tmp, 0, 256); return tmp; } for (int i = 0; i < 256; i++) ((uint8_t *)tmp)[i] = *t18_sram_rp(o + i); return tmp; }
static int m_gate, m_full = 1; static uint32_t m_gate_ms;
static int m_held(void) { uint32_t rows = m_len / 256u, key = 0, black = 0; if (rows > 128) rows = 128;
    for (uint32_t y = 0; y < rows; y++) { const uint16_t *p = m_row(m_src + y * 256u); int z = 1; for (int i = 0; i < 128; i++) { if (p[i] < 0x20u && p[i] && p[i] != 0x000Cu) key++; if (p[i]) z = 0; } black += (uint32_t)z; }
    if (getenv("GDBG") && (key || black)) fprintf(stderr, "gate t=%u key=%u black=%u\n", m_ms, key, black);
    if (key < 8u && black < 8u) { m_gate_ms = 0; return 0; }
    if (!m_gate_ms) m_gate_ms = m_ms ? m_ms : 1; return m_ms - m_gate_ms < 1000u; }
static void m_present(int force) {
    if (!m_len) return;
    if (m_gate && !force && m_held()) return;
    if (m_gate && m_full) force = 1; uint32_t rows = m_len / 256u; if (rows > 128) rows = 128;
    static int fnv = -1; if (fnv < 0) fnv = getenv("FNV") != 0;   /* test14: T18_SUM_FNV */
    for (uint32_t y = 0; y < rows; y++) { const uint16_t *p = m_row(m_src + y * 256u); uint32_t sum = 0x9E3779B9u;
        if (fnv) { sum = 0x811C9DC5u; for (int i = 0; i < 128; i += 2) sum = (sum ^ ((uint32_t)p[i] | (uint32_t)p[i + 1] << 16)) * 0x01000193u; sum ^= sum >> 13; }
        else for (int i = 0; i < 128; i += 2) sum = (sum << 5 | sum >> 27) ^ ((uint32_t)p[i] | (uint32_t)p[i + 1] << 16);
        sum ^= sum >> 16; sum &= 0xFFFF;
        if (force || sum != m_sum[y]) memcpy(m_panel[y], p, 256); m_sum[y] = sum; }
    m_full = 0;
}
static uint32_t m_hash(int step) { uint32_t h = 0x811C9DC5u, rows = m_len / 256u; if (rows > 128) rows = 128;
    for (uint32_t y = 0; y < rows; y++) { const uint16_t *p = m_row(m_src + y * 256u); for (int i = 0; i < 128; i += step) h = (h ^ p[i]) * 16777619u; } return h; }
static int m_cleared(void) { uint32_t rows = m_len / 256u, n = 0; if (rows > 128) rows = 128;
    for (uint32_t y = 0; y < rows; y++) { const uint16_t *p = m_row(m_src + y * 256u); int z = 1; for (int i = 0; i < 128; i++) if (p[i]) { z = 0; break; } n += z; } return n >= 24; }

static void m_tick(void) {
    arm7_t *c = tg18_cpu(T); static uint64_t ic0, cy0; static uint32_t pt;
    int on = tg18_power(T) && tg18_backlight(T) > 0;
    if (m_xfer) { static int bl = -1; m_on = on; if (on != bl) { bl = on; if (on) { if (m_gate) m_full = 1; else m_present(1); } } return; }
    if (!m_calm) {
        static int bl = -1; if (on != bl) { if (getenv("BLLOG")) fprintf(stderr, "bl %d at %u (power %d light %d)\n", on, m_ms, tg18_power(T), tg18_backlight(T)); bl = on; m_on = on; if (on) { if (m_gate) m_full = 1; else m_present(1); } }
        uint64_t ic = c->idle_cycles, cy = c->cycles; uint32_t tn = m_ms; int waited = (ic - ic0) * 4u > cy - cy0;
        static uint32_t ht, h_last;
        if (m_on && m_len && waited && tn - pt >= 33u) { pt = tn; ht = tn; m_present(0); }
        else if (m_on && m_len && tn - ht >= 250u) { ht = tn; uint32_t h = m_hash(3); if ((h == h_last && tn - pt >= 250u) || tn - pt >= 5000u) { pt = tn; m_present(0); } h_last = h; }
        ic0 = ic; cy0 = cy; return;
    }
    /* calm (the same as main.c): every 8 ms a check value of the picture; a picture goes out when the firmware
     * waited in these 8 ms and the picture is the same as 8 ms before, or it holds still for 250 ms; after waking
     * the first one goes out whole, and not while it is still the cleared buffer (2 s at most) */
    static uint32_t h_prev, h_shown, t_shown, t_same, wake_t, n, t_busy; static int was_on, full = 1;
    static uint64_t wi0, wc0;
    m_on = on; if (!on || !m_len) { was_on = 0; ic0 = c->idle_cycles; cy0 = c->cycles; return; }
    if ((++n & 1u)) return;                                   /* every 8 ms */
    uint64_t ic = c->idle_cycles, cy = c->cycles;
    if (getenv("IDBG")) fprintf(stderr, "%u:%d ", m_ms, (int)((ic - wi0) * 100 / (cy - wc0 + 1)));
    int idle = (ic - wi0) * 2u > cy - wc0; wi0 = ic; wc0 = cy;   /* half of these 8 ms or more */ (void)ic0; (void)cy0;   /* idle half of these 16 ms or more */
    uint32_t tn = m_ms;
    if (!idle) t_busy = tn;
    if (!was_on) { was_on = 1; wake_t = tn; full = 1; h_shown = 0; }
    uint32_t h = m_hash(4);
    if (h != h_prev) t_same = tn; h_prev = h;
    if (h == h_shown) return;
    int quiet = (tn - t_busy < 16u) ? (tn - t_same >= 300u) : (tn - t_same >= MQ);   /* busy: only a picture that settled */
    int go = (quiet && tn - t_shown >= 33u) || tn - t_shown >= 1500u;
    if (!go) return;
    if (tn - wake_t < 2000u && m_cleared()) return;
    if (m_cleared() && tn - t_shown < 1000u) return;
    if (getenv("PDBG")) fprintf(stderr, "present t=%u quiet=%d same=%u busy=%u shown=%u\n", tn, quiet, tn - t_same, tn - t_busy, tn - t_shown);
    m_present(full); full = 0; h_shown = h; t_shown = tn; (void)pt;
}
static void m_run_ms(double ms) { static int sp; if (!sp) sp = getenv("SPEED") ? atoi(getenv("SPEED")) : 100; for (uint32_t i = 0; i < (uint32_t)(ms / 4); i++) { tg18_run_us(T, 4000); m_ms += 400u / (uint32_t)sp; m_tick(); } }
static void m_shot(const char *fn) { FILE *o = fopen(fn, "wb"); for (int y = 0; y < 128; y++) for (int x = 0; x < 128; x++) { uint16_t v = m_panel[y][x]; if (!m_on) v = (uint16_t)((v >> 2) & 0x39E7); fwrite(&v, 2, 1, o); } fclose(o); }
#endif
int main(int argc, char **argv)
{
    if (argc < 3) { fprintf(stderr, "ref <rom> <script>\n"); return 1; }
    g_flash = malloc(0x800000); memset(g_flash, 0xFF, 0x800000);
    if (getenv("T18_NOIDLE")) t18_idle_on = 0;
    FILE *f = fopen(argv[1], "rb"); size_t n = fread(g_flash, 1, 0x800000, f); fclose(f);
    t18_sram = calloc(1, T18_SRAM_SIZE);
#if T18_HOLE_HI > T18_HOLE_LO
    memset(t18_sram + T18_HOLE_LO, 0xA5, T18_HOLE_HI - T18_HOLE_LO);   /* not the guest's: must never be used */
    t18_hole_new = hole_new; t18_hole_free = hole_free;
#endif
    for (int i = 0; i < 128; i++) t18_fblk[i] = g_flash + (i << 16);
    T = calloc(1, tg18_struct_size());
#ifdef T18_PSTAT
    { extern void *t18_cur_cpu; t18_cur_cpu = tg18_cpu(T); }
#endif
    tg18_init(T, g_flash, (uint32_t)n);
    tg18_set_time(T, 2026, 10, 15, 8, 0, 0);
#ifdef T18_PANEL_MODEL
    tg18_set_present(T, m_present_cb, 0); m_calm = getenv("POLICY") && !strcmp(getenv("POLICY"), "calm"); if (getenv("MQ")) MQ = (uint32_t)atoi(getenv("MQ")); m_xfer = getenv("POLICY") && !strcmp(getenv("POLICY"), "xfer"); m_gate = getenv("GATE") != 0;
#endif
#ifdef T18_PROF
    if (argc > 4) { g_tr = fopen(argv[3], "w"); g_trend = strtoull(argv[4], 0, 0); if (argc > 5) g_trbeg = strtoull(argv[5], 0, 0); }
#endif
    FILE *sc = fopen(argv[2], "r"); char l[256];
    while (fgets(l, sizeof l, sc)) {
        char cmd[32], a[128] = ""; int k = sscanf(l, "%31s %127s", cmd, a); if (k < 1) continue;
        if (!strcmp(cmd, "wait")) {
            arm7_t *c = tg18_cpu(T); uint64_t c0 = c->cycles, i0 = c->idle_cycles, s0 = c->idle_skipped;
            tg18_run_us(T, (uint32_t)(atof(a) * 1000));
#ifdef T18_PSTAT
            ps_snap();
#endif
            if (getenv("T18_ACT")) fprintf(stderr, "mult %u wait %6.0f ms: active %5.1f%%  skipped %5.1f%%  halted %5.1f%%\n", c->cyc_mult, atof(a),
                100.0 * (double)(c->cycles - c0 - (c->idle_cycles - i0)) / (double)(c->cycles - c0),
                100.0 * (double)(c->idle_skipped - s0) / (double)(c->cycles - c0),
                100.0 * (double)((c->idle_cycles - i0) - (c->idle_skipped - s0)) / (double)(c->cycles - c0));
        }
#ifdef T18_GROW_TEST
        /* grow <adult 0..9 or -1> <param hex>: the grow-up of main.c (T18_GROW), step by step on the PC */
        else if (!strcmp(cmd, "grow")) {
            static const uint8_t AG[10] = {0,0,0,0,0,1,1,1,1,1}, AM[10] = {0,0,0,2,7,0,0,0,2,7}, AV[10] = {0,5,10,0,0,0,0,5,0,0};
            static const uint16_t AO[10] = {0,0x140,0x141,0,0,0,0x144,0x145,0,0}, AT[4] = {0, 0x3C, 0x5DC, 0xB7C};
            int who = atoi(a); unsigned pa = 0; sscanf(l, "%*s %*s %x", &pa); uint32_t P = pa - 0xF8000000u;
            #define RD(o, n) ({ uint32_t v_ = 0; for (int i_ = 0; i_ < (n); i_++) v_ |= (uint32_t)*t18_sram_rp(P + (o) + i_) << (8 * i_); v_; })
            #define WR(o, v, n) do { for (int i_ = 0; i_ < (n); i_++) t18_sram_wr(P + (o) + i_, ((v) >> (8 * i_)) & 0xFF, 1); } while (0)
            int g2 = getenv("GROW2") != NULL, wakes = 0; unsigned gst = 0xFF; int hold = -1;   /* test3: T18_GROW2 of main.c */
            if (g2) tg18_set_keep_awake(T, 1); else tg18_set_clock_mult(T, 60);
            int miss = -1, last = -1;
            for (int t = 0; t < 4000; t++) {
                if (!tg18_power(T)) { wakes++; tg18_set_buttons(T, 4); tg18_run_us(T, 120000); tg18_set_buttons(T, 0); }
                tg18_run_us(T, 100000);
                if (getenv("GSHOT") && (t % 20) == 0 && t < 600) { char fn[64]; snprintf(fn, sizeof fn, "%s%03d.raw", getenv("GSHOT"), t / 10); FILE *o = fopen(fn, "wb"); fwrite(tg18_picture(T), 2, 128 * 128, o); fclose(o); }
                if (!RD(0, 3) && !RD(0x52, 1)) continue;
                if (RD(0x16, 1) & 0x10) { fprintf(stderr, "grow: gone\n"); break; }
                uint32_t st = RD(0x52, 1);
                if (st != (uint32_t)last) { fprintf(stderr, "grow: t=%.1f s stage %u age %u gen %u\n", t / 10.0, st, RD(0x1C, 4), RD(0x3A, 2)); last = (int)st; }
                if (st == 0) continue;
                if (miss < 0) { miss = (int)RD(0x14, 1); int tm[6]; tg18_get_time(T, tm); if (tm[3] < 8 || tm[3] >= 19) tg18_set_time(T, tm[0], tm[1], tm[2], 12, 0, 0); }
                if (st >= 4) { WR(0x14, (uint32_t)miss, 1); if (RD(0x36, 1) < 3) WR(0x36, 3, 1); WR(0x11, 100, 1); fprintf(stderr, "grow: adult, character %u\n", RD(0, 2)); break; }
                if (RD(0x05, 1) < 6) WR(0x05, 6, 1);
                if (RD(0x11, 1) < 30) WR(0x11, 30, 1);
                { uint32_t fl = RD(0x16, 1); if (fl & 1) { WR(0x16, fl & 0xFE, 1); WR(0x18, 0, 2); WR(0x0C, RD(0x0C, 1) & 0xF7, 1); } }
                if (st == 3 && who >= 0 && RD(0x3A, 2) < 2) { WR(0x04, AG[who], 1); WR(0x14, AM[who], 1); for (int i = 0; i < 8; i++) WR(0x13E + i, 0, 1); if (AO[who]) WR(AO[who], AV[who], 1); }
                if (g2) { if (st != gst) { if (gst != 0xFF) hold = t; gst = st; }
                          if (hold >= 0) { if (t - hold < 250) { tg18_set_clock_mult(T, getenv("HOLDMULT") ? atoi(getenv("HOLDMULT")) : 1); continue; } hold = -1; } }
                uint32_t age = RD(0x1C, 4);
                if (st <= 3 && (age < AT[st] - 1u || age > AT[st] + 5u)) { WR(0x1C, AT[st] - 1u, 4); age = AT[st] - 1u; }
                if (g2) tg18_set_clock_mult(T, (st <= 3 && age < AT[st]) ? 60 : 1);
                if (g2 && (t % 50) == 0) fprintf(stderr, "grow: t=%.1f st %u age %u hunger %u mood %u ill %u mult %d\n", t / 10.0, st, RD(0x1C, 4), RD(0x05, 1), RD(0x11, 1), RD(0x16, 1) & 1, tg18_clock_mult(T));
            }
            if (g2) { tg18_set_keep_awake(T, 0); fprintf(stderr, "grow: wakes %d\n", wakes); }
            tg18_set_clock_mult(T, 1);
        }
#endif
#ifdef T18_PANEL_MODEL
        else if (!strcmp(cmd, "mwait")) m_run_ms(atof(a));
        else if (!strcmp(cmd, "mpress")) {   /* mpress <keys> [ms]: as press, the panel model running */
            int m = 0; for (char *p = a; *p; p++) m |= *p == 'A' ? 1 : *p == 'B' ? 2 : *p == 'C' ? 4 : 0;
            double ms = 150; sscanf(l, "%*s %*s %lf", &ms); tg18_set_buttons(T, m); m_run_ms(ms); tg18_set_buttons(T, 0); }
        else if (!strcmp(cmd, "pshot")) m_shot(a);
        else if (!strcmp(cmd, "fdur")) {   /* fdur <ms>: how long each distinct picture of the buffer stays (4 ms steps) */
            double ms = atof(a); uint32_t hl = 0, t0 = 0, k2 = 0;
            for (uint32_t t = 0; t < (uint32_t)ms; t += 4) { tg18_run_us(T, 4000); uint32_t h = m_len ? m_hash(1) : 0;
                if (h != hl) { if (t) fprintf(stderr, "%u ", t - t0); hl = h; t0 = t; if (getenv("FDUMP")) { char fn[64]; snprintf(fn, sizeof fn, "f%04u.raw", k2); FILE *o = fopen(fn, "wb"); uint32_t rows = m_len / 256u; for (uint32_t y = 0; y < 128; y++) fwrite(y < rows ? m_row(m_src + y * 256u) : m_panel[0], 2, 128, o); fclose(o); } k2++; } }
            fprintf(stderr, "\n"); }
        else if (!strcmp(cmd, "pseq")) {   /* pseq <prefix> <ms> <step ms>: panel shots while running */
            double ms = 1000, st = 20; sscanf(l, "%*s %*s %lf %lf", &ms, &st); int k2 = 0;
            for (double t = 0; t < ms; t += st) { m_run_ms(st); char fn[200]; snprintf(fn, sizeof fn, "%s%03d.raw", a, k2++); m_shot(fn); }
            if (getenv("STALE") && m_on && m_len) { int n = 0; uint32_t rows = m_len / 256u; if (rows > 128) rows = 128; for (uint32_t y = 0; y < rows; y++) if (memcmp(m_panel[y], m_row(m_src + y * 256u), 256)) n++; fprintf(stderr, "stale rows at the end of %s: %d\n", a, n); } }
#endif
        else if (!strcmp(cmd, "press")) {
            int m = 0; for (char *p = a; *p; p++) m |= *p == 'A' ? 1 : *p == 'B' ? 2 : *p == 'C' ? 4 : 0;
            double ms = 150; sscanf(l, "%*s %*s %lf", &ms);
            tg18_set_buttons(T, m); tg18_run_us(T, (uint32_t)(ms * 1000)); tg18_set_buttons(T, 0);
        } else if (!strcmp(cmd, "shot")) {
            char fn[160]; if (k > 1) snprintf(fn, sizeof fn, "%s", a); else snprintf(fn, sizeof fn, "shot%02d.raw", nshot++);
            FILE *o = fopen(fn, "wb"); fwrite(tg18_picture(T), 2, 128 * 128, o); fclose(o);
#ifdef T18_PROF
        } else if (!strcmp(cmd, "armprof")) {
            if (!g_armh) g_armh = calloc(1 << 22, 4);
            g_profon = 1; tg18_run_us(T, (uint32_t)(atof(a) * 1000)); g_profon = 0;
            FILE *o = fopen("armprof.bin", "wb"); fwrite(g_armh, 4, 1 << 22, o); fclose(o);
#endif
#if defined(T18_ACNT) && !defined(T18_PSTAT)
        } else if (!strcmp(cmd, "acnt")) {
            acn_on = 1; tg18_run_us(T, (uint32_t)(atof(a) * 1000)); acn_on = 0;
            uint64_t tot = 0, reg[16] = {0}; for (int i = 0; i < 65536; i++) { tot += av[i]; reg[ak[i] >> 28] += av[i]; }
            printf("total %llu\n", (unsigned long long)tot);
            for (int r = 0; r < 16; r++) if (reg[r]) printf("region %x: %llu\n", r, (unsigned long long)reg[r]);
            { /* per 4 KB flash page */
              static uint32_t pg[2048]; memset(pg, 0, sizeof pg);
              for (int i = 0; i < 65536; i++) if (av[i] && (ak[i] >> 28) == 2) pg[(ak[i] & 0x7FFFFF) >> 12] += av[i];
              int np = 0; for (int i = 0; i < 2048; i++) if (pg[i]) np++;
              printf("pages read %d:", np);
              for (int i = 0; i < 2048; i++) if (pg[i]) printf(" %03x:%u", i, pg[i]);
              printf("\n");
            }
            for (int n = 0; n < 25; n++) { int b = 0; for (int i = 1; i < 65536; i++) if (av[i] > av[b]) b = i; if (!av[b]) break; printf("%08x %c %u\n", ak[b] & ~1u, ak[b] & 1 ? 'W' : 'R', av[b]); av[b] = 0; }
#endif
        } else if (!strcmp(cmd, "savest")) {
            /* state, then the guest's SRAM (128 KB): for the M3 test bed (loadst) */
            uint32_t n = tg18_state_size(T); uint8_t *b = malloc(n); tg18_save_state(T, b, n);
            FILE *o = fopen(a, "wb"); fwrite(b, 1, n, o); for (uint32_t q = 0; q < T18_SRAM_SIZE; q++) fputc(*t18_sram_rp(q), o); fclose(o); free(b);
        } else if (!strcmp(cmd, "loadst")) {
            FILE *i = fopen(a, "rb"); uint32_t n = tg18_state_size(T); uint8_t *b = malloc(n + T18_SRAM_SIZE);
            size_t got = fread(b, 1, n + T18_SRAM_SIZE, i); fclose(i);
            if (got != n + T18_SRAM_SIZE || tg18_load_state(T, b, n) < 0) { fprintf(stderr, "loadst failed\n"); return 1; }
            for (uint32_t q = 0; q < T18_SRAM_SIZE; q += 4) { uint32_t w; memcpy(&w, b + n + q, 4); t18_sram_wr(q, w, 4); }
            free(b);
        } else if (!strcmp(cmd, "flash")) {
            FILE *o = fopen(a, "wb"); fwrite(g_flash, 1, 0x800000, o); fclose(o);
        } else if (!strcmp(cmd, "peek")) {   /* peek <guest SRAM address hex> <n>: bytes of the guest SRAM (test2) */
            unsigned ad = 0, n = 16; sscanf(l, "%*s %x %u", &ad, &n);
            printf("peek %08x:", ad); for (unsigned q = 0; q < n; q++) printf(" %02x", *t18_sram_rp(ad - 0xF8000000u + q)); printf("\n");
#ifdef T18_LINK
        } else if (!strcmp(cmd, "linkrun")) {   /* linkrun <ms>: lockstep with the other ref */
            int ms = atoi(a); for (int i = 0; i < ms; i++) { tg18_run_us(T, 1000); lk_pairstep(); }
        } else if (!strcmp(cmd, "blereplay")) {   /* blereplay <capture> <session> */
            int si = 0; char fn2[200]; sscanf(l, "%*s %199s %d", fn2, &si); lk_replay(fn2, si);
#endif
        } else if (!strcmp(cmd, "poke")) {   /* poke <guest SRAM address hex> <byte hex> (test6) */
            unsigned ad = 0, val = 0; sscanf(l, "%*s %x %x", &ad, &val); t18_sram_wr(ad - 0xF8000000u, val & 0xFF, 1);
        } else if (!strcmp(cmd, "power")) {
            printf("power %d backlight %d\n", tg18_power(T), tg18_backlight(T));
        } else if (!strcmp(cmd, "sram")) {
            FILE *o = fopen(a, "wb"); for (uint32_t q = 0; q < T18_SRAM_SIZE; q++) fputc(*t18_sram_rp(q), o); fclose(o);
        } else if (!strcmp(cmd, "hash")) {
            arm7_t *c = tg18_cpu(T);
            printf("cyc %llu pc %08x sram %08x\n", (unsigned long long)c->cycles, c->r[15], sram_hash());
#ifdef T18_IDLE_DBG
            fprintf(stderr, "why ok %u noret %u bad %u cpsr %u regs %u attn %u target %u sram %u\n", g_why[0], g_why[1], g_why[2], g_why[3], g_why[4], g_why[5], g_why[6], g_why[7]);
#endif
            fprintf(stderr, "idle skipped %.1f%%\n", 100.0 * (double)c->idle_skipped / (double)c->cycles);
        }
    }
#ifdef T18_PSTAT
    for (int m = 0; m < 32; m++) if (sp_max[m]) fprintf(stderr, "mode %x sp %08x..%08x\n", m, sp_min[m], sp_max[m]);
    for (int i = 0; i < 512; i++) if (sp_hist[i]) fprintf(stderr, "sp page %05x %u\n", i * 256, sp_hist[i]);
    { FILE *o = fopen("pstat.txt", "w"); for (int i = 0; i < 512; i++) fprintf(o, "%05x %d %llu %llu\n", i * 256, ps_nz[i], (unsigned long long)ps_r[i], (unsigned long long)ps_w[i]); fclose(o); }
#endif
#ifdef T18_WMAP
    { int hi = 0; for (int i = 0; i < 512; i++) if (t18_wmap[i]) hi = i;
      fprintf(stderr, "highest written page %05x\n", hi << 8);
      for (int i = 0; i < 512; i++) fputc(t18_wmap[i] ? '#' : '.', stderr); fputc('\n', stderr); }
#endif
    return 0;
}
