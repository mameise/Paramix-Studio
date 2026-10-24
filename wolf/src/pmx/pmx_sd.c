/* Paramix Wolf3D: sound on the Paradise's buzzer. The game's PC speaker sounds are one tone at a time already:
 * one byte every 1/140 s, 0 = silence, else the PC timer divisor / 60 (frequency = 1193181 / (byte * 60) Hz).
 * pmx_sd_service() is called every millisecond (the Paradise's tick) or often (PC test).
 * Author: mameise. GPL-2.0-or-later. */
#include "wl_def.h"
#include "pmx_plat.h"
#include "pmx_ca.h"

globalsoundpos channelSoundPos[1];
boolean AdLibPresent, SoundBlasterPresent, SoundPositioned;
byte    SoundMode = sdm_PC, DigiMode = sds_Off, MusicMode = smm_Off;
int     pmx_sound_on = 1;

static volatile const byte *s_data;
static volatile uint32_t s_len, s_start;
static volatile int s_num, s_prio;
static volatile uint32_t s_hz;

void SD_Startup (void) {}

boolean SD_PlaySound (int sound)
{
    uint32_t len; int prio;
    if (!pmx_sound_on) return false;
    const byte *d = pmx_sound (sound, &len, &prio);
    if (!d || !len) return false;
    if (s_data && prio < s_prio) return false;      /* a more important sound is playing */
    s_data = 0;                                     /* the tick does not read while we change it */
    s_len = len; s_prio = prio; s_num = sound; s_start = plat_ms ();
    s_data = d;
    return true;
}

void pmx_sd_service (uint32_t now)
{
    const byte *d = (const byte *)s_data;
    uint32_t hz = 0;
    if (d)
    {
        uint32_t i = (now - s_start) * 140u / 1000u;
        if (i >= s_len) { s_data = 0; s_num = 0; }
        else { byte b = d[i]; hz = b ? 1193181u / ((uint32_t)b * 60u) : 0; }
    }
    if (hz != s_hz)
    {
        /* a new pitch always starts the buzzer anew (as in GBEmu test39: changing the running timer stays silent) */
        if (hz && s_hz) plat_tone (0);
        s_hz = hz; plat_tone (hz);
    }
}

/* before writing the flash: nothing in the millisecond tick reads the flash any more */
void pmx_sd_halt (void) { s_data = 0; s_num = 0; s_hz = 0; plat_tone (0); }
void SD_StopSound (void)
{
    s_data = 0; s_num = 0;
#ifndef PMX_SD_IN_TICK
    pmx_sd_service (plat_ms ());
#endif
}
word SD_SoundPlaying (void) { return s_data ? (word)s_num : 0; }
void SD_WaitSoundDone (void) { uint32_t t0 = plat_ms (); while (s_data && plat_ms () - t0 < 3000u) { pmx_idle (); plat_wait (2); } }
void SD_PositionSound (int leftvol, int rightvol) { (void)leftvol; (void)rightvol; }
void SD_SetPosition (int channel, int leftvol, int rightvol) { (void)channel; (void)leftvol; (void)rightvol; }
void SD_StopDigitized (void) {}

void Delay (int32_t wolfticks)
{
    if (wolfticks > 0) SDL_Delay ((uint32_t)wolfticks * 100u / 7u);
}
