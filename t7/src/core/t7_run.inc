/*
 * t7_run.inc - fast run loop for the Paradise (included at the end of t7_cpu.c with T7_FASTRUN).
 *
 * Same instructions as t7_cpu_step(), but the 6502 registers and flags live in local
 * variables for a whole run (the compiler keeps most of them in ARM registers) instead of
 * being loaded from and stored to memory for every instruction. The result is the same
 * instruction by instruction (checked on the PC against t7_cpu_step, see t7/host/runcmp.c).
 * Author: mameise
 */
#ifdef T7_FASTRUN
#include "t7_sound.h"
static void ir_hooks(void)
{
  if (CPU.pc == 0x60C4) {
    s_ir_activity++;
    if (s_ir_rx_pending) {
      for (int i = 0; i < 32; i++) g_wram[0x200 + i] = s_ir_rx_pkt[i];
      g_wram[0x5A] = 0x20; g_wram[0x01B2] = 0x20;
      CPU.a = 0x80; s_nz_z = 0x80; s_flag_c = 0; CPU.pc = 0x60C7;
      s_ir_rx_pending = 0;
    }
  } else if (s_ir_bridge_en && CPU.pc == 0x7926) {
    s_ir_tx_pending = 1;
  }
}

uint32_t T7_HOT t7_cpu_run(uint32_t budget)
{
  uint32_t pc = CPU.pc, a = CPU.a, x = CPU.x, y = CPU.y, sp = CPU.s;
  uint32_t fc = s_flag_c ? 1u : 0u, fv = s_flag_v, fd = s_flag_d, fi = s_flag_i, nzn = s_nz_n, nzz = s_nz_z;
  uint8_t *const *rp = t7_read_page, *const *wp = t7_write_page;
  const int son = g_snd_on;
  int32_t snd = g_snd_next;
  uint32_t cyc = 0;
#ifdef T7_JIT
  uint32_t lp = pc + 0x8000u;          /* pc of the last interpreted instruction: blocks start only where a run of code starts */
#endif
  uint32_t chk = 1, b3 = (g_rom_bank_id == 3 && !g_t7_static_off);   /* look at IRQs / IR hooks only when something changed */

#define T7R_SAVE() do { CPU.pc = (uint16_t)pc; CPU.a = (uint8_t)a; CPU.x = (uint8_t)x; CPU.y = (uint8_t)y; CPU.s = (uint8_t)sp; \
  s_flag_c = (uint8_t)fc; s_flag_v = (uint8_t)fv; s_flag_d = (uint8_t)fd; s_flag_i = (uint8_t)fi; s_nz_n = (uint8_t)nzn; s_nz_z = (uint8_t)nzz; g_snd_next = snd; } while (0)
#define T7R_LOAD() do { pc = CPU.pc; a = CPU.a; x = CPU.x; y = CPU.y; sp = CPU.s; fc = s_flag_c ? 1u : 0u; fv = s_flag_v; fd = s_flag_d; \
  fi = s_flag_i; nzn = s_nz_n; nzz = s_nz_z; snd = g_snd_next; } while (0)
#define RD(ad) ({ uint32_t _ra = (ad); const uint8_t *_rp = rp[_ra >> 8]; _rp ? (uint32_t)_rp[_ra & 0xFF] : (uint32_t)t7_io_read((uint16_t)_ra); })
#define WR(ad, v) do { uint32_t _wa = (ad); uint8_t *_wp = wp[_wa >> 8]; uint8_t _wv = (uint8_t)(v); \
  if (_wp) _wp[_wa & 0xFF] = _wv; else if ((_wa >> 12) == 3) { g_snd_next = snd; t7_io_write((uint16_t)_wa, _wv); snd = g_snd_next; chk = 1; b3 = (g_rom_bank_id == 3 && !g_t7_static_off); } } while (0)
#define FETCH() ({ uint32_t _fv = RD(pc); pc = (pc + 1) & 0xFFFF; _fv; })
#define FETCH16() ({ uint32_t _f16l = FETCH(); uint32_t _f16h = FETCH(); _f16l | (_f16h << 8); })
#define PUSH(v) do { g_wram[0x100 | sp] = (uint8_t)(v); sp = (sp - 1) & 0xFF; } while (0)   /* page 1 is always work RAM */
#define PULL() ({ sp = (sp + 1) & 0xFF; (uint32_t)g_wram[0x100 | sp]; })
#define NZ(v) do { nzn = nzz = (v); } while (0)
#define PACKP() ((fc ? 0x01u : 0u) | (nzz == 0 ? 0x02u : 0u) | (fi ? 0x04u : 0u) | (fd ? 0x08u : 0u) | 0x20u | (fv ? 0x40u : 0u) | (nzn & 0x80u))
#define UNPACKP(p) do { uint32_t _up = (p); fc = _up & 1u; fi = _up & 0x04u; fd = _up & 0x08u; fv = _up & 0x40u; nzn = (_up & 0x80u) ? 0x80u : 0u; nzz = (_up & 0x02u) ? 0u : 1u; } while (0)
  /* addressing */
#define A_IMM() ({ uint32_t _im = pc; pc = (pc + 1) & 0xFFFF; _im; })
#define A_ZP()  FETCH()
#define ZRD(z) ((uint32_t)g_wram[(z)])      /* page 0: always work RAM */
#define ZWR(z, v) (g_wram[(z)] = (uint8_t)(v))
#define A_ZPX() ((FETCH() + x) & 0xFF)
#define A_ZPY() ((FETCH() + y) & 0xFF)
#define A_ABS() FETCH16()
#define A_ABX() ((FETCH16() + x) & 0xFFFF)
#define A_ABY() ((FETCH16() + y) & 0xFFFF)
#define A_INX() ({ uint32_t _xz = (FETCH() + x) & 0xFF; uint32_t _xl = ZRD(_xz); uint32_t _xh = ZRD((_xz + 1) & 0xFF); _xl | (_xh << 8); })
#define A_INY() ({ uint32_t _yz = FETCH(); uint32_t _yl = ZRD(_yz); uint32_t _yh = ZRD((_yz + 1) & 0xFF); ((_yl | (_yh << 8)) + y) & 0xFFFF; })
#define A_IND() ({ uint32_t _ip = FETCH16(); uint32_t _il = RD(_ip); uint32_t _ih = RD((_ip & 0xFF00) | ((_ip + 1) & 0xFF)); _il | (_ih << 8); })
  /* operations */
#define LDA(m) do { a = RD(m); NZ(a); } while (0)
#define LDX(m) do { x = RD(m); NZ(x); } while (0)
#define LDY(m) do { y = RD(m); NZ(y); } while (0)
#define AND(m) do { a &= RD(m); NZ(a); } while (0)
#define ORA(m) do { a |= RD(m); NZ(a); } while (0)
#define EOR(m) do { a ^= RD(m); NZ(a); } while (0)
#define CMPR(r, m) do { uint32_t _d = RD(m); fc = (r) >= _d; NZ(((r) - _d) & 0xFF); } while (0)
#define INCM(m) do { uint32_t _a = (m); uint32_t _v = (RD(_a) + 1) & 0xFF; NZ(_v); WR(_a, _v); } while (0)
#define DECM(m) do { uint32_t _a = (m); uint32_t _v = (RD(_a) - 1) & 0xFF; NZ(_v); WR(_a, _v); } while (0)
#define ASLM(m) do { uint32_t _a = (m); uint32_t _d = RD(_a); uint32_t _o = (_d << 1) & 0xFF; fc = _d >> 7; NZ(_o); WR(_a, _o); } while (0)
#define LSRM(m) do { uint32_t _a = (m); uint32_t _d = RD(_a); uint32_t _o = _d >> 1; fc = _d & 1u; NZ(_o); WR(_a, _o); } while (0)
#define ROLM(m) do { uint32_t _a = (m); uint32_t _d = RD(_a); uint32_t _o = ((_d << 1) & 0xFF) | fc; fc = _d >> 7; NZ(_o); WR(_a, _o); } while (0)
#define RORM(m) do { uint32_t _a = (m); uint32_t _d = RD(_a); uint32_t _o = (_d >> 1) | (fc << 7); fc = _d & 1u; NZ(_o); WR(_a, _o); } while (0)
#define BIT(m) do { uint32_t _v = RD(m); fv = _v & 0x40u; nzn = _v; nzz = a & _v; } while (0)
#define ADC(m) do { uint32_t _d = RD(m); uint32_t _o = a + _d + fc; fv = (~(a ^ _d) & (a ^ _o)) & 0x80u; NZ(_o & 0xFF); \
  if (fd) { int _al = (int)(a & 0x0F) + (int)(_d & 0x0F) + (int)fc; int _ah = (int)(a & 0xF0) + (int)(_d & 0xF0) + ((_al >= 0x10) ? 0x10 : 0); \
    if (_al > 0x09) _al += 0x06; if (_ah > 0x90) _ah += 0x60; _o = (uint32_t)((_al & 0x0F) + _ah); } \
  fc = _o > 0xFF; a = _o & 0xFF; } while (0)
#define SBC(m) do { uint32_t _d = RD(m); int _o = (int)a - (int)_d - (fc ? 0 : 1); fv = ((a ^ _d) & (a ^ (uint32_t)_o)) & 0x80u; NZ((uint32_t)_o & 0xFF); \
  fc = _o >= 0; \
  if (fd) { int _al = (int)(a & 0x0F) - (int)(_d & 0x0F) - (fc ? 0 : 1); int _ah = (int)(a & 0xF0) - (int)(_d & 0xF0) - ((_al < 0) ? 0x10 : 0); \
    if (_al < 0) _al -= 0x06; if (_ah < 0) _ah -= 0x60; _o = (_al & 0x0F) + _ah; } \
  a = (uint32_t)_o & 0xFF; } while (0)
#define BR(cond) do { int32_t _o = (int8_t)FETCH(); if (cond) pc = (pc + (uint32_t)_o) & 0xFFFF; } while (0)

  while (cyc < budget) {
    if (b3 && (pc == 0x60C4 || pc == 0x7926)) { T7R_SAVE(); ir_hooks(); T7R_LOAD(); }
    if (chk) {
#ifdef T7_STOP
      /* test12: halted (STOP): the cycles up to the end or the next sound event pass without instructions; any
       * IRQ request wakes the CPU (taken only when the I flag allows, as on the chip) */
      if (g_t7_halted) {
        if (g_cpureg[0x73] | g_cpureg[0x74]) g_t7_halted = 0;
        else {
          uint32_t lim = budget - cyc;
          if (son) { if (snd <= 0) lim = 1; else if ((uint32_t)snd < lim) lim = (uint32_t)snd; }
          cyc += lim; g_t7_idle_cyc += lim;
          if (son && (snd -= (int32_t)lim) <= 0) { g_snd_next = snd; t7_snd_service(); snd = g_snd_next; }
          continue;                                    /* chk stays set */
        }
      }
#endif
      if (g_irq_pending_any && !fi) {
        uint32_t pending = ((uint32_t)g_cpureg[0x73] << 8) | g_cpureg[0x74];
        if (pending) {
          PROFILE_INC(irq_entries);
          int idx = __builtin_clz(pending << 16);
          PUSH(pc >> 8); PUSH(pc & 0xFF); PUSH(PACKP());
          pc = g_irq_vectors[idx]; fi = 1;
          g_irq_pending_any = (g_cpureg[0x73] | g_cpureg[0x74]) != 0;
        }
      }
#ifdef T7_IDLE
      /* test12: the BEQ went back to $CE66 (chk bit 1) and no IRQ came: the passes that fit up to the end or the
       * next sound event at once, the same state as interpreting them (t7_idle_check, TamaGo-Pebble) */
      if ((chk & 2u) && pc == T7_IDLE_PC && !fd && g_wram[0x2D] == 0) {
        uint32_t lim = budget - cyc;
        if (son) { if (snd <= 0) lim = 0; else if ((uint32_t)snd < lim) lim = (uint32_t)snd; }
        uint32_t pass = t7_idle_pass_cyc(), n = lim / pass;
        if (n) {
          static const uint8_t k[5] = { 0x07, 0x03, 0x0D, 0x17, 0x13 };
          uint32_t prev = 0;
          for (int i = 0; i < 5; i++) { uint32_t v = (g_wram[0xC4 + i] + (n - 1u) * k[i]) & 0xFF; prev = v; g_wram[0xC4 + i] = (uint8_t)(v + k[i]); }
          uint32_t o = prev + 0x13u;
          fv = (~(prev ^ 0x13u) & (prev ^ o)) & 0x80u; fc = o > 0xFFu;
          g_wram[0x100 | sp] = 0xCE; g_wram[0x100 | ((sp - 1) & 0xFF)] = 0x74;
          a = 0; NZ(0);
          uint32_t s = n * pass;
          cyc += s; g_t7_idle_cyc += s;
          if (son && (snd -= (int32_t)s) <= 0) { g_snd_next = snd; t7_snd_service(); snd = g_snd_next; }
          chk = 1; continue;
        }
      }
#endif
    }
    chk = 0;
#ifdef T7_JIT
    /* 1.6.12 test2: translated ROM code, when the interpreter would run all of the block now (t7_jit.c) */
    if (g_t7j_on && !fd && pc >= 0x4000u) {
      T7JEnt *je = t7_jit_get((uint32_t)(uintptr_t)(rp[pc >> 8] + (pc & 0xFFu)), pc, rp, b3 | (((pc - lp - 1u) & 0xFFFFu) > 2u ? 2u : 0u));
      if (je && cyc + je->pre < budget && (!son || snd > (int32_t)je->all)) {
        T7J js;
        js.a = a; js.x = x; js.y = y; js.s = sp; js.nzn = nzn; js.nzz = nzz; js.fc = fc; js.fv = fv; js.fi = fi;
        js.snd = snd; js.wram = g_wram; js.rp = rp; js.wp = wp; js.cyc = 0; js.rem = (int32_t)(budget - cyc); js.son = (uint32_t)son;
        t7_jit_run(&js, je->code);
        a = js.a; x = js.x; y = js.y; sp = js.s; nzn = js.nzn; nzz = js.nzz; fc = js.fc; fv = js.fv; fi = js.fi; pc = js.pc;
        cyc += js.cyc;
        if (js.io) {                         /* ended after an I/O write: as WR, then the IRQ check */
          snd = g_snd_next; chk = 1; b3 = (g_rom_bank_id == 3 && !g_t7_static_off);
          if (son && (snd -= (int32_t)js.io) <= 0) { g_snd_next = snd; t7_snd_service(); snd = g_snd_next; }
        } else snd = js.snd;
        lp = pc + 0x8000u;
        continue;
      }
      lp = pc;
    }
#endif
#ifdef T7_COUNT
    { extern volatile uint32_t g_t7_ninstr; g_t7_ninstr++; }
#endif
    uint32_t op = FETCH();
    uint32_t c = opcode_cycles[op];
    switch (op) {
    case 0xA9: LDA(A_IMM()); break; case 0xA5: a = ZRD(FETCH()); NZ(a); break; case 0xB5: a = ZRD(A_ZPX()); NZ(a); break; case 0xAD: LDA(A_ABS()); break;
    case 0xBD: LDA(A_ABX()); break; case 0xB9: LDA(A_ABY()); break; case 0xA1: LDA(A_INX()); break; case 0xB1: LDA(A_INY()); break;
    case 0xA2: LDX(A_IMM()); break; case 0xA6: x = ZRD(FETCH()); NZ(x); break; case 0xB6: LDX(A_ZPY()); break; case 0xAE: LDX(A_ABS()); break; case 0xBE: LDX(A_ABY()); break;
    case 0xA0: LDY(A_IMM()); break; case 0xA4: y = ZRD(FETCH()); NZ(y); break; case 0xB4: LDY(A_ZPX()); break; case 0xAC: LDY(A_ABS()); break; case 0xBC: LDY(A_ABX()); break;
    case 0x85: ZWR(FETCH(), a); break; case 0x95: ZWR(A_ZPX(), a); break; case 0x8D: WR(A_ABS(), a); break; case 0x9D: WR(A_ABX(), a); break;
    case 0x99: WR(A_ABY(), a); break; case 0x81: WR(A_INX(), a); break; case 0x91: WR(A_INY(), a); break;
    case 0x86: ZWR(FETCH(), x); break; case 0x96: WR(A_ZPY(), x); break; case 0x8E: WR(A_ABS(), x); break;
    case 0x84: ZWR(FETCH(), y); break; case 0x94: WR(A_ZPX(), y); break; case 0x8C: WR(A_ABS(), y); break;
    case 0xAA: x = a; NZ(x); break; case 0xA8: y = a; NZ(y); break; case 0x8A: a = x; NZ(a); break; case 0x98: a = y; NZ(a); break;
    case 0xBA: x = sp; NZ(x); break; case 0x9A: sp = x; break;
    case 0xE8: x = (x + 1) & 0xFF; NZ(x); break; case 0xC8: y = (y + 1) & 0xFF; NZ(y); break;
    case 0xCA: x = (x - 1) & 0xFF; NZ(x); break; case 0x88: y = (y - 1) & 0xFF; NZ(y); break;
    case 0xE6: { uint32_t _z = FETCH(); uint32_t _v = (ZRD(_z) + 1) & 0xFF; NZ(_v); ZWR(_z, _v); } break; case 0xF6: INCM(A_ZPX()); break; case 0xEE: INCM(A_ABS()); break; case 0xFE: INCM(A_ABX()); break;
    case 0xC6: { uint32_t _z = FETCH(); uint32_t _v = (ZRD(_z) - 1) & 0xFF; NZ(_v); ZWR(_z, _v); } break; case 0xD6: DECM(A_ZPX()); break; case 0xCE: DECM(A_ABS()); break; case 0xDE: DECM(A_ABX()); break;
    case 0x29: AND(A_IMM()); break; case 0x25: a &= ZRD(FETCH()); NZ(a); break; case 0x35: AND(A_ZPX()); break; case 0x2D: AND(A_ABS()); break;
    case 0x3D: AND(A_ABX()); break; case 0x39: AND(A_ABY()); break; case 0x21: AND(A_INX()); break; case 0x31: AND(A_INY()); break;
    case 0x09: ORA(A_IMM()); break; case 0x05: a |= ZRD(FETCH()); NZ(a); break; case 0x15: ORA(A_ZPX()); break; case 0x0D: ORA(A_ABS()); break;
    case 0x1D: ORA(A_ABX()); break; case 0x19: ORA(A_ABY()); break; case 0x01: ORA(A_INX()); break; case 0x11: ORA(A_INY()); break;
    case 0x49: EOR(A_IMM()); break; case 0x45: EOR(A_ZP()); break; case 0x55: EOR(A_ZPX()); break; case 0x4D: EOR(A_ABS()); break;
    case 0x5D: EOR(A_ABX()); break; case 0x59: EOR(A_ABY()); break; case 0x41: EOR(A_INX()); break; case 0x51: EOR(A_INY()); break;
    case 0x24: BIT(A_ZP()); break; case 0x2C: BIT(A_ABS()); break;
    case 0x0A: fc = a >> 7; a = (a << 1) & 0xFF; NZ(a); break;
    case 0x06: ASLM(A_ZP()); break; case 0x16: ASLM(A_ZPX()); break; case 0x0E: ASLM(A_ABS()); break; case 0x1E: ASLM(A_ABX()); break;
    case 0x4A: fc = a & 1u; a >>= 1; NZ(a); break;
    case 0x46: LSRM(A_ZP()); break; case 0x56: LSRM(A_ZPX()); break; case 0x4E: LSRM(A_ABS()); break; case 0x5E: LSRM(A_ABX()); break;
    case 0x2A: { uint32_t _o = ((a << 1) & 0xFF) | fc; fc = a >> 7; a = _o; NZ(a); } break;
    case 0x26: ROLM(A_ZP()); break; case 0x36: ROLM(A_ZPX()); break; case 0x2E: ROLM(A_ABS()); break; case 0x3E: ROLM(A_ABX()); break;
    case 0x6A: { uint32_t _o = (a >> 1) | (fc << 7); fc = a & 1u; a = _o; NZ(a); } break;
    case 0x66: RORM(A_ZP()); break; case 0x76: RORM(A_ZPX()); break; case 0x6E: RORM(A_ABS()); break; case 0x7E: RORM(A_ABX()); break;
    case 0x69: ADC(A_IMM()); break; case 0x65: ADC(A_ZP()); break; case 0x75: ADC(A_ZPX()); break; case 0x6D: ADC(A_ABS()); break;
    case 0x7D: ADC(A_ABX()); break; case 0x79: ADC(A_ABY()); break; case 0x61: ADC(A_INX()); break; case 0x71: ADC(A_INY()); break;
    case 0xE9: SBC(A_IMM()); break; case 0xE5: SBC(A_ZP()); break; case 0xF5: SBC(A_ZPX()); break; case 0xED: SBC(A_ABS()); break;
    case 0xFD: SBC(A_ABX()); break; case 0xF9: SBC(A_ABY()); break; case 0xE1: SBC(A_INX()); break; case 0xF1: SBC(A_INY()); break;
    case 0xC9: CMPR(a, A_IMM()); break; case 0xC5: { uint32_t _d = ZRD(FETCH()); fc = a >= _d; NZ((a - _d) & 0xFF); } break; case 0xD5: CMPR(a, A_ZPX()); break; case 0xCD: CMPR(a, A_ABS()); break;
    case 0xDD: CMPR(a, A_ABX()); break; case 0xD9: CMPR(a, A_ABY()); break; case 0xC1: CMPR(a, A_INX()); break; case 0xD1: CMPR(a, A_INY()); break;
    case 0xE0: CMPR(x, A_IMM()); break; case 0xE4: CMPR(x, A_ZP()); break; case 0xEC: CMPR(x, A_ABS()); break;
    case 0xC0: CMPR(y, A_IMM()); break; case 0xC4: CMPR(y, A_ZP()); break; case 0xCC: CMPR(y, A_ABS()); break;
    case 0x18: fc = 0; break; case 0x38: fc = 1; break; case 0x58: fi = 0; chk = 1; break; case 0x78: fi = 1; break;
    case 0xB8: fv = 0; break; case 0xD8: fd = 0; break; case 0xF8: fd = 1; break;
    case 0x48: PUSH(a); break;
    case 0x68: a = PULL(); NZ(a); break;
    case 0x08: PUSH(PACKP() | 0x10u); break;
    case 0x28: UNPACKP(PULL()); chk = 1; break;
    case 0x4C: pc = A_ABS(); break;
    case 0x6C: pc = A_IND(); break;
#ifdef T7_JSR_HLE
    case 0x20: { uint32_t _t = A_ABS(); uint32_t _r = (pc - 1) & 0xFFFF;
      if (g_t7_hle && (_t == 0xD460u || _t == 0xE4CDu)) {
        if (_t == 0xE4CDu && (g_t7_hle & 2u) && g_t7_dark) {      /* blit, screen off: JSR + RTS only */
          g_wram[0x100 | sp] = (uint8_t)(_r >> 8); g_wram[0x100 | ((sp - 1) & 0xFF)] = (uint8_t)_r;
          c += opcode_cycles[0x60]; break;
        }
        if (_t == 0xD460u && (g_t7_hle & 1u)) {                   /* delay: whole when its RTS runs in this run */
          uint32_t pre = c + opcode_cycles[0xA2] + 47u * (opcode_cycles[0xE8] + opcode_cycles[0xD0]), all = pre + opcode_cycles[0x60];
          if (cyc + pre < budget && (!son || snd > (int32_t)all)) {
            g_wram[0x100 | sp] = (uint8_t)(_r >> 8); g_wram[0x100 | ((sp - 1) & 0xFF)] = (uint8_t)_r;
            x = 0; nzn = 0; nzz = 0; c = all; break;
          }
        }
      }
      PUSH(_r >> 8); PUSH(_r & 0xFF); pc = _t; } break;
#else
    case 0x20: { uint32_t _t = A_ABS(); uint32_t _r = (pc - 1) & 0xFFFF; PUSH(_r >> 8); PUSH(_r & 0xFF); pc = _t; } break;
#endif
    case 0x60: { uint32_t _l = PULL(); uint32_t _h = PULL(); pc = ((_l | (_h << 8)) + 1) & 0xFFFF; } break;
    case 0x40: { UNPACKP(PULL()); uint32_t _l = PULL(); uint32_t _h = PULL(); pc = _l | (_h << 8); } chk = 1; break;
    case 0x00: pc = (pc + 1) & 0xFFFF; PUSH(pc >> 8); PUSH(pc & 0xFF); PUSH(PACKP() | 0x10u);
               { uint32_t _l = RD(0xFFFE); uint32_t _h = RD(0xFFFF); pc = _l | (_h << 8); } fi = 1; break;
    case 0x10: BR(!(nzn & 0x80u)); break; case 0x30: BR(nzn & 0x80u); break;
    case 0x50: BR(!fv); break; case 0x70: BR(fv); break;
    case 0x90: BR(!fc); break; case 0xB0: BR(fc); break;
    case 0xD0: BR(nzz != 0); break;
#ifdef T7_IDLE
    case 0xF0: BR(nzz == 0); if (pc == T7_IDLE_PC && g_t7_idle_ok) chk |= 2u; break;
#else
    case 0xF0: BR(nzz == 0); break;
#endif
    case 0xEA: break;
    default: c = 2; break;
    }
    cyc += c;
    if (son && (snd -= (int32_t)c) <= 0) { g_snd_next = snd; t7_snd_service(); snd = g_snd_next; chk = 1; }
  }
  T7R_SAVE();
  return cyc;
}
#undef T7R_SAVE
#undef T7R_LOAD
#undef RD
#undef WR
#endif
