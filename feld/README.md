# Paramix menu (field switch, items, Tamapoints)

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
The menu starts them through the launcher entry at +0x0C (r0 = image). The Studio fills it in (`src/feld.js`, profiles gen1,
jade, gen3).

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
