#!/usr/bin/env python3
"""Install download content (TAMAGO records from .jpg) into an 8 MB image.
dlc.py <image_in> <file.jpg> <image_out> [--fach N]   (games: slot 0 or 1)
dlc.py <image> --liste"""
import sys
GAMES = (0x7A0000, 0x8000, 2)
KINDS = [('Essen', 0x590000, 0x800, 20), ('Snacks', 0x5A0000, 0x800, 20), ('Kleidung', 0x600000, 0x4000, 5),
         ('Zubehoer', 0x650000, 0x2000, 10), ('Sachen', 0x6A0000, 0x2000, 10), ('Alltag', 0x740000, 0x8000, 5),
         ('Tapeten', 0x780000, 0x2000, 10), ('Spiele', 0x7A0000, 0x8000, 2), ('Reiseziele', 0x7B0000, 0x8000, 2)]

def payload(data):
    if data[:6] == b'TAMAGO': blob = data
    else:
        e = data.find(b'\xff\xd9'); m = data.find(b'TAMAGO', e + 2 if e >= 0 else 0)
        if m < 0: raise SystemExit('no TAMAGO record in the file')
        blob = data[m:]
    def ok(n): return n >= 3 and (sum(blob[:n - 2]) & 0xFFFF) == int.from_bytes(blob[n - 2:n], 'big')
    ln = int.from_bytes(blob[0x48:0x4C], 'big') if len(blob) >= 0x4C else 0
    if 0x60 <= ln <= len(blob) and ok(ln): return blob[:ln]
    for s in range(17):
        n = len(blob) - s
        if n >= max(0x60, ln) and ok(n): return blob[:n]
    raise SystemExit('record checksum does not match')

def info(rec):
    name = rec[6:0x34].decode('utf-16-be', 'replace').rstrip('\0').strip()
    return rec[0x34:0x48].split(b'\0')[0].decode('latin1'), name

def liste(img):
    for l, b, sz, n in KINDS:
        for s in range(n):
            o = b + s * sz
            if img[o:o + 6] == b'TAMAGO': print('%-10s Fach %d  %s  %s' % (l, s, *info(img[o:o + sz])))

def main():
    img = bytearray(open(sys.argv[1], 'rb').read()); img += b'\xff' * (0x800000 - len(img))
    if '--liste' in sys.argv: liste(img); return
    rec = payload(open(sys.argv[2], 'rb').read())
    if rec[0x4E] != 0x94 or rec[0x50] != 0x5B: raise SystemExit('not a game (type %02x cat %02x)' % (rec[0x4E], rec[0x50]))
    base, sz, n = GAMES
    fach = int(sys.argv[sys.argv.index('--fach') + 1]) if '--fach' in sys.argv else n - 1
    o = base + fach * sz
    print('ersetze', info(img[o:o + sz]) if img[o:o + 6] == b'TAMAGO' else 'leer', '->', info(rec))
    img[o:o + sz] = rec + b'\xff' * (sz - len(rec))
    open(sys.argv[3], 'wb').write(img)

if __name__ == '__main__': main()
