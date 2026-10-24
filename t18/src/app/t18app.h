/* T18 (Meets/On, TG18 firmware) on the Tamagotchi Paradise: settings block and flash layout. */
#ifndef T18APP_H
#define T18APP_H
#include <stdint.h>

/* Settings block "PMX8" at 0x140 in the program, filled in by the Studio (src/emu.js, t18Patch).
 * The firmware lies in the Paradise flash in 64 KB blocks wherever there was room: map[i] is the
 * Paradise block (offset >> 16) of guest block i, 0xFFFF = erased (reads 0xFF). Blocks the guest
 * writes later (its save data) come from the spare list, which the Studio leaves erased. */
#define T18_MAGIC 0x38584D50u          /* "PMX8" */
#define T18_SPARE_MAX 64
typedef struct {
  uint32_t magic, version;
  uint32_t save_off;                   /* Paradise flash: map sector, settings, session (see main.c) */
  uint32_t nspare;
  uint16_t map[128];
  uint16_t spare[T18_SPARE_MAX];
  uint32_t flags;                      /* T18F_* */
  uint32_t idle_addr, softrtc, dma_tab;   /* found by the Studio in the firmware (tg18_set_addrs) */
  uint32_t wall_cnt, wall_sod, wall_ymd;  /* written by the Studio on the device: seconds counter, local time, date */
  uint32_t param_addr;                 /* 1.6.8 (test47): the game's Param block in the guest SRAM (0xF800....), 0 = unknown */
  uint32_t adult_flags;                /* bit 0: Sanrio firmware (Flowertchi instead of Pompomtchi) */
  uint32_t pic_off;                    /* pictures for grow up ("T8PI", the user's own, see src/emu.js), 0 = none */
} T18Cfg;

#define T18F_EN    1u                 /* English texts */
#define T18F_CLOCK 2u                 /* at every start the clock is set to the Paradise's time (nothing caught up) */
#define T18F_HOTWIN 4u                /* the Studio put the hot firmware blocks into 0xF00000..0xFFFFFF and a second
                                       * copy of the flash part at T18_XIP_HOT (see main.c, hot window) */

/* code that is not hot: in the flash at T18_XIP_OFF, seen in the window 0x10000000 (base 0x60011000);
 * it begins with T18_XIP_MAGIC and the build number of the program (T18_BUILD) */
#define T18_XIP_OFF   0xF0000u
#define T18_XIP_MAX   0x10000u
#define T18_XIP_MAGIC 0x58383154u         /* "T18X" */
/* hot window (1.6.6): the cached flash window shows 1 MB from its base. With base 0x60F00000 it shows
 * 0xF00000..0xFFFFFF; the flash part is then needed at 0xFDF000 (same link address 0x100DF000) */
#define T18_HOT_BASE  0xF00000u
#define T18_XIP_HOT   (T18_HOT_BASE + T18_XIP_OFF - 0x11000u)

/* Paradise flash from save_off */
#define T18_MAP_SEC     0x0000u        /* "T8MP": the map as it is now, with the spares in use */
#define T18_NOTE_SEC    0x1000u        /* "T8NT": seconds counter and game time when leaving */
#define T18_SESS_SEC    0x2000u        /* "T8SS": CPU and SoC state, then the SRAM (32 sectors) */
#define T18_SAVE_LEN    0x24000u
#endif
