/* Paradise: settings for the DOOM core (replaces main.h of the nRF52840 version) */
#ifndef MAIN_H
#define MAIN_H
#include <stdbool.h>
#include <stdint.h>
#include "graphics.h"
#ifndef CACHE_ALL_COLORMAP_TO_RAM
#define CACHE_ALL_COLORMAP_TO_RAM 0
#endif
#ifndef DEBUG_SETUP
#define DEBUG_SETUP 0
#endif
#ifndef START_MAP
#define START_MAP 1
#endif
#ifndef SHOW_FPS
#define SHOW_FPS false
#endif
#ifndef AUTOSTART_GAME
#define AUTOSTART_GAME 0
#endif
#define GAMMA_CORRECTION 0
#define I2C_GAMEPAD 4
#ifdef PARADISE_DEV
#define KEYBOARD I2C_GAMEPAD
#endif
typedef uint16_t pkey_t;
#define key_t pkey_t
#endif
