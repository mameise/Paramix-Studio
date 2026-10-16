# T18Emu for the Tamagotchi Paradise

An emulator for the Tamagotchi Meets and On (TG18 firmware, ARM7TDMI with the
GPEL3101A peripherals) that runs on the Paradise (Cortex-M3, 96 MHz). The
Studio builds it into the ROM together with the user's own 8 MB firmware. No
firmware is part of this repository.

- `t18.bin` and `t18x.bin`: the program. The TamaEmu launcher copies `t18.bin`
  (64 KB at most) to PRAM and starts it; `t18x.bin` is the code that is not hot,
  run from the flash window (flash 0x0F0000). Its settings block ("PMX8") is at
  0x140 and is filled in by the Studio (`src/emu.js`, `t18Patch`). Flag bit 1:
  set the clock to the Paradise's time at every start.
- `src/core`: the core from the T18Emu app by mameise, slimmed down for the
  Paradise:
  - `t18cpu.c`: ARM7TDMI interpreter. Guest SRAM (0xF8000000) is the Paradise
    SRAM, the firmware is reached through a map of 64 KB blocks. Idle rounds:
    a round of a loop that changes nothing (same registers, SRAM as before, only
    reads of registers that hold still until the next event) is watched once,
    then the further rounds up to the next event are skipped. The result is the
    same as without skipping, instruction by instruction.
  - `t18jit.c`: translator from ARM and Thumb code to Thumb-2 for the
    Cortex-M3. A block only runs when the cycle budget covers it, so interrupts
    come at the same instruction as in the interpreter; SRAM accesses are
    direct, the rest goes to the SoC. `t2enc.h` encodes the instructions.
  - `t18soc.c`: the SoC (timers, interrupts, RTC, SPI flash, DMA, LCD, tone).
- `src/app`: the Paradise side (`main.c`): screen, keys, tone, flash writes of
  the firmware (two 4 KB sectors held in RAM, blocks the firmware writes come
  from spare blocks), session, clock note.
- `host/ref.c`: the core on the PC as the reference (script commands wait,
  press, hash, shot, sram, flash, armprof, acnt).
- `test`: `m3main.c` with `m3run.py` runs the core with the translator in
  Unicorn on a Cortex-M3 with the memory of the Paradise (PRAM 64 KB, RAM at
  0x20000000 only 4 KB, writes into the PRAM only from the flash window) and compares with the reference (hash lines) and counts
  host instructions per emulated second. `devt18.py` runs the whole `t18.bin` on
  a simulated Paradise (screen pictures, keys, speed per second).

## Memory on the Paradise

The RAM at 0x20000000 has only 4 KB (mirrored 16 times), and they belong to the
Paradise firmware: nothing of T18Emu is there (1.5.0 put its data there, which
is why it hung on the device). Writing the PRAM while the CPU fetches code from
it corrupts instructions (known from TamaEmu), so the PRAM is only written by
`t18_pram_w16`, which writes whole words (1.6.2: a halfword write on the device
fills the other half of the word with the halfword written last) and runs from the SRAM (in `.data`, like the copy loop of
TamaEmu) with interrupts off. Without `T18_PRAM_WR_SRAM` it runs from the flash
window (1.5.1 to 1.5.3; E1 at the start on the device).

| Where | What |
| --- | --- |
| PRAM 0x00000000 (64 KB) | hot code; from `__jc_start` on (over the start code) the translated code |
| flash window 0x100DF000 (flash 0x0F0000) | the rest of the code (`.xip`), first word "T18X" and the build number |
| SRAM 0x18000000..0x18020000 | guest SRAM 0xF8000000 1:1, except for the hole |
| SRAM 0x1800E600..0x18014400 (the hole) | stack (2 KB, bottom), data, tables, pages for the guest's bytes in the hole |

The guest SRAM is the Paradise SRAM itself (host = guest + 0x20000000). All five
TG18 firmwares only zero 0xF800E600..0xF8014400 at the start and use a few
hundred bytes of it later (both stacks lie above it), so that range holds
T18Emu's own data. The guest's bytes there live in pages of 512 bytes, made on
the first non-zero write (8 pages; out of pages: red screen with code E2).
The translated code checks every SRAM access for the hole (two compares); the
stack pointer is used directly only above it (checked before each block that
uses it).

Errors show a red screen: E1 fault (two columns: PC LR, CFSR fault address, start
stage 1 to 8 and the firmware's PC, r0 r1, r2 r3, r12 xPSR, SP EXC_RETURN, HFSR and
the number of translated blocks with bit 31 set when the translator is off), E6
self test of PRAM code failed (the two results, expected 5A and A5), E2 out of
pages for the hole, E3 stack overflow, E4 the flash part is missing or from
another build, E5 state too big. The wheel goes back to the Paradise.

Holding B while T18Emu starts switches the translator off (interpreter only,
about 60 % speed, for tests on the device).

## Layout in the Paradise flash

| Offset | What |
| --- | --- |
| 0x0A0000 | program (copied to PRAM on start) |
| 0x0C0000 | block map as it is now ("T8MP": spares in use) |
| 0x0C1000 | clock note ("T8NT": seconds counter and game time when leaving) |
| 0x0C2000 | session ("T8SS": CPU and SoC state, then the 128 KB SRAM) |
| 0x0F0000 | the code part of the program (`t18x.bin`, up to 64 KB) |
| free 64 KB blocks, from 0xF00000 and from 0xD40000 downwards | the firmware's used blocks, then up to 10 spares; the bottom stays for the field saves |

## Build

```
sh src/app/build.sh            # arm-none-eabi-gcc; writes t18.bin and t18x.bin
KEEPELF=1 sh src/app/build.sh  # keeps src/app/t18.elf for devt18.py
```

## Tests

```
gcc -O2 -o ref host/ref.c src/core/t18soc.c src/core/t18cpu.c -lz
./ref fw_tg18.bin script.txt
gcc -O2 -DT18_HOLE_LO=0xE600u -DT18_HOLE_HI=0x14400u -o refh host/ref.c src/core/t18soc.c src/core/t18cpu.c -lz   # the hole on the PC
sh test/build_m3.sh /tmp/m3.elf -DT18_JIT -DT18_JC_LINKED -DT18_PRAM_WR -DJH_BITS=8 -DJT_BITS=8 -DJ_HOT=8
python3 test/m3run.py /tmp/m3.elf fw_tg18.bin script.txt   # same hash lines as ./ref
python3 test/devt18.py src/app/t18.elf paradise_with_t18.bin 30000   # PARA="2026-10-15 18:42:07": a Paradise clock in the SRAM
```
