# 1.6.13 test5: sampling profile (PC every K host instructions) of the T18 core in the M3 test bed, from a ref state.
# LOADST=state python3 test/m3samp.py <elf> <fw> <warm s> <measure s>
import sys, struct, subprocess, bisect, collections, os
exec(open('test/m3run.py').read().split("import collections, os")[0])
syms_s = []
for l in subprocess.run(['arm-none-eabi-nm', '-S', '--size-sort', elf], capture_output=True, text=True).stdout.splitlines():
    p = l.split()
    if len(p) == 4 and p[2] in 'tTwW': syms_s.append((int(p[0], 16), int(p[1], 16), p[3]))
syms_s.sort(); starts = [x[0] for x in syms_s]
jc = syms.get('__jc_start', 0xFFFFFFF0); je = syms.get('__jc_end', jc)
cnt = collections.Counter(); ON = [False]
spin_pcs = set(range(syms['mb_spin'] & ~1, (syms['mb_spin'] & ~1) + 12, 2))
KS = 997
TU = [0]
def name(a):
    if jc <= a < je: return '<JIT code>'
    i = bisect.bisect_right(starts, a) - 1
    return syms_s[i][2] if i >= 0 and a < syms_s[i][0] + syms_s[i][1] + 4 else '?%x' % a
def run(n):
    global pc
    while True:
        u.emu_start(pc, 0xFFFFFFFF, count=KS); a = u.reg_read(UC_ARM_REG_PC); pc = a | 1
        TU[0] += 1
        if TU[0] % 70 == 0 and jc <= (a & ~1) < je and 't18_tu_pc' in syms: u.mem_write(syms['t18_tu_pc'], struct.pack('<I', a | 1))   # SysTick every ~70000
        if (a & ~1) in spin_pcs and u.reg_read(UC_ARM_REG_R3) > 1000: return
        if ON[0]:
            nm = name(a & ~1)
            if nm == 'mb_spin': continue
            cnt[nm] += 1
            if nm in ('arm7_run', 'exec_arm', 'idle_try'): cnt['  gpc %08x' % (u.reg_read(UC_ARM_REG_R0) if False else 0)] += 0
seq = 0
def cmd(c, *a):
    global seq
    wr(0, c)
    for i, v in enumerate(a): wr(1 + i, v)
    seq += 1; wr(9, seq); u.reg_write(UC_ARM_REG_R3, 0); run(0)
run(0)
_b = open(os.environ['LOADST'], 'rb').read(); u.mem_write(0x60FC0000, _b); cmd(7, len(_b) - 0x20000)
cmd(1, int(float(sys.argv[3]) * 1000000))
ON[0] = True
cmd(1, int(float(sys.argv[4]) * 1000000))
tot = sum(cnt.values())
print('samples', tot)
for k, v in cnt.most_common(40): print('%-28s %5.1f%%' % (k, 100 * v / tot))
