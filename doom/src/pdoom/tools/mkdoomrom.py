#!/usr/bin/env python3
# Assemble the DOOM ROM: base ROM with stub (from mkrom1.js), program window part, WAD.
# mkdoomrom.py <rom_with_stub> <xip.bin> <wad> <output>
import sys, struct
rom = bytearray(open(sys.argv[1], 'rb').read())
xip = open(sys.argv[2], 'rb').read()
wad = open(sys.argv[3], 'rb').read()
CODE_OFF, SAVE_OFF = 0xB1000, 0x10E000
WAD_OFF, REGION_OFF, REGION_END = 0x830000, 0xCA0000, 0xD48000
assert len(rom) == 0x1000000
assert CODE_OFF + len(xip) <= SAVE_OFF, 'program part too big'
assert WAD_OFF + 4 + len(wad) <= REGION_OFF, 'WAD too big: %d bytes, room %d' % (len(wad), REGION_OFF - WAD_OFF - 4)
rom[CODE_OFF:SAVE_OFF] = b'\xff' * (SAVE_OFF - CODE_OFF)
rom[CODE_OFF:CODE_OFF + len(xip)] = xip
rom[SAVE_OFF:0x110000] = b'\xff' * (0x110000 - SAVE_OFF)
rom[WAD_OFF:REGION_END] = b'\xff' * (REGION_END - WAD_OFF)
rom[WAD_OFF:WAD_OFF + 4] = struct.pack('<I', len(wad))
rom[WAD_OFF + 4:WAD_OFF + 4 + len(wad)] = wad
open(sys.argv[4], 'wb').write(rom)
print(sys.argv[4], 'Programm', len(xip), 'WAD', len(wad), 'frei vor dem Bereich', REGION_OFF - WAD_OFF - 4 - len(wad))
