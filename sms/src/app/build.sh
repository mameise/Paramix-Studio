#!/bin/sh
# Build SMSEmu for the Paradise (clang/lld, Cortex-M3). Output: ../../sms.bin
set -e
cd "$(dirname "$0")"
E=../../../emu/src/emu
CF="--target=thumbv7m-none-eabi -mcpu=cortex-m3 -mthumb -ffreestanding -nostdlib -fno-common -Wall -Wno-unused-function -Wno-unused-variable -fno-builtin-memset -isystem ../stub -DFAST_LCD -DLCD_FASTCLK -DTONE_FIX -DBL_SLEEP -DPARA_BACK -DPARA_MB_KEEP -DBL_PWM -DBL_MR3RST -DTONE_SAFE -DSMS_STATE -DSMS_GG $XCF"
O=""
for f in main.c $E/hw.c $E/flash.c $E/libc.c; do
  n=$(basename $f .c); OPT=-Os; [ "$n" = main ] && OPT=-O2; clang $CF $OPT -c $f -o $n.o; O="$O $n.o"; done
ld.lld -T link.ld --no-rosegment $O -o sms.elf
llvm-objcopy -O binary -j .text -j .data -j .init sms.elf sms.bin
cp sms.bin ../../sms.bin; rm -f *.o sms.bin; [ -n "$KEEPELF" ] || rm -f sms.elf; ls -l ../../sms.bin
