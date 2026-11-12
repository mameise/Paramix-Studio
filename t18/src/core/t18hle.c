/* T18_HLE_FILL (1.7.4): the firmware's picture fill natively. Included by t18cpu.c.
 * The firmware draws its picture with a Thumb routine in its SRAM (fairy v057: 0xF80061D8): for every pixel of a
 * rectangle it calls a per-pixel routine that walks all layers from the top (20-byte records), samples the first
 * one that covers the pixel and is not transparent, looks the colour up in three tables, blends the upper part
 * of the screen in some scenes and stores RGB565. That was more than half of all guest instructions (more with
 * twins: more layers). Here the same in C, with the layers of each row picked out once.
 * Two forms of the per-pixel routine are known: kind 1 (fairy, magic, sanrio: the sampling in its own routine)
 * and kind 2 (wondergarden, fantasy, pastel, sweets: inlined, the layer list 4 bytes earlier); both do the same.
 * Same result as the firmware's code (checked against it in host/ref.c, T18_HLE_CHECK). Guest cycles are charged
 * as the firmware's code would have used them (counters, fitted against the interpreter). Runs in slices up to
 * the run's target: the guest pc stays at the routine until it is done; an interrupt in between returns to it
 * and the fill goes on where it stopped.
 * Author: mameise */
#ifndef T18_HLE_MAXL
#define T18_HLE_MAXL 64
#endif
uint8_t t18_flash_rd(uint32_t a);
uint32_t t18_hle_pc = 1u;            /* the fill routine (Thumb, bit 0 clear); 1 = not found */
#if defined(T18_HLE_CHECK) || defined(T18_HLE_COUNT)
uint32_t t18_hle_n;                  /* fills done natively */
#endif
/* what the search found and where a fill stopped */
static struct {
    uint32_t L, fbv, tr, lo;         /* layer list, picture pointer, red table (green +0x200, blue +0x400), blend range */
    uint8_t toff, kind, cnt, rec;    /* table offset, form, layer count and first record in the list */
    uint32_t sp, lr, a0, a1, a2, a3; /* the call in progress */
    uint16_t idx; uint8_t y, x, on;
} hv;
static int32_t hf_hit;
/* share of the firmware's cycles a native fill is charged with, in 1/16 (16 = as the firmware's code) */
#ifndef T18_HLE_Q
#define T18_HLE_Q 16u
#endif
uint32_t t18_hle_q = T18_HLE_Q;
volatile uint32_t t18_hle_done;   /* fills finished (T18_PRES_FILL in main.c: a finished picture goes out) */
int t18_hle_busy(void) { return hv.on; }
/* guest byte reads (images in the flash, tables in the SRAM): one copy of rd8, not one per use */
static __attribute__((noinline)) uint32_t hr8(arm7_t *c, uint32_t a) { return rd8(c, a); }
/* image bytes: straight from the flash block when it is mapped (as mp_r), else as the guest reads them */
static inline uint32_t im8(arm7_t *c, uint32_t a)
{
    uint32_t o = a - 0x20000000u;
    if (o < 0x800000u) { const uint8_t *b = t18_fblk[o >> 16]; if (b) return b[o & 0xFFFFu]; }
    return hr8(c, a);
}

static inline uint32_t g16(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8; }
static inline uint32_t g32(const uint8_t *p) { return g16(p) | g16(p + 2) << 16; }
static inline const uint8_t *sp_(uint32_t a) { return t18_sram + (a - 0xF8000000u); }
static inline uint32_t lit(uint32_t ins) { uint32_t op = g16(sp_(ins)); return g32(sp_(((ins + 4u) & ~3u) + (op & 0xFFu) * 4u)); }
static inline uint32_t bl_tgt(uint32_t a) { const uint8_t *p = sp_(a); uint32_t h = g16(p), l = g16(p + 2); int32_t off = (int32_t)((h & 0x7FFu) << 21) >> 9; return a + 4u + (uint32_t)off + ((l & 0x7FFu) << 1); }

#ifdef PARADISE
#define HLE_COLD __attribute__((section(".xiptext"), noinline))
#else
#define HLE_COLD
#endif
/* code fingerprint (halfwords; BL offsets, literal places and small ldrb offsets left out): FNV-1a */
HLE_COLD static uint32_t fp_code(uint32_t a, uint32_t len)
{
    uint32_t h = 2166136261u; const uint8_t *p = sp_(a);
    for (uint32_t i = 0; i < len; i += 2) {
        uint32_t op = g16(p + i);
        if ((op & 0xF800u) == 0xF000u) { i += 2; op = 0xF000u; }
        else if ((op & 0xF800u) == 0x4800u) op &= 0xF800u;
        else if ((op & 0xF800u) == 0x7800u && (op & 0x7C0u) <= 0x200u) op &= 0xF83Fu;
        h = (h ^ op) * 16777619u;
    }
    return h;
}
/* fingerprints of the firmwares seen (T18_HLE_FP prints them) */
#define HF_FP_FILL 0x9B253DFAu      /* the fill (both kinds) */
#define HF_FP_PIX1 0xC86488CFu      /* kind 1: per-pixel routine and its sampling routine */
#define HF_FP_SMP1 0xED689851u
#define HF_FP_PIX2 0xAB5ABE09u              /* kind 2: per-pixel routine with the sampling inside */

/* counters of what the firmware's code would have walked through (fitted in host/ref.c) */
enum { K_PIX, K_ROW, K_CALL, K_ST1, K_ST2, K_ST3, K_BLEND, K_NOHIT, K_XN, K_XB, K_FBP, K_S0, K_SH,
       K_SW, K_YF, K_XF, K_B8, K_B4O, K_B4E, K_B10, K_B10Y, K_B10X, K_B10R, K_DIV, K_Z, K_H, K_DN, K_DB, K_N };
#ifdef T18_HLE_CHECK
uint32_t hk[K_N];
#define HK(k) (hk[k]++)
#else
#define HK(k) do { } while (0)
#endif
/* guest cycles per counter and kind: fitted against the interpreter (host/ref.c T18_HLE_CHECK, all 9 firmwares,
 * about 1000 fills: 0.05 % off on average); NOHIT and H did not occur: estimates */
static const uint16_t HCT[2][K_N] = {
    /* PIX ROW CALL ST1 ST2 ST3 BLEND NOHIT XN XB FBP  S0   SH  SW YF XF B8 B4O B4E B10 B10Y B10X B10R DIV Z  H  DN DB */
    {  0,  0,  0,   0,  0,  7,  122,  224, 75, 89, 322, 166, 390, 0, 21, 5, 0, 37, 0,  0,  2,   0,   0,   64, 16,25,43,56 },
    {  0,  0,  0,   0,  0,  7,  122,  224, 70, 86, 326, 154, 385, 0, 21, 5, 0, 29, 0,  0,  2,   0,   0,   61, 18,25,36,49 },
};
#define HC(k) (hc[k])
#define HKC(k) do { HK(k); *cy += HC(k); } while (0)

/* sampling of one layer at (x, y) inside it, as the firmware (layers turned by 90 degrees, rows with many layers) */
HLE_COLD static uint32_t smp(arm7_t *c, const uint8_t *e, uint32_t x, uint32_t y, uint32_t *cy, const uint16_t *hc)
{
    uint32_t img = g32(e), f = g16(e + 14);
    if (f & 0x40u) { uint32_t t = x; x = y; y = t; HKC(K_SW); }
    if (f & 0x20u) { y = (e[9] - 1u - y) & 0xFFu; HKC(K_YF); }
    if (f & 0x10u) { x = (e[8] - 1u - x) & 0xFFu; HKC(K_XF); }
    uint32_t off = g16(e + 16);
    if (f & 0x8000u) { HKC(K_B8); return im8(c, img + x + y * im8(c, img) + off + 6u); }
    if (f & 0x400u) { HK(K_B10); if (!y) { HKC(K_B10Y); if (x <= 4u) { HKC(K_B10X); if (f & 0x800u) { HK(K_B10R); return 0; } } } }
    uint32_t b = im8(c, img + y * (im8(c, img) >> 1) + off + (x >> 1) + 6u);
    if (x & 1u) { HKC(K_B4O); return b >> 4; }
    HK(K_B4E); return b & 15u;
}

/* per row: the layers that cover it (top first), each with what its sampling in this row needs, and the cycles of
 * the other layers the walk passes above each. More than HLE_MAXR such layers: the row goes the slow way. */
#ifndef HLE_MAXR
#define HLE_MAXR 24
#endif
enum { M8 = 1, MXF = 2, MB10 = 4, MB11 = 8, MFB = 16, MDIV = 32, MSLOW = 64 };
typedef struct { const uint8_t *p; uint32_t ga; int16_t lx; uint16_t pre; uint8_t w, m, i, sc; } hent_t;
typedef struct { uint8_t n, slow; uint16_t tail; hent_t e[HLE_MAXR]; } hrow_t;

static inline uint32_t rej_kind(const uint8_t *e, uint32_t y)
{
    if (!g32(e)) return K_Z;
#ifdef T18_HLE_K2VIS
    /* 1.7.5 (user: Wondergarden, worn accessories not shown): only the kind-1 pixel routine (fairy, magic, sanrio)
     * skips layers with bit 14; the kind-2 routine (wondergarden, fantasy, pastel, sweets) draws them */
    if (hv.kind == 1u && (g16(e + 14) & 0x4000u)) return K_H;
#else
    if (g16(e + 14) & 0x4000u) return K_H;
#endif
    int32_t dy = (int16_t)(uint16_t)(y - g16(e + 6));
    if (dy < 0) return K_DN;
    if (dy >= (int32_t)e[9]) return K_DB;
    return 0;
}

/* a host pointer to n guest bytes at a (one flash block or plain SRAM), else 0 */
static inline const uint8_t *gptr(uint32_t a, uint32_t n)
{
    uint32_t o = a - 0x20000000u;
    if (o < 0x800000u) { const uint8_t *b = t18_fblk[o >> 16]; return b && (o & 0xFFFFu) + n <= 0x10000u ? b + (o & 0xFFFFu) : 0; }
    o = a - 0xF8000000u;
    return o < T18_SRAM_SIZE && T18_SRAM_SIZE - o >= n && !t18_hole_n(o, n) ? t18_sram + o : 0;
}

static void row_pick(arm7_t *c, hrow_t *rw, const uint8_t *R0, uint32_t n, uint32_t y, const uint16_t *hc)
{
    uint32_t pre = 0;
    rw->n = 0; rw->slow = 0;
    for (int32_t i = (int32_t)n - 1; i >= 0; i--) {
        const uint8_t *e = R0 + 20u * (uint32_t)i;
        uint32_t k = rej_kind(e, y);
        if (k) { pre += hc[k]; continue; }
        if (rw->n >= HLE_MAXR) { rw->slow = 1; return; }
        hent_t *E = &rw->e[rw->n++];
        uint32_t f = g16(e + 14), w = e[8], dy = (y - g16(e + 6)) & 0xFFu, m = 0, sc = 0;
        E->pre = (uint16_t)pre; E->lx = (int16_t)g16(e + 4); E->w = (uint8_t)w; E->i = (uint8_t)i; E->p = 0; E->ga = 0;
        if (f & 0x1000u) m = MFB;
        else if (f & 0x40u) m = MSLOW;
        else {
            uint32_t img = g32(e), yy = dy, st, len;
            if (f & 0x20u) { yy = (e[9] - 1u - yy) & 0xFFu; sc += hc[K_YF]; }
            if (f & 0x10u) { m |= MXF; sc += hc[K_XF]; }
            if (f & 0x8000u) { m |= M8; sc += hc[K_B8]; st = im8(c, img); len = w; }
            else { st = im8(c, img) >> 1; len = (w + 1u) >> 1;
                   if ((f & 0x400u) && !yy) { m |= MB10; sc += hc[K_B10Y]; if (f & 0x800u) m |= MB11; } }
            E->ga = img + yy * st + g16(e + 16) + 6u; E->p = gptr(E->ga, len);
        }
        if (f & 0x2000u) m |= MDIV;
        E->m = (uint8_t)m; E->sc = (uint8_t)sc;
    }
    rw->tail = (uint16_t)pre;
}

/* one pixel as the firmware's per-pixel routine: the first layer from the top that covers it and is not
 * transparent; its value plus the layer's palette offset, 0 when none */
static inline uint32_t pixel(arm7_t *c, const hrow_t *rw, const uint8_t *R0, uint32_t x, uint32_t y, uint32_t fb, uint32_t *cy, const uint16_t *hc)
{
    for (uint32_t j = 0; j < rw->n; j++) {
        const hent_t *E = &rw->e[j];
        int32_t dx = (int16_t)(uint16_t)(x - (uint32_t)(uint16_t)E->lx);
        if (dx < 0) { HKC(K_XN); continue; }
        if (dx >= (int32_t)E->w) { HKC(K_XB); continue; }
        uint32_t m = E->m, v;
        if (m & MFB) { HKC(K_FBP); *cy += E->pre; hf_hit = E->i; return rd16(c, fb + ((((y << 7) + x) & 0xFFFFu) << 1)); }
        const uint8_t *e = R0 + 20u * E->i;
        if (m & MSLOW) v = smp(c, e, (uint32_t)dx & 0xFFu, (y - g16(e + 6)) & 0xFFu, cy, hc);
        else {
            uint32_t xx = (m & MXF) ? E->w - 1u - (uint32_t)dx : (uint32_t)dx;
#ifdef T18_HLE_CHECK
            { uint32_t f = g16(e + 14); if (f & 0x20u) HK(K_YF); if (f & 0x10u) HK(K_XF); if (f & 0x400u && !(m & M8)) HK(K_B10); if (m & MB10) HK(K_B10Y); }
#endif
            *cy += E->sc;
            if (m & M8) { HK(K_B8); v = E->p ? E->p[xx] : im8(c, E->ga + xx); }
            else {
                if ((m & MB10) && xx <= 4u) HKC(K_B10X);
                if ((m & (MB10 | MB11)) == (MB10 | MB11) && xx <= 4u) { HK(K_B10R); v = 0; }
                else {
                    uint32_t b = E->p ? E->p[xx >> 1] : im8(c, E->ga + (xx >> 1));
                    if (xx & 1u) { HKC(K_B4O); v = b >> 4; } else { HK(K_B4E); v = b & 15u; }
                }
            }
        }
        if (m & MDIV) {   /* the firmware's signed divide helper: only positive values here, 0 for a divisor 0 */
            uint32_t d = hr8(c, g32(e) + 2u); HKC(K_DIV); v = d ? (~v & 0xFFFFu) % d : 0u; }
        if (v) { HKC(K_SH); *cy += E->pre; hf_hit = E->i; return (v + g16(e + 12)) & 0xFFFFu; }
        HKC(K_S0);
    }
    HKC(K_NOHIT); *cy += rw->tail; hf_hit = -1;
    return 0;
}

/* a row with many layers: the walk as the firmware does it, layer by layer */
HLE_COLD static uint32_t pixel_all(arm7_t *c, const uint8_t *R0, uint32_t n, uint32_t x, uint32_t y, uint32_t fb, uint32_t *cy, const uint16_t *hc)
{
    for (int32_t i = (int32_t)n - 1; i >= 0; i--) {
        const uint8_t *e = R0 + 20u * (uint32_t)i;
        uint32_t k = rej_kind(e, y);
        if (k) { *cy += hc[k]; continue; }
        int32_t dx = (int16_t)(uint16_t)(x - g16(e + 4));
        if (dx < 0) { HKC(K_XN); continue; }
        if (dx >= (int32_t)e[8]) { HKC(K_XB); continue; }
        uint32_t f = g16(e + 14);
        if (f & 0x1000u) { HKC(K_FBP); hf_hit = i; return rd16(c, fb + ((((y << 7) + x) & 0xFFFFu) << 1)); }
        uint32_t v = smp(c, e, (uint32_t)dx & 0xFFu, (y - g16(e + 6)) & 0xFFu, cy, hc);
        if (f & 0x2000u) { uint32_t d = hr8(c, g32(e) + 2u); HKC(K_DIV); v = d ? (~v & 0xFFFFu) % d : 0u; }
        if (v) { HKC(K_SH); hf_hit = i; return (v + g16(e + 12)) & 0xFFFFu; }
        HKC(K_S0);
    }
    HKC(K_NOHIT); hf_hit = -1;
    return 0;
}

#ifdef T18_HLE_CHECK
/* the layers the walk passed above the hit (or all), by kind */
static void rej_count(const uint8_t *R0, uint32_t n, uint32_t y)
{
    for (int32_t i = (int32_t)n - 1; i > hf_hit; i--) { uint32_t k = rej_kind(R0 + 20u * (uint32_t)i, y); if (k) hk[k]++; }
}
/* host/ref.c: the native fill into hf_dry (no guest writes, no cycles), the guest's code then runs and is compared */
uint16_t *hf_dry;
#define HF_SLICE (!hf_dry)
#else
#define HF_SLICE 1
#endif

/* the picture row straight in the SRAM when nothing has to see the writes (as blk_ptr), else 0 (wr16 each) */
static inline uint16_t *fb_row(arm7_t *c, uint32_t a, uint32_t n, uint32_t idx)
{
#ifdef T18_HLE_CHECK
    if (hf_dry) return 0;
#endif
    uint32_t len = n * 2u, o = a - 0xF8000000u;
    if (c->watch || idx + n > 0x10000u || o >= T18_SRAM_SIZE || T18_SRAM_SIZE - o < len || t18_hole_n(o, len)) return 0;
    if (c->note_hi > c->note_lo && a + len > c->note_lo && a < c->note_hi) return 0;
#ifdef T18_JIT
    if (t18_jit_code_range(o, len)) return 0;
#endif
    return (uint16_t *)(void *)(t18_sram + o);
}

#ifdef T18_HLE_OS
#define HLE_OPT __attribute__((optimize("Os")))   /* smaller: more room for translated code */
#else
#define HLE_OPT
#endif
/* 1: done (or a slice of it), 0: not taken (the guest code runs) */
HLE_OPT static int t18_hle_fill(arm7_t *c, uint64_t target)
{
    const uint8_t *p = sp_(t18_hle_pc);
    if (g32(p) != 0x465FB5F0u || g32(p + 12) != 0x9203B08Bu) { t18_hle_pc = 1u; return 0; }   /* code gone: search again */
    const uint8_t *L = sp_(hv.L), *R0 = L + hv.rec;
    uint32_t n = L[hv.cnt];
    if (c->watch || n > T18_HLE_MAXL) return 0;
    const uint16_t *hc = HCT[hv.kind - 1u];
    uint32_t mult = c->cyc_mult, cyc = 0;
    if (!(hv.on && hv.sp == c->r[13] && hv.lr == c->r[14] && hv.a0 == c->r[0] && hv.a1 == c->r[1] && hv.a2 == c->r[2] && hv.a3 == c->r[3])) {
        if (c->r[3] >= 255u || (c->r[1] & 0xFFu) == 255u) return 0;   /* would run forever in the firmware too */
        hv.sp = c->r[13]; hv.lr = c->r[14]; hv.a0 = c->r[0]; hv.a1 = c->r[1]; hv.a2 = c->r[2]; hv.a3 = c->r[3];
        hv.idx = (uint16_t)(c->r[2] + (c->r[0] << 7)); hv.y = (uint8_t)c->r[0]; hv.x = (uint8_t)c->r[2]; hv.on = 1;
        HK(K_CALL); cyc += HC(K_CALL);
    }
    uint32_t fb = rd32a(c, hv.fbv), y1 = hv.a1 & 0xFFu, x0 = hv.a2 & 0xFFu, x1 = hv.a3, adj = (hv.a2 - hv.a3 + 127u) & 0xFFFFu;
    uint32_t lo = hr8(c, hv.lo), hi = hr8(c, hv.lo + 1u);
    const uint8_t *T = sp_(hv.tr + hv.toff);   /* the colour tables: SRAM outside the hole (checked by the search) */
    uint32_t y = hv.y, x = hv.x, idx = hv.idx;
    hrow_t rw;
    while (y <= y1) {
        row_pick(c, &rw, R0, n, y, hc);
        uint16_t *fp = fb_row(c, fb + (idx << 1), x1 + 1u - x, idx);
        while (x <= x1) {
            uint32_t v = rw.slow ? pixel_all(c, R0, n, x, y, fb, &cyc, hc) : pixel(c, &rw, R0, x, y, fb, &cyc, hc);
#ifdef T18_HLE_CHECK
            rej_count(R0, n, y);
#endif
            uint32_t R, G, B;
            if (v < 0x200u) { R = T[v]; G = T[0x200u + v]; B = T[0x400u + v]; }
            else { uint32_t a = hv.tr + hv.toff + v; R = hr8(c, a); G = hr8(c, a + 0x200u); B = hr8(c, a + 0x400u); }
            if (lo > v) { HK(K_ST1); cyc += HC(K_ST1); }
            else if (hi <= v) { HK(K_ST2); cyc += HC(K_ST2); }
            else if (y > 64u) { HK(K_ST3); cyc += HC(K_ST3); }
            else {
                R = ((R * y + 88u * (64u - y)) & 0xFFFFu) >> 6;
                G = ((G * y + 81u * (64u - y)) & 0xFFFFu) >> 6;
                B = ((B * y + 160u * (64u - y)) & 0xFFFFu) >> 6;
                HK(K_BLEND); cyc += HC(K_BLEND);
            }
            HK(K_PIX); cyc += HC(K_PIX);
            uint32_t px = (((R >> 3) & 0x1Fu) << 11) | ((G << 3) & 0x7E0u) | ((B & 0xFFu) >> 3);
#ifdef T18_HLE_CHECK
            if (hf_dry) hf_dry[idx] = (uint16_t)px; else
#endif
            if (fp) *fp++ = (uint16_t)px; else wr16(c, fb + (idx << 1), px);
            idx = (idx + 1u) & 0xFFFFu; x = (x + 1u) & 0xFFu;
            if (HF_SLICE && x <= x1 && c->cycles + (uint64_t)((cyc * t18_hle_q) >> 4) * mult >= target) {
                hv.y = (uint8_t)y; hv.x = (uint8_t)x; hv.idx = (uint16_t)idx;
                c->cycles += (uint64_t)((cyc * t18_hle_q) >> 4) * mult;
                return 1;
            }
        }
        idx = (idx + adj) & 0xFFFFu; y = (y + 1u) & 0xFFu; x = x0; HK(K_ROW); cyc += HC(K_ROW);
    }
    hv.on = 0;
#ifdef T18_HLE_CHECK
    if (hf_dry) { uint32_t sum = 0; for (int k = 0; k < K_N; k++) sum += hk[k] * hc[k];
                  if (sum != cyc) { extern uint32_t t18_hle_cbad; t18_hle_cbad++; }
                  return 1; }
#endif
#if defined(T18_HLE_CHECK) || defined(T18_HLE_COUNT)
    t18_hle_n++;
#endif
    t18_hle_done++;
#ifdef T18_HLE_LOG
    { extern void t18_hle_log(arm7_t *, uint32_t, uint32_t, uint32_t, uint32_t); t18_hle_log(c, hv.a0, hv.a1, hv.a2, hv.a3); }
#endif
    c->cycles += (uint64_t)((cyc * t18_hle_q) >> 4) * mult;
    uint32_t lr = c->r[14];
    c->r[0] = lr; c->r[15] = lr & ~1u;
    if (!(lr & 1u)) c->cpsr &= ~ARM7_T;
    return 1;
}

/* find the fill routine in the guest's SRAM (the firmware copies its code there); 1 when found */
HLE_COLD int t18_hle_find(void)
{
    for (uint32_t o = 0x100u; o + 0x200u <= T18_SRAM_SIZE; o += 2) {
        if (o + 0x200u > T18_HOLE_LO && o < T18_HOLE_HI) { o = T18_HOLE_HI; continue; }
        const uint8_t *p = t18_sram + o;
        /* push {r4-r7,lr}; mov r7,fp; mov r6,sl; mov r5,r9; mov r4,r8; push {r4-r7}; sub sp,#44; str r2,[sp,#12]; mov r8,r3 */
        if (g32(p) != 0x465FB5F0u || g32(p + 4) != 0x464D4656u || g32(p + 8) != 0xB4F04644u || g32(p + 12) != 0x9203B08Bu || g16(p + 16) != 0x4698u) continue;
        uint32_t a = 0xF8000000u + o;
        if ((g16(p + 0xB0) & 0xF800u) != 0xF000u || fp_code(a, 0x1E8u) != HF_FP_FILL) continue;
        uint32_t pix = bl_tgt(a + 0xB0u);
        if (pix - 0xF8000000u >= T18_SRAM_SIZE - 0x280u) continue;
        const uint8_t *q = sp_(pix);
        uint32_t kind = 0, L = 0, fbl = 0, dvl = 0;
        if (g16(q + 12) == 0xB087u && g16(q + 14) == 0x9004u && (g16(q + 16) & 0xFF00u) == 0x4B00u && g16(q + 18) == 0x7A1Cu) {
            uint32_t sm = bl_tgt(pix + 0xE4u);   /* kind 1: sub sp,#28; str r0,[sp,#16]; ldr r3,=list; ldrb r4,[r3,#8] */
#ifdef T18_HLE_FP
            { extern void t18_hle_fp(int, uint32_t, uint32_t, uint32_t); t18_hle_fp(1, fp_code(a, 0x1E8u), fp_code(pix, 0x130u), sm - 0xF8000000u < T18_SRAM_SIZE - 0xE0u ? fp_code(sm, 0xDCu) : 0); }
#endif
            if (sm - 0xF8000000u >= T18_SRAM_SIZE - 0xE0u || fp_code(pix, 0x130u) != HF_FP_PIX1 || fp_code(sm, 0xDCu) != HF_FP_SMP1) continue;
            kind = 1; L = lit(pix + 16u); fbl = lit(pix + 0x3Au); dvl = lit(pix + 0xF8u); hv.cnt = 8; hv.rec = 12;
        } else if (g16(q + 12) == 0xB085u && (g16(q + 14) & 0xFF00u) == 0x4B00u && g16(q + 16) == 0x791Cu) {
#ifdef T18_HLE_FP
            { extern void t18_hle_fp(int, uint32_t, uint32_t, uint32_t); t18_hle_fp(2, fp_code(a, 0x1E8u), fp_code(pix, 0x27Cu), 0); }
#endif
            if (fp_code(pix, 0x27Cu) != HF_FP_PIX2) continue;   /* kind 2: sub sp,#20; ldr r3,=list; ldrb r4,[r3,#4] */
            kind = 2; L = lit(pix + 14u); fbl = lit(pix + 0x23Au); dvl = lit(pix + 0xF0u); hv.cnt = 4; hv.rec = 8;
        } else continue;
        uint32_t tr = lit(a + 0x46u);
        if (lit(a + 0x4Au) != tr + 0x200u || lit(a + 0xCAu) != tr + 0x400u || lit(a + 0xE2u) != lit(a + 0xD6u) + 1u || fbl != lit(a + 0x6Eu)) continue;
        if (L - 0xF8000000u >= T18_SRAM_SIZE - 20u * 256u - 16u) continue;
        { uint32_t to = tr - 0xF8000000u;   /* tables (3 x 0x200 + offset) in the SRAM, not in the hole */
          if (to + 0x620u > T18_SRAM_SIZE || t18_hole_n(to, 0x620u)) continue; }
        /* the divide helper of the "inverted" layers (bit 13): ARM __aeabi_idivmod as in all firmwares seen */
        { uint8_t b[16]; for (uint32_t i = 0; i < 16; i++) b[i] = t18_flash_rd((dvl - 0x20000000u + i) & 0x7FFFFFu);
          if (dvl - 0x20000000u >= 0x800000u || g32(b) != 0xE3510000u || g32(b + 8) != 0xE92D4003u || g32(b + 12) != 0xEBFFFFB3u) continue; }
        hv.kind = (uint8_t)kind; hv.L = L; hv.fbv = fbl; hv.tr = tr; hv.lo = lit(a + 0xD6u);
        hv.toff = (uint8_t)((g16(p + 0xBC) >> 6) & 31u); hv.on = 0;
        t18_hle_pc = a;
        return 1;
    }
    return 0;
}

#ifdef T18_HLE_CHECK
#include <stdio.h>
#include <stdlib.h>
uint32_t t18_exc_n;   /* exceptions taken (t18cpu.c) */
uint32_t t18_hle_cbad;   /* fills whose charged cycles differ from the counters */
static struct { uint32_t on, sp, lr, fb, a0, a1, a2, a3; uint32_t cnt[K_N]; } ck;
uint64_t t18_hle_own;   /* cycles of the routine's own code while checking (t18cpu.c) */
uint32_t t18_hle_bad, t18_hle_ok, t18_hle_dv, t18_hle_px, t18_hle_sm;
static inline int hle_own(uint32_t pc) { return ck.on && (pc - t18_hle_pc < 0x1E8u || pc - t18_hle_px < (hv.kind == 1 ? 0x130u : 0x27Cu) || (hv.kind == 1 && pc - t18_hle_sm < 0xDCu) || pc - t18_hle_dv < 0x144u); }
void t18_hle_cfit(const uint32_t *cnt, int n, uint64_t cycles, int clean);   /* host/ref.c */
static void t18_hle_chk(arm7_t *c)
{
    static uint16_t dry[65536];
    if (ck.on && c->r[15] == (ck.lr & ~1u) && c->r[13] == ck.sp) {
        ck.on = 0;
        uint32_t y0 = ck.a0 & 0xFFu, y1 = ck.a1 & 0xFFu, x0 = ck.a2 & 0xFFu, idx = (ck.a2 + (ck.a0 << 7)) & 0xFFFFu, bad = 0;
        for (uint32_t y = y0; y <= y1; y++) {
            uint32_t i = idx;
            for (uint32_t x = x0; x <= ck.a3; x++) { if (rd16(c, ck.fb + (i << 1)) != dry[i]) { if (!bad && getenv("HLE_DIFF")) fprintf(stderr, "diff x %u y %u guest %04x hle %04x\n", x, y, rd16(c, ck.fb + (i << 1)), dry[i]); bad++; } i = (i + 1u) & 0xFFFFu; }
            idx = (idx + 128u) & 0xFFFFu;
        }
        if (bad && getenv("HLE_DIFF")) {
            uint32_t sv[6]; memcpy(sv, c->r, 16); sv[4] = c->r[13]; sv[5] = c->r[14];
            c->r[0] = ck.a0; c->r[1] = ck.a1; c->r[2] = ck.a2; c->r[3] = ck.a3; c->r[13] = ck.sp; c->r[14] = ck.lr;
            hf_dry = dry; t18_hle_fill(c, ~0ull); hf_dry = 0;
            memcpy(c->r, sv, 16); c->r[13] = sv[4]; c->r[14] = sv[5];
            uint32_t bad2 = 0, i2 = (ck.a2 + (ck.a0 << 7)) & 0xFFFFu;
            for (uint32_t y = y0; y <= y1; y++) { uint32_t i = i2; for (uint32_t x = x0; x <= ck.a3; x++) { if (rd16(c, ck.fb + (i << 1)) != dry[i]) bad2++; i = (i + 1u) & 0xFFFFu; } i2 = (i2 + 128u) & 0xFFFFu; }
            fprintf(stderr, "  computed again at the end: %u differ\n", bad2);
            fprintf(stderr, "  %u pixels differ of %u x %u, args %x %x %x %x, layers %u\n", bad, ck.a3 + 1u - x0, y1 + 1u - y0, ck.a0, ck.a1, ck.a2, ck.a3, sp_(hv.L)[hv.cnt]);
            const uint8_t *R0 = sp_(hv.L) + hv.rec;
            for (uint32_t i = 0; i < sp_(hv.L)[hv.cnt]; i++) { const uint8_t *e = R0 + 20u * i;
                fprintf(stderr, "   L%2u img %08x x %4x y %4x w %3u h %3u pal %4x f %04x off %4x\n", i, g32(e), g16(e+4), g16(e+6), e[8], e[9], g16(e+12), g16(e+14), g16(e+16)); }
        }
        if (bad) t18_hle_bad++; else t18_hle_ok++;
        t18_hle_cfit(ck.cnt, K_N, t18_hle_own, !bad);
        return;
    }
    if (!ck.on && t18_hle_pc != 1u && c->r[15] == t18_hle_pc && (c->cpsr & ARM7_T) && !c->watch) {
        ck.sp = c->r[13]; ck.lr = c->r[14]; ck.a0 = c->r[0]; ck.a1 = c->r[1]; ck.a2 = c->r[2]; ck.a3 = c->r[3];
        ck.fb = rd32a(c, hv.fbv); t18_hle_own = 0;
        t18_hle_px = bl_tgt(t18_hle_pc + 0xB0u); t18_hle_sm = bl_tgt(t18_hle_px + 0xE4u);
        t18_hle_dv = lit(t18_hle_px + (hv.kind == 1 ? 0xF8u : 0xF0u)) - 0x120u;
        memset(hk, 0, sizeof hk); hf_dry = dry; int ok = t18_hle_fill(c, ~0ull); hf_dry = 0;
        if (ok) { memcpy(ck.cnt, hk, sizeof ck.cnt); ck.on = 1; }
    }
}
#endif
