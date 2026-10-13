# TamaEmu for the Tamagotchi Paradise

An emulator for colour Tamagotchis (Epson S1C33) that runs on the Paradise.
The Studio builds it into the ROM together with the user's own firmware.

- `emu.bin`: the emulator program. It is copied to PRAM by the launcher and
  runs from there.
- `stub_tpl.bin`: the launcher (hooks on the home screen, A+C held) with
  placeholders that the Studio fills in.
- `src/core`: the emulator core, derived from
  [tamaemu](https://github.com/maragotchi/tamaemu) by maragotchi (GPL-3.0),
  with a JIT translator to Thumb-2 (`tc_jit.c`).
- `src/emu`: the Paradise side (display, keys, sound, flash, saving the state).
- `src/stub/stub.S`: the launcher.
- `src/tools`: Python versions of the ROM patching steps (for tests).

## Layout in the Paradise flash

| Offset | What |
| --- | --- |
| 0x0A0000 | emulator program (64 KB, copied to PRAM on start) |
| 0x0B0000 | launcher and home screen hooks |
| 0x0C0000 | run state (written by the emulator) |
| 0x940000 | guest firmware, packed in 64 KB blocks; blocks the guest writes stay unpacked and hold its save |

## Download content (8 MB firmware)

Stores and routing follow tamaemu `tools/dlc.c`: a record is a TAMAGO block,
routed by type (0x4E), category (0x50) and flag (0x51) into one of nine stores
(meals, snacks, clothing, accessories, items, daily items, wallpapers, games,
destinations). A new record goes into the first erased slot.

## Build

```
sh src/emu/build.sh      # clang, ld.lld, llvm-objcopy -> src/emu/emu.bin
```

Launcher template (link address 0x1009F000):

```
clang --target=thumbv7m-none-eabi -mcpu=cortex-m3 -x assembler-with-cpp -c src/stub/stub.S -o stub.o \
  -DHOOKS=1 -DLIMIT=90 -DAPP_SRC=0x1234ABCD -DORIG0=0x22222221 -DORIG1=0x33333331 -DORIG2=0x44444441 -DORIG3=0x55555551
ld.lld --no-rosegment -Ttext=0x1009F000 -e start stub.o -o stub.elf
llvm-objcopy -O binary stub.elf stub_tpl.bin
```

Both results are byte-identical to the files shipped here.

License: GNU GPL version 3 (see `LICENSE`).
