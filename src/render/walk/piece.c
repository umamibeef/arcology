/*  The piece tables: which family a tile's piece belongs to, how it
 *  links to its neighbours, and the second piece a tile may carry. */
#include <math.h>
#include <string.h>

#include "mesh/internal.h"
#include "net/internal.h"

const uint8_t *s_check_xbld; /* the last built city's XBLD, for the piece scan */

/*  What road a building byte carries, as arc.rules.road_tiles reads the
 *  city: 1 a road piece, 2 a road crossing something, 3 a road running
 *  under a deck that stands on the tile.  Read once a generation into a
 *  table, since every tile of the map is looked up in it. */
static int road_carried(uint8_t b)
{
    return script_bytes("road_tiles")[b];
}

/*  A tile whose building stands up: a structure the deck would have to
 *  clear, rather than a surface network it crosses over.  Which bytes is
 *  arc.rules.standing_tiles. */
int net_stands_up(uint8_t b)
{
    static uint8_t up[256];
    static int     gen = -1;
    if (gen != script_generation())
    {
        gen = script_generation();
        memcpy(up, script_bytes("standing_tiles"), sizeof up);
    }
    return up[b];
}

/*  A CARRIER: a bridge, a tunnel end, a crossing or a highway -- a tile
 *  a road runs on into rather than stopping at.  Which bytes is
 *  arc.rules.carrier_tiles. */
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

/*  A road crossing a railway -- a node of both networks, which pins the
 *  two to one altitude.  Which bytes is arc.rules.road_tiles's
 *  "crossing" entries that name a railway. */
int net_road_over_rail(uint8_t b)
{
    static uint8_t over[256];
    static int     gen = -1;
    if (gen != script_generation())
    {
        gen = script_generation();
        memcpy(over, script_bytes("rail_crossing_tiles"), sizeof over);
    }
    return over[b];
}

/*  A road standing on the tile itself. */
int net_road_on(uint8_t b)
{
    int k = road_carried(b);
    return k == 1 || k == 2;
}

/*  A road a ramp or a lane may join: on the tile, or under a deck that
 *  stands on it. */
int net_road_near(uint8_t b)
{
    return road_carried(b) != 0;
}

/*  The family a piece belongs to and its place in the shared layout, or
 *  -1 for a byte no family claims.  Which byte is which is the SCRIPT'S
 *  (scripts/net_tiles.lua), read once a generation into a table: every
 *  tile of the map is looked up in it, twice.
 *
 *  With no rule no byte carries a network at all, which is what a run
 *  that cannot find the scripts draws. */
static void piece_table(const unsigned char **fam, const signed char **piece,
                        const unsigned char **fam2, const signed char **piece2)
{
    script_pieces(fam, piece, fam2, piece2);
}

/*  A crossing answers for the family whose surface it is; piece_second
 *  gives the other one, on the other axis. */
int piece_family(uint8_t b, Family *f)
{
    const unsigned char *fam, *fam2;
    const signed char   *piece, *piece2;
    piece_table(&fam, &piece, &fam2, &piece2);
    *f = (Family)fam[b];
    return piece[b];
}

/*  The second family a crossing carries, on the other axis: its piece
 *  index, or -1. */
int piece_second(uint8_t b, Family *f)
{
    const unsigned char *fam, *fam2;
    const signed char   *piece, *piece2;
    piece_table(&fam, &piece, &fam2, &piece2);
    *f = (Family)fam2[b];
    return piece2[b];
}

/*  A crossing a RAILWAY is part of, on either axis: a road over a line,
 *  or a line under a power line.  Derived from the same table the
 *  crossings themselves are declared in, so a script that names them
 *  differently moves this with them. */
int net_rail_crossing(uint8_t b)
{
    const unsigned char *fam, *fam2;
    const signed char   *piece, *piece2;
    piece_table(&fam, &piece, &fam2, &piece2);
    return piece2[b] >= 0 && (fam[b] == F_RAIL || fam2[b] == F_RAIL);
}


const float ROAD_MU[4] = {0.5f, 1.0f, 0.5f, 0.0f}; /* edge midpoints, N E S W */
const float ROAD_MV[4] = {0.0f, 0.5f, 1.0f, 0.5f};
const float ROAD_DU[4] = {0.0f, 1.0f, 0.0f, -1.0f}; /* out through the edge */
const float ROAD_DV[4] = {-1.0f, 0.0f, 1.0f, 0.0f};

/*  Which edges a piece joins, from the road art: the asphalt (palette
 *  0x91) or a dash (0x8B) in the three-by-three around each edge's
 *  midpoint, a tenth of the way in.  The slope pieces' art is tall; they
 *  are straight along their slope, which the terrain code says. */
/*  The artwork a piece's links are read from, and the answers read so
 *  far.  It is always the FINEST level, whatever level is being drawn: a
 *  road piece's connectivity is a property of its tile id, not of how
 *  large the sprite is.  At eight pixels the three-by-three window the
 *  probe puts at each quarter of the diamond reaches the middle of the
 *  tile, finds road under all four, and reads every piece as a
 *  crossroads. */
static const RAtlasLevel *s_piece_art;
static int8_t             s_piece_links[15];
static int                s_piece_read;

void net_piece_art(const RAtlas *a)
{
    const RAtlasLevel *fine = a && a->n_levels > 0 ? &a->level[a->n_levels - 1] : NULL;
    if (fine == s_piece_art)
        return;
    s_piece_art  = fine;
    s_piece_read = 0;
}

int piece_links(const RAtlasLevel *l, int piece, uint8_t xter)
{
    const RTile *t;
    int32_t      tw, th, y0, e, links = 0;
    if (s_piece_art)
        l = s_piece_art;
    tw = l->tile_w, th = l->tile_h;
    if (!s_piece_read)
    {
        memset(s_piece_links, -1, sizeof s_piece_links);
        s_piece_read = 1;
    }
    if (piece < 0 || piece > 14)
        return 0;
    if (piece >= 2 && piece <= 5)
    {
        uint8_t mask = CODE_MASK[slope_code(xter)];
        return (mask == 3 || mask == 12) ? (L_E | L_W) : (L_N | L_S);
    }
    if (s_piece_links[piece] >= 0)
        return s_piece_links[piece];
    t = atlas_tile(l, l->id_base + 0x1D + piece);
    if (!t)
        return 0;
    y0 = (int32_t)t->h - (th + 1); /* the diamond fills the bottom rows */
    for (e = 0; e < 4; ++e)
    {
        static const float mx[4] = {0.25f, 0.25f, 0.75f, 0.75f};
        static const float my[4] = {0.25f, 0.75f, 0.75f, 0.25f};
        int32_t            sx    = (int32_t)((0.5f + (mx[e] - 0.5f) * 0.9f) * (float)tw);
        int32_t            sy    = y0 + (int32_t)((0.5f + (my[e] - 0.5f) * 0.9f) * (float)th);
        int32_t            dx, dy, n = 0;
        for (dy = -1; dy <= 1; ++dy)
            for (dx = -1; dx <= 1; ++dx)
            {
                int32_t x = sx + dx, y = sy + dy;
                uint8_t v;
                if (x < 0 || y < 0 || x >= (int32_t)t->w || y >= (int32_t)t->h)
                    continue;
                v = l->indices[((size_t)t->y + (size_t)y) * (size_t)l->w +
                               (size_t)t->x + (size_t)x];
                if (v == 0x91u || v == 0x8Bu)
                    ++n;
            }
        if (n >= 2)
            links |= 1 << e;
    }
    s_piece_links[piece] = (int8_t)links;
    return links;
}

/*  What a tile carries of a family: its links, or 0. */
int tile_links(const RCity *c, const RAtlasLevel *l, int32_t col, int32_t row, Family want)
{
    int32_t idx;
    Family  f;
    int     piece;
    if (col < 0 || row < 0 || col >= R_MAP || row >= R_MAP)
        return 0;
    idx   = row * R_MAP + col;
    piece = piece_family(c->xbld[idx], &f);
    if (piece >= 0 && f == want)
        return piece_links(l, piece, c->xter[idx]);
    piece = piece_second(c->xbld[idx], &f);
    if (piece >= 0 && f == want)
        return piece_links(l, piece, c->xter[idx]);
    return 0;
}

int link_count(int links)
{
    return (links & 1) + ((links >> 1) & 1) + ((links >> 2) & 1) + ((links >> 3) & 1);
}

/*  The links a tile can actually follow: those its neighbour returns,
 *  and those that leave the map.  A piece whose art points at grass, a
 *  stub against the flat side of a T, a road bulldozed short: the
 *  original draws every such sprite as it is, so the segment must end
 *  there, at a butt end, rather than the tile going undrawn because
 *  the chain it lies on has no node to be walked from. */
int eff_links(const RCity *c, const RAtlasLevel *l, int32_t col, int32_t row, Family f)
{
    int links = tile_links(c, l, col, row, f), out = 0, e;
    for (e = 0; e < 4; ++e)
    {
        int32_t nc, nr;
        if (!(links & (1 << e)))
            continue;
        nc = col + (int32_t)ROAD_DU[e];
        nr = row + (int32_t)ROAD_DV[e];
        if (nc < 0 || nr < 0 || nc >= R_MAP || nr >= R_MAP)
            out |= 1 << e; /* off the map: the road runs to the edge */
        else if (tile_links(c, l, nc, nr, f) & (1 << ((e + 2) & 3)))
            out |= 1 << e;
    }
    return out;
}
