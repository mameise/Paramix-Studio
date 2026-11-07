# PokeMiniEmu

The Pokemon mini on the Tamagotchi Paradise, as a further program of the Paramix menu.
No game and no BIOS here: bring your own games (.min). Without an own BIOS the free BIOS of PokeMini runs.

- `src/core`: PokeMini by JustBurn (GPL 3 or later, `LICENSE_pokemini`), libretro version, commit 132111b. Taken:
  the CPU (MinxCPU*), PRC (picture chip), LCD, timers, IRQ, I/O (keys, EEPROM, RTC), sound registers, Hardware.c,
  FreeBIOS. `pm_glue.c` replaces PokeMini.c and PMCommon.c (no files, no colour PRC, no multicart): create, reset, the
  cartridge in the flash window (`pm_set_rom`), the clock (`pm_set_time`), the state in a buffer (`pm_state_save`,
  `pm_state_load`, PokeMini's sections without the colour part). Changes in the core files are marked "PokeMiniEmu":
  `MinxLCD.c` without Video.h, `Hardware.c` with `PM_SYNCCYCLES` and `PM_FASTLCD`, `MinxAudio.c` with `pm_audio_now`.
- `src/stub`: the few C library headers the core needs, and a small memory stream.
- `src/app/main.c`: the Paradise program (PRAM, about 62 KB). Settings block "PMXP" at 0x140: game, length, save
  (64 KB: 4 KB settings of the device, 8 KB EEPROM, the state up to 36 KB, an own BIOS at +0xC000), flags (bit 0
  English, bit 1 wheel the other way round, bit 2 own BIOS), name, keys (12 x u16, see below), place of the BIOS.
- Screen: 96 x 64 to 128 x 85 from y 21 (3 dots to 4: a, (a+2b)/3, (2b+c)/3, c, both ways). PM_FASTLCD: the program
  reads the LCD memory itself for every second frame and mixes it with the frame before (dot on in one of the two:
  grey), the core's per-dot buffers and the analog decay are left out (18 % of the time). The contrast of the game
  sets the two shades.
- Keys: `keys[i]` are Pokemon mini buttons (1 A, 2 B, 4 C, 8 up, 16 down, 32 left, 64 right, 128 power, 256 shake)
  for the Paradise input i: 0 A, 1 B, 2 C, 3 wheel press, 4 wheel up, 5 wheel down, 6 A+C, 7 B+C, 8 A+B. Default: A
  left, B = B, C right, wheel press = A, wheel up/down, A+C = C, A+B shake. Fixed: A+B+C settings (power, colour,
  sound, light, dim, screen off, restart game, language); wheel held 2 s = save and back to the Paradise.
- Sound: timer 3 of the Pokemon mini drives its piezo; its frequency (and silence at volume 0 or a pulse of 0 or
  100 %) goes to the Paradise's buzzer once per frame.
- Save: the EEPROM goes to the flash 3 s after the game wrote it (1 s without a key), when the screen goes off and on
  leaving. On leaving the whole state too (`emu/src/emu/pstate.h`), read once at the next start.
- Clock: a new start sets the Pokemon mini's clock from the Paradise's time (the games still ask for their own time
  once). The seconds counter keeps running while the game waits (settings, screen off, back in the Paradise): the
  seconds of the Paradise's counter are added when it goes on.
- Speed (Unicorn simulation, 96 MHz): all 13 tried games at full speed (72 frames a second), 3 to 10 ms per frame.

Build: `sh src/app/build.sh` (clang, lld) gives `pm.bin`.
Test: `python3 test/mkimg.py pm.bin game.min img.bin [flags] [bios.min]` then
`python3 test/devpm.py src/app/pm.elf img.bin <ms>` (KEYS="t:mask,...", SHOTS="t,...", OUT=dir); `test/prof.py`
counts instructions per function.

Author: mameise

No session (1.7.3 test7, switch SESS_FLAG): flags bit 12 set by the Studio ("Remember where you left off" off): leaving writes
no state, a start drops an old one; only the game's own save (cartridge RAM or EEPROM) is kept.
