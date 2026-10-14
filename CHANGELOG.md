# Changelog

Versions are listed newest first.

## 1.2.6 (2026-10-14)

- TamaEmu: updating keeps the save. A ROM that already has the emulator gets the new emulator program when it is saved, without choosing the firmware again. Written with "Keep saves", the Tamagotchi's save, its items and the saved session stay on the device.
- Keep saves: the spare pages of TamaEmu (item stores, since 1.2.3) are kept too. Before they were overwritten even with "Keep saves".

## 1.2.5 (2026-10-14)

- Six fields: the planet now follows the field. On Land, Water and Sky the planet view and the travel scenes show the Land, Water and Sky planet instead of the Tropical or Glacier one, like on the original devices. New switch "Planet to match the field" (on by default). Jade Forest: the Sky planet on Sky.
- Helper page version 5 (planet tables per field). ROMs with more fields from 1.2.0 to 1.2.4 are updated when saved: Jade Forest on its own, Tropical/Glacier with your Land, Water or Sky ROM chosen.
- TamaEmu: the clock no longer runs slow. The emulator dropped part of each millisecond while the Tamagotchi runs on its slow clock (about 2.3 percent, up to half an hour a day). It also follows the seconds counter of the Paradise now, so time lost in busy scenes (shopping, saving) is added back.
- Emulator program v0.9.3.

## 1.2.4 (2026-10-14)

- Six fields: the special character of each field. On Land, Water and Sky the firmware now picks Chodracotchi, Mermarintchi and Yayacorntchi instead of the Tropical, Glacier and Forest special characters (Manapatchi, Hobohorntchi, Tatsutchi). Jade Forest: Yayacorntchi on Sky.
- Helper page version 4 (the site list also holds plain values). ROMs with more fields from 1.2.0 to 1.2.3 are updated when saved: Jade Forest on its own, Tropical/Glacier with your Land, Water or Sky ROM chosen.

## 1.2.3 (2026-10-14)

- TamaEmu: fixed the crash when buying in the Tamadepa (for example the mop under daily items on the iD L). The iD L writes its item stores into 64 KB flash sectors; the emulator only kept writes to the small parameter sectors, so the purchase was lost and the game hung waiting for the flash. Now 64 KB sectors are erased and written like on the real chip. Blocks that are empty in the firmware get spare pages behind the packed firmware. The in-game reset of the iD L works again too (it hung on "WAIT!" before).
- TamaEmu: a long press after such a hang no longer saves the hung session (no save if the core stopped or guest writes were lost).
- TamaEmu: new way out of a bad session: release A and C after starting and press the wheel within the first 3 seconds. The saved session is dropped, the next start begins fresh from the Tamagotchi's own save. No reflashing needed.
- Emulator program v0.9.2, settings block version 3 (end of the spare area). Saved sessions of older versions are not resumed (the Tamagotchi's own save stays).

## 1.2.2 (2026-10-14)

- Six fields: rain on Land, Water and Sky. The rain pictures lie in the unused flash area from 0xF00000 (the cells the clear pictures do not share); the Studio points them there again after every save.
- Six fields: the scene animations now follow Jade Forest's tables, which have the same layout as Tropical's (the columns of 1.2.1 were partly wrong), and the wagon scene of the planet view shows Land, Water and Sky too.
- Jade Forest: new switch "Sky as 4th field". Jade Forest already carries everything of Sky, no second ROM is needed. Sky uses the Water slot with the set byte; the menu lists Land, Water, Forest, Sky.
- Helper page version 3 (one code for both firmwares, all addresses and tables filled in by the Studio). ROMs of 1.2.0 and 1.2.1 are updated when saved with a Land, Water or Sky ROM chosen.
- Menu settings block: field table for the set byte (Jade Forest: Sky; Tropical/Glacier: Land, Water, Sky). The menu no longer packs the area from 0xF00000 into the field saves.
- Device: "Keep saves" does not keep the area from 0xF00000 of the device when the new ROM has the extra fields (it holds pictures there).

## 1.2.1 (2026-10-14)

- Six fields: on Land, Water and Sky the travel backgrounds, the field eggs and the scene animations (birthday and others) now show the Land, Water and Sky pictures too. Eleven more places of the firmware jump into the helper page; the Land scripts are copied with the scripts and sprites they use (most sprites are already in Tropical/Glacier and are shared).
- Six fields of 1.2.0 are updated automatically when the ROM is saved again.
- DOOM: the wheel button is now followed every millisecond. Quick taps on the wheel (looking for secret doors) no longer add up to a long press that ended the game. The long press (2 s) counts from the last press.

## 1.2.0 (2026-10-14)

- Menu tab: new switch "All 6 fields" for Tropical and Glacier. Adds Land, Water and Sky to Tropical, Glacier and Forest. The field pictures come from your own Land, Water or Sky ROM (any of the three).
- How it works: the Tropical/Glacier firmware has three field slots and already carries the characters of all six fields. The Studio copies the field pictures of Land, Water and Sky into the archive (128 colours per picture, tiles shared between the times of day) and patches four places of the firmware: a set byte in the save (offset 0xF04) switches the three slots to Land, Water, Sky for the backgrounds and for child, teen and adult characters. Helper code at flash 0x10F000, source in `six/src`.
- To make room the Tropical, Glacier and Forest field pictures are stored with 128 colours (no visible difference).
- On Land, Water and Sky rain shows the clear picture (no room for the rain pictures). Some scenes still show the pictures of the matching Tropical, Glacier or Forest slot (fixed in 1.2.1).
- Menu on the device: six fields in a compact list; saves of the fields in two regions (behind the menu and behind the character data). Settings block version 4.
- Not possible together with DOOM or the emulator (Tropical, Glacier and Jade Forest have no room for them anyway).

## 1.1.0 (2026-10-13)

- Characters tab: new field filter. Shows only the characters of one field (Land, Water, Sky, Forest, Tropical, Glacier), with their names from the ROM.
- "Field as ZIP": all sheets of one field in one ZIP: 22 characters (child, 4 teens, 17 adults), the 7 field map pictures (plus 7 overlays on Tropical/Glacier), the 6 close-up backgrounds, a contents list and field.json.
- "Load field from ZIP": puts a field ZIP (from the same or another ROM) into the selected field, character by character in the same order. Characters whose eye images do not fit stay original as a whole.
- Character tiles show the character's name.
- A character sheet is now applied completely or not at all (no half replaced characters).

## 1.0.5 (2026-10-13)

- Fixed missing item pictures in ROMs with DOOM: the Studio searched the whole ROM for the image table and found a similar looking table inside the DOOM program first. It now searches only the asset archive. This was the cause of the codes P6 and P7 on the device.

## 1.0.4 (2026-10-13)

- Catalog of the menu: the Studio now stores the flash address, length, sum and first word of every item picture. The menu reads the picture straight from there and no longer looks it up in the ROM's image table (the device returned wrong values there, code P6).
- Menu: three ways to read a picture (32-bit reads, byte reads, the cached XIP window); the first one whose sum matches is used. If none matches, "RD" shows the sums of all three and the first word read and expected.

## 1.0.3 (2026-10-13)

- Menu on the device: item pictures are copied to RAM with aligned 32-bit reads (the way the flash app reads dumps) and checked against a sum stored by the Studio before they are drawn. Damaged data can no longer hang the menu; a mismatch shows "RD" with both sums.
- Picture decoder: every loop is bounded by the entry size.

## 1.0.2 (2026-10-13)

- English page: error messages of the emulator, DOOM and menu parts are now English too (they were German). The German page translates them.
- Card tab: "No." instead of "Nr." on the English page. Saved file names use "_fields" instead of "_felder".
- Menu: a ROM with a menu from an older Studio version gets the current menu when it is saved or written (the overview shows "gets updated"). Before, such a ROM was kept unchanged, so item pictures (new in 0.9.6) were missing on the device.
- Menu on the device: if an item picture cannot be drawn, a short code (NO PICS, P1 to P4) appears in the picture box.

## 1.0.1 (2026-10-13)

- Disclaimer on the page, in the Device panel and in the README: use at your own risk, no warranty, no liability for damage to devices or data.
- Trademark notice on the page and in the README.

## 1.0.0 (2026-10-13)

- Device tab: read the ROM over a UART dongle (Web Serial, Chrome or Edge) and write a ROM to the device. The flash app is loaded from the download screen, so no P-COM is needed. The source of the flash app is in flash/src.
- Device tab, "only write changes": the tool compares CRCs per 64 KB and 4 KB and writes only the sectors that differ.
- Device tab, "keep saves": save data, field slots, TamaEmu state and guest save, and the DOOM save are taken over from the device.
- TamaEmu: all download content stores of the 8 MB firmware are supported: meals, snacks, clothing, accessories, items, daily items, wallpapers, games and destinations.
- TamaEmu: several .jpg files can be added at once, and each slot can be removed. The store layout follows tools/dlc.c from tamaemu.
- Release: English code comments, English README, LICENSE (GPL-3.0), NOTICE with third party credits and this CHANGELOG. Every shipped binary rebuilds byte-identical from the sources.

## 0.9.6

- New overview at the top, "Your new ROM": shows what the saved ROM will contain (new, kept, removed) and how full the flash is.
- Emulator tab warns that a chosen firmware replaces DOOM. The overview then shows "DOOM will be removed".
- DOOM program v0.4.0: key layout can be set in the DOOM tab (fire, forward, back, use, hold key, double tap run). A ROM with DOOM already built in can be changed without the WAD.
- DOOM program v0.4.0: fixed "back" sometimes firing for one tick before the 120 ms delay had passed.
- Field switch shows three fields for every ROM (Land, Water, Sky; Land, Water, Forest on Jade Forest; Tropical, Glacier, Forest).
- Item picker shows the item picture next to the amount.

## 0.9.5

- New Menu tab. The on-device menu is called PARAMIX and opens with A+C on the home screen. Texts are in English.
- FIELD: switch field, as in 0.9.4. Works for Land/Water/Sky and for Tropical/Glacier.
- ITEMS: show the bag, add items from a catalogue (food, snacks, toys, goods), set amounts, save.
- POINTS: set Tamapoints digit by digit.
- Jade Forest gets ITEMS and POINTS only.

## 0.9.4

- New Fields tab with "Field switch". The A+C menu lists Land, Water and Sky. Switching packs the save of the old field into its own slot and brings back the save of the new field.
- A field that never ran starts like a new device. Switching back restores the old state.
- Works together with TamaEmu or DOOM. With DOOM each field has room for about 36 KB of packed save. If it does not fit, error code 11 is shown and nothing changes.
- Dumps that already contain stored fields keep them when saved.

## 0.9.3

- New DOOM tab. Choose your own DOOM.WAD (Ultimate DOOM or Shareware, episode 1), the Studio builds DOOM into the ROM. The WAD stays on your computer and no game graphics ship with the Studio.
- The WAD converter runs in the browser (WebAssembly).
- DOOM program v0.3.1: title screen, status bar and menu labels are built from your own WAD. No third party graphics remain in the program.
- DOOM program v0.3.0: black start screen with a loading bar on first start, no test screens. C held plus wheel press switches sound: all on, music off, all off.
- DOOM program v0.2.5: music, a one voice player that plays the main theme of the first level. Music volume 0 in the menu turns it off.
- DOOM program v0.2.4: sound effects on the buzzer. Wheel turns the other way. C held plus wheel changes weapon. C alone goes back.
- DOOM program v0.2.3: game starts and runs. Fixed a crash on unaligned memory access when a level loads.
- DOOM program v0.2.0 to v0.2.2: fixed a black screen and a crash caused by RAM at 0x20000000 being only 4 KB. Frame and palette now work.
- DOOM program v0.1.x: first bring-up on the device (128x128 picture, wheel and button control). Fixed several start hangs. Based on nRF52840 Doom by next-hack (GPL).
- Studio and test builds share the same build path, so the ROM is the same in the browser and on the command line.

## 0.9.2

- Fewer short hangs when keys are pressed quickly.
- Saving happens when the screen goes dark, not while playing.

## 0.9.1

- Fewer hangs on fast key presses in menus and games.
- The TamaEmu save is written when the guest screen goes dark, invisibly.

## 0.9.0

- First finished TamaEmu build without any test numbers on screen.
- Start screen is black instead of blue.
- The wheel short press no longer shows a speed readout.
- Behavior otherwise as in 0.8.22.

## 0.8.22

- The guest save is written to flash in small pieces instead of all at once. This shortens the hangs that made sound stretch.

## 0.8.21

- Test build: shows where the program spends its time in a game.

## 0.8.20

- Test build: shows how much time emulation and screen output take.

## 0.8.19

- Cleaner screen: no leftovers of old figures, no stripes.
- A second short wheel press removes the number lines again.
- Backlight stays off on a black picture and comes back on a key press.

## 0.8.18

- Backlight handling fixed: it turns off when the guest picture is black.
- Test build with faster screen output ("LCDschnell").

## 0.8.17

- Faster screen output.
- Backlight really turns off when the guest picture is black.
- Frequently read game data is kept in fast memory.

## 0.8.16

- Translated code is back in fast memory and has more room.
- Backlight turns off when the guest picture goes black.
- Short wheel press shows two lines of speed numbers.

## 0.8.15

- Guest ROM is read through the fast flash cache.
- The translator clears its code more cheaply. New sound output.
- Short wheel press shows a speed number line.

## 0.8.14

- Translated code now lives in working memory. Nothing writes into program memory any more.

## 0.8.13

- Memory probe test build and a normal build.

## 0.8.12

- The translator no longer writes its code byte by byte into program RAM. Added a write self test.
- A long wheel press returns to the Paradise even on an error screen.

## 0.8.11

- Further hardening of the jump guard. Same behavior as 0.8.10.

## 0.8.10

- Further hardening of the jump guard. Same behavior as 0.8.9.

## 0.8.9

- Further hardening of the jump guard. Same behavior as 0.8.8.

## 0.8.8

- A guard checks every jump into translated code and reports when one returns wrongly.

## 0.8.7

- Program code is write protected. A write into it shows an error screen with the place that wrote.
- Test build without the translator.

## 0.8.6

- Self test at start checks whether the device runs freshly written code correctly. If it fails, TamaEmu runs without the translator (slower).
- Test build that never uses the translator.

## 0.8.5

- TamaEmu no longer reads the guest ROM through the flash cache.

## 0.8.4

- At start the emulator code in memory is checked and repaired.

## 0.8.3

- Start hang report shows more lines (read test, error picture).

## 0.8.2

- Start screen shows numbers so that a hang on the real device can be located.

## 0.8.1

- Start on the real device reworked.

## 0.8.0

- TamaEmu runs much faster, with its own JIT.

## 0.7.0

- TamaEmu is faster and plays sound.
- Fixed game 3 (camera) hanging.
- Keys in the menu react at once and beep.
- A saved state from 0.6 is discarded.

## 0.6.0

- New Emulator tab. Choose your own colour Tamagotchi firmware (4 MB or 8 MB) and TamaEmu is built into the ROM.
- Start with A+C held for about two seconds on the home screen. Hold the wheel for two seconds to save and return to the Paradise. The next start continues where you left off.
- With an 8 MB firmware a download game (.jpg) can be put into one of two game slots.
- The guest display switches off after about 36 seconds without a key and wakes on a key press.
- TamaEmu core is derived from tamaemu by maragotchi (GPL-3.0).

## 0.5.0

- Baby, child and teen stay original (figure, eyes, cell). Only adults are replaced.
- A replaced adult keeps the eyes of the card figure. The cell has no eyes for that stage.
- The first adult is preselected as target.

## 0.4.1

- No brand names in the Studio. The tab is called "Card", figures are "No. 0" to "No. 16".
- ROMs are the same as in 0.4.0.

## 0.4.0

- The cell stays original with all eye expressions. The baby stays the original Tama and card figures start at the child stage.
- The switch "Make figure blink" is off by default. It is the old way from 0.3.0.

## 0.3.0

- Card figures get their own eye layer, so they blink and the cell shows eyes.
- Drag the eye frame by clicking the eye.
- The selected figure is shown large with a pink frame around the eye.

## 0.2.0

- First Studio release. Open a Paradise 16 MB flash dump, pick a figure, download its sheet, edit it and upload it again.
- Download all sheets as a ZIP, or upload a ZIP or many PNG files at once.
- Insert figures from a figure card image, including the egg.
- Number test ROM: each figure shows its number.
- Land, Water, Sky, Jade Forest, Tropical and Glacier are recognised. Archive checksums are recalculated and every changed graphic is read back before saving.

## 0.1.0

- Command line tool paramix.py builds ROMs from recipe files with figures from a figure card image.
- Three sample recipes: one evolution line, all figures of a card, and a number test.
