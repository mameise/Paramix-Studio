#!/usr/bin/env python3
"""Autostart in the home screen stub, run in a Thumb emulator (unicorn).
python3 test/stub_auto_test.py   (needs: pip install unicorn)"""
import struct, os
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_HOOK_CODE
from unicorn.arm_const import UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_PC
here = os.path.dirname(os.path.abspath(__file__))
BASE, ORIG, DONE = 0x1009F000, 0x10100000, 0x10200000
bad = 0
def ok(c, m):
    global bad
    print(('ok   ' if c else 'WRONG ') + m); bad += 0 if c else 1
def stub(tpl, auto):
    d = bytearray(open(tpl, 'rb').read())
    for o in range(0, len(d) - 3, 2):
        w = struct.unpack_from('<I', d, o)[0]
        if w in (0x22222221, 0x33333331, 0x44444441, 0x55555551): struct.pack_into('<I', d, o, ORIG | 1)
        elif w == 0x66666661: struct.pack_into('<I', d, o, auto)
        elif w == 0x6789EF01: struct.pack_into('<I', d, o, 0x100A0001)
    return bytes(d)
def run(code, gpio, marker, secs=5, shpr=0):
    u = Uc(UC_ARCH_ARM, UC_MODE_THUMB)
    for a, n in ((0x10000000, 0x400000), (0x20000000, 0x1000), (0x40018000, 0x1000), (0x18000000, 0x10000), (0x45000000, 0x1000), (0xE000E000, 0x1000)):
        u.mem_map(a, n)
    u.mem_write(BASE, code)
    u.mem_write(0x40018000, struct.pack('<I', gpio))
    u.mem_write(0x20000FFC, struct.pack('<I', marker))
    u.mem_write(0x45000304, struct.pack('<I', secs))
    u.mem_write(0xE000ED1A, bytes([shpr]))
    u.reg_write(UC_ARM_REG_SP, 0x18008000); u.reg_write(UC_ARM_REG_LR, DONE | 1)
    where = []
    def hk(uc, addr, size, _):
        if addr in (BASE, ORIG, DONE): where.append(addr); uc.emu_stop()
    u.hook_add(UC_HOOK_CODE, hk)
    u.emu_start((BASE + 0x40) | 1, 0, count=400)
    return (where[0] if where else None), struct.unpack('<I', u.mem_read(0x20000FFC, 4))[0], u.reg_read(UC_ARM_REG_SP), u.mem_read(0xE000ED1A, 1)[0]
L, R, IDLE, BHELD = 0x4C584D50, 0x52584D50, 0xFFFFFFFF, 0xFFFFFFFF & ~(1 << 11)
for name, tpl in (('emu', '../emu/stub_tpl.bin'), ('menu', '../feld/stub_feld_tpl.bin')):
    t = os.path.join(here, tpl)
    w, m, sp, f = run(stub(t, 1), IDLE, 0x12345678, secs=5)
    ok(w == BASE and m == R and f, name + ': power-on (clock new): emulator')
    w, m, sp, f = run(stub(t, 1), IDLE, R, secs=5, shpr=0xE0)
    ok(w == ORIG and m == R and sp == 0x18008000, name + ': later calls in the same boot: original handler')
    w, m, sp, f = run(stub(t, 1), IDLE, L, secs=9999)
    ok(w == ORIG and m == R and sp == 0x18008000, name + ': back from the emulator: Paradise')
    w, m, sp, f = run(stub(t, 1), IDLE, R, secs=9999)
    ok(w == BASE and m == R, name + ': reset button (marker kept): emulator')
    w, m, sp, f = run(stub(t, 1), IDLE, 0x00000000, secs=9999)
    ok(w == ORIG and m == R and sp == 0x18008000, name + ': waking from deep sleep (marker lost, clock ran): Paradise')
    w, m, sp, f = run(stub(t, 1), BHELD, 0x12345678, secs=5)
    ok(w == ORIG and m == R, name + ': B held at power-on: Paradise')
    w, m, sp, f = run(stub(t, 0), IDLE, 0x1234, secs=5)
    ok(w == ORIG and m == 0x1234 and f == 0, name + ': switch off: nothing changes')
    w, m, sp, f = run(stub(t, 0), IDLE & ~0x600, 0, secs=5)
    ok(w == ORIG, name + ': A+C held: counts as before (original handler until the limit)')
print('all good' if not bad else '%d WRONG' % bad)
