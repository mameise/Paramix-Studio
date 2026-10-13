/*
 *  st_gfx.c
 *  This file used to hold the 240x32 status bar of the GBA version as image data.
 *  Since v0.3.1 (switch STBAR_LUMP) the ROM builder makes the bar from your own WAD
 *  (lump STBARGBA, see tools/gbaextras.py or Paramix Studio doom.js). So there is
 *  no graphics data here any more.
 */
#include "doomdef.h"
#ifndef STBAR_LUMP
#error "Without STBAR_LUMP there is no status bar: the image data is no longer in the source"
#endif
