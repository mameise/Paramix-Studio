# Wolfenstein 3D for the Tamagotchi Paradise

Only the program is here. No game data, no graphics, no sounds. Everyone brings their own files of Wolfenstein 3D:
the shareware (`.WL1`, episode 1) or the full version (`.WL6`, six episodes), version 1.4. The Studio reads them in
the browser only and builds one data pack for the flash (`src/wolf.js`).

- `wolf.bin`: the program. The first 64 KB go to the program memory (PRAM) when the Paramix menu starts it, the rest
  (code for the RAM at 0x20001000 and the start values of the data) the program copies from the flash itself.
- Source in `src`:
  - `w4`: the game from Wolf4SDL (KS-Presto, commit dc8b250), based on the Wolfenstein 3D source by id Software:
    actors, doors, player, ray casting. Changes are marked `PARAMIX`. GPL-2.0-or-later (`w4/license-gpl.txt`; the id
    license `w4/license-id.txt` is the alternative Wolf4SDL offers).
  - `pmx`: new for the Paradise: screen 128 x 128 (view 128 x 96, status bar 32), keys and wheel, sound on the buzzer
    (the game's PC speaker sounds), menus, end of floor, the pack, saving. `pmx_tables.h` is made by
    `tools/gen_tables.c` (the trig tables, so the program needs no floating point).
  - `dev`: the Paradise part (`paradise.c`, `link.ld`, `build.sh`).
  - `host`: the same game on the PC as a test runner (pictures as PPM, sounds as a list, keys from a script).
- `test/devwolf.py`: the program in a Cortex-M3 simulation (Unicorn), `test/mkimg.py` makes a test image.

## On the device

Started from the Paramix menu. Keys as DOOM on the Paradise (set in the Studio, tab Wolf3D): A fire, B forward,
C back, wheel turns, wheel press use (doors, switches), hold C + wheel: weapon, B twice quickly: run on/off.
Fixed: A+C menu (continue, new game, sound, back to the Paradise), wheel held 2 s: save and back to the Paradise.
At the next start "Continue" goes on where the game was left.

## Pack and flash

Pack "W3DP" (see `src/wolf.js`): walls and sprites as in VSWAP (without the digitized sounds), maps as in GAMEMAPS
(packed), the PC speaker sounds of AUDIOT, the pictures the program uses (unpacked, rows from the top). In the flash:
the pack (64 KB aligned; the cached window of 1 MB is put on it, anything behind the first MB is read through the
uncached window), then the program (128 KB), then 64 KB (4 KB settings, the saved game).

Settings block "PMXW" at 0x140: magic, version, pack, its length, save, flags (bit 0 English, bit 1 wheel the other
way round), name (16), keys fire, forward, back, use, hold key (masks A 1, B 2, C 4, wheel press 8), run with forward
twice, degrees per wheel step, 0, the program's own place.

## Build

```
cd src && sh dev/build.sh            # arm-none-eabi-gcc 13, newlib-nano -> ../wolf.bin
sh host/build.sh                     # gcc -> host/wolfhost (test runner)
gcc tools/gen_tables.c -lm && ./a.out > pmx/pmx_tables.h
```
