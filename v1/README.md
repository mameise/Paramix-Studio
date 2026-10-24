# V1Emu

The first Tamagotchis and the Digital Monster (Epson E0C6S46 / E0C6S48, 4-bit) on the Tamagotchi
Paradise, as a further program of the Paramix menu.

- `src/core`: TamaLIB by Jean-Christophe Rona (GPL v2 or later, `LICENSE_tamalib`). Changes for the
  Paradise: a decode table instead of the search per instruction, `cpu_v1_save` / `cpu_v1_load` (the
  whole CPU state as bytes), the hardware functions renamed to `thw_*` (the Paradise's `hw_*` exist).
- `src/app/main.c`: the program (PRAM, 64 KB image). Settings block "PMX1" at 0x140: ROM, session (4 KB
  at save_off), settings of the device (4 KB behind it), name, flags (bit 0 English, bit 1 clock from the
  Paradise, bit 3 the toy's clock is known: P1, P2, Angel, Morino, Digimon; bit 5 LCD turned; bit 6 Digimon), optional icons
  (8 x 16x16, used when icons_ok is 1). Virtual time in microseconds, moved on by the core's own waits.
- Screen: 32x16 dots four times enlarged (y 34..97), four icons above and below.
- Keys: A, B, C = K02, K01, K00; turning the wheel (encoder at GPIO2 bits 0..1) = K03 for 120 ms (shake).
- As the main program (Studio `v1Patch`): image at 0x0A0000, ROM at 0x940000, session and settings at 0x0C0000.
- Icons: the Studio fills them in per ROM (`V1_ICONS` in src/emu.js).
- Clock of P1, P2, Angel, Morino: RAM nibbles 0x10..0x15 = seconds (BCD), minutes (BCD), hours (binary
  0..23). Fresh start: the program presses B, B, A, C itself (clock setting, one hour, set) and then writes
  the Paradise's time; resumed: written right after loading. Found with a host run of TamaLIB.
- Sound: the buzzer only on a change, never longer than 3 s in one go. The one-shot buzzer (0xF75 bit 3,
  31.25 or 125 ms) is emulated in the core (1.6.8): the Digimon beeps only with it.
- Digimon (flags bit 6): its own LCD wiring (`thw_digi`, core `hw.c`), its icons on segments 28, 37, 38, 39
  (com 15: status, food, training, battle), 11, 10, 9 (com 0: clean, light, heal) and 0 (com 0: call), and its
  keys by interrupt: TamaLIB wrote the K00-K03 interrupt mask into the serial slot (`cpu_kmask_fix`, only for
  the Digimon, the Tamagotchis stay as before).
- Light (1.6.8, `BL_PWM`): settings "Light" 5 to 100 % (r0 bits 0..3, bit 7 = layout) and "Dim to" 1 to 50 %.

Build: `sh src/app/build.sh` (clang, ld.lld) -> `v1.bin`.

Studio: `src/emu.js` (`v1Detect`, `v1Name`, `appsPlace` kind `v1`: program, session, settings, ROM at
+0x12000, 4 KB aligned, preferably behind the Paradise's save from 0xF00000).

- 1.6.8-test3 V1_CLK_CHECK: 2.5 s after the start (and after the clock was set by keys) hours and minutes of the toy are compared
  with the Paradise's time; off by more than a minute: written again, three times at most. Bernd saw a resumed P2 with its old time.

## Digimon battles between two Paradise (V1_LINK, 1.6.9)

The Digimon talks over P20 (0xF62 bit 0) with IO_CTRL 0xF7D bit 2 as direction (output when set), open drain with
pull-up: send at 0x0B06 (pre-low about 60 ms, start bit, 16 bits as in dmcomm's "V" timings), receive at 0x0B5B
(only when the line is low at a main loop pass, about every 25 ms), both called from the main loop at 0x013E while
the mode is 5/2 (RAM 0x09/0x0A). tamalib had no P ports: a read of 0xF62 gave 0 (line low). Now `cpu.c` gives the
line as low when this toy drives it or the other one does (`v1_link_in`), and passes each change of its own drive
with the CPU's time (`ref_ts`, cycles; the pacing time `g_vt` runs ahead when the CPU halts) to `v1_link_out`.
`main.c` sends the edges as 'D' frames (level, time) over the contacts (`../emu/src/emu/plink.h`); the other side
plays them back at their time plus an offset, set at the first edge after 30 ms quiet to now + 20 ms. The lengths
of the pulses stay exact, the transport only shifts them.

Checked on the PC (host runner with two cores and edges in lockstep): the packet of one toy has the dmcomm V timings
(60 ms low, start 2.0/0.9 ms, bits 1.0/3.3 and 2.7/1.7 ms), the other one receives it and answers. With the test
state (Botamon, mode set by hand) both end in リトライ; a real battle (two rookies) is not checked yet.

V1_REGB: `cpu_v1_save`/`cpu_v1_load` named their buffer `b` like register B: the session kept the buffer's address
instead of B, and loading set the pointer from it (same address on the Paradise, so it went unnoticed; B was 0
after a resume). Now B is kept in the same 4 bytes; an older session (an address there) leaves B at 0 as before.
