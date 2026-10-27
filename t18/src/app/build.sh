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
REL="-DT18_HOTWIN -DT18_ROWSUM_RAM20 -DT18_JHDR -DT18_JH_SPLIT=0x20000100u -DT18_PRESENT_IDLE -DT18_PRESENT_STABLE -DT18_SUM_FNV -DT18_ROW_RR -DT18_PRES_FIRST -DT18_WAKE_ASYNC -DWAKE_ASYNC -DT18_SF_FAST -DT18_HOLE_FAST -DT18_TRY_SEQ -DT18_MEM_HELPER -DT18_FLUSH_IDLE -DT18_FF_FAST -DT18_IDLE_RUN -DT18_COMPACT_ON=40000000u -DT18_COMPACT_OFF=200000000u"
# 1.6.8: the switches of the test builds 1.6.7-test10..13 (measured on the device: egg 45 -> 63 %, call 56 -> 73 %)
REL="$REL -DT18_STM_SPLIT -DT18_SF_ASM -DT18_CHK_FLASH -DT18_CHK_HOLE -DT18_GFAIL_SHARED -DT18_SEQ_FAST -DT18_JT2 -DT18_SPCHK_LD -DT18_CYC32 -DT18_OVERSHOOT"
# 1.6.8 (test40): switches measured in the M3 test bed (egg -7 %, call -11 %, pet -5 %; same results as the reference)
REL="$REL -DT18_IO_FAST -DT18_SPCHK_MOD -DT18_TIERUP -DT18_IDLE_CAP=256 -DT18_IDLE_DISARM=32 -DT18_IDLE_PASS -DT18_XRET -DT18_CLOCK_AFTER -DPARA_BACK -DPARA_MB_KEEP -DT18_RADIO -DBL_PWM -DBL_MR3RST -DT18_TONE_Q -DT18_IDLE_MEMO -DT18_IDLE_BACKOFF=13 -DT18_JH_PROBE=16 -DT18_JT_MUL -DT18_SEC_SPARE -DT18_GROW -DT18_GROW2 -DT18_WAKE_START -DT18_DAY_FIX -DT18_LINK -DASK_EXTRA -DBAT_SHOW -DCLK_ECO -DECO_LAG=120 -DT18_WHEEL_WAKE -DT18_POOL_RECLAIM -DT18_CARE -DASK_NUM -DT18_PW -DT18_WHEEL_RAW -DT18_LOOP_XIP -DT18_KNOWN -DT18_SPEC -DT18_PW_RTCWAIT -DT18_PRESENT_CONFIRM -DT18_RAW_LABEL -DT18_CATCHUP_SMOOTH -DT18_PRESENT_S32 -DT18_CPU_SLOW=5 -DT18_CPU_ADAPT -DT18_IO_DIRECT -DT18_IO_FASTW -DT18_BL_STRICT -DASK_DIM -DASK_NEVER -DT18_DIM -DBAT_SMOOTH -DASK_LANG -DT18_LANG -DT18_ON_FULL -DT18_KEY_NOIDLE=300 -DT18_S32_EMU -DT18_S32_MS=72u -DBAT_GUARD -DBAT_PIC"
[ -n "$T18_TEST" ] || REL="$REL -DT18_QUIET_FATAL -DT18_NO_SPEED"
[ -n "$T18_TEST" ] && REL="$REL -DT18_TEST_KEYS"
[ -n "$T18_PLAIN" ] && REL=""
JHB=${JHB:-9}; T18_XIP_MORE=${T18_XIP_MORE-1}; JTB=${JTB:-9}; JHOT=${JHOT:-4}   # 1.6.8: as in the test builds 10..13
CF="-mcpu=cortex-m3 -mthumb -ffreestanding -nostdlib -fno-common -ffunction-sections -fdata-sections -Wall -Wno-unused-function -DPARADISE -DT18_JIT -DT18_JC_LINKED -DT18_PRAM_WR -DT18_PRAM_WR_SRAM -DJH_BITS=${JHB:-8} -DJT_BITS=${JTB} -DJ_HOT=${JHOT} -DT18_BUILD=${BUILD}u -DFAST_LCD -DEXIT_KEY -DLCD_FASTCLK -DTONE_FIX -DBL_SLEEP -DHW_OPTS -DSESS_ONCE -DSESS_AUTO -DOPT_MENU -I../core $REL $XCF"
O=""
for f in main.c $E/hw.c $E/ask.c $E/flash.c $E/libc.c ../core/t18cpu.c ../core/t18soc.c ../core/t18jit.c; do
  n=$(basename $f .c); OPT=-Os; [ "$n" = t18cpu ] && OPT=-O2; [ "$n" = libc ] && OPT="-Os -fno-builtin -fno-tree-loop-distribute-patterns"
  $CC $CF $OPT -c $f -o $n.o; O="$O $n.o"; done
# T18_XIP_MORE=1: rarely run code into the flash window as well (bigger code buffer)
XM_CPU=${XM_CPU:-.text.arm7_step}   # idle_try stays in the PRAM (1.6.6, T18_IDLE_RUN)
if [ -n "$T18_XIP_MORE" ]; then MORE='*ask.o(.text* .rodata*) *t18soc.o(.text.io_wr .text.io_rd .text.rq_push .text.rd32le .text.tg18_run_cycles .text.tmr_io_rd .text.tmr_io_wr) *t18cpu.o('"$XM_CPU"') *libgcc.a:_udivmoddi4.o(.text*)'; else MORE=''; fi
sed "s|XIPMORE|$MORE|;s|JTSIZE|$((8 << JTB))|" link.ld > link_gen.ld
$CC $CF -T link_gen.ld -Wl,--gc-sections -Wl,-Map=t18.map $O -lgcc -o t18.elf
# test43 fix: what runs before the flash window is set up (Reset_Handler up to hw_init) must be in the PRAM
for f in Reset_Handler para_clock para_mailbox para_leave_mark para_days hw_init; do
  a=$(arm-none-eabi-nm t18.elf | awk -v f=$f '$3==f || index($3, f".")==1 {print $1; exit}')
  [ -z "$a" ] || [ $((0x$a)) -lt $((0x10000000)) ] || { echo "build.sh: $f is in the flash window (0x$a), it runs before the window is set up"; exit 1; }
done
arm-none-eabi-objcopy -O binary -j .text -j .data -j .init t18.elf t18.bin
arm-none-eabi-objcopy -O binary -j .xip t18.elf t18x.bin
cp t18.bin ../../t18.bin; cp t18x.bin ../../t18x.bin; rm -f *.o t18.bin t18x.bin link_gen.ld; [ -n "$KEEPELF" ] || rm -f t18.elf t18.map
ls -l ../../t18.bin ../../t18x.bin
