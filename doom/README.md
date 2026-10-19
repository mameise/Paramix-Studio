# DOOM for the Tamagotchi Paradise

Only the program and the converter are here. No WAD and no game graphics.
Everyone brings their own WAD (Ultimate DOOM `DOOM.WAD` or shareware
`DOOM1.WAD`); the Studio reads it in the browser only.

- `doom_app.bin`, `doom_xip.bin`: DOOM for the Paradise (PRAM part and flash
  part). Source in `src/pdoom`, GPL-2.0-or-later. Based on nRF52840 Doom by
  next-hack, GBADoom by doomhack, PrBoom and the DOOM source by id Software.
- `mcuwad.wasm`: the WAD converter MCUDoomWadUtil by Nicola Wrachien (GPL-2.0),
  compiled to WebAssembly. Source in `src/wasm` (with a small C library
  `libc.c`); `src/umwandler` is the same converter as a command line program.
- The extra pictures of the GBA version (status bar 240x32, narrow digits, menu
  texts) are built from the user's WAD by `src/doom.js` (and
  `src/pdoom/tools/gbaextras.py`).

## Keys

The key layout is stored in the settings block PDMC of the program (version 2,
build switch `-DKEYMAP`): fire, forward, back, use (key or wheel press), hold key
for weapon change (hold + wheel) and sound (hold + wheel press), run with a
double tap on forward, wheel direction. The Studio sets it when building DOOM in
and can change it in a ROM that already has DOOM, without the WAD.
The wheel button is sampled every millisecond by the timer (from v0.4.1), so quick
taps between two frames are not lost and do not add up to the long press (2 s, back
to the Paradise). `src/pdoom/test/tasten_test.c` checks the key logic on a PC
(`cc -w tasten_test.c && ./a.out`).

## Build

```
cd src/pdoom && sh dev/build_dev.sh      # arm-none-eabi-gcc 13, newlib-nano
# -> dev/pram.bin (= doom_app.bin) and dev/xip.bin (= doom_xip.bin)

cd src/wasm
for f in libc wadfile wadprocessor conv; do
  clang --target=wasm32 -O2 -nostdlib -ffreestanding -Iinc -DKEEP_DP -DKEEP_MUS -c $f.c -o $f.o
done
wasm-ld --no-entry --export-dynamic -o mcuwad.wasm libc.o wadfile.o wadprocessor.o conv.o --export=__heap_base
```

The results are byte-identical to the files shipped here.

License: GNU GPL version 2 or later for DOOM (`LICENSE-GPL-2.0`), GNU GPL
version 2 for MCUDoomWadUtil.

Leaving DOOM (1.6.8, switch `-DPMXL_BACK` in `dev/build_dev.sh`) writes "PMXL" to the mailbox word 0x20000FFC like the
emulators, so the autostart of the stub keeps the Paradise after DOOM was left on purpose.
