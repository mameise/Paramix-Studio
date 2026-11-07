#!/bin/sh
# Build PokeMiniEmu for the Paradise (clang/lld, Cortex-M3). Output: ../../pm.bin
set -e
cd "$(dirname "$0")"
E=../../../emu/src/emu
C=../core
CF="--target=thumbv7m-none-eabi -mcpu=cortex-m3 -mthumb -ffreestanding -nostdlib -fno-common -Wall -Wno-unused-function -Wno-unused-variable -Wno-unused-but-set-variable -Wno-parentheses -Wno-dangling-else -fno-builtin-memset -ffunction-sections -isystem ../stub -I$C -DPERFORMANCE -DPM_SYNCCYCLES=64 -DPM_FASTLCD -DPM_HEAP=0x7400 -DFAST_LCD -DEXIT_KEY -DLCD_FASTCLK -DTONE_FIX -DBL_SLEEP -DPARA_BACK -DPARA_MB_KEEP -DBL_PWM -DBL_MR3RST -DTONE_SAFE -DGPLAY -DSESS_FLAG $XCF"
O=""
for f in main.c $C/*.c $E/hw.c $E/flash.c $E/libc.c; do
  n=$(basename $f .c); OPT=-Os
  case $n in MinxCPU*|Hardware|MinxPRC|MinxTimers|MinxIRQ) OPT=-O2;; esac
  clang $CF $OPT -c $f -o $n.o; O="$O $n.o"; done
ld.lld -T link.ld --gc-sections --no-rosegment $O -o pm.elf
llvm-objcopy -O binary -j .text -j .data -j .init pm.elf pm.bin
cp pm.bin ../../pm.bin; rm -f *.o pm.bin; [ -n "$KEEPELF" ] || rm -f pm.elf; ls -l ../../pm.bin
