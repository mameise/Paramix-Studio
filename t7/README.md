# T7Emu for the Tamagotchi Paradise

An emulator for the Tama-Go (6502 based V7 chip) that runs on the Paradise. It also runs the
Tamagotchi Friends (same chip family, test).
The Studio builds it into the ROM together with the user's own Tama-Go ROM and
figure ROMs. No ROM is part of this repository.

- `t7.bin`: the program. The TamaEmu launcher copies it to PRAM and starts it.
  Its settings block ("PMX7") is at 0x140 and is filled in by the Studio
  (`src/emu.js`, `t7Patch`).
- `src/core`: the emulator core, a C port of
  [tamago](https://github.com/asterick/tamago) by asterick (BSD), taken over
  from the T7Emu app. `t7_run.inc` is a faster run loop with the same results
  as `t7_cpu_step` (kept for the host tests).
- `src/app`: the Paradise side. `t7app.c` has screen, settings menu, clock
  and saved state and also builds on the PC; `main.c` has the hardware part
  (it uses `emu/src/emu/hw.c` and `flash.c`).
- `host`: PC tests. `hrun.c` boots the ROM and saves screenshots, `happ.c`
  tests the app logic (clock, catching up, save, figures), `runcmp.c` and
  `tr.c` compare the fast loop with the old step function. `setup.txt` is the
  key sequence that finishes the first setup and hatches the egg.
- `test`: `devt7.py` runs `t7.bin` in Unicorn with a simulated Paradise,
  `proft7.py` profiles it.

## Build

```
sh src/app/build.sh          # clang, ld.lld, llvm-objcopy; writes t7.bin
KEEPELF=1 sh src/app/build.sh   # keeps src/app/t7.elf for the profiler
```

## Layout in the Paradise flash

| Offset | What |
| --- | --- |
| 0x0A0000 | program (copied to PRAM on start) |
| 0x0C0000 | session (state of the running emulator, "T7SS") |
| 0x0C1000 | EEPROM of the Tama-Go, two slots of 8 KB ("T7EE", sequence, page mask, checksum, then 4 KB) |
| 0x0C5000 | settings from the menu ("T7S2", 16 bytes) and at +32 the clock note ("T7CK": seconds counter, game time, month and day when leaving) |
| 0x940000 | Tama-Go ROM (640 KB) or Tamagotchi Friends ROM (768 KB) |
| behind it, up to 0xD40000 | figure ROMs, trailing empty bytes cut, each on a 4 KB boundary |

The save.bin of the T7Emu app (page mask, then the 4 KB EEPROM, 4098 bytes)
has the same content as one EEPROM slot, see `src/saves.js`.

## More instances (1.6.0)

The program only uses its settings block for ROM, figures and save area, so the
Studio can put several copies into the flash (one per ROM, see the main README,
"More emulators"). `test/devt7.py` runs one of them with `APPOFF=<image offset>`.

## Tamagotchi Friends

The Friends ROM (768 KB) is recognised by its reset vector: not at 0x3FFC like the Tama-Go but at
0x7FFC. Differences, all in `src/core` behind `g_t7_static_off`:

- The file has 16 KB more in front. The static ROM ($C000) is at 0x4000, bank 0 begins at 0x4000
  (its code is linked to $8000), bank n >= 1 at (n + 1) * 32 KB. 23 banks.
- The frame tick comes from IRQ 3 (every 122nd, counter $55) instead of the video IRQ. Instead of
  7.3 kHz of interrupts the core sets the counter just below and fires IRQ 3 once per video frame.
- The second of the game clock is at 0x136 (Tama-Go 0x396); hour, minute, day and month as on the
  Tama-Go.
- The IR hooks of the Tama-Go (bank 3) are off. No figures.

## Clock

Settings flag bit 1 (the Studio's "Take the clock from the Paradise at the
start", on by default): at every start the game clock is set to the Paradise's
own time, read from its SRAM before T7Emu uses it (`../emu/src/emu/paraclock.h`).
Nothing is caught up. When the game asks for the time (hour 255) it gets the
same time. Without the flag (or with the clock set to "Game" in the menu, or
when the time is not found) the old way: after a pause the missed hours pass,
up to 30.

## Timing

The core runs at 3.03 MHz with a video interrupt every 66,667 cycles (60 Hz).
The game clock is kept on the Paradise's seconds counter: every 2 s the app
sets minute and second; when a whole hour is missing it lets the game roll
the hour itself, so the day goes on as in the game. After a pause up to 30
hours are caught up, then the hour is set directly.
