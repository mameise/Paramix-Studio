#!/usr/bin/env python3
# Generates the screen dependent tables (like R_InitTextureMapping) for a width W and view height VH.
import re, sys
W = int(sys.argv[1]); H = int(sys.argv[2]); VH = int(sys.argv[3])
src = open(sys.argv[4]).read()
def arr(name):
    m = re.search(r'const fixed_t ' + name + r'\[\d+\] =\s*\{(.*?)\};', src, re.S)
    return [int(x) for x in re.findall(r'-?\d+', m.group(1))]
finetangent = arr('finetangent'); finesine = arr('finesine')
FRACBITS = 16; FRACUNIT = 1 << 16; FINEANGLES = 8192; FIELDOFVIEW = 2048; ANGLETOFINESHIFT = 19; ANG90 = 0x40000000
def s32(x): x &= 0xFFFFFFFF; return x - (1 << 32) if x & 0x80000000 else x
def FixedMul(a, b): return s32((a * b) >> 16)
def FixedDiv(a, b):
    if (abs(a) >> 14) >= abs(b): return -0x80000000 if (a ^ b) < 0 else 0x7FFFFFFF
    q = abs(a << 16) // abs(b)
    return s32(q if (a < 0) == (b < 0) else -q)
centerx = W // 2; centerxfrac = centerx << FRACBITS
focallength = FixedDiv(centerxfrac, finetangent[FINEANGLES // 4 + FIELDOFVIEW // 2])
v2x = []
for i in range(FINEANGLES // 2):
    if finetangent[i] > FRACUNIT * 2: t = -1
    elif finetangent[i] < -FRACUNIT * 2: t = W + 1
    else:
        t = FixedMul(finetangent[i], focallength)
        t = (centerxfrac - t + FRACUNIT - 1) >> FRACBITS
        t = max(-1, min(W + 1, t))
    v2x.append(t)
x2v = []
for x in range(W + 1):
    i = 0
    while v2x[i] > x: i += 1
    x2v.append(s32((i << ANGLETOFINESHIFT) - ANG90))
v2x = [0 if t == -1 else (W if t == W + 1 else t) for t in v2x]
CHEIGHT = W * 3 // 4
projectiony = ((CHEIGHT * (W // 2) * 320) // 200) * FRACUNIT // W
ys = []
for i in range(H):
    if i < VH:
        dy = abs(((i - VH // 2) << FRACBITS) + FRACUNIT // 2)
        ys.append(FixedDiv(projectiony, dy))
    else: ys.append(0)
ds = []
for i in range(W):
    cosadj = abs(finesine[(x2v[i] & 0xFFFFFFFF) >> ANGLETOFINESHIFT if False else ((x2v[i] & 0xFFFFFFFF) >> ANGLETOFINESHIFT) + FINEANGLES // 4])
    ds.append(FixedDiv(FRACUNIT, cosadj))
def out(name, vals): return '#elif ' + name + '\n{ ' + ', '.join(str(v) for v in vals) + ' };\n'
print('/* viewangletox */'); print(out('SCREENWIDTH == %d' % W, v2x))
print('/* xtoviewangle */'); print(out('SCREENWIDTH == %d' % W, x2v))
print('/* yslope */'); print(out('SCREENHEIGHT == %d' % H, ys))
print('/* distscale */'); print(out('SCREENWIDTH == %d' % W, ds))
print('/* clipangle', x2v[0], '*/')
