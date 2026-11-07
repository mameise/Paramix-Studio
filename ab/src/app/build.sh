#!/bin/sh
# Build ArduboyEmu for the Paradise (clang/lld, Cortex-M3). Output: ../../ab.bin
set -e
cd "$(dirname "$0")"
E=../../../emu/src/emu
C=../core
CF="--target=thumbv7m-none-eabi -mcpu=cortex-m3 -mthumb -ffreestanding -nostdlib -fno-common -Wall -Wno-unused-function -Wno-unused-variable -Wno-parentheses -Wno-dangling-else -Wno-unknown-attributes -fno-builtin-memset -ffunction-sections -DAVR_FAST= -DPERFORMANCE -DFAST_LCD -DEXIT_KEY -DLCD_FASTCLK -DTONE_FIX -DBL_SLEEP -DPARA_BACK -DPARA_MB_KEEP -DBL_PWM -DBL_MR3RST -DTONE_SAFE -DGPLAY -DSESS_FLAG $XCF"
O=""
for f in main.c $C/avr.c $E/hw.c $E/flash.c $E/libc.c; do
  n=$(basename $f .c); OPT=-Os
  case $n in avr) OPT=-O2;; esac
  if [ $n = avr ] && [ -z "$CLANG_CORE" ]; then
    # the core with GCC: each handler gets its own dispatch (clang shares one indirect jump for all)
    arm-none-eabi-gcc -mcpu=cortex-m3 -mthumb -ffreestanding -nostdlib -fno-common -ffunction-sections -fno-tree-loop-distribute-patterns -Wall -Wno-unused-function -Wno-unused-variable -Wno-misleading-indentation -DAVR_FAST= $XCF -O2 -c $f -o $n.o
  else clang $CF $OPT -c $f -o $n.o; fi
  O="$O $n.o"; done
ld.lld -T link.ld --gc-sections --no-rosegment $O -o ab.elf
llvm-objcopy -O binary -j .text -j .data -j .init ab.elf ab.bin
cp ab.bin ../../ab.bin; rm -f *.o ab.bin; [ -n "$KEEPELF" ] || rm -f ab.elf; ls -l ../../ab.bin
