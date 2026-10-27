#!/bin/sh
# Build the T7 app for the Paradise (clang/lld, Cortex-M3). Output: ../../t7.bin
set -e
cd "$(dirname "$0")"
E=../../../emu/src/emu
XID=${XID:-0x$(date +%s | sha1sum | cut -c1-8)}   # the two parts belong together
CF="--target=thumbv7m-none-eabi -mcpu=cortex-m3 -mthumb -ffreestanding -nostdlib -fno-common -Wall -Wno-unused-function -fno-builtin-memset -DFAST_LCD -DEXIT_KEY -DLCD_FASTCLK -DTONE_FIX -DBL_SLEEP -DT7_NOMALLOC -DT7_FREESTANDING -DT7_NO_SYNTH -DSPI_TRACE_MAX=1 -DT7_FASTRUN -DT7_VIDEO_CYCLES -DT7_SND_RING=16u -DT7_SND_SYN_N=4u -DT7_BELL_WAKE -DT7_FR_ICONS -DSESS_ONCE -DSESS_AUTO -DPARA_BACK -DPARA_MB_KEEP -DBL_PWM -DBL_MR3RST -DT7_PW_FIX -DT7_LINK -DT7_IDLE -DT7_STOP -DT7_KEYQ -DT7_STOP_KEYACT=4000000u -DT7_TONE_Q -DT7_SESS_EESEQ -DT7_EE_SOON -DBAT_SHOW -DCLK_ECO -DECO_LAG=400 -DT7_WFI_FIX -DT7_EE_SHIFT -DECO_RTC -DT7_GAME_KEEP -DT7_CK_WRAP -DT7_CARE -DASK_NUM -DT7_JSR_HLE -DT7_DARK_STEP -DT7_PW -DT7_SND_LAZY -DT7_JIT -DT7_TBH_RATE=1515150u -DT7_VOL -DHW_VOL5 -DT7_FIXED -DT7_TONE_EV -DT7_TONE_ISR -DHW_TONE_IRQSAFE -DTONE_SAFE -DT7_WHEEL_RAW -DT7_PW_RTCWAIT -DT7_CATCHUP_SMOOTH -DBAT_SMOOTH -DT7_CLK_COLD -DBAT_GUARD -DBAT_PIC -DT7_SESS_KEEP -DT7_KQ_CAP -DPARA_FLASH -DPARA_FLASH_LATE -DT7X_ID=$XID -I../core $XCF"
O=""
for f in main.c t7app.c compat.c $E/hw.c $E/ask.c $E/flash.c $E/libc.c libgcc.c ../core/t7.c ../core/t7_cpu.c ../core/t7_memory.c ../core/t7_eeprom.c ../core/t7_sound.c ../core/figure_data.c ../core/t7_jit.c; do
  n=$(basename $f .c); OPT=-Os; [ "$n" = t7_cpu ] && OPT=${CPUOPT:--O2}; clang $CF $OPT -c $f -o $n.o; O="$O $n.o"; done
ld.lld -T link.ld --no-rosegment $O -o t7.elf
llvm-objcopy -O binary -j .text -j .data -j .init t7.elf t7.bin
llvm-objcopy -O binary -j .xcode t7.elf x.raw
python3 - "$XID" <<'PY'
import struct, sys
d = open('x.raw', 'rb').read(); d += b'\0' * (-len(d) % 4)
s = sum(struct.unpack('<%dI' % (len(d) // 4), d)) & 0xFFFFFFFF
open('../../t7x.bin', 'wb').write(struct.pack('<4I', 0x31583754, int(sys.argv[1], 16), len(d), s) + d)
PY
rm -f x.raw
cp t7.bin ../../t7.bin; rm -f *.o t7.bin; [ -n "$KEEPELF" ] || rm -f t7.elf; llvm-size ../../t7.bin 2>/dev/null; ls -l ../../t7.bin ../../t7x.bin
