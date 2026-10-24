# test image: v3.bin at 0xA0000 with its settings block, the ROM at 0x200000, the saves at 0x300000 (16 KB)
import sys, struct
app, rom, out = sys.argv[1], sys.argv[2], sys.argv[3]
fl = bytearray(b'\xff' * 0x400000)
a = open(app, 'rb').read(); r = open(rom, 'rb').read()
fl[0xA0000:0xA0000 + len(a)] = a; fl[0x200000:0x200000 + len(r)] = r
struct.pack_into('<6I16s', fl, 0xA0000 + 0x140, 0x31584D50, 1, 0x200000, len(r), 0x300000, int(sys.argv[4]) if len(sys.argv) > 4 else 0, b'TAMA V3')
open(out, 'wb').write(fl)
