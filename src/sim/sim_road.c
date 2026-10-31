/*  sim_road.c: the road pieces, $1D to $2B.  How a tile of the network fits its piece to
 *  its neighbors.  Addresses point into CODE 2. */
#include "sim.h"
#include "sim_int.h"

/*  The bits of the join mask that indexes NET_PIECE.  Each bit is one
 *  neighbor the road joins. */
enum
{
    JOIN_X_BEFORE = 1, /* (y, x-1) */
    JOIN_Y_AFTER  = 2, /* (y+1, x) */
    JOIN_X_AFTER  = 4, /* (y, x+1) */
    JOIN_Y_BEFORE = 8  /* (y-1, x) */
};

/*  $1F6D2: the pieces a road joins.  These are a road, a tunnel end or a
 *  crossing ($3F to $46), a highway over a road ($4B, $4C), and an
 *  on-ramp ($5D to $60).  The test does not look at the axis of the
 *  neighbor.  A crossing that runs across the road joins it all the same. */
static int road_joins(int b)
{
    return (b >= 0x1D && b < 0x2C) || (b >= 0x3F && b < 0x47) || b == 0x4B || b == 0x4C ||
           (b >= 0x5D && b < 0x61);
}

int sim_road_joins(int b)
{
    return road_joins(b);
}

/* ================================================================== *
 *  $1F632  fit the road piece on (y,x) to its neighbors.  A tile that
 *  does not carry a road piece stays as it is.
 *
 *  On a slope that NET_SLOPED marks, the piece comes from the terrain
 *  alone, through NET_SLOPE_PIECE.  Water terrain ($30 and up) counts as
 *  flat.  On other terrain, a neighbor adds its bit to a mask when it
 *  carries a piece the road joins.  The terrain of the neighbor must
 *  also let the road in.  NET_JOIN_Y tests the two neighbors along y,
 *  and NET_JOIN_X the two along x.  A tile labeled $FA in XTXT also
 *  joins each edge of the map it is on.  NET_PIECE turns the mask into
 *  the piece.
 *
 *  NET_SLOPE_PIECE holds five words, for terrain 0 to 4.  The original
 *  indexes it with the full terrain value.  So a slope from 9 to 12, or
 *  a sloped tile under or at the edge of water, reads the bytes of
 *  NET_JOIN_X that come next as words.  That gives a piece above $FF.
 *  The table here holds the same bytes.
 * ================================================================== */
void sim_fit_road(City *c, int y, int x)
{
    int ter, mask = 0;

    if (!(T_XBLD(c, y, x) >= 0x1D && T_XBLD(c, y, x) < 0x2C))
        return; /* $1F662 */

    ter = T_XTER(c, y, x); /* $1F676 */
    if (ter >= 0x30)
        ter = 0;
    if (NET_SLOPED[ter & 0x0F]) /* $1F68C */
    {
        sim_set_tile(c, y, x, 0x1D + NET_SLOPE_PIECE[ter]); /* $1F6A4 */
        return;
    }

    if (y > 0 && road_joins(T_XBLD(c, y - 1, x)) && NET_JOIN_Y[T_XTER(c, y - 1, x)])
        mask += JOIN_Y_BEFORE; /* $1F756 */
    if (y < c->h - 1 && road_joins(T_XBLD(c, y + 1, x)) && NET_JOIN_Y[T_XTER(c, y + 1, x)])
        mask += JOIN_Y_AFTER; /* $1F800 */
    if (x > 0 && road_joins(T_XBLD(c, y, x - 1)) && NET_JOIN_X[T_XTER(c, y, x - 1)])
        mask += JOIN_X_BEFORE; /* $1F8B8 */
    if (x < c->w - 1 && road_joins(T_XBLD(c, y, x + 1)) && NET_JOIN_X[T_XTER(c, y, x + 1)])
        mask += JOIN_X_AFTER; /* $1F974 */

    /*  $1F98A: the label $FA joins the edge of the map.  The neighbor
     *  test skips that side, so its bit is clear before this. */
    if (T_XTXT(c, y, x) == 0xFA)
    {
        if (y == 0)
            mask += JOIN_Y_BEFORE;
        if (y == c->h - 1)
            mask += JOIN_Y_AFTER;
        if (x == 0)
            mask += JOIN_X_BEFORE;
        if (x == c->w - 1)
            mask += JOIN_X_AFTER;
    }

    sim_set_tile(c, y, x, 0x1D + NET_PIECE[mask]); /* $1F9BE */
}
