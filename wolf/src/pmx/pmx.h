/* Paramix: what the Wolfenstein 3D core (Wolf4SDL) needs from its id_* layers, for a 128 x 128 screen with
 * three keys and a wheel. Replaces id_pm.h, id_sd.h, id_in.h, id_vl.h, id_vh.h, id_us.h, id_ca.h, wl_menu.h.
 * Implemented in pmx_*.c. Author: mameise. GPL-2.0-or-later like Wolf4SDL. */
#ifndef PMX_H
#define PMX_H

/* ---- SDL bits the core uses ---- */
typedef struct { uint8_t r, g, b, a; } SDL_Color;
typedef struct { byte *pixels; } SDL_Surface;
#define SDL_ALPHA_OPAQUE 255
uint32_t SDL_GetTicks (void);
void     SDL_Delay (uint32_t ms);
static inline void SDL_GetRelativeMouseState (int *x, int *y) { *x = 0; *y = 0; }

/* ---- id_pm ---- */
#define PMPageSize             (TEXTURESIZE * TEXTURESIZE)
#define PM_GetSpritePage(v)    PM_GetPage (PMSpriteStart + (v))
extern word ChunksInFile, PMSpriteStart, PMSoundStart;
void     PM_Startup (void);
uint32_t PM_GetPageSize (int page);
byte    *PM_GetPage (int page);

/* ---- id_ca ---- */
#define NUMMAPS         60
#define MAPPLANES       2
typedef struct
{
    int32_t planestart[3];
    word    planelength[3];
    word    width,height;
    char    name[16];
} maptype;
extern word    *mapsegs[MAPPLANES];
extern maptype *mapheaderseg[NUMMAPS];
void CA_Startup (void);
void CA_CacheMap (int mapnum);
#define CA_CacheAudioChunk(c) (0)
#define CA_LoadAllSounds()
#define UNCACHEAUDIOCHUNK(c)
#define UNCACHEGRCHUNK(c)
#define CA_CacheGrChunk(c)

/* ---- id_sd ---- */
#define TickBase        70
enum SDMode  { sdm_Off, sdm_PC, sdm_AdLib };
enum SMMode  { smm_Off, smm_AdLib };
enum SDSMode { sds_Off, sds_PC, sds_SoundBlaster };
typedef struct { longword length; word priority; } SoundCommon;
typedef struct { int valid; fixed globalsoundx, globalsoundy; } globalsoundpos;
extern globalsoundpos channelSoundPos[];
extern boolean AdLibPresent, SoundBlasterPresent, SoundPositioned;
extern byte    SoundMode, DigiMode, MusicMode;
#define GetTimeCount()  ((SDL_GetTicks()*7)/100)
void    Delay (int32_t wolfticks);
void    SD_Startup (void);
boolean SD_PlaySound (int sound);
void    SD_StopSound (void);
void    SD_WaitSoundDone (void);
word    SD_SoundPlaying (void);
void    SD_PositionSound (int leftvol, int rightvol);
void    SD_SetPosition (int channel, int leftvol, int rightvol);
#define SD_StartMusic(c)
#define SD_ContinueMusic(c, o)
#define SD_MusicOn()
#define SD_MusicOff() (0)
#define SD_FadeOutMusic()
#define SD_MusicPlaying() (false)
#define SD_PrepareSound(w)
#define SD_GetChannelForDigi(w) (0)

/* ---- id_in ---- */
#define sc_None 0
#define sc_Last 64
typedef int32_t ScanCode;
/* the keys the core asks for (Keyboard[]); set by pmx_in.c from the Paradise's keys */
enum { sc_Return = 1, sc_Escape, sc_Space, sc_BackSpace, sc_Tab, sc_Alt, sc_Control, sc_LShift, sc_RShift,
       sc_UpArrow, sc_DownArrow, sc_LeftArrow, sc_RightArrow, sc_Y, sc_N, sc_Pause,
       sc_1, sc_2, sc_3, sc_4, sc_5, sc_6, sc_7, sc_8, sc_9, sc_0,
       sc_F1, sc_F2, sc_F3, sc_F4, sc_F5, sc_F6, sc_F7, sc_F8, sc_F9, sc_F10, sc_F11, sc_F12,
       sc_A, sc_B, sc_C, sc_D, sc_E, sc_G, sc_H, sc_I, sc_J, sc_K, sc_L, sc_M, sc_O, sc_P, sc_Q, sc_R, sc_S, sc_T,
       sc_U, sc_V, sc_W, sc_X, sc_Z, sc_Enter2 };
#define sc_LAlt sc_Alt
#define sc_RAlt sc_Alt
#define sc_LControl sc_Control
#define sc_RControl sc_Control
#define sc_Enter sc_Return
#define key_None sc_None
enum Direction { dir_North, dir_NorthEast, dir_East, dir_SouthEast, dir_South, dir_SouthWest, dir_West, dir_NorthWest, dir_None };
typedef struct
{
    boolean button0,button1,button2,button3;
    int16_t x,y;
    int16_t xaxis,yaxis;
    byte    dir;
} ControlInfo;
extern bool     Keyboard[sc_Last];
extern boolean  MousePresent, Paused;
extern ScanCode LastScan;
#define IN_ClearKey(code) { Keyboard[code] = false; if (code == LastScan) LastScan = sc_None; }
void    IN_ClearKeysDown (void);
void    IN_Ack (void);
boolean IN_UserInput (longword delay);
void    IN_ProcessEvents (void);
static inline int  IN_MouseButtons (void) { return 0; }
static inline int  IN_JoyButtons (void) { return 0; }
static inline void IN_GetJoyDelta (int *dx, int *dy) { *dx = 0; *dy = 0; }
static inline void IN_GetJoyFineDelta (int *dx, int *dy) { *dx = 0; *dy = 0; }
static inline void IN_CenterMouse (void) {}
void    IN_StartAck (void);
boolean IN_CheckAck (void);

/* ---- id_vl / id_vh ---- */
extern SDL_Surface *screenBuffer;
extern int16_t  screenWidth, screenHeight;
extern unsigned screenPitch, bufferPitch;
extern int      scaleFactor;
extern boolean  screenfaded;
extern unsigned bordercolor;
extern uint32_t *ylookup;
extern SDL_Color gamepal[256];
#define WHITE  15
#define BLACK  0
#define VL_WaitVBL(a)        SDL_Delay((a)*8)
void VL_SetPalette  (SDL_Color *palette, bool forceupdate);
void VL_FadeOut     (int start, int end, int red, int green, int blue, int steps);
void VL_FadeIn      (int start, int end, SDL_Color *palette, int steps);
byte *VL_LockSurface (SDL_Surface *surface);
void VL_UnlockSurface (SDL_Surface *surface);
void VL_BarScaledCoord (int scx, int scy, int scwidth, int scheight, int color);
void VL_Bar (int x, int y, int width, int height, int color);
void VL_ClearScreen (int color);
void VWB_Bar (int x, int y, int width, int height, int color);
#define VWB_BarScaledCoord VL_BarScaledCoord
void VWB_DrawPic (int x, int y, int chunknum);
void VWB_DrawPicScaledCoord (int x, int y, int chunknum);
void VH_UpdateScreen (SDL_Surface *surface);
#define VW_UpdateScreen()   VH_UpdateScreen (screenBuffer)
#define VW_WaitVBL          VL_WaitVBL
#define VW_FadeIn()         VL_FadeIn(0,255,gamepal,30);
#define VW_FadeOut()        VL_FadeOut(0,255,0,0,0,30);
boolean FizzleFade (SDL_Surface *source, int x1, int y1, unsigned width, unsigned height, unsigned frames, boolean abortable);
extern byte fontcolor, backcolor;
extern int  fontnumber;
extern int  px, py;
#define SETFONTCOLOR(f,b) fontcolor=f;backcolor=b;

/* ---- id_us ---- */
#define MaxX 320
#define MaxY 200
extern boolean ingame;
extern word    PrintX, PrintY, WindowX, WindowY, WindowW, WindowH;
void US_Print (const char *s);
void US_PrintSigned (int32_t n);
void US_PrintUnsigned (longword n);
void US_CPrint (const char *s);
void US_DrawWindow (word x, word y, word w, word h);
void US_CenterWindow (word w, word h);
void US_InitRndT (int randomize);
int  US_RndT (void);
boolean US_LineInput (int x, int y, char *buf, const char *def, boolean escok, int maxchars, int maxwidth);
void US_ControlPanel (ScanCode s);

/* ---- wl_menu ---- */
#define MenuFadeOut()   VL_FadeOut(0,255,43,0,0,10)
#define MenuFadeIn()    VL_FadeIn(0,255,gamepal,10)
#define INTROSONG       NAZI_NOR_MUS
#define VIEWCOLOR       0x7f
void Message (const char *string);
void CenterWindow (word w, word h);
void ClearMemory (void);
void ClearSplitVWB (void);

/* ---- Paramix own (pmx_*.c) ---- */
int  pmx_iangle (int32_t dy, int32_t dx);   /* atan2 in whole degrees 0..359 (as (int)(atan2/(2 pi)*360)) */
void pmx_hud_dirty (void);              /* the status bar changed */
extern unsigned pmx_face;               /* face pic (game chunk number) set by DrawFace */
void pmx_status (void);                 /* draw it into the screen buffer */
uint32_t pmx_ms (void);                 /* milliseconds */
void pmx_present (void);                /* the 8-bit screen to the display */
void pmx_poll (void);                   /* keys */
extern uint32_t pmx_keys;               /* PMX_K_* held */
#define PMX_K_A     1u
#define PMX_K_B     2u
#define PMX_K_C     4u
#define PMX_K_WHEEL 8u
void pmx_buttons (void);               /* keys to buttonstate[] (PollKeyboardButtons) */
void pmx_move (int delta);              /* keys and wheel to controlx, controly (PollKeyboardMove) */
void pmx_error (const char *s);         /* shown, then back to the Paradise */
typedef struct { uint32_t fire, fwd, back, use, mod, run_dbl, invert, turn_deg; } PmxKeys;
extern PmxKeys pmx_k;                   /* key layout (from the settings block the Studio writes) */
extern int pmx_run, pmx_leave_req, pmx_sound_on;
void pmx_leave (void);                  /* save and back to the Paradise */
void pmx_idle (void);                   /* waiting: sound, leave request */
void pmx_sd_service (uint32_t now);
byte *pmx_screen (void);
const uint16_t *pmx_lut (void);
void pmx_vl_init (void);
void pmx_palshift (int r, int g, int b, int i, int steps);   /* the game's palette shifted i/steps towards r, g, b */
void pmx_text (int x, int y, const char *s, int color);
void pmx_text_c (int y, const char *s, int color);
void pmx_drawpic (int x, int y, int k);
void pmx_drawpic_scaled (int x, int y, int k, int dw, int dh);
extern int pmx_wheel;                   /* wheel steps since the last poll (+ one way) */

#endif
