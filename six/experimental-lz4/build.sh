#!/bin/sh
# Write only the requested output; intermediates stay in a temporary directory.
set -eu
if [ "$#" -lt 1 ] || [ "$#" -gt 2 ]; then
    echo "usage: sh build.sh /path/to/helper.bin [gen1|jade|gen3]" >&2
    exit 2
fi
case "$1" in /*) output=$1;; *) output="$(pwd)/$1";; esac
case ${2:-gen3} in
    gen1) family=1; enter=0x1006A288; dispatch=0x1006A2B8; ret=0x1006A316 ;;
    jade) family=2; enter=0x1006E120; dispatch=0x1006E150; ret=0x1006E1AE ;;
    gen3) family=0; enter=0x1007B9FA; dispatch=0x1007BA26; ret=0x1007BA34 ;;
    *) echo "unknown codec profile: $2" >&2; exit 2 ;;
esac
cd "$(dirname "$0")"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
clang --target=thumbv7m-none-eabi -DLEGACY_CODEC="$family" -c hook.S -o "$work/hook.o"
clang --target=thumbv7m-none-eabi -std=c11 -Wall -Wextra -Werror -Oz -ffreestanding \
    -fno-unwind-tables -fno-asynchronous-unwind-tables -c decode.c -o "$work/decode.o"
ld.lld --no-rosegment --defsym legacy_enter="$enter" --defsym legacy_dispatch="$dispatch" --defsym legacy_return="$ret" -T link.ld "$work/hook.o" "$work/decode.o" -o "$work/helper.elf"
llvm-objcopy -O binary -j .text "$work/helper.elf" "$output"
