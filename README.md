# Paramix Studio

A tool for the Tamagotchi Paradise that runs in your browser. Open a dump of the
device (the 16 MB flash image), change it, save a new ROM and write it back to
the device over a UART dongle.

- **Characters:** export every character as an image sheet, edit it in any paint
  program, upload it again.
- **TamaEmu:** build an emulator for colour Tamagotchis into the ROM. You bring
  your own firmware (4 MB or 8 MB). Download content (food, clothes, games ...)
  can be added for the 8 MB firmware.
- **DOOM:** build DOOM into the ROM from your own DOOM.WAD.
- **Menu:** an on-device menu to switch fields, put items in the bag and set
  Tamapoints.
- **Device:** read the ROM from the device and write a new one, only the parts
  that changed, keeping the saves on the device.

Everything runs locally in one HTML file. Your ROM, firmware and WAD never leave
your computer. The Studio contains no ROM, no firmware, no WAD and no game
graphics; you need your own.

## Disclaimer

Use this tool at your own risk. Changing a ROM and writing it to a device can
make the device unusable or erase its saves, and something can always go wrong
(a loose cable, an empty battery, a bug). Read a backup of your device first and
only use the tool if you know what you are doing.

This software is provided "as is", without warranty of any kind. The authors
are not liable for any damage to devices, data or anything else that results
from using it (see sections 15 and 16 of the [LICENSE](LICENSE)).

## Trademarks

Tamagotchi is a trademark of BANDAI. DOOM is a trademark of id Software. All
trademarks and product names belong to their respective owners. They are only
named to describe what this tool works with. This is not an official product
and is not affiliated with, endorsed or sponsored by any of these companies.
The repository contains no firmware, ROM, WAD or game graphics of theirs; use
your own devices and your own files.

## Use

1. Download `ParamixStudio-vX.Y.Z.html` from the
   [Releases](../../releases) and open it in Chrome or Edge (or open the GitHub
   Pages site of this repository).
2. Open a ROM. Recognised editions: Land, Water, Sky, Jade Forest, Tropical,
   Glacier. No dump yet? Read one with the Device button (see below).
3. Make your changes in the tabs. "Your new ROM" at the top shows what the new
   ROM will contain and how full the flash is.
4. "Save new ROM" downloads the new ROM, or write it straight to the device.

### Characters
Click a character, download its sheet, edit it (keep the size), upload it.
Extras: all sheets as ZIP, upload many sheets at once, take characters from the
image of a character card, number test ROM (each character shows its number).

Rules the tool keeps on its own: eye images keep their byte size, new colours
use palette slots 8 to 15 (outline slot 1, slots 2 to 7 are recoloured by the
game), archive checksums are recalculated and every changed image is read back
before saving.

### Fields
Every edition carries complete character sets for several field types: Land,
Water and Sky ROMs have all three, Jade Forest also has Sky, Tropical and
Glacier carry all six. Each field has the same structure: 1 child, 4 teens,
17 adults. In the Characters tab, "Field" shows only one field's characters.
"Field as ZIP" exports everything of that field (characters, field map,
close-up backgrounds) for editing; upload the edited sheets as usual.
"Load field from ZIP" puts a field from the same or another ROM into the
selected field. Eye images must not get bigger than the original ones, so
characters whose eyes do not fit stay original.

### TamaEmu
Tab "Emulator": choose the firmware of a colour Tamagotchi (4 MB or 8 MB image).
It is packed into the ROM on save. On the device: hold A and C on the home
screen to start, hold the wheel to go back (the state is saved and continues
next time). To start over from the Tamagotchi's own save instead, release A
and C and press the wheel within the first 3 seconds: the saved session is
dropped and you are back on the Paradise; the next start begins fresh.

The guest may write anywhere in its flash (item stores, shop purchases, its
own reset). Written parts of blocks that are empty in the firmware go into
spare pages behind the packed firmware (up to 0xD40000).

With the 8 MB firmware the tab lists the download content stores (meals,
snacks, clothing, accessories, items, daily items, wallpapers, games,
destinations). Add `.jpg` download files (several at once), remove entries per
slot.

### DOOM
Tab "DOOM": choose your own `DOOM.WAD` (Ultimate DOOM or the shareware
`DOOM1.WAD`) and an episode. The WAD is converted in the browser. Keys can be
changed, also for a ROM that already has DOOM. Start with A and C like the
emulator. Only one of TamaEmu and DOOM fits into the ROM.

### Menu (fields, items, Tamapoints)
Tab "Menu": tick "Field switch" and/or "Items and Tamapoints". On the device,
hold A and C on the home screen: wheel selects, B confirms, C goes back.

- Field switch: every edition has three fields (Land/Water/Sky, Jade Forest:
  Land/Water/Forest, Tropical and Glacier: Tropical/Glacier/Forest). Switching
  packs the save of the current field into its own slot and brings back the save
  of the chosen one. A field that never ran starts like a new device.
- All 6 fields (Tropical and Glacier): tick "All 6 fields" and choose your
  own Land, Water or Sky ROM. The menu then offers Tropical, Glacier, Forest,
  Land, Water and Sky. The pictures of Land, Water and Sky are taken from that
  ROM; characters of all six fields are already in Tropical/Glacier.
- Sky as 4th field (Jade Forest): tick "Sky as 4th field". Jade Forest
  already has everything of Sky, no second ROM is needed.
- Neither works together with DOOM or the emulator (these editions have no
  room for them).
- Items: put items into the bag and set their amount, with picture. The list
  and the pictures come from your own ROM.
- Tamapoints: set digit by digit.
- TamaEmu or DOOM, when built in, is the last entry of the same menu.

### Device (read and write over UART)
Needs a USB UART dongle with 3.3 V (CH340 or similar) and Chrome or Edge on a
computer (Web Serial).

Wiring to the three contacts of the Paradise: left = RX of the dongle,
middle = GND, right = TX of the dongle. 3.3 V, not 5 V.

1. On the Paradise open the download screen: Planet, A, Connect, Download,
   Download.
2. Click "Device", then "Connect" and pick the port. The Studio loads its own
   flash app into the device's RAM (nothing is written for this; switching the
   device off and on returns to normal).
3. "Read ROM" reads all 16 MB (about 6 minutes), downloads the file as a backup
   and opens it in the Studio.
4. "Write new ROM to the device" writes the ROM you put together.
   - "Only write changes": the device's flash is compared first (CRC per 64 KB,
     then per 4 KB) and only differing sectors are written. A typical change
     takes about a minute instead of 40.
   - "Keep saves": the game progress stays as it is on the device: the save area
     of the Paradise, stored fields of the menu, the TamaEmu state and its guest
     save, the DOOM save. The rest of the new ROM is written. To change the
     edition (for example Glacier onto a Land), untick it.
   Keep the device switched on while writing. Every sector is checked after it
   is written and the whole flash is checked at the end. If something goes
   wrong, leave the device on and write again.

## Build

```
node build.js          # creates dist/ParamixStudio.html
node test/unit_test.js # checks the core without a ROM
```

The other tests in `test/` need your own dumps and are run by hand (see the
first lines of each file). `test/flash_bridge_test.js` and
`test/ui_device_test.js` run the flasher against an emulated Paradise.

GitHub Actions builds the HTML on every push and attaches it to a release named
after `VERSION` (see `.github/workflows/paramix-studio.yml`). Optional: Settings,
Pages, Source "GitHub Actions" publishes the page as a website as well.

## Repository layout

| Folder | What |
| --- | --- |
| `src/` | the Studio (HTML, JavaScript) |
| `emu/` | TamaEmu, the emulator program for the Paradise, with source in `emu/src` |
| `doom/` | DOOM for the Paradise and the WAD converter (WebAssembly), with source in `doom/src` |
| `feld/` | the on-device menu (field switch, items, Tamapoints), with source in `feld/src` |
| `six/` | helper code for six fields on Tropical/Glacier, with source in `six/src` |
| `flash/` | the flash app loaded for reading and writing over UART, with source in `flash/src` |
| `test/` | tests |

Every shipped binary (`emu/emu.bin`, `emu/stub_tpl.bin`, `doom/*.bin`,
`doom/mcuwad.wasm`, `feld/*.bin`, `six/six.bin`, `flash/flashapp.bin`) is built from the source
next to it; the build commands are in the README of each folder.

## Credits and license

Paramix Studio is free software under the GNU General Public License version 3
or later, see [LICENSE](LICENSE). It builds on the work of others, see
[NOTICE.md](NOTICE.md) for the full list:

- **TamaEmu** is based on [tamaemu](https://github.com/maragotchi/tamaemu) by
  maragotchi (GPL-3.0). The JIT and the Paradise port were written for this
  project; the emulator core and the download content layout come from tamaemu.
- **DOOM** is based on nRF52840 Doom by next-hack, GBADoom by doomhack, PrBoom
  and the DOOM source by id Software (GPL-2.0-or-later). The WAD converter is
  MCUDoomWadUtil by Nicola Wrachien (GPL-2.0).
- File format research: [tama-para-research](https://github.com/GMMan/tama-para-research)
  and the S1C33 SLEIGH definition by GMMan.

Made by mameise.
! Supported by AI !
