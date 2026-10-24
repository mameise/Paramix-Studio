#!/bin/sh
# Build GBEmu for the Paradise (clang/lld, Cortex-M3). Output: ../../gb.bin
set -e
cd "$(dirname "$0")"
E=../../../emu/src/emu
CF="--target=thumbv7m-none-eabi -mcpu=cortex-m3 -mthumb -ffreestanding -nostdlib -fno-common -Wall -Wno-unused-function -Wno-unused-variable -fno-builtin-memset -isystem ../stub -DFAST_LCD -DLCD_FASTCLK -DTONE_FIX -DBL_SLEEP -DPARA_BACK -DPARA_MB_KEEP -DBL_PWM -DBL_MR3RST -DGB_SOUND -DTONE_SAFE -DGB_FAST -DPGB_FAST_LINE -DGB_SKIP_ADAPT -DGB_TONE_RESTART $XCF"
O=""
for f in main.c $E/hw.c $E/flash.c $E/libc.c; do
  n=$(basename $f .c); OPT=-Os; [ "$n" = main ] && OPT=-O2; clang $CF $OPT -c $f -o $n.o; O="$O $n.o"; done
ld.lld -T link.ld --no-rosegment $O -o gb.elf
llvm-objcopy -O binary -j .text -j .data -j .init gb.elf gb.bin
cp gb.bin ../../gb.bin; rm -f *.o gb.bin; [ -n "$KEEPELF" ] || rm -f gb.elf; ls -l ../../gb.bin
