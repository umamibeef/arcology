/*  sim_place.c -- putting something down deliberately.  What the player
 *  builds and what the city builds for itself: the zone tiers, the upgrade,
 *  the church, the special buildings with their runways and cranes, and the
 *  stations and marinas a zone gets automatically when it grows next to the
 *  right thing.  Split out of sim.c; addresses still point into CODE 2. */
#include "ext80.h"
#include "sim.h"
#include "sim_int.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const int SCAN_DY[4] = {0, 2, 0, -1}; /* A5-0x623E */
static const int SCAN_DX[4] = {-1, 0, 2, 0}; /* A5-0x6236 */
/*  The ring, anchored at the top-right corner.  Used twice: once to
 *  test and once to clear, in the same order. */
static const int RING_DY[8] = {0, 0, 0, 1, 2, 2, 2, 1};
static const int RING_DX[8] = {0, -1, -2, -2, -2, -1, 0, 0};
/* ================================================================== *
 *  $33A90 -- lay a crane and its pier.
 *
 *  A seaport reaches into the water: the anchor tile takes the crane
 *  and four tiles running away from it take the pier.  The direction is
 *  whichever of the four neighbours is water, tried east, south, west,
 *  north, and the first one wins.
 *
 *  The conditions are strict, which is why a seaport grows slowly: the
 *  four tiles have to be water, empty, on the map, and the last of them
 *  deep enough -- its own altitude plus two no higher than the water
 *  level stored in the same word.  A strip running east or west also
 *  wants an even row, and one running north or south an even column.
 * ================================================================== */
static const int CRANE_DY[4] = {0, 1, 0, -1};
static const int CRANE_DX[4] = {1, 0, -1, 0};

/* ================================================================== *
 *  $B0BC  autoRailStationTry -- put a train on this tile if it is rail,
 *  unoccupied, and the city has not already auto-placed five things
 *  this cycle.  A train is three records: one locomotive and two
 *  carriages, chained through byte +2, with the tile tagged in XTXT so
 *  nothing else lands on it.
 * ================================================================== */
static int auto_rail_station_try(City *c, int y, int x)
{
    int b, dir, loco, car1, car2;

    if (c->road_count >= 5)
        return 0; /* $B0CC */
    if (y < 2 || y > 0x7C)
        return 0; /* $B0D6 */
    if (x < 2 || x > 0x7C)
        return 0; /* $B0E8 */

    /*  the tile itself has to be rail of some kind */
    b = c->xbld[y][x];
    if (!((b >= 0x2C && b < 0x3F) || (b >= 0x45 && b < 0x49) ||
          (b >= 0x6C && b < 0x70) || b == 0x4D || b == 0x4E))
        return 0; /* $B180 */

    /*  $B19A adds $FFD4 in a WORD and then compares SIGNED, so this is
     *  "bld - $2C <= 9" with 16-bit wraparound: only ids $2C..$35 get
     *  through, and everything the four range tests above admitted --
     *  $4D, $4E, the $45 and $6C runs -- is rejected here.  Computing
     *  the sum in int, without the wrap, rejects everything. */
    if ((int16_t)(uint16_t)(c->xbld[y][x] + 0xFFD4) > 9)
        return 0; /* $B19E */
    if (c->xtxt[y][x] != 0)
        return 0; /* $B1BC */

    dir = pick_direction(c, y, x, (int)game_rand(4), 0x0A); /* $B1CC/$B1DA */
    if (dir < 0)
        return 0; /* $B1E8 */

    loco              = alloc_thing(c);
    thing(c, loco)[0] = 0x0A; /* $B1FE */
    car1              = alloc_thing(c);
    thing(c, car1)[0] = 0x0B; /* $B212 */
    car2              = alloc_thing(c);
    thing(c, car2)[0] = 0x0B; /* $B226 */

    thing(c, loco)[1] = (uint8_t)dir; /* $B234 */
    thing(c, car1)[1] = (uint8_t)dir;
    thing(c, car2)[1] = (uint8_t)dir;

    thing(c, loco)[3] = (uint8_t)y; /* $B25E */
    thing(c, car1)[3] = (uint8_t)y;
    thing(c, car2)[3] = (uint8_t)y;
    thing(c, loco)[4] = (uint8_t)x; /* $B282 */
    thing(c, car1)[4] = (uint8_t)x;
    thing(c, car2)[4] = (uint8_t)x;

    /*  only the locomotive looks ahead; the carriages sit on the tile */
    thing(c, loco)[6] = (uint8_t)(y + STEP_DY[dir]); /* $B2B6 */
    thing(c, car1)[6] = (uint8_t)y;
    thing(c, car2)[6] = (uint8_t)y;
    thing(c, loco)[7] = (uint8_t)(x + STEP_DX[dir]); /* $B2EA */
    thing(c, car1)[7] = (uint8_t)x;
    thing(c, car2)[7] = (uint8_t)x;

    thing(c, loco)[5] = 0; /* $B30E */
    thing(c, car1)[5] = 0;
    thing(c, car2)[5] = 0;

    thing(c, loco)[0x0A] = c->xtxt[y][x]; /* $B340 */
    thing(c, car1)[0x0A] = 0;
    thing(c, car2)[0x0A] = 0;

    thing(c, loco)[2] = (uint8_t)car1; /* $B366 */
    thing(c, car1)[2] = (uint8_t)car2; /* $B372 */

    c->xtxt[y][x] = (uint8_t)(loco + 0xC9); /* $B382 */
    c->road_count++;                        /* $B386 */
    return 1;
}

void grow_to_3x3(City *c, int y, int x, int zone)
{
    int alt = c->altm[y][x] & 0x1F; /* $32C14 */
    int idx;

    for (idx = 0; idx < 4; idx++) /* $33016 */
    {
        /*  $32C20 -- the four anchors: (y, x), (y-1, x), (y, x+1),
         *  (y-1, x+1). */
        int ay = y - (idx & 1);
        int ax = x + (idx >> 1);
        int i, road = 0;

        for (i = 0; i < 8; i++) /* $32C4E .. $32D4E */
            if (!tile_fits(c, ay + RING_DY[i], ax + RING_DX[i], alt, zone, 0xAE))
                break;
        if (i != 8)
            continue; /* $33012, this anchor will not take it */

        /*  $32D58 -- road access.  Four diagonals just outside the
         *  block, each accepting the road pieces that actually point
         *  into it.  Any one of them is enough. */
        if (ay > 0 && ay < 127)
        {
            int b = c->xbld[ay - 1][ax + 1];
            if (b == 0x23 || b == 0x27 || b == 0x28 || b == 0x2B)
                road = 1;
        }
        if (!road && ay > 0 && ax > 2) /* $32DA6 */
        {
            int b = c->xbld[ay - 1][ax - 3];
            if (b == 0x24 || b == 0x28 || b == 0x29 || b == 0x2B)
                road = 1;
        }
        if (!road && ay < 125 && ax > 2) /* $32DEE */
        {
            int b = c->xbld[ay + 3][ax - 3];
            if (b == 0x25 || b == 0x29 || b == 0x2A || b == 0x2B)
                road = 1;
        }
        if (!road && ay < 125 && ax < 127) /* $32E36 */
        {
            int b = c->xbld[ay + 3][ax + 1];
            if (b == 0x26 || b == 0x2A || b == 0x27 || b == 0x2B)
                road = 1;
        }
        if (!road)
            continue; /* $32E86 */

        /*  $32E90 -- anything substantial already standing on the ring
         *  is demolished first, in the same order it was tested. */
        for (i = 0; i < 8; i++)
            if (c->xbld[ay + RING_DY[i]][ax + RING_DX[i]] >= 0x8C)
                clear_footprint(c, ay + RING_DY[i], ax + RING_DX[i]);

        sim_place(c, ay, ax, 4, 3); /* $3300A */
        return;                     /* $33010 */
    }
}

/* ================================================================== *
 *  $C104  autoMarinaTry -- put a boat on this water tile.  A boat is a
 *  single record, not a chain: kind $09, a direction taken from the
 *  dice rather than by looking where it can go, and both "ahead" bytes
 *  fixed at 4.
 * ================================================================== */
static int auto_marina_try(City *c, int y, int x)
{
    int      slot = alloc_thing(c); /* $C114 */
    uint8_t *t;

    if (slot == 0)
        return 0; /* $C11C, the table is full */
    if (c->xbld[y][x] != 0)
        return 0; /* $C132 */
    if (c->xtxt[y][x] != 0)
        return 0; /* $C148 */

    t       = thing(c, slot);
    t[0]    = 0x09;                  /* $C154 */
    t[1]    = (uint8_t)game_rand(3); /* $C15E */
    t[3]    = (uint8_t)y;            /* $C178 */
    t[4]    = (uint8_t)x;            /* $C184 */
    t[6]    = 4;                     /* $C190 */
    t[7]    = 4;                     /* $C19E */
    t[5]    = 0;                     /* $C1AC */
    t[0x0A] = 0;                     /* $C1B8 */
    t[2]    = 0;                     /* $C1C4 */

    c->xtxt[y][x] = (uint8_t)(slot + 0xC9); /* $C1D4 */
    c->boat_count++;                        /* $C1D8 */
    return 1;
}

/* ================================================================== *
 *  $C070  autoMarinaScan -- the four tiles around the marina.  Unlike
 *  the rail version this does NOT stop at the first success: it tries
 *  all four every time, so one marina can put out several boats in a
 *  cycle, up to the budget of four.
 * ================================================================== */
void auto_marina(City *c, int y, int x)
{
    static const int MDY[4] = {0, 1, 0, -1}; /* A5-0x620C */
    static const int MDX[4] = {-1, 0, 1, 0}; /* A5-0x6204 */
    int              i;

    if (c->boat_count >= 4)
        return; /* $C080 */

    for (i = 0; i < 4; i++) /* $C0F6 */
    {
        int ty = y + MDY[i]; /* $C096 */
        int tx = x + MDX[i]; /* $C0A2 */
        if (ty < 0 || ty >= MAP_H || tx < 0 || tx >= MAP_W)
            continue;
        if (!(c->xbit[ty][tx] & XBIT_WATER))
            continue; /* $C0CE */
        if (c->xtxt[ty][tx] != 0)
            continue;               /* $C0E2 */
        auto_marina_try(c, ty, tx); /* $C0EC */
    }
}

/* ================================================================== *
 *  $B058  autoRailStationScan -- try four tiles around the station.
 * ================================================================== */
int sim_auto_rail_station(City *c, int y, int x)
{
    int i;
    for (i = 0; i < 4; i++) /* $B0AE */
    {
        int ty = y + SCAN_DY[i]; /* $B076 */
        int tx = x + SCAN_DX[i]; /* $B082 */
        if (ty < 0 || ty >= MAP_H || tx < 0 || tx >= MAP_W)
            continue;
        if (auto_rail_station_try(c, ty, tx))
            return 1; /* $B0A6 */
    }
    return 0;
}

/* ================================================================== *
 *  $33844 -- lay a runway.
 *
 *  A runway is a strip of five tiles rather than a footprint, so it has
 *  its own path.  Which way the strip runs comes from the parity of how
 *  many runway tiles the city already has and of the tile's own
 *  coordinates, so successive runways alternate between across and
 *  down and a tile on an even row and an even column takes neither.
 *
 *  The walk happens twice.  The first pass only checks: five tiles that
 *  are on the map and in the right zone, and a tile that is already
 *  runway does not count toward the five, so an existing strip is
 *  extended rather than counted twice.  The second pass lays them.
 *
 *  XBIT bit 1 carries the tile's orientation, and $DD and $DE are the
 *  two halves of the sprite.  A tile already laid the wrong way round
 *  is turned into $DE rather than left alone.
 * ================================================================== */
static int place_runway(City *c, int y, int x, int zone)
{
    int ystep = 0, xstep = 0;
    int yy, xx, n, want;

    /*  $33846 -- the direction.  Odd runway count prefers down, even
     *  prefers across, and each falls back to the other. */
    if (c->census[0xDD] & 1)
    {
        if (x & 1)
            ystep = 1; /* $33860 */
        else if (y & 1)
            xstep = 1; /* $3386E */
        else
            return 0; /* $33872 */
    }
    else
    {
        if (y & 1)
            xstep = 1; /* $33882 */
        else if (x & 1)
            ystep = 1; /* $33890 */
        else
            return 0; /* $33894 */
    }

    /*  $3389A -- the checking pass */
    yy = y;
    xx = x;
    for (n = 0; n < 5;)
    {
        if (yy < 0 || yy >= MAP_H || xx < 0 || xx >= MAP_W)
            return 0; /* $338C0 */
        if ((c->xzon[yy][xx] & 0x0F) != zone)
            return 0; /* $338E2 */
        /*  $338FC -- a tile that is already runway is stepped over
         *  without counting, so the strip runs past it */
        if (c->xbld[yy][xx] != 0xDD && c->xbld[yy][xx] != 0xDE)
            n++;
        yy += ystep;
        xx += xstep;
    }

    /*  $3391C -- which of the two sprites this strip wants, from its
     *  direction and the map rotation */
    want = ((ystep != 0) != ((c->rotation & 1) != 0));

    /*  $3393E -- the laying pass */
    yy = y;
    xx = x;
    for (n = 0; n < 5;)
    {
        int b = c->xbld[yy][xx];

        if (b == 0xDD || b == 0xDE)
        {
            /*  $33970 -- already runway, so it does not count */
            if (b == 0xDD)
            {
                int have = (c->xbit[yy][xx] & 0x02) ? 1 : 0; /* $3398E */
                if (have != want)
                {
                    /*  $339A8 -- laid the wrong way round */
                    sim_set_tile(c, yy, xx, 0xDE);
                    c->xzon[yy][xx] = (uint8_t)((c->xzon[yy][xx] & 0x0F) | 0xF0);
                    if (zone != 7)
                        c->xbit[yy][xx] |= 0xC0;       /* $339E6 */
                    c->xbit[yy][xx] &= (uint8_t)~0x02; /* $339FA */
                }
            }
        }
        else
        {
            if (b >= 0x0D)
                clear_tile(c, yy, xx); /* $33A0C */
            sim_set_tile(c, yy, xx, 0xDD);
            c->xzon[yy][xx] = (uint8_t)((c->xzon[yy][xx] & 0x0F) | 0xF0);
            if (zone != 7)
                c->xbit[yy][xx] |= 0xC0; /* $33A5A */
            if (want)
                c->xbit[yy][xx] |= 0x02; /* $33A74 */
            n++;
        }
        yy += ystep;
        xx += xstep;
    }
    return 0xFF; /* $33EB6 */
}

static int place_crane(City *c, int y, int x, int zone)
{
    int d, yy, xx, n, want;

    /*  $33A94 -- which way is the water */
    for (d = 0; d < 4; d++)
    {
        int ny = y + CRANE_DY[d], nx = x + CRANE_DX[d];
        if (ny < 0 || ny >= MAP_H || nx < 0 || nx >= MAP_W)
            continue;
        if (c->xbit[ny][nx] & XBIT_WATER)
            break;
    }
    if (d == 4)
        return 0; /* $33AD4, nothing to reach into */

    /*  $33ADA -- a pier running across wants an even row, one running
     *  down an even column */
    if (CRANE_DX[d] != 0 && (y & 1))
        return 0;
    if (CRANE_DY[d] != 0 && (x & 1))
        return 0;

    /*  $33B12 -- four tiles of open water, and nothing already on them */
    yy = y;
    xx = x;
    for (n = 0; n < 5; n++)
    {
        yy += CRANE_DY[d];
        xx += CRANE_DX[d];
        if (yy < 0 || yy >= MAP_H || xx < 0 || xx >= MAP_W)
            return 0; /* $33B4C */
        if (!(c->xbit[yy][xx] & XBIT_WATER))
            return 0; /* $33B6A */
        if (c->xbld[yy][xx] != 0)
            return 0; /* $33B84 */
    }

    /*  $33B96 -- and the far end deep enough.  ALTM keeps the tile's
     *  own height in the low five bits and the water level in the next
     *  five, so this asks for two levels of clearance. */
    {
        uint16_t a  = c->altm[yy][xx];
        int      hi = (a >> 5) & 0x1F;
        int      lo = (a & 0x1F) + 2;
        if (lo > hi)
            return 0; /* $33BCA */
    }

    /*  $33BEA -- the crane goes on the anchor tile */
    if (c->xbld[y][x] >= 0x0D)
        clear_tile(c, y, x);           /* $33BF8 */
    stamp_footprint(c, y, x, 0xE0, 1); /* $33C0E */
    c->xzon[y][x] =
        (uint8_t)((c->xzon[y][x] & 0xF0) | (zone & 0x0F)); /* $33C3C */
    if (zone == 7)
        c->xbit[y][x] &= 0x0F; /* $33C60 */

    /*  $33C64 -- which of the two pier sprites, from the direction and
     *  the map rotation, exactly as the runway picks its own */
    want = ((CRANE_DY[d] != 0) != ((c->rotation & 1) != 0));

    /*  $33C96 -- and the four pier tiles */
    yy = y;
    xx = x;
    for (n = 0; n < 4; n++)
    {
        yy += CRANE_DY[d];
        xx += CRANE_DX[d];
        sim_set_tile(c, yy, xx, 0xDF); /* $33CC0 */
        c->xzon[yy][xx] = (uint8_t)((c->xzon[yy][xx] & 0x0F) | 0xF0);
        if (want)
            c->xbit[yy][xx] |= 0x02; /* $33CFC */
    }
    return 0xFF; /* $33EB6 */
}

int sim_place_special(City *c, int y, int x, int bld, int zone)
{
    /*  $333D0 -- everything but the military has to be on or beside a
     *  powered tile; a base makes its own arrangements. */
    if (zone != 7 && !near_powered(c, y, x))
        return 0; /* $333EA */

    /* ---- $3343C: one tile ---------------------------------------- */
    if ((bld >= 0xE1 && bld <= 0xE8) || bld == 0xEA)
    {
        if (c->xbld[y][x] >= 0x0D)
            return 0xFF;                                                   /* $3345A */
        stamp_footprint(c, y, x, bld, 1);                                  /* $3346E */
        c->xzon[y][x] = (uint8_t)((c->xzon[y][x] & 0xF0) | (zone & 0x0F)); /* $33498 */
        if (zone == 7)
            c->xbit[y][x] &= 0x0F; /* $334BE */
        return 0xFF;
    }

    /* ---- $334C6: two by two -------------------------------------- */
    if ((bld >= 0xEE && bld <= 0xF2) || bld == 0xF6)
    {
        static const int dy[4] = {0, 1, 0, 1};
        static const int dx[4] = {0, 0, 1, 1};
        int              i;

        y &= ~1; /* $334C6 */
        x &= ~1;

        /*  the first tile alone also rejects anything from $EB up */
        if (c->xbld[y][x] >= 0xEB)
            return 0;           /* $334F6 */
        for (i = 0; i < 4; i++) /* $334FE, $3352E, $33562, $33586 */
        {
            int b = c->xbld[y + dy[i]][x + dx[i]];
            if (b == 0xDD || b == 0xDE || b == 0xE0)
                return 0;
        }
        /*  and every tile of the square has to already be this zone */
        for (i = 0; i < 4; i++) /* $335B6, $335DC, $33606, $3362E */
            if ((c->xzon[y + dy[i]][x + dx[i]] & 0x0F) != zone)
                return 0;

        for (i = 0; i < 4; i++) /* $33658, $3368A, $336C0, $336F8 */
            if (c->xbld[y + dy[i]][x + dx[i]] >= 0x0D)
                clear_tile(c, y + dy[i], x + dx[i]);
        stamp_footprint(c, y, x, bld, 2); /* $33726 */

        for (i = 0; i < 4; i++) /* $33740, $33770, $3379E, $337C8 */
        {
            uint8_t *z = &c->xzon[y + dy[i]][x + dx[i]];
            *z         = (uint8_t)((*z & 0xF0) | (zone & 0x0F));
        }
        if (zone == 7) /* $337DA */
            for (i = 0; i < 4; i++)
                c->xbit[y + dy[i]][x + dx[i]] &= 0x0F;
        return 0xFF;
    }

    /* ---- $33D12: the military three by three --------------------- */
    if (bld == 0xF9)
    {
        int r, cc;

        /*  NOTE: at y == 0 or x == 0 these read one row/column off the
         *  edge.  The original does the same -- it indexes its row
         *  pointer table at -1 -- so the behaviour is left alone rather
         *  than guarded, which would diverge.  Unreachable in practice:
         *  the zone has to extend past the tile for the walk to move.
         *
         *  Walk up to two tiles up and two left for as long as the
         *  neighbour is still the same zone, so the 3x3 lands on the
         *  corner of the base rather than wherever the scan happened to
         *  be standing. */
        if ((c->xzon[y - 1][x] & 0x0F) == zone)
            y--; /* $33D38 */
        if ((c->xzon[y - 1][x] & 0x0F) == zone)
            y--; /* $33D58 */
        if ((c->xzon[y][x - 1] & 0x0F) == zone)
            x--; /* $33D7A */
        if ((c->xzon[y][x - 1] & 0x0F) == zone)
            x--; /* $33D9C */

        for (r = 0; r < 3; r++)        /* $33DE0 */
            for (cc = 0; cc < 3; cc++) /* $33DD8 */
            {
                sim_set_tile(c, y + r, x + cc, 0xF9); /* $33DB6 */
                set_under(c, y + r, x + cc, 0x22);    /* $33DCC */
            }

        {
            const int16_t *rot = &ROT_CORNER_4[c->rotation * 4];
            c->xzon[y][x]      = (uint8_t)((c->xzon[y][x] & 0x0F) | rot[0]); /* $33E0C */
            c->xzon[y + 2][x]  = (uint8_t)((c->xzon[y + 2][x] & 0x0F) | rot[1]);
            c->xzon[y + 2][x + 2] =
                (uint8_t)((c->xzon[y + 2][x + 2] & 0x0F) | rot[2]);
            c->xzon[y][x + 2] = (uint8_t)((c->xzon[y][x + 2] & 0x0F) | rot[3]);
        }
        return 0xFF;
    }

    /* ---- $33844: the runway, a five-tile strip ------------------- */
    if (bld == 0xDD)
        return place_runway(c, y, x, zone);

    /* ---- $33A90: the crane and its pier -------------------------- */
    if (bld == 0xE0)
        return place_crane(c, y, x, zone);
    return 0xFF;
}

/*  $332C6 -- may this tile join a building anchored nearby?  It has to
 *  be on the map, at the same altitude, in the same zone, carrying
 *  nothing bigger than `maxbld`, and be neither road nor rail. */
int tile_fits(const City *c, int y, int x, int alt, int zone, int maxbld)
{
    uint8_t  b;
    uint16_t m;

    if (y < 0 || y >= MAP_H || x < 0 || x >= MAP_W)
        return 0;
    if ((c->altm[y][x] & 0x1F) != alt)
        return 0; /* $3330A */
    if (XZON_TYPE(c->xzon[y][x]) != zone)
        return 0; /* $3332C */
    b = c->xbld[y][x];
    if (b >= maxbld)
        return 0; /* $3334C */
    if (b >= 0x70)
        return 1;
    m = BUILDING[b].dept;
    if (m & (uint16_t)(1u << DEPT_ROAD))
        return 0; /* $33356 */
    if (m & (uint16_t)(1u << DEPT_RAIL))
        return 0; /* $3338A */
    return 1;
}

void sim_place(City *c, int y, int x, int tier, int kind) /* placeBuilding $3258A */
{
    /*  How big the building is, measured by calling the routine for
     *  every (tier, kind) pair and seeing which tiles it wrote: it
     *  depends on the tier alone, and tiers 2 and 3 are both 2x2.
     *  The block hangs down and to the LEFT of the tile given. */
    static const int SIZE[4] = {1, 2, 2, 3};
    int              id, n, i, j;

    if (tier < 1 || tier > 4)
        return;

    if (tier == 1 && kind == 0)
    {
        /*  $325AA -- three bands of four, so a cell worth 192 or more
         *  always draws from the top band. */
        int band = c->xval[y / 2][x / 2] / 64;
        if (band > 2)
            band = 2;
        id = BLD_CHOICE_BASE[0] + band * 4 + (Random() & 3);
    }
    else
    { /* $325F8 and $3267C compute this identically */
        int cnt  = BLD_CHOICE_COUNT[kind * 4 + tier - 1];
        int base = BLD_CHOICE_BASE[kind * 4 + tier - 1];
        id       = base + (cnt ? (uint16_t)Random() % (uint16_t)cnt : 0);
    }

    n = SIZE[tier - 1];
    if (n == 1)
    { /* $3263C, a single tile keeps all four corner bits */
        sim_set_tile(c, y, x, (uint8_t)id);
        c->xzon[y][x] = (uint8_t)((c->xzon[y][x] & 0x0F) | 0xF0);
        c->xbit[y][x] |= 0xE0;
        return;
    }

    /*  $326C4 -- the edge test uses the HALF extent, not the full one.  The
     *  original carries a size of 1, 2, 3 or 4 by tier and halves it, which
     *  gives 0, 1, 1, 2 -- exactly n - 1 here.  Testing against n instead
     *  rejects a row and a column that the game accepts, so a building at
     *  row 125 or column 125 never appeared.  That is why every remaining
     *  difference sat at the map edge. */
    {
        const int half = n - 1;
        if (y < 2 || x < 2 || y > 0x7E - half || x > 0x7E - half)
            return;
    }

    for (i = 0; i < n; i++)
        for (j = 0; j < n; j++)
        {
            int ty = y + i, tx = x - n + 1 + j;
            sim_set_tile(c, ty, tx, (uint8_t)id);
            c->xzon[ty][tx] &= 0x0F;
            c->xbit[ty][tx] |= 0xE0;
        }

    /*  $3274C -- only the four corners of the block carry a corner bit,
     *  and which bit goes where turns with the map.  Order is top-left,
     *  bottom-left, bottom-right, top-right. */
    {
        static const int CY[4] = {0, 1, 1, 0};
        static const int CX[4] = {0, 0, 1, 1};
        for (i = 0; i < 4; i++)
        {
            uint8_t *z = &c->xzon[y + CY[i] * (n - 1)][x - n + 1 + CX[i] * (n - 1)];
            *z         = (uint8_t)((*z & 0x0F) | ROT_CORNER_4[(c->rotation & 3) * 4 + i]);
        }
    }
}

/*  $33028 -- grow the building at (y,x) into what its new tier needs.  The
 *  coin the caller tossed decides between two shapes at every tier above
 *  the first: one big building, or several small ones. */
void sim_upgrade(City *c, int y, int x, int tier, int coin)
{
    switch (tier)
    {
        case 1:
            sim_place(c, y, x, 1, 4); /* $33058 */
            break;

        case 2:
            if (!coin)
            {
                sim_place(c, y, x, 2, 4); /* $330C0, one 2x2 */
                break;
            }
            sim_place(c, y, x, 1, 4); /* $33070, four 1x1 */
            sim_place(c, y + 1, x, 1, 4);
            sim_place(c, y + 1, x - 1, 1, 4);
            sim_place(c, y, x - 1, 1, 4);
            break;

        case 3:
            if (coin)
                sim_place(c, y, x, 2, 4); /* $330D8 */
            else
                sim_place(c, y, x, 3, 4); /* $330EC */
            break;

        case 4:
            if (!coin)
            {
                sim_place(c, y, x, 4, 4); /* $331D2, one 3x3 */
                break;
            }
            {
                /*  $33106 -- eight small buildings round the edge of the
                 *  3x3, then one 2x2 dropped on a corner of it at
                 *  random. */
                static const int DY[8] = {0, 1, 2, 2, 2, 1, 0, 0};
                static const int DX[8] = {0, 0, 0, -1, -2, -2, -2, -1};
                int              k, r;
                for (k = 0; k < 8; k++)
                    sim_place(c, y + DY[k], x + DX[k], 1, 4);
                r = Random() & 3;                             /* $3319E */
                sim_place(c, y + (r & 1), x - (r / 2), 3, 4); /* $331A8 */
            }
            break;

        default:
            break;
    }
}

/*  $32830 -- a decaying residential block becomes a church when the city
 *  has fewer than one per 2500 people.  Two tiles by two, anchored one
 *  column left of the tile that decayed. */
void sim_build_church(City *c, int y, int x)
{
    int dy, dx, i = 0;

    if (y < 1 || x < 1 || y > 0x7E || x > 0x7E)
        return; /* $32840 */
    for (dy = 0; dy <= 1; dy++)
        for (dx = -1; dx <= 0; dx++)
        {
            sim_set_tile(c, y + dy, x + dx, BLD_CHURCH); /* $32872 */
            c->xzon[y + dy][x + dx] = 0;                 /* both nibbles */
            c->xbit[y + dy][x + dx] |= 0xE0;
        }
    /*  $328EC -- then one corner bit per sub-tile, chosen by the current
     *  rotation so the building is simulated exactly once.  The four writes
     *  are unrolled in the original and they do NOT run in raster order:
     *  $328EC takes the top-left, $32916 the bottom-left, $32938 the
     *  bottom-right and $3296E the top-right.  Every other footprint in the
     *  game goes round the block the same way, so walking this one in rows
     *  put 0x20 and 0x40 on the wrong diagonal -- which the renderer reads
     *  as a church facing the other way. */
    {
        static const int CY[4] = {0, 1, 1, 0};
        static const int CX[4] = {-1, -1, 0, 0};
        for (i = 0; i < 4; i++)
        {
            uint8_t *z = &c->xzon[y + CY[i]][x + CX[i]];
            *z         = (uint8_t)((*z & 0x0F) | ROT_CORNER_4[(c->rotation & 3) * 4 + i]);
        }
    }
}
