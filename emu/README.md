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
| behind it, up to 0xD40000 | spare pages: a table (4 KB) and 8 KB pages for blocks that are empty in the firmware but written by the guest |
| 0x840000 to 0xD48000 | instead of the two rows above, for firmware that does not fit there (P's, 4U+, 4U); the menu's field saves then have no room |

## Writing the guest flash

The guest erases and programs its flash like the real chip. Parameter sectors
(8 KB) and, since v0.9.2, the 64 KB sectors are kept as 8 KB copies in RAM and
written back to the Paradise flash when the slots run out or the game is
quiet. Erasing a 64 KB sector erases the stored block right away. Blocks that
are empty in the firmware are not stored by the Studio; their written 8 KB
parts go into spare pages behind the packed firmware. The table (magic
"PMXP", checksum) is written after the page, so a power cut leaves either the
old or the new state.

Before v0.9.2 writes to 64 KB sectors were lost. The iD L then hung when it
saved something there, for example buying the mop in the Tamadepa (daily
items), and the long press saved that hung state.

Leaving with a long press does not save the session if the core stopped or
guest writes were lost. Pressing the wheel within the first 3 seconds after a
resumed start drops the saved session.

## Autostart

With "Start when switched on" the stub decides once per boot, at the first
home screen call. Mailbox word 0x20000FFC survives a restart and the reset
button but not the Paradise's deep sleep: "PMXL" (written by the emulator when
going back) keeps the Paradise, "PMXR" (written by the stub) means a restart
while running, so the emulator starts. Without either, the seconds counter
0x45000304 tells power-on (counter new) from waking up (counter ran). The
"decided" flag is the UsageFault priority byte, which every reset clears.

## Clock

Each millisecond of the Paradise runs the guest for one millisecond of its
current clock. Since v0.9.3 the remainder of that division is carried over
(at 32768 Hz the guest used to get 32 instead of 32.768 cycles per ms, its
clock lost 2.3 percent). About once a second the emulator also compares the
guest time with the seconds counter of the Paradise (0x45000304) and adds
whole seconds to the guest clock when it fell behind, for example when busy
scenes (shopping, flash writes) made the emulator drop time. Implausible jumps
of the counter are ignored.

At the start (settings version 4, flag bit 1, set by the Studio's "Take the
clock from the Paradise at the start"): the emulator reads the Paradise's own
time from the SRAM before it overwrites it (`paraclock.h`: the base of days and
seconds next to the six display fields, checked against the seconds counter)
and sets the guest's RTC registers to it, after a resumed session and on a
fresh start. Nothing is caught up. Without the flag, or when the time is not
found, a resumed session moves the clock on by the pause as before.

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

With `-DFELD=1` (menu launcher) the stub has a second entry at +0x0C
(`start_src`, 1.6.0): it copies the 64 KB image at the XIP address in r0 to
PRAM and restarts, like `start` does with its own program. The menu starts
further programs through it. Since 1.6.6 it reads from the uncached flash
(r0 + 0x50011000): the cached window shows only 1 MB. Build the menu launcher
with the same line plus `-DFELD=1 -DFELD_SRC=0x6789EF01 -DAUTO=0x66666661`.

License: GNU GPL version 3 (see `LICENSE`).

## Code that overwrites itself

The translator throws away all translated code when the guest writes to an
a0ram page that holds translated code. Since v0.9.6 a page that causes this 8
times within about 0.1 s of guest time is run by the interpreter only, for
about 5 s, then it gets another chance. Before, Connect to a Plus Color on the
iD L (code at 0xC00 next to its data) caused thousands of flushes per second
and the emulator stood still on the device. Build switch `JIT_WHOT`.
