#!/bin/sh
# Build V1Emu for the Paradise (clang/lld, Cortex-M3). Output: ../../v1.bin
set -e
cd "$(dirname "$0")"
E=../../../emu/src/emu
CF="--target=thumbv7m-none-eabi -mcpu=cortex-m3 -mthumb -ffreestanding -nostdlib -fno-common -Wall -Wno-unused-function -Wno-unused-variable -fno-builtin-memset -DFAST_LCD -DLCD_FASTCLK -DTONE_FIX -DBL_SLEEP -DSESS_ONCE -DPARA_BACK -DBL_PWM -DBL_MR3RST -I../core $XCF"
O=""
for f in main.c $E/hw.c $E/ask.c $E/flash.c $E/libc.c ../core/cpu.c ../core/hw.c ../core/tamalib.c; do
  n=$(basename $f .c); [ "$f" = ../core/hw.c ] && n=thw; OPT=-Os; [ "$n" = cpu ] && OPT=-O2; clang $CF $OPT -c $f -o $n.o; O="$O $n.o"; done
ld.lld -T link.ld --no-rosegment $O -o v1.elf
llvm-objcopy -O binary -j .text -j .data -j .init v1.elf v1.bin
cp v1.bin ../../v1.bin; rm -f *.o v1.bin; [ -n "$KEEPELF" ] || rm -f v1.elf; ls -l ../../v1.bin
