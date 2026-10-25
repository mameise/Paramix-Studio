# test image: wolf.bin at 0xA0000 with its settings block, the pack at 0x200000, the save at 0x380000 (64 KB)
import sys, struct
app, pack, out = sys.argv[1], sys.argv[2], sys.argv[3]
fl = bytearray(b'\xff' * 0x1000000)
a = open(app, 'rb').read(); p = open(pack, 'rb').read()
fl[0xA0000:0xA0000 + len(a)] = a; fl[0x200000:0x200000 + len(p)] = p
c = 0xA0000 + 0x140
assert struct.unpack_from('<I', fl, c)[0] == 0x57584D50
struct.pack_into('<IIII', fl, c + 8, 0x200000, len(p), 0x380000, int(sys.argv[4]) if len(sys.argv) > 4 else 0)
struct.pack_into('<I', fl, c + 48, 0xA0000)   # the program's own place (the rest of it is copied from there)
open(out, 'wb').write(fl)
