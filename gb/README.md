# GBEmu

The Game Boy (DMG, black and white) on the Tamagotchi Paradise, as a further program of the Paramix menu.
No ROM here: bring your own game (.gb). Game Boy Color games are refused by the Studio.

- `src/core/peanut_gb.h`: Peanut-GB by Mahyar Koshkouei (MIT License, notice in the file), commit d0bcca7.
  Changes for the Paradise: the ROM is read straight from the cached flash window (`PGB_ROM`, two places marked
  "Paramix"), `__gb_read` can take an attribute (`PGB_READ_ATTR`, unused).
- `src/stub`: three small headers so Peanut-GB builds without a C library.
- `src/app/main.c`: the Paradise program (PRAM, about 34 KB). Settings block "PMXG" at 0x140: ROM, length, save
  (4 KB settings of the device, then the cartridge RAM, up to 32 KB), flags (bit 0 English, bit 1 wheel the other
  way round), name, keys (9 bytes, see below).
- Screen: 160 x 144 shrunk to 128 x 115, every 5 x 5 dots mixed into 4 x 4 (weights 4/1, 3/2, 2/3, 1/4 both ways),
  so thin lines stay visible as a lighter shade; nothing is cut off. Black bars above and below. Every second frame
  is drawn (the display takes about 30 pictures a second), only the rows that changed are sent.
- Keys: `keys[i]` are Game Boy buttons (1 A, 2 B, 4 SELECT, 8 START, 16 RIGHT, 32 LEFT, 64 UP, 128 DOWN) for the
  Paradise input i: 0 A, 1 B, 2 C, 3 wheel press, 4 wheel up, 5 wheel down, 6 A+C, 7 B+C, 8 A+B. A pair that has a
  meaning takes its two keys while both are held. One wheel step holds up or down for 150 ms (steps add up). The
  wheel's encoder is read in the 1 ms tick, so no step is lost while a frame runs.
  Default (test37): A left, B = B, C right, wheel press = A, wheel up/down, A+C START. B+C and A+B are free, so B with
  a direction works (run). SELECT is a line in the settings (pressed for 200 ms when the menu closes).
  Fixed: A+B+C together = settings (select, colour, sound, light, dim, screen off, restart game, language); wheel held
  2 s = save and back to the Paradise.
- Save: the cartridge RAM goes to the flash 3 s after the game wrote it (1 s without a key), when the screen goes
  off and on leaving. With the screen off the game waits.
- Sound (switch GB_SOUND, test37): the Paradise has one square-wave buzzer. The core keeps the sound registers; a hook
  (`PGB_APU_WRITE`, marked "Paramix" in the core) sees triggers. Once per Game Boy frame the program follows length,
  volume envelope and the sweep of channel 1, and plays the louder of the two square channels (channel 1 first at the
  same loudness, it carries most effects). The wave channel (mostly the bass) only when both are quiet, the noise
  channel never. Setting "Sound" in the device's settings. TONE_SAFE (test38, hw.c): when the new period is below the
  running counter, the counter starts again (before, the buzzer stayed silent until the 32-bit counter wrapped).
  GB_TONE_RESTART (test39): every new pitch stops and starts the timer (on the device a pitch changed in the running
  timer stayed silent); changes below about 1.5 % (vibrato) are left out.
- Speed (test38): PGB_FAST_LINE draws background and window a tile at a time (`pgb_spread`, `pgb_pal4`, marked
  "Paramix" in the core), GB_FAST shrinks four dots at once (table `g_h5`), GB_SKIP_ADAPT leaves out up to three frames
  in a row when the game falls behind (normally every second frame is drawn). Same picture dot for dot.

- State (GB_STATE, 1.6.11 test2): on leaving (wheel held) the whole Game Boy goes to the flash behind the cartridge
  RAM (`emu/src/emu/pstate.h`), read once at the next start and dropped right after.
- Link cable (GB_LINK, 1.6.11 test2, `src/app/gb_link.h`): the serial port between two Paradise plugged together at the
  top (plink over the contacts, 460800 baud, frames 'G'). The clocking side sends its byte ('M') and keeps the transfer
  open (PGB_SERIAL_WAIT, marked "Paramix" in the core) until the other side's game is ready to receive and answers with
  its own byte ('R'); both clocking: 'B', both get 0xFF; no answer within 400 ms: 0xFF. Without a partner everything
  is as before. Tested: `test/link2.c` (two cores on the PC, a Pokemon trade from the Cable Club to the end), and two
  devgb.py joined by pipes (LINKW, LINKR) with `test/mklinkrom.py` (an own small program, 256 bytes both ways).

Speed (Unicorn simulation, 96 MHz): Super Mario Land and Kirby's Dream Land 2 at full speed (59.7 frames a second);
Blargg's cpu_instrs (no HALT at all) at about 75 %.

Build: `sh src/app/build.sh` (clang, lld) gives `gb.bin`.
Test: `python3 test/mkimg.py gb.bin game.gb img.bin` then `python3 test/devgb.py src/app/gb.elf img.bin <ms>`
(KEYS="t:mask,...", WHEEL="t:steps", SHOTS="t,...", OUT=dir); `test/prof.py` counts instructions per function.

Author: mameise
