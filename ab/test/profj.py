# where the time goes in the translated code: dispatcher stub vs blocks (python3 profj.py elf img ms)
import os, sys, subprocess, collections, builtins
elf = sys.argv[1]
syms0 = {}
for l in subprocess.run(['llvm-nm', elf], capture_output=True, text=True).stdout.splitlines():
    p = l.split()
    if len(p) == 3: syms0[p[2]] = int(p[0], 16)
cb = syms0['g_code']; cnt = collections.Counter(); tot = [0]
def blk(uc, addr, size, ud):
    n = size // 2; tot[0] += n
    if cb <= addr < cb + 0x10000: cnt['stub' if addr - cb < 0x50 else 'block'] += n
    else: cnt['C'] += n
builtins.__gbprof = (blk, cnt)
exec(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'devab.py')).read())
for k, v in cnt.most_common(): print('%6.2f%% %s' % (100 * v / tot[0], k))
