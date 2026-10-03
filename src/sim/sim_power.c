/*  sim_power.c: the power line pieces, $0E to $1C.  How a tile of the
 *  network fits its piece to its neighbors.  Addresses point into CODE 2. */
#include "sim.h"
#include "sim_int.h"

enum
{
    POWER_FIRST = 0x0E, /* the first power line piece  */
    POWER_END   = 0x1D  /* one past the last           */
};

/* ================================================================== *
 *  $1F2FE  fit the power line piece on (y,x).  A tile without a power
 *  line keeps what it has.  On a slope that NET_SLOPED names, the
 *  slope alone gives the piece.  Anywhere else the line joins each
 *  neighbor that conducts power and stands on terrain the line can
 *  reach.  The original then redraws the tile at $1F49C.
 * ================================================================== */
void sim_fit_power(City *c, int y, int x)
{
    const int b    = c->xbld[y][x];
    int       s    = c->xter[y][x];
    int       mask = 0;

    if (b < POWER_FIRST || b >= POWER_END)
        return; /* $1F32E */

    if (s >= 0x30)
        s = 0; /* $1F34C: water and the codes from $30 up count as flat */

    /*  $1F358: the test reads the low four bits, but the piece is
     *  indexed by the whole XTER. */
    if (NET_SLOPED[s & 0x0F])
    {
        sim_set_tile(c, y, x, POWER_FIRST + NET_SLOPE_PIECE[s]); /* $1F370 */
        return;
    }

    if (y > 0 && (c->xbit[y - 1][x] & XBIT_CONDUCTIVE) && NET_JOIN_Y[c->xter[y - 1][x]])
        mask += 8; /* $1F3B4 */
    if (y < 0x7F && (c->xbit[y + 1][x] & XBIT_CONDUCTIVE) && NET_JOIN_Y[c->xter[y + 1][x]])
        mask += 2; /* $1F3F0 */
    if (x > 0 && (c->xbit[y][x - 1] & XBIT_CONDUCTIVE) && NET_JOIN_X[c->xter[y][x - 1]])
        mask += 1; /* $1F434 */
    if (x < 0x7F && (c->xbit[y][x + 1] & XBIT_CONDUCTIVE) && NET_JOIN_X[c->xter[y][x + 1]])
        mask += 4; /* $1F47C */

    sim_set_tile(c, y, x, POWER_FIRST + NET_PIECE[mask]); /* $1F490 */
}
