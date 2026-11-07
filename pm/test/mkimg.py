# test image: pm.bin at 0xA0000 with its settings block, the game at 0x200000, the saves at 0x300000 (64 KB)
# python3 mkimg.py pm.bin game.min out.bin [flags] [bios.min]
import sys, struct
app, rom, out = sys.argv[1], sys.argv[2], sys.argv[3]
fl = bytearray(b'\xff' * 0x400000)
a = open(app, 'rb').read(); r = open(rom, 'rb').read()
fl[0xA0000:0xA0000 + len(a)] = a; fl[0x200000:0x200000 + len(r)] = r
flags = int(sys.argv[4]) if len(sys.argv) > 4 else 0
bios = 0
if len(sys.argv) > 5: b = open(sys.argv[5], 'rb').read(); fl[0x310000:0x310000 + len(b)] = b; bios = 0x310000; flags |= 4
c = 0xA0000 + 0x140
struct.pack_into('<6I16s', fl, c, 0x50584D50, 1, 0x200000, len(r), 0x300000, flags, b'POKEMON MINI')
struct.pack_into('<12H', fl, c + 40, 0x020, 0x002, 0x040, 0x001, 0x008, 0x010, 0x004, 0x000, 0x100, 0, 0, 0)
struct.pack_into('<I', fl, c + 64, bios)
open(out, 'wb').write(fl)
