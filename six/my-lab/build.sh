#!/bin/sh
set -eu
cd "$(dirname "$0")"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
cat > "$tmp/link.ld" <<'LD'
ENTRY(connection_selection)
SECTIONS {
    .text 0 : { *(.text.entry) *(.text*) *(.rodata*) }
    /DISCARD/ : { *(.ARM.exidx*) *(.ARM.extab*) }
}
LD
for firmware in 1 4 5; do
    "${CLANG:-clang}" --target=thumbv7m-none-eabi -Oz -ffreestanding -fpie -fno-unwind-tables \
        -fno-asynchronous-unwind-tables -DFIRMWARE="$firmware" -c release.c -o "$tmp/release.o"
    "${LLD:-ld.lld}" -T "$tmp/link.ld" "$tmp/release.o" -o "$tmp/release.elf"
    "${LLVM_OBJCOPY:-llvm-objcopy}" -O binary -j .text "$tmp/release.elf" "$tmp/$firmware.bin"
    "${LLVM_NM:-llvm-nm}" --defined-only "$tmp/release.elf" > "$tmp/$firmware.symbols"
done
python3 - "$tmp" <<'PY'
import json
import sys
from pathlib import Path

tmp = Path(sys.argv[1])
programs = {}
for firmware in (1, 4, 5):
    symbols = {}
    for line in (tmp / f"{firmware}.symbols").read_text().splitlines():
        value, kind, name = line.split()
        if kind == "T":
            symbols[name] = int(value, 16)
    payload = (tmp / f"{firmware}.bin").read_bytes().hex()
    programs[firmware] = {"hex": payload, "entries": symbols}
Path("helper.json").write_text(json.dumps(programs) + "\n")
PY
