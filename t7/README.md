# T7Emu for the Tamagotchi Paradise

An emulator for the Tama-Go (6502 based V7 chip) that runs on the Paradise. It also runs the
Tamagotchi Friends (same chip family, test).
The Studio builds it into the ROM together with the user's own Tama-Go ROM and
figure ROMs. No ROM is part of this repository.

- `t7.bin`: the program. The TamaEmu launcher copies it to PRAM and starts it.
  Its settings block ("PMX7") is at 0x140 and is filled in by the Studio
  (`src/emu.js`, `t7Patch`).
- `src/core`: the emulator core, a C port of
  [tamago](https://github.com/asterick/tamago) by asterick (BSD), taken over
  from the T7Emu app. `t7_run.inc` is a faster run loop with the same results
  as `t7_cpu_step` (kept for the host tests).
- `src/app`: the Paradise side. `t7app.c` has screen, settings menu, clock
  and saved state and also builds on the PC; `main.c` has the hardware part
  (it uses `emu/src/emu/hw.c` and `flash.c`).
- `host`: PC tests. `hrun.c` boots the ROM and saves screenshots, `happ.c`
  tests the app logic (clock, catching up, save, figures), `runcmp.c` and
  `tr.c` compare the fast loop with the old step function. `setup.txt` is the
  key sequence that finishes the first setup and hatches the egg.
- `test`: `devt7.py` runs `t7.bin` in Unicorn with a simulated Paradise,
  `proft7.py` profiles it.

## Build

```
sh src/app/build.sh          # clang, ld.lld, llvm-objcopy; writes t7.bin and t7x.bin (the JIT, second part)
KEEPELF=1 sh src/app/build.sh   # keeps src/app/t7.elf for the profiler
```

## Layout in the Paradise flash

| Offset | What |
| --- | --- |
| 0x0A0000 | program (copied to PRAM on start) |
| 0x0C0000 | session (state of the running emulator, "T7SS") |
| 0x0C1000 | EEPROM of the Tama-Go, two slots of 8 KB ("T7EE", sequence, page mask, checksum, then 4 KB) |
| 0x0C5000 | settings from the menu ("T7S2", 16 bytes; layout 3 in r1 bits 0..3: light in bits 4..7, dim level in r0) and at +32 the clock note ("T7CK": seconds counter, game time, month and day when leaving) |
| 0x0C8000 | second part of the program (t7x.bin, the JIT, 1.6.12 test2) |
| 0x940000 | Tama-Go ROM (640 KB) or Tamagotchi Friends ROM (768 KB) |
| behind it, up to 0xD40000 | figure ROMs, trailing empty bytes cut, each on a 4 KB boundary |

The save.bin of the T7Emu app (page mask, then the 4 KB EEPROM, 4098 bytes)
has the same content as one EEPROM slot, see `src/saves.js`.

## More instances (1.6.0)

The program only uses its settings block for ROM, figures and save area, so the
Studio can put several copies into the flash (one per ROM, see the main README,
"More emulators"). `test/devt7.py` runs one of them with `APPOFF=<image offset>`.

## Tamagotchi Friends

The Friends ROM (768 KB) is recognised by its reset vector: not at 0x3FFC like the Tama-Go but at
0x7FFC. Differences, all in `src/core` behind `g_t7_static_off`:

- The file has 16 KB more in front. The static ROM ($C000) is at 0x4000, bank 0 begins at 0x4000
  (its code is linked to $8000), bank n >= 1 at (n + 1) * 32 KB. 23 banks.
- The frame tick comes from IRQ 3 (every 122nd, counter $55) instead of the video IRQ. Instead of
  7.3 kHz of interrupts the core sets the counter just below and fires IRQ 3 once per video frame.
- The second of the game clock is at 0x136 (Tama-Go 0x396); hour, minute, day and month as on the
  Tama-Go.
- The IR hooks of the Tama-Go (bank 3) are off. No figures.

## Clock

Settings flag bit 1 (the Studio's "Take the clock from the Paradise at the
start", on by default): at every start the game clock is set to the Paradise's
own time, read from its SRAM before T7Emu uses it (`../emu/src/emu/paraclock.h`).
Nothing is caught up. When the game asks for the time (hour 255) it gets the
same time. Without the flag (or with the clock set to "Game" in the menu, or
when the time is not found) the old way: after a pause the missed hours pass,
up to 30.

## Timing

The core runs at 3.03 MHz with a video interrupt every 66,667 cycles (60 Hz).
The game clock is kept on the Paradise's seconds counter: every 2 s the app
sets minute and second; when a whole hour is missing it lets the game roll
the hour itself, so the day goes on as in the game. After a pause up to 30
hours are caught up, then the hour is set directly.

## PC passwords (1.6.8-test4, T7_PW_FIX)

The PC password code (bank 2, login $6229, encoding $6378, logout check $643E, reward $6192) uses a nibble helper: the
game writes a byte to $3080 and reads its high nibble at $3082, its low nibble at $3083 and the byte with swapped
nibbles at $308A. The core returned 0 there, so the login was always 000000000000 and every logout decoded to zeros
and failed. With T7_PW_FIX the login changes every time and a correct logout is accepted (host test: 0 to 5000 points).
Logout: same format as the login; byte 0 = 0x80 | login byte 0, byte 1 = login byte 1, points in thousands in byte 3
and the low 2 bits of byte 4, checksum nibble. Gotchi Points: RAM $0320-$0324 and EEPROM 0x030-0x034, 5 bytes BCD
lowest digits first (save.bin of the app: file offset 0x32-0x36, behind the 2 byte page mask); no checksum.

## Battery (T7_IDLE, T7_STOP, 1.6.9 test12)

Taken over from TamaGo for the Pebble (v0.3.0). T7_IDLE: the Tama-Go's main loop waits at $CE66 (5 JSR to the seed
routines at $D2ED, LDA $2D, BEQ back); when the BEQ goes back and no IRQ is taken, the passes that fit up to the end of
the run or the next sound event are added up at once (seeds $C4..$C8, the stack slots of the last JSR, A, flags).
Checked against the ROM bytes at the start (t7_idle_check; Friends: off). Bit-identical: host/runcmp.c gives the same
state hash every 2 ms with and without the switch. T7_STOP: writing 7 to $3001 halts the CPU (original sound only,
its timers wake it); any IRQ request or a key ($3007 bit 0 set, reported in $3008 bit 0) wakes it. On the PC (runcmp,
sound on, 10 minutes after setup): 78 % of the emulated cycles skipped, the game clock the same as without.

## Power save with the screen off (T7_JSR_HLE, T7_DARK_STEP, T7_SND_LAZY, T7_PW, 1.6.12 test1)

Measured on the PC with a hatched Tama in its room (host tools, see NOTIZEN): the 6502 runs about 213000 instructions a
second, 77 % of them in the sprite blit at $E4CD; with the screen off nobody sees it. Device simulation: nearly half of
the remaining time went into the sound timers (3322 and 512 events a second) although their IRQs are masked.

- T7_JSR_HLE (core): JSR $E4CD is left out while g_t7_dark is set (screen off): the game state stays the same apart from
  the random seeds $C4..$C8, the picture comes back with the next frames. JSR $D460 (a delay: LDX #$D1, INX/BNE, RTS)
  is done at once when it ends inside the run: the same state and cycles (runcmp identical). Both checked against the
  ROM bytes at the start (Tama-Go only).
- T7_SND_LAZY (core): a sound timer whose IRQ is masked gets no events; when the mask opens (or the timer is set) its
  next overflow is put where it would be. runcmp with sound: identical.
- T7_DARK_STEP: screen off: the emulation in steps up to 50 ms (the same emulated time).
- T7_PW (setting "Power save": Normal, Strong, Maximal; default Strong): screen off, no menu, no tone, no link:
  Strong runs the chip from its internal 12 MHz RC with PLL and crystal off (datasheet: 1.6 mA instead of 9 to 12 mA),
  Maximal also goes to deep sleep (0.4 mA) whenever the emulation has caught up; the RTC (1 s, as the Paradise sets it)
  or a key wakes it, the ms tick continues from the RTC. The setting "Sleep/12M" shows the share of the time asleep and
  at 12 MHz since the start (for tests).

Device simulation (test/devt7.py models the clock source and deep sleep), 35 s screen off: awake at 12 MHz 7 %, asleep
93 %; before: 48 MHz all the time.

## JIT (T7_JIT, 1.6.12 test2)

`src/core/t7_jit.c` translates straight runs of ROM code ($4000 up) into Thumb-2 code: 6502 A, X, Y, S and the flags
in ARM registers, memory through the page table (work and display RAM at a fixed address directly, the rest through
two shared stubs; an I/O write ends the block). A block runs only when the interpreter would run all of it in this
call (cycles left > its cycles before the last instruction, no sound event inside), with the same cycle sums, so the
result is the same instruction by instruction. Blocks follow each other inside the translated code while that holds
(exit stub with its own lookup). Blocks start where a run of code starts (after a jump or a block) and are made on
the 8th visit; keyed by the physical address, so banked code needs no flushing; buffer full: all thrown away.

Checked under qemu-arm against the x86 interpreter (host runner, state hash every 2 ms, 600 s with keys and sound):
identical. Device simulation (Unicorn, screen on): about 6,400 instead of 35,000 ARM instructions per ms.

The translator does not fit into the 64 KB of PRAM: it is a second part (`t7x.bin`, about 8 KB, magic "T7X1", build id,
length, sum) that the Studio writes into the save area at +0x8000 (main program 0x0C8000, menu instances save + 0x8000).
The program copies it into the SRAM behind its data; the rest of the SRAM up to the stack holds the translated code
(about 44 KB). Without a matching second part (older Studio, other build) T7Emu runs as before. Setting "JIT" (On/Off,
"missing" without the second part), settings byte r3 bit 0.

## Volume (T7_VOL, HW_VOL5, 1.6.12 test3)

The buzzer gets a pulse per period; its length sets the volume. Before: 1/32 of the period. Setting "Sound": Off, Quiet
(1/32, as before), Medium (1/8), Loud (1/4). A half period (square wave) was tried and was quieter than 1/4 on the
device. Stored in g_set.sound (old value 1 = Quiet).

## Tone and fixed settings (1.6.12 test4)

- T7_TONE_EV: the tone changes come from the core's tone events (t7_snd_tone_events: a look every ms of game time, a
  new pitch only when it stays 4 ms, silent when the envelope at $93 has died away, with the cycle it happened). Before,
  the main loop looked every 2 ms: at each note start the next pitch sounded for 2 ms, then 4 ms of silence, then the
  note (host comparison of a melody: a click at every note); notes rang on after their envelope.
- T7_TONE_ISR: the tone queue (T7_TONE_Q) is played from the 1 ms tick instead of the main loop, where a note change
  waited for drawing and the emulation. HW_TONE_IRQSAFE: hw_tone with the interrupts off, TONE_SAFE: a shorter period
  below the running counter restarts it (otherwise silent until the counter wraps).
- T7_FIXED: power save maximal and the JIT always, without settings (tested over night).
- T7_WHEEL_RAW: the wheel's long press is timed with a tick count that is never set ahead (g_ms is moved on after
  deep sleep and by eco_rtc, up to a second at once: a short press right after waking counted as long and left).

## Game clock tempo (T7_TBH_RATE, 1.6.12 test2)

The game counts its seconds from the 2 Hz timer (IRQ 13, two passes of $D1A3 per second). The core fired it every
2,000,000 cycles (4 MHz); the Paradise runs the core at 3.03 MHz for the tempo, so a game second lasted 1.32 s and the
clock note set it ahead by 2 to 3 seconds every few seconds (the seconds jumped). T7_TBH_RATE=1515150: two per real
second.

## Keys (T7_KEYQ, 1.6.9 test13)

The main loop read the keys once per pass; a pass that draws and catches up took longer than a quick tap, the tap was
lost. The 1 ms tick now counts every press (GPIO0 P0.9 A, P0.11 B, P0.10 C, pressed = 0); each counted press goes to
the game for at least 120 ms of emulated time (longer while held), then 100 ms released (test14: on the PC the
firmware lost taps shorter than about 120 ms or gaps shorter than about 80 ms, also without STOP).
test15: a press counts only after the key was up for 30 ms (KQ_DEB): the contacts bounce, one press
was taken as two (clock and straight back to the room).
T7_STOP_KEYACT (cycles, 4000000 = 1 s): after a key change no STOP. Halted, the firmware missed the release
(only IRQ 13 enabled, 2 Hz) and took the next press as the same one: 3 quick taps, 1 or 2 seen. Presses that wake the screen,
close the settings menu or come while it is open do not reach the game. host/hrun.c "k A hold gap" taps in 2 ms steps:
the core takes taps of 20 ms; test/devt7.py feeds the GPIO and the press counter.

## Save after a battery change (T7_SESS_EESEQ, T7_EE_SOON, 1.6.9 test16)

A user report: items bought in a figure shop vanished, after taking the batteries out the game was new. The run
state (session, 0x0C0000) holds the CPU, RAM and display, not the EEPROM; SESS_AUTO writes it every 30 minutes or
when the screen goes dark. After a battery change that old run state was resumed: its RAM won over the EEPROM the
game had written since, and the game wrote the old state back. Now the session sector notes at 0xFF8 "T7EQ" and the
sequence of the EEPROM slot in the flash (the EEPROM is stored first, then the session); a session whose sequence is
not the current one is dropped and the game starts from its EEPROM. Sessions without the note load as before.
T7_EE_SOON: the game's EEPROM writes go to the flash 5 s later (2 s without a key) instead of after a minute
without keys. The game writes its EEPROM seldom (host/hrun.c with -DT7_EEDBG, EEDBG=1: at the setup and at
events, nothing in 10 idle minutes). Checked in test/devt7.py (session with SESS_T1/T2 short: sequence noted;
a session with an older sequence: cold start from the EEPROM, the egg is there).

## Tone (T7_TONE_Q, 1.6.9 test14)

The tone was set right after each 2 ms step. After drawing or a flash write the loop catches up up to 30 ms at
once, so the notes of a melody (about 26 ms, host/hrun.c "tl <s>" lists them) came within a millisecond and were
swallowed. As T18Emu: tone changes queued with the game time, played 80 ms after the first of a sequence on the
real clock, late by more than 200 ms the timing starts anew.

## Pixel mode (T7_PIXSOFT, tried in test13, off)

A game pixel is 2 display pixels (pixel exact) or 2 to 3 (large). test13 drew the gap of every cell in its own colour,
a little lighter; on the device it no longer looked like pixels. Off again, the pixel mode is as before.

## Visits between two Paradise (T7_LINK, 1.6.9)

Tama-Go: connection icon, TAMA-GO, VISIT or GAME. Both wait in STAND BY, B on the device that goes visiting sends.
The game sends a packet with `$748E` (bank 3): `$32` and `$5B` minus the count, a preamble, then the bytes from `$0220`
by switching the infrared LED (`$3016` bit 3). `t7_ir_led_on` (called by `t7_io_write`, outside the hot loop) takes
the packet at the first LED switch-on of the preamble (`$32 == $5B`) as 32 bytes with the sum in the last, like the
older `t7_ir_bridge_tx_poll` (that one compared `$0220` and lost a packet sent again unchanged). The other device puts
it in with `t7_ir_bridge_rx` (the receive hook at `$60C4` of the IR bridge), only while its game is in the connection
screen (`t7_ir_activity` changed within 1.5 s), so an old packet does not wait. Between the devices: `T` frames over
the UART on the contacts (`../emu/src/emu/plink.h`, 460800 8N1, A5 type len data sum, hello every second).

Sound must be on: the game waits for its beeps to end (`$A3` bit 7) also during the visit. Checked on the PC with two
cores linked in lockstep: visit (guest away, the host shows both, guest home), GAME (YOU WIN / YOU LOSE), the second
device entering STAND BY late (the first one sends again).

## Clock note after a battery change (T7_CK_WRAP, 1.6.11 test1)

When the game asks for the time, the noted game time is counted on with the Paradise's seconds counter. After a
battery change that counter starts at 0 again, below the noted value; the difference wrapped to about 136 years and
the game got a date far off. Now the noted time is taken as it was (the clock from the Paradise sets it right after).
