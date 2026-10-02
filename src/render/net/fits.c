/*  net/fits.c: see net/fits.h. */
#include <string.h>

#include "dump.h"
#include "log.h"
#include "mesh/fit.h"
#include "net/fits.h"
#include "net/internal.h"
#include "net/net.h"
#include "opt.h"

#define FITS_MAX  16384
#define FIT_TILES 262144
#define FIT_ARENA 262144

typedef enum
{
    FIT_ASK_TILES,
    FIT_ASK_POINTS
} FitAskKind;

typedef struct
{
    FitAskKind kind;
    int32_t    col, row;
    int        e, bucket, cand, free_reach; /* cand: 0 the only one, 1 held, 2 free */
    float      hw, rmax, rmin, gro, reserve;
    V2         start, goal;
    int32_t    ex0, ex1;
    Family     f;
    const char *name; /* what the choice names: the family's, or the band's */
    /*  a corridor of tiles, in the arena's tile arrays */
    int at, nt;
    /*  a chain of points, with the band's own corridor */
    const RCity   *c;
    const int32_t *own;
    int            n_own;
    const uint8_t *own_mask;
    const V2      *chain;
    int            nc;
} FitAsk;

struct FitArena
{
    FitAsk  ask[FITS_MAX];
    int     n;
    int32_t tc[FIT_TILES], tr[FIT_TILES];
    int     nt;
    /*  The path the fit in hand is writing, and the two ways kept aside
     *  while the choice waits. */
    V2    q[MAX_PTS];
    float rad[MAX_PTS], tlim[MAX_PTS];
    V2    held_q[MAX_PTS], free_q[MAX_PTS];
    float held_rad[MAX_PTS], held_tlim[MAX_PTS], free_rad[MAX_PTS], free_tlim[MAX_PTS];
    int   held_nk, free_nk;
    char  tally[2][512], before[512];
    int   held_sc[3], free_sc[3]; /* corners, tight, nodes */
    int   at_choice;
    int   kept_free; /* a points ask: which way was kept, -1 for none yet */
};

static FitArena s_lines, s_band;

FitArena *fits_lines(void)
{
    return &s_lines;
}

FitArena *fits_band(void)
{
    return &s_band;
}

/*  ---- the lines' kept store ------------------------------------------
 *
 *  Every kept path, filed by the cell and edge its segment starts at,
 *  and queued to be cut.  Cutting it into pieces is arc.rules.pieces's:
 *  the drive comes round when every path is fitted and the segment
 *  reads the answer back. */
static struct
{
    int32_t col, row;
    int     e, fx, at, nk, cut;
} s_kept[FITS_MAX];
static int   s_n_kept;
static V2    s_kept_q[FIT_ARENA];
static float s_kept_rad[FIT_ARENA], s_kept_tlim[FIT_ARENA];
static int   s_kept_n;
static int   s_kept_at[2][R_MAP * R_MAP * 4];

static void fit_keep(Family f, int32_t col, int32_t row, int e, const V2 *q, const float *rad, const float *tlim, int nk)
{
    int k, fx = FAMX(f);
    if (s_n_kept >= FITS_MAX || s_kept_n + nk > FIT_ARENA)
    {
        R_ERR("net", "no room for the fit at %d,%d: %d paths of %d points is the most held", (int)col, (int)row, FITS_MAX, FIT_ARENA);
        return;
    }
    k                = s_n_kept++;
    s_kept[k].col    = col;
    s_kept[k].row    = row;
    s_kept[k].e      = e;
    s_kept[k].fx     = fx;
    s_kept[k].at     = s_kept_n;
    s_kept[k].nk     = nk;
    s_kept[k].cut    = net_cut_add(q, nk, rad, tlim);
    s_kept_at[fx][(row * R_MAP + col) * 4 + e] = k;
    for (int i = 0; i < nk; ++i)
    {
        s_kept_q[s_kept_n + i]    = q[i];
        s_kept_rad[s_kept_n + i]  = rad[i];
        s_kept_tlim[s_kept_n + i] = tlim[i];
    }
    s_kept_n += nk;
}

int net_seg_pieces_of(Family f, int32_t col, int32_t row, int e, Piece *out, int cap, int *count)
{
    int k = s_kept_at[FAMX(f)][(row * R_MAP + col) * 4 + e];
    if (k < 0 || k >= s_n_kept)
        return -1;
    return net_cut_pieces(s_kept[k].cut, out, cap, count);
}

int net_seg_fit_of(Family f, int32_t col, int32_t row, int e, V2 *q, float *rad, float *tlim, int cap)
{
    int i, k = s_kept_at[FAMX(f)][(row * R_MAP + col) * 4 + e], nk;
    if (k < 0 || k >= s_n_kept)
        return 0;
    nk = s_kept[k].nk < cap ? s_kept[k].nk : cap;
    for (i = 0; i < nk; ++i)
    {
        q[i]    = s_kept_q[s_kept[k].at + i];
        rad[i]  = s_kept_rad[s_kept[k].at + i];
        tlim[i] = s_kept_tlim[s_kept[k].at + i];
    }
    return nk;
}

/*  ---- the arena -------------------------------------------------------- */

void fits_reset(FitArena *a)
{
    a->n = a->nt = 0;
    a->held_nk = a->free_nk = 0;
    a->at_choice = -1;
    a->kept_free = -1;
    if (a == &s_lines)
    {
        s_n_kept = s_kept_n = 0;
        memset(s_kept_at, -1, sizeof s_kept_at);
    }
}

int fits_add_tiles(FitArena *a, const NetFamily *fam, Family f, int32_t col, int32_t row, int e, const Seg *x, int cand, int free_reach)
{
    FitAsk *k;
    int     i;
    if (a->n >= FITS_MAX || a->nt + x->nt > FIT_TILES)
    {
        R_ERR("net", "no room for the fit at %d,%d: %d fits of %d tiles is the most read", (int)col, (int)row, FITS_MAX, FIT_TILES);
        return -1;
    }
    k             = &a->ask[a->n++];
    memset(k, 0, sizeof *k);
    k->kind       = FIT_ASK_TILES;
    k->col        = col;
    k->row        = row;
    k->e          = e;
    k->f          = f;
    k->bucket     = fam->fit_fam;
    k->name       = fam->name;
    k->cand       = cand;
    k->free_reach = free_reach;
    k->at         = a->nt;
    k->nt         = x->nt;
    k->hw         = x->hw;
    k->rmax       = *fam->rmax;
    k->rmin       = *fam->rmin;
    k->gro        = x->hw / (fam->ref_width * 0.5f);
    k->reserve    = fam->turnout > 0.0f ? fam->turnout - x->hw + 0.05f : 0.0f;
    k->start      = x->pts[0];
    k->goal       = x->pts[x->n - 1];
    k->ex0        = x->kind0 == 2 ? (int32_t)(row * R_MAP + col) : -1;
    k->ex1        = x->kind1 == 2 ? (int32_t)(x->cr * R_MAP + x->cc) : -1;
    for (i = 0; i < x->nt && i < MAX_PTS; ++i)
        a->tc[a->nt + i] = x->tcol[i], a->tr[a->nt + i] = x->trow[i];
    a->nt += x->nt;
    return 0;
}

int fits_add_points(FitArena *a, const RCity *c, const int32_t *own, int n_own, const uint8_t *own_mask, const V2 *chain, int nc, V2 b0, V2 b1, int cand, int bucket, const char *name)
{
    FitAsk *k;
    if (a->n >= FITS_MAX)
    {
        R_ERR("net", "no room for the fit: %d fits is the most read", FITS_MAX);
        return -1;
    }
    k           = &a->ask[a->n++];
    memset(k, 0, sizeof *k);
    k->kind     = FIT_ASK_POINTS;
    k->cand     = cand;
    k->bucket   = bucket;
    k->name     = name;
    k->c        = c;
    k->own      = own;
    k->n_own    = n_own;
    k->own_mask = own_mask;
    k->chain    = chain;
    k->nc       = nc;
    k->start    = b0;
    k->goal     = b1;
    k->hw       = TUNE(band_w);
    k->rmax     = TUNE(band_rmax);
    k->rmin     = TUNE(band_rmin);
    k->ex0 = k->ex1 = -1;
    /*  Both ways count from the tally as it stands before either. */
    if (cand == 1)
        fit_tally_get(bucket, a->before, sizeof a->before);
    return 0;
}

int fits_count(const FitArena *a)
{
    return a->n;
}

int fits_begin(FitArena *a, const RCity *c, int i)
{
    const FitAsk *k;
    if (i < 0 || i >= a->n)
        return 0;
    k = &a->ask[i];
    if (k->kind == FIT_ASK_TILES)
    {
        fit_tally_into(k->bucket);
        if (k->cand == 1)
            fit_tally_get(k->bucket, a->before, sizeof a->before);
        if (k->cand)
            fit_tally_set(k->bucket, a->before, sizeof a->before);
        return path_fit_begin(c, a->tc + k->at, a->tr + k->at, k->nt, k->hw, k->start, k->goal, k->rmax, k->rmin, k->gro, k->reserve, k->ex0, k->ex1, k->cand == 2 ? k->free_reach : 0, a->q, a->rad, a->tlim, MAX_PTS);
    }
    fit_tally_set(k->bucket, a->before, sizeof a->before);
    return path_fit_points_begin(band_corridor(k->c, k->own, k->n_own, k->cand - 1), k->own_mask, k->chain, k->nc, k->hw, k->start, k->goal, k->rmax, k->rmin, 1.0f, -1, -1, k->cand - 1, a->q, a->rad, a->tlim, MAX_PTS);
}

static void fit_score(const float *rad, int nk, float rmin, int *out)
{
    int k;
    out[0] = out[1] = 0;
    out[2] = nk;
    for (k = 1; k + 1 < nk; ++k)
        if (rad[k] < 0.01f)
            ++out[0];
        else if (rad[k] < rmin)
            ++out[1];
}

int fits_done(FitArena *a, int i)
{
    const FitAsk *k;
    int           nk, j;
    a->at_choice = -1;
    if (i < 0 || i >= a->n)
        return 0;
    k  = &a->ask[i];
    nk = k->kind == FIT_ASK_TILES ? path_fit_end() : path_fit_points_end();
    if (k->cand == 0)
    {
        fit_keep(k->f, k->col, k->row, k->e, a->q, a->rad, a->tlim, nk);
        return 0;
    }
    fit_tally_get(k->bucket, a->tally[k->cand - 1], sizeof a->tally[0]);
    if (k->cand == 1)
    {
        for (j = 0; j < nk; ++j)
            a->held_q[j] = a->q[j], a->held_rad[j] = a->rad[j], a->held_tlim[j] = a->tlim[j];
        a->held_nk = nk;
        fit_score(a->rad, nk, k->rmin, a->held_sc);
        return 0;
    }
    for (j = 0; j < nk; ++j)
        a->free_q[j] = a->q[j], a->free_rad[j] = a->rad[j], a->free_tlim[j] = a->tlim[j];
    a->free_nk = nk;
    fit_score(a->rad, nk, k->rmin, a->free_sc);
    a->at_choice = i;
    return 1;
}

const char *fits_choice(const FitArena *a, const int **free_, const int **held)
{
    if (a->at_choice < 0)
        return NULL;
    *free_ = a->free_sc;
    *held  = a->held_sc;
    return a->ask[a->at_choice].name;
}

void fits_choice_is(FitArena *a, int keep_free)
{
    const FitAsk *k;
    int           i = a->at_choice;
    if (i < 0)
        return;
    k            = &a->ask[i];
    a->at_choice = -1;
    if (g_dev.path_dump)
        dumpf("FIT %s %s kept: free %d corners %d tight %d nodes, own %d corners %d tight %d nodes\n", k->name, keep_free ? "free" : "own", a->free_sc[0], a->free_sc[1], a->free_sc[2], a->held_sc[0], a->held_sc[1], a->held_sc[2]);
    fit_tally_set(k->bucket, a->tally[keep_free ? 1 : 0], sizeof a->tally[0]);
    if (k->kind == FIT_ASK_POINTS)
    {
        a->kept_free = keep_free ? 1 : 0;
        return;
    }
    if (keep_free)
        fit_keep(k->f, k->col, k->row, k->e, a->free_q, a->free_rad, a->free_tlim, a->free_nk);
    else
        fit_keep(k->f, k->col, k->row, k->e, a->held_q, a->held_rad, a->held_tlim, a->held_nk);
}

int fits_take(const FitArena *a, V2 *q, float *rad, float *tlim)
{
    int j, nk;
    if (a->kept_free < 0)
        return 0;
    nk = a->kept_free ? a->free_nk : a->held_nk;
    for (j = 0; j < nk; ++j)
        if (a->kept_free)
            q[j] = a->free_q[j], rad[j] = a->free_rad[j], tlim[j] = a->free_tlim[j];
        else
            q[j] = a->held_q[j], rad[j] = a->held_rad[j], tlim[j] = a->held_tlim[j];
    return nk;
}

/*  ---- the lines' arena, as the script drives it ------------------------- */

int net_fits(void)
{
    return fits_count(&s_lines);
}

int net_fit_begin(const RCity *c, int i)
{
    return fits_begin(&s_lines, c, i);
}

int net_fit_done(int i)
{
    return fits_done(&s_lines, i);
}

const char *net_fit_choice(const int **free_, const int **held)
{
    return fits_choice(&s_lines, free_, held);
}

void net_fit_choice_is(int keep_free)
{
    fits_choice_is(&s_lines, keep_free);
}
