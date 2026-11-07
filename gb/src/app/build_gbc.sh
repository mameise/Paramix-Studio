#!/bin/sh
# Build GBCEmu (Game Boy Color) for the Paradise. Output: ../../gbc.bin
set -e
cd "$(dirname "$0")"
E=../../../emu/src/emu
CF="--target=thumbv7m-none-eabi -mcpu=cortex-m3 -mthumb -ffreestanding -nostdlib -fno-common -Wall -Wno-unused-function -Wno-unused-variable -fno-builtin-memset -isystem ../stub -DFAST_LCD -DEXIT_KEY -DLCD_FASTCLK -DTONE_FIX -DBL_SLEEP -DPARA_BACK -DPARA_MB_KEEP -DBL_PWM -DBL_MR3RST -DGB_SOUND -DTONE_SAFE -DGB_SKIP_ADAPT -DGB_TONE_RESTART -DGB_STATE -DGB_LINK -DPGB_SERIAL_WAIT -DGB_TRIM -DGBC -DGB_PAGES -DGPLAY -DSESS_FLAG -DPGB_BATCH -DPGB_FAST_CGB $XCF"
O=""
for f in main.c $E/hw.c $E/flash.c $E/libc.c; do
  n=$(basename $f .c); OPT=-Os; [ "$n" = main ] && OPT=-O2; clang $CF $OPT -c $f -o c_$n.o; O="$O c_$n.o"; done
ld.lld -T link.ld --no-rosegment $O -o gbc.elf
llvm-objcopy -O binary -j .text -j .data -j .init gbc.elf gbc.bin
cp gbc.bin ../../gbc.bin; rm -f c_*.o gbc.bin; [ -n "$KEEPELF" ] || rm -f gbc.elf; ls -l ../../gbc.bin
