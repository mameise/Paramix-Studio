#!/bin/sh
# Build six.bin (one 4 KB page, linked for XIP 0x100FE000 = flash 0x10F000)
set -e
cd "$(dirname "$0")"
clang --target=thumbv7m-none-eabi -mcpu=cortex-m3 -c six.s -o six.o
ld.lld -Ttext=0x100FE000 -e _start six.o -o six.elf
llvm-objcopy -O binary -j .text six.elf ../six.bin
llvm-objdump -d six.elf | head -80
ls -l ../six.bin
# Native Tama File helper, linked at zero so it can use any launcher page.
for book in book book-gen1 book-jade; do
  case "$book" in book) profile=0 ;; book-gen1) profile=1 ;; book-jade) profile=2 ;; esac
  clang --target=thumbv7m-none-eabi -mcpu=cortex-m3 -x assembler-with-cpp -DBOOK_PLATFORM=$profile -c book.s -o book.o
  ld.lld -Ttext=0 -e _start book.o -o book.elf
  llvm-objcopy -O binary -j .text book.elf "../$book.bin"
  chmod 644 "../$book.bin"
done
chmod 644 ../six.bin
