# Profile host code by function (Unicorn block hook, sizes as instruction estimate).
import sys, struct, subprocess, bisect, collections, os
exec(open('test/m3run.py').read().split("import collections, os")[0])
syms_s = []
for l in subprocess.run(['arm-none-eabi-nm', '-S', '--size-sort', elf], capture_output=True, text=True).stdout.splitlines():
    p = l.split()
    if len(p) == 4 and p[2] in 'tTwW': syms_s.append((int(p[0], 16), int(p[1], 16), p[3]))
syms_s.sort(); starts = [x[0] for x in syms_s]
jc = syms.get('JC', 0xFFFFFFF0); print('JC', hex(jc))
cnt = collections.Counter()
calls = [0]
samp = []
ON = [False]
def hb(uc, a, sz, ud):
    if not ON[0]: return
    calls[0] += 1
    if calls[0] % 50000 == 0: samp.append((hex(a), sz))
    if jc <= a < jc + int(os.environ.get("JCSZ", "16384")):
        cnt['<JIT code>'] += sz // 2; return
    i = bisect.bisect_right(starts, a) - 1
    nm = syms_s[i][2] if i >= 0 and a < syms_s[i][0] + syms_s[i][1] + 4 else '?%x' % a
    cnt[nm] += sz // 2
K = 200000
spin_pcs = set(range(syms['mb_spin'] & ~1, (syms['mb_spin'] & ~1) + 12, 2))
def run(n):
    global pc
    m = 0
    while True:
        u.emu_start(pc, 0xFFFFFFFF, count=K); pc = u.reg_read(UC_ARM_REG_PC) | 1; m += K
        if u.reg_read(UC_ARM_REG_PC) & ~1 in spin_pcs and u.reg_read(UC_ARM_REG_R3) > 1000:
            print('count method: %d slices, spins %d -> %d' % (m // K, u.reg_read(UC_ARM_REG_R3), m - 4 * u.reg_read(UC_ARM_REG_R3))); return
seq = 0
def cmd(c, *a):
    global seq
    wr(0, c)
    for i, v in enumerate(a): wr(1 + i, v)
    seq += 1; wr(9, seq); u.reg_write(UC_ARM_REG_R3, 0); run(0)
import os
if os.environ.get('CODE'):
    def hc(uc, a, sz, ud): hb(uc, a, 2, None)
    u.hook_add(UC_HOOK_CODE, hc)
else: u.hook_add(UC_HOOK_BLOCK, hb)
run(0)
cmd(5, 2026, 10, 15, 8, 0, 0)
cmd(1, int(float(sys.argv[3]) * 1000))
ON[0] = True
cmd(1, int(float(sys.argv[4]) * 1000))
tot = sum(cnt.values())
print('total approx %.1f M host instr, %d blocks' % (tot / 1e6, calls[0])); print(samp[:20])
for k, v in cnt.most_common(30): print('%-28s %5.1f%%' % (k, 100 * v / tot))
