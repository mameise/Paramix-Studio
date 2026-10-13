#!/usr/bin/env python3
# Partial WAD for the Paradise: only the chosen episodes, only used textures and flats, no digital sound and no music.
# wadsub.py <DOOM.WAD> <output.wad> <episodes e.g. 1 or 12> [musik]
import struct, sys, re
src, dst, eps = sys.argv[1], sys.argv[2], sys.argv[3]
MUSIK = len(sys.argv) > 4 and sys.argv[4] == 'musik'   # v0.2.5: keep the episode music (D_ lumps, MUS)
d = open(src, 'rb').read()
ident, n, off = struct.unpack_from('<4sII', d, 0)
L = []
for i in range(n):
    o, s, nm = struct.unpack_from('<II8s', d, off + 16 * i)
    L.append([nm.rstrip(b'\0').decode('latin1'), d[o:o + s]])
MAPL = ('THINGS', 'LINEDEFS', 'SIDEDEFS', 'VERTEXES', 'SEGS', 'SSECTORS', 'NODES', 'SECTORS', 'REJECT', 'BLOCKMAP')
out = []; i = 0; usedtex = set(); usedflat = set(); usedthings = set()
while i < len(L):
    nm, data = L[i]
    m = re.match(r'E(\d)M(\d)$', nm)
    if m:
        keep = m.group(1) in eps
        j = i + 1
        while j < len(L) and L[j][0] in MAPL: j += 1
        if keep:
            out += L[i:j]
            for nm2, dd in L[i:j]:
                if nm2 == 'SIDEDEFS':
                    for k in range(0, len(dd), 30):
                        for t in (dd[k+4:k+12], dd[k+12:k+20], dd[k+20:k+28]): usedtex.add(t.rstrip(b'\0').decode('latin1').upper())
                if nm2 == 'THINGS':
                    for k in range(0, len(dd), 10): usedthings.add(struct.unpack_from('<H', dd, k + 6)[0])
                if nm2 == 'SECTORS':
                    for k in range(0, len(dd), 26):
                        for t in (dd[k+4:k+12], dd[k+12:k+20]): usedflat.add(t.rstrip(b'\0').decode('latin1').upper())
        i = j; continue
    if nm.startswith('D_') and MUSIK and (re.match(r'D_E[%s]M\d$' % eps, nm) or nm in ('D_INTER', 'D_INTRO', 'D_INTROA', 'D_VICTOR')):
        out.append(L[i]); i += 1; continue
    if nm.startswith('DS') or nm.startswith('D_') or nm in ('ENDOOM', 'GENMIDI', 'DMXGUS') or re.match(r'DEMO\d$', nm):
        i += 1; continue
    out.append(L[i]); i += 1
# Switches, animations, sky: keep whole sequences if any part is used
anims = [('NUKAGE3','NUKAGE1'),('FWATER4','FWATER1'),('SWATER4','SWATER1'),('LAVA4','LAVA1'),('BLOOD3','BLOOD1'),('RROCK08','RROCK05'),('SLIME04','SLIME01'),('SLIME08','SLIME05'),('SLIME12','SLIME09')]
tanims = [('BLODGR4','BLODGR1'),('SLADRIP3','SLADRIP1'),('BLODRIP4','BLODRIP1'),('FIREWALL','FIREWALA'),('GSTFONT3','GSTFONT1'),('FIRELAVA','FIRELAV3'),('FIREMAG3','FIREMAG1'),('FIREBLU2','FIREBLU1'),('ROCKRED3','ROCKRED1'),('BFALL4','BFALL1'),('SFALL4','SFALL1'),('WFALL4','WFALL1'),('DBRAIN4','DBRAIN1')]
for e in eps: usedtex.add('SKY' + e)
# Switch pairs
sw = set()
for t in list(usedtex):
    if t.startswith('SW1'): sw.add('SW2' + t[3:])
    if t.startswith('SW2'): sw.add('SW1' + t[3:])
usedtex |= sw
names = [x[0] for x in out]
fs, fe = names.index('F_START'), names.index('F_END')
flats = names[fs+1:fe]
for a, b in anims:
    if a in flats and b in flats:
        ia, ib = flats.index(b), flats.index(a)
        rng = flats[ia:ib+1]
        if any(x in usedflat for x in rng): usedflat |= set(rng)
# Monsters that appear in none of the chosen maps: drop their sprites
MON = {3004: ['POSS'], 9: ['SPOS'], 3001: ['TROO', 'BAL1'], 3002: ['SARG'], 58: ['SARG'], 3003: ['BOSS', 'BAL7'],
       3005: ['HEAD', 'BAL2'], 3006: ['SKUL'], 16: ['CYBR'], 7: ['SPID']}
need = set(); allmon = set()
for t, sp in MON.items():
    allmon |= set(sp)
    if t in usedthings: need |= set(sp)
drop = allmon - need
ss, se = names.index('S_START'), names.index('S_END')
out = [x for j, x in enumerate(out) if not (ss < j < se and x[0][:4] in drop)]
names = [x[0] for x in out]; fs, fe = names.index('F_START'), names.index('F_END')
print('without enemy sprites:', ' '.join(sorted(drop)))
# Filter textures
def readtex(dd):
    cnt = struct.unpack_from('<I', dd, 0)[0]; offs = struct.unpack_from('<%dI' % cnt, dd, 4); res = []
    for o in offs:
        nm = dd[o:o+8].rstrip(b'\0').decode('latin1').upper(); pc = struct.unpack_from('<H', dd, o + 20)[0]
        res.append((nm, dd[o:o + 22 + 10 * pc]))
    return res
alltex = []
for k in ('TEXTURE1', 'TEXTURE2'):
    if k in names: alltex += [t[0] for t in readtex(out[names.index(k)][1])]
for a, b in tanims:
    if a in alltex and b in alltex:
        ia, ib = alltex.index(b), alltex.index(a)
        rng = alltex[ia:ib+1]
        if any(x in usedtex for x in rng): usedtex |= set(rng)
for k in ('TEXTURE1', 'TEXTURE2'):
    if k not in names: continue
    idx = names.index(k)
    tl = readtex(out[idx][1])
    # the first texture (AASHITTY/AASTINKY) stays, it stands for "no texture"
    keep = [t for j, t in enumerate(tl) if (k == 'TEXTURE1' and j == 0) or t[0] in usedtex]
    hdr = struct.pack('<I', len(keep)); pos = 4 + 4 * len(keep); body = b''; offs = []
    for nm, raw in keep: offs.append(pos + len(body)); body += raw
    out[idx][1] = hdr + struct.pack('<%dI' % len(keep), *offs) + body
# Filter flats
out = [x for j, x in enumerate(out) if not (fs < j < fe and x[0] not in usedflat and not x[0].endswith('_START') and not x[0].endswith('_END'))]
# Full screen images: drop those for other episodes, shrink the rest to 128x128
FULL_DROP = {'1': [], '2': [], '3': []}
dropfull = set(['WIMAP0', 'WIMAP1', 'WIMAP2', 'VICTORY2', 'PFUB1', 'PFUB2'])
for e in eps:
    dropfull.discard('WIMAP%d' % (int(e) - 1))
    if e == '2': dropfull.discard('VICTORY2')
    if e == '3': dropfull -= {'PFUB1', 'PFUB2'}
out = [x for x in out if x[0] not in dropfull]
names = [x[0] for x in out]
pal = out[names.index('PLAYPAL')][1][:768]
def decode(dd):
    w, h, lo, to = struct.unpack_from('<hhhh', dd, 0)
    img = bytearray(w * h); mask = bytearray(w * h)
    for x in range(w):
        p = struct.unpack_from('<I', dd, 8 + 4 * x)[0]
        while dd[p] != 0xFF:
            top, ln = dd[p], dd[p + 1]
            for k in range(ln):
                y = top + k
                if y < h: img[y * w + x] = dd[p + 3 + k]; mask[y * w + x] = 1
            p += ln + 4
    return w, h, img, mask
PW, PH = int(__import__('os').environ.get('PDW', '128')), int(__import__('os').environ.get('PDH', '128'))
NEAR = {}
def nearest(r, g, b):
    key = (r << 16) | (g << 8) | b
    v = NEAR.get(key)
    if v is None:
        best = 1 << 30; v = 0
        for i in range(256):
            dr = pal[3 * i] - r; dg = pal[3 * i + 1] - g; db = pal[3 * i + 2] - b
            e = dr * dr + dg * dg + db * db
            if e < best: best = e; v = i
        NEAR[key] = v
    return v
def shrink(img, w, h):
    # v0.3.1: average per target cell, then nearest palette color. Same as in the studio (doom.js), byte for byte.
    res = bytearray(PW * PH)
    for ty in range(PH):
        y0 = ty * h // PH; y1 = max(y0 + 1, (ty + 1) * h // PH)
        for tx in range(PW):
            x0 = tx * w // PW; x1 = max(x0 + 1, (tx + 1) * w // PW)
            sr = sg = sb = 0; n = 0
            for y in range(y0, y1):
                for x in range(x0, x1):
                    c = img[y * w + x]; sr += pal[3 * c]; sg += pal[3 * c + 1]; sb += pal[3 * c + 2]; n += 1
            res[ty * PW + tx] = nearest((sr + n // 2) // n, (sg + n // 2) // n, (sb + n // 2) // n)
    return bytes(res)
def encode(img, w, h):
    cols = []
    for x in range(w):
        col = bytes([0, h, 0]) + bytes(img[y * w + x] for y in range(h)) + bytes([0, 0xFF])
        cols.append(col)
    hdr = struct.pack('<hhhh', w, h, 0, 0); offs = []; pos = 8 + 4 * w; body = b''
    for c in cols: offs.append(pos + len(body)); body += c
    return hdr + struct.pack('<%dI' % w, *offs) + body
for k, (nm, dd) in enumerate(out):
    if nm in ('TITLEPIC', 'HELP1', 'HELP2', 'CREDIT', 'VICTORY2', 'PFUB1', 'PFUB2', 'INTERPIC') or nm.startswith('WIMAP') or (nm.startswith('END') and len(nm) == 4):
        try:
            w, h, img, mask = decode(dd)
        except Exception:
            continue
        if (w, h) != (320, 200): continue
        out[k][1] = encode(shrink(img, w, h), PW, PH)
# write
body = b''; dirs = []
for nm, data in out:
    dirs.append((12 + len(body), len(data), nm)); body += data
hdr = struct.pack('<4sII', b'IWAD', len(out), 12 + len(body))
dd = b''.join(struct.pack('<II8s', o, s, nm.encode('latin1')) for o, s, nm in dirs)
open(dst, 'wb').write(hdr + body + dd)
print(dst, len(hdr + body + dd), 'Lumps', len(out), 'Texturen', len(usedtex), 'Boeden', len(usedflat))
