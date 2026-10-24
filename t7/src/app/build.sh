#!/bin/sh
# Build the T7 app for the Paradise (clang/lld, Cortex-M3). Output: ../../t7.bin
set -e
cd "$(dirname "$0")"
E=../../../emu/src/emu
CF="--target=thumbv7m-none-eabi -mcpu=cortex-m3 -mthumb -ffreestanding -nostdlib -fno-common -Wall -Wno-unused-function -fno-builtin-memset -DFAST_LCD -DLCD_FASTCLK -DTONE_FIX -DBL_SLEEP -DT7_NOMALLOC -DT7_FREESTANDING -DT7_NO_SYNTH -DSPI_TRACE_MAX=1 -DT7_FASTRUN -DT7_VIDEO_CYCLES -DT7_SND_RING=16u -DT7_SND_SYN_N=4u -DT7_BELL_WAKE -DT7_FR_ICONS -DSESS_ONCE -DSESS_AUTO -DPARA_BACK -DPARA_MB_KEEP -DBL_PWM -DBL_MR3RST -DT7_PW_FIX -DT7_LINK -DT7_IDLE -DT7_STOP -DT7_KEYQ -DT7_STOP_KEYACT=4000000u -DT7_TONE_Q -DT7_SESS_EESEQ -DT7_EE_SOON -DBAT_SHOW -DCLK_ECO -DECO_LAG=400 -DT7_WFI_FIX -DT7_EE_SHIFT -DECO_RTC -DT7_GAME_KEEP -DT7_CARE -DASK_NUM -I../core $XCF"
O=""
for f in main.c t7app.c compat.c $E/hw.c $E/ask.c $E/flash.c $E/libc.c libgcc.c ../core/t7.c ../core/t7_cpu.c ../core/t7_memory.c ../core/t7_eeprom.c ../core/t7_sound.c ../core/figure_data.c; do
  n=$(basename $f .c); OPT=-Os; [ "$n" = t7_cpu ] && OPT=${CPUOPT:--O2}; clang $CF $OPT -c $f -o $n.o; O="$O $n.o"; done
ld.lld -T link.ld --no-rosegment $O -o t7.elf
llvm-objcopy -O binary -j .text -j .data -j .init t7.elf t7.bin
cp t7.bin ../../t7.bin; rm -f *.o t7.bin; [ -n "$KEEPELF" ] || rm -f t7.elf; llvm-size ../../t7.bin 2>/dev/null; ls -l ../../t7.bin
