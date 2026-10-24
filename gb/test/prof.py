# count instructions per function (PROF=1 in devgb): exec devgb with a block hook
import os, sys, subprocess, bisect, collections
elf = sys.argv[1]
fs = []
for l in subprocess.run(['llvm-nm', '-S', '--size-sort', elf], capture_output=True, text=True).stdout.splitlines():
    p = l.split()
    if len(p) == 4 and p[2] in 'tT': fs.append((int(p[0], 16) & ~1, int(p[1], 16), p[3]))
fs.sort(); starts = [f[0] for f in fs]
cnt = collections.Counter()
def blk(uc, addr, size, ud):
    i = bisect.bisect_right(starts, addr) - 1
    if i >= 0 and addr < fs[i][0] + fs[i][1]: cnt[fs[i][2]] += size // 2
    else: cnt['?'] += size // 2
import builtins
builtins.__gbprof = (blk, cnt)

exec(open(os.path.join(os.path.dirname(__file__), 'devgb.py')).read())
tot = sum(cnt.values())
for n, c in cnt.most_common(25): print('%6.2f%% %s' % (100 * c / tot, n))
