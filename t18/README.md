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
  the firmware (two 4 KB sectors held in RAM; empty blocks the firmware writes
  get 4 KB sectors from the spare blocks, `T18_SEC_SPARE`), session, clock note.
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
| 0x0C0000 | block map as it is now ("T8MP": spares in use; at +0x144 "T8SS": the sector table of `T18_SEC_SPARE`) |
| 0x0C1000 | clock note ("T8NT": seconds counter and game time when leaving) |
| 0x0C2000 | session ("T8SS": CPU and SoC state, then the 128 KB SRAM) |
| 0x0F0000 | the code part of the program (`t18x.bin`, up to 64 KB) |
| free 64 KB blocks, from 0xF00000 and from 0xD40000 downwards | the firmware's used blocks, then up to 10 spares; the bottom stays for the field saves |

## Build

```
sh src/app/build.sh            # arm-none-eabi-gcc; writes t18.bin and t18x.bin
KEEPELF=1 sh src/app/build.sh  # keeps src/app/t18.elf for devt18.py
```

## Speed switches (1.6.8)

build.sh sets them (REL). Each one keeps the results bit for bit as the reference (checked without T18_OVERSHOOT):
- T18_IO_FAST: the device registers the firmware polls most (timer control, buttons, interrupt vector and mask) are
  read straight from the translator's slow path, without io_rd in the flash window.
- T18_SPCHK_MOD: only blocks that move sp check it at their start (push, pop, add/sub sp; add/mov sp, rX is left
  to the interpreter).
- T18_TIERUP: SysTick notes the interrupted pc in the code buffer; a block hit three times while it uses the shared
  check routine is translated again with the checks inline.
- T18_IDLE_CAP, T18_IDLE_DISARM, T18_IDLE_PASS: idle tries away from the known idle pc stop after 256 steps; the
  known idle pc is given up after 32 failed tries in a row; once tried in a run it no longer stops the translated code.
- T18_XRET: a conditional return from an exception (ldmcc ..., {..., pc}^) no longer splits the interrupt handler.

- T18_CLOCK_AFTER: when the firmware sets its battery clock (first setup), the Paradise's time is applied again
  500 ms later.
- T18_RADIO: the Bluetooth module of the Meets/On (SPI 0xC0080000, chip select GPIO 0xC0000064 bit 12) answers
  like the real one without a phone (register 0x0A boot/patch/running, register 0x08 bytes waiting, setup
  answers, module memory reads, HCI Command Complete). Without it the camera (item box, Special) stayed at READY?
  and took no key. About 300 bytes in the SoC struct, outside the saved state.

- T18_TONE_Q: the tone in real time. Tone changes are queued with the game time and played on the Paradise's
  clock 300 ms after the first of a sequence; below 100 % a melody keeps its tempo (late by more than 200 ms:
  timing starts anew).
- T18_IDLE_MEMO: the last skippable idle round at the known idle pc is kept with every value it read (memory and
  registers that hold still); the same start and the same values now skip it without stepping (living room:
  ~900 rounds a second were single-stepped). The guest flash counts as one value (t18_flash_gen).
- T18_IDLE_BACKOFF=13: tries away from the idle pc back off up to 2000 << 13 master ticks after failing.
- T18_JH_PROBE=16: the block table searches 16 slots (with 8, busy keys pushed each other out before they got hot
  and were interpreted every time).
- T18_JT_MUL: multiplicative hash for the indirect jump cache (the old one put 4 of the 10 busiest return
  addresses of the egg scene into one set).
- test45 T18_SEC_SPARE: spares by the 4 KB sector for empty guest blocks (tables in the Paradise RAM, saved as "T8SS" in
  the map sector). With 6 spare blocks the old way lost the 7th block the firmware writes (photo, diary).
- test44: RM_N (48) and W_LOG (32, was 64) can be set with -D; TQ_N 32. With 64 the build no longer fit the hole.
  Code that runs before the flash window is set up (Reset_Handler up to hw_init) must stay in the PRAM;
  build.sh checks it.
M3 (Minstr per guest second, plus uncached flash reads at 96 each): egg 121.7 -> 98.7, call 84.5 -> 80.5,
living room 75.8 -> 55.6. Exact as before (suite 20 of 20, without T18_OVERSHOOT).

## Tests

```
gcc -O2 -o ref host/ref.c src/core/t18soc.c src/core/t18cpu.c -lz
./ref fw_tg18.bin script.txt
gcc -O2 -DT18_HOLE_LO=0xE600u -DT18_HOLE_HI=0x14400u -o refh host/ref.c src/core/t18soc.c src/core/t18cpu.c -lz   # the hole on the PC
sh test/build_m3.sh /tmp/m3.elf -DT18_JIT -DT18_JC_LINKED -DT18_PRAM_WR -DJH_BITS=8 -DJT_BITS=8 -DJ_HOT=8
python3 test/m3run.py /tmp/m3.elf fw_tg18.bin script.txt   # same hash lines as ./ref
python3 test/devt18.py src/app/t18.elf paradise_with_t18.bin 30000   # PARA="2026-10-15 18:42:07": a Paradise clock in the SRAM
```
