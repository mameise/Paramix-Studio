# Paramix menu (field switch, items, Tamapoints, Tama)

A small program for the Paradise, started by the launcher (A+C held on the home
screen). Texts on the device are English, the font and graphics are our own.

- `feld.bin`: the menu program, runs from PRAM.
- `feldc.bin` (1.7.2): the same with `FELD_CLEAN`, only for ROMs without the Paradise (own clock, settings version 10).
- `stub_feld_tpl.bin`: the launcher with menu support and placeholders.
- `src/feld`: source of the menu, `src/stub/stub.S`: source of the launcher.

## What it does

- **Field switch:** the save of the active field is packed (LZSS per 4 KB page)
  into its own slot, the save of the chosen field is restored (or the factory
  settings pages 0xEFE000/0xEFF000 are written for a fresh field), then the
  menu offers to stay or restart. The settings pages are written last, so an interrupted switch
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
- **Batch items (local modification):** in ITEMS, `+ ADD ALL MISSING` restores
  zero-count items and adds absent IDs from the ROM's editable catalog, leaving
  owned counts alone. `= SET OWNED COUNTS` sets every owned, non-fixed item's
  count to the chosen value (1 to 15), leaving zero-count items alone. Both show
  the number of entries to change and ask for confirmation. Changes are held in
  RAM until leaving ITEMS and accepting SAVE CHANGES. Capacity is checked before
  adding anything; a full bag never causes a partial batch. Fixed catalog entries
  are excluded. Run `node test/feld_bulk_test.js` from the repository root for
  synthetic-save host tests, including UI navigation and mirrored save CRCs.

## Menu extensions (1.7.5-menu7)

The menu supports batch inventory edits, independent save copies, slot cleanup,
and direct save-copy transfer between compatible Paradise devices. After saving,
EXIT MENU? defaults to NO, allowing more changes without restarting. B on NO or
C returns to the parent menu; YES + B exits. The normal Paradise menu home exits
on C and labels this C EXIT. The program-only launcher keeps its launcher behavior.

SAVES caches availability, field and character metadata on entry, refreshing
only after a successful copy, clear or reception. Scrolling and cancelling
options do not read/decompress every saved game. Input handlers wait for an
actual event instead of repeatedly repainting and consuming physical B/C presses.

Save-link transitions repaint the whole screen, preventing confirmation text
from remaining behind a progress bar. ROM checking and save preparation show
percentages; waiting shows elapsed seconds and an animated indicator. The
receiver shows PEER FOUND, then READING SAVE INFO and RECEIVING SAVE. Byte-based
transfer progress is followed by verification, slot commit and link completion.
Receiver repaints happen before ACK while the sender waits, and idle animation
runs outside frame parsing.

- SAVES always shows slot number, ACTIVE/SAVED/EMPTY, field and character.
  B opens slot options. Empty slots default to NO COPY - NEW GAME, or can
  copy an existing slot (the active one uses the current live progress).
  Choose the target field afterwards. Source slots are unchanged. Copies use
  independent shared-pool pages and publish their header last; capacity is
  checked for both the copy and the subsequent backup of the live save.
- Existing slots offer LOAD / RESUME, CLEAR THIS SLOT and SEND TO TAMA.
  Clear defaults to CANCEL. Active slots cannot be cleared. Clearing erases
  only the header, never an entire physical slot region, because pool pages
  may belong to other saves. New copies retain the source character and
  progression; cross-field evolution/field-specific progress needs real-device
  validation, especially with six-field ROMs.
- TAMAPOINTS offers SET MAX: 99999 (first and selected by default), followed by
  EDIT AMOUNT. Saving the maximum still requires confirmation.
- ADD ITEM hides x15 entries by default; SHOW MAX ITEMS can display them again.
  Item lists, count editors and batch count editors wrap in both directions.
  New/batch counts wrap 1..15; owned count editing wraps 0..15 (0 = remove).
- Empty slots offer RECEIVE FROM TAMA. Both devices must run a menu with the
  same save-link protocol. Connect at the top, start SEND and RECEIVE, then
  approve the incoming source slot/field. Reception stores a copy and leaves
  the current live save untouched. Select the received slot to load it.
  Matching asset-archive CRC and layout/field signatures are required; this
  intentionally refuses different game resources or save layouts. No ROM or
  emulator programs are sent. The imported header records the two devices'
  clock-counter origins so loading/copying rebases the save's clock without
  changing the receiver's hardware counter or its other slots.
- The experimental UART save-link uses 460800 8N1, a distinct PMS1 framing
  magic, protocol 1, 128-byte payloads, session/sequence IDs, CRC32, ACK/retry,
  bounded waits and C cancellation. Only empty, inactive slots may receive.
  Page addresses, ordering, lengths, decompression, settings CRCs and aggregate
  packed checksum are validated before header publication. Interrupted imports
  leave an invalid target header and reusable pool pages. A lost final ACK may
  mean the receiver has a complete copy even if the sender reports failure;
  inspect the receiving slot before retrying.

Validation: `node test/feld_bulk_test.js`, `node test/feld_menu_test.js`,
`node test/feld_slot_clock_test.js`, `node test/unit_test.js`, and both ARM builds. Menu tests use synthetic data
with ASan/UBSan and two separate socket-connected processes, including dropped
ACK, duplicate frames, incompatible resources, disconnect, and different clock
counters. Physical two-device UART save transfer has been tested and confirmed
working. Cross-field gameplay compatibility requires separate validation.
No device is flashed by these commands.

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

Main program of the start device (1.6.9 test13, settings version 9, switch `FELD_AUTO`): DOOM must stay at
0x0A0000, so the main program can also be one of the list. `main_src` (offset 600) names it: the menu shows it
first. `vflags` bit 0 (offset 604): start it when switched on instead of showing the menu, once per switching on,
with the stub's mailbox at 0x20000FFC ("PMXL" a program went back, "PMXM" the menu decided, "PMXR" restart while it
ran, else the seconds counter at 0x45000304 below 30 means switched on). B held while switching on: the menu.
Host test: `AUTO_CNT=5 AUTO_MB=... AUTO_GPIO=... ./menu_host ...` (build with `-DFELD_AUTO`).

Location: launcher 0x0B0000, program and catalog from 0x0B1000, slots behind the
character data. Tropical and Glacier still have firmware code there, so launcher
and program move to the first free page (0x0B6000). With DOOM, program, catalog
and slots use the free end of the DOOM program area. With six fields the six
slots lie behind the menu (up to 0x10F000, where the helper page starts) and
behind the character data.

Clean ROM, clock not asked (1.7.3 test7, switch `FELD_NOCLK`): `vflags` bit 1 set by the Studio ("Set the clock at the
start" off): a missing clock is not asked for when switched on (nor before the autostart); it is set under SETTINGS.

## Clock for the programs (PARA_FLASH, 1.6.9 test37)

At its start the menu looks the Paradise's time up in the SRAM (`para_clock`) and puts it in the mailbox 0x20000FC0
for the programs. When the SRAM has no clock and the mailbox is empty, it takes the base of the Paradise's clock
from the settings page 0xEFE000 (copy 0xEFF000): days since 2025-01-01 at +0x0C, seconds at +0x10, plus the seconds
counter 0x45000304 (`para_flash` in `emu/src/emu/paraclock.h`; page CRC and date fields at +0x1C are checked).

## Build

```
sh src/feld/build.sh     # clang, ld.lld, llvm-objcopy -> src/feld/feld.bin and feldc.bin (copy them to feld/)
```

Launcher template: as in `emu/README.md`, plus `-DFELD=1 -DFELD_SRC=0x6789EF01`.
Without `-DFELD` the same source gives `emu/stub_tpl.bin`.

PC test without a device:

```
cd src/feld && cc -DHOST -DFELD_AUTO -w -I. host.c main.c -o menu_host
./menu_host <rom_with_menu.bin> <out.bin> <picture_dir> "<keys>"   # keys: n p a b c
```

License: GNU GPL version 3 or later.

Autostart after a reset (clean ROM, 1.7.3 test9, switches FELD_SEEN, FELD_BSETTLE, FELD_AUTODBG): on the device the
mailbox still held "PMXL" at a reset after a program had gone back and the menu was shown, so the reset showed the menu
again. The menu now writes "PMXS" into 0x20000FF8 when it shows itself for a "PMXL" (the programs set that word to 0
with every "PMXL"): a "PMXL" with "PMXS" (test12: with any word other than 0 there, e.g. the Paradise stub's counter) is an old one and the main program starts. B counts as held only over five
looks a few ms apart. AUTODBG (test builds): bottom middle of the home screen: mailbox (L M R 0 ?), GPIO0 bits 8..11,
B held or "-", seen mark (S 0 ?).
