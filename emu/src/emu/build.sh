#!/bin/sh
set -e
cd "$(dirname "$0")"
CF="--target=thumbv7m-none-eabi -mcpu=cortex-m3 -mthumb -ffreestanding -nostdlib -O2 -g -fno-common -Wall -Wno-initializer-overrides -Wno-unused-const-variable -fno-builtin-memset -DTC_JIT -DJIT_HASH=512 -DJIT_HWRITE -DNOSRAMWATCH -DJIT_STAGE -DROM_CACHED -DTONE_FIX -DBL_BLACK -DBL_OFFFIX -DBL_SLEEP -DFAST_LCD -DLCD_FASTCLK -DLCD_ROWS -DLCD_SKIP -DGFLUSH_STEP -DCLEAN -DGFLUSH_DARK -DGFLUSH_IDLE -DJIT_WHOT -DRTC_MS -DLCD_QUIET -DHW_OPTS -DSESS_ONCE -DSESS_AUTO -DOPT_MENU -DPARA_BACK -DBL_PWM -DBL_MR3RST -mllvm -arm-load-store-opt=false $XCF"
O=""
for f in main.c hw.c ask.c libc.c flash.c state.c ../core/tc_all.c; do
  n=$(basename $f .c); X=""; [ "$n" = ask ] && X=-Os; [ "$n" = hw ] && X=-Os; [ "$n" = state ] && X=-Os; clang $CF $X -c $f -o $n.o; O="$O $n.o"; done
ld.lld -T link.ld --no-rosegment $O -o emu.elf
llvm-objcopy -O binary -j .text -j .data -j .init emu.elf emu.bin
llvm-size emu.elf; ls -l emu.bin
