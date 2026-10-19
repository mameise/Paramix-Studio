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
