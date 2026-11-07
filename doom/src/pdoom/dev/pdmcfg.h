/* DOOM settings block (PDMC), filled in by the Studio. Shared with the core for DOOM_FREE (1.7.2 test3): the places
 * of the WAD, the level data, the save and the program part are read from here instead of being fixed. */
#ifndef PDMCFG_H
#define PDMCFG_H
#include <stdint.h>
typedef struct {
  uint32_t magic, version;
  uint32_t wad_off, wad_len;            /* WAD in flash (offset), 4 byte length before the WAD */
  uint32_t region_off, region_len;      /* area for precomputed data (the core writes it itself) */
  uint32_t save_off;                    /* 2 x 4 KB: settings, save */
  uint32_t code_off;                    /* program part in the flash window */
  uint32_t detent;                      /* encoder steps per detent (2 or 4) */
  uint32_t turn_tics;                   /* turn duration per detent in game tics */
  uint32_t sound;                       /* 1 = sound on */
  uint32_t invert;                      /* 1 = wheel reversed */
  /* from version 2 (KEYMAP): key mapping as button masks (BTN_A 1, BTN_B 2, BTN_C 4, BTN_WHEEL 8) */
  uint32_t k_fire, k_fwd, k_back, k_use, k_mod;   /* k_mod: hold + wheel = switch weapon, hold + press wheel = sound */
  uint32_t run_dbl;                     /* 1 = double tap forward toggles run */
#ifdef PDM_CMAPXIP
  uint32_t cmap_off;                    /* version 3 (1.7.3 test17): flash place of a COLORMAP copy in the program part (0: none) */
#endif
} PdmCfg;
extern volatile const PdmCfg g_dcfg;
#ifdef DOOM_FREE
/* the two 4 MB windows of the packed flash pointers (set at start from wad_off) */
extern uint32_t g_pk_lo, g_pk_hi;
/* the program part sits at XIP 0x100A0000: the window base follows code_off */
#define PD_XIP_BASE (0x60000000u + g_dcfg.code_off - 0xA0000u)
#else
#define PD_XIP_BASE 0x60011000u
#endif
#endif
