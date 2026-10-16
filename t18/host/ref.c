/* PC run of the T18 core with the Paradise memory map (reference for the JIT and the device).
 * ref <rom> <script> [csv]
 * script: wait <ms> | press <A|B|C|AB..> [ms] | shot [file] | hash | state <file> | time y m d h mi s */
#include "../src/core/t18soc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

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
    if (c->cycles > 16000000000ull && (g_why[why] % 300) == 1) {
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
static uint32_t fnv(const uint8_t *p, size_t n, uint32_t h) { for (size_t i = 0; i < n; i++) h = (h ^ p[i]) * 16777619u; return h; }
/* hash over guest SRAM 0xF8000000..0xF801FFFF, whatever part is direct */
/* hash over the guest's view of its SRAM (with a hole: its pages, not the host bytes) */
static uint32_t sram_hash(void) { uint32_t h = 2166136261u; for (uint32_t o = 0; o < T18_SRAM_SIZE; o += 256) h = fnv(t18_sram_rp(o), 256, h); return h; }
#if T18_HOLE_HI > T18_HOLE_LO
static uint8_t g_pool[64][T18_HOLE_PG]; static int g_pool_n;
static uint8_t *hole_new(void) { if (g_pool_n >= (getenv("T18_NPOOL") ? atoi(getenv("T18_NPOOL")) : 64)) return 0; uint8_t *p = g_pool[g_pool_n++]; memset(p, 0, T18_HOLE_PG); return p; }
static void hole_free(void) { g_pool_n = 0; }
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
        } else if (!strcmp(cmd, "flash")) {
            FILE *o = fopen(a, "wb"); fwrite(g_flash, 1, 0x800000, o); fclose(o);
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
