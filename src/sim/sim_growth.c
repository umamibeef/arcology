/*  sim_growth.c.  Whether a zone grows, and into what.  The trip walk
 *  that decides whether a tile can reach work.  The original's own lossy
 *  breadth-first search, ring and all: and the scan that walks the map
 *  asking it, tier by tier.  This is the phase the city's shape actually
 *  comes out of.  Addresses still point into CODE
 *  2. */
#include "ext80.h"
#include "sim.h"
#include "sim_int.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*  How many of the structural-change routines this scan wanted that are
 *  not reconstructed yet.  Counted rather than ignored, so the coverage
 *  figure in the report is measured and not asserted. */
static int32_t growth_todo;
static int32_t growth_stub[8];

/*  Which transport mode a trip starts in, from the tile it steps onto.
 *  $24660: the road set, then the three stations. */
static int start_mode(uint8_t t)
{
    if (t < 0x70 && (BUILDING[t].dept & (1u << DEPT_ROAD)))
        return 0;
    if (t == 0xEC)
        return 8; /* bus     */
    if (t == 0xED)
        return 10; /* rail    */
    if (t == 0xE9)
        return 11; /* subway  */
    return -1;
}

/* ================================================================== *
 *  $247EC  walkStep: one step of a trip.
 *
 *  A trip walks the transport network from a zoned tile and looks for a
 *  zone that satisfies it.  The mode says what the traveller is on now.
 *  The cost is travel time.  It is charged against a budget of 100.
 *  When the cost reaches the budget the trip fails.
 *
 *  The cost per tile IS the traffic model:
 *      road        3        band     1
 *      rail        1        subway      1
 *      board a bus, a train or a subway        4, once
 *  A road-only city spends the whole budget in about 25 tiles.  The
 *  same budget carries a trip 75 tiles along rail.  Transit works in
 *  this game because it makes the journey cheaper, not because it adds
 *  capacity.
 *
 *  Modes 0 to 3 and modes 4 to 7 are the same four states twice.  The
 *  second set means "this trip has used a bus".  A bus makes every later
 *  road tile cost 2 instead of 3.
 *
 *      0/4  road      1/5  band    2/6  bridge or tunnel
 *      3/7  a second road family      8/9  on a bus
 *      10   board a train             12   on a train
 *      11   board a subway            13   in a subway
 *
 *  This was a generated [mode][tile] table of 7,168 entries.  Three
 *  arms read a second map layer, which such a table cannot hold, and
 *  the missing conditions cost two whole cities.  See
 *  tools/walk_deps.py.
 * ================================================================== */
static int is_road_tile(int b) /* $248C8 */
{
    return (b >= 0x1D && b < 0x2C) || (b >= 0x3F && b < 0x47) || b == 0x4B ||
           b == 0x4C;
}

static int is_rail_tile(int b) /* $24DE0 */
{
    return (b >= 0x2C && b < 0x3F) || (b >= 0x45 && b < 0x49) ||
           (b >= 0x6C && b < 0x70) || b == 0x4D || b == 0x4E;
}

static WalkStep walk_step(const City *c, int mode, int y, int x)
{
    /*  When the step does not move, the mode field is not read.  Keep
     *  the current mode in it so the result matches the table exactly
     *  and tools/walkcheck can compare the two. */
    const WalkStep NO = {(int8_t)mode, 0, 0, 0};
    const int      b  = c->xbld[y][x];
    WalkStep       r  = {0, 0, 1, 0}; /* moved, same mode, no cost yet */

    r.mode = (int8_t)mode;

    /*  $2481C and $2494A.  On a road.  The bus flag picks the cost and
     *  which half of the mode range the trip moves into. */
    if (mode == 0 || mode == 4)
    {
        const int bus  = (mode == 4);
        const int road = bus ? 2 : 3;

        if (b >= 0x3F && b < 0x43)
        {
            r.mode = bus ? 6 : 2;
            r.cost = (int8_t)road;
            return r;
        }
        if ((b >= 0x51 && b < 0x5D) || b == 0x6A || b == 0x6B)
        {
            r.mode = bus ? 7 : 3;
            r.cost = (int8_t)road;
            return r;
        }
        if (b >= 0x5D && b < 0x61)
        {
            r.mode = bus ? 5 : 1;
            r.cost = 2;
            return r;
        }
        if (is_road_tile(b))
        {
            r.cost = (int8_t)road;
            return r;
        }
        if (b == 0xEC)
        {
            r.mode = bus ? 9 : 8;
            r.cost = 4;
            return r;
        } /* bus stop */
        if (b == 0xED)
        {
            r.mode = 10;
            r.cost = 4;
            return r;
        } /* rail station */
        if (b == 0xE9)
        {
            r.mode = 11;
            r.cost = 4;
            return r;
        } /* subway entrance */
        if (b > 0xFA)
        {
            r.arrived = 1;
            return r;
        }
        return NO;
    }

    /*  $24A78 and $24AD0.  On a band.  A band costs 1 a tile, so
     *  it carries a trip three times as far as a road. */
    if (mode == 1 || mode == 5)
    {
        if ((b >= 0x61 && b < 0x6C) || (b >= 0x49 && b < 0x51))
        {
            r.cost = 1;
            return r;
        }
        if (b >= 0x5D && b < 0x61)
        {
            r.mode = (mode == 5) ? 4 : 0;
            r.cost = 1;
            return r;
        }
        return NO;
    }

    /*  $24B28 and $24BF0.  On a bridge or in a tunnel.  ALTM bits 10 to
     *  14 mark the tile.  A generated table indexed by XBLD could not
     *  see this. */
    if (mode == 2 || mode == 6)
    {
        const int cost = (mode == 6) ? 2 : 3;
        if ((c->altm[y][x] >> 10) & 0x1F)
        {
            r.cost = (int8_t)cost;
            return r;
        }
        if (is_road_tile(b) || (b >= 0x5D && b < 0x61))
        {
            r.mode = (mode == 6) ? 4 : 0;
            r.cost = (int8_t)cost;
            return r;
        }
        return NO;
    }

    /*  $24CB8 and $24D34.  The second road family. */
    if (mode == 3 || mode == 7)
    {
        const int cost = (mode == 7) ? 2 : 3;
        if ((b >= 0x51 && b < 0x5D) || b == 0x6A || b == 0x6B)
        {
            r.cost = (int8_t)cost;
            return r;
        }
        if (is_road_tile(b) || (b >= 0x5D && b < 0x61))
        {
            r.mode = (mode == 7) ? 4 : 0;
            r.cost = (int8_t)cost;
            return r;
        }
        return NO;
    }

    /*  $24EC0 and $24F68.  On a bus.  A bus can end a trip, so both
     *  arms test the zone first. */
    if (mode == 8 || mode == 9)
    {
        if (b == 0xEC)
        {
            r.cost = 4;
            return r;
        }
        if (mode == 9 && b == 0xED)
        {
            r.cost = 4;
            return r;
        }
        /*  $24F52 and $24FFA also take the 5D..60 run, which the
         *  road helper does not cover. */
        if (is_road_tile(b) || (b >= 0x5D && b < 0x61))
        {
            r.mode = (mode == 8) ? 4 : 0;
            r.cost = (mode == 8) ? 2 : 3;
            return r;
        }
        return NO;
    }

    /*  $25016.  Board a train at a station, then follow the track. */
    if (mode == 10)
    {
        if (b == 0xED)
        {
            r.cost = 4;
            return r;
        }
        if (is_rail_tile(b))
        {
            r.mode = 12;
            r.cost = 1;
            return r;
        }
        return NO;
    }

    /*  $2507A.  Board a subway.  The track is in XUND, not XBLD. */
    if (mode == 11)
    {
        const int u = c->xund[y][x];
        if ((u >= 1 && u < 0x10) || u == 0x1F || u == 0x20 || u == 0x22 || u == 0x23)
        {
            r.mode = 13;
            r.cost = 1;
            return r;
        }
        return NO;
    }

    /*  $24DB0.  On a train. */
    if (mode == 12)
    {
        if (b == 0xED)
        {
            r.mode = 9;
            r.cost = 4;
            return r;
        }
        if (is_rail_tile(b))
        {
            r.cost = 1;
            return r;
        }
        if (b > 0xFA)
        {
            r.arrived = 1;
            return r;
        }
        return NO;
    }

    /*  $24E28.  In a subway.  XBLD comes first: a station brings the
     *  trip up to the surface onto a bus. */
    if (mode == 13)
    {
        const int u = c->xund[y][x];
        if (b == 0xE9)
        {
            r.mode = 9;
            r.cost = 4;
            return r;
        }
        if ((u >= 1 && u < 0x10) || u == 0x1F || u == 0x20 || u == 0x22 || u == 0x23)
        {
            r.cost = 1;
            return r;
        }
        return NO;
    }
    return NO;
}

int sim_trip(City *c, int y, int x, int zone, int tier, int budget)
{
    rng_log_mark(y * 1000 + x);
    /*  Three parallel 512-byte stacks in the original's frame: the mode
     *  the trip was in, the directions still untried there, and the
     *  length so far. */
    static uint8_t mode_st[512], mask_st[512], len_st[512];
    int            sp = 0;
    int            cy = 0, cx = 0, mode = -1, len = 0, mask = 0xF, dir = 0;
    int            turn, arrived = 0, moved;
    int            used_bus = 0, used_rail = 0, used_subway = 0;
    int            i;

    /*  $24602: the first transport tile in the search order starts the
     *  trip, and its kind picks the starting mode.  Order matters: the
     *  loop stops at the first hit. */
    for (i = 0; i < 24; i++)
    {
        int nx = x + NEIGHBOUR_ORDER[2 * i];
        int ny = y + NEIGHBOUR_ORDER[2 * i + 1];
        if (ny < 0 || ny >= MAP_H || nx < 0 || nx >= MAP_W)
            continue;
        mode = start_mode(c->xbld[ny][nx]);
        cy   = ny;
        cx   = nx;
        if (mode >= 0)
            break;
    }
    if (mode < 0)
        return 0; /* $246C2 */

    q_reset();
    q_push(cy, cx);
    mask_st[0] = 0xF;
    mode_st[0] = (uint8_t)mode;
    sp         = 1;

    turn = ((Random() & 1) * 2) + 1; /* $24706 */
    if (tier == 1)
        budget -= budget >> 2; /* $24722 */

    while (!arrived && len < budget)
    {                         /* $251F0 */
        dir   = Random() & 3; /* $24732 */
        moved = 0;
        for (i = 0; i < 4 && !moved; i++)
        { /* $250E6 */
            int ny, nx;
            dir = (dir + turn) & 3; /* $24748 */
            if (!(mask & (1 << dir)))
                continue;
            mask -= (1 << dir); /* $2476C */
            ny = cy + WALK_DY[dir];
            nx = cx + WALK_DX[dir];
            if (ny < 0 || ny >= MAP_H || nx < 0 || nx >= MAP_W)
            {
                /*  Walked off the map.  At $247C0, if the tile it stands
                 *  on is marked 0xFA in XTXT, the road leaves for a
                 *  neighboring city, and that counts as arriving. */
                if (c->xtxt[cy][cx] == 0xFA)
                {
                    moved   = 1;
                    arrived = 1;
                }
                continue;
            }
            {
                /*  $247EC: the fourteen-case switch, transcribed above
                 *  as walk_step and not as a generated [mode][tile]
                 *  table, which cannot express three of the fourteen
                 *  arms.  The subway modes read the underground layer. */
                WalkStep        step = walk_step(c, mode, ny, nx);
                const WalkStep *w    = &step;

                /*  $24848: arriving is decided first, and by the zone
                 *  the trip started in rather than by the table.  Modes
                 *  0, 4, 8 and 9 test for it ($2483C, $2496A, $24EE0 and
                 *  $24F88): a trip riding a bus can still get off at its
                 *  destination.  One in a tunnel or on the subway cannot. */
                if (mode == 0 || mode == 4 || mode == 8 || mode == 9)
                {
                    int z = XZON_TYPE(c->xzon[ny][nx]);
                    if (ZONE_ATTRACTS[zone] & (1 << z))
                    {
                        /*  $24850 sets the two flags and nothing else.
                         *  Arriving does not advance the position, so
                         *  the route on the queue stops one short of the
                         *  destination. */
                        moved   = 1;
                        arrived = 1;
                        continue;
                    }
                }
                if (!w->moved)
                    continue;
                moved = 1;
                mode  = w->mode;
                len += w->cost;
                if (w->arrived)
                    arrived = 1;
                cy = ny;
                cx = nx;
            }
        }

        if (!moved)
        {
            /*  $250F8: dead end, so unwind to the last junction that
             *  still has an untried direction. */
            do
            {
                sp--;
                if (sp <= 0)
                    break;
                q_pop_back(&cy, &cx);
                q_peek_back(&cy, &cx);
                mode = mode_st[sp - 1];
                mask = mask_st[sp - 1];
                len  = len_st[sp - 1];
            } while (mask == 0 && sp > 0);
            if (sp == 0)
                len = budget; /* $25160 */
            continue;
        }
        if (arrived)
            break; /* $25168 */

        mask_st[sp - 1] = (uint8_t)mask; /* $25178 */
        q_push(cy, cx);
        if (mode == 3 || mode == 7)
            mask = 1 << dir; /* keep going straight */
        else if (mode == 11)
            mask = 0xF;
        else
            mask = WALK_TURN_MASK[dir];
        /*  $251CE..$251E8: all three stacks are written at the CURRENT
         *  sp and only then is sp incremented.  The mode goes in last
         *  but still at the old index: the compiler kept it in d0 across
         *  the increment. */
        mask_st[sp] = (uint8_t)mask;
        len_st[sp]  = (uint8_t)len;
        mode_st[sp] = (uint8_t)mode;
        sp++;
    }

    if (trip_mark_log)
        fprintf(stderr, "TRIP %d %d zone=%d tier=%d arrived=%d len=%d sp=%d\n", y, x, zone, tier, arrived, len, sp);

    /*  $25204: a journey that failed leaves no traffic behind: the route
     *  is only drained and stamped when the trip arrived. */
    if (!arrived || tier <= 0)
        return arrived != 0;

    /*  $25216: walk the route back out.  Only the surface modes leave
     *  traffic.  Riding the subway does not put cars on the road. */
    while (!q_empty())
    {
        int py, px, m;
        q_pop_back(&py, &px);
        sp--;
        m = mode_st[sp < 0 ? 0 : sp];
        if (m == 11)
            used_subway = 1;
        if (m == 10)
            used_rail = 1;
        if (m == 8)
            used_bus = 1;
        if (m == 0 || m == 1 || m == 3)
        {
            int32_t v = (int32_t)c->xtrf[py / 2][px / 2] + tier;
            if (trip_mark_log)
                fprintf(stderr, "MARK %d %d\n", py, px);
            if (v > 255)
                v = 255;
            c->xtrf[py / 2][px / 2] = (uint8_t)v;
        }
    }
    if (used_subway)
        c->transit_subway += tier; /* $252DC */
    if (used_rail)
        c->transit_rail += tier;
    if (used_bus)
        c->transit_bus += tier;
    rng_log_mark(-1);
    return arrived != 0;
}

/* ================================================================== *
 *  growFootprint ($32998): grow a zone into a bigger building.
 *
 *  An empty lot just gets a one-tile building.  Growing a one-tile
 *  building into a 2x2 needs three free tiles beside it, and there are
 *  four ways round that can fall.  The game tries them in a fixed order
 *  and takes the first that fits, so a block grows down and to the
 *  right by preference.
 * ================================================================== */
void sim_grow_footprint(City *c, int y, int x, int tier, int zone)
{
    int alt, m = 0;

    if (tier == 0)
    {
        sim_place(c, y, x, 1, 3); /* $329BA */
        return;
    }
    if (tier == 2)
    {
        sim_place(c, y, x, 3, 3); /* $32BE2, still 2x2, just a better one */
        return;
    }
    if (tier != 1)
    {
        grow_to_3x3(c, y, x, zone); /* $32BFA */
        return;
    }

    alt = c->altm[y][x] & 0x1F; /* $329E8 */

    if (tile_fits(c, y + 1, x, alt, zone, 0x8C))
        m |= 0x01;
    if (tile_fits(c, y, x + 1, alt, zone, 0x8C))
        m |= 0x04;
    if (tile_fits(c, y + 1, x + 1, alt, zone, 0x8C))
        m |= 0x02;
    if ((m & 0x07) == 0x07)
    {
        sim_place(c, y, x + 1, 2, 3); /* $32A78, down and right */
        return;
    }
    if (tile_fits(c, y, x - 1, alt, zone, 0x8C))
        m |= 0x10;
    if (tile_fits(c, y + 1, x - 1, alt, zone, 0x8C))
        m |= 0x08;
    if ((m & 0x19) == 0x19)
    {
        sim_place(c, y, x, 2, 3); /* $32AF4, down and left */
        return;
    }
    if (tile_fits(c, y - 1, x, alt, zone, 0x8C))
        m |= 0x40;
    if (tile_fits(c, y - 1, x + 1, alt, zone, 0x8C))
        m |= 0x20;
    if ((m & 0x64) == 0x64)
    {
        sim_place(c, y - 1, x + 1, 2, 3); /* $32B6C, up and right */
        return;
    }
    if (tile_fits(c, y - 1, x - 1, alt, zone, 0x8C))
        m |= 0x80;
    if ((m & 0xD0) == 0xD0)
        sim_place(c, y - 1, x, 2, 3); /* $32BC6, up and left */
}

/*  $324B8: is this tile, or one of its four neighbors, powered?  Reads
 *  only XBIT bit 6, and stops at the first hit. */
static int powered_near(const City *c, int y, int x)
{
    if (c->xbit[y][x] & XBIT_POWERED)
        return 1;
    if (y > 1 && (c->xbit[y - 1][x] & XBIT_POWERED))
        return 1;
    if (x > 1 && (c->xbit[y][x - 1] & XBIT_POWERED))
        return 1;
    if (y < MAP_H - 1 && (c->xbit[y + 1][x] & XBIT_POWERED))
        return 1;
    if (x < MAP_W - 1 && (c->xbit[y][x + 1] & XBIT_POWERED))
        return 1;
    return 0;
}

/*  Tiles $24530 accepts as transport: the road department's own set,
 *  plus the three station buildings.  Both halves were measured by
 *  calling the routine with one tile type placed next door
 *  (tools/gen_budget.py owns the first half already). */
static int is_transport(uint8_t t)
{
    if (t < 0x70)
        return (BUILDING[t].dept & (1u << DEPT_ROAD)) != 0;
    return t == 0xE9 || t == 0xEC || t == 0xED;
}

/*  $24530: can a zone here develop?  True when some transport tile lies
 *  within an L1 distance of 3, center excluded.  The offset list in the
 *  binary is exactly that diamond: all 24 cells, confirmed by probing
 *  every offset in a 7x7 box. */
static int near_transport(const City *c, int y, int x)
{
    int dy, dx;

    for (dy = -3; dy <= 3; dy++)
    {
        for (dx = -3; dx <= 3; dx++)
        {
            int ty = y + dy, tx = x + dx;
            if ((dy == 0 && dx == 0) || abs(dy) + abs(dx) > 3)
                continue;
            if (ty < 0 || tx < 0 || ty >= MAP_H || tx >= MAP_W)
                continue;
            if (is_transport(c->xbld[ty][tx]))
                return 1;
        }
    }
    return 0;
}

void sim_growth_scan(City *c, int y0, int x0)
{
    int      y, x, church_pressure;
    uint16_t rot_mask;

    /*  $31716: is there less than one church per 2500 people?  The flag
     *  is read much later, at $31E5C, to decide whether a decaying
     *  residential building is replaced by a church. */
    church_pressure = (int32_t)c->census[0xF7] * 2500 < c->population;

    /*  $3173A: which XZON corner bit counts this rotation, so a
     *  multi-tile building is simulated once. */
    rot_mask = (uint16_t)ROT_CORNER_MASK[c->rotation & 3];

    for (y = y0; y < MAP_H; y += 4)
    {
        for (x = x0; x < MAP_W; x += 4)
        {
            uint8_t zone = (uint8_t)XZON_TYPE(c->xzon[y][x]);
            uint8_t bld  = c->xbld[y][x];

            if (zone == 0)
            {
                /* ---- unzoned ------------------------------------- */
                if (bld < 0x1D)
                    continue; /* $317B8 */

                /*  $317C0.  One tile in 128 is considered for decay.
                 *  The department that pays for it is the same one the
                 *  budget charges.  The ranges are tested in a fixed
                 *  order, so a tile owned by two departments decays
                 *  against the first of them. */
                if (game_rand127() == 0)
                {
                    static const int ORDER[5] = {DEPT_ROAD, DEPT_RAIL, DEPT_SUBWAY, DEPT_POWER, DEPT_BAND};
                    uint16_t         m        = BUILDING[bld].dept;
                    int              k, done = 0;

                    for (k = 0; k < 5 && !done; k++)
                    {
                        int d = ORDER[k];
                        if (!(m & (uint16_t)(1u << d)))
                            continue;
                        done = 1;
                        if (c->dept[d].funding == 100)
                            break; /* $31804 */
                        if (((uint16_t)Random() % 100) < (uint16_t)c->dept[d].funding)
                            break; /* $3181C, it survives */
                        if (d == DEPT_POWER || d == DEPT_BAND)
                        {
                            /*  Power lines are removed through $5FAA and
                             *  bands through an eight-tile teardown.
                             *  Neither is reconstructed. */
                            growth_todo++, growth_stub[2]++;
                            break;
                        }
                        sim_set_tile(c, y, x, (uint8_t)((Random() & 3) + 1)); /* $31824 */
                        c->xbit[y][x] &= (uint8_t)~XBIT_CONDUCTIVE;
                    }
                    if (done && bld < 0x70)
                        continue; /* went to the next tile */
                }

                /*  $31B30: the automatic builds.  Reached whether or not
                 *  the decay roll above fired.  A rail station or a
                 *  marina with power will, one time in four, put another
                 *  of itself nearby: but only while the city wants more
                 *  of them than have been placed this cycle. */
                if (y == 36 && x == 101)
                    if (bld < 0xED)
                        continue; /* $31B34 */

                if (bld == 0xED && (c->xbit[y][x] & XBIT_POWERED) && /* $31B54 */
                    game_rand3() == 0)                               /* $31B5E */
                {
                    if ((uint16_t)((uint16_t)c->census[0xED] >> 2) >
                        (uint16_t)c->road_count)        /* $31B6A */
                        sim_auto_rail_station(c, y, x); /* $31B76 */
                    continue;
                }
                if (bld == 0xF8 && (c->xbit[y][x] & XBIT_POWERED) && /* $31B9E */
                    game_rand3() == 0)                               /* $31BA8 */
                {
                    if ((uint16_t)((uint16_t)c->census[0xF8] / 9) >
                        (uint16_t)c->boat_count) /* $31BB8 */
                        auto_marina(c, y, x);    /* $31BC4 */
                    continue;
                }
                /*  $31BD0: an arcology scores its own quality of life
                 *  once a cycle, 0 to 12.  Crime and pollution take from
                 *  it and land value adds to it, each scaled down by 32.
                 *  Losing power halves the score.  Losing water halves
                 *  it again.  The score lives in byte 1 of the
                 *  arcology's XMIC record. */
                if (bld >= 0xFB && bld <= 0xFE)
                {
                    int slot, rec, score;

                    if ((c->xzon[y][x] & 0xF0) != 0x80)
                        continue; /* $31BF6 */
                    slot = c->xtxt[y][x];
                    if (slot < 0x33 || slot >= 0xC9)
                        continue; /* $31C14 */
                    rec = slot - 0x33;
                    if (!c->xmic || (size_t)(rec * 8 + 1) >= c->xmic_len)
                        continue;
                    if (c->xmic[rec * 8] < 0xFB || c->xmic[rec * 8] > 0xFE)
                        continue; /* $31C34 */

                    score = 12;
                    score -= c->xcrm[y / 2][x / 2] >> 5; /* $31C6C */
                    score -= c->xplt[y / 2][x / 2] >> 5; /* $31C82 */
                    score += c->xval[y / 2][x / 2] >> 5; /* $31C98 */
                    /*  divs truncates toward zero, so a negative score
                     *  must not be shifted here. */
                    if (!(c->xbit[y][x] & XBIT_POWERED))
                        score /= 2; /* $31CB4 */
                    if (!(c->xbit[y][x] & XBIT_WATERED))
                        score /= 2; /* $31CD2 */
                    if (score < 0)
                        score = 0; /* $31CDA */
                    if (score > 12)
                        score = 12;                        /* $31CE2 */
                    c->xmic[rec * 8 + 1] = (uint8_t)score; /* $31CEE */
                    continue;
                }
                continue;
            }

            /* ---- zoned --------------------------------------------- */
            if (zone > 6)
            {
                /*  $31FDA: a military base, airport or seaport grows its
                 *  own furniture rather than zone buildings, and only
                 *  one time in four.  The placement itself goes through
                 *  $333C8, which is not reconstructed.  The dice are
                 *  reproduced here so the tiles after this one still see
                 *  the sequence the original gave them. */
                if (zone == 7)
                {
                    /*  $31FE2: which stage the base has reached picks
                     *  which building it wants next.  Every stage but
                     *  the last rolls one time in four first. */
                    int stage = (uint8_t)c->misc[MISC_MIL_MODE]; /* $1FC0 */
                    int want;

                    if (stage == 5)
                    { /* $32180, no roll */
                        if (bld != 0xF9)
                            sim_place_special(c, y, x, 0xF9, 7); /* $321A6 */
                        continue;
                    }
                    if (stage < 2 || stage > 4)
                        continue; /* $31FFE */
                    if ((Random() & 3) != 0)
                        continue;

                    if (stage == 4)
                    { /* $32010 */
                        int n = c->infra[10];
                        if ((uint16_t)c->infra[4] >> 2 >= n)
                            want = 0xE0; /* $32022 */
                        else if (n > (uint16_t)c->infra[9] >> 2)
                            want = 0xF1; /* $32032 */
                        else if (n > (uint16_t)c->infra[6] / 3)
                            want = 0xE3; /* $32046 */
                        else
                            want = 0xF2; /* $3204C */
                        /*  the big one first.  If the map will not take
                         *  it, settle for the small one */
                        if (!sim_place_special(c, y, x, want, 7)) /* $32058 */
                            sim_place_special(c, y, x, 0xE3, 7);  /* $32070 */
                        continue;
                    }
                    if (stage == 2)
                    { /* $3208A */
                        int quota = (uint16_t)c->infra[13] / 12;
                        want      = ((uint16_t)c->infra[3] >> 2) > quota ? 0xE8 : 0xEF;
                        if (!sim_place_special(c, y, x, want, 7)) /* $320B4 */
                            sim_place_special(c, y, x, 0xE8, 7);  /* $320CC */
                        continue;
                    }
                    { /* stage 3, $320E6: a seven-way ladder, no fallback */
                        int n = (uint16_t)(c->infra[1] + c->infra[2]) / 5;
                        if ((uint16_t)c->infra[3] >> 2 >= n)
                            want = 0xDD;
                        else if (n > c->infra[11] * 2)
                            want = 0xE2;
                        else if (n > c->infra[5] * 2)
                            want = 0xEA;
                        else if (n > c->infra[12])
                            want = 0xE7;
                        else if (n > (uint16_t)c->infra[7] >> 1)
                            want = 0xE4;
                        else if (n > (uint16_t)c->infra[8] >> 1)
                            want = 0xE5;
                        else if (n > (uint16_t)c->infra[14] >> 2)
                            want = 0xF6;
                        else
                            want = 0xEF;
                        sim_place_special(c, y, x, want, 7); /* $32174 */
                        continue;
                    }
                }
                if (zone == 9)
                { /* $321BA */
                    int want;
                    if ((Random() & 3) != 0)
                    {
                        /*  $321C6: not this tile's turn.  A pier may
                         *  still launch a boat, which needs the moving
                         *  object system. */
                        if (bld != 0xE0)
                            continue;
                        if ((Random() & 3) != 0)
                            continue;          /* $321CE */
                        seaport_ship(c, y, x); /* $321E0 */
                        continue;
                    }
                    { /* $321EC */
                        int n = c->census[0xE0];
                        if ((uint16_t)c->census[0xF2] >> 2 >= n)
                            want = 0xE0;
                        else if (n > (uint16_t)c->census[0xF0] >> 2)
                            want = 0xF0;
                        else if (n > (uint16_t)c->census[0xE3] / 3)
                            want = 0xE3;
                        else
                            want = 0xF2;
                    }
                    if (!sim_place_special(c, y, x, want, 9)) /* $32234 */
                        sim_place_special(c, y, x, 0xE3, 9);  /* $3224C */
                    continue;
                }
                if (zone == 8)
                { /* $32260 */
                    int want, n;
                    if ((Random() & 3) != 0)
                    {
                        /*  $3226E: the runway spawns aircraft, which
                         *  need the moving object system. */
                        if (bld != 0xDD)
                            continue;
                        if ((uint16_t)Random() % 30 != 0)
                            continue;
                        if (!(c->xbit[y][x] & XBIT_POWERED))
                            continue; /* $3229E */
                        /*  $322A6: four times in ten a helicopter leaves
                         *  the airport.  Otherwise a plane does, and the
                         *  map rotation together with XBIT bit 1 picks
                         *  which of the two plane kinds. */
                        if ((uint16_t)Random() % 10 < 4)
                            spawn_helicopter(c, y, x); /* $322BC */
                        else if (c->rotation & 1)      /* $322CC */
                            spawn_plane(c, y, x, (c->xbit[y][x] & 0x02) ? 0 : 2);
                        else /* $32314 */
                            spawn_plane(c, y, x, (c->xbit[y][x] & 0x02) ? 2 : 0);
                        continue;
                    }
                    /*  $32352: the same seven-way ladder the military
                     *  uses at stage 3, against the building census. */
                    n = (uint16_t)(c->census[0xDD] + c->census[0xDE]) / 5;
                    if ((uint16_t)c->census[0xEE] >> 2 >= n)
                        want = 0xDD;
                    else if (n > c->census[0xE1] * 2)
                        want = 0xE1;
                    else if (n > c->census[0xEA] * 2)
                        want = 0xEA;
                    else if (n > c->census[0xE6])
                        want = 0xE6;
                    else if (n > (uint16_t)c->census[0xE4] >> 1)
                        want = 0xE4;
                    else if (n > (uint16_t)c->census[0xE5] >> 1)
                        want = 0xE5;
                    else if (n > (uint16_t)c->census[0xF6] >> 2)
                        want = 0xF6;
                    else
                        want = 0xEE;
                    sim_place_special(c, y, x, want, 8); /* $323E0 */
                    continue;
                }
                continue;
            }
            {
                int     tier;
                int32_t demand, head;

                if (bld >= BLD_ZONE_FIRST)
                { /* $31D06 */
                    if (!(XZON_CORNERS(c->xzon[y][x]) & rot_mask))
                        continue;
                    tier = BUILDING[bld].tier;
                }
                else
                {
                    if (bld >= 0x1D)
                        continue; /* $31D3A */
                    if (!near_transport(c, y, x))
                        continue;
                    tier = 0;
                }

                /*  $31D5C: a tile with no power nearby.  From which no
                 *  journey can be made, has no demand at all and the
                 *  whole 4000 of headroom. */
                if (!powered_near(c, y, x) || !sim_trip(c, y, x, zone, tier, 100))
                {
                    demand = 0;
                    head   = 4000;
                }
                else
                { /* $31D9A */
                    demand = c->rci_demand[(zone - 1) / 2] + 2000;
                    head   = 4000 - demand;
                }

                /*  $31DCC: the population accumulator.  This runs
                 *  whichever way the branch above went, which is why it
                 *  can be reconstructed before the placement engine. */
                if (tier > 0 && BUILDING[bld].tier_flag == 0)
                {
                    c->accum8[zone] += GROWTH_TABLE[tier];
                    /*  $31E0A: roll for growth against the headroom this
                     *  tier still has. */
                    if ((int32_t)(uint16_t)Random() < head / tier)
                    {
                        /* $31E1A: grow into the next tier */
                        sim_upgrade(c, y, x, tier, Random() & 1);
                        continue;
                    }
                }
                if (BUILDING[bld].tier_flag == 1)
                { /* $31E38 */
                    /*  The tier divides here.  A tier of zero would trap
                     *  the original outright and no shipped city reaches
                     *  one.  So the guard is defensive rather than
                     *  faithful: it draws the die either way, which is
                     *  what keeps the stream in step. */
                    if ((uint16_t)Random() < (uint16_t)(tier ? 0x4000 / tier : 0))
                    {
                        if (church_pressure && (tier & 2) && zone < 3)
                            sim_build_church(c, y, x); /* $32874 */
                        else
                            sim_place(c, y, x, tier, (zone - 1) / 2); /* $31E96 */
                        continue;
                    }
                }
                else if (BUILDING[bld].tier_flag == 2)
                { /* $31EA2 */
                    c->accum8[7] += GROWTH_TABLE[tier];
                    if ((int32_t)(uint16_t)Random() < (tier ? (15 * demand) / tier : 0))
                        sim_place(c, y, x, tier, (zone - 1) / 2); /* $31EFE */
                    continue;
                }
                /*  $31F0A: an empty or under-built zone grows into the
                 *  next tier.  But only where the land is worth it: each
                 *  tier has a land-value floor, and industry is exempt
                 *  from all of them. */
                if (tier == 4)
                    continue;
                if ((zone & 1) && tier > 0)
                    continue; /* $31F12 */
                if (zone < 5)
                {
                    int hv = c->xval[y / 2][x / 2];
                    if (tier == 1 && hv < 0x20)
                        continue; /* $31F46 */
                    if (tier == 2 && hv < 0x60)
                        continue; /* $31F6E */
                    if (tier == 3 && hv < 0xC0)
                        continue; /* $31F96 */
                }
                if ((int32_t)(uint16_t)Random() < (3 * demand) / (tier + 1))
                    sim_grow_footprint(c, y, x, tier, zone); /* growFootprint $32998 */
            }
        }
    }
}

int32_t sim_growth_stub(int i) { return growth_stub[i & 7]; }

int32_t sim_growth_unimplemented(void) { return growth_todo; }
