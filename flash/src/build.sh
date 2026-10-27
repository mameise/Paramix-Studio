#!/bin/sh
# Builds the flash app (Cortex-M4, Thumb) into ../flashapp.bin.
# Needs clang, ld.lld and llvm-objcopy. The result is byte-identical to the
# flashapp.bin shipped in this folder.
set -e
cd "$(dirname "$0")"
clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -ffreestanding -nostdlib -Os -fno-common -DFW_PRECHECK -c fw.c -o /tmp/fw.o
ld.lld -T link.ld --no-rosegment /tmp/fw.o -o /tmp/fw.elf
llvm-objcopy -O binary /tmp/fw.elf ../flashapp.bin
echo "done: flashapp.bin"
