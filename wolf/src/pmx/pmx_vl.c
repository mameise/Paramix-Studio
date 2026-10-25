/* Paramix Wolf3D: the screen. One 8-bit buffer of 128 x 128 (view 128 x 96 above, status bar 128 x 32 below),
 * sent to the display through a table of the current palette (RGB565).
 * Author: mameise. GPL-2.0-or-later. */
#include "wl_def.h"
#include "pmx_plat.h"
#include "pmx_ca.h"
#include "font.h"

#define RGB(r, g, b) {(r)*255/63, (g)*255/63, (b)*255/63, 0}
SDL_Color gamepal[] = {
#ifdef SPEAR
#include "sodpal.inc"
#else
#include "wolfpal.inc"
#endif
};
#undef RGB

static byte g_screen[128 * 128] __attribute__((aligned(4)));
static SDL_Surface g_surf = { g_screen };
SDL_Surface *screenBuffer = &g_surf;
int16_t  screenWidth = 128, screenHeight = 128;
unsigned screenPitch = 128, bufferPitch = 128;
int      scaleFactor = 1;
boolean  screenfaded;
unsigned bordercolor;
static uint32_t g_ylookup[128];
uint32_t *ylookup = g_ylookup;
byte fontcolor, backcolor;
int  fontnumber;
int  px, py;
static uint16_t g_lut[256];
static SDL_Color g_curpal[256];

byte *pmx_screen (void) { return g_screen; }
const uint16_t *pmx_lut (void) { return g_lut; }

void pmx_vl_init (void)
{
    for (int i = 0; i < 128; i++) g_ylookup[i] = (uint32_t)i * 128u;
    VL_SetPalette (gamepal, true);
}

int pmx_lut_changed;
void VL_SetPalette (SDL_Color *palette, bool forceupdate)
{
    (void)forceupdate;
    pmx_lut_changed = 1;
    for (int i = 0; i < 256; i++)
    {
        g_curpal[i] = palette[i];
        g_lut[i] = (uint16_t)((palette[i].r >> 3) << 11 | (palette[i].g >> 2) << 5 | (palette[i].b >> 3));
    }
}

void pmx_palshift (int r, int g, int b, int i, int steps)
{
    for (int j = 0; j < 256; j++)
    {
        const SDL_Color *p = &gamepal[j]; SDL_Color *o = &g_curpal[j];
        o->r = (uint8_t)(p->r + (r - p->r) * i / steps);
        o->g = (uint8_t)(p->g + (g - p->g) * i / steps);
        o->b = (uint8_t)(p->b + (b - p->b) * i / steps);
        g_lut[j] = (uint16_t)((o->r >> 3) << 11 | (o->g >> 2) << 5 | (o->b >> 3));
    }
    pmx_lut_changed = 1;
}

/* fades in a few steps (each one a full picture) */
#define FADE_STEPS 6
void VL_FadeOut (int start, int end, int red, int green, int blue, int steps)
{
    (void)steps;
    SDL_Color pal[256], from[256];
    memcpy (from, g_curpal, sizeof from);
    red = red * 255 / 63; green = green * 255 / 63; blue = blue * 255 / 63;
    for (int s = 1; s <= FADE_STEPS; s++)
    {
        memcpy (pal, from, sizeof pal);
        for (int i = start; i <= end; i++)
        {
            pal[i].r = (uint8_t)(from[i].r + (red - from[i].r) * s / FADE_STEPS);
            pal[i].g = (uint8_t)(from[i].g + (green - from[i].g) * s / FADE_STEPS);
            pal[i].b = (uint8_t)(from[i].b + (blue - from[i].b) * s / FADE_STEPS);
        }
        VL_SetPalette (pal, true);
        pmx_present ();
        SDL_Delay (20);
    }
    screenfaded = true;
}
void VL_FadeIn (int start, int end, SDL_Color *palette, int steps)
{
    (void)steps;
    SDL_Color pal[256], from[256];
    memcpy (from, g_curpal, sizeof from);
    for (int s = 1; s <= FADE_STEPS; s++)
    {
        memcpy (pal, from, sizeof pal);
        for (int i = start; i <= end; i++)
        {
            pal[i].r = (uint8_t)(from[i].r + (palette[i].r - from[i].r) * s / FADE_STEPS);
            pal[i].g = (uint8_t)(from[i].g + (palette[i].g - from[i].g) * s / FADE_STEPS);
            pal[i].b = (uint8_t)(from[i].b + (palette[i].b - from[i].b) * s / FADE_STEPS);
        }
        VL_SetPalette (pal, true);
        pmx_present ();
        SDL_Delay (20);
    }
    VL_SetPalette (palette, true);
    screenfaded = false;
}

byte *VL_LockSurface (SDL_Surface *surface) { return surface->pixels; }
void VL_UnlockSurface (SDL_Surface *surface) { (void)surface; }

void VL_Bar (int x, int y, int w, int h, int color)
{
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > 128) w = 128 - x;
    if (y + h > 128) h = 128 - y;
    for (int j = 0; j < h; j++) memset (g_screen + (y + j) * 128 + x, color, (size_t)(w > 0 ? w : 0));
}
void VL_BarScaledCoord (int scx, int scy, int scwidth, int scheight, int color) { VL_Bar (scx, scy, scwidth, scheight, color); }
void VWB_Bar (int x, int y, int width, int height, int color) { VL_Bar (x, y, width, height, color); }
void VL_ClearScreen (int color) { memset (g_screen, color, sizeof g_screen); }

/* a pic of the pack, cut at the screen's edges; colour 255 is not transparent in Wolf3D pics */
void pmx_drawpic (int x, int y, int k)
{
    int w, h; const byte *p = pmx_pic (k, &w, &h);
    if (!p) return;
    for (int j = 0; j < h; j++)
    {
        int yy = y + j; if (yy < 0 || yy >= 128) continue;
        for (int i = 0; i < w; i++) { int xx = x + i; if (xx >= 0 && xx < 128) g_screen[yy * 128 + xx] = p[j * w + i]; }
    }
}
/* a pic shrunk to dw x dh (nearest dot) */
void pmx_drawpic_scaled (int x, int y, int k, int dw, int dh)
{
    int w, h; const byte *p = pmx_pic (k, &w, &h);
    if (!p || dw <= 0 || dh <= 0) return;
    for (int j = 0; j < dh; j++)
    {
        int yy = y + j; if (yy < 0 || yy >= 128) continue;
        const byte *row = p + (j * h / dh) * w;
        for (int i = 0; i < dw; i++) { int xx = x + i; if (xx >= 0 && xx < 128) g_screen[yy * 128 + xx] = row[i * w / dw]; }
    }
}
/* the game's chunk numbers (gfxv_wl6.h) to the pack's pics */
int chunk_to_pk (int c)
{
#ifdef SPEAR   /* 1.6.11 test2 Spear of Destiny: the pack keeps the order of Wolf3D, the faces of SOD come after */
    if (c >= L_COLONPIC && c <= L_BJWINSPIC) return c - L_COLONPIC;
    if (c >= KNIFEPIC && c <= GOTGATLINGPIC) return c - KNIFEPIC + PK_KNIFE;
    if (c >= GODMODEFACE1PIC && c <= BJOUCHPIC) return PK_SOD_GOD1 + c - GODMODEFACE1PIC;
    switch (c)
    {
        case STATUSBARPIC: return PK_STATUSBAR;
        case TITLE1PIC: return PK_TITLE;
        case PG13PIC: return PK_PG13;
        case CREDITSPIC: return PK_CREDITS;
        case HIGHSCORESPIC: return PK_HIGHSCORES;
        case PAUSEDPIC: return PK_PAUSED;
        case GETPSYCHEDPIC: return PK_GETPSYCHED;
    }
    return -1;
#else
    return c - L_COLONPIC;
#endif
}
void VWB_DrawPic (int x, int y, int chunknum) { pmx_drawpic (x * 128 / 320, y * 128 / 320, chunk_to_pk (chunknum)); }
void VWB_DrawPicScaledCoord (int x, int y, int chunknum) { pmx_drawpic (x, y, chunk_to_pk (chunknum)); }

/* ---- text: 5 x 7 dots, 6 per letter ---- */
void pmx_text (int x, int y, const char *s, int color)
{
    for (; *s; s++, x += 6)
    {
        int c = (unsigned char)*s; if (c >= 'a' && c <= 'z') c -= 32;
        c = (c < 32 || c > 127) ? 0 : c - 32;
        for (int r = 0; r < 7; r++)
        {
            int yy = y + r; if (yy < 0 || yy >= 128) continue;
            unsigned bits = g_font[c][r];
            for (int k = 0; k < 5; k++) if ((bits & (0x10u >> k)) && x + k >= 0 && x + k < 128) g_screen[yy * 128 + x + k] = (byte)color;
        }
    }
}
void pmx_text_c (int y, const char *s, int color) { pmx_text ((128 - 6 * (int)strlen (s)) / 2 + 1, y, s, color); }

/* ---- the id print routines the game still uses (debug texts, messages) ---- */
word PrintX, PrintY, WindowX, WindowY, WindowW, WindowH;
void US_Print (const char *s)
{
    char line[24]; int n = 0;
    for (;; s++)
    {
        if (*s == '\n' || !*s || n == 21)
        {
            line[n] = 0; pmx_text (PrintX, PrintY, line, fontcolor);
            if (!*s) { PrintX += 6 * n; break; }
            PrintY += 8; PrintX = WindowX; n = 0;
            if (*s != '\n') line[n++] = *s;
            continue;
        }
        line[n++] = *s;
    }
}
void US_PrintSigned (int32_t n) { char b[16]; snprintf (b, sizeof b, "%ld", (long)n); US_Print (b); }
void US_PrintUnsigned (longword n) { char b[16]; snprintf (b, sizeof b, "%lu", (unsigned long)n); US_Print (b); }
void US_CPrint (const char *s) { US_Print (s); }
void US_DrawWindow (word x, word y, word w, word h)
{
    WindowX = x; WindowY = y; WindowW = w; WindowH = h; PrintX = x; PrintY = y;
    VL_Bar (x, y, w, h, 0x7f);
}
void US_CenterWindow (word w, word h) { US_DrawWindow (4, 20, 120, 60); (void)w; (void)h; }

void VH_UpdateScreen (SDL_Surface *surface)
{
    (void)surface;
    pmx_present ();
}

boolean FizzleFade (SDL_Surface *source, int x1, int y1, unsigned width, unsigned height, unsigned frames, boolean abortable)
{
    (void)source; (void)abortable;
    pmx_present ();
    SDL_Delay (frames * 1000u / 70u / 3u);
    (void)x1; (void)y1; (void)width; (void)height;
    return false;
}
