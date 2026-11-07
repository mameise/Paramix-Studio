import random, sys, struct
def gen(seed, lim=10**9):
    R = random.Random(seed); w = []
    # init registers r0..r31 via r16
    for r in list(range(0, 16)) + list(range(17, 32)):
        v = R.randrange(256); w.append(0xE000 | (v & 0xF0) << 4 | (0 << 4) | (v & 15))  # ldi r16, v
        w.append(0x2C00 | (r & 16) << 5 | (r & 15) << 4 | 0 | (16 & 16) << 5 | (16 & 15))  # mov r, r16 (placeholder fixed below)
    # fix movs: mov rd, r16: 0010 11rd dddd rrrr with r=16 -> r bit4 at bit9
    out = []
    for i in range(0, len(w), 2):
        out.append(w[i]); r = (list(range(0, 16)) + list(range(17, 32)))[i // 2]
        out.append(0x2C00 | 0x200 | ((r & 16) << 4) | ((r & 15) << 4) | 0)
    v = R.randrange(256); out.append(0xE000 | (v & 0xF0) << 4 | (v & 15))
    # SREG: random consistent (S = N^V), no I
    s = R.randrange(128) & 0x6F; s |= (((s >> 2) ^ (s >> 3)) & 1) << 4
    out.append(0xE010 | (s & 0xF0) << 4 | (s & 15))   # ldi r17, s
    out.append(0xBF1F)                                # out SREG, r17
    out.append(0xE0A0); out.append(0xE0B1)            # X = 0x100
    out.append(0xE0C8); out.append(0xE0D1)            # Y = 0x108
    out.append(0xE0E0 | 0); out.append(0xE0F1)        # Z = 0x100 (r30=0? ldi r30,0)
    body = []
    n = 120
    i = 0
    depth = 0
    while i < n:
        d = R.randrange(32); r = R.randrange(32); K = R.randrange(256); d4 = R.randrange(16); b = R.randrange(8)
        c = R.randrange(46)
        def dr(op): return op | (r & 16) << 5 | d << 4 | (r & 15)
        if c < 10: x = dr([0x0C00,0x1C00,0x1800,0x0800,0x2000,0x2800,0x2400,0x1400,0x0400,0x2C00][c])
        elif c < 15: x = [0x5000,0x4000,0x7000,0x6000,0x3000][c-10] | (K & 0xF0) << 4 | d4 << 4 | (K & 15)
        elif c < 23: x = 0x9400 | d << 4 | [0,1,2,3,5,6,7,10][c-15]
        elif c == 23: x = 0x9C00 | (r & 16) << 5 | d << 4 | (r & 15)
        elif c == 24: x = 0x0200 | d4 << 4 | R.randrange(16)
        elif c == 25: x = 0x0300 | R.randrange(256) & 0xFF
        elif c == 26: x = 0x9600 | R.randrange(256) & 0x3F | 0x00  # adiw r24
        elif c == 27: x = 0x9700 | R.randrange(256) & 0x3F
        elif c == 28: x = 0x0100 | R.randrange(256)
        elif c == 29: x = 0xFA00 | d << 4 | b
        elif c == 30: x = 0xF800 | d << 4 | b
        elif c == 31: x = 0x9408 | [0,1,5,6][R.randrange(4)] << 4 | R.randrange(2) << 7
        elif c in (32, 33): x = 0xF000 | R.randrange(2) << 10 | 1 << 3 | R.choice([0,1,2,3,4,6,7])   # branch .+1 (not on H)
        elif c == 34: x = 0xFC00 | R.randrange(2) << 9 | d << 4 | b
        elif c == 35: x = 0x1000 | (r & 16) << 5 | d << 4 | (r & 15)
        elif c == 36:   # ld/st through X, Y, Z (pointers kept in 0x100..0x13F)
            if d >= 26: d = 16
            x = [0x900C,0x900D,0x9009,0x9001,0x920C,0x920D,0x9209,0x9201,0x8000,0x8200][R.randrange(10)] | d << 4
        elif c == 37:  # ldd/std Y+q
            if d >= 26: d = 17
            q = R.randrange(8); x = (0x8008 | (R.randrange(2) << 9) | d << 4 | q)
        elif c == 38:
            if not depth: continue
            if d >= 26: d = 18
            x = 0x900F | d << 4; depth -= 1   # pop
        elif c == 39: x = 0x920F | d << 4; depth += 1   # push
        elif c == 40: x = 0x95C8            # lpm
        elif c == 41: x = 0x9004 | d << 4   # lpm rd, Z
        elif c == 42:  # reset pointers into range
            body += [0xE0A0 | R.randrange(16), 0xE0B1, 0xE0C0 | R.randrange(8), 0xE0D1, 0xE0E0 | R.randrange(16), 0xE0F1]; i += 6; continue
        elif c == 43: x = 0x0000   # (in rd, SREG: H differs on purpose)
        elif c == 44: x = 0x9000 | (d % 26) << 4; body += [0, x, 0x0100 + R.randrange(0x40)]; i += 3; continue   # lds
        else: x = 0x9200 | d << 4; body += [0, x, 0x0100 + R.randrange(0x40)]; i += 3; continue          # sts
        # keep the pointers sane: never write r26..r31 with ALU ops
        if c < 36 and c not in (28,) and ((x >> 4) & 31) >= 26 and c not in (32, 33, 34, 35, 29, 31): x = 0
        if c == 28 and ((x >> 4) & 15) * 2 >= 26: x = 0
        if c in (26, 27): x = x & ~0x30   # adiw/sbiw r24 only
        body.append(x); i += 1
    # avoid skips landing in the middle of a 2-word instruction: put a nop after each 2-word one
    fixed = []
    for k, x in enumerate(body):
        fixed.append(x)
        if (x & 0xFE0F) in (0x9000, 0x9200) and k + 1 < len(body): pass
    prog = out + fixed[:lim] + [0x0000, 0x0000, 0xCFFF]
    return prog
def to_hex(prog, fn):
    b = b''.join(struct.pack('<H', x & 0xFFFF) for x in prog)
    with open(fn, 'w') as f:
        for a in range(0, len(b), 16):
            c = b[a:a+16]; s = len(c) + (a >> 8) + (a & 255) + sum(c)
            f.write(':%02X%04X00%s%02X\n' % (len(c), a, c.hex().upper(), (-s) & 255))
        f.write(':00000001FF\n')
    open(fn + '.bin', 'wb').write(b)
if __name__ == '__main__':
    to_hex(gen(int(sys.argv[1]), int(sys.argv[3]) if len(sys.argv) > 3 else 10**9), sys.argv[2])
