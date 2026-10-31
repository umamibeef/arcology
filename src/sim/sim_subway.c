/*  sim_subway.c: the subway, XUND $01 to $0F.  How a tile of the network fits its piece to
 *  its neighbors.  Addresses point into CODE 2. */
#include "sim.h"
#include "sim_int.h"

/*  A tile the subway joins.  It is a piece of track, $01..$0F, or one of
 *  the four values that carry the subway with something else.  $1F and
 *  $20 cross a pipe, a missile silo puts $22 under itself, and $23 is a
 *  station.  These are also the values that $43A2 counts toward
 *  transit. */
static int joins(int u)
{
    return (u >= 0x01 && u < 0x10) || u == 0x23 || u == 0x1F || u == 0x20 || u == 0x22;
}

/* ================================================================== *
 *  $20352  fit the piece on (y,x) to its neighbors.  A tile that does
 *  not carry track, $01..$0F, is left alone.
 *
 *  The slope is the low four bits of XTER.  A tile with XTER above $30
 *  counts as flat.  On a slope that NET_SLOPED marks, the tile gets
 *  the ramp that NET_SLOPE_PIECE gives, and the neighbors do not matter.
 *
 *  On any other tile, a neighbor adds a bit when the subway joins it
 *  and its terrain lets the track in.  The neighbor at y-1 adds 8 and
 *  the neighbor at y+1 adds 2, if NET_JOIN_Y lets them in.  The
 *  neighbor at x-1 adds 1 and the neighbor at x+1 adds 4, if NET_JOIN_X
 *  lets them in.  NET_PIECE turns the four bits into the piece.  The
 *  piece is one more than the entry of either table.
 *
 *  The original has a defect on a slope that raises one corner, 9..12.
 *  NET_SLOPED marks these, but the table proper of NET_SLOPE_PIECE has
 *  entries only for 0..4.  So the original reads the bytes of NET_JOIN_X
 *  as words, $101 or $100.  It gives $43A2 the word $102 or $101, which
 *  writes the piece $02 or $01 and does not count it toward transit.
 *  This is transcribed as written.
 * ================================================================== */
void sim_fit_subway(City *c, int y, int x)
{
    int u, t, slope, mask;

    u = T_XUND(c, y, x);
    if (u < 0x01 || u >= 0x10)
        return; /* $20382 */

    t     = T_XTER(c, y, x);
    slope = t > 0x30 ? 0 : t & 0x0F; /* $2039A */
    if (NET_SLOPED[slope])           /* $203B2 */
    {
        set_under(c, y, x, NET_SLOPE_PIECE[slope] + 1); /* $203CA */
        return;
    }

    mask = 0;
    if (y > 0 && joins(T_XUND(c, y - 1, x)) && NET_JOIN_Y[T_XTER(c, y - 1, x)])
        mask += 8; /* $20452 */
    if (y < c->h - 1 && joins(T_XUND(c, y + 1, x)) && NET_JOIN_Y[T_XTER(c, y + 1, x)])
        mask += 2; /* $204D2 */
    if (x > 0 && joins(T_XUND(c, y, x - 1)) && NET_JOIN_X[T_XTER(c, y, x - 1)])
        mask += 1; /* $20560 */
    if (x < c->w - 1 && joins(T_XUND(c, y, x + 1)) && NET_JOIN_X[T_XTER(c, y, x + 1)])
        mask += 4;                           /* $205F2 */
    set_under(c, y, x, NET_PIECE[mask] + 1); /* $20606 */
}
