/* Paramix Wolf3D: start, menus, interludes and the status bar for the 128 x 128 screen.
 * BuildTables, CalcProjection, SetupWalls, SetViewSize, NewGame and the level bonus logic come from Wolf4SDL
 * (wl_main.c, wl_inter.c by id Software); the screens are new.
 * Author: mameise. GPL-2.0-or-later. */
#include "wl_def.h"
#include "pmx_plat.h"
#include "pmx_ca.h"
extern int pmx_english;
#define TX(de, en) (pmx_english ? (en) : (de))
/* settings kept on the device (4 KB settings page): sound */
typedef struct { uint32_t magic; uint8_t sound, r[3]; } PmxSet;
#define SET_MAGIC 0x31533357u   /* "W3S1" */
static void set_load (void) { PmxSet t; plat_settings_read (&t, sizeof t); if (t.magic == SET_MAGIC) pmx_sound_on = t.sound & 1; }
static void set_store (void) { PmxSet t = { SET_MAGIC, (uint8_t)pmx_sound_on, {0, 0, 0} }; PmxSet o; plat_settings_read (&o, sizeof o); if (memcmp (&o, &t, sizeof t)) plat_settings_write (&t, sizeof t); }

/* ---- wl_main.c globals ---- */
#define FOCALLENGTH     (0x5700l)
#define VIEWGLOBAL      0x10000
char    str[80];
int     dirangle[9] = {0,ANGLES/8,2*ANGLES/8,3*ANGLES/8,4*ANGLES/8,5*ANGLES/8,6*ANGLES/8,7*ANGLES/8,ANGLES};
fixed    focallength;
unsigned screenofs;
int      viewscreenx, viewscreeny;
int      viewwidth;
int      viewheight;
short    centerx,centery;
int      shootdelta;
fixed    scale;
int32_t  heightnumerator;
boolean startgame;
boolean loadedgame;
int     mouseadjustment;
boolean param_debugmode = false;
boolean param_nowait = false;
int     param_difficulty = 1;
int     param_tedlevel = -1;
int     param_mission = 0;
boolean param_goodtimes = true;
LRstruct LevelRatios[LRpack];
static short g_pixelangle[128];
static int16_t g_wallheight[128];
unsigned pmx_face;
static int hud_dirty = 1;

static const byte rndtable_[256] = {
#include "rndtable.inc"
};
static int rndindex;
void US_InitRndT (int randomize) { rndindex = randomize ? (int)((plat_ms () >> 4) & 0xff) : 0; }
int  US_RndT (void) { rndindex = (rndindex + 1) & 0xff; return rndtable_[rndindex]; }

uint32_t SDL_GetTicks (void) { return plat_ms (); }
void SDL_Delay (uint32_t ms)
{
    uint32_t t0 = plat_ms ();
    pmx_idle ();
    while (plat_ms () - t0 < ms) { plat_wait (ms - (plat_ms () - t0) > 5 ? 5 : 1); pmx_idle (); }
}
void pmx_idle (void)
{
#ifndef PMX_SD_IN_TICK   /* on the Paradise the millisecond tick serves the sound */
    pmx_sd_service (plat_ms ());
#endif
    if (pmx_leave_req) pmx_leave ();
}

/* ---- tables (wl_main.c BuildTables and CalcProjection, computed on the PC for a view 128 wide) ---- */
#include "pmx_tables.h"
void BuildTables (void) {}
void CalcProjection (int32_t focal)
{
    focallength = focal;
    scale = PMX_T_SCALE;
    heightnumerator = PMX_T_HEIGHTNUMERATOR;
    for (int i = 0; i < viewwidth && i < 128; i++) pixelangle[i] = pmx_pixelangle[i];
}
void SetupWalls (void)
{
    horizwall[0]=0;
    vertwall[0]=0;
    for (int i=1;i<MAXWALLTILES;i++)
    {
        horizwall[i]=(i-1)*2;
        vertwall[i]=(i-1)*2+1;
    }
}
boolean SetViewSize (unsigned width, unsigned height)
{
    viewwidth = width&~15;
    viewheight = height&~1;
    centerx = viewwidth/2-1;
    centery = viewheight / 2;
    shootdelta = viewwidth/10;
    viewscreenx = 0; viewscreeny = 0; screenofs = 0;
    CalcProjection (FOCALLENGTH);
    return true;
}
void NewGame (int difficulty,int episode)
{
    memset (&gamestate,0,sizeof(gamestate));
    gamestate.difficulty = difficulty;
    gamestate.weapon = gamestate.bestweapon = gamestate.chosenweapon = wp_pistol;
    gamestate.health = 100;
    gamestate.ammo = STARTAMMO;
    gamestate.lives = 3;
    gamestate.nextextra = EXTRAPOINTS;
    gamestate.episode=episode;
    startgame = true;
}
void Quit (const char *errorStr, ...)
{
    char b[80];
    va_list ap; va_start (ap, errorStr); vsnprintf (b, sizeof b, errorStr ? errorStr : "", ap); va_end (ap);
    pmx_error (b);
}

/* ---- screen out ---- */
void pmx_hud_dirty (void) { hud_dirty = 1; }
#define HUD_Y 96
static int hud_bg = 0x1d;
static void num_big (int x, int y, int digits, int32_t v)
{
    char s[12]; snprintf (s, sizeof s, "%ld", (long)v);
    int n = (int)strlen (s);
    for (int i = 0; i < digits; i++)
    {
        int k = i - (digits - n);
        pmx_drawpic (x + 8 * i, y, k < 0 ? PK_N_BLANK : PK_N_0 + (s[k] - '0'));
    }
}
void pmx_status (void)
{
    byte *s = pmx_screen ();
    memset (s + HUD_Y * 128, hud_bg, 32 * 128);
    for (int x = 0; x < 128; x++) s[HUD_Y * 128 + x] = 0x00;
    if (pmx_face) pmx_drawpic (0, HUD_Y, (int)pmx_face - L_COLONPIC);     /* 24 x 32 */
    num_big (26, HUD_Y + 2, 3, gamestate.health);                        /* 8 x 16 digits */
    pmx_text (50, HUD_Y + 6, "%", 0x0f);
    num_big (58, HUD_Y + 2, 2, gamestate.ammo > 99 ? 99 : gamestate.ammo);
    pmx_drawpic (78, HUD_Y + 2, (gamestate.keys & 1) ? PK_GOLDKEY : PK_NOKEY);
    pmx_drawpic (86, HUD_Y + 2, (gamestate.keys & 2) ? PK_SILVERKEY : PK_NOKEY);
    if (gamestate.weapon >= 0) pmx_drawpic_scaled (96, HUD_Y + 2, PK_KNIFE + gamestate.weapon, 32, 16);
    char line[24];
    snprintf (line, sizeof line, "%d-%d", gamestate.episode + 1, gamestate.mapon + 1);
    pmx_text (26, HUD_Y + 22, line, 0x0f);
    snprintf (line, sizeof line, "%d", gamestate.lives);
    pmx_drawpic_scaled (52, HUD_Y + 20, PK_FACE1A, 6, 8);
    pmx_text (60, HUD_Y + 22, line, 0x0f);
    snprintf (line, sizeof line, "%07ld", (long)gamestate.score);
    pmx_text (128 - 6 * 7, HUD_Y + 22, line, 0x0f);
    if (pmx_run) pmx_text (72, HUD_Y + 22, ">>", 0x0e);
    hud_dirty = 0;
}
static int g_hud_on, g_hud_shown;
extern int pmx_lut_changed;
/* in a level the status bar is drawn and sent only when it changed (or the palette did) */
void pmx_present (void)
{
    int full = 1;
    if (g_hud_on)
    {
        full = hud_dirty || !g_hud_shown || pmx_lut_changed;
        if (hud_dirty || !g_hud_shown) pmx_status ();
        g_hud_shown = 1;
    }
    else g_hud_shown = 0;
    pmx_lut_changed = 0;
    pmx_idle ();
    plat_lcd (pmx_screen (), 0, full ? 128 : 128 - STATUSLINES, pmx_lut ());
}

/* ---- menus ---- */
#define C_TEXT 0x0f
#define C_HI   0x0e
#define C_DIM  0x08
#define C_BACK 0x00

/* keys for menus: a fresh press of A/B (ok), C (back), wheel steps or A+C */
enum { MK_NONE, MK_UP, MK_DOWN, MK_OK, MK_BACK };
static uint32_t mk_last;
static int menu_key (void)
{
    for (;;)
    {
        pmx_idle ();
        uint32_t b = plat_keys (), n = b & ~mk_last;
        int det = plat_wheel ();
        if (pmx_k.invert) det = -det;
        mk_last = b;
        if (det > 0) return MK_DOWN;
        if (det < 0) return MK_UP;
        if (n & (PMX_K_A | PMX_K_B | PMX_K_WHEEL)) return MK_OK;
        if (n & PMX_K_C) return MK_BACK;
        plat_wait (10);
    }
}
static int menu (const char *title, const char *const *items, int n, int sel, int top_pic)
{
    mk_last = plat_keys ();
    for (;;)
    {
        VL_ClearScreen (C_BACK);
        int y = 4;
        if (top_pic) { pmx_drawpic_scaled (0, 0, PK_TITLE, 128, 80); y = 82; }
        else { pmx_text_c (y, title, C_HI); y += 12; }
        int rows = (128 - y) / 10, first = sel - rows + 1; if (first < 0) first = 0;
        for (int i = first; i < n && i < first + rows; i++)
        {
            int yy = y + (i - first) * 10;
            if (i == sel) VL_Bar (0, yy - 2, 128, 10, 0x29);
            pmx_text (4, yy, items[i], i == sel ? C_HI : C_TEXT);
        }
        pmx_present ();
        switch (menu_key ())
        {
            case MK_UP:   sel = (sel + n - 1) % n; break;
            case MK_DOWN: sel = (sel + 1) % n; break;
            case MK_OK:   return sel;
            case MK_BACK: return -1;
        }
    }
}

static const char *const EPISODES[6] = { "ESCAPE", "EISENFAUST", "DIE FUHRER DIE", "A DARK SECRET", "TRAIL OF MADMAN", "CONFRONTATION" };
static const char *const SKILLS[4] = { "CAN I PLAY, DADDY?", "DON'T HURT ME.", "BRING 'EM ON!", "I AM DEATH INCARNATE" };

static int choose_new_game (void)
{
    int ep = 0;
    if (pmx_episodes > 1)
    {
        const char *items[6]; char buf[6][24];
        for (int i = 0; i < pmx_episodes; i++) { snprintf (buf[i], sizeof buf[i], "%d %s", i + 1, EPISODES[i]); items[i] = buf[i]; }
        ep = menu ("EPISODE", items, pmx_episodes, 0, 0);
        if (ep < 0) return 0;
    }
    int sk = menu (TX ("SCHWIERIGKEIT", "DIFFICULTY"), SKILLS, 4, 2, 0);
    if (sk < 0) return 0;
    NewGame (sk, ep);
    return 1;
}

/* in game (A+C): returns with startgame set for a new game, playstate ex_abort to the title */
void US_ControlPanel (ScanCode s)
{
    (void)s;
    g_hud_on = 0;
    for (;;)
    {
        const char *items[] = { TX ("WEITER", "CONTINUE"), TX ("NEUES SPIEL", "NEW GAME"), pmx_sound_on ? TX ("TON: AN", "SOUND: ON") : TX ("TON: AUS", "SOUND: OFF"), TX ("ZUM PARADISE", "TO THE PARADISE") };
        int r = menu ("MENU", items, 4, 0, 0);
        if (r < 0 || r == 0) break;
        if (r == 1) { if (choose_new_game ()) break; continue; }
        if (r == 2) { pmx_sound_on ^= 1; if (!pmx_sound_on) SD_StopSound (); set_store (); continue; }
        if (r == 3) pmx_leave ();
    }
    VL_SetPalette (gamepal, true);
    g_hud_on = 1; hud_dirty = 1;
}

void Message (const char *string)
{
    VL_Bar (4, 30, 120, 40, 0x7f);
    PrintX = WindowX = 8; PrintY = 34; fontcolor = 0;
    US_Print (string);
    pmx_present ();
}
void ClearSplitVWB (void) {}
void DrawPlayScreen (void) { VL_ClearScreen (0); g_hud_on = 1; hud_dirty = 1; DrawFace (); }
void DrawPlayBorder (void) {}
void DrawPlayBorderSides (void) {}
void DrawStatusBorder (byte color) { (void)color; }
void Write (int x, int y, const char *string) { pmx_text (x * 128 / 40, y * 128 / 25, string, C_TEXT); }
boolean US_LineInput (int x, int y, char *buf, const char *def, boolean escok, int maxchars, int maxwidth)
{ (void)x; (void)y; (void)buf; (void)def; (void)escok; (void)maxchars; (void)maxwidth; return false; }

void PreloadGraphics (void)
{
    VL_Bar (0, 0, 128, viewheight, VIEWCOLOR);
    pmx_drawpic_scaled (0, 34, PK_GETPSYCHED, 128, 27);
    VW_UpdateScreen ();
    VW_FadeIn ();
    SDL_Delay (1000);
}

/* ---- end of a floor (bonus as in wl_inter.c LevelCompleted) ---- */
static const uint16_t PAR[60] = {   /* par times in seconds (wl_inter.c parTimes; 0 = boss or secret floor) */
    90,120,120,210,180,180,150,150,0,0,   90,210,180,120,240,360,60,180,0,0,   90,90,150,150,210,150,120,360,0,0,
    120,120,90,60,270,210,120,270,0,0,   150,90,150,150,240,180,270,210,0,0,   390,240,270,360,300,300,330,330,510,0 };
static void line2 (int y, const char *a, const char *b) { pmx_text (6, y, a, C_TEXT); pmx_text (128 - 6 * (int)strlen (b) - 4, y, b, C_HI); }
void LevelCompleted (void)
{
    char t[24], v[16];
    g_hud_on = 0;
    VL_SetPalette (gamepal, true);
    VL_ClearScreen (VIEWCOLOR);
    if (gamestate.mapon < 8)
    {
        int sec = gamestate.TimeCount / 70, kr = 0, sr = 0, tr = 0;
        int32_t timeleft = 0;
        if (sec > 99 * 60) sec = 99 * 60;
        int par = PAR[(gamestate.episode * 10 + gamestate.mapon) % 60];
        if (gamestate.TimeCount < par * 70) timeleft = par - sec;
        if (gamestate.killtotal) kr = (gamestate.killcount * 100) / gamestate.killtotal;
        if (gamestate.secrettotal) sr = (gamestate.secretcount * 100) / gamestate.secrettotal;
        if (gamestate.treasuretotal) tr = (gamestate.treasurecount * 100) / gamestate.treasuretotal;
        int32_t bonus = timeleft * 500 + 10000 * (kr >= 100) + 10000 * (sr >= 100) + 10000 * (tr >= 100);
        snprintf (t, sizeof t, "FLOOR %d COMPLETED", gamestate.mapon + 1); pmx_text_c (6, t, C_HI);
        snprintf (v, sizeof v, "%ld", (long)bonus); line2 (24, "BONUS", v);
        snprintf (v, sizeof v, "%02d:%02d", sec / 60, sec % 60); line2 (36, "TIME", v);
        snprintf (v, sizeof v, "%02d:%02d", par / 60, par % 60); line2 (46, "PAR", par > 0 ? v : "??:??");
        snprintf (v, sizeof v, "%d%%", kr); line2 (60, "KILL RATIO", v);
        snprintf (v, sizeof v, "%d%%", sr); line2 (70, "SECRET RATIO", v);
        snprintf (v, sizeof v, "%d%%", tr); line2 (80, "TREASURE RATIO", v);
        GivePoints (bonus);
        LevelRatios[gamestate.mapon].kill = kr;
        LevelRatios[gamestate.mapon].secret = sr;
        LevelRatios[gamestate.mapon].treasure = tr;
        LevelRatios[gamestate.mapon].time = sec;
        SD_PlaySound (bonus ? ENDBONUS2SND : NOBONUSSND);
    }
    else
    {
        pmx_text_c (30, "SECRET FLOOR", C_HI);
        pmx_text_c (40, "COMPLETED!", C_HI);
        pmx_text_c (60, "15000 BONUS!", C_TEXT);
        GivePoints (15000);
        SD_PlaySound (ENDBONUS2SND);
    }
    snprintf (v, sizeof v, "%ld", (long)gamestate.score); line2 (100, "SCORE", v);
    pmx_text_c (116, TX ("TASTE: WEITER", "KEY: GO ON"), C_DIM);
    pmx_present ();
    IN_Ack ();
    g_hud_on = 1; hud_dirty = 1;
}

void Victory (void)
{
    char v[24];
    int32_t sec = 0, kr = 0, sr = 0, tr = 0, n = 0;
    for (int i = 0; i < LRpack && i < 8; i++) { sec += LevelRatios[i].time; kr += LevelRatios[i].kill; sr += LevelRatios[i].secret; tr += LevelRatios[i].treasure; n++; }
    g_hud_on = 0;
    VL_SetPalette (gamepal, true);
    VL_ClearScreen (VIEWCOLOR);
    pmx_text_c (8, "YOU WIN!", C_HI);
    snprintf (v, sizeof v, "%02ld:%02ld", (long)(sec / 60) % 100, (long)(sec % 60)); line2 (30, "TOTAL TIME", v);
    snprintf (v, sizeof v, "%ld%%", (long)(kr / n)); line2 (44, "KILL", v);
    snprintf (v, sizeof v, "%ld%%", (long)(sr / n)); line2 (54, "SECRET", v);
    snprintf (v, sizeof v, "%ld%%", (long)(tr / n)); line2 (64, "TREASURE", v);
    snprintf (v, sizeof v, "%ld", (long)gamestate.score); line2 (84, "SCORE", v);
    pmx_text_c (116, TX ("TASTE: WEITER", "KEY: GO ON"), C_DIM);
    pmx_present ();
    IN_Ack ();
}
void CheckHighScore (int32_t score, word other) { (void)score; (void)other; }

/* ---- start ---- */
static void init_game (void)
{
    if (!pmx_pack_open ()) pmx_error (TX ("KEINE WOLF3D-DATEN", "NO WOLF3D DATA"));
    set_load ();
    pmx_vl_init ();
    BuildTables ();
    SetupWalls ();
    pixelangle = g_pixelangle;
    wallheight = g_wallheight;
    viewsize = 20;
    SetViewSize (128, 128 - STATUSLINES);
    InitRedShifts ();
    US_InitRndT (true);
    hud_bg = 0x1d;
    { int w, h; const byte *sb = pmx_pic (PK_STATUSBAR, &w, &h); if (sb && w > 4 && h > 20) hud_bg = sb[20 * w + 2]; }
}

void pmx_leave (void)
{
    pmx_leave_req = 0;
    plat_tone (0);
    if (ingame && !playstate && gamestate.health > 0)
    {
        VL_Bar (0, 40, 128, 16, 0);
        pmx_text_c (45, TX ("SPEICHERE", "SAVING"), C_HI);
        plat_lcd (pmx_screen (), 40, 56, pmx_lut ());
        pmx_save_game ();
    }
    plat_back ();
}

void pmx_main (void)
{
    init_game ();
    for (;;)
    {
        int cont = pmx_save_present ();
        const char *items[4]; int n = 0, act[4];
        if (cont) { items[n] = TX ("WEITER", "CONTINUE"); act[n++] = 0; }
        items[n] = TX ("NEUES SPIEL", "NEW GAME"); act[n++] = 1;
        items[n] = pmx_sound_on ? TX ("TON: AN", "SOUND: ON") : TX ("TON: AUS", "SOUND: OFF"); act[n++] = 2;
        items[n] = TX ("ZUM PARADISE", "TO THE PARADISE"); act[n++] = 3;
        g_hud_on = 0;
        VL_SetPalette (gamepal, true);
        int r = menu ("", items, n, 0, 1);
        if (r < 0) continue;
        r = act[r];
        if (r == 0) { if (!pmx_load_game ()) { pmx_save_forget (); continue; } loadedgame = true; }
        else if (r == 1) { if (!choose_new_game ()) continue; }
        else if (r == 2) { pmx_sound_on ^= 1; set_store (); continue; }
        else pmx_leave ();
        g_hud_on = 1;
        GameLoop ();
        g_hud_on = 0;
    }
}

/* atan2 without floating point: the first quadrant from finetangent (tenths of a degree), then the quadrant */
int pmx_iangle (int32_t dy, int32_t dx)
{
    int64_t ay = dy < 0 ? -(int64_t)dy : dy, ax = dx < 0 ? -(int64_t)dx : dx;
    int a;
    if (!ax) a = ay ? 900 : 0;
    else
    {
        int64_t r = (ay << 16) / ax;
        int lo = 0, hi = FINEANGLES / 4;            /* first a with tan(a + 0.5 tenth) > r */
        while (lo < hi) { int m = (lo + hi) / 2; if (finetangent[m] <= r) lo = m + 1; else hi = m; }
        a = lo;                                     /* tenths of a degree, 0..900 */
    }
    if (dx < 0) a = 1800 - a;
    if (dy < 0) a = 3600 - a;
    if (a >= 3600) a -= 3600;
    return a / 10;
}
