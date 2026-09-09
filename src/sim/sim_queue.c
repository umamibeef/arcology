/*  sim_queue.c -- the 512-entry ring the original walks the map with.
 *  Three quite different passes share it: the power and water floods, the
 *  bulldozer's run along a line, and the trip walk that decides whether a
 *  zone can grow.  It is one ring, not three, because the original had one
 *  -- see the note below on why its size is part of the reconstruction
 *  rather than a limit to raise. */
#include "sc2k.h"
#include "sim_int.h"

/*  The BFS queue -- and it is not the unbounded queue you would write.
 *  $13B2 is NewPtr(0x800): 2048 bytes, or 512 four-byte Points.  Both the
 *  push at $21DF2 and the pop at $21E3A mask their index with 0x1FF, so the
 *  ring holds 512 entries, and $21E26 handles a full ring by moving the
 *  tail forward -- silently discarding the oldest entry.  On any network
 *  larger than a few hundred tiles this overflows constantly, so the
 *  traversal is a lossy breadth-first walk rather than a complete one.
 *  That is not an artefact to tidy away: both floods hand out capacity in
 *  queue order, so the dropped entries change which tiles end up powered
 *  and watered.  Giving this queue a comfortable size makes the
 *  reconstruction wrong. */
#define QMASK 0x1FF
static struct
{
    int16_t y, x;
} q[QMASK + 1];
static int qw, qr; /* $13B6 write, $13B8 read */

void q_reset(void) { qw = qr = 0; } /* queueReset $21DD4 */

void q_push(int y, int x) /* queuePush $21DF2 */
{
    q[qw].y = (int16_t)y;
    q[qw].x = (int16_t)x;
    qw      = (qw + 1) & QMASK;
    if (qw == qr)
        qr = (qw + 1) & QMASK; /* $21E26 */
}

void q_pop(int *y, int *x) /* queuePop $21E3A */
{
    *y = q[qr].y;
    *x = q[qr].x;
    qr = (qr + 1) & QMASK;
}

int q_empty(void) { return qw == qr; }

/*  The ring at $13B2 is a queue for the flood fills and a stack here,
 *  so it needs both ends.  $21E66 pops the newest entry, $21E96 reads
 *  it without removing it. */
void q_pop_back(int *y, int *x) /* queuePopBack $21E66 */
{
    qw = (qw + QMASK) & QMASK;
    *y = q[qw].y;
    *x = q[qw].x;
}

void q_peek_back(int *y, int *x) /* queuePeekBack $21E96 */
{
    int i = (qw + QMASK) & QMASK;
    *y    = q[i].y;
    *x    = q[i].x;
}
