#!/bin/sh
set -e
cd "$(dirname "$0")"
CF="--target=thumbv7m-none-eabi -mcpu=cortex-m3 -mthumb -ffreestanding -nostdlib -O2 -g -fno-common -Wall -Wno-initializer-overrides -Wno-unused-const-variable -fno-builtin-memset -DTC_JIT -DJIT_HASH=512 -DJIT_HWRITE -DNOSRAMWATCH -DJIT_STAGE -DROM_CACHED -DTONE_FIX -DBL_BLACK -DBL_OFFFIX -DBL_SLEEP -DFAST_LCD -DLCD_FASTCLK -DLCD_ROWS -DLCD_SKIP -DGFLUSH_STEP -DCLEAN -DGFLUSH_DARK -DJIT_WHOT -mllvm -arm-load-store-opt=false $XCF"
O=""
for f in main.c hw.c libc.c flash.c state.c ../core/tc_all.c; do
  n=$(basename $f .c); clang $CF -c $f -o $n.o; O="$O $n.o"; done
ld.lld -T link.ld --no-rosegment $O -o emu.elf
llvm-objcopy -O binary -j .text -j .data -j .init emu.elf emu.bin
llvm-size emu.elf; ls -l emu.bin
