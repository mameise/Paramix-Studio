# Notice: authors and licenses

Paramix Studio by mameise is licensed under the GNU General Public License,
version 3 or (at your option) any later version. The full text is in
[LICENSE](LICENSE).

Parts of this repository are based on the work of other people. Their
copyright and licenses stay in force; the files keep their original notices.

## TamaEmu (`emu/`)

- **tamaemu** by maragotchi, https://github.com/maragotchi/tamaemu,
  GNU GPL version 3 (copy in `emu/LICENSE` and `emu/src/core/LICENSE-tamaemu`).
  The emulator core in `emu/src/core` is derived from tamaemu. The layout of
  the download content stores and the routing of download records
  (`src/emu.js`, `emu/src/tools/dlc.py`) follow tamaemu `tools/dlc.c`.
- Changes for this project: a slim core for the Paradise without an operating
  system, the JIT translator from S1C33 to Thumb-2 (`tc_jit.c`), the Paradise
  host (`emu/src/emu`), the launcher stub and the ROM packing. Same license.
- The S1C33 instruction decoding follows the SLEIGH definition by GMMan,
  https://github.com/GMMan/s1c33_sleigh.

## T7Emu (`t7/`)

- The 6502 core in `t7/src/core` is a C port of the JavaScript emulator
  **tamago** by asterick, https://github.com/asterick/tamago, which its
  package declares under the BSD license. The port comes from the T7Emu app by
  mameise. The note in the source files stays.
- Changes for this project: a fast run loop (`t7_run.inc`), a build without C
  library, the Paradise host (`t7/src/app`) with screen, settings menu, clock
  and save. The 20x20 icons were drawn by mameise for the T7Emu ESP32 version.
- No ROM of the Tama-Go or of a figure is in this repository.

## T18Emu (`t18/`)

- The ARM7TDMI core and the TG18 SoC model in `t18/src/core` come from the
  T18Emu app by mameise.
- Changes for this project: a slim core for the Paradise, idle round skipping,
  the translator from ARM and Thumb to Thumb-2 (`t18jit.c`, `t2enc.h`) and the
  Paradise host (`t18/src/app`). It uses the Paradise hardware layer of TamaEmu
  (`emu/src/emu`). Same license as the rest of this repository.
- No firmware of the Meets or On is in this repository.

## DOOM (`doom/`)

- **DOOM** source code by id Software, released under the GNU GPL.
- **PrBoom** (based on BOOM, LxDoom and LSDLDoom), Copyright (C) 1999 id
  Software, Chi Hoang, Lee Killough, Jim Flynn, Rand Phares, Ty Halderman;
  (C) 1999-2000 Jess Haas, Nicolas Kalkhof, Colin Phipps, Florian Schulze;
  (C) 2005, 2006 Florian Schulze, Colin Phipps, Neil Stevens, Andrey Budko.
- **GBADoom** by doomhack, https://github.com/doomhack/GBADoom, with additions
  by Kippykip.
- **nRF52840 Doom** by Nicola Wrachien (next-hack),
  https://github.com/next-hack/nRF52840Doom.

  These are licensed under the GNU GPL version 2 or (at your option) any later
  version (copy of version 2 in `doom/LICENSE-GPL-2.0`). The Paradise port in
  `doom/src/pdoom/dev` is by mameise under the same terms.
- **MCUDoomWadUtil** by Nicola Wrachien, based on GbaWadUtil by doomhack,
  GNU GPL version 2. It is used as a separate program: compiled to WebAssembly
  (`doom/mcuwad.wasm`, source in `doom/src/wasm`, command line version in
  `doom/src/umwandler`) and only fed with the WAD file chosen by the user.
- **printf** by Marco Paland (PALANDesign Hannover), MIT License, notice in
  `doom/src/pdoom/plat/printf.c`.

No WAD file and no DOOM game graphics are part of this repository. The user
brings their own WAD; the Studio converts it in the browser.

## V1Emu (`v1/`)

- **TamaLIB** by Jean-Christophe Rona, https://github.com/jcrona/tamalib,
  GNU GPL version 2 or (at your option) any later version (copy in
  `v1/src/core/LICENSE_tamalib`). The core in `v1/src/core` is TamaLIB with
  changes for the Paradise (decode table, saving the CPU state, the Digimon's
  LCD, keys, buzzer and contact P20).
- The Paradise program (`v1/src/app`) and the icons are by mameise.
- No ROM of a Tamagotchi or Digimon is in this repository.

## V3Emu (`v3/`)

- **BrickEmuPy** by azya52, https://github.com/azya52/BrickEmuPy, released
  under **CC0 1.0 Universal** (public domain dedication: the author gave up all
  copyright, the work may be copied, changed and used for any purpose without
  asking and without credit). Credit is given here anyway.
  From it come: the Sunplus SPLB32 core (`cores/SPLB32.py`, the 256
  instructions translated to C by `v3/gen/py2c.py`, the rest by hand), the
  HT24LC08 EEPROM (`peripherals/HT24LC08.py`), the infrared byte link
  (`peripherals/CON_V3_IR.py`) and the positions of the 32 x 30 dots and the
  ten icon segments of the Connection V3 face (`assets/Tamagotchi_v3.svg`,
  only the positions, in `v3/src/core/v3lcd.inc`).
- The C port, the timer and halt speedups, the Paradise program
  (`v3/src/app`) and the icons are by mameise; with the rest of this
  repository under the GNU GPL version 3.
- No ROM of the Connection V3 is in this repository.

## GBEmu (`gb/`)

- **Peanut-GB** by Mahyar Koshkouei, https://github.com/deltabeard/Peanut-GB,
  **MIT License** (the notice is at the top of `gb/src/core/peanut_gb.h`).
  Parts of it come from SameBoy by Lior Halphon (MIT), marked in the file.
  Changed for the Paradise: the ROM is read straight from the flash window
  (marked "Paramix").
- The Paradise program (`gb/src/app`) is by mameise; with the rest of this
  repository under the GNU GPL version 3.
- No Game Boy game is in this repository.

## Wolfenstein 3D (`wolf/`)

- **Wolf4SDL** by Moritz "Ripper" Kroll and others, maintained by KS-Presto,
  https://github.com/KS-Presto/Wolf4SDL (commit dc8b250), based on the
  Wolfenstein 3D source code by **id Software** (1992). Wolf4SDL offers the
  original source license (`wolf/src/w4/license-id.txt`) or the **GNU GPL
  version 2 or later** (`wolf/src/w4/license-gpl.txt`); used here under the GPL.
  John Carmack confirmed the GPL relicensing of the Wolfenstein 3D source.
  The OPL emulators of Wolf4SDL (MAME license) are not used.
- Taken: the game parts (`wolf/src/w4`, changes marked `PARAMIX`). New for the
  Paradise (`wolf/src/pmx`, `wolf/src/dev`, `src/wolf.js`) by mameise; with the
  rest of this repository under the GNU GPL version 3.
- No game data of Wolfenstein 3D (maps, graphics, sounds) is in this repository.

## Formats and protocols

- File formats of the Paradise flash (archive, sprites, scenes):
  https://github.com/GMMan/tama-para-research by GMMan.
- The rest (settings block, ID pages, field slots, app loading and the PAU1
  flash protocol in `flash/`) was worked out for this project.

## Trademarks

Tamagotchi is a trademark of BANDAI. DOOM is a trademark of id Software. All
trademarks belong to their respective owners and are only named to describe
what this project works with. This project is not affiliated with, endorsed or
sponsored by them. It contains no firmware,
ROM, WAD or game graphics of theirs; you need your own devices and files.

- **TamaLIB** by Jean-Christophe Rona, https://github.com/jcrona/tamalib,
  GNU GPL version 2 or later (copy in `v1/src/core/LICENSE_tamalib`). V1Emu
  (`v1/src/core`) is TamaLIB with small changes for the Paradise (decode table,
  state as bytes, names of the hardware functions).
