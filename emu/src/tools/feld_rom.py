#!/usr/bin/env python3
"""Install the field switch into the Paradise ROM (Land/Water/Sky, firmware v1).
feld_rom.py <rom_in> <rom_out> [--app app.bin] [--feld feld.bin]
Holding B+C on the home screen starts the field switch, A+C (if --app)
starts the program as before. The three save slots sit after the characters."""
import sys, struct, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from patch_rom import scene_table, xip, APP_OFF, STUB_OFF
import subprocess, tempfile

FELD_OFF = 0xB1000
SLOT_SIZE = 0x58000
FELD_MAGIC = 0x444C4546

def build_stub(app_src, feld_src, origs, limit=90):
    here = os.path.dirname(os.path.abspath(__file__))
    S = os.path.join(here, '..', 'stub', 'stub.S')
    t = tempfile.mkdtemp()
    defs = ['-DAPP_SRC=%d' % app_src, '-DFELD=1', '-DFELD_SRC=%d' % feld_src, '-DHOOKS=1', '-DLIMIT=%d' % limit]
    defs += ['-DORIG%d=%d' % (i, v) for i, v in enumerate(origs)]
    if os.environ.get('HOOK_TEST'): defs.append('-DHOOK_TEST=1')
    subprocess.check_call(['clang', '--target=thumbv7em-none-eabi', '-mcpu=cortex-m4', '-x', 'assembler-with-cpp', '-c', S, '-o', t + '/s.o'] + defs)
    subprocess.check_call(['ld.lld', '--no-rosegment', '-Ttext=%#x' % xip(STUB_OFF), '-e', 'start', t + '/s.o', '-o', t + '/s.elf'])
    subprocess.check_call(['llvm-objcopy', '-O', 'binary', t + '/s.elf', t + '/s.bin'])
    return open(t + '/s.bin', 'rb').read()

def slot_off(d):
    asset_end = (0x111000 + 16 + struct.unpack_from('<I', d, 0x111000 + 8)[0] + 0xFFF) & ~0xFFF
    return max(0x830000, asset_end)

def main():
    rin, rout = sys.argv[1:3]
    d = bytearray(open(rin, 'rb').read())
    assert len(d) == 0x1000000
    fid = struct.unpack_from('<I', d, 0xD49000)[0]
    assert fid in (1, 2, 3), 'nur Land, Water oder Sky (Kennung %d)' % fid
    here = os.path.dirname(os.path.abspath(__file__))
    fb = sys.argv[sys.argv.index('--feld') + 1] if '--feld' in sys.argv else os.path.join(here, '..', 'feld', 'feld.bin')
    f = bytearray(open(fb, 'rb').read())
    i = f.find(struct.pack('<I', FELD_MAGIC))
    assert i >= 0
    so = slot_off(d)
    assert so + 3 * SLOT_SIZE <= 0x940000, 'no room for the slots'
    struct.pack_into('<II', f, i + 8, so, SLOT_SIZE)
    assert len(f) <= 0xC0000 - FELD_OFF
    tab = scene_table(bytes(d))
    o = tab['PSID_HOME']
    origs = list(struct.unpack_from('<4I', d, o + 12))
    if any(xip(STUB_OFF) <= v < xip(STUB_OFF) + 0x1000 for v in origs):
        raise SystemExit('ROM ist schon gepatcht')
    app_src = 0
    if '--app' in sys.argv:
        a = open(sys.argv[sys.argv.index('--app') + 1], 'rb').read()
        assert len(a) <= 0x10000
        d[APP_OFF:APP_OFF + 0x10000] = a + b'\x00' * (0x10000 - len(a))
        app_src = xip(APP_OFF)
    st = build_stub(app_src, xip(FELD_OFF), origs)
    d[STUB_OFF:STUB_OFF + 0x1000] = st + b'\xff' * (0x1000 - len(st))
    d[FELD_OFF:FELD_OFF + len(f)] = f
    for k in range(4):
        struct.pack_into('<I', d, o + 12 + 4 * k, (xip(STUB_OFF) + 0x40 + 6 * k) | 1)
    open(rout, 'wb').write(d)
    print('field switch added: slots from %#x, %#x each' % (so, SLOT_SIZE))

if __name__ == '__main__': main()
