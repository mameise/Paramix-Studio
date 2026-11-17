#!/bin/sh
# Builds the flash app (Cortex-M4, Thumb) into ../flashapp.bin.
# Needs clang, ld.lld and llvm-objcopy. The result is byte-identical to the
# flashapp.bin shipped in this folder.
set -e
cd "$(dirname "$0")"
clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -ffreestanding -nostdlib -Os -fno-common -DFW_PRECHECK -c fw.c -o /tmp/fw.o
ld.lld -T link.ld --no-rosegment /tmp/fw.o -o /tmp/fw.elf
llvm-objcopy -O binary /tmp/fw.elf ../flashapp.bin
python3 - <<'CHECK'
from pathlib import Path
p=Path("../flashapp.bin")
if p.stat().st_size > 0xE000:
    raise SystemExit("flashapp.bin exceeds the rescue region (0xE000)")
print(f"done: flashapp.bin ({p.stat().st_size} bytes)")
CHECK
