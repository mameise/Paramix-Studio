/* Fast interpreter for S1C33 (same semantics as tc_cpu.c, which is derived
 * from tamaemu cpu.c). Hot state lives in local variables so the compiler
 * keeps it in registers. Verified by lockstep runs against tc_cpu.c. */
#include "tc.h"
#ifdef ROMSTAT
__attribute__((section(".stats"), used)) volatile uint32_t tc_rom_hits[128];   /* stats build: reads per 64 KB guest block */
#define ROMHIT(o) (tc_rom_hits[((o) >> 16) & 127]++)
#else
#define ROMHIT(o) ((void)0)
#endif

#define SEXT(v, bits) ((uint32_t)(((uint32_t)(v) ^ (1u << ((bits) - 1))) - (1u << ((bits) - 1))))

static inline uint8_t *f_ramp(Emu *e, uint32_t a, uint32_t n)
{
    if (a <= A0RAM_MAX - n) return e->a0ram + a;
    uint32_t o = a - 0x80000u;
    if (o <= IVRAM_MAX - n) return e->ivram + o;
    o = a - 0x84000u;
    if (o <= DSTRAM_MAX - n) return e->dstram + o;
    return 0;
}
#ifdef TC_JIT
/* Write to RAM that has already been translated: discard all translations */
#ifdef JIT_WHOT
/* v0.9.6: a0ram pages whose code keeps getting overwritten (iD L, Connect to Plus Color: code at 0xC00
 * next to its data) are no longer translated. Before, every write threw away all translations, thousands
 * of times per second, and the emulator stood still on the device. */
uint8_t jit_wcnt[128], jit_nojit[128];
uint64_t jit_wwin, jit_nojit_at;
#define JIT_WWIN   2000000u     /* window for counting (about 0.1 s of guest time) */
#define JIT_NOJIT 100000000u    /* after about 5 s the pages get another chance */
static void jit_cw_note(Emu *e, uint32_t a, uint32_t n)
{
    if (e->cycles - jit_wwin > JIT_WWIN) { memset(jit_wcnt, 0, sizeof jit_wcnt); jit_wwin = e->cycles; }
    for (uint32_t p = a >> 8; p <= ((a + n - 1) >> 8) && p < 128u; p++)
        if (e->jit_map[p] && ++jit_wcnt[p] >= 8u) { jit_nojit[p] = 1; jit_wcnt[p] = 0; jit_nojit_at = e->cycles; }
}
static int jit_nojit_pc(Emu *e, uint32_t pc)
{
    if (!jit_nojit[pc >> 8]) return 0;
    if (e->cycles - jit_nojit_at > JIT_NOJIT) { memset(jit_nojit, 0, sizeof jit_nojit); return 0; }
    return 1;
}
#else
#define jit_cw_note(e, a, n) ((void)0)
#endif
__attribute__((noinline)) static void jit_cw_chk(Emu *e, uint32_t a, uint32_t n)
{
    if (e->jit_map[a >> 8] | e->jit_map[(a + n - 1) >> 8]) { jit_cw_note(e, a, n); e->jit_flush = 1; memset(e->jit_map, 0, sizeof e->jit_map); }
}
#endif
#ifdef TC_STATS
void stat_acc(uint32_t a, int w);
#define FST(a, w) stat_acc((a), (w))
#else
#define FST(a, w) ((void)0)
#endif
static inline uint32_t f_rd8(Emu *e, uint32_t a)
{
    FST(a, 0);
    uint8_t *p = f_ramp(e, a, 1); if (p) return *p;
    uint32_t o = a - e->dev.rom_base;
    if (o < e->dev.rom_size && !e->flash_busy && !e->flash_autoselect) { ROMHIT(o); const uint8_t *b = e->rom_blk[o >> 16]; if (b) return b[o & 0xFFFFu]; }
    return mem_read8(e, a);
}
static inline uint32_t f_rd16(Emu *e, uint32_t a)
{
    FST(a, 0);
    uint8_t *p = f_ramp(e, a, 2); if (p) return (uint32_t)(p[0] | (p[1] << 8));
    uint32_t o = a - e->dev.rom_base;
    if (o < e->dev.rom_size && (o & 0xFFFFu) <= 0xFFFEu && !e->flash_busy && !e->flash_autoselect) { ROMHIT(o); const uint8_t *b = e->rom_blk[o >> 16]; if (b) { const uint8_t *q = b + (o & 0xFFFFu); return (uint32_t)(q[0] | (q[1] << 8)); } }
    return mem_read16(e, a);
}
static inline uint32_t f_rd32(Emu *e, uint32_t a)
{
    FST(a, 0);
    uint8_t *p = f_ramp(e, a, 4); if (p) return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
    uint32_t o = a - e->dev.rom_base;
    if (o < e->dev.rom_size && (o & 0xFFFFu) <= 0xFFFCu && !e->flash_busy && !e->flash_autoselect) { ROMHIT(o); const uint8_t *b = e->rom_blk[o >> 16]; if (b) { const uint8_t *q = b + (o & 0xFFFFu); return (uint32_t)q[0] | ((uint32_t)q[1] << 8) | ((uint32_t)q[2] << 16) | ((uint32_t)q[3] << 24); } }
    return mem_read32(e, a);
}
static inline void f_wr8(Emu *e, uint32_t a, uint32_t v) { uint8_t *p = f_ramp(e, a, 1); if (p) *p = (uint8_t)v; else mem_write8(e, a, (uint8_t)v); }
static inline void f_wr16(Emu *e, uint32_t a, uint32_t v) { uint8_t *p = f_ramp(e, a, 2); if (p) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); } else mem_write16(e, a, (uint16_t)v); }
static inline void f_wr32(Emu *e, uint32_t a, uint32_t v) { uint8_t *p = f_ramp(e, a, 4); if (p) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); } else mem_write32(e, a, v); }

/* Flags in one word: bit3 N, bit2 Z, bit1 V, bit0 C */
#define FN (fl >> 3 & 1)
#define FZ (fl >> 2 & 1)
#define FV (fl >> 1 & 1)
#define FC (fl & 1)
#define NZ(r)        do { uint32_t _r = (r); fl = (fl & 3u) | ((_r >> 31) << 3) | ((uint32_t)(_r == 0) << 2); } while (0)
#define SCARRY(a, b) ((((~((a) ^ (b))) & ((a) ^ ((a) + (b)))) >> 31))
#define SBORROW(a, b) (((((a) ^ (b)) & ((a) ^ ((a) - (b)))) >> 31))
#define PUSH(v)      do { sp -= 4; WR32(sp, (v)); } while (0)
#define POP()        (sp += 4, f_rd32(e, sp - 4))
#define PSR_PACK()   ((e->psr_rest & ~0xF1Fu) | FN | (FZ << 1) | (FV << 2) | (FC << 3) | ((uint32_t)e->f_ie << 4) | ((uint32_t)(e->il & 0xF) << 8))
#define PSR_UNPACK(v) do { uint32_t _v = (v); fl = ((_v & 1) << 3) | (((_v >> 1) & 1) << 2) | (((_v >> 2) & 1) << 1) | ((_v >> 3) & 1); \
                          e->f_ie = (_v >> 4) & 1; e->il = (_v >> 8) & 0xF; e->psr_rest = _v; chk = 1; } while (0)

static inline uint32_t op_add(uint32_t a, uint32_t b, uint32_t *fl)
{ uint32_t r = a + b; *fl = ((r >> 31) << 3) | ((uint32_t)(r == 0) << 2) | (SCARRY(a, b) << 1) | (uint32_t)(r < a); return r; }
static inline uint32_t op_sub(uint32_t a, uint32_t b, uint32_t *fl)
{ uint32_t r = a - b; *fl = ((r >> 31) << 3) | ((uint32_t)(r == 0) << 2) | (SBORROW(a, b) << 1) | (uint32_t)(a < b); return r; }
#define ADD(a, b) op_add((a), (b), &fl)
#define SUB(a, b) op_sub((a), (b), &fl)
#define CMP(a, b) ((void)op_sub((a), (b), &fl))
#define LOG(r)    ({ uint32_t _q = (r); fl = (fl & 1u) | ((_q >> 31) << 3) | ((uint32_t)(_q == 0) << 2); _q; })

/* Deferred jump effect after the delay slot */
enum { D_NONE, D_JP, D_CALL, D_RET };

static inline int cond_ok(int op, uint32_t fl)
{
    uint32_t fn = fl >> 3 & 1, fz = fl >> 2 & 1, fv = fl >> 1 & 1, fc = fl & 1;
    switch (op) {
    case 4:  return !fz && fn == fv;
    case 5:  return fn == fv;
    case 6:  return fn != fv;
    case 7:  return fz || fn != fv;
    case 8:  return !fz && !fc;
    case 9:  return !fc;
    case 10: return fc;
    case 11: return fz || fc;
    case 12: return fz;
    case 13: return !fz;
    default: return 1;
    }
}

static int pc_ok(Emu *e, uint32_t a)
{
    const DeviceProfile *d = &e->dev;
    return a - d->rom_base < d->rom_size || a - d->a0ram_base < d->a0ram_size ||
           a - d->ivram_base < d->ivram_size || a - d->dstram_base < d->dstram_size;
}

/* Runs until cycles >= ziel (target), or the CPU sleeps or stops. Peripherals
 * are ticked every TICK cycles. */
#define TICK 256
__attribute__((minsize)) void tc_exec(Emu *e, uint64_t ziel)
{
    uint32_t pc = e->pc, sp = e->sp;
    uint32_t fl = ((uint32_t)e->f_n << 3) | ((uint32_t)e->f_z << 2) | ((uint32_t)e->f_v << 1) | e->f_c;
    int32_t left;
#define CYC() (e->evt - (uint64_t)(int64_t)left)
#define r (e->r)
    const uint8_t *fwp = e->fw_ptr; uint32_t fwlo = e->fw_lo, fwlen = e->fw_len;
    int slot = 0;
#define dkind (e->ext_count)
#define dtarget (e->ext_val[0])
    dkind = D_NONE;

#define SYNC_OUT() do { e->pc = pc; e->sp = sp; e->f_n = FN; e->f_z = FZ; e->f_v = FV; e->f_c = FC; e->cycles = CYC(); } while (0)
#define SYNC_IN()  do { pc = e->pc; sp = e->sp; fl = ((uint32_t)e->f_n << 3) | ((uint32_t)e->f_z << 2) | ((uint32_t)e->f_v << 1) | e->f_c; left = (int32_t)(e->evt - e->cycles); \
                        fwp = e->fw_ptr; fwlo = e->fw_lo; fwlen = e->fw_len; } while (0)

/* Write to RAM that has already been translated: discard all translations */
#ifdef TC_JIT
#define JCW(a, n) do { if ((a) <= A0RAM_MAX - (n)) jit_cw_chk(e, (a), (n)); } while (0)
#else
#define JCW(a, n) ((void)0)
#endif
#define WR8(a, v)  do { uint32_t _a = (a), _v = (v); uint8_t *_p = f_ramp(e, _a, 1); if (_p) { *_p = (uint8_t)_v; JCW(_a, 1); } \
                         else { SYNC_OUT(); mem_write8(e, _a, (uint8_t)_v); SYNC_IN(); chk = 1; } } while (0)
#define WR16(a, v) do { uint32_t _a = (a), _v = (v); uint8_t *_p = f_ramp(e, _a, 2); if (_p) { _p[0] = (uint8_t)_v; _p[1] = (uint8_t)(_v >> 8); JCW(_a, 2); } \
                         else { SYNC_OUT(); mem_write16(e, _a, (uint16_t)_v); SYNC_IN(); chk = 1; } } while (0)
#define WR32(a, v) do { uint32_t _a = (a), _v = (v); uint8_t *_p = f_ramp(e, _a, 4); if (_p) { _p[0] = (uint8_t)_v; _p[1] = (uint8_t)(_v >> 8); _p[2] = (uint8_t)(_v >> 16); _p[3] = (uint8_t)(_v >> 24); JCW(_a, 4); } \
                         else { SYNC_OUT(); mem_write32(e, _a, _v); SYNC_IN(); chk = 1; } } while (0)

#define NEXT_EVT(c) do { uint64_t _n = (e->last_tick + TICK < ziel) ? e->last_tick + TICK : ziel; e->evt = _n; left = (int32_t)(_n - (c)); } while (0)
#define FORCE_EVT() do { uint64_t _c = CYC(); e->evt = _c; left = 0; } while (0)
    NEXT_EVT(e->cycles);
    int chk = 1;   /* check for interrupts (after peripherals, I/O, PSR) */
    if (e->halted || e->stopped) goto out;
    for (;;) {
        int in_slot = slot; slot = 0;
        if (!in_slot && chk) {
            chk = 0;
            if (e->pending_irq && e->f_ie && e->pending_level > e->il) {
                uint32_t vec = e->pending_irq;
                PUSH(pc);
                PUSH(PSR_PACK());
                e->f_ie = 0;
                e->il = e->pending_level;
                pc = f_rd32(e, e->ttbr + vec * 4);
                e->pending_irq = 0;
            }
        }
        /* Fetch */
        if (pc - fwlo >= fwlen) {
            pc &= 0x0FFFFFFFu;
            if (!pc_ok(e, pc)) { e->stopped = true; FORCE_EVT(); e->stop_pc = pc; e->stop_code = pc; break; }
            tc_fetch_window(e, pc); fwp = e->fw_ptr; fwlo = e->fw_lo; fwlen = e->fw_len;
        }
#define FETCH(a) ((a) - fwlo < fwlen ? *(const uint16_t *)(fwp + ((a) - fwlo)) : (uint16_t)f_rd16(e, (a)))
        uint32_t hw = FETCH(pc);
        int ext_n = 0; uint32_t ext0 = 0, ext1 = 0;
        while ((hw >> 13) == 6) {
            if (ext_n == 0) ext0 = hw & 0x1FFF; else if (ext_n == 1) ext1 = hw & 0x1FFF;
            ext_n++;
            pc += 2; left--;
            hw = FETCH(pc);
            if (ext_n > 2) { e->stopped = true; FORCE_EVT(); e->stop_pc = pc; e->stop_code = hw; goto out; }
        }
        if (ext_n > 2) ext_n = 2;
        uint32_t iaddr = pc;
        pc += 2; left--;
        int rd = hw & 0xF, rs = (hw >> 4) & 0xF;

        switch (hw >> 13) {
        case 0: {
            int op1 = (hw >> 9) & 0xF, d = (hw >> 8) & 1;
            if (op1 <= 3) {
                int cnst = (hw >> 4) & 3;
                if (cnst == 0) {
                    int op2 = (hw >> 6) & 3;
                    switch (op1 * 8 + d * 4 + op2) {
                    case 0: break;
                    case 1: e->halted = true; e->halt_hard = !(e->cmu.opt & 0x3E00); e->wake_at = CYC() + 20000; FORCE_EVT(); break;
                    case 2: e->halted = true; e->halt_hard = true; FORCE_EVT(); break;
                    case 8: for (int i = rd; i >= 0; i--) PUSH(r[i]); break;
                    case 9: for (int i = 0; i <= rd; i++) r[i] = POP(); break;
                    case 11: pc = iaddr + r[rd]; break;
                    case 15: dkind = D_JP; dtarget = iaddr + r[rd]; slot = 1; break;
                    case 16: e->stopped = true; FORCE_EVT(); e->stop_pc = iaddr; break;
                    case 17: e->stopped = true; FORCE_EVT(); e->stop_pc = iaddr; break;
                    case 18: { uint32_t vec = 12 + (hw & 3);
                        PUSH(pc); PUSH(PSR_PACK()); e->f_ie = 0;
                        pc = f_rd32(e, e->ttbr + vec * 4); break; }
                    case 19: { PSR_UNPACK(POP()); pc = POP(); break; }
                    case 24: { uint32_t t = r[rd]; PUSH(pc); pc = t; break; }
                    case 25: pc = POP(); break;
                    case 26: pc = r[rd]; break;
                    case 28: dkind = D_CALL; dtarget = r[rd]; slot = 1; break;
                    case 29: dkind = D_RET; slot = 1; break;
                    case 30: dkind = D_JP; dtarget = r[rd]; slot = 1; break;
                    default: e->stopped = true; FORCE_EVT(); e->stop_pc = iaddr; e->stop_code = hw; break;
                    }
                } else if (cnst == 1 && op1 == 0) {
                    switch ((hw >> 6) & 7) {
                    case 0: PUSH(r[rd]); break;
                    case 1: r[rd] = POP(); break;
                    case 2: if ((hw & 0xF) == 2) PUSH(e->alr);
                            else if ((hw & 0xF) == 3) { PUSH(e->ahr); PUSH(e->alr); }
                            else { e->stopped = true; FORCE_EVT(); e->stop_pc = iaddr; } break;
                    case 3: if ((hw & 0xF) == 2) e->alr = POP();
                            else if ((hw & 0xF) == 3) { e->alr = POP(); e->ahr = POP(); }
                            else { e->stopped = true; FORCE_EVT(); e->stop_pc = iaddr; } break;
                    case 7: fl = 0; break;
                    default: e->stopped = true; FORCE_EVT(); e->stop_pc = iaddr; e->stop_code = hw; break;
                    }
                } else { e->stopped = true; FORCE_EVT(); e->stop_pc = iaddr; e->stop_code = hw; }
            } else {
                uint32_t u8 = hw & 0xFF, target;
                if (ext_n == 0)      target = iaddr + SEXT(u8, 8) * 2;
                else if (ext_n == 1) target = iaddr + ((SEXT(ext0, 13) << 9) | (u8 << 1));
                else                 target = iaddr + (((ext0 & 0x1ff8) << 19) | (ext1 << 9) | (u8 << 1));
                if (in_slot) break;   /* branch in a delay slot: ignored, like the original */
                if (op1 == 14) {
                    if (d) { dkind = D_CALL; dtarget = target; slot = 1; }
                    else { PUSH(pc); pc = target; }
                } else if (op1 == 15) {
                    if (d) { dkind = D_JP; dtarget = target; slot = 1; } else pc = target;
                } else {
                    int taken = cond_ok(op1, fl);
                    if (d) { dkind = taken ? D_JP : D_NONE; dtarget = target; slot = 1; }
                    else if (taken) pc = target;
                }
            }
            break;
        }
        case 1: {
            int op1 = (hw >> 10) & 7, op2 = (hw >> 8) & 3;
            if (op2 == 3) {
                uint32_t n = ((hw >> 4) & 0xF) + 16, a = r[rd], q;
                switch (op1) {
                case 0: q = a >> n; break;
                case 1: case 3: q = a << n; break;
                case 2: q = (uint32_t)((int32_t)a >> n); break;
                case 4: q = (a >> n) | (a << (32 - n)); break;
                case 5: q = (a << n) | (a >> (32 - n)); break;
                default: e->stopped = true; FORCE_EVT(); e->stop_pc = iaddr; q = a; break;
                }
                r[rd] = q; NZ(q);
                break;
            }
            if (op2 == 2) {
                uint32_t imm = (ext_n == 1) ? ext0 : ((ext0 << 13) | ext1);
                switch (op1) {
                case 0: r[rd] = ext_n ? ADD(r[rs], imm) : ADD(r[rd], r[rs]); break;
                case 1: r[rd] = ext_n ? SUB(r[rs], imm) : SUB(r[rd], r[rs]); break;
                case 2: if (ext_n) CMP(r[rs], imm); else CMP(r[rd], r[rs]); break;
                case 3: if (ext_n) { e->stopped = true; FORCE_EVT(); e->stop_pc = iaddr; break; } r[rd] = r[rs]; break;
                case 4: r[rd] = ext_n ? LOG(r[rs] & imm) : LOG(r[rd] & r[rs]); break;
                case 5: r[rd] = ext_n ? LOG(r[rs] | imm) : LOG(r[rd] | r[rs]); break;
                case 6: r[rd] = ext_n ? LOG(r[rs] ^ imm) : LOG(r[rd] ^ r[rs]); break;
                case 7: if (ext_n) { e->stopped = true; FORCE_EVT(); e->stop_pc = iaddr; break; } r[rd] = LOG(~r[rs]); break;
                }
                break;
            }
            {
                uint32_t addr = r[rs];
                int postinc = (op2 == 1);
                if (ext_n) {
                    if (postinc) { e->stopped = true; FORCE_EVT(); e->stop_pc = iaddr; break; }
                    addr += (ext_n == 1) ? ext0 : ((ext0 << 13) | ext1);
                }
                switch (op1) {
                case 0: r[rd] = SEXT(f_rd8(e, addr), 8);   if (postinc) r[rs] += 1; break;
                case 1: r[rd] = f_rd8(e, addr);            if (postinc) r[rs] += 1; break;
                case 2: r[rd] = SEXT(f_rd16(e, addr), 16); if (postinc) r[rs] += 2; break;
                case 3: r[rd] = f_rd16(e, addr);           if (postinc) r[rs] += 2; break;
                case 4: r[rd] = f_rd32(e, addr);           if (postinc) r[rs] += 4; break;
                case 5: WR8(addr, r[rd]); if (postinc) r[rs] += 1; break;
                case 6: WR16(addr, r[rd]); if (postinc) r[rs] += 2; break;
                case 7: WR32(addr, r[rd]); if (postinc) r[rs] += 4; break;
                }
                break;
            }
        }
        case 2: {
            int op1 = (hw >> 10) & 7;
            uint32_t imm6 = (hw >> 4) & 0x3F, off;
            if (ext_n == 0) { static const uint8_t scale[8] = {1,1,2,2,4,1,2,4}; off = imm6 * scale[op1]; }
            else if (ext_n == 1) off = (ext0 << 6) | imm6;
            else off = (ext0 << 19) | (ext1 << 6) | imm6;
            uint32_t addr = sp + off;
            switch (op1) {
            case 0: r[rd] = SEXT(f_rd8(e, addr), 8); break;
            case 1: r[rd] = f_rd8(e, addr); break;
            case 2: r[rd] = SEXT(f_rd16(e, addr), 16); break;
            case 3: r[rd] = f_rd16(e, addr); break;
            case 4: r[rd] = f_rd32(e, addr); break;
            case 5: WR8(addr, r[rd]); break;
            case 6: WR16(addr, r[rd]); break;
            case 7: WR32(addr, r[rd]); break;
            }
            break;
        }
        case 3: {
            int op1 = (hw >> 10) & 7;
            uint32_t imm6 = (hw >> 4) & 0x3F, imm;
            if (ext_n == 0) imm = (op1 == 0 || op1 == 1) ? imm6 : SEXT(imm6, 6);
            else if (ext_n == 1) { imm = (ext0 << 6) | imm6; if (!(op1 == 0 || op1 == 1)) imm = SEXT(imm, 19); }
            else imm = (ext0 << 19) | (ext1 << 6) | imm6;
            switch (op1) {
            case 0: r[rd] = ADD(r[rd], imm); break;
            case 1: r[rd] = SUB(r[rd], imm); break;
            case 2: CMP(r[rd], imm); break;
            case 3: r[rd] = imm; break;
            case 4: r[rd] = LOG(r[rd] & imm); break;
            case 5: r[rd] = LOG(r[rd] | imm); break;
            case 6: r[rd] = LOG(r[rd] ^ imm); break;
            case 7: r[rd] = LOG(~imm); break;
            }
            break;
        }
        case 4: {
            int op1 = (hw >> 10) & 7, op2 = (hw >> 8) & 3;
            if (op1 == 0) { sp += (uint32_t)(hw & 0x3FF) * 4; break; }
            if (op1 == 1) { sp -= (uint32_t)(hw & 0x3FF) * 4; break; }
            uint32_t amt = ((op2 == 0) ? ((hw >> 4) & 0xF) : r[rs]) & 31, a = r[rd], q;
            switch (op1) {
            case 2: q = a >> amt; r[rd] = q; NZ(q); break;
            case 3: case 5: q = a << amt; r[rd] = q; NZ(q); break;
            case 4:
                if (op2 == 2) { uint32_t v = r[rs]; r[rd] = (v << 24) | ((v & 0xFF00) << 8) | ((v >> 8) & 0xFF00) | (v >> 24); }
                else { q = (uint32_t)((int32_t)a >> amt); r[rd] = q; NZ(q); }
                break;
            case 6:
                if (op2 == 2) { uint32_t v = r[rs];
                    r[rd] = ((v & 0x00FF0000) << 8) | ((v >> 8) & 0x00FF0000) | ((v & 0x000000FF) << 8) | ((v >> 8) & 0x000000FF); }
                else { q = amt ? ((a >> amt) | (a << (32 - amt))) : a; r[rd] = q; NZ(q); }
                break;
            case 7: q = amt ? ((a << amt) | (a >> (32 - amt))) : a; r[rd] = q; NZ(q); break;
            }
            break;
        }
        case 5: {
            int op1 = (hw >> 10) & 7, op2 = (hw >> 8) & 3;
            switch (op1 * 4 + op2) {
            case 0: { uint32_t v = r[rs];
                switch (hw & 0xF) {
                case 0: PSR_UNPACK(v); break;
                case 1: sp = v; break;
                case 2: e->alr = v; break;
                case 3: e->ahr = v; break;
                case 8: e->ttbr = v; break;
                case 10: e->idir = v; break;
                case 11: e->dbbr = v; break;
                case 15: pc = v; break;
                default: e->stopped = true; FORCE_EVT(); e->stop_pc = iaddr; break;
                } break; }
            case 1: r[rd] = SEXT(r[rs] & 0xFF, 8); break;
            case 2: e->alr = (uint32_t)((int32_t)SEXT(r[rd] & 0xFFFF, 16) * (int32_t)SEXT(r[rs] & 0xFFFF, 16)); break;
            case 4: {
                uint32_t v;
                switch (rs) {
                case 0: v = PSR_PACK(); break;
                case 1: v = sp; break;
                case 2: v = e->alr; break;
                case 3: v = e->ahr; break;
                case 8: v = e->ttbr; break;
                case 10: v = e->idir; break;
                case 11: v = e->dbbr; break;
                case 15: v = iaddr; break;
                default: e->stopped = true; FORCE_EVT(); e->stop_pc = iaddr; v = 0; break;
                }
                r[rd] = v; break; }
            case 5: r[rd] = r[rs] & 0xFF; break;
            case 6: e->alr = (r[rd] & 0xFFFF) * (r[rs] & 0xFFFF); break;
            case 8: case 12: case 16: case 20: {
                uint32_t addr = r[rs];
                if (ext_n) addr += (ext_n == 1) ? ext0 : ((ext0 << 13) | ext1);
                int pos = hw & 7;
                uint32_t b = f_rd8(e, addr);
                switch (op1) {
                case 2: fl = (fl & ~4u) | ((uint32_t)(((b >> pos) & 1) == 0) << 2); break;
                case 3: WR8(addr, b & ~(1u << pos)); break;
                case 4: WR8(addr, b |  (1u << pos)); break;
                case 5: WR8(addr, b ^  (1u << pos)); break;
                }
                break; }
            case 9: r[rd] = SEXT(r[rs] & 0xFFFF, 16); break;
            case 10: { int64_t p = (int64_t)(int32_t)r[rd] * (int32_t)r[rs];
                       e->alr = (uint32_t)p; e->ahr = (uint32_t)((uint64_t)p >> 32); break; }
            case 13: r[rd] = r[rs] & 0xFFFF; break;
            case 14: { uint64_t p = (uint64_t)r[rd] * r[rs]; e->alr = (uint32_t)p; e->ahr = (uint32_t)(p >> 32); break; }
            case 17: case 21: break;
            case 24: { uint32_t a = r[rd], b = r[rs], cz = FC, s = a + b;
                uint32_t v = SCARRY(a, b) ^ SCARRY(s, cz), c = (s < a) || ((s + cz) < s);
                uint32_t q = s + cz; fl = ((q >> 31) << 3) | ((uint32_t)(q == 0) << 2) | (v << 1) | c; r[rd] = q; break; }
            case 28: { uint32_t a = r[rd], b = r[rs], cz = FC, dd = a - b;
                uint32_t v = SBORROW(a, b) || SBORROW(dd, cz), c = (a < b) || (dd < cz);
                uint32_t q = dd - cz; fl = ((q >> 31) << 3) | ((uint32_t)(q == 0) << 2) | (v << 1) | c; r[rd] = q; break; }
            case 31: {
                int op3 = (hw >> 6) & 3, imm5 = hw & 0x1F;
                if (op3 == 0) break;
                if (op3 == 1 || op3 == 2) {
                    int val = (op3 == 1);
                    switch (imm5) {
                    case 0: fl = (fl & ~8u) | ((uint32_t)val << 3); break;
                    case 1: fl = (fl & ~4u) | ((uint32_t)val << 2); break;
                    case 2: fl = (fl & ~2u) | ((uint32_t)val << 1); break;
                    case 3: fl = (fl & ~1u) | (uint32_t)val; break;
                    case 4: e->f_ie = val; chk = 1; break;
                    default: break;
                    }
                    break;
                }
                e->stopped = true; FORCE_EVT(); e->stop_pc = iaddr; break; }
            default: e->stopped = true; FORCE_EVT(); e->stop_pc = iaddr; e->stop_code = hw; break;
            }
            break;
        }
        default:
            e->stopped = true; FORCE_EVT(); e->stop_pc = iaddr; e->stop_code = hw; break;
        }
        if (in_slot) {
            int k = dkind; dkind = D_NONE;
            if (k == D_JP) pc = dtarget;
            else if (k == D_CALL) { PUSH(pc); pc = dtarget; }
            else if (k == D_RET) pc = POP();
        }
        if (left <= 0 && !slot) {
            uint64_t cyc = CYC();
            if (e->halted || e->stopped) break;
            if (cyc - e->last_tick >= TICK) {
                SYNC_OUT();
                periph_tick(e, (uint32_t)(cyc - e->last_tick));
                e->last_tick = cyc;
                SYNC_IN();
                chk = 1;
            }
            if (cyc >= ziel) break;
            NEXT_EVT(cyc);
        }
    }
out:
#undef r
    SYNC_OUT();
    e->fw_ptr = fwp; e->fw_lo = fwlo; e->fw_len = fwlen;
}

#ifndef TC_WITH_REF
void cpu_reset(Emu *e)
{
    memset(e->r, 0, sizeof e->r);
    e->sp = 0; e->alr = e->ahr = 0;
    e->ttbr = e->dev.ttbr_reset;
    e->idir = e->dbbr = 0;
    e->f_n = e->f_z = e->f_v = e->f_c = e->f_ie = 0;
    e->il = 0; e->psr_rest = 0;
    e->ext_count = 0;
    e->halted = false; e->halt_hard = false;
    e->stopped = false;
    e->pc = mem_read32(e, e->ttbr);
}
#endif
