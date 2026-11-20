#!/bin/sh
# Write only the requested output; intermediates stay in a temporary directory.
set -eu
if [ "$#" -ne 1 ]; then
    echo "usage: sh build.sh /path/to/helper.bin" >&2
    exit 2
fi
case "$1" in /*) output=$1;; *) output="$(pwd)/$1";; esac
cd "$(dirname "$0")"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
clang --target=thumbv7m-none-eabi -c hook.S -o "$work/hook.o"
clang --target=thumbv7m-none-eabi -std=c11 -Wall -Wextra -Werror -Oz -ffreestanding \
    -fno-unwind-tables -fno-asynchronous-unwind-tables -c decode.c -o "$work/decode.o"
ld.lld --no-rosegment -T link.ld "$work/hook.o" "$work/decode.o" -o "$work/helper.elf"
llvm-objcopy -O binary -j .text "$work/helper.elf" "$output"
