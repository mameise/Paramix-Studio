/* ArduboyEmu: the ATmega32U4 of the Arduboy (16 MHz AVR) with what the Arduboy uses of it: timers 0, 1, 3, 4, SPI to
 * the SSD1306 display, the buttons, EEPROM, ADC (random seeds), the PLL and an idle USB, the speaker pins.
 * Own code, no other emulator in it. Author: mameise. License: GPL-3.0-or-later (as Paramix Studio). */
#ifndef AVR_H
#define AVR_H
#include <stdint.h>
typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32; typedef int32_t s32;

#define AVR_FLASH_WORDS 16384u
#define AVR_DATA 0xB00u            /* 32 registers, 64 + 160 I/O, 2.5 KB SRAM */
#define AVR_EEP 1024u

/* buttons (pressed = 1) */
#define AB_UP 1u
#define AB_DOWN 2u
#define AB_LEFT 4u
#define AB_RIGHT 8u
#define AB_A 16u
#define AB_B 32u

#if (defined(__arm__) && !defined(AB_NOJIT)) || defined(AB_JIT_HOST)
#define AB_JIT 1                   /* the fast core (jit.inc) */
#else
#define AB_JIT 0
#endif
/* the data space (4 KB: addresses are masked), the fast core's context just before it */
typedef struct { u32 jit[64]; u8 d[0x1000]; } AbMem;
extern AbMem g_mem;
#define A_D (g_mem.d)
extern u8 A_EEP[AVR_EEP];
extern u8 A_GD[1024];              /* the display's memory: 8 pages of 128 columns */
#if !AB_JIT
extern u32 A_OP[AVR_FLASH_WORDS + 4];  /* the program, decoded */
#endif
extern const u16 *A_FLASH;         /* the program as it is (words, little endian) */
extern u32 A_CYC;                  /* cycles run (wraps) */
extern int A_EEP_DIRTY;            /* the game wrote the EEPROM */
extern u32 A_KEYS;                 /* buttons held */
extern u32 A_FRAMES;               /* display frames sent (1024 data bytes) */
/* display state the program needs to draw */
typedef struct { u8 on, invert, contrast, flip_x, flip_y, allon, start; } AvrLcd;
extern AvrLcd A_LCD;

void ab_frame(void);                         /* called after each full picture (weak, the program may give its own) */
void ab_load(const u16 *flash, u32 words);   /* decode the program (words beyond: 0xFFFF) */
void ab_reset(void);
void ab_run(u32 cycles);                     /* run about this many cycles */
/* the speaker: tone in Hz (0 silent), from the timers driving the pins or the pin changes counted since the last call
 * over `cycles` */
u32 ab_tone(u32 cycles);
/* state save / load (registers, data, timers, display): bytes used, 0 when it did not fit */
u32 ab_state_save(u8 *buf, u32 size);
/* 9 KB of scratch memory for the program between runs (with the fast core: the end of its cache, emptied) */
#define AB_SCRATCH 0x2400u
u8 *ab_scratch(void);
int ab_state_load(const u8 *buf, u32 size);
#endif
