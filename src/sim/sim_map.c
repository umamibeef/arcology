/*  sim_map.c -- changing what is on a tile.  The bulldozer and everything
 *  it has to understand: a run along a line, the 2x2 and larger footprints,
 *  the bridges that sink when their ends go, the slope code the terrain is
 *  fixed up to afterwards, and the footprint stamp that puts a new building
 *  down.  Every path that writes XBLD other than growth and placement comes
 *  through here.  Split out of sim.c; addresses still point into CODE 2. */
#include "ext80.h"
#include "sim.h"
#include "sim_int.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

/*  The eight neighbours in the order the original walks them: A5-0x4F4E and
 *  A5-0x4F3C, which are the same two tables the disasters step by.  Shared
 *  through sim_int.h rather than written out twice. */
const int BEAM_DY[8] = {0, 1, 1, 1, 0, -1, -1, -1};
const int BEAM_DX[8] = {-1, -1, 0, 1, 1, 1, 0, -1};
/*  A5-0x10D2.  Higher wins.  A seaport or airport ($C7, $C8 -> 8, 9)
 *  outranks everything, and an ordinary zone ranks 1. */
static const uint8_t PROBLEM_RANK[16] = {0, 1, 1, 1, 1, 1, 1, 1, 4, 8, 5, 5, 5, 5, 5, 5};
/*  A5-0x4DF6.  Neighbour i lifts these corners of the tile.  The eight
 *  entries walk the compass from west, so the diagonals name one corner
 *  and the sides name two. */
static const uint8_t SLOPE_CORNER[8] = {3, 2, 6, 4, 12, 8, 9, 1};
/*  A5-0x4DEE, read only through a four-bit index: the slope code for
 *  each set of raised corners.  $32 at the end is the "raise this tile"
 *  answer, not a code. */
static const uint8_t SLOPE_CODE[16] = {0, 9, 10, 2, 11, 13, 3, 6, 12, 1, 13, 5, 4, 8, 7, 0x32};
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

/* ================================================================== *
 *  $324B8  nearPowered -- is this tile, or any of its four orthogonal
 *  neighbours, supplied with power?  XBIT bit $40.
 *
 *  (Not a water test: $40 is XBIT_POWERED, set by the power flood at
 *  $20FC4.  Water covered is bit $04, which is what $3590 rejects.)
 *  The edge guards are asymmetric in the original: it looks one row up
 *  only from row 2, and one row down only to row 126.
 * ================================================================== */
int near_powered(const City *c, int y, int x)
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
void set_under(City *c, int y, int x, uint8_t und)
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

/*  $68D0 and $6BD4 -- four building ids are not square.  A runway or a pier
 *  is a run of identical tiles, so those are taken down by flooding over
 *  the neighbours that carry the same id rather than by walking a
 *  footprint.  Nothing marks a tile as visited: the rubble written into
 *  XBLD no longer matches, which is what stops the walk.  The queue is
 *  popped from the back, so the flood is depth first. */
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

/*  $606E -- a bridge is a run of tiles over water, not a footprint.  Taking
 *  one down means putting the water back: every tile of the run is cleared,
 *  and the land tile at each end is dropped a level and flooded.  The run's
 *  direction comes from bit 1 of XBIT, which is the orientation flag a
 *  two-form tile needs.  Neither walk is bounded in the original.  A bridge
 *  always has land at both ends, so it stops; the guards here only keep the
 *  C inside its arrays. */
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
        /*  Every way out of here writes the tile itself, so the code
         *  the slope would have carried is not read again. */

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
void clear_tile(City *c, int y, int x)
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
int stamp_footprint(City *c, int y, int x, int bld, int size)
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
void clear_footprint(City *c, int y, int x)
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
