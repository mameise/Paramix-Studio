# per-function instruction profile of v3.bin in Unicorn (devv3 setup), counted from FROM ms on
import sys, os, collections, bisect
os.environ.setdefault('SHOTS', '')
FROM = int(os.environ.get('FROM', '0'))
src = open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'devv3.py')).read()
src = src.replace("while state['ms'] < total_ms", "cnt=collections.Counter()\ndef hc(uc,a,s,d):\n    if state['ms']>=FROM: cnt[a]+=1\nu.hook_add(UC_HOOK_CODE,hc,begin=0,end=0xffff)\nwhile state['ms'] < total_ms")
exec(compile(src, 'devv3', 'exec'))
fn = sorted((v, k) for k, v in syms.items() if v < 0x10000)
keys = [a for a, _ in fn]; per = collections.Counter()
for a, c in cnt.items():
    i = bisect.bisect_right(keys, a) - 1; per[fn[i][1] if i >= 0 else '?'] += c
tot = sum(per.values())
for n, c in per.most_common(25): print('%-28s %6.2f%%' % (n, 100 * c / tot))
print('ARM instr total', tot, 'per ms', tot / max(1, total_ms - FROM))
