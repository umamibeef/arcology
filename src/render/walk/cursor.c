/*  cursor.c: THE WALK AS THE DRIVE STEPS IT.
 *
 *  Every cursor scripts/compose/world.lua turns is here.  The junctions,
 *  in the order the walk visits them.  The fits waiting to be settled.
 *  The classes a segment may be given.  The boxes and the segments the
 *  drawing pass hands over, one at a time.
 *
 *  Walking a segment is walk.c's own business.  This is the ORDER the
 *  build asks for them in.  Nothing here decides what any of them
 *  becomes: a cursor opens a thing, the script is handed it, and the
 *  answer is taken back. */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "dump.h"
#include "incr.h"
#include "log.h"
#include "mesh/internal.h"
#include "mesh/model.h"
#include "net/net.h"
#include "opt.h"
#include "pipeline.h"
#include "script.h"
#include "walk/internal.h"

/*  Every junction on the map, in the order the walk visits them, so the
 *  script can be asked for each one's ring. */
int build_junction_count(const RCity *c, const RAtlasLevel *l)
{
    int fk, n = 0;
    (void)c;
    (void)l;
    for (fk = 0; fk < net_n_walked; ++fk)
        n += net_disc_junctions(fk);
    return n;
}

int build_junction_nth(const RCity *c, const RAtlasLevel *l, int i, Family *f, int32_t *ocol, int32_t *orow, int *olinks)
{
    int fk, k, n = 0;
    for (fk = 0; fk < net_n_walked; ++fk)
    {
        Family fam = net_walked[fk]->f;
        k          = net_disc_junctions(fk);
        if (i - n < k)
        {
            int32_t cell = net_disc_junction(fk, i - n);
            *f           = fam;
            *ocol        = cell % R_MAP;
            *orow        = cell / R_MAP;
            *olinks      = eff_links(c, l, *ocol, *orow, fam);
            return 1;
        }
        n += k;
    }
    return 0;
}
/*  ------------------------------------------------------------------
 *  The classes, read before anything is fitted
 *
 *  One class for a whole segment settles how wide it is, where its lanes
 *  run and what is painted on it.  So it has to be known before the fit.
 *  The walk here does nothing but step the tiles of every segment and
 *  count how many of them read as each class.  The drive settles each
 *  one and hands it back, and the walks that follow look the answer up.
 *
 *  It reaches the same segments in the same order as the pass that
 *  follows it, because seg_walk reads the map and its own visited marks
 *  and nothing either walk writes.
 *  ------------------------------------------------------------------ */
#define CLASSES_MAX 16384

static struct
{
    int32_t col, row;
    int     e, cnt[3];
    int     at, nt; /* the segment's own cells, in s_cls_cells */
} s_cls_ask[CLASSES_MAX];
/*  And the cells themselves.  A rule that decides what a line carries
 *  from the density and the neighborhood around it needs the tiles it
 *  runs over.  A single tile is not enough for itally of them. */
#define CLASS_CELLS 262144
static int32_t s_cls_cells[CLASS_CELLS];
static int     s_n_cls_cells;
static int   s_n_cls_ask;
static float s_seg_cls[R_MAP * R_MAP * 4];

int net_seg_class_of(int32_t col, int32_t row, int e)
{
    if (col < 0 || row < 0 || col >= R_MAP || row >= R_MAP || e < 0 || e > 3)
        return 0;
    return (int)s_seg_cls[(row * R_MAP + col) * 4 + e];
}

/*  ------------------------------------------------------------------
 *  And the fitted path of every segment, read at the same time
 *
 *  The fit is a pure function of the tiles a segment covers and the
 *  nodes at its ends.  Nothing it needs comes from anything the walk
 *  does afterwards: so it is worked out here, once, and the walk looks
 *  the answer up.  The points of every path lie end to end in one
 *  arena, since a path is a few points and a city is thousands of them.
 *  ------------------------------------------------------------------ */
#define FITS_MAX  16384
#define FIT_ARENA 262144

static struct
{
    int32_t col, row;
    int     e, fx, at, nk, cut; /* cut: where the drive's answer for this path is */
} s_fit_ask[FITS_MAX];
static int   s_n_fit_ask;
static V2    s_fits_q[FIT_ARENA];
static float s_fits_rad[FIT_ARENA], s_fits_tlim[FIT_ARENA];
static int   s_fit_n;
static int   s_fit_at[2][R_MAP * R_MAP * 4];

static void fit_keep(Family f, int32_t col, int32_t row, int e, const V2 *q, const float *rad, const float *tlim, int nk)
{
    int k, fx = FAMX(f);
    if (s_n_fit_ask >= FITS_MAX || s_fit_n + nk > FIT_ARENA)
    {
        R_ERR("net", "no room for the fit at %d,%d: %d paths of %d points is the most held",
              (int)col, (int)row, FITS_MAX, FIT_ARENA);
        return;
    }
    k                   = s_n_fit_ask++;
    s_fit_ask[k].col    = col;
    s_fit_ask[k].row    = row;
    s_fit_ask[k].e      = e;
    s_fit_ask[k].fx     = fx;
    s_fit_ask[k].at     = s_fit_n;
    s_fit_ask[k].nk     = nk;
    /*  And the path queued to be CUT.  Cutting it into pieces is
     *  arc.rules.pieces's.  So the drive comes round when every path is
     *  fitted and the segment reads the answer back. */
    s_fit_ask[k].cut    = net_cut_add(q, nk, rad, tlim);
    s_fit_at[fx][(row * R_MAP + col) * 4 + e] = k;
    for (int i = 0; i < nk; ++i)
    {
        s_fits_q[s_fit_n + i]    = q[i];
        s_fits_rad[s_fit_n + i]  = rad[i];
        s_fits_tlim[s_fit_n + i] = tlim[i];
    }
    s_fit_n += nk;
}

/*  The path the fit settled for this segment, into the walk's own
 *  arrays.  Answers 0 where none was kept, which is a segment with
 *  nothing to draw. */
/*  The pieces the drive cut for the segment at (col, row, e), or -1
 *  where it cut none. */
int net_seg_pieces_of(Family f, int32_t col, int32_t row, int e, Piece *out, int cap, int *count)
{
    int k = s_fit_at[FAMX(f)][(row * R_MAP + col) * 4 + e];
    if (k < 0 || k >= s_n_fit_ask)
        return -1;
    return net_cut_pieces(s_fit_ask[k].cut, out, cap, count);
}

int net_seg_fit_of(Family f, int32_t col, int32_t row, int e, V2 *q, float *rad, float *tlim, int cap)
{
    int i, k = s_fit_at[FAMX(f)][(row * R_MAP + col) * 4 + e], nk;
    if (k < 0 || k >= s_n_fit_ask)
        return 0;
    nk = s_fit_ask[k].nk < cap ? s_fit_ask[k].nk : cap;
    for (i = 0; i < nk; ++i)
    {
        q[i]    = s_fits_q[s_fit_ask[k].at + i];
        rad[i]  = s_fits_rad[s_fit_ask[k].at + i];
        tlim[i] = s_fits_tlim[s_fit_ask[k].at + i];
    }
    return nk;
}

/*  ------------------------------------------------------------------
 *  The fits, run by the drive
 *
 *  Every segment's path is a pure function of the tiles it covers and
 *  the nodes at its ends.  So the walk asks for none of them: the pass
 *  above reads what each fit needs, the drive runs each in turn.  The
 *  walk looks the answer up.
 *
 *  A family whose runs may leave its own cells: one sweeping
 *  across the field: is fitted TWICE, once held to its tiles and once
 *  free, and which of the two to keep is arc.rules.fit_choice's.
 *  ------------------------------------------------------------------ */
#define FIT_TILES 262144

static struct
{
    int32_t col, row;
    int     e, fit_fam, cand, free_reach; /* cand: 0 the only one, 1 held, 2 free */
    int     at, nt;
    float   hw, rmax, rmin, gro, reserve;
    V2      start, goal;
    int32_t ex0, ex1;
    Family  f;
} s_fitq[FITS_MAX];
static int     s_n_fitq;
static int32_t s_fitq_tc[FIT_TILES], s_fitq_tr[FIT_TILES];
static int     s_fitq_nt;

/*  The path the fit in hand is writing, and the held candidate kept
 *  aside while the free one is fitted. */
static V2    s_fit_q[MAX_PTS];
static float s_fit_rad[MAX_PTS], s_fit_tlim[MAX_PTS];
static V2    s_fit_held_q[MAX_PTS];
static float s_fit_held_rad[MAX_PTS], s_fit_held_tlim[MAX_PTS];
static V2    s_fit_free_q[MAX_PTS];
static float s_fit_free_rad[MAX_PTS], s_fit_free_tlim[MAX_PTS];
static int   s_fit_held_nk, s_fit_free_nk;
static char  s_fit_tally[2][512], s_fit_before[512];
static int   s_fit_held_sc[3], s_fit_free_sc[3]; /* corners, tight, nodes */
static int   s_fit_at_choice = -1;

static void fitq_add(const NetFamily *fam, Family f, int32_t col, int32_t row, int e,
                     const Seg *x, int cand, int free_reach)
{
    int k, i;
    if (s_n_fitq >= FITS_MAX || s_fitq_nt + x->nt > FIT_TILES)
    {
        R_ERR("net", "no room for the fit at %d,%d: %d fits of %d tiles is the most read",
              (int)col, (int)row, FITS_MAX, FIT_TILES);
        return;
    }
    k                    = s_n_fitq++;
    s_fitq[k].col        = col;
    s_fitq[k].row        = row;
    s_fitq[k].e          = e;
    s_fitq[k].f          = f;
    s_fitq[k].fit_fam    = fam->fit_fam;
    s_fitq[k].cand       = cand;
    s_fitq[k].free_reach = free_reach;
    s_fitq[k].at         = s_fitq_nt;
    s_fitq[k].nt         = x->nt;
    s_fitq[k].hw         = x->hw;
    s_fitq[k].rmax       = *fam->rmax;
    s_fitq[k].rmin       = *fam->rmin;
    s_fitq[k].gro        = x->hw / (fam->ref_width * 0.5f);
    s_fitq[k].reserve    = fam->turnout > 0.0f ? fam->turnout - x->hw + 0.05f : 0.0f;
    s_fitq[k].start      = x->pts[0];
    s_fitq[k].goal       = x->pts[x->n - 1];
    s_fitq[k].ex0        = x->kind0 == 2 ? (int32_t)(row * R_MAP + col) : -1;
    s_fitq[k].ex1        = x->kind1 == 2 ? (int32_t)(x->cr * R_MAP + x->cc) : -1;
    for (i = 0; i < x->nt && i < MAX_PTS; ++i)
        s_fitq_tc[s_fitq_nt + i] = x->tcol[i], s_fitq_tr[s_fitq_nt + i] = x->trow[i];
    s_fitq_nt += x->nt;
}

int net_fits(void)
{
    return s_n_fitq;
}

/*  One of them set up and its lines found, ready for the drive to walk
 *  its boundaries.  Answers how many boundaries there are. */
int net_fit_begin(const RCity *c, int i)
{
    if (i < 0 || i >= s_n_fitq)
        return 0;
    fit_tally_into(s_fitq[i].fit_fam);
    if (s_fitq[i].cand == 1)
        fit_tally_get(s_fitq[i].fit_fam, s_fit_before, sizeof s_fit_before);
    if (s_fitq[i].cand)
        fit_tally_set(s_fitq[i].fit_fam, s_fit_before, sizeof s_fit_before);
    return path_fit_begin(c, s_fitq_tc + s_fitq[i].at, s_fitq_tr + s_fitq[i].at, s_fitq[i].nt,
                          s_fitq[i].hw, s_fitq[i].start, s_fitq[i].goal, s_fitq[i].rmax, s_fitq[i].rmin,
                          s_fitq[i].gro, s_fitq[i].reserve, s_fitq[i].ex0, s_fitq[i].ex1,
                          s_fitq[i].cand == 2 ? s_fitq[i].free_reach : 0,
                          s_fit_q, s_fit_rad, s_fit_tlim, MAX_PTS);
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

/*  And the path it settled, kept: or held aside where the segment is
 *  fitted twice, until the script says which of the two to keep.
 *  Answers 1 when a choice is now waiting on that. */
int net_fit_done(int i)
{
    int nk, k;
    s_fit_at_choice = -1;
    if (i < 0 || i >= s_n_fitq)
        return 0;
    nk = path_fit_end();
    if (s_fitq[i].cand == 0)
    {
        fit_keep(s_fitq[i].f, s_fitq[i].col, s_fitq[i].row, s_fitq[i].e, s_fit_q, s_fit_rad, s_fit_tlim, nk);
        return 0;
    }
    fit_tally_get(s_fitq[i].fit_fam, s_fit_tally[s_fitq[i].cand - 1], sizeof s_fit_tally[0]);
    if (s_fitq[i].cand == 1)
    {
        for (k = 0; k < nk; ++k)
            s_fit_held_q[k] = s_fit_q[k], s_fit_held_rad[k] = s_fit_rad[k], s_fit_held_tlim[k] = s_fit_tlim[k];
        s_fit_held_nk = nk;
        fit_score(s_fit_rad, nk, s_fitq[i].rmin, s_fit_held_sc);
        return 0;
    }
    for (k = 0; k < nk; ++k)
        s_fit_free_q[k] = s_fit_q[k], s_fit_free_rad[k] = s_fit_rad[k], s_fit_free_tlim[k] = s_fit_tlim[k];
    s_fit_free_nk = nk;
    fit_score(s_fit_rad, nk, s_fitq[i].rmin, s_fit_free_sc);
    s_fit_at_choice = i;
    return 1;
}

const char *net_fit_choice(const int **free_, const int **held)
{
    if (s_fit_at_choice < 0)
        return NULL;
    *free_ = s_fit_free_sc;
    *held  = s_fit_held_sc;
    return net_family(s_fitq[s_fit_at_choice].f)->name;
}

/*  The one the script kept, and the tally that goes with it. */
void net_fit_choice_is(int keep_free)
{
    int i = s_fit_at_choice;
    if (i < 0)
        return;
    s_fit_at_choice = -1;
    if (g_dev.path_dump)
        dumpf("FIT %s %s kept: free %d corners %d tight %d nodes, own %d corners %d tight %d nodes\n",
              net_family(s_fitq[i].f)->name, keep_free ? "free" : "own",
              s_fit_free_sc[0], s_fit_free_sc[1], s_fit_free_sc[2],
              s_fit_held_sc[0], s_fit_held_sc[1], s_fit_held_sc[2]);
    fit_tally_set(s_fitq[i].fit_fam, s_fit_tally[keep_free ? 1 : 0], sizeof s_fit_tally[0]);
    if (keep_free)
        fit_keep(s_fitq[i].f, s_fitq[i].col, s_fitq[i].row, s_fitq[i].e, s_fit_free_q, s_fit_free_rad, s_fit_free_tlim, s_fit_free_nk);
    else
        fit_keep(s_fitq[i].f, s_fitq[i].col, s_fitq[i].row, s_fitq[i].e, s_fit_held_q, s_fit_held_rad, s_fit_held_tlim, s_fit_held_nk);
}

static void seg_class_ask(RMesh *m, const RCity *c, const RAtlasLevel *l, uint8_t mask_bit, int comp, Family f, const NetRun *run, uint8_t *visited)
{
    Seg              x;
    const NetFamily *fam = net_family(f);
    int32_t          col = run->cell[0] % R_MAP, row = run->cell[0] / R_MAP;
    int              e   = net_run_edge(run);
    if (visited[(row * R_MAP + col) * 4 + e])
        return;
    visited[(row * R_MAP + col) * 4 + e] = 1;
    memset(&x, 0, sizeof x);
    x.m = m, x.c = c, x.l = l, x.mask_bit = mask_bit, x.comp = comp, x.f = f, x.col = col, x.row = row, x.e = e, x.visited = visited;
    x.pts = s_wk_pts, x.q = s_wk_q, x.rad = s_wk_rad, x.tlim = s_wk_tlim;
    x.tcol = s_wk_tcol, x.trow = s_wk_trow, x.pieces = s_wk_pieces, x.marks = s_wk_marks;
    x.hw    = *fam->width * 0.5f;
    x.kind0 = node_kind(c, l, f, col, row);
    x.cc = col, x.cr = row, x.back = (e + 2) & 3, x.ee = e;
    if (seg_from_cells(&x, run) != 0 || x.n < 2)
        return;
    if (fam->classed)
    {
        if (s_n_cls_ask >= CLASSES_MAX)
        {
            R_ERR("net", "no room for the class at %d,%d: %d segments is the most read", (int)col, (int)row, CLASSES_MAX);
            return;
        }
        s_cls_ask[s_n_cls_ask].col = col;
        s_cls_ask[s_n_cls_ask].row = row;
        s_cls_ask[s_n_cls_ask].e   = e;
        seg_class_counts(&x, s_cls_ask[s_n_cls_ask].cnt);
        s_cls_ask[s_n_cls_ask].at = s_n_cls_cells;
        s_cls_ask[s_n_cls_ask].nt = 0;
        {
            int t;
            for (t = 0; t < x.nt && s_n_cls_cells < CLASS_CELLS; ++t)
                s_cls_cells[s_n_cls_cells++] = x.trow[t] * R_MAP + x.tcol[t],
                ++s_cls_ask[s_n_cls_ask].nt;
        }
        ++s_n_cls_ask;
    }
    /*  The corridor is the segment's own tiles, its gates the crossable
     *  part of each shared edge.  The path is the taut string through
     *  them.  It cuts every corner of a staircase into one diagonal by
     *  itself, with each corner then swept as wide as the corridor
     *  allows.  The corridor is tested against the line's own half
     *  width, not a fraction of it: what has to fit inside the corridor
     *  is the line. */
    if (fam->free_reach > 0)
    {
        fitq_add(fam, f, col, row, e, &x, 1, fam->free_reach);
        fitq_add(fam, f, col, row, e, &x, 2, fam->free_reach);
    }
    else
        fitq_add(fam, f, col, row, e, &x, 0, 0);
}

int build_networks_classes(RMesh *m, const RCity *c, const RAtlasLevel *l, uint8_t mask_bit, int comp)
{
    static uint8_t visited[R_MAP * R_MAP * 4];
    int            fk;
    s_n_cls_ask = s_n_cls_cells = 0;
    net_cut_reset(); /* the fits queue their cuts as they are kept */
    s_n_fitq = s_fitq_nt = 0;
    s_n_fit_ask = s_fit_n = 0;
    memset(s_seg_cls, 0, sizeof s_seg_cls);
    memset(s_fit_at, -1, sizeof s_fit_at);
    for (fk = 0; fk < net_n_walked; ++fk)
    {
        /*  The same runs the measuring walk reads, in the same order:
             both step through the network the script discovered, so the
             class a segment is given and the segment it is given to
             cannot be about different runs. */
        Family fam = net_walked[fk]->f;
        int    i, n = net_disc_count(fk);
        memset(visited, 0, sizeof visited);
        for (i = 0; i < n; ++i)
        {
            NetRun run;
            if (net_disc_run_get(fk, i, &run))
                seg_class_ask(m, c, l, mask_bit, comp, fam, &run, visited);
        }
    }
    return s_n_cls_ask;
}

int net_seg_class_at(int i, int cnt[3], const int32_t **cells, int *nt)
{
    if (i < 0 || i >= s_n_cls_ask)
        return 0;
    cnt[0] = s_cls_ask[i].cnt[0], cnt[1] = s_cls_ask[i].cnt[1], cnt[2] = s_cls_ask[i].cnt[2];
    *cells = &s_cls_cells[s_cls_ask[i].at];
    *nt    = s_cls_ask[i].nt;
    return 1;
}

/*  The class the rule settled, on to every link of the segment's start:
 *  the walks that follow read it there. */
void net_seg_class_is(int i, int cls)
{
    if (i < 0 || i >= s_n_cls_ask)
        return;
    s_seg_cls[(s_cls_ask[i].row * R_MAP + s_cls_ask[i].col) * 4 + s_cls_ask[i].e] = (float)cls;
}

/*  ------------------------------------------------------------------
 *  The drawing pass, walked by the drive
 *
 *  The pass is a cursor, not a loop: the drive asks a family for its
 *  junctions, then for one thing at a time until there is nothing left.
 *  The order is the SCRIPT'S: the network it discovered, entry by
 *  entry: and the junctions come first so a leg knows whether it is
 *  signaled before it draws its stripe.
 *  ------------------------------------------------------------------ */
static struct
{
    RMesh             *m;
    const RCity       *c;
    const RAtlasLevel *l;
    uint8_t            mask_bit;
    int                comp;
    Family             fam;
    int                fk; /* which walked family: its list in the store */
    int                at; /* how far down that list the cursor has come */
} s_draw;
static uint8_t s_draw_visited[R_MAP * R_MAP * 4];
static double  s_draw_t;

int build_networks_draw(RMesh *m, const RCity *c, const RAtlasLevel *l, uint8_t mask_bit, int comp)
{
    double tt = tms();
    (void)m, (void)c, (void)l, (void)mask_bit, (void)comp;
    tnote("stage three: junction shapes", tt);
    s_draw_t = tms();
    return 0;
}

int build_draw_families(void)
{
    return net_n_walked;
}

/*  One family's junctions, all of them, before any of its segments.  It
 *  is a cursor like the one below, because the drive lays the fill on
 *  each outline between the two halves of its box. */
static struct
{
    RMesh             *m;
    const RCity       *c;
    const RAtlasLevel *l;
    uint8_t            mask_bit;
    Family             fam;
    int                fk, at;
} s_box;

void build_draw_boxes_begin(RMesh *m, const RCity *c, const RAtlasLevel *l, uint8_t mask_bit, int fk)
{
    memset(&s_box, 0, sizeof s_box);
    s_box.m = m, s_box.c = c, s_box.l = l, s_box.mask_bit = mask_bit;
    s_box.fam = net_walked[fk]->f;
    s_box.fk  = fk;
}

int build_draw_box_next(void)
{
    while (s_box.at < net_disc_junctions(s_box.fk))
    {
        const int32_t cell = net_disc_junction(s_box.fk, s_box.at++);
        const int32_t col = cell % R_MAP, row = cell / R_MAP;
        const int     links = eff_links(s_box.c, s_box.l, col, row, s_box.fam);
        if (build_junction(s_box.m, s_box.c, s_box.mask_bit, s_box.fam, col, row, links,
                           tile_order(s_box.c, col, row, s_box.mask_bit) + net_family(s_box.fam)->junc_lift) != 0)
            return -1;
        return 1;
    }
    return 0;
}

/*  And the cursor over what it draws after them, set to the first.  What
 *  it steps through is the network the script discovered: the runs and
 *  the lone pieces, in the order they were handed over. */
void build_draw_begin(RMesh *m, const RCity *c, const RAtlasLevel *l, uint8_t mask_bit, int comp, int fk)
{
    memset(&s_draw, 0, sizeof s_draw);
    s_draw.m = m, s_draw.c = c, s_draw.l = l;
    s_draw.mask_bit = mask_bit, s_draw.comp = comp;
    s_draw.fam = net_walked[fk]->f;
    s_draw.fk  = fk;
    memset(s_draw_visited, 0, sizeof s_draw_visited);
}

/*  The next thing this family draws.  Answers 1 for one drawn, 0 when
 *  the family has none left, -1 with the reason already reported. */
int build_draw_next(void)
{
    RMesh             *m        = s_draw.m;
    const RCity       *c        = s_draw.c;
    const RAtlasLevel *l        = s_draw.l;
    const uint8_t      mask_bit = s_draw.mask_bit;
    const int          comp     = s_draw.comp;
    const Family       fam      = s_draw.fam;
    while (s_draw.at < net_disc_count(s_draw.fk))
    {
        const int     i    = s_draw.at++;
        const int     kind = net_disc_kind(s_draw.fk, i);
        const int32_t cell = net_disc_cell(s_draw.fk, i);
        const int32_t col = cell % R_MAP, row = cell / R_MAP;
        net_cut_reset(); /* this thing's own paths, and nothing left from the last */
        if (kind == NET_DISC_RUN)
        {
            NetRun run;
            if (!net_disc_run_get(s_draw.fk, i, &run))
                continue;
            if (walk_segment(m, c, l, mask_bit, comp, fam, &run, s_draw_visited) != 0)
                return -1;
            return 1;
        }
        /*  A junction is not drawn here: the box cursor above has
         *  already drawn every one of them.  So a leg knows whether it
         *  is signaled before it draws its stripe. */
        if (kind == NET_DISC_JUNCTION)
            continue;
        /*  A lone piece: its own short band.  The kind whose links all
         *  leave the map is drawn only where no run covered the tile
         *  after all.  A run reaches it, steps straight off the map and
         *  comes back with one cell.  Which is why the script hands
         *  those over last. */
        if (kind == NET_DISC_EDGE && net_tile_served(row * R_MAP + col))
            continue;
        if (build_island(m, c, l, mask_bit, comp, fam, col, row) != 0)
            return -1;
        return 1;
    }
    return 0;
}

/*  The margins are the composing script's own pass, run after this one
 *  (scripts/compose/world.lua).  It asks for each path the network holds
 *  and lays the band over it.  They come last because every junction has
 *  registered its corners by then.  So the bands are laid in one place
 *  instead of inside whichever box happened to make them. */
int build_networks_drawn(void)
{
    tnote("junctions and segments drawn", s_draw_t);
    return 0;
}

/*  A tile the corridor pass graded: all four of its corners belong to a
 *  corridor. */
/*  The grading pass skips what only the drawing needs: junction boxes,
 *  lanes, spurs, transitions.  --grade-all runs them anyway, for a
 *  comparison. */
float net_cross_depth(Family f, int32_t col, int32_t row, int e)
{
    if (col < 0 || row < 0 || col >= R_MAP || row >= R_MAP || e < 0 || e > 3)
        return 0.0f;
    return s_xwalk[FAMX(f)][(row * R_MAP + col) * 4 + e];
}

int grade_only(int allowed)
{
    return s_pass == 1 && !g_dev.grade_all && !allowed;
}
