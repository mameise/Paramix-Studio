# ArduboyEmu

The Arduboy on the Tamagotchi Paradise, as a further program of the Paramix menu.
No game here: bring your own games (.hex).

- `src/core/avr.c`: own emulation of the ATmega32U4 (16 MHz AVR) and of what the Arduboy uses of it:
  - CPU: every instruction of the AVRe+ core with its cycles. The program is decoded once into one word per
    instruction (handler and operands). The flags are kept apart and only put together when SREG is read.
    Straight code runs without the event check; jumps check it.
  - Timers 0, 1, 3 and 4: brought up to date when read or written and at their next event (all modes the
    Arduino and Arduboy libraries use). Each timer's next event is kept until a write, a flag or an interrupt
    changes it (AB_EVC), so a fast sound interrupt costs little.
  - SPI to the SSD1306 display (commands, both addressing modes, inverting, contrast, start line, flipping), the
    display's reset pin, the state the boot loader leaves it in.
  - Buttons on the pins (kept current in the data space, so reading them is a plain load), EEPROM (1 KB), ADC (random values for seeds), PLL, watchdog (reset), sleep.
  - Busy waiting (AB_SPIN): a loop that only asks the clock (millis(), micros()) and changes nothing is skipped up to
    the next event.
  - Sound: the tone of the speaker pins, from the timer that drives them or from the pin changes (AB_SND2: the pitch
    from the times of the changes, both pins at once count once), and the PWM samples of OC4A (ATMlib music) from
    their rising edges. The program leaves out changes below 3 % so the buzzer is not started anew every 20 ms.
- `src/core/jit.inc` (AB_JIT, on the Paradise): the fast core. The program is translated block by block into Thumb-2
  code in the SRAM (a 56 KB cache, emptied when full). AVR registers stay in the data space, the flags in ARM
  registers; a flag is only worked out when something later reads it (H only inside a block). Blocks jump straight
  to each other while cycles are left (AB_CHAIN), else through a small dispatcher. Rare instructions (I/O, SREG,
  sleep, the signed multiplications) run in C (`jit_op`) with the flags handed over. The display byte (OUT SPDR) has its
  own short helper; reading SPSR and the button pins, and SBIC/SBIS on them, are translated inline. On the PC the interpreter of
  avr.c runs (it is the reference).
- `src/app/main.c`: the Paradise program (PRAM). Settings block "ABXP" at 0x140: game, length, save, flags (bit 0
  English, bit 1 wheel the other way round), name, keys (12 bytes: Arduboy buttons for the Paradise inputs 0 A, 1 B,
  2 C, 3 wheel press, 4 wheel up, 5 wheel down, 6 A+C, 7 B+C, 8 A+B; bits 1 up, 2 down, 4 left, 8 right, 16 A, 32 B).
  The game lies at the start of its own 64 KB block (the cached window), the save behind it at +0x8000 (4 KB settings,
  4 KB EEPROM, 8 KB state).
- Screen: 128 x 64 at 1:1, 3:2 or 2:1 height (setting PICTURE, default 1:1 = SMALL, switch AB_SIZE0). Pictures that games switch for greys come out grey.
- Fixed keys: A+B+C settings (colour, picture, sound, light, dim, screen off, restart game, language); wheel held 2 s
  = save and back to the Paradise.
- Save: the EEPROM goes to the flash 3 s after the game wrote it (1 s without a key), when the screen goes off and on
  leaving. On leaving the whole state too (`emu/src/emu/pstate.h`), read once at the next start.
- The core is built with GCC (arm-none-eabi-gcc): each handler gets its own dispatch.

Build: `sh src/app/build.sh` gives `ab.bin`.
Tests:
- `test/hostab.c` (gcc on the PC): runs a game, pictures as PGM (KEYS, SHOTS, OUT, PCS for a pc histogram).
- `test/fuzz.c`, `test/lock.c`: the CPU against simavr (libsimavr-dev), see the files.
- `test/jitfuzz/`: random programs run by the interpreter on the PC and by the fast core in Unicorn, compared
  (`run.sh seed`).
- `python3 test/mkimg.py ab.bin game.hex img.bin` then `python3 test/devab.py src/app/ab.elf img.bin <ms>` (Unicorn,
  KEYS="t:mask,...", SHOTS="t,...", OUT=dir); `test/prof.py` instructions per function, `test/profl.py` per line.

Author: mameise

No session (1.7.3 test7, switch SESS_FLAG): flags bit 12 set by the Studio ("Remember where you left off" off): leaving writes
no state, a start drops an old one; only the game's own save (cartridge RAM or EEPROM) is kept.
