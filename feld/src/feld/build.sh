#!/bin/sh
# Build the Paramix field switch (feld.bin, runs from PRAM 0)
set -e
cd "$(dirname "$0")"
CF="--target=thumbv7m-none-eabi -mcpu=cortex-m3 -mthumb -ffreestanding -nostdlib -Os -fno-common -Wall -Wno-initializer-overrides -Wno-unused-const-variable -fno-builtin-memset -DTONE_FIX -DBL_BLACK -DBL_OFFFIX -DBL_SLEEP -DFAST_LCD -DLCD_FASTCLK -DCLEAN -DPARA_BACK -DPARA_MB_KEEP -DPARA_FLASH -DPARA_FLASH_LATE -DFELD_AUTO -DFELD_BFALL -DFELD_BFALL_NOA -DCLEAN_AUTO -DBAT_SHOW -DBAG_DEL -DFELD_PW -DFELD_TAMA_ANY -DFELD_NOCLK -DFELD_BSETTLE -DFELD_SEEN $XCF"
O=""
for f in main.c hw.c libc.c flash.c; do n=$(basename $f .c); OPT=; [ "$n" = main ] && OPT=-Oz; clang $CF $OPT -c $f -o $n.o; O="$O $n.o"; done   # 1.6.8: main with -Oz (menu code, size before speed)
N=${OUT:-feld}
ld.lld -T link.ld --no-rosegment $O -o $N.elf
llvm-objcopy -O binary -j .text $N.elf $N.bin
llvm-size $N.elf; ls -l $N.bin
# 1.7.2 CLEAN_ROM: a second build for ROMs without the Paradise (own clock); the start device keeps the build above
if [ -z "$XCF" ]; then XCF=-DFELD_CLEAN OUT=feldc sh "$0" --inner; fi
