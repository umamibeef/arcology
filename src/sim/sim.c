/*  sim.c -- SimCity 2000's simulation, reconstructed from CODE 2.
 *
 *  Layout follows the original's phase structure rather than any tidier
 *  arrangement, so each function can be read next to the listing it came
 *  from.  Arithmetic is deliberately kept in the original's widths: the
 *  game runs on 16-bit registers in many places and the truncation is
 *  observable in the results.
 */
#include "ext80.h"
#include "sc2k.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* THINK C's __sdiv32 ($524) truncates toward zero, which is also what
 * C99 '/' does, so plain division is faithful here.  The 68000 asr used
 * for power-of-two division is NOT the same thing -- it floors -- so
 * anywhere the original used asr we shift, and anywhere it called the
 * helper we divide. */
#define ASR(v, n) ((int32_t)(v) >> (n)) /* arithmetic, floors */

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
static void release_label(City *c, int v); /* $EE3C, defined below */

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

/*  The eight compass steps: A5-0x1102 / A5-0x10F2 for the microwave
 *  beam, and the same values again at A5-0x6568 / A5-0x6556 for the
 *  neighbour sweep in $8758. */
static const int BEAM_DY[8] = {0, 1, 1, 1, 0, -1, -1, -1};
static const int BEAM_DX[8] = {-1, -1, 0, 1, 1, 1, 0, -1};

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

        /*  $37F70.  This draw only picks which of two sounds to ask
         *  for, and both are pure drawing -- but it takes a number from
         *  the same generator the tile choices come from, so leaving it
         *  out shifts every later decision.
         *
         *  It is not unconditional in the original: $37F64 calls $30FE,
         *  which hit-tests a point against the visible window, and tests
         *  the low byte of the result.  The game scrolls the view to a
         *  disaster before running it, so the erupting tile is on screen
         *  and the answer is yes -- which is why this is written
         *  straight through.  A volcano watched from the far side of the
         *  map would roll fewer times and erupt differently, and that is
         *  the original's behaviour too, not an approximation here.
         *
         *  $30FE itself is a second entry into $30E4, past the `link`
         *  and past the load of $2C42 into the frame, so the y it passes
         *  to $8E60 comes off the caller's return address.  Five call
         *  sites do it.  It is a real quirk of the shipped binary, not a
         *  bad disassembly -- an earlier note here said otherwise and
         *  was wrong. */
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

/*  $41B8 and $42AC -- which infra[] counter a military building id
 *  belongs to.  The two switches in $4110 share this one table. */
static int infra_slot(int bld)
{
    /*        $DD $DE $DF $E0 $E1 $E2 $E3 $E4 $E5 $E6 $E7 $E8 $E9 $EA $EB */
    static const uint8_t SLOT[0x1D] = {
        1, 2, 0, 10, 0, 11, 6, 7, 8, 0, 12, 13, 0, 5, 0,
        /* $EC $ED $EE $EF $F0 $F1 $F2 $F3 $F4 $F5 $F6 $F7 $F8 $F9 */
        0,
        0,
        0,
        3,
        0,
        9,
        4,
        0,
        0,
        0,
        14,
        0,
        0,
        15};
    if (bld < 0xDD || bld > 0xF9)
        return 0; /* $41AA */
    return SLOT[bld - 0xDD];
}

/* ================================================================== *
 *  $4110  setTile -- the routine 168 call sites funnel through.
 *
 *  Besides writing XBLD it keeps a running census of every building id
 *  on the map.  That census is what lets the growth pass ask questions
 *  like "how many churches are there" without walking the map.
 * ================================================================== */
void sim_set_tile(City *c, int y, int x, uint8_t bld)
{
    uint8_t old;

    if (y < 0 || y >= MAP_H || x < 0 || x >= MAP_W)
        return; /* $4124 */

    if (XZON_TYPE(c->xzon[y][x]) == ZONE_MILITARY)
    {
        /*  $418C -- a military tile keeps its own tally instead.  Two
         *  identical 29-case switches over ids $DD..$F9 pick a slot in
         *  infra[]: the old building's slot goes down, the new one's
         *  goes up.  Anything outside that range, and every id in it
         *  the table does not name, lands on slot 0. */
        c->infra[infra_slot(c->xbld[y][x])]--; /* $41B8 */
        c->infra[infra_slot(bld)]++;           /* $42AC */
        c->xbld[y][x] = bld;                   /* $4396 */
        return;
    }

    old = c->xbld[y][x]; /* $4166 */
    c->census[old]--;    /* $4174 */
    c->census[bld]++;    /* $417E */
    c->xbld[y][x] = bld; /* $4184 */
}

/*  Rebuild the census from scratch.  The game maintains it incrementally
 *  from a new-city state; we have to derive it after loading a save. */
void sim_rebuild_census(City *c)
{
    int y, x;
    memset(c->census, 0, sizeof c->census);
    for (y = 0; y < MAP_H; y++)
        for (x = 0; x < MAP_W; x++)
            c->census[c->xbld[y][x]]++;
}

/* ================================================================== *
 *  A shared BFS queue.  The original keeps it at A5+0x13B6/0x13B8 with
 *  the ring buffer alongside; push is $21DD4 / $21DF2 and pop $21E3A.
 *  Both flood fills use it, one at a time.
 * ================================================================== */
/*  The BFS queue -- and it is not the unbounded queue you would write.
 *
 *  $13B2 is NewPtr(0x800): 2048 bytes, or 512 four-byte Points.  Both
 *  the push at $21DF2 and the pop at $21E3A mask their index with
 *  0x1FF, so the ring holds 512 entries, and $21E26 handles a full ring
 *  by moving the tail forward -- silently discarding the oldest entry.
 *
 *  On any network larger than a few hundred tiles this overflows
 *  constantly, so the traversal is a lossy breadth-first walk rather
 *  than a complete one.  That is not an artefact to tidy away: both
 *  floods hand out capacity in queue order, so the dropped entries
 *  change which tiles end up powered and watered.  Giving this queue a
 *  comfortable size makes the reconstruction wrong.
 */
#define QMASK 0x1FF
static struct
{
    int16_t y, x;
} q[QMASK + 1];
static int qw, qr; /* $13B6 write, $13B8 read */

static void q_reset(void) { qw = qr = 0; } /* queueReset $21DD4 */
static void q_push(int y, int x)           /* queuePush $21DF2 */
{
    q[qw].y = (int16_t)y;
    q[qw].x = (int16_t)x;
    qw      = (qw + 1) & QMASK;
    if (qw == qr)
        qr = (qw + 1) & QMASK; /* $21E26 */
}
static int  q_empty(void) { return qw == qr; }
static void q_pop_back(int *y, int *x); /* queuePopBack $21E66, below */
static void q_pop(int *y, int *x)       /* queuePop $21E3A */
{
    *y = q[qr].y;
    *x = q[qr].x;
    qr = (qr + 1) & QMASK;
}

/*  Push the four neighbours that have not been visited yet.
 *
 *  The order is load bearing and is taken literally from $21250,
 *  $2128A, $212C6 and $21304: west, north, east, south.  Power is
 *  handed out in queue order until capacity runs out, so this order
 *  decides which tiles brown out when a network is short.
 */
static void push_neighbours(City *c, int y, int x)
{
    if (x > 0 && !(c->xbit[y][x - 1] & XBIT_VISITED))
        q_push(y, x - 1);
    if (y > 0 && !(c->xbit[y - 1][x] & XBIT_VISITED))
        q_push(y - 1, x);
    if (x < MAP_W - 1 && !(c->xbit[y][x + 1] & XBIT_VISITED))
        q_push(y, x + 1);
    if (y < MAP_H - 1 && !(c->xbit[y + 1][x] & XBIT_VISITED))
        q_push(y + 1, x);
}

/*  Pass 2 walks the marks pass 1 left, so its test is inverted: push a
 *  neighbour only while it is still marked ($21482, $214C2, ...).  The
 *  pass clears each mark as it goes, which both terminates the walk and
 *  leaves the map clean for the next plant.  Getting this backwards
 *  makes the flood die on the first tile -- it produced almost no
 *  powered tiles at all.
 */
static void push_marked(City *c, int y, int x)
{
    if (x > 0 && (c->xbit[y][x - 1] & XBIT_VISITED))
        q_push(y, x - 1);
    if (y > 0 && (c->xbit[y - 1][x] & XBIT_VISITED))
        q_push(y - 1, x);
    if (x < MAP_W - 1 && (c->xbit[y][x + 1] & XBIT_VISITED))
        q_push(y, x + 1);
    if (y < MAP_H - 1 && (c->xbit[y + 1][x] & XBIT_VISITED))
        q_push(y + 1, x);
}

/* ================================================================== *
 *  Phase 1 -- the power grid.  $20FC4 clears the flags and hunts for
 *  plants; $210A2 floods outward from each one.
 * ================================================================== */

/*  Output of one tile of a power plant.  Eight of the ten are constants
 *  from the switch at $21174; two are computed.  $211DA reads the
 *  terrain, which is why a wind farm on a mountain really does produce
 *  more, and $2119C reads the weather global at A5+0x1F02.
 */
static int32_t plant_output(const City *c, int y, int x, uint8_t bld)
{
    int32_t v = BUILDING[bld].power;
    if (v >= 0)
        return v;

    /*  Both rolls are UNCONDITIONAL in the original -- $211EA and
     *  $211B2 draw first and divide afterwards.  Skipping the draw when
     *  the span works out at zero costs a number out of the stream, and
     *  the solar span reaches zero as soon as cloud cover passes 90,
     *  which the weather walk makes reachable.  The guard is kept on
     *  the division alone, where the original would trap.
     *
     *  The roll is the WHOLE sixteen bits: $211B6 and $211EE clear a
     *  register and move the word into it, so the value the divide sees
     *  runs 0..65535.  Masking to 0x7FFF loses the top bit and gives a
     *  different remainder for half of all draws. */
    if (v == -1)
    { /* Wind,  $211DA */
        int span = ASR(c->weather1, 3) + 1;
        int r    = (uint16_t)Random();
        int alt  = c->altm[y][x] & 0x1F;
        return (alt + (span ? r % span : 0)) >> 1;
    }
    { /* Solar, $2119C */
        int span = (100 - c->weather2) / 10;
        int r    = (uint16_t)Random();
        return (span ? r % span : 0) + 5;
    }
}

/*  $210A2 -- flood one power network.  Two BFS passes over the same
 *  network, which is the part that matters:
 *
 *    pass 1  walk every conductive tile, sum generating capacity, count
 *            the tiles that draw power, and mark each tile visited.
 *    pass 2  walk it again handing out power in queue order, one unit
 *            per drawing tile, until capacity is exhausted.  Tiles the
 *            second pass reaches after that stay unpowered, and the
 *            visited mark is cleared behind it so a different plant can
 *            pick up the remainder on a later flood.
 *
 *  That is why $20FC4's outer loop skips tiles that already have
 *  XBIT_POWERED: a fully served network is done, a starved one is not.
 */
static void power_flood(City *c, int y0, int x0, int32_t *supply, int32_t *drawn)
{
    int32_t generated = 0, consumers = 0, capacity;
    int     y, x;

    /* ---- pass 1: measure ---------------------------------------- */
    q_reset();
    q_push(y0, x0);
    while (!q_empty())
    {
        uint8_t bits, bld;
        q_pop(&y, &x);
        if (y < 0 || y >= MAP_H || x < 0 || x >= MAP_W)
            continue;

        bits = c->xbit[y][x];
        if (bits & XBIT_VISITED)
            continue; /* $2110C */
        if (!(bits & XBIT_CONDUCTIVE))
            continue; /* $21126 */

        bld = c->xbld[y][x];
        if (bld >= BLD_POWER_FIRST && bld <= BLD_POWER_LAST)
            generated += plant_output(c, y, x, bld); /* $21140 */
        else if (BLD_CONSUMES_POWER(bld))
            consumers++; /* $21226 */

        c->xbit[y][x] |= XBIT_VISITED; /* $2124C */
        push_neighbours(c, y, x);
    }

    /*  $21354 adds the raw total to the reported capacity, and only
     *  then does $21358 apply the ordinance's extra twelfth.  The order
     *  matters: the boost raises what consumers are allowed to draw,
     *  it does not raise the capacity the percentage divides by.
     *  Boosting first inflates capacity by exactly 13/12 and drags the
     *  reported figure down with it. */
    *supply += generated; /* $21354, before the boost */

    if (c->ordinances & 0x10000) /* $2135C */
        generated += generated / 12;

    capacity = generated;
    if (consumers > capacity)
        consumers = capacity; /* $21374 */
    *drawn += consumers;      /* $21384 */

    /* ---- pass 2: distribute ------------------------------------- */
    q_reset();
    q_push(y0, x0); /* $21388 */
    while (!q_empty())
    {
        q_pop(&y, &x);
        if (y < 0 || y >= MAP_H || x < 0 || x >= MAP_W)
            continue;
        if (!(c->xbit[y][x] & XBIT_VISITED))
            continue; /* $213E8 */

        if (capacity != 0)
        { /* $21404 */
            if (c->xbld[y][x] >= BLD_ZONE_FIRST)
                capacity--;
            c->xbit[y][x] |= XBIT_POWERED; /* $21442 */
        }
        c->xbit[y][x] &= (uint8_t)~XBIT_VISITED; /* $2145E */
        push_marked(c, y, x);
    }
}

/*  $20FC4 -- phase 1.  Note what power_pct actually is: the game divides
 *  drawn-by-capacity, not capacity-by-demand, and returns 100 when there
 *  are no plants at all.  It is a load meter, not a coverage meter.     */
void sim_power_grid(City *c)
{
    int32_t supply = 0, drawn = 0;
    int     y, x;

    for (y = 0; y < MAP_H; y++) /* $20FD4 */
        for (x = 0; x < MAP_W; x++)
            c->xbit[y][x] &= (uint8_t)~(XBIT_POWERED | XBIT_VISITED);

    for (y = 0; y < MAP_H; y++) /* $2100C */
        for (x = 0; x < MAP_W; x++)
        {
            uint8_t bld;
            if (c->xbit[y][x] & XBIT_POWERED)
                continue; /* network already served */
            bld = c->xbld[y][x];
            if (bld < BLD_POWER_FIRST || bld > BLD_POWER_LAST)
                continue;
            power_flood(c, y, x, &supply, &drawn);
        }

    c->power_capacity = supply; /* A5+0x11D6 */
    if (supply == 0)
        c->power_pct = 100; /* $21086 */
    else
        c->power_pct = drawn * 100 / supply;
    if (c->power_pct > 100)
        c->power_pct = 100; /* $2108C */
}

/* ================================================================== *
 *  Phase 20 -- the water network.  Same shape as power, with its own
 *  pair of flag bits: 0x20 conducts, 0x10 supplied.
 * ================================================================== */
/*  $2182E -- flood one water network.  Structurally identical to the
 *  power flood: measure, then distribute, with the visited bit as the
 *  handshake between the two passes.  What differs is where capacity
 *  comes from, and it is worth reading:
 *
 *    Pump ($218F0)            weather/2 + 5*pumpTerm, plus 10 for every
 *                             tile of FRESH water in its 3x3 neighbourhood
 *    Desalinization ($21990)  20 for every tile of SALT water in its 3x3
 *    Reservoir ($21A10)       stores 100; contributes 100 if it was still
 *                             watered from last cycle, then is cleared
 *    Water Treatment ($21A36) contributes nothing, always clears
 *
 *  Fresh is (XBIT & 0x05) == 0x04 and salt is == 0x05, which is what
 *  identifies bit 0 as the salt flag.  Pumps and desalinators only work
 *  while powered, so the water grid genuinely depends on the power grid
 *  having run first.
 *
 *  One consequence worth stating, because it caps how exactly a saved
 *  city can be reproduced.  The schedule runs the water grid at phase 20
 *  ($220DA) and $33FAE at phase 21 ($220E4), and $33FAE rewrites both
 *  weather bytes as a running average ($34CAC, $34CD2).  So the weather
 *  saved in MISC[26] is always at least one update newer than the
 *  weather this pass actually used.  Networks with a comfortable
 *  capacity margin are insensitive to that and reproduce exactly;
 *  networks sitting right at their budget boundary cannot, and no
 *  amount of care here will fix it -- the input is gone.
 */
static int fresh_or_salt(const City *c, int y, int x, int want)
{
    int n = 0, yy, xx;
    for (yy = y - 1; yy <= y + 1; yy++)
    {
        if (yy < 0 || yy > MAP_H - 1)
            continue;
        for (xx = x - 1; xx <= x + 1; xx++)
        {
            if (xx < 0 || xx > MAP_W - 1)
                continue;
            if ((c->xbit[yy][xx] & (XBIT_WATER | XBIT_SALT)) == want)
                n++;
        }
    }
    return n;
}

static void water_flood(City *c, int y0, int x0, int32_t *cap_out, int32_t *met_out)
{
    int32_t capacity = 0, consumers = 0, reservoir = 0, budget;
    int     y, x;

    /* ---- pass 1: measure ---------------------------------------- */
    q_reset();
    q_push(y0, x0);
    while (!q_empty())
    {
        uint8_t bits, bld;
        q_pop(&y, &x);
        if (y < 0 || y >= MAP_H || x < 0 || x >= MAP_W)
            continue;

        bits = c->xbit[y][x];
        if (bits & XBIT_VISITED)
            continue; /* $21884 */
        if (!(bits & XBIT_CONDUCTS_WATER))
            continue; /* 0x20, $21890 */

        bld = c->xbld[y][x];
        if (bld >= BLD_ZONE_FIRST)
        {
            switch (bld)
            {
                case 0xDC: /* Pump */
                    if (bits & XBIT_POWERED)
                    {
                        capacity += c->weather2 / 2;    /* $21918 */
                        capacity += 5 * c->water_level; /* $21926, and see the note
                                                         *  on the field itself */
                        capacity += 10 * fresh_or_salt(c, y, x, XBIT_WATER);
                    }
                    break;
                case 0xFA: /* Desalinization */
                    if (bits & XBIT_POWERED)
                        capacity += 20 * fresh_or_salt(c, y, x, XBIT_WATER | XBIT_SALT);
                    break;
                case 0xEB:            /* Reservoir */
                    reservoir += 100; /* $21A10 */
                    if (bits & XBIT_WATERED)
                        capacity += 100;
                    c->xbit[y][x] &= (uint8_t)~XBIT_WATERED; /* $21A4E */
                    break;
                case 0xF4: /* Water Treatment */
                    break;
                default:
                    consumers++; /* $21A54 */
                    break;
            }
        }
        c->xbit[y][x] |= XBIT_VISITED; /* $21A6E */
        push_neighbours(c, y, x);
    }

    /* ---- accounting, $21B76 ------------------------------------- */
    if (consumers > capacity)
        consumers = capacity;
    if (capacity - consumers < reservoir)
        reservoir = capacity - consumers;
    reservoir = (reservoir + 50) / 100; /* $21B8A */
    *cap_out += capacity;
    *met_out += consumers;

    /* ---- pass 2: distribute, $21BC2 ----------------------------- */
    budget = consumers;
    q_reset();
    q_push(y0, x0);
    while (!q_empty())
    {
        uint8_t bld;
        q_pop(&y, &x);
        if (y < 0 || y >= MAP_H || x < 0 || x >= MAP_W)
            continue;
        if (!(c->xbit[y][x] & XBIT_VISITED))
            continue; /* $21BE4 */

        bld = c->xbld[y][x];
        if (bld == 0xEB)
        { /* $21C36 Reservoir */
            if ((c->xbit[y][x] & XBIT_POWERED) && reservoir)
            {
                c->xbit[y][x] |= XBIT_WATERED;
                reservoir--; /* spends stored water */
            }
        }
        else if (bld == 0xF4 || bld == 0xDC || bld == 0xFA)
        {
            /*  $21C5E, reached from three tests at $21C22/$21C28/$21C2E:
             *  Water Treatment, Pump and Desalinization are watered
             *  whenever they have power, and cost nothing from the
             *  budget.  That is what keeps every source watered, so the
             *  outer sweep skips them and each network floods once. */
            if (c->xbit[y][x] & XBIT_POWERED)
                c->xbit[y][x] |= XBIT_WATERED;
        }
        else if (budget)
        { /* $21C84 */
            c->xbit[y][x] |= XBIT_WATERED;
            if (bld >= BLD_ZONE_FIRST)
                budget--;
        }
        c->xbit[y][x] &= (uint8_t)~XBIT_VISITED; /* $21CC4 */
        push_marked(c, y, x);
    }
}

/*  $2156E.  The sweep order genuinely depends on the map rotation --
 *  the original branches four ways on g_rotation and runs a different
 *  loop nest for each.  Rotation 0 is column-major, shown here.  Order
 *  decides which source claims a shared network and, since water is
 *  rationed in queue order, which tiles run dry.
 */
void sim_water_grid(City *c)
{
    int32_t capacity = 0, met = 0;
    int     y, x;

    for (y = 0; y < MAP_H; y++)
        for (x = 0; x < MAP_W; x++)
        {
            if (c->xbld[y][x] != BLD_RESERVOIR) /* $2159A */
                c->xbit[y][x] &= (uint8_t)~XBIT_WATERED;
            c->xbit[y][x] &= (uint8_t)~XBIT_VISITED;
        }

    /*  All four sweeps, $215F8 / $21674 / $216EC / $2175E.  They apply
     *  identical tests and differ only in iteration order, which is not
     *  cosmetic: water is rationed in queue order, so the order decides
     *  which source claims a shared network and which tiles run dry.
     *      rot 0  col 0..127 outer, row 0..127 inner
     *      rot 1  row 0..127 outer, col 127..0 inner
     *      rot 2  col 127..0 outer, row 127..0 inner
     *      rot 3  row 127..0 outer, col 0..127 inner
     */
    {
        int rot = c->rotation & 3, i, j;
        for (i = 0; i < MAP_W; i++)
        {
            for (j = 0; j < MAP_H; j++)
            {
                switch (rot)
                {
                    case 0:
                        x = i;
                        y = j;
                        break;
                    case 1:
                        y = i;
                        x = MAP_W - 1 - j;
                        break;
                    case 2:
                        x = MAP_W - 1 - i;
                        y = MAP_H - 1 - j;
                        break;
                    default:
                        y = MAP_H - 1 - i;
                        x = j;
                        break;
                }
                {
                    uint8_t bld = c->xbld[y][x];
                    if (c->xbit[y][x] & XBIT_WATERED)
                        continue;
                    if (bld != 0xDC && bld != 0xFA)
                        continue; /* Pump, Desalinization */
                    if (!(c->xbit[y][x] & XBIT_POWERED))
                        continue;
                    water_flood(c, y, x, &capacity, &met);
                }
            }
        }
    }

    /*  $217D2 -- water treatment, and it has to happen here, before
     *  water_pct is rescaled, because the comparison is against the
     *  raw delivered total rather than the percentage.
     *
     *  Each Water Treatment plant covers two thousand units of what
     *  the network actually delivers, and the building is 2x2, so the
     *  census tile count is divided by four to count plants.  Once the
     *  plants cover the whole delivery the flag goes up, and $23308
     *  adds it to the pollution blur divisor -- a larger divisor being
     *  how treatment shows up as less pollution.
     *
     *  Leaving this out costs nothing until the water phase runs
     *  before the scan, which is the order the clock uses ($220DA then
     *  $21FB0).  Driving the scan on its own hides it entirely. */
    c->misc[1043] =
        (uint32_t)((uint32_t)(c->census[0xF4] >> 2) * 2000u) >= (uint32_t)met
            ? 1
            : 0;

    /* $217FE: like power, this is a load meter -- what fraction of the
     * network's capacity is being drawn -- and 100 when there is none. */
    c->water_capacity = capacity; /* A5+0x11D2 */
    c->water_pct      = capacity ? met * 100 / capacity : 100;
}

/* ================================================================== *
 *  Phase 19 -- traffic.  $2530E both decays the layer and totals it,
 *  which is why a saved city's MISC traffic figure is a snapshot from
 *  the last time this ran rather than the sum of the layer on disk.
 * ================================================================== */
void sim_traffic_total(City *c)
{
    int y, x;
    c->traffic_tot = 0;
    for (y = 0; y < HALF_H; y++)
        for (x = 0; x < HALF_W; x++)
        {
            int v         = c->xtrf[y][x];
            v             = v - ASR(v, 2); /* $25336: decay 25% */
            c->xtrf[y][x] = (uint8_t)v;
            c->traffic_tot += v;
        }
}

/* ================================================================== *
 *  Phase 2 -- the data layers.  $2317E rebuilds pollution from the
 *  buildings and traffic underneath each 64x64 cell, then blurs it.
 * ================================================================== */
/*  A5+0x13BA -- the one scratch plane the whole of $2317E shares, 128
 *  rows of 128 words.  Nothing ever clears it between stages, so each
 *  stage reads whatever the last one left in the cells it touches.
 *  That is not tidy, and reproducing it is the difference between land
 *  value at 89% and at 100%: stage 4 seeds its accumulators from
 *  stage 1's raw pollution and stage 3's building marks.  Modelling the
 *  two stages with separate arrays loses exactly that. */
static int16_t pad[MAP_H][MAP_W];

/*  $23302 computes the blur divisor as 4 - A5+0x2C8A + A5+0x2CA0, plus
 *  one when ordinance bit 0x80000 is set.  All three inputs are in the
 *  save -- MISC[1037], MISC[1043] and MISC[1000] -- so the whole thing
 *  is derived here rather than passed in, which is how an earlier
 *  version managed to drop the ordinance term silently.               */
void sim_pollution(City *c)
{
    int y2, x2;
    int divisor_base = 4 - (int)c->misc[1037] + (int)c->misc[1043];
    if (c->ordinances & 0x80000)
        divisor_base++; /* $2331C */

    /* --- stage 1: raw pollution per half-resolution cell ---------- */
    for (y2 = 0; y2 < HALF_H; y2++)
    {
        for (x2 = 0; x2 < HALF_W; x2++)
        {
            int     y = y2 * 2, x = x2 * 2, i;
            int32_t acc = c->xplt[y2][x2] + c->xtrf[y2][x2] / 5; /* $231CE */

            for (i = 0; i < 4; i++)
            { /* the 2x2 tile block */
                int     ty = y + (i & 1), tx = x + (i >> 1);
                uint8_t b = c->xbld[ty][tx];
                if (b >= 0x70)
                    acc += BUILDING[b].pollution; /* $231FC */
                if (b == 5)
                    acc += 200; /* $2320E, id 5 unidentified */
            }
            pad[y2][x2] = (int16_t)acc;
        }
    }

    /* --- stage 2: 5-point blur, centre weighted twice ------------- */
    c->pollution_tot = 0;
    for (y2 = 0; y2 < HALF_H; y2++)
    {
        for (x2 = 0; x2 < HALF_W; x2++)
        {
            int32_t sum = (int32_t)pad[y2][x2] * 2;
            int     div = divisor_base < 1 ? 1 : divisor_base;
            int32_t v;

            if (y2 > 0)
            {
                sum += pad[y2 - 1][x2];
                div++;
            }
            if (y2 < HALF_H - 1)
            {
                sum += pad[y2 + 1][x2];
                div++;
            }
            if (x2 > 0)
            {
                sum += pad[y2][x2 - 1];
                div++;
            }
            if (x2 < HALF_W - 1)
            {
                sum += pad[y2][x2 + 1];
                div++;
            }

            v = sum / div; /* $233E8 */
            if (v > 255)
                v = 255; /* $233F0 */
            c->xplt[y2][x2] = (uint8_t)v;
            c->pollution_tot += v; /* $23412 */
        }
    }
}

/* ================================================================== *
 *  Phase 21 -- population.  $33FAE, short enough to read whole.
 * ================================================================== */
void sim_population(City *c)
{
    int i;

    c->accum8[0] = 0;
    for (i = 1; i <= 6; i++)
        c->accum8[0] += c->accum8[i]; /* $33FC0 */

    c->pop_increase = 0;
    c->pop_decrease = 0;
    {
        int32_t newpop = c->accum8[0] * 10; /* $33FE6 */
        if (newpop < c->population)
            c->pop_decrease = c->population - newpop;
        else
            c->pop_increase = newpop - c->population;
        c->population = newpop;
        c->misc[16] += newpop; /* $3402E, A5+0x1E22 */
    }

    /*  $34032 -- the residential, commercial and industrial split.
     *  accum8 holds six zone accumulators and the three figures are
     *  consecutive pairs of them, which is why their sum is accum8[0]
     *  and why graph series 0 equals the sum of series 1 to 3. */
    for (i = 0; i < 3; i++)
        c->rci_pop[i] = c->accum8[2 * i + 1] + c->accum8[2 * i + 2];

    /*  $345DA -- the three zone departments' tax base is that split
     *  times ten, a plain integer multiply.  The SANE sequence just
     *  above it in $33FAE looks like it feeds this and does not: its
     *  result goes to a local array at -$18(a6), and $345E0 overwrites
     *  the same scratch long with rci_pop[i] * 10 before the store.
     *
     *  What the float sequence actually computes is a growth ratio,
     *  (previous / (rci_pop[i] + 1)) - 1, kept for the demand model.
     *  That part is not ported. */
    for (i = 0; i < 3; i++)
        c->dept[i].amount = c->rci_pop[i] * 10;

    /*  $34602 -- and a share of the same ordinance term the graph uses,
     *  a sixth to residential and a twelfth to the other two. */
    c->dept[0].amount += c->misc[MISC_2C98] / 6;
    c->dept[1].amount += c->misc[MISC_2C98] / 12;
    c->dept[2].amount += c->misc[MISC_2C98] / 12;

    sim_demand(c);
}

/* ================================================================== *
 *  $34068 .. $34790 -- the demand model, the rest of populationPass.
 *
 *  For each of the three zone kinds the routine works out how much
 *  room there is, divides that by how much is already built, and moves
 *  the demand figure by the shortfall.  Two things shape it: what the
 *  city can support, and what the tax rate costs.
 *
 *  The arithmetic is SANE single precision throughout.  Every step is
 *  taken back to a float rather than staying extended, so the chain of
 *  conversions below is the calculation, not ceremony around it --
 *  folding it into double arithmetic gives different answers.
 *
 *  One faithful oddity: the original builds its 64-bit comp operands as
 *  a cleared high long and the value in the low long, so a negative
 *  would convert as a large positive.  Every value here is a population
 *  or a building count, so it never arises.
 * ================================================================== */
static ext80 z2x_i16(int16_t v) { return ext_from_i16(v); }
static ext80 z2x_i32(int32_t v) { return ext_from_i32(v); }
static float x2z_sgl(ext80 a) { return (float)ext_to_double(a); }
static ext80 z2x_sgl(float f) { return ext_from_float(f); }

void sim_demand(City *c)
{
    /*  room[i] is how much of each zone the city could carry; the
     *  original keeps it as three singles at -$c(a6). */
    float   room[3], ratio[3];
    int32_t cap;
    int     i;

    /*  $34068 -- the workforce, and last month's residential head count
     *  over it.  A5+0x2C7E holds the previous month's figure and is
     *  replaced with this month's at $34110, so the ratio always looks
     *  one month back. */
    const float jobs  = x2z_sgl(z2x_i32(c->rci_pop[1] + c->rci_pop[2]));
    const float share = x2z_sgl(ext_div(z2x_sgl(x2z_sgl(z2x_i32(c->misc[29]))),
                                        ext_add(ext_from_i32(1), z2x_sgl(jobs))));
    c->misc[29]       = c->rci_pop[0]; /* $34110 */

    /*  $34114 -- the city's own size against a fixed 150000, and the
     *  difficulty multiplier with a hundredth of A5+0x2C8C added. */
    const float growth =
        x2z_sgl(ext_div(z2x_sgl(x2z_sgl(z2x_i32(c->population + 50000))),
                        ext_from_i32(150000)));
    const int   lvl = (c->difficulty >= 0 && c->difficulty < 4) ? c->difficulty : 0;
    const float level =
        x2z_sgl(ext_add(z2x_sgl(DEMAND_LEVEL[lvl]),
                        ext_div(z2x_i16((int16_t)c->misc[1036]),
                                ext_from_i32(100))));

    /*  $34254 -- residential room is the workforce plus a fiftieth of
     *  the residents themselves. */
    room[0] = x2z_sgl(ext_add(z2x_sgl(jobs),
                              z2x_sgl(x2z_sgl(z2x_i32(c->rci_pop[0] / 50)))));

    /*  $34284 -- commerce and industry both scale the industrial head
     *  count by that jobs share, then by their own multiplier. */
    {
        const ext80 base =
            ext_mul(z2x_sgl(x2z_sgl(z2x_i32(c->rci_pop[2]))), z2x_sgl(share));
        room[1] = x2z_sgl(ext_mul(z2x_sgl(growth), base));
        room[2] = x2z_sgl(ext_mul(z2x_sgl(level), base));
    }
    if (room[2] < 15.0f)
        room[2] = 15.0f; /* $34362 */

    /*  $3436A -- and then four ceilings, which is where the city's own
     *  buildings come in.  Residential is held down by how much there
     *  is to do: stadium, marina, zoo, and a third of the parks. */
    {
        uint16_t t = (uint16_t)(10 + c->census[0xD7] + c->census[0xF8] +
                                c->census[0xDA] + (uint16_t)(c->census[0xD5] / 3));
        cap        = (int32_t)t * 1500 / 10;
        if (room[0] > (float)cap)
            room[0] = (float)cap; /* $343E8 */
    }
    /*  $343EE -- and by how much commerce there is to work in */
    cap = c->rci_pop[1] * 4 + 500;
    if (room[0] > (float)cap)
        room[0] = (float)cap; /* $34446 */

    /*  $3444C -- commerce is held down by the airport: runway tiles
     *  plus the counter at A5+0x2C96. */
    {
        uint16_t t = (uint16_t)(c->census[0xDD] + c->census[0xDE] +
                                (uint16_t)c->misc[1044]);
        cap        = (int32_t)(uint16_t)(t / 5 + 1) * 15000 / 10;
        if (room[1] > (float)cap)
            room[1] = (float)cap; /* $344C6 */
    }
    /*  $344CC -- and industry by the seaport: cranes plus A5+0x2C94. */
    {
        uint16_t t =
            (uint16_t)(c->census[0xE0] + 1 + (uint16_t)c->misc[1033]);
        cap = (int32_t)t * 15000 / 10;
        if (room[2] > (float)cap)
            room[2] = (float)cap; /* $34538 */
    }

    /*  $34544 -- room against what is already there.  A ratio of zero
     *  means the city is exactly as full as it can be. */
    for (i = 0; i < 3; i++)
        ratio[i] = x2z_sgl(ext_add(ext_from_i32(-1),
                                   ext_div(z2x_sgl(room[i]),
                                           z2x_sgl(x2z_sgl(z2x_i32(
                                               c->rci_pop[i] + 1))))));

    /*  $34644 -- and the month's move: six hundred times the shortfall,
     *  plus whatever the tax rate is worth.  Ordinances nudge the rate
     *  the table is read at rather than the demand itself. */
    for (i = 0; i < 3; i++)
    {
        int32_t f = c->dept[i].funding;
        int32_t v;

        if (i == 0)
        { /* $3465A */
            if (c->ordinances & (1L << 1))
                f++;
            if (c->ordinances & (1L << 14))
                f--;
        }
        else if (i == 1)
        { /* $34676 */
            if (c->ordinances & (1L << 0))
                f++;
            if (c->ordinances & (1L << 12))
                f--;
            if (c->ordinances & (1L << 15))
                f--;
            if (c->ordinances & (1L << 18))
                f--;
        }
        else
        { /* $346A6 */
            if (c->ordinances & (1L << 13))
                f--;
            if (c->ordinances & (1L << 19))
                f++;
        }
        if (f < 0)
            f = 0; /* $346BE */
        /*  the original indexes the table with no upper bound; funding
         *  never reaches the end of it, and stopping there is safer
         *  than reading past the table in C. */
        if (f > 23)
            f = 23;

        {
            ext80 acc = z2x_i16(c->rci_demand[i]); /* $34728 */
            ext80 mv  = ext_mul(ext_from_i32(600), z2x_sgl(ratio[i]));
            mv        = ext_add(mv, z2x_sgl(x2z_sgl(z2x_i16(DEMAND_TAX[f]))));
            acc       = ext_add(acc, mv); /* $3473C */
            v         = ext_to_i32(acc);  /* $34746 truncates */
        }
        /*  $3475A keeps only the low word of the long */
        c->rci_demand[i] = (int16_t)v;
        if (c->rci_demand[i] < -2000)
            c->rci_demand[i] = -2000; /* $3476C */
        else if (c->rci_demand[i] > 2000)
            c->rci_demand[i] = 2000; /* $34784 */
    }

    sim_forest(c);
    sim_news_rolls(c);
    sim_weather(c);
}

/* ================================================================== *
 *  $34792 -- the forest, still inside populationPass.
 *
 *  One tile a month, chosen at random, and then its neighbour in a
 *  random direction.  Trees advance a stage; bare ground that is not
 *  rubble puts out a sapling.  Water grows nothing, and anything above
 *  a tree is left alone.
 *
 *  It is small and it is why a city left alone slowly turns green
 *  again.  It also draws four to six dice a month, which is enough to
 *  put every later roll out of step if it is missing.
 * ================================================================== */
#define TREE_FIRST 6  /* $3480E, the first forest stage */
#define TREE_LAST  11 /* $3482E, the last one that can advance */
#define RUBBLE     5  /* $347D8 */

void sim_forest(City *c)
{
    int32_t y = (int32_t)(Random() & 0xFFFF) % 128; /* $34794 */
    int32_t x = (int32_t)(Random() & 0xFFFF) % 128; /* $347AE */
    int32_t b = c->xbld[y][x];

    /*  $347D8 -- rubble clears itself one time in sixteen */
    if (b == RUBBLE && (Random() & 0xF) == 0)
        sim_set_tile(c, (int)y, (int)x, 0);

    if (c->xbit[y][x] & XBIT_WATER) /* $34804 */
        return;

    /*  $3480E -- a tile that is not already forest only gets a turn one
     *  time in sixteen */
    if ((b < TREE_FIRST || b > 13) && (Random() & 0xF) != 0)
        return;

    if (b >= TREE_FIRST && b < 12) /* $34828 */
        sim_set_tile(c, (int)y, (int)x, (uint8_t)(b + 1));

    /*  $34846 -- and now the neighbour, one step in one direction, the
     *  map edge simply refusing to move */
    switch (Random() & 3)
    {
        case 0:
            if (y < 127)
                y++;
            break;
        case 1:
            if (y > 0)
                y--;
            break;
        case 2:
            if (x < 127)
                x++;
            break;
        default:
            if (x > 0)
                x--;
            break;
    }

    b = c->xbld[y][x];
    if (c->xbit[y][x] & XBIT_WATER) /* $348A0 */
        return;
    if (b >= 12 || b == RUBBLE) /* $348A8, $348AE */
        return;
    if (b >= TREE_FIRST)
        sim_set_tile(c, (int)y, (int)x, (uint8_t)(b + 1)); /* $348BE */
    else
        sim_set_tile(c, (int)y, (int)x, TREE_FIRST); /* $348CE */
}

/* ================================================================== *
 *  $348F0 -- the newspaper's dice.
 *
 *  Every roll here only decides whether a headline is printed, and the
 *  headlines are interface.  The rolls themselves are not optional: the
 *  original draws between fourteen and eighteen numbers a month here,
 *  and a stream that skips them puts every later roll in the month out
 *  of step.  The same lesson as the train horn, at a larger scale.
 *
 *  So the shape is kept and the messages are dropped, with one piece of
 *  real state: $34BF4 walks the seventeen deadlines at A5+0x1E4C and
 *  clears any the calendar has passed.
 * ================================================================== */
void sim_news_rolls(City *c)
{
    int32_t r;
    int     i;

    /*  $348F0 -- one of six openings, and only the first rolls again */
    if ((int32_t)(Random() & 0xFFFF) % 6 == 0)
    {
        Random(); /* $34920 */
        Random(); /* $3493A */
    }

    /*  $349A4 -- the stadium's own headline, drawn only when there is
     *  a stadium to write about */
    if (c->census[0xD7] != 0)
        Random(); /* $349B0 */

    /*  $349DC .. $34AFC -- eight rolls against four of the map-view
     *  averages, a coarse one and a fine one each */
    Random(); /* $349E6 traffic   & 0x7F */
    Random(); /* $34A10 traffic   & 0x0F */
    Random(); /* $34A3A pollution & 0x7F */
    Random(); /* $34A64 pollution & 0x0F */
    Random(); /* $34A8E crime     & 0x7F */
    Random(); /* $34AB8 crime     & 0x0F */
    Random(); /* $34ADA jobless   & 0x3F */
    Random(); /* $34AFC jobless   & 0x03 */

    /*  $34B1C and $34B80 -- education and health each take one roll,
     *  and which of the two branches runs decides what it is compared
     *  against rather than whether it happens. */
    Random(); /* $34B26 or $34B5A */
    Random(); /* $34B8A or $34BBE */

    /*  $34BE6 -- one time in eight the deadlines are looked at */
    r = Random() & 7;
    if (r == 0)
        for (i = 0; i < 17; i++)
        {
            /*  MISC[462..478] is A5+0x1E4C, seventeen years by which
             *  something has to be done.  Zero means no deadline. */
            int32_t year = c->misc[462 + i];
            if (year == 0)
                continue;
            if (c->year_founded + c->years < year) /* $34C10 */
                continue;
            c->misc[462 + i] = 0; /* $34C44 */
            break;                /* $34C4E */
        }
}

/* ================================================================== *
 *  $34C58 -- the weather.
 *
 *  Twelve states, and each month the weather steps to one of eight
 *  successors drawn from a table that changes with the season.  Cloud,
 *  wind and temperature then move half way toward what the new state
 *  calls for, so the weather drifts rather than jumps.
 *
 *  This is not scenery.  The wind plant turns weather1 into output and
 *  the solar plant reads weather2, so the weather is why a city's
 *  generating capacity is never quite the same two months running.
 * ================================================================== */
void sim_weather(City *c)
{
    /*  $15256 -- the season is the month shifted one and divided by
     *  three, so December, January and February share one set. */
    const int season = (int)(((c->month + 1) % 12) / 3);
    int       st     = c->weather_state;
    int       roll;

    if (st < 0 || st >= 12)
        st = 0;
    roll             = (int)(Random() & 7); /* $34C74 */
    st               = WEATHER_NEXT[season * 96 + st * 8 + roll];
    c->weather_state = (int16_t)st;
    c->misc[27]      = st;

    /*  $34C8A -- half way toward the new state, three times over. */
    c->weather2            = (int16_t)((c->weather2 + WEATHER_CLOUD[st]) / 2);
    c->weather1            = (int16_t)((c->weather1 + WEATHER_WIND[st]) / 2);
    c->temperature         = (int16_t)((c->temperature + WEATHER_TEMP[st]) / 2);
    c->misc[MISC_WEATHER2] = c->weather2;
    c->misc[MISC_WEATHER1] = c->weather1;
    c->misc[24]            = c->temperature;
}

/* ================================================================== *
 *  The population model, applied to a map rather than accumulated over
 *  a cycle.  $3170E credits a tile only at the corner selected by the
 *  current rotation, so a multi-tile building counts once; the amount
 *  is GROWTH_TABLE indexed by the building's tier ($31DDA), and $33FAE
 *  multiplies the total by ten.
 * ================================================================== */
int32_t sim_map_population(const City *c)
{
    int     y, x;
    int32_t units = 0;
    uint8_t mask  = (uint8_t)ROT_CORNER_MASK[c->rotation & 3];

    for (y = 0; y < MAP_H; y++)
        for (x = 0; x < MAP_W; x++)
        {
            uint8_t z = c->xzon[y][x], b = c->xbld[y][x];
            int     zone = XZON_TYPE(z), tier;

            if (zone < ZONE_RES_LIGHT || zone > ZONE_IND_DENSE)
                continue;
            if (b < BLD_ZONE_FIRST || b > BLD_ZONE_LAST)
                continue;
            if (!(XZON_CORNERS(z) & mask))
                continue;

            tier = BUILDING[b].tier;
            if (tier > 0 && !BUILDING[b].tier_flag)
                units += GROWTH_TABLE[tier];
        }
    return units * 10;
}

/* ================================================================== *
 *  $224BA  overlayAverages -- the four numbers under the map views.
 *
 *  These are not standalone globals.  A5+0x2BDC is XGRP, a table of
 *  sixteen pointers, one per graph series, and A5+0x2BEC, 0x2BF0,
 *  0x2BF4 and 0x2BF8 are entries 4 to 7 of it.  What this routine
 *  stores is the newest sample of the traffic, pollution, land value
 *  and crime series; the map views and the ambient rolls at $9E76 read
 *  that sample.
 *
 *  It is one step of graphHistoryPass ($22330), which shifts every
 *  series back a slot before this runs and maintains the graph scales
 *  after it.  Call sim_graph_pass for the whole month; this entry
 *  point stays because the four values are read on their own.
 *
 *  Traffic divides by three transport departments' `amount`; the other
 *  three divide by a quarter of the developed-tile count.  Both
 *  divisors add one, so an empty city divides by one rather than
 *  faulting.
 *
 *  The driver at $9E76 reads two of these to decide whether to roll for
 *  an ambient sound, which is the only place the simulation reads them
 *  back at all.
 * ================================================================== */
void sim_overlay_averages(City *c)
{
    /*  $224C0 -- departments 10, 11 and 12, field +0x60 */
    const int32_t roads = c->dept[10].amount + c->dept[11].amount +
                          c->dept[12].amount + 1; /* $224CC */
    const int32_t d     = c->developed;
    int32_t       n;

    c->graph[GRAPH_TRAFFIC][0] =
        (int32_t)((uint32_t)c->traffic_tot / (uint32_t)roads);

    /*  $224E0 -- d/4 rounded toward zero, then +1.  The shift dance is
     *  how THINK C divides a signed word by four. */
    n = ((d + ((d >> 1 >> 8 >> 6) & 3)) >> 2) + 1;

    c->graph[GRAPH_POLLUTION][0] =
        (int32_t)((uint32_t)c->pollution_tot / (uint32_t)n);
    c->graph[GRAPH_VALUE][0] =
        (int32_t)((uint32_t)c->land_value_tot / (uint32_t)n);
    c->graph[GRAPH_CRIME][0] =
        (int32_t)((uint32_t)c->crime_tot / (uint32_t)n);
}

/* ================================================================== *
 *  $22330  graphHistoryPass -- one month of graph history.
 *
 *  Phase 21 of the clock calls it, at $220F0, straight after
 *  populationPass ($33FAE) and economyPass ($34D04) -- so the readings
 *  it takes are the ones those two have just settled.
 *
 *  The pass runs in four movements: shift the monthly band along, take
 *  a fresh reading for each of the sixteen series, bring the vertical
 *  scales up to date, and then shift the two slower bands if the
 *  calendar calls for it.
 *
 *  Three details are worth keeping in view.  The arcology term at
 *  $2238E adds twenty thousand residents for every arcology past the
 *  hundred and fortieth, and the count it works from is a tile count
 *  divided by sixteen, because an arcology covers a 4x4 footprint.
 *  The scales are shared rather than independent: the four population
 *  series draw against one maximum and the four map overlays against
 *  another, so the shapes stay comparable inside each group.  And all
 *  the comparisons are unsigned, which matters because a series can
 *  hold a negative sample.
 * ================================================================== */

/*  $2233C, $2263E and $226A8 -- shift a band up by one, oldest first,
 *  leaving `first` free for a new sample. */
static void graph_shift(int32_t *series, int first, int count)
{
    int k;

    for (k = first + count - 1; k > first; k--)
        series[k] = series[k - 1];
}

/*  $22490 onward -- the running maximum, always an unsigned compare. */
static int32_t graph_rise(int32_t have, int32_t sample)
{
    return (uint32_t)sample > (uint32_t)have ? sample : have;
}

void sim_graph_pass(City *c)
{
    const int32_t m = c->misc[MISC_2C98];
    int32_t       arcos, bonus, scale, jobless;
    int           i;

    /*  $2233C -- every series gives up its oldest month */
    for (i = 0; i < N_GRAPH; i++)
        graph_shift(c->graph[i], GRAPH_MONTH, GRAPH_N_MONTH);

    /*  $22370 -- the four arcology counts are tile counts and an
     *  arcology covers sixteen tiles, so >> 4 counts buildings.  The
     *  original adds them in sixteen bits, so the sum can wrap. */
    arcos = (uint16_t)(c->census[0xFB] + c->census[0xFC] +
                       c->census[0xFD] + c->census[0xFE]) >>
            4;
    bonus = arcos > 140 ? (arcos - 140) * 20000 : 0; /* $22396 */

    /*  $223B0 -- city size, then the three zone series.  The shift
     *  dances at $223CA and $22408 are signed divides by two and by
     *  four, which C rounds toward zero the same way. */
    c->graph[GRAPH_CITY_SIZE][0] = c->accum8[0] * 10 + m + bonus;
    c->graph[GRAPH_RESIDENTS][0] = c->rci_pop[0] * 10 + m / 2 + bonus / 2;
    c->graph[GRAPH_COMMERCE][0]  = c->rci_pop[1] * 10 + m / 4 + bonus / 4;
    c->graph[GRAPH_INDUSTRY][0]  = c->rci_pop[2] * 10 + m / 4 + bonus / 4;

    /*  $22490 -- the four population series share one scale, and it is
     *  city size alone that can push it up. */
    scale = graph_rise(c->graph_max[GRAPH_CITY_SIZE],
                       c->graph[GRAPH_CITY_SIZE][0]);
    for (i = GRAPH_CITY_SIZE; i <= GRAPH_INDUSTRY; i++)
        c->graph_max[i] = scale;

    /*  $224BA -- traffic, pollution, land value and crime */
    sim_overlay_averages(c);

    /*  $2252A -- the four map overlays share a scale too, and here any
     *  of the four can raise it. */
    scale = 0;
    for (i = GRAPH_TRAFFIC; i <= GRAPH_CRIME; i++)
    {
        scale = graph_rise(scale, c->graph_max[i]);
        scale = graph_rise(scale, c->graph[i][0]);
    }
    for (i = GRAPH_TRAFFIC; i <= GRAPH_CRIME; i++)
        c->graph_max[i] = scale;

    /*  $2257C -- coverage is stored as the share supplied, so the
     *  globals hold the shortfall. */
    c->graph[GRAPH_POWER][0] = 100 - c->power_pct;
    c->graph[GRAPH_WATER][0] = 100 - c->water_pct;

    /*  $22594 -- the two age-weighted scores.  The original writes
     *  education first. */
    c->graph[GRAPH_EDUCATION][0] = c->misc[MISC_AGE_W90];
    c->graph[GRAPH_HEALTH][0]    = c->misc[MISC_AGE_W65];

    /*  $225A4 -- the jobless share, kept at A5+0x2C82 as well because
     *  the economy reads it back.  The +1 keeps an empty city from
     *  dividing by zero. */
    jobless                         = (int32_t)(((uint32_t)c->accum8[7] * 100u) /
                                                (uint32_t)(c->accum8[0] + c->accum8[7] + 1));
    c->unemployment                 = jobless;
    c->graph[GRAPH_UNEMPLOYMENT][0] = jobless;

    /*  $225CE -- the three national figures */
    c->graph[GRAPH_NAT_GNP][0]  = c->misc[MISC_NAT_INDEX2];
    c->graph[GRAPH_NAT_POP][0]  = c->misc[MISC_NAT_INDEX];
    c->graph[GRAPH_FED_RATE][0] = (int16_t)c->misc[MISC_NAT_MOOD];

    /*  $225E8 -- the remaining eight each keep their own scale */
    for (i = GRAPH_POWER; i < N_GRAPH; i++)
        c->graph_max[i] = graph_rise(c->graph_max[i], c->graph[i][0]);

    /*  $22616 -- except that GNP is drawn against national population
     *  whenever that is the taller of the two. */
    c->graph_max[GRAPH_NAT_GNP] =
        graph_rise(c->graph_max[GRAPH_NAT_GNP], c->graph_max[GRAPH_NAT_POP]);

    /*  $2262C -- January and July move the half-yearly band, and the
     *  new slot takes a copy of this month rather than its own
     *  reading. */
    if (c->month == 0 || c->month == 6)
        for (i = 0; i < N_GRAPH; i++)
        {
            graph_shift(c->graph[i], GRAPH_HALFYEAR, GRAPH_N_HALFYEAR);
            c->graph[i][GRAPH_HALFYEAR] = c->graph[i][0];
        }

    /*  $2268E -- every fifth January the five-yearly band moves too */
    if (c->month == 0 && c->years % 5 == 0)
        for (i = 0; i < N_GRAPH; i++)
        {
            graph_shift(c->graph[i], GRAPH_FIVEYEAR, GRAPH_N_FIVEYEAR);
            c->graph[i][GRAPH_FIVEYEAR] = c->graph[i][0];
        }
}

/* ================================================================== *
 *  $2317E stages 3-5 -- city centre and land value.
 *
 *  These share one 128-row scratch plane of words, the row-pointer
 *  array at A5+0x13BA.  The stages address it at different resolutions,
 *  which is confusing but deterministic:
 *      stage 1  scratch[y/2][x/2]        raw pollution      (64x64)
 *      stage 3  scratch[y][x] = 40       at building tiles  (128x128)
 *      stage 4  scratch[y/4][x/4]        amenity plane A    (32x32)
 *               scratch[y/4+32][x/4]     water plane B      (32x32)
 *  Nothing clears it between stages, so stage 4's accumulators start
 *  from whatever the earlier stages left at the same address.
 * ================================================================== */

/*  $23432 -- the city centre is the mean row and column of every tile
 *  carrying a developed building.  It also seeds the scratch and clears
 *  the flood bit that stage 4 reuses as a half-res mask. */
void sim_city_centre(City *c, int *cy, int *cx)
{
    int32_t sy = 0, sx = 0, n = 1;
    int     y, x;
    for (y = 0; y < MAP_H; y++)
        for (x = 0; x < MAP_W; x++)
        {
            if (c->xbld[y][x] < BLD_ZONE_FIRST)
                continue;
            sy += y;
            sx += x;
            n++;
            pad[y][x] = 0x28;                        /* $234A8 */
            c->xbit[y][x] &= (uint8_t)~XBIT_VISITED; /* $234C6 */
        }
    /*  $234EA divides by twice the count, and the value kept in a2/a3
     *  -- the one the distance term uses -- is that half-resolution
     *  quotient.  Only the copy written to the globals at $23508 is
     *  doubled, for MISC[1030]/[1031].  Using the doubled value in the
     *  distance term is wrong and quietly halves every centrality
     *  bonus in the city. */
    n *= 2; /* $234EA */
    *cy = (int)((uint32_t)sy / (uint32_t)n);
    *cx = (int)((uint32_t)sx / (uint32_t)n);
}

/*  stage 4 ($2351A) builds the two planes and marks, at half resolution,
 *  which cells carry something developed or zoned. */
static void build_planes(City *c)
{
    int y, x;
    /*  $2351A clears the developed count before the loop that raises
     *  it at $23614.  The count is saved (MISC[1067]) and restored, so
     *  without this clear the scan adds a second city's worth to the
     *  first and the three averages that divide by it come out half. */
    c->developed = 0;
    for (y = 0; y < MAP_H; y++)
    {
        int hy = y / 2, qy = hy / 2;
        for (x = 0; x < MAP_W; x++)
        {
            int     hx = x / 2, qx = hx / 2;
            uint8_t b = c->xbld[y][x], t;
            int16_t a = pad[qy][qx], w = pad[qy + 32][qx];

            if (b == 0)
            { /* $23582 */
                if (c->xbit[y][x] & XBIT_WATER)
                {
                    a += 12;
                    w += 12;
                }
                else
                    a += 4;
            }
            else if (b == 0xD5)
                a += 0x28; /* SimPark  */
            else if (b >= 6 && b < 14)
                a += 0x14; /* trees    */
            else if (b <= 5)
                a -= 0x14; /* rubble   */

            if (b >= 0x1D || XZON_TYPE(c->xzon[y][x]))
            {
                c->xbit[hy][hx] |= XBIT_VISITED; /* $2360E half-res mask */
                c->developed++;                  /* $23614 */
            }
            if (c->xbit[y][x] & XBIT_WATERED)
            {
                a += 4;
                w += 4;
            }

            t = c->xter[y][x];
            if (t != 0 && t < 0x10)
                a += 12; /* $2365A */

            pad[qy][qx]      = a;
            pad[qy + 32][qx] = w;
        }
    }
}

/*  a 5-point mean over one of the 32x32 planes */
static int32_t stencil(int plane_row0, int qy, int qx)
{
    int32_t s = pad[plane_row0 + qy][qx];
    int     n = 1;
    if (qy > 0)
    {
        s += pad[plane_row0 + qy - 1][qx];
        n++;
    }
    if (qy < 31)
    {
        s += pad[plane_row0 + qy + 1][qx];
        n++;
    }
    if (qx > 0)
    {
        s += pad[plane_row0 + qy][qx - 1];
        n++;
    }
    if (qx < 31)
    {
        s += pad[plane_row0 + qy][qx + 1];
        n++;
    }
    return s / n;
}

/*  stage 5 ($236E4) turns the planes into XVAL.  Commercial reads plane
 *  A and gets the full centrality bonus plus a lift from population
 *  density; residential reads plane A at half the bonus; industry reads
 *  plane B, the water plane, at a quarter.  Each subtracts pollution and
 *  crime at its own weight, which is where the character of the three
 *  zone types actually lives.
 */
void sim_land_value(City *c)
{
    int cy, cx, hy, hx;

    /*  The shared plane is deliberately NOT cleared here.  Stage 4 seeds
     *  its two accumulators from whatever is already in it -- stage 1's
     *  raw, unblurred pollution, overwritten at building tiles by
     *  stage 3's marks -- so land value depends on an intermediate that
     *  no save file records.  Run after sim_pollution, as $2317E runs
     *  its stages, that intermediate is present and land value comes
     *  out exact; run on a bare save it cannot, and the gap between the
     *  two numbers in the report is the size of what the file lost. */
    sim_city_centre(c, &cy, &cx);
    /*  $23506 doubles both before storing them: sim_city_centre
     *  hands back the half-resolution centre, which is what the
     *  land value stage wants, and the globals hold it in whole
     *  tiles, which is what the disasters want. */
    c->centre_y = (int16_t)(cy * 2); /* $23508 */
    c->centre_x = (int16_t)(cx * 2); /* $23510 */
    build_planes(c);

    c->land_value_tot = 0;
    for (hy = 0; hy < HALF_H; hy++)
    {
        int qy = hy / 2;
        for (hx = 0; hx < HALF_W; hx++)
        {
            int     qx = hx / 2, zone, dist;
            int32_t v;
            uint8_t b;

            if (!(c->xbit[hy][hx] & XBIT_VISITED))
            { /* $2371E */
                c->xval[hy][hx] = 0;
                continue;
            }
            zone = XZON_TYPE(c->xzon[2 * hy][2 * hx]);
            if (zone == 0)
                zone = XZON_TYPE(c->xzon[2 * hy + 1][2 * hx + 1]);

            dist = (cy > hy ? cy - hy : hy - cy) + (cx > hx ? cx - hx : hx - cx);

            if (zone == 5 || zone == 6)
            { /* industrial */
                v = stencil(32, qy, qx);
                if (zone == 6)
                    v += 0x15;
                if (64 - dist > 0)
                    v += (64 - dist) / 4;
                v -= c->xplt[hy][hx] / 16;
                v -= c->xcrm[hy][hx] / 4;
            }
            else if (zone == 3 || zone == 4)
            { /* commercial */
                v = stencil(0, qy, qx);
                if (64 - dist > 0)
                    v += 64 - dist;
                v -= c->xplt[hy][hx] / 4;
                v -= c->xcrm[hy][hx] / 3;
                v += c->xpop[qy][qx] / 3;
            }
            else
            { /* residential */
                v = stencil(0, qy, qx);
                if (c->xpop[qy][qx] < 0x40)
                    v += 0x15; /* $23B08 */
                if (64 - dist > 0)
                    v += (64 - dist) / 2;
                v -= c->xplt[hy][hx] / 5;
                v -= c->xcrm[hy][hx] / 3;
            }

            b = c->xbld[2 * hy][2 * hx]; /* $23B96 */
            if (b >= BLD_ZONE_FIRST && BUILDING[b].tier_flag == 2)
                v -= v / 2;
            if (v > 255)
                v = 255;
            else if (v < 0)
                v = 0;
            c->xval[hy][hx] = (uint8_t)v;
            c->land_value_tot += v;
        }
    }
}

/* ================================================================== *
 *  $2317E stage 9 ($23FAE) -- crime.
 *
 *  Every input is produced earlier in the same pass, so a saved city
 *  holds exactly the values this stage saw.  That makes crime, unlike
 *  pollution and land value, reproducible from a save.
 *
 *      crime = density - police/2 - landValue/4  (+16 under one
 *              ordinance, which raises crime rather than lowering it)
 *
 *  then a 5-point blur, clamped to a byte.  Cells the developed mask
 *  does not cover are zeroed outright.
 * ================================================================== */
void sim_crime(City *c)
{
    static uint8_t mask[HALF_H][HALF_W];
    int            hy, hx, y, x;

    /*  Stage 4 marks this mask into XBIT bit 3, but that bit is also the
     *  flood-fill scratch and both flood passes clear it, so whether a
     *  save still carries it depends on when the save happened.  Derive
     *  it from the map instead -- same condition, $235DA. */
    memset(mask, 0, sizeof mask);
    for (y = 0; y < MAP_H; y++)
        for (x = 0; x < MAP_W; x++)
            if (c->xbld[y][x] >= 0x1D || XZON_TYPE(c->xzon[y][x]))
                mask[y / 2][x / 2] = 1;

    for (hy = 0; hy < HALF_H; hy++)
    {
        int qy = hy / 2;
        for (hx = 0; hx < HALF_W; hx++)
        {
            int     qx = hx / 2;
            int32_t v;
            if (!mask[hy][hx])
            { /* $23FCA */
                pad[hy][hx] = 0;
                continue;
            }
            v = c->xpop[qy][qx];      /* $23FF0 */
            v -= c->xplc[qy][qx] / 2; /* $2401E */
            v -= c->xval[hy][hx] / 4; /* $24044 */
            if (c->ordinances & 0x04)
                v += 0x10; /* $2404E */
            pad[hy][hx] = (int16_t)v;
        }
    }

    c->crime_tot = 0;
    for (hy = 0; hy < HALF_H; hy++)
    {
        for (hx = 0; hx < HALF_W; hx++)
        {
            int32_t s = pad[hy][hx];
            int     n = 1;
            if (hy > 0)
            {
                s += pad[hy - 1][hx];
                n++;
            }
            if (hy < HALF_H - 1)
            {
                s += pad[hy + 1][hx];
                n++;
            }
            if (hx > 0)
            {
                s += pad[hy][hx - 1];
                n++;
            }
            if (hx < HALF_W - 1)
            {
                s += pad[hy][hx + 1];
                n++;
            }
            s /= n;
            if (s > 255)
                s = 255;
            else if (s < 0)
                s = 0;
            c->xcrm[hy][hx] = (uint8_t)s;
            c->crime_tot += s;
        }
    }
}

/* ================================================================== *
 *  $2317E stages 6-8 ($23C3A .. $23F3C) -- police, fire and density.
 *
 *  These three layers come out of one walk of the map, so they are one
 *  function here too.  The walk first clears the 32x32 scratch plane
 *  and both coverage layers, then for every tile
 *
 *    - a developed zone building adds its population to the scratch
 *      plane and, under the two service ordinances, two points of free
 *      coverage to the cell it stands in;
 *    - anything above the zone range adds a flat 2 (12 for an arcology)
 *      and, if it is a police or fire station standing on its corner
 *      tile, stamps a coverage diamond around itself.
 *
 *  Density is then four times the scratch plane.  The station radius is
 *  what made this the last blocked stage: it scales with the
 *  department's funding level, which lives in the budget block, and the
 *  budget block was not decoded until the MISC unpacker at $295D6 was
 *  run under the interpreter (tools/miscload.py).
 * ================================================================== */

/*  $241B2 -- add to one cell of a coverage layer, clamped to a byte.
 *  Off-map cells are dropped rather than wrapped. */
static void coverage_point(uint8_t plane[QTR_H][QTR_W], int y, int x, int16_t amount)
{
    int32_t v;

    if (y < 0 || x < 0 || y >= QTR_H || x >= QTR_W)
        return; /* $241C4 */
    v = (int32_t)plane[y][x] + amount;
    if (v > 255)
        v = 255;
    else if (v < 0)
        v = 0;
    plane[y][x] = (uint8_t)v;
}

/*  $24232 -- stamp the diamond.  The original writes out all thirty-odd
 *  calls by hand; COVERAGE_KERNEL is the same list, recovered by running
 *  it.  Each ring step is done in sixteen bits, so a station funded hard
 *  enough to overflow the intermediate really does wrap, and that is
 *  reproduced rather than smoothed over. */
static void coverage_spread(uint8_t plane[QTR_H][QTR_W], int y, int x, int16_t s)
{
    int16_t ring[COVERAGE_RINGS];
    int     i;

    ring[0] = s;
    ring[1] = (int16_t)((int16_t)(s * 4) / 5);
    ring[2] = (int16_t)((int16_t)(3 * ring[1]) / 4);
    ring[3] = (int16_t)((int16_t)(2 * ring[2]) / 3);
    ring[4] = (int16_t)(ring[3] / 2);

    for (i = 0; i < COVERAGE_KERNEL_LEN; i++)
        coverage_point(plane, y + COVERAGE_KERNEL[i].dy, x + COVERAGE_KERNEL[i].dx, ring[COVERAGE_KERNEL[i].ring]);
}

/*  A station that has lost its power covers half as far.  $23E4A */
static int16_t station_range(int32_t product, int powered)
{
    int16_t s = (int16_t)(product / 2);
    if (!powered)
        s = (int16_t)((int32_t)s / 2);
    return s;
}

void sim_coverage(City *c)
{
    static int16_t acc[QTR_H][QTR_W]; /* A5+0x13BA at quarter res */
    int            y, x, qy, qx;

    memset(acc, 0, sizeof acc); /* $23C46 */
    memset(c->xplc, 0, sizeof c->xplc);
    memset(c->xfir, 0, sizeof c->xfir);

    /*  1 .. 0x7E.  The first and last row and column are skipped, which
     *  is the loop the original writes ($23CB2 sets 1, $23ED0 stops
     *  below 0x7F), not an off-by-one here. */
    for (y = 1; y < MAP_H - 1; y++)
    {
        for (x = 1; x < MAP_W - 1; x++)
        {
            uint8_t b = c->xbld[y][x];

            qy = y / 4;
            qx = x / 4;

            if (b >= BLD_ZONE_FIRST && b < BLD_POWER_FIRST)
            { /* $23CFC */
                acc[qy][qx] = (int16_t)(acc[qy][qx] + BUILDING[b].population);
                if ((c->ordinances & ORD_NEIGHBOURHOOD_WATCH) /* $23D2E */
                    && c->xplc[qy][qx] < 0xFE)
                    c->xplc[qy][qx] = (uint8_t)(c->xplc[qy][qx] + 2);
                if ((c->ordinances & ORD_VOLUNTEER_FIRE) /* $23D56 */
                    && c->xfir[qy][qx] < 0xFE)
                    c->xfir[qy][qx] = (uint8_t)(c->xfir[qy][qx] + 2);
                continue;
            }
            if (b < BLD_POWER_FIRST)
                continue; /* $23D86 */

            acc[qy][qx] = (int16_t)(acc[qy][qx] + ((b >= 0xFB && b <= 0xFE) ? 12 : 2));

            /*  Only the corner tile carrying XZON bit 0x80 spreads, so a
             *  3x3 station is stamped once and not nine times. $23DEE */
            if (!(c->xzon[y][x] & 0x80))
                continue;

            if (b == 0xD2)
            { /* $23E0C */
                int32_t r = (int32_t)(int16_t)(c->police_term + 5) * c->dept[DEPT_POLICE].funding;
                coverage_spread(c->xplc, qy, qx, station_range(r, c->xbit[y][x] & XBIT_POWERED));
            }
            else if (b == 0xD3)
            { /* $23E6C */
                int32_t r = c->dept[DEPT_FIRE].funding * 5;
                coverage_spread(c->xfir, qy, qx, station_range(r, c->xbit[y][x] & XBIT_POWERED));
            }
        }
    }

    /*  $23EE4.  The multiply by four is a word shift and the clamp is
     *  one sided -- the original never floors the result at zero. */
    for (qy = 0; qy < QTR_H; qy++)
    {
        for (qx = 0; qx < QTR_W; qx++)
        {
            int32_t v = (int16_t)(acc[qy][qx] * 4);
            int32_t d, r;
            if (v > 255)
                v = 255;

            /*  $23F30 -- the rate of growth, before the new density
             *  overwrites the old.  It is an average of the CHANGE,
             *  weighted seven to one toward the running value, and it
             *  is offset by 128 so that a quarter holding steady reads
             *  as the middle of the range rather than as zero. */
            d = (v - c->xpop[qy][qx]) * 8 + 128;        /* $23F32 */
            r = (int16_t)(c->xrog[qy][qx] * 7 + d) / 8; /* $23F52 */
            if (r < 0)
                r = 0; /* $23F6A */
            if (r > 255)
                r = 255; /* $23F72 */

            c->xpop[qy][qx] = (uint8_t)v; /* $23F3C */
            c->xrog[qy][qx] = (uint8_t)r; /* $23F7C */
        }
    }
}

/* ================================================================== *
 *  $3258A / $33028 / $32830 -- putting a building on the map.
 *
 *  All three end in the same place: pick an id out of a group and stamp
 *  it down.  The groups are five kinds by four tiers, and which variant
 *  inside the group is chosen is random -- except for the smallest
 *  residential group, where land value decides which third of the group
 *  to draw from, so poor land gets the shacks and rich land the houses.
 * ================================================================== */

/*  How many of the structural-change routines this scan wanted that are
 *  not reconstructed yet.  Counted rather than ignored, so the coverage
 *  figure in the report is measured and not asserted. */
static int32_t growth_todo;
static int32_t growth_stub[8];
int32_t        sim_growth_stub(int i) { return growth_stub[i & 7]; }

/* ================================================================== *
 *  $EEAE  allocMicro -- hands a building an XMIC slot, the eight-byte
 *  record that holds a stadium's team or an arcology's stage.  Returns
 *  the slot index, which $3590 writes into XTXT.
 *
 *  NOT RECONSTRUCTED.  It reads a kind table at A5-0x5D42 indexed by
 *  bld - 0xC6, then linear-probes XMIC for a free eight-byte record.
 *  It draws no randoms, so leaving it out cannot shift the RNG stream
 *  and cannot move XBLD, XZON, XBIT or XTRF -- the four layers the
 *  growth oracle compares.  What it costs is the XTXT label byte. */
/*  XMIC lives with the year-end pass at the end of this file; the
 *  allocator here needs the same four helpers. */
static uint8_t *micro_rec(const City *c, int i);
static int      micro_w(const uint8_t *r, int k);
static void     micro_set_w(uint8_t *r, int k, int v);
static int      micro_cap(const City *c, int want, int per);

/*  $FD28.  What a brand-new record starts life holding.  A fixed-slot
 *  building ADDS to a record shared with every other copy of itself; a
 *  slot of its own is cleared first and then filled.
 *
 *  The seven plant figures are the game's megawatt ratings -- gas 50,
 *  oil 220, nuclear 500, solar 50, microwave 1600, fusion 2500, coal
 *  200, with hydro worth 20 apiece and wind 4 -- which is a useful check
 *  that the id chain has been read the right way round. */
static void micro_init(City *c, int slot, int bld)
{
    uint8_t *r    = micro_rec(c, slot);
    int      year = (int)(c->years + c->year_founded);
    if (!r)
        return;
    if (MICRO_CLASS[bld - 0xC6] >= 0x11) /* $FD48, a shared record */
    {
        switch (bld)
        {
            case 0xC6:
            case 0xC7: /* hydro: 20 MW each */
                micro_set_w(r, 0, micro_w(r, 0) + 1);
                micro_set_w(r, 1, micro_w(r, 1) + 20);
                break;
            case 0xC8: /* wind: 4 MW each */
                micro_set_w(r, 0, micro_w(r, 0) + 1);
                micro_set_w(r, 1, micro_w(r, 1) + 4);
                break;
            case 0xD5: /* a park adds nine to its acreage */
                micro_set_w(r, 1, micro_w(r, 1) + 9);
                break;
            case 0xE9: /* subway, bus, rail: one more of them */
            case 0xEC:
            case 0xED:
                micro_set_w(r, 0, micro_w(r, 0) + 1);
                break;
            default:
                break;
        }
        return;
    }

    /*  $FE0C -- a slot of its own: cleared, then filled by type. */
    r[1] = 0;
    micro_set_w(r, 0, 0);
    micro_set_w(r, 1, 0);
    micro_set_w(r, 2, 0);
    switch (bld)
    {
        case 0xC9:
            micro_set_w(r, 0, 50);
            break; /* gas       */
        case 0xCA:
            micro_set_w(r, 0, 220);
            break; /* oil       */
        case 0xCB:
            micro_set_w(r, 0, 500);
            break; /* nuclear   */
        case 0xCC:
            micro_set_w(r, 0, 50);
            break; /* solar     */
        case 0xCD:
            micro_set_w(r, 0, 1600);
            break; /* microwave */
        case 0xCE:
            micro_set_w(r, 0, 2500);
            break; /* fusion    */
        case 0xCF:
            micro_set_w(r, 0, 200);
            break; /* coal      */

        case 0xD0: /* city hall, $FF58 */
            micro_set_w(r, 0, micro_cap(c, 0xC8, 0x384));
            micro_set_w(r, 1, year);
            break;
        case 0xD2: /* police, $FF8E */
            micro_set_w(r, 0, micro_cap(c, (int)(c->dept[DEPT_POLICE].funding * 2), 0x5A));
            break;
        case 0xD3: /* fire, $FFB8 */
            micro_set_w(r, 0, micro_cap(c, (int)ASR(c->dept[DEPT_FIRE].funding, 1), 0x46));
            micro_set_w(r, 1, 4); /* $FFE6, one crew to start */
            break;
        case 0xD4: /* museum, $FFF2 */
            r[1] = 100;
            break;
        case 0xD1: /* hospital, school, college: $10060 */
        case 0xD6:
        case 0xD9:
            r[1] = 6;
            break;
        case 0xDB: /* a statue remembers the year, $10090 */
            micro_set_w(r, 0, year);
            break;
        case 0xF3: /* the mayor's house, $10006 */
            micro_set_w(r, 0, year);
            micro_set_w(r, 1, (int)((uint16_t)Random() % 30) + 10); /* $1001E */
            micro_set_w(r, 2, (int)((uint16_t)Random() % 60));      /* $10040 */
            break;
        /*  The four arcologies, $100AA/$100E4/$1011E/$1015A: a starting
         *  population, a life of five, and the YEAR they went up, which
         *  is what the Launch Arco's ending counts from.  The llama dome
         *  keeps its year the same way ($1018E). */
        case 0xFB:
            micro_set_w(r, 0, 0x37);
            r[1] = 5;
            micro_set_w(r, 2, year);
            break;
        case 0xFC:
            micro_set_w(r, 0, 0x1E);
            r[1] = 5;
            micro_set_w(r, 2, year);
            break;
        case 0xFD:
            micro_set_w(r, 0, 0x2D);
            r[1] = 5;
            micro_set_w(r, 2, year);
            break;
        case 0xFE:
            micro_set_w(r, 0, 0x41);
            r[1] = 5;
            micro_set_w(r, 2, year);
            break;
        case 0xFF:
            micro_set_w(r, 2, year);
            break;
        default:
            break;
    }
}

/* ================================================================== *
 *  $EEAE  allocMicro -- give a newly placed special building its XMIC
 *  record, and answer the marker the caller writes into XTXT.
 *
 *  Which slot it gets is a table, not a search: `MICRO_CLASS` maps the
 *  building id to zero (no record at all -- runways and cranes are
 *  here, which is why the growth scan never allocates one), to a FIXED
 *  slot shared with every other copy of that building, or to "take the
 *  first free slot from ten up".
 *
 *  When they are all taken the table is not simply full: an arcology
 *  gives up, and anything else EVICTS the first record below 0xFB and
 *  scrubs its marker off the map, so the arcologies outlive everything
 *  else in the table.
 *
 *  The marker is the slot plus 0x33, which is what micro_find_tile
 *  searches XTXT for.
 * ================================================================== */
int sim_alloc_micro(City *c, int y, int x, int bld)
{
    int slot, cls;
    (void)y;
    (void)x;
    if (bld < 0xC6 || bld > 0xFF) /* $EEBC */
        return 0;
    cls = MICRO_CLASS[bld - 0xC6];
    if (cls == 0) /* $EED8, this building keeps no record */
        return 0;

    if (cls >= 0x11)
        slot = cls - 16; /* $EEE8, `moveq #$f0` is -16 */
    else
    {
        for (slot = 10; slot < N_MICRO; slot++) /* $EEF2 */
        {
            const uint8_t *r = micro_rec(c, slot);
            if (!r || r[0] == 0)
                break;
        }
    }

    if (slot >= N_MICRO) /* $EF0C, the table is full */
    {
        if (bld >= 0xFB)
            return 0; /* $EF16, an arcology does not evict */
        for (slot = 10; slot < N_MICRO; slot++)
        {
            const uint8_t *r = micro_rec(c, slot);
            if (!r || r[0] < 0xFB)
                break; /* $EF30 */
        }
        if (slot >= N_MICRO)
            return 0; /* $EF88 */
        /*  $EF4C -- and the evicted record's marker comes off the map,
         *  or a tile would point at a record that is now somebody
         *  else's. */
        {
            int yy, xx;
            for (yy = 0; yy < MAP_H; yy++)
                for (xx = 0; xx < MAP_W; xx++)
                    if (c->xtxt[yy][xx] == (uint8_t)(slot + 0x33))
                        c->xtxt[yy][xx] = 0;
        }
    }

    {
        uint8_t *r = micro_rec(c, slot);
        if (!r)
            return 0;
        r[0] = (uint8_t)bld; /* $EFA0 */
        micro_init(c, slot, bld);
    }

    /*  $EFB6 -- the default name, unless this is a shared record that
     *  has been named already. */
    if (c->xlab && (size_t)((slot + 0x33) * 25 + 16) <= c->xlab_len)
    {
        uint8_t *lab = c->xlab + (slot + 0x33) * 25;
        if (!(cls >= 0x11 && lab[0] != 0))
        {
            int k;
            for (k = 0; k < 16; k++)
                lab[k] = MICRO_LABEL[(bld - 0xC6) * 16 + k];
        }
    }
    return slot + 0x33; /* $F00C */
}

/* ================================================================== *
 *  $324B8  nearPowered -- is this tile, or any of its four orthogonal
 *  neighbours, supplied with power?  XBIT bit $40.
 *
 *  (Not a water test: $40 is XBIT_POWERED, set by the power flood at
 *  $20FC4.  Water covered is bit $04, which is what $3590 rejects.)
 *  The edge guards are asymmetric in the original: it looks one row up
 *  only from row 2, and one row down only to row 126.
 * ================================================================== */
static int near_powered(const City *c, int y, int x)
{
    if (c->xbit[y][x] & XBIT_POWERED)
        return 1; /* $324D8 */
    if (y > 1 && (c->xbit[y - 1][x] & XBIT_POWERED))
        return 1; /* $324FC */
    if (x > 1 && (c->xbit[y][x - 1] & XBIT_POWERED))
        return 1; /* $32524 */
    if (y < 127 && (c->xbit[y + 1][x] & XBIT_POWERED))
        return 1; /* $3254C */
    if (x < 127 && (c->xbit[y][x + 1] & XBIT_POWERED))
        return 1; /* $32576 */
    return 0;     /* $32582 */
}

/* ================================================================== *
 *  $43A2  setUnder -- write the underground layer and keep the transit
 *  counter straight.  A tile counts toward transit if its value is
 *  1..15, or one of $1F, $20, $22, $23; the old value is subtracted and
 *  the new one added.  Tiles inside a military zone are exempt from the
 *  bookkeeping but still get written.
 * ================================================================== */
static void set_under(City *c, int y, int x, uint8_t und)
{
    static const int counts[] = {0x1F, 0x20, 0x22, 0x23};
    int              i;

    if ((c->xzon[y][x] & 0x0F) != 7) /* $43CC */
    {
        uint8_t old = c->xund[y][x];
        int     o   = (old >= 1 && old < 0x10);
        int     n   = (und >= 1 && und < 0x10);
        for (i = 0; i < 4; i++)
        {
            if (old == counts[i])
                o = 1;
            if (und == counts[i])
                n = 1;
        }
        if (o)
            c->transit_term--; /* $4408 */
        if (n)
            c->transit_term++; /* $4430 */
    }
    c->xund[y][x] = und; /* $443A */
}

/* ================================================================== *
 *  $EE3C  releaseLabel -- give back whatever record the XTXT byte of a
 *  tile was pointing at.  The byte says which kind:
 *      $C9 .. $F0   a moving object; clear the XTXT it had saved
 *      $34 .. $FF   below $C9, a microsim record and its label
 *      $01 .. $32   a sign, so only the label
 *      $33 .. $3C   nothing at all
 * ================================================================== */
static void release_label(City *c, int v)
{
    if (v == 0)
        return;

    if (v >= 0xC9) /* $EE4A */
    {
        if (v >= 0xF1)
            return;
        if (c->xthg && (size_t)((v - 0xC9) * 12 + 0x0A) < c->xthg_len) /* 12 = THING_SZ */
            c->xthg[(v - 0xC9) * 12 + 0x0A] = 0;                       /* $EE6C */
        return;
    }
    if (v >= 0x33) /* $EE72 */
    {
        if (v <= 0x3C)
            return;
        if (c->xmic && (size_t)((v - 0x33) * 8) < c->xmic_len)
            c->xmic[(v - 0x33) * 8] = 0; /* $EE88 */
    }
    if (c->xlab && (size_t)(v * 0x19) < c->xlab_len)
        c->xlab[v * 0x19] = 0; /* $EE94 and $EEA6 */
}

/* ================================================================== *
 *  $5FAA  demolishAndPlace -- take a building off the map.
 *
 *  Most of its 5,680 bytes animate the collapse.  All of the change to
 *  the city happens in the loop at $73A4, which walks the footprint the
 *  size table gives and does the same six things to every tile:
 *
 *      the new building is rubble, $01 to $04 by a coin, unless the
 *          tile stands on sloped or watered ground, where it goes bare
 *      XBIT keeps only bits 0, 2, 3, 4 and 5, so conducting, powered
 *          and bit 1 all go
 *      XZON keeps its zone nibble and loses its corner marker
 *      the XTXT byte is cleared, unless it names a moving object
 *      whatever that byte pointed at is released
 *      a tile that was burnt out is taken off the burnt tally
 *
 *  Then the terrain around the footprint is put back in order.
 * ================================================================== */
/*  $68D0 and $6BD4 -- four building ids are not square.  A runway or a
 *  pier is a run of identical tiles, so those are taken down by
 *  flooding over the neighbours that carry the same id rather than by
 *  walking a footprint.  Nothing marks a tile as visited: the rubble
 *  written into XBLD no longer matches, which is what stops the walk.
 *
 *  The queue is popped from the back, so the flood is depth first. */
static void demolish_run(City *c, int y, int x, int a, int b, int rubble, int even_bare)
{
    q_reset();         /* $21DD4 clears both ends */
    q_push(y, x);      /* and pushes the first point */
    while (!q_empty()) /* $21EBE */
    {
        int r, cx;
        q_pop_back(&r, &cx); /* $68D4 */

        sim_set_tile(c, r, cx, rubble ? (uint8_t)(1 + (Random() & 3)) : 0); /* $6BF4 */
        c->xzon[r][cx] = (uint8_t)(c->xzon[r][cx] & 0x0F);                  /* $6908 */
        c->xbit[r][cx] = (uint8_t)(c->xbit[r][cx] & 0x3D);                  /* $6922 */

        /*  $6926 and $6C36 -- the collapse animation, two draws a tile. */
        if (even_bare)
        {
            (void)Random();
            (void)Random();
        }

        if (r > 0 && (c->xbld[r - 1][cx] == a || c->xbld[r - 1][cx] == b))
            q_push(r - 1, cx); /* $6D1E */
        if (r < 0x7F && (c->xbld[r + 1][cx] == a || c->xbld[r + 1][cx] == b))
            q_push(r + 1, cx); /* $6D68 */
        if (cx > 0 && (c->xbld[r][cx - 1] == a || c->xbld[r][cx - 1] == b))
            q_push(r, cx - 1); /* $6DB2 */
        if (cx < 0x7F && (c->xbld[r][cx + 1] == a || c->xbld[r][cx + 1] == b))
            q_push(r, cx + 1); /* $6DFC */
    }
}

/* ================================================================== *
 *  $1D322  classifyPair -- what kind of two-by-two structure stands on
 *  a tile.  It answers $FF for anything that is not a two-by-two id at
 *  all, and otherwise a small code.  $5FAA only cares whether the code
 *  reaches $0D, which marks the raised pieces: those are demolished as
 *  a run along their line rather than as one footprint.
 * ================================================================== */
static int classify_2x2(const City *c, int y, int x)
{
    int b;

    if (y < 0 || y >= MAP_H || x < 0 || x >= MAP_W)
        return 0; /* $1D34A */

    b = c->xbld[y][x];
    if (!((b >= 0x61 && b < 0x6C) || (b >= 0x49 && b < 0x51)))
        return -1; /* $1D37C -- moveq #$ff is -1, and the caller's
                    *  compare is signed, so this is NOT $FF */

    if ((c->xzon[y][x] & 0xF0) != 0xF0) /* $1D398 */
    {
        const int code = b - 0x5D; /* $1D502 */
        if (code < 0x0D)
            return code;                                 /* $1D530, and it can be negative */
        return (c->xbit[y][x + 1] & 0x02) ? 0x10 : 0x0F; /* $1D520 */
    }

    /*  $1D3A0 -- a single-tile marker, so look at the four tiles of the
     *  pair in turn: the first that is a bridge piece, or that stands on
     *  water, decides. */
    {
        static const int DY[4] = {0, 1, 1, 0};
        static const int DX[4] = {0, 0, 1, 1};
        int              i;
        for (i = 0; i < 4; i++)
        {
            const int ty = y + DY[i], tx = x + DX[i];
            if (ty < 0 || ty >= MAP_H || tx < 0 || tx >= MAP_W)
                continue;
            b = c->xbld[ty][tx];
            if (b >= 0x4B && b < 0x51)
                return b & 1; /* $1D3AC */
            if (c->xbit[ty][tx] & XBIT_WATER)
                return (b == 0x49) ? 0x0D : 0x0E; /* $1D3D4 */
        }
        if (b >= 0x49 && b < 0x4B)
            return (b & 1) + 2; /* $1D4F4 */
        return -1;              /* $1D4FE, moveq #$ff again */
    }
}

/*  $621C and $6794 -- put the terrain back under all four tiles. */
static void fix_pair(City *c, int y, int x)
{
    sim_fix_terrain(c, y, x);         /* $6250 */
    sim_fix_terrain(c, y + 1, x);     /* $6270 */
    sim_fix_terrain(c, y + 1, x + 1); /* $6298 */
    sim_fix_terrain(c, y, x + 1);     /* $62C0 */
}

/*  $6418 with $653E -- clear all four tiles of the pair. */
static void clear_pair(City *c, int y, int x)
{
    static const int DY[4] = {0, 0, 1, 1};
    static const int DX[4] = {0, 1, 1, 0};
    int              i;
    for (i = 0; i < 4; i++)
    {
        const int ty = y + DY[i], tx = x + DX[i];
        if (ty < 0 || ty >= MAP_H || tx < 0 || tx >= MAP_W)
            continue;
        sim_set_tile(c, ty, tx, 0);                           /* $6422 */
        c->xzon[ty][tx] = (uint8_t)(c->xzon[ty][tx] & 0x0F);  /* $6442 */
        c->xbit[ty][tx] = (uint8_t)(c->xbit[ty][tx] & ~0x02); /* $645C */
    }
}

/* ================================================================== *
 *  $6046 -- the raised two-by-two pieces, the elevated rail and road
 *  that cross water.  Like a bridge they are a run, not a footprint,
 *  so the walk goes back to the start of the line and then forward,
 *  clearing a pair at a time.  Which way the line runs comes from the
 *  low bit of the code the tile to the left reports.
 * ================================================================== */
static void demolish_pair_run(City *c, int y, int x, int bld, int even_bare)
{
    const int d4 = classify_2x2(c, y, x - 1); /* $6050 */
    const int dy = (d4 & 1) ? 0 : 1;          /* $60A8 */
    const int dx = (d4 & 1) ? 1 : 0;

    x--; /* $6076 */

    while (classify_2x2(c, y, x) >= 0x0D) /* $60D0 */
    {
        if (y - dy * 2 < 0 || x - dx * 2 < 0)
            break;
        y -= dy * 2; /* $60BC */
        x -= dx * 2;
    }

    if (bld != 0x49 && bld != 0x4A)
        fix_pair(c, y, x); /* $6226 */

    for (;;)
    {
        if (y + dy * 2 >= MAP_H || x + dx * 2 >= MAP_W)
            break;
        y += dy * 2; /* $664A */
        x += dx * 2;
        if (classify_2x2(c, y, x) < 0x0D)
            break; /* $665E */
        /*  five draws a step here, not the bridge's two: the pair has
         *  three more debris blocks at $6478, $64BA and $64FC. */
        if (even_bare)
        {
            (void)Random(); /* $637E */
            (void)Random(); /* $63EA */
            (void)Random(); /* $6478 */
            (void)Random(); /* $64BA */
            (void)Random(); /* $64FC */
        }
        clear_pair(c, y, x);
    }

    if (bld != 0x49 && bld != 0x4A)
        fix_pair(c, y, x); /* $679E */
}

/*  $606E -- a bridge is a run of tiles over water, not a footprint.
 *  Taking one down means putting the water back: every tile of the run
 *  is cleared, and the land tile at each end is dropped a level and
 *  flooded.  The run's direction comes from bit 1 of XBIT, which is the
 *  orientation flag a two-form tile needs.
 *
 *  Neither walk is bounded in the original.  A bridge always has land
 *  at both ends, so it stops; the guards here only keep the C inside
 *  its arrays. */
static int is_bridge(int b)
{
    return (b >= 0x51 && b < 0x5D) || b == 0x6A || b == 0x6B; /* $66A6 */
}

static void sink_bridge_end(City *c, int y, int x)
{
    if (c->xbit[y][x] & XBIT_WATER)
        return;               /* $6704, already water */
    sim_set_tile(c, y, x, 0); /* $6724 */
    c->altm[y][x] =
        (uint16_t)((c->altm[y][x] & ~(uint16_t)0x1F) |
                   (unsigned)((c->altm[y][x] & 0x1F) - 1)); /* $6764 */
    c->xbit[y][x] |= XBIT_WATER;                            /* $677E */
    sim_fix_terrain(c, y, x);                               /* $678A */
}

static void demolish_bridge(City *c, int y, int x, int even_bare)
{
    const int dy = (c->xbit[y][x] & 0x02) ? 1 : 0; /* $607A */
    const int dx = (c->xbit[y][x] & 0x02) ? 0 : 1;

    while (is_bridge(c->xbld[y][x])) /* $60D0, back off the front */
    {
        if (y - dy < 0 || x - dx < 0)
            break;
        y -= dy; /* $60BC */
        x -= dx;
    }

    sink_bridge_end(c, y, x);        /* $6158 */
    c->xbit[y][x] &= (uint8_t)~0x02; /* $6214, only on this end */

    for (;;) /* $62DC, forward over the run */
    {
        if (y + dy >= MAP_H || x + dx >= MAP_W)
            break;
        y += dy;
        x += dx;
        if (!is_bridge(c->xbld[y][x]))
        {
            sink_bridge_end(c, y, x); /* $66E2, the far end */
            break;
        }
        /*  $6374 -- with the collapse shown, each tile of the run draws a
         *  debris shape and a mirror flag before it is cleared. */
        if (even_bare)
        {
            (void)Random(); /* $637E */
            (void)Random(); /* $63EA */
        }
        sim_set_tile(c, y, x, 0);                         /* $6422 */
        c->xzon[y][x] = (uint8_t)(c->xzon[y][x] & 0x0F);  /* $6442 */
        c->xbit[y][x] = (uint8_t)(c->xbit[y][x] & ~0x02); /* $645C */
    }
}

void sim_demolish_and_place(City *c, int y, int x, int even_bare)
{
    const int oy = y, ox = x; /* $5FB2, before $763A moves them */
    int       bld = c->xbld[y][x];
    int       n, dy, dx;

    (void)even_bare; /* $75C6 only uses it to ask for a redraw */

    if (bld < 6)
        return; /* $5FD2 */

    n = sim_footprint_origin(c, &y, &x, bld); /* $5FEE */

    /*  $6026 -- a single-tile bridge id is a run over water. */
    if (n == 1 && ((bld >= 0x51 && bld < 0x5D) || bld == 0x6A || bld == 0x6B))
    {
        demolish_bridge(c, oy, ox, even_bare);
        return;
    }

    /*  $6046 -- a raised pair is a run along its line, not a footprint. */
    if (n == 2 && classify_2x2(c, y, x - 1) >= 0x0D)
    {
        demolish_pair_run(c, y, x, bld, even_bare);
        return;
    }

    /*  $6898 and $6B9C, before the footprint walk.  These two pairs are
     *  runs rather than squares.  $DF and $E0 leave bare ground, $DD and
     *  $DE leave rubble. */
    if (bld == 0xDF || bld == 0xE0)
    {
        demolish_run(c, oy, ox, 0xDF, 0xE0, 0, even_bare);
        return;
    }
    if (bld == 0xDD || bld == 0xDE)
    {
        demolish_run(c, oy, ox, 0xDD, 0xDE, 1, even_bare);
        return;
    }

    /*  $71A4 -- when the caller asked for the collapse to be shown, the
     *  debris is animated for n frames over the n by n footprint, and
     *  each tile of each frame draws a shape and a mirror flag.  The
     *  animation changes nothing, but it takes 2 * n^3 numbers from the
     *  generator every other decision downstream depends on. */
    if (even_bare)
        for (dy = 0; dy < n * n * n; dy++)
        {
            (void)Random(); /* $71CA, the debris shape */
            (void)Random(); /* $71FA, the mirror */
        }

    for (dy = 0; dy < n; dy++)     /* $7518 */
        for (dx = 0; dx < n; dx++) /* $750E */
        {
            const int r  = y + dy;
            const int cx = x - dx; /* $73AE, the origin is the right edge */
            int       v;

            if (r < 0 || r >= MAP_H || cx < 0 || cx >= MAP_W)
                continue;

            if (c->xter[r][cx] > 0)
                sim_set_tile(c, r, cx, 0); /* $73DE, no rubble on a slope */
            else
                sim_set_tile(c, r, cx, (uint8_t)(1 + (Random() & 3))); /* $7406 */

            c->xbit[r][cx] = (uint8_t)(c->xbit[r][cx] & 0x3D); /* $742A */
            c->xzon[r][cx] = (uint8_t)(c->xzon[r][cx] & 0x0F); /* $7452 */

            v = c->xtxt[r][cx];
            if (v == 0)
                continue; /* $7472 */
            if (v >= 0xF1 && v != 0xFA)
                continue; /* $747A */
            if (v < 0xC9 || v == 0xFA)
                c->xtxt[r][cx] = 0; /* $74AE */
            release_label(c, v);    /* $74B4 */

            if (v == 0xFA) /* $74BA, it had already burnt out */
            {
                if ((bld >= 0x1D && bld < 0x2C) || (bld >= 0x3F && bld < 0x47) ||
                    bld == 0x4B || bld == 0x4C || (bld >= 0x5D && bld < 0x61))
                    c->burnt_road--; /* $7502 */
                else
                    c->burnt_other--; /* $7508 */
            }
        }

    /*  $7520 -- the two-by-two ranges have their terrain put back on all
     *  four tiles.  Everything else gets one tile, and only when it is a
     *  single tile below $70 standing on terrain that is not flat. */
    if ((bld >= 0x61 && bld < 0x6C) || (bld >= 0x49 && bld < 0x51))
    {
        sim_fix_terrain(c, y, x);         /* $7548 */
        sim_fix_terrain(c, y + 1, x);     /* $755A */
        sim_fix_terrain(c, y + 1, x - 1); /* $7570 */
        sim_fix_terrain(c, y, x - 1);     /* $7582 */
    }
    else if (n == 1 && bld < 0x70 && c->xter[y][x]) /* $758C */
        sim_fix_terrain(c, y, x);                   /* $75BE */
}

/* ================================================================== *
 *  $3A000  demolishTile -- what a disaster calls to flatten one tile.
 *
 *  It takes the building down through $5FAA, then optionally scorches
 *  the footprint, then records the tile in the city's worst-problem
 *  slot.  Building ids $3F to $42 are exempt and nothing happens.
 *
 *  The problem code is the zone for an ordinary building, ten for $C6,
 *  and the id less $BD above that.  A rank table at A5-0x10D2 decides
 *  whether the new code displaces the one already recorded.
 * ================================================================== */

/*  A5-0x10D2.  Higher wins.  A seaport or airport ($C7, $C8 -> 8, 9)
 *  outranks everything, and an ordinary zone ranks 1. */
static const uint8_t PROBLEM_RANK[16] = {0, 1, 1, 1, 1, 1, 1, 1, 4, 8, 5, 5, 5, 5, 5, 5};

void sim_demolish_tile(City *c, int y, int x, int flag_c, int scorch)
{
    int bld, zon, n, oy, ox, i, j, code;

    if (y < 0 || y >= MAP_H || x < 0 || x >= MAP_W)
        return; /* $3A010 */

    bld = c->xbld[y][x];
    zon = c->xzon[y][x] & 0x0F;
    if (bld >= 0x3F && bld < 0x43)
        return; /* $3A072 */

    oy = y;
    ox = x;
    n  = sim_footprint_origin(c, &oy, &ox, bld); /* $3A098 */
    sim_demolish_and_place(c, y, x, flag_c);     /* $3A0A8 */

    for (i = 0; i < n; i++)     /* $3A118 */
        for (j = 0; j < n; j++) /* $3A112 */
        {
            const int yy = oy + i;
            const int xx = ox - j; /* $3A0C4 */
            if (yy < 0 || yy >= MAP_H || xx < 0 || xx >= MAP_W)
                continue;
            if (c->xbit[yy][xx] & XBIT_WATER)
                continue; /* $3A0F4 */
            if (!scorch)
                continue;           /* $3A0FA */
            c->xtxt[yy][xx] = 0xFF; /* $3A10A */
        }

    if (bld < 0xC6)
        code = zon; /* $3A124 */
    else if (bld == 0xC6)
        code = 10; /* $3A132 */
    else
        code = bld - 0xBD; /* $3A13A */

    if ((uint16_t)c->worst_problem == 0xFFFF)
        c->worst_problem = (int16_t)code; /* $3A160 */
    else if (code >= 0 && code < 16 && c->worst_problem >= 0 &&
             c->worst_problem < 16 &&
             PROBLEM_RANK[code] > PROBLEM_RANK[c->worst_problem])
        c->worst_problem = (int16_t)code; /* $3A15E */
}

/* ================================================================== *
 *  $128DE  fixTerrain -- put one tile back in order after the land
 *  under it has moved.
 *
 *  First it clears what can no longer stand there: a building of $0D or
 *  more is demolished, the tile is emptied unless its building is 5,
 *  and anything underground goes.
 *
 *  Then it works out the tile's shape.  Each of the eight neighbours
 *  that stands higher raises the corners it touches -- the byte table
 *  at A5-0x4DF6 says which corners those are -- and the four-bit set of
 *  raised corners picks a slope code out of the sixteen at A5-0x4DEE.
 *  Code $32 is not a slope: it means all four corners are higher, so
 *  this tile has to come up a step itself.
 *
 *  Last it decides land or water.  A tile at or above the city's water
 *  level (MISC[912]) loses its water bit and keeps its slope code.  One
 *  below gets the water bit, has the water level written into ALTM bits
 *  5..9, is emptied, and takes a shifted code: $20 plus the slope when
 *  it sits exactly one step under the water line -- the shoreline -- and
 *  $10 plus the slope when it is deeper.
 * ================================================================== */

/*  A5-0x4DF6.  Neighbour i lifts these corners of the tile.  The eight
 *  entries walk the compass from west, so the diagonals name one corner
 *  and the sides name two. */
static const uint8_t SLOPE_CORNER[8] = {3, 2, 6, 4, 12, 8, 9, 1};

/*  A5-0x4DEE, read only through a four-bit index: the slope code for
 *  each set of raised corners.  $32 at the end is the "raise this tile"
 *  answer, not a code. */
static const uint8_t SLOPE_CODE[16] = {0, 9, 10, 2, 11, 13, 3, 6, 12, 1, 13, 5, 4, 8, 7, 0x32};

void sim_fix_terrain(City *c, int y, int x)
{
    int alt, mask = 0, code, i;

    if (y < 0 || y >= MAP_H || x < 0 || x >= MAP_W)
        return; /* $128EE */

    if (c->xbld[y][x] >= 0x0D)
        sim_demolish_and_place(c, y, x, 0xFF); /* $1292C */
    if (c->city_mode != 0 && c->xbld[y][x] != 5)
        sim_set_tile(c, y, x, 0); /* $12946 */
    if (c->xund[y][x])
        set_under(c, y, x, 0); /* $12968 */

    alt = c->altm[y][x] & 0x1F; /* $12984 */
    for (i = 0; i < 8; i++)     /* $129F6 */
    {
        const int ny = y + BEAM_DY[i]; /* A5-0x4F4E */
        const int nx = x + BEAM_DX[i]; /* A5-0x4F3C */
        if (ny < 0 || ny >= MAP_H || nx < 0 || nx >= MAP_W)
            continue;
        if ((c->altm[ny][nx] & 0x1F) > alt)
            mask |= SLOPE_CORNER[i]; /* $129F0 */
    }
    code = SLOPE_CODE[mask & 0x0F]; /* $12A06 */

    if (code != 0)
        c->xzon[y][x] = (uint8_t)(c->xzon[y][x] & 0xF0); /* $12A24 */

    if (code == 0x32) /* $12A26 -- not a slope: the tile itself rises */
    {
        alt = (c->altm[y][x] & 0x1F) + 1; /* $12A56 */
        c->altm[y][x] =
            (uint16_t)((c->altm[y][x] & ~(uint16_t)0x1F) | (unsigned)alt);
        code = 0; /* the code below is written flat, not shifted */

        if (alt >= c->water_level) /* $12A6E */
        {
            c->xbit[y][x] &= (uint8_t)~XBIT_WATER; /* $12A82 */
            c->xter[y][x] = 0;                     /* $12A96 */
            return;
        }
        c->xbit[y][x] |= XBIT_WATER; /* $12AAC */
        c->altm[y][x] = (uint16_t)((c->altm[y][x] & 0xFC1F) |
                                   ((unsigned)c->water_level << 5)); /* $12AD6 */
        if (c->xbld[y][x] != 5)
            sim_set_tile(c, y, x, 0); /* $12AF4 */
        c->xter[y][x] = 0x10;         /* $12B0A */
        return;
    }

    alt = c->altm[y][x] & 0x1F; /* $12B24 */
    if (alt >= c->water_level)  /* $12B30 */
    {
        c->xbit[y][x] &= (uint8_t)~XBIT_WATER; /* $12B44 */
        c->xter[y][x] = (uint8_t)code;         /* $12B58 */
        return;
    }
    c->xbit[y][x] |= XBIT_WATER; /* $12B6E */
    c->altm[y][x] = (uint16_t)((c->altm[y][x] & 0xFC1F) |
                               ((unsigned)c->water_level << 5)); /* $12B9C */
    if (c->xbld[y][x] != 5)
        sim_set_tile(c, y, x, 0); /* $12BBA */
    if (c->water_level - 1 == alt)
        c->xter[y][x] = (uint8_t)(0x20 + code); /* $12BE0, the shoreline */
    else
        c->xter[y][x] = (uint8_t)(0x10 + code); /* $12BF8 */
}

/* ================================================================== *
 *  $12C04  fixNeighbourhood -- put a tile and its eight neighbours
 *  back in order.  The ninth entry of the tables at A5-0x4F4E and
 *  A5-0x4F3C is (0,0), which is how the tile itself is included.
 *
 *  Each tile is redrawn, fixed, and redrawn again ($15A54 either side
 *  of $128DE).  Only the middle call changes any state.
 * ================================================================== */
static const int N9_DY[9] = {0, 1, 1, 1, 0, -1, -1, -1, 0};
static const int N9_DX[9] = {-1, -1, 0, 1, 1, 1, 0, -1, 0};

void sim_fix_neighbourhood(City *c, int y, int x)
{
    int i;
    for (i = 0; i < 9; i++) /* $12C52 */
        sim_fix_terrain(c, y + N9_DY[i], x + N9_DX[i]);
}

/* ================================================================== *
 *  $33EC2  clearTile -- take an existing special off the map before
 *  something else is put there.  Only ids from $C6 up are cleared at
 *  all; $DB..$EA are one tile, everything else is a 2x2 snapped to even
 *  coordinates.  Note it clears the top two XBIT bits (power) and the
 *  high nibble of XZON (the corner markers), leaving the zone kind.
 * ================================================================== */
static void clear_tile(City *c, int y, int x)
{
    int b = c->xbld[y][x];
    int dy, dx;

    if (b < 0xC6)
        return;                 /* $33EEA */
    if (b >= 0xDB && b <= 0xEA) /* $33EF2 / $33EF8 */
    {
        sim_set_tile(c, y, x, 0); /* $33F00 */
        c->xbit[y][x] &= 0x3F;    /* $33F14 */
        c->xzon[y][x] &= 0x0F;    /* $33F30 */
        return;
    }
    y &= ~1; /* $33F36 */
    x &= ~1;
    for (dy = 0; dy < 2; dy++)     /* $33FA0 */
        for (dx = 0; dx < 2; dx++) /* $33F98 */
        {
            sim_set_tile(c, y + dy, x + dx, 0); /* $33F54 */
            c->xbit[y + dy][x + dx] &= 0x3F;    /* $33F72 */
            c->xzon[y + dy][x + dx] &= 0x0F;    /* $33F92 */
        }
}

/* ================================================================== *
 *  $3590  stampFootprint -- the routine that actually puts a
 *  multi-tile building on the map.  Two passes: walk the whole
 *  footprint checking every tile will take it, and only then walk it
 *  again writing.  Nothing is written if any tile fails, so a building
 *  never lands half-placed.
 *
 *  `size` is the footprint edge.  Note $35C6: for anything bigger than
 *  2x2 the anchor is nudged one tile up and left first, so a 3x3 is
 *  centred on the tile it was asked for rather than hanging off it.
 * ================================================================== */
static int stamp_footprint(City *c, int y, int x, int bld, int size)
{
    int span = size - 1; /* $35C6 */
    int yy, xx;
    int flag; /* -$6(a6), the XBIT bits this kind gets */
    int slot; /* -$1(a6), the XMIC index from $EEAE    */

    if (span > 1) /* $35CC -- centre anything bigger than 2x2 */
    {
        y--;
        x--;
    }

    /* ---- pass one: will every tile take it? ---------------------- */
    for (yy = y; yy <= y + span; yy++) /* $36E6 */
    {
        for (xx = x; xx <= x + span; xx++) /* $36DA */
        {
            /*  a footprint with any span keeps one tile clear of the
             *  map edge; a single tile only has to be on the map */
            if (span > 0) /* $35E6 */
            {
                if (yy < 1 || xx < 1 || yy > 126 || xx > 126)
                    return 0;
            }
            else if (yy < 0 || yy >= MAP_H || xx < 0 || xx >= MAP_W)
                return 0; /* $3624 */

            if (c->xbld[yy][xx] >= 0x1D)
                return 0; /* $3642 */
            if (c->xbld[yy][xx] == 0x05)
                return 0; /* $365E */
            if (c->xbld[yy][xx] == 0x0D)
                return 0; /* $367A */
            if (c->xter[yy][xx] != 0)
                return 0; /* $36B6 */
            if (c->xbit[yy][xx] & XBIT_WATER)
                return 0; /* $36D2 */
        }
    }

    /*  $3726 -- roads and one other kind keep the low XBIT bits and
     *  take $20; everything else takes $E0. */
    flag = (bld == 0xD5 || bld == 0x0D) ? 0x20 : 0xE0;

    if (bld == 0x0D && c->xbld[y][x] >= 0x0D)
        return 0; /* $3754 */

    slot = sim_alloc_micro(c, y, x, bld); /* $3764 */

    /* ---- pass two: write ----------------------------------------- */
    for (yy = y; yy <= y + span; yy++) /* $37FC */
    {
        for (xx = x; xx <= x + span; xx++) /* $37F2 */
        {
            c->xbit[yy][xx] = (uint8_t)((c->xbit[yy][xx] & 0x1F) | flag); /* $3796 */
            sim_set_tile(c, yy, xx, (uint8_t)bld);                        /* $37A0 */
            /*  $37B8 then $37D0: the original masks the zone byte to its
             *  high nibble and then masks THAT to its low nibble, which
             *  leaves zero.  Written twice, through two different
             *  address registers pointing at the same byte. */
            c->xzon[yy][xx] = 0;
            if (slot != 0)
                c->xtxt[yy][xx] = (uint8_t)slot; /* $37EC */
        }
    }

    /*  $380A -- the corner markers.  A single tile just gets $F0 in its
     *  high nibble; a real footprint gets four different corner codes
     *  out of a rotation-indexed table, so the renderer knows which way
     *  round the building is drawn. */
    if (span == 0)
        c->xzon[y][x] = (uint8_t)((c->xzon[y][x] & 0x0F) | 0xF0); /* $382E */
    else
    {
        const int16_t *r     = &ROT_CORNER_4[c->rotation * 4];
        c->xzon[y][x]        = (uint8_t)((c->xzon[y][x] & 0x0F) | r[0]); /* $3866 */
        c->xzon[y + span][x] = (uint8_t)((c->xzon[y + span][x] & 0x0F) | r[1]);
        c->xzon[y + span][x + span] =
            (uint8_t)((c->xzon[y + span][x + span] & 0x0F) | r[2]);
        c->xzon[y][x + span] = (uint8_t)((c->xzon[y][x + span] & 0x0F) | r[3]);
    }
    return 0xFF; /* $3920 */
}

/* ================================================================== *
 *  The moving-object table, XTHG: forty twelve-byte records.  Slot 0 is
 *  never used -- $9DDA starts its search at 1 and treats 40 as "full".
 *
 *  Record layout, as $B0BC fills it:
 *      +0  kind        $0A locomotive, $0B carriage
 *      +1  direction   0..3, from $E18E
 *      +2  next        slot index of the next car, 0 to end the chain
 *      +3  y           where it is
 *      +4  x
 *      +5  0
 *      +6  y2          the tile ahead (the locomotive only)
 *      +7  x2
 *      +A  the XTXT byte this record displaced
 * ================================================================== */
#define THING_N  40
#define THING_SZ 12

/*  $9DDA  allocThing -- first free record, or 0 when the table is full. */
static int alloc_thing(City *c)
{
    int i;
    if (!c->xthg || c->xthg_len < (size_t)(THING_N * THING_SZ))
        return 0;
    for (i = 1; i < THING_N; i++) /* $9DF4 */
        if (c->xthg[i * THING_SZ] == 0)
            break;
    return i == THING_N ? 0 : i; /* $9DFA */
}

static uint8_t *thing(City *c, int slot) { return c->xthg + slot * THING_SZ; }

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

/*  Four directions, and the two step tables the train code uses.  The
 *  scan at $B058 steps twice as far as the movement tables do. */
/*  $9D7E  freeThing -- give a record back and clear the tile it held. */
static void free_thing(City *c, int slot)
{
    uint8_t *r = thing(c, slot);
    int      y, x;
    r[0] = 0; /* $9D90 */
    y    = r[3];
    x    = r[4];
    if (y >= 0 && y < MAP_H && x >= 0 && x < MAP_W)
        c->xtxt[y][x] = 0; /* $9DD0 */
}

/* ================================================================== *
 *  $38766  disasterTornado and $38574  disasterMonster -- disaster
 *  types 7 and 8.  Both put one record on the map at the disaster
 *  point and let the mover take it from there, and only one of each
 *  may exist at a time.
 *
 *  Both clamp the point into the map rather than refusing an off-map
 *  one, and both throw away whatever record already stood on the tile.
 * ================================================================== */
static int spawn_disaster_thing(City *c, int kind)
{
    uint8_t *r;
    int      slot, y, x, t;

    slot = alloc_thing(c); /* $3877C */
    if (slot == 0)
        return 0;

    y = c->disaster_h;
    x = c->disaster_v;
    if (y < 0)
        y = 0; /* $38796 */
    if (x < 0)
        x = 0;
    if (y > 0x7F)
        y = 0x7F;
    if (x > 0x7F)
        x = 0x7F;

    t = c->xtxt[y][x]; /* $387C6 */
    if (t >= 0xC9 && t < 0xF1)
        free_thing(c, t - 0xC9); /* $387E2 */

    r    = thing(c, slot);
    r[0] = (uint8_t)kind;
    r[3] = (uint8_t)y;
    r[4] = (uint8_t)x;
    r[6] = 8;
    r[7] = 8;

    if (kind == 0x0F) /* the tornado */
    {
        r[1] = (uint8_t)(Random() & 7);             /* $387FA */
        r[8] = (uint8_t)((uint16_t)Random() % 128); /* $38844 */
        r[9] = (uint8_t)((uint16_t)Random() % 128);
        r[5] = (uint8_t)(c->altm[y][x] & 0x1F); /* $3889E */
    }
    else /* the monster */
    {
        r[1] = 2;                                   /* $3860E */
        r[8] = (uint8_t)((uint16_t)Random() % 128); /* $3864A */
        r[9] = (uint8_t)((uint16_t)Random() % 128);
        r[5] = 0x0F; /* $38698 */
    }

    r[0x0A] = c->xtxt[y][x]; /* $388C4, after the record above went away */
    r[2]    = 0;

    if (kind == 0x05) /* $386C6 -- in a scenario the monster is fixed */
    {
        if ((uint8_t)c->misc[MISC_SCEN_ACTIVE])
            r[0x0B] = 0;
        else if (Random() & 1)
            r[0x0B] = 0; /* $386FC */
        else
            r[0x0B] = (uint8_t)(((uint16_t)Random() % 3) + 1); /* $386F0 */
        c->monster_count++;
    }
    else
        c->tornado_count++;

    c->xtxt[y][x]  = (uint8_t)(slot + 0xC9); /* $38726 */
    c->thing_focus = (int16_t)slot;          /* $3872A */
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

static const int SCAN_DY[4] = {0, 2, 0, -1}; /* A5-0x623E */
static const int SCAN_DX[4] = {-1, 0, 2, 0}; /* A5-0x6236 */
static const int STEP_DY[4] = {0, 1, 0, -1}; /* A5-0x622E, and A5-0x619C */
static const int STEP_DX[4] = {-1, 0, 1, 0}; /* A5-0x6226, and A5-0x6194 */
static const int TURN_A[4]  = {0, 1, 3, 2};  /* A5-0x61A4 */
static const int TURN_B[4]  = {0, 3, 1, 2};  /* A5-0x61A0 */

/* ================================================================== *
 *  $DD7E  canTravel -- may a thing of this kind stand on this tile?
 *  A train wants rail; anything else wants the underground layer.  In
 *  both cases a tile already carrying a thing (XTXT >= $C9) is refused,
 *  which is what stops two trains sharing a tile.
 * ================================================================== */
static int can_travel(const City *c, int y, int x, int kind)
{
    int b;
    if (y < 0 || y >= MAP_H || x < 0 || x >= MAP_W)
        return 0; /* $DDA4 */
    if (c->xtxt[y][x] >= 0xC9)
        return 0; /* $DDC0 */

    b = c->xbld[y][x];
    if (kind == 0x0A) /* $DDE2 -- rail */
    {
        if ((b >= 0x2C && b < 0x3F) || (b >= 0x45 && b < 0x49) ||
            (b >= 0x6C && b < 0x70))
            return 1; /* $DE0E */
        if (b == 0x4D || b == 0x4E || b == 0x5A || b == 0x5B)
            return 1;
        return 0; /* $DE9E */
    }
    {
        int u = c->xund[y][x]; /* $DE44 */
        if ((u >= 1 && u < 0x10) || u == 0x1F || u == 0x20 || u == 0x22 ||
            u == 0x23)
            return 1; /* $DE76 */
    }
    return (b >= 0x6C && b <= 0x70) ? 1 : 0; /* $DE9A */
}

/* ================================================================== *
 *  $E18E  pickDirection -- which way should a new thing face?  Try the
 *  four directions in one of two fixed orders, chosen by a coin flip,
 *  each rotated by `turn`, and take the first that is passable.
 *  Returns -1 when the thing is boxed in.
 * ================================================================== */
static int pick_direction(City *c, int y, int x, int turn, int kind)
{
    int flip = (int)lib_rand(2); /* $E19A -- THINK C rand(), not the LFSR */
    int i;
    for (i = 0; i < 4; i++) /* $E250 */
    {
        int d   = flip ? TURN_A[i] : TURN_B[i]; /* $E1AE / $E1BA */
        int dir = (d + turn) & 3;               /* $E1CA */
        int ty  = y + STEP_DY[dir];             /* $E1D8 */
        int tx  = x + STEP_DX[dir];             /* $E1E6 */
        if (ty < 0 || ty >= MAP_H || tx < 0 || tx >= MAP_W)
            continue;
        if (!can_travel(c, ty, tx, kind))
            continue; /* $E222 */
        if (c->xtxt[ty][tx] >= 0xC9)
            continue; /* $E242 */
        return dir;   /* $E24A */
    }
    return -1; /* $E258 */
}

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

    /*  $B19A -- and it has to be one of the ten ids from $2C up that a
     *  train can actually start on */
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

/*  defined further down, next to the rest of the growth engine */
static int tile_fits(const City *c, int y, int x, int alt, int zone, int maxbld);

/* ================================================================== *
 *  $763A  footprintOrigin -- given any tile of a building, say how big
 *  the building is and move the caller's coordinates to its origin.
 *
 *  Three groups of buildings:
 *      $49..$50 and $61..$6B are two by two, and the origin is found
 *          by arithmetic alone: the row loses its low bit, the column
 *          loses its low bit and gains one.
 *      anything below $70 is a single tile.
 *      $70 and up take their size from the table at A5-0x1252.
 *
 *  For a three or four tile building the origin is found by looking at
 *  the corner markers in the high nibble of XZON.  There are four of
 *  them, $10 $20 $40 $80, and which one means which corner turns with
 *  the view, so the marker for corner k is $10 << ((rotation + k) & 3).
 *  The search tries each corner in turn and steps toward it.
 *
 *  The original reads XZON one tile outside the map without checking.
 *  It cannot happen in a saved city, because nothing larger than one
 *  tile is ever placed within two tiles of the edge, so this returns
 *  zero there rather than read past the array.
 * ================================================================== */
static int zon_corner(int rot, int k) { return 0x10 << ((rot + k) & 3); }

static int zon_at(const City *c, int y, int x)
{
    long i = (long)y * MAP_W + x;
    if (i < 0 || i >= MAP_H * MAP_W)
        return 0;
    return ((const uint8_t *)c->xzon)[i] & 0xF0;
}

int sim_footprint_origin(const City *c, int *py, int *px, int bld)
{
    int n, k, rot = c->rotation;

    if ((bld >= 0x61 && bld < 0x6C) || (bld >= 0x49 && bld < 0x51))
    {
        *py = *py & ~1;       /* $7668 */
        *px = (*px & ~1) + 1; /* $7670 */
        return 2;
    }
    if (bld < 0x70)
        return 1; /* $7684 */

    n = BUILDING[bld].size; /* $7694, A5-0x1252 */
    if (n == 1 || n > 4)
        return n; /* $769C */

    if (n >= 3) /* $76B2 -- walk to the corner marker */
    {
        for (k = 0; k < 4; k++)
        {
            const int mark = zon_corner(rot, k);
            const int dy   = (k == 1 || k == 2) ? 1 : -1;
            const int dx   = (k >= 2) ? 1 : -1;

            if (zon_at(c, *py + dy, *px + dx) == mark)
            {
                *py += dy;
                *px += dx;
            }
            if (zon_at(c, *py, *px + dx) == mark)
                *px += dx;
            if (zon_at(c, *py + dy, *px) == mark)
                *py += dy;
        }
    }

    /*  $7882 -- and finally shift from whichever corner we are on to
     *  the one the rest of the game calls the origin. */
    {
        const int here = zon_at(c, *py, *px);
        if (here == zon_corner(rot, 0))
            *px += n - 1;
        else if (here == zon_corner(rot, 1))
        {
            *px += n - 1;
            *py -= n - 1;
        }
        else if (here == zon_corner(rot, 2))
            *py -= n - 1;
    }
    return n;
}

/* ================================================================== *
 *  $331EA  clearFootprint -- take an existing multi-tile building off
 *  the map.  The XZON high nibble says which corner of the building
 *  this tile is; rotating that by the current view gives the direction
 *  back to the building's own corner, and the 2x2 there is cleared by
 *  placing tier 1 kind 4 over it.
 * ================================================================== */
static void clear_footprint(City *c, int y, int x)
{
    int n = c->xzon[y][x] & 0xF0; /* $33208 */
    int d;

    /*  $3320C -- four corner markers, each rotated by the view.  The
     *  original falls through with a STALE d5 for any other nibble
     *  (notably $F0, which stampFootprint writes for a one-tile
     *  special); we use 0 there and note the divergence rather than
     *  reproduce an uninitialised register. */
    if (n == 0x10)
        d = (4 - c->rotation) & 3; /* $3322A */
    else if (n == 0x20)
        d = (5 - c->rotation) & 3; /* $33236 */
    else if (n == 0x40)
        d = (6 - c->rotation) & 3; /* $33242 */
    else if (n == 0x80)
        d = (7 - c->rotation) & 3; /* $3324E */
    else
        d = 0;

    if (d == 0)
        x += 1; /* $33266 */
    else if (d == 1)
    {
        x += 1;
        y -= 1;
    } /* $3326A */
    else if (d == 2)
        y -= 1; /* $33270 */

    sim_place(c, y, x, 1, 4);         /* $3327C */
    sim_place(c, y + 1, x, 1, 4);     /* $3328E */
    sim_place(c, y + 1, x - 1, 1, 4); /* $332A4 */
    sim_place(c, y, x - 1, 1, 4);     /* $332B6 */
}

/* ================================================================== *
 *  $32BFA  growTo3x3 -- try to turn a 2x2 into a 3x3.
 *
 *  Four candidate anchors are tried in turn.  For each, the eight tiles
 *  around a 3x3 whose TOP-RIGHT corner is the anchor must all take the
 *  building -- the centre is neither tested nor cleared -- and at least
 *  one of four diagonal positions just outside the block must hold a
 *  road of the right orientation.  Only then is anything written.
 * ================================================================== */
/*  The ring, anchored at the top-right corner.  Used twice: once to
 *  test and once to clear, in the same order. */
static const int RING_DY[8] = {0, 0, 0, 1, 2, 2, 2, 1};
static const int RING_DX[8] = {0, -1, -2, -2, -2, -1, 0, 0};

static void grow_to_3x3(City *c, int y, int x, int zone)
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
 *  The moving-object stepper.  This is NOT part of the 25-phase clock:
 *  $09E0A runs from the main loop, once a frame, and the clock never
 *  calls it.  So a headless simulation that only ticks will never move
 *  anything, which is why a saved city reproduces exactly without it.
 *
 *  Seven of the seventeen types never move.  Types 0, 4, 11, 13 and 14
 *  are disabled in the table at A5-0x635A, and the two that are enabled
 *  but have a stepper -- 7 and 8 -- reach $D7CE and $D7D6, which are
 *  `link; unlk; rts` and do nothing at all.
 *
 *  Type 11, the train carriage, is the commonest thing in the shipped
 *  cities and is disabled here.  It still moves: the locomotive drags
 *  the whole chain through the `next` field at +2.
 * ================================================================== */

/*  A5-0x635A, indexed by type.  A zero here and the driver skips the
 *  record entirely. */
static const uint8_t THING_ENABLED[18] = {0, 1, 1, 1, 1, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1, 1, 0};

/*  A5-0x61D4 and A5-0x61C4: the same four steps the road walk uses, at
 *  another address. */
#define TRAIN_DY STEP_DY
#define TRAIN_DX STEP_DX

/*  A5-0x61B4, four bytes a direction: the sprite a train shows when it
 *  turns from one heading to another. */
static const uint8_t TURN_SPRITE[16] = {0, 1, 2, 7, 1, 2, 3, 4, 2, 3, 4, 5, 7, 4, 5, 6};

/*  $DEA6  derails -- is this tile something a train may NOT stand on?
 *  Off the map counts as fine, which is what stops a train leaving the
 *  edge from being wrecked. */
static int derails(const City *c, int y, int x)
{
    int b, u;

    if (y < 0 || y >= MAP_H || x < 0 || x >= MAP_W)
        return 0; /* $DECA */

    b = c->xbld[y][x];
    if ((b >= 0x2C && b < 0x3F) || (b >= 0x45 && b < 0x49) ||
        (b >= 0x6C && b < 0x70))
        return 0; /* $DF08, rail of some kind */
    if (b == 0x4D || b == 0x4E || b == 0x5A || b == 0x5B)
        return 0; /* $DF0E, the crossings */

    u = c->xund[y][x];
    if ((u >= 1 && u < 0x10) || u == 0x1F || u == 0x20 || u == 0x22 ||
        u == 0x23)
        return 0; /* $DF70, a subway underneath */

    b = c->xbld[y][x];
    if (b >= 0x6C && b <= 0x70)
        return 0; /* $DF94 -- note <= here, unlike the < above */
    return 1;     /* $DF98 */
}

/*  $DF9E  copyCar -- move one car's whole state onto the car behind it,
 *  demoting a locomotive to a carriage as it goes. */
static void copy_car(City *c, int src, int dst)
{
    uint8_t *a = thing(c, src), *b = thing(c, dst);
    b[3]    = a[3];
    b[4]    = a[4];
    b[6]    = a[6];
    b[7]    = a[7];
    b[0x0A] = a[0x0A];
    b[1]    = a[1];
    b[8]    = a[8];
    b[0]    = a[0];
    if (b[0] == 0x0A)
        b[0] = 0x0B; /* $E05E */
    if (b[0] == 0x0C)
        b[0] = 0x0D; /* $E072 */
}

/*  $E07E  reverseTrain -- the head and the tail change places and the
 *  whole train faces the other way. */
static void reverse_train(City *c, int head, int tail)
{
    uint8_t  *h  = thing(c, head);
    uint8_t  *t  = thing(c, tail); /* a2, the record the reads come from */
    const int oy = t[3], ox = t[4], keep = t[0x0A];
    int       dir = t[1]; /* $E0BE -- the TAIL's heading, not the head's */

    t[3]    = h[3]; /* $E0CA -- the tail takes the head's place */
    t[4]    = h[4];
    t[6]    = h[6];
    t[7]    = h[7];
    t[0x0A] = h[0x0A];

    h[3]    = (uint8_t)oy; /* $E126 -- and the head takes the tail's */
    h[4]    = (uint8_t)ox;
    h[6]    = (uint8_t)oy;
    h[7]    = (uint8_t)ox;
    h[0x0A] = (uint8_t)keep;

    /*  $E15C -- both of these land on the HEAD's record, indexed by the
     *  first argument, which is what makes the whole train face about. */
    dir  = ((dir & 0x0F) + 2) & 3;
    h[8] = (uint8_t)((dir + 4) & 7);
    h[1] = (uint8_t)dir;
}

/*  $BF96  spawnWreck -- what a derailed train leaves behind: one
 *  type-6 record on the tile it came off at. */
static void spawn_wreck(City *c, int y, int x)
{
    uint8_t *t;
    int      slot = alloc_thing(c); /* $BFA6 */

    if (slot == 0)
        return;
    if (c->xtxt[y][x] >= 0xC9)
        return; /* $BFC8 */

    t             = thing(c, slot);
    t[0]          = 6; /* $BFD8 */
    t[1]          = 0;
    t[3]          = (uint8_t)y;
    t[4]          = (uint8_t)x;
    t[6]          = 8;
    t[7]          = 8;
    t[5]          = 0;
    t[0x0A]       = c->xtxt[y][x]; /* $C036 */
    t[0x0B]       = 0;
    t[2]          = 0;
    c->xtxt[y][x] = (uint8_t)(slot + 0xC9); /* $C064 */
}

/* ================================================================== *
 *  $D7DE  stepTrain -- types 10 and 12, and with them every carriage.
 *
 *  A locomotive advances onto the tile it was already pointing at, the
 *  cars behind shuffle up one, and then a new tile ahead is chosen.
 *  Four things can interrupt that: a station beside it, a car already
 *  standing on the tile ahead, track that has gone, or nowhere left to
 *  go.
 * ================================================================== */
static void step_train(City *c, int slot)
{
    uint8_t *t = thing(c, slot);
    int      y = t[3], x = t[4];
    int      ay = t[6], ax = t[7]; /* the tile it is heading for */
    int      type = t[0];
    int      car2, car3, dir, nd, v, b;

    /*  $D81C -- half the time a locomotive stops beside a station. */
    if (type == 0x0A && game_rand1())
    {
        const int k = (t[1] & 1) ? (int)lib_rand(2) + 2 : (int)lib_rand(2);
        if (k == 0 && y > 0 && c->xbld[y - 1][x] == 0xED)
            return;
        if (k == 1 && y < 0x7F && c->xbld[y + 1][x] == 0xED)
            return;
        if (k == 2 && x > 0 && c->xbld[y][x - 1] == 0xED)
            return;
        if (k == 3 && x < 0x7F && c->xbld[y][x + 1] == 0xED)
            return;
    }

    car2 = t[2]; /* $D91C */
    car3 = thing(c, car2)[2];

    if (derails(c, y, x)) /* $D92E -- the track under it has gone */
    {
        uint8_t *b2 = thing(c, car2), *b3 = thing(c, car3);
        t[0] = b2[0] = b3[0] = 0; /* $D944 */
        if (c->xtxt[y][x] == slot + 0xC9)
            c->xtxt[y][x] = 0;
        if (c->xtxt[b2[3]][b2[4]] == car2 + 0xC9)
            c->xtxt[b2[3]][b2[4]] = 0;
        if (c->xtxt[b3[3]][b3[4]] == car3 + 0xC9)
            c->xtxt[b3[3]][b3[4]] = 0;
        spawn_wreck(c, y, x); /* $DA12 */
        return;
    }

    /*  $DA1E -- is the tile ahead free?  Our own tail car does not
     *  count, because it is about to move out of the way. */
    v = c->xtxt[ay][ax];
    if (v >= 0xC9 && (v - 0xC9) != car3)
        return; /* $DA52 */

    if (!(y == ay && x == ax)) /* $DA56 */
    {
        uint8_t *b2 = thing(c, car2), *b3 = thing(c, car3);
        c->xtxt[y][x]         = (uint8_t)(car2 + 0xC9); /* $DA7C */
        c->xtxt[b2[3]][b2[4]] = (uint8_t)(car3 + 0xC9); /* $DAB2 */
        c->xtxt[b3[3]][b3[4]] = b3[0x0A];               /* $DAD2 */
        copy_car(c, car2, car3);                        /* $DADC */
        copy_car(c, slot, car2);                        /* $DAE6 */
        t[0x0A]         = c->xtxt[ay][ax];              /* $DB0C */
        c->xtxt[ay][ax] = (uint8_t)(slot + 0xC9);       /* $DB26 */
    }

    t[3] = t[6]; /* $DB3C -- the locomotive is now where it was pointing */
    t[4] = t[7];
    dir  = t[1] & 0x0F;
    y    = t[3];
    x    = t[4];

    /*  $DB92 -- a tunnel mouth flips the train between the two forms. */
    b = c->xbld[y][x];
    if (b >= 0x6C && b <= 0x70)
    {
        t[0] = (uint8_t)((type == 0x0A) ? 0x0C : 0x0A);
        type = t[0];
    }

    /*  $DBD8 -- one turn in four is considered, left or right by a coin,
     *  and taken only if the track goes that way. */
    if (game_rand(4) == 0)
    {
        const int nw = game_rand(2) ? (dir + 1) & 3 : (dir + 3) & 3;
        if (can_travel(c, y + TRAIN_DY[nw], x + TRAIN_DX[nw], type))
            dir = nw; /* $DC50 */

        /*  $DC54 -- and then it sounds the horn, one time in 256.  The
         *  draw is taken either way and changes nothing, but it comes
         *  from the Toolbox generator that the boats and ships also
         *  read, so leaving it out is invisible in a city with only
         *  trains and wrong in every other one. */
        if ((Random() & 0xFF) == 0)
            ; /* $DC64, sound $20C */
    }

    {
        const int ty = y + TRAIN_DY[dir], tx = x + TRAIN_DX[dir];

        /*  $DCA2 is a `bne`: the track ahead being clear is the common
         *  case, and it simply carries on.  Note it masks the heading
         *  rather than storing the turned one, so a train that turned
         *  above keeps its old value at +1 while +6, +7 and +8 use the
         *  new one. */
        if (can_travel(c, ty, tx, type))
        {
            t[1] = (uint8_t)(t[1] & 0x0F); /* $DD44 */
            t[8] = (uint8_t)(dir * 2);
            t[6] = (uint8_t)ty;
            t[7] = (uint8_t)tx;
            return;
        }

        /*  blocked, so look for any way out at all */
        nd = pick_direction(c, y, x, dir, type); /* $DCB0 */
        if (nd < 0)
        {
            reverse_train(c, slot, car3); /* $DCC4 */
            return;
        }
        t[1] = (uint8_t)nd;                           /* $DCD8 */
        t[8] = TURN_SPRITE[(dir & 3) * 4 + (nd & 3)]; /* $DCF6 */
        t[6] = (uint8_t)(y + TRAIN_DY[nd]);           /* $DD10 */
        t[7] = (uint8_t)(x + TRAIN_DX[nd]);           /* $DD2C */
    }
}

/*  A5-0x618C and A5-0x6184, the pixel step per heading.  The two tables
 *  are four words each and adjacent, so the column table is simply the
 *  row table read four entries further on. */
static const int SUBSTEP[8] = {0, 16, 0, -16, -16, 0, 16, 0};

/* ================================================================== *
 *  $E388  advanceSubtile -- move one step along the heading at +1.  The
 *  pixel offset at +6/+7 carries into the tile coordinate at +3/+4.
 *  Returns 0 only when the carry took the thing off the map, in which
 *  case the slot has been released.
 * ================================================================== */
static int advance_subtile(City *c, int slot)
{
    uint8_t  *t   = thing(c, slot);
    const int dir = t[1];
    int       py  = t[6] + SUBSTEP[dir];     /* $E3B0 */
    int       px  = t[7] + SUBSTEP[dir + 4]; /* $E3BE */
    int       cy = 0, cx = 0, carry = 0;
    int       ny, nx;

    /*  The tests are `> 16`, not `>= 16`, so a thing that lands exactly
     *  on a tile boundary spends one step sitting on it. */
    if (py > 16)
    {
        cy = 1;
        py -= 16;
        carry = 1;
    } /* $E3CA */
    if (py < 0)
    {
        cy = -1;
        py += 16;
        carry = 1;
    } /* $E3DA */
    if (px > 16)
    {
        cx = 1;
        px -= 16;
        carry = 1;
    } /* $E3E8 */
    if (px < 0)
    {
        cx = -1;
        px += 16;
        carry = 1;
    } /* $E3FC */

    t[6] = (uint8_t)py; /* $E416 */
    t[7] = (uint8_t)px;
    if (!carry)
        return 1; /* $E426 */

    ny                  = t[3] + cy;
    nx                  = t[4] + cx;
    c->xtxt[t[3]][t[4]] = 0; /* $E474, leave the old tile first */

    /*  $E482 guards with $7F, not $80.  The last row and column of the
     *  map are out of bounds here even though they are real tiles, so a
     *  thing that reaches the far edge is released rather than parked. */
    if (ny < 0 || nx < 0 || ny >= 0x7F || nx >= 0x7F)
    {
        free_thing(c, slot); /* $E492 */
        return 0;
    }
    t[3]            = (uint8_t)ny; /* $E4A4 */
    t[4]            = (uint8_t)nx;
    c->xtxt[ny][nx] = (uint8_t)(slot + 0xC9); /* $E4CC */
    return 1;
}

/* ================================================================== *
 *  $E4DA  boatCanEnter -- may the boat move one step in `dir`?
 *
 *  Off the map counts as yes: $E388 makes its own bounds test and
 *  releases the slot there, so the two routines split the check between
 *  them rather than duplicating it.
 * ================================================================== */
static int boat_can_enter(City *c, int slot, int dir)
{
    uint8_t  *t = thing(c, slot);
    const int y = t[3] + STEP_DY[dir]; /* $E500 */
    const int x = t[4] + STEP_DX[dir]; /* $E510 */
    int       b;

    if (y < 0 || y >= MAP_H || x < 0 || x >= MAP_W)
        return 1; /* $E52C */

    b = c->xbld[y][x];
    if (b == 0xF8) /* $E546, a marina: the boat has arrived */
    {
        /*  Released by hand rather than through $9D7E, and note it
         *  clears the boat's OWN tile, not the marina it entered. */
        t[0]                = 0; /* $E554 */
        c->xtxt[t[3]][t[4]] = 0; /* $E578 */
        return 0;
    }
    if (b == 0xDF) /* $E580, the near half of a bridge */
        return 0;
    if (c->xtxt[y][x]) /* $E598, something is already there */
        return 0;
    return (c->xbit[y][x] & XBIT_WATER) ? 1 : 0; /* $E5B4 */
}

/* ================================================================== *
 *  $E262  stepBoat -- the sailboat, thing type 9.
 *
 *  Four may be alive at once.  The counter is cleared by the driver
 *  every frame and rebuilt here, so the fifth boat reached in a pass is
 *  the one that gets released -- which boat that is depends on slot
 *  order, not on age.
 * ================================================================== */
static void step_boat(City *c, int slot)
{
    uint8_t *t = thing(c, slot);
    int      dir;

    c->boat_count++; /* $E26E */
    if (c->boat_count > 4)
    {
        free_thing(c, slot); /* $E27A */
        return;
    }

    if (t[2]) /* $E28E -- already sinking */
    {
        if (game_rand(5) == 0)
            free_thing(c, slot); /* $E2A4 */
        return;
    }

    if (game_rand(4) != 0) /* $E2B4 -- three steps in four it just sails */
    {
        if (boat_can_enter(c, slot, t[1]))
            advance_subtile(c, slot); /* $E2DE */
        return;
    }

    /*  The fourth step is the one where it looks around. */
    if (!(c->xbit[t[3]][t[4]] & XBIT_WATER))
    {
        free_thing(c, slot); /* $E316, aground */
        return;
    }
    if (game_rand(0xFA0) == 0) /* $E324, one step in four thousand */
    {
        t[2] = 1; /* $E338, it starts to sink */
        /*  $E342 reports it, which only makes a sound. */
    }
    /*  $E35A -- turn by -1, 0 or +1, from the Toolbox generator rather
     *  than the game's own.  This is the only draw in the routine that
     *  does not come from the shift register. */
    dir  = t[1] + (int)((uint16_t)Random() % 3) - 1;
    t[1] = (uint8_t)(dir & 3);
}

/* ================================================================== *
 *  Ships -- type 3.
 *
 *  A ship is a five-state machine on the byte at +2, dispatched through
 *  the jump table at $E68C: 0 sails, 1 lines itself up, 2 works its way
 *  out of a corner, 3 sits at the dock and 4 runs for the seaport.  It
 *  keeps a heading in 0..7 rather than the trains' four, looks THREE
 *  tiles ahead rather than one, and carries whatever XTXT it is sitting
 *  on in +$0A so it can put it back when it leaves.
 * ================================================================== */

/*  A5-0x612C and A5-0x611C: how far ahead the ship checks. */
static const int SHIP_DY[8] = {0, 3, 3, 3, 0, -3, -3, -3};
static const int SHIP_DX[8] = {-3, -3, 0, 3, 3, 3, 0, -3};
/*  A5-0x6102 and A5-0x60F2: the pixel step it actually takes.  The two
 *  differ -- the diagonals move four pixels on their long axis but the
 *  lookahead is three tiles in every direction. */
static const int SHIP_SUB_DY[8] = {0, 3, 4, 3, 0, -3, -4, -3};
static const int SHIP_SUB_DX[8] = {-4, -3, 0, 3, 4, 3, 0, -3};
/*  A5-0x613C and A5-0x6134: the four tiles two steps out that state 0
 *  checks for a wharf. */
static const int DOCK_DY[4] = {2, 0, -2, 0};
static const int DOCK_DX[4] = {0, 2, 0, -2};
/*  A5-0x615C and A5-0x614C: the order headings are tried in when the
 *  way ahead is shut -- clockwise and anticlockwise.  Both are nine
 *  long, and the ninth entry is not a continuation of the sweep. */
static const int TURN_CW[9]  = {0, 1, 2, 3, 4, 5, 6, 7, 0};
static const int TURN_CCW[9] = {0, 7, 6, 5, 4, 3, 2, 1, 2};

/*  The nine building ids at A5-0x610C that a ship may sail over: the
 *  bridge pieces it passes under. */
static const uint8_t SHIP_PASSABLE[9] = {0x51, 0x52, 0x54, 0x55, 0x58, 0x59, 0x5B, 0x5C, 0x6B};

/*  $EA0E  shipWater -- is this tile navigable? */
static int ship_water(City *c, int y, int x)
{
    const int b = c->xbld[y][x];
    int       i;

    if (!(c->xbit[y][x] & XBIT_WATER))
        return 0; /* $EA2E */
    if (c->xter[y][x] < 0x10)
        return 0; /* $EA4E, too shallow */
    if (c->xter[y][x] >= 0x20)
        return 0; /* $EA6C */
    if (b == 0xF8)
        return 0; /* $EA8A, a marina */
    if (b == 0)
        return 1;           /* $EA96, open water */
    for (i = 0; i < 9; i++) /* $EA9C */
        if (b == SHIP_PASSABLE[i])
            return 1;
    return 0;
}

/*  $E978  shipCanEnter -- may the ship head that way?  Off the map
 *  counts as yes, and an XTXT of $C9 or more means another record is
 *  already there, so ships never stack. */
static int ship_can_enter(City *c, int slot, int dir)
{
    const uint8_t *t = thing(c, slot);
    const int      y = t[3] + SHIP_DY[dir]; /* $E99E */
    const int      x = t[4] + SHIP_DX[dir]; /* $E9AE */

    if (y < 0 || y >= MAP_H || x < 0 || x >= MAP_W)
        return 1; /* $E9CA */
    if (!ship_water(c, y, x))
        return 0;                         /* $E9DE */
    return c->xtxt[y][x] >= 0xC9 ? 0 : 1; /* $E9F8 */
}

/*  $EAC2  shipAdvance -- the ship's version of $E388.  Two differences
 *  from the boat's: the bounds are $80 rather than $7F, and the tile it
 *  is standing on is saved in +$0A and put back when it leaves. */
static int ship_advance(City *c, int slot)
{
    uint8_t  *t   = thing(c, slot);
    const int dir = t[1];
    int       py  = t[6] + SHIP_SUB_DY[dir]; /* $EAEA */
    int       px  = t[7] + SHIP_SUB_DX[dir]; /* $EAF8 */
    int       cy = 0, cx = 0, carry = 0;
    int       oy, ox, ny, nx;

    if (py > 16)
    {
        cy = 1;
        py -= 16;
        carry = 1;
    } /* $EB04 */
    if (py < 0)
    {
        cy = -1;
        py += 16;
        carry = 1;
    } /* $EB14 */
    if (px > 16)
    {
        cx = 1;
        px -= 16;
        carry = 1;
    } /* $EB22 */
    if (px < 0)
    {
        cx = -1;
        px += 16;
        carry = 1;
    } /* $EB36 */

    t[6] = (uint8_t)py; /* $EB50 */
    t[7] = (uint8_t)px;
    if (!carry)
        return 1; /* $EB60 */

    oy              = t[3];
    ox              = t[4];
    ny              = oy + cy;
    nx              = ox + cx;
    c->xtxt[oy][ox] = t[0x0A]; /* $EBBA, put back what was underneath */

    if (ny < 0 || nx < 0 || ny >= MAP_H || nx >= MAP_W)
    {
        /*  $EBD8 -- and note $9D7E then zeroes the tile that was just
         *  restored, because the record still holds the old position. */
        free_thing(c, slot);
        return 0;
    }
    t[3]            = (uint8_t)ny; /* $EBEA */
    t[4]            = (uint8_t)nx;
    t[0x0A]         = c->xtxt[ny][nx];        /* $EC0E, pick the new one up */
    c->xtxt[ny][nx] = (uint8_t)(slot + 0xC9); /* $EC20 */
    return 1;
}

/*  $B4CC  bearing -- the compass direction from one tile to another,
 *  0..7 matching SHIP_DY/SHIP_DX.  A half-step of slack on each axis
 *  decides whether a heading counts as straight or diagonal. */
static int bearing(int y0, int x0, int y1, int x1)
{
    const int dy = y1 - y0, dx = x1 - x0;
    const int ay = dy < 0 ? -dy : dy;
    const int ax = dx < 0 ? -dx : dx;

    if ((ax + 1) / 2 > ay) /* $B50C */
        return dx < 0 ? 0 : 4;
    if (dy < 0) /* $B51A */
        return (ay + 1) / 2 > ax ? 6 : (dx < 0 ? 7 : 5);
    return (ay + 1) / 2 > ax ? 2 : (dx < 0 ? 1 : 3);
}

/*  $B3D8  turnToward -- one step round the compass toward `want`, the
 *  short way.  It ALWAYS moves: given cur == want it takes the second
 *  arm, finds a difference of zero, and still steps one.  $B46A below
 *  is the same routine with that case guarded. */
static int turn_toward(int cur, int want)
{
    if (want < cur)
        cur += (cur - want > 4) ? 1 : -1; /* $B3EA */
    else
        cur += (want - cur > 4) ? -1 : 1; /* $B3FC */
    return cur & 7;
}

/*  $B46A  steerToward -- bearing, then one step toward it, holding
 *  still when it is already right. */
static int steer_toward(int cur, int y0, int x0, int y1, int x1)
{
    const int want = bearing(y0, x0, y1, x1); /* $B486 */
    if (want == cur)
        return cur; /* $B492 */
    return turn_toward(cur, want);
}

/* ================================================================== *
 *  $E5C8  stepShip -- thing type 3.
 * ================================================================== */
static void step_ship(City *c, int slot)
{
    uint8_t  *t = thing(c, slot);
    const int y = t[3], x = t[4];   /* $E5FC/$E606, before it moves */
    const int ty = t[8], tx = t[9]; /* $E60C/$E616, where it is headed */
    int       i, d5         = 0, d6;

    c->ship_count++; /* $E5D4 */
    if ((Random() & 0xFF) == 0)
        ; /* $E5E8 sounds the horn, $205 */

    if (!(c->xbit[y][x] & XBIT_WATER)) /* $E630 */
    {
        /*  Aground.  The record is not released -- it turns into a
         *  type 6 where it stands. */
        t[0]    = 6; /* $E63E */
        t[2]    = 0; /* $E64C */
        t[0x0B] = 0; /* $E658 */
        t[1]    = 0; /* $E664 */
        return;
    }
    if (t[2] > 4)
        return; /* $E67E */

    switch (t[2]) /* $E688 */
    {
        case 0:                     /* $E696 -- under way */
            if (game_rand(10) == 0) /* $E69A */
            {
                d6 = steer_toward(t[1], y, x, ty, tx); /* $E6C2 */
                if (ship_can_enter(c, slot, d6))       /* $E6CC */
                    t[1] = (uint8_t)d6;
            }
            if (ship_can_enter(c, slot, t[1])) /* $E6F8 */
                ship_advance(c, slot);         /* $E706 */
            else
                t[2] = 1; /* $E718 */

            /*  $E71E -- either way, look two tiles out in four
             *  directions.  A wharf beside the ship sends it to the
             *  waiting state.  The scan uses the position the ship had
             *  when the routine started, not where it just moved to. */
            for (i = 0; i < 4; i++)
            {
                const int ny = y + DOCK_DY[i]; /* $E72C */
                const int nx = x + DOCK_DX[i]; /* $E738 */
                if (ny < 0 || ny >= MAP_H || nx < 0 || nx >= MAP_W)
                    continue;                /* $E73C */
                if (c->xbld[ny][nx] == 0xDF) /* $E768 */
                    t[2] = 3;
            }
            break;

        case 1:                                    /* $E788 -- lining up on the target */
            d6   = bearing(y, x, ty, tx);          /* $E794 */
            t[1] = (uint8_t)turn_toward(t[1], d6); /* $E7AC */
            if (d6 != t[1])
                break;                                  /* $E7CA, still coming round */
            t[2] = ship_can_enter(c, slot, d6) ? 0 : 2; /* $E7EC/$E7FC */
            break;

        case 2: /* $E806 -- boxed in, sweep for any way out */
            d6 = t[1];
            if (game_rand(2) != 0) /* $E818 */
            {
                for (i = 0; i < 9; i++) /* $E828 */
                {
                    d5 = (d6 + TURN_CW[i]) & 7;
                    if (ship_can_enter(c, slot, d5))
                        break;
                }
            }
            else
            {
                for (i = 0; i < 9; i++) /* $E856 */
                {
                    d5 = (d6 + TURN_CCW[i]) & 7;
                    if (ship_can_enter(c, slot, d5))
                        break;
                }
            }
            /*  $E87E tests for exactly 8, so the ship is released when
             *  the ninth heading is the one that worked -- but NOT when
             *  the sweep ran out with nothing, which leaves the loop at
             *  9.  The two writes below then land on a freed slot. */
            if (i == 8)
                free_thing(c, slot); /* $E886 */
            t[1] = (uint8_t)d5;      /* $E894 */
            t[2] = 0;                /* $E8A0 */
            break;

        case 3: /* $E8A8 -- tied up, one chance in thirty of leaving */
            if (game_rand(0x1E) != 0)
                break;
            t[2] = 4; /* $E8C2 */
            /*  $E8CC sounds the horn again. */
            /*  $E8DE/$E8EA take the LOW BYTE of the seaport position at
             *  A5+0x12F4 and 0x12F6 -- the odd addresses, not the even
             *  ones -- and make that the new target. */
            t[8] = (uint8_t)c->ship_y;
            t[9] = (uint8_t)c->ship_x;
            break;

        case 4:                                /* $E8F4 -- running for the seaport */
            if (ship_can_enter(c, slot, t[1])) /* $E906 */
            {
                ship_advance(c, slot); /* $E914 */
                break;
            }
            d6 = t[1];
            /*  $E930 -- the sweep starts at 0 or 1, so the ship
             *  sometimes skips its own heading before looking. */
            for (i = (int)game_rand(2); i < 9; i++)
            {
                d5 = (d6 + TURN_CW[i]) & 7;
                if (ship_can_enter(c, slot, d5))
                    break;
            }
            t[1] = (uint8_t)d5; /* $E96C */
            break;

        default:
            break;
    }
}

/* ================================================================== *
 *  Helicopters -- type 2.
 *
 *  Six states on the byte at +2, jump table at $C83C.  0 climbs, 1 does
 *  nothing at all, 2 cruises, 3 descends to land, 4 sits, 5 spirals in
 *  and crashes.  The altitude lives at +5 and the target at +8/+9.
 * ================================================================== */

/*  A5-0x638A and A5-0x637A: a plain eight-way unit step.  A5-0x621E
 *  scales it -- the helicopter passes index 2, so four pixels a frame. */
static const int STEP8_DY[8]   = {0, 1, 1, 1, 0, -1, -1, -1};
static const int STEP8_DX[8]   = {-1, -1, 0, 1, 1, 1, 0, -1};
static const int MOVE_SPEED[8] = {0, 8, 4, 2, 0, 8, 0, 0};

/*  A5-0x61F4 / A5-0x61E4, the same three-tile lookahead the ship uses. */
static const int HELI_DY[8] = {0, 3, 3, 3, 0, -3, -3, -3};
static const int HELI_DX[8] = {-3, -3, 0, 3, 3, 3, 0, -3};
/*  A5-0x61FC: the order alternative headings are tried in, alternating
 *  outward from the current one. */
static const int HELI_SWEEP[7] = {1, 7, 2, 6, 3, 5, 4};

/*  $A6E4 -- Manhattan distance. */
static int manhattan(int y0, int x0, int y1, int x1)
{
    const int dy = y1 - y0, dx = x1 - x0;
    return (dy < 0 ? -dy : dy) + (dx < 0 ? -dx : dx);
}

/*  What _TickCount answers.  $C928 is the only place the simulation
 *  reads the clock, and the interpreter feeds it a counter rather than
 *  a real one, so the reconstruction does the same. */
static int32_t tick_count(City *c) { return (c->ticks += 64); }

/*  $CBFE  heliBlocked -- NOTE THE SENSE.  This returns NONZERO when the
 *  tile three steps out is blocked, the opposite of the ship's $E978.
 *  Off the map is not blocked, so a helicopter will fly off the edge. */
static int heli_blocked(City *c, int slot, int dir)
{
    const uint8_t *t = thing(c, slot);
    const int      y = t[3] + HELI_DY[dir]; /* $CC24 */
    const int      x = t[4] + HELI_DX[dir]; /* $CC34 */

    if (y < 0 || y >= MAP_H || x < 0 || x >= MAP_W)
        return 0;                         /* $CC50 */
    return c->xbld[y][x] >= 0xFB ? 1 : 0; /* $CC68 */
}

/*  $CB86 -- hold the heading while it is clear, otherwise take the
 *  first of seven alternatives that is. */
static void heli_avoid(City *c, int slot)
{
    uint8_t *t = thing(c, slot);
    int      i, d = t[1];

    if (!heli_blocked(c, slot, t[1]))
        return;             /* $CBAE */
    for (i = 0; i < 7; i++) /* $CBB4 */
    {
        d = (t[1] + HELI_SWEEP[i]) & 7;
        if (!heli_blocked(c, slot, d))
            break;
    }
    t[1] = (uint8_t)d; /* $CBF2, written even when nothing was clear */
}

/* ================================================================== *
 *  $B568  advanceSpeed -- the general sub-tile advance, scaled by a
 *  speed index, and the only one of the three that will walk more than
 *  one tile in a frame: landing on an occupied tile makes it step again
 *  in the same direction until it finds a free one or leaves the map.
 * ================================================================== */
static void advance_speed(City *c, int speed_idx, int slot, int dir)
{
    uint8_t  *t  = thing(c, slot);
    const int sp = MOVE_SPEED[speed_idx];     /* $B58C */
    int       py = t[6] + sp * STEP8_DY[dir]; /* $B59A */
    int       px = t[7] + sp * STEP8_DX[dir]; /* $B5AE */
    int       cy = 0, cx = 0, carry = 0;
    int       ny, nx;

    if (py > 16)
    {
        cy = 1;
        py -= 16;
        carry = 1;
    } /* $B5C0 */
    if (py < 0)
    {
        cy = -1;
        py += 16;
        carry = 1;
    } /* $B5D4 */
    if (px > 16)
    {
        cx = 1;
        px -= 16;
        carry = 1;
    } /* $B5E6 */
    if (px < 0)
    {
        cx = -1;
        px += 16;
        carry = 1;
    } /* $B5FA */

    t[6] = (uint8_t)py; /* $B614 */
    t[7] = (uint8_t)px;
    if (!carry)
        return; /* $B628 */

    ny                  = t[3] + cy;
    nx                  = t[4] + cx;
    c->xtxt[t[3]][t[4]] = t[0x0A]; /* $B686, put back what was underneath */

    for (;;)
    {
        /*  $B6E2 reads XTXT before testing the bounds, so the original
         *  can read one row past the table here.  The reconstruction
         *  cannot reproduce that read, and treats off-map as unoccupied,
         *  which takes the same branch the read almost always would. */
        const int occupied = (ny >= 0 && ny < MAP_H && nx >= 0 && nx < MAP_W)
                                 ? c->xtxt[ny][nx] >= 0xC9
                                 : 0;
        if (!occupied)
            break;
        /*  $B68C -- it moves onto the occupied tile anyway and tries
         *  again one further on. */
        t[3] = (uint8_t)ny;
        t[4] = (uint8_t)nx;
        ny   = t[3] + cy;
        nx   = t[4] + cx;
        if (ny < 0 || ny >= MAP_H || nx < 0 || nx >= MAP_W) /* $B6BE */
        {
            free_thing(c, slot); /* $B6D8 */
            return;
        }
    }
    if (ny < 0 || ny >= MAP_H || nx < 0 || nx >= MAP_W) /* $B704 */
    {
        free_thing(c, slot); /* $B71E */
        return;
    }
    t[3]            = (uint8_t)ny; /* $B72E */
    t[4]            = (uint8_t)nx;
    t[0x0A]         = c->xtxt[ny][nx];        /* $B74C */
    c->xtxt[ny][nx] = (uint8_t)(slot + 0xC9); /* $B762 */
}

/* ================================================================== *
 *  $C7A4  stepHeli -- thing type 2.
 * ================================================================== */
static void step_heli(City *c, int slot)
{
    uint8_t  *t   = thing(c, slot);
    const int dir = t[1];

    c->heli_count++; /* $C7B0 */

    if (c->xbld[t[3]][t[4]] >= 0xFB) /* $C7EA */
    {
        /*  Over something it cannot be over.  Like the ship running
         *  aground, it becomes a type 6 where it stands. */
        t[0]    = 6; /* $C7F8 */
        t[2]    = 5; /* $C806 */
        t[0x0B] = 0; /* $C814 */
        t[1]    = 0; /* $C820 */
        return;
    }
    if (t[2] > 5)
        return; /* $C82E */

    switch (t[2]) /* $C838 */
    {
        case 0: /* $C848 -- climbing away */
            t[1] = (uint8_t)((dir + 1) & 7);
            if (t[5] < 10)
                t[5]++; /* $C874 */
            else
                t[2] = 2; /* $C880 */
            break;

        case 1: /* $CB7E -- state 1 does nothing at all */
            break;

        case 2: /* $C88A -- cruising */
            {
                const int td = manhattan(t[3], t[4], t[8], t[9]);

                t[1] = (uint8_t)steer_toward(dir, t[3], t[4], t[8], t[9]); /* $C8B6 */
                heli_avoid(c, slot);                                       /* $C8CA */
                advance_speed(c, 2, slot, t[1]);                           /* $C8E6 */

                /*  $C91C -- the traffic report.  XTRF is at half the map's
                 *  resolution, and the report is rate-limited by the clock
                 *  rather than by the simulation: five thousand ticks. */
                if (c->xtrf[t[3] / 2][t[4] / 2] >= 0xAA &&
                    tick_count(c) > c->heli_timer)
                {
                    /*  $C936 plays it. */
                    c->heli_timer = tick_count(c) + 0x1388; /* $C948 */
                }

                if (td >= 2)
                    break; /* $C984, still on its way */

                /*  Arrived.  Pick somewhere new near where the player is
                 *  looking, redrawing either coordinate that lands off the
                 *  map.  Every one of these is a Toolbox draw. */
                {
                    int ny = c->view_y + (int)((uint16_t)Random() % 0x40) - 0x20;
                    int nx = c->view_x + (int)((uint16_t)Random() % 0x40) - 0x20;
                    if (ny < 0 || ny >= MAP_H)
                        ny = (int)((uint16_t)Random() % 0x40) + 0x20; /* $C9E2 */
                    if (nx < 0 || nx >= MAP_W)
                        nx = (int)((uint16_t)Random() % 0x40) + 0x20; /* $CA08 */
                    t[8] = (uint8_t)ny;                               /* $CA28 */
                    t[9] = (uint8_t)nx;
                }
                if (!(Random() & 1))
                    break; /* $CA42 */
                if (c->xbld[t[3]][t[4]] != 0)
                    break; /* $CA6E */
                if (c->xter[t[3]][t[4]] != 0)
                    break; /* $CA88 */
                t[2] = 3;  /* $CA8E, bare ground -- go down */
                break;
            }

        case 3: /* $CA98 -- descending */
            t[1] = (uint8_t)((dir + 1) & 7);
            if (t[5] > 2)
                t[5]--; /* $CAC4 */
            else
                t[2] = 4; /* $CAD0 */
            break;

        case 4: /* $CADA -- on the ground, one chance in twenty of going */
            if ((uint16_t)Random() % 0x14 != 0)
                break;
            t[2] = 0; /* $CAF8 */
            break;

        case 5: /* $CB00 -- going down hard */
            t[1] = (uint8_t)((dir + 2) & 7);
            if (t[5] == 4)
                ; /* $CB2C sounds the crash, $203 */
            if (t[5] > 2)
            {
                t[5]--; /* $CB42 */
                break;
            }
            t[0]    = 6;    /* $CB50 */
            t[2]    = 0x11; /* $CB5E */
            t[0x0B] = 1;    /* $CB6C */
            t[1]    = 0;    /* $CB7A */
            break;

        default:
            break;
    }
}

/* ================================================================== *
 *  Aeroplanes -- type 1.
 *
 *  Eight states, jump table at $C336: 0 climbs out, 1 lands, 2 cruises,
 *  3 turns onto the approach, 4 flies the approach, 5 and 6 do nothing,
 *  7 spirals in.  The state byte packs more than a state: the LOW nibble
 *  is the state and the HIGH nibble is the runway heading, which states
 *  3 and 4 read back out to line the aeroplane up.
 * ================================================================== */

/*  $B418 -- the coarse bearing, decided by the sign of each delta alone
 *  rather than by $B4CC's half-step slack. */
static int bearing_coarse(int y0, int x0, int y1, int x1)
{
    const int dy = y1 - y0, dx = x1 - x0;

    if (dy < 0) /* $B42E */
        return dx < 0 ? 7 : (dx > 0 ? 5 : 6);
    if (dy > 0) /* $B446 */
        return dx < 0 ? 1 : (dx > 0 ? 3 : 2);
    return dx < 0 ? 0 : 4; /* $B45C */
}

/*  $B394 -- one chance in n of drifting a point either way. */
static int wander(int dir, int n)
{
    if ((uint16_t)Random() % (uint16_t)n != 0)
        return dir;                                       /* $B3B0 */
    return (dir + (int)((uint16_t)Random() % 3) - 1) & 7; /* $B3C2 */
}

/* ================================================================== *
 *  $C1E6  stepPlane -- thing type 1.
 * ================================================================== */
static void step_plane(City *c, int slot)
{
    uint8_t  *t     = thing(c, slot);
    const int state = t[2] & 0x0F; /* $C208 */
    const int b     = c->xbld[t[3]][t[4]];
    int       runway;

    c->plane_count++; /* $C1F2 */

    /*  $C23C -- anything over $70 is a real building, and an aeroplane
     *  low enough is going to hit it.  A zone nibble of exactly 8 is the
     *  airport, which it is allowed to be over. */
    if (b > 0x70 && (c->xzon[t[3]][t[4]] & 0x0F) != 8)
    {
        if (b >= 0xFB) /* $C262 */
        {
            t[0] = 6; /* $C270 */
            t[2] = 5; /* $C27E */
            /*  $C288 -- one crash in sixteen leaves the flag set. */
            t[0x0B] = (uint8_t)(game_rand(0x10) == 0 ? 1 : 0);
            t[1]    = 0; /* $C2B8 */
            return;
        }
        /*  $C2DA -- the building's shape height over three.  Flying
         *  lower than that is flying into it. */
        if (BUILDING[b].sprite_h / 3 > t[5])
        {
            t[0]    = 6; /* $C2EE */
            t[2]    = 5;
            t[0x0B] = 1;
            t[1]    = 0;
            return;
        }
    }
    if (state > 7)
        return; /* $C328 */

    switch (state) /* $C332 */
    {
        case 0:                              /* $C346 -- climbing out */
            advance_speed(c, 1, slot, t[1]); /* $C35C */
            if (t[5] == 0)
                ; /* $C378 sounds the engine, $206 */
            if (t[5] < 14)
                t[5]++; /* $C38E */
            else
                t[2] = 2; /* $C39E */
            break;

        case 1: /* $C3A8 -- on the ground, rolling out */
            advance_speed(c, 1, slot, t[1]);
            /*  $C3EC computes the distance to the target here and never
             *  looks at it. */
            (void)manhattan(t[3], t[4], t[8], t[9]);
            if (c->anim_phase & 1)
                t[5]--; /* $C408, so it sheds height on alternate frames */
            if (t[5] >= 1)
                break; /* $C41E */
            /*  $C426 sounds it, then the record goes back. */
            c->xtxt[t[3]][t[4]] = t[0x0A]; /* $C458 */
            free_thing(c, slot);           /* $C45E */
            break;

        case 2:                              /* $C468 -- cruising */
            t[1] = (uint8_t)wander(t[1], 5); /* $C47C */
            heli_avoid(c, slot);             /* $C490, shared with the helicopter */
            advance_speed(c, 1, slot, t[1]); /* $C4AC */
            break;

        case 3:                                                     /* $C4B8 -- turning onto the approach */
            t[1] = (uint8_t)bearing_coarse(t[3], t[4], t[8], t[9]); /* $C4E2 */
            heli_avoid(c, slot);                                    /* $C4F6 */
            advance_speed(c, 1, slot, t[1]);                        /* $C512 */
            if (manhattan(t[3], t[4], t[8], t[9]) >= 2)
                break; /* $C552 */

            runway = t[2] >> 4;                          /* $C564 */
            t[1]   = (uint8_t)turn_toward(t[1], runway); /* $C56A */
            t[2]   = (uint8_t)((runway << 4) + 4);       /* $C57C */
            /*  $C58E -- back the target off six tiles along the runway,
             *  so the aeroplane flies past the threshold and turns in. */
            switch (runway)
            {
                case 1:
                    t[9] = (uint8_t)(t[9] - 6);
                    break; /* $C5B0 */
                case 3:
                    t[8] = (uint8_t)(t[8] + 6);
                    break; /* $C5C2 */
                case 5:
                    t[9] = (uint8_t)(t[9] + 6);
                    break; /* $C5D4 */
                case 7:
                    t[8] = (uint8_t)(t[8] - 6);
                    break; /* $C5E6 */
                default:
                    break;
            }
            break;

        case 4:                                                         /* $C5EC -- flying the approach */
            t[1] = (uint8_t)steer_toward(t[1], t[3], t[4], t[8], t[9]); /* $C61E */
            advance_speed(c, 1, slot, t[1]);                            /* $C638 */
            if (manhattan(t[3], t[4], t[8], t[9]) >= 2)
                break; /* $C678 */

            runway = t[2] >> 4; /* $C690 */
            t[1]   = (uint8_t)runway;
            t[2]   = 1; /* $C69E, down to state 1 */
            t[6]   = 8; /* $C6AC, centre it in the tile */
            t[7]   = 8;
            /*  $C6C0 -- and snap the axis it is landing along. */
            if (runway == 1 || runway == 5)
                t[3] = t[8]; /* $C6E6 */
            else if (runway == 3 || runway == 7)
                t[4] = t[9]; /* $C700 */
            break;

        case 7: /* $C70A -- going down */
            if (t[5] == 0)
            {
                t[0]    = 6; /* $C76E */
                t[2]    = 5;
                t[0x0B] = 1;
                t[1]    = 0;
                break;
            }
            t[5]--; /* $C71C */
            if (t[5] == 8)
                ; /* $C734 sounds the crash, $203 */
            /*  $C752 turns the aeroplane as it falls, but moves it on the
             *  heading it had BEFORE the turn. */
            {
                const int old = t[1];
                t[1]          = (uint8_t)((old + 1) & 7);
                advance_speed(c, 1, slot, old); /* $C762 */
            }
            break;

        default: /* 5 and 6 fall straight through to $C79C */
            break;
    }
}

/* ================================================================== *
 *  $09E0A  stepThings -- one pass over the forty records.
 * ================================================================== */
void sim_step_things(City *c)
{
    int slot;

    c->anim_phase++; /* $9E24, A5+0x12F8 */
    if (c->anim_phase > 0x3FF)
        c->anim_phase = 0;

    /*  $9E56 -- the game does not trust an incremented count.  Every
     *  per-type total is cleared here and rebuilt by the steppers. */
    /*  $9E76 -- two ambient sounds, a traffic one and a police siren.
     *  Neither changes any state, but each rolls a Toolbox random every
     *  frame it is eligible, and the steppers draw from that same
     *  stream.  So the rolls have to happen even though the sound does
     *  not: skipping one shifts every later draw in the pass.
     *
     *  Both averages come from the statistics pass at $224DA, which is
     *  not reconstructed, so in practice neither gate opens yet. */
    if (c->graph[GRAPH_TRAFFIC][0] > 0x23 && (Random() & 0xFF) == 0)
        ;                       /* $9E90 sound $209 */
    if (c->census[0xD2] != 0 && /* a police station exists */
        c->graph[GRAPH_CRIME][0] > 0x28 && (Random() & 0xFF) == 0)
        ; /* $9EBC sound $1FA */

    c->plane_count = c->heli_count = c->ship_count = 0;
    c->count_12E6 = c->boat_count = c->monster_count = 0;
    c->road_count = c->tornado_count = 0;

    for (slot = 1; slot < THING_N; slot++) /* $A000 */
    {
        const int type = c->xthg[slot * THING_SZ];
        if (type == 0 || type > 17)
            continue; /* $9ED8 */
        if (!THING_ENABLED[type])
            continue; /* $9EE0 */

        switch (type) /* $9F1C */
        {
            case 10:
            case 12:
                c->road_count++; /* $9FB6, only this arm counts */
                step_train(c, slot);
                break;
            case 9:
                step_boat(c, slot); /* $9F5C */
                break;
            case 3:
                step_ship(c, slot); /* $9F58 */
                break;
            case 2:
                step_heli(c, slot); /* $9F4C */
                break;
            case 1:
                step_plane(c, slot); /* $9F40 */
                break;
            default:
                /*  types 5, 6, 15 and 16 have steppers that are not
                 *  reconstructed yet -- none of them occurs in any
                 *  shipped city.  7 and 8 reach empty ones. */
                break;
        }
    }
}

/* ================================================================== *
 *  $BA7A  spawnHelicopter and $B76E  spawnPlane.
 *
 *  Both end with a call to $A3E4.  That routine only projects the thing
 *  to screen coordinates and unions a redraw rectangle.  It makes no
 *  call and writes nothing outside its own stack frame, so leaving it
 *  out changes no simulation state.
 *
 *  g_disastersActive gates both.  While a disaster runs, nothing new
 *  takes to the air.
 * ================================================================== */
static void spawn_helicopter(City *c, int y, int x)
{
    uint8_t *t;
    int      slot;

    if (c->xtxt[y][x] >= 0xC9)
        return; /* $BA96 */
    if (c->monster_count != 0)
        return; /* $BAAA */
    if (c->heli_count >= 1)
        return; /* $BAB6 */

    slot = alloc_thing(c);
    if (slot == 0)
        return; /* $BAC8 */

    t       = thing(c, slot);
    t[0]    = 2;          /* $BAD8, a helicopter */
    t[1]    = 2;          /* $BAEC */
    t[3]    = (uint8_t)y; /* $BB00 */
    t[4]    = (uint8_t)x;
    t[6]    = 8;
    t[7]    = 8;
    t[8]    = (uint8_t)((uint16_t)Random() % 128); /* $BB2A */
    t[9]    = (uint8_t)((uint16_t)Random() % 128);
    t[5]    = 0;
    t[0x0A] = c->xtxt[y][x];
    t[2]    = 0;

    c->heli_count++;                        /* $BB86 */
    c->heli_timer = 0;                      /* $BB8A, clears A5+0x12F0 */
    c->xtxt[y][x] = (uint8_t)(slot + 0xC9); /* $BB9C */
}

static void spawn_plane(City *c, int y, int x, int kind)
{
    uint8_t *t;
    int      slot, ty, tx;

    if (c->xtxt[y][x] >= 0xC9)
        return; /* $B78E */
    if (c->monster_count != 0)
        return; /* $B7A2 */
    if (c->plane_count >= 2)
        return; /* $B7AE */

    slot = alloc_thing(c);
    if (slot == 0)
        return; /* $B7C0 */

    t    = thing(c, slot);
    t[0] = 1; /* $B7CC */
    t[6] = 8;
    t[7] = 8;

    if ((uint16_t)Random() % 10 >= 5) /* $B7F8, start at the airport */
    {
        ty   = y;
        tx   = x;
        t[8] = 0x14; /* $B9DC */
        t[9] = 0x14;
        t[1] = (uint8_t)kind;
        t[5] = 0;
        t[2] = 0;
    }
    else /* $B80C, fly in from one edge of the map */
    {
        int edge  = (int)(Random() & 3);
        int along = (int)((uint16_t)Random() % 100) + 10;
        switch (edge)
        {
            case 0:
                ty   = 0;
                tx   = along;
                t[1] = 3;
                break; /* $B822 */
            case 1:
                tx   = 0;
                ty   = along;
                t[1] = 5;
                break; /* $B860 */
            case 2:
                ty   = 0x7F;
                tx   = along;
                t[1] = 7;
                break; /* $B89C */
            default:
                tx   = 0x7F;
                ty   = along;
                t[1] = 1;
                break; /* $B8DA */
        }
        t[5] = 0x10; /* $B916 */
        t[2] = (uint8_t)((kind << 4) + 3);
        if (kind == 0)
        {
            t[8] = (uint8_t)y;
            t[9] = (uint8_t)(x + 0x10);
        }
        else
        {
            t[8] = (uint8_t)(y - 0x10);
            t[9] = (uint8_t)x;
        }
    }
    t[3] = (uint8_t)ty;
    t[4] = (uint8_t)tx;

    t[0x0A]         = c->xtxt[ty][tx]; /* $B97C */
    c->xtxt[ty][tx] = (uint8_t)(slot + 0xC9);
    c->plane_count++; /* $BA5E */
}

/* ================================================================== *
 *  $B4CC  headingToward -- an eight point compass from one tile to
 *  another.  A direction counts as diagonal only when the two spans
 *  are within a factor of two of each other.
 * ================================================================== */
static int heading_toward(int fy, int fx, int ty, int tx)
{
    int dy = ty - fy; /* $B4D6 */
    int dx = tx - fx; /* $B4E0 */
    int ay = dy < 0 ? -dy : dy;
    int ax = dx < 0 ? -dx : dx;

    if ((ax + 1) / 2 > ay)
        return dx < 0 ? 0 : 4; /* $B510, mostly sideways */
    if (dy < 0)                /* $B51A, upward */
    {
        if ((ay + 1) / 2 > ax)
            return 6;
        return dx < 0 ? 7 : 5;
    }
    if ((ay + 1) / 2 > ax)
        return 2; /* $B540, downward */
    return dx < 0 ? 1 : 3;
}

/* ================================================================== *
 *  $BBB0  seaportShip -- send a ship in from the edge of the map.
 *
 *  The dice choose one of the four edges, then the whole edge is
 *  scanned for water.  The LAST water tile found wins, because the scan
 *  does not stop at the first.  The ship starts there and heads for the
 *  seaport that called.
 * ================================================================== */
static void seaport_ship(City *c, int y, int x)
{
    int      edge, i, sy = 0, sx = 0, found = 0, slot;
    uint8_t *t;

    if (c->ship_count >= 1)
        return; /* $BBB6, one ship at a time */

    edge = (int)(Random() & 3); /* $BBC6 */
    for (i = 0; i < MAP_W; i++)
    {
        int ty = (edge == 0) ? 2 : (edge == 1) ? 0x7E
                                               : i;
        int tx = (edge == 2) ? 2 : (edge == 3) ? 0x7E
                                               : i;
        if (c->xter[ty][tx] == 0x10) /* $BBEE, open water */
        {
            sy    = ty;
            sx    = tx;
            found = 1; /* keeps the LAST match */
        }
    }
    if (!found)
        return; /* $BC9A */
    if (c->xtxt[sy][sx] >= 0xC9)
        return; /* $BCAE */

    slot = alloc_thing(c);
    if (slot == 0)
        return; /* $BCC2 */

    t       = thing(c, slot);
    t[0]    = 3;                                     /* $BCCE, a ship */
    t[1]    = (uint8_t)heading_toward(sy, sx, y, x); /* $BCE4 */
    t[3]    = (uint8_t)sy;                           /* $BCF2 */
    t[4]    = (uint8_t)sx;
    t[6]    = 8;
    t[7]    = 8;
    t[5]    = 1;
    t[0x0A] = c->xtxt[sy][sx];
    t[2]    = 0;

    c->ship_y = (int16_t)sy; /* $BD46 */
    c->ship_x = (int16_t)sx;
    /*  $BD52 calls $3F636, which does nothing unless the machine
     *  reports more than $9C40 bytes free.  The reconstruction models
     *  no memory pressure, so that gate stays shut on both sides. */
    c->ship_count++;                          /* $BD62 */
    c->xtxt[sy][sx] = (uint8_t)(slot + 0xC9); /* $BD6E */
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
static void auto_marina(City *c, int y, int x)
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
 *  $333C8  placeSpecial -- grows the furniture that belongs to a
 *  military base, an airport or a seaport.  Dispatches on the building
 *  id: $E1..$E8 and $EA are one tile, $EE..$F2 and $F6 are two by two,
 *  $F9 is the military three by three.  $DD and $E0 have handlers of
 *  their own ($33844, $33A90) that the growth scan never reaches.
 *
 *  Returns $FF when the caller should consider the job done and 0 when
 *  it should fall back to a smaller building.  Note that several
 *  "rejections" still return $FF -- a tile that is already built on
 *  counts as done, not as a failure.
 * ================================================================== */
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

int trip_mark_log;

/*  $332C6 -- may this tile join a building anchored nearby?  It has to
 *  be on the map, at the same altitude, in the same zone, carrying
 *  nothing bigger than `maxbld`, and be neither road nor rail. */
static int tile_fits(const City *c, int y, int x, int alt, int zone, int maxbld)
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

    /*  $326C4 -- the edge test uses the HALF extent, not the full one.
     *  The original carries a size of 1, 2, 3 or 4 by tier and halves
     *  it, which gives 0, 1, 1, 2 -- exactly n - 1 here.  Testing
     *  against n instead rejects a row and a column that the game
     *  accepts, so a building at row 125 or column 125 never appeared.
     *  That is why every remaining difference sat at the map edge. */
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

/*  $33028 -- grow the building at (y,x) into what its new tier needs.
 *  The coin the caller tossed decides between two shapes at every tier
 *  above the first: one big building, or several small ones. */
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
     *  rotation so the building is simulated exactly once.
     *
     *  The four writes are unrolled in the original and they do NOT run
     *  in raster order: $328EC takes the top-left, $32916 the
     *  bottom-left, $32938 the bottom-right and $3296E the top-right.
     *  Every other footprint in the game goes round the block the same
     *  way, so walking this one in rows put 0x20 and 0x40 on the wrong
     *  diagonal -- which the renderer reads as a church facing the
     *  other way. */
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

/* ================================================================== *
 *  tripGenerate ($245E8) -- can a journey be made from here?
 *
 *  Asked "can a building of this zone and tier work here", the game
 *  answers by trying to make a journey.  It steps onto the nearest
 *  transport tile, then walks the network at random -- turning
 *  consistently left or right, never immediately doubling back --
 *  spending a length budget as it goes.  The trip succeeds if it
 *  reaches a tile whose zone the starting zone wants (ZONE_ATTRACTS:
 *  homes want shops and factories, shops want homes and factories) or
 *  reaches a road off the edge of the map, which is a neighbouring
 *  city.  It fails if the budget runs out or the network dead-ends.
 *
 *  Every accepted step is pushed on the shared ring, and on the way out
 *  the whole route is drained again and stamped into XTRF.  That is
 *  where traffic comes from: it is not modelled, it is the residue of
 *  journeys that were actually attempted.
 * ================================================================== */

/*  The ring at $13B2 is a queue for the flood fills and a stack here,
 *  so it needs both ends.  $21E66 pops the newest entry, $21E96 reads
 *  it without removing it. */
static void q_pop_back(int *y, int *x) /* queuePopBack $21E66 */
{
    qw = (qw + QMASK) & QMASK;
    *y = q[qw].y;
    *x = q[qw].x;
}

static void q_peek_back(int *y, int *x) /* queuePeekBack $21E96 */
{
    int i = (qw + QMASK) & QMASK;
    *y    = q[i].y;
    *x    = q[i].x;
}

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
 *  $247EC  walkStep -- one step of a trip.
 *
 *  A trip walks the transport network from a zoned tile and looks for a
 *  zone that satisfies it.  The mode says what the traveller is on now.
 *  The cost is travel time, and it is charged against a budget of 100.
 *  When the cost reaches the budget the trip fails.
 *
 *  The cost per tile IS the traffic model:
 *      road        3        highway     1
 *      rail        1        subway      1
 *      board a bus, a train or a subway        4, once
 *  A road-only city spends the whole budget in about 25 tiles.  The
 *  same budget carries a trip 75 tiles along rail.  Transit works in
 *  this game because it makes the journey cheaper, not because it adds
 *  capacity.
 *
 *  Modes 0 to 3 and modes 4 to 7 are the same four states twice.  The
 *  second set means "this trip has used a bus".  A bus makes every
 *  later road tile cost 2 instead of 3.
 *
 *      0/4  road      1/5  highway    2/6  bridge or tunnel
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

    /*  $24A78 and $24AD0.  On a highway.  A highway costs 1 a tile, so
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

    /*  $24602 -- the first transport tile in the search order starts the
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
                /*  Walked off the map.  $247C0: if the tile we are
                 *  standing on is marked 0xFA in XTXT the road leaves
                 *  for a neighbouring city, and that counts as arriving. */
                if (c->xtxt[cy][cx] == 0xFA)
                {
                    moved   = 1;
                    arrived = 1;
                }
                continue;
            }
            {
                /*  $247EC -- the fourteen-case switch, as a table.  The
                 *  subway modes read the underground layer. */
                /*  $247EC, transcribed above.  This replaced a
                 *  generated [mode][tile] table of 7,168 entries that
                 *  could not express three of the fourteen arms. */
                WalkStep        step = walk_step(c, mode, ny, nx);
                const WalkStep *w    = &step;

                /*  $24848 -- arriving is decided first, and by the zone
                 *  the trip started in rather than by the table. */
                /*  Modes 0, 4, 8 and 9 test for arrival -- $2483C,
                 *  $2496A, $24EE0 and $24F88.  A trip that is riding a
                 *  bus can still get off at its destination; one in a
                 *  tunnel or on the subway cannot. */
                if (mode == 0 || mode == 4 || mode == 8 || mode == 9)
                {
                    int z = XZON_TYPE(c->xzon[ny][nx]);
                    if (ZONE_ATTRACTS[zone] & (1 << z))
                    {
                        /*  $24850 sets the two flags and nothing else:
                         *  arriving does not advance the position, so
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
            /*  $250F8 -- dead end, so unwind to the last junction that
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
        /*  $251CE..$251E8 -- all three stacks are written at the
         *  CURRENT sp and only then is sp incremented.  The mode goes in
         *  last but still at the old index: the compiler kept it in d0
         *  across the increment. */
        mask_st[sp] = (uint8_t)mask;
        len_st[sp]  = (uint8_t)len;
        mode_st[sp] = (uint8_t)mode;
        sp++;
    }

    if (trip_mark_log)
        fprintf(stderr, "TRIP %d %d zone=%d tier=%d arrived=%d len=%d sp=%d\n", y, x, zone, tier, arrived, len, sp);

    /*  $25204 -- a journey that failed leaves no traffic behind: the
     *  route is only drained and stamped when the trip arrived. */
    if (!arrived || tier <= 0)
        return arrived != 0;

    /*  $25216 -- walk the route back out.  Only the surface modes leave
     *  traffic; riding the subway does not put cars on the road. */
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
 *  growFootprint ($32998) -- grow a zone into a bigger building.
 *
 *  An empty lot just gets a one-tile building.  Growing a one-tile
 *  building into a 2x2 needs three free tiles beside it, and there are
 *  four ways round that can fall; the game tries them in a fixed order
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

/* ================================================================== *
 *  growthScan ($3170E) -- phases 3 through 18.
 *
 *  Sixteen phases each walk a quarter-offset lattice: phase (y0,x0)
 *  visits y = y0, y0+4, ... and x = x0, x0+4, ..., so over one cycle
 *  every tile is visited exactly once.  Per tile it does one of three
 *  things:
 *
 *    unzoned, infrastructure     with probability 1/128, roll against
 *                                the owning department's funding level
 *                                and let the tile rot if underfunded
 *    unzoned, special building   the automatic builds (rail station,
 *                                marina, arcology upkeep)
 *    zoned                       accumulate this building's population,
 *                                then roll for growth or decay
 *
 *  The population accumulator is the important output: $33FAE at phase
 *  21 sums accum8[1..6] and multiplies by ten, and that is the number
 *  on the status bar.
 * ================================================================== */

int32_t sim_growth_unimplemented(void) { return growth_todo; }

/*  $324B8 -- is this tile, or one of its four neighbours, powered?
 *  Reads only XBIT bit 6, and stops at the first hit. */
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

/*  $24530 -- can a zone here develop?  True when some transport tile
 *  lies within an L1 distance of 3, centre excluded.  The offset list
 *  in the binary is exactly that diamond: all 24 cells, confirmed by
 *  probing every offset in a 7x7 box. */
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

                /*  $317C0 -- one tile in 128 is considered for decay.
                 *  The department that pays for it is the same one the
                 *  budget charges, and the ranges are tested in a fixed
                 *  order, so a tile owned by two departments decays
                 *  against the first of them. */
                if (game_rand127() == 0)
                {
                    static const int ORDER[5] = {DEPT_ROAD, DEPT_RAIL, DEPT_SUBWAY, DEPT_POWER, DEPT_HIGHWAY};
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
                        if (d == DEPT_POWER || d == DEPT_HIGHWAY)
                        {
                            /*  Power lines are removed through $5FAA and
                             *  highways through an eight-tile teardown;
                             *  neither is reconstructed. */
                            growth_todo++, growth_stub[2]++;
                            break;
                        }
                        sim_set_tile(c, y, x, (uint8_t)((Random() & 3) + 1)); /* $31824 */
                        c->xbit[y][x] &= (uint8_t)~XBIT_CONDUCTIVE;
                    }
                    if (done && bld < 0x70)
                        continue; /* went to the next tile */
                }

                /*  $31B30 -- the automatic builds.  Reached whether or
                 *  not the decay roll above fired.  A rail station or a
                 *  marina with power will, one time in four, put another
                 *  of itself nearby -- but only while the city wants
                 *  more of them than have been placed this cycle. */
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
                /*  $31BD0 -- an arcology scores its own quality of
                 *  life once a cycle, 0 to 12.  Crime and pollution
                 *  take from it and land value adds to it, each scaled
                 *  down by 32.  Losing power halves the score.  Losing
                 *  water halves it again.  The score lives in byte 1 of
                 *  the arcology's XMIC record. */
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
                /*  $31FDA -- a military base, airport or seaport grows
                 *  its own furniture rather than zone buildings, and
                 *  only one time in four.  The placement itself goes
                 *  through $333C8, which is not reconstructed; the dice
                 *  are reproduced here so the tiles after this one still
                 *  see the sequence the original gave them. */
                if (zone == 7)
                {
                    /*  $31FE2 -- which stage the base has reached picks
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
                        /*  the big one first; if the map will not take
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
                    { /* stage 3, $320E6 -- a seven-way ladder, no fallback */
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
                        /*  $321C6 -- not this tile's turn; a pier may
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
                        /*  $3226E -- the runway spawns aircraft, which
                         *  need the moving object system. */
                        if (bld != 0xDD)
                            continue;
                        if ((uint16_t)Random() % 30 != 0)
                            continue;
                        if (!(c->xbit[y][x] & XBIT_POWERED))
                            continue; /* $3229E */
                        /*  $322A6 -- four times in ten a helicopter
                         *  leaves the airport.  Otherwise a plane does,
                         *  and the map rotation together with XBIT bit
                         *  1 picks which of the two plane kinds. */
                        if ((uint16_t)Random() % 10 < 4)
                            spawn_helicopter(c, y, x); /* $322BC */
                        else if (c->rotation & 1)      /* $322CC */
                            spawn_plane(c, y, x, (c->xbit[y][x] & 0x02) ? 0 : 2);
                        else /* $32314 */
                            spawn_plane(c, y, x, (c->xbit[y][x] & 0x02) ? 2 : 0);
                        continue;
                    }
                    /*  $32352 -- the same seven-way ladder the military
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

                /*  $31D5C -- a tile with no power nearby, or from
                 *  which no journey can be made, has no demand at all
                 *  and the whole 4000 of headroom. */
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

                /*  $31DCC -- the population accumulator.  This runs
                 *  whichever way the branch above went, which is why it
                 *  can be reconstructed before the placement engine. */
                if (tier > 0 && BUILDING[bld].tier_flag == 0)
                {
                    c->accum8[zone] += GROWTH_TABLE[tier];
                    /*  $31E0A -- roll for growth against the headroom
                     *  this tier still has. */
                    if ((int32_t)(uint16_t)Random() < head / tier)
                    {
                        /* $31E1A -- grow into the next tier */
                        sim_upgrade(c, y, x, tier, Random() & 1);
                        continue;
                    }
                }
                if (BUILDING[bld].tier_flag == 1)
                { /* $31E38 */
                    if ((uint16_t)Random() < (uint16_t)(0x4000 / tier))
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
                    if ((int32_t)(uint16_t)Random() < (15 * demand) / tier)
                        sim_place(c, y, x, tier, (zone - 1) / 2); /* $31EFE */
                    continue;
                }
                /*  $31F0A -- an empty or under-built zone grows into
                 *  the next tier, but only where the land is worth it:
                 *  each tier has a land-value floor, and industry is
                 *  exempt from all of them. */
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

/* ================================================================== *
 *  budgetPass ($263C8) -- the monthly budget pass, phase 0.
 *
 *  This is what police and fire coverage were waiting on.  It keeps
 *  sixteen department records: each holds an `amount` recomputed from
 *  the tile census, a `funding` level the mayor sets, and the year's
 *  accrual of their product, settled into the treasury every January.
 *  Coverage reads the police and fire funding levels straight out of
 *  it, which is why nothing could compute XPLC or XFIR until the block
 *  itself was located in MISC.
 *
 *  Service buildings are counted in tiles, so dividing by 9 or 16 turns
 *  a tile count back into a building count.  The infrastructure ranges
 *  overlap on purpose: a bridge tile is charged to both the road and
 *  the highway department.
 * ================================================================== */

int32_t sim_ordinance_cost(const City *c, int which) /* ordinanceCost $41368 */
{
    OrdinanceCost o;
    int32_t       v;

    if (which < 0 || which >= 20)
        return 0;
    o = ORDINANCE_COST[which];
    if (o.source < 0)
        return 0;
    if (o.source == 3) /* $41500 */
        return -c->population - c->misc[MISC_2C98];
    v = c->dept[o.source].amount * o.num;
    return v / o.den;
}

void sim_budget(City *c)
{
    int i, t;

    /*  The January reconciliation, $263E0.  It only runs if last
     *  December armed it, and it divides the year's accrual by twelve
     *  times the department's own divisor -- positive for the four
     *  revenue departments, negative for the twelve that spend. */
    if (c->year_end && c->month == 0)
    {
        for (i = 0; i < N_DEPT; i++)
        {
            int32_t div = 12 * DEPT_YEAR_DIVISOR[i];
            if (div)
                c->funds += c->dept[i].accrued / div;
            c->dept[i].accrued = 0; /* $2642C */
        }
        c->year_end = 0; /* $26438 */
        /*  $26442 -- and with the year closed, every special building
         *  on the map takes its turn.  Inside the year-end block, after
         *  the flag is cleared, exactly where the original calls it. */
        sim_microsim(c);
    }

    for (i = 0; i < N_DEPT; i++)
    { /* $26482 */
        c->dept[i].history_amount[c->month]  = c->dept[i].amount;
        c->dept[i].history_funding[c->month] = c->dept[i].funding;
        c->dept[i].accrued += c->dept[i].amount * c->dept[i].funding;
    }
    if (c->month == 11)
        c->year_end = 1; /* $264D8 */

    c->dept[DEPT_BONDS].amount   = c->bonds;            /* $264E6 */
    c->dept[DEPT_POLICE].amount  = c->census[0xD2] / 9; /* $264F0 */
    c->dept[DEPT_FIRE].amount    = c->census[0xD3] / 9;
    c->dept[DEPT_HEALTH].amount  = c->census[0xD1] / 9;
    c->dept[DEPT_SCHOOL].amount  = c->census[0xD6] / 9;
    c->dept[DEPT_COLLEGE].amount = c->census[0xD9] >> 4; /* 4x4 */

    for (i = 10; i <= 15; i++)
        c->dept[i].amount = 0; /* $26572 */

    for (t = 0x1D; t < 0x70; t++)
    { /* $2658C */
        uint16_t m = BUILDING[t].dept;
        for (i = 10; i < N_DEPT; i++)
            if (m & (uint16_t)(1u << i))
                c->dept[i].amount += c->census[t];
    }

    /*  Three station types are charged after the loop rather than in
     *  it.  The bus term is truncated to sixteen bits before it is
     *  added, which matters once a city has more than about a thousand
     *  bus stops. $26696, $266AE, $266C0 */
    c->dept[DEPT_TRANSIT].amount =
        (uint16_t)(c->transit_term + c->census[0xE9]);
    c->dept[DEPT_RAIL].amount += c->census[0xED];
    c->dept[DEPT_ROAD].amount +=
        (uint16_t)((c->census[0xEC] >> 2) * 250);

    c->dept[DEPT_ORDINANCE].amount = 0; /* $266DA */
    for (i = 0; i < 20; i++)
        if (c->ordinances & ((int32_t)1 << i))
            c->dept[DEPT_ORDINANCE].amount += sim_ordinance_cost(c, i);

    /*  $2670A -- and then, one month in eight, a city with money in the
     *  bank finds an ordinance has been passed without it.  The
     *  treasury has to beat a random figure plus fifty thousand, so it
     *  only happens to a rich city, and the newspaper announces it.
     *
     *  The same A5+0x13AA switch that turns disasters off turns this
     *  off too, which is the only reason to think of it as one.  Both
     *  its dice are drawn whenever the switch is on, so a model that
     *  leaves it out is a draw short every month. */
    if (!c->disasters_off && (Random() & 7) == 0)
    {
        int32_t bar = (int32_t)(uint16_t)Random() + 50000; /* $26726 */
        if (bar < c->funds)                                /* $2672C */
        {
            int k = (int)((uint16_t)Random() % 20); /* $2673C */
            c->ordinances |= (int32_t)1 << k;       /* $26748 */
        }
    }
}

/* ================================================================== *
 *  $2317E stages 6-7 -- population density.
 *
 *  Every building contributes a value from the table at A5-0x3982 into
 *  a 32x32 accumulator; the density is four times that, saturated to a
 *  byte.  Specials outside the zone range contribute a flat 2, except
 *  arcologies (0xFB..0xFE) which contribute 12.
 *
 *  The growth-rate layer that follows it, XROG, is an exponential
 *  average of the CHANGE in density -- (7*old + 8*delta + 128) / 8 --
 *  so it needs the pre-pass state and cannot be rebuilt from a save.
 * ================================================================== */
void sim_density(City *c)
{
    /*  Density is not a pass of its own: it is the tail of the same map
     *  walk that lays down police and fire coverage, and shares that
     *  walk's accumulator.  Kept as a name because that is what the
     *  layer is called. */
    sim_coverage(c);
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

/* ================================================================== *
 *  $3152A  opinionPoll -- the February poll.
 *
 *  Once a year, in month 2, phase 0 stops a hundred imaginary citizens
 *  in the street and asks each one what is wrong with the city.  The
 *  answer is not computed from the indicators directly: each of the
 *  seven complaints is given a weight, contentment is given a weight of
 *  its own, and the hundred answers are drawn from that distribution.
 *  So a city with a little crime still returns a few people who name
 *  crime, and the ranking wobbles from year to year even when nothing
 *  has changed.
 *
 *  The seven weights ($31544 to $315AE) are the raw indicators, not
 *  normalised: the three map averages as they stand, the tax rate
 *  tripled, the unemployment count, and the two shortfalls -- how far
 *  education is below 100 and life expectancy below 70.  A city that
 *  is over those two marks contributes nothing from them.
 *
 *  Contentment is 50 plus the land value average ($315B2).  That is the
 *  whole of the poll's optimism, and it is why a rich city polls well
 *  even with problems: land value is measured in the hundreds while the
 *  complaints are usually in the tens.
 *
 *  Two details are easy to get wrong.  The map averages are read as
 *  words from the middle of a long ($31548 reads offset 2 of the graph
 *  slot), so only the low sixteen bits count -- contentment takes the
 *  whole long.  And the tax term reads the same department three times
 *  over ($31568 loads a1 and a0 from one pointer), so it is three times
 *  the residential rate and not the sum of the three tax rates.
 *
 *  Afterwards the seven are sorted worst first ($31656, a bubble sort
 *  on the index array) and the sorted counts are kept beside them.  The
 *  newspaper and the advisors read the ranking; nothing else does.
 * ================================================================== */
int sim_opinion_poll(City *c)
{
    int16_t w[N_PROBLEM];
    int16_t counts[N_PROBLEM];
    int16_t index[N_PROBLEM];
    int16_t was;
    int32_t total;
    int     i, j, n;

    /*  $31544 -- the three map overlays, low word only */
    w[PROBLEM_TRAFFIC]   = (int16_t)c->graph[GRAPH_TRAFFIC][0];
    w[PROBLEM_POLLUTION] = (int16_t)c->graph[GRAPH_POLLUTION][0];
    w[PROBLEM_CRIME]     = (int16_t)c->graph[GRAPH_CRIME][0];

    /*  $31562 -- three times department 0's rate, see above */
    w[PROBLEM_TAXES] = (int16_t)(c->dept[0].funding * 3);

    /*  $31578 -- the low word of the unemployment count */
    w[PROBLEM_UNEMPLOYMENT] = (int16_t)c->unemployment;

    /*  $3157E and $31596 -- the two shortfalls, floored at zero */
    w[PROBLEM_EDUCATION] = (int32_t)c->misc[MISC_AGE_W90] >= 100
                               ? 0
                               : (int16_t)(100 - c->misc[MISC_AGE_W90]);
    w[PROBLEM_HEALTH]    = (int32_t)c->misc[MISC_AGE_W65] >= 70
                               ? 0
                               : (int16_t)(70 - c->misc[MISC_AGE_W65]);

    /*  $315B2 -- contentment, then every complaint on top of it */
    total = 50 + c->graph[GRAPH_VALUE][0];
    for (i = 0; i < N_PROBLEM; i++)
    {
        total += w[i];          /* $315C0 */
        counts[i] = 0;          /* $315CA */
        index[i]  = (int16_t)i; /* $315D4 */
    }

    /*  $315EA -- a city with no weight at all, or under a hundred
     *  people, is not polled and keeps last year's ranking. */
    if ((int16_t)total == 0 || c->population < 100)
        return SIM_EV_NONE;

    was         = c->approval; /* $315FA */
    c->approval = 0;           /* $31600 */

    /*  $31608 -- a hundred citizens, each landing in one bucket */
    for (n = 0; n < 100; n++)
    {
        int r = (uint16_t)Random() % (uint16_t)(int16_t)total; /* $31612 */

        for (i = 0; i < N_PROBLEM; i++) /* $3161C */
        {
            if (r < w[i])
                break;
            r -= w[i];
        }
        if (i == N_PROBLEM)
            c->approval++; /* $3163A -- nothing to complain about */
        else
            counts[i]++; /* $31646 */
    }

    /*  $31656 -- sort the index worst first, then read the counts back
     *  through it so the two arrays line up. */
    for (n = N_PROBLEM - 1; n > 0; n--)
        for (j = 0; j < n; j++)
            if (counts[index[j]] < counts[index[j + 1]])
            {
                int16_t t    = index[j];
                index[j]     = index[j + 1];
                index[j + 1] = t;
            }
    for (i = 0; i < N_PROBLEM; i++) /* $316B2 */
    {
        c->problem_rank[i]  = index[i];
        c->problem_votes[i] = counts[index[i]];
    }

    /*  $316D6 -- crossing four fifths approval is congratulated once,
     *  on the way up only. */
    return (was < 80 && c->approval >= 80)
               ? SIM_EV_APPROVAL /* $316EA sound, $316FC message */
               : SIM_EV_NONE;
}

/* ================================================================== *
 *  $101AC  microsimPass -- the year-end turn of every special building.
 *
 *  budgetPass calls it once a year, from the January settlement, right
 *  after the sixteen departments have been reconciled into the treasury
 *  ($26442).  It is the only thing in the game that walks XMIC.
 *
 *  XMIC is 150 records of eight bytes at A5+0x2BC6, one per special
 *  building on the map: the power plants, the stations, the schools and
 *  hospitals, the arcologies, the marina.  Records 1 to 149 are walked
 *  in order; record 0 is never used.  Each record's first byte is a
 *  building id, and `$1027C` dispatches on it -- 58 entries covering
 *  $C6 to $FF, of which 26 are distinct and two are "do nothing".
 *
 *  What the three words in a record mean is the type's own business.  A
 *  power plant keeps its age; a marina keeps how many boats it has.
 *  There is no common schema and pretending there is one would be an
 *  invention.
 * ================================================================== */

/*  The record, in place in the loaded chunk.  XMIC is stored big-endian
 *  like every other chunk, so the words are read and written through
 *  helpers rather than cast. */
static uint8_t *micro_rec(const City *c, int i)
{
    if (!c->xmic || (size_t)(i * 8 + 8) > c->xmic_len)
        return NULL;
    return c->xmic + i * 8;
}

static int micro_w(const uint8_t *r, int k)
{
    return (int)(int16_t)((uint16_t)r[2 + k * 2] << 8 | r[3 + k * 2]);
}

static void micro_set_w(uint8_t *r, int k, int v)
{
    r[2 + k * 2] = (uint8_t)((v >> 8) & 0xFF);
    r[3 + k * 2] = (uint8_t)(v & 0xFF);
}

/*  $11246, twenty callers.  What a building would like, capped by what
 *  the city can support: `min(want, population / per)`.  A `per` of
 *  zero means one per hundred people. */
/*  $1127E.  Which tile owns micro record `rec`?  XTXT carries the
 *  record index offset by 0x33 on the building's own tile, so the
 *  search is for that one byte.  Returns (col << 8) | row, or 0.
 *
 *  `moveq #$cd` at $112BA is -51, not 205: the test is
 *  `rec == xtxt - 51`, which is the offset read the other way. */
static int micro_find_tile(const City *c, int rec)
{
    int y, x;
    for (y = 0; y < MAP_H; y++)
        for (x = 0; x < MAP_W; x++)
        {
            int v = c->xtxt[y][x];
            if (v == 0 || v < 0x33 || v >= 0xC9) /* $112A6 */
                continue;
            if (rec == v - 51)
                return (x << 8) | y; /* $112C2 */
        }
    return 0; /* $112DE */
}

/*  $10392.  What the city is charged to put a worn-out plant back:
 *  A5-0x5198 maps the plant's id onto an index, and A5+0x616 is the
 *  cost table the build menu prices from. */
static int32_t micro_rebuild_cost(int type)
{
    int n;
    if (type < 0xC6 || type > 0xD0)
        return 0;
    n = MICRO_REBUILD_IDX[type - 0xC6];
    return (n >= 0 && n < 24) ? BUILD_COST[n] : 0;
}

/*  The age pyramid's head count for one five-year bracket -- A5+0x1EDE
 *  as the microsim reads it, three longs to a bracket. */
#define HEADS_AT(c, b) ((c)->misc[MISC_HIST_BASE + 3 * (b)])

static int micro_cap(const City *c, int want, int per)
{
    /*  Everything here is a WORD.  $1124E and $11252 read both arguments
     *  as words, $1126A truncates the quotient to one, and $1126C
     *  compares them SIGNED.  A big city divides out past 32767 and the
     *  comparison then reads it as negative, so the cap does not clamp
     *  -- it wraps, and the museum in a million-strong city reports a
     *  negative attendance.  Computing this in ints gives a tidier
     *  answer and the wrong one. */
    int16_t w = (int16_t)want;
    int16_t p = (int16_t)per;
    int16_t have;
    if (p == 0)
        p = 100; /* $1125A */
    have = (int16_t)((uint32_t)c->population / (uint32_t)(int32_t)p);
    return have >= w ? (int)w : (int)have; /* $1126C, signed */
}

void sim_microsim(City *c)
{
    int i;
    /*  $101E4 -- a2, the ceiling nearly every staffed building uses. */
    const int pop_50 = (int)((uint32_t)c->population / 50u);
    /*  What the loop accumulates.  $101B4 counts the Launch Arcos it
     *  passes; $101DA totals the police workload, which the tail stores
     *  at A5+0x2C92; $101DE totals the population living in arcologies.
     *
     *  $101D4 takes a copy of the police term BEFORE $101E0 clears it,
     *  because the police arm divides by five minus last year's value
     *  while the tail is computing this year's. */
    int       launch_arco  = 0;
    int32_t   police_load  = 0;
    int32_t   arco_pop     = 0;
    const int police_term0 = c->police_term;
    /*  $10240 -- raised when any school's rating falls under four. */
    int school_failing = 0;
    int riot_brewing   = 0;

    /*  $101B4 -- the counts the arms share, each a tile census divided
     *  by the building's footprint. */
    const int arco_n    = (c->census[0xFB] + c->census[0xFC] + c->census[0xFD] +
                           c->census[0xFE]) >>
                          4;                    /* $101F8, 4x4 */
    const int prison_n  = c->census[0xD8] >> 4; /* $10210, 4x4 */
    const int school_n  = c->census[0xD6] / 9;  /* $1021A, 3x3 */
    const int college_n = c->census[0xD9] >> 4; /* $10228, 4x4 */
    const int hosp_n    = c->census[0xD1] / 9;  /* $10232, 3x3 */

    (void)arco_pop;
    (void)arco_n;
    (void)school_n;
    (void)college_n;
    (void)hosp_n;

    for (i = 1; i < N_MICRO; i++)
    {
        uint8_t *r = micro_rec(c, i);
        int      t;
        if (!r)
            break;
        t = r[0];
        if (t == 0) /* $1025E, an empty slot */
            continue;
        if (t < 0xC6 || t > 0xFF) /* $10266, off the table */
            continue;
        switch (t)
        {
            /*  $1042C -- the stadium.  Attendance is the city divided
             *  between its stadiums, held under 25000, and then capped
             *  again at a fifth of the population. */
            case 0xD7:
                {
                    int32_t att;
                    int     n = c->census[0xD7] < 1 ? 1 : (int)c->census[0xD7];
                    att       = (int32_t)((uint32_t)c->population / (uint32_t)n);
                    if (att > 0x61A8)
                        att = 0x61A8 - (int32_t)(Random() & 0xFF); /* $10456 */
                    att = micro_cap(c, (int)att, 5);
                    micro_set_w(r, 0, (int)(att + (int32_t)(Random() & 0xFF)));
                    r[1] = (uint8_t)((Random() & 0x1F) + 9); /* $10498 */
                    break;
                }

            /*  $104B6 -- city hall.  One number, and it is the only arm
             *  that takes both of $11246's arguments as constants: the
             *  long pushed at $104B6 is two words, 200 then 900. */
            case 0xD0:
                micro_set_w(r, 0, micro_cap(c, 0xC8, 0x384));
                break;

            /*  $10614 -- a prison.  Three quarters of last year's
             *  inmates stay, the city's police workload sends more, and
             *  what comes out is added to the police term -- so the
             *  prisons, not the stations, are what set the coverage
             *  radius at the end of the pass. */
            case 0xD8:
                {
                    int32_t held = (int32_t)(uint16_t)micro_w(r, 0);
                    int32_t rate;
                    int     n = prison_n < 1 ? 1 : prison_n;
                    held -= ASR(held + (int32_t)((uint32_t)ASR(held, 1) >> 30), 2);
                    /*  $10646 reads the GLOBAL at A5+0x2C92, which the
                     *  pass does not write until its tail -- so a prison
                     *  sees LAST year's city-wide police workload, not the
                     *  total this year's loop is busy accumulating.  Using
                     *  the running total instead is wrong by a year and the
                     *  arithmetic never gives it away. */
                    held += c->misc[MISC_POLICE_LOAD] / n;
                    if (held > 0x2710)
                        held = (int32_t)(Random() & 0x3FF) + 0x2710; /* $1065E */
                    micro_set_w(r, 0, micro_cap(c, (int)held, 0x14));
                    micro_set_w(r, 1, micro_cap(c, (int)(c->dept[DEPT_POLICE].funding * 3), 0x78));
                    rate = held / 100; /* $106B6 */
                    micro_set_w(r, 2, (int)rate);
                    c->police_term = (int16_t)(c->police_term + rate); /* $106D0 */
                    if (rate > 0x69)
                        riot_brewing = 1; /* $106DC */
                    if (rate <= 0x5A)
                    {
                        r[1] = 0; /* $10732 */
                        break;
                    }
                    /*  $106E8 -- `moveq #$a6` is -90, so this is rate - 90. */
                    rate = rate - 90 + (100 - c->dept[DEPT_POLICE].funding) / 10;
                    r[1] = (uint8_t)(rate ? (int32_t)Random() % rate : 0);
                    break;
                }

            /*  $10C56, $10C5A -- the arcologies.  Their residents are
             *  what the structure holds, what the region's arcologies
             *  can between them support, and what the tax rates allow;
             *  the smallest wins, and the total is the population term
             *  graph series 0 adds to the head count.
             *
             *  $FE, the Launch Arco, counts itself on the way past --
             *  see the end of the pass for what that is for. */
            case 0xFE:
                launch_arco++; /* $10C56, then falls through */
                /* fall through */
            case 0xFB:
            case 0xFC:
            case 0xFD:
                {
                    int32_t held = (int32_t)(uint16_t)micro_w(r, 0) * 1000;
                    int32_t room, tax;
                    int     n = arco_n < 1 ? 1 : arco_n;
                    room      = micro_cap(c, (int)(held / 10), n * 20);
                    tax       = ((20 - c->dept[0].funding) + (20 - c->dept[1].funding) +
                                 (20 - c->dept[2].funding)) /
                                6;                                        /* $10CA8 */
                    tax       = ((tax + (int32_t)r[1]) * 2) * 100 - 2000; /* $10CE0 */
                    if (tax < room)
                        room = tax; /* $10CF4 */
                    {
                        int32_t was = (int32_t)(uint16_t)micro_w(r, 1);
                        room += was / 50; /* $10D12 */
                        room += was;
                        if (room > held)
                            room = held;                /* $10D44 */
                        room += (int32_t)game_rand63(); /* $10D4C */
                        micro_set_w(r, 1, (int)room);
                        arco_pop += (int32_t)(uint16_t)micro_w(r, 1); /* $10D76 */
                    }
                    break;
                }

            /*  $10D7C -- the Llama Dome, which reports three figures
             *  about llamas and derives them from the city's size and
             *  four dice.  It is a joke building and the code treats it
             *  as one. */
            case 0xFF:
                {
                    int32_t d3;
                    r[1] = (uint8_t)(Random() & 0xFF);
                    d3   = (int32_t)((uint32_t)c->population >> 3) +
                           (int32_t)(Random() & 0x3FF);
                    micro_set_w(r, 0, (int)d3);
                    micro_set_w(r, 1, (int)(ASR(d3 + (int32_t)((uint32_t)ASR(d3, 2) >> 29), 3) + (int32_t)(Random() & 0x7F)));
                    micro_set_w(r, 2, (int)(ASR(d3 + (int32_t)((uint32_t)ASR(d3, 3) >> 28), 4) + (int32_t)(Random() & 0x3F)));
                    break;
                }

            /*  $102F0 -- the seven burning power plants.  They age a
             *  year, report an output that wanders a little around the
             *  supplied percentage, get a newspaper story at
             *  forty-eight, and at fifty they are finished.
             *
             *  Which way "finished" goes depends on the DISASTER switch,
             *  and not the way round you would guess: with disasters
             *  turned OFF ($13AA set) the city quietly rebuilds the
             *  plant and bills you for it, falling back to demolition
             *  if the treasury cannot cover it.  With disasters ON the
             *  plant simply goes. */
            case 0xC9:
            case 0xCA:
            case 0xCB:
            case 0xCC:
            case 0xCD:
            case 0xCE:
            case 0xCF:
                {
                    int at;
                    r[1]++; /* $102FA */
                    micro_set_w(r, 1, (int)(c->power_pct + (int32_t)(Random() & 7)));
                    /*  $1032E -- the story at forty-eight is interface. */
                    if (r[1] <= 0x32) /* $10358 */
                        break;
                    at = micro_find_tile(c, i); /* $1127E */
                    if (at == 0)
                        break;
                    {
                        int y = at & 0xFF, x = (at >> 8) & 0xFF;
                        if (c->disasters_off) /* $10380 */
                        {
                            int32_t cost = micro_rebuild_cost(t);
                            if (cost <= c->funds) /* $103A8 */
                            {
                                c->funds -= cost;
                                r[1] = 0; /* a new plant, no years on it */
                                break;
                            }
                        }
                        /*  $103C4 and $103F8, the same two lines twice.
                         *  The call they guard with MISC[1021] is $392E, which
                         *  SCROLLS THE VIEW to the tile so you watch the plant
                         *  go -- interface, not simulation, and nothing to port.
                         *  (symbols.json carried it as `powerLineRemove` for a
                         *  while, which is why an earlier note here said a
                         *  routine was missing.  None is.)
                         *
                         *  No shipped city has a fifty-year-old plant, so
                         *  this arm is transcription only: read off the listing
                         *  and never checked against the oracle. */
                        sim_demolish_and_place(c, y, x, 0xFF);
                        r[0] = 0; /* $103EE, the record dies with it */
                    }
                    break;
                }

            /*  $1073A -- a school.  Pupils come out of the youngest
             *  two age brackets, teachers out of the education budget
             *  less a penalty for last year's rating, and the new
             *  rating is pupils per teacher on a scale that runs
             *  backwards.  A rating under four raises a flag the pass
             *  carries to its end. */
            case 0xD6:
                {
                    int32_t fund = c->dept[DEPT_SCHOOL].funding;
                    int32_t pupils, staff;
                    int     per, w0, w1;
                    /*  $1073E -- signed divide by four, THINK C's way. */
                    micro_set_w(r, 2, (int)ASR(fund + (int32_t)((uint32_t)ASR(fund, 1) >> 30), 2));
                    per    = school_n < 1 ? 1 : school_n;
                    pupils = (int32_t)(Random() & 0xF); /* $10790, drawn FIRST */
                    pupils += (int32_t)((uint32_t)(HEADS_AT(c, 1) + HEADS_AT(c, 2)) /
                                        (uint32_t)per);
                    if (pupils > 0x5DC)
                        pupils = (int32_t)(Random() & 0xFF) + 0x5DC; /* $107C6 */
                    w0 = micro_cap(c, (int)pupils, 0x14);
                    micro_set_w(r, 0, w0);
                    staff = (int32_t)(Random() & 7); /* $107F6 */
                    staff += (fund * 6) / 10;        /* $10810 */
                    staff -= 12 - (int32_t)r[1];     /* $10834, NOT doubled */
                    if (staff < 0)
                        staff = 0;
                    w1 = micro_cap(c, (int)staff, 0x64);
                    micro_set_w(r, 1, w1);
                    {
                        int d = w1 < 1 ? 1 : w1;
                        int v = w0 / d; /* $1086E */
                        v     = v < 0x0F   ? 12
                                : v > 0x33 ? 0
                                           : (0x33 - v) / 3;
                        r[1]  = (uint8_t)v;
                        if (v < 4)
                            school_failing = 1; /* $108B2 */
                    }
                    break;
                }

            /*  $108BC -- a college.  The same shape as the school one
             *  bracket up, and the penalty for a bad year is four times
             *  as heavy. */
            case 0xD9:
                {
                    int32_t fund = c->dept[DEPT_COLLEGE].funding;
                    int32_t students, staff;
                    int     per, w0, w1;
                    micro_set_w(r, 2, (int)(fund & 0xFFFF)); /* $108CA, low word */
                    per      = college_n < 1 ? 1 : college_n;
                    students = (int32_t)((uint32_t)HEADS_AT(c, 3) / (uint32_t)per);
                    students += (int32_t)(Random() & 0x1F); /* $108F4 */
                    if (students > 0x1388)
                        students = (int32_t)(Random() & 0x1FF) + 0x1388; /* $10910 */
                    w0 = micro_cap(c, (int)students, 0x1E);
                    micro_set_w(r, 0, w0);
                    staff = (int32_t)(Random() & 0xF) + fund * 2; /* $10940 */
                    staff -= (int32_t)((12 - (int)r[1]) << 2);    /* $1096A */
                    if (staff < 0)
                        staff = 0;
                    w1 = micro_cap(c, (int)staff, 0x64);
                    micro_set_w(r, 1, w1);
                    {
                        int d = w1 < 1 ? 1 : w1;
                        int v = (w0 * 4) / d; /* $109A6 */
                        r[1]  = (uint8_t)(v < 0x32   ? 12
                                          : v > 0x6E ? 0
                                                     : (0x6E - v) / 5);
                    }
                    break;
                }

            /*  $109E8 -- a hospital.  Patients are the city's people
             *  divided between the hospitals; staff is what health
             *  funding buys, less a penalty for last year's rating; and
             *  the new rating is patients per staff, on a scale that
             *  runs backwards -- fewer patients each is better. */
            case 0xD1:
                {
                    int32_t fund = c->dept[DEPT_HEALTH].funding;
                    int32_t pat, staff;
                    int     per, w0, w1;
                    /*  $109EC -- (x + (x >>> 31)) >> 1 is how THINK C
                     *  divides a signed long by two. */
                    micro_set_w(r, 2, (int)ASR(fund + (int32_t)((uint32_t)fund >> 31), 1));
                    per = 25 * hosp_n;
                    if (per < 1)
                        per = 1; /* $10A14 */
                    pat = (int32_t)((uint32_t)c->population / (uint32_t)per);
                    pat += (int32_t)(Random() & 0xF); /* $10A2E */
                    if (pat > 1000)
                        pat = (int32_t)(Random() & 0x7F) + 1000; /* $10A4A */
                    w0 = micro_cap(c, (int)pat, 0x1E);
                    micro_set_w(r, 0, w0);
                    staff = (int32_t)(Random() & 7) + fund; /* $10A7A */
                    staff -= (12 - (int32_t)r[1]) * 2;      /* $10AA0 */
                    if (staff < 0)
                        staff = 0;
                    w1 = micro_cap(c, (int)staff, 0x78);
                    micro_set_w(r, 1, w1);
                    {
                        int d = w1 < 1 ? 1 : w1;
                        int v = (w0 * 10) / d; /* $10ADC */
                        r[1]  = (uint8_t)(v < 0x32   ? 12
                                          : v > 0x6E ? 0
                                                     : (0x6E - v) / 5);
                    }
                    break;
                }

            /*  $10B24 -- the zoo.  Four numbers about the animals, and
             *  not one of them is derived from anything: the zoo is
             *  scenery that reports on itself.  Note the generator --
             *  $20EE6 is THINK C's rand, not the Toolbox's. */
            case 0xDA:
                r[1] = (uint8_t)lib_rand(100);
                micro_set_w(r, 0, (int)lib_rand(100));
                micro_set_w(r, 1, (int)lib_rand(100));
                micro_set_w(r, 2, (int)lib_rand(100));
                break;

            /*  $10B8A -- the mayor's house.  It keeps the approval
             *  figure the February poll last wrote, and counts down a
             *  timer whose expiry is somebody else's business. */
            case 0xF3:
                micro_set_w(r, 1, (int)c->approval);
                if (micro_w(r, 2) != 0) /* $10BB0 */
                {
                    micro_set_w(r, 2, micro_w(r, 2) - 1);
                    r[1]++;
                }
                break;

            /*  $11028 -- the library system, funded from the school
             *  budget.  Its second word is the only ACCUMULATING figure
             *  in the whole pass: knowledge is added to, not
             *  recomputed, and a library funded below fifty per cent
             *  subtracts -- `moveq #$ce` is -50, not 206. */
            case 0xF5:
                {
                    int32_t fund = c->dept[DEPT_SCHOOL].funding;
                    int32_t n    = (int32_t)c->census[0xF5];
                    int32_t know;
                    micro_set_w(r, 0, micro_cap(c, (int)(n * (fund << 2)), 0x12));
                    know = n * (fund - 50) + (int32_t)(uint16_t)micro_w(r, 1);
                    /*  $1109A -- kept only while it is under 32000 and
                     *  above zero; outside that the old figure stands. */
                    if (know < 0x7D00 && know > 0)
                        micro_set_w(r, 1, (int)know);
                    {
                        int32_t pop = c->population < 1 ? 1 : c->population;
                        int32_t v   = (n * fund * 300) / pop;
                        if (v > 12)
                            v = 12; /* $110EC */
                        r[1] = (uint8_t)v;
                    }
                    break;
                }

            /*  $104D6 -- a police station.  Its beat is what the
             *  budget pays for, capped by the city's size; its workload
             *  is the city's crime spread over the stations; and the
             *  workload accumulates into a city-wide total that the
             *  tail turns into the coverage radius. */
            case 0xD2:
                {
                    int32_t fund = c->dept[DEPT_POLICE].funding;
                    int     n, load, per;
                    r[1] = (uint8_t)(fund & 0xFF); /* $104E4, the low byte */
                    micro_set_w(r, 0, micro_cap(c, (int)(fund * 2), 0x5A));
                    n    = c->census[0xD2] < 1 ? 1 : (int)c->census[0xD2]; /* $10518 */
                    load = (int)((uint32_t)c->crime_tot / (uint32_t)n);
                    micro_set_w(r, 1, load);
                    /*  $1053E -- five minus the term the pass started with,
                     *  never less than one. */
                    per = 5 - police_term0;
                    if (per < 1)
                        per = 1;
                    {
                        int v = load / per + (int)(Random() & 0xF); /* $1055E */
                        micro_set_w(r, 2, v);
                        /*  $10580 -- and into the city-wide total, which
                         *  saturates rather than wrapping. */
                        if (police_load + v >= 0xFFFF)
                            police_load = 0xFFFF;
                        else
                            police_load += v;
                    }
                    break;
                }

            /*  $105A0 -- a fire station.  Engines from the budget, crews
             *  from the engines, and a response time nobody can predict. */
            case 0xD3:
                {
                    int32_t fund = c->dept[DEPT_FIRE].funding;
                    int     w0;
                    r[1] = (uint8_t)(fund & 0xFF); /* $105AE */
                    w0   = micro_cap(c, (int)ASR(fund, 1), 0x46);
                    micro_set_w(r, 0, w0);
                    micro_set_w(r, 1, (int)((uint16_t)w0 >> 4) + 1); /* $105E8 */
                    micro_set_w(r, 2, (int)((uint16_t)Random() % 20) + 2);
                    break;
                }

            /*  $10F18 -- the park system.  Visitors are what the parks
             *  could take, and what the city has people to send; both
             *  are held under 65000 before the smaller wins. */
            case 0xD5:
                {
                    int32_t vis = (int32_t)(uint16_t)(micro_w(r, 1) * 0x19C);
                    int32_t can = (int32_t)((uint32_t)c->population / 6u);
                    int     w1;
                    if (vis > 0xFDE8)
                        vis = 0xFDE8;
                    if (can > 0xFDE8)
                        can = 0xFDE8;
                    if (can < vis)
                        vis = can; /* $10F58 */
                    micro_set_w(r, 0, (int)vis);
                    w1 = (int)(uint16_t)(c->census[0xD5] + c->census[0x0D]);
                    micro_set_w(r, 1, w1);
                    micro_set_w(r, 2, micro_cap(c, (int)((uint16_t)w1 / 9), 0x78));
                    break;
                }

            /*  $10FBC -- a museum, funded out of the education budget
             *  like the schools and the library. */
            case 0xD4:
                {
                    int32_t fund = c->dept[DEPT_COLLEGE].funding;
                    micro_set_w(r, 0, micro_cap(c, (int)(c->census[0xD4] * (fund << 2)), 0x14));
                    micro_set_w(r, 1, (int)((fund / 10) * c->census[0xD4]));
                    break;
                }

            /*  $10EDE -- hydro dams.  Two ids because a dam has two
             *  orientations, and neither ages: they are the one plant
             *  the game never makes you replace. */
            case 0xC6:
            case 0xC7:
                {
                    int n = (int)(uint16_t)(c->census[0xC6] + c->census[0xC7]);
                    micro_set_w(r, 0, n);
                    micro_set_w(r, 1, (int)(uint16_t)(20 * (unsigned)n)); /* $10F0C */
                    break;
                }

            /*  $10EAE -- wind.  Also ageless. */
            case 0xC8:
                micro_set_w(r, 0, (int)c->census[0xC8]);
                micro_set_w(r, 1, (int)(uint16_t)(c->census[0xC8] << 2));
                break;

            /*  $10E86, $10E1A, $10E5A -- the three transit systems.
             *  Each takes a tile count and last year's ridership, which
             *  the tail then clears ready for the year ahead.  The
             *  ridership counters are longs and only their LOW word is
             *  read here ($1248, $124C, $1250 are the second halves of
             *  $1246, $124A, $124E). */
            case 0xE9: /* subway */
                micro_set_w(r, 0, (int)c->census[0xE9]);
                micro_set_w(r, 2, (int)(int16_t)(c->transit_subway & 0xFFFF));
                break;
            case 0xEC: /* bus */
                micro_set_w(r, 0, (int)(uint16_t)(c->census[0xEC] >> 2));
                micro_set_w(r, 1, (int)c->census[0xEC]);
                micro_set_w(r, 2, (int)(int16_t)(c->transit_bus & 0xFFFF));
                break;
            case 0xED: /* rail */
                micro_set_w(r, 0, (int)(uint16_t)(c->census[0xED] >> 2));
                micro_set_w(r, 2, (int)(int16_t)(c->transit_rail & 0xFFFF));
                break;

            /*  $10BCA -- a statue has nothing to simulate.  It picks a
             *  number so the plaque can say something. */
            case 0xDB:
                micro_set_w(r, 1, (int)((uint16_t)Random() % 42));
                break;

            /*  $10BEC -- water treatment and desalinization.  Both keep
             *  the supply percentage they saw, a satisfaction roll, and
             *  a staff figure the city's size caps. */
            case 0xF4:
            case 0xFA:
                r[1] = (uint8_t)(c->water_pct + (Random() & 7));    /* $10BFA */
                micro_set_w(r, 0, (int)((uint16_t)Random() % 100)); /* $10C16 */
                {
                    int v = (int)(Random() & 0x1F) + 0x87; /* $10C34 */
                    if (v > pop_50)
                        v = pop_50; /* $10C42 */
                    micro_set_w(r, 1, v);
                }
                break;

            /*  $11104 -- the marina's boats: eight a berth plus a few,
             *  and never more than the city has people to sail them. */
            case 0xF8:
                {
                    int v = (int)(uint16_t)(c->census[0xF8] << 3) +
                            (int)game_rand(0x14);             /* $1110C */
                    micro_set_w(r, 0, micro_cap(c, v, 0x96)); /* $11122 */
                    break;
                }

            default:
                break;
        }
    }

    /*  $11144 -- the tail. */
    c->misc[MISC_POLICE_LOAD] = police_load; /* $11144 */
    c->misc[MISC_ARCO_POP]    = arco_pop;    /* $1114A */
    /*  $1114E -- the three ridership counters start the year at zero.
     *  The transit arms above have just copied them into their records,
     *  which is the only place last year's figure survives. */
    c->transit_bus = c->transit_rail = c->transit_subway = 0;

    /*  $1115A -- the police term the prisons accumulated, per prison.
     *  It comes out as 0 or 1 and nothing else: under eighty it is 1,
     *  at eighty or over it is 0, and with no prisons at all it is 0.
     *  $23E0C reads it as `(term + 5) * funding`, so all this decides
     *  is whether the beat reaches five tiles or six. */
    if (prison_n > 0)
        c->police_term =
            (int16_t)((c->police_term / prison_n) < 0x50 ? 1 : 0);
    else
        c->police_term = 0; /* $11182 */

    /*  $11186 and $1119A -- a failing school and a prison over capacity
     *  each get a newspaper story.  Interface, both of them. */
    (void)school_failing;
    (void)riot_brewing;

    /*  $111AE -- and the ending.  More than three hundred Launch Arcos
     *  with six million people in the city's arcologies, and they all
     *  leave: every $FE tile is demolished and the treasury is paid a
     *  hundred thousand a piece. */
    if ((c->census[0xFE] >> 4) > 0x12C &&
        c->misc[MISC_ARCO_POP] > 0x5B8D80)
    {
        int y, x;
        for (y = 0; y < MAP_H; y++)
            for (x = 0; x < MAP_W; x++)
                if (c->xbld[y][x] == 0xFE)
                    sim_demolish_and_place(c, y, x, 0xFF); /* $11200 */
        c->funds += (int32_t)launch_arco * 100000;         /* $11222 */
    }
}

/* ====================================================================
 *  simTick $21EDE -- one phase of the 25-phase clock.
 *
 *  The date advances first ($21EE6) and the phase is the date modulo
 *  25 ($21EF0); there is no phase counter of its own to save.  The
 *  jump table at $21F0A has 25 arms.  Whatever an arm does that is not
 *  simulation -- the window title, dialogs, menus, the cursor, the graph
 *  windows, the newspaper -- is named here and left out.
 * ==================================================================== */
int sim_tick(City *c)
{
    int ev = SIM_EV_NONE;
    int phase;

    c->date++;
    phase = (int)(c->date % 25);
    if (phase < 0)
        return ev; /* $21EF6: bhi.w past the table */
    switch (phase)
    {
        case 0:
            /*  updateWindowTitle $1522A runs first, and it is what
             *  refreshes the month ($1523E) and the year count ($15268)
             *  that the budget and the economy read. */
            c->month = (int16_t)((c->date / 25) % 12);
            c->years = c->date / 300;
            /*  $21F42: g_yearEndDue opens the year-end budget dialog
             *  ($2535E).  $21F62: $2EAEA is a dialog in months 3 and 7.
             *  Neither touches the model; the poll below does. */
            sim_budget(c); /* $21F4E */

            /*  $21F54 -- the poll runs in month 2 and only there */
            if (c->month == 2)
            {
                int e = sim_opinion_poll(c);
                if (e != SIM_EV_NONE)
                    ev = e;
            }
            {
                int i;
                for (i = 1; i < 8; i++)
                    c->accum8[i] = 0; /* $21F88 */
            }
            break;
        case 1:
            sim_power_grid(c); /* $21FA6 */
            break;
        case 2:
            /*  cityScanPass $2317E, stages 1 to 9 in order; coverage
             *  runs inside the density stage. */
            sim_pollution(c);
            sim_land_value(c);
            sim_density(c);
            sim_crime(c);
            break;
        case 19:
            sim_traffic_total(c); /* $220D0 */
            break;
        case 20:
            sim_water_grid(c); /* $220DA */
            break;
        case 21:
            sim_population(c); /* $220E4 */
            sim_economy(c);
            sim_graph_pass(c);
            break;
        case 22:
            {
                /*  $220FA: the city is promoted a stage when its population
                 *  passes the next rung of the ladder at A5-0x3ED8, indexed
                 *  by stage + 1; a zero rung ends the ladder.  The newspaper
                 *  and the reward prompt that follow ($2EDE4, $4094, $22708)
                 *  are interface. */
                int32_t stage = c->misc[MISC_STAGE];
                int32_t need  = (stage >= 0 && stage + 1 < 10)
                                    ? CITY_STAGE_POP[stage + 1]
                                    : 0;
                int     r;
                if (need && (uint32_t)c->population > (uint32_t)need)
                {
                    c->misc[MISC_STAGE] = stage + 1; /* $22128 */
                    ev                  = SIM_EV_STAGE;
                }
                /*  $2219C: the scenario goals, once a month. */
                r = sim_scenario_check(c);
                if (r > 0)
                    ev = SIM_EV_SCEN_WON;
                else if (r < 0)
                    ev = SIM_EV_SCEN_LOST;
                /*  $222AC: below -100,000 the city is bankrupt, $230E6(2). */
                if (c->funds < -100000)
                    ev = SIM_EV_BANKRUPT;
                break;
            }
        case 23:
            /*  $222C4: the redraw and the graph windows. */
            break;
        case 24:
            /*  $222F8: $30E30 picks the newspaper's story, and then
             *  decides whether anything is about to go wrong. */
            sim_disaster_roll(c);
            break;
        default:
            /*  $21FBA..$220C4: sixteen growthScan slices, the argument
             *  packed as y0 << 16 | x0 in the order (0,0) (0,1) ... */
            sim_growth_scan(c, (phase - 3) >> 2, (phase - 3) & 3);
            break;
    }
    return ev;
}
