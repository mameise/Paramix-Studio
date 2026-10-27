#!/bin/sh
set -e
cd "$(dirname "$0")"
CF="--target=thumbv7m-none-eabi -mcpu=cortex-m3 -mthumb -ffreestanding -nostdlib -O2 -g -fno-common -Wall -Wno-initializer-overrides -Wno-unused-const-variable -fno-builtin-memset -DTC_JIT -DJIT_HASH=512 -DJIT_HWRITE -DNOSRAMWATCH -DJIT_STAGE -DROM_CACHED -DTONE_FIX -DBL_BLACK -DBL_OFFFIX -DBL_SLEEP -DFAST_LCD -DEXIT_KEY -DLCD_FASTCLK -DLCD_ROWS -DLCD_SKIP -DGFLUSH_STEP -DCLEAN -DGFLUSH_DARK -DGFLUSH_IDLE -DJIT_WHOT -DRTC_MS -DLCD_QUIET -DHW_OPTS -DSESS_ONCE -DSESS_AUTO -DOPT_MENU -DASK_EXTRA -DASK_SLIM -DASK_NUM -DEMU_CARE -DPARA_BACK -DPARA_MB_KEEP -DBL_PWM -DBL_MR3RST -DAPP_SELF -DBL_GUEST_PWM -DTC_IRLINK -DBAT_SHOW -DCLK_ECO -DECO_LAG=50 -DEMU_OVL -DASK_OVL -DDISP_PACK -DASK_DIM -DEMU_DIM -DASK_LANG -DBAT_SMOOTH -DEMU_PW -DJIT_HOT=8 -DEMU_TONE_Q -DEMU_TONE_GAP -DEMU_TQ_DROP -DTONE_SAFE -DEMU_DATE_FIX -DBAT_GUARD -DBAT_PIC -DLIBC_ALIAS -DHW_COLD_MIN -DLK_SYNC -mllvm -arm-load-store-opt=false $XCF"
O=""
for f in main.c hw.c ask.c libc.c flash.c state.c ../core/tc_all.c; do
  n=$(basename $f .c); X=""; [ "$n" = ask ] && X=-Oz; [ "$n" = hw ] && X=-Os; [ "$n" = state ] && X=-Os; [ "$n" = flash ] && X=-Os; case "$CF" in *-DEMU_TONE_Q*) [ "$n" = state ] && X=-Oz; [ "$n" = flash ] && X=-Oz;; esac; case "$CF" in *-DEMU_OVL*) [ "$n" != tc_all ] && X="$X -mno-outline";; esac; clang $CF $X -c $f -o $n.o; O="$O $n.o"; done
LDS=link.ld; case "$CF" in *-DEMU_OVL*) LDS=link_ovl.ld;; esac
ld.lld -T $LDS --no-rosegment $O -o emu.elf
llvm-objcopy -O binary -j .text -j .data -j .init emu.elf emu.bin
llvm-size emu.elf; ls -l emu.bin
