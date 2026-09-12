/*  band_walk.c: ONE BAND WALKED, FITTED AND LOFTED.
 *
 *  A band's cells are read in band.c.  This is what happens to them.
 *  The walk from one end, and the chain of fit points the rule picks.
 *  The fit two ways, with the better kept.  The overlay, and the slab's
 *  loft.
 *
 *  With those go three more.  The walk the build hands out one band at a
 *  time.  The replay of a band the table already holds.  And the family
 *  the whole lot is declared under.
 *
 *  Nothing here decides any of it.  Arc.rules.stair picks the points the
 *  fit is given.  Arc.rules.fit_choice keeps one of the two fits.  What
 *  the slab carries is the family's own declaration. */
#include <string.h>

#include "dump.h"
#include "mesh/internal.h"
#include "mesh/model.h"
#include "net/internal.h"
#include "net/net.h"
#include "opt.h"
#include "pipeline.h"
#include "incr.h"


/*  Walk one band from an end and loft its slab.  The spine runs along
 *  the seam: half a tile across from the primary tile's center. */

/*  One band's walk, in stages: the tiles, the ends, the fit, the
 *  overlay, the loft.  Each stage reads what it needs off this and
 *  writes back what it changed. */
typedef struct
{
    RMesh       *m;
    const RCity *c;
    uint8_t      mask_bit;
    int          comp;
    int32_t      col, row; /* where the walk began */
    int          ew, sign;
    uint8_t     *seen;
    V2          *pts; /* the spine's points: seam points and block centers */
    uint8_t     *spur;
    uint8_t     *block; /* per point: a curve block's center */
    uint8_t     *stair; /* per point: the collapsed staircase it belongs to, 1-based, or 0 (band_chain_ask) */
    Piece       *pieces;
    int32_t     *own; /* the band's own tiles */
    V2          *q;   /* the fit's nodes, their radii and tangent budgets */
    float       *rad, *tlim;
    int          n, n_own, np, nk;
    int          cut;          /* where the drive's pieces for this band are */
    int          table;        /* the band's entry in the segment table, -1 for none: the loft's cache key */
    V2           band0, band1; /* the ends, run out to the edge of the end cells */
    int32_t      cc, cr, dx, dy;
    int          cew;
    float        total, spur0, spur1;
    BandRun       *run; /* the cells the band is made of, before anything is built from them */
} BandWalk;



/*  Through a curve block, and on through every block that follows it.
 *  Its center lies on both seams.  So the spine turns there, and the
 *  fillet carries the arc back into the tile before and on into the one
 *  after.  The art draws a diagonal band as a chain of these blocks
 *  touching at their corners.  So the walk follows the chain by whatever
 *  step joins one to the next.  The straightener makes one line of the
 *  centers. 1 when a chain was taken (the walk goes on from where it
 *  left the chain, or ends if nothing continued it). */





/*  The tiles: from the first cell along the band wherever it goes.
 *  Straight on while it can, through a curve block at a right angle, or
 *  a step sideways.  Until nothing continues it. */
/*  And the band BUILT from a run of cells: the spine's points, the tiles
 *  the band owns, and which of its cells carry a spur.  Everything here
 *  follows from the run.  Nothing decides which cells are in it.  A
 *  cell's point sits on the seam, half a tile across from its own
 *  center.  A curve block's is its corner, which lies on both seams at
 *  once. */
static void band_run_take(BandWalk *x)
{
    const BandRun *r = x->run;
    int          i;
    x->n = x->n_own = 0;
    for (i = 0; i < r->n; ++i)
    {
        const int32_t cc = r->cell[i] % R_MAP, cr = r->cell[i] / R_MAP;
        if (r->block[i])
        {
            if (x->n_own + 4 <= 4 * MAX_PTS)
            {
                x->own[x->n_own++] = cr * R_MAP + cc;
                x->own[x->n_own++] = cr * R_MAP + cc + 1;
                x->own[x->n_own++] = (cr + 1) * R_MAP + cc;
                x->own[x->n_own++] = (cr + 1) * R_MAP + cc + 1;
            }
            x->spur[x->n]  = 0;
            x->block[x->n] = 1;
            x->pts[x->n]   = (V2){(float)cc + 1.0f, (float)cr + 1.0f};
            ++x->n;
            continue;
        }
        if (x->n_own + 2 <= 4 * MAX_PTS)
        {
            x->own[x->n_own++] = cr * R_MAP + cc;
            x->own[x->n_own++] = r->ew[i] ? (cr + 1) * R_MAP + cc : cr * R_MAP + cc + 1;
        }
        x->spur[x->n]  = (uint8_t)band_is_incline(x->c->xbld[cr * R_MAP + cc]);
        x->block[x->n] = 0;
        x->pts[x->n]   = (V2){(float)cc + (r->ew[i] ? 0.5f : 1.0f), (float)cr + (r->ew[i] ? 1.0f : 0.5f)};
        ++x->n;
    }
}


/*  Run the spine to the outer edge of the end cells.  The slab then
 *  covers its whole first and last segment, rather than stopping at
 *  their centers.  The direction at each end is the polyline's own,
 *  since the band may have turned along the way. */
static void band_ends(BandWalk *x)
{
    V2 *pts   = x->pts;
    V2  band0;
    V2  band1;
    int n     = x->n;
    {
        float d0x = pts[0].x - pts[1].x, d0y = pts[0].y - pts[1].y;
        float d1x = pts[n - 1].x - pts[n - 2].x, d1y = pts[n - 1].y - pts[n - 2].y;
        float l0 = sqrtf(d0x * d0x + d0y * d0y), l1 = sqrtf(d1x * d1x + d1y * d1y);
        band0 = pts[0];
        band1 = pts[n - 1];
        if (l0 > 1e-4f)
        {
            band0.x += d0x / l0 * 0.5f;
            band0.y += d0y / l0 * 0.5f;
        }
        if (l1 > 1e-4f)
        {
            band1.x += d1x / l1 * 0.5f;
            band1.y += d1y / l1 * 0.5f;
        }
    }
    x->band0 = band0;
    x->band1 = band1;
}

/*The band's tiles as a mask, for the fit's coverage rule.  The slab must
 *pass over every one of them.  Take away the tiles of the curve blocks.
 *A corner's sweep or a staircase's diagonal rightly cuts inside them.
 *The straight cells beside an on-spur are in it like any other.  The fit
 *holds its runs and its joins to them.  So a staircase may collapse next
 *to a spur and the slab still passes the spur's cell. */
static uint8_t        s_own_mask[R_MAP * R_MAP];
static const uint8_t *band_own_mask(const BandWalk *x)
{
    int k;
    memset(s_own_mask, 0, sizeof s_own_mask);
    for (k = 0; k < x->n_own; ++k)
        s_own_mask[x->own[k]] = 1;
    for (k = 0; k < x->n; ++k)
        if (x->block[k])
        {
            int32_t bc = (int32_t)floorf(x->pts[k].x) - 1, br = (int32_t)floorf(x->pts[k].y) - 1, dc, dr;
            for (dr = 0; dr < 2; ++dr)
                for (dc = 0; dc < 2; ++dc)
                    if (bc + dc >= 0 && br + dr >= 0 && bc + dc < R_MAP && br + dr < R_MAP)
                        s_own_mask[(br + dr) * R_MAP + bc + dc] = 0;
        }
    return s_own_mask;
}

/*  Which way the band turns at point j.  It is the sign of the cross
 *  product of the steps in and out.  It is 0 at an end, or straight
 *  through. */
static int band_turn(const V2 *pts, int n, int j)
{
    float cross;
    if (j <= 0 || j + 1 >= n)
        return 0;
    cross = (pts[j].x - pts[j - 1].x) * (pts[j + 1].y - pts[j].y) - (pts[j].y - pts[j - 1].y) * (pts[j + 1].x - pts[j].x);
    return cross > 1e-4f ? 1 : cross < -1e-4f ? -1
                                              : 0;
}

/*  Is a straight cell's point pinned by an on-spur beside it?  An
 *  on-spur joins the slab cell it touches.  So the slab must pass over
 *  that cell: a staircase breaks at such a cell rather than sliding its
 *  diagonal off it. */
static int band_pinned(const RCity *c, V2 p)
{
    int32_t fx = (int32_t)floorf(p.x), fy = (int32_t)floorf(p.y);
    int32_t t[2][2], k, d;
    static int gix_cell_axis = -1;
    /*  How far into a tile the point has to lie before the cell reads as
     *  east-west rather than north-south is the script's. */
    if (p.x - (float)fx > geo_num(&gix_cell_axis, "slab_cell_axis")) /* east-west: the pair is the rows above and below */
        t[0][0] = fx, t[0][1] = fy - 1, t[1][0] = fx, t[1][1] = fy;
    else /* a north-south cell: the pair is the columns either side */
        t[0][0] = fx - 1, t[0][1] = fy, t[1][0] = fx, t[1][1] = fy;
    for (k = 0; k < 2; ++k)
        for (d = 0; d < 4; ++d)
        {
            static const int32_t DC[4] = {0, 1, 0, -1}, DR[4] = {-1, 0, 1, 0};
            int32_t              qc = t[k][0] + DC[d], qr = t[k][1] + DR[d];
            uint8_t              b;
            if (qc < 0 || qr < 0 || qc >= R_MAP || qr >= R_MAP)
                continue;
            b = c->xbld[qr * R_MAP + qc];
            if (net_band_spur(b))
                return 1;
        }
    return 0;
}

/*  The points the fit is given: the straight cells', and a curve block's
 *  center only where the block is a corner of its own.  A STAIRCASE is
 *  blocks turning alternately, with at most two straight cells between.
 *  It is the game's way of laying a diagonal.  It is given as ONE point,
 *  the center of the blocks and short straights it is made of.  So the
 *  run before it, the diagonal through its middle and the run after it
 *  are three legs the fit fillets at their two bends.  The coverage rule
 *  keeps the slab over the short straights.  With every block a point, a
 *  staircase was a polyline of right-angle corners two tiles apart, each
 *  filleted at a tile's radius: a serpentine.  With nothing of it, the
 *  fit joined the runs at its ends by an L across the block beside it. */
/*  Which way the chain turns at cell i: 1, -1, or 0 for straight on. */
int band_stair_turn(const StairFan *s, int i)
{
    return band_turn(s->pts, s->n, i);
}

/*  Is the cell pinned by an on-spur beside it? */
int band_stair_pinned(const StairFan *s, int i)
{
    return band_pinned((const RCity *)s->c, s->pts[i]);
}

/*  A cell as a point of the chain, and a stair as the one point at the
 *  center of the cells it is made of. */
void band_stair_point(StairFan *s, int i)
{
    s->chain[s->nc++] = s->pts[i];
}

void band_stair_centre(StairFan *s, int i, int j)
{
    V2  mid = {0.0f, 0.0f};
    int k, m = 0;
    for (k = i; k <= j; ++k, ++m)
        mid.x += s->pts[k].x, mid.y += s->pts[k].y;
    s->chain[s->nc++] = (V2){mid.x / (float)m, mid.y / (float)m};
}

/*  The points the fit is given, as arc.rules.stair picks them: the band
 *  is read here and the rule picks from it.  So the chain stands ready
 *  before the fit is set up. */
static V2       s_band_chain[MAX_PTS];
static StairFan s_band_stair;

static void band_chain_ask(const BandWalk *x)
{
    memset(&s_band_stair, 0, sizeof s_band_stair);
    s_band_stair.c     = x->c;
    s_band_stair.pts   = x->pts;
    s_band_stair.block = x->block;
    s_band_stair.n     = x->n;
    s_band_stair.gap   = (int)s_tune.band_stair;
    s_band_stair.chain = s_band_chain;
}

StairFan *net_band_chain(void)
{
    return s_band_stair.n > 0 ? &s_band_stair : NULL;
}

/*  The fit two ways, the better kept.  One way has free lines and the
 *  band's spur tiles in its corridor.  The slab runs as straight as its
 *  corridor allows, and the plain fit.  Which of the two is better is
 *  arc.rules.fit_choice's. */
/*  The fit two ways, the better kept.  One way has free lines and the
 *  band's spur tiles in its corridor.  The slab runs as straight as its
 *  corridor allows, and the plain fit.  Which of the two is better is
 *  arc.rules.fit_choice's.  So the band reads what each fit needs, the
 *  drive runs both and settles the choice, and the walk takes the one
 *  that was kept. */
static struct
{
    const RCity   *c;
    const BandWalk  *x;
    const int32_t *own;
    const V2      *chain;
    int            n_own, nc, live;
    V2             band0, band1;
} s_bandfit;
static V2    s_bandfit_q[2][MAX_PTS];
static float s_bandfit_rad[2][MAX_PTS], s_bandfit_tlim[2][MAX_PTS];
static int   s_bandfit_nk[2], s_bandfit_sc[2][3];
static char  s_bandfit_tally[2][512], s_bandfit_before[512];
static int   s_bandfit_kept = -1;

static void band_fit_ask(const RCity *c, const BandWalk *x, const int32_t *own, int n_own, const V2 *chain, int nc, V2 band0, V2 band1)
{
    s_bandfit.c     = c;
    s_bandfit.x     = x;
    s_bandfit.own   = own;
    s_bandfit.n_own = n_own;
    s_bandfit.chain = chain;
    s_bandfit.nc    = nc;
    s_bandfit.band0 = band0;
    s_bandfit.band1 = band1;
    s_bandfit.live  = 1;
    s_bandfit_nk[0] = s_bandfit_nk[1] = 0;
    fit_tally_get(2, s_bandfit_before, sizeof s_bandfit_before);
}

int net_band_fits(void)
{
    return s_bandfit.live ? 2 : 0;
}

int net_band_fit_begin(int w)
{
    if (!s_bandfit.live || w < 0 || w > 1)
        return 0;
    fit_tally_set(2, s_bandfit_before, sizeof s_bandfit_before);
    return path_fit_points_begin(band_corridor(s_bandfit.c, s_bandfit.own, s_bandfit.n_own, w),
                                 band_own_mask(s_bandfit.x), s_bandfit.chain, s_bandfit.nc,
                                 s_tune.band_w, s_bandfit.band0, s_bandfit.band1,
                                 s_tune.band_rmax, s_tune.band_rmin, 1.0f, -1, -1, w,
                                 s_bandfit_q[w], s_bandfit_rad[w], s_bandfit_tlim[w], MAX_PTS);
}

void net_band_fit_done(int w)
{
    int k;
    if (!s_bandfit.live || w < 0 || w > 1)
        return;
    s_bandfit_nk[w]    = path_fit_points_end();
    s_bandfit_sc[w][0] = s_bandfit_sc[w][1] = 0;
    s_bandfit_sc[w][2] = s_bandfit_nk[w];
    for (k = 1; k + 1 < s_bandfit_nk[w]; ++k)
        if (s_bandfit_rad[w][k] < 0.01f)
            ++s_bandfit_sc[w][0];
        else if (s_bandfit_rad[w][k] < s_tune.band_rmin)
            ++s_bandfit_sc[w][1];
    fit_tally_get(2, s_bandfit_tally[w], sizeof s_bandfit_tally[w]);
}

const char *net_band_fit_choice(const int **free_, const int **held)
{
    if (!s_bandfit.live)
        return NULL;
    *free_ = s_bandfit_sc[1];
    *held  = s_bandfit_sc[0];
    return "band";
}

/*  The one the script kept, into the band's own arrays, and the tally
 *  that goes with it. */
void net_band_fit_choice_is(int keep_free)
{
    const int w = keep_free ? 1 : 0;
    if (!s_bandfit.live)
        return;
    s_bandfit.live = 0;
    s_bandfit_kept = w;
    if (g_dev.path_dump)
        dumpf("FIT %s kept: free %d corners %d tight %d nodes, plain %d corners %d tight %d nodes\n",
              keep_free ? "free" : "plain",
              s_bandfit_sc[1][0], s_bandfit_sc[1][1], s_bandfit_sc[1][2],
              s_bandfit_sc[0][0], s_bandfit_sc[0][1], s_bandfit_sc[0][2]);
    fit_tally_set(2, s_bandfit_tally[w], sizeof s_bandfit_tally[w]); /* the kept fit's tallies alone */
}

static int net_band_fit_take(V2 *q, float *rad, float *tlim)
{
    const int w = s_bandfit_kept;
    int       k;
    if (w < 0)
        return 0;
    for (k = 0; k < s_bandfit_nk[w]; ++k)
        q[k] = s_bandfit_q[w][k], rad[k] = s_bandfit_rad[w][k], tlim[k] = s_bandfit_tlim[w][k];
    return s_bandfit_nk[w];
}

/*  The corridor fit, straightened as a line is and filleted with the
 *  wide radius a band wants.  1 when there is nothing to draw. */
static int band_fit(BandWalk *x)
{
    const RCity *c      = x->c;
    int32_t      col    = x->col;
    int32_t      row    = x->row;
    V2          *pts    = x->pts;
    uint8_t     *spur   = x->spur;
    Piece       *pieces = x->pieces;
    int32_t     *own    = x->own;
    V2          *q      = x->q;
    float       *rad    = x->rad;
    float       *tlim   = x->tlim;
    int          n_own  = x->n_own;
    V2           band0  = x->band0;
    V2           band1  = x->band1;
    int          n      = x->n;
    int          np     = x->np;
    float        spur0;
    float        spur1;
    int          nk;
    /*  Straightened as a line is: a staircase of cells becomes one
     *  diagonal.  Then every bend is filleted, with the wide radius a
     *  band wants so the curve begins well before the corner.  The spur
     *  lengths come first, measured along the raw polyline in TILES: the
     *  taper below compares them against arc length.  A count of points
     *  is no length at all once the straightener has collapsed a run.  A
     *  slab is raised end to end: the 0x61-0x64 cells at a band's ends
     *  are the band's own, the slab runs over them.  The taper is the
     *  on-spurs' alone. */
    spur0 = spur1 = 0.0f;
    (void)spur;
    /*  The slab on the corridor fit, as a chain of its own points: the
     *  seam points and the block centers.  The corridor is not the
     *  band's tiles alone: a slab is RAISED, and may sweep over any tile
     *  that is free air: ground, trees, water.  Only what stands in the
     *  way bounds it: a building, another network, another slab.  So a
     *  jog of two tiles becomes one long S over the ground beside it.  A
     *  corner block's arc takes the radius a band wants.  Nothing has to
     *  be covered: where the arc cuts inside a block, the ground shows
     *  under the viaduct, as it should. */
    fit_tally_into(net_band->fit_fam);
    /*  Every one of the band's own tiles must end up under the slab, as
     *  a line's must under its strip.  The corridor says where the slab
     *  MAY sweep, the own tiles where it MUST pass.  Without that rule a
     *  long band's jogs collapse into a line across the block beside
     *  them.  That line lies tiles off the band's cells, and over lines
     *  and buildings.  The fit is given the chain band_chain_ask makes of the
     *  points: the straight cells', a lone block's corner, and nothing
     *  of a staircase.  So the runs at its ends meet across it.  The
     *  band the fit holds to the corridor is the slab's own width and no
     *  wider.  At a full tile each side its inner edge at a corner
     *  samples the on-spur tile beside the slab, which is not free air.
     *  Every fillet is refused down to a kink. */
    band_fit_ask(c, x, own, n_own, s_band_chain, s_band_stair.nc, band0, band1);
    (void)pts, (void)pieces, (void)q, (void)rad, (void)tlim, (void)np, (void)col, (void)row, (void)n, (void)nk;
    x->spur0 = spur0;
    x->spur1 = spur1;
    return 0;
}

/*  And the fit the drive kept, into the band.  It holds its pieces, its
 *  nodes and its own tiles, which the building pass replays from rather
 *  than walking and fitting it again. */
static int band_fit_take(BandWalk *x)
{
    const RCity *c      = x->c;
    int32_t      col    = x->col;
    int32_t      row    = x->row;
    V2          *pts    = x->pts;
    int32_t     *own    = x->own;
    V2          *q      = x->q;
    float       *rad    = x->rad;
    float       *tlim   = x->tlim;
    int          n_own  = x->n_own;
    int          n      = x->n;
    int          nk     = net_band_fit_take(q, rad, tlim);
    (void)c;
    if (nk < 2)
    {
        x->nk = nk;
        return 1;
    }
    if (g_dev.band_dump)
    {
        int q2;
        dumpf("band from c%d r%d: %d points ->", (int)col, (int)row, n);
        for (q2 = 0; q2 < n && q2 < 80; ++q2)
            dumpf(" (%.2f,%.2f)", (double)pts[q2].x, (double)pts[q2].y);
        dumpf("\n   fitted %d ->", nk);
        for (q2 = 0; q2 < nk && q2 < 14; ++q2)
            dumpf(" (%.2f,%.2f) r%.2f", (double)q[q2].x, (double)q[q2].y, (double)rad[q2]);
        dumpf("\n");
    }
    if (g_dev.path_dump)
    {
        /*  The same lines the line fit prints.  So tools/plan.py draws a
         *  slab the way it draws a line: its tiles, the fitted line, its
         *  radii and budgets, and the runs it found. */
        int d;
        dumpf("PATH hw=%.3f\nTILES", (double)s_tune.band_w);
        for (d = 0; d < n_own; ++d)
            dumpf(" %d,%d", (int)(own[d] % R_MAP), (int)(own[d] / R_MAP));
        dumpf("\nGATES\nPTS");
        for (d = 0; d < nk; ++d)
            dumpf(" %.3f,%.3f", (double)q[d].x, (double)q[d].y);
        dumpf("\nRAD");
        for (d = 0; d < nk; ++d)
            dumpf(" %.3f", (double)rad[d]);
        dumpf("\nTLIM");
        for (d = 0; d < nk; ++d)
            dumpf(" %.3f", (double)tlim[d]);
        dumpf("\n");
        path_fit_prims();
    }
    /*  The band's path queued to be cut.  The cut is arc.rules.pieces's
     *  and the drive makes it, so this stops here and band_cut
     *  takes the answer.  A band queues alone: every segment's pieces
     *  have been drawn by the time the bands are walked.  So the queue
     *  is emptied for each band rather than growing through the pass. */
    net_cut_reset();
    x->cut = net_cut_add(q, nk, rad, tlim);
    x->nk  = nk;
    x->np  = 0;
    return 0;
}

/*  And the band built from the pieces the drive cut. */
static int band_fit_cut(BandWalk *x)
{
    Piece *pieces = x->pieces;
    int    np     = 0;
    if (net_cut_pieces(x->cut, pieces, MAX_PIECES, &np) != 0 || np == 0)
    {
        x->np = 0;
        return 1;
    }
    /*  The band kept for the building pass and the next build: its
     *  pieces, its fit's nodes, its own tiles.  The building pass replays
     *  it from here rather than walking and fitting it again. */
    if (s_pass != 2)
        x->table = seg_store_band(x->col, x->row, x->ew, x->sign, pieces, np,
                                  x->q, x->rad, x->tlim, x->nk, x->own, x->n_own);
    x->np = np;
    return 0;
}

/*  The fit's nodes and corridors as ground highlights, when the overlay
 *  is on.  1 to stop, as a failed highlight always has. */
static int band_overlay(BandWalk *x)
{
    RMesh       *m        = x->m;
    const RCity *c        = x->c;
    uint8_t      mask_bit = x->mask_bit;
    int32_t     *own      = x->own;
    V2          *q        = x->q;
    float       *rad      = x->rad;
    int          n_own    = x->n_own;
    int          nk       = x->nk;
    /*  The corridor the fit had to work with, asked for again: the walk
     *  stamped it per band and the highlight reads the same tiles. */
    const uint8_t *corr = band_corridor(c, own, n_own, 1);
    /*  The fit's nodes, when the overlay is on: the same marks the line
     *  walk draws, on the slab rather than the ground.  And under them
     *  what the fit had to work with: the band's own tiles outlined in
     *  tan, the free air beside them in blue.  Ground highlights,
     *  blended tints under everything that stands on the tile.  The
     *  outlines before them, and the slabs before those, hid the town. */
    if (s_tune.show_curves > 0.5f && s_pass != 1)
    {
        int     k3;
        int32_t ti;
        for (ti = 0; ti < R_MAP * R_MAP; ++ti)
        {
            int32_t tc = ti % R_MAP, tr = ti / R_MAP;
            int     isown = 0;
            for (k3 = 0; k3 < n_own && !isown; ++k3)
                isown = own[k3] == ti;
            if (!isown && !corr[ti])
                continue;
            if (tile_highlight(m, c, mask_bit, tc, tr, isown ? 3.0f : 6.0f) != 0)
                return 1;
        }
        for (k3 = 0; k3 < nk; ++k3)
        {
            int32_t tc = (int32_t)floorf(q[k3].x), tr = (int32_t)floorf(q[k3].y);
            float   paint, z, half;
            if (tc < 0 || tr < 0 || tc >= R_MAP || tr >= R_MAP)
                continue;
            paint = (k3 == 0 || k3 + 1 == nk) ? 7.0f : (rad[k3] > 0.001f ? 3.0f : 1.0f);
            if (k3 > 0 && k3 + 1 < nk && rad[k3] <= 0.001f)
            {
                V2    u1 = {q[k3].x - q[k3 - 1].x, q[k3].y - q[k3 - 1].y};
                V2    u2 = {q[k3 + 1].x - q[k3].x, q[k3 + 1].y - q[k3].y};
                float l1 = v2len(u1), l2 = v2len(u2);
                if (l1 > 1e-5f && l2 > 1e-5f && (u1.x * u2.x + u1.y * u2.y) / (l1 * l2) > 0.9999f)
                    paint = 7.0f;
            }
            (void)half;
            z    = surface_at_world(c, mask_bit, q[k3].x, q[k3].y) + BAND_LIFT;
            /*  The mark is a model (scripts/models): a line's end
             *  is a smaller one than a turn. */
            if (net_model_put_on(net_model_find(k3 == 0 || k3 + 1 == nk ? "node_end" : "node_mid"),
                                 m, c, mask_bit, tile_order(c, tc, tr, mask_bit),
                                 q[k3].x, q[k3].y, 1.0f, 0.0f, 0.0f, paint, 0.0f, z, z, 1) != 0)
                return 1;
        }
    }
    return 0;
}

/*  The band the walk lofted, held for the drive: it composes the slab,
 *  and build_band_done lays the lanes under it. */
static BandWalk s_band_hold;
static int    s_band_hold_live;
static int    s_band_of_hold;

/*  The slab lofted on the pieces.  Its lanes follow the composition. */
static int band_loft(BandWalk *x)
{
    RMesh       *m        = x->m;
    const RCity *c        = x->c;
    uint8_t      mask_bit = x->mask_bit;
    int          comp     = x->comp;
    Piece       *pieces   = x->pieces;
    int32_t     *own      = x->own;
    int          n_own    = x->n_own;
    int          np       = x->np;
    float        total    = x->total;
    float        spur0    = x->spur0;
    float        spur1    = x->spur1;
    int          guard;
    for (guard = 0; guard < np; ++guard)
        total += pieces[guard].len;
    /*  The lift tapers over the spur cells at each end.  It runs from
     *  the slab's height where the elevated tiles begin, down to the
     *  ground at the band's outer end.  So a spur is a spur. */
    int   rc;
    RLoft d = {0};
    if (spur0 >= total)
        spur1 = 0.0f; /* all spur: one slope, not two */
    d.f             = net_line->f;
    d.fam           = net_band;
    d.hw            = s_tune.band_w; /* the slab's half width */
    d.ground_margin = geo_num(&gix_slab_ground_margin, "slab_ground_margin"); /* it reads the ground a hair beyond its edges */
    d.mat           = MAT_BAND;
    d.kind          = LOFT_SLAB;
    d.spur0         = spur0;
    d.spur1         = spur1;
    d.pin0 = d.pin1 = 1;
    net_band_record(own, n_own);
    d.band = ++s_band_st_at; /* the band the loft records its stations under */
    d.cls  = -1.0f;
    /* the stations from the table's cache, as a line's: this build's, or the last one's when no edit came near */
    d.cache = x->table + 1;
    d.hash  = pieces_hash(pieces, np, total);
    for (guard = 0; guard < n_own && !d.hot; ++guard)
        if (incr_near(own[guard] % R_MAP, own[guard] / R_MAP))
            d.hot = 1;
    d.records_only = s_incr_on;
    for (guard = 0; guard < n_own && d.records_only; ++guard)
        if (incr_want_tile(own[guard] % R_MAP, own[guard] / R_MAP))
            d.records_only = 0;
    rc = loft(m, c, mask_bit, comp, &d, pieces, np, total);
    if (rc != 0)
        return rc;
    s_band_hold = *x;
    s_band_of_hold = s_band_st_at;
    s_band_hold_live = 1;
    return 0;
}

/*  The slab composed, and its six lanes laid under it (lane.c) at
 *  fractions of the slab's half width.  The drive composes the slab
 *  between the loft and this. */
int build_band_done(void)
{
    int rc = net_loft_close();
    if (rc != 0 || !s_band_hold_live)
        return rc;
    s_band_hold_live = 0;
    return lane_slab(s_band_hold.m, s_band_hold.c, s_band_hold.mask_bit, s_band_hold.pieces, s_band_hold.np, s_tune.band_w, s_band_of_hold);
}

/*  The band the walk fitted, held while the drive runs its fit. */
static BandWalk s_band_pend;
static int    s_band_pend_live;

static int walk_band(RMesh *m, const RCity *c, uint8_t mask_bit, int comp, int32_t col, int32_t row, int ew, int sign, const BandRun *given)
{
    static V2      pts[MAX_PTS];
    static uint8_t spur[MAX_PTS];
    static uint8_t block[MAX_PTS];
    static uint8_t stair[MAX_PTS];
    static Piece   pieces[MAX_PIECES];
    static int32_t own[4 * MAX_PTS]; /* the band's own tiles, both of each cell and all four of a block */
    static V2      q[MAX_PTS];
    static float   rad[MAX_PTS], tlim[MAX_PTS];
    static BandRun   run;
    BandWalk         x;
    memset(&x, 0, sizeof x);
    x.m = m, x.c = c, x.mask_bit = mask_bit, x.comp = comp, x.col = col, x.row = row, x.ew = ew, x.sign = sign;
    x.pts = pts, x.spur = spur, x.block = block, x.stair = stair, x.pieces = pieces, x.own = own, x.q = q, x.rad = rad, x.tlim = tlim;
    x.cc = col, x.cr = row, x.cew = ew;
    x.dx = ew ? sign : 0, x.dy = ew ? 0 : sign;
    x.table = -1;
    x.run   = &run;
    run     = *given;
    /*  The stages: what is built from the cells the script found, the
     *  ends, and the fit: which the drive runs.  So this stops here and
     *  build_band_fitted takes up the overlay and the loft. */
    band_run_take(&x);
    if (x.n < 2)
        return 0;
    band_ends(&x);
    /*  The band is held HERE, before the fit reads it: the fit keeps a
     *  handle on the band it is fitting.  The drive runs it after this
     *  walk has returned. */
    s_band_pend      = x;
    s_band_pend_live = 1;
    band_chain_ask(&s_band_pend);
    return 0;
}

/*  And the fit set up from the chain the rule picked. */
int build_band_chained(void)
{
    if (!s_band_pend_live)
        return 0; /* a band the building pass replayed: it was never walked */
    if (band_fit(&s_band_pend) != 0)
        s_band_pend_live = 0;
    return 0;
}

int build_band_fitted(void)
{
    if (!s_band_pend_live)
        return 0; /* a band the building pass replayed: it was never fitted */
    if (band_fit_take(&s_band_pend) != 0)
        s_band_pend_live = 0;
    return 0;
}

/*  And the band drawn from the pieces the drive cut for it. */
int build_band_cut(void)
{
    if (!s_band_pend_live)
        return 0;
    s_band_pend_live = 0;
    if (band_fit_cut(&s_band_pend) != 0)
        return 0;
    if (band_overlay(&s_band_pend) != 0)
        return 0;
    return band_loft(&s_band_pend);
}

/*  A band from the table: what the walk and the fit found, drawn again:
 *  its overlay, its loft, its lanes. */
static int band_replay(RMesh *m, const RCity *c, uint8_t mask_bit, int comp, int index, const RSeg *r)
{
    static Piece   pieces[MAX_PIECES];
    static int32_t own[4 * MAX_PTS];
    static V2      q[MAX_PTS];
    static float   rad[MAX_PTS], tlim[MAX_PTS];
    const Piece   *pc;
    const V2      *rq;
    const float   *rrad, *rtlim;
    const int32_t *tcol, *trow;
    BandWalk         x;
    int            k;
    seg_table_arenas(r, &pc, &rq, &rrad, &rtlim, &tcol, &trow);
    memset(&x, 0, sizeof x);
    x.m = m, x.c = c, x.mask_bit = mask_bit, x.comp = comp, x.col = r->col, x.row = r->row;
    x.pieces = pieces, x.own = own, x.q = q, x.rad = rad, x.tlim = tlim;
    x.table = index;
    for (k = 0; k < r->np && k < MAX_PIECES; ++k)
        pieces[k] = pc[k];
    x.np = k;
    for (k = 0; k < r->nk && k < MAX_PTS; ++k)
    {
        q[k]    = rq[k];
        rad[k]  = rrad[k];
        tlim[k] = rtlim[k];
    }
    x.nk = k;
    for (k = 0; k < r->nt && k < 4 * MAX_PTS; ++k)
        own[k] = trow[k] * R_MAP + tcol[k];
    x.n_own = k;
    if (band_overlay(&x) != 0)
        return 0;
    return band_loft(&x);
}

static double s_band_tp; /* where the spurs ended, so the links time from there */

/*  ------------------------------------------------------------------
 *  The bands, one at a time
 *
 *  The building pass reads every band the grading pass kept, in the
 *  order it walked them, since the spurs find a band's stations by its
 *  number.  A grading pass has nothing kept, and reads the bands the
 *  SCRIPT discovered instead (scripts/compose/bands.lua): in the order
 *  it found them, which is the order everything downstream numbers them
 *  by.  The drive composes each slab between its loft and the lanes laid
 *  under it.
 *  ------------------------------------------------------------------ */
static struct
{
    RMesh             *m;
    const RCity       *c;
    const RAtlasLevel *l;
    uint8_t            mask_bit;
    int                comp;
    int                phase; /* 0 the table's bands, 1 the script's, 3 nothing left */
    int                i;     /* how far down that list the cursor has come */
} s_bands_walk;



/*  Whether this build has bands to DISCOVER.  A building pass with a
 *  segment table replays the grading pass's fits.  And there is nothing
 *  for a script to find. */
int net_band_replaying(void)
{
    return !(s_pass == 2 && seg_table_count() > 0 && !g_dev.no_replay);
}

static void build_bands_begin(RMesh *m, const RCity *c, const RAtlasLevel *l, uint8_t mask_bit, int comp)
{
    memset(&s_bands_walk, 0, sizeof s_bands_walk);
    s_bands_walk.m = m, s_bands_walk.c = c, s_bands_walk.l = l, s_bands_walk.mask_bit = mask_bit, s_bands_walk.comp = comp;
    s_bands_walk.phase = net_band_replaying() ? 1 : 0;
}


int build_band_next(void)
{
    RMesh             *m        = s_bands_walk.m;
    const RCity       *c        = s_bands_walk.c;
    const uint8_t      mask_bit = s_bands_walk.mask_bit;
    const int          comp     = s_bands_walk.comp;
    while (s_bands_walk.phase < 3)
    {
        if (s_bands_walk.phase == 0)
        {
            const RSeg *r;
            int         i = s_bands_walk.i;
            if (i >= seg_table_count())
            {
                s_bands_walk.phase = 3;
                continue;
            }
            ++s_bands_walk.i;
            r = seg_table_entry(i);
            if (!r || !r->band)
                continue;
            if (band_replay(m, c, mask_bit, comp, i, r) != 0)
                return -1;
            return 1;
        }
        /*  The bands the script found, in the order it found them.
         *  Nothing is walked here: which cells make a band is
         *  scripts/compose/bands.lua's. */
        {
            const BandRun *run;
            int32_t      col, row;
            int          ew, sign;
            if (!net_band_disc_get(s_bands_walk.i, &run, &col, &row, &ew, &sign))
            {
                s_bands_walk.phase = 3;
                continue;
            }
            ++s_bands_walk.i;
            if (walk_band(m, c, mask_bit, comp, col, row, ew, sign, run) != 0)
                return -1;
            return 1;
        }
    }
    return 0;
}

int build_bands(RMesh *m, const RCity *c, const RAtlasLevel *l, uint8_t mask_bit, int comp)
{
    int32_t col, row;
    double  tp;
    tp = prof_now();
    band_free_air(c, l);
    net_prof_add(NET_PROF_BAND_AIR, prof_now() - tp), tp = prof_now();
    /* band_lanes ran before the junctions (mesh.c), so a junction knows the spur beside it */
    net_station_reset();
    net_bands_reset();
    build_bands_begin(m, c, l, mask_bit, comp);
    (void)col, (void)row;
    s_band_tp = tp;
    return 0;
}

/*  And what stands on the bands the drive has just had composed: every
 *  network tile tinted for outline mode, and the spurs. */
int build_band_spurs(RMesh *m, const RCity *c, const RAtlasLevel *l, uint8_t mask_bit, int comp)
{
    double tp = s_band_tp;
    net_prof_add(NET_PROF_BANDS, prof_now() - tp), tp = prof_now();
    /*  Every spur tile marked orange on the ground, for outline mode:
     *  the corridor markers' ground-highlight pipeline.  The shader
     *  shows it only while the grid is on. */
    {
        /*  And every network tile tinted with it.  Which byte takes
         *  which paint is the script's (`outline_tint`).  A spur tile
         *  keeps its orange, and a spur is one the pipeline found rather
         *  than a byte. */
        const unsigned char *tint = script_bytes("outline_tint");
        int32_t              i;
        for (i = 0; i < R_MAP * R_MAP; ++i)
        {
            float paint = lane_spur_tile(i % R_MAP, i / R_MAP) ? 8.0f : (float)tint[c->xbld[i]];
            if (paint > 0.0f && tile_highlight(m, c, mask_bit, i % R_MAP, i / R_MAP, paint) != 0)
                return -1;
        }
    }
    net_prof_add(NET_PROF_BAND_TINT, prof_now() - tp);
    build_spurs_begin(m, c, l, mask_bit, comp);
    s_band_tp = prof_now();
    return 0;
}

/*  And the bands' ends into the lines they become (lane.c), once the
 *  drive has carried the open ends across the meets. */
void *build_band_links(RMesh *m, const RCity *c, uint8_t mask_bit)
{
    return net_links_fan(m, c, mask_bit);
}

/*  And what follows the script joining them: every lane end goes
 *  somewhere and every start has something arriving. */
int build_band_links_done(void)
{
    double tp = s_band_tp;
    net_prof_add(NET_PROF_BAND_TRANS, prof_now() - tp), tp = prof_now();
    lane_check_ends();
    net_prof_add(NET_PROF_BAND_CHECK, prof_now() - tp);
    return 0;
}

/* ---- the loft's stages a slab or a spur supplies ---------------------------- */


/*  The slab stands clear (spec 7.2): 5 m under the soffit plus the
 *  girder is about 7.5 m to the line surface.  The vertical unit here is
 *  the altitude level, seven to eight meters.  So a little over one
 *  level, applied after the profile is settled so the slab follows the
 *  ground's shape while riding above it.  A slab is a structure, not a
 *  carpet: its support line may rise or fall no faster than a sixth of a
 *  level a tile.  So it runs straight over what the ground does under it
 *  and the columns take up the difference. */

/*  Station i: how far along it is, the height it stands at, and the
 *  ground under it. */
void band_prof_at(const ProfFan *p, int i, float *s_at, float *z, float *ground)
{
    const Sample *smp = (const Sample *)p->smp;
    *s_at             = smp[i].s;
    *z                = smp[i].z;
    *ground           = smp[i].z;
}

/*  Station i: where it is and which way it runs.  A rule shaping the
 *  heights can look at the map beyond the strip's own ends with it. */
void band_prof_pose(const ProfFan *p, int i, float *x, float *y, float *dx, float *dy)
{
    const Sample *smp = (const Sample *)p->smp;
    *x                = smp[i].pos.x;
    *y                = smp[i].pos.y;
    *dx               = smp[i].dir.x;
    *dy               = smp[i].dir.y;
}

void band_prof_set(ProfFan *p, int i, float z)
{
    ((Sample *)p->smp)[i].z = z;
}

/*  A band strip's elevation: arc.rules.profile lays it out. */
static int band_profile(Loft *x)
{
    static ProfFan s_prof;
    ProfFan        p;
    int            ns = x->ns;
    memset(&p, 0, sizeof p);
    p.smp        = x->smp;
    p.n          = ns;
    p.total      = x->total;
    p.spur       = x->d->struct_ != 0;
    p.lane_piece = x->d->lane_piece != 0;
    p.lane_off   = x->d->lane_off != 0;
    p.flat       = x->d->flat != 0;
    p.z0         = x->d->z0;
    p.spur0      = x->d->spur0;
    p.spur1      = x->d->spur1;
    p.grade      = s_tune.band_grade;
    p.stiff      = s_tune.band_stiff;
    p.lift       = BAND_LIFT;
    s_prof       = p;
    net_stage_hand(&s_prof, "profile");
    return 0;
}

/*  The lane drop: what a spur took from the slab, station by station.
 *  And the stations themselves, for the spurs built after the bands.
 *  Both read the slab's heights, so both wait until the rule that shapes
 *  them has answered. */
static int band_profile_done(Loft *x)
{
    if (x->d->struct_)
        return 0;
    band_lane_stations(x->smp, x->ns);
    return 0;
}

/* ---- the band as a family --------------------------------------------- */

/*  A slab records its edge in the traffic's graph under a class of its
 *  own.  It never takes whatever the last line walked left behind, so
 *  its cars use both lanes.  It has no margin and no stripe. */
/*  What this file lends the declarations: the slab's stages.  The band
 *  is walked by its own bands, not by tile family (its tiles are the
 *  line's).  So only what the loft asks a family for is here. */
void band_primitives(void)
{
    net_hook_add_split2(NH_PROFILE, "slab_profile", (NetHookFn)band_profile, (NetHookFn)band_profile_done, "profile", "drop");
}
