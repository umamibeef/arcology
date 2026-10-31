/*  sim_pipe.c: the water pipes, XUND $10 to $1E.  How a tile of the network fits its piece to
 *  its neighbors.  Addresses point into CODE 2. */
#include "sim.h"
#include "sim_int.h"

/*  The pipe pieces are XUND $10 to $1E.  $1F and $20 are the two tiles
 *  where a pipe crosses the subway. */
enum
{
    PIPE_FIRST = 0x10,
    PIPE_END   = 0x1F,
    PIPE_CROSS = 0x20
};

/*  A neighbor carries a pipe when it holds a pipe piece or either
 *  crossing.  The test does not look at the way a crossing runs, so a
 *  pipe joins a crossing from all four sides. */
static int carries_pipe(int u)
{
    if (u >= PIPE_FIRST && u < PIPE_END)
        return 1;                            /* $20058 */
    return u == PIPE_END || u == PIPE_CROSS; /* $2007A */
}

/* ================================================================== *
 *  $1FFB8  fit the pipe piece on (y,x) to its neighbors.  A tile with
 *  no pipe piece is left as it is.  On a slope that NET_SLOPED marks,
 *  the tile takes the sloped piece and the neighbors do not count.
 *  Elsewhere each neighbor that carries a pipe, and whose terrain lets
 *  the pipe through, adds its bit to the mask that picks the piece.  A
 *  tile that joins nothing takes the piece for all four sides.
 *
 *  The sloped piece for slopes 9 to 12 reads past the end of
 *  NET_SLOPE_PIECE and gets $110 or $111.  The original gives that word
 *  to $43A2, which keeps its low byte, so the tile takes $10 or $11.
 * ================================================================== */
void sim_fit_pipe(City *c, int y, int x)
{
    const int u = T_XUND(c, y, x);
    int       slope, mask = 0;

    if (u < PIPE_FIRST || u >= PIPE_END)
        return; /* $1FFE8 */

    slope = T_XTER(c, y, x);
    slope = slope > 0x30 ? 0 : slope & 0x0F; /* $20000 */
    if (NET_SLOPED[slope])                   /* $20018 */
    {
        set_under(c, y, x, PIPE_FIRST + NET_SLOPE_PIECE[slope]); /* $20030 */
        return;
    }

    if (y > 0 && carries_pipe(T_XUND(c, y - 1, x)) && NET_JOIN_Y[T_XTER(c, y - 1, x)])
        mask += 8; /* $200A6 */
    if (y < c->h - 1 && carries_pipe(T_XUND(c, y + 1, x)) && NET_JOIN_Y[T_XTER(c, y + 1, x)])
        mask += 2; /* $20120 */
    if (x > 0 && carries_pipe(T_XUND(c, y, x - 1)) && NET_JOIN_X[T_XTER(c, y, x - 1)])
        mask += 1; /* $2019C */
    if (x < c->w - 1 && carries_pipe(T_XUND(c, y, x + 1)) && NET_JOIN_X[T_XTER(c, y, x + 1)])
        mask += 4; /* $2021C */
    if (mask == 0)
        mask = 15;                                    /* $20222 */
    set_under(c, y, x, PIPE_FIRST + NET_PIECE[mask]); /* $20236 */
}
