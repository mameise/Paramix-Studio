/* Paramix Wolf3D: the game is saved when leaving to the Paradise and can be continued at the next start.
 * Same contents as SaveTheGame / LoadTheGame in Wolf4SDL's wl_main.c (id Software), streamed in 4 KB pieces into
 * the save area of the flash instead of a file. A header tells this program's build and the pack apart, so an
 * old save after an update is not used. Author: mameise. GPL-2.0-or-later. */
#include "wl_def.h"
#include "pmx_plat.h"
#include "pmx_ca.h"

extern statetype s_grdstand, s_player;
#define SAVE_MAGIC 0x56533357u   /* "W3SV" */
#define SAVE_MAX   0xF000

typedef struct { uint32_t magic, build, pack_len, len, sum; } SaveHead;

static uint32_t build_tag (void)
{
    return (uint32_t)(uintptr_t)&s_grdstand ^ ((uint32_t)(uintptr_t)&s_player << 7) ^ (uint32_t)sizeof (objtype) << 20
         ^ (uint32_t)sizeof (statobj_t) << 26 ^ (uint32_t)sizeof (gamestate);
}

/* ---- writing: a 4 KB page at a time ---- */
static byte *wbuf;          /* borrowed: the map unpacking buffer (pmx_ca.c), free while a level runs */
static uint32_t wpos, wsum;
static int werr;
extern byte *pmx_ca_scratch (uint32_t *len);
static void wflush (void)
{
    uint32_t page = (wpos - 1) & ~0xFFFu, n = wpos - page;
    if (!werr) plat_save_page (page, wbuf, (int)n);
}
static void wr (const void *p, uint32_t n)
{
    const byte *s = (const byte *)p;
    while (n--)
    {
        if (wpos >= SAVE_MAX) { werr = 1; return; }
        wsum = wsum * 31u + *s;
        wbuf[wpos & 0xFFFu] = *s++; wpos++;
        if (!(wpos & 0xFFFu)) wflush ();
    }
}

int pmx_save_game (void)
{
    uint32_t cap; wbuf = pmx_ca_scratch (&cap);
    if (cap < 0x1000u) return 0;
    wpos = sizeof (SaveHead); wsum = 0; werr = 0;
    memset (wbuf, 0xFF, sizeof (SaveHead));
    wr (&gamestate, sizeof gamestate);
    wr (LevelRatios, sizeof LevelRatios);
    wr (tilemap, sizeof tilemap);
    for (int i = 0; i < mapwidth; i++)
        for (int j = 0; j < mapheight; j++)
        {
            objtype *ob = actorat[i][j];
            word actnum = ISPOINTER (ob) ? (word)(0x8000 | (word)(ob - objlist)) : (word)(uintptr_t)ob;
            wr (&actnum, sizeof actnum);
        }
    wr (areaconnect, sizeof areaconnect);
    wr (areabyplayer, sizeof areabyplayer);
    objtype nullobj;
    for (objtype *ob = player; ob; ob = ob->next)
    {
        memcpy (&nullobj, ob, sizeof nullobj);
        if (ob == player) nullobj.state = (statetype *)((uintptr_t)nullobj.state - (uintptr_t)&s_player);
        else nullobj.state = (statetype *)((uintptr_t)nullobj.state - (uintptr_t)&s_grdstand);
        wr (&nullobj, sizeof nullobj);
    }
    nullobj.active = ac_badobject;
    wr (&nullobj, sizeof nullobj);
    word laststatobjnum = (word)(laststatobj - statobjlist);
    wr (&laststatobjnum, sizeof laststatobjnum);
    for (statobj_t *st = &statobjlist[0]; st < laststatobj; st++) wr (st, sizeof *st);
    for (doorobj_t *door = &doorobjlist[0]; door < lastdoorobj; door++) wr (door, sizeof *door);
    wr (&pwallstate, sizeof pwallstate); wr (&pwalltile, sizeof pwalltile); wr (&pwallx, sizeof pwallx);
    wr (&pwally, sizeof pwally); wr (&pwalldir, sizeof pwalldir); wr (&pwallpos, sizeof pwallpos);
    if (werr) return 0;
    uint32_t len = wpos;
    if (wpos & 0xFFFu) wflush ();
    /* the header last: a save cut short (power gone) stays invalid */
    SaveHead h = { SAVE_MAGIC, build_tag (), pmx_pack_len (), len, wsum };
    plat_save_read (wbuf, 0x1000);
    memcpy (wbuf, &h, sizeof h);
    plat_save_page (0, wbuf, (int)(len < 0x1000u ? len : 0x1000u));
    return 1;
}

/* ---- reading: straight from the flash ---- */
static uint32_t rpos, rsum, rlen;
static int rd (void *p, uint32_t n)
{
    if (rpos + n > rlen) return 0;
    byte *d = (byte *)p;
    plat_save_at (rpos, d, (int)n);
    for (uint32_t i = 0; i < n; i++) rsum = rsum * 31u + d[i];
    rpos += n;
    return 1;
}
static int head_ok (SaveHead *h)
{
    plat_save_at (0, h, sizeof *h);
    return h->magic == SAVE_MAGIC && h->build == build_tag () && h->pack_len == pmx_pack_len () && h->len > sizeof *h && h->len <= SAVE_MAX;
}
int pmx_save_present (void) { SaveHead h; return head_ok (&h); }
void pmx_save_forget (void)
{
    uint32_t cap; byte *b = pmx_ca_scratch (&cap);
    plat_save_read (b, 0x1000);
    memset (b, 0xFF, sizeof (SaveHead));
    plat_save_page (0, b, 0x1000);
}

int pmx_load_game (void)
{
    SaveHead h;
    if (!head_ok (&h)) return 0;
    /* the checksum over everything first, so a broken save does not leave a half loaded game */
    { byte b[64]; rpos = sizeof h; rlen = h.len; rsum = 0;
      while (rpos < rlen) { uint32_t n = rlen - rpos < sizeof b ? rlen - rpos : sizeof b; rd (b, n); }
      if (rsum != h.sum) return 0; }
    rpos = sizeof h; rlen = h.len; rsum = 0;
    rd (&gamestate, sizeof gamestate);
    rd (LevelRatios, sizeof LevelRatios);
    SetupGameLevel ();
    rd (tilemap, sizeof tilemap);
    for (int i = 0; i < mapwidth; i++)
        for (int j = 0; j < mapheight; j++)
        {
            word actnum = 0; rd (&actnum, sizeof actnum);
            actorat[i][j] = (actnum & 0x8000) ? &objlist[actnum & 0x7fff] : (objtype *)(uintptr_t)actnum;
        }
    rd (areaconnect, sizeof areaconnect);
    rd (areabyplayer, sizeof areabyplayer);
    InitActorList ();
    objtype nullobj, *newobj;
    for (;;)
    {
        if (!rd (&nullobj, sizeof nullobj)) return 0;
        if (nullobj.active == ac_badobject) break;
        if (nullobj.obclass == playerobj) { newobj = player; nullobj.state = (statetype *)((uintptr_t)nullobj.state + (uintptr_t)&s_player); }
        else { newobj = GetNewActor (); nullobj.state = (statetype *)((uintptr_t)nullobj.state + (uintptr_t)&s_grdstand); }
        memcpy (newobj, &nullobj, sizeof nullobj - (sizeof nullobj.next + sizeof nullobj.prev));
    }
    word laststatobjnum = 0; rd (&laststatobjnum, sizeof laststatobjnum);
    if (laststatobjnum > MAXSTATS) return 0;
    laststatobj = &statobjlist[laststatobjnum];
    for (statobj_t *st = &statobjlist[0]; st < laststatobj; st++) rd (st, sizeof *st);
    for (doorobj_t *door = &doorobjlist[0]; door < lastdoorobj; door++) rd (door, sizeof *door);
    rd (&pwallstate, sizeof pwallstate); rd (&pwalltile, sizeof pwalltile); rd (&pwallx, sizeof pwallx);
    rd (&pwally, sizeof pwally); rd (&pwalldir, sizeof pwalldir); rd (&pwallpos, sizeof pwallpos);
    if (gamestate.secretcount)
    {
        word *map = mapsegs[0], tile;
        for (int y = 0; y < mapheight; y++)
            for (int x = 0; x < mapwidth; x++)
            {
                tile = *map;
                if (MAPSPOT (x, y, 1) == PUSHABLETILE && !tilemap[x][y] && !VALIDAREA (tile))
                {
                    if (VALIDAREA (*(map + 1))) tile = *(map + 1);
                    if (VALIDAREA (*(map - mapwidth))) tile = *(map - mapwidth);
                    if (VALIDAREA (*(map + mapwidth))) tile = *(map + mapwidth);
                    if (VALIDAREA (*(map - 1))) tile = *(map - 1);
                    *map = tile;
                    MAPSPOT (x, y, 1) = 0;
                }
                map++;
            }
    }
    Thrust (0, 0);
    lastgamemusicoffset = 0;
    return 1;
}
