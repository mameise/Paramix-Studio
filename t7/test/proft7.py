# per-function instruction profile of t7.bin in Unicorn (uses devt7 setup)
import sys, os, struct, subprocess, collections
os.environ.setdefault('SHOTS','')
src = open(os.path.join(os.path.dirname(__file__), 'devt7.py')).read()
src = src.replace("while state['ms'] < total_ms", "cnt=collections.Counter()\nFROM=int(os.environ.get('FROM','0'))\ndef hc(uc,a,s,d):\n    if state['ms']>=FROM: cnt[a]+=1\nu.hook_add(UC_HOOK_CODE,hc,begin=0,end=0xffff)\nwhile state['ms'] < total_ms")
exec(compile(src, 'devt7', 'exec'))
fn = sorted((v, k) for k, v in syms.items() if v < 0x10000)
import bisect
keys=[a for a,_ in fn]
per=collections.Counter()
for a,c in cnt.items():
    i=bisect.bisect_right(keys,a)-1; per[fn[i][1] if i>=0 else '?']+=c
tot=sum(per.values())
for n,c in per.most_common(25): print('%-28s %6.2f%%' % (n, 100*c/tot))
e=syms.get('t7_cpu_step',0)&~1
print('6502 instructions', cnt[e], 'ARM instr total', tot, 'per 6502 instr', tot/max(1,cnt[e]))
