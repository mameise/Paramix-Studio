#!/usr/bin/env python3
# Generates z80_ops.inc (cases of the main opcode switch) and z80_fns.inc (CB, ED, DD/FD) for z80.h.
# Author: mameise
import os
R = ['B', 'C', 'D', 'E', 'H', 'L', None, 'A']
ALU = ['z_add', 'z_adc', 'z_sub', 'z_sbc', 'z_and', 'z_xor', 'z_or', 'z_cp']
CC = ['!(F & ZF)', '(F & ZF)', '!(F & CF)', '(F & CF)', '!(F & PF)', '(F & PF)', '!(F & SF)', '(F & SF)']
RP = ['BC', 'DE', 'HL', 'SP']
ROT = ['z_rlc', 'z_rrc', 'z_rl', 'z_rr', 'z_sla', 'z_sra', 'z_sll', 'z_srl']
main = {}
def m(op, code, t):
    assert op not in main, hex(op)
    main[op] = (code, t)

m(0x00, '', 4)
for i, rp in enumerate(RP):
    b = i << 4
    m(b | 0x01, f'{rp} = z_imm16(z);', 10)
    m(b | 0x03, f'{rp}++;', 6)
    m(b | 0x0B, f'{rp}--;', 6)
    m(b | 0x09, f'HL = z_add16(z, HL, {rp});', 11)
m(0x02, 'WR(BC, A); z->wz.b.l = (uint8_t)(BC + 1); z->wz.b.h = A;', 7)
m(0x12, 'WR(DE, A); z->wz.b.l = (uint8_t)(DE + 1); z->wz.b.h = A;', 7)
m(0x0A, 'A = RD(BC); WZ = (uint16_t)(BC + 1);', 7)
m(0x1A, 'A = RD(DE); WZ = (uint16_t)(DE + 1);', 7)
m(0x22, '{ uint16_t a = z_imm16(z); WR16(a, HL); WZ = (uint16_t)(a + 1); }', 16)
m(0x2A, '{ uint16_t a = z_imm16(z); HL = RD16(a); WZ = (uint16_t)(a + 1); }', 16)
m(0x32, '{ uint16_t a = z_imm16(z); WR(a, A); z->wz.b.l = (uint8_t)(a + 1); z->wz.b.h = A; }', 13)
m(0x3A, '{ uint16_t a = z_imm16(z); A = RD(a); WZ = (uint16_t)(a + 1); }', 13)
for i, r in enumerate(R):
    if r is None:
        m(0x34, '{ uint8_t v = RD(HL); WR(HL, z_inc(z, v)); }', 11)
        m(0x35, '{ uint8_t v = RD(HL); WR(HL, z_dec(z, v)); }', 11)
        m(0x36, '{ uint8_t v = IMM8(); WR(HL, v); }', 10)
        continue
    m(i << 3 | 4, f'{r} = z_inc(z, {r});', 4)
    m(i << 3 | 5, f'{r} = z_dec(z, {r});', 4)
    m(i << 3 | 6, f'{r} = IMM8();', 7)
m(0x07, 'A = (uint8_t)(A << 1 | A >> 7); F = (uint8_t)((F & (SF | ZF | PF)) | (A & (YF | XF | CF)));', 4)
m(0x0F, 'F = (uint8_t)((F & (SF | ZF | PF)) | (A & CF)); A = (uint8_t)(A >> 1 | A << 7); F |= (uint8_t)(A & (YF | XF));', 4)
m(0x17, '{ uint8_t r = (uint8_t)(A << 1 | (F & CF)); F = (uint8_t)((F & (SF | ZF | PF)) | (A >> 7) | (r & (YF | XF))); A = r; }', 4)
m(0x1F, '{ uint8_t r = (uint8_t)(A >> 1 | F << 7); F = (uint8_t)((F & (SF | ZF | PF)) | (A & CF) | (r & (YF | XF))); A = r; }', 4)
m(0x08, '{ uint16_t x = AF; AF = z->af2; z->af2 = x; }', 4)
m(0x10, '{ int8_t e = (int8_t)IMM8(); if (--B) { PC = (uint16_t)(PC + e); WZ = PC; t = 13; break; } }', 8)
m(0x18, '{ int8_t e = (int8_t)IMM8(); PC = (uint16_t)(PC + e); WZ = PC; }', 12)
for k in range(4):
    m(0x20 | k << 3, f'{{ int8_t e = (int8_t)IMM8(); if ({CC[k]}) {{ PC = (uint16_t)(PC + e); WZ = PC; t = 12; break; }} }}', 7)
m(0x27, 'z_daa(z);', 4)
m(0x2F, 'A ^= 0xFF; F = (uint8_t)((F & (SF | ZF | PF | CF)) | HF | NF | (A & (YF | XF)));', 4)
m(0x37, 'F = (uint8_t)((F & (SF | ZF | PF)) | CF | (A & (YF | XF)));', 4)
m(0x3F, 'F = (uint8_t)(((F & (SF | ZF | PF | CF)) | ((F & CF) << 4) | (A & (YF | XF))) ^ CF);', 4)
for d in range(8):
    for s in range(8):
        op = 0x40 | d << 3 | s
        if op == 0x76:
            m(op, 'z->halt = 1;', 4); continue
        if s == 6: m(op, f'{R[d]} = RD(HL);', 7)
        elif d == 6: m(op, f'WR(HL, {R[s]});', 7)
        else: m(op, '' if d == s else f'{R[d]} = {R[s]};', 4)
for k in range(8):
    for s in range(8):
        op = 0x80 | k << 3 | s
        if s == 6: m(op, f'{ALU[k]}(z, RD(HL));', 7)
        else: m(op, f'{ALU[k]}(z, {R[s]});', 4)
    m(0xC6 | k << 3, f'{ALU[k]}(z, IMM8());', 7)
    m(0xC7 | k << 3, f'z_push(z, PC); PC = 0x{k * 8:02X}; WZ = PC;', 11)
    m(0xC0 | k << 3, f'if ({CC[k]}) {{ PC = z_pop(z); WZ = PC; t = 11; break; }}', 5)
    m(0xC2 | k << 3, f'{{ uint16_t a = z_imm16(z); WZ = a; if ({CC[k]}) PC = a; }}', 10)
    m(0xC4 | k << 3, f'{{ uint16_t a = z_imm16(z); WZ = a; if ({CC[k]}) {{ z_push(z, PC); PC = a; t = 17; break; }} }}', 10)
for i, rp in enumerate(['BC', 'DE', 'HL', 'AF']):
    m(0xC1 | i << 4, f'{rp} = z_pop(z);', 10)
    m(0xC5 | i << 4, f'z_push(z, {rp});', 11)
m(0xC3, 'PC = z_imm16(z); WZ = PC;', 10)
m(0xC9, 'PC = z_pop(z); WZ = PC;', 10)
m(0xCD, '{ uint16_t a = z_imm16(z); z_push(z, PC); PC = a; WZ = a; }', 17)
m(0xD3, '{ uint8_t n = IMM8(); Z_OUT((uint16_t)(n | A << 8), A); z->wz.b.l = (uint8_t)(n + 1); z->wz.b.h = A; }', 11)
m(0xDB, '{ uint16_t p = (uint16_t)(IMM8() | A << 8); A = Z_IN(p); WZ = (uint16_t)(p + 1); }', 11)
m(0xD9, '{ uint16_t x = BC; BC = z->bc2; z->bc2 = x; x = DE; DE = z->de2; z->de2 = x; x = HL; HL = z->hl2; z->hl2 = x; }', 4)
m(0xE3, '{ uint16_t v = RD16(SP); WR16(SP, HL); HL = v; WZ = v; }', 19)
m(0xE9, 'PC = HL;', 4)
m(0xEB, '{ uint16_t x = DE; DE = HL; HL = x; }', 4)
m(0xF3, 'z->iff1 = z->iff2 = 0;', 4)
m(0xFB, 'z->iff1 = z->iff2 = 1; z->ei = 1;', 4)
m(0xF9, 'SP = HL;', 6)
m(0xCB, 'op = z_fetch(z); t = z80_cb(z, op); break;', 0)
m(0xED, 'op = z_fetch(z); t = z80_ed(z, op); break;', 0)
m(0xDD, 'op = z_fetch(z); t = z80_xy(z, &z->ix.w, op); if (!t) { z->cyc -= 4; goto again; } break;', 0)
m(0xFD, 'op = z_fetch(z); t = z80_xy(z, &z->iy.w, op); if (!t) { z->cyc -= 4; goto again; } break;', 0)
assert len(main) == 256, len(main)

out = []
for op in range(256):
    code, t = main[op]
    if t: out.append(f'case 0x{op:02X}: t = {t}; {code} break;')
    else: out.append(f'case 0x{op:02X}: {code}')
OUTD = os.path.dirname(os.path.abspath(__file__))
open(os.path.join(OUTD, 'z80_ops.inc'), 'w').write('/* generated by gen_z80.py */\n' + '\n'.join(out) + '\n')

# ---- CB ----
f = ['/* generated by gen_z80.py */', 'static int z80_cb(Z80 *z, uint8_t op)', '{', '  switch (op) {']
for op in range(256):
    s = op & 7; r = R[s]; k = (op >> 3) & 7; g = op >> 6
    if g == 0:
        if r: f.append(f'  case 0x{op:02X}: {r} = {ROT[k]}(z, {r}); return 8;')
        else: f.append(f'  case 0x{op:02X}: {{ uint8_t v = RD(HL); WR(HL, {ROT[k]}(z, v)); }} return 15;')
    elif g == 1:
        if r: f.append(f'  case 0x{op:02X}: z_bit(z, {k}, {r}); return 8;')
        else: f.append(f'  case 0x{op:02X}: z_bitm(z, {k}, RD(HL), z->wz.b.h); return 12;')
    elif g == 2:
        if r: f.append(f'  case 0x{op:02X}: {r} &= (uint8_t)~{1 << k}; return 8;')
        else: f.append(f'  case 0x{op:02X}: {{ uint8_t v = RD(HL); WR(HL, v & (uint8_t)~{1 << k}); }} return 15;')
    else:
        if r: f.append(f'  case 0x{op:02X}: {r} |= {1 << k}; return 8;')
        else: f.append(f'  case 0x{op:02X}: {{ uint8_t v = RD(HL); WR(HL, v | {1 << k}); }} return 15;')
f += ['  }', '  return 8;', '}', '']

# ---- ED ----
f += ['static int z80_ed(Z80 *z, uint8_t op)', '{', '  switch (op) {']
for i, r in enumerate(R):
    op = 0x40 | i << 3
    if r: f.append(f'  case 0x{op:02X}: {r} = Z_IN(BC); F = (uint8_t)((F & CF) | zSZP[{r}]); WZ = (uint16_t)(BC + 1); return 12;')
    else: f.append(f'  case 0x{op:02X}: {{ uint8_t v = Z_IN(BC); F = (uint8_t)((F & CF) | zSZP[v]); WZ = (uint16_t)(BC + 1); }} return 12;')
    f.append(f'  case 0x{op | 1:02X}: Z_OUT(BC, {r if r else 0}); WZ = (uint16_t)(BC + 1); return 12;')
for i, rp in enumerate(RP):
    b = 0x40 | i << 4
    f.append(f'  case 0x{b | 2:02X}: z_sbc16(z, {rp}); return 15;')
    f.append(f'  case 0x{b | 0xA:02X}: z_adc16(z, {rp}); return 15;')
    f.append(f'  case 0x{b | 3:02X}: {{ uint16_t a = z_imm16(z); WR16(a, {rp}); WZ = (uint16_t)(a + 1); }} return 20;')
    f.append(f'  case 0x{b | 0xB:02X}: {{ uint16_t a = z_imm16(z); {rp} = RD16(a); WZ = (uint16_t)(a + 1); }} return 20;')
for op in (0x44, 0x4C, 0x54, 0x5C, 0x64, 0x6C, 0x74, 0x7C):
    f.append(f'  case 0x{op:02X}: {{ uint8_t v = A; A = 0; z_sub(z, v); }} return 8;')
for op in (0x45, 0x4D, 0x55, 0x5D, 0x65, 0x6D, 0x75, 0x7D):
    f.append(f'  case 0x{op:02X}: PC = z_pop(z); WZ = PC; z->iff1 = z->iff2; return 14;')
for op, im in ((0x46, 0), (0x4E, 0), (0x66, 0), (0x6E, 0), (0x56, 1), (0x76, 1), (0x5E, 2), (0x7E, 2)):
    f.append(f'  case 0x{op:02X}: z->im = {im}; return 8;')
f.append('  case 0x47: z->i = A; return 9;')
f.append('  case 0x4F: z->r = A; z->r7 = A; return 9;')
f.append('  case 0x57: A = z->i; F = (uint8_t)((F & CF) | zSZ[A] | (z->iff2 ? VF : 0)); return 9;')
f.append('  case 0x5F: A = (uint8_t)((z->r & 0x7F) | (z->r7 & 0x80)); F = (uint8_t)((F & CF) | zSZ[A] | (z->iff2 ? VF : 0)); return 9;')
f.append('  case 0x67: { uint8_t n = RD(HL); WR(HL, (uint8_t)(n >> 4 | A << 4)); A = (uint8_t)((A & 0xF0) | (n & 0x0F)); F = (uint8_t)((F & CF) | zSZP[A]); WZ = (uint16_t)(HL + 1); } return 18;')
f.append('  case 0x6F: { uint8_t n = RD(HL); WR(HL, (uint8_t)(n << 4 | (A & 0x0F))); A = (uint8_t)((A & 0xF0) | (n >> 4)); F = (uint8_t)((F & CF) | zSZP[A]); WZ = (uint16_t)(HL + 1); } return 18;')
f.append('  case 0xA0: z_ldi(z, 1); return 16;')
f.append('  case 0xA8: z_ldi(z, -1); return 16;')
f.append('  case 0xB0: z_ldi(z, 1); if (BC) { PC -= 2; WZ = (uint16_t)(PC + 1); return 21; } return 16;')
f.append('  case 0xB8: z_ldi(z, -1); if (BC) { PC -= 2; WZ = (uint16_t)(PC + 1); return 21; } return 16;')
f.append('  case 0xA1: z_cpi(z, 1); return 16;')
f.append('  case 0xA9: z_cpi(z, -1); return 16;')
f.append('  case 0xB1: z_cpi(z, 1); if (BC && !(F & ZF)) { PC -= 2; WZ = (uint16_t)(PC + 1); return 21; } return 16;')
f.append('  case 0xB9: z_cpi(z, -1); if (BC && !(F & ZF)) { PC -= 2; WZ = (uint16_t)(PC + 1); return 21; } return 16;')
f.append('  case 0xA2: z_ini(z, 1); return 16;')
f.append('  case 0xAA: z_ini(z, -1); return 16;')
f.append('  case 0xB2: z_ini(z, 1); if (B) { PC -= 2; return 21; } return 16;')
f.append('  case 0xBA: z_ini(z, -1); if (B) { PC -= 2; return 21; } return 16;')
f.append('  case 0xA3: z_outi(z, 1); return 16;')
f.append('  case 0xAB: z_outi(z, -1); return 16;')
f.append('  case 0xB3: z_outi(z, 1); if (B) { PC -= 2; return 21; } return 16;')
f.append('  case 0xBB: z_outi(z, -1); if (B) { PC -= 2; return 21; } return 16;')
f += ['  }', '  return 8;', '}', '']

# ---- DD / FD ----
XR = ['B', 'C', 'D', 'E', 'XH', 'XL', None, 'A']
f += ['#define XY (*xy)', '#define XH (((uint8_t *)xy)[1])', '#define XL (((uint8_t *)xy)[0])',
      '#define XA() (WZ = (uint16_t)(XY + (int8_t)IMM8()))',
      'static int z80_xy(Z80 *z, uint16_t *xy, uint8_t op)', '{', '  switch (op) {']
for i, rp in enumerate(['BC', 'DE', 'XY', 'SP']):
    f.append(f'  case 0x{i << 4 | 9:02X}: XY = z_add16(z, XY, {rp}); return 15;')
f.append('  case 0x21: XY = z_imm16(z); return 14;')
f.append('  case 0x22: { uint16_t a = z_imm16(z); WR16(a, XY); WZ = (uint16_t)(a + 1); } return 20;')
f.append('  case 0x2A: { uint16_t a = z_imm16(z); XY = RD16(a); WZ = (uint16_t)(a + 1); } return 20;')
f.append('  case 0x23: XY++; return 10;')
f.append('  case 0x2B: XY--; return 10;')
f.append('  case 0x24: XH = z_inc(z, XH); return 8;')
f.append('  case 0x25: XH = z_dec(z, XH); return 8;')
f.append('  case 0x26: XH = IMM8(); return 11;')
f.append('  case 0x2C: XL = z_inc(z, XL); return 8;')
f.append('  case 0x2D: XL = z_dec(z, XL); return 8;')
f.append('  case 0x2E: XL = IMM8(); return 11;')
f.append('  case 0x34: { uint16_t a = XA(); uint8_t v = RD(a); WR(a, z_inc(z, v)); } return 23;')
f.append('  case 0x35: { uint16_t a = XA(); uint8_t v = RD(a); WR(a, z_dec(z, v)); } return 23;')
f.append('  case 0x36: { uint16_t a = XA(); uint8_t v = IMM8(); WR(a, v); } return 19;')
for d in range(8):
    for s in range(8):
        op = 0x40 | d << 3 | s
        if op == 0x76: continue
        if s == 6: f.append(f'  case 0x{op:02X}: {R[d]} = RD(XA()); return 19;')
        elif d == 6: f.append(f'  case 0x{op:02X}: WR(XA(), {R[s]}); return 19;')
        elif d in (4, 5) or s in (4, 5):
            f.append(f'  case 0x{op:02X}: {XR[d]} = {XR[s]}; return 8;')
for k in range(8):
    for s in (4, 5, 6):
        op = 0x80 | k << 3 | s
        if s == 6: f.append(f'  case 0x{op:02X}: {ALU[k]}(z, RD(XA())); return 19;')
        else: f.append(f'  case 0x{op:02X}: {ALU[k]}(z, {XR[s]}); return 8;')
f.append('  case 0xE1: XY = z_pop(z); return 14;')
f.append('  case 0xE5: z_push(z, XY); return 15;')
f.append('  case 0xE3: { uint16_t v = RD16(SP); WR16(SP, XY); XY = v; WZ = v; } return 23;')
f.append('  case 0xE9: PC = XY; return 8;')
f.append('  case 0xF9: SP = XY; return 10;')
f.append('''  case 0xCB: {
    uint16_t a = XA(); uint8_t c = IMM8(), v = RD(a), g = c >> 6, k = (c >> 3) & 7, r;
    if (g == 1) { z_bitm(z, k, v, (uint8_t)(a >> 8)); return 20; }
    r = g == 0 ? z_rot(z, k, v) : g == 2 ? (uint8_t)(v & ~(1u << k)) : (uint8_t)(v | 1u << k);
    WR(a, r);
    switch (c & 7) { case 0: B = r; break; case 1: C = r; break; case 2: D = r; break; case 3: E = r; break;
      case 4: H = r; break; case 5: L = r; break; case 7: A = r; break; default: break; }
    return 23; }''')
f += ['  }', '  return 0;', '}', '#undef XY', '#undef XH', '#undef XL', '#undef XA', '']
open(os.path.join(OUTD, 'z80_fns.inc'), 'w').write('\n'.join(f) + '\n')
