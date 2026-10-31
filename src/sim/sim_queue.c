/*  sim_queue.c: the ring the original walks the map with, 512 entries on
 *  the original's own map.  Three quite different passes share it.
 *
 *      The power and water floods.  The bulldozer's run along a line.
 *      The trip walk that decides whether a zone can grow.
 *
 *  It is one ring, not three.  This is because the original had one.
 *  See the note below on why its size is part of the reconstruction
 *  rather than a limit to raise. */
#include "sim.h"
#include "sim_int.h"

/*  The BFS queue: and it is not the unbounded queue you would write.
 *  $13B2 is NewPtr(0x800): 2048 bytes, or 512 four-byte Points.  Both
 *  the push at $21DF2 and the pop at $21E3A mask their index with 0x1FF.
 *  So the ring holds 512 entries, and $21E26 handles a full ring by
 *  moving the tail forward: silently discarding the oldest entry.  On
 *  any network larger than a few hundred tiles this overflows
 *  constantly, so the traversal is a lossy breadth-first walk rather
 *  than a complete one.  That is not an artefact to tidy away: both
 *  floods hand out capacity in queue order, so the dropped entries
 *  change which tiles end up powered and watered.  Giving this queue a
 *  comfortable size makes the reconstruction wrong.  So a city on the
 *  original's own map keeps 512 entries.  A city of any other extent
 *  has a ring as large as its window (city_extent), and drops nothing,
 *  because the reconstruction has no original to match there. */

void q_reset(City *c) { c->qw = c->qr = 0; } /* queueReset $21DD4 */

void q_push(City *c, int y, int x) /* queuePush $21DF2 */
{
    c->q[c->qw].y = (int16_t)y;
    c->q[c->qw].x = (int16_t)x;
    c->qw         = (c->qw + 1) & c->qmask;
    if (c->qw == c->qr)
        c->qr = (c->qw + 1) & c->qmask; /* $21E26 */
}

void q_pop(City *c, int *y, int *x) /* queuePop $21E3A */
{
    *y    = c->q[c->qr].y;
    *x    = c->q[c->qr].x;
    c->qr = (c->qr + 1) & c->qmask;
}

int q_empty(const City *c) { return c->qw == c->qr; }

/*  The ring at $13B2 is a queue for the flood fills, and a stack here.
 *  So it needs both ends.  $21E66 pops the newest entry, $21E96 reads it
 *  without removing it. */
void q_pop_back(City *c, int *y, int *x) /* queuePopBack $21E66 */
{
    c->qw = (c->qw + c->qmask) & c->qmask;
    *y    = c->q[c->qw].y;
    *x    = c->q[c->qw].x;
}

void q_peek_back(const City *c, int *y, int *x) /* queuePeekBack $21E96 */
{
    int i = (c->qw + c->qmask) & c->qmask;
    *y    = c->q[i].y;
    *x    = c->q[i].x;
}
