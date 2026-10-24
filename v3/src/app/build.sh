#!/bin/sh
# Build V3Emu for the Paradise (clang/lld, Cortex-M3). Output: ../../v3.bin
set -e
cd "$(dirname "$0")"
E=../../../emu/src/emu
CF="--target=thumbv7m-none-eabi -mcpu=cortex-m3 -mthumb -ffreestanding -nostdlib -fno-common -Wall -Wno-unused-function -Wno-unused-variable -fno-builtin-memset -DFAST_LCD -DLCD_FASTCLK -DTONE_FIX -DBL_SLEEP -DSESS_ONCE -DPARA_BACK -DPARA_MB_KEEP -DBL_PWM -DBL_MR3RST -DBAT_SHOW -DV3_FREESTANDING -DV3_ICONS -DV3_LINK -DV3_CLOCK -DCLK_ECO -DECO_RTC -DV3_SETX -DV3_CARE -DASK_NUM -DV3_CLK_RETRY -I../core $XCF"
O=""
for f in main.c $E/hw.c $E/ask.c $E/flash.c $E/libc.c ../core/v3cpu.c; do
  n=$(basename $f .c); OPT=-Os; [ "$n" = v3cpu ] && OPT=-O2; clang $CF $OPT -c $f -o $n.o; O="$O $n.o"; done
ld.lld -T link.ld --no-rosegment $O -o v3.elf
llvm-objcopy -O binary -j .text -j .data -j .init v3.elf v3.bin
cp v3.bin ../../v3.bin; rm -f *.o v3.bin; [ -n "$KEEPELF" ] || rm -f v3.elf; ls -l ../../v3.bin
