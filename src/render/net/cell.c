/*  cell.c: WHAT A CELL CARRIES.  Which family a tile's piece belongs
 *  to.  How it links to its neighbors.  And the second piece a tile may
 *  carry.  The piece's own arithmetic is mesh/piece.c's. */
#include <math.h>
#include <string.h>

#include "mesh/internal.h"
#include "pipeline.h"
#include "net/net.h"
#include "net/cell.h"
#include "script.h"

const uint8_t *s_check_xbld; /* the last built city's XBLD, for the piece scan */

/*  What line a building byte carries, as arc.bytes("line_tiles") reads the
 *  city.  1 is a line piece, and 2 a line lapped by something.  3 is a
 *  line running under a slab that stands on the tile.  Read once a
 *  generation into a table, since every tile of the map is looked up in
 *  it. */
static int line_carried(uint8_t b)
{
    static uint8_t carried[256];
    static int     gen = -1;
    if (gen != script_generation())
    {
        gen = script_generation();
        memcpy(carried, script_bytes("line_tiles"), sizeof carried);
    }
    return carried[b];
}

/*  A CARRIER: a bridge, a tunnel end, a meet or a band: a tile a line
 *  runs on into rather than stopping at.  Which bytes is
 *  arc.bytes("carrier_tiles"). */
int net_carrier(uint8_t b)
{
    static uint8_t carries[256];
    static int     gen = -1;
    if (gen != script_generation())
    {
        gen = script_generation();
        memcpy(carries, script_bytes("carrier_tiles"), sizeof carries);
    }
    return carries[b];
}

/*  Two families lapping one cell: a node of both networks, which pins
 *  the two to one altitude.  Which bytes is arc.bytes("line_tiles")'s
 *  "meet" entries that name the second family. */
int net_line_lapped(uint8_t b)
{
    static uint8_t over[256];
    static int     gen = -1;
    if (gen != script_generation())
    {
        gen = script_generation();
        memcpy(over, script_bytes("lap_tiles"), sizeof over);
    }
    return over[b];
}

/*  A line standing on the tile itself. */
int net_line_on(uint8_t b)
{
    int k = line_carried(b);
    return k == 1 || k == 2;
}

/*  A line a spur or a lane may join: on the tile, or under a slab that
 *  stands on it. */
int net_line_near(uint8_t b)
{
    return line_carried(b) != 0;
}

/*  The family a piece belongs to and its place in the shared layout, or
 *  -1 for a byte no family claims.  Which byte is which is the SCRIPT'S
 *  (scripts/net_tiles.lua), read once a generation into a table: every
 *  tile of the map is looked up in it, twice.
 *
 *  Three families share one layout of fifteen pieces.  They are two
 *  straights, four slopes, four corners, four tees and a lap.  Which
 *  byte each family's pieces start at is the script's (arc.pieces), and
 *  each family takes the piece at the same offset.  Which edges a piece
 *  joins comes from the art, which a script reads
 *  (arc.bytes("piece_links")).  A meet carries two families on one tile.
 *
 *  With no rule no byte carries a network at all, which is what a run
 *  that cannot find the scripts draws. */
static void piece_table(const unsigned char **fam, const signed char **piece,
                        const unsigned char **fam2, const signed char **piece2)
{
    script_pieces(fam, piece, fam2, piece2);
}

/*  A meet answers for the family whose surface it is.  Piece_second
 *  gives the other one, on the other axis. */
int piece_family(uint8_t b, Family *f)
{
    const unsigned char *fam, *fam2;
    const signed char   *piece, *piece2;
    piece_table(&fam, &piece, &fam2, &piece2);
    *f = (Family)fam[b];
    return piece[b];
}

/*  The second family a meet carries, on the other axis: its piece
 *  index, or -1. */
int piece_second(uint8_t b, Family *f)
{
    const unsigned char *fam, *fam2;
    const signed char   *piece, *piece2;
    piece_table(&fam, &piece, &fam2, &piece2);
    *f = (Family)fam2[b];
    return piece2[b];
}

/*  A lap the SECOND family is part of, on either axis: a line over a line,
 *  or a line under a power line.  Derived from the same table the
 *  meets themselves are declared in, so a script that names them
 *  differently moves this with them. */
int net_thread_lap(uint8_t b)
{
    const unsigned char *fam, *fam2;
    const signed char   *piece, *piece2;
    piece_table(&fam, &piece, &fam2, &piece2);
    return piece2[b] >= 0 && (fam[b] == net_thread->f || fam2[b] == net_thread->f);
}


const float SIDE_MU[4] = {0.5f, 1.0f, 0.5f, 0.0f}; /* edge midpoints, N E S W */
const float SIDE_MV[4] = {0.0f, 0.5f, 1.0f, 0.5f};
const float SIDE_DU[4] = {0.0f, 1.0f, 0.0f, -1.0f}; /* out through the edge */
const float SIDE_DV[4] = {-1.0f, 0.0f, 1.0f, 0.0f};

/*  WHICH EDGES A PIECE JOINS.  Every piece but one answers from the
 *  LINE ART, which a script read for it (`piece_links`,
 *  scripts/art_tiles.lua).  A run that cannot find its scripts joins
 *  nothing, which is a city with no network at all.
 *
 *  A SLOPE piece is the exception.  It is straight along its slope, and
 *  no picture can say which way that runs: the terrain does
 *  (`slope_runs_ew`).  Which pieces are slopes is the script's as well
 *  (`slope_pieces`).  So this file and the one that reads the art
 *  cannot come to differ about it.
 *
 *  All three are read once a generation into tables of their own, since
 *  every tile of the map asks this several times. */
int piece_links(int piece, uint8_t xter)
{
    static uint8_t joins[15], slope[15], runs_ew[256];
    static int     gen = -1;
    if (piece < 0 || piece > 14)
        return 0;
    if (gen != script_generation())
    {
        const unsigned char *t = script_bytes("piece_links");
        const unsigned char *p = script_bytes("slope_pieces");
        int                  i;
        gen = script_generation();
        memcpy(runs_ew, script_bytes("slope_runs_ew"), sizeof runs_ew);
        for (i = 0; i < 15; ++i)
            joins[i] = t[i], slope[i] = p[i];
    }
    if (slope[piece])
        return runs_ew[slope_code(xter)] ? (L_E | L_W) : (L_N | L_S);
    return joins[piece];
}

/*  What a tile carries of a family: its links, or 0. */
int tile_links(const RCity *c, int32_t col, int32_t row, Family want)
{
    int32_t idx;
    Family  f;
    int     piece;
    if (col < 0 || row < 0 || col >= R_MAP || row >= R_MAP)
        return 0;
    idx   = row * R_MAP + col;
    piece = piece_family(c->xbld[idx], &f);
    if (piece >= 0 && f == want)
        return piece_links(piece, c->xter[idx]);
    piece = piece_second(c->xbld[idx], &f);
    if (piece >= 0 && f == want)
        return piece_links(piece, c->xter[idx]);
    return 0;
}

int link_count(int links)
{
    return (links & 1) + ((links >> 1) & 1) + ((links >> 2) & 1) + ((links >> 3) & 1);
}

/*  The links a tile can actually follow: those its neighbor returns, and
 *  those that leave the map.  A piece whose art points at grass, a stub
 *  against the flat side of a T, a line bulldozed short.  The original
 *  draws every such sprite as it is.  So the segment must end there, at
 *  a butt end.  Otherwise the tile goes undrawn, because the chain it
 *  lies on has no node to be walked from. */
int eff_links(const RCity *c, int32_t col, int32_t row, Family f)
{
    int links = tile_links(c, col, row, f), out = 0, e;
    for (e = 0; e < 4; ++e)
    {
        int32_t nc, nr;
        if (!(links & (1 << e)))
            continue;
        nc = col + (int32_t)SIDE_DU[e];
        nr = row + (int32_t)SIDE_DV[e];
        if (nc < 0 || nr < 0 || nc >= R_MAP || nr >= R_MAP)
            out |= 1 << e; /* off the map: the line runs to the edge */
        else if (tile_links(c, nc, nr, f) & (1 << ((e + 2) & 3)))
            out |= 1 << e;
    }
    return out;
}
