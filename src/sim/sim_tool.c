/*  sim_tool.c: the player's tools.  $2512 gives a click in the city
 *  window to one of eighteen tools.  Each tool runs a loop of its own
 *  while the mouse button is down.  This file is the work those loops do
 *  at each tile.  The mouse, the redraw and the sound stay with the
 *  caller, which drives a SimGesture through begin, step and end.
 *  Addresses point into CODE 2. */
#include "net_cell.h"
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
    const int b = T_XBLD(c, y, x);
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
    if (y < c->h - 1)
        refit_surface_at(c, y + 1, x); /* $20C22 */
    if (x > 0)
        refit_surface_at(c, y, x - 1); /* $20C8E */
    if (x < c->w - 1)
        refit_surface_at(c, y, x + 1); /* $20CF8 */
}

/* ================================================================== *
 *  $20D6E  the same for what is under the ground.  XUND 1..$0F is a
 *  subway and $10..$1E a pipe.
 * ================================================================== */
static void refit_under_at(City *c, int y, int x)
{
    const int u = T_XUND(c, y, x);
    if (u >= 0x01 && u < 0x10)
        sim_fit_subway(c, y, x); /* $20DAC */
    if (u >= 0x10 && u < 0x1F)
        sim_fit_pipe(c, y, x); /* $20DC6 */
}

void sim_refit_under(City *c, int y, int x)
{
    if (y > 0)
        refit_under_at(c, y - 1, x); /* $20D7E */
    if (y < c->h - 1)
        refit_under_at(c, y + 1, x); /* $20DCC */
    if (x > 0)
        refit_under_at(c, y, x - 1); /* $20E1E */
    if (x < c->w - 1)
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
    return (T_ALTM(c, y, x) >> 10) & 0x1F; /* $8116 */
}

/*  The reasons $8064 refuses a tile before it is paid, in its order.
 *  They read the tile and nothing else, so the interface asks them too,
 *  to show a refusal before the button goes down. */
static int bulldoze_refuses(const City *c, int flags, int y, int x)
{
    const int bld = T_XBLD(c, y, x);
    if (T_XTXT(c, y, x) == 0xFF && !(flags & GESTURE_KEY))
        return 1; /* $80AA */
    if ((T_XZON(c, y, x) & 0x0F) == 7)
        return 1;                    /* $80CA */
    if (flags & GESTURE_UNDERGROUND) /* $80E4 */
        return T_XUND(c, y, x) == 0 && tunnel_depth(c, y, x) != 1 &&
               !(T_XBIT(c, y, x) & XBIT_CONDUCTS_WATER);     /* $813A */
    return bld == 5 || (bld == 0 && T_XTER(c, y, x) < 0x30); /* $8146, $816A */
}

/*  $5FAA asked to show the collapse.  The footprint is found before
 *  the demolition clears it, and the interface is asked to show it. */
static void demolish_shown(City *c, SimGesture *g, int y, int x)
{
    int oy = y, ox = x, n = sim_footprint_origin(c, &oy, &ox, T_XBLD(c, y, x));
    if (n < 1 || n > 4)
        n = 1, oy = y, ox = x;
    if (sim_demolish_and_place(c, y, x, 1))
        emit(g, TOOL_EV_COLLAPSE, TOOL_COLLAPSE_ARG(oy, ox, n));
}

/*  The player's street data goes with the tile.  Its own two edges go,
 *  and so does the edge each neighbor in the previous column and the
 *  previous row keeps toward it.  The original holds no such data, so this changes nothing
 *  it reads. */
static void street_clear(City *c, int y, int x)
{
    T_STREET(c, y, x) = 0;
    if (x > 0)
        T_STREET(c, y, x - 1) = STREET_BYTE(0, STREET_NEXT_ROW(T_STREET(c, y, x - 1)));
    if (y > 0)
        T_STREET(c, y - 1, x) = STREET_BYTE(STREET_NEXT_COL(T_STREET(c, y - 1, x)), 0);
}

int sim_bulldoze_tile(City *c, SimGesture *g, int y, int x)
{
    const int under = (g->flags & GESTURE_UNDERGROUND) != 0;
    const int bld   = T_XBLD(c, y, x);
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
            T_XBIT(c, y, x) &= (uint8_t)~XBIT_CONDUCTS_WATER; /* $81BC */
        if (tunnel_depth(c, y, x) == 1 || T_XUND(c, y, x) == 0x23)
            demolish_shown(c, g, y, x); /* $81E6 */
        set_under(c, y, x, 0);          /* $81F4 */
        sim_refit_surface(c, y, x);     /* $84BC */
        sim_refit_under(c, y, x);       /* $84C6 */
        return 0xFF;
    }

    if (bld == 0xE9 || (bld >= 0x6C && bld <= 0x70))
        set_under(c, y, x, 0); /* $8218 */
    if (bld >= 0x0D)
        demolish_shown(c, g, y, x); /* $822E */
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

    if (T_XTER(c, y, x) >= 0x30) /* $82C6 */
    {
        T_XBIT(c, y, x) &= (uint8_t)~XBIT_WATER; /* $82DC */
        if (T_XTER(c, y, x) == 0x3E)
            sim_fix_terrain(c, y, x); /* $82F0 */
        else
            T_XTER(c, y, x) = 0; /* $8308 */

        fit_water_around(c, y, x); /* $8330 */
    }

    street_clear(c, y, x);
    sim_refit_surface(c, y, x); /* $84BC */
    sim_refit_under(c, y, x);   /* $84C6 */
    return 0xFF;
}

/* ================================================================== *
 *  $7D44  the bulldozer's loop.  It works at the tile under the mouse,
 *  and again each time the mouse comes onto another tile.  A bulldozer
 *  drives along with it: record 0 of XTHG, which carries the marker $C9
 *  in XTXT on the tile it stands on.  The sub-tool decides the work.
 *  Demolish and Raise are here.  Level ($8008) and Lower ($8B04) are
 *  not reconstructed.  De-zone is one of the zone tools below.
 * ================================================================== */
static int bulldozer_ported(int sub)
{
    return sub == 0 || sub == 2;
}

static void bulldozer_work(City *c, SimGesture *g, int y, int x)
{
    uint8_t *t = bulldozer_thing(c);

    if (T_XTXT(c, y, x) == 0)
        T_XTXT(c, y, x) = MARK_BULLDOZER; /* $7DE4 */
    if (t)
        t[5] = T_XTER(c, y, x) != 0; /* $7E00 */

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

    if (T_XTXT(c, y, x) == 0)
        T_XTXT(c, y, x) = MARK_BULLDOZER; /* $7F48 */
    if (t)
    {
        t[1] = (uint8_t)dir; /* $7F50 */
        t[3] = (uint8_t)y;   /* $7F58 */
        t[4] = (uint8_t)x;   /* $7F60 */
    }
    if (T_XTXT(c, g->y, g->x) == MARK_BULLDOZER)
        T_XTXT(c, g->y, g->x) = 0; /* $7FC2 */
}

static int on_map(const City *c, int y, int x)
{
    return y >= 0 && y < c->h && x >= 0 && x < c->w;
}

/* ================================================================== *
 *  THE ZONES.  These tools are the interface's own and reconstruct
 *  nothing.  What they write is the simulation's: the zone in the low
 *  half of XZON, as the growth pass reads it.
 *
 *  A zone is dragged as a rectangle from the tile the button went down
 *  on to the tile it comes up on.  Each tile in it that takes the zone
 *  costs the sub-tool's price.  The whole rectangle is paid or nothing
 *  is.  De-zone is the bulldozer's fifth sub-tool and writes zone 0.
 * ================================================================== */
enum
{
    SOUND_REFUSED = 0x1F5,
    SOUND_ZONED   = 0x1F7
};

/*  The zone a tool writes, or -1 when the tool writes none. */
static int zone_code(int tool, int sub)
{
    if (tool >= TOOL_RESIDENTIAL && tool <= TOOL_INDUSTRIAL && (sub == 0 || sub == 1))
        return ZONE_RES_LIGHT + 2 * (tool - TOOL_RESIDENTIAL) + sub;
    if (tool == TOOL_PORT && (sub == 0 || sub == 1))
        return sub == 0 ? ZONE_SEAPORT : ZONE_AIRPORT;
    if (tool == TOOL_BULLDOZER && sub == 4)
        return ZONE_NONE;
    return -1;
}

/*  Whether a tile takes zone `z`.  A zone goes on flat dry land that
 *  carries nothing past the trees and the rubble, and never over the
 *  military's own.  Radioactive ground (5) and a tile already in that
 *  zone do not take it.  With limits, a zone other than 0 goes only
 *  inside them, once the city has anything in it. */
static int zone_takes(const City *c, int y, int x, int z)
{
    const int b    = T_XBLD(c, y, x);
    const int have = T_XZON(c, y, x) & 0x0F;
    if ((T_XBIT(c, y, x) & XBIT_WATER) || T_XTER(c, y, x) != 0)
        return 0;
    if (b >= 0x1D || b == 0x05 || b == 0x0D)
        return 0;
    if (have == ZONE_MILITARY || have == z)
        return 0;
    if (z != ZONE_NONE && c->limits_glow > 0 && c->limits_n > 0 && !T_LIMITS(c, y, x))
        return 0;
    return 1;
}

/*  The rectangle from (y0,x0) to (y,x), as r = least row, least column,
 *  greatest row, greatest column.  Answers how many of its tiles take
 *  zone `z`. */
static int zone_rect(const City *c, int z, int y0, int x0, int y, int x, int16_t r[4])
{
    int n = 0, ty, tx;
    r[0]  = (int16_t)(y0 < y ? y0 : y);
    r[1]  = (int16_t)(x0 < x ? x0 : x);
    r[2]  = (int16_t)(y0 < y ? y : y0);
    r[3]  = (int16_t)(x0 < x ? x : x0);
    for (ty = r[0]; ty <= r[2]; ++ty)
        for (tx = r[1]; tx <= r[3]; ++tx)
            n += zone_takes(c, ty, tx, z);
    return n;
}

static void zone_apply(City *c, SimGesture *g, int y, int x)
{
    const int     z = zone_code(g->tool, g->sub);
    int16_t       r[4];
    const int     n     = zone_rect(c, z, g->y0, g->x0, y, x, r);
    const int32_t price = (int32_t)n * TOOL_COST[g->tool * N_SUBTOOL + g->sub];
    int           ty, tx;
    if (n == 0 || price > c->funds)
    {
        emit(g, TOOL_EV_SOUND, SOUND_REFUSED);
        return;
    }
    for (ty = r[0]; ty <= r[2]; ++ty)
        for (tx = r[1]; tx <= r[3]; ++tx)
        {
            if (!zone_takes(c, ty, tx, z))
                continue;
            if (z == ZONE_NONE && T_XBLD(c, ty, tx) >= 1 && T_XBLD(c, ty, tx) <= 4)
                sim_set_tile(c, ty, tx, 0); /* the rubble goes with the zone */
            T_XZON(c, ty, tx) = (uint8_t)((T_XZON(c, ty, tx) & 0xF0) | z);
        }
    c->funds -= price;
    g->spent += price;
    emit(g, TOOL_EV_SOUND, SOUND_ZONED);
    if (c->limits_glow > 0)
        sim_limits(c);
}

/* ================================================================== *
 *  THE STREET TOOLS.  These are the interface's own, and the original
 *  has nothing like them.  They write the player's street layer and
 *  nothing else, so the simulation reads nothing they change.  Each is
 *  a sub-tool of the road tool, after the original's five.
 *
 *      Upgrade Street: the class goes up one, street to avenue to
 *      boulevard and back to street.  A click takes the whole street
 *      through the tile, junction to junction.  A drag takes the edges
 *      it crosses, at the class one up from the first.
 *      One-way Street: a drag makes the edges it crosses one way, the
 *      way it was dragged.  A click makes the whole street through the
 *      tile two-way again.
 *
 *  An edge counts only while the two tiles join across it, as layer 0
 *  reads them.
 * ================================================================== */
enum
{
    STREET_SUB_CLASS  = 5,
    STREET_SUB_ONEWAY = 6
};

static int street_tool(int tool, int sub)
{
    return tool == TOOL_ROAD && (sub == STREET_SUB_CLASS || sub == STREET_SUB_ONEWAY);
}

/*  The edges the road part of a tile joins, as layer 0 reads them. */
static int road_edges(const City *c, int y, int x)
{
    NetPart p[2];
    int     n = net_cell_parts(T_XBLD(c, y, x), T_XBIT(c, y, x), p), j, e = 0;
    for (j = 0; j < n; ++j)
        if (p[j].net == NETW_ROAD)
            e |= p[j].edges;
    return e;
}

/*  The tile across edge e, and the edge back from it. */
static void across_edge(int y, int x, int e, int *ny, int *nx, int *back)
{
    *ny   = y + (e == NET_EDGE_ROW_AFTER ? 1 : e == NET_EDGE_ROW_BEFORE ? -1
                                                                        : 0);
    *nx   = x + (e == NET_EDGE_COL_AFTER ? 1 : e == NET_EDGE_COL_BEFORE ? -1
                                                                        : 0);
    *back = e == NET_EDGE_ROW_AFTER ? NET_EDGE_ROW_BEFORE : e == NET_EDGE_ROW_BEFORE ? NET_EDGE_ROW_AFTER
                                                        : e == NET_EDGE_COL_AFTER    ? NET_EDGE_COL_BEFORE
                                                                                     : NET_EDGE_COL_AFTER;
}

/*  Whether the two tiles across edge e of (y,x) join there. */
static int street_joins(const City *c, int y, int x, int e)
{
    int ny, nx, back;
    across_edge(y, x, e, &ny, &nx, &back);
    return on_map(c, y, x) && on_map(c, ny, nx) && (road_edges(c, y, x) & e) && (road_edges(c, ny, nx) & back);
}

/*  The street layer's nibble for edge e of (y,x): it lives on the tile
 *  with the lower column or row.  `keeper` says whether that is (y,x). */
static uint8_t street_get(const City *c, int y, int x, int e, int *keeper)
{
    *keeper = e == NET_EDGE_COL_AFTER || e == NET_EDGE_ROW_AFTER;
    if (e == NET_EDGE_COL_AFTER)
        return STREET_NEXT_COL(T_STREET(c, y, x));
    if (e == NET_EDGE_ROW_AFTER)
        return STREET_NEXT_ROW(T_STREET(c, y, x));
    if (e == NET_EDGE_COL_BEFORE)
        return STREET_NEXT_COL(T_STREET(c, y, x - 1));
    return STREET_NEXT_ROW(T_STREET(c, y - 1, x));
}

static void street_put(City *c, int y, int x, int e, uint8_t v)
{
    if (e == NET_EDGE_COL_BEFORE)
        x -= 1, e = NET_EDGE_COL_AFTER;
    else if (e == NET_EDGE_ROW_BEFORE)
        y -= 1, e = NET_EDGE_ROW_AFTER;
    if (e == NET_EDGE_COL_AFTER)
        T_STREET(c, y, x) = STREET_BYTE(v, STREET_NEXT_ROW(T_STREET(c, y, x)));
    else
        T_STREET(c, y, x) = STREET_BYTE(STREET_NEXT_COL(T_STREET(c, y, x)), v);
}

/*  One edge written by the tool in hand: its class, or its direction one
 *  way from (y,x) across e, or two-way where `one_way` is 0. */
static void street_set(City *c, const SimGesture *g, int y, int x, int e, int one_way)
{
    int     keeper;
    uint8_t v = street_get(c, y, x, e, &keeper);
    if (g->sub == STREET_SUB_CLASS)
        v = STREET_EDGE(g->street, STREET_DIR(v));
    else
        v = STREET_EDGE(STREET_CLASS(v), !one_way ? STREET_TWO_WAY : keeper ? STREET_OUT
                                                                            : STREET_IN);
    street_put(c, y, x, e, v);
}

/*  Every joined edge of the street through (y,x), junction to junction.
 *  The walk goes on through each tile that joins two others, and stops
 *  at a tile that joins one or three. */
static void street_whole(City *c, const SimGesture *g, int y, int x)
{
    static const int E[4] = {NET_EDGE_COL_BEFORE, NET_EDGE_ROW_AFTER, NET_EDGE_COL_AFTER, NET_EDGE_ROW_BEFORE};
    int              k;
    for (k = 0; k < 4; ++k)
    {
        int cy = y, cx = x, e = E[k], guard = c->w * c->h;
        while (guard-- > 0 && street_joins(c, cy, cx, e))
        {
            int ny, nx, back, out = 0, n = 0, j;
            street_set(c, g, cy, cx, e, 0);
            across_edge(cy, cx, e, &ny, &nx, &back);
            for (j = 0; j < 4; ++j)
                if (street_joins(c, ny, nx, E[j]))
                {
                    ++n;
                    if (E[j] != back)
                        out = E[j];
                }
            if (n != 2 || (ny == y && nx == x))
                break;
            cy = ny, cx = nx, e = out;
        }
    }
}

/*  The drag from (y0,x0) on to (y,x): each edge it crosses, a column
 *  at a time and then a row at a time. */
static void street_drag(City *c, SimGesture *g, int y0, int x0, int y, int x)
{
    while (y0 != y || x0 != x)
    {
        int e = x0 < x ? NET_EDGE_COL_AFTER : x0 > x ? NET_EDGE_COL_BEFORE
                                          : y0 < y   ? NET_EDGE_ROW_AFTER
                                                     : NET_EDGE_ROW_BEFORE;
        int ny, nx, back;
        across_edge(y0, x0, e, &ny, &nx, &back);
        if (street_joins(c, y0, x0, e))
        {
            if (g->street < 0)
            {
                int keeper;
                g->street = (int8_t)((STREET_CLASS(street_get(c, y0, x0, e, &keeper)) + 1) % 3);
            }
            street_set(c, g, y0, x0, e, 1);
        }
        y0 = ny, x0 = nx;
    }
}

int sim_tool_area(const City *c, const SimGesture *g, int y, int x, ToolPreview *p, int16_t r[4])
{
    const int z = zone_code(g->tool, g->sub);
    int       n;
    if (z < 0 || !on_map(c, y, x))
        return 0;
    n        = zone_rect(c, z, g->y0, g->x0, y, x, r);
    p->y0    = r[0];
    p->x0    = r[3];
    p->n     = 1;
    p->price = (int32_t)n * TOOL_COST[g->tool * N_SUBTOOL + g->sub];
    p->ok    = n > 0 && p->price <= c->funds;
    return 1;
}

int sim_tool_ported(int tool, int sub)
{
    if (sub < 0 || sub >= N_SUBTOOL)
        return 0;
    if (zone_code(tool, sub) >= 0 || street_tool(tool, sub))
        return 1;
    return tool == TOOL_BULLDOZER && bulldozer_ported(sub);
}

int sim_tool_preview(const City *c, int tool, int sub, int flags, int y, int x, ToolPreview *p)
{
    p->y0    = (int16_t)y;
    p->x0    = (int16_t)x;
    p->n     = 1;
    p->price = 0;
    p->ok    = 0;
    if (!sim_tool_ported(tool, sub) || !on_map(c, y, x))
        return 0;
    p->price = TOOL_COST[tool * N_SUBTOOL + sub];
    p->ok    = p->price <= c->funds;
    if (street_tool(tool, sub))
    {
        p->price = 0;
        p->ok    = road_edges(c, y, x) != 0;
        return 1;
    }
    if (zone_code(tool, sub) >= 0)
    {
        p->ok = p->ok && zone_takes(c, y, x, zone_code(tool, sub));
        return 1;
    }
    if (tool == TOOL_BULLDOZER && sub == 0)
    {
        /*  Demolish takes the whole building down, through $5FAA, so
         *  the area is the building's footprint as $763A finds it.  In
         *  the underground view it is the one tile. */
        int oy = y, ox = x, n = 1;
        if (!(flags & GESTURE_UNDERGROUND))
            n = sim_footprint_origin(c, &oy, &ox, T_XBLD(c, y, x));
        if (n >= 1 && n <= 4 && ox - n + 1 >= 0 && oy + n <= c->h)
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
    g->tool   = (int16_t)tool;
    g->sub    = (int16_t)sub;
    g->flags  = (uint8_t)flags;
    g->y      = (int16_t)y;
    g->x      = (int16_t)x;
    g->y0     = (int16_t)y;
    g->x0     = (int16_t)x;
    g->street = -1;
    if (!sim_tool_ported(tool, sub) || !on_map(c, y, x))
        return 0;
    if (zone_code(tool, sub) >= 0 || street_tool(tool, sub))
        return 1;

    g->start_alt = (int16_t)(T_ALTM(c, y, x) & 0x1F); /* $7D6C */
    emit(g, TOOL_EV_SOUND_LOOP, 0x1FC);               /* $7D8C */
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
    if (!sim_tool_ported(g->tool, g->sub) || !on_map(c, y, x))
        return 0;
    if (zone_code(g->tool, g->sub) >= 0)
    {
        g->y = (int16_t)y;
        g->x = (int16_t)x;
        return 1;
    }
    if (street_tool(g->tool, g->sub))
    {
        if (y != g->y || x != g->x)
        {
            street_drag(c, g, g->y, g->x, y, x);
            g->stepped = 1;
        }
        g->y = (int16_t)y;
        g->x = (int16_t)x;
        return 1;
    }
    /*  $7E9A: each time round, the loop starts the dozer's sound again
     *  if it has stopped.  A tree that falls stops it. */
    emit(g, TOOL_EV_SOUND_LOOP, 0x1FC);
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
    if (zone_code(g->tool, g->sub) >= 0)
    {
        zone_apply(c, g, on_map(c, y, x) ? y : g->y, on_map(c, y, x) ? x : g->x);
        return 1;
    }
    if (street_tool(g->tool, g->sub))
    {
        if (on_map(c, y, x) && (y != g->y || x != g->x))
            street_drag(c, g, g->y, g->x, y, x), g->stepped = 1;
        if (!g->stepped)
        {
            /*  A click: the whole street through the tile, a class up
             *  from the first edge it joins, or two-way again. */
            static const int E[4] = {NET_EDGE_COL_BEFORE, NET_EDGE_ROW_AFTER, NET_EDGE_COL_AFTER, NET_EDGE_ROW_BEFORE};
            int              k, keeper;
            for (k = 0; k < 4 && g->street < 0; ++k)
                if (street_joins(c, g->y0, g->x0, E[k]))
                    g->street = (int8_t)((STREET_CLASS(street_get(c, g->y0, g->x0, E[k], &keeper)) + 1) % 3);
            if (g->street >= 0)
                street_whole(c, g, g->y0, g->x0);
        }
        return 1;
    }
    if (on_map(c, y, x))
    {
        bulldozer_move(c, g, y, x);
        if (T_XTXT(c, y, x) == MARK_BULLDOZER)
            T_XTXT(c, y, x) = 0; /* $7FEC */
    }
    emit(g, TOOL_EV_SOUND_STOP, 0x1FC); /* $7FF8 */
    return 1;
}
