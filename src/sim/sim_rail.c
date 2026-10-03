/*  sim_rail.c: the rail pieces, $2C to $3A.  How a tile of the network fits its piece to
 *  its neighbors.  Addresses point into CODE 2. */
#include "sim.h"
#include "sim_int.h"

/*  The fifteen pieces the fitter chooses start at RAIL_FIRST.  The four
 *  ids from RAIL_END continue a slope piece onto the next tile.  They are
 *  not in the range the fitter takes, so a tile does not change again
 *  after it gets one. */
enum
{
    RAIL_FIRST = 0x2C, /* the straight piece along the columns */
    RAIL_END   = 0x3B
};

/* ================================================================== *
 *  $1FC2E  what a rail piece links to.  The rail pieces and their slope
 *  ends ($2C..$3E), the crossings that carry a rail ($45..$48), the
 *  subway connections ($6C..$6F), and a highway over a rail ($4D, $4E).
 *  The original does not look at the axis of a crossing.
 * ================================================================== */
static int rail_links_to(int b)
{
    return (b >= 0x2C && b < 0x3F) || (b >= 0x45 && b < 0x49) || (b >= 0x6C && b < 0x70) ||
           b == 0x4D || b == 0x4E;
}

/* ================================================================== *
 *  $1FB60  fit the rail piece on (y,x) to its neighbors.  A tile that
 *  is not a piece from $2C to $3A keeps what it has.
 *
 *  A slope that NET_SLOPED marks chooses the piece alone.  The
 *  terrain from $30 up counts as flat.  Then the four neighbors go in
 *  the order y-1, y+1, x-1, x+1.  A neighbor that is the slope piece
 *  for its side gives the tile the end piece for that side, and the
 *  first one wins.
 *
 *  Else each neighbor the rail links to counts, unless the slope under
 *  it is across the link.  A tile that XTXT marks $FA has a link to a
 *  neighbor city, so it also links off each map edge it is on.
 *
 *  For terrain from 5 to $2F, NET_SLOPE_PIECE is past its end and reads
 *  the next table as words.  The piece then comes out at $100 or more.
 *  This is a defect of the original, and the C does the same.
 * ================================================================== */
void sim_fit_rail(City *c, int y, int x)
{
    int bld = c->xbld[y][x], ter, link = 0;

    if (bld < RAIL_FIRST || bld >= RAIL_END)
        return; /* $1FB90 */

    ter = c->xter[y][x];
    if (ter >= 0x30)
        ter = 0; /* $1FBAE */
    if (NET_SLOPED[ter & 0x0F])
    {
        sim_set_tile(c, y, x, RAIL_FIRST + NET_SLOPE_PIECE[ter]); /* $1FBD2 */
        return;
    }

    if (y > 0)
    {
        bld = c->xbld[y - 1][x];
        if (bld == 0x2E) /* the slope piece for terrain 1 */
        {
            sim_set_tile(c, y, x, 0x3B); /* $1FC16 */
            return;
        }
        if (rail_links_to(bld) && NET_JOIN_Y[c->xter[y - 1][x]])
            link += 8; /* $1FC7E */
    }
    if (y < MAP_H - 1)
    {
        bld = c->xbld[y + 1][x];
        if (bld == 0x30) /* the slope piece for terrain 3 */
        {
            sim_set_tile(c, y, x, 0x3D); /* $1FCAE */
            return;
        }
        if (rail_links_to(bld) && NET_JOIN_Y[c->xter[y + 1][x]])
            link += 2; /* $1FD16 */
    }
    if (x > 0)
    {
        bld = c->xbld[y][x - 1];
        if (bld == 0x2F) /* the slope piece for terrain 2 */
        {
            sim_set_tile(c, y, x, 0x3C); /* $1FD44 */
            return;
        }
        if (rail_links_to(bld) && NET_JOIN_X[c->xter[y][x - 1]])
            link += 1; /* $1FDAE */
    }
    if (x < MAP_W - 1)
    {
        bld = c->xbld[y][x + 1];
        if (bld == 0x31) /* the slope piece for terrain 4 */
        {
            sim_set_tile(c, y, x, 0x3E); /* $1FDE0 */
            return;
        }
        if (rail_links_to(bld) && NET_JOIN_X[c->xter[y][x + 1]])
            link += 4; /* $1FE4A */
    }

    if (c->xtxt[y][x] == 0xFA) /* $1FE60 */
    {
        if (y == 0)
            link += 8;
        if (y == MAP_H - 1)
            link += 2;
        if (x == 0)
            link += 1;
        if (x == MAP_W - 1)
            link += 4;
    }
    sim_set_tile(c, y, x, RAIL_FIRST + NET_PIECE[link]); /* $1FE94 */
}
