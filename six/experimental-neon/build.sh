#!/bin/sh
set -eu
if [ "$#" -ne 1 ]; then
    echo "usage: sh build.sh /path/to/growth.bin" >&2
    exit 2
fi
case "$1" in /*) output=$1;; *) output="$(pwd)/$1";; esac
cd "$(dirname "$0")"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
clang --target=thumbv7m-none-eabi -c growth.S -o "$work/hook.o"
clang --target=thumbv7m-none-eabi -std=c11 -Wall -Wextra -Werror -Oz -ffreestanding \
    -fno-unwind-tables -fno-asynchronous-unwind-tables -c growth.c -o "$work/growth.o"
ld.lld --no-rosegment -T link.ld "$work/hook.o" "$work/growth.o" -o "$work/growth.elf"
llvm-objcopy -O binary -j .text "$work/growth.elf" "$output"
