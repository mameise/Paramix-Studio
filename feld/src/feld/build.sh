#!/bin/sh
# Build the Paramix field switch (feld.bin, runs from PRAM 0)
set -e
cd "$(dirname "$0")"
CF="--target=thumbv7m-none-eabi -mcpu=cortex-m3 -mthumb -ffreestanding -nostdlib -Os -fno-common -Wall -Wno-initializer-overrides -Wno-unused-const-variable -fno-builtin-memset -DTONE_FIX -DBL_BLACK -DBL_OFFFIX -DBL_SLEEP -DFAST_LCD -DLCD_FASTCLK -DCLEAN $XCF"
O=""
for f in main.c hw.c libc.c flash.c; do n=$(basename $f .c); clang $CF -c $f -o $n.o; O="$O $n.o"; done
ld.lld -T link.ld --no-rosegment $O -o feld.elf
llvm-objcopy -O binary -j .text feld.elf feld.bin
llvm-size feld.elf; ls -l feld.bin
