/*  sim_tool.c: the player's tools.  $2512 gives a click in the city
 *  window to one of eighteen tools.  Each tool runs a loop of its own
 *  while the mouse button is down.  This file is the work those loops do
 *  at each tile.  The mouse, the redraw and the sound stay with the
 *  caller, which drives a SimGesture through begin, step and end.
 *  Addresses point into CODE 2. */
#include "sim.h"
#include "sim_int.h"

#include <string.h>

/*  The moving-object record a bulldozer gesture drives.  $7D44 always
 *  takes record 0 of XTHG (A5+0x2BCA) and makes it kind 4.  The marker
 *  $C9 in XTXT is that record's index plus $C9, as for every other
 *  moving thing. */
enum
{
    THING_BULLDOZER = 4,
    MARK_BULLDOZER  = 0xC9
};

static uint8_t *bulldozer_thing(City *c)
{
    return c->xthg && c->xthg_len >= THING_SZ ? c->xthg : NULL;
}

static void emit(SimGesture *g, int kind, int32_t arg)
{
    if (g->n_ev < GESTURE_EVENTS)
    {
        g->ev[g->n_ev].kind = (int16_t)kind;
        g->ev[g->n_ev].arg  = arg;
        g->n_ev++;
    }
}

/* ================================================================== *
 *  $961A  the way a moving thing faces after a step from (py,px) to
 *  (y,x).  -1 when it did not move.  The larger of the two distances
 *  decides: 1 or 3 along the rows, 2 or 0 along the columns.  A tie
 *  goes to the columns.
 * ================================================================== */
static int step_direction(int y, int x, int py, int px)
{
    const int dy = y - py, dx = x - px;
    if (dy == 0 && dx == 0)
        return -1; /* $963A */
    if ((dy < 0 ? -dy : dy) > (dx < 0 ? -dx : dx))
        return dy > 0 ? 1 : 3; /* $9666 */
    return dx > 0 ? 2 : 0;     /* $9672 */
}

/* ================================================================== *
 *  $20BAA  after a tile changes, each of its four neighbors that
 *  carries a road, a rail or a power line fits its piece again.  The
 *  neighbors go north, south, west, east.  A tile in more than one
 *  range is only ever in one, because the three ranges do not overlap.
 * ================================================================== */
static void refit_surface_at(City *c, int y, int x)
{
    const int b = c->xbld[y][x];
    if (b >= 0x1D && b < 0x2C)
        sim_fit_road(c, y, x); /* $20BE8 */
    if (b >= 0x2C && b < 0x3B)
        sim_fit_rail(c, y, x); /* $20C02 */
    if (b >= 0x0E && b < 0x1D)
        sim_fit_power(c, y, x); /* $20C1C */
}

void sim_refit_surface(City *c, int y, int x)
{
    if (y > 0)
        refit_surface_at(c, y - 1, x); /* $20BBA */
    if (y < MAP_H - 1)
        refit_surface_at(c, y + 1, x); /* $20C22 */
    if (x > 0)
        refit_surface_at(c, y, x - 1); /* $20C8E */
    if (x < MAP_W - 1)
        refit_surface_at(c, y, x + 1); /* $20CF8 */
}

/* ================================================================== *
 *  $20D6E  the same for what is under the ground.  XUND 1..$0F is a
 *  subway and $10..$1E a pipe.
 * ================================================================== */
static void refit_under_at(City *c, int y, int x)
{
    const int u = c->xund[y][x];
    if (u >= 0x01 && u < 0x10)
        sim_fit_subway(c, y, x); /* $20DAC */
    if (u >= 0x10 && u < 0x1F)
        sim_fit_pipe(c, y, x); /* $20DC6 */
}

void sim_refit_under(City *c, int y, int x)
{
    if (y > 0)
        refit_under_at(c, y - 1, x); /* $20D7E */
    if (y < MAP_H - 1)
        refit_under_at(c, y + 1, x); /* $20DCC */
    if (x > 0)
        refit_under_at(c, y, x - 1); /* $20E1E */
    if (x < MAP_W - 1)
        refit_under_at(c, y, x + 1); /* $20E6E */
}

/* ================================================================== *
 *  $8064  bulldoze one tile.  Returns 0 when the tile refuses, and
 *  $FF when the bulldozer was paid and did its work.
 *
 *  A tile refuses for one of these reasons:
 *      it carries the label $FF, and the key $810 tests is up;
 *      its zone is 7, the military's;
 *      in the underground view, it has nothing underground, is not a
 *          tunnel, and does not conduct water;
 *      on the surface, it is radioactive (5), or it is empty and is not
 *          a canal (XTER $30 and up);
 *      the funds do not cover the price of the selected sub-tool;
 *      it is a tree, and the roll of one in twenty brings out the tree
 *          huggers.  The price is paid already when this happens.
 *
 *  Underground, the bulldozer clears XUND, and takes the building down
 *  only when the tile is a tunnel or a subway station.  On the surface
 *  it takes the building down, and also clears XUND under a subway
 *  station ($E9) or a tile from $6C to $70.  A canal goes back to dry
 *  ground, and each water neighbor fits its edge again.  Last, the four
 *  neighbors fit their network pieces again.
 * ================================================================== */
/*  The tunnel depth under a tile, the ALTM bits from 10 up.  1 is the
 *  depth the underground view treats as a tunnel. */
static int tunnel_depth(const City *c, int y, int x)
{
    return (c->altm[y][x] >> 10) & 0x1F; /* $8116 */
}

/*  The reasons $8064 refuses a tile before it is paid, in its order.
 *  They read the tile and nothing else, so the interface asks them too,
 *  to show a refusal before the button goes down. */
static int bulldoze_refuses(const City *c, int flags, int y, int x)
{
    const int bld = c->xbld[y][x];
    if (c->xtxt[y][x] == 0xFF && !(flags & GESTURE_KEY))
        return 1; /* $80AA */
    if ((c->xzon[y][x] & 0x0F) == 7)
        return 1;                    /* $80CA */
    if (flags & GESTURE_UNDERGROUND) /* $80E4 */
        return c->xund[y][x] == 0 && tunnel_depth(c, y, x) != 1 &&
               !(c->xbit[y][x] & XBIT_CONDUCTS_WATER);     /* $813A */
    return bld == 5 || (bld == 0 && c->xter[y][x] < 0x30); /* $8146, $816A */
}

int sim_bulldoze_tile(City *c, SimGesture *g, int y, int x)
{
    const int under = (g->flags & GESTURE_UNDERGROUND) != 0;
    const int bld   = c->xbld[y][x];
    int32_t   cost;

    if (bulldoze_refuses(c, g->flags, y, x))
        return 0;

    cost = TOOL_COST[TOOL_BULLDOZER * N_SUBTOOL + g->sub]; /* $817C */
    if (cost > c->funds)
        return 0;     /* $8186 */
    c->funds -= cost; /* $818C */
    g->spent += cost;

    if (under) /* $81A0 */
    {
        if (bld < 0x70)
            c->xbit[y][x] &= (uint8_t)~XBIT_CONDUCTS_WATER; /* $81BC */
        if (tunnel_depth(c, y, x) == 1 || c->xund[y][x] == 0x23)
            sim_demolish_and_place(c, y, x, 1); /* $81E6 */
        set_under(c, y, x, 0);                  /* $81F4 */
        sim_refit_surface(c, y, x);             /* $84BC */
        sim_refit_under(c, y, x);               /* $84C6 */
        return 0xFF;
    }

    if (bld == 0xE9 || (bld >= 0x6C && bld <= 0x70))
        set_under(c, y, x, 0); /* $8218 */
    if (bld >= 0x0D)
        sim_demolish_and_place(c, y, x, 1); /* $822E */
    else if (bld < 6)
        sim_set_tile(c, y, x, 0); /* $8244 */
    else
    {
        /*  A tree.  The roll is the Toolbox's, taken as unsigned. */
        if ((uint16_t)Random(c) % 20 == 0) /* $8258 */
        {
            emit(g, TOOL_EV_SOUND_STOP, 0x1FC); /* $8266 */
            emit(g, TOOL_EV_SOUND, 0x200);      /* $8270 */
            emit(g, TOOL_EV_PICTURE, 0x193);    /* $8282 */
            emit(g, TOOL_EV_NEWS, 0x28);        /* $828E */
            return 0;
        }
        sim_set_tile(c, y, x, 0); /* $82AA */
    }

    if (c->xter[y][x] >= 0x30) /* $82C6 */
    {
        c->xbit[y][x] &= (uint8_t)~XBIT_WATER; /* $82DC */
        if (c->xter[y][x] == 0x3E)
            sim_fix_terrain(c, y, x); /* $82F0 */
        else
            c->xter[y][x] = 0; /* $8308 */

        fit_water_around(c, y, x); /* $8330 */
    }

    sim_refit_surface(c, y, x); /* $84BC */
    sim_refit_under(c, y, x);   /* $84C6 */
    return 0xFF;
}

/* ================================================================== *
 *  $7D44  the bulldozer's loop.  It works at the tile under the mouse,
 *  and again each time the mouse comes onto another tile.  A bulldozer
 *  drives along with it: record 0 of XTHG, which carries the marker $C9
 *  in XTXT on the tile it stands on.  The sub-tool decides the work.
 *  Demolish and Raise are here.  Level ($8008), Lower ($8B04) and the
 *  de-zone ($1E782) are not reconstructed.
 * ================================================================== */
static int bulldozer_ported(int sub)
{
    return sub == 0 || sub == 2;
}

static void bulldozer_work(City *c, SimGesture *g, int y, int x)
{
    uint8_t *t = bulldozer_thing(c);

    if (c->xtxt[y][x] == 0)
        c->xtxt[y][x] = MARK_BULLDOZER; /* $7DE4 */
    if (t)
        t[5] = c->xter[y][x] != 0; /* $7E00 */

    switch (g->sub) /* $7E10 */
    {
        case 0:
            sim_bulldoze_tile(c, g, y, x); /* $7E32 */
            break;
        case 2:
            if (sim_can_raise(c, y, x)) /* $872C */
            {
                const int32_t before = c->funds;
                sim_raise_tile(c, y, x); /* $8744 */
                g->spent += before - c->funds;
            }
            break;
        default:
            break;
    }
    g->y = (int16_t)y; /* $7E94 */
    g->x = (int16_t)x;
}

/*  $7F22: the mouse came onto (y,x).  The bulldozer turns toward it and
 *  moves there.  The marker leaves the tile behind it. */
static void bulldozer_move(City *c, SimGesture *g, int y, int x)
{
    uint8_t  *t   = bulldozer_thing(c);
    const int dir = step_direction(y, x, g->y, g->x); /* $7F2A */

    if (c->xtxt[y][x] == 0)
        c->xtxt[y][x] = MARK_BULLDOZER; /* $7F48 */
    if (t)
    {
        t[1] = (uint8_t)dir; /* $7F50 */
        t[3] = (uint8_t)y;   /* $7F58 */
        t[4] = (uint8_t)x;   /* $7F60 */
    }
    if (c->xtxt[g->y][g->x] == MARK_BULLDOZER)
        c->xtxt[g->y][g->x] = 0; /* $7FC2 */
}

int sim_tool_ported(int tool, int sub)
{
    if (sub < 0 || sub >= N_SUBTOOL)
        return 0;
    return tool == TOOL_BULLDOZER && bulldozer_ported(sub);
}

static int on_map(int y, int x)
{
    return y >= 0 && y < MAP_H && x >= 0 && x < MAP_W;
}

int sim_tool_preview(const City *c, int tool, int sub, int flags, int y, int x, ToolPreview *p)
{
    p->y0    = (int16_t)y;
    p->x0    = (int16_t)x;
    p->n     = 1;
    p->price = 0;
    p->ok    = 0;
    if (!sim_tool_ported(tool, sub) || !on_map(y, x))
        return 0;
    p->price = TOOL_COST[tool * N_SUBTOOL + sub];
    p->ok    = p->price <= c->funds;
    if (tool == TOOL_BULLDOZER && sub == 0)
    {
        /*  Demolish takes the whole building down, through $5FAA, so
         *  the area is the building's footprint as $763A finds it.  In
         *  the underground view it is the one tile. */
        int oy = y, ox = x, n = 1;
        if (!(flags & GESTURE_UNDERGROUND))
            n = sim_footprint_origin(c, &oy, &ox, c->xbld[y][x]);
        if (n >= 1 && n <= 4 && ox - n + 1 >= 0 && oy + n <= MAP_H)
        {
            p->y0 = (int16_t)oy;
            p->x0 = (int16_t)ox;
            p->n  = (int16_t)n;
        }
        p->ok = p->ok && !bulldoze_refuses(c, flags, y, x);
    }
    return 1;
}

int sim_tool_begin(City *c, SimGesture *g, int tool, int sub, int flags, int y, int x)
{
    uint8_t *t;

    memset(g, 0, sizeof *g);
    g->tool  = (int16_t)tool;
    g->sub   = (int16_t)sub;
    g->flags = (uint8_t)flags;
    g->y     = (int16_t)y;
    g->x     = (int16_t)x;
    if (!sim_tool_ported(tool, sub) || !on_map(y, x))
        return 0;

    g->start_alt = (int16_t)(c->altm[y][x] & 0x1F); /* $7D6C */
    emit(g, TOOL_EV_SOUND_LOOP, 0x1FC);             /* $7D8C */
    t = bulldozer_thing(c);
    if (t) /* $7D9C */
    {
        t[0] = THING_BULLDOZER;
        t[1] = 0;
        t[8] = 0;
        t[9] = 0;
        t[6] = 0;
        t[7] = 0;
        t[5] = 0;
    }
    bulldozer_work(c, g, y, x);
    return 1;
}

int sim_tool_step(City *c, SimGesture *g, int y, int x)
{
    if (!sim_tool_ported(g->tool, g->sub) || !on_map(y, x))
        return 0;
    if (y == g->y && x == g->x)
        return 1; /* $7EF2, the loop works only on another tile */
    bulldozer_move(c, g, y, x);
    bulldozer_work(c, g, y, x); /* $7FCA */
    return 1;
}

/*  $7FCE: the button came up over (y,x).  The bulldozer moves there as
 *  for any step, does no work, and its marker goes. */
int sim_tool_end(City *c, SimGesture *g, int y, int x)
{
    if (!sim_tool_ported(g->tool, g->sub))
        return 0;
    if (on_map(y, x))
    {
        bulldozer_move(c, g, y, x);
        if (c->xtxt[y][x] == MARK_BULLDOZER)
            c->xtxt[y][x] = 0; /* $7FEC */
    }
    emit(g, TOOL_EV_SOUND_STOP, 0x1FC); /* $7FF8 */
    return 1;
}
