/* Glue between the core and the Paradise platform */
#include "doomstat.h"
#include "sounds.h"
#include "global_data.h"
const char *plat_sfx_name(int id)
{
    if (id <= 0 || id >= NUMSFX) return 0;
    const sfxinfo_t *s = &S_sfx[id];
    if (s->link) s = s->link;
    return s->name;
}
void plat_toggle_run(void) { _g->alwaysRun = 1 - _g->alwaysRun; }
#include "w_wad.h"
/* v0.2.0: values for the diagnostics page before the first frame */
void plat_diag_info(unsigned int *o)
{
    o[0] = _g->gamma; o[1] = _g->newpal;
    o[2] = (unsigned int) p_wad_immutable_flash_data;
    o[3] = p_wad_immutable_flash_data ? (unsigned int) p_wad_immutable_flash_data->palette_lump : 0;
    o[4] = p_wad_immutable_flash_data ? (unsigned int) p_wad_immutable_flash_data->colormaps : 0;
    o[5] = (unsigned int) _g->current_palette;
    o[6] = _g->gamestate; o[7] = _g->gametic;
}

/* v0.2.5: music lump name (D_xxx) for a number from musicenum_t */
static const char *const mus_names[] = { 0,
 "D_E1M1","D_E1M2","D_E1M3","D_E1M4","D_E1M5","D_E1M6","D_E1M7","D_E1M8","D_E1M9",
 "D_E2M1","D_E2M2","D_E2M3","D_E2M4","D_E2M5","D_E2M6","D_E2M7","D_E2M8","D_E2M9",
 "D_E3M1","D_E3M2","D_E3M3","D_E3M4","D_E3M5","D_E3M6","D_E3M7","D_E3M8","D_E3M9",
 "D_INTER","D_INTRO","D_BUNNY","D_VICTOR","D_INTROA"
#ifdef PDM_LUMPTXT
 /* 1.7.3 test17: DOOM II's songs (were missing: DOOM II played no music) */
 ,"D_RUNNIN","D_STALKS","D_COUNTD","D_BETWEE","D_DOOM","D_THE_DA","D_SHAWN","D_DDTBLU","D_IN_CIT","D_DEAD",
 "D_STLKS2","D_THEDA2","D_DOOM2","D_DDTBL2","D_RUNNI2","D_DEAD2","D_STLKS3","D_ROMERO","D_SHAWN2","D_MESSAG",
 "D_COUNT2","D_DDTBL3","D_AMPIE","D_THEDA3","D_ADRIAN","D_MESSG2","D_ROMER2","D_TENSE","D_SHAWN3","D_OPENIN",
 "D_EVIL","D_ULTIMA","D_READ_M","D_DM2TTL","D_DM2INT"
#endif
 };
#ifdef PDM_LUMPTXT
int pdm_mus_h; char pdm_mus_n[9];   /* s_sound.c: this song number plays the lump pdm_mus_n (own E4 songs) */
#endif
const char *plat_mus_name(int h)
{
#ifdef PDM_LUMPTXT
    if (h && h == pdm_mus_h) return pdm_mus_n;
#endif
    if (h <= 0 || h >= (int)(sizeof(mus_names) / sizeof(mus_names[0]))) return 0;
    return mus_names[h];
}
