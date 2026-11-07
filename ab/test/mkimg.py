# test image: ab.bin at 0xA0000 with its settings block, the game at 0x200000, the saves at 0x300000 (16 KB)
# python3 mkimg.py ab.bin game.hex out.bin [flags]
import sys, struct
app, hexf, out = sys.argv[1], sys.argv[2], sys.argv[3]
def hex2bin(fn):
    b = bytearray(b'\xff' * 0x8000); top = 0; hi = 0
    for l in open(fn):
        l = l.strip()
        if not l.startswith(':'): continue
        n, ad, ty = int(l[1:3], 16), int(l[3:7], 16), int(l[7:9], 16)
        if ty == 0:
            for i in range(n):
                a = hi + ad + i
                if a < len(b): b[a] = int(l[9 + 2 * i:11 + 2 * i], 16); top = max(top, a + 1)
        elif ty == 2: hi = int(l[9:13], 16) << 4
        elif ty == 4: hi = int(l[9:13], 16) << 16
    return bytes(b[:top + (top & 1)])
fl = bytearray(b'\xff' * 0x400000)
a = open(app, 'rb').read(); r = hex2bin(hexf)
fl[0xA0000:0xA0000 + len(a)] = a; fl[0x200000:0x200000 + len(r)] = r
flags = int(sys.argv[4]) if len(sys.argv) > 4 else 0
c = 0xA0000 + 0x140
struct.pack_into('<6I16s', fl, c, 0x50584241, 1, 0x200000, len(r), 0x300000, flags, b'ARDUBOY')
struct.pack_into('<12B', fl, c + 40, 4, 32, 8, 16, 1, 2, 0, 0, 0, 0, 0, 0)
open(out, 'wb').write(fl)
