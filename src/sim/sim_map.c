/*  sim_map.c: changing what is on a tile.  The bulldozer and everything
 *  it has to understand.  A run along a line, and the 2x2 and larger
 *  footprints.  The bridges that sink when their ends go, and the slope
 *  code the terrain is fixed up to afterwards.  The footprint stamp that
 *  puts a new building down.  Every path that writes XBLD other than
 *  growth and placement comes through here.
 *  Addresses still point into CODE 2. */
#include "ext80.h"
#include "sim.h"
#include "sim_int.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ================================================================== *
 *  $763A  footprintOrigin: given any tile of a building, say how big
 *  the building is and move the caller's coordinates to its origin.
 *
 *  Three groups of buildings:
 *      $49..$50 and $61..$6B are two by two, and the origin is found
 *          by arithmetic alone: the row loses its low bit, the column
 *          loses its low bit and gains one.
 *      anything below $70 is a single tile.
 *      $70 and up take their size from the table at A5-0x1252.
 *
 *  For a three or four tile building the origin comes from the corner
 *  markers.  They are in the high nibble of XZON.  There are four of
 *  them, $10 $20 $40 $80.  The original reads the marker for corner k as
 *  $10 << ((turn + k) & 3).  The bits are in the grid's own terms, turn
 *  0, so the marker for corner k is $10 << k.  The search tries each
 *  corner in order and steps toward it.
 *
 *  The original reads XZON one tile outside the map without checking.
 *  It cannot happen in a saved city, because nothing larger than one
 *  tile is ever placed within two tiles of the edge, so this returns
 *  zero there rather than read past the array.
 * ================================================================== */
static int zon_corner(int k) { return 0x10 << (k & 3); }

/*  The eight neighbors in the order the original walks them: A5-0x4F4E
 *  and A5-0x4F3C.  This are the same two tables the disasters step by.
 *  Shared through sim_int.h rather than written out twice. */
const int BEAM_DY[8] = {0, 1, 1, 1, 0, -1, -1, -1};
const int BEAM_DX[8] = {-1, -1, 0, 1, 1, 1, 0, -1};
/*  A5-0x10D2.  Higher wins.  A seaport or airport ($C7, $C8 -> 8, 9)
 *  outranks everything, and an ordinary zone ranks 1. */
static const uint8_t PROBLEM_RANK[16] = {0, 1, 1, 1, 1, 1, 1, 1, 4, 8, 5, 5, 5, 5, 5, 5};
/*  A5-0x4DF6.  Neighbor i lifts these corners of the tile.  The eight
 *  entries walk the compass from west, so the diagonals name one corner
 *  and the sides name two. */
static const uint8_t SLOPE_CORNER[8] = {3, 2, 6, 4, 12, 8, 9, 1};
/*  A5-0x4DEE, read only through a four-bit index.  It is the slope code
 *  for each set of raised corners.  $32 at the end is the "raise this
 *  tile" answer, not a code. */
static const uint8_t SLOPE_CODE[16] = {0, 9, 10, 2, 11, 13, 3, 6, 12, 1, 13, 5, 4, 8, 7, 0x32};
/* ================================================================== *
 *  $12C04  fixNeighbourhood: put a tile and its eight neighbors
 *  back in order.  The ninth entry of the tables at A5-0x4F4E and
 *  A5-0x4F3C is (0,0), which is how the tile itself is included.
 *
 *  Each tile is redrawn, fixed, and redrawn again ($15A54 either side
 *  of $128DE).  Only the middle call changes any state.
 * ================================================================== */
static const int N9_DY[9] = {0, 1, 1, 1, 0, -1, -1, -1, 0};
static const int N9_DX[9] = {-1, -1, 0, 1, 1, 1, 0, -1, 0};

/* ================================================================== *
 *  $324B8  nearPowered: is this tile, or any of its four orthogonal
 *  neighbors, supplied with power?  XBIT bit $40.
 *
 *  (Not a water test: $40 is XBIT_POWERED, set by the power flood at
 *  $20FC4.  Water covered is bit $04, which is what $3590 rejects.)
 *  The edge guards are asymmetric in the original: it looks one row up
 *  only from row 2, and one row down only to row 126.
 * ================================================================== */
int near_powered(const City *c, int y, int x)
{
    if (T_XBIT(c, y, x) & XBIT_POWERED)
        return 1; /* $324D8 */
    if (y > 1 && (T_XBIT(c, y - 1, x) & XBIT_POWERED))
        return 1; /* $324FC */
    if (x > 1 && (T_XBIT(c, y, x - 1) & XBIT_POWERED))
        return 1; /* $32524 */
    if (y < c->h - 1 && (T_XBIT(c, y + 1, x) & XBIT_POWERED))
        return 1; /* $3254C */
    if (x < c->w - 1 && (T_XBIT(c, y, x + 1) & XBIT_POWERED))
        return 1; /* $32576 */
    return 0;     /* $32582 */
}

/* ================================================================== *
 *  $43A2  setUnder: write the underground layer and keep the transit
 *  counter straight.  A tile counts toward transit if its value is
 *  1..15, or one of $1F, $20, $22, $23.  The old value is subtracted and
 *  the new one added.  Tiles inside a military zone are exempt from the
 *  bookkeeping but still get written.  The value is a word.  XUND keeps
 *  its low byte, and the test reads the whole word, so a word of $100
 *  or more never counts.  The network fitters give one when they read a
 *  table past its end.
 * ================================================================== */
void set_under(City *c, int y, int x, int und)
{
    static const int counts[] = {0x1F, 0x20, 0x22, 0x23};
    int              i;

    if ((T_XZON(c, y, x) & 0x0F) != 7) /* $43CC */
    {
        uint8_t old = T_XUND(c, y, x);
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
    T_XUND(c, y, x) = (uint8_t)und; /* $443A */
}

/* ================================================================== *
 *  $EE3C  releaseLabel: give back whatever record the XTXT byte of a
 *  tile was pointing at.  The byte says which kind:
 *      $C9 .. $F0   a moving object.  Clear the XTXT it had saved
 *      $34 .. $FF   below $C9, a microsim record and its label
 *      $01 .. $32   a sign, so only the label
 *      $33 .. $3C   nothing at all
 * ================================================================== */
void release_label(City *c, int v)
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

/*  $68D0 and $6BD4: four building ids are not square.  A runway or a
 *  pier is a run of identical tiles.  So those are taken down by
 *  flooding over the neighbors that carry the same id rather than by
 *  walking a footprint.  Nothing marks a tile as visited: the rubble
 *  written into XBLD stops matching.  This is what stops the walk.
 *  The queue is popped from the back, so the flood is depth first. */
static void demolish_run(City *c, int y, int x, int a, int b, int rubble, int even_bare)
{
    q_reset(c);         /* $21DD4 clears both ends */
    q_push(c, y, x);    /* and pushes the first point */
    while (!q_empty(c)) /* $21EBE */
    {
        int r, cx;
        q_pop_back(c, &r, &cx); /* $68D4 */

        sim_set_tile(c, r, cx, rubble ? (uint8_t)(1 + (Random(c) & 3)) : 0); /* $6BF4 */
        T_XZON(c, r, cx) = (uint8_t)(T_XZON(c, r, cx) & 0x0F);               /* $6908 */
        T_XBIT(c, r, cx) = (uint8_t)(T_XBIT(c, r, cx) & 0x3D);               /* $6922 */

        /*  $6926 and $6C36: the collapse animation, two draws a tile. */
        if (even_bare)
        {
            (void)Random(c);
            (void)Random(c);
        }

        if (r > 0 && (T_XBLD(c, r - 1, cx) == a || T_XBLD(c, r - 1, cx) == b))
            q_push(c, r - 1, cx); /* $6D1E */
        if (r < c->h - 1 && (T_XBLD(c, r + 1, cx) == a || T_XBLD(c, r + 1, cx) == b))
            q_push(c, r + 1, cx); /* $6D68 */
        if (cx > 0 && (T_XBLD(c, r, cx - 1) == a || T_XBLD(c, r, cx - 1) == b))
            q_push(c, r, cx - 1); /* $6DB2 */
        if (cx < c->w - 1 && (T_XBLD(c, r, cx + 1) == a || T_XBLD(c, r, cx + 1) == b))
            q_push(c, r, cx + 1); /* $6DFC */
    }
}

/* ================================================================== *
 *  $1D322  classifyPair: what kind of two-by-two structure stands on
 *  a tile.  It answers $FF for anything that is not a two-by-two id at
 *  all, and otherwise a small code.  $5FAA only cares whether the code
 *  reaches $0D, which marks the raised pieces: those are demolished as
 *  a run along their line rather than as one footprint.
 * ================================================================== */
static int classify_2x2(const City *c, int y, int x)
{
    int b;

    if (y < 0 || y >= c->h || x < 0 || x >= c->w)
        return 0; /* $1D34A */

    b = T_XBLD(c, y, x);
    if (!((b >= 0x61 && b < 0x6C) || (b >= 0x49 && b < 0x51)))
        return -1; /* $1D37C: moveq #$ff is -1, and the caller's
                    *  compare is signed, so this is NOT $FF */

    if ((T_XZON(c, y, x) & 0xF0) != 0xF0) /* $1D398 */
    {
        const int code = b - 0x5D; /* $1D502 */
        if (code < 0x0D)
            return code;                                   /* $1D530, and it can be negative */
        return (T_XBIT(c, y, x + 1) & 0x02) ? 0x10 : 0x0F; /* $1D520 */
    }

    /*  $1D3A0: a single-tile marker, so look at the four tiles of the
     *  pair in turn.  The first that is a bridge piece, or that stands
     *  on water, decides. */
    {
        static const int DY[4] = {0, 1, 1, 0};
        static const int DX[4] = {0, 0, 1, 1};
        int              i;
        for (i = 0; i < 4; i++)
        {
            const int ty = y + DY[i], tx = x + DX[i];
            if (ty < 0 || ty >= c->h || tx < 0 || tx >= c->w)
                continue;
            b = T_XBLD(c, ty, tx);
            if (b >= 0x4B && b < 0x51)
                return b & 1; /* $1D3AC */
            if (T_XBIT(c, ty, tx) & XBIT_WATER)
                return (b == 0x49) ? 0x0D : 0x0E; /* $1D3D4 */
        }
        if (b >= 0x49 && b < 0x4B)
            return (b & 1) + 2; /* $1D4F4 */
        return -1;              /* $1D4FE, moveq #$ff again */
    }
}

/*  $621C and $6794: put the terrain back under all four tiles. */
static void fix_pair(City *c, int y, int x)
{
    sim_fix_terrain(c, y, x);         /* $6250 */
    sim_fix_terrain(c, y + 1, x);     /* $6270 */
    sim_fix_terrain(c, y + 1, x + 1); /* $6298 */
    sim_fix_terrain(c, y, x + 1);     /* $62C0 */
}

/*  $6418 with $653E: clear all four tiles of the pair. */
static void clear_pair(City *c, int y, int x)
{
    static const int DY[4] = {0, 0, 1, 1};
    static const int DX[4] = {0, 1, 1, 0};
    int              i;
    for (i = 0; i < 4; i++)
    {
        const int ty = y + DY[i], tx = x + DX[i];
        if (ty < 0 || ty >= c->h || tx < 0 || tx >= c->w)
            continue;
        sim_set_tile(c, ty, tx, 0);                               /* $6422 */
        T_XZON(c, ty, tx) = (uint8_t)(T_XZON(c, ty, tx) & 0x0F);  /* $6442 */
        T_XBIT(c, ty, tx) = (uint8_t)(T_XBIT(c, ty, tx) & ~0x02); /* $645C */
    }
}

/* ================================================================== *
 *  $6E4A  a tunnel mouth, $3F to $42.  The id says which way the tunnel
 *  runs from it: $3F toward y-1, $40 toward x-1, $41 toward y+1 and $42
 *  toward x+1.  Both mouths go, and every tile from one to the other
 *  loses its tunnel depth, the ALTM bits from 10 up.  The terrain is not
 *  fixed afterward.  The collapse is drawn at each mouth whether or not
 *  the caller asks for it, so each mouth takes two numbers from the
 *  generator.  The original does not stop at the edge of the map.  The C
 *  does, so the two can differ only for a tunnel with no far mouth.
 * ================================================================== */
static void demolish_tunnel(City *c, int y, int x, int bld)
{
    static const int DY[4] = {-1, 0, 1, 0}, DX[4] = {0, -1, 0, 1};
    const int        dy = DY[bld - 0x3F], dx = DX[bld - 0x3F]; /* $6E76 */

    sim_set_tile(c, y, x, 0);                              /* $6EB8 */
    T_XZON(c, y, x) = (uint8_t)(T_XZON(c, y, x) & 0x0F);   /* $6ED8 */
    T_ALTM(c, y, x) = (uint16_t)(T_ALTM(c, y, x) & 0x3FF); /* $6F02 */
    (void)Random(c);                                       /* $6F12, the debris shape */
    (void)Random(c);                                       /* $6F68, the mirror */
    do
    {
        y += dy; /* $6F98 */
        x += dx;
        if (y < 0 || y >= c->h || x < 0 || x >= c->w)
            return;
        T_ALTM(c, y, x) = (uint16_t)(T_ALTM(c, y, x) & 0x3FF); /* $6FC0 */
        bld             = T_XBLD(c, y, x);
    } while (bld < 0x3F || bld >= 0x43); /* $6FDA */
    sim_set_tile(c, y, x, 0);                            /* $6FFA */
    T_XZON(c, y, x) = (uint8_t)(T_XZON(c, y, x) & 0x0F); /* $701A */
    (void)Random(c);                                     /* $701C, the debris shape */
    (void)Random(c);                                     /* $707A, the mirror */
}

/* ================================================================== *
 *  $6046: the raised two-by-two pieces, the elevated rail and road
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
        if (y + dy * 2 >= c->h || x + dx * 2 >= c->w)
            break;
        y += dy * 2; /* $664A */
        x += dx * 2;
        if (classify_2x2(c, y, x) < 0x0D)
            break; /* $665E */
        /*  five draws a step here, not the bridge's two: the pair has
         *  three more debris blocks at $6478, $64BA and $64FC. */
        if (even_bare)
        {
            (void)Random(c); /* $637E */
            (void)Random(c); /* $63EA */
            (void)Random(c); /* $6478 */
            (void)Random(c); /* $64BA */
            (void)Random(c); /* $64FC */
        }
        clear_pair(c, y, x);
    }

    if (bld != 0x49 && bld != 0x4A)
        fix_pair(c, y, x); /* $679E */
}

/*  $606E: a bridge is a run of tiles over water, not a footprint.
 *  Taking one down means putting the water back.  Every tile of the run
 *  is cleared, and the land tile at each end is dropped a level and
 *  flooded.  The run's direction comes from bit 1 of XBIT, which is the
 *  orientation flag a two-form tile needs.  Neither walk is bounded in
 *  the original.  A bridge always has land at both ends, so it stops.
 *  The guards here only keep the C inside its arrays. */
static int is_bridge(int b)
{
    return (b >= 0x51 && b < 0x5D) || b == 0x6A || b == 0x6B; /* $66A6 */
}

static void sink_bridge_end(City *c, int y, int x)
{
    if (T_XBIT(c, y, x) & XBIT_WATER)
        return;               /* $6704, already water */
    sim_set_tile(c, y, x, 0); /* $6724 */
    T_ALTM(c, y, x) =
        (uint16_t)((T_ALTM(c, y, x) & ~(uint16_t)0x1F) |
                   (unsigned)((T_ALTM(c, y, x) & 0x1F) - 1)); /* $6764 */
    T_XBIT(c, y, x) |= XBIT_WATER;                            /* $677E */
    sim_fix_terrain(c, y, x);                                 /* $678A */
}

static void demolish_bridge(City *c, int y, int x, int even_bare)
{
    const int dy = (T_XBIT(c, y, x) & 0x02) ? 1 : 0; /* $607A */
    const int dx = (T_XBIT(c, y, x) & 0x02) ? 0 : 1;

    while (is_bridge(T_XBLD(c, y, x))) /* $60D0, back off the front */
    {
        if (y - dy < 0 || x - dx < 0)
            break;
        y -= dy; /* $60BC */
        x -= dx;
    }

    sink_bridge_end(c, y, x);          /* $6158 */
    T_XBIT(c, y, x) &= (uint8_t)~0x02; /* $6214, only on this end */

    for (;;) /* $62DC, forward over the run */
    {
        if (y + dy >= c->h || x + dx >= c->w)
            break;
        y += dy;
        x += dx;
        if (!is_bridge(T_XBLD(c, y, x)))
        {
            sink_bridge_end(c, y, x); /* $66E2, the far end */
            break;
        }
        /*  $6374: with the collapse shown, each tile of the run draws a
         *  debris shape and a mirror flag before it is cleared. */
        if (even_bare)
        {
            (void)Random(c); /* $637E */
            (void)Random(c); /* $63EA */
        }
        sim_set_tile(c, y, x, 0);                             /* $6422 */
        T_XZON(c, y, x) = (uint8_t)(T_XZON(c, y, x) & 0x0F);  /* $6442 */
        T_XBIT(c, y, x) = (uint8_t)(T_XBIT(c, y, x) & ~0x02); /* $645C */
    }
}

int sim_demolish_and_place(City *c, int y, int x, int even_bare)
{
    const int oy = y, ox = x; /* $5FB2, before $763A moves them */
    int       bld = T_XBLD(c, y, x);
    int       n, dy, dx;

    /*  Beyond the explosion, $75C6 uses `even_bare` only to ask for a
     *  redraw. */
    const int shown = even_bare != 0;

    if (bld < 6)
        return 0; /* $5FD2 */

    n = sim_footprint_origin(c, &y, &x, bld); /* $5FEE */

    /*  $6026: a single-tile bridge id is a run over water. */
    if (n == 1 && ((bld >= 0x51 && bld < 0x5D) || bld == 0x6A || bld == 0x6B))
    {
        demolish_bridge(c, oy, ox, even_bare);
        return shown; /* $62F8 */
    }

    /*  $6046: a raised pair is a run along its line, not a footprint. */
    if (n == 2 && classify_2x2(c, y, x - 1) >= 0x0D)
    {
        demolish_pair_run(c, y, x, bld, even_bare);
        return shown; /* $6872 */
    }

    /*  $6898 and $6B9C, before the footprint walk.  These two pairs are
     *  runs rather than squares.  $DF and $E0 leave bare ground, $DD and
     *  $DE leave rubble. */
    if (bld == 0xDF || bld == 0xE0)
    {
        demolish_run(c, oy, ox, 0xDF, 0xE0, 0, even_bare);
        return shown; /* $6B76 */
    }
    if (bld == 0xDD || bld == 0xDE)
    {
        demolish_run(c, oy, ox, 0xDD, 0xDE, 1, even_bare);
        return shown; /* $6E24 */
    }
    if (bld >= 0x3F && bld < 0x43)
    {
        demolish_tunnel(c, oy, ox, bld); /* $6E4A */
        return shown;                    /* $70BC */
    }

    /*  $71A4: when the caller asked for the collapse to be shown, the
     *  debris is animated for n frames over the n by n footprint, and
     *  each tile of each frame draws a shape and a mirror flag.  The
     *  animation changes nothing, but it takes 2 * n^3 numbers from the
     *  generator every other decision downstream depends on. */
    if (even_bare)
        for (dy = 0; dy < n * n * n; dy++)
        {
            (void)Random(c); /* $71CA, the debris shape */
            (void)Random(c); /* $71FA, the mirror */
        }

    for (dy = 0; dy < n; dy++)     /* $7518 */
        for (dx = 0; dx < n; dx++) /* $750E */
        {
            const int r  = y + dy;
            const int cx = x - dx; /* $73AE, the origin is the right edge */
            int       v;

            if (r < 0 || r >= c->h || cx < 0 || cx >= c->w)
                continue;

            if (T_XTER(c, r, cx) > 0)
                sim_set_tile(c, r, cx, 0); /* $73DE, no rubble on a slope */
            else
                sim_set_tile(c, r, cx, (uint8_t)(1 + (Random(c) & 3))); /* $7406 */

            T_XBIT(c, r, cx) = (uint8_t)(T_XBIT(c, r, cx) & 0x3D); /* $742A */
            T_XZON(c, r, cx) = (uint8_t)(T_XZON(c, r, cx) & 0x0F); /* $7452 */

            v = T_XTXT(c, r, cx);
            if (v == 0)
                continue; /* $7472 */
            if (v >= 0xF1 && v != 0xFA)
                continue; /* $747A */
            if (v < 0xC9 || v == 0xFA)
                T_XTXT(c, r, cx) = 0; /* $74AE */
            release_label(c, v);      /* $74B4 */

            if (v == 0xFA) /* $74BA, it had already burned out */
            {
                if ((bld >= 0x1D && bld < 0x2C) || (bld >= 0x3F && bld < 0x47) ||
                    bld == 0x4B || bld == 0x4C || (bld >= 0x5D && bld < 0x61))
                    c->burnt_road--; /* $7502 */
                else
                    c->burnt_other--; /* $7508 */
            }
        }

    /*  $7520: the two-by-two ranges have their terrain put back on all
     *  four tiles.  Everything else gets one tile, and only when it is a
     *  single tile below $70 standing on terrain that is not flat. */
    if ((bld >= 0x61 && bld < 0x6C) || (bld >= 0x49 && bld < 0x51))
    {
        sim_fix_terrain(c, y, x);         /* $7548 */
        sim_fix_terrain(c, y + 1, x);     /* $755A */
        sim_fix_terrain(c, y + 1, x - 1); /* $7570 */
        sim_fix_terrain(c, y, x - 1);     /* $7582 */
    }
    else if (n == 1 && bld < 0x70 && T_XTER(c, y, x)) /* $758C */
        sim_fix_terrain(c, y, x);                     /* $75BE */
    return shown;                                     /* $726C */
}

void sim_demolish_tile(City *c, int y, int x, int flag_c, int scorch)
{
    int bld, zon, n, oy, ox, i, j, code;

    if (y < 0 || y >= c->h || x < 0 || x >= c->w)
        return; /* $3A010 */

    bld = T_XBLD(c, y, x);
    zon = T_XZON(c, y, x) & 0x0F;
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
            if (yy < 0 || yy >= c->h || xx < 0 || xx >= c->w)
                continue;
            if (T_XBIT(c, yy, xx) & XBIT_WATER)
                continue; /* $3A0F4 */
            if (!scorch)
                continue;             /* $3A0FA */
            T_XTXT(c, yy, xx) = 0xFF; /* $3A10A */
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

void sim_fix_terrain(City *c, int y, int x)
{
    int alt, mask = 0, code, i;

    if (y < 0 || y >= c->h || x < 0 || x >= c->w)
        return; /* $128EE */

    if (T_XBLD(c, y, x) >= 0x0D)
        sim_demolish_and_place(c, y, x, 0xFF); /* $1292C */
    if (c->city_mode != 0 && T_XBLD(c, y, x) != 5)
        sim_set_tile(c, y, x, 0); /* $12946 */
    if (T_XUND(c, y, x))
        set_under(c, y, x, 0); /* $12968 */

    alt = T_ALTM(c, y, x) & 0x1F; /* $12984 */
    for (i = 0; i < 8; i++)       /* $129F6 */
    {
        const int ny = y + BEAM_DY[i]; /* A5-0x4F4E */
        const int nx = x + BEAM_DX[i]; /* A5-0x4F3C */
        if (ny < 0 || ny >= c->h || nx < 0 || nx >= c->w)
            continue;
        if ((T_ALTM(c, ny, nx) & 0x1F) > alt)
            mask |= SLOPE_CORNER[i]; /* $129F0 */
    }
    code = SLOPE_CODE[mask & 0x0F]; /* $12A06 */

    if (code != 0)
        T_XZON(c, y, x) = (uint8_t)(T_XZON(c, y, x) & 0xF0); /* $12A24 */

    if (code == 0x32) /* $12A26: not a slope: the tile itself rises */
    {
        alt = (T_ALTM(c, y, x) & 0x1F) + 1; /* $12A56 */
        T_ALTM(c, y, x) =
            (uint16_t)((T_ALTM(c, y, x) & ~(uint16_t)0x1F) | (unsigned)alt);
        /*  Every way out of here writes the tile itself, so the code
         *  the slope would have carried is not read again. */

        if (alt >= c->water_level) /* $12A6E */
        {
            T_XBIT(c, y, x) &= (uint8_t)~XBIT_WATER; /* $12A82 */
            T_XTER(c, y, x) = 0;                     /* $12A96 */
            return;
        }
        T_XBIT(c, y, x) |= XBIT_WATER; /* $12AAC */
        T_ALTM(c, y, x) = (uint16_t)((T_ALTM(c, y, x) & 0xFC1F) |
                                     ((unsigned)c->water_level << 5)); /* $12AD6 */
        if (T_XBLD(c, y, x) != 5)
            sim_set_tile(c, y, x, 0); /* $12AF4 */
        T_XTER(c, y, x) = 0x10;       /* $12B0A */
        return;
    }

    alt = T_ALTM(c, y, x) & 0x1F; /* $12B24 */
    if (alt >= c->water_level)    /* $12B30 */
    {
        T_XBIT(c, y, x) &= (uint8_t)~XBIT_WATER; /* $12B44 */
        T_XTER(c, y, x) = (uint8_t)code;         /* $12B58 */
        return;
    }
    T_XBIT(c, y, x) |= XBIT_WATER; /* $12B6E */
    T_ALTM(c, y, x) = (uint16_t)((T_ALTM(c, y, x) & 0xFC1F) |
                                 ((unsigned)c->water_level << 5)); /* $12B9C */
    if (T_XBLD(c, y, x) != 5)
        sim_set_tile(c, y, x, 0); /* $12BBA */
    if (c->water_level - 1 == alt)
        T_XTER(c, y, x) = (uint8_t)(0x20 + code); /* $12BE0, the shoreline */
    else
        T_XTER(c, y, x) = (uint8_t)(0x10 + code); /* $12BF8 */
}

void sim_fix_neighbourhood(City *c, int y, int x)
{
    int i;
    for (i = 0; i < 9; i++) /* $12C52 */
        sim_fix_terrain(c, y + N9_DY[i], x + N9_DX[i]);
}

/* ================================================================== *
 *  $122E0  fitWater: fit the water shape in XTER on (y,x) to its
 *  neighbors, and make the tile water if it is not.
 *
 *  XTER $2E and $3E are left alone.  A slope, $01..$0F, takes shape 14.
 *  Any other tile takes its shape from WATER_EDGE_SHAPE, by which of its
 *  four sides are water.  With water on all four sides, the shape comes
 *  from WATER_CORNER_SHAPE, by which of its four diagonals are dry.
 *  What is written depends on the XTER the tile has:
 *      $00..$0F    $30 plus the shape.
 *      $10..$1F    nothing.
 *      $20..$2F    XTER less $10, unless the low four bits of the shape
 *                  and of XTER agree.
 *      $30..$FF    $30 plus the shape.  This goes as high as $45.
 *  When the shape agrees with XTER at $20 and up, the routine stops.
 *  The original compares a shape with a slope code at $20..$2F, and it
 *  writes the slope code under water, not the shape.  This is kept.
 *
 *  A tile that is not water then becomes water.  The building goes, the
 *  water surface in ALTM is set to the height of the ground, and XBIT
 *  $04 is set.  Then each of the eight neighbors that is water is fitted
 *  in turn.  A tile that stops early is not made water, even when it is
 *  dry.  The original does not make sure (y,x) is on the map.  Every
 *  caller reads the tile first.
 * ================================================================== */
void sim_fit_water(City *c, int y, int x)
{
    const int ter   = T_XTER(c, y, x); /* $12320 */
    int       sides = 0, dry = 0, shape;

    if (ter == 0x2E || ter == 0x3E)
        return; /* $12330 */
    if (ter > 0 && ter < 0x10)
        shape = 14; /* $1233E */
    else
    {
        if (x > 0 && (T_XBIT(c, y, x - 1) & XBIT_WATER))
            sides |= 1; /* $12364 */
        if (y < c->h - 1 && (T_XBIT(c, y + 1, x) & XBIT_WATER))
            sides |= 2; /* $12388 */
        if (x < c->w - 1 && (T_XBIT(c, y, x + 1) & XBIT_WATER))
            sides |= 4; /* $123AE */
        if (y > 0 && (T_XBIT(c, y - 1, x) & XBIT_WATER))
            sides |= 8; /* $123CE */
        if (sides < 15)
            shape = WATER_EDGE_SHAPE[sides]; /* $123DE */
        else
        {
            if (x > 0 && y > 0 && !(T_XBIT(c, y - 1, x - 1) & XBIT_WATER))
                dry |= 1; /* $1240C */
            if (x > 0 && y < c->h - 1 && !(T_XBIT(c, y + 1, x - 1) & XBIT_WATER))
                dry |= 2; /* $12438 */
            if (x < c->w - 1 && y < c->h - 1 && !(T_XBIT(c, y + 1, x + 1) & XBIT_WATER))
                dry |= 4; /* $12468 */
            if (x < c->w - 1 && y > 0 && !(T_XBIT(c, y - 1, x + 1) & XBIT_WATER))
                dry |= 8;                    /* $12494 */
            shape = WATER_CORNER_SHAPE[dry]; /* $1249E */
        }
    }

    if (ter < 0x10)
        T_XTER(c, y, x) = (uint8_t)(0x30 + shape); /* $124BA */
    else if (ter >= 0x30)
    {
        if (ter == 0x30 + shape)
            return;                                /* $124CC */
        T_XTER(c, y, x) = (uint8_t)(0x30 + shape); /* $124E2 */
    }
    else if (ter >= 0x20)
    {
        if ((shape & 0x0F) == (ter & 0x0F))
            return;                              /* $124FC */
        T_XTER(c, y, x) = (uint8_t)(ter - 0x10); /* $12512 */
    }

    if (T_XBIT(c, y, x) & XBIT_WATER)
        return;               /* $1252A */
    sim_set_tile(c, y, x, 0); /* $12534 */
    T_ALTM(c, y, x) = (uint16_t)((T_ALTM(c, y, x) & 0xFC1F) |
                                 ((T_ALTM(c, y, x) & 0x1F) << 5)); /* $1256E */
    T_XBIT(c, y, x) |= XBIT_WATER;                                 /* $12572 */

    fit_water_around(c, y, x); /* $125AA */
}

/*  Each of the eight neighbors of (y,x) that is water fits its edge
 *  again: the four sides first, then the four corners.  The original
 *  carries this same run of calls twice, at $125AA in $122E0 and at
 *  $8330 in the bulldozer. */
void fit_water_around(City *c, int y, int x)
{
    if (y > 0 && (T_XBIT(c, y - 1, x) & XBIT_WATER))
        sim_fit_water(c, y - 1, x);
    if (x < c->w - 1 && (T_XBIT(c, y, x + 1) & XBIT_WATER))
        sim_fit_water(c, y, x + 1);
    if (y < c->h - 1 && (T_XBIT(c, y + 1, x) & XBIT_WATER))
        sim_fit_water(c, y + 1, x);
    if (x > 0 && (T_XBIT(c, y, x - 1) & XBIT_WATER))
        sim_fit_water(c, y, x - 1);
    if (x > 0 && y > 0 && (T_XBIT(c, y - 1, x - 1) & XBIT_WATER))
        sim_fit_water(c, y - 1, x - 1);
    if (x > 0 && y < c->h - 1 && (T_XBIT(c, y + 1, x - 1) & XBIT_WATER))
        sim_fit_water(c, y + 1, x - 1);
    if (x < c->w - 1 && y < c->h - 1 && (T_XBIT(c, y + 1, x + 1) & XBIT_WATER))
        sim_fit_water(c, y + 1, x + 1);
    if (x < c->w - 1 && y > 0 && (T_XBIT(c, y - 1, x + 1) & XBIT_WATER))
        sim_fit_water(c, y - 1, x + 1);
}

static int zon_at(const City *c, int y, int x)
{
    long i = (long)y * c->w + x;
    if (i < 0 || i >= c->h * c->w)
        return 0;
    return T_XZON(c, (int)(i / c->w), (int)(i % c->w)) & 0xF0; /* the row wraps, as the flat array does */
}

int sim_footprint_origin(const City *c, int *py, int *px, int bld)
{
    int n, k;

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

    if (n >= 3) /* $76B2: walk to the corner marker */
    {
        for (k = 0; k < 4; k++)
        {
            const int mark = zon_corner(k);
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

    /*  $7882: and finally shift from whichever corner we are on to the
     *  one the rest of the game calls the origin. */
    {
        const int here = zon_at(c, *py, *px);
        if (here == zon_corner(0))
            *px += n - 1;
        else if (here == zon_corner(1))
        {
            *px += n - 1;
            *py -= n - 1;
        }
        else if (here == zon_corner(2))
            *py -= n - 1;
    }
    return n;
}

/*  $41B8 and $42AC: which infra[] counter a military building id belongs
 *  to.  The two switches in $4110 share this one table. */
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
 *  $4110  setTile: the routine 168 call sites funnel through.
 *
 *  Besides writing XBLD it keeps a running census of every building id
 *  on the map.  That census is what lets the growth pass ask questions
 *  like "how many churches are there" without walking the map.
 *
 *  The id is a word.  XBLD keeps its low byte, and the census counts the
 *  whole word.  The network fitters read a table past its end, and then
 *  give a word of $100 or more.  That count lands past the end of the
 *  census, outside anything the city keeps, so the low byte gets none.
 * ================================================================== */
void sim_set_tile(City *c, int y, int x, int bld)
{
    uint8_t old;

    if (y < 0 || y >= c->h || x < 0 || x >= c->w)
        return; /* $4124 */

    if (XZON_TYPE(T_XZON(c, y, x)) == ZONE_MILITARY)
    {
        /*  $418C: a military tile keeps its own tally instead.  Two
         *  identical 29-case switches over ids $DD..$F9 pick a slot in
         *  infra[]: the old building's slot goes down, the new one's
         *  goes up.  Anything outside that range, and every id in it the
         *  table does not name, lands on slot 0. */
        c->infra[infra_slot(T_XBLD(c, y, x))]--; /* $41B8 */
        c->infra[infra_slot(bld)]++;             /* $42AC */
        T_XBLD(c, y, x) = (uint8_t)bld;          /* $4396 */
        return;
    }

    old = T_XBLD(c, y, x); /* $4166 */
    c->census[old]--;      /* $4174 */
    if (bld < 0x100)
        c->census[bld]++;           /* $417E */
    T_XBLD(c, y, x) = (uint8_t)bld; /* $4184 */
}
