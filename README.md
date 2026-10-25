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
  Grow up on the device, items and Tamapoints from MyMeets files, visits and
  marriages over mixing-meets.com through the UART dongle.
- **V1Emu (test):** the first Tamagotchis (P1 Japanese and English, P2, Angel ...) and the Digital
  Monster V1, V2 and V3, from your own ROMs; small enough for many of them on one ROM.
- **V3Emu (test):** the Tamagotchi Connection V3, from your own ROM.
- **Visits between two Paradise:** plug two Paradise together at the top:
  T18Emu, T7Emu (Tama-Go), TamaEmu (P's, iD L) and V1Emu (Digimon battles)
  talk to each other as two toys would.
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
- **Names:** rename fields, Tamas and items; move the eyes and the mouth of a
  character; take characters from TamaSma cards.
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

Two colour Tamas in one ROM (1.6.9, start device, e.g. iD L as the main program and P's in the menu): each
starts its own firmware (switch `APP_SELF`). The iD L clock no longer runs fast while its screen is off and only
finished pictures go to the panel (1.6.8). When the colour Tama wakes its display without its own light (the P's
does it every 20 minutes for about 9 s, all night) the Paradise's light stays off: it follows the toy's own
backlight, a key or a tone (1.6.9, switch `BL_GUEST_PWM`).

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
sound (off, quiet, medium, loud), light (5 to 100 %), dim after, dim to (1 to 50 %), screen off after, clock and language. The settings stay on the device. When the
game asks for the time, T7Emu fills it in (the Paradise's time, or the local
time written along by the Studio, or after a restart the game time from before);
birthday and name follow. The game clock follows the Paradise clock (see "How
the emulators behave"). If T7Emu hangs: start with A and C, release them and
press the wheel within the first 3 seconds.

Since 1.6.13 T7Emu translates the game code while it runs (much less work for the chip) and saves power with the
screen off: it runs slower and sleeps between the seconds; a call still rings and lights the screen. The game clock
no longer skips seconds.

Updating: open the ROM in a newer Studio and save it. Written with "Keep
saves", the Tama-Go keeps its save as long as its ROM is the same.

Gotchi Points (1.6.9, under ▾ in the Tama-Go's row): 0 to 999999, written into its saved EEPROM like an
imported save (the Tama stays, the running state starts anew).

PC passwords (1.6.9): Door and PC of the Tama-Go work. The game's nibble helper at $3080 was missing, so
the login password was always 000000000000 and no logout password was taken (switch `T7_PW_FIX`).

Battery, keys and tone (1.6.9): the game's wait for the next frame is added up at once and its STOP
(the Tama-Go stops its CPU until a timer or a key) halts the emulated CPU too, as in TamaGo for the Pebble;
about 80 % of the emulated time no longer needs the Paradise's CPU (switches `T7_IDLE`, `T7_STOP`). For 1 s
after a key there is no STOP (`T7_STOP_KEYACT`). Every press is counted every millisecond (after 30 ms
released, against bouncing contacts) and goes to the game for at least 120 ms, then 100 ms released
(`T7_KEYQ`): quick presses are kept in order instead of lost. Tones are queued and played on the real clock,
80 ms late, so melodies keep every note (`T7_TONE_Q`). The main loop sleeps whenever it is less than one 2 ms
step behind (`T7_WFI_FIX`, 1.6.9; before it spun through every other millisecond). Details in `t7/README.md`.

### More emulators (1.6.0)
"Choose firmware or ROM" (tab Emulator) takes the main emulator first; every further file goes to the
list of further programs (Tama-Go, Friends, V1Emu ROMs, up to twelve). Each entry has a × to take it
out again. The filled star marks the main program (DOOM when it is in the ROM; in a start device the emulators
then all go to the menu; DOOM has its own × and ↺ in the list), a click on an empty star makes another one the main
program. In a normal ROM a colour Tama or TG18 always becomes the main program. Each program goes in once.

Editions whose firmware reaches past 0x0A0000 (Jade Forest wave 2 up to 0x0A1683, Tropical and Glacier up to
0x0B5E00; 1.6.9, GitHub issue) have no room for a main program there. The Studio then puts every program into
the menu: in a normal ROM Tama-Go, Friends, V1Emu and V3Emu (A and C, with field switch or items switched on),
colour Tamas and TG18 only in a start device. 0x0A0000.. of the firmware stays untouched. DOOM as
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
Genjintchi, Tamaotchi, Digimon V1, V2 and V3; other ROMs of the same kind run as well. Each one takes about
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

The Digimon's LCD is mounted the other way round: V1Emu turns it (and its icons). The Digimon V3 (1.6.9) is
handled like V1 and V2: turned LCD, the Digimon icons, its clock at 0x1A (checked on the host: set and running as
the V1), battles over the contacts (`V1_LINK`; its send and receive routines are the same as in the V1, at 0x0B06
and 0x0B5B).

Clock (1.6.8): for Tamagotchi P1, P2, Angel, Morino and the Digimon V1/V2/V3 V1Emu takes the Paradise's time at every start
(tick "Take the clock from the Paradise" under ▾ in the program's row, and "Clock: Auto" on the device).
On a fresh start it first sets the toy's clock with the toy's own keys (B, B, A, C), so the Angel comes down
on its own. For the other ROMs set the clock in the game as on the toy.
After the start the toy's clock is checked against the Paradise's time and set again when it is off (1.6.9,
switch `V1_CLK_CHECK`; a resumed P2 kept its old time). Register B of the toy is kept with the running state
(1.6.9, `V1_REGB`; it was 0 after a resume).

### V3Emu (1.6.9, test version)
The Tamagotchi Connection V3: ROM of 512 KB (Sunplus SPLB32, a 6502, as BrickEmuPy reads it). Choose it like a
V1Emu ROM ("Choose firmware or ROM"); the Studio treats it as a V1Emu (same settings block and save layout) and puts
V3Emu in instead of V1Emu. Main program: ROM at 0x940000. In the menu: about 584 KB (ROM, 64 KB aligned, then
program, session 4 KB, settings 4 KB).

On the device: A, B and C as on the toy (left, middle, right). The screen shows the ten menu icons above and
below, lit as the toy lights them, and the 32 x 30 dots three times enlarged (setting "Icons" off: no icons, the
dots four times enlarged). The icon pictures are mameise's V3 icons (`v3/gen/icons.py` makes `icons_v3.h` from the PNGs). Wheel short: settings (battery, colour, grid in percent,
pixel view in percent (the unlit dots), icons on/off, icons inactive in percent (how strongly the icons that are off show),
sound, light, dim after, dim to, screen off after, clock Auto/Game (when the Studio knows the toy's clock), language;
A next line, B change, C close; switch `V3_SETX` for the percent settings and the clock line). Wheel long: save and back. The toy's own save (its EEPROM, 1 KB) is
kept in the settings sector and written 3 s after the game changed it; the running state on leaving, when the
screen goes off and every 15 minutes. If the game has saved since the last running state, it starts from its own
save, like the toy after a battery change.

The core (v3/src/core) is ported from BrickEmuPy: the instructions are generated from its Python (v3/gen/py2c.py),
the rest follows it line by line, checked to the bit against it on the host. The timers only get the time when
something can happen, and while the toy has stopped its CPU the steps up to its next timer event are skipped at once.
Clock (switch `V3_CLOCK`): with "Take the clock from the Paradise" (on by default) V3Emu writes the Paradise's time
and date into the toy at every start: seconds 0x81/0x82, minutes 0x83/0x84, hour 0x85 (1..12) and 0x86 (PM), day 0x13D (ones)
and 0x13E (tens), month 0x13F. Only when the toy's clock runs, so set it once in the game; then it is checked and written again if
it is off.

Connection (switch `V3_LINK`): two Paradise plugged together at the top, V3Emu on both, then the connection in the
game as with two real V3. The core turns the toy's infrared pulses into bytes and back (as BrickEmuPy's CON_V3_IR,
which links two emulators over a serial line); the bytes go as 'V' frames over the UART on the contacts (plink.h).

V3Emu can start when the Paradise is switched on (main program, ▾ "Start when switched on"), like the others.

### T18Emu
Tab "Emulator": choose the firmware of the Tamagotchi Meets or On (TG18, 8 MB)
instead of a colour Tama firmware. The Studio puts its used 64 KB blocks into
free blocks of the Paradise flash, from the top down (from 0xF00000, then from
0xD40000 downwards; empty blocks are left out), plus up to 10 spare blocks for
the save data the firmware writes later. T18Emu fills them by the 4 KB sector
(1.6.8): start, photo and diary need 30 sectors in 7 blocks, 6 spare blocks hold 96. A game that has grown up
needs more (a baby to child step was measured with 99), so in a start device the Studio adds spares up to 15 where
there is room (1.6.9, also to an installed T18Emu, behind its spares: the save stays), and a full pool takes over
sectors the firmware has erased (switch `T18_POOL_RECLAIM`). The bottom (from 0x830000) stays free, so the
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

Grow up (on the device, wheel short, line "Grow up"): as in the Android T18Emu, the Tama becomes an adult in
about a minute and a half (its age is put just before each step, the game clock runs fast only for that one game
minute, the step and its animation run in real time, care stays full, the screen stays on, the firmware does the rest). A Tama of the
first generation can be chosen (ten adults, the Sanrio firmware has Flowertchi); a child of a married pair grows
up like its parents. Pictures for the choice: the Studio has none; under ▾ in the T18Emu row "Choose pictures"
takes your own PNG files named after the character (Mametchi.png, Kuchipatchi.png ...) or its number (16.png),
or the folder erwachsen of the Android T18Emu with its erwachsen.json. They are scaled to 64x64 and stored at
0x0E4000 (main program only).

Items (under ▾ in the T18Emu row): MyMeets files (tg18_gh..., _oy..., _as..., _ac..., _bg... .txt, also in a .zip)
go into the game's save, with a picture, the number of pieces (1 to 3) and a room. Tamapoints can be set. The game
must have run once (read the ROM from the device). The running state of the emulator starts anew, the Tama stays. A name in
Japanese letters shows as its number (Nr. 3006). T18Emu in the menu of a start device keeps its place when the ROM is
saved again.
With "Keep saves" ticked, writing items or Tamapoints to the device asks first: T18Emu's save is replaced by the
one in the ROM.

The date (1.6.9, switch `T18_DAY_FIX`): the game counts the days itself and a new start (CONTINUE) loaded the day of
leaving; the days since are added once after the start. Started from the menu, a sleeping game is woken once
with a short C (`T18_WAKE_START`).

Screen (1.6.9): the picture goes out whole once the game has finished drawing it, only the rows that changed
(1.6.6 to 1.6.8). The check value per row is now FNV-1a (`T18_SUM_FNV`): the old one gave the same value for
many different rows and left parts of the picture old (AM/PM on the clock, frames). One row per picture always
goes out (`T18_ROW_RR`), the first picture after the start is complete (`T18_PRES_FIRST`), and switching the
screen on no longer stops the game for 145 ms while the panel wakes (`T18_WAKE_ASYNC`).

MixingMeets (1.6.9, under ▾ in the T18Emu row): visits and marriages over mixing-meets.com through the UART
dongle on the Paradise's contacts. Drag the "Paramix bridge" bookmark to the bookmarks bar once; disconnect
the Studio from the dongle ("Release UART" or close it); on the Tama CONNECTION, APP, VISIT; on
mixing-meets.com open Connect or Return, click the bookmark, then Select Tamagotchi and choose the dongle.
Click the bookmark again after every page change. The page's Bluetooth goes over the dongle to T18Emu, which
hands it to the firmware's Bluetooth module like the Android T18Emu (switch `T18_LINK`).

### Visits between two Paradise (1.6.9)
Plug two Paradise together at the top (the connector crosses TX and RX), start the same program on both and
connect as with two toys. The programs talk over the contacts (UART1, 460800 baud), the Studio does not take
part (under ▾ it only says how).

| Program | On both devices |
| --- | --- |
| T18Emu | CONNECTION, TAMAGOTCHI: playdate, gift, marriage (switch `T18_LINK`) |
| T7Emu (Tama-Go) | connection, TAMA-GO, VISIT or GAME; B on the visiting one (`T7_LINK`) |
| TamaEmu (P's, iD L) | P's: あそぶ, あそびにいく on one, ともだちをよぶ on the other (`TC_IRLINK`) |
| V1Emu (Digimon) | choose and start the battle (`V1_LINK`, not yet checked with a real battle) |
| V3Emu | the connection in the game on both (`V3_LINK`, not yet checked on two devices) |

Checked on the PC with two cores in lockstep; on the device see the test notes.

### Game Boy (1.6.9)
Tab "Game Boy": choose Game Boy games (.gb, the black and white Game Boy; Game Boy Color games do not work). They go
into the Paramix menu (switch the menu on in the tab "Menu"). The screen is shrunk from 160 x 144 to 128 x 115, nothing
is cut off. The keys are set in the tab for all games (default: A left, C right, B = B, wheel press = A, wheel up and
down, A+C Start). On the device: A+B+C together opens the settings (with Select and sound), holding the wheel for 2 s
saves and goes back. Battery saves of the games are kept. Sound: the Paradise's buzzer plays one tone at a time (the
melody or the effect). Since 1.6.12: holding the wheel keeps the whole game, the next start continues exactly there;
"Restart game" in the settings; two Paradise plugged together at the top work like a link cable (e.g. trading
Pokémon). Program and details: `gb/`.

### Wolfenstein 3D (1.6.10)
Tab "Wolf3D": choose all files of your own Wolfenstein 3D together, the shareware (.WL1, episode 1) or the full
version (.WL6, six episodes), version 1.4. The Studio builds one data pack from them in the browser (nothing is sent
anywhere) and the game goes into the Paramix menu (switch the menu on in the tab "Menu"). Screen: the view 128 x 96,
below a status bar with face, health, ammo, keys, weapon, floor, lives and score. Sound: the game's PC speaker sounds
on the buzzer (no music). Keys as DOOM on the Paradise, set in the tab: A fire, B forward, C back, wheel turns (degrees
per step to choose), wheel press doors and switches, hold C + wheel weapon, B twice quickly run. Fixed: A+C menu,
holding the wheel for 2 s saves the game and goes back to the Paradise; "Continue" goes on at the next start.
Since 1.6.12 also Spear of Destiny (.SOD, 21 floors), as a second entry next to Wolfenstein 3D.
Game from Wolf4SDL (GPL), program and details: `wolf/`.

### Master System (1.6.11)
Tab "Master System": choose your own Sega Master System games (.sms). They go into the
Paramix menu (switch the menu on in the tab "Menu"). The screen is shrunk from 256 x 192 to 128 x 96, nothing is cut
off. The keys are set in the tab for all games (default: A left, C right, B button 1, wheel press button 2, wheel up
and down, A+C Pause). On the device: A+B+C together opens the settings (with Pause and sound), holding the wheel for
2 s saves and goes back. Battery saves of the games are kept. Sound: the buzzer plays the loudest tone channel.
Since 1.6.12 also Game Gear games (.gg, 160 x 144 shrunk to 128 x 115, START on A+C), the whole game kept when
holding the wheel, and "Restart game" in the settings. Own emulator (Z80, video chip, Sega mapper), program and
details: `sms/`.

### Simulated visit for the Paradise itself (1.6.9)
Under "Device" the Studio can play a second Paradise over the dongle, so one device is enough for a playdate.
The Paradise runs its normal game (no flash app). "Connect" above is not needed for it.

1. Choose the edition the visitor comes from (Land, Water, Sky, Jade Forest, Tropics, Glacier), "Start", pick the dongle.
2. On the Paradise: planet menu, CONNECTION, PLAYDATE, then B.

The visitor is a copy of your own Tama (its picture comes from your device, the Studio has no game data); only
the edition and its IDs are its own, so every edition is a friend of its own. At the young stage the Paradise
notes the edition of each partner (seen in its save after a visit from Water and one from Sky); the secret
characters want two other colours there. Whether the secret character then comes is to be checked on the device.
Protocol: GMMan's tama-para-research (TCP over UART, 460800 baud), code in `src/visit.js`. The Studio
answers as the device that was asked (the Paradise presses B), never breeds (it answers BREED 0).
Checked on the PC with the emulated Land against the Studio in the browser (Sky, Water, Jade Forest).

### Battery (1.6.9)
Battery saving (1.6.9, switch `CLK_ECO`, `emu/src/emu/eco.h`): with the screen off T7Emu, V1Emu and V3Emu lower the
chip's clock (AHB prescaler `SYS_AHBCP` 0x45000010, Sonix TRM: SYSCLK / 2^n for core, memory and peripherals) step by
step from 96 MHz down to 12 MHz while the emulation keeps up (busy share from the DWT cycle counter every 500 ms:
below 35 % one step down, above 80 % one step up). If it falls 25 ms behind: full speed at once, then one step less
deep for 5 minutes. A key, the screen, a tone or link traffic (30 s) bring full speed. SysTick is set for 1 ms at
each step, a playing tone keeps its pitch. Measured in the Unicorn simulation with the screen off: V1Emu (P2) at 12 MHz, V3Emu and T7Emu at 48 MHz,
T18Emu (Meets in standby) mostly at 12 MHz. TamaEmu has it too (no simulation for it); on the device the busy share
decides how far each one goes down. ECO_LAG per program (T7Emu 400, TamaEmu 50, T18Emu 120, V1/V3 25 ms).
Tick check (1.6.9, switch `ECO_RTC`): T7Emu, V1Emu and V3Emu compare the millisecond tick with the Paradise's seconds
counter every 4 whole seconds; a clock step that counted fewer than 3800 ms is not used again until the next start,
T7Emu adds the missing time (its emulation catches up). The counts go into T7Emu's session sector at 0xFF0
(clock steps found wrong | corrections << 8, ms added), so a dump shows them.

Care and points on the device (wheel short, in the settings): T18Emu has "Tamapoints" (A next digit, B the
digit + 1, C OK, the wheel goes back without change) and "Full care" (hunger and mood full, not sick, no call), on
all nine known firmwares. T7Emu (Tama-Go and Friends) has "Gotchi Points" (up to 999999) and "Full care" (hunger
and happy 5 hearts). TamaEmu has "Gotchi Points" (up to 99999) and "Full care" with Plus Color, iD L (v050 to v055),
P's, 4U and 4U+ (hunger full and the happy bar as far as the game allows it: iD L friend level and
Plus Color HAPPY! by the medals, P's ring by the medals, 4U love ring by the stage). V3Emu has "Gotchi Points" (4 digits)
and "Full care" (hunger and happy 4 hearts). V1Emu has "Full care" for all twelve known ROMs (P1 also English, P2, Angel, Genjintch,
Morino, Tamaotch, Umino, Yasashii, Digimon V1 to V3; Digimon: hunger and strength). Other ROMs show no such lines.

The settings of T7Emu, V1Emu, TamaEmu and T18Emu (wheel short) show the battery, e.g. "2.84V 80%", and the
home screen of a start device shows it bottom right. It is measured as the Paradise firmware does it: SAR ADC at
0x4003A000, channel 4 (P0.6), volts = sample x 3.3 / 1024; below 2.2 V the Paradise switches off (0 %), 3.0 V and
more count as full. Shared code `emu/src/emu/batt.h`, switch `BAT_SHOW`. Not yet checked on the device; "?" means no
reading came.

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
- Main program with DOOM (1.6.9): DOOM stays at its place, but the star in the
  Emulator tab can make any program of the menu the main one. It is shown first
  and, with "Start when switched on" under ▾, starts instead of the menu after a
  battery change or a reset while it ran (hold B: the menu). Switch `FELD_AUTO`
  in the menu program, settings version 9.

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
  and the pictures come from your own ROM. Amount 0 ("REMOVE") takes an item out
  again; items the game holds with amount 0 (eaten up) are not shown (switch `BAG_DEL`).
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
Since 1.6.12 every program in the list has export and import: T18Emu the firmware with its save (.bin, as a
firmware for T18Emu), Game Boy and Master System the cartridge RAM (.sav, as other emulators use it; a kept game
state is dropped), Wolfenstein 3D and Spear of Destiny their saved game (.bin). An imported save wins over "Keep saves".
T7Emu saves written up to 1.6.8 may lie 2 bytes off in the flash (the page buffer was not word aligned, the flash DMA
takes words); the export finds them too and gives the right save.bin, and T7Emu from 1.6.9 reads them itself
(switch `T7_EE_SHIFT`).

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
| `gb/` | GBEmu, the Game Boy emulator program for the Paradise (core Peanut-GB), with source in `gb/src` |
| `sms/` | SMSEmu, the Master System emulator program for the Paradise, with source in `sms/src` |
| `six/` | helper code for six fields on Tropical/Glacier, with source in `six/src` |
| `flash/` | the flash app loaded for reading and writing over UART, with source in `flash/src` |
| `test/` | tests (`visit_test.js`: the simulated visit with a test key) |

Every shipped binary (`emu/emu.bin`, `t7/t7.bin`, `t18/t18.bin`, `emu/stub_tpl.bin`, `doom/*.bin`,
`doom/mcuwad.wasm`, `gb/gb.bin`, `sms/sms.bin`, `v1/v1.bin`, `v3/v3.bin`, `wolf/wolf.bin`, `wolf/wolfsod.bin`, `feld/*.bin`, `six/six.bin`, `flash/flashapp.bin`) is built from the source
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
- **V3Emu** is based on [BrickEmuPy](https://github.com/azya52/BrickEmuPy) by
  azya52, released under CC0 1.0 (public domain dedication: free to use, change
  and pass on, no conditions): the SPLB32 core, the HT24LC08 EEPROM, the
  infrared byte link and the dot and icon layout of the Connection V3 face.
  See NOTICE.md.
- **DOOM** is based on nRF52840 Doom by next-hack, GBADoom by doomhack, PrBoom
  and the DOOM source by id Software (GPL-2.0-or-later). The WAD converter is
  MCUDoomWadUtil by Nicola Wrachien (GPL-2.0).
- File format research: [tama-para-research](https://github.com/GMMan/tama-para-research)
  and the S1C33 SLEIGH definition by GMMan.

Made by mameise.
! Supported by AI !
