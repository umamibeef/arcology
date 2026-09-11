/*  The path fit: a segment's tiles become pieces.  Runs, arcs, biarcs.
 *  That stay inside the corridor the family allows. */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "dump.h"
#include "mesh/internal.h"
#include "pipeline.h"
#include "opt.h"

/*  The strip being lofted, as loft() was asked for it (RLoft).  Read by
 *  the loft's helpers through s_ld.  Outside a loft it holds zeros, a
 *  plain line strip. */

/* ---- stage one: the corridor ------------------------------------------- */

/*  A tile of a corridor, and the gate on its far side. */
typedef struct
{
    int32_t col, row;
} Cell;

/* ---- stage two: the taut path ------------------------------------------ */

/*  Does a band of half-width `hw` along a→b stay on the corridor? */
static int swept_fits(const uint8_t *mark, V2 a, V2 b, float hw)
{
    float dx = b.x - a.x, dy = b.y - a.y;
    float len = sqrtf(dx * dx + dy * dy), px, py;
    int   sm, ns = 6, side;
    if (len < 1e-5f)
        return 1;
    px = -dy / len;
    py = dx / len;
    for (sm = 0; sm <= ns; ++sm)
    {
        float t  = (float)sm / (float)ns;
        float qx = a.x + dx * t, qy = a.y + dy * t;
        for (side = -1; side <= 1; side += 2)
        {
            float   ex = qx + px * hw * (float)side, ey = qy + py * hw * (float)side;
            int32_t tc = (int32_t)floorf(ex), tr = (int32_t)floorf(ey);
            if (tc < 0 || tr < 0 || tc >= R_MAP || tr >= R_MAP || !mark[tr * R_MAP + tc])
                return 0;
        }
    }
    return 1;
}

/*  Every corridor tile the band covers between a and b, stamped into
 *  `cov`.  Proves a node may be dropped: the merged edge has to cover
 *  everything the two edges it replaces covered.  The tiles it stops
 *  covering come out bare. */
static void swept_cover(V2 a, V2 b, float hw, uint8_t *cov, uint8_t stamp)
{
    float dx = b.x - a.x, dy = b.y - a.y;
    float len = sqrtf(dx * dx + dy * dy), px, py;
    int   sm, ns, side;
    if (len < 1e-5f)
        return;
    px = -dy / len;
    py = dx / len;
    ns = (int)(len * 8.0f) + 2;
    for (sm = 0; sm <= ns; ++sm)
    {
        float t  = (float)sm / (float)ns;
        float qx = a.x + dx * t, qy = a.y + dy * t;
        for (side = -2; side <= 2; ++side)
        {
            float   ex = qx + px * hw * 0.5f * (float)side;
            float   ey = qy + py * hw * 0.5f * (float)side;
            int32_t tc = (int32_t)floorf(ex), tr = (int32_t)floorf(ey);
            if (tc >= 0 && tr >= 0 && tc < R_MAP && tr < R_MAP)
                cov[tr * R_MAP + tc] = stamp;
        }
    }
}

/*  Does a->b stray only onto tiles already stamped?  */

/*  ------------------------------------------------------------------
 *  The corridor fit as it is actually solved elsewhere.
 *
 *  The polyline-and-fillet pipeline decides curvature locally and after
 *  the fact: each corner asks for the widest arc that happens to fit
 *  between its two edges.  When none does the corner is emitted hard.
 *  Every symptom that follows is that choice coming back.  Nodes lie too
 *  close to sweep.  A radius floor rises with the band, and corners are
 *  left with no legal arc.
 *
 *  It is solved.  The standard method is safe-corridor optimization.
 *  Put the path in a basis whose CONTROL POINTS bound the curve, by the
 *  convex hull property of a uniform cubic B-spline.  Then box each
 *  control point into the corridor, and minimize curvature.  Containment
 *  stops being a test applied afterwards and becomes a constraint on
 *  points.  Curvature is continuous by construction.  So there is no
 *  such thing as a hard corner in the result.  Quadrotor and
 *  self-driving planners fit paths this way.  The civil engineering
 *  equivalent is the tangent-spiral-arc alignment line design has used
 *  for a century.
 *
 *  Minimising the discrete bending energy sum|p[i-1] - 2p[i] + p[i+1]|^2
 *  makes the stationary condition a fourth difference of zero, so one sweep
 *  of projected Gauss-Seidel is
 *
 *  p[i] <- box( (4(p[i-1] + p[i+1]) - (p[i-2] + p[i+2])) / 6 )
 *
 *  which is a few dozen iterations of arithmetic per segment and needs no
 *  solver, no global system and no per-corner search.
 *  ------------------------------------------------------------------ */

static float turn_radius(V2 a, V2 b, V2 c);

/*  Whether the last path fitted took the spline.  Under the spline fit
 *  the node marks are drawn ONLY for the segments that did not.  So the
 *  overlay shows at a glance which runs the spline fit did not take, and
 *  so where a harsh turn that remains must be. */

/*  The corridor's own convex regions, one per span of the curve.  The
 *  convex hull property bounds a span by its four control points.  So a
 *  span is safe exactly when all four lie in one CONVEX piece of the
 *  corridor.  Boxing each point into its own tile proves nothing about
 *  the curve between them.  This was why nine spans in ten escaped and
 *  had to be thrown away.  The corridor here is a run of unit tiles.  So
 *  the convex pieces are the solid rectangles it contains.  They are the
 *  four tiles' bounding rectangle when the corridor fills it.  Otherwise
 *  they are the middle tile alone, which forces the span's points
 *  together and turns the corner tightly but legally. */
/*  ==================================================================
 *  The corridor fit, by spline
 *
 *  Control points boxed into convex pieces of the corridor, curvature
 *  minimised under tension toward the taut line, the minimum radius
 *  imposed, and the curve checked as a swept band before it is accepted.
 *  Off by default.  The 'spline fit' knob turns it on.
 *  ================================================================== */
/*  The point where the line through a in direction da meets the line
 *  through b in direction db.  0 when they are parallel. */
int line_meet(V2 a, V2 da, V2 b, V2 db, V2 *out)
{
    float den = v2cross(da, db);
    float t;
    if (fabsf(den) < 1e-5f)
        return 0;
    t      = v2cross((V2){b.x - a.x, b.y - a.y}, db) / den;
    out->x = a.x + da.x * t;
    out->y = a.y + da.y * t;
    return 1;
}

/*  ==================================================================
 *  The corridor fit, by tangents
 *
 *  A line is straight lines joined by arcs: what the spec sets out in
 *  3.8 to 3.10.  Diagonals optimize to diagonals.  The effort goes into
 *  the smaller connections.
 *
 *  The fit classifies first and fits only the joins.  A fit that treats
 *  every tile as a free node and smooths makes a straight corridor
 *  wobble.  Relaxation and spline both do it, and turns a corner into a
 *  chain of small kinks.  Nothing in it knows what a straight IS.
 *
 *  runs    the step sequence is cut into straights (two or more equal
 *          steps), regular slopes (N,E,N,E or N,N,E,N,N,E: two full
 *          periods or more, spec 3.10 step 4) and gaps.  A small dynamic
 *          program picks the cut that covers the most steps with the
 *          fewest runs, so a slope is not nibbled into sawtooth by the
 *          short straights inside it.
 *  lines   every run is a line, and it does not bend: a straight through
 *          its tile centers, the middle of the corridor.  A slope as the
 *          exact midline of its staircase.
 *  joins   where two lines cross, one arc at the meet: the widest
 *          whose BAND stays on the corridor, taking its tangent length
 *          from both lines.  The straight gives ground so the arc can be
 *          legal.  Where two lines are parallel: a jog, a U-turn: an
 *          equal-tangent biarc, its radii growing with how far back the
 *          tangent points sit.  A gap no direct curve can be held in
 *          falls back to the tile-center polyline, there and only there.
 *
 *  Containment is the hard rule.  A join with no legal radius takes the
 *  tightest arc that fits rather than a kink, and is counted as tight, so
 *  the number is read instead of the picture being judged.  The output is
 *  the piece builder's own contract: vertices, a radius each, and a
 *  tangent budget each: so the loft, the junctions, the plan view and
 *  the curve metric see nothing new.
 *  ================================================================== */
#define TF_MIN_STRAIGHT 2     /* steps: a straight has a straight tile inside it   */
#define TF_MIN_PERIODS  2     /* spec 3.10: two full periods before a stair is a line */
#define TF_MAX_RUNS     (MAX_PTS / 2)

/*  The fit works on a chain of POINTS and the steps between them.  For a
 *  line the points are tile centers and every step is a unit step on the
 *  grid.  For a band they are the band's seam points and block centers.
 *  A step may be two tiles along, or two along and two across between
 *  the blocks of a diagonal chain.  Nothing below cares which: a
 *  straight is equal steps, a slope is two perpendicular steps in a
 *  regular pattern, and a line is a line. */
static int tf_same(V2 a, V2 b)
{
    return fabsf(a.x - b.x) < 1e-4f && fabsf(a.y - b.y) < 1e-4f;
}

static int tf_perp(V2 a, V2 b)
{
    return fabsf(a.x * b.x + a.y * b.y) < 1e-4f;
}

static unsigned long s_tf_probes; /* how many corridor samples one build takes */
static int gix_fit_straight_dot = -1, gix_fit_edge = -1, gix_fit_edge_slab = -1;
static float s_tf_edge; /* how far inside the band's edge the corridor is sampled (arc.geo.fit_edge) */

/*  Does a straight line's band hold on the corridor from a to b?  Every
 *  quarter tile, the center and both edges. */
static int tf_line_holds(const uint8_t *mark, V2 a, V2 b, float hw)
{
    ++s_tf_probes;
    float dx = b.x - a.x, dy = b.y - a.y, len = sqrtf(dx * dx + dy * dy), px, py;
    int   m, k, sd;
    if (len < 1e-5f)
        return 1;
    px = -dy / len;
    py = dx / len;
    {
        static int gix_fit_probe_run = -1;
        float      st = geo_num(&gix_fit_probe_run, "fit_probe_run");
        m             = (int)(len / (st > 1e-6f ? st : 0.25f)) + 2;
    }
    for (k = 0; k <= m; ++k)
    {
        float t = (float)k / (float)m, qx = a.x + dx * t, qy = a.y + dy * t;
        for (sd = -1; sd <= 1; ++sd)
        {
            int32_t tc = (int32_t)floorf(qx + px * (hw - s_tf_edge) * (float)sd), tr = (int32_t)floorf(qy + py * (hw - s_tf_edge) * (float)sd);
            if (tc < 0 || tr < 0 || tc >= R_MAP || tr >= R_MAP || !mark[tr * R_MAP + tc])
                return 0;
        }
    }
    return 1;
}

static void tf_line(const V2 *pts, Run *r);
static V2   tf_onto(const Run *r, V2 q);

static const uint8_t *s_tf_own;        /* the tiles that must end up under the band.  NULL: none need to */
static int            s_tf_cover_runs; /* ... held by the runs and the corner joins too, not the arcs alone: the band asks for it */
static int            s_tf_free_lines; /* a run may be any span whose chord, shifted midway across its covered points, holds and covers: the band asks for it */
static int32_t        s_tf_ex0, s_tf_ex1; /* the junction tiles at the chain's ends, -1 for none: defined with the arc coverage below */

/*  A point's distance to the segment ab. */
static float seg_dist(V2 p, V2 a, V2 b)
{
    V2    ab = {b.x - a.x, b.y - a.y};
    float l2 = ab.x * ab.x + ab.y * ab.y, t = l2 > 1e-9f ? ((p.x - a.x) * ab.x + (p.y - a.y) * ab.y) / l2 : 0.0f;
    t = t < 0.0f ? 0.0f : t > 1.0f ? 1.0f
                                   : t;
    return v2len((V2){p.x - a.x - ab.x * t, p.y - a.y - ab.y * t});
}

/*  Does a corner join keep every covered point of the gap it spans under
 *  the band?  The points between the two runs have tiles that are the
 *  band's own.  Each must lie within the band's half width of one of
 *  the corner's two legs.  Without the rule a band staircase with a
 *  spur pinned to it joins by an L that misses the spur's own cells. */
static int tf_gap_covers(const V2 *pts, int first, int last, V2 a, V2 corner, V2 b, float band)
{
    ++s_tf_probes;
    int k;
    if (!s_tf_own || !s_tf_cover_runs)
        return 1;
    for (k = first; k <= last; ++k)
    {
        int32_t tc = (int32_t)floorf(pts[k].x), tr = (int32_t)floorf(pts[k].y);
        if (tc < 0 || tr < 0 || tc >= R_MAP || tr >= R_MAP || !s_tf_own[tr * R_MAP + tc])
            continue;
        if (seg_dist(pts[k], a, corner) > band && seg_dist(pts[k], corner, b) > band)
            return 0;
    }
    return 1;
}

/*  Does a run's line keep every covered point under the band?  A point
 *  whose tile is the band's own must lie within the band's half width of
 *  the line.  Without it a run straightens its way off its own cells.
 *  A band staircase with a spur pinned to it then comes out as an L
 *  that misses the spur's cells. */
static int tf_run_covers(const V2 *pts, int i, int j, const Run *r, float band)
{
    ++s_tf_probes;
    int k;
    if (!s_tf_own || !s_tf_cover_runs)
        return 1;
    for (k = i; k <= j; ++k)
    {
        int32_t tc = (int32_t)floorf(pts[k].x), tr = (int32_t)floorf(pts[k].y);
        V2      q;
        if (tc < 0 || tr < 0 || tc >= R_MAP || tr >= R_MAP || !s_tf_own[tr * R_MAP + tc])
            continue;
        q = tf_onto(r, pts[k]);
        if (v2len((V2){q.x - pts[k].x, q.y - pts[k].y}) > band)
            return 0;
    }
    return 1;
}

/*  How far either side of the span's chord its covered points lie: the
 *  outermost offsets of them.  Nothing where the span has no covered
 *  point or no length at all. */
static int tf_spread(const V2 *pts, int i, int j, float *plo, float *phi)
{
    V2    d = {pts[j].x - pts[i].x, pts[j].y - pts[i].y}, nrm;
    float l = v2len(d), lo = 1e9f, hi = -1e9f;
    int   k;
    if (l < 1e-4f)
        return 0;
    d.x /= l, d.y /= l;
    nrm = (V2){-d.y, d.x};
    for (k = i; k <= j; ++k)
    {
        int32_t tc = (int32_t)floorf(pts[k].x), tr = (int32_t)floorf(pts[k].y);
        float   off;
        if (tc < 0 || tr < 0 || tc >= R_MAP || tr >= R_MAP || !s_tf_own || !s_tf_own[tr * R_MAP + tc])
            continue;
        off = (pts[k].x - pts[i].x) * nrm.x + (pts[k].y - pts[i].y) * nrm.y;
        lo  = off < lo ? off : lo, hi = off > hi ? off : hi;
    }
    if (lo > hi)
        return 0;
    *plo = lo;
    *phi = hi;
    return 1;
}

/*  The free line the span i..j is given: its chord's direction, moved
 *  across by `off`. */
static void tf_chord(const V2 *pts, int i, int j, float off, Run *r)
{
    V2    d = {pts[j].x - pts[i].x, pts[j].y - pts[i].y}, nrm;
    float l = v2len(d);
    d.x /= l, d.y /= l;
    nrm    = (V2){-d.y, d.x};
    r->i0 = i, r->i1 = j - 1, r->kind = 2, r->period = 0, r->ta = i, r->tb = j;
    r->d = d;
    r->p = (V2){pts[i].x + nrm.x * off, pts[i].y + nrm.y * off};
    r->da = d, r->db = r->p;
}

int path_run_perp(const RunFan *x, int a, int b)
{
    return tf_perp(x->st[a], x->st[b]);
}

/*  The slope the script found reaching from step i: how far it runs, how
 *  many steps one of its periods takes.  Which step is the majority and
 *  which the minority.  Both are named by the step they repeat. */
void path_run_slope(RunFan *x, int i, int len, int period, int major, int minor)
{
    x->slen[i] = len;
    x->sper[i] = period;
    x->sda[i]  = x->st[major];
    x->sdb[i]  = x->st[minor];
}

/*  The offsets of a span's covered points either side of its chord, and
 *  the chord the script settles on. */
int path_run_spread(const RunFan *x, int i, int j, float *lo, float *hi)
{
    return tf_spread(x->pts, i, j, lo, hi);
}

void path_run_chord(RunFan *x, int i, int j, float off)
{
    tf_chord(x->pts, i, j, off, &x->cand);
}

/*  A span the script refused before it was ever sampled, for
 *  --path-dump. */
void path_run_note(const RunFan *x, int i, int j, const char *why)
{
    if (g_dev.path_dump && j - i >= 3)
        dumpf("FREE %d..%d (%.2f,%.2f)-(%.2f,%.2f): %s\n", i, j, (double)x->pts[i].x, (double)x->pts[i].y, (double)x->pts[j].x, (double)x->pts[j].y, why);
}

/*  Whether the candidate span i..j stands.  A slope is a line only if
 *  its band holds on the corridor along it.  For a line the corridor is
 *  its own tiles.  So this is the inscribed width of its staircase.  It
 *  is tile/root 2 at 45 degrees, and tile/root 5 at 2:1 (spec 3.9).  A
 *  line's band on a 2:1 stair overhangs every inner corner: such a span
 *  stays short straights and the S-curves between them.  For a viaduct
 *  the corridor is free air, and the same test lets a chain of blocks be
 *  the diagonal it is. */
int path_run_try(RunFan *x, int i, int j, int kind)
{
    Run *tmp = &x->cand;
    int  len = j - i;
    if (kind != 2)
    {
        tmp->i0     = i;
        tmp->i1     = j - 1;
        tmp->kind   = 1;
        tmp->da     = x->sda[i];
        tmp->db     = x->sdb[i];
        tmp->period = x->sper[i];
        tmp->ta     = i;
        tmp->tb     = j;
        tf_line(x->pts, tmp);
    }
    if (!tf_line_holds(x->mark, tf_onto(tmp, x->pts[i]), tf_onto(tmp, x->pts[j]), x->band))
    {
        if (kind == 2 && g_dev.path_dump && len >= 3)
            dumpf("FREE %d..%d (%.2f,%.2f)-(%.2f,%.2f): off the corridor\n", i, j, (double)x->pts[i].x, (double)x->pts[i].y, (double)x->pts[j].x, (double)x->pts[j].y);
        return 0;
    }
    if (!tf_run_covers(x->pts, i, j, tmp, x->band))
    {
        if (kind == 2 && g_dev.path_dump && len >= 3)
            dumpf("FREE %d..%d (%.2f,%.2f)-(%.2f,%.2f): a covered point off the band\n", i, j, (double)x->pts[i].x, (double)x->pts[i].y, (double)x->pts[j].x, (double)x->pts[j].y);
        return 0;
    }
    return 1;
}

/*  The candidate just tested wins the prefix ending at j. */
void path_run_keep(RunFan *x, int j)
{
    x->won[j] = x->cand;
}

/*  A run of the answer: the span i..j, as the kind the script settled
 *  on.  A straight is its own repeated step.  A slope and a free line
 *  are the candidate that won the prefix. */
void path_run_emit(RunFan *x, int i, int j, int kind)
{
    Run *r;
    if (x->nr >= x->cap)
        return;
    r         = &x->runs[x->nr++];
    r->i0     = i;
    r->i1     = j - 1;
    r->kind   = kind;
    r->da     = kind ? x->won[j].da : x->st[i];
    r->db     = kind ? x->won[j].db : (V2){0, 0};
    r->period = kind ? x->won[j].period : 0;
    r->ta     = r->i0;
    r->tb     = r->i1 + 1;
}

/*  The script names the runs walking the chain back, so they come out
 *  last first: put them in order along the segment. */
void path_run_order(RunFan *x)
{
    int lo, hi;
    for (lo = 0, hi = x->nr - 1; lo < hi; ++lo, --hi)
    {
        Run t       = x->runs[lo];
        x->runs[lo] = x->runs[hi];
        x->runs[hi] = t;
    }
}

/*  Cut the steps into runs.  The slope reaching from each step is found
 *  once.  Which spans become runs is arc.rules.runs. */
static void tf_runs(const V2 *st, const V2 *pts, int ns, float band, const uint8_t *mark, Run *runs, int cap, RunFan *out)
{
    static int slen[MAX_PTS], sp[MAX_PTS], code[MAX_PTS], moves[MAX_PTS];
    static V2  sda[MAX_PTS], sdb[MAX_PTS];
    static Run won[MAX_PTS + 1];
    RunFan     x;
    int        i, j;
    if (ns > MAX_PTS)
        ns = MAX_PTS;
    /*  Each step named by the first step it is the same as, so the script
     *  can read the pattern of them without comparing vectors. */
    for (i = 0; i < ns; ++i)
    {
        moves[i] = v2len(st[i]) > 1e-4f;
        code[i]  = i;
        for (j = 0; j < i; ++j)
            if (code[j] == j && tf_same(st[j], st[i]))
            {
                code[i] = j;
                break;
            }
    }
    memset(slen, 0, sizeof slen);
    memset(sp, 0, sizeof sp);
    memset(&x, 0, sizeof x);
    x.st         = st;
    x.pts        = pts;
    x.mark       = mark;
    x.ns         = ns;
    x.cap        = cap;
    x.band       = band;
    x.free_lines = s_tf_free_lines;
    x.ex0        = s_tf_ex0 >= 0;
    x.ex1        = s_tf_ex1 >= 0;
    x.slen       = slen;
    x.sper       = sp;
    x.sda        = sda;
    x.sdb        = sdb;
    x.code       = code;
    x.moves      = moves;
    x.won        = won;
    x.runs       = runs;
    *out = x;
}

/*  The line a run is.  A straight runs through its points.  A slope is
 *  the midline of its staircase, exact and not least-squares (spec 3.10
 *  step 4).  Its direction is the period's steps summed, and it sits
 *  halfway between the outermost step midpoints across it.  For a line
 *  those are the gate midpoints. */
static void tf_line(const V2 *pts, Run *r)
{
    if (r->kind == 2)
    {
        r->d = r->da; /* a free line: its direction and point were settled when it was found */
        r->p = r->db;
        return;
    }
    if (r->kind == 0)
    {
        float l = v2len(r->da);
        r->d    = (V2){r->da.x / l, r->da.y / l};
        r->p    = pts[r->ta];
        return;
    }
    {
        float k = (float)(r->period - 1);
        V2    d = {k * r->da.x + r->db.x, k * r->da.y + r->db.y};
        float l = v2len(d), lo = 1e9f, hi = -1e9f;
        V2    nrm, g0;
        int   s;
        d.x /= l;
        d.y /= l;
        nrm = (V2){-d.y, d.x};
        g0  = pts[r->i0];
        for (s = r->i0; s <= r->i1; ++s)
        {
            /* the midpoint of this step: for a line, the gate it crosses */
            V2    g = {0.5f * (pts[s].x + pts[s + 1].x), 0.5f * (pts[s].y + pts[s + 1].y)};
            float off;
            if (s == r->i0)
                g0 = g;
            off = (g.x - g0.x) * nrm.x + (g.y - g0.y) * nrm.y;
            if (off < lo)
                lo = off;
            if (off > hi)
                hi = off;
        }
        r->d = d;
        r->p = (V2){g0.x + nrm.x * 0.5f * (lo + hi), g0.y + nrm.y * 0.5f * (lo + hi)};
    }
}

/*  A point projected onto a run's line. */
static V2 tf_onto(const Run *r, V2 q)
{
    float u = (q.x - r->p.x) * r->d.x + (q.y - r->p.y) * r->d.y;
    return (V2){r->p.x + r->d.x * u, r->p.y + r->d.y * u};
}

/*  Does the band of an arc stay on the corridor?  Sampled every
 *  twentieth of a tile along it, the center and both edges: the edges by
 *  the radial.  This iS the band's normal on an arc.  This is the band
 *  itself, not the square around a point that corridor_holds inscribes.
 *  This asks for hw * root two of room on a diagonal and refused arcs
 *  that fitted. */
static V2      s_tf_fail; /* the sample that refused the last arc, for --sweep-probe */
static int     s_tf_fail_side;
static int32_t s_tf_ex0 = -1, s_tf_ex1 = -1; /* the junction tiles: theirs is the junction's surface */

/*  Coverage.  An arc cuts inside the corner it replaces.  If it cuts far
 *  enough it leaves the corner tile with no line over it at all.  The
 *  shape that does it is a V: two 45 degree arms meeting at a right
 *  angle, whose bottom tile comes out bare.
 *
 *  The rule is the one fit_spacing kept when it merged nodes: what the
 *  arc replaces covered these tiles, so the arc must too.  The stubs are
 *  stamped with one mark, and the arc's band over them with the next.
 *  Any corridor tile still wearing the first is one the arc abandoned. */
static uint8_t s_tf_cov[R_MAP * R_MAP];
static uint8_t s_tf_stamp;

static void tf_stamp_arc(V2 cen, float R, float a0, float sweep, float hw, uint8_t stamp)
{
    static int gix_fit_probe_arc = -1;
    float      step = geo_num(&gix_fit_probe_arc, "fit_probe_arc");
    int m = (int)(fabsf(sweep) * R / (step > 1e-6f ? step : 0.05f)) + 3, k, s;
    for (k = 0; k <= m; ++k)
    {
        float ang = a0 + sweep * (float)k / (float)m;
        float cx = cosf(ang), sy = sinf(ang);
        for (s = -2; s <= 2; ++s)
        {
            float   rr = R + hw * 0.5f * (float)s;
            int32_t tc = (int32_t)floorf(cen.x + cx * rr), tr = (int32_t)floorf(cen.y + sy * rr);
            if (tc >= 0 && tr >= 0 && tc < R_MAP && tr < R_MAP)
                s_tf_cov[tr * R_MAP + tc] = stamp;
        }
    }
}

/*  Does the arc cover every corridor tile the stubs a->b and b->c did?
 *  `hw` here is the band's own half width, not the margin-padded one.
 *  What has to be covered is what the band will actually paint. */

static int tf_arc_covers(const uint8_t *mark, V2 a, V2 b, V2 c, V2 cen, float R, float a0, float sweep, float hw)
{
    ++s_tf_probes;
    uint8_t pair, arc;
    int32_t i;
    (void)mark;
    if (!s_tf_own)
        return 1;
    if (s_tf_stamp > 250)
    {
        memset(s_tf_cov, 0, sizeof s_tf_cov);
        s_tf_stamp = 0;
    }
    pair = ++s_tf_stamp;
    swept_cover(a, b, hw, s_tf_cov, pair);
    swept_cover(b, c, hw, s_tf_cov, pair);
    arc = ++s_tf_stamp;
    tf_stamp_arc(cen, R, a0, sweep, hw, arc);
    /* the stubs' tiles: those on the corridor still wearing the pair's mark */
    {
        int32_t c0 = (int32_t)floorf(fminf(fminf(a.x, b.x), c.x)) - 1, c1 = (int32_t)floorf(fmaxf(fmaxf(a.x, b.x), c.x)) + 1;
        int32_t r0 = (int32_t)floorf(fminf(fminf(a.y, b.y), c.y)) - 1, r1 = (int32_t)floorf(fmaxf(fmaxf(a.y, b.y), c.y)) + 1;
        int32_t tc, tr;
        for (tr = r0 < 0 ? 0 : r0; tr <= r1 && tr < R_MAP; ++tr)
            for (tc = c0 < 0 ? 0 : c0; tc <= c1 && tc < R_MAP; ++tc)
            {
                i = tr * R_MAP + tc;
                if (s_tf_own[i] && s_tf_cov[i] == pair && i != s_tf_ex0 && i != s_tf_ex1)
                    return 0;
            }
    }
    return 1;
}

static int tf_arc_holds(const uint8_t *mark, V2 cen, float R, float a0, float sweep, float hw)
{
    ++s_tf_probes;
    static int gix_fit_probe_arc = -1;
    float      step = geo_num(&gix_fit_probe_arc, "fit_probe_arc");
    int m = (int)(fabsf(sweep) * R / (step > 1e-6f ? step : 0.05f)) + 3, k, s;
    for (k = 0; k <= m; ++k)
    {
        float ang = a0 + sweep * (float)k / (float)m;
        float cx = cosf(ang), sy = sinf(ang);
        for (s = -1; s <= 1; ++s)
        {
            /*  The edges a hair inside: a slab two tiles wide has its
             *  edge exactly on the next tile's boundary.  Floorf there
             *  reads the tile next door. */
            float   rr = R + (hw - s_tf_edge) * (float)s;
            float   x = cen.x + cx * rr, y = cen.y + sy * rr;
            int32_t tc = (int32_t)floorf(x), tr = (int32_t)floorf(y);
            if (tc < 0 || tr < 0 || tc >= R_MAP || tr >= R_MAP || !mark[tr * R_MAP + tc])
            {
                s_tf_fail      = (V2){x, y};
                s_tf_fail_side = s;
                return 0;
            }
        }
    }
    return 1;
}

/*  The widest arc a corner may sweep inside its corridor, given the
 *  tangent length it may take from its edges.  Searched finely from the
 *  ceiling down to the floor an arc still reads as one.  The first that
 *  holds is the answer.  Sets `tight` when the answer is under the
 *  minimum radius, and returns 0, a corner, when nothing holds. */
/*  Does an arc of this radius sit in the corner and stay on the
 *  corridor.  Does it still cover the tiles the corner was drawn for?
 *  An arc cuts inside the corner it replaces.  If it cuts far enough it
 *  leaves the corner tile with no line over it at all. */
int path_sweep_holds(SweepFan *s, float r)
{
    float d   = r * s->tan_half;
    V2    t1  = {s->b.x - s->ui.x * d, s->b.y - s->ui.y * d};
    V2    nrm = s->cross > 0.0f ? (V2){-s->ui.y, s->ui.x} : (V2){s->ui.y, -s->ui.x};
    V2    cen = {t1.x + nrm.x * r, t1.y + nrm.y * r};
    float a0  = atan2f(t1.y - cen.y, t1.x - cen.x);
    float sw  = s->cross > 0.0f ? s->theta : -s->theta;
    if (!tf_arc_holds(s->mark, cen, r, a0, sw, s->hw))
    {
        if (s->probe)
            dumpf("PROBE %.3f,%.3f r %.3f refused by %s sample %.3f,%.3f (tile %d,%d)\n", (double)s->b.x, (double)s->b.y, (double)r,
                  s_tf_fail_side < 0 ? "inner" : s_tf_fail_side > 0 ? "outer"
                                                                   : "centre",
                  (double)s_tf_fail.x, (double)s_tf_fail.y, (int)floorf(s_tf_fail.x), (int)floorf(s_tf_fail.y));
        return 0;
    }
    {
        V2 t2 = {s->b.x + s->uo.x * d, s->b.y + s->uo.y * d};
        if (!tf_arc_covers(s->mark, t1, s->b, t2, cen, r, a0, sw, s->hw - s_tune.margin))
        {
            if (s->probe)
                dumpf("PROBE %.3f,%.3f r %.3f holds but leaves a tile bare\n", (double)s->b.x, (double)s->b.y, (double)r);
            return 0;
        }
    }
    if (s->probe)
        dumpf("PROBE %.3f,%.3f r %.3f holds (tlim %.3f, %.0f deg, band %.3f)\n", (double)s->b.x, (double)s->b.y, (double)r, (double)s->tlim, (double)(s->theta * 57.2958f), (double)s->hw);
    return 1;
}

/*  The radius the corner is given, and whether it came out under the
 *  minimum. */
void path_sweep_answer(SweepFan *s, float r, int tight)
{
    s->r     = r;
    s->tight = tight;
}

/*  The largest radius a fillet at this corner may have and still hold
 *  inside the band.  Arc.rules.sweep searches for it.  The switch
 *  --sweep-probe X,Y prints one corner's search at the vertex there,
 *  every radius tried and the sample that refused it.  How a "why is
 *  this corner tight" is answered, rather than by reasoning about it. */
static SweepFan s_sweep;

SweepFan *path_sweep_ask(const void *markv, V2 a, V2 b, V2 c, float tlim, float rmax, float rmin, float hw)
{
    const uint8_t *mark = (const uint8_t *)markv;
    SweepFan       s;
    V2       ui = {b.x - a.x, b.y - a.y}, uo = {c.x - b.x, c.y - b.y};
    float    li = v2len(ui), lo = v2len(uo), dot, px, py;
    memset(&s, 0, sizeof s);
    s.mark   = mark;
    s.b      = b;
    s.tlim   = tlim;
    s.rmax   = rmax;
    s.rmin   = rmin;
    s.hw     = hw;
    s.margin = s_tune.margin;
    s.padded = s_tf_own != NULL;
    s.probe  = g_dev.sweep_probe && sscanf(g_dev.sweep_probe, "%f,%f", &px, &py) == 2 && fabsf(px - b.x) < 0.01f && fabsf(py - b.y) < 0.01f;
    if (li < 1e-5f || lo < 1e-5f || tlim <= 1e-4f)
        s.straight = 1;
    else
    {
        ui.x /= li;
        ui.y /= li;
        uo.x /= lo;
        uo.y /= lo;
        dot      = ui.x * uo.x + ui.y * uo.y;
        s.ui     = ui;
        s.uo     = uo;
        s.cross  = ui.x * uo.y - ui.y * uo.x;
        s.straight = dot > geo_num(&gix_fit_straight_dot, "fit_straight_dot");
        s.theta    = acosf(dot < -1.0f ? -1.0f : dot);
        s.tan_half = tanf(0.5f * s.theta);
    }
    s_sweep = s;
    return &s_sweep;
}

/*  And what the search came to: the radius, and whether it had to go
 *  under the minimum the family asks for. */
float path_sweep_take(int *tight)
{
    if (tight)
        *tight = s_sweep.tight;
    return s_sweep.r;
}

/*  The equal-tangent biarc from (A, t0) to (B, t1): two fillets with
 *  the same tangent length d, at C0 = A + d t0 and C1 = B - d t1, whose
 *  shared edge is exactly 2d: which is precisely the shape the piece
 *  builder makes from two vertices, so a biarc is nothing new to it.
 *  |v - d s|^2 = 4 d^2 with v = B - A and s = t0 + t1 gives
 *  2(1-c) d^2 + 2 (v.s) d - v.v = 0, c = t0.t1.  Returns 0 when there is
 *  no such biarc, which is B behind A. */
int tf_biarc(V2 A, V2 t0, V2 B, V2 t1, V2 *c0, V2 *c1, float *dout)
{
    V2    v = {B.x - A.x, B.y - A.y}, s = {t0.x + t1.x, t0.y + t1.y};
    float c = t0.x * t1.x + t0.y * t1.y, vs = v.x * s.x + v.y * s.y, vv = v.x * v.x + v.y * v.y;
    float k = 2.0f * (1.0f - c), d;
    if (k < 1e-5f)
    {
        if (vs <= 1e-6f)
            return 0;
        d = vv / (2.0f * vs);
    }
    else
        d = (-vs + sqrtf(vs * vs + k * vv)) / k;
    if (d <= 1e-4f)
        return 0;
    *c0   = (V2){A.x + t0.x * d, A.y + t0.y * d};
    *c1   = (V2){B.x - t1.x * d, B.y - t1.y * d};
    *dout = d;
    return 1;
}

/*  Both arcs of a biarc, as the piece builder will make them, held to
 *  the corridor.  Returns the smaller radius, or -1 if either escapes. */
static float tf_biarc_holds(const uint8_t *mark, V2 prev, V2 c0, V2 c1, V2 next, float d, float hw)
{
    float rmin_ = 1e9f;
    int   k;
    for (k = 0; k < 2; ++k)
    {
        V2    a = k ? c0 : prev, b = k ? c1 : c0, c = k ? next : c1;
        V2    ui = {b.x - a.x, b.y - a.y}, uo = {c.x - b.x, c.y - b.y};
        float li = v2len(ui), lo = v2len(uo), dot, cross, theta, r;
        V2    t1, nrm, cen;
        if (li < 1e-5f || lo < 1e-5f)
            return -1.0f;
        ui.x /= li;
        ui.y /= li;
        uo.x /= lo;
        uo.y /= lo;
        dot   = ui.x * uo.x + ui.y * uo.y;
        cross = ui.x * uo.y - ui.y * uo.x;
        if (dot > geo_num(&gix_fit_straight_dot, "fit_straight_dot"))
            continue; /* no turn at this one: a straight through */
        theta = acosf(dot < -1.0f ? -1.0f : dot);
        r     = d / tanf(0.5f * theta);
        t1    = (V2){b.x - ui.x * d, b.y - ui.y * d};
        nrm   = cross > 0.0f ? (V2){-ui.y, ui.x} : (V2){ui.y, -ui.x};
        cen   = (V2){t1.x + nrm.x * r, t1.y + nrm.y * r};
        if (!tf_arc_holds(mark, cen, r, atan2f(t1.y - cen.y, t1.x - cen.x), cross > 0.0f ? theta : -theta, hw))
            return -1.0f;
        {
            V2 t2 = {b.x + uo.x * d, b.y + uo.y * d};
            if (!tf_arc_covers(mark, t1, b, t2, cen, r, atan2f(t1.y - cen.y, t1.x - cen.x), cross > 0.0f ? theta : -theta, hw - s_tune.margin))
                return -1.0f;
        }
        if (r < rmin_)
            rmin_ = r;
    }
    /*  And the leg between the two arcs, held as a run's line is.  The
     *  arcs alone were checked.  An S slid across a staircase then ran
     *  its middle over the spur tile beside it, which the corridor keeps
     *  out. */
    {
        V2    u = {c1.x - c0.x, c1.y - c0.y};
        float l = v2len(u);
        if (l > 2.0f * d + 1e-4f)
        {
            V2 a = {c0.x + u.x / l * d, c0.y + u.y / l * d}, b = {c1.x - u.x / l * d, c1.y - u.y / l * d};
            if (!tf_line_holds(mark, a, b, hw))
                return -1.0f;
        }
    }
    return rmin_;
}

#define TALLY_BUCKETS 3 /* the caller's own: nothing here knows what one means */

/*  What the fit did, summed over the build, printed under --mesh-check.
 *  The numbers are the judgment.  How many joins were swept legally, how
 *  many had to go tight, how many stayed corners. */
static struct
{
    int segments, straights, slopes, joins, biarcs, fallbacks, swept, tight, hard, jogs;
} s_tf_by[TALLY_BUCKETS], *s_tf_p = &s_tf_by[0];
static int s_tf_bucket; /* which of them this fit counts into: the caller's own */
#define s_tf (*s_tf_p)

/*  A family's tallies (0 line, 1 thread, 2 band).  They are saved and
 *  restored around a fit that may be discarded, because band.c fits a
 *  band two ways and keeps one. */
void path_fit_probes(void)
{
    dumpf("fit  %lu corridor samples\n", s_tf_probes);
}

void fit_tally_get(int bucket, void *dst, size_t cap)
{
    memcpy(dst, &s_tf_by[bucket], sizeof s_tf_by[bucket] < cap ? sizeof s_tf_by[bucket] : cap);
}

void fit_tally_set(int bucket, const void *src, size_t cap)
{
    memcpy(&s_tf_by[bucket], src, sizeof s_tf_by[bucket] < cap ? sizeof s_tf_by[bucket] : cap);
}

/*  Counted once per build: the network is walked twice, once to
 *  measure and once to draw, and the fit runs in both. */
/*  The fit's statistics count the pass that fits: the grading pass (or
 *  the one pass of a build without grading).  They had counted the
 *  building pass, which fits nothing now that the table replays every
 *  segment and band.  It had only ever seen the bands' second walk. */
static void tf_count(int *c)
{
    if (s_pass != 2)
        ++*c;
}

/*  The runs of the last fit, for the plan view's PRIM lines. */
static struct
{
    V2  a, b;
    int kind;
} s_tf_prims[TF_MAX_RUNS];
static int s_tf_nprims;

/*  What a corner asks of each edge for a unit radius: tan of half its
 *  turn.  Nothing for a straight vertex. */
static float tf_demand(V2 a, V2 b, V2 c)
{
    float la = v2len((V2){b.x - a.x, b.y - a.y}), lc = v2len((V2){c.x - b.x, c.y - b.y}), dot, theta;
    if (la < 1e-6f || lc < 1e-6f)
        return 0.0f;
    dot = ((b.x - a.x) * (c.x - b.x) + (b.y - a.y) * (c.y - b.y)) / (la * lc);
    if (dot > geo_num(&gix_fit_straight_dot, "fit_straight_dot"))
        return 0.0f;
    theta = acosf(dot < -1.0f ? -1.0f : dot);
    return tanf(0.5f * theta);
}

/*  The tangent an arc of the smallest legal radius needs at this corner.
 *  What an end edge does with that.  Whether it keeps its approach or
 *  gives way to the arc is arc.end_budget's.  It answers -1 for a corner
 *  with no turn to it, which has no such arc. */
static float tf_need(V2 a, V2 b, V2 c, float rmin)
{
    V2    ui = {b.x - a.x, b.y - a.y}, uo = {c.x - b.x, c.y - b.y};
    float li = v2len(ui), lo = v2len(uo), dot;
    if (li < 1e-5f || lo < 1e-5f)
        return -1.0f;
    dot = (ui.x * uo.x + ui.y * uo.y) / (li * lo);
    return rmin * tanf(0.5f * acosf(dot > 1.0f ? 1.0f : dot < -1.0f ? -1.0f
                                                                    : dot));
}

/*  The fit's working state: what it was asked for, the lines it laid
 *  through the runs, and the vertices it has placed so far.  Each stage
 *  below reads what it needs off it and writes back what it changed. */
typedef struct
{
    const uint8_t *mark, *own;
    const V2      *pts;
    int            nt, ns;
    float          hw, band, appr, res0, res1, rmax, rmin, gro;
    V2             start, goal;
    int32_t        ex0, ex1;
    V2            *out;
    float         *rad, *tlim;
    int            cap;
    V2            *st;    /* the steps between the points */
    Run           *runs;  /* the runs the steps make */
    Run           *lines; /* the chain of lines: the start's, the runs', the goal's */
    float         *fixed; /* per vertex: a biarc vertex's tangent length, -1 for a searched one */
    int            nr, nl, n;
} Tf;

/*  One boundary between two lines of the chain, as the vertex stages
 *  see it: P's last tile, Q's first, and what lies before and after. */
typedef struct
{
    int        k;
    const Run *P, *Q;
    V2         prev, EP, SQ, after;
} TfPair;

/*  An end pulled onto a run's own angle.  A slope that owns the junction
 *  tile arrives at that angle.  The strip starts on the slope's line, at
 *  the point nearest the junction's center, and the junction shapes a
 *  skew arm (spec 3.9).  Held to the axis instead, the diagonal has to
 *  turn 45 degrees in the quarter tile between the gate and the mouth.
 *  That is the tightest arc in the whole network, at every diagonal
 *  junction. */
void path_chain_aim(ChainFan *c, int which)
{
    if (which == 0)
        c->start = tf_onto(&c->runs[0], c->pts[0]);
    else
        c->goal = tf_onto(&c->runs[c->nr - 1], c->pts[c->nt - 1]);
}

/*  The end already lies on that run's line, so a line of its own would
 *  be the same line twice. */
int path_chain_on_line(const ChainFan *c, int which)
{
    const Run *r = which == 0 ? &c->runs[0] : &c->runs[c->nr - 1];
    V2         e = which == 0 ? c->start : c->goal;
    return fabsf(v2cross(r->d, (V2){e.x - r->p.x, e.y - r->p.y})) < 1e-3f;
}

/*  An end's own line: through the end, along the step it leaves by. */
void path_chain_end(ChainFan *c, int which)
{
    Run   e;
    V2    st = which == 0 ? c->st0 : c->st1;
    float l  = v2len(st);
    memset(&e, 0, sizeof e);
    e.kind = 0;
    e.da   = st;
    e.d    = l > 1e-4f ? (V2){st.x / l, st.y / l} : (V2){0, 1};
    if (which == 0)
    {
        e.p  = c->start;
        e.i0 = e.i1 = -1;
        e.ta = e.tb = 0;
    }
    else
    {
        e.p  = c->goal;
        e.i0 = e.i1 = c->ns;
        e.ta = e.tb = c->nt - 1;
    }
    c->lines[c->nl++] = e;
}

/*  A run, as a line of the chain. */
void path_chain_run(ChainFan *c, int i)
{
    c->lines[c->nl++] = c->runs[i];
}

/*  The steps, and the runs through them.  The lines the runs lie on, and
 *  the chain of lines from the start's exit step to the goal's last. */
/*  The fit in hand, from the moment its corridor is set up to the
 *  moment its shape is closed: every stage below reads it. */
static Tf     s_path;
static TfPair s_path_pair;
static V2     s_path_end; /* the far line's own end, where the rule keeps that over the meet */
static int    s_path_ready;

/*  ------------------------------------------------------------------
 *  The lines through the runs, in three steps with the drive between
 *
 *  A run is a span of the corridor that may be one straight, and the
 *  pattern of steps it is read from is arc.rules.runs's.  The chain of
 *  lines those runs become, with the ends the fit starts and finishes
 *  at, is arc.rules.chain's.  Neither is measured here: what is measured
 *  is the corridor, and what is decided is the script's.
 *  ------------------------------------------------------------------ */
static RunFan   s_tf_runfan;
static ChainFan s_tf_chainfan;

RunFan *path_runs(void)
{
    Tf *x = &s_path;
    int i;
    if (!s_path_ready)
        return NULL;
    s_tf_p   = &s_tf_by[s_tf_bucket];
    s_tf_own = x->own;
    s_tf_ex0 = x->ex0;
    s_tf_ex1 = x->ex1;
    for (i = 0; i < x->ns; ++i)
        x->st[i] = (V2){x->pts[i + 1].x - x->pts[i].x, x->pts[i + 1].y - x->pts[i].y};
    tf_runs(x->st, x->pts, x->ns, x->band, x->mark, x->runs, TF_MAX_RUNS, &s_tf_runfan);
    return &s_tf_runfan;
}

/*  And the line each run is: a straight through its points, a slope
 *  along the steps it repeats. */
ChainFan *path_chain(void)
{
    Tf *x = &s_path;
    int i;
    if (!s_path_ready)
        return NULL;
    x->nr = s_tf_runfan.nr;
    for (i = 0; i < x->nr; ++i)
        tf_line(x->pts, &x->runs[i]);
    memset(&s_tf_chainfan, 0, sizeof s_tf_chainfan);
    s_tf_chainfan.pts   = x->pts;
    s_tf_chainfan.runs  = x->runs;
    s_tf_chainfan.lines = x->lines;
    s_tf_chainfan.nr    = x->nr;
    s_tf_chainfan.nl    = x->nl;
    s_tf_chainfan.nt    = x->nt;
    s_tf_chainfan.ns    = x->ns;
    s_tf_chainfan.ex0   = x->ex0 >= 0;
    s_tf_chainfan.ex1   = x->ex1 >= 0;
    s_tf_chainfan.start = x->start;
    s_tf_chainfan.goal  = x->goal;
    s_tf_chainfan.st0   = x->st[0];
    s_tf_chainfan.st1   = x->st[x->ns - 1];
    return &s_tf_chainfan;
}

/*  And what the chain came to: the lines the fit walks the boundaries
 *  of, and the ends it starts and finishes at. */
int path_lined(void)
{
    Tf *x = &s_path;
    int i;
    if (!s_path_ready)
        return 0;
    x->nl    = s_tf_chainfan.nl;
    x->start = s_tf_chainfan.start;
    x->goal  = s_tf_chainfan.goal;
    tf_count(&s_tf.segments);
    s_tf_nprims = 0;
    for (i = 0; i < x->nr && s_tf_nprims < TF_MAX_RUNS; ++i)
    {
        s_tf_prims[s_tf_nprims].a    = tf_onto(&x->runs[i], x->pts[x->runs[i].ta]);
        s_tf_prims[s_tf_nprims].b    = tf_onto(&x->runs[i], x->pts[x->runs[i].tb]);
        s_tf_prims[s_tf_nprims].kind = x->runs[i].kind;
        ++s_tf_nprims;
        if (x->runs[i].kind)
            tf_count(&s_tf.slopes);
        else
            tf_count(&s_tf.straights);
    }
    x->out[0]   = x->start;
    x->fixed[0] = -1.0f;
    x->n        = 1;
    return x->nl > 1 ? x->nl - 1 : 0;
}

/*  The lines cross: one arc at the meet, if the meet is ahead
 *  of P and behind Q and an arc can be held there.  1 when the vertex is
 *  placed. */
/*  A leg's extension to the meet.  A line reaching a meet behind its own
 *  end is slab the run never held, so it must hold too. */
int path_join_holds(const JoinFan *j, int leg)
{
    const Tf     *x = (const Tf *)j->fit;
    const TfPair *p = (const TfPair *)j->pair;
    return leg == 0 ? swept_fits(x->mark, j->at, p->EP, x->band)
                    : swept_fits(x->mark, p->SQ, j->at, x->band);
}

/*  Does the corner keep every covered point of the gap it spans under
 *  the band?  A free line is checked along both whole lines, since past
 *  the meet the slab rides the other one.  A run's corner only along the
 *  gap between them. */
int path_join_covers(const JoinFan *j)
{
    const Tf     *x  = (const Tf *)j->fit;
    const TfPair *p  = (const TfPair *)j->pair;
    int           fr = j->free_line;
    V2            QE = p->Q->i0 >= x->ns ? x->goal : tf_onto(p->Q, x->pts[p->Q->tb]);
    return tf_gap_covers(x->pts, fr ? p->P->ta : p->P->tb + 1, fr ? p->Q->tb : p->Q->ta - 1,
                         p->prev, j->at, fr ? QE : p->after, x->band);
}

/*  The radius the corridor allows at the meet, given the tangent the
 *  script allows it.  Decided provisionally with half-edge budgets.  The
 *  radius is searched for real once every vertex is placed. */
SweepFan *path_join_arc(const JoinFan *j, float tl)
{
    const Tf     *x = (const Tf *)j->fit;
    const TfPair *p = (const TfPair *)j->pair;
    return path_sweep_ask(x->mark, p->prev, j->at, p->after, tl > 0.0f ? tl : 0.0f, x->rmax, x->rmin, x->band);
}

/*  The arc is held.  So must be the straights that reach it from each
 *  run's end.  Two 45 degree arms meeting at a right angle cross beyond
 *  the end of one of them.  The line from that run's last tile to the
 *  meet then runs through a tile the line does not own.  And past the
 *  one it does, which comes out bare. */
int path_join_legs(const JoinFan *j, float r)
{
    const Tf     *x  = (const Tf *)j->fit;
    const TfPair *p  = (const TfPair *)j->pair;
    V2            pi = j->at;
    V2            ui = {pi.x - p->prev.x, pi.y - p->prev.y}, uo = {p->after.x - pi.x, p->after.y - pi.y};
    float         li = v2len(ui), lo = v2len(uo), dot, d;
    V2            t1, t2;
    ui.x /= li;
    ui.y /= li;
    uo.x /= lo;
    uo.y /= lo;
    dot = ui.x * uo.x + ui.y * uo.y;
    d   = r * tanf(0.5f * acosf(dot > 1.0f ? 1.0f : dot < -1.0f ? -1.0f
                                                                : dot));
    t1  = (V2){pi.x - ui.x * d, pi.y - ui.y * d};
    t2  = (V2){pi.x + uo.x * d, pi.y + uo.y * d};
    if ((pi.x - t1.x) * p->P->d.x + (pi.y - t1.y) * p->P->d.y < (pi.x - p->EP.x) * p->P->d.x + (pi.y - p->EP.y) * p->P->d.y && !swept_fits(x->mark, p->EP, t1, x->band))
        return 0;
    if ((t2.x - pi.x) * p->Q->d.x + (t2.y - pi.y) * p->Q->d.y < (p->SQ.x - pi.x) * p->Q->d.x + (p->SQ.y - pi.y) * p->Q->d.y && !swept_fits(x->mark, t2, p->SQ, x->band))
        return 0;
    return 1;
}

/*  The vertex, at the meet, searched for its radius later. */
void path_join_place(JoinFan *j)
{
    Tf *x = (Tf *)j->fit;
    x->out[x->n]     = j->at;
    x->fixed[x->n++] = -1.0f;
    tf_count(&s_tf.joins);
    j->placed = 1;
}

/*  The lines cross: arc.rules.meet says whether the meet takes a
 *  vertex.  1 when one is placed. */
static void tf_join(Tf *x, const TfPair *p, V2 pi, JoinFan *out)
{
    JoinFan j;
    V2      QE = p->Q->i0 >= x->ns ? x->goal : tf_onto(p->Q, x->pts[p->Q->tb]); /* Q's far end, on its line */
    memset(&j, 0, sizeof j);
    j.fit       = x;
    j.pair      = (void *)p;
    j.at        = pi;
    j.free_line = p->P->kind == 2 || p->Q->kind == 2;
    j.ahead     = (pi.x - p->EP.x) * p->P->d.x + (pi.y - p->EP.y) * p->P->d.y;
    j.behind    = (p->SQ.x - pi.x) * p->Q->d.x + (p->SQ.y - pi.y) * p->Q->d.y;
    j.reach     = (p->EP.x - p->prev.x) * p->P->d.x + (p->EP.y - p->prev.y) * p->P->d.y;
    j.reach_on  = (QE.x - p->SQ.x) * p->Q->d.x + (QE.y - p->SQ.y) * p->Q->d.y;
    j.first      = x->n - 1 == 0;
    j.last       = p->k + 2 >= x->nl;
    j.len_in     = v2len((V2){pi.x - p->prev.x, pi.y - p->prev.y});
    j.len_out    = v2len((V2){p->after.x - pi.x, p->after.y - pi.y});
    j.fixed_prev = x->fixed[x->n - 1];
    j.res0       = x->res0;
    j.res1       = x->res1;
    j.need       = tf_need(p->prev, pi, p->after, x->rmin);
    j.share      = s_tune.corner_share;
    j.trim_cap   = s_tune.trim_cap;
    *out = j;
}

/*  One placing of the S.  Its tangent points are drawn back `a` along P
 *  and `b` along Q.  It holds the biarc between them.  It also holds the
 *  room its tangents leave the neighboring vertices.  Half an edge each
 *  way, the other half being theirs, unless the neighbor is the band's
 *  own end.  This turns nothing and needs only its reserve: and whether
 *  the band holds it.  The radius held. -1 for a placing the band
 *  refuses. -2 for no placing at all. */
/*  One placing of the S: its tangent points drawn back `a` along P and
 *  `c` along Q, and the biarc between them.  0 where there is no biarc
 *  at all, which is B behind A. */
int path_bridge_solve(BridgeFan *b, float a, float c, int f, int shift)
{
    const Tf     *x = (const Tf *)b->fit;
    const TfPair *p = (const TfPair *)b->pair;
    V2            A = {p->EP.x - p->P->d.x * a, p->EP.y - p->P->d.y * a};
    V2            B = {p->SQ.x + p->Q->d.x * c, p->SQ.y + p->Q->d.y * c};
    b->try_a     = a;
    b->try_c     = c;
    b->try_f     = f;
    b->try_shift = shift;
    if (!tf_biarc(A, p->P->d, B, p->Q->d, &b->c0, &b->c1, &b->d))
    {
        if (b->probe)
            dumpf("PROBE   f%d a %.2f b %.2f: no biarc\n", f, (double)a, (double)c);
        return 0;
    }
    (void)x;
    b->solve_in  = v2len((V2){b->c0.x - p->prev.x, b->c0.y - p->prev.y});
    b->solve_out = v2len((V2){p->after.x - b->c1.x, p->after.y - b->c1.y});
    return 1;
}

/*  A placing the outer edges have no room for, as --sweep-probe reports
 *  it. */
void path_bridge_refuse(const BridgeFan *b, int out, float room, float len)
{
    if (!b->probe)
        return;
    dumpf("PROBE   f%d a %.2f b %.2f d %.3f: no room on the way %s (%.3f of %.3f)\n", b->try_f, (double)b->try_a, (double)b->try_c, (double)b->d, out ? "out" : "in", (double)room, (double)len);
}

/*  Does the band hold the placing?  The radius it comes out at. */
float path_bridge_holds(BridgeFan *b)
{
    const Tf *x = (const Tf *)b->fit;
    return tf_biarc_holds(x->mark, ((const TfPair *)b->pair)->prev, b->c0, b->c1, ((const TfPair *)b->pair)->after, b->d, x->band);
}

/*  The placing as --sweep-probe reports it, and the sample that refused
 *  it. */
void path_bridge_result(const BridgeFan *b, float r)
{
    if (!b->probe)
        return;
    dumpf("PROBE   f%d shift %+d a %.2f b %.2f d %.3f: r %.3f%s\n", b->try_f, b->try_shift, (double)b->try_a, (double)b->try_c, (double)b->d, (double)r, r < 0.0f ? " refused" : "");
    if (r < 0.0f)
        dumpf("PROBE      by %s sample %.2f,%.2f (tile %d,%d)\n", s_tf_fail_side < 0 ? "inner" : s_tf_fail_side > 0 ? "outer"
                                                                                                                   : "centre",
              (double)s_tf_fail.x, (double)s_tf_fail.y, (int)floorf(s_tf_fail.x), (int)floorf(s_tf_fail.y));
}

/*  The pair as --sweep-probe reports it, with the budgets the script
 *  worked out. */
void path_bridge_note(const BridgeFan *b, float ba, float bc)
{
    const TfPair *p = (const TfPair *)b->pair;
    if (!b->probe)
        return;
    dumpf("PROBE biarc at %.2f,%.2f -> %.2f,%.2f budgets %.2f %.2f prev %.2f,%.2f after %.2f,%.2f\n", (double)p->EP.x, (double)p->EP.y, (double)p->SQ.x, (double)p->SQ.y, (double)ba, (double)bc, (double)p->prev.x, (double)p->prev.y, (double)p->after.x, (double)p->after.y);
}

/*  The placing just tried is the best so far. */
void path_bridge_keep(BridgeFan *b)
{
    b->b0     = b->c0;
    b->b1     = b->c1;
    b->best_d = b->d;
}

/*  The two vertices the kept S leaves, each carrying the tangent length
 *  it was built with so the radius stage does not search them. */
void path_bridge_place(BridgeFan *b)
{
    Tf *x = (Tf *)b->fit;
    x->out[x->n]     = b->b0;
    x->fixed[x->n++] = b->best_d;
    x->out[x->n]     = b->b1;
    x->fixed[x->n++] = b->best_d;
    tf_count(&s_tf.biarcs);
    b->placed = 1;
}

/*  Parallel lines: arc.rules.bridge draws a biarc between them.  1 when
 *  the two vertices are placed. */
static void tf_bridge(Tf *x, const TfPair *p, BridgeFan *out)
{
    BridgeFan b;
    float     px, py;
    memset(&b, 0, sizeof b);
    b.fit        = x;
    b.pair       = (void *)p;
    b.len_in     = v2len((V2){p->EP.x - p->prev.x, p->EP.y - p->prev.y});
    b.len_out    = v2len((V2){p->after.x - p->SQ.x, p->after.y - p->SQ.y});
    b.fixed_prev = x->fixed[x->n - 1];
    b.gap        = (p->SQ.x - p->EP.x) * p->P->d.x + (p->SQ.y - p->EP.y) * p->P->d.y;
    b.res0       = x->res0;
    b.res1       = x->res1;
    b.share      = s_tune.corner_share;
    b.band       = x->band;
    b.margin     = s_tune.margin;
    b.padded     = x->own != NULL;
    b.head       = p->P->i0 < 0;
    b.tail       = p->Q->i0 >= x->ns;
    b.first      = x->n - 1 == 0;
    b.last       = p->k + 2 >= x->nl;
    b.probe      = g_dev.sweep_probe && sscanf(g_dev.sweep_probe, "%f,%f", &px, &py) == 2 && fabsf(px - p->EP.x) < 1.5f && fabsf(py - p->EP.y) < 1.5f;
    *out = b;
}

/*  Neither: the join is walked point by point.  P's end, the gap's
 *  points, Q's start.  Or, for a jog, drawn as one diagonal. */
/*  Does the gap point beside this end already lie in line with it?
 *  Then the end turns nothing as a vertex, and only shortens the edge
 *  the real corner beside it may take its tangent from. */
int path_step_inline(const StepFan *w, int side)
{
    const Tf     *x = (const Tf *)w->fit;
    const TfPair *p = (const TfPair *)w->pair;
    if (side == 0)
    {
        V2 d0 = {x->pts[p->P->tb + 1].x - p->EP.x, x->pts[p->P->tb + 1].y - p->EP.y};
        return fabsf(v2cross(p->P->d, d0)) < 1e-3f;
    }
    else
    {
        V2 d1 = {p->SQ.x - x->pts[p->Q->ta - 1].x, p->SQ.y - x->pts[p->Q->ta - 1].y};
        return fabsf(v2cross(p->Q->d, d1)) < 1e-3f;
    }
}

/*  A JOG: the walk's two points one tile apart, square to two parallel
 *  lines heading the same way.  It is the data's way of drawing a
 *  sideways step. */
int path_step_jog(const StepFan *w)
{
    const Tf     *x = (const Tf *)w->fit;
    const TfPair *p = (const TfPair *)w->pair;
    V2            jg1, jst;
    float         jsl;
    if (fabsf(v2cross(p->P->d, p->Q->d)) >= 1e-3f || p->P->d.x * p->Q->d.x + p->P->d.y * p->Q->d.y <= 0.999f)
        return 0;
    jg1 = x->pts[p->P->tb + 1];
    jst = (V2){p->SQ.x - jg1.x, p->SQ.y - jg1.y};
    jsl = v2len(jst);
    return jsl > 0.5f && jsl <= 1.0f + 1e-3f && fabsf(jst.x * p->P->d.x + jst.y * p->P->d.y) < 1e-3f;
}

/*  The jog drawn as a DIAGONAL.  The step crosses the two tile edges at
 *  their middles.  A 45 degree line between those holds the band at line
 *  width.  The corner it passes lies 0.354 off it, with a 45 degree
 *  corner at each end instead of a right angle.  `keep` of straight
 *  stays before and after it, since a corner a quarter tile from a
 *  junction's port turns the port itself.  Where the neighbors are
 *  closer than that the corners slide toward the step's own tiles, by no
 *  more than `cap`.  The corridor still has to hold the steeper line
 *  that makes. 0 where it does not. */
int path_step_diagonal(StepFan *w, float keep, float cap)
{
    Tf           *x   = (Tf *)w->fit;
    const TfPair *p   = (const TfPair *)w->pair;
    V2            jg1 = x->pts[p->P->tb + 1], jg2 = p->SQ;
    V2            ja  = {jg1.x - p->P->d.x * keep, jg1.y - p->P->d.y * keep};
    V2            jb  = {jg2.x + p->Q->d.x * keep, jg2.y + p->Q->d.y * keep};
    float         ain  = (ja.x - p->prev.x) * p->P->d.x + (ja.y - p->prev.y) * p->P->d.y;
    float         bout = (p->after.x - jb.x) * p->Q->d.x + (p->after.y - jb.y) * p->Q->d.y;
    if (ain < keep)
    {
        float back = keep - ain > cap ? cap : keep - ain;
        ja         = (V2){ja.x + p->P->d.x * back, ja.y + p->P->d.y * back};
    }
    if (bout < keep)
    {
        float fwd = keep - bout > cap ? cap : keep - bout;
        jb        = (V2){jb.x - p->Q->d.x * fwd, jb.y - p->Q->d.y * fwd};
    }
    if (!swept_fits(x->mark, ja, jb, x->band) || x->n + 2 >= x->cap)
        return 0;
    x->out[x->n]     = ja;
    x->fixed[x->n++] = -1.0f;
    x->out[x->n]     = jb;
    x->fixed[x->n++] = -1.0f;
    tf_count(&s_tf.jogs);
    w->placed = 1;
    return 1;
}

/*  A line's own end as a vertex, and a point of the gap between them. */
void path_step_end(StepFan *w, int side)
{
    Tf           *x = (Tf *)w->fit;
    const TfPair *p = (const TfPair *)w->pair;
    if (x->n + 1 >= x->cap)
        return;
    x->out[x->n]     = side == 0 ? p->EP : p->SQ;
    x->fixed[x->n++] = -1.0f;
}

void path_step_point(StepFan *w, int t)
{
    Tf           *x = (Tf *)w->fit;
    const TfPair *p = (const TfPair *)w->pair;
    if (x->n + 1 >= x->cap)
        return;
    x->out[x->n]     = x->pts[p->P->tb + 1 + t];
    x->fixed[x->n++] = -1.0f;
    tf_count(&s_tf.fallbacks);
}

/*  Neither a meet nor a biarc: arc.rules.step goes between the two
 *  lines point by point, as the fit did before there were lines. */
static void tf_walk(Tf *x, const TfPair *p, StepFan *out)
{
    StepFan w;
    memset(&w, 0, sizeof w);
    w.fit  = x;
    w.pair = (void *)p;
    w.gap  = p->Q->ta - 1 - (p->P->tb + 1) + 1;
    w.head = p->P->i0 >= 0;
    w.tail = p->Q->i0 < x->ns;
    w.hw   = x->hw;
    *out = w;
}

/*  The three the composition asks for by name: the corridor sweep, a
 *  corner's demand for tangent, and what an end may spare.  The sampling
 *  and the arithmetic are the fit's.  What to do with the answers is
 *  scripts/compose/fit.lua's. */


float path_fit_demand(V2 a, V2 b, V2 c)
{
    return tf_demand(a, b, c);
}

float path_fit_need(V2 a, V2 b, V2 c, float rmin)
{
    return tf_need(a, b, c, rmin);
}

void path_fit_count(const char *what)
{
    if (strcmp(what, "tight") == 0)
        tf_count(&s_tf.tight);
    else if (strcmp(what, "swept") == 0)
        tf_count(&s_tf.swept);
    else if (strcmp(what, "corner") == 0)
        tf_count(&s_tf.hard);
}

/*  The idle vertices dropped and the radius at each corner: the SCRIPT'S
 *  (arc.rules.fit).  The join stage above produced the vertices.  Which
 *  of them say nothing, and how much of each edge the two corners
 *  sharing it may take, are decisions. */
/*  The path as it stands, for the rule that drops the idle vertices and
 *  settles the radius at each of the rest. */
static FitFan s_tf_fitfan;

static void tf_finish(Tf *x, FitFan *out)
{
    FitFan f;
    f.out = x->out, f.fixed = x->fixed, f.rad = x->rad, f.tlim = x->tlim;
    f.n = x->n;
    f.res0 = x->res0, f.res1 = x->res1;
    f.rmax = x->rmax, f.rmin = x->rmin, f.band = x->band;
    f.share    = s_tune.corner_share;
    f.trim_cap = s_tune.trim_cap;
    f.mark = x->mark;
    *out = f;
}

/*  ------------------------------------------------------------------
 *  The tangent fit, walked by the drive
 *
 *  The lines through the runs.  A vertex at every boundary between two
 *  lines: one arc where they cross, a biarc where they are parallel, a
 *  walked join where neither holds.  The idle vertices dropped.  The
 *  radius at each.  Which of the three to try at a boundary, and in what
 *  order, is the SCRIPT'S (arc.rules.join), and so is what lies after a
 *  line for the budget its join is given (arc.rules.after): so the fit
 *  is set up here, walked pair by pair from outside, and finished here.
 *  ------------------------------------------------------------------ */

static int tangent_begin(const uint8_t *mark, const uint8_t *own, const V2 *pts, int nt, float hw, V2 start, V2 goal, float rmax, float rmin, float gro, float reserve, int32_t ex0, int32_t ex1, V2 *out, float *rad, float *tlim, int cap)
{
    static V2    st[MAX_PTS];
    static Run   runs[TF_MAX_RUNS];
    static Run   lines[TF_MAX_RUNS + 2];
    static float fixed[MAX_PTS]; /* a biarc vertex's tangent length.  -1 for a searched one */
    /*  A line is held inside its corridor by the margin.  A slab IS its
     *  band, two tiles exactly.  Its corridor is the air beside it.
     *  Padding it would push every straight a hair into the tile next
     *  door, and refuse the whole slab wherever that is built on. */
    const float band = own ? hw + s_tune.margin : hw;
    /*  The approach a junction's mouth reserves straight.  It is the knob's, scaled by the width, or the length the family asks for.  A thread turnout's reach, so the cut lands on straight thread and the port where the lane ends. */
    const float appr = reserve > 0.0f ? reserve : s_tune.approach * (g_dev.noscale ? 1.0f : gro);
    static int  gix_fit_free_end = -1;
    const float freen   = geo_num(&gix_fit_free_end, "fit_free_end");
    const float res0 = ex0 >= 0 ? appr : freen, res1 = ex1 >= 0 ? appr : freen;
    int         ns   = nt - 1;
    Tf         *x    = &s_path;
    s_path_ready       = 0;
    if (nt < 2 || cap < 3)
        return 0;
    x->mark = mark, x->own = own, x->pts = pts, x->nt = nt, x->ns = ns, x->hw = hw, x->band = band, x->appr = appr;
    x->res0 = res0, x->res1 = res1, x->rmax = rmax, x->rmin = rmin, x->gro = gro, x->start = start, x->goal = goal;
    x->ex0 = ex0, x->ex1 = ex1, x->out = out, x->rad = rad, x->tlim = tlim, x->cap = cap;
    x->st = st, x->runs = runs, x->lines = lines, x->fixed = fixed, x->nr = 0, x->nl = 0, x->n = 0;
    s_path_ready = 1;
    return 1;
}

/*  The fit in hand, for the rule that walks its boundaries.  Everything
 *  it works on is this file's own, so the handle is the fit itself. */
void *path_handle(void)
{
    return s_path_ready ? (void *)&s_path : NULL;
}

int path_pairs(void)
{
    return s_path_ready && s_path.nl > 1 ? s_path.nl - 1 : 0;
}

/*  One boundary between two lines, set up: what the two measurements the
 *  scripts make of it read.  `after` is absent at the last boundary,
 *  where there is no line beyond Q to cross. */
int path_pair(int k, PathPair *out)
{
    Tf     *x = &s_path;
    TfPair *p = &s_path_pair;
    V2      pi;
    memset(out, 0, sizeof *out);
    if (!s_path_ready || k < 0 || k + 1 >= x->nl || x->n + 4 >= x->cap)
        return 0;
    p->k    = k;
    p->P    = &x->lines[k];
    p->Q    = &x->lines[k + 1];
    p->prev = x->out[x->n - 1];
    /* the ends of the two lines, on the lines: P's last tile, Q's first */
    p->EP = p->P->i0 < 0 ? x->start : tf_onto(p->P, x->pts[p->P->tb]);
    p->SQ = p->Q->i0 >= x->ns ? x->goal : tf_onto(p->Q, x->pts[p->Q->ta]);
    p->after = x->goal;
    if (k + 2 < x->nl)
    {
        const Run *R  = &x->lines[k + 2];
        V2         QE = p->Q->i0 >= x->ns ? x->goal : tf_onto(p->Q, x->pts[p->Q->tb]);
        out->has_after = 1;
        out->met       = line_meet(p->Q->p, p->Q->d, R->p, R->d, &p->after);
        out->free      = p->Q->kind == 2 || R->kind == 2;
        out->ahead     = (p->after.x - p->SQ.x) * p->Q->d.x + (p->after.y - p->SQ.y) * p->Q->d.y;
        out->reach     = (QE.x - p->SQ.x) * p->Q->d.x + (QE.y - p->SQ.y) * p->Q->d.y;
        s_path_end  = QE;
    }
    out->cross     = line_meet(p->P->p, p->P->d, p->Q->p, p->Q->d, &pi);
    out->free_join = p->P->kind == 2 || p->Q->kind == 2;
    return 1;
}

/*  And the answer: the far budget runs to the meet the rule kept, or
 *  to the line's own end where it did not. */
void path_after_is(int meet)
{
    if (s_path_ready && !meet)
        s_path_pair.after = s_path_end;
}

/*  One way of joining the two tried: an arc at the meet, a biarc between
 *  them, or the join walked tile by tile.  Each is a reading handed to a
 *  rule of its own.  Path_held answers whether what the rule did with it
 *  held, and the first way that does wins the boundary. */
static JoinFan   s_try_join;
static BridgeFan s_try_bridge;
static StepFan   s_try_step;
static int       s_try_way; /* 0 none, 1 an arc, 2 a biarc, 3 the walk */

const char *path_try(const char *how, void **obj)
{
    Tf     *x = &s_path;
    TfPair *p = &s_path_pair;
    V2      pi;
    s_try_way = 0;
    *obj      = NULL;
    if (!s_path_ready || !how)
        return NULL;
    if (strcmp(how, "arc") == 0)
    {
        if (!line_meet(p->P->p, p->P->d, p->Q->p, p->Q->d, &pi))
            return NULL; /* they never cross: there is no arc to try */
        tf_join(x, p, pi, &s_try_join);
        s_try_way = 1, *obj = &s_try_join;
        return "meet";
    }
    if (strcmp(how, "biarc") == 0)
    {
        tf_bridge(x, p, &s_try_bridge);
        s_try_way = 2, *obj = &s_try_bridge;
        return "bridge";
    }
    if (strcmp(how, "walk") == 0)
    {
        tf_walk(x, p, &s_try_step);
        s_try_way = 3, *obj = &s_try_step;
        return "step";
    }
    return NULL;
}

int path_held(void)
{
    switch (s_try_way)
    {
    case 1: return s_try_join.placed;
    case 2: return s_try_bridge.placed;
    case 3: return 1; /* the walk always works, and always looks like it */
    default: break;
    }
    return 0;
}

/*  The goal, and then the path as it stands.  The rule that drops the
 *  idle vertices reads them, and settles the radius at each of the rest. */
FitFan *path_ending(void)
{
    Tf *x = &s_path;
    if (!s_path_ready)
        return NULL;
    x->out[x->n]     = x->goal;
    x->fixed[x->n++] = -1.0f;
    tf_finish(x, &s_tf_fitfan);
    return &s_tf_fitfan;
}

int path_finish(void)
{
    Tf *x = &s_path;
    if (!s_path_ready)
        return 0;
    s_path_ready = 0;
    x->n         = s_tf_fitfan.n;
    return x->n;
}

/*  The fit on a chain of points, for the band walk: it has no tile
 *  chain, only its seam points.  Its corridor is not its tiles. */
int path_fit_points_begin(const uint8_t *mark, const uint8_t *own, const V2 *pts, int n, float hw, V2 start, V2 goal, float rmax, float rmin, float gro, int32_t ex0, int32_t ex1, int free_lines, V2 *out, float *rad, float *tlim, int cap)
{
    s_tf_nprims     = 0;
    s_tf_edge       = geo_num(&gix_fit_edge_slab, "fit_edge_slab"); /* the point chain is the band's */
    s_tf_cover_runs = 1;          /* and its runs and corners hold the covered cells, not its arcs alone */
    s_tf_free_lines = free_lines; /* and a run may be any span its corridor lets be straight */
    return tangent_begin(mark, own, pts, n, hw, start, goal, rmax, rmin, gro, 0.0f, ex0, ex1, out, rad, tlim, cap);
}

int path_fit_points_end(void)
{
    int nk          = path_finish();
    s_tf_cover_runs = 0;
    s_tf_free_lines = 0;
    s_tf_edge       = geo_num(&gix_fit_edge, "fit_edge");
    return nk;
}

/*  Which tally bucket the next fit counts into.  The fit knows nothing
 *  about what a bucket MEANS.  A caller that wants its own numbers apart
 *  from another's names an index.  It reads that index back with
 *  fit_tally_get. */
void fit_tally_into(int bucket)
{
    s_tf_bucket = bucket >= 0 && bucket < TALLY_BUCKETS ? bucket : 0;
}

/*  The runs of the last fit, for a caller that prints its own dump. */
void path_fit_prims(void)
{
    int d;
    for (d = 0; d < s_tf_nprims; ++d)
        dumpf("PRIM %d %.3f,%.3f %.3f,%.3f\n", s_tf_prims[d].kind, (double)s_tf_prims[d].a.x, (double)s_tf_prims[d].a.y, (double)s_tf_prims[d].b.x, (double)s_tf_prims[d].b.y);
}

void fit_stats(void)
{
    int f;
    for (f = 0; f < 3; ++f)
    {
        s_tf_p = &s_tf_by[f];
        if (s_tf.segments == 0)
            continue;
        dumpf("tangent fit  %s: %d segments, %d straights, %d slopes; %d joins, %d biarcs, %d tile-walked; "
               "%d swept, %d tight (under the minimum radius), %d corners\n",
               f == 0 ? "line" : f == 1 ? "thread"
                                        : "band",
               s_tf.segments,
               s_tf.straights,
               s_tf.slopes,
               s_tf.joins,
               s_tf.biarcs,
               s_tf.fallbacks,
               s_tf.swept,
               s_tf.tight,
               s_tf.hard);
    }
    s_tf_p = &s_tf_by[0];
}

void fit_stats_reset(void)
{
    memset(s_tf_by, 0, sizeof s_tf_by);
    s_tf_p = &s_tf_by[0];
}

/*  The whole path of one segment: its corridor's gates from the tile
 *  list, a smooth line through them, and the radius each corner may
 *  sweep.  The taut string was tried first and is wrong for a line: the
 *  shortest path hugs the inside of every bend.  So the band runs along
 *  one wall of its corridor.  It leaves the far side of the tile bare,
 *  and the tiles it left bare carried no geometry at all.  What a line
 *  wants is the smoothest line the corridor allows, near its middle
 *  where the corridor is straight.  So the line starts at the gates'
 *  midpoints and is relaxed.  Each point moves toward the mean of its
 *  neighbors, then back onto its own gate, over and over.  That
 *  converges on a curve which is straight where the corridor is
 *  straight, cuts a staircase into one diagonal because the gates let
 *  it.  Never leaves the room it was given. */
/*  The radius of the circle through three points: how tight the line
 *  turns at the middle one.  Straight gives infinity. */
static float turn_radius(V2 a, V2 b, V2 c)
{
    float abx = b.x - a.x, aby = b.y - a.y;
    float bcx = c.x - b.x, bcy = c.y - b.y;
    float cax = a.x - c.x, cay = a.y - c.y;
    float ab  = sqrtf(abx * abx + aby * aby);
    float bc  = sqrtf(bcx * bcx + bcy * bcy);
    float ca  = sqrtf(cax * cax + cay * cay);
    float ar2 = fabsf(abx * bcy - aby * bcx); /* twice the area */
    if (ar2 < 1e-6f)
        return 1e6f;
    return ab * bc * ca / (2.0f * ar2);
}

/*  ------------------------------------------------------------------
 *  The corridor fit, stage by stage.
 *
 *  It was one six-hundred-line function that carried every stage in the
 *  same scope, so a change to any of them could reach all the others.
 *  The stages now stand on their own and pass this working state between
 *  them.  Each one still reads the same names it always did, so the
 *  bodies are the bodies that were measured, unchanged.
 *
 *  corridor the run's own tiles, and nothing else gates the crossable part
 *  of each shared edge seed the first line: a point on every gate relax
 *  smooth it, hold the minimum radius, keep it inside collapse straight
 *  runs back into single edges spacing nodes far enough apart to be
 *  filleted spline the alternative fit, when it is asked for radii the
 *  widest legal arc at every corner
 *  ------------------------------------------------------------------ */
typedef struct
{
    uint8_t *mark;   /* corridor tiles, by index                    */
    int32_t *marked; /* which indices, so they can be cleared       */
    int      nm;
    V2      *gl, *gr; /* each gate's two ends                       */
    int      ng;
    V2      *p;    /* the line being fitted                       */
    int     *gate; /* the gate a point sits on, or -1 for free    */
    int      n;
    float    hw, rmin, rmax, gro;
} Fit;

/*  Ground a line may sweep across, as the script pushed it
 *  (scripts/line_tiles.lua). */
static int fit_open_ground(uint8_t b)
{
    return script_bytes("open_tiles")[b];
}

/*  The corridor of the fit in hand, held between the two halves: the
 *  cells it marked, and what the dump reads when it is done. */
static struct
{
    uint8_t        mark[R_MAP * R_MAP];
    int32_t        marked[MAX_PTS * 26];
    V2             centres[MAX_PTS];
    int            nm, nt, live;
    const int32_t *tcol, *trow;
    float          hw;
    V2             start, goal;
    V2            *out;
    float         *rad, *tlim;
} s_corr_fit;

/*  The corridor as the SCRIPT reads it.
 *
 *      The cells the fit was given.
 *      The two ends it must run between.
 *      The band's own half width.
 *
 *  A script that sweeps a line its own way starts here. */
int path_corridor(const int32_t **tcol, const int32_t **trow, int *nt, V2 *start, V2 *goal, float *hw)
{
    if (!s_corr_fit.live)
        return 0;
    *tcol  = s_corr_fit.tcol;
    *trow  = s_corr_fit.trow;
    *nt    = s_corr_fit.nt;
    *start = s_corr_fit.start;
    *goal  = s_corr_fit.goal;
    *hw    = s_corr_fit.hw;
    return 1;
}

int path_fit_begin(const RCity *c, const int32_t *tcol, const int32_t *trow, int nt, float hw, V2 start, V2 goal, float rmax, float rmin, float gro, float reserve, int32_t ex0, int32_t ex1, int free_reach, V2 *out, float *rad, float *tlim, int cap)
{
    s_tf_edge = geo_num(&gix_fit_edge, "fit_edge"); /* a chain of tiles samples its own band's edge */
    uint8_t *const mark    = s_corr_fit.mark;
    int32_t *const marked  = s_corr_fit.marked;
    V2 *const      centres = s_corr_fit.centres;
    int            i, nm = 0, n_own;
    s_corr_fit.live = 0;
    if (nt < 1 || cap < 2)
        return 0;
    for (i = 0; i < nt && nm < MAX_PTS; ++i)
        if (tcol[i] >= 0 && trow[i] >= 0 && tcol[i] < R_MAP && trow[i] < R_MAP && !mark[trow[i] * R_MAP + tcol[i]])
        {
            mark[trow[i] * R_MAP + tcol[i]] = 1;
            marked[nm++]                    = trow[i] * R_MAP + tcol[i];
        }
    n_own = nm;
    /*  The corridor is the cells the network occupies.  For a line
     *  allowed it, it also holds the free ground within reach of them.
     *  This is bare ground, rubble or trees, not water and nothing built
     *  or laid.  A second family sweeps its corners across the field and
     *  runs a staircase as one line.  Held to its tiles it turned inside
     *  each of them, at radius 0.4.  A branch was then a chain of hooks,
     *  with the thread broken where two met.  Those cells need not end
     *  up under the band, so the coverage is off for such a line.  The
     *  fit is the tangent fit, the one that stayed. */
    if (free_reach > 0 && c)
        for (i = 0; i < n_own; ++i)
        {
            int32_t oc = marked[i] % R_MAP, orr = marked[i] / R_MAP, dc, dr;
            for (dr = -free_reach; dr <= free_reach; ++dr)
                for (dc = -free_reach; dc <= free_reach; ++dc)
                {
                    int32_t tc = oc + dc, tr = orr + dr, j;
                    if (tc < 0 || tr < 0 || tc >= R_MAP || tr >= R_MAP)
                        continue;
                    j = tr * R_MAP + tc;
                    if (mark[j] || !fit_open_ground(c->xbld[j]) || is_water(c->xter[j]) || nm >= MAX_PTS * 26)
                        continue;
                    mark[j]      = 1;
                    marked[nm++] = j;
                }
        }
    for (i = 0; i < nt && i < MAX_PTS; ++i)
        centres[i] = (V2){(float)tcol[i] + 0.5f, (float)trow[i] + 0.5f};
    s_tf_nprims     = 0;
    s_tf_free_lines = free_reach > 0; /* a run may be any span the corridor lets be straight */
    s_corr_fit.nm   = nm;
    s_corr_fit.nt   = nt;
    s_corr_fit.tcol = tcol, s_corr_fit.trow = trow;
    s_corr_fit.hw   = hw;
    s_corr_fit.start = start, s_corr_fit.goal = goal;
    s_corr_fit.out = out, s_corr_fit.rad = rad, s_corr_fit.tlim = tlim;
    s_corr_fit.live = 1;
    return tangent_begin(mark, free_reach > 0 ? NULL : mark, centres, nt < MAX_PTS ? nt : MAX_PTS, hw, start, goal, rmax, rmin, gro, reserve, ex0, ex1, out, rad, tlim, cap);
}

/*  And what the fit leaves: the path, the corridor's marks cleared, and
 *  the dumps that read it. */
/*  A path the SCRIPT settled outright, in place of the fit's own stages.
 *  It is written where the fit's answer would have gone.  So everything
 *  after it reads a script's path exactly as it reads the pipeline's
 *  own.  A line swept some other way is a script and not a compile. */
static int s_path_given;

int path_answer(const V2 *q, const float *rad, const float *tlim, int n)
{
    int i;
    if (!s_corr_fit.live || !q || n < 2)
        return 0;
    if (n > MAX_PTS)
        n = MAX_PTS;
    for (i = 0; i < n; ++i)
    {
        s_corr_fit.out[i]  = q[i];
        s_corr_fit.rad[i]  = rad ? rad[i] : 0.0f;
        s_corr_fit.tlim[i] = tlim ? tlim[i] : 0.0f;
    }
    s_path_given = n;
    return n;
}

int path_fit_end(void)
{
    uint8_t *const mark   = s_corr_fit.mark;
    int32_t *const marked = s_corr_fit.marked;
    const int32_t *tcol = s_corr_fit.tcol, *trow = s_corr_fit.trow;
    const int      nt = s_corr_fit.nt, nm = s_corr_fit.nm;
    const float    hw   = s_corr_fit.hw;
    V2 *const      out  = s_corr_fit.out;
    float *const   rad  = s_corr_fit.rad;
    float *const   tlim = s_corr_fit.tlim;
    int            n, i, k;
    if (!s_corr_fit.live)
        return 0;
    s_corr_fit.live = 0;
    n               = path_finish();
    if (s_path_given)
    {
        n            = s_path_given;
        s_path_given = 0;
    }
    s_tf_free_lines = 0;
    if (n < 2)
    {
        for (i = 0; i < nm; ++i)
            mark[marked[i]] = 0;
        return 0;
    }
    /*  --curve-dump 1: every corner of every fitted path: where it is,
     *  how far it turns, and the radius it was given.  A radius of 0 is
     *  a hard corner, infinite curvature.  Anything under the band's own
     *  half width would turn the inner lip inside out.  This is the
     *  metric that says whether the geometry is legal, sampled at every
     *  corner rather than judged by eye. */
    if (g_dev.curve_dump)
        for (k = 1; k + 1 < n; ++k)
        {
            V2    ui = {out[k].x - out[k - 1].x, out[k].y - out[k - 1].y};
            V2    uo = {out[k + 1].x - out[k].x, out[k + 1].y - out[k].y};
            float li = sqrtf(ui.x * ui.x + ui.y * ui.y);
            float lo = sqrtf(uo.x * uo.x + uo.y * uo.y);
            float dot;
            if (li < 1e-5f || lo < 1e-5f)
                continue;
            dot = (ui.x * uo.x + ui.y * uo.y) / (li * lo);
            if (dot > 1.0f)
                dot = 1.0f;
            if (dot < -1.0f)
                dot = -1.0f;
            if (acosf(dot) * 57.2958f < 1.0f)
                continue; /* straight through */
            /*  The last field is the line's OWN radius through those
             *  three points.  This is what a curve has and a corner does
             *  not.  It is the one number that compares a fillet fit
             *  with a fit that has no fillets. */
            dumpf("CURVE %.3f %.3f %.1f %.3f %.3f %.3f %.4f %.4f %d\n", (double)out[k].x, (double)out[k].y, (double)(acosf(dot) * 57.2958f), (double)rad[k], (double)hw, (double)turn_radius(out[k - 1], out[k], out[k + 1]), (double)li, (double)lo, 0);
        }
    if (g_dev.path_dump)
    {
        int d;
        dumpf("PATH hw=%.3f\nTILES", (double)hw);
        for (d = 0; d < nt; ++d)
            dumpf(" %d,%d", (int)tcol[d], (int)trow[d]);
        dumpf("\nGATES"); /* the older fit's gates: none now, the line kept for tools/plan.py */
        dumpf("\nPTS");
        for (d = 0; d < n; ++d)
            dumpf(" %.3f,%.3f", (double)out[d].x, (double)out[d].y);
        dumpf("\nRAD");
        for (d = 0; d < n; ++d)
            dumpf(" %.3f", (double)rad[d]);
        dumpf("\nTLIM");
        for (d = 0; d < n; ++d)
            dumpf(" %.3f", (double)tlim[d]);
        dumpf("\n");
        /*  The lines the tangent fit found, straights and slopes, so the
         *  plan view can show what stayed straight and where the joins are. */
        for (d = 0; d < s_tf_nprims; ++d)
            dumpf("PRIM %d %.3f,%.3f %.3f,%.3f\n", s_tf_prims[d].kind, (double)s_tf_prims[d].a.x, (double)s_tf_prims[d].a.y, (double)s_tf_prims[d].b.x, (double)s_tf_prims[d].b.y);
    }
    for (i = 0; i < nm; ++i)
        mark[marked[i]] = 0;
    return n;
}

/* ---- stage one, upright: the corridor's grade ---------------------------- */

/* ---- the segment pipeline (the line spec, part 3.10) ------------------ */

/*  A network is walked as segments between its nodes, the junction and
 *  end tiles.  Every other tile has two links and lies on one segment.
 *  A segment's centerline is the polyline through its tiles' centers,
 *  from the side of the junction box it leaves to the side it reaches.
 *  The polyline is straightened, a staircase of corners becomes one
 *  straight line, at 45 degrees or 2:1.  Every remaining bend is
 *  filleted with an arc, the quarter circle of a lone corner or the
 *  gentler sweep where a diagonal meets the grid.  Then one strip is
 *  lofted along the whole path by arc length.  Its width is a function
 *  of the direction at each sample.  Its dashes and stripes are placed
 *  by the distance from the junction.  There is no join inside a segment
 *  to get wrong, and a segment meets its junction box square on. */

/*  ==================================================================
 *  Pieces
 *
 *  A fitted line becomes straights and arcs.  Everything downstream walks
 *  pieces by arc length rather than by point.
 *  ================================================================== */
/*  The half rule: a corner may take half of each edge beside it, so two
 *  corners on one edge never overlap (spec 3.10, step 3).  It is what
 *  every fit before the tangent one assumed, and fillet_r keeps it.  The
 *  half is the corner-share knob now.  Over 0.5, two corners on one edge
 *  may want more than the edge has. */
void tlim_half(const V2 *q, int n, float *tlim)
{
    int i;
    for (i = 0; i < n; ++i)
        tlim[i] = 0.0f;
    for (i = 1; i + 1 < n; ++i)
    {
        float lin  = v2len((V2){q[i].x - q[i - 1].x, q[i].y - q[i - 1].y});
        float lout = v2len((V2){q[i + 1].x - q[i].x, q[i + 1].y - q[i].y});
        tlim[i]    = s_tune.corner_share * (lin < lout ? lin : lout);
    }
}


/*  Fillet the polyline into straights and arcs.  At each bend the arc's
 *  radius is the one it was given.  It is clamped so its tangent points
 *  stay within the tangent budget the fit allowed it, which is half an
 *  edge under the older fits.  Under the tangent fit whatever room the
 *  vertex beside it left.  This is how an arc takes the whole of an end
 *  edge and a biarc's two vertices split theirs exactly. */
/*  A straight piece, unless it would have no length: a piece shorter
 *  than this is nothing to loft and leaves `cur` where it was. */
static void piece_run(PieceFan *p, V2 to)
{
    float l = v2len((V2){to.x - p->cur.x, to.y - p->cur.y});
    if (l <= 1e-4f)
        return;
    if (p->np + 1 > MAX_PIECES)
    {
        p->over = 1;
        return;
    }
    p->out[p->np].arc = 0;
    p->out[p->np].a   = p->cur;
    p->out[p->np].b   = to;
    p->out[p->np].len = l;
    ++p->np;
    p->cur = to;
}

/*  Corner i.  It gives the turn there, and how much of the outgoing edge
 *  it has.  It also gives how much of the incoming edge is left after
 *  the piece already laid. 0 for a vertex with no turn to it, which is
 *  no corner at all. */
int path_piece_corner(PieceFan *p, int i)
{
    V2    u_in  = {p->q[i].x - p->q[i - 1].x, p->q[i].y - p->q[i - 1].y};
    V2    u_out = {p->q[i + 1].x - p->q[i].x, p->q[i + 1].y - p->q[i].y};
    float lin = v2len(u_in), lout = v2len(u_out), dot;
    if (lin < 1e-6f || lout < 1e-6f)
        return 0;
    u_in.x /= lin;
    u_in.y /= lin;
    u_out.x /= lout;
    u_out.y /= lout;
    dot = u_in.x * u_out.x + u_in.y * u_out.y;
    if (dot > geo_num(&gix_fit_straight_dot, "fit_straight_dot"))
        return 0; /* straight on: no vertex */
    p->ui       = u_in;
    p->uo       = u_out;
    p->cross    = u_in.x * u_out.y - u_in.y * u_out.x;
    p->theta    = acosf(dot < -1.0f ? -1.0f : dot); /* the turn, 0..pi */
    p->tan_half = tanf(0.5f * p->theta);
    return 1;
}

/*  A corner the corridor gives no room to sweep stays a corner: the line
 *  runs to it and turns.  Skipping the vertex instead loses the path's
 *  shape: with every corner skipped a segment becomes one straight line
 *  between its ends.  This leaves the tiles it should have run through
 *  bare. */
void path_piece_straight(PieceFan *p, int i)
{
    if (!p->over)
        piece_run(p, p->q[i]);
}

/*  The fillet.  Its tangent points lie at distance r * tan(turn / 2)
 *  either side of the vertex.  Its center sits off the first of them
 *  along the inward normal, toward the turn. */
void path_piece_arc(PieceFan *p, int i, float r)
{
    float d = r * p->tan_half;
    V2    t1, t2, n1, cen;
    if (p->over)
        return;
    t1  = (V2){p->q[i].x - p->ui.x * d, p->q[i].y - p->ui.y * d};
    t2  = (V2){p->q[i].x + p->uo.x * d, p->q[i].y + p->uo.y * d};
    n1  = p->cross > 0.0f ? (V2){-p->ui.y, p->ui.x} : (V2){p->ui.y, -p->ui.x};
    cen = (V2){t1.x + n1.x * r, t1.y + n1.y * r};
    if (p->np + 2 > MAX_PIECES)
    {
        p->over = 1;
        return;
    }
    piece_run(p, t1);
    p->out[p->np].arc = 1;
    p->out[p->np].c   = cen;
    p->out[p->np].r   = r;
    p->out[p->np].t0  = atan2f(t1.y - cen.y, t1.x - cen.x);
    p->out[p->np].t1  = p->out[p->np].t0 + (p->cross > 0.0f ? p->theta : -p->theta);
    p->out[p->np].len = r * p->theta;
    ++p->np;
    p->cur = t2;
}

/*  The run out to the path's far end. */
void path_piece_tail(PieceFan *p)
{
    if (p->over)
        return;
    if (p->np + 1 > MAX_PIECES)
    {
        p->over = 1;
        return;
    }
    piece_run(p, p->q[p->n - 1]);
}

/*  ---- THE CUT QUEUE ----------------------------------------------------
 *
 *  Cutting a fitted path into pieces is arc.rules.pieces's, and only the
 *  drive may ask for it.  A pass that needs a path cut puts the chain
 *  here.  It reads the pieces back once the drive has been round.  The
 *  drive hands each queued chain to the rule as a `pieces` handle.
 *
 *  The queue is emptied at each place the drive cuts.  So what has to
 *  fit in it is one gathering step's worth.  That is a city's segment
 *  fits, or one junction's candidate connectors.  It is never a whole
 *  build's. */
