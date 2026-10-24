# Changelog

Versions are listed newest first.

## 1.6.9 (2026-10-24)

- .gitignore: v3/v3.bin and gb/gb.bin are allowed (an unpacked repository would have left them out).
- Game Boy sound stopped after the first notes on the device (reported with Super Mario Land: the first notes, then now and then a stray tone). The first notes had silence between them, so the buzzer was started anew each time; later notes follow each other directly and only the pitch of the running timer was changed, which leaves the buzzer silent on the device. Now every new pitch starts the timer anew; changes below about 1.5 % (vibrato) are left out (switch GB_TONE_RESTART). Also, when a shorter period lies below the running counter, the counter starts again (switch TONE_SAFE, only in GBEmu).
- Game Boy faster with many sprites (reported: Mario slows down when enemies are on the screen). Background and window are drawn a tile at a time (four dots from one table lookup, no clearing of the line), the shrinking to 128 x 115 works on four dots at once; the drawing takes about a third of the time it took (the whole frame about 35 % less), the picture is the same dot for dot (checked with Super Mario Land and Kirby's Dream Land 2). When the game still falls behind, up to three frames in a row are not drawn, so the game keeps its speed and only the picture gets fewer updates. Switches PGB_FAST_LINE (core), GB_FAST, GB_SKIP_ADAPT.
- V3Emu clock: when the toy's clock stands at the start (the toy started from its own save and asks for the time, like after a battery change), V3Emu looks again every 5 s for 10 minutes; once the clock was set in the game, the Paradise's time is written. Before, it looked only once, 2.5 s after the start. The settings line "Clock" shows what happened: AUTO OK (written), AUTO ! (the toy's clock stands, set it once in the game), AUTO - (no time from the Paradise), AUTO ? (written, but it did not take). Switch V3_CLK_RETRY.
- clock of the programs (reported: V3Emu and Tama-Go now and then without the time, for example after the Game Boy). The menu looks the Paradise's time up in the SRAM and leaves it in the mailbox for the programs; when neither the SRAM nor the mailbox has it (the Paradise restarted), it now takes the base of the Paradise's clock from its settings page 0xEFE000 (days since 2025 at +0x0C, seconds at +0x10, plus the seconds counter; CRC and date fields checked, the copy at 0xEFF000 as a second chance). Switch PARA_FLASH (menu). The programs themselves are unchanged (TamaEmu has no room for it); they get the time from the menu.
- no clock box for Game Boy games in the Emulator tab (the Game Boy has no clock).
- Game Boy: B and C (or B and A) held together were taken as the pair B+C (SELECT), so running to the right did not work (reported with Super Mario Land). Default keys now A left, C right, A+C Start, B+C and A+B free; SELECT is a line in the device's settings. Sound on the Paradise's buzzer (switch GB_SOUND): one tone at a time, the louder square channel (melody or effect), the wave channel when they are quiet; setting "Sound" on the device.
- Game Boy (GBEmu, new tab "Game Boy"): black and white Game Boy games (.gb) in the Paramix menu, core Peanut-GB (MIT). Screen shrunk to 128 x 115 (5 dots mixed into 4, nothing cut off). Keys set in the tab for all games (default A right, C left, B = B, wheel press = A, wheel up/down, A+C Start, B+C Select, wheel the other way round as an option); on the device A+B+C opens the settings (colour, light, dim, screen off, restart, language), wheel held 2 s goes back. The cartridge RAM (battery save) goes to the flash a few seconds after the game wrote it and is kept by "Keep saves". Checked in the simulation: Super Mario Land and Kirby's Dream Land 2 at full speed. No sound yet; not as the main program.
- the link values of the Paradise (key for the data over the contacts, token and mask of the download file) are no longer plain text in `src/flash.js`, they are kept as two mixed tables and put together when used. Same values, same function; the unit test checks them by their hash.
- the visit hint says that "Connect" above is only for the ROM and is not needed for a visit (feedback: the playdate worked).
- simulated visit for the Paradise (Device, "Simulate a visit"): the Studio plays a second Paradise over the dongle. Choose the edition, Start, then on the Paradise CONNECTION, PLAYDATE, B. The visitor is a copy of the own Tama with the chosen edition and its own IDs, so the Paradise registers that edition (secret characters need two other colours at the young stage). Protocol after GMMan (tama-para-research), flow measured with two emulated Paradise; checked on the PC with the emulated Land against the Studio in the browser. New file src/visit.js, test test/visit_test.js.
- TamaEmu "Full care" also fills the happy bar, but only as far as the game itself lets it grow (read from each firmware's own code): iD L the friend level (20 per medal plus 19, all four medals: full), Plus Color the HAPPY! bar (10 per medal plus 10), P's the ring (4 per medal plus 4), 4U and 4U+ the love ring (30, from the stage after child 100). Checked in the PC run: iD L without medals the first part full, with two medals three parts, with four all of it.
- care and points for more programs. V3Emu: "Gotchi Points" (4 digits, as its status page shows them) and "Full care" (hunger and happy 4 hearts). V1Emu: "Full care" (hunger and happy full; Digimon: hunger and strength) for all twelve known ROMs: P1, P1 English, P2, Angel, Genjintch, Morino, Tamaotch, Umino, Yasashii and Digimon V1, V2, V3. TamaEmu: the 4U (v001, without plus) too. All found in the PC runs and checked on the games' status pages; V1Emu and V3Emu also in the device simulation. Behind the switches V3_CARE, V1_CARE.
- V1Emu: the English P1 (tama.bin) is known by name ("TAMA P1 EN") with the P1's clock (set from the Paradise) and icons; an older V1Emu with it gets the name and clock when updated.
- V1Emu: Yasashii drives segment n as column n; with the P1 table a black block stood in the middle of its screen and columns were lost (found on the PC by reading the segments of its status pages). Behind the switch V1_DIRECT (Studio flag bit 7).
- Care and points on the device, in the settings (wheel short). T18Emu: "Tamapoints" (0 to 65535, A next digit, B digit + 1, C OK, wheel back without change) and "Full care" (hunger and mood full, not sick, no call), all nine known firmwares. T7Emu (Tama-Go and Tama Friends, same places): "Gotchi Points" (up to 999999) and "Full care" (hunger and happy 5 hearts). TamaEmu: "Gotchi Points" (up to 99999) and "Full care" for Plus Color, iD L (v050, v053, v054, v055; English, 15th anniversary, Princess Spacy), P's and 4U+: hunger full, on P's also the happy ring, on 4U+ also the love ring. The firmware is told by its reset vector; another one shows no such lines. All places were found in the simulation and checked on the games' status and profile pages, with every firmware we have (T18: a new baby in each, values set, profile page). Not yet: V1Emu, V3Emu, the 4U without plus. Behind the switches T18_CARE, T7_CARE, EMU_CARE (number entry ASK_NUM; to fit into TamaEmu: its menu without the list and picture pages, ASK_SLIM, ask.c built with -Oz and flash.c with -Os as in T7Emu).
- T18Emu hung while growing up (reported: baby to child, the yellow stripes stayed for minutes, also after a reset). Found with the dump: the firmware writes its save in sectors spread over its flash, and T18Emu keeps such sectors in a pool made of its spare blocks. With 6 spares the pool (96 sectors) was full; three sectors of a save block found no room, their writes were lost and the firmware wrote that block again and again. Now a pool sector the firmware has erased is taken over when the pool is full (switch T18_POOL_RECLAIM), and the Studio gives T18Emu in a start device up to 15 spares where there is room, also to an installed one: they are only added behind its spares, so "Keep saves" keeps its save. Checked in the device simulation with the dump: the growing goes on.
- T18Emu: wheel short with the screen dark only wakes it (as T7Emu). Before, the settings opened on the dark screen. The sleeping game is woken with a short C. Behind the switch T18_WHEEL_WAKE.
- Start device: the programs lost the Paradise's time after the first program. Going back, a program cleared the clock mailbox; a start device goes back to the menu, not to the Paradise, so the next program found no time (reported: T18Emu's clock did not set itself). The mailbox now keeps the time, moved on to the moment of leaving, and holds up to 400 days instead of one. TamaEmu, T7Emu, V1Emu, V3Emu, T18Emu and the menu; DOOM still clears it. Behind the switch PARA_MB_KEEP.
- Items menu on Jade Forest (GitHub feedback "food does not show up"): the bag list of Jade Forest is 4 bytes further on (0x244, amounts 0x744; read from its firmware, the count getter uses 0x740 from block + 4 where Land uses 0x73c). The menu wrote at Land's place: the first two items before the list, the next ones into it with their amounts one byte off. Toys showed, food (needs its amount) did not. The money stays at 0xb4. Checked in the Paradise emulator on Land: food from the menu shows in the feed menu with its amount (an apple x4, bread x3).
- Items menu on the device (GitHub feedback): an item put into the bag by mistake can be taken out again. The amount now goes down to 0, shown as "REMOVE"; the game itself treats amount 0 as not there. Entries the game keeps with amount 0 (eaten up) are no longer listed. Behind the switch BAG_DEL.
- T7Emu: the pet's own save (its EEPROM) could be written 2 bytes off. The flash controller copies the 4 KB by DMA in whole words; the page buffer lay at an address ending in 2, so the flash got 2 foreign bytes first and the pages without their last 2. The check then failed at the next start: the game began anew from the egg, and with "Keep saves" the saved state was dropped too because it belonged to that EEPROM (reported: Tama-Go new after writing a dump with "Keep saves"). The buffers are word aligned now; such a slot is still read (2 bytes on) and written again correctly. The Studio reads it too (Save tab, T7Emu save.bin). Behind the switch T7_EE_SHIFT.
- T7Emu, V1Emu, V3Emu (CLK_ECO): the millisecond tick is checked against the Paradise's seconds counter every 4 s. If it counted too few at a lower clock, that clock step is not used again until the next start, and T7Emu adds the missing time (the emulation catches up) (reported: Tama-Go's clock more than a minute behind after a few minutes with the screen off). Behind the switch ECO_RTC.
- T7Emu: with "Clock: Game" the game's clock is also kept on the Paradise's seconds counter while it runs (only its own time; the Paradise's time is not taken). Behind the switch T7_GAME_KEEP.
- V3Emu settings like T7Emu: "Icons inactive" in percent (how strongly the icons that are off show, 0 to 30 %), grid in percent (0 to 100 %), "Pixel view" in percent instead of shadow on/off (0 to 20 %), and "Clock" Auto or Game when the Studio knows the toy's clock. Behind the switch V3_SETX.
- T7Emu (Tama-Go) battery: the main loop runs the emulation in steps of 2 ms but slept only when it was exactly up to date; one millisecond behind it spun without sleeping until the next tick, about half of all the time. Now it sleeps then too. Measured in the simulation (Tama-Go in its room): the Paradise's CPU awake about 30 % of the time instead of about two thirds. Behind the switch T7_WFI_FIX.
- Jade Forest wave 2, Tropical and Glacier (GitHub issue): their firmware reaches past 0x0A0000, the Emulator tab said "This edition has no room for it". Now the emulators go into the menu instead of a main program at 0x0A0000 (normal ROM: Tama-Go, Friends, V1Emu, V3Emu; start device: all), the firmware there stays untouched. Colour Tamas and TG18 need the start device on these editions.
- V1Emu: Digimon V3 known by name ("DIGIMON V3" instead of "TAMA 48"): LCD turned, the Digimon icons, the Paradise's clock, battles between two Paradise over the contacts like V1 and V2 (V1_LINK).
- NOTICE.md: sections for V1Emu (TamaLIB, GPL-2.0-or-later) and V3Emu (BrickEmuPy, CC0 1.0).
- T7Emu, V1Emu and V3Emu save batteries with the screen off: the clock goes down step by step from 96 MHz to as low as 12 MHz (AHB prescaler of the Paradise's chip), as long as the emulation keeps up (measured with the core's cycle counter). Too slow: at once back to full speed, then one step less deep for 5 minutes. Full speed for a key, the screen, a tone or link traffic; SysTick and the tone pitch stay right. Behind the switch CLK_ECO (emu/src/emu/eco.h).
- TamaEmu and T18Emu: the same lower clock with the screen dark (CLK_ECO). Whether it goes down is decided on the device by how busy the emulation is; a colour Tama or the Meets that keeps the CPU busy stays at full speed. TamaEmu: the main loop is built for size to make room (the translator's area grows by 552 bytes). T18Emu: the list of written flash blocks is one bit per block (the 112 bytes were needed).
- V3Emu: the Tamagotchi Connection V3 on the Paradise (bring your own ROM, 512 KB). As the main program or in the menu, like V1Emu: keys A, B, C as on the toy, wheel short for the settings (colour, grid, shadow, sound, light, dim, battery), wheel long saves and goes back. The toy's own save (its EEPROM) goes to the flash 3 s after the game wrote it, the running state on leaving, when the screen goes off and every 15 minutes. The 6502 core (Sunplus SPLB32) is ported from BrickEmuPy by azya52 (CC0) and gives the same result to the bit; the timers only get the time when something can happen and the stopped CPU is skipped to its next timer event, so the Paradise has room to spare. No clock sync and no infrared yet.
- V3Emu: the ten menu icons around the screen, lit as on the toy (the dots three times enlarged then). Setting "Icons" (on by default); off shows the dots four times enlarged as before. The pictures are mameise's own V3 icons (the second one from T7Emu). Behind the switch V3_ICONS.
- V3Emu: the clock from the Paradise at every start (tick "Take the clock from the Paradise", on by default), with the date. The day is two digits (ones 0x13D, tens 0x13E); an earlier test build wrote it as one number, the toy showed "2." instead of 22 and counted the day up to 99 when set by hand. Only once the clock was set in the game; checked after writing. Behind the switch V3_CLOCK.
- V3Emu: connection between two Paradise over the contacts (both plugged together at the top). The toy's infrared pulses become bytes and back, as BrickEmuPy links two emulators; the bytes go over the UART like T7Emu and T18Emu. Behind the switch V3_LINK.
- V3Emu as the program that starts when the Paradise is switched on (main program, "Start when switched on"), and as the main program of a start device.
- Battery: the voltage and charge show in the settings of T7Emu, V1Emu, TamaEmu and T18Emu (wheel short) and on the home screen of a start device (bottom right). Measured like the Paradise does it (its ADC on P0.6); 2.2 V is empty, there the Paradise switches off. Behind the switch BAT_SHOW.
- T18Emu: grow up on the device (wheel short, "Grow up"), with the choice of the adult for a first generation Tama and, if you add your own pictures in the Studio, its picture. Behind the switch T18_GROW. With the Sanrio firmware the list shows Flowertchi instead of Pompomtchi; a child of a married pair grows up like its parents.
- T18Emu: items from MyMeets files (food, snacks, items, accessories, room) and Tamapoints in the Studio, written into the game's save.
- T18Emu as a further program in the menu stays where it is when the ROM is saved again (items, settings, another program removed). Placing it anew needed more room, because its save grows while it runs: "too little room" on a ROM read from the device.
- T18Emu grow up: the game clock runs fast only for the minute before each step, the step and its animation in real time, the screen stays on (it flickered, the Tama got hungry and ill, the animation took ages). Behind the switch T18_GROW2.
- T18Emu as a menu program: the Studio rewrote its settings with bytes of the older program (grow up wrote into the wrong place); now its addresses come from its firmware again.
- T18Emu items and Tamapoints with "Keep saves": the device brought back its old T18 save over the change; now the whole T18 save comes from the new ROM then.
- T18Emu started from the menu: a sleeping game gets woken once, the screen no longer stays black until a key. Behind the switch T18_WAKE_START.
- V1Emu: after the start the toy's clock is checked against the Paradise's time and written again if it is off (a resumed P2 kept its old time). Behind the switch V1_CLK_CHECK.
- T18Emu: the date moves on too. The game counts the days itself and a new start (CONTINUE) loaded the day of leaving; now the days since are added once after the start. Behind the switch T18_DAY_FIX.
- T18Emu items and Tamapoints: writing to the device asks first when "Keep saves" is ticked, because T18Emu's save is replaced by the one in the ROM.
- T18Emu grow up: 18 s instead of 25 s after each step.
- T7Emu: the Tama-Go's PC passwords work (Door, PC). The login password was always 000000000000 and no logout password was taken: the nibble helper at $3080 ($3082 high nibble, $3083 low nibble, $308A swapped) was missing. Behind the switch T7_PW_FIX.
- T7Emu (Tama-Go): Gotchi Points in the Studio (Emulator tab, ▾ of the Tama-Go). They go in like an imported save: the Tama stays, the running state starts anew.
- TamaEmu: two colour Tamas in one ROM (e.g. iD L and P's) both start their own firmware. The start check compared the program with the image at 0x0A0000 and copied the main TamaEmu's settings over the other one's: always the one added first started. Behind the switch APP_SELF.
- TamaEmu: the screen stays dark when the colour Tama wakes its display without its own light (the P's does it every 20 minutes for about 9 s, all night). The light now follows the toy's own backlight (timer channel 2), a key or a tone. Behind the switch BL_GUEST_PWM. The P's run state is no longer saved every 20 minutes at night because of it.
- T18Emu: visits and marriages over mixing-meets.com through the UART dongle on the Paradise's contacts. In the Studio (▾ of T18Emu) the "Paramix bridge" bookmark: clicked on mixing-meets.com, the page's Bluetooth goes over the dongle to T18Emu, which hands the data to the firmware's Bluetooth module like the Android T18Emu. Behind the switch T18_LINK (device) and in the core.
- T7Emu (Tama-Go) battery, as in TamaGo for the Pebble: the main loop's wait for the next frame is added up at once instead of run instruction by instruction (same result to the bit, switch T7_IDLE), and the Tama-Go's STOP (it stops its CPU until a timer or a key) now halts the emulated CPU too (switch T7_STOP). About 80 % of the emulated time no longer needs the Paradise's CPU.
- Items: names the Studio cannot read (Japanese letters) show as their number, the rows no longer run into the next box.
- T18Emu: visits between two Paradise (CONNECTION, TAMAGOTCHI: playdate, gift, marriage). The game's infrared bytes go 1:1 over the contacts as on the Android T18Emu's link: both Paradise plugged together at the top. Part of T18_LINK.
- T7Emu (Tama-Go): visits and the game between two Paradise over the contacts (connection, TAMA-GO, VISIT or GAME), like T18Emu (both Paradise plugged together). Behind the switch T7_LINK.
- TamaEmu (P's, iD L): infrared between two Paradise over the contacts, like T18Emu and T7Emu. The bytes of the infrared UART go to the other device and come in there at the toy's byte rate, with the P's waiting for its session as in tamaemu (Android TamaEmu: link.c, periph.c). Behind the switch TC_IRLINK. To make room in the 64 KB program, the start-up code now runs from the translator's area (it was only needed once): the translator keeps a little more room than before.
- V1Emu (Digimon): battles between two Paradise over the contacts (the Digimon's own contact P20, its pulses passed on with their timing). Behind the switch V1_LINK. Not yet checked with a real battle.
- V1Emu: register B of the toy is now kept with the running state (it stayed 0 after a resume). Behind the switch V1_REGB (on).
- T18Emu link: its RAM block was 20 bytes too big and ran into the rows of the spare sector table (0x20000F00); now checked when building.
- Visits between two Paradise: the Studio only says how (plug both together at the top), it does not pass anything on.
- MixingMeets bridge: the Studio says when it still holds the dongle and lets it go ("Release UART"); the bookmark says so too when the dongle is busy.
- T7Emu (Tama-Go): quick key presses are no longer lost. Each press is counted every millisecond and given to the game for at least 120 ms, then 100 ms released (the game needs that). Behind the switch T7_KEYQ. And for 1 s after a key the emulated CPU no longer stops (T7_STOP): asleep it missed the release and took the next press as the same one. Switch T7_STOP_KEYACT.
- T7Emu (Tama-Go): one press no longer counts twice (the key contacts bounce; to the clock and straight back to the room). A press counts after the key was up for 30 ms. Part of T7_KEYQ.
- T7Emu (Tama-Go): progress survives a battery change. The automatic run state (up to 30 minutes old) won over the game's own save after a battery change: items bought since (e.g. in a figure shop) vanished, and with an early run state the whole game started anew. A run state now notes which save was in the flash; if the game has saved since, it starts from its own save like the toy after a battery change (switch T7_SESS_EESEQ). What the game saves goes to the flash 5 s later instead of after a minute without keys (T7_EE_SOON).
- T7Emu: melodies no longer lose notes. After drawing the emulation catches up several steps at once and the notes (about 26 ms each) came within a millisecond; now the tone changes are queued and played on the real clock, as in T18Emu. Behind the switch T7_TONE_Q. Tones start about 80 ms later than before.
- T18Emu screen: rows were not sent when their check value stayed the same although the picture changed. The old check (rotate and xor) gave the same value for many rows (a row of one colour, frames, AM/PM), more so since the link (a 1.6.9 test build) halved it to 16 bits. Now multiply and xor per pixel pair (T18_SUM_FNV), plus one row per picture always sent (T18_ROW_RR). Started from the menu, the first picture is complete again (T18_PRES_FIRST).
- T18Emu: switching the screen on (a call in standby) no longer stops the emulator for 145 ms while the panel wakes; it wakes in steps from the main loop (T18_WAKE_ASYNC).
- Start device with DOOM: any program of the menu can be the main program (star in the Emulator tab) and start when switched on. DOOM stays where it is and goes to the menu list. Before, DOOM was always the main program. Behind the switch FELD_AUTO (menu). The main program is listed first; hold B when switching on for the menu; after going back the menu stays.
- Start device with DOOM: the menu named DOOM after the colour Tama chosen in the Emulator tab (e.g. two times ID L); now DOOM.

## 1.6.8 (2026-10-19)

- Start device: the Paradise as a device for programs only. After its own setup the PARAMIX menu comes up with the emulators and DOOM, a SETTINGS entry (sound, display time) and DOWNLOAD (starts the flash program for the PC). All game pictures go, about 5 MB more room; colour Tamas (TamaEmu) and a TG18 (T18Emu) run as further programs there.
- V1Emu (new): the first Tamagotchis (P1, P2, Angel, Morino, Umino, Yasashii, Genjintchi, Tamaotchi) and Digimon V1/V2, from your own ROMs, about 88 KB each. Clock from the Paradise for P1, P2, Angel, Morino and Digimon; turning the wheel shakes the toy; Digimon and Yasashii turned the right way; own icons for Angel and Digimon. Digimon: its icons, its beeps (one-shot buzzer) and its keys (key interrupt) now work.
- Emulator tab: one button for all emulators, every program with its row (▾: clock, start when switched on, figures, download slots), a star for the main program, DOOM in the list, each program once, a program that no longer fits is refused when chosen.
- TamaEmu and T18Emu settings in the Studio and on the device (wheel short): volume, brightness 5 to 100 %, screen off after.
- Light: all emulators now dim the backlight itself, not the colours. T7Emu and V1Emu: "Light" 5 to 100 % and "Dim to" 1 to 50 %.
- Battery change: every emulator saves its run state by itself (screen dark, or every 30 minutes); a session is used once.
- The Paradise no longer lives the time spent in a program at once when you come back (hunger, care misses): its "lived up to" date moves on by that time.
- Save tab: Programs (back up and restore the save of every program) and Paradise (with the editor and, with several saves, the slot to export or write).
- Names tab: rename fields, Tamas and items. Figure drawer: move the eyes and the mouth, per pose. Card tab: TamaSma cards too. Glacier and Tropical with six fields: all six names.
- T18Emu: much faster (translator, idle detection, cached flash), melodies on the real clock, camera and diary on all nine firmwares, the clock after the game's own setup, the save data of the firmware by the 4 KB sector (a 7th block was lost before: the diary hung). Starts again after a test build put start code into the flash window.
- TamaEmu: the 4U and 4U+ need one press per step (NFC chip answered), the iD L clock no longer runs fast with the screen off, only finished pictures, the clock after the game's own setup.
- Settings of the start device and the clock from the Paradise also when started from the menu.
- T18Emu is no longer marked as a test version.
- Fields tab: "Game menu Fields: all fields from level 4": the game's own field menu offers all six fields and the Tama stays (adults only get the three fields of their set).
- Menu: new entry TAMA ("Change Tama" in the Menu tab) sets level, character and care (fed, happy, healthy); the same in the Studio's save editor.
- Menu: after a field change inside the game the menu says "The Tama moved to ... in the game" and asks whether to delete or keep the older save of that field. "Switch was interrupted" only comes after a real power loss.
- Menu: one tick "Several saves (with field choice)". SAVES lists SAVE 1 to 3 (up to 6) with field and Tama, an empty slot starts a new game on a chosen field. A ROM with the old menu per field is converted when written, the stored saves stay.
- A ROM with T18Emu and the menu no longer loses the T18 firmware when it is saved again in the Studio.
- TamaEmu: switch "4U: emulate the NFC chip" (4U and 4U+ only, on by default).
- Menu entries are named after the firmware: TAMA-GO, FRIENDS, ID L, 4U+, PS, PLUS COLOR, TAMA MEETS. iD L versions are named after their version (ID L, ID L JP, ID L 15TH, ID L SPACY).
- Studio: a start device ROM is recognised when loaded (DOOM and emulators); the same firmware chosen again is not added twice.
- Start device: the 1 MB behind the Paradise's save is free for programs, except with T18Emu.
- Menu tab: "All 6 fields" can be unticked again on a ROM with six fields.
- Menu: saving SETTINGS, items or Tamapoints no longer gives error 41 (the menu's flash buffers are word aligned).
- Writing with "Keep saves": the same firmware chosen again no longer counts as a new one (the save stays, no question); a colour Tama keeps its save when an item goes into a store that already has items; a T7Emu or V1Emu that moves between main program and menu takes its save along.
- The menu takes up to 12 further programs and pages through them.
- V1Emu settings (wheel short): colour, grid, shadow, icons, sound, light, dim, screen off, clock, language. V1Emu can be the main program, also in a normal ROM (A and C, start when switched on).
- DOOM in the program list with × and ↺. DOOM as the main program of a normal ROM can start when switched on and stays in the Paradise after leaving.
- Normal ROM: choosing a colour Tama or TG18 makes it the main program; a Tama-Go or Friends that was main goes to the menu.
- All emulators: no white flash when the screen wakes; the display gets 120 ms first, then the light (switch WAKE_QUICK for the old way).
- TamaEmu and T18Emu: the settings menu looks like the one of T7Emu and V1Emu.
- All sprites: the field pictures added from Land, Water or Sky show after saving and loading again (pale, locked).
- T18Emu: a visit (CONNECTION, TAMAGOTCHI) without a partner waits and goes back after a few seconds instead of hanging.
- V1Emu Digimon: the picture is no longer shifted by 3 pixels (own LCD mapping).

## 1.6.7 (2026-10-16)

- More emulators (started from the menu): they never started on the device and went back to the Paradise. The cached flash window shows only 1 MB, the further programs lie higher up. The launcher now copies them from the uncached flash (stub_feld_tpl.bin rebuilt).
- Writing with "Keep saves": Tropical and Glacier (and Land, Water, Sky) are the same firmware, only the active field differs. A device on Glacier with a ROM made from Tropical no longer refuses; the device keeps its save and its field, a note says so.

## 1.6.6 (2026-10-16)

- T7Emu: new light settings. "Dim after" (10 s to 10 min or never), "Dim to" (1 % to 50 %, only when dimming) and "Screen off after" (never or a time, always longer than the dim time). Dimmed means darker colours (the backlight PWM had no effect on the device).
- T7Emu: when the pet calls, the screen comes on (or bright again), as after a key.
- T7Emu: the Tamagotchi Friends gets its own icons around the screen.
- T7Emu, T18Emu, TamaEmu: the wheel within the first 3 seconds after a resumed start now asks first ("Reset?", A yes, B no). A press meant for the menu no longer throws the game away.
- TamaEmu: the Tama's own save is also written while the screen stays on: after 20 seconds without a key, at the latest 10 minutes after the first change. Before, with "always on", it could wait until leaving, and a power loss lost it.
- TamaEmu: saves of the Android emulator with download content (more than 80 blocks of 64 KB, P's) did not fit and could not be used. The sparsest blocks now go into spare pages behind the firmware, as the device does with blocks it writes; exporting gives the same .sav back.
- TamaEmu: download content (.jpg) for the P's, 4U+ and 4U as well, not only the iD L (stores and routing from tamaemu's dlc tool).
- TamaEmu: the reset question follows the language of the Studio.
- T18Emu (experimental) a lot faster: the most read firmware blocks lie in a part of the Paradise flash that is read through the cache; blocks that are empty in the firmware are answered at once; a bigger buffer for translated code; the idle check runs from the fast memory. The screen only shows finished pictures. The last written part of the firmware's own save is written back after 3 seconds (before only when leaving).
- T18Emu: the speed display and the code screens of the test builds are gone.

## 1.6.5 (2026-10-15)

- T18Emu: 1.6.4 hung on the device at the black start screen. At the start every firmware block is now checked: only where the cached flash window shows the same as the flash itself is it read through the cache, otherwise uncached as before.
- T18Emu: the speed display (wheel short) shows a second line "C" with the number of firmware blocks read through the cache.

## 1.6.4 (2026-10-15)

- T18Emu faster: the firmware's flash (pictures, code) is now read through the cached flash window instead of the uncached one (about 1.5 us per access on the device; in the simulation the start alone made about 300,000 such accesses per second). Blocks the firmware writes to are still read uncached. Switch T18_FLASH_UNCACHED for the old way.
- T18Emu: the picture goes to the screen at the moment the firmware has finished it. Before it was read later, while the next one was being drawn: built up from the top, black behind texts. Switch T18_PRESENT_LATE for the old way.

## 1.6.3 (2026-10-15)

- T18Emu: the next red screen E1 (UNALIGNED). The translator's helper for memory accesses read its two data words after the call with one double load (LDRD); they are only halfword aligned, and the Paradise's processor faults there (the PC emulator did not). Now read as halfwords.
- Device simulation: ALIGNCHK=1 reports every LDRD/STRD/LDM/STM/PUSH/POP with an address that is not a multiple of 4.

## 1.6.2 (2026-10-15)

- T18Emu: cause of the red screen E1 found. On the device a halfword write into the PRAM fills the other half of its word with the halfword written just before (somewhere else). The translator patches single halfwords, so the first translated block got a broken instruction. The translated code is now written as whole words (read, change one half, write the word). Build switch T18_PRAM_HALF for the old way.
- Device simulation (t18/test/devt18.py): PRAMQUIRK=1 behaves like that PRAM; with it the old build stops exactly like on the device, the new one runs. IRQ=1 takes the SysTick as a real exception.

## 1.6.1 (2026-10-15)

- Saves of the further emulators: in the Save tab a choice "T7Emu" (main emulator or one of the further ones) appears; backing up and taking over a save.bin works for each. "Back up save" on the device also fetches the save of every further T7Emu of the loaded ROM.
- "Keep saves" with a save.bin taken over for a further emulator: its new pet stays, its settings come from the device.

## 1.6.0 (2026-10-15)

- More emulators in one ROM: in the Emulator tab "Add another emulator" takes one more Tama-Go or Friends ROM (up to 6). Each runs its own T7Emu with its own save and is started from the menu (A and C), under "PROGRAMS". The menu must be on (field switch or items).
- The menu (settings block version 5) lists the programs; the launcher has a second entry for that. "Keep saves" keeps the save of each further emulator while its ROM stays the same.
- The flash bar counts the further emulators.

## 1.5.6 (2026-10-15)

- Writing to the device with "Keep saves" after T18Emu: the old firmware blocks of T18Emu are no longer kept as if they were a save of the Paradise. They are freed (the new ROM's content goes there), and the Studio no longer reads about 1 MB from the device before writing.
- Writing: if the device does not answer right before writing, the Studio asks again (with a ping in between) instead of stopping at once; if it still does not answer, a clear message says that nothing was written.
- Web Serial: a buffer overrun, framing or parity error no longer ends the reading for good; the Studio takes a new reader.

## 1.5.5 (2026-10-15)

- T18Emu: the red screen E1 shows 16 values in two columns (fault frame, stack, fault status, start stage, translator).
- T18Emu: self test at the start: code written into the PRAM where code already ran must run as written, otherwise red screen E6.
- T18Emu: holding B while T18Emu starts runs the firmware without the translator (slower, for tests).

## 1.5.4 (2026-10-15)

- T18Emu: red screen E1 at the start on the device. The translated code is now written into the PRAM by a small routine running from the SRAM (the way TamaEmu does it on the device) instead of from the flash window.
- T18Emu: the red screen E1 shows more: PC, LR, fault status, fault address, how far the start got and the firmware's PC.

## 1.5.3 (2026-10-15)

- T7Emu runs the Tamagotchi Friends too (test): choose its ROM (768 KB) in the Emulator tab instead of the Tama-Go ROM. Same settings, saves and clock as with the Tama-Go. There are no figures for it.
- T7Emu core: the Friends ROM has 16 KB more in front and its bank 0 sits differently; its frame tick comes from another interrupt. Both are recognised from the ROM, the Tama-Go runs as before.

## 1.5.2 (2026-10-15)

- T7Emu: the full figure dumps (4 MB, with the extra games) can be added. The Studio keeps one copy of the 1 MB figure (the dump holds it four times), so it takes about 700 KB. In the menu it shows as "... Full".
- The hint on the number of figures is gone.
- New "Help" button at the top: steps, buttons on the Paradise, clock, saves. Opens and closes on a click.
- The repository allows t7/t7.bin, t18/t18.bin and t18/t18x.bin (.gitignore).

## 1.5.1 (2026-10-15)

- T18Emu runs on the device. 1.5.0 hung with a black screen: the RAM at 0x20000000, where it kept its data and the translated code, has only 4 KB on the Paradise. Now the hot code and the translated code are in the PRAM, the rest of the code runs from the flash, the data is in a part of the SRAM the firmware does not use.
- T18Emu: a red screen with a code instead of hanging when something goes wrong; the wheel goes back to the Paradise.
- T18Emu: the firmware is placed from the top of the flash down, so the field switch of the menu now fits next to it (Land, Sky, Water).
- All emulators: new switch "Take the clock from the Paradise at the start" (tab Emulator, on by default). At every start the emulator sets its clock to the Paradise's time, nothing is caught up.
- Writing to the device with "Keep saves": if the device has an emulator whose save cannot be kept (another emulator, or a newly chosen firmware), the Studio says so before writing, so it can be backed up first.
- README: how the emulators behave when leaving and coming back, the clock, the shortcuts.

## 1.5.0 (2026-10-15)

- New emulator T18Emu (test version) for the Tamagotchi Meets and On: choose the TG18 firmware (8 MB) in the Emulator tab. The Studio places its used blocks into free blocks of the Paradise flash and keeps spares for the firmware's own save data.
- On the device: hold A and C on the home screen to start, hold the wheel to go back (saves). A short press on the wheel shows the speed. The firmware runs through a translator from ARM to Cortex-M3 code; loops that only wait are skipped. The pet screen may still run a little slower than on the toy.
- "Keep saves" keeps the T18Emu save as long as the firmware is the same.
- The menu on A and C shows T18EMU.

## 1.4.2 (2026-10-15)

- T7Emu: on a new install the screen starts pixel exact and without the pixel look.
- Studio: the Emulator and Save tabs now name both emulators (TamaEmu for colour tamas, T7Emu for the Tama-Go) and both save formats.

## 1.4.1 (2026-10-15)

- T7Emu: the first time setting got stuck. The game only takes the time once the hour was changed, so at midnight it started over and the egg did not hatch. T7Emu now fills in the time: the one written along by the Studio when writing to the device, or after a restart the game time from before. Birthday and name follow as on the toy.
- T7Emu: the clock no longer corrects the time while the game asks for it.
- T7Emu settings: light and dark grey, grid, pixel view, icons in steps, clock (Auto or game), language. New "Screen": Large fills the width (the pixels are 2 or 3 dots wide), Pixel exact keeps every pixel the same size (smaller).
- The menu on A and C shows T7EMU instead of PROGRAM.

## 1.4.0 (2026-10-14)

- New emulator T7Emu: choose the ROM of the Tama-Go (640 KB) in the Emulator tab, like a firmware. Figure ROMs can be added (up to 16) and removed again.
- On the device: hold A and C on the home screen to start, hold the wheel to go back (saves). A short press on the wheel opens the settings: figure, background colour, Tama colour, pixel mode, icons, sound and light. They are kept on the device.
- The clock of the Tama-Go follows the Paradise clock. After a pause the missed hours pass (up to 30).
- Tab "Save": the T7Emu save as a .bin file (the same as the save.bin of the T7Emu app), out of the ROM and back in. "Back up save" on a device with T7Emu also downloads it.
- "Keep saves" keeps the Tama-Go save as long as the Tama-Go ROM stays the same.

## 1.3.4 (2026-10-14)

- Fix: 1.3.3 did not load any ROM because of a broken text. Every script is now checked when building.

## 1.3.3 (2026-10-14)

- TamaEmu: Connect on the iD L (Plus Color) hung. Code that keeps overwriting itself made the emulator throw away its translated code thousands of times per second. Such parts now run without translation; after the waiting time FAILED appears and the iD L goes back as usual.
- Emulator tab: the hint says how to restart TamaEmu if it hangs.

## 1.3.2 (2026-10-14)

- TamaEmu: firmware of the P's, 4U+ and 4U is recognised. Their firmware is bigger than the room so far; it now goes into a larger area of the ROM. With such a firmware the field switch of the menu has no room (items in the menu still work).
- TamaEmu: the 4U+ and 4U run with their own clock (30 MHz). They need more computing power than the iD L, so games run slower on the Paradise.

## 1.3.1 (2026-10-14)

- Tab "Save": own buttons to import a save. A TamaEmu .sav goes into the ROM, a Paradise .tamasave onto the connected device.
- Switching the language now updates every text, also in the tabs.
- Some error messages now have a German text.

## 1.3.0 (2026-10-14)

- Save games: back up the Paradise save from the device as a .tamasave for the TamaParadise app, with the device's clock, and the TamaEmu save as a .sav for the TamaEmu app. A .tamasave goes back onto the device; a save of another edition is refused. New tab "Save" for the same from a loaded ROM.
- TamaEmu: a .sav from the TamaEmu app can be chosen like a firmware, so the game carries on on the Paradise.
- Device: the edition is now read from the device's flash. Before, every device counted as Land, so "Keep saves" refused other editions.
- Flash app: reads and sets the Paradise's clock.

## 1.2.12 (2026-10-14)

- Menu: the saved fields share one room instead of a fixed slot each. With all 6 fields on Tropical or Glacier a slot was only 72 KB, so a much played save stopped the switch with error code 11. Now one field can use up to about 430 KB there (the room the other fields do not need). Saves of older versions are still read; they move to the new form at the next switch.
- Menu on Tropical and Glacier: since 1.2.2 the menu wrote the device ID to the wrong page (0xD49000, inside the save area) and left the real ID page alone. Fixed: the ID page is set again on every switch.
- Menu program rebuilt.

## 1.2.11 (2026-10-14)

- Menu: field saves get all the free room. Without emulator or DOOM each field slot is now up to about 1.7 MB instead of 352 KB. A large save (much played, many items) no longer stops the field switch with error code 11. Saved fields on the device are moved to the new slots when writing with "Keep saves".
- Menu: error codes 10 and 11 now say "SAVE TOO BIG" on the device. The active field stays as it is.
- Menu program rebuilt.

## 1.2.10 (2026-10-14)

- Start page: describes everything the Studio does (TamaEmu, DOOM, more fields, menu, device, characters) instead of only the character sheets.

## 1.2.9 (2026-10-14)

- TamaEmu: if the screen of the Tamagotchi is dark right after resuming (its own screen saver ran out while the clock caught up, often after the autostart), the emulator presses C once to wake it. Only in the first seconds after the start and only while the screen is dark.
- Emulator program v0.9.5.

## 1.2.8 (2026-10-14)

- TamaEmu autostart: going back with a long press now keeps the Paradise, also after its screen went dark (deep sleep). The emulator comes back with A and C, the reset button, or after the battery was out. Holding B while the Paradise starts skips the emulator.
- Emulator program v0.9.4 (tells the stub that you went back on purpose).

## 1.2.7 (2026-10-14)

- TamaEmu: new switch "Start when switched on". After power-on the Paradise goes straight into the emulator. Holding the wheel goes back to the Paradise as before and it stays there until the next power-on (A and C start the emulator again). Holding B while switching on skips the emulator once.
- Stub: the original home screen handlers now sit at a fixed place in both stub variants. Menus of older versions are still recognised.

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
