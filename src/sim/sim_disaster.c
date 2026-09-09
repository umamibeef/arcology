/*  sim_disaster.c -- the nineteen disasters, and the roll that picks one.
 *  Fire and how it spreads, the firestorm, the volcano, the hurricane, the
 *  meltdown, the earthquake, the two spills, the flood, the riot, the three
 *  things that fly, and the scenario check that watches for a win.  Split
 *  out of sim.c; the addresses in the comments still point into CODE 2, and
 *  the arithmetic is still the original's widths. */
#include "ext80.h"
#include "sim.h"
#include "sim_int.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ================================================================== *
 *  $38002  startFireNear -- spiral outward from the disaster point and
 *  set light to the first thing that will burn.
 *
 *  The spiral is the ordinary square one: step, and every second turn
 *  the leg gets longer.  It uses the same WALK_DY/WALK_DX the road walk
 *  uses.  A tile burns if it carries road or road-like infrastructure,
 *  is not water, and has nothing already on it.
 *
 *  A burning tile is marked in XTXT with $FD or $FE, chosen by a coin.
 *  Returns 1 when something caught, 0 when the spiral left the map
 *  without finding anything.
 * ================================================================== */
static int fire_will_take(int b) /* $38076 .. $380C8 */
{
    return (b >= 0x1D && b < 0x2C) || (b >= 0x3F && b < 0x47) || b == 0x4B ||
           b == 0x4C || (b >= 0x5D && b < 0x61);
}

int sim_start_fire_near(City *c)
{
    int h = c->disaster_h, v = c->disaster_v;
    int dir = 0, leg = 1, step = 0;

    while (leg < 128) /* $38176 */
    {
        h += WALK_DY[dir]; /* $38020, the same tables the road walk uses */
        v += WALK_DX[dir];

        if (h >= 0 && h < MAP_H && v >= 0 && v < MAP_W)
        {
            int b = c->xbld[h][v];
            if (fire_will_take(b) && !(c->xbit[h][v] & XBIT_WATER) &&
                c->xtxt[h][v] == 0) /* $380E0 */
            {
                c->xtxt[h][v] = (uint8_t)((Random() & 1) + 0xFD); /* $38118 */
                /*  $3813E scrolls the view to the fire when the player
                 *  asked to follow disasters, and $3F636 is the
                 *  memory-gated redraw.  Neither is simulation. */
                c->disaster_h = (int16_t)h; /* $38152 */
                c->disaster_v = (int16_t)v;
                return 1;
            }
        }
        /*  $3815A -- one step along this leg, and the leg grows every
         *  second turn, which is what makes it a spiral. */
        step++;
        if (step >= leg)
        {
            step = 0;
            if (dir & 1)
                leg++;
            dir = (dir + 1) & 3;
        }
    }
    return 0;
}

/* ================================================================== *
 *  $38290  disasterFire -- disaster type 1, the Disasters menu's first
 *  item.  Fire breaks out in a large building near where the player is
 *  looking; failing that, anywhere at all.
 *
 *  It starts within twenty tiles of the view centre, spirals out as far
 *  as leg sixty-four, and takes the first tile whose XBLD is $70 or
 *  more -- the big buildings.  If the spiral finds none it draws up to
 *  two hundred tiles anywhere on the map and burns the first one that
 *  will take.
 *
 *  The two searches do not use the same generator.  The spiral's start
 *  comes from the Toolbox _Random, the fallback from the game's own
 *  shift register.
 * ================================================================== */
int sim_disaster_fire(City *c)
{
    int h   = c->view_y + (int)((uint16_t)Random() % 40) - 20; /* $3829A */
    int v   = c->view_x + (int)((uint16_t)Random() % 40) - 20;
    int dir = 0, leg = 1, step = 0, i;

    while (leg < 0x40) /* $38370 */
    {
        h += WALK_DY[dir]; /* $382D6 */
        v += WALK_DX[dir];

        if (h >= 0 && h < MAP_H && v >= 0 && v < MAP_W &&
            c->xbld[h][v] >= 0x70 &&   /* $3831E, a large building */
            sim_burn_tile(c, h, v, 0)) /* $3832A */
        {
            c->disaster_h = (int16_t)h; /* $38350 SetPt */
            c->disaster_v = (int16_t)v;
            return 1;
        }
        step++; /* $38358 */
        if (step >= leg)
        {
            step = 0;
            if (dir & 1)
                leg++;
            dir = (dir + 1) & 3;
        }
    }

    for (i = 0; i < 200; i++) /* $383C4 */
    {
        h = (int)game_rand127(); /* $3837E */
        v = (int)game_rand127();
        if (sim_burn_tile(c, h, v, 0)) /* $38394 */
        {
            c->disaster_h = (int16_t)h; /* $383BA SetPt */
            c->disaster_v = (int16_t)v;
            return 1;
        }
    }
    return 0;
}

/* ================================================================== *
 *  $39B70  burnTile -- destroy what is on one tile.
 *
 *  `even_bare` says whether bare land counts.  The firestorm passes
 *  true, so it scorches everything in its path.
 *
 *  The XTXT byte carries the tile's state and decides what happens:
 *      0            nothing there yet, so light it
 *      < $33        a sign, removed by $EE3C
 *      < $C9        a microsim record, removed by $3A000
 *      < $F1        refuse
 *      < $FA        already burning, so advance the flame
 *      = $FA        burnt out: clear it and dock the right counter
 *  A tile that catches fire is marked $FF and its traffic is cleared.
 * ================================================================== */
int sim_burn_tile(City *c, int y, int x, int even_bare)
{
    int t;

    if (y < 0 || y >= MAP_H || x < 0 || x >= MAP_W)
        return 0; /* $39B98 */
    if (c->xbit[y][x] & XBIT_WATER)
        return 0; /* $39BB4 */
    if (c->xbld[y][x] < 6 && !even_bare)
        return 0; /* $39BD6 */

    t = c->xtxt[y][x];
    if (t != 0) /* $39BF6 */
    {
        if (t < 0x33)
        {
            release_label(c, t); /* $39C06, it was a sign */
        }
        else if (t < 0xC9)
        {
            /*  $39C24 -- a microsim record, so the building it belongs
             *  to comes down before the fire takes hold.  This is how
             *  the earthquake's burn branch demolishes: it does not
             *  call $3A000 itself. */
            sim_demolish_tile(c, y, x, 0xFF, 0xFF);
        }
        else if (t < 0xF1)
        {
            return 0; /* $39C36 */
        }
        else if (t < 0xFA)
        {
            /*  $39C3C -- already alight, so step the flame on. */
            sim_set_tile(c, y, x, (uint8_t)(game_rand(4) + 1));
            return 0;
        }
        else if (t == 0xFA)
        {
            /*  $39C64 -- burnt out.  Clear it, then dock whichever
             *  counter owned the thing that stood here. */
            int b         = c->xbld[y][x];
            c->xtxt[y][x] = 0;
            if ((b >= 0x1D && b < 0x2C) || (b >= 0x3F && b < 0x47) ||
                b == 0x4B || b == 0x4C || (b >= 0x5D && b < 0x61))
                c->burnt_road--; /* $39CF0, A5+0x2C96 */
            else
                c->burnt_other--; /* $39CF6, A5+0x2C94 */
        }
        else
        {
            return 0; /* $39CFC */
        }
    }

    c->xtxt[y][x]         = 0xFF; /* $39D00 */
    c->xtrf[y / 2][x / 2] = 0;    /* $39D2E, the traffic goes with it */
    return 1;
}

/* ================================================================== *
 *  $37C66  disasterFirestorm -- spiral out from the disaster point and
 *  burn up to sixty-five tiles.
 *
 *  The same square spiral the riot's fire search uses, but instead of
 *  stopping at the first thing that catches, it keeps going until the
 *  budget runs out or the spiral leaves the map.
 * ================================================================== */
int sim_disaster_firestorm(City *c)
{
    int h = c->disaster_h, v = c->disaster_v;
    int budget = 0x41, any = 0; /* $37C78, sixty-five tiles */
    int dir = 0, leg = 1, step = 0;

    while (leg < 128 && budget != 0) /* $37D00, $37D0A */
    {
        h += WALK_DY[dir]; /* $37C8C */
        v += WALK_DX[dir];

        if (h >= 0 && h < MAP_H && v >= 0 && v < MAP_W &&
            sim_burn_tile(c, h, v, 1)) /* $37CC2 */
        {
            budget--; /* $37CD0 */
            c->disaster_h = (int16_t)h;
            c->disaster_v = (int16_t)v;
            any           = 1;
        }
        step++;
        if (step >= leg)
        {
            step = 0;
            if (dir & 1)
                leg++;
            dir = (dir + 1) & 3;
        }
    }
    return any;
}

/* ================================================================== *
 *  $8758  canRaiseTile and $896C  raiseTile -- the pair that lifts one
 *  tile of land by a step, and everything around it that would be left
 *  hanging.
 *
 *  They work together through bit 3 of XBIT.  $8758 walks the land that
 *  would have to move, marks every tile it visits, and says whether the
 *  whole set may move.  $896C then walks the marks, clearing each one
 *  as it goes, and does the raising.  So a test always precedes a
 *  raise, and a raise consumes the test's marks.
 *
 *  A tile may not move if it is in a military zone, if it is already at
 *  the ceiling of thirty, or if any of its eight neighbours is
 *  military.  Otherwise each of the four orthogonal neighbours that
 *  sits lower has to be raisable too, which is where the recursion
 *  comes from.
 * ================================================================== */
int sim_can_raise(City *c, int y, int x)
{
    int alt, i;

    if (y < 0 || y >= MAP_H || x < 0 || x >= MAP_W)
        return 1; /* $8780, off the map is no obstacle */
    if (XZON_TYPE(c->xzon[y][x]) == ZONE_MILITARY)
        return 0; /* $879E */
    if (c->xbit[y][x] & XBIT_VISITED)
        return 1;                  /* $87C0, already counted */
    c->xbit[y][x] |= XBIT_VISITED; /* $87D6 */

    alt = c->altm[y][x] & 0x1F;
    if (alt >= 0x1E)
        return 0; /* $87F8 */

    for (i = 0; i < 8; i++) /* $885A, the eight neighbours */
    {
        const int ny = y + BEAM_DY[i]; /* A5-0x6568 */
        const int nx = x + BEAM_DX[i]; /* A5-0x6556 */
        if (ny < 0 || ny >= MAP_H || nx < 0 || nx >= MAP_W)
            continue;
        if (XZON_TYPE(c->xzon[ny][nx]) == ZONE_MILITARY)
            return 0; /* $8852 */
    }

    if (y > 0 && (c->altm[y - 1][x] & 0x1F) < alt &&
        !sim_can_raise(c, y - 1, x))
        return 0; /* $888C */
    if (x > 0 && (c->altm[y][x - 1] & 0x1F) < alt &&
        !sim_can_raise(c, y, x - 1))
        return 0; /* $88CC */
    if (y < 0x7F && (c->altm[y + 1][x] & 0x1F) < alt &&
        !sim_can_raise(c, y + 1, x))
        return 0; /* $890E */
    if (x < 0x7F && (c->altm[y][x + 1] & 0x1F) < alt &&
        !sim_can_raise(c, y, x + 1))
        return 0; /* $8950 */
    return 1;
}

void sim_raise_tile(City *c, int y, int x)
{
    int alt;

    if (y < 0 || y >= MAP_H || x < 0 || x >= MAP_W)
        return;
    if (!(c->xbit[y][x] & XBIT_VISITED))
        return;                     /* $89AA, only what the test marked */
    c->xbit[y][x] &= ~XBIT_VISITED; /* $89B2 */

    alt = c->altm[y][x] & 0x1F;
    if (alt >= 0x1E)
        return; /* $89CE */

    if (y > 0 && (c->altm[y - 1][x] & 0x1F) < alt)
        sim_raise_tile(c, y - 1, x); /* $89FC */
    if (x > 0 && (c->altm[y][x - 1] & 0x1F) < alt)
        sim_raise_tile(c, y, x - 1); /* $8A30 */
    if (y < 0x7F && (c->altm[y + 1][x] & 0x1F) < alt)
        sim_raise_tile(c, y + 1, x); /* $8A66 */
    if (x < 0x7F && (c->altm[y][x + 1] & 0x1F) < alt)
        sim_raise_tile(c, y, x + 1); /* $8A9E */

    /*  $8AA6 -- the neighbours go up whether or not this tile can be
     *  paid for.  Only the tile itself waits on the money. */
    if (c->raise_cost > c->funds)
        return;
    c->funds -= c->raise_cost;
    c->altm[y][x] = (uint16_t)((c->altm[y][x] & ~0x1F) | (alt + 1)); /* $8AD6 */
    c->xzon[y][x] = (uint8_t)(c->xzon[y][x] & 0xF0);                 /* $8AEE */
    sim_fix_neighbourhood(c, y, x);                                  /* $8AF4 */
}

/* ================================================================== *
 *  $37DD6  disasterVolcano -- disaster type 11.  Lava piles up around
 *  the disaster point and the ground rises under it.
 *
 *  The loop counter is the treasury.  It saves the player's money,
 *  writes 25,000 into the funds global, and runs until that is spent:
 *  a tile that will not rise costs 1,000, and every tile that does rise
 *  costs the ordinary 25 through $896C.  So the eruption is a thousand
 *  raises, or twenty-five refusals, or any mix.  The real balance goes
 *  back at the end.
 *
 *  Each turn marks one tile within two of the centre and one within
 *  sixteen: $FF on land, $FB on water.  A point far enough off the map
 *  that no offset lands on it would spin here for ever, since only a
 *  refused raise spends anything.  The menu never sets one.
 * ================================================================== */
int sim_disaster_volcano(City *c)
{
    const int     h     = c->disaster_h;
    const int     v     = c->disaster_v;
    const int32_t saved = c->funds; /* $37DFA */

    c->funds = 0x61A8;   /* $37DFE */
    while (c->funds > 0) /* $37F94 */
    {
        int ty, tx, by, bx;

        /*  the treasury is this loop's counter, so log it beside the
         *  dice while a comparison is running.  Inert otherwise. */
        rng_log_mark(c->funds);

        ty = h + (int)((uint16_t)Random() % 5) - 2; /* $37E18 */
        tx = v + (int)((uint16_t)Random() % 5) - 2;

        if (ty < 0 || ty >= MAP_H || tx < 0 || tx >= MAP_W)
            continue; /* $37E48, draw again without spending */

        c->xtxt[ty][tx] = (uint8_t)((Random() & 1) ? 0xFF : 0xFB); /* $37E6C */

        if (sim_can_raise(c, ty, tx))
            sim_raise_tile(c, ty, tx); /* $37EA2, and every fourth one
                                        *  calls $15408, which draws */
        else
            c->funds -= 1000; /* $37EBA */

        by = h + (int)((uint16_t)Random() % 32) - 16; /* $37ECE */
        bx = v + (int)((uint16_t)Random() % 32) - 16;
        if (by >= 0 && by < MAP_H && bx >= 0 && bx < MAP_W)
            c->xtxt[by][bx] =
                (uint8_t)((c->xbit[by][bx] & XBIT_WATER) ? 0xFB : 0xFF); /* $37F48 */

        /*  $37F70.  This draw only picks which of two sounds to ask for,
         *  and both are pure drawing -- but it takes a number from the same
         *  generator the tile choices come from, so leaving it out shifts
         *  every later decision.  It is not unconditional in the original:
         *  $37F64 calls $30FE, which hit-tests a point against the visible
         *  window, and tests the low byte of the result.  The game scrolls
         *  the view to a disaster before running it, so the erupting tile
         *  is on screen and the answer is yes -- which is why this is
         *  written straight through.  A volcano watched from the far side
         *  of the map would roll fewer times and erupt differently, and
         *  that is the original's behaviour too, not an approximation here.
         *  $30FE itself is a second entry into $30E4, past the `link` and
         *  past the load of $2C42 into the frame, so the y it passes to
         *  $8E60 comes off the caller's return address.  Five call sites do
         *  it.  It is a real quirk of the shipped binary, not a bad
         *  disassembly -- an earlier note here said otherwise and was
         *  wrong. */
        (void)Random();
    }
    c->funds = saved; /* $37FA4 */
    return 1;
}

/* ================================================================== *
 *  $3755A  disasterHurricane -- disaster type 16.
 *
 *  The wind blows along one axis, chosen by the view rotation, and the
 *  original writes all four directions out in full.  Each does two
 *  passes over the map.  The first picks twenty lines and, on each,
 *  jumps inward by up to twenty tiles at a time until it meets a
 *  building, which it demolishes.  The second picks fifty or a hundred
 *  lines and walks inward one tile at a time until it meets anything
 *  built, which it floods.
 *
 *  The four differ in more than the axis: two hand $3A000 a set flag
 *  and two a clear one, the second pass runs fifty times for the two
 *  that blow from the far edge and a hundred for the other two, and
 *  the case that scans columns from the left counts a demolition twice
 *  against its own budget, so it does fewer of them.
 *
 *  A jump of zero is possible, so the first pass can stall on a line
 *  with nothing on it.  The shift register does not stay at zero, so it
 *  always gets going again.
 * ================================================================== */
int sim_disaster_hurricane(City *c)
{
    /*  axis 0 walks the column and keeps the row; axis 1 the reverse */
    static const struct
    {
        int axis, step, flag_c, wet;
    } W[4] = {
        {0, -1, 0xFF, 50 },
        {1, -1, 0,    100},
        {0, 1,  0,    100},
        {1, 1,  0xFF, 50 }
    };
    const int dir  = (c->rotation + 1) & 3;
    const int axis = W[dir].axis, step = W[dir].step;
    int       done, p, fixed;

    c->flood_timer     = 0x3C; /* $37564 */
    c->hurricane_timer = 0x32; /* $3756A */

#define BLD_AT(P) (axis ? c->xbld[P][fixed] : c->xbld[fixed][P])

    for (done = 0; done < 0x14;) /* $37600 and its three copies */
    {
        done++;
        fixed = (int)game_rand127(); /* $375A2 */
        p     = (step > 0) ? 0 : 0x7F;

        if (step > 0)
            while (p <= 0x7F) /* $375DE */
            {
                if (p < 0x7F && BLD_AT(p) >= 0x0D)
                    break;               /* $375CA */
                p += (int)game_rand(20); /* $375D4 */
            }
        else
            while (p >= 0) /* $3769C */
            {
                if (p > 0 && BLD_AT(p) >= 0x0D)
                    break;
                p -= (int)game_rand(20);
            }

        if (step > 0 ? (p >= 0x7F) : (p <= 0))
            continue; /* $375E6 */

        if (axis)
            sim_demolish_tile(c, p, fixed, W[dir].flag_c, 0); /* $375F6 */
        else
            sim_demolish_tile(c, fixed, p, W[dir].flag_c, 0);
        if (dir == 2)
            done++; /* $375FC, this one counts twice */
    }

    for (done = 0; done < W[dir].wet; done++) /* $37658 and its copies */
    {
        fixed = (int)game_rand127(); /* $3760C */
        p     = (step > 0) ? 0 : 0x7F;

        if (step > 0)
            while (p < 0x7F && BLD_AT(p) < 6)
                p++; /* $37618 */
        else
            while (p >= 0 && BLD_AT(p) < 6)
                p--;

        if (step > 0 ? (p >= 0x7F) : (p <= 0))
            continue; /* $3763C */

        if (axis)
            c->xtxt[p][fixed] = 0xFC; /* $37652 */
        else
            c->xtxt[fixed][p] = 0xFC;
    }
#undef BLD_AT
    return 1;
}

/* ================================================================== *
 *  $38916  disasterMeltdown -- disaster type 9.
 *
 *  It needs a nuclear plant ($CB).  If the disaster point is not on
 *  one it walks the whole map for the first, and gives up when there is
 *  none.  The plant itself comes down, then fallout is scattered over a
 *  65 by 65 box, and finally a four by four patch of radioactive ground
 *  is left where the plant stood.
 * ================================================================== */
int sim_disaster_meltdown(City *c)
{
    int y = c->disaster_h;
    int x = c->disaster_v;
    int dy, dx;

    if (c->xbld[y][x] != 0xCB) /* $38944 */
    {
        for (y = 0; y < MAP_H; y++) /* $38992 */
        {
            for (x = 0; x < MAP_W; x++) /* $3897A */
                if (c->xbld[y][x] == 0xCB)
                    break;
            if (x < MAP_W)
                break; /* $38984 */
        }
        if (y == 128 && x == 128)
            return 0; /* $3899C, no plant anywhere */
    }

    sim_footprint_origin(c, &y, &x, 0xCB); /* $389D4 */
    y++;                                   /* $389DA */
    x--;
    sim_demolish_tile(c, y, x, 0xFF, 0xFF); /* $389F2 */

    for (dy = -0x20; dy <= 0x20; dy++)     /* $38B10 */
        for (dx = -0x20; dx <= 0x20; dx++) /* $38B04 */
        {
            const int ty = y + dy, tx = x + dx;

            if ((Random() & 0x1F) != 0)
                continue; /* $38A0A */
            if (ty < 0 || ty >= MAP_H || tx < 0 || tx >= MAP_W)
                continue;

            if (Random() & 3) /* $38A54 */
            {
                sim_demolish_tile(c, ty, tx, 0, 0); /* $38A74 */
                if (!(Random() & 1))
                    continue; /* $38A7A */
                if (c->xbit[ty][tx] & XBIT_WATER)
                    c->xtxt[ty][tx] = 0xFB; /* $38AC0 */
                else
                    sim_set_tile(c, ty, tx, 5); /* $38ADC, radioactive */
            }
            else
                sim_burn_tile(c, ty, tx, 0xFF); /* $38AFA */
        }

    for (dy = -1; dy <= 2; dy++)     /* $38B54 */
        for (dx = -2; dx <= 1; dx++) /* $38B4C */
            if (Random() & 1)
                sim_set_tile(c, y + dy, x + dx, 5); /* $38B42 */
    return 1;
}

/* ================================================================== *
 *  $383D4  disasterEarthquake -- disaster type 6.
 *
 *  Half of this routine shakes the screen: it copies the city bitmap
 *  back and forth twenty-four times with a tick of delay between, and
 *  changes nothing.  The damage is the loop below.
 *
 *  It sweeps a 65 by 65 box around the disaster point and gives every
 *  cell of it a one in 64 chance, drawing for the whole box whether or
 *  not the cell is on the map.  That keeps the dice independent of
 *  where the earthquake struck.  A cell that comes up is demolished
 *  three times in four and set alight the fourth time.
 * ================================================================== */
int sim_disaster_earthquake(City *c)
{
    const int h = c->disaster_h;
    const int v = c->disaster_v;
    int       dy, dx;

    for (dy = -0x20; dy <= 0x20; dy++)     /* $38560 */
        for (dx = -0x20; dx <= 0x20; dx++) /* $38556 */
        {
            int ty, tx;

            if ((Random() & 0x3F) != 0)
                continue; /* $384B6 */

            ty = h + dy;
            tx = v + dx;
            if (ty < 0 || ty >= MAP_H || tx < 0 || tx >= MAP_W)
                continue; /* $384C2 */
            if (c->xbld[ty][tx] < 0x0E)
                continue; /* $3850A */

            if (Random() & 3)
                sim_demolish_tile(c, ty, tx, 0, 0); /* $38530 */
            else
                sim_burn_tile(c, ty, tx, 0); /* $3854C */
        }
    return 1;
}

/* ================================================================== *
 *  $37FB6  disasterPollution -- disaster type 4.  It puts one chemical
 *  marker on the disaster point and nothing more.
 * ================================================================== */
int sim_disaster_pollution(City *c)
{
    c->xtxt[c->disaster_h][c->disaster_v] = 0xFB; /* $37FE6 */
    return 1;
}

/* ================================================================== *
 *  $37888  disasterChemicalSpill -- disaster type 15.  The same shape
 *  as the riot, but the box is only eight tiles wide and the tiles get
 *  the chemical marker $FB instead of a fire.
 * ================================================================== */
int sim_disaster_chemical(City *c)
{
    int h0    = c->disaster_h;
    int v0    = c->disaster_v;
    int tries = (int)((uint32_t)c->population / 10000u) + 5; /* $3789C */
    int i, any = 0;

    for (i = 0; i < tries; i++) /* $37918 */
    {
        int ty = h0 + ((uint16_t)Random() & 7) - 4; /* $378B8 */
        int tx = v0 + ((uint16_t)Random() & 7) - 4;

        if (ty < 0 || ty >= MAP_H || tx < 0 || tx >= MAP_W)
            continue;
        c->xtxt[ty][tx] = 0xFB; /* $37902 */
        any             = 1;
        c->disaster_h   = (int16_t)ty; /* $37914 SetPt */
        c->disaster_v   = (int16_t)tx;
    }
    return any;
}

/* ================================================================== *
 *  $379FC  floodSpread -- find the nearest shoreline tile and let the
 *  water onto the land around it.
 *
 *  It scans a box that grows a ring at a time until it finds terrain in
 *  the shoreline range $20..$2F, floods up to four neighbours of that
 *  one tile, and returns.  It does NOT keep going, so one call wets a
 *  small patch.  The flood marker is $FC in XTXT.
 *
 *  Two quirks, both transcribed as written:
 *
 *  The last two cases test the neighbour on ONE side and flood the
 *  neighbour on the OPPOSITE side.  $37B0E reads XBIT at row - 1 and
 *  $37B2E writes XTXT at row + 1; $37B54 and $37B6C do the same left
 *  for right.  The first two cases test and write the same tile, so
 *  this looks like two lines that were copied and only half edited.
 *
 *  And their guards compare the ring OFFSET against 127, not the
 *  resulting coordinate, so neither the row nor the column is really
 *  bounded before the write.
 * ================================================================== */
int sim_flood_spread(City *c)
{
    int h = c->disaster_h, v = c->disaster_v;
    int ring, dy, dx;

    for (ring = 0; ring < 128; ring++) /* $37BD0 */
    {
        for (dy = -ring; dy <= ring; dy++) /* $37BC8 */
        {
            for (dx = -ring; dx <= ring; dx++) /* $37BBE */
            {
                int ty = h + dy, tx = v + dx, t;

                if (ty < 0 || ty >= MAP_H || tx < 0 || tx >= MAP_W)
                    continue;
                t = c->xter[ty][tx];
                if (t < 0x20 || t >= 0x30)
                    continue; /* $37A9E, shoreline only */

                if (dy > 0 && !(c->xbit[ty - 1][tx] & XBIT_WATER))
                    c->xtxt[ty - 1][tx] = 0xFC; /* $37AB4 */
                if (dx > 0 && !(c->xbit[ty][tx - 1] & XBIT_WATER))
                    c->xtxt[ty][tx - 1] = 0xFC; /* $37AEE */
                if (dy < 0x7F && !(c->xbit[ty - 1][tx] & XBIT_WATER))
                    c->xtxt[ty + 1][tx] = 0xFC; /* $37B32, see above */
                if (dx < 0x7F && !(c->xbit[ty][tx - 1] & XBIT_WATER))
                    c->xtxt[ty][tx + 1] = 0xFC; /* $37B70, see above */

                c->flood_timer = 0x3C;        /* $37B8A */
                c->disaster_h  = (int16_t)ty; /* $37B9E */
                c->disaster_v  = (int16_t)tx;
                return 1;
            }
        }
    }
    return 0;
}

/* ================================================================== *
 *  $37940  disasterFlood -- the same shape as the riot.  It picks
 *  population/10000 + 5 spots within sixteen tiles of the disaster
 *  point and lets the water in at each.
 * ================================================================== */
int sim_disaster_flood(City *c)
{
    int h0    = c->disaster_h;
    int v0    = c->disaster_v;
    int tries = (int)((uint32_t)c->population / 10000u) + 5; /* $37956 */
    int i, any = 0;

    for (i = 0; i < tries; i++) /* $379EA */
    {
        int ty = h0 + ((uint16_t)Random() % 32) - 16;
        int tx = v0 + ((uint16_t)Random() % 32) - 16;
        /*  unlike the riot, this one checks the point is on the map
         *  before it uses it ($379B4) */
        if (ty < 0 || ty >= MAP_H || tx < 0 || tx >= MAP_W)
            continue;
        c->disaster_h = (int16_t)ty;
        c->disaster_v = (int16_t)tx;
        if (sim_flood_spread(c))
            any = 1; /* $379DC */
    }
    return any;
}

/* ================================================================== *
 *  $37D34  disasterRiot -- a riot is simply several fires.
 *
 *  It picks population/10000 + 5 spots, each within sixteen tiles of
 *  the disaster point, and tries to start a fire at every one.  So a
 *  bigger city riots harder.
 * ================================================================== */
int sim_disaster_riot(City *c)
{
    int h0    = c->disaster_h; /* $37D38 */
    int v0    = c->disaster_v;
    int tries = (int)((uint32_t)c->population / 10000u) + 5; /* $37D4C */
    int i, any = 0;

    for (i = 0; i < tries; i++) /* $37DC6 */
    {
        /*  smod32 by 32 then less 16, so the offset runs -16 .. +15 */
        c->disaster_h = (int16_t)(h0 + ((uint16_t)Random() % 32) - 16);
        c->disaster_v = (int16_t)(v0 + ((uint16_t)Random() % 32) - 16);
        if (sim_start_fire_near(c))
            any = 1; /* $37DB8 */
    }
    return any;
}

/* ================================================================== *
 *  $0221A8  scenarioCheck -- has the player won the scenario yet?
 *
 *  A flag starts at true and every unmet goal clears it.  Two details
 *  decide the whole thing:
 *
 *  A limit of ZERO means NO limit, not "must be zero".  Every shipped
 *  scenario leaves the pollution, crime and traffic limits at zero, so
 *  without that test none of them could ever be won.
 *
 *  Losing is not the opposite of winning.  Meeting every goal wins at
 *  once.  Failing only spends a month off the counter, and the loss
 *  comes when that counter reaches zero.
 * ================================================================== */
int sim_scenario_check(City *c)
{
    int won = 1;
    int item, tiles;

    if (!(uint8_t)c->misc[MISC_SCEN_ACTIVE])
        return 0; /* $2219C */

    if ((uint32_t)c->misc[MISC_GOAL_POP] > (uint32_t)c->population)
        won = 0; /* $221AC */
    if (c->misc[MISC_GOAL_RES] > c->dept[0].amount)
        won = 0; /* $221BC */
    if (c->misc[MISC_GOAL_COM] > c->dept[1].amount)
        won = 0; /* $221CC */
    if (c->misc[MISC_GOAL_IND] > c->dept[2].amount)
        won = 0; /* $221DC */
    if (c->funds - c->bonds < c->misc[MISC_GOAL_CASH])
        won = 0; /* $221EC */
    if ((uint32_t)c->misc[MISC_GOAL_LANDVAL] > (uint32_t)c->land_value_tot)
        won = 0; /* $221F8 */
    if ((uint32_t)(int16_t)c->misc[MISC_GOAL_LIFE] >
        (uint32_t)c->misc[MISC_AGE_W65])
        won = 0; /* $22204 */
    if ((uint32_t)(int16_t)c->misc[MISC_GOAL_EDU] >
        (uint32_t)c->misc[MISC_AGE_W90])
        won = 0; /* $22210 */

    /*  the three limits, each skipped when it is zero or less */
    if (c->misc[MISC_LIMIT_PLT] > 0 &&
        (uint32_t)c->misc[MISC_LIMIT_PLT] < (uint32_t)c->pollution_tot)
        won = 0; /* $22218 */
    if (c->misc[MISC_LIMIT_CRM] > 0 &&
        (uint32_t)c->misc[MISC_LIMIT_CRM] < (uint32_t)c->crime_tot)
        won = 0; /* $2222A */
    if (c->misc[MISC_LIMIT_TRF] > 0 &&
        (uint32_t)c->misc[MISC_LIMIT_TRF] < (uint32_t)c->traffic_tot)
        won = 0; /* $2223C */

    /*  and up to two "build N of these" requirements */
    item  = (uint8_t)c->misc[MISC_BUILD_ONE];
    tiles = (int16_t)c->misc[MISC_TILES_ONE];
    if (item > 0 && c->census[item] < (uint16_t)tiles)
        won = 0; /* $2224E */
    item  = (uint8_t)c->misc[MISC_BUILD_TWO];
    tiles = (int16_t)c->misc[MISC_TILES_TWO];
    if (item > 0 && c->census[item] < (uint16_t)tiles)
        won = 0; /* $2226A */

    if (won)
        return 1; /* $2229C, $230E6(1) */

    c->misc[MISC_SCEN_MONTHS] = (int16_t)(c->misc[MISC_SCEN_MONTHS] - 1);
    if (c->misc[MISC_SCEN_MONTHS] == 0)
        return -1; /* $2228A, $230E6(0) */
    return 0;
}

/* ================================================================== *
 *  $38186  disasterAirCrash -- disaster type 18, the Disasters menu's
 *  third item.  It looks for a free tile in the middle sixty-four of
 *  the map and puts an aircraft on it, marked to come down: the record
 *  is kind 1 like any other aeroplane, but field 5 holds $10 and field
 *  2 holds 7, which no ordinary flight sets.
 *
 *  The search has no attempt limit.  It draws pairs until one lands on
 *  a tile whose XTXT is zero, so a city whose centre is completely
 *  covered would spin here.  In practice XTXT is empty almost
 *  everywhere.
 * ================================================================== */
int sim_disaster_air_crash(City *c)
{
    uint8_t *t;
    int      y, x, slot;

    do /* $38192 */
    {
        y = (int)game_rand63() + 0x20;
        x = (int)game_rand63() + 0x20;
    } while (c->xtxt[y][x] != 0);

    slot = alloc_thing(c); /* $381C6 */
    if (slot == 0)
        return 0;

    c->xtxt[y][x] = (uint8_t)(slot + 0xC9); /* $381F0 */

    t       = thing(c, slot);
    t[0]    = 1; /* $381FC, an aircraft */
    t[3]    = (uint8_t)y;
    t[4]    = (uint8_t)x;
    t[6]    = 8;
    t[7]    = 8;
    t[5]    = 0x10; /* $3823E, the mark that says it will crash */
    t[0x0A] = 0;
    t[2]    = 7;

    c->disaster_h = (int16_t)y; /* $3826E SetPt */
    c->disaster_v = (int16_t)x;
    return 1;
}

int sim_disaster_tornado(City *c)
{
    if (c->tornado_count >= 1)
        return 0; /* $3876E */
    return spawn_disaster_thing(c, 0x0F);
}

int sim_disaster_monster(City *c)
{
    if (c->monster_count >= 1)
        return 0; /* $3857C */
    return spawn_disaster_thing(c, 0x05);
}

/* ================================================================== *
 *  $38B6C  disasterMicrowave -- disaster type 10.  The microwave
 *  receiver's beam wanders off its dish and scorches what it crosses.
 *
 *  It finds the first tile carrying building $CD, then walks up to
 *  forty steps, each in a fresh random compass direction, burning
 *  everything it touches.  A water tile also gets the chemical marker.
 *
 *  The search for the dish reads one column past the end of each row:
 *  when the inner loop finishes without a match it leaves the column
 *  at 128 and the outer test reads there anyway ($38BB0).  Rows sit
 *  next to each other, so that read lands on the first tile of the row
 *  below.  Transcribed as written.
 * ================================================================== */
int sim_disaster_microwave(City *c)
{
    const uint8_t *bld = (const uint8_t *)c->xbld;
    int            y = 0, x = 0, dir, left;

    for (; y < MAP_W; y++) /* $38BBC */
    {
        for (x = 0; x < MAP_W; x++) /* $38B98 */
            if (bld[y * MAP_W + x] == 0xCD)
                break;
        if (bld[y * MAP_W + x] == 0xCD) /* $38BB0, x may be 128 here */
            break;
    }
    if (y == 128 && x == 128)
        return 0; /* $38BC4 */

    dir  = (int)(Random() & 7); /* $38BEA */
    left = 40;

    for (;;)
    {
        left--; /* $38C9A */
        if (left <= 0)
            break;
        if (y < 0 || y >= MAP_H || x < 0 || x >= MAP_W)
            break;

        if (bld[y * MAP_W + x] != 0xCD) /* $38C0E, spare the dish itself */
        {
            if (c->xbit[y][x] & XBIT_WATER)
                c->xtxt[y][x] = 0xFB;  /* $38C3A */
            sim_burn_tile(c, y, x, 1); /* $38C68 */
        }
        y += BEAM_DY[dir]; /* $38C7A */
        x += BEAM_DX[dir];
        dir = (int)(Random() & 7); /* $38C90 */
    }
    return 1;
}

/* ================================================================== *
 *  $310B0 -- what starts a disaster.
 *
 *  Phase 24 runs this after the newspaper has picked its story.  Three
 *  things gate it: a flag at A5+0x13AA turns disasters off entirely, a
 *  city younger than DISASTER_ODDS[difficulty] months is spared, and
 *  then it is a one-in-that-many chance.  A hard game rolls one in
 *  thirty and the easiest one in a hundred, so the difficulty setting
 *  is the same number in all three places.
 *
 *  Two kinds jump the queue on the weather alone.  Otherwise a second
 *  roll picks one of nineteen, five of which do nothing, and each of
 *  the rest asks the city whether it is the sort of place that can have
 *  such a thing: a heatwave needs heat, a riot needs unemployment and
 *  heat together, smog looks for the dirtiest quarter on the map.
 *
 *  This only chooses.  Firing the chosen one is the dispatch's job.
 * ================================================================== */
static void disaster_at(City *c, int v, int h)
{
    c->disaster_v = (int16_t)v; /* $13A2, the Mac Point's v */
    c->disaster_h = (int16_t)h; /* $13A4 */
}

/*  the point most arms use: anywhere but the outermost ring */
static void disaster_anywhere(City *c)
{
    int v = (int)((uint16_t)Random() % 126) + 1;
    int h = (int)((uint16_t)Random() % 126) + 1;
    disaster_at(c, v, h);
}

/*  and the one the arms that start downtown use */
static void disaster_near_centre(City *c, int bias)
{
    int v = (int)((uint16_t)Random() & 0x1F) + c->centre_y + bias;
    int h = (int)((uint16_t)Random() & 0x1F) + c->centre_x + bias;
    disaster_at(c, v, h);
}

void sim_disaster_roll(City *c)
{
    const int lvl  = (c->difficulty >= 0 && c->difficulty < 4) ? c->difficulty : 0;
    const int odds = DISASTER_ODDS[lvl];
    int       d3, kind;

    if (c->disasters_off)
        return; /* $310A6, A5+0x13AA */
    if (odds == 0 || c->date / 25 < odds)
        return; /* $310CA, too young a city */

    d3 = (int)((uint16_t)Random() % (uint16_t)odds); /* $310E2 */

    /*  $310F2 -- two the weather brings on by itself.  These set the
     *  kind directly rather than going through the table. */
    if (c->weather_state == 0x0A && c->misc[1041] && d3 < 15)
    {
        c->disaster_kind = 0x10; /* $31106 */
        return;
    }
    if (c->weather_state == 0x0B && d3 < 15)
    {
        c->disaster_kind = 7; /* $3111E */
        disaster_anywhere(c); /* $3112A, $31142 */
        return;
    }

    if (d3 != 0)
        return; /* $3115E */

    kind = (int)((uint16_t)Random() % 19); /* $31166 */

    switch (kind)
    {
        case 14: /* $3120E, then into 2 */
            if (c->weather_state < 9)
                return;
            /* fall through */
        case 2: /* $3121C */
            if (!c->misc[1042] && !c->misc[1041])
                return;
            if (c->weather_state < 3)
                return;
            disaster_anywhere(c);
            break;

        case 1: /* $311B4 -- only when it is hot enough */
            if (((int)((uint16_t)Random() & 0x7F) + 0x7F) > c->temperature)
                return;
            disaster_anywhere(c);
            break;

        case 13: /* $31272, then into 3 */
            if (c->population < 0x7530)
                return;
            /* fall through */
        case 3: /* $3127E -- out of work and too hot */
            if (c->unemployment < 10)
                return;
            if (c->temperature < 0xAA)
                return;
            disaster_near_centre(c, -16); /* $312A4, $312CA */
            break;

        case 4: /* $312EC -- over the dirtiest quarter on the map */
            {
                int best = 0, by = 0, bx = 0, hy, hx;
                c->disaster_h = -1; /* $312EC */
                for (hy = 0; hy < HALF_H; hy++)
                    for (hx = 0; hx < HALF_W; hx++)
                    {
                        int v = c->xplt[hy][hx];
                        if (v < 0x96)
                            continue; /* $31314 */
                        if (best >= v)
                            continue; /* $3131A */
                        if (game_rand(10) != 0)
                            continue; /* $31324, one in ten */
                        best = c->xplt[hy][hx];
                        by   = hy * 2 + (int)game_rand(10) - 5; /* $3133C */
                        bx   = hx * 2 + (int)game_rand(10) - 5; /* $3134E */
                    }
                if (by < 0 || by >= MAP_H || bx < 0 || bx >= MAP_W)
                    return; /* $3137C */
                if (best == 0)
                    return; /* $31398 */
                disaster_at(c, by, bx);
                break;
            }

        case 6: /* $31434 -- anywhere, no questions asked */
            disaster_anywhere(c);
            break;

        case 7: /* $3146E */
            if (c->weather_state < 8)
                return;
            disaster_anywhere(c);
            break;

        case 8: /* $314B4 -- a big city only */
            if (c->population < 0xAFC8)
                return;
            disaster_near_centre(c, -15); /* $314C4, $314D8 */
            break;

        case 9: /* $314EE -- only if there is one to go wrong */
            if (c->census[0xCB] == 0)
                return;
            break;

        case 10: /* $314FA */
            if (c->census[0xCD] == 0)
                return;
            break;

        case 15: /* $313AC */
            if (c->dept[2].history_amount[0] < 0x2710)
                return;
            disaster_near_centre(c, -15); /* $313C2, $313D6 */
            break;

        case 16: /* $31506 */
            if (c->weather_state < 8 || !c->misc[1041])
                return;
            break;

        case 18: /* $313EE -- needs a runway to fall out of the sky */
            if (c->census[0xDD] == 0)
                return;
            disaster_anywhere(c);
            break;

        default: /* $3151A -- 0, 5, 11, 12 and 17 do nothing */
            return;
    }

    c->disaster_kind = (int16_t)kind; /* $3151C */
}
