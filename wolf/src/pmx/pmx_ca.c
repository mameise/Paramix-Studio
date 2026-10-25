/* Paramix Wolf3D: the data pack (W3DP, built by the Studio from the user's game files): pages, maps, pics, sounds.
 * Maps are unpacked (Carmack, then RLEW) into RAM when a level starts; everything else is read in place.
 * Author: mameise. GPL-2.0-or-later (CAL_CarmackExpand and CA_RLEWexpand from Wolf4SDL / id Software). */
#include "wl_def.h"
#include "pmx_plat.h"
#include "pmx_ca.h"

word ChunksInFile, PMSpriteStart, PMSoundStart;
word *mapsegs[MAPPLANES];
maptype *mapheaderseg[NUMMAPS];
static word g_plane[MAPPLANES][MAPAREA];
static maptype g_maphead;
static const uint8_t *P, *PF;     /* the pack: fast up to g_win, then through PF */
static uint32_t g_win;
static const uint8_t *at (uint32_t o) { return o < g_win ? P + o : PF + o; }
/* a piece that ends behind the fast part is read whole through the slow path */
static const uint8_t *atn (uint32_t o, uint32_t n) { return o + n <= g_win ? P + o : PF + o; }
static uint32_t n_pages, pages_tab, n_maps, rlew, maps_tab, n_snd, snd_tab, n_pics, pics_tab;
int pmx_episodes, pmx_shareware;

static uint32_t rd32 (uint32_t o) { const uint8_t *b = at (o); return b[0] | b[1] << 8 | b[2] << 16 | (uint32_t)b[3] << 24; }
static uint16_t rd16 (uint32_t o) { const uint8_t *b = at (o); return (uint16_t)(b[0] | b[1] << 8); }

int pmx_pack_open (void)
{
    P = plat_pack (); PF = plat_pack_far (); g_win = plat_pack_win ();
    if (!P || P[0] != 'W' || P[1] != '3' || P[2] != 'D' || P[3] != 'P') return 0;
    pmx_shareware = rd32 (8) & 1;
#ifdef SPEAR
    if (!(rd32 (8) & 2)) return 0;   /* 1.6.11 test2: the Spear of Destiny program needs a pack of the .SOD files */
    pmx_episodes = 1;
#else
    if (rd32 (8) & 2) return 0;
#endif
    n_pages = rd32 (16); PMSpriteStart = (word)rd32 (20); pages_tab = rd32 (24);
    n_maps = rd32 (28); rlew = rd32 (32); maps_tab = rd32 (36);
    n_snd = rd32 (40); snd_tab = rd32 (44);
    n_pics = rd32 (48); pics_tab = rd32 (52);
#ifndef SPEAR
    pmx_episodes = (int)rd32 (60);
#endif
    if (pmx_episodes < 1) pmx_episodes = 1;
    if (pmx_episodes > 6) pmx_episodes = 6;
    ChunksInFile = PMSoundStart = (word)n_pages;
    for (int i = 0; i < MAPPLANES; i++) mapsegs[i] = g_plane[i];
    return 1;
}

/* ---- pages (walls 64 x 64 by columns, sprites as t_compshape) ---- */
byte *PM_GetPage (int page)
{
    if ((uint32_t)page >= n_pages) Quit ("PM_GetPage: page %d", page);
    uint32_t o = rd32 (pages_tab + 8u * (uint32_t)page);
    if (!o) return (byte *)P + 64;          /* empty page: something harmless */
    return (byte *)atn (o, rd32 (pages_tab + 8u * (uint32_t)page + 4));
}
uint32_t PM_GetPageSize (int page) { return (uint32_t)page < n_pages ? rd32 (pages_tab + 8u * (uint32_t)page + 4) : 0; }
void PM_Startup (void) {}

/* ---- maps ---- */
#define NEARTAG 0xa7
#define FARTAG  0xa8
static void carmack (const byte *src, word *dest, int length)
{
    word ch, chhigh, count, offset;
    word *copyptr, *outptr = dest;
    length /= 2;
    while (length > 0)
    {
        ch = (word)(src[0] | src[1] << 8); src += 2;
        chhigh = ch >> 8;
        if (chhigh == NEARTAG || chhigh == FARTAG)
        {
            count = ch & 0xff;
            if (!count) { ch |= *src++; *outptr++ = ch; length--; continue; }
            if (chhigh == NEARTAG) { offset = *src++; copyptr = outptr - offset; }
            else { offset = (word)(src[0] | src[1] << 8); src += 2; copyptr = dest + offset; }
            length -= count;
            if (length < 0) return;
            while (count--) *outptr++ = *copyptr++;
        }
        else { *outptr++ = ch; length--; }
    }
}
static void rlewexpand (const word *source, word *dest, int32_t length, word tag)
{
    word *end = dest + length / 2;
    do
    {
        word value = *source++;
        if (value != tag) *dest++ = value;
        else { word count = *source++; value = *source++; for (word i = 1; i <= count && dest < end; i++) *dest++ = value; }
    } while (dest < end);
}
/* the Carmack-unpacked plane (RLEW packed, at most a little longer than the plane): in the screen buffer, which is
 * drawn anew after a level was loaded (saves 8 KB of RAM) */
#define SCRATCH_LEN (128u * 128u)
static word *scratch (void) { return (word *)pmx_screen (); }

void CA_CacheMap (int mapnum)
{
    if ((uint32_t)mapnum >= n_maps || !rd32 (maps_tab + 4u * (uint32_t)mapnum)) Quit ("no map %d", mapnum);
    uint32_t h = rd32 (maps_tab + 4u * (uint32_t)mapnum);
    pmx_read (&g_maphead, h, sizeof g_maphead);
    mapheaderseg[mapnum] = &g_maphead;
    for (int plane = 0; plane < MAPPLANES; plane++)
    {
        uint32_t pos = (uint32_t)g_maphead.planestart[plane], len = g_maphead.planelength[plane];
        if (!pos || !len) Quit ("map %d plane %d", mapnum, plane);
        const byte *src = atn (pos, len);    /* read in place (memory mapped flash) */
        word expanded = (word)(src[0] | src[1] << 8);
        if (expanded > SCRATCH_LEN) Quit ("map %d too big", mapnum);
        carmack (src + 2, scratch (), expanded);
        rlewexpand (scratch () + 1, g_plane[plane], MAPAREA * 2, (word)rlew);
    }
}
void CA_Startup (void) {}
/* the map unpacking buffer, free while a level runs (the save uses it) */
byte *pmx_ca_scratch (uint32_t *len) { *len = SCRATCH_LEN; return (byte *)scratch (); }
uint32_t pmx_pack_len (void) { return rd32 (12); }

/* ---- pics (8-bit, rows from the top; index see pmx_ca.h) ---- */
const byte *pmx_pic (int k, int *w, int *h)
{
    if ((uint32_t)k >= n_pics) { *w = *h = 0; return 0; }
    uint32_t o = rd32 (pics_tab + 8u * (uint32_t)k);
    *w = rd16 (pics_tab + 8u * (uint32_t)k + 4); *h = rd16 (pics_tab + 8u * (uint32_t)k + 6);
    if (!o) { *w = *h = 0; return 0; }
    return atn (o, (uint32_t)*w * (uint32_t)*h);
}

/* ---- PC speaker sounds: u32 length, u16 priority, bytes ---- */
const byte *pmx_sound (int k, uint32_t *len, int *prio)
{
    if ((uint32_t)k >= n_snd) return 0;
    uint32_t o = rd32 (snd_tab + 8u * (uint32_t)k);
    if (!o) return 0;
    *len = rd32 (o); *prio = rd16 (o + 4);
    return atn (o + 6, *len);
}

void pmx_read (void *dst, uint32_t off, uint32_t n)
{
    const uint8_t *s = atn (off, n); uint8_t *d = (uint8_t *)dst;
    while (n--) *d++ = *s++;
}

/* demos are not played or recorded on the Paradise */
void CA_LoadFile (const char *filename, void **ptr) { (void)filename; *ptr = 0; Quit ("no demos"); }
void CA_WriteFile (const char *filename, void *ptr, int32_t length) { (void)filename; (void)ptr; (void)length; }
