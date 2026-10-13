#!/bin/sh
# Build DOOM for the Paradise: pram.bin (into program memory, loaded by the stub) and xip.bin (flash window)
set -e
cd "$(dirname "$0")/.."
CF="-mcpu=cortex-m3 -mthumb -O2 -ffunction-sections -fdata-sections -IDoom/include -Iplat -Idev -w -DPARADISE -DPARADISE_DEV -DPDOOM_W=128 -DPDOOM_H=128 -DMAX_STATIC_ZONE=${ZONE:-57344} -DCACHE_ALL_COLORMAP_TO_RAM=1 -DXIP_FLUSH -DFRAME_ROW -DFAST_LCD -DLCD_FASTCLK -DTONE_FIX -DBL_SLEEP -DNO_STACK_SWITCH -DSINGLE_FB -DUA_FIX -DCHG_WITH_C -DMUS_PLAY -DSND_TOGGLE -DCLEAN -DSTBAR_LUMP -DKEYMAP $XCF"
mkdir -p dev/obj; rm -f dev/obj/*.o
for f in Doom/source/*.c plat/printf.c dev/paradise.c dev/glue.c dev/hw.c dev/flash.c dev/libc_extra.c; do
  [ -f $f ] || continue
  n=$(basename $f .c); arm-none-eabi-gcc $CF -c $f -o dev/obj/$n.o; done
cd dev
arm-none-eabi-gcc -mcpu=cortex-m3 -mthumb -nostartfiles --specs=nano.specs -Wl,--gc-sections -Wl,-Map=doom.map -T ${LDS:-paradise_sram.ld} obj/*.o -lc -lgcc -lnosys -o doom.elf 2>&1 | grep -v "warning\|note:" || true
arm-none-eabi-objcopy -O binary -j .pram -j .data doom.elf pram.bin
arm-none-eabi-objcopy -O binary -j .r20 -j .xip -j .ARM.exidx doom.elf xip.bin
arm-none-eabi-size -A doom.elf | grep -v "debug\|^$"
ls -l pram.bin xip.bin
