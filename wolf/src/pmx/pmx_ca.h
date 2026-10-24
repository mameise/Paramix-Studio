/* Paramix Wolf3D: the data pack. Author: mameise. */
#ifndef PMX_CA_H
#define PMX_CA_H
#include <stdint.h>

/* pics in the pack: the game's range L_COLONPIC .. GETPSYCHEDPIC (same order in every version) */
enum {
    PK_L_COLON = 0, PK_L_NUM0 = 1, PK_L_PERCENT = 11, PK_L_A = 12,   /* L_APIC .. L_ZPIC: 26 */
    PK_L_EXPOINT = 38, PK_L_APOSTROPHE = 39, PK_L_GUY = 40, PK_L_GUY2 = 41,
    PK_STATUSBAR = 42, PK_TITLE = 43, PK_PG13 = 44, PK_CREDITS = 45, PK_HIGHSCORES = 46,
    PK_KNIFE = 47, PK_GUN, PK_MACHINEGUN, PK_GATLING, PK_NOKEY, PK_GOLDKEY, PK_SILVERKEY, PK_N_BLANK,
    PK_N_0 = 55, PK_FACE1A = 65, PK_FACE8A = 86, PK_GOTGATLING = 87, PK_MUTANTBJ = 88, PK_PAUSED = 89, PK_GETPSYCHED = 90
};

extern int pmx_episodes, pmx_shareware;
int  pmx_pack_open (void);
const uint8_t *pmx_pic (int k, int *w, int *h);
const uint8_t *pmx_sound (int k, uint32_t *len, int *prio);
void pmx_read (void *dst, uint32_t off, uint32_t n);
uint32_t pmx_pack_len (void);
int  pmx_save_game (void);
int  pmx_load_game (void);
int  pmx_save_present (void);
void pmx_save_forget (void);
#endif
