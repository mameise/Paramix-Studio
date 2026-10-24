/* v3cpu.h - Tamagotchi Connection V3 (Sunplus SPLB32, a 6502) in C, ported from BrickEmuPy (azya52, CC0):
 * cores/SPLB32.py, peripherals/HT24LC08.py (EEPROM), direct_input.py (keys). Time in ticks of 1/128 cycle
 * (4 MHz main clock, the 32768 Hz clock is exactly 15625 ticks), so the timers count as the Python floats did.
 * Author: mameise */
#ifndef V3CPU_H
#define V3CPU_H
#include <stdint.h>
#define V3_CLOCK 4000000u
#define V3_TICKS 128u                      /* ticks per cycle */
#define V3_EE_SIZE 1024u
#define V3_DPRAM 0x200u
void v3_init(const uint8_t *rom, uint32_t size);   /* rom stays where it is (flash) */
void v3_reset(void);
uint64_t v3_run(uint64_t ticks);           /* runs at least ticks, returns the ticks run */
uint32_t v3_step(void);                    /* one instruction (or one 32 kHz step while stopped), its ticks */
void v3_set_keys(uint32_t mask);           /* bit 0 left (A), 1 middle (B), 2 right (C) */
const uint8_t *v3_vram(int *mode);         /* the LCD RAM (0x200 bytes); mode 0 off, 1 on, 2 all on */
uint32_t v3_tone_hz(void);                 /* square tone of the buzzer now, 0 = quiet */
uint8_t *v3_eeprom(void);                  /* 1 KB */
int v3_eeprom_dirty(int clear);
uint32_t v3_state_size(void);
void v3_state_save(uint8_t *b);
int v3_state_load(const uint8_t *b, uint32_t n);
void v3_ir_rx(const uint8_t *d, int n);   /* infrared: bytes from the other toy */
int v3_ir_tx(uint8_t *out, int max);       /* infrared: bytes this toy sent (taken out) */
uint8_t *v3_ram(void);                     /* 0x4C0 bytes from address 0x40 */
/* debug */
uint32_t v3_pc(void); void v3_regs(uint8_t r[5], uint8_t f[6]); uint32_t v3_bank(void);
#endif
