# test image: sms.bin at 0xA0000 with its settings block, the ROM at 0x200000, the saves at 0x300000 (64 KB)
import sys, struct
app, rom, out = sys.argv[1], sys.argv[2], sys.argv[3]
fl = bytearray(b'\xff' * 0x400000)
a = open(app, 'rb').read(); r = open(rom, 'rb').read()
fl[0xA0000:0xA0000 + len(a)] = a; fl[0x200000:0x200000 + len(r)] = r
struct.pack_into('<6I16s', fl, 0xA0000 + 0x140, 0x53584D50, 1, 0x200000, len(r), 0x300000, int(sys.argv[4]) if len(sys.argv) > 4 else 2, b'MASTER SYSTEM')
open(out, 'wb').write(fl)
