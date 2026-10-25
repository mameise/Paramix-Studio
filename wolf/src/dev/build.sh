#!/bin/sh
# Wolf3D for the Paradise (arm-none-eabi-gcc, newlib-nano). Output: ../../wolf.bin (program into PRAM, up to 64 KB)
# SOD=1: Spear of Destiny (-DSPEAR), output ../../wolfsod.bin
set -e
cd "$(dirname "$0")/.."
E=../../emu/src/emu
N=wolf; [ -n "$SOD" ] && { N=wolfsod; XCF="-DSPEAR $XCF"; }
CF="-mcpu=cortex-m3 -mthumb -Os -ffunction-sections -fdata-sections -fno-common -w -DPARAMIX -DPMX_SD_IN_TICK -Iw4 -Ipmx -DFAST_LCD -DLCD_FASTCLK -DTONE_FIX -DTONE_SAFE -DBL_SLEEP -DPARA_BACK -DPARA_MB_KEEP -DBL_PWM -DBL_MR3RST $XCF"
mkdir -p dev/obj; rm -f dev/obj/*.o
for f in w4/*.c pmx/*.c dev/paradise.c $E/hw.c $E/flash.c; do
  n=$(basename $f .c); arm-none-eabi-gcc $CF -c $f -o dev/obj/$n.o; done
arm-none-eabi-gcc -mcpu=cortex-m3 -mthumb -nostartfiles --specs=nano.specs -Wl,--gc-sections -Wl,-Map=dev/wolf.map -Wl,--noinhibit-exec -T dev/link.ld dev/obj/*.o -lc -lgcc -o dev/wolf.elf
arm-none-eabi-objcopy -O binary -j .text -j .data -j .scode --gap-fill 0xff dev/wolf.elf dev/wolf.bin
arm-none-eabi-size -A dev/wolf.elf | grep -v "debug\|^$\|comment\|ARM"
cp dev/wolf.bin ../$N.bin; rm -rf dev/obj dev/wolf.bin; [ -n "$KEEPELF" ] || rm -f dev/wolf.elf dev/wolf.map; ls -l ../$N.bin
