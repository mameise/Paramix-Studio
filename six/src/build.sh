#!/bin/sh
# Build six.bin (one 4 KB page, linked for XIP 0x100FE000 = flash 0x10F000)
set -e
cd "$(dirname "$0")"
clang --target=thumbv7m-none-eabi -mcpu=cortex-m3 -c six.s -o six.o
ld.lld -Ttext=0x100FE000 -e _start six.o -o six.elf
llvm-objcopy -O binary -j .text six.elf ../six.bin
llvm-objdump -d six.elf | head -80
ls -l ../six.bin
