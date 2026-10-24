/* Paramix Wolf3D: the Paradise's keys to the game (same layout as DOOM on the Paradise, set in the Studio):
 * A fire, B forward, C back, wheel turns, wheel press use (doors, switches), hold C + wheel: weapon,
 * B twice quickly: run on/off, A+C: menu, wheel held 2 s: save and back to the Paradise.
 * The hold key (C) only moves back after 120 ms and while the wheel was not turned.
 * Author: mameise. GPL-2.0-or-later. */
#include "wl_def.h"
#include "pmx_plat.h"

bool     Keyboard[sc_Last];
boolean  MousePresent, Paused;
ScanCode LastScan;
uint32_t pmx_keys;
int      pmx_wheel;

PmxKeys pmx_k = { PMX_K_A, PMX_K_B, PMX_K_C, PMX_K_WHEEL, PMX_K_C, 1, 0, 15 };
int pmx_run;                 /* run on (B twice) */
static int turn_pending;     /* controlx units still to turn (20 per degree) */
static int use_left, chg_left, chg_dir;
static uint32_t mod_t0, b_last, b_prev, wh_t0;
static int det_seen, menu_hold, esc_req;
int pmx_leave_req;

/* once per game tic batch (PollControls): read the keys and the wheel */
void pmx_poll (void)
{
    uint32_t now = plat_ms (), b = plat_keys ();
    int det = plat_wheel ();
    if (pmx_k.invert) det = -det;
    pmx_keys = b;
    /* wheel pressed: short = use, held 2 s = leave */
    if (b & PMX_K_WHEEL) { if (!wh_t0) wh_t0 = now | 1u; else if (now - wh_t0 > 2000u) pmx_leave_req = 1; }
    else { if (wh_t0 && now - wh_t0 < 800u && pmx_k.use == PMX_K_WHEEL) use_left = 2; wh_t0 = 0; }
    if (det)
    {
        if (b & pmx_k.mod) { chg_left = 1; chg_dir = det > 0; det_seen = 1; }
        else
        {
            int add = det * pmx_k.turn_deg * 20;
            if ((add > 0) != (turn_pending > 0)) turn_pending = 0;
            turn_pending += add;
        }
    }
    int ac = (b & PMX_K_A) && (b & PMX_K_C);
    if (ac) { if (++menu_hold == 1) esc_req = 1; } else menu_hold = 0;
    if (!(b & pmx_k.mod)) { mod_t0 = 0; det_seen = 0; }
    else if (!mod_t0) mod_t0 = now | 1u;
    if (pmx_k.run_dbl && (b & pmx_k.fwd) && !(b_last & pmx_k.fwd))
    {
        if (b_prev && now - b_prev < 300u) { pmx_run ^= 1; b_prev = 0; pmx_hud_dirty (); } else b_prev = now;
    }
    if (pmx_k.use != PMX_K_WHEEL && (b & pmx_k.use) && !(b_last & pmx_k.use)) use_left = 2;
    b_last = b;
}

static int held (uint32_t key)
{
    uint32_t b = pmx_keys;
    if ((b & PMX_K_A) && (b & PMX_K_C)) return 0;           /* the menu pair */
    if (!(b & key)) return 0;
    if (key == pmx_k.mod) return !det_seen && mod_t0 && (int)(plat_ms () - mod_t0) >= 120;
    return 1;
}

void pmx_buttons (void)
{
    pmx_poll ();
    if (held (pmx_k.fire)) buttonstate[bt_attack] = true;
    if (use_left) { use_left--; buttonstate[bt_use] = true; }
    if (pmx_run) buttonstate[bt_run] = true;
    if (chg_left) { chg_left--; buttonstate[chg_dir ? bt_nextweapon : bt_prevweapon] = true; }
    if (esc_req) { esc_req = 0; buttonstate[bt_esc] = true; }
}

void pmx_move (int delta)
{
    if (held (pmx_k.fwd)) controly -= delta;
    if (held (pmx_k.back)) controly += delta;
    if (turn_pending)
    {
        int max = 100 * (int)tics, t = turn_pending;
        if (t > max) t = max;
        if (t < -max) t = -max;
        controlx += t; turn_pending -= t;
    }
}

/* ---- the id input calls the game still makes ---- */
void IN_ClearKeysDown (void) { memset (Keyboard, 0, sizeof Keyboard); LastScan = sc_None; esc_req = 0; }
void IN_ProcessEvents (void) { if (pmx_leave_req) pmx_leave (); }

/* a fresh key press (any of A, B, C, wheel press, or a wheel step) */
static uint32_t ack_last;
void IN_StartAck (void) { ack_last = plat_keys (); plat_wheel (); }
boolean IN_CheckAck (void)
{
    uint32_t b = plat_keys ();
    int det = plat_wheel ();
    boolean r = (b & ~ack_last) != 0;
    ack_last = b;
    (void)det;
    pmx_idle ();
    return r;
}
void IN_Ack (void) { IN_StartAck (); while (!IN_CheckAck ()) plat_wait (10); }
boolean IN_UserInput (longword delay)
{
    uint32_t t0 = plat_ms (), ms = delay * 1000u / 70u;
    IN_StartAck ();
    while (plat_ms () - t0 < ms) { if (IN_CheckAck ()) return true; plat_wait (10); }
    return false;
}

/* the key layout from the settings block (the Studio writes it) */
int pmx_english;
void pmx_cfg_keys (uint32_t fire, uint32_t fwd, uint32_t back, uint32_t use, uint32_t mod, uint32_t run_dbl, uint32_t invert, uint32_t turn_deg, uint32_t english)
{
    if (fire && fwd && back && fire != fwd && fire != back && fwd != back)
    { pmx_k.fire = fire; pmx_k.fwd = fwd; pmx_k.back = back; pmx_k.use = use ? use : PMX_K_WHEEL; pmx_k.mod = mod; }
    pmx_k.run_dbl = run_dbl; pmx_k.invert = invert;
    if (turn_deg >= 2 && turn_deg <= 90) pmx_k.turn_deg = turn_deg;
    pmx_english = (int)english;
}
