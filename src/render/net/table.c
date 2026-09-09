/*  The segment table: every segment the grading pass walked and fitted,
 *  replayed by the building pass, and the loft's stations kept per
 *  segment across passes and builds. */
#include <stdlib.h>
#include <string.h>

#include "dump.h"
#include "mesh/internal.h"
#include "net/internal.h"
#include "opt.h"

/*  One segment walk's working state, handed to the stages below so
 *  each can be read on its own: the tiles walked, the fit's points and
 *  pieces, the nodes at either end and what kind they are. */

/*  THE SEGMENT TABLE. Every segment the measuring walk fits is kept -- its
 *  pieces, the fit's nodes, its corridor tiles, the tiles it marked
 *  visited, its nodes and class -- so the drawing walk reads it back
 *  instead of walking and fitting the map a second time.  A segment the
 *  pools cannot hold is simply walked again, as before. */
#define SEG_MAX    16384
#define SEG_PIECES 262144
#define SEG_PTS    131072
RSeg           s_segs[SEG_MAX];
static Piece   s_segp[SEG_PIECES];
static V2      s_segq[SEG_PTS];
static float   s_segrad[SEG_PTS];
static float   s_segtl[SEG_PTS]; /* the fit's tangent budgets, beside the radii: a band's overlay prints them */
static int32_t s_segt[2][SEG_PTS];
static int32_t s_segm[SEG_PTS];
static int     s_nseg, s_nsegp, s_nsegq, s_nsegt, s_nsegm;
int32_t        s_seg_at[R_MAP * R_MAP * 4]; /* (tile, edge) -> the segment starting or ending there, or -1 */

/*  The loft's stations per kept segment, kept across the two passes and,
 *  for a segment no edit came near, across builds: sampling every segment
 *  was a third of a build (Atlanta: 9.8 ms in each pass of 83).  Two
 *  arenas, this build's and the last one's, grown on demand; a segment's
 *  samples are valid for the same trimmed pieces (hashed) when no tile near
 *  it changed (mesh_incr_near).  The last build's table survives as (start
 *  tile, edge) -> hash and range. */
typedef struct
{
    int32_t  sfirst, ns, np;
    uint64_t phash;
} PrevSeg;
static int      s_miss[4]; /* the cache's misses, by reason: no previous entry, its pieces differ, an edit came near, no entry to look up */
static Sample  *s_segsmp[2];
static uint32_t s_segsmp_cap[2];
static int      s_smp_cur, s_nsegsmp;
static PrevSeg  s_prev[SEG_MAX];
static int32_t  s_prev_at[R_MAP * R_MAP * 4];
static int      s_nprev;

/*  The trimmed pieces as a key: the fields that carry geometry, and for
 *  an arc its centre, radius and angles -- never a straight's unset arc
 *  fields, nor an arc's unset a and b, which hold whatever the fit's
 *  buffers held before and made every arc hash differently from one
 *  build to the next (211 of 1,322 segments missed the cache after an
 *  edit for that alone). */
static void hash_bytes(uint64_t *h, const void *p, size_t n)
{
    const uint8_t *b = (const uint8_t *)p;
    size_t         i;
    for (i = 0; i < n; ++i)
        *h = (*h ^ b[i]) * 1099511628211ull;
}

uint64_t pieces_hash(const Piece *pc, int np, float total)
{
    uint64_t h = 1469598103934665603ull;
    int      k;
    for (k = 0; k < np; ++k)
    {
        const Piece *p = &pc[k];
        hash_bytes(&h, &p->arc, sizeof p->arc);
        hash_bytes(&h, &p->len, sizeof p->len);
        if (p->arc)
        {
            /* an arc is its centre, radius and angles; the fillet leaves its a and b unset */
            hash_bytes(&h, &p->c, sizeof p->c);
            hash_bytes(&h, &p->r, sizeof p->r);
            hash_bytes(&h, &p->t0, sizeof p->t0);
            hash_bytes(&h, &p->t1, sizeof p->t1);
        }
        else
        {
            hash_bytes(&h, &p->a, sizeof p->a);
            hash_bytes(&h, &p->b, sizeof p->b);
        }
    }
    hash_bytes(&h, &total, sizeof total);
    hash_bytes(&h, &np, sizeof np);
    return h;
}

void net_table_free(void)
{
    int k;
    for (k = 0; k < 2; ++k)
    {
        free(s_segsmp[k]);
        s_segsmp[k]     = NULL;
        s_segsmp_cap[k] = 0;
    }
}

/*  The samples this build's table already has for the segment, or the
 *  last build's if the same trimmed pieces and nothing near it changed. */
int loft_cached(Loft *x)
{
    RSeg *r;
    int   p, k;
    int   seg = x->d->cache - 1;
    if (seg < 0 || seg >= s_nseg)
    {
        ++s_miss[3];
        return -1;
    }
    r = &s_segs[seg];
    k = s_smp_cur;
    if (r->ns > 0 && r->phash == x->d->hash)
    {
        memcpy(x->smp, &s_segsmp[k][r->sfirst], (size_t)r->ns * sizeof *x->smp);
        x->ns = r->ns;
        return 0;
    }
    if (x->d->hot)
    {
        ++s_miss[2];
        return -1;
    }
    p = s_prev_at[(r->row * R_MAP + r->col) * 4 + r->e];
    if (p < 0 || s_prev[p].ns <= 0)
    {
        ++s_miss[0];
        return -1;
    }
    if (s_prev[p].phash != x->d->hash || s_prev[p].np != r->np)
    {
        ++s_miss[1];
        return -1;
    }
    k = s_smp_cur ^ 1;
    memcpy(x->smp, &s_segsmp[k][s_prev[p].sfirst], (size_t)s_prev[p].ns * sizeof *x->smp);
    x->ns = s_prev[p].ns;
    return 0;
}

/*  Keep the loft's stations in this build's arena, for the second pass
 *  and the next build. */
void loft_keep(const Loft *x)
{
    RSeg    *r;
    int      k = s_smp_cur;
    uint32_t need;
    int      seg = x->d->cache - 1;
    if (seg < 0 || seg >= s_nseg || x->ns <= 0)
        return;
    need = (uint32_t)s_nsegsmp + (uint32_t)x->ns;
    if (need > s_segsmp_cap[k])
    {
        uint32_t cap = s_segsmp_cap[k] ? s_segsmp_cap[k] : 65536;
        Sample  *ns;
        while (cap < need)
            cap *= 2;
        ns = (Sample *)realloc(s_segsmp[k], (size_t)cap * sizeof *ns);
        if (!ns)
            return; /* no room: sampled again next time, nothing lost */
        s_segsmp[k]     = ns;
        s_segsmp_cap[k] = cap;
    }
    memcpy(&s_segsmp[k][s_nsegsmp], x->smp, (size_t)x->ns * sizeof *x->smp);
    r         = &s_segs[seg];
    r->sfirst = s_nsegsmp;
    r->ns     = x->ns;
    r->phash  = x->d->hash;
    s_nsegsmp += x->ns;
}

/*  The station cache's misses this pass, by reason, on the timing line. */
void seg_table_misses_print(void)
{
    if (g_dev.times && (s_miss[0] | s_miss[1] | s_miss[2] | s_miss[3]))
        dumpf("time    station cache misses: %d with no previous entry, %d whose pieces differ, %d near the edit, %d with no entry\n", s_miss[0], s_miss[1], s_miss[2], s_miss[3]);
    memset(s_miss, 0, sizeof s_miss);
}

void seg_table_reset(void)
{
    int i;
    memset(s_prev_at, -1, sizeof s_prev_at);
    s_nprev = 0;
    for (i = 0; i < s_nseg; ++i)
        if (s_segs[i].ns > 0)
        {
            s_prev[s_nprev].sfirst                                               = s_segs[i].sfirst;
            s_prev[s_nprev].ns                                                   = s_segs[i].ns;
            s_prev[s_nprev].np                                                   = s_segs[i].np;
            s_prev[s_nprev].phash                                                = s_segs[i].phash;
            s_prev_at[(s_segs[i].row * R_MAP + s_segs[i].col) * 4 + s_segs[i].e] = s_nprev++;
        }
    s_smp_cur ^= 1;
    s_nsegsmp = 0;
    s_nseg = s_nsegp = s_nsegq = s_nsegt = s_nsegm = 0;
    memset(s_seg_at, 0xff, sizeof s_seg_at);
}

int seg_table_count(void)
{
    return s_nseg;
}

/*  A walk's working buffers: one segment at a time, whichever walk. */

/*  Keep a fitted segment.  Returns 0 when it did not fit the pools. */
int seg_store(const Seg *x)
{
    RSeg *r;
    int   k;
    if (s_nseg >= SEG_MAX || s_nsegp + x->np > SEG_PIECES || s_nsegq + x->nk > SEG_PTS || s_nsegt + x->nt > SEG_PTS ||
        s_nsegm + x->nm > SEG_PTS)
        return 0;
    r          = &s_segs[s_nseg];
    r->col     = x->col;
    r->row     = x->row;
    r->cc      = x->cc;
    r->cr      = x->cr;
    r->e       = (int8_t)x->e;
    r->sfirst  = -1;
    r->ns      = 0;
    r->phash   = 0;
    r->band    = 0;
    r->back    = (int8_t)x->back;
    r->kind0   = (int8_t)x->kind0;
    r->kind1   = (int8_t)x->kind1;
    r->square0 = (int8_t)x->square0;
    r->square1 = (int8_t)x->square1;
    r->f       = x->f;
    r->cls     = x->cls;
    r->hw      = x->hw;
    r->total   = x->total;
    r->first   = s_nsegp;
    r->np      = x->np;
    for (k = 0; k < x->np; ++k)
        s_segp[s_nsegp++] = x->pieces[k];
    r->qfirst = s_nsegq;
    r->nk     = x->nk;
    for (k = 0; k < x->nk; ++k)
    {
        s_segq[s_nsegq]    = x->q[k];
        s_segrad[s_nsegq]  = x->rad[k];
        s_segtl[s_nsegq++] = x->tlim[k];
    }
    r->tfirst = s_nsegt;
    r->nt     = x->nt;
    for (k = 0; k < x->nt; ++k)
    {
        s_segt[0][s_nsegt]   = x->tcol[k];
        s_segt[1][s_nsegt++] = x->trow[k];
    }
    r->mfirst = s_nsegm;
    r->nm     = x->nm;
    for (k = 0; k < x->nm; ++k)
        s_segm[s_nsegm++] = x->marks[k];
    s_seg_at[(x->row * R_MAP + x->col) * 4 + x->e] = s_nseg;
    /*  The far end too, so a walk starting there finds it -- but only on
     *  the map: a road running off the edge leaves the walk's far node
     *  one tile outside, and that index aliases another tile's edge
     *  (Atlanta 0,35: it took the slot of the segment leaving south). */
    if (x->cc >= 0 && x->cr >= 0 && x->cc < R_MAP && x->cr < R_MAP)
        s_seg_at[(x->cr * R_MAP + x->cc) * 4 + x->back] = s_nseg;
    ++s_nseg;
    return 1;
}

/*  Read a kept segment back into a walk's context: the pieces into the
 *  walk's own buffers (the trims cut them), the tiles it marked as
 *  visited marked again, its class restored. */
void seg_load(Seg *x, const RSeg *r)
{
    int k;
    x->cc      = r->cc;
    x->cr      = r->cr;
    x->back    = r->back;
    x->kind0   = r->kind0;
    x->kind1   = r->kind1;
    x->square0 = r->square0;
    x->square1 = r->square1;
    x->hw      = r->hw;
    x->total   = r->total;
    x->n       = 2;
    x->np      = r->np;
    for (k = 0; k < r->np; ++k)
        x->pieces[k] = s_segp[r->first + k];
    x->nk = r->nk;
    for (k = 0; k < r->nk; ++k)
    {
        x->q[k]   = s_segq[r->qfirst + k];
        x->rad[k] = s_segrad[r->qfirst + k];
    }
    x->nt = r->nt;
    for (k = 0; k < r->nt; ++k)
    {
        x->tcol[k] = s_segt[0][r->tfirst + k];
        x->trow[k] = s_segt[1][r->tfirst + k];
    }
    for (k = 0; k < r->nm; ++k)
        x->visited[s_segm[r->mfirst + k]] = 1;
    x->cls = r->cls;
}

/*  A kept segment's ends and corridor tiles, for the incremental
 *  rebuild's closure (mesh/incr.c). */
int seg_table_get(int i, int32_t *col, int32_t *row, int32_t *cc, int32_t *cr, const int32_t **tcol, const int32_t **trow, int *nt)
{
    const RSeg *r;
    if (i < 0 || i >= s_nseg)
        return -1;
    r     = &s_segs[i];
    *col  = r->col;
    *row  = r->row;
    *cc   = r->cc;
    *cr   = r->cr;
    *tcol = &s_segt[0][r->tfirst];
    *trow = &s_segt[1][r->tfirst];
    *nt   = r->nt;
    return 0;
}

/*  The fitted pieces of the segment at (col, row, e), taken from that
 *  end: as stored when it starts there, reversed when it ends there.  A
 *  turnout cuts its branch along this path and puts the port on it
 *  (lane.c port_pose), so a branch that bends within the reach is met
 *  by a curve and not by a hook. */
int seg_table_pieces_from(int32_t col, int32_t row, int e, Piece *out, int cap, int *np)
{
    int32_t     i = s_seg_at[(row * R_MAP + col) * 4 + e];
    const RSeg *r;
    int         k;
    if (i < 0 || i >= s_nseg)
        return -1;
    r = &s_segs[i];
    if (r->np > cap || r->np < 1)
        return -1;
    if (r->col == col && r->row == row && r->e == e)
        for (k = 0; k < r->np; ++k)
            out[k] = s_segp[r->first + k];
    else
        for (k = 0; k < r->np; ++k)
        {
            Piece q = s_segp[r->first + r->np - 1 - k];
            V2    t = q.a;
            q.a     = q.b;
            q.b     = t;
            if (q.arc)
            {
                float u = q.t0;
                q.t0    = q.t1;
                q.t1    = u;
            }
            out[k] = q;
        }
    *np = r->np;
    return 0;
}

int seg_table_nodes(int i, const V2 **q, const float **rad, int *nk)
{
    const RSeg *r;
    if (i < 0 || i >= s_nseg)
        return -1;
    r    = &s_segs[i];
    *q   = &s_segq[r->qfirst];
    *rad = &s_segrad[r->qfirst];
    *nk  = r->nk;
    return 0;
}

/*  The building pass's measure, from the table: every kept segment's
 *  arms and crossings again, without walking or fitting anything.  The
 *  grading pass fitted them; the pieces are the same in both. */
int seg_table_replay(RMesh *m, const RCity *c, const RAtlasLevel *l, uint8_t mask_bit, int comp, uint8_t *visited)
{
    int i;
    for (i = 0; i < s_nseg; ++i)
    {
        const RSeg *r = &s_segs[i];
        Seg         x;
        if (r->band)
            continue; /* a highway band: no arms, no crossings; hiway.c replays it */
        memset(&x, 0, sizeof x);
        x.m = m, x.c = c, x.l = l, x.mask_bit = mask_bit, x.comp = comp, x.f = r->f, x.col = r->col, x.row = r->row, x.e = r->e;
        x.visited = visited;
        x.pts = s_wk_pts, x.q = s_wk_q, x.rad = s_wk_rad, x.tlim = s_wk_tlim, x.tcol = s_wk_tcol, x.trow = s_wk_trow;
        x.pieces = s_wk_pieces, x.marks = s_wk_marks;
        x.ee = r->e;
        seg_load(&x, r);
        seg_measure_arms(&x);
        if (seg_measure_crossings(&x) != 0)
            return -1;
    }
    return 0;
}

/* ---- highway bands in the table ------------------------------------------ */

int seg_store_band(int32_t col, int32_t row, int ew, int sign, const Piece *pc, int np, const V2 *q, const float *rad, const float *tlim, int nk, const int32_t *own, int n_own)
{
    RSeg *r;
    int   k;
    float total = 0.0f;
    if (s_nseg >= SEG_MAX || s_nsegp + np > SEG_PIECES || s_nsegq + nk > SEG_PTS || s_nsegt + n_own > SEG_PTS)
        return -1;
    for (k = 0; k < np; ++k)
        total += pc[k].len;
    r = &s_segs[s_nseg];
    memset(r, 0, sizeof *r);
    r->col    = col;
    r->row    = row;
    r->cc     = col;
    r->cr     = row;
    r->e      = (int8_t)(ew + (sign < 0 ? 2 : 0)); /* the start cell and the way the walk ran: the band's key across builds */
    r->back   = (int8_t)sign;
    r->f      = F_ROAD;
    r->cls    = -1.0f;
    r->hw     = 1.0f;
    r->total  = total;
    r->sfirst = -1;
    r->band   = 1;
    r->first  = s_nsegp;
    r->np     = np;
    for (k = 0; k < np; ++k)
        s_segp[s_nsegp++] = pc[k];
    r->qfirst = s_nsegq;
    r->nk     = nk;
    for (k = 0; k < nk; ++k)
    {
        s_segq[s_nsegq]    = q[k];
        s_segrad[s_nsegq]  = rad[k];
        s_segtl[s_nsegq++] = tlim[k];
    }
    r->tfirst = s_nsegt;
    r->nt     = n_own;
    for (k = 0; k < n_own; ++k)
    {
        s_segt[0][s_nsegt]   = own[k] % R_MAP;
        s_segt[1][s_nsegt++] = own[k] / R_MAP;
    }
    r->mfirst = s_nsegm;
    r->nm     = 0;
    return s_nseg++;
}

const RSeg *seg_table_entry(int i)
{
    return i >= 0 && i < s_nseg ? &s_segs[i] : NULL;
}

void seg_table_arenas(const RSeg *r, const Piece **pc, const V2 **q, const float **rad, const float **tlim, const int32_t **tcol, const int32_t **trow)
{
    *pc   = &s_segp[r->first];
    *q    = &s_segq[r->qfirst];
    *rad  = &s_segrad[r->qfirst];
    *tlim = &s_segtl[r->qfirst];
    *tcol = &s_segt[0][r->tfirst];
    *trow = &s_segt[1][r->tfirst];
}

/*  Did this entry's pieces come out as the previous build's did?  The
 *  incremental rebuild's closure asks it of a band: an unchanged fit
 *  changes geometry only where the ground under it or a lane drop did. */
int seg_table_unchanged(int i)
{
    const RSeg *r;
    int         p;
    if (i < 0 || i >= s_nseg)
        return 0;
    r = &s_segs[i];
    p = s_prev_at[(r->row * R_MAP + r->col) * 4 + r->e];
    return p >= 0 && s_prev[p].ns > 0 && r->ns > 0 && s_prev[p].phash == r->phash && s_prev[p].np == r->np;
}

/*  The table index of the k-th band, in the order the walk stored them
 *  -- the order hiway.c's band table keeps too. */
int seg_table_band_index(int k)
{
    int i;
    for (i = 0; i < s_nseg; ++i)
        if (s_segs[i].band && k-- == 0)
            return i;
    return -1;
}
