/*
 * t7_eeprom.h - emulated 4 KB I²C EEPROM backed by Pebble persist
 *
 * The real V7 has a 32 kbit (4 KB) external SPI/I²C EEPROM that
 * holds all save data — Tama character, growth stage, stats, etc. When
 * power cycles the internal RAM is lost but the EEPROM persists.
 *
 * We emulate that EEPROM in RAM and back it with Pebble's persist
 * storage so Tama state survives app exits and watch reboots.
 *
 * Wire protocol (matches asterick/tamago eeprom.js exactly):
 *   START   = data falling edge while CLK high   → state = COMMAND
 *   STOP    = data rising  edge while CLK high   → if state was WRITE, commit
 *   bit     = sampled on CLK rising edge (input)
 *           = driven on CLK falling edge (output, only during READ)
 *
 * Commands (sent after START):
 *   $A0  Write — followed by 2 address bytes, then data bytes
 *   $A1  Read  — start streaming data bytes from current address
 *
 * Wired to Port B ($3015 = direction, $3016 = data):
 *   bit 2  POWER  (chip enable)
 *   bit 1  CLK
 *   bit 0  DATA  (input from CPU when configured as output;
 *                 driven by EEPROM when CPU configures as input)
 */

#ifndef T7_EEPROM_H
#define T7_EEPROM_H

#include <stdint.h>
#include <stdbool.h>

// Load saved data from persist storage. Call once during t7_init.
void t7_eeprom_init(void);

// Flush all dirty pages back to persist. Call on app exit and
// optionally periodically.
void t7_eeprom_flush(void);

// Wipe the entire EEPROM (in-memory and persist storage). The Tama
// firmware will boot into its first-time setup flow on the next start.
void t7_eeprom_wipe(void);

// Update the bus state. Equivalent to JS's eeprom.update(power, clk, data).
// power, clk, data are 0/non-zero (booleans).
void t7_eeprom_update(uint8_t power, uint8_t clk, uint8_t data);

// Current output bit (what the EEPROM is driving on the data line).
// 0 = pulled low, 1 = high-impedance / NACK.
uint8_t t7_eeprom_output(void);

#endif  // T7_EEPROM_H
