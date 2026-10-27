# keys.py "5:B 1.5:B*10 5:A 1:B 360: 0:W2000" -> KEYS for devemu.py: each item <wait s>:<key>[*n], key A B C W (wheel),
# W<ms> holds the wheel that long; a press lasts 150 ms. Prints KEYS and the total seconds.
import sys
M = {'A': 1, 'B': 2, 'C': 4, 'W': 8}
t = 0.0; out = []
for it in sys.argv[1].split():
    w, k = it.split(':'); n = 1
    if '*' in k: k, n = k.split('*'); n = int(n)
    for _ in range(n):
        t += float(w)
        if not k: continue
        hold = 0.15
        if len(k) > 1: hold = int(k[1:]) / 1000.0
        out.append('%.3f:%d' % (t, M[k[0]])); out.append('%.3f:0' % (t + hold)); t += hold
print(','.join(out)); print(int(t * 1000) + 3000, file=sys.stderr)
