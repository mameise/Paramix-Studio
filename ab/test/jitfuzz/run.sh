#!/bin/bash
# setup: cp * /tmp/jf; cd /tmp/jf; gcc -O1 -o ref ref.c <studio>/ab/src/core/avr.c; cp <studio>/ab/ab.bin <studio>/ab/src/app/ab.elf /tmp/jf (KEEPELF=1 build)
# run.sh seed: compare host interpreter and the device JIT (Unicorn)
s=$1; d=/tmp/jf/w$s${2:+_$2}; mkdir -p $d; cd /tmp/jf
python3 gen.py $s $d/p.hex $2
./ref $d/p.hex.bin > $d/ref.txt
cd /home/claude/studio/ab/test
python3 mkimg.py /tmp/jf/ab.bin $d/p.hex $d/img.bin
OUT=$d DUMP="g_mem+256:96,g_mem+512:64" python3 devab.py /tmp/jf/ab.elf $d/img.bin 40 2>&1 | grep DUMP | awk '{print $3}' > $d/jit.txt
if python3 /tmp/jf/same.py $d; then echo "seed $s ok"; rm -rf $d; else echo "seed $s DIFF"; fi
