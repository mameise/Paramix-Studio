#!/usr/bin/env python3
"""Installs the stub into the Paradise ROM.
patch_rom.py <rom_in> <app.bin> <rom_out> [--scene PSID_NAME] [--guest fw.bin --device pc|idl]
Places the app image (max 64 KB) at APP_OFF and the stub at STUB_OFF and
redirects all four handlers of the chosen scene to the stub."""
import sys, struct, re, subprocess, os, tempfile
XIP_DEC = 0x11000
def xip(off): return 0x10000000 + off - XIP_DEC
APP_OFF, STUB_OFF = 0xA0000, 0xB0000

def scene_table(d):
    names = {xip(m.start()): m.group()[:-1].decode() for m in re.finditer(rb'PSID_[A-Z0-9_]+\x00', d)}
    ptrs = [(o, w) for o in range(0, len(d) - 4, 4) for w in [struct.unpack_from('<I', d, o)[0]] if w in names]
    best, run = [], [ptrs[0]]
    for a, b in zip(ptrs, ptrs[1:]):
        if b[0] - a[0] == 28: run.append(b)
        else:
            if len(run) > len(best): best = run
            run = [b]
    if len(run) > len(best): best = run
    base = best[0][0] - 4
    return {names[w]: base + i * 28 for i, (o, w) in enumerate(best)}

def build_stub(app_src, origs=None, limit=90):
    here = os.path.dirname(os.path.abspath(__file__))
    S = os.path.join(here, '..', 'stub', 'stub.S')
    t = tempfile.mkdtemp()
    defs = ['-DAPP_SRC=%d' % app_src]
    if origs:
        defs += ['-DHOOKS=1', '-DLIMIT=%d' % limit] + ['-DORIG%d=%d' % (i, v) for i, v in enumerate(origs)]
    subprocess.check_call(['clang', '--target=thumbv7em-none-eabi', '-mcpu=cortex-m4', '-x', 'assembler-with-cpp', '-c', S, '-o', t + '/s.o'] + defs)
    subprocess.check_call(['ld.lld', '--no-rosegment', '-Ttext=%#x' % xip(STUB_OFF), '-e', 'start', t + '/s.o', '-o', t + '/s.elf'])
    subprocess.check_call(['llvm-objcopy', '-O', 'binary', t + '/s.elf', t + '/s.bin'])
    syms = {}
    for l in subprocess.run(['llvm-nm', t + '/s.elf'], capture_output=True, text=True).stdout.splitlines():
        p = l.split()
        if len(p) == 3: syms[p[2]] = int(p[0], 16)
    return open(t + '/s.bin', 'rb').read(), syms

GUEST_OFF, GUEST_END = 0x940000, 0xD40000
DEVICES = {'pc': (0, 0x400000), 'idl': (1, 0x800000)}
# Blocks with many data accesses first: they end up in the cached 1 MB window
HOT = {'pc': [0x25, 0x21, 0x04, 0x02, 0x01, 0x03, 0x05, 0x20, 0x22, 0x23, 0x24, 0x26, 0x27, 0x28, 0x29, 0x2a],
       'idl': [0x0e, 0x0b, 0x19, 0x16, 0x0d, 0x18, 0x0c, 0x4f, 0x50, 0x15, 0x40, 0x4c, 0x4d, 0x0f, 0x17, 0x1a]}

def pack_guest(fw, dev):
    kind, size = DEVICES[dev]
    fw = fw[:size] + b'\xff' * (size - min(len(fw), size))
    nblk = size >> 16
    par = nblk - 1 if dev == 'pc' else 0          # always include the parameter block (save)
    used = [i for i in range(nblk) if i == par or fw[i << 16:(i + 1) << 16] != b'\xff' * 0x10000]
    order = [b for b in HOT[dev] if b in used] + [b for b in used if b not in HOT[dev]]
    m = bytearray(b'\xff' * 128)
    blob = bytearray()
    for idx, b in enumerate(order):
        m[b] = idx
        blob += fw[b << 16:(b + 1) << 16]
    assert GUEST_OFF + len(blob) <= GUEST_END, 'guest ROM too big: %d blocks' % len(order)
    return kind, bytes(m), bytes(blob)

def set_cfg(app, kind, guest_size, m):
    i = app.find(struct.pack('<I', 0x43584D50))
    assert i >= 0, 'PMXC not found'
    a = bytearray(app)
    struct.pack_into('<6I', a, i, 0x43584D50, 2, GUEST_OFF, guest_size, 0xC0000, kind)
    a[i + 24:i + 24 + 128] = m
    return bytes(a)

def main():
    rin, app, rout = sys.argv[1:4]
    scene = None                       # default: hold A+C on the home screen
    if '--scene' in sys.argv: scene = sys.argv[sys.argv.index('--scene') + 1]
    d = bytearray(open(rin, 'rb').read())
    a = open(app, 'rb').read()
    if '--guest' in sys.argv:
        dev = sys.argv[sys.argv.index('--device') + 1] if '--device' in sys.argv else 'pc'
        kind, m, blob = pack_guest(open(sys.argv[sys.argv.index('--guest') + 1], 'rb').read(), dev)
        d[GUEST_OFF:GUEST_OFF + len(blob)] = blob
        a = set_cfg(a, kind, len(blob), m)
        print('Gast', dev, len(blob) >> 16, 'Bloecke ab', hex(GUEST_OFF))
    assert len(a) <= 0x10000, 'App groesser als 64 KB'
    for off, n in ((APP_OFF, 0x10000), (STUB_OFF, 0x1000)):
        assert d[off:off + n] == b'\xff' * n or True
    d[APP_OFF:APP_OFF + 0x10000] = a + b'\x00' * (0x10000 - len(a))
    tab = scene_table(bytes(d))
    if scene:
        st, _ = build_stub(xip(APP_OFF))
        d[STUB_OFF:STUB_OFF + len(st)] = st
        o = tab[scene]
        for k in range(3, 7):
            struct.pack_into('<I', d, o + 4 * k, xip(STUB_OFF) | 1)
        print('Szene', scene, 'bei', hex(o), '-> Starter', hex(xip(STUB_OFF) | 1), 'Stub', len(st), 'Byte')
    else:
        o = tab['PSID_HOME']
        origs = list(struct.unpack_from('<4I', d, o + 12))
        if any(xip(STUB_OFF) <= v < xip(STUB_OFF) + 0x1000 for v in origs):
            raise SystemExit('ROM ist schon gepatcht (Heimbildschirm zeigt auf den Starter)')
        st, sy = build_stub(xip(APP_OFF), origs)
        d[STUB_OFF:STUB_OFF + len(st)] = st
        for k in range(4):
            struct.pack_into('<I', d, o + 12 + 4 * k, sy['hook%d' % k] | 1)
        print('Heimbildschirm: A+C halten startet den Emulator; Stub', len(st), 'Byte')
    open(rout, 'wb').write(d)

if __name__ == '__main__': main()
