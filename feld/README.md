# Paramix menu (field switch, items, Tamapoints, Tama)

A small program for the Paradise, started by the launcher (A+C held on the home
screen). Texts on the device are English, the font and graphics are our own.

- `feld.bin`: the menu program, runs from PRAM.
- `stub_feld_tpl.bin`: the launcher with menu support and placeholders.
- `src/feld`: source of the menu, `src/stub/stub.S`: source of the launcher.

## What it does

- **Field switch:** the save of the active field is packed (LZSS per 4 KB page)
  into its own slot, the save of the chosen field is restored (or the factory
  settings pages 0xEFE000/0xEFF000 are written for a fresh field), then the
  device restarts. The settings pages are written last, so an interrupted switch
  is detected and the menu offers to restore the old save.
- The active field lives in the settings block (byte 439 on Land/Water/Sky, 443
  on Jade Forest, 499 on Tropical/Glacier), not in the ID page. Every edition has
  three fields; their names are texts 14 to 16 in the ROM.
- **Saves (1.6.8, flag 8, default):** the slots hold saves, not fields. The menu lists SAVE 1 to n
  (n = number of fields) with field and Tama; the number of the live save is in the settings block
  (0xF06, number + 1). An empty save starts a new game on a field the user picks. A Tama that moves
  to another field in the game stays the same save. Without flag 8 the menu works per field as before.
  A switch marks the header of the stored save with "PEND" (res[2]) until it is complete; only then
  does the menu offer to restore after an interruption.
- **Tama:** planet level, full care (hunger 6, happiness 20, not sick) and a new character (kid,
  young or adult of the ROM's character table). The character goes into the settings block and
  into the pet's own file (pages of the save with the old character in their header).
- **Items and Tamapoints:** edits the settings block 0xEFE000 (copy at 0xEFF000)
  and its CRC-16/ARC. The item catalog is built by the Studio from the user's
  own ROM (28 bytes per entry); pictures are drawn straight from the ROM's image
  table.

Settings block of the program (372 bytes at program + 0x140, version 4): slots,
ID page (footer, constant), fields (ID, name, colour), factory settings per
edition and field, positions of money, item list, counts, catalog. Version 4
adds fields 5 and 6, the set byte of six fields (see `six/README.md`) and a
second slot region. Version 5 (1.6.0, 492 bytes) adds the name of the launcher's
own program (380, 12 bytes), the number of further programs (392) and up to six
entries (396, 16 bytes each: XIP address of the 64 KB program image, name).
The menu starts them through the launcher entry at +0x0C (r0 = image). Version 6 (1.6.8, 500 bytes)
adds the TAMA menu (flag 4): offset of the pet (492) and of the planet level (496) in the settings
block; the characters come with the catalog (fixed byte 2, +6 stage, +7 field index + 1). The Studio fills it in (`src/feld.js`, profiles gen1,
jade, gen3).

**Start device (1.6.8, flag 16):** the Studio's "start device only" switch. The menu is the home screen:
it lists the programs and, as the last entry, DOWNLOAD (the Studio's flash app as its own 64 KB image,
settings version 7 at offset 500), so the PC can write the ROM again without the Paradise's own download
screen. C does nothing at the top; after two minutes without a key the display goes dark until the next
key. The Studio shrinks every picture of the game that the setup does not need (`launcherChanges` in
`src/core.js`). The handlers of the game's scenes (home, care, events, lab, shop, space) go through
trampolines behind the menu program: "ldr.w r12, original; b.w launcher + 0x2C". The launcher starts the
menu once the Paradise has a save (settings block 0xEFE000 stamped, year and month set), otherwise it
runs the original handler, so a setup after "reset" finishes in the Paradise itself. Measured in the
Paradise emulator: the four handlers 8 bytes behind a scene's name belong to the next scene of the table.
The launcher page holds at +0x400 "PLNC", the count and the offset of the trampoline pages (52 bytes per
scene: table offset, four original handlers, four trampolines); removing the menu restores the handlers.
Further programs may then also be TamaEmu (a colour Tama firmware) and T18Emu (one only), see `src/emu.js`
(`appsPlace`). With DOOM, DOOM is the program at 0x0A0000 and the emulators are further programs.
An entry SETTINGS (before DOWNLOAD, build switch `NO_LSET` removes it) changes the Paradise's sound level
(settings block byte 8, bits 3..4: 0 off .. 3 loud, found with the firmware's own sound screen) and the
display time of the menu (settings block byte 0xF07: 0 = 2 minutes, else index + 1 of 30 s, 1, 2, 5 min,
never). Both are written to 0xEFE000 and 0xEFF000 with the stamp. Programs that fit go behind the
Paradise's save (0xF00000..0xFFFFFF, never written by the Paradise) when there is no T18Emu.

Location: launcher 0x0B0000, program and catalog from 0x0B1000, slots behind the
character data. Tropical and Glacier still have firmware code there, so launcher
and program move to the first free page (0x0B6000). With DOOM, program, catalog
and slots use the free end of the DOOM program area. With six fields the six
slots lie behind the menu (up to 0x10F000, where the helper page starts) and
behind the character data.

## Build

```
sh src/feld/build.sh     # clang, ld.lld, llvm-objcopy -> src/feld/feld.bin (copy it to feld/feld.bin)
```

Launcher template: as in `emu/README.md`, plus `-DFELD=1 -DFELD_SRC=0x6789EF01`.
Without `-DFELD` the same source gives `emu/stub_tpl.bin`.

PC test without a device:

```
cd src/feld && cc -DHOST -w -I. host.c main.c -o menu_host
./menu_host <rom_with_menu.bin> <out.bin> <picture_dir> "<keys>"   # keys: n p a b c
```

License: GNU GPL version 3 or later.
