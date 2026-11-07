# instructions per source line (needs an ELF with -g): python3 profl.py elf img ms
import os, sys, subprocess, collections
elf = sys.argv[1]
cnt = collections.Counter()
def blk(uc, addr, size, ud): cnt[addr] += size // 2
import builtins
builtins.__gbprof = (blk, cnt)
exec(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'devab.py')).read())
# expand blocks to instruction addresses roughly: attribute the block to its first address
addrs = sorted(cnt)
out = subprocess.run(['llvm-addr2line', '-e', elf, '-f'] + [hex(a) for a in addrs], capture_output=True, text=True).stdout.splitlines()
# llvm-addr2line with -i prints inline chains; take the first file:line per address
res = collections.Counter(); i = 0; k = 0
lines = out
mp = {}
cur = 0
j = 0
for a in addrs:
    fn = lines[j]; loc = lines[j + 1]; j += 2
    while j < len(lines) and not lines[j].startswith('0x') and j + 1 < len(lines) and ':' in lines[j + 1] and not lines[j].startswith('/') and lines[j] != '??' and False: j += 2
    res[(fn, loc.split('/')[-1])] += cnt[a]
tot = sum(res.values())
for (fn, loc), c in res.most_common(40): print('%6.2f%% %s %s' % (100 * c / tot, fn, loc))
