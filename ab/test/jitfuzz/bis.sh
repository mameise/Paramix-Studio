#!/bin/bash
# smallest prefix of seed $1 that differs
lo=0; hi=200
while [ $((hi-lo)) -gt 1 ]; do m=$(((lo+hi)/2)); if ./run.sh $1 $m | grep -q ok; then lo=$m; else hi=$m; fi; done
echo "seed $1 first diff at body $hi"
python3 -c "
import sys; sys.path.insert(0,'/tmp/jf'); import gen
p=gen.gen($1,$hi); n=len(gen.gen($1,0))-3
for k,x in enumerate(p[n:n+$hi]): print(k, '%04x'%x)
" | tail -4
