#!/usr/bin/env python3
"""Replacement for gbadoom.wad, built only from your own DOOM.WAD (no third-party graphics).
gbaextras.py <DOOM.WAD or partial WAD> <output.wad>
Lumps: CREDITS (empty), M_ARUN, M_GAMMA (from STCFN letters), STGANUM0..9 (narrower STTNUM),
PLAYPAL1..5 (copies, the core computes gamma itself), STBARGBA (240x32 status bar, raw, from STBAR/STARMS/STTPRCNT)."""
import struct, sys

def read_wad(path):
    d = open(path, 'rb').read(); n, o = struct.unpack('<II', d[4:12]); L = {}
    for k in range(n):
        p, s, nm = struct.unpack('<II8s', d[o + 16 * k:o + 16 * k + 16]); L[nm.rstrip(b'\0').decode('latin1')] = d[p:p + s]
    return L

def patch_read(b):
    w, h, lo, to = struct.unpack('<hhhh', b[:8]); px = [[None] * h for _ in range(w)]
    for x in range(w):
        c = struct.unpack('<I', b[8 + 4 * x:12 + 4 * x])[0]
        top = -1
        while b[c] != 255:
            y0 = b[c]; ln = b[c + 1]
            for t in range(ln):
                if y0 + t < h: px[x][y0 + t] = b[c + 3 + t]
            c += ln + 4
    return w, h, lo, to, px

def patch_write(w, h, lo, to, px):
    cols = []
    for x in range(w):
        col = bytearray(); y = 0
        while y < h:
            if px[x][y] is None: y += 1; continue
            y0 = y; run = []
            while y < h and px[x][y] is not None and len(run) < 128: run.append(px[x][y]); y += 1
            col += bytes([y0, len(run), run[0]]) + bytes(run) + bytes([run[-1]])
        col += b'\xff'; cols.append(bytes(col))
    head = struct.pack('<hhhh', w, h, lo, to); off = 8 + 4 * w; offs = []
    for c in cols: offs.append(off); off += len(c)
    return head + b''.join(struct.pack('<I', o) for o in offs) + b''.join(cols)

def squeeze(px, w, h, nw):
    return [px[min(w - 1, (x * w + w // 2) // nw)] for x in range(nw)] if nw != w else px

def text_patch(L, txt, scale=2):
    glyphs = []
    for ch in txt:
        if ch == ' ': glyphs.append(None); continue
        glyphs.append(patch_read(L['STCFN%03d' % ord(ch)]))
    H = max(g[1] for g in glyphs if g) * scale; cols = []
    for g in glyphs:
        if g is None: cols += [[None] * H] * (4 * scale); continue
        w, h, lo, to, px = g
        for x in range(w):
            col = [None] * H
            for y in range(h):
                v = px[x][y]
                if v is not None:
                    for sy in range(scale): col[(H - h * scale) + y * scale + sy] = v
            cols += [col] * scale
        cols += [[None] * H] * scale
    return patch_write(len(cols), H, 0, 0, cols)

# Status bar 240 wide: sections of the PC bar (320) squeezed onto the GBA layout
SEGS = [(0, 48, 0, 34), (48, 104, 34, 78), (104, 143, 78, 107), (143, 178, 107, 135),
        (178, 236, 135, 177), (236, 249, 177, 187), (249, 320, 187, 240)]
def stbar_gba(L):
    W, H, _, _, bar = patch_read(L['STBAR'])
    aw, ah, _, _, arms = patch_read(L['STARMS'])
    for x in range(aw):                       # weapon panel (like the console) instead of FRAG
        for y in range(ah):
            if arms[x][y] is not None and 104 + x < W: bar[104 + x][y] = arms[x][y]
    out = [[0] * 32 for _ in range(240)]
    for s0, s1, d0, d1 in SEGS:
        for x in range(d0, d1):
            sx = s0 + ((x - d0) * (s1 - s0) + (s1 - s0) // 2) // (d1 - d0)
            for y in range(32): v = bar[min(W - 1, sx)][y]; out[x][y] = v if v is not None else 0
    pw, ph, _, _, pc = patch_read(L['STTPRCNT'])
    pc = squeeze(pc, pw, ph, 10)
    for px0 in (68, 168):                     # percent signs baked into the bar (the core does not draw them)
        for x in range(10):
            for y in range(ph):
                v = pc[x][y]
                if v is not None and 0 <= px0 + x < 240: out[px0 + x][6 + y] = v
    return bytes(out[x][y] for y in range(32) for x in range(240))

def main():
    L = read_wad(sys.argv[1])
    lumps = [('CREDITS', patch_write(1, 1, 0, 0, [[None]])),
             ('M_ARUN', text_patch(L, 'ALWAYS RUN')), ('M_GAMMA', text_patch(L, 'GAMMA'))]
    for i in range(10):
        w, h, lo, to, px = patch_read(L['STTNUM%d' % i])
        lumps.append(('STGANUM%d' % i, patch_write(10, h, lo, to, squeeze(px, w, h, 10))))
    for i in range(1, 6): lumps.append(('PLAYPAL%d' % i, L['PLAYPAL']))
    lumps.append(('STBARGBA', stbar_gba(L)))
    body = bytearray(); dirs = []; pos = 12
    for nm, data in lumps:
        dirs.append(struct.pack('<II8s', pos, len(data), nm.encode().ljust(8, b'\0'))); body += data; pos += len(data)
    open(sys.argv[2], 'wb').write(b'PWAD' + struct.pack('<II', len(lumps), pos) + bytes(body) + b''.join(dirs))
main()
