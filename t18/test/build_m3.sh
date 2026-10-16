#!/bin/sh
# Build the core with the translator for the Unicorn test bed (m3main.c, memory as on the Paradise:
# test/m3.ld). Objects one by one with the device's optimisation, so the linker script can place them.
# sh test/build_m3.sh out.elf [flags], e.g. -DT18_JIT -DT18_JC_LINKED -DT18_PRAM_WR -DJH_BITS=8 -DJT_BITS=8 -DJ_HOT=8
O=$1; shift
cd "$(dirname "$0")/.." || exit 1
D=$(mktemp -d)
CF="-mcpu=cortex-m3 -mthumb -DPARADISE -ffreestanding -nostdlib -ffunction-sections -fdata-sections -Wall -Wno-unused-function"
for f in test/m3main.c src/core/t18cpu.c src/core/t18soc.c src/core/t18jit.c; do
  n=$(basename $f .c); OPT=${OPT_ALL:--Os}; [ "$n" = t18cpu ] && OPT=-O2
  arm-none-eabi-gcc $CF $OPT "$@" -c $f -o $D/$n.o || exit 1
done
arm-none-eabi-gcc $CF -Wl,--gc-sections -T ${LD:-test/m3.ld} $D/m3main.o $D/t18cpu.o $D/t18soc.o $D/t18jit.o -lgcc -o $O && arm-none-eabi-size -A $O | head -8
rm -rf $D
