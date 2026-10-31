/*  turn.h: the view from another side.  A turn of the view is perspective.
 *  The renderer draws the same cells from another side, and the ids that
 *  encode a direction go through the original's tables for that side.
 *  Nothing of the city turns. */
#ifndef R_TURN_H
#define R_TURN_H

#include "city.h"

/*  `u` seen from quarter turn `q`, read off it unturned. */
void adapt_view_turned(RCity *v, const RCity *u, int q);
/*  The cell of the unturned `n` by `n` grid that the cell (x,y) of the
 *  view from quarter turn `q` reads. */
void adapt_turned_src(int32_t x, int32_t y, int q, int32_t n, int32_t *sx, int32_t *sy);
/*  The cell of that view where the cell (sx,sy) of the unturned grid
 *  shows: adapt_turned_src the other way. */
void adapt_turned_dst(int32_t sx, int32_t sy, int q, int32_t n, int32_t *x, int32_t *y);

#endif /* R_TURN_H */
