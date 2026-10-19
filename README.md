# Paramix Studio

A tool for the Tamagotchi Paradise that runs in your browser. Open a dump of the
device (the 16 MB flash image), change it, save a new ROM and write it back to
the device over a UART dongle.

- **TamaEmu:** build an emulator for colour Tamagotchis into the ROM (Plus Color,
  iD L, P's, 4U+, 4U). You bring your own firmware (4 MB or 8 MB). Download
  content (food, clothes, games ...) can be added for the 8 MB firmware.
  Optionally starts when the Paradise is switched on.
- **T7Emu:** the Tama-Go with its figures (also the full figure dumps) and the
  Tamagotchi Friends, from your own ROMs.
- **T18Emu:** the Tamagotchi Meets / On (TG18 firmware).
- **V1Emu (test):** the first Tamagotchis (P1, P2, Angel ...) and the Digital
  Monster, from your own ROMs; small enough for many of them on one ROM.
- **More emulators:** up to twelve more programs next to the main one, started
  from the on-device menu.
- **Start device:** turn the Paradise into a device for programs only: after
  the setup (language, clock) a menu with your emulators and DOOM comes up,
  with its own settings (sound, display time) and a DOWNLOAD entry for the
  PC. All pictures of the game go, which gives about 5 MB more room.
- **DOOM:** build DOOM into the ROM from your own DOOM.WAD.
- **More fields:** Land, Water and Sky on Tropical and Glacier, Sky on Jade
  Forest, with pictures, special characters and the matching planet.
- **Menu:** an on-device menu with several saves (each on a field of your
  choice), items in the bag, Tamapoints and changes to the Tama (level, care,
  character).
- **Save editor:** change level, character and care of a Paradise save
  (a .tamasave or the save inside the loaded ROM).
- **Characters:** export every character as an image sheet, edit it in any paint
  program, upload it again. Digimon from DIM cards too.
- **Device:** read the ROM from the device and write a new one, only the parts
  that changed, keeping the saves on the device.
- **Save games:** back up the Paradise and the TamaEmu save on their own and
  restore them, for example to play on in the TamaParadise or TamaEmu app.

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
Known: Plus Color, iD L, P's, 4U+ and 4U. The 4U and 4U+ firmware keeps asking
its NFC chip, which the Paradise does not have; TamaEmu answers in its place,
so the menus react to a single press ("4U: emulate the NFC chip", on by
default). The P's and 4U firmware is bigger and
uses the room the menu needs for the field switch, so the two do not go together.
It is packed into the ROM on save. On the device: hold A and C on the home
screen to start, hold the wheel to go back (the state is saved and continues
next time). To start over from the Tamagotchi's own save instead, release A
and C and press the wheel within the first 3 seconds: the saved session is
dropped and you are back on the Paradise; the next start begins fresh.

Battery change (1.6.8): the emulators no longer need the wheel to keep the game. Every emulator writes
its run state by itself when the screen goes dark (10 minutes after the last time at the earliest) and
otherwise every 30 minutes after 10 s without a key (V1Emu: every 15 minutes). A session is used once:
after it has been resumed it is dropped, so an old one never wins over newer play. Switches SESS_ONCE
and SESS_AUTO in the build scripts (SESS_T1/SESS_T2 for shorter test times).

TamaEmu and T18Emu settings (1.6.8): volume (normal, quiet, loud, off), brightness (5, 10, 20 .. 90, 100 %),
screen off after (as the toy, 10 s to 10 min; the first key only wakes). In the Studio under ▾ in the
program's row (flags bits 16-22 and 24-27, switch HW_OPTS), and on the device with a short press on the wheel
(A next line, B change, C close; switch OPT_MENU). A change on the device is kept in the save area
(TamaEmu: its last sector, T18Emu: behind the session) and applies until the Studio's setting changes.

Card tab (1.6.8): besides character cards (4 MB) also TamaSma cards (1 or 2 MB): their characters are drawn
from body and face layers (accessories that live in the toy are left out) and go into a figure like card characters.

Names (1.6.8, tab Names): fields, Tamas and items get new names (capitals, digits, space and . & ' ! ? /).
They go into every text table with Latin letters (EN, FR, DE, PT, ES, IT; third file of the asset
archive), laid out anew, and into the menu's field names. With six fields switched on (Tropical, Glacier)
the names of Land, Water and Sky can be changed too.

Eyes and mouth (1.6.8, figure drawer): "Move the eyes" and "Move the mouth" shift a character's eye or mouth layer in all its composite entries
on the field (small figure half as far). The game copies these entries when a Tama hatches or grows,
so a Tama that is already there keeps its old eyes until then. The preview steps through every pose of the big
and the small figure; "This pose only" moves eyes or mouth of the pose shown alone (for own figures). A layer of the data pack (table 15) is a mask
word, one word per set bit (x, y, sub-image, ..., type) and the picture last; x and y are the picture's centre.

Six fields: the field pictures taken from the Land, Water or Sky ROM show under All Sprites once the new ROM
is loaded, but they cannot be changed (they share their data and are made again when the fields are updated).

Start when switched on (tick under ▾ in the main program's row, tab Emulator): after power-on the
Paradise starts the emulator right away. Holding the wheel goes back to the
Paradise, which stays there, also when its screen goes dark. A and C, the
reset button or a battery change bring the emulator back. Hold B while the
Paradise starts to skip the emulator once.

Updating the emulator: open your ROM with the emulator in a newer Studio and
save it (do not choose the firmware again). Only the emulator program is
replaced. Written with "Keep saves", the Tamagotchi keeps its save and the
saved session. Choosing the firmware again starts the emulator fresh.

The guest may write anywhere in its flash (item stores, shop purchases, its
own reset). Written parts of blocks that are empty in the firmware go into
spare pages behind the packed firmware (up to 0xD40000).

With the 8 MB firmware the tab lists the download content stores (meals,
snacks, clothing, accessories, items, daily items, wallpapers, games,
destinations). Add `.jpg` download files (several at once), remove entries per
slot.

### T7Emu
Tab "Emulator": choose the ROM of the Tama-Go (640 KB) instead of a firmware.
The ROM of the Tamagotchi Friends (768 KB) works the same way (test version,
no figures).
Figure ROMs (the ones that start with AA 55) can be added with "Add figure",
also the full figure dumps (4 MB, with the extra games), and removed with the cross next to their name. Everything is packed
into the ROM on save; you bring your own ROMs.

On the device: hold A and C on the home screen to start, hold the wheel to go
back (the state is saved). A short press on the wheel opens the settings: A
goes to the next line, B changes it, C goes back or closes. Figure, background
colour, Tama colour, light and dark grey, screen (Large fills the width, Pixel
exact keeps every pixel the same size), pixel look, grid, pixel view, icons,
sound, light (5 to 100 %), dim after, dim to (1 to 50 %), screen off after, clock and language. The settings stay on the device. When the
game asks for the time, T7Emu fills it in (the Paradise's time, or the local
time written along by the Studio, or after a restart the game time from before);
birthday and name follow. The game clock follows the Paradise clock (see "How
the emulators behave"). If T7Emu hangs: start with A and C, release them and
press the wheel within the first 3 seconds.

Updating: open the ROM in a newer Studio and save it. Written with "Keep
saves", the Tama-Go keeps its save as long as its ROM is the same.

### More emulators (1.6.0)
"Choose firmware or ROM" (tab Emulator) takes the main emulator first; every further file goes to the
list of further programs (Tama-Go, Friends, V1Emu ROMs, up to twelve). Each entry has a × to take it
out again. The filled star marks the main program (DOOM when it is in the ROM; in a start device the emulators
then all go to the menu; DOOM has its own × and ↺ in the list), a click on an empty star makes another one the main
program. In a normal ROM a colour Tama or TG18 always becomes the main program. Each program goes in once. DOOM as
the main program of a normal ROM can start when switched on (▾ in its row).
▾ opens a program's row: its settings (clock per program, NFC for a 4U, start when switched on
for the main one) and its figures or download slots. The overview "Your new ROM" shows all emulators in one
entry, the main one in bold. Every program has its extras in its own row, wherever it sits (1.6.8): figures for every Tama-Go,
download content with ⓘ for the slots for every 8 MB colour Tama (iD L, P's, 4U+, 4U). The room on the ROM decides
what fits. Each program keeps its own save: T7Emu 24 KB, TamaEmu its run state (320 KB) and spare pages,
T18Emu its save area, V1Emu 4 KB session plus 4 KB settings. With "Keep saves" a colour Tama keeps its save
when only download content was added; the first item in a still empty store needs a new block and starts the
save anew (the Studio says so). In a start device (see below) also colour
Tama firmware (TamaEmu) and one TG18 firmware (T18Emu). In the menu each
program is named after its firmware (TAMA-GO, FRIENDS, ID L, 4U+, PS,
PLUS COLOR, TAMA MEETS). Each one gets its own copy of T7Emu, its
own save and its own ROM, placed from the top of the emulator area (0xD40000)
down: program 64 KB, save 24 KB, then the ROM. They are started from the menu
(A and C, then "PROGRAMS"), so the field switch or the items must be switched
on in the Menu tab. In the Save tab a
choice "T7Emu" picks which one a save.bin is backed up from or taken over into. "Keep saves" keeps
the save of each one as long as its ROM is the same; a T7Emu or V1Emu that changes its place (main program and
menu) takes its save along. The Save tab has two parts: "Programs" lists every program (main and menu) to back up or restore its
save (T7Emu, TamaEmu, V1Emu), "Paradise" holds the Paradise save and its editor.

### V1Emu (1.6.8, test version)
The first Tamagotchis and the Digital Monster: ROMs of 12 KB or 16 KB (Epson E0C6S46 and E0C6S48, as
TamaLIB and MAME read them). Known by name: Tamagotchi P1 and P2, Angel, Morino, Umino, Yasashii,
Genjintchi, Tamaotchi, Digimon V1 and V2; other ROMs of the same kind run as well. Each one takes about
88 KB (program, session, settings, ROM), so many fit next to the other programs; the menu takes up to
12 further programs. Choose the ROM with "Choose firmware or ROM", it goes to the menu.

On the device: A, B and C as on the toy. Wheel short: settings (colour, grid, shadow of the unlit dots,
icons on or off, brightness of the unlit icons, sound, light (5 to 100 %), dim after, dim to (1 to 50 %),
screen off after, clock, language,
A next line, B change, C close). Digimon V1/V2 and Yasashii have their LCD the other way round; V1Emu turns them (flags bit 5). Turning the wheel shakes the toy (Angel's shake sensor). Wheel long: save and back. The toy is
paused while you are away. The eight LCD icons are our own pictograms. The Digimon has its own icon
segments, takes its keys by interrupt and beeps with the one-shot buzzer; V1Emu does all three (1.6.8).

V1Emu can be the main program too (star in the list): A and C on the home screen, start when switched on (▾).
Shake (the Angel's sensor): turn the wheel. Icons: the Angel and the Digimon have their own sets, the others the P1/P2 set for now.

The Digimon's LCD is mounted the other way round: V1Emu turns it (and its icons).

Clock (1.6.8): for Tamagotchi P1, P2, Angel, Morino and the Digimon V1/V2 V1Emu takes the Paradise's time at every start
(tick "Take the clock from the Paradise" under ▾ in the program's row, and "Clock: Auto" on the device).
On a fresh start it first sets the toy's clock with the toy's own keys (B, B, A, C), so the Angel comes down
on its own. For the other ROMs set the clock in the game as on the toy.

### T18Emu
Tab "Emulator": choose the firmware of the Tamagotchi Meets or On (TG18, 8 MB)
instead of a colour Tama firmware. The Studio puts its used 64 KB blocks into
free blocks of the Paradise flash, from the top down (from 0xF00000, then from
0xD40000 downwards; empty blocks are left out), plus up to 10 spare blocks for
the save data the firmware writes later. T18Emu fills them by the 4 KB sector
(1.6.8): start, photo and diary need 30 sectors in 7 blocks, 6 spare blocks hold 96. The bottom (from 0x830000) stays free, so the
field switch of the menu fits next to it on Land, Sky and Water. The program
has two parts: one is loaded into the PRAM, the other runs from the flash
(0x0F0000).

On the device: hold A and C on the home screen to start, hold the wheel to go
back (the state is saved). The firmware keeps its own save in its flash as on the toy; the
first start sets that area up and takes a while. If T18Emu hangs: start with A
and C, release them, press the wheel within the first 3 seconds and confirm with A; the
firmware then starts over from its own save ("CONTINUE"). Busy scenes (a call) can still run a little slower than on the toy; melodies keep
their tempo anyway (played on the real clock, about 0.3 s late). The camera
(Item Box, Special) and the diary work on all nine known firmwares.

### How the emulators behave (TamaEmu, T7Emu, T18Emu)

Starting and leaving:

| What | TamaEmu | T7Emu | T18Emu |
| --- | --- | --- | --- |
| Start | A and C on the home screen | same | same |
| Wheel long (about 1.5 s) | save the state, back to the Paradise | same | same |
| Wheel short | nothing | settings menu | nothing |
| Wheel in the first 3 s after a resumed start | asks first (A yes, B no); yes drops the saved state, back to the Paradise; next start fresh from the Tama's own save | same | same |
| Autostart on, hold B while the Paradise starts | the Paradise starts instead of the emulator | same | same |
| Reset button, battery change | with autostart: the emulator starts again | same | same |

Leaving with the wheel saves the whole state (CPU, memory, screen). The next
start continues exactly there. The Tama's own save (the one the toy keeps in
its flash or EEPROM) is written by the game itself as on the toy and stays even
when the state is dropped.

The clock: by default ("Take the clock from the Paradise at the start", under ▾ in each
program's row of the tab "Emulator", set per program) every start sets the emulator's clock to the Paradise's time. The
time in between is not played through by the emulator: the Tama sees the clock
jump, as when the toy's clock is set. If the Paradise's time cannot be read
(for example right after its own first setup) the old way is used. Without
the tick: TamaEmu moves its clock on by the time the Paradise counted while
away, T7Emu lets the missed hours pass (up to 30, the pet ages), T18Emu moves
its clock on like TamaEmu. While running, all three keep their clock on the
Paradise's seconds counter, also when the emulation falls behind.

The Paradise while a program runs (1.6.8, switch `PARA_BACK`): the Paradise does not run, but its
seconds counter does, and back home its firmware would live the whole gap at once (hunger, care
misses). So every program (and the menu) notes the Paradise's time when it starts (clock mailbox
"PCLK" at 0x20000FC0, kept over the restart) and, when it goes back, moves the "lived up to" date of the
Paradise's settings block (both copies, new CRC) on by the time spent outside, never past now.
The Paradise's clock itself stays right. Shared code: `emu/src/emu/paraback.h`.

Brightness and dimming (1.6.8, switch `BL_PWM`): the light itself, not darker colours. Traced in the
Paradise emulator: the Paradise's own brightness setting (settings byte 8, bits 0..2) changes nothing but
the duty of timer 2 (MR1 = 85, 70 or 55 % of the period 0x17700; MR1 = period at the start). Left at that,
the counter never restarts at MR3 (match control 0 after the setup), so MR1 changes nothing; the emulators
keep MR3 reset on for a dimmed light (`hw_bl_set` in `emu/src/emu/hw.c`, switch `BL_MR3RST`), a 750 Hz PWM.
TamaEmu and T18Emu "Brightness" 5 to 100 %; T7Emu and V1Emu "Light" 5 to 100 % and "Dim to" 1 to 50 %
after the dim time (never brighter than the light).

Battery: the emulators sleep (WFI) whenever they have caught up with real
time; waiting loops of the guest firmware are skipped, the screen only gets
the rows that changed, and the light goes off when the guest switches it off.
A running pet screen keeps the CPU busy, as on the toy.

### DOOM
Tab "DOOM": choose your own `DOOM.WAD` (Ultimate DOOM or the shareware
`DOOM1.WAD`) and an episode. The WAD is converted in the browser. Keys can be
changed, also for a ROM that already has DOOM. Start with A and C like the
emulator. In a normal ROM only one of the emulator and DOOM fits; in a start
device both go together (the emulator becomes a further program of the menu).

### Start device (1.6.8)
Tab "Menu": "Start device for programs only". Of the Paradise only its setup
stays (continue or new, clock, after "new" also language, birthday and planet
name). Then the PARAMIX menu comes up instead of the home screen: wheel
selects, B starts. Leaving a program brings the menu back. All pictures of the
game are replaced by tiny empty ones, which frees about 5 MB for programs. The
menu only comes once the Paradise has a save, so a fresh device runs its setup
to the end first.

- SETTINGS in the menu: sound of the Paradise (off, low, medium, loud; its own
  settings screen is gone) and the time after which the display goes dark
  when no key is pressed (30 s to 5 min, or never). B changes, C saves.
- DOWNLOAD in the menu: starts the Studio's flash app, the Studio then connects
  without the Paradise's download screen (which a start device no longer has).
- The Studio opens a start device again: the programs show in the Emulator tab
  as further programs. Choosing the same firmware again does not add it twice.
- The room behind the Paradise's save (from 0xF00000) is used for programs as
  long as there is no T18Emu (which needs it).

### Menu (saves, fields, items, Tamapoints, Tama)
Tab "Menu": tick "Several saves (with field choice)", "Items and Tamapoints"
and/or "Change Tama". On the device, hold A and C on the home screen: wheel
selects, B confirms, C goes back.

- Several saves: the menu keeps several saves and switches between them; a new
  save starts on a field of your choice.

- Field switch: every edition has three fields (Land/Water/Sky, Jade Forest:
  Land/Water/Forest, Tropical and Glacier: Tropical/Glacier/Forest). Switching
  packs the save of the current field and brings back the save of the chosen
  one. A field that never ran starts like a new device. All fields share the
  room for the saves, so one big save can use what the others do not need.
- All 6 fields (Tropical and Glacier): tick "All 6 fields" and choose your
  own Land, Water or Sky ROM. The menu then offers Tropical, Glacier, Forest,
  Land, Water and Sky. The pictures of Land, Water and Sky are taken from that
  ROM; characters of all six fields are already in Tropical/Glacier. Without
  the tick you keep the three own fields and still have several saves; on a
  ROM that already has them, untick it to take the extra fields out again
  (the device must be on one of the own fields).
- Game menu Fields: all fields from level 4 in the game's own field menu.
- Sky as 4th field (Jade Forest): tick "Sky as 4th field". Jade Forest
  already has everything of Sky, no second ROM is needed.
- Planet to match the field: on by default. Land, Water and Sky then show
  their own planet like the original devices. Untick it to keep the planet of
  the edition. Changing it on Tropical/Glacier needs the Land, Water or Sky ROM.
- Neither works together with DOOM or the emulator (these editions have no
  room for them).
- Items: put items into the bag and set their amount, with picture. The list
  and the pictures come from your own ROM.
- Tamapoints: set digit by digit.
- Change Tama: level, care (fed, happy, healthy) and the character.
- TamaEmu or DOOM, when built in, is the last entry of the same menu.

### Device (read and write over UART)
Needs a USB UART dongle with 3.3 V (CH340 or similar) and Chrome or Edge on a
computer (Web Serial).

Wiring to the three contacts of the Paradise: left = RX of the dongle,
middle = GND, right = TX of the dongle. 3.3 V, not 5 V.

1. On the Paradise open the download screen: Planet, A, Connect, Download,
   Download.
   On a start device choose DOWNLOAD in the menu instead.
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
5. "Back up save" reads only the game progress: the Paradise save as a
   .tamasave for the TamaParadise app (with the device's clock), and if the
   device runs TamaEmu also the colour Tama's save as a .sav for the TamaEmu
   app. "Save to device" writes a .tamasave back. It must be from the same
   edition; a save of another edition is refused. Only pages that differ are
   written and the clock is set from the save.

### Save games (tab "Save")
From the loaded ROM: the Paradise save as a .tamasave (without the clock, set
it in the app if needed), the TamaEmu save as a .sav and the T7Emu save as a .bin
(the save.bin of the T7Emu app). Import: a .sav or a T7Emu .bin goes into the
ROM (then save the ROM or write it to the device), a .tamasave goes straight to
a connected device.

Slot (1.6.8): with the menu's "Several saves" (or the field switch) the menu keeps further saves in
slots. "Slot" picks which one: export takes it out of the ROM (unpacked), write to device puts the
.tamasave into that slot on the device (packed like the menu does it, header page last). With the
field switch a save only goes into the slot of its own field. The device must have the same menu
as the loaded ROM.

Change save: open a .tamasave, or take the save inside the loaded ROM. Level,
character (names from your ROM) and care (fed, happy, healthy) can be changed.
A .tamasave is saved as a new file; the ROM's own save is changed in the ROM
(then save the ROM, or write it without "Keep saves"). "Close" puts it away.

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
| `t7/` | T7Emu, the Tama-Go and Tamagotchi Friends emulator program for the Paradise, with source in `t7/src` |
| `t18/` | T18Emu, the Meets/On emulator program for the Paradise, with source in `t18/src` |
| `doom/` | DOOM for the Paradise and the WAD converter (WebAssembly), with source in `doom/src` |
| `feld/` | the on-device menu (field switch, items, Tamapoints), with source in `feld/src` |
| `six/` | helper code for six fields on Tropical/Glacier, with source in `six/src` |
| `flash/` | the flash app loaded for reading and writing over UART, with source in `flash/src` |
| `test/` | tests |

Every shipped binary (`emu/emu.bin`, `t7/t7.bin`, `t18/t18.bin`, `emu/stub_tpl.bin`, `doom/*.bin`,
`doom/mcuwad.wasm`, `feld/*.bin`, `six/six.bin`, `flash/flashapp.bin`) is built from the source
next to it; the build commands are in the README of each folder.

## Credits and license

Paramix Studio is free software under the GNU General Public License version 3
or later, see [LICENSE](LICENSE). It builds on the work of others, see
[NOTICE.md](NOTICE.md) for the full list:

- **TamaEmu** is based on [tamaemu](https://github.com/maragotchi/tamaemu) by
  maragotchi (GPL-3.0). The JIT and the Paradise port were written for this
  project; the emulator core and the download content layout come from tamaemu.
- **V1Emu** is based on [TamaLIB](https://github.com/jcrona/tamalib) by
  Jean-Christophe Rona (GPL-2.0-or-later).
- **DOOM** is based on nRF52840 Doom by next-hack, GBADoom by doomhack, PrBoom
  and the DOOM source by id Software (GPL-2.0-or-later). The WAD converter is
  MCUDoomWadUtil by Nicola Wrachien (GPL-2.0).
- File format research: [tama-para-research](https://github.com/GMMan/tama-para-research)
  and the S1C33 SLEIGH definition by GMMan.

Made by mameise.
! Supported by AI !
