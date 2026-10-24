/* Paramix Wolf3D on the PC (test runner): the pack from a file, a virtual clock, keys from a script,
 * pictures as PPM, the buzzer as a list. No window.
 *   ./wolfhost pack.bin seconds [KEYS="t:mask,..."] [WHEEL="t:steps,..."] [SHOTS="t,t,..."] [OUT=dir]
 * mask: A 1, B 2, C 4, wheel press 8. Author: mameise. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <setjmp.h>
#include "pmx_plat.h"

void pmx_main (void);
static uint8_t *g_pack;
static uint32_t g_ms;
static double g_end;
static jmp_buf g_quit;
static const char *g_out = ".";
typedef struct { double t; long v; } Ev;
static Ev g_keys[256], g_wheel[256]; static int n_keys, n_wheel, i_wheel;
static double g_shots[256]; static int n_shots, i_shot;
static uint8_t g_last[128 * 128 * 3];
static long g_frames;
static FILE *g_tones;

static int parse (const char *s, Ev *e, int max)
{
    int n = 0; if (!s) return 0;
    while (*s && n < max) { double t; long v; int k; if (sscanf (s, "%lf:%ld%n", &t, &v, &k) != 2) break; e[n].t = t; e[n].v = v; n++; s += k; if (*s == ',') s++; }
    return n;
}
static void shot (const char *name)
{
    char p[512]; snprintf (p, sizeof p, "%s/%s", g_out, name);
    FILE *f = fopen (p, "wb"); if (!f) return;
    fprintf (f, "P6\n128 128\n255\n"); fwrite (g_last, 1, sizeof g_last, f); fclose (f);
}
static void tick_to (uint32_t ms)
{
    while (g_ms < ms)
    {
        g_ms++;
        pmx_sd_service (g_ms);
        while (i_shot < n_shots && g_ms >= g_shots[i_shot] * 1000) { char n[32]; snprintf (n, sizeof n, "shot_%02d.ppm", i_shot); shot (n); i_shot++; }
        if (g_ms >= g_end * 1000) longjmp (g_quit, 1);
    }
}
uint32_t plat_ms (void) { return g_ms; }
void plat_wait (uint32_t ms) { tick_to (g_ms + (ms ? ms : 1)); }
uint32_t plat_keys (void)
{
    long m = 0;
    for (int i = 0; i < n_keys; i++) if (g_ms >= g_keys[i].t * 1000) m = g_keys[i].v;
    return (uint32_t)m;
}
int plat_wheel (void)
{
    int d = 0;
    while (i_wheel < n_wheel && g_ms >= g_wheel[i_wheel].t * 1000) d += (int)g_wheel[i_wheel++].v;
    return d;
}
void plat_lcd (const uint8_t *fb, int y0, int y1, const uint16_t *lut)
{
    for (int y = y0; y < y1; y++) for (int x = 0; x < 128; x++)
    {
        uint16_t c = lut[fb[y * 128 + x]]; uint8_t *o = g_last + 3 * (y * 128 + x);
        o[0] = (uint8_t)((c >> 11) << 3); o[1] = (uint8_t)(((c >> 5) & 63) << 2); o[2] = (uint8_t)((c & 31) << 3);
    }
    g_frames++;
    tick_to (g_ms + 1);            /* sending a picture takes a little time */
}
void plat_tone (uint32_t hz) { if (g_tones) fprintf (g_tones, "%u %u\n", g_ms, hz); }
const uint8_t *plat_pack (void) { return g_pack; }
const uint8_t *plat_pack_far (void) { return g_pack; }
uint32_t plat_pack_win (void) { return getenv ("WIN") ? (uint32_t)strtoul (getenv ("WIN"), 0, 0) : 0xFFFFFFFFu; }
static uint8_t g_set[256];
int plat_settings_read (void *buf, int len) { memcpy (buf, g_set, (size_t)len); return len; }
void plat_settings_write (const void *buf, int len) { memcpy (g_set, buf, (size_t)len); }
void plat_back (void) { printf ("back to the Paradise at %u ms\n", g_ms); longjmp (g_quit, 2); }
void plat_fatal (const char *s) { printf ("FATAL: %s\n", s); longjmp (g_quit, 3); }
void pmx_error (const char *s) { plat_fatal (s); }
static uint8_t g_save[0xF000];
static const char *g_savefile;
void plat_save_at (uint32_t off, void *dst, int len) { if (off + (uint32_t)len <= sizeof g_save) memcpy (dst, g_save + off, (size_t)len); }
void plat_save_page (uint32_t off, const void *src, int len)
{
    if (off + 0x1000 > sizeof g_save) return;
    memset (g_save + off, 0xFF, 0x1000); memcpy (g_save + off, src, (size_t)len);
    if (g_savefile) { FILE *f = fopen (g_savefile, "wb"); if (f) { fwrite (g_save, 1, sizeof g_save, f); fclose (f); } }
}

int main (int argc, char **argv)
{
    if (argc < 3) { fprintf (stderr, "wolfhost pack seconds\n"); return 1; }
    FILE *f = fopen (argv[1], "rb"); if (!f) { perror (argv[1]); return 1; }
    fseek (f, 0, SEEK_END); long n = ftell (f); fseek (f, 0, SEEK_SET);
    g_pack = malloc ((size_t)n); if (fread (g_pack, 1, (size_t)n, f) != (size_t)n) return 1; fclose (f);
    g_end = atof (argv[2]);
    memset (g_save, 0xFF, sizeof g_save);
    g_savefile = getenv ("SAVE");
    if (g_savefile) { FILE *sf = fopen (g_savefile, "rb"); if (sf) { if (fread (g_save, 1, sizeof g_save, sf)) {} fclose (sf); } }
    n_keys = parse (getenv ("KEYS"), g_keys, 256);
    n_wheel = parse (getenv ("WHEEL"), g_wheel, 256);
    if (getenv ("OUT")) g_out = getenv ("OUT");
    { const char *s = getenv ("SHOTS"); while (s && *s && n_shots < 256) { g_shots[n_shots++] = atof (s); s = strchr (s, ','); if (s) s++; } }
    { char p[512]; snprintf (p, sizeof p, "%s/tones.txt", g_out); g_tones = fopen (p, "w"); }
    int r = setjmp (g_quit);
    if (!r) pmx_main ();
    shot ("shot_end.ppm");
    printf ("ms %u frames %ld end %d\n", g_ms, g_frames, r);
    if (g_tones) fclose (g_tones);
    return r == 3;
}
