#!/bin/sh
# Build the T18 app for the Paradise (arm-none-eabi-gcc, Cortex-M3).
# Output: ../../t18.bin (PRAM part, loaded by the stub) and ../../t18x.bin (flash part, at T18_XIP_OFF).
set -e
cd "$(dirname "$0")"
E=../../../emu/src/emu
CC=${CC:-arm-none-eabi-gcc}
SRC="main.c t18app.h link.ld $E/hw.c $E/ask.c $E/ask.h $E/flash.c $E/libc.c $E/paraclock.h ../core/t18cpu.c ../core/t18cpu.h ../core/t18soc.c ../core/t18soc.h ../core/t18jit.c ../core/t2enc.h"
BUILD=$(cat $SRC | cksum | cut -d' ' -f1)
# 1.6.6: the switches that proved themselves are the default; T18_TEST=1 keeps the code screens and the speed display
REL="-DT18_HOTWIN -DT18_ROWSUM_RAM20 -DT18_JHDR -DT18_JH_SPLIT=0x20000100u -DT18_PRESENT_IDLE -DT18_PRESENT_STABLE -DT18_SF_FAST -DT18_HOLE_FAST -DT18_TRY_SEQ -DT18_MEM_HELPER -DT18_FLUSH_IDLE -DT18_FF_FAST -DT18_IDLE_RUN"
[ -n "$T18_TEST" ] || REL="$REL -DT18_QUIET_FATAL -DT18_NO_SPEED"
[ -n "$T18_PLAIN" ] && REL=""
JHB=${JHB:-9}; T18_XIP_MORE=${T18_XIP_MORE-1}
CF="-mcpu=cortex-m3 -mthumb -ffreestanding -nostdlib -fno-common -ffunction-sections -fdata-sections -Wall -Wno-unused-function -DPARADISE -DT18_JIT -DT18_JC_LINKED -DT18_PRAM_WR -DT18_PRAM_WR_SRAM -DJH_BITS=${JHB:-8} -DJT_BITS=8 -DJ_HOT=8 -DT18_BUILD=${BUILD}u -DFAST_LCD -DLCD_FASTCLK -DTONE_FIX -DBL_SLEEP -I../core $REL $XCF"
O=""
for f in main.c $E/hw.c $E/ask.c $E/flash.c $E/libc.c ../core/t18cpu.c ../core/t18soc.c ../core/t18jit.c; do
  n=$(basename $f .c); OPT=-Os; [ "$n" = t18cpu ] && OPT=-O2; [ "$n" = libc ] && OPT="-Os -fno-builtin -fno-tree-loop-distribute-patterns"
  $CC $CF $OPT -c $f -o $n.o; O="$O $n.o"; done
# T18_XIP_MORE=1: rarely run code into the flash window as well (bigger code buffer)
XM_CPU=${XM_CPU:-.text.arm7_step}   # idle_try stays in the PRAM (1.6.6, T18_IDLE_RUN)
if [ -n "$T18_XIP_MORE" ]; then MORE='*ask.o(.text*) *t18soc.o(.text.io_wr .text.io_rd .text.tg18_run_cycles .text.tmr_io_rd .text.tmr_io_wr) *t18cpu.o('"$XM_CPU"') *libgcc.a:_udivmoddi4.o(.text*)'; else MORE=''; fi
sed "s|XIPMORE|$MORE|" link.ld > link_gen.ld
$CC $CF -T link_gen.ld -Wl,--gc-sections -Wl,-Map=t18.map $O -lgcc -o t18.elf
arm-none-eabi-objcopy -O binary -j .text -j .data -j .init t18.elf t18.bin
arm-none-eabi-objcopy -O binary -j .xip t18.elf t18x.bin
cp t18.bin ../../t18.bin; cp t18x.bin ../../t18x.bin; rm -f *.o t18.bin t18x.bin link_gen.ld; [ -n "$KEEPELF" ] || rm -f t18.elf t18.map
ls -l ../../t18.bin ../../t18x.bin
