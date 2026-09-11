/*  cut.c: THE CUT QUEUE.
 *
 *  A pass that wants a path cut into pieces does not cut it.  It QUEUES
 *  the chain, and reads the pieces back once the drive has been round
 *  and arc.rules.pieces has answered.  That is what lets one rule cut
 *  every path in the world, a segment's, a connector's, a spur's leg, a
 *  thread junction's thread.  What keeps a pass from reaching up into
 *  Lua in the middle of its own work.
 *
 *  Nothing here decides how a chain becomes pieces.  It holds the
 *  chains, hands each to the drive, and keeps what came back. */
#include <math.h>
#include <string.h>

#include "mesh/internal.h"
#include "pipeline.h"

#include "net/net.h"
/*  How many chains one drive may queue, and the arenas they and their
 *  pieces are cut into. */
#define CUT_MAX    8192
#define CUT_PTS    131072
#define CUT_PIECES 131072

static struct
{
    int first_q, n;    /* the chain, in the point arena */
    int first_p, np;   /* the pieces, in the piece arena */
    int cut, over;
} s_cut[CUT_MAX];
static V2       s_cut_q[CUT_PTS];
static float    s_cut_rad[CUT_PTS], s_cut_tlim[CUT_PTS];
static Piece    s_cut_pc[CUT_PIECES];
static int      s_n_cut, s_n_cut_q, s_n_cut_pc;
static PieceFan s_cut_fan;
static int      s_cut_full;

void net_cut_reset(void)
{
    s_n_cut = s_n_cut_q = s_n_cut_pc = 0;
    s_cut_full = 0;
}

/*  One chain queued.  Answers where to read its pieces back from.  -1
 *  where there is no room: which the build reports rather than quietly
 *  drawing a path short. */
int net_cut_add(const V2 *q, int n, const float *rad, const float *tlim)
{
    int k, i;
    /*  A path of fewer than two points is nothing to cut.  Is not the
     *  queue running out: a fit that found no line answers one. */
    if (n < 2)
        return -1;
    if (s_n_cut >= CUT_MAX || s_n_cut_q + n > CUT_PTS)
    {
        s_cut_full = 1;
        return -1;
    }
    k                 = s_n_cut++;
    s_cut[k].first_q  = s_n_cut_q;
    s_cut[k].n        = n;
    s_cut[k].first_p  = 0;
    s_cut[k].np       = 0;
    s_cut[k].cut      = 0;
    s_cut[k].over     = 0;
    for (i = 0; i < n; ++i)
    {
        s_cut_q[s_n_cut_q]      = q[i];
        s_cut_rad[s_n_cut_q]    = rad ? rad[i] : 0.0f;
        s_cut_tlim[s_n_cut_q++] = tlim ? tlim[i] : 0.0f;
    }
    return k;
}

int net_cuts(void)
{
    return s_n_cut;
}

int net_cut_full(void)
{
    return s_cut_full;
}

/*  Chain i, set up as the handle the drive hands the rule. */
void *net_cut_at(int i)
{
    if (i < 0 || i >= s_n_cut || s_n_cut_pc + 2 * s_cut[i].n + 2 > CUT_PIECES)
    {
        if (i >= 0 && i < s_n_cut)
            s_cut_full = 1;
        return NULL;
    }
    memset(&s_cut_fan, 0, sizeof s_cut_fan);
    s_cut_fan.q    = &s_cut_q[s_cut[i].first_q];
    s_cut_fan.rad  = &s_cut_rad[s_cut[i].first_q];
    s_cut_fan.tlim = &s_cut_tlim[s_cut[i].first_q];
    s_cut_fan.n    = s_cut[i].n;
    s_cut_fan.out  = &s_cut_pc[s_n_cut_pc];
    s_cut_fan.cur  = s_cut_q[s_cut[i].first_q];
    s_cut[i].first_p = s_n_cut_pc;
    return &s_cut_fan;
}

/*  And the answer taken. */
void net_cut_done(int i)
{
    if (i < 0 || i >= s_n_cut)
        return;
    s_cut[i].np   = s_cut_fan.over ? 0 : s_cut_fan.np;
    s_cut[i].over = s_cut_fan.over;
    s_cut[i].cut  = 1;
    s_n_cut_pc += s_cut[i].np;
}

/*  The pieces of chain i, or 0 where the drive cut none: no rule, or a
 *  path the cut refused. */
int net_cut_pieces(int i, Piece *out, int cap, int *count)
{
    int k;
    if (i < 0 || i >= s_n_cut || !s_cut[i].cut || s_cut[i].over || s_cut[i].np < 1)
        return -1;
    *count = s_cut[i].np < cap ? s_cut[i].np : cap;
    for (k = 0; k < *count; ++k)
        out[k] = s_cut_pc[s_cut[i].first_p + k];
    return 0;
}

