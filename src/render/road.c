/*  road.c -- the road, rail and power geometry, end to end.
 *
 *  One algorithm, one file (the user, 3 September 2026: "why so many
 *  files?  this is the kind of algorithm that should live in a single
 *  file").  It was split across two -- the path fit in one, the segment
 *  and junction work in another -- and following it meant holding both
 *  in your head at once, which is where several of this renderer's bugs
 *  came from.
 *
 *  It runs in the order the user set out:
 *
 *    stage one    the corridor: the tiles a segment occupies, and the
 *                 gate on each shared edge -- the crossable window, the
 *                 edge shrunk by the band's half width.
 *    stage two    the path through them.  Two fitters answer the same
 *                 question and can be compared on the same city: the
 *                 shipped one relaxes a polyline and sweeps each corner
 *                 with the widest arc its corridor allows, and the other
 *                 boxes a spline's control points into the corridor and
 *                 minimises curvature.  The knobs are live, in Road
 *                 tuning.
 *    stage three  the junctions: each takes its shape from the arms that
 *                 reach it and hands them back where to start.
 *    then         the loft: the cross-section swept along the pieces,
 *                 the profile ramped between node altitudes, the
 *                 markings, the furniture, and the corridor's own shelf.
 *
 *  Separated by TASK inside, each section behind a banner, in the order
 *  the pipeline runs:
 *
 *      The corridor and its gates    what a run may occupy
 *      The corridor fit, by spline   the alternative fitter, off by default
 *      The corridor fit, by fillet   the shipped fitter, stage by stage
 *      Pieces                        straights and arcs, walked by length
 *      Stations, records and knobs   what the section stands on, what the
 *                                    traffic model is told, the live values
 *      Lofting                       the ribbon, its furniture, its surface
 *      Walking the network           node to node, fit to geometry
 *      Junctions                     the polygon the arms cut out
 *      The other things this file builds
 *
 *  What stays outside: the triangle emitters and tile clipping shared
 *  with the terrain (mesh_net.c), what a tile draws (mesh_tile.c),
 *  the driver that walks the network twice (mesh.c), and the highway
 *  path with the straightener it is the only caller of
 *  (mesh_hiway.c).
 */
#include "mesh_int.h"


#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- stage one: the corridor ------------------------------------------- */

/*  A tile of a corridor, and the gate on its far side. */
typedef struct
{
    int32_t col, row;
} Cell;

/*  ==================================================================
 *  The corridor and its gates
 *
 *  The run's own tiles are the whole world this algorithm sees.  A gate is
 *  the crossable part of a shared edge, shrunk by the half width so what
 *  fits through it is the BAND and not the line.  Everything below asks
 *  the same two questions of a piece of line: does the band stay on the
 *  run's tiles, and which tiles does it cover.
 *  ================================================================== */
/*  The gate between two 4-adjacent tiles: their shared edge, shrunk by
 *  `hw` at each end so a band of that half-width passes through it
 *  whole.  `left` and `right` are the ends as seen looking along the
 *  direction of travel.  A gate narrower than nothing (a band wider
 *  than the tile) collapses to the edge's midpoint. */
static void gate_between(int32_t ac, int32_t ar, int32_t bc, int32_t br, float hw, V2 *left, V2 *right)
{
    float dx = (float)(bc - ac), dy = (float)(br - ar);
    float mx = (float)ac + 0.5f + dx * 0.5f, my = (float)ar + 0.5f + dy * 0.5f;
    /*  Across the gate.  The funnel's sign convention is a y-up frame's;
     *  a map's rows increase southward, so every cross product flips and
     *  the sides swap with them: what the traveller calls left goes in
     *  `right`.  Getting this backwards made the string zig-zag between
     *  the gates' ends, or ignore them and run straight through walls. */
    float px = -dy, py = dx;
    float half = 0.5f - hw;
    if (half < 0.0f)
        half = 0.0f;
    left->x  = mx + px * half;
    left->y  = my + py * half;
    right->x = mx - px * half;
    right->y = my - py * half;
}

/*  Whether a point is far enough inside the corridor for a band of
 *  half-width `hw`: the square it inscribes lies on corridor tiles.
 *  `mark` holds one byte per tile, non-zero on the corridor. */
static int corridor_holds(const uint8_t *mark, float x, float y, float hw)
{
    int k;
    for (k = 0; k < 4; ++k)
    {
        float   sx = x + ((k & 1) ? hw : -hw), sy = y + ((k & 2) ? hw : -hw);
        int32_t tc = (int32_t)floorf(sx), tr = (int32_t)floorf(sy);
        if (tc < 0 || tr < 0 || tc >= R_MAP || tr >= R_MAP)
            return 0;
        if (!mark[tr * R_MAP + tc])
            return 0;
    }
    return 1;
}

/* ---- stage two: the taut path ------------------------------------------ */

/*  Does a band of half-width `hw` along a→b stay on the corridor? */
static int band_fits(const uint8_t *mark, V2 a, V2 b, float hw)
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
        float t = (float)sm / (float)ns;
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
 *  `cov`.  Used to prove a node may be dropped: the merged edge has to
 *  cover everything the two edges it replaces covered, or the tiles it
 *  stops covering come out bare. */
static void band_cover(V2 a, V2 b, float hw, uint8_t *cov, uint8_t stamp)
{
    float dx = b.x - a.x, dy = b.y - a.y;
    float len = sqrtf(dx * dx + dy * dy), px, py;
    int   sm, ns, side;
    if (len < 1e-5f)
        return;
    px  = -dy / len;
    py  = dx / len;
    ns  = (int)(len * 8.0f) + 2;
    for (sm = 0; sm <= ns; ++sm)
    {
        float t = (float)sm / (float)ns;
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


/*  The tiles a band along a->b covers that are NOT the run's own,
 *  stamped into `bad`.  A rail's minimum radius outranks its corridor, so
 *  a fitted line may legitimately overhang; what matters when a node is
 *  dropped is therefore not whether the line that replaces it is
 *  perfectly inside, but whether it strays any FURTHER than the two
 *  edges it replaces already do. */
static void band_stray(const uint8_t *mark, V2 a, V2 b, float hw, uint8_t *bad, uint8_t stamp)
{
    float dx = b.x - a.x, dy = b.y - a.y;
    float len = sqrtf(dx * dx + dy * dy), px, py;
    int   sm, ns, side;
    if (len < 1e-5f)
        return;
    px = -dy / len;
    py = dx / len;
    ns = (int)(len * 12.0f) + 4;
    for (sm = 0; sm <= ns; ++sm)
    {
        float t = (float)sm / (float)ns;
        float qx = a.x + dx * t, qy = a.y + dy * t;
        for (side = -1; side <= 1; side += 2)
        {
            float   ex = qx + px * hw * (float)side, ey = qy + py * hw * (float)side;
            int32_t tc = (int32_t)floorf(ex), tr = (int32_t)floorf(ey);
            if (tc < 0 || tr < 0 || tc >= R_MAP || tr >= R_MAP)
                continue;
            if (!mark[tr * R_MAP + tc])
                bad[tr * R_MAP + tc] = stamp;
        }
    }
}

/*  Does a->b stray only onto tiles already stamped?  */
static int32_t s_stray_hit = -1; /* the tile that failed, for the dump */

static int band_stray_within(const uint8_t *mark, V2 a, V2 b, float hw, const uint8_t *bad,
                             uint8_t stamp)
{
    float dx = b.x - a.x, dy = b.y - a.y;
    float len = sqrtf(dx * dx + dy * dy), px, py;
    int   sm, ns, side;
    if (len < 1e-5f)
        return 1;
    px = -dy / len;
    py = dx / len;
    ns = (int)(len * 12.0f) + 4;
    for (sm = 0; sm <= ns; ++sm)
    {
        float t = (float)sm / (float)ns;
        float qx = a.x + dx * t, qy = a.y + dy * t;
        for (side = -1; side <= 1; side += 2)
        {
            float   ex = qx + px * hw * (float)side, ey = qy + py * hw * (float)side;
            int32_t tc = (int32_t)floorf(ex), tr = (int32_t)floorf(ey);
            if (tc < 0 || tr < 0 || tc >= R_MAP || tr >= R_MAP)
            {
                s_stray_hit = -2;
                return 0;
            }
            if (!mark[tr * R_MAP + tc] && bad[tr * R_MAP + tc] != stamp)
            {
                s_stray_hit = tr * R_MAP + tc;
                return 0; /* somewhere neither the run nor the pair reached */
            }
        }
    }
    return 1;
}



/*  ------------------------------------------------------------------
 *  The corridor fit as it is actually solved elsewhere.
 *
 *  The polyline-and-fillet pipeline decides curvature locally and after
 *  the fact: each corner asks for the widest arc that happens to fit
 *  between its two edges, and when none does the corner is emitted hard.
 *  Every symptom that follows -- nodes too close to sweep, a radius floor
 *  that rises with the band, corners with no legal arc -- is that choice
 *  coming back (the user: "How is this not a solved problem").
 *
 *  It is solved.  The standard method is safe-corridor optimisation: put
 *  the path in a basis whose CONTROL POINTS bound the curve (a uniform
 *  cubic B-spline, by the convex hull property), box each control point
 *  into the corridor, and minimise curvature.  Containment stops being a
 *  test applied afterwards and becomes a constraint on points; curvature
 *  is continuous by construction, so there is no such thing as a hard
 *  corner in the result.  Quadrotor and self-driving planners fit paths
 *  this way; the civil engineering equivalent is the tangent-spiral-arc
 *  alignment road design has used for a century.
 *
 *  Minimising the discrete bending energy sum|p[i-1] - 2p[i] + p[i+1]|^2
 *  makes the stationary condition a fourth difference of zero, so one
 *  sweep of projected Gauss-Seidel is
 *
 *      p[i] <- box( (4(p[i-1] + p[i+1]) - (p[i-2] + p[i+2])) / 6 )
 *
 *  which is a few dozen iterations of arithmetic per segment and needs no
 *  solver, no global system and no per-corner search.
 *  ------------------------------------------------------------------ */

static float turn_radius(V2 a, V2 b, V2 c);

/*  Whether the last path fitted took the spline.  Under the spline fit
 *  the node marks are drawn ONLY for the segments that did not, so the
 *  overlay shows at a glance which runs still carry the old geometry --
 *  and, since every harsh turn left in Toronto is in one of them, exactly
 *  where the remaining trouble is. */
static int s_last_splined = 0;

/*  The corridor's own convex regions, one per span of the curve.
 *
 *  The convex hull property bounds a span by its four control points, so
 *  a span is safe exactly when all four lie in one CONVEX piece of the
 *  corridor -- boxing each point into its own tile proves nothing about
 *  the curve between them, which was why nine spans in ten escaped and
 *  had to be thrown away.  The corridor here is a run of unit tiles, so
 *  the convex pieces are the solid rectangles it contains: the four
 *  tiles' bounding rectangle when the corridor fills it, and otherwise
 *  the middle tile alone, which forces the span's points together and
 *  turns the corner tightly but legally. */
/*  ==================================================================
 *  The corridor fit, by spline
 *
 *  Control points boxed into convex pieces of the corridor, curvature
 *  minimised under tension toward the taut line, the minimum radius
 *  imposed, and the curve checked as a swept band before it is accepted.
 *  Off by default; the 'spline fit' knob turns it on.
 *  ================================================================== */
/*  The point where the line through a in direction da meets the line
 *  through b in direction db; 0 when they are parallel. */
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

static void span_region(const uint8_t *mark, const int32_t *tc, const int32_t *tr, int n, int j,
                        int32_t *c0, int32_t *r0, int32_t *c1, int32_t *r1)
{
    int     k, i0 = j < 0 ? 0 : j;
    int32_t a = tc[i0], b = tc[i0], c = tr[i0], d = tr[i0];
    for (k = 0; k < 4; ++k)
    {
        int i = j + k;
        i     = i < 0 ? 0 : i >= n ? n - 1 : i;
        a     = tc[i] < a ? tc[i] : a;
        b     = tc[i] > b ? tc[i] : b;
        c     = tr[i] < c ? tr[i] : c;
        d     = tr[i] > d ? tr[i] : d;
    }
    for (k = c; k <= d; ++k)
    {
        int32_t x;
        for (x = a; x <= b; ++x)
            if (x < 0 || k < 0 || x >= R_MAP || k >= R_MAP || !mark[k * R_MAP + x])
            {
                /*  Not solid: fall back to the span's middle tile. */
                int mid = j + 1;
                mid     = mid < 0 ? 0 : mid >= n ? n - 1 : mid;
                *c0 = *c1 = tc[mid];
                *r0 = *r1 = tr[mid];
                return;
            }
    }
    *c0 = a;
    *c1 = b;
    *r0 = c;
    *r1 = d;
}

/*  One span of a uniform cubic B-spline. */
static V2 bspline_at(V2 a, V2 b, V2 c, V2 d, float t)
{
    float t2 = t * t, t3 = t2 * t;
    float w0 = (-t3 + 3.0f * t2 - 3.0f * t + 1.0f) / 6.0f;
    float w1 = (3.0f * t3 - 6.0f * t2 + 4.0f) / 6.0f;
    float w2 = (-3.0f * t3 + 3.0f * t2 + 3.0f * t + 1.0f) / 6.0f;
    float w3 = t3 / 6.0f;
    V2    r;
    r.x = a.x * w0 + b.x * w1 + c.x * w2 + d.x * w3;
    r.y = a.y * w0 + b.y * w1 + c.y * w2 + d.y * w3;
    return r;
}

/*  Smooth the control polygon inside its boxes, then hand back the curve
 *  it defines, sampled.  Returns the new point count, or 0 to say the
 *  caller should keep what it had. */
static int spline_try(const uint8_t *mark, const int32_t *marked, int nm, const V2 *pt, int n,
                      float hw, float inset, float rmin, int rounds, V2 *out, int cap)
{
    static V2      ctl[MAX_PTS];
    static float   bx0[MAX_PTS], by0[MAX_PTS], bx1[MAX_PTS], by1[MAX_PTS];
    static int32_t tc[MAX_PTS], tr[MAX_PTS];
    int            i, j, it, per, m = 0;
    if (n < 4 || n > MAX_PTS)
        return 0;
    /*  Denser control points before anything else.  A span is safe when
     *  its four points share one convex piece of corridor, so shorter
     *  spans are likelier to find one -- subdivision is the standard
     *  answer when a corridor is too tight for the points you have, and
     *  it also pulls the curve closer to its control polygon. */
    {
        static V2 dense[3][MAX_PTS];
        int       round;
        for (round = 0; round < rounds && 2 * n - 1 <= MAX_PTS; ++round)
        {
            V2 *dst = dense[round % 3];
            int d2  = 0;
            for (i = 0; i + 1 < n; ++i)
            {
                dst[d2++] = pt[i];
                dst[d2++] = (V2){0.5f * (pt[i].x + pt[i + 1].x), 0.5f * (pt[i].y + pt[i + 1].y)};
            }
            dst[d2++] = pt[n - 1];
            pt        = dst;
            n         = d2;
        }
    }
    for (i = 0; i < n; ++i)
    {
        ctl[i] = pt[i];
        tc[i]  = (int32_t)floorf(pt[i].x);
        tr[i]  = (int32_t)floorf(pt[i].y);
        bx0[i] = -1e9f;
        by0[i] = -1e9f;
        bx1[i] = 1e9f;
        by1[i] = 1e9f;
    }
    /*  Each point is held by every span that uses it, so its box is the
     *  intersection of those spans' regions -- still a box. */
    for (j = -1; j + 2 < n; ++j)
    {
        int32_t c0, r0, c1, r1;
        int     k;
        span_region(mark, tc, tr, n, j, &c0, &r0, &c1, &r1);
        for (k = 0; k < 4; ++k)
        {
            int   i2 = j + k;
            float lx, ly, hx, hy;
            if (i2 < 0 || i2 >= n)
                continue;
            lx = (float)c0 + inset;
            hx = (float)(c1 + 1) - inset;
            ly = (float)r0 + inset;
            hy = (float)(r1 + 1) - inset;
            bx0[i2] = lx > bx0[i2] ? lx : bx0[i2];
            bx1[i2] = hx < bx1[i2] ? hx : bx1[i2];
            by0[i2] = ly > by0[i2] ? ly : by0[i2];
            by1[i2] = hy < by1[i2] ? hy : by1[i2];
        }
    }
    for (i = 0; i < n; ++i)
    {
        /*  Spans that disagree leave nothing in common -- the corridor is
         *  too tight for this many points to share one convex piece.  The
         *  point then keeps its OWN tile, which is always legal; the span
         *  that wanted otherwise loses its hull guarantee and the check at
         *  the end decides.  Splitting the difference instead put the
         *  point on a tile boundary, where the band cannot fit at all. */
        if (bx1[i] < bx0[i])
        {
            bx0[i] = (float)tc[i] + inset;
            bx1[i] = (float)(tc[i] + 1) - inset;
        }
        if (by1[i] < by0[i])
        {
            by0[i] = (float)tr[i] + inset;
            by1[i] = (float)(tr[i] + 1) - inset;
        }
        ctl[i].x = ctl[i].x < bx0[i] ? bx0[i] : ctl[i].x > bx1[i] ? bx1[i] : ctl[i].x;
        ctl[i].y = ctl[i].y < by0[i] ? by0[i] : ctl[i].y > by1[i] ? by1[i] : ctl[i].y;
    }
    /*  Bending energy alone has no opinion about STRAIGHT: any lazy curve
     *  through the corridor's slack costs about the same as the straight
     *  line, so the fit wanders and runs that should be dead straight bow
     *  (the user, of the first result: "That's worse").  A real alignment
     *  is straight tangents with curvature spent only where the ground
     *  forces it, so the energy carries a tension term as well --
     *
     *      E = sum |p[i-1] - 2p[i] + p[i+1]|^2  +  lam * sum |p[i] - q[i]|^2
     *
     *  where q is the taut line the corridor fit already found.  Straight
     *  stays straight because straight is where q is; curvature appears
     *  only where the boxes push the line off q. */
    for (it = 0; it < 64; ++it)
        for (i = 1; i + 1 < n; ++i)
        {
            const float lam = 0.6f;
            V2          t;
            if (i >= 2 && i + 2 < n)
            {
                t.x = (4.0f * (ctl[i - 1].x + ctl[i + 1].x) - (ctl[i - 2].x + ctl[i + 2].x) +
                       6.0f * lam * pt[i].x) /
                      (6.0f * (1.0f + lam));
                t.y = (4.0f * (ctl[i - 1].y + ctl[i + 1].y) - (ctl[i - 2].y + ctl[i + 2].y) +
                       6.0f * lam * pt[i].y) /
                      (6.0f * (1.0f + lam));
            }
            else
            {
                t.x = (0.5f * (ctl[i - 1].x + ctl[i + 1].x) + lam * pt[i].x) / (1.0f + lam);
                t.y = (0.5f * (ctl[i - 1].y + ctl[i + 1].y) + lam * pt[i].y) / (1.0f + lam);
            }
            ctl[i].x = t.x < bx0[i] ? bx0[i] : t.x > bx1[i] ? bx1[i] : t.x;
            ctl[i].y = t.y < by0[i] ? by0[i] : t.y > by1[i] ? by1[i] : t.y;
        }
    /*  And now the CONSTRAINT, which minimising curvature is not.  An
     *  energy expresses a preference and will settle happily on a harsh
     *  curve wherever the corridor squeezes; the minimum radius has to be
     *  imposed (the user: "why are you allowing harsh curves still").
     *
     *  Every control point whose corner is tighter than the floor is
     *  pulled toward the line between its neighbours -- the one move that
     *  always opens a turn -- and then put back in its box.  Interleaved
     *  with the smoothing so the two settle together rather than fight. */
    for (it = 0; it < 96; ++it)
    {
        int moved = 0;
        for (i = 1; i + 1 < n; ++i)
        {
            float r = turn_radius(ctl[i - 1], ctl[i], ctl[i + 1]);
            V2    mid, mv;
            float pull;
            if (r >= rmin)
                continue;
            mid.x = 0.5f * (ctl[i - 1].x + ctl[i + 1].x);
            mid.y = 0.5f * (ctl[i - 1].y + ctl[i + 1].y);
            pull  = 1.0f - r / rmin;
            if (pull > 0.5f)
                pull = 0.5f;
            mv.x     = ctl[i].x + (mid.x - ctl[i].x) * pull;
            mv.y     = ctl[i].y + (mid.y - ctl[i].y) * pull;
            ctl[i].x = mv.x < bx0[i] ? bx0[i] : mv.x > bx1[i] ? bx1[i] : mv.x;
            ctl[i].y = mv.y < by0[i] ? by0[i] : mv.y > by1[i] ? by1[i] : mv.y;
            ++moved;
        }
        if (!moved)
            break;
        /*  One smoothing sweep after each opening pass, so opening a
         *  corner does not leave a kink beside it. */
        for (i = 1; i + 1 < n; ++i)
        {
            const float lam = 0.6f;
            V2          t;
            t.x = (0.5f * (ctl[i - 1].x + ctl[i + 1].x) + lam * pt[i].x) / (1.0f + lam);
            t.y = (0.5f * (ctl[i - 1].y + ctl[i + 1].y) + lam * pt[i].y) / (1.0f + lam);
            ctl[i].x = t.x < bx0[i] ? bx0[i] : t.x > bx1[i] ? bx1[i] : t.x;
            ctl[i].y = t.y < by0[i] ? by0[i] : t.y > by1[i] ? by1[i] : t.y;
        }
    }
    /*  A clamped spline, by padding: the first and last control points
     *  stand three times each, so the curve starts and ends exactly on
     *  them.  Clamping the span index instead repeated points at zero
     *  spacing, which sampled as cusps -- local radii of a hundredth of a
     *  tile in the middle of what should be the smoothest part. */
    {
        static V2 pad[MAX_PTS + 4];
        int       np2 = 0, j2;
        pad[np2++] = ctl[0];
        pad[np2++] = ctl[0];
        for (i = 0; i < n; ++i)
            pad[np2++] = ctl[i];
        pad[np2++] = ctl[n - 1];
        pad[np2++] = ctl[n - 1];
        per = 6;
        while (per > 1 && (np2 - 3) * per + 1 > cap)
            --per;
        for (j2 = 0; j2 + 3 < np2; ++j2)
        {
            int k;
            for (k = 0; k < per && m + 1 < cap; ++k)
                out[m++] = bspline_at(pad[j2], pad[j2 + 1], pad[j2 + 2], pad[j2 + 3],
                                      (float)k / (float)per);
        }
        if (m + 1 < cap)
            out[m++] = ctl[n - 1];
    }
    /*  Checked as a swept band, not as a square: corridor_holds tests the
     *  four corners of an axis-aligned box, which asks for hw*sqrt(2) of
     *  room and fails a diagonal that fits perfectly well. */
    for (i = 0; i + 1 < m; ++i)
        if (!band_fits(mark, out[i], out[i + 1], hw))
        {
            if (getenv("SC2K_SPLINE_DUMP"))
                printf("SPLINE fallback at sample %d of %d (%.0f%% along) from %d controls\n",
                       i, m, (double)(100.0 * i / (m > 1 ? m - 1 : 1)), n);
            return 0;
        }
    /*  And every tile of the run must still have road over it.  The
     *  fillet path guarantees this by keeping a point at every gate; a
     *  curve that smooths across a tile does not, and a tile whose
     *  geometry goes missing is a hole in the city -- two of them in
     *  JUNETOWN and FLARANGE before this check existed. */
    {
        static uint8_t seen[R_MAP * R_MAP];
        static uint8_t cstamp = 0;
        int            t2;
        if (++cstamp == 0)
        {
            memset(seen, 0, sizeof seen);
            cstamp = 1;
        }
        for (i = 0; i + 1 < m; ++i)
            band_cover(out[i], out[i + 1], hw, seen, cstamp);
        for (t2 = 0; t2 < nm; ++t2)
            if (seen[marked[t2]] != cstamp)
            {
                if (getenv("SC2K_SPLINE_DUMP"))
                    printf("SPLINE fallback: tile %d,%d would have no road over it\n",
                           (int)(marked[t2] % R_MAP), (int)(marked[t2] / R_MAP));
                return 0;
            }
    }
    if (getenv("SC2K_SPLINE_DUMP"))
        printf("SPLINE ok %d samples from %d control points\n", m, n);
    return m;
}

/*  A segment is not handed back to the fillet path at the first refusal.
 *  Where the curve escapes, the answer is more room to bend in, so the
 *  boxes are inset further and the solve repeated -- every fallback is a
 *  segment that keeps the old harsh corners, and measuring them showed
 *  that ALL of the harsh turns left in Toronto were in the fourteen per
 *  cent that bailed out, and none in the splined ones (the user: "why are
 *  you allowing harsh curves still"). */
static int spline_fit(const uint8_t *mark, const int32_t *marked, int nm, const V2 *pt, int n,
                      float hw, float rmin, V2 *out, int cap)
{
    /*  Two levers, tried in order: more control points, which pulls the
     *  curve onto its polygon, and more inset, which gives it room to
     *  bend.  Subdivision first -- it costs nothing but arithmetic. */
    /*  More room to bend in, tried in order.  NOT more subdivision: past
     *  two rounds the control points sit in boxes smaller than the space
     *  between them, the polygon starts to zig-zag inside its own
     *  corridor, and the curvature it was meant to fix gets worse -- 368
     *  splined nodes over thirty degrees against none at two rounds. */
    static const float grow[4] = {1.0f, 1.15f, 1.35f, 1.6f};
    int                g;
    for (g = 0; g < 4; ++g)
    {
        int m = spline_try(mark, marked, nm, pt, n, hw, hw * grow[g], rmin, 2, out, cap);
        if (m >= 2)
            return m;
    }
    return 0;
}

/*  ==================================================================
 *  The corridor fit, by fillet
 *
 *  The shipped fit: a polyline through the gates, relaxed, thinned, and
 *  then swept at each corner by the widest arc its corridor allows.  The
 *  stages are below, and path_fit drives them in order.
 *  ================================================================== */
/*  A point clamped to a gate's segment. */
static V2 gate_clamp(V2 p, V2 a, V2 b)
{
    float dx = b.x - a.x, dy = b.y - a.y, l2 = dx * dx + dy * dy, t;
    if (l2 < 1e-9f)
        return a;
    t = ((p.x - a.x) * dx + (p.y - a.y) * dy) / l2;
    if (t < 0.0f)
        t = 0.0f;
    if (t > 1.0f)
        t = 1.0f;
    return (V2){a.x + dx * t, a.y + dy * t};
}

/*  The whole path of one segment: its corridor's gates from the tile
 *  list, a smooth line through them, and the radius each corner may
 *  sweep.
 *
 *  The taut string was tried first and is wrong for a road: the
 *  shortest path hugs the inside of every bend, so the band runs along
 *  one wall of its corridor and leaves the far side of the tile bare --
 *  453 tiles of Bay View's roads had no geometry over them.  What a road
 *  wants is the smoothest line the corridor allows, near its middle
 *  where the corridor is straight.  So the line starts at the gates'
 *  midpoints and is relaxed: each point moves toward the mean of its
 *  neighbours, then back onto its own gate, over and over.  That
 *  converges on a curve which is straight where the corridor is
 *  straight, cuts a staircase into one diagonal because the gates let it,
 *  and never leaves the room it was given.
 */
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
 *  same scope, so a change to any of them could reach all the others
 *  (the user: "I smell spaghetti").  The stages now stand on their own
 *  and pass this working state between them; each one still reads the
 *  same names it always did, so the bodies are the bodies that were
 *  measured, unchanged.
 *
 *      corridor  the run's own tiles, and nothing else
 *      gates     the crossable part of each shared edge
 *      seed      the first line: a point on every gate
 *      relax     smooth it, hold the minimum radius, keep it inside
 *      collapse  straight runs back into single edges
 *      spacing   nodes far enough apart to be filleted
 *      spline    the alternative fit, when it is asked for
 *      radii     the widest legal arc at every corner
 *  ------------------------------------------------------------------ */
typedef struct
{
    uint8_t *mark;   /* corridor tiles, by index                    */
    int32_t *marked; /* which indices, so they can be cleared       */
    int      nm;
    V2      *gl, *gr; /* each gate's two ends                       */
    int      ng;
    V2      *p;      /* the line being fitted                       */
    int     *gate;   /* the gate a point sits on, or -1 for free    */
    int      n;
    float    hw, rmin, rmax, gro;
} Fit;

/*  Smooth the line, hold the minimum radius, and keep the band inside
 *  the corridor.  The three pull against each other on purpose. */
static void fit_relax(Fit *F, int old_clear)
{
    uint8_t       *mark = F->mark;
    V2            *p = F->p, *gl = F->gl, *gr = F->gr;
    int           *gate = F->gate;
    const int      n    = F->n;
    const float    hw = F->hw, rmin = F->rmin;
    int            i, it;
    static V2      prev[MAX_PTS];
    /*  The relaxation, with the corridor as a wall.
     *
     *  Each sweep moves every point toward the mean of its neighbours,
     *  which is what makes the line smooth, and then back onto its own
     *  gate.  That alone keeps the CENTRELINE inside -- both ends of a
     *  run lie on one tile's edges and a tile is convex -- but not the
     *  road: a chord that cuts across a tile corner carries its band out
     *  over the corner into a tile that is not the corridor's (the user:
     *  "even going past the corridor ... the whole road + stuff need to
     *  fit within the corridor").  So each sweep also walks the two runs
     *  either side of a point, and wherever the band's edge has left the
     *  corridor it pushes the point back across the run.
     *
     *  That single constraint is also what opens the curves.  A point
     *  that cannot hug its corner pushes back on the mean, and the
     *  neighbours it pulls carry the turn into the tiles before and
     *  after it, which is how a curve gets a radius bigger than the tile
     *  it turns in (the user: "it should be favouring elegant curves vs.
     *  tight straights that cut through things and the previous segments
     *  should help make those curves possible"). */
    for (i = 0; i < n; ++i)
        prev[i] = p[i];
    for (it = 0; it < 128; ++it)
    {
        for (i = 1; i + 1 < n; ++i)
        {
            V2 mid = {0.5f * (p[i - 1].x + p[i + 1].x), 0.5f * (p[i - 1].y + p[i + 1].y)};
            V2 mv  = {p[i].x + 0.6f * (mid.x - p[i].x), p[i].y + 0.6f * (mid.y - p[i].y)};
            /*  Smoothing only.  A pull toward each gate's midpoint was
             *  tried and is wrong: on a staircase the midpoints zigzag,
             *  so centring on them makes the road snake tile by tile.
             *  What keeps the line off the walls is the margin in the
             *  clearance below, not a target in the middle. */
            p[i] = gate[i] >= 0 ? gate_clamp(mv, gl[gate[i]], gr[gate[i]]) : mv;
        }
        /*  The tightest curve the family may take.  Wherever three
         *  consecutive points turn tighter than the floor, the middle one
         *  is pulled toward the line between its neighbours, which is the
         *  one move that always opens the turn.  A rail's floor is large,
         *  so a staircase of rail tiles straightens into one diagonal
         *  instead of weaving between its pinch corners; a road's is
         *  small, because a road may be laid round a tight corner.
         *
         *  It shapes the line; it does not overrule the corridor, which
         *  is cleared after it and therefore has the last word.  Where a
         *  corner cannot be swept this wide inside its own tiles it is
         *  swept as wide as it can be, and no wider. */
        for (i = 1; i + 1 < n; ++i)
        {
            float r = turn_radius(p[i - 1], p[i], p[i + 1]);
            V2    mid, mv;
            float pull;
            if (r >= rmin)
                continue;
            mid  = (V2){0.5f * (p[i - 1].x + p[i + 1].x), 0.5f * (p[i - 1].y + p[i + 1].y)};
            pull = 1.0f - r / rmin;
            if (pull > 0.9f)
                pull = 0.9f;
            mv   = (V2){p[i].x + (mid.x - p[i].x) * pull, p[i].y + (mid.y - p[i].y) * pull};
            p[i] = gate[i] >= 0 ? gate_clamp(mv, gl[gate[i]], gr[gate[i]]) : mv;
        }
        if (old_clear)
        {
        for (i = 1; i + 1 < n; ++i)
        {
            int try_;
            /*  Push until the band is inside, not by a fixed nudge: a
             *  fixed one settles wherever the smoothing pull happens to
             *  balance it, which is a road still over the line. */
            for (try_ = 0; try_ < 16; ++try_)
            {
                int   side, run, sm, out_n = 0;
                V2    push = {0.0f, 0.0f};
                for (run = 0; run < 2; ++run)
                {
                    V2    a = run ? p[i] : p[i - 1], b = run ? p[i + 1] : p[i];
                    float dx = b.x - a.x, dy = b.y - a.y;
                    float len = sqrtf(dx * dx + dy * dy), px, py;
                    if (len < 1e-5f)
                        continue;
                    px = -dy / len;
                    py = dx / len;
                    for (sm = 0; sm <= 4; ++sm)
                    {
                        float t  = (float)sm / 4.0f;
                        float qx = a.x + dx * t, qy = a.y + dy * t;
                        for (side = -1; side <= 1; side += 2)
                        {
                            /*  A small margin, so the band is held just
                             *  inside the corridor rather than touching
                             *  it.  It cannot be large: a staircase is
                             *  only 0.707 of a tile wide across its
                             *  diagonal, so a wide margin forces the line
                             *  to weave through the tile centres instead
                             *  of running straight down it. */
                            float   ex = qx + px * (hw + s_tune.margin) * (float)side;
                            float   ey = qy + py * (hw + s_tune.margin) * (float)side;
                            int32_t tc = (int32_t)floorf(ex), tr = (int32_t)floorf(ey);
                            float   wgt;
                            if (tc >= 0 && tr >= 0 && tc < R_MAP && tr < R_MAP &&
                                mark[tr * R_MAP + tc])
                                continue;
                            /*  Out: back across the run, hardest where
                             *  this point has most say over the sample. */
                            wgt = run ? 1.0f - t : t;
                            ++out_n;
                            push.x -= px * (float)side * wgt;
                            push.y -= py * (float)side * wgt;
                        }
                    }
                }
                if (!out_n)
                    break;
                {
                    float pl = sqrtf(push.x * push.x + push.y * push.y);
                    V2    was = p[i], mv;
                    if (pl < 1e-5f)
                        break;
                    /*  A tenth of a tile a try, measured: bigger
                     *  overshoots into the far wall and smaller settles
                     *  where the smoothing pull balances it. */
                    mv   = (V2){p[i].x + push.x / pl * 0.10f, p[i].y + push.y / pl * 0.10f};
                    p[i] = gate[i] >= 0 ? gate_clamp(mv, gl[gate[i]], gr[gate[i]]) : mv;
                    if (fabsf(p[i].x - was.x) + fabsf(p[i].y - was.y) < 1e-4f)
                        break; /* the gate has no more room to give */
                }
            }
        }
        }
        else
        {
        for (i = 1; i + 1 < n; ++i)
        {
            /*  Keep the point in the space a band of THIS width can
             *  occupy, by projection rather than by shoving.
             *
             *  The smoothing does not know how wide the road is, so it
             *  will happily move a point somewhere the band cannot go.
             *  Pushing it back afterwards, a nudge at a time, leaves a
             *  kink at every place it was pushed -- and the wider the
             *  road, the more places, which is why widening it made the
             *  geometry worse rather than better (the user: "the segment
             *  doesn't grow into the available space as I increase the
             *  road width ... it's like the initial curve doesn't take
             *  the road width into account, when it should be").
             *
             *  Instead the point is pulled straight back toward where it
             *  last stood -- which was feasible -- by bisection, so it
             *  lands on the boundary of the space its own width allows
             *  and the line stays smooth. */
            V2  want = p[i], was = prev[i];
            int fits = band_fits(mark, p[i - 1], want, hw) && band_fits(mark, want, p[i + 1], hw);
            if (!fits)
            {
                float lo = 0.0f, hi = 1.0f;
                int   it2;
                for (it2 = 0; it2 < 12; ++it2)
                {
                    float mid = 0.5f * (lo + hi);
                    V2    try2 = {was.x + (want.x - was.x) * mid, was.y + (want.y - was.y) * mid};
                    if (band_fits(mark, p[i - 1], try2, hw) && band_fits(mark, try2, p[i + 1], hw))
                        lo = mid;
                    else
                        hi = mid;
                }
                p[i] = (V2){was.x + (want.x - was.x) * lo, was.y + (want.y - was.y) * lo};
                if (gate[i] >= 0)
                    p[i] = gate_clamp(p[i], gl[gate[i]], gr[gate[i]]);
            }
            prev[i] = p[i];
        }
        }
    }
}

/*  Straight runs back into single edges before the fillet. */
static void fit_collapse(Fit *F)
{
    V2 *p = F->p;
    int n = F->n;
    /*  Every gate keeps its point, so the line cannot skip a tile and
     *  every piece of road has geometry over it by construction, with
     *  nothing to check afterwards.  Simplifying it was tried and
     *  dropped: a chord that replaced two runs would bypass a tile, and
     *  299 tiles of Bay View went bare.  The smoothness comes from the
     *  relaxation above and from the sweep at each joint, not from
     *  throwing points away.
     *
     *  A straight run still costs one piece: the fillet skips the
     *  points that do not turn. */
    /*  Straight runs back into single edges before the fillet.  The
     *  free points between the gates are there to let the line bow; once
     *  it has, a point that lies on the line between its neighbours says
     *  nothing, and leaving it there halves the edges either side of a
     *  bend -- which caps the sweep, because a fillet may not eat more
     *  than half an edge.  A 45 degree bend that its corridor would allow
     *  three tiles of radius was coming out at about half a tile (the
     *  user: "the curves still look like shit, hugging too tight").  The
     *  tolerance is a fiftieth of a tile, so the line keeps its shape and
     *  cannot skip a tile. */
    {
        const float tol = 0.02f;
        int         w = 1, keep = 0, r;
        for (r = 1; r + 1 < n; ++r)
        {
            /*  Can the run from the last kept point to r+1 replace every
             *  point between them?  Each is measured against THAT line,
             *  not against the line as it stood when it was dropped, so
             *  the error cannot accumulate along a run. */
            V2    a = p[keep], c2 = p[r + 1];
            float ex = c2.x - a.x, ey = c2.y - a.y;
            float el = sqrtf(ex * ex + ey * ey);
            int   k2, ok = el > 1e-5f;
            for (k2 = keep + 1; k2 <= r && ok; ++k2)
                if (fabsf((p[k2].x - a.x) * ey - (p[k2].y - a.y) * ex) / el > tol)
                    ok = 0;
            if (ok)
                continue; /* the whole run lies on one line: drop it */
            p[w++] = p[r];
            keep   = r;
        }
        p[w++] = p[n - 1];
        n      = w;
    }
    F->n = n;
}

/*  Nodes far enough apart to be swept. */
static void fit_spacing(Fit *F, int32_t ex0, int32_t ex1)
{
    uint8_t       *mark = F->mark;
    const int32_t *marked = F->marked;
    const int      nm     = F->nm;
    V2            *p      = F->p;
    int           *gate   = F->gate;
    int            n      = F->n;
    const float    hw = F->hw, rmin = F->rmin;
    int            i;
    /*  Nodes far enough apart to be filleted (the user: "the nodes that
     *  compose the segments get too close together, allowing very illegal
     *  angles at the macro scales").
     *
     *  A gate at every shared edge plus a free point between each pair
     *  leaves nodes about a third of a tile apart, while a fillet of
     *  radius R at a deflection theta needs HALF an edge of R*tan(theta/2)
     *  either side of it -- 0.9 of a tile at the road's own minimum radius
     *  through a right angle, nearly triple the room there is.  So the
     *  corner search finds nothing and the corner is emitted hard, and a
     *  run of those bunched hard corners is the macro-scale angle that
     *  should have been one sweeping curve.
     *
     *  A node is dropped only when the line that replaces it stays inside
     *  the corridor AND still covers every tile the two edges it replaces
     *  covered -- which is what an earlier simplifier got wrong, leaving
     *  299 tiles of Bay View with no geometry over them. */
    {
        static uint8_t cov[R_MAP * R_MAP];
        static uint8_t stray_t[R_MAP * R_MAP];
        static uint8_t stamp = 0, stray_stamp = 0;
        int            pass, dropped = 1;
        /*  SC2K_NO_SPACING=1 leaves the nodes where the fit put them, for
         *  a side by side of what the spacing buys. */
        if (getenv("SC2K_NO_SPACING"))
            dropped = 0;
        for (pass = 0; pass < 8 && dropped; ++pass)
        {
            dropped = 0;
            for (i = 1; i + 1 < n; ++i)
            {
                V2    a = p[i - 1], b = p[i], c2 = p[i + 1];
                float ax = b.x - a.x, ay = b.y - a.y;
                float bx = c2.x - b.x, by = c2.y - b.y;
                float la = sqrtf(ax * ax + ay * ay), lb = sqrtf(bx * bx + by * by);
                float dot, theta, need, shorter;
                int   t2, ok = 1;
                uint8_t pair;
                if (la < 1e-5f || lb < 1e-5f)
                    continue;
                dot = (ax * bx + ay * by) / (la * lb);
                if (dot > 1.0f)
                    dot = 1.0f;
                if (dot < -1.0f)
                    dot = -1.0f;
                theta   = acosf(dot);
                need    = 2.0f * rmin * tanf(0.5f * theta);
                shorter = la < lb ? la : lb;
                if (theta < 1e-3f || shorter >= need)
                    continue; /* this corner already has room to be swept */
                if (++stray_stamp == 0)
                {
                    memset(stray_t, 0, sizeof stray_t);
                    stray_stamp = 1;
                }
                band_stray(mark, a, b, hw, stray_t, stray_stamp);
                band_stray(mark, b, c2, hw, stray_t, stray_stamp);
                if (!band_stray_within(mark, a, c2, hw, stray_t, stray_stamp))
                {
                    if (getenv("SC2K_SPACE_DUMP"))
                        printf("SPACE node %d at %.2f,%.2f short %.2f need %.2f: "
                               "the merged line strays further, onto tile %d,%d\n",
                               i, (double)b.x, (double)b.y, (double)shorter, (double)need,
                               s_stray_hit >= 0 ? (int)(s_stray_hit % R_MAP) : -1,
                               s_stray_hit >= 0 ? (int)(s_stray_hit / R_MAP) : -1);
                    continue;
                }
                /*  Two stamps: the pair's tiles, then the merge's over
                 *  the top.  A corridor tile still wearing the pair's
                 *  stamp is one the merge stopped covering. */
                if (stamp + 2 < stamp) /* unreachable; keeps the intent plain */
                    stamp = 0;
                if (stamp > 250)
                {
                    memset(cov, 0, sizeof cov);
                    stamp = 0;
                }
                pair = ++stamp;
                band_cover(a, b, hw, cov, pair);
                band_cover(b, c2, hw, cov, pair);
                band_cover(a, c2, hw, cov, ++stamp);
                for (t2 = 0; t2 < nm && ok; ++t2)
                {
                    /*  A junction tile takes its surface from the
                     *  junction polygon, not from the strip that arrives
                     *  at it, so the segment need not cover it -- and
                     *  requiring it to was pinning the last node hard
                     *  against the junction, leaving an edge too short
                     *  for any fillet (the user, of a rail corner at
                     *  58,95: "there would've been a legal arc if the
                     *  segment was pushed out"). */
                    if (marked[t2] == ex0 || marked[t2] == ex1)
                        continue;
                    if (cov[marked[t2]] == pair)
                        ok = 0; /* a tile the pair covered and the merge does not */
                }
                if (!ok)
                {
                    if (getenv("SC2K_SPACE_DUMP"))
                        printf("SPACE node %d at %.2f,%.2f short %.2f need %.2f: "
                               "the merge would leave a tile bare\n",
                               i, (double)b.x, (double)b.y, (double)shorter, (double)need);
                    continue;
                }
                for (t2 = i; t2 + 1 < n; ++t2)
                {
                    p[t2]    = p[t2 + 1];
                    gate[t2] = gate[t2 + 1];
                }
                --n;
                ++dropped;
            }
        }
    }
    F->n = n;
}

/*  The widest arc each corner will take inside its corridor. */
static void fit_radii(Fit *F, V2 *out, float *rad, int n, int splined)
{
    static uint8_t near_end[MAX_PTS];
    uint8_t    *mark = F->mark;
    const float hw = F->hw, gro = F->gro;
    float       rmax = F->rmax;
    int         k;
    /*  Each corner takes the largest radius whose arc stays in the
     *  corridor: the sweep is as wide as the room allows, which is the
     *  point of a band narrower than its tile.  A joint between two
     *  gates is rounded, not cut, so its arc stays by its own tile,
     *  whatever wider radius the caller asks for.  The ceiling was 0.45
     *  of a tile, which a band 0.71 wide could rarely reach anyway; a
     *  narrower band has the room, and a diagonal's 45 degree joints
     *  sweep properly at more than a tile of radius. */
    /*  No ceiling of its own beyond the caller's: the corridor decides
     *  how wide a corner may sweep, and a cap here only stopped the ones
     *  that had room (the user: "the curves still look like shit, hugging
     *  too tight"). */
    if (rmax > 8.0f)
        rmax = 8.0f;
    for (k = 0; k < n; ++k)
        rad[k] = 0.0f;
    /*  A straight approach at both ends.  No corner within this distance
     *  of a node may be swept, so every arm enters its junction along a
     *  straight tangent and the junction has a stable heading to trim
     *  against.  Arms that arrive already curving are what make an
     *  intersection read as a blob. */
    {
        /*  The straight a segment holds as it leaves a junction scales
         *  with the band, like the junction itself: a wider road leaves
         *  on its heading for longer and only then begins to turn, so
         *  the end segment is what guides the rest of the run (the user:
         *  "when I increase the road width, I expect the connect (end)
         *  segment of the road to be the one guiding the rest of the
         *  segment").  `gro` is the width against the family's default,
         *  so the shipped widths keep the tuned number. */
        const float approach = s_tune.approach * (getenv("SC2K_NOSCALE") ? 1.0f : gro);
        float       run[MAX_PTS];
        float       acc = 0.0f;
        run[0] = 0.0f;
        for (k = 1; k < n; ++k)
        {
            acc += sqrtf((out[k].x - out[k - 1].x) * (out[k].x - out[k - 1].x) +
                         (out[k].y - out[k - 1].y) * (out[k].y - out[k - 1].y));
            run[k] = acc;
        }
        for (k = 0; k < n; ++k)
        {
            near_end[k] = run[k] < approach || acc - run[k] < approach;
            /*  Unless the corner is a real turn.  Reserving a straight
             *  approach is meant to stop an arm arriving mid-curve; it is
             *  not licence to draw a right angle.  Suppressing the sweep
             *  at a sharp corner exempted the tightest turns in the whole
             *  network from the minimum radius, which is exactly where
             *  they were needed (the user, on a bend at Toronto 58,97:
             *  "that sharp curve should be illegal ... it's 90 degrees").
             *  Past fifteen degrees the corner keeps its arc. */
            if (near_end[k] && k > 0 && k + 1 < n)
            {
                V2    u1 = {out[k].x - out[k - 1].x, out[k].y - out[k - 1].y};
                V2    u2 = {out[k + 1].x - out[k].x, out[k + 1].y - out[k].y};
                float l1 = sqrtf(u1.x * u1.x + u1.y * u1.y);
                float l2 = sqrtf(u2.x * u2.x + u2.y * u2.y);
                if (l1 > 1e-5f && l2 > 1e-5f)
                {
                    float dot = (u1.x * u2.x + u1.y * u2.y) / (l1 * l2);
                    if (dot < 0.966f) /* more than fifteen degrees */
                        near_end[k] = 0;
                }
            }
        }
    }
    for (k = 1; k + 1 < n && !splined; ++k)
    {
        if (near_end[k])
            continue;
        V2    a = out[k - 1], b = out[k], c3 = out[k + 1];
        V2    ui = {b.x - a.x, b.y - a.y}, uo = {c3.x - b.x, c3.y - b.y};
        float li = sqrtf(ui.x * ui.x + ui.y * ui.y), lo = sqrtf(uo.x * uo.x + uo.y * uo.y);
        float dot, theta, r;
        if (li < 1e-5f || lo < 1e-5f)
            continue;
        ui.x /= li;
        ui.y /= li;
        uo.x /= lo;
        uo.y /= lo;
        dot = ui.x * uo.x + ui.y * uo.y;
        if (dot > 0.9999f)
            continue;
        theta = acosf(dot < -1.0f ? -1.0f : dot);
        /*  Finely, not in big steps: the corridor decides the ceiling
         *  and a coarse search lands well under it, which is what turns
         *  a curve back into a kink. */
        /*  Never below the band's own half width.  Offsetting a curve of
         *  radius R by d gives an inner edge of radius R - d, so a fillet
         *  tighter than the half width turns that edge inside out: the
         *  two kerbs cross and the ribbon ties itself in a knot (the
         *  user: "you can see in the width where it's doing some kind of
         *  wild turn").  A corner with no room for a legal arc is a hard
         *  corner, which is what a T junction is. */
        for (r = rmax; r > hw * 1.05f; r *= 0.88f)
        {
            float d     = r * tanf(0.5f * theta);
            float cross = ui.x * uo.y - ui.y * uo.x;
            V2    t1 = {b.x - ui.x * d, b.y - ui.y * d}, cen;
            V2    nrm = cross > 0.0f ? (V2){-ui.y, ui.x} : (V2){ui.y, -ui.x};
            int   ok  = 1, sm;
            if (d > 0.5f * li || d > 0.5f * lo)
                continue;
            cen = (V2){t1.x + nrm.x * r, t1.y + nrm.y * r};
            for (sm = 0; sm <= 8 && ok; ++sm)
            {
                float ang = atan2f(t1.y - cen.y, t1.x - cen.x) + (cross > 0.0f ? theta : -theta) * (float)sm / 8.0f;
                if (!corridor_holds(mark, cen.x + r * cosf(ang), cen.y + r * sinf(ang), hw))
                    ok = 0;
            }
            if (ok)
            {
                rad[k] = r;
                break;
            }
        }
        /*  Nothing the corridor allows is wide enough: the corner stays
         *  a corner rather than overhanging.  A curve belongs inside its
         *  corridor, which is what the corridor is for (the user: "a
         *  curve should never be allowed to overhang tiles.  the whole
         *  point was to fit nice curves within the corridor"). */
    }
}

int path_fit(const RCity *c_unused, const int32_t *tcol, const int32_t *trow, int nt, float hw, V2 start, V2 goal, float rmax, float rmin, float gro, int32_t ex0, int32_t ex1, V2 *out, float *rad, int cap)
{
    static V2      gl[MAX_PTS], gr[MAX_PTS], p[MAX_PTS];
    /*  Two ways to keep the line inside its corridor, chosen by the
     *  "wide fit" knob (SC2K_OLD_CLEAR=1 forces the first, for headless
     *  comparisons).  The first nudges a point out of a violation a step
     *  at a time; the second projects it back into the room its own
     *  width allows.  The second holds up far better as the width grows
     *  -- hard corners across Toronto grow 1.6x from a quarter-tile road
     *  to a three-quarter one, against 3.1x for the first -- and the
     *  first is the better of the two at the shipped width, so it stays
     *  the default and the choice is the person looking at the city's. */
    int            old_clear;
    static int     gate[MAX_PTS]; /* the gate a point belongs to, or -1: free */
    static uint8_t mark[R_MAP * R_MAP];
    static int32_t marked[MAX_PTS];
    int            ng = 0, n = 0, i, k, nm = 0, splined = 0;
    Fit            F;
    (void)c_unused; /* the corridor is the run's own cells; nothing else is consulted */
    old_clear = s_tune.wide_fit > 0.5f ? 0 : 1;
    if (getenv("SC2K_OLD_CLEAR"))
        old_clear = 1;
    if (nt < 1 || cap < 2)
        return 0;
    for (i = 0; i < nt && nm < MAX_PTS; ++i)
        if (tcol[i] >= 0 && trow[i] >= 0 && tcol[i] < R_MAP && trow[i] < R_MAP && !mark[trow[i] * R_MAP + tcol[i]])
        {
            mark[trow[i] * R_MAP + tcol[i]] = 1;
            marked[nm++]                    = trow[i] * R_MAP + tcol[i];
        }
    /*  The corridor is the cells the network occupies and nothing else.
     *  Widening it onto the bare ground beside it was tried and is
     *  wrong: the cells an edge occupies are its exact spatial limits.
     *  What buys a straight diagonal is not more ground, it is a
     *  carriageway narrower than the cell -- the widest straight ribbon a
     *  staircase at heading t can hold is 1/(|cos t| + |sin t|), which is
     *  0.707 at 45 degrees, and a road 0.5 wide fits inside that at every
     *  heading. */
    for (i = 0; i + 1 < nt && ng + 2 < MAX_PTS; ++i)
    {
        int32_t dc = tcol[i + 1] - tcol[i], dr = trow[i + 1] - trow[i];
        if ((dc == 0) == (dr == 0))
            continue; /* not a 4-step: no gate to build */
        gate_between(tcol[i], trow[i], tcol[i + 1], trow[i + 1], hw, &gl[ng], &gr[ng]);
        ++ng;
    }
    /*  Step two of the second handoff -- total least squares per run,
     *  split on containment -- is the right shape and is NOT implemented
     *  here.  A first attempt fitted each run independently and emitted
     *  the projections of its ends, so consecutive fits did not share a
     *  vertex: the ribbon jumped between them and 28 tiles came out with
     *  no road over them at all.  Doing it properly needs the fit to be
     *  chained through a shared vertex, and coverage tested as well as
     *  containment.  Until then the gate fit below stands: it is a
     *  different route to the same place -- the line is held to pass
     *  through every tile edge's crossable window, which is what keeps a
     *  diagonal straight and inside its cells. */
    /*  The line: the ends are fixed, one point on every gate, and one
     *  free point between each pair of them.
     *
     *  A point per gate alone cannot round a turn.  Where a corridor
     *  turns inside a tile the path crosses it as one chord from gate to
     *  gate, and a chord carries its band out over the tile's corner
     *  with no vertex in between to bow inward.  The free points are
     *  that freedom: they belong to no gate, they may sit anywhere the
     *  band fits, and it is they that let a turn become an arc that
     *  starts in the tile before it. */
    p[n++]   = start;
    gate[0]  = -1;
    for (i = 0; i < ng && n + 2 < MAX_PTS; ++i)
    {
        V2 g = {0.5f * (gl[i].x + gr[i].x), 0.5f * (gl[i].y + gr[i].y)};
        V2 back = p[n - 1];
        gate[n] = -1;
        p[n++]  = (V2){0.5f * (back.x + g.x), 0.5f * (back.y + g.y)};
        gate[n] = i;
        p[n++]  = g;
    }
    if (n + 2 < MAX_PTS)
    {
        gate[n] = -1;
        p[n]    = (V2){0.5f * (p[n - 1].x + goal.x), 0.5f * (p[n - 1].y + goal.y)};
        ++n;
    }
    gate[n] = -1;
    p[n++]  = goal;
    F.mark   = mark;
    F.marked = marked;
    F.nm     = nm;
    F.gl     = gl;
    F.gr     = gr;
    F.ng     = ng;
    F.p      = p;
    F.gate   = gate;
    F.n      = n;
    F.hw     = hw;
    F.rmin   = rmin;
    F.rmax   = rmax;
    F.gro    = gro;
    fit_relax(&F, old_clear);
    fit_collapse(&F);
    fit_spacing(&F, ex0, ex1);
    n = F.n;
    for (i = 0; i < n && i < cap; ++i)
        out[i] = p[i];
    n = n < cap ? n : cap;
    /*  The corridor fit as it is solved elsewhere, when asked for: the
     *  line becomes the curve its control points define, so it has no
     *  corners to sweep and the fillet search below is skipped. */
    if (s_tune.spline > 0.5f)
    {
        static V2 sp[MAX_PTS];
        int       ms = spline_fit(mark, marked, nm, out, n, hw, rmin, sp,
                                  cap < MAX_PTS ? cap : MAX_PTS);
        if (ms >= 2)
        {
            memcpy(out, sp, (size_t)ms * sizeof(V2));
            n       = ms;
            splined = 1;
        }
    }
    /*  SC2K_PATH_DUMP=1 prints every stage of every segment in a form
     *  tools/plan.py draws as a plan view: the corridor's tiles, its
     *  gates and the fitted line.  Seeing the stages from above is how
     *  this is worked on (the user: "you can probably do all this in 2D
     *  for previews eh? ... much easier for you to see too"). */
    F.n = n;
    fit_radii(&F, out, rad, n, splined);
    /*  SC2K_CURVE_DUMP=1: every corner of every fitted path -- where it
     *  is, how far it turns, and the radius it was given.  A radius of 0
     *  is a hard corner, infinite curvature; anything under the band's
     *  own half width would turn the inner kerb inside out.  This is the
     *  metric that says whether the geometry is legal, sampled at every
     *  corner rather than judged by eye (the user: "use a metric that
     *  samples every point of the road segment"). */
    s_last_splined = splined;
    if (getenv("SC2K_CURVE_DUMP"))
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
             *  three points, which is what a curve has and a corner does
             *  not -- the one number that compares a fillet fit with a
             *  fit that has no fillets. */
            printf("CURVE %.3f %.3f %.1f %.3f %.3f %.3f %.4f %.4f %d\n", (double)out[k].x,
                   (double)out[k].y, (double)(acosf(dot) * 57.2958f), (double)rad[k], (double)hw,
                   (double)turn_radius(out[k - 1], out[k], out[k + 1]), (double)li, (double)lo,
                   splined);
        }
    if (getenv("SC2K_PATH_DUMP"))
    {
        int d;
        printf("PATH hw=%.3f\nTILES", (double)hw);
        for (d = 0; d < nt; ++d)
            printf(" %d,%d", (int)tcol[d], (int)trow[d]);
        printf("\nGATES");
        for (d = 0; d < ng; ++d)
            printf(" %.3f,%.3f,%.3f,%.3f", (double)gl[d].x, (double)gl[d].y, (double)gr[d].x, (double)gr[d].y);
        printf("\nPTS");
        for (d = 0; d < n; ++d)
            printf(" %.3f,%.3f", (double)out[d].x, (double)out[d].y);
        printf("\nRAD");
        for (d = 0; d < n; ++d)
            printf(" %.3f", (double)rad[d]);
        printf("\n");
    }
    for (i = 0; i < nm; ++i)
        mark[marked[i]] = 0;
    return n;
}

/* ---- stage one, upright: the corridor's grade ---------------------------- */




/* ---- the segment pipeline (the road spec, part 3.10) ------------------ */

/*  A network is walked as segments between its nodes, the junction and
 *  end tiles; every other tile has two links and lies on one segment.
 *  A segment's centreline is the polyline through its tiles' centres,
 *  from the side of the junction box it leaves to the side it reaches.
 *  The polyline is straightened -- a staircase of corners becomes one
 *  straight line, at 45 degrees or 2:1 -- and every remaining bend is
 *  filleted with an arc, the quarter circle of a lone corner or the
 *  gentler sweep where a diagonal meets the grid.  Then one strip is
 *  lofted along the whole path by arc length, its width a function of
 *  the direction at each sample, its dashes and crosswalks placed by
 *  the distance from the junction.  There is no join inside a segment
 *  to get wrong, and a segment meets its junction box square on. */





/*  ==================================================================
 *  Pieces
 *
 *  A fitted line becomes straights and arcs; everything downstream walks
 *  pieces by arc length rather than by point.
 *  ================================================================== */
/*  Fillet the polyline into straights and arcs.  At each bend the arc's
 *  radius is the class's, clamped so the tangent points stay within the
 *  adjacent edges (spec 3.10, step 3). */
int fillet_r(const V2 *q, int n, const float *rad, Piece *out, int *count)
{
    int i, np = 0;
    V2  cur = q[0];
    for (i = 1; i < n - 1; ++i)
    {
        V2    u_in  = {q[i].x - q[i - 1].x, q[i].y - q[i - 1].y};
        V2    u_out = {q[i + 1].x - q[i].x, q[i + 1].y - q[i].y};
        float lin = v2len(u_in), lout = v2len(u_out);
        float cross, dot, theta, d, R;
        V2    t1, t2, cen, n1;
        if (lin < 1e-6f || lout < 1e-6f)
            continue;
        u_in.x /= lin;
        u_in.y /= lin;
        u_out.x /= lout;
        u_out.y /= lout;
        cross = u_in.x * u_out.y - u_in.y * u_out.x;
        dot   = u_in.x * u_out.x + u_in.y * u_out.y;
        if (dot > 0.9999f)
            continue;                             /* straight on: no vertex */
        theta = acosf(dot < -1.0f ? -1.0f : dot); /* the turn, 0..pi */
        /* the tangent points at distance d from the vertex; R clamped to the edges */
        R = rad[i];
        if (R <= 0.0f)
        {
            /*  A corner the corridor gives no room to sweep stays a
             *  corner: the line runs to it and turns.  Skipping the
             *  vertex instead lost the path's shape -- with every corner
             *  skipped a segment became one straight line between its
             *  ends, which left the tiles it should have run through
             *  bare (Bay View's row 0, 246 tiles). */
            if (v2len((V2){q[i].x - cur.x, q[i].y - cur.y}) > 1e-4f)
            {
                if (np + 1 > MAX_PIECES)
                    return -1;
                out[np].arc = 0;
                out[np].a   = cur;
                out[np].b   = q[i];
                out[np].len = v2len((V2){q[i].x - cur.x, q[i].y - cur.y});
                ++np;
                cur = q[i];
            }
            continue;
        }
        {
            float lim   = 0.5f * (lin < lout ? lin : lout);
            float tan_h = tanf(0.5f * theta);
            if (R * tan_h > lim)
                R = lim / tan_h;
        }
        if (R < 0.04f)
            R = 0.04f;
        d  = R * tanf(0.5f * theta);
        t1 = (V2){q[i].x - u_in.x * d, q[i].y - u_in.y * d};
        t2 = (V2){q[i].x + u_out.x * d, q[i].y + u_out.y * d};
        /* the arc's centre: off t1 along the inward normal of u_in, toward the turn */
        n1    = cross > 0.0f ? (V2){-u_in.y, u_in.x} : (V2){u_in.y, -u_in.x};
        cen   = (V2){t1.x + n1.x * R, t1.y + n1.y * R};
        if (np + 2 > MAX_PIECES)
            return -1;
        if (v2len((V2){t1.x - cur.x, t1.y - cur.y}) > 1e-4f)
        {
            out[np].arc = 0;
            out[np].a   = cur;
            out[np].b   = t1;
            out[np].len = v2len((V2){t1.x - cur.x, t1.y - cur.y});
            ++np;
        }
        out[np].arc = 1;
        out[np].c   = cen;
        out[np].r   = R;
        out[np].t0  = atan2f(t1.y - cen.y, t1.x - cen.x);
        out[np].t1  = out[np].t0 + (cross > 0.0f ? theta : -theta);
        out[np].len = R * theta;
        ++np;
        cur = t2;
    }
    if (np + 1 > MAX_PIECES)
        return -1;
    if (v2len((V2){q[n - 1].x - cur.x, q[n - 1].y - cur.y}) > 1e-4f)
    {
        out[np].arc = 0;
        out[np].a   = cur;
        out[np].b   = q[n - 1];
        out[np].len = v2len((V2){q[n - 1].x - cur.x, q[n - 1].y - cur.y});
        ++np;
    }
    *count = np;
    return 0;
}

int fillet(const V2 *q, int n, float rmax, Piece *out, int *count)
{
    static float rad[MAX_PTS];
    int          i;
    for (i = 0; i < n && i < MAX_PTS; ++i)
        rad[i] = rmax;
    return fillet_r(q, n, rad, out, count);
}

/*  The point and unit direction at distance `t` along a piece. */
static void piece_at(const Piece *p, float t, V2 *pos, V2 *dir)
{
    if (!p->arc)
    {
        float f = p->len > 1e-6f ? t / p->len : 0.0f;
        pos->x  = p->a.x + (p->b.x - p->a.x) * f;
        pos->y  = p->a.y + (p->b.y - p->a.y) * f;
        dir->x  = (p->b.x - p->a.x) / (p->len > 1e-6f ? p->len : 1.0f);
        dir->y  = (p->b.y - p->a.y) / (p->len > 1e-6f ? p->len : 1.0f);
    }
    else
    {
        float f  = p->len > 1e-6f ? t / p->len : 0.0f;
        float th = p->t0 + (p->t1 - p->t0) * f;
        float s  = p->t1 > p->t0 ? 1.0f : -1.0f;
        pos->x   = p->c.x + p->r * cosf(th);
        pos->y   = p->c.y + p->r * sinf(th);
        dir->x   = -sinf(th) * s;
        dir->y   = cosf(th) * s;
    }
}

typedef struct
{
    V2    pos, dir;
    float s, z; /* z: the section's height, the highest ground it spans */
    float xd;   /* the distance along to the nearest level crossing on the segment */
} Sample;

/*  ==================================================================
 *  Stations, records and the knobs
 *
 *  What the cross-section stands on, what the traffic model is told, and
 *  the live tuning values.
 *  ================================================================== */
/*  The height a cross-section sits at: the highest of the ground under
 *  its centre and its two edges, so the terrain never cuts through the
 *  road (the user: "raise roadways if they're going to cut into the
 *  terrain"); where that lifts it, the skirts below make the fill. */
static float section_height(const RCity *c, uint8_t mask_bit, V2 pos, V2 dir, float h)
{
    /*  Five points across the band, and the section sits at their MEAN,
     *  not their highest (the user: "I still see rails being lifted",
     *  and on the sliver walls a raised road left: "I don't see
     *  engineered walls on the raised segments").  Taking the highest
     *  lifted the whole band clear of the ground wherever the ground
     *  fell away across its width, so a rail floated on a cross-slope
     *  and the fill under it was two metres at the most.  At the mean
     *  the band cuts the high side of the section and fills the low,
     *  which is what a formation is.  Tried and reverted for now: with
     *  the band at the mean the corner cap, which may lower the ground
     *  by a level at the most, cannot always take the high side down to
     *  it, and 54 cities showed terrain through the band.  It belongs
     *  with the notch, where the corridor's tiles are graded outright,
     *  not with the capping. */
    float z = surface_at_world(c, mask_bit, pos.x, pos.y);
    int   k;
    for (k = -2; k <= 2; ++k)
    {
        float o  = (float)k * 0.5f * h;
        float zk = surface_at_world(c, mask_bit, pos.x + dir.y * o, pos.y - dir.x * o);
        if (zk > z)
            z = zk;
    }
    return z;
}

/*  Loft one strip along the pieces: cross-sections by arc length, their
 *  width the class's scaled by the direction (the snap view's
 *  compensation), one quad per pair, each with the painter's order of
 *  the tile under it.  `zeb0`/`zeb1`: the length of the crosswalk band
 *  at either end, 0 for none. */
/*  Record a road segment's stations in the mesh's network, for the
 *  traffic.  The lane centres by class, tiles from the centreline: a
 *  road's one lane each way at half the carriageway, an avenue's outer
 *  and inner lanes, a boulevard's outer and inner between median and
 *  curb, where the material draws its lines. */
static int net_record(RRoadNet *net, const Sample *smp, int ns, float total, int cls, int rail)
{
    RNetSeg *sg;
    int      i;
    if (net->n_pts + (uint32_t)ns > net->cap_pts)
    {
        uint32_t nc = net->cap_pts ? net->cap_pts * 2u : 4096u;
        RNetPt  *np;
        while (nc < net->n_pts + (uint32_t)ns)
            nc *= 2u;
        np = (RNetPt *)realloc(net->pts, nc * sizeof *np);
        if (!np)
            return -1;
        net->pts     = np;
        net->cap_pts = nc;
    }
    if (net->n_segs + 1u > net->cap_segs)
    {
        uint32_t nc = net->cap_segs ? net->cap_segs * 2u : 256u;
        RNetSeg *np = (RNetSeg *)realloc(net->segs, nc * sizeof *np);
        if (!np)
            return -1;
        net->segs     = np;
        net->cap_segs = nc;
    }
    sg        = &net->segs[net->n_segs++];
    sg->first = net->n_pts;
    sg->count = (uint32_t)ns;
    sg->total = total;
    sg->cls   = cls;
    memcpy(sg->node, s_seg_node, sizeof sg->node);
    sg->kind[0]  = s_seg_kind[0];
    sg->kind[1]  = s_seg_kind[1];
    sg->ctrl[0]  = s_seg_ctrl[0];
    sg->ctrl[1]  = s_seg_ctrl[1];
    /*  Where the traffic runs, across the strip.  These are fractions of
     *  the road's own width, so a change of width carries the lanes with
     *  it instead of leaving the cars off the asphalt. */
    sg->lane_out = ROAD_W * (cls == 0 ? 0.200f : cls == 1 ? 0.306f : 0.317f);
    sg->lane_in  = ROAD_W * (cls == 0 ? 0.200f : cls == 1 ? 0.115f : 0.162f);
    if (s_hiway)
    {
        /*  A deck's lanes, from the 7.1 section: the half deck is a tile
         *  and holds three of them, their centres 2.75, 6.45 and 10.15 m
         *  out of 14.9.  Two lanes carry the traffic here, so it runs in
         *  the one against the median and the one against the shoulder. */
        sg->lane_in  = 0.185f;
        sg->lane_out = 0.681f;
    }
    if (rail)
        sg->lane_out = sg->lane_in = 0.133f; /* a rail: the track to the right of travel */
    for (i = 0; i < ns; ++i)
    {
        RNetPt *q = &net->pts[net->n_pts++];
        q->x      = smp[i].pos.x;
        q->y      = smp[i].pos.y;
        q->z      = smp[i].z;
        q->dx     = smp[i].dir.x;
        q->dy     = smp[i].dir.y;
        q->s      = smp[i].s;
    }
    return 0;
}

/*  Whether a station's tile is a level crossing or within a tile of one. */
static int near_crossing(const RCity *c, V2 pos)
{
    int32_t col = (int32_t)floorf(pos.x), row = (int32_t)floorf(pos.y), dc, dr;
    for (dr = -1; dr <= 1; ++dr)
        for (dc = -1; dc <= 1; ++dc)
        {
            int32_t cc = col + dc, rr = row + dr;
            uint8_t b;
            if (cc < 0 || rr < 0 || cc >= R_MAP || rr >= R_MAP)
                continue;
            b = c->xbld[rr * R_MAP + cc];
            if (b >= 0x45u && b <= 0x48u)
                return 1;
        }
    return 0;
}

static float s_zorig[8192];

/*  The ground under a sunken road follows it (the user: "I think a lot
 *  of the carving issues could be resolved if we simply modified the
 *  height of the tile containing the road"): where a vertical curve
 *  sinks a road below the ground, the corners of the tiles it passes
 *  through are capped to a hair under the road and the mesh is built
 *  again on that field, so the terrain bends into the cut instead of
 *  being carved out of it.  One height per grid corner, shared by the
 *  four tiles around it, so the bend is continuous and the walls close
 *  what is left. */
float   s_zcap[GRID * GRID];
uint8_t s_corr[GRID * GRID];  /* the corner belongs to a corridor: its height is the corridor's */
float   s_zlow[GRID * GRID];
RRoadTune s_tune = {0.50f, 0.62f, 0.9f, 3.0f, 6.0f, 8.0f, 0.8f, 0.04f, 0.45f, 0.0f, 0.0f, 0.0f};

float *mesh_tune(void)
{
    return &s_tune.road_w; /* twelve floats, in the struct's own order */
}

float        s_tilez[R_MAP * R_MAP * 4];
static float s_tiled[R_MAP * R_MAP * 4]; /* per corner: how far the station that set it was */

/*  The profile's own height at an arc length, interpolated between the
 *  stations either side of it.  The shelf is this function of distance
 *  along the segment and nothing else, so it is ONE continuous surface
 *  the whole length of the corridor -- flat across it, because the
 *  across coordinate does not appear (the user: "the shelves are a
 *  continuous plane ... you're doing a shelf per tile").  Two tiles that
 *  share a corner ask for the same arc length and get the same answer. */
static float profile_at(const Sample *smp, int ns, float at)
{
    int lo = 0, hi = ns - 1, mid;
    if (ns < 2)
        return ns ? smp[0].z : 0.0f;
    if (at <= smp[0].s)
        return smp[0].z;
    if (at >= smp[ns - 1].s)
        return smp[ns - 1].z;
    while (hi - lo > 1)
    {
        mid = (lo + hi) / 2;
        if (smp[mid].s <= at)
            lo = mid;
        else
            hi = mid;
    }
    {
        float d = smp[hi].s - smp[lo].s;
        float t = d > 1e-6f ? (at - smp[lo].s) / d : 0.0f;
        return smp[lo].z + (smp[hi].z - smp[lo].z) * t;
    }
}

void s_tile_reset(int32_t i)
{
    s_tiled[i] = 1e9f;
}
float   s_zdist[GRID * GRID]; /* how far the nearest station that set it was */
int   s_pass; /* 0 not capping, 1 collecting the caps, 2 building on them */

static int put_prism_clip_m(RMesh *m, const RCity *c, uint8_t mask_bit, float order, float cx, float cy, float dx, float dy, float len, float wid, float zb, float zf, float z0, float z1, float paint, float mat);


/*  ------------------------------------------------------------------
 *  Lofting, stage by stage.  The ribbon is swept from the stations, but
 *  three tasks that ride along with it -- the furniture beside the line,
 *  the corridor surface under it, and the overlay drawn over it for
 *  looking at -- are their own jobs and stand on their own here.
 *  ------------------------------------------------------------------ */

/*  ==================================================================
 *  Lofting
 *
 *  The ribbon swept from the stations, with three jobs of its own beside
 *  it: the furniture that stands next to the line, the corridor surface
 *  notched under it, and the overlay drawn over it for looking at.
 *  ================================================================== */
/*  Signals, lighting, and everything else that stands BESIDE the line
 *  rather than being part of the ribbon. */
static int loft_furniture(RMesh *m, const RCity *c, uint8_t mask_bit, int comp, Family f,
                          Sample *smp, int ns, float hw, float mat, float total, int pin0,
                          int pin1)
{
    int i;
    (void)comp;
    (void)mat;
    (void)total;
    /*  A rail's signalling (spec 5.6), right-hand running: the track to
     *  the right of the walk carries traffic forward and its signals
     *  stand on its outer side facing back; the other track's face
     *  forward on the other side.  A block signal every ten tiles,
     *  green; an absolute one, red, a tile before a junction; a whistle
     *  post two tiles before a level crossing; none on or beside a
     *  crossing. */
    /*  Street lighting (spec 1.6, 6.4): on an avenue or a boulevard a
     *  cobra-head luminaire on a davit arm every two tiles, thirty
     *  metres, staggered side to side: a round pole on the sidewalk
     *  1.35 levels tall, ten metres, an arm three metres out over the
     *  road from its top, and the head at the arm's end; none within a
     *  tile of a junction, an end or a level crossing.  A local road
     *  goes unlit, as the spec's post-tops are a downtown's. */
    if (f == F_ROAD && s_seg_class >= 0.5f && total > 2.5f)
    {
        float at;
        int   side = 0;
        for (at = 1.25f; at < total - 1.0f; at += 2.0f, side ^= 1)
        {
            int   j = 1;
            float sgn, px, py, ax, ay, hx, hy, order;
            while (j < ns - 1 && smp[j].s < at)
                ++j;
            if (near_crossing(c, smp[j].pos))
                continue;
            sgn = side ? -1.0f : 1.0f;
            hx  = smp[j].dir.x;
            hy  = smp[j].dir.y;
            ax  = -hy * sgn; /* across, toward the pole's side */
            ay  = hx * sgn;
            px  = smp[j].pos.x + ax * hw * 0.90f;
            py  = smp[j].pos.y + ay * hw * 0.90f;
            {
                int32_t tc = (int32_t)floorf(px), tr = (int32_t)floorf(py);
                if (tc < 0 || tr < 0 || tc >= R_MAP || tr >= R_MAP)
                    continue;
                order = tile_order(c, tc, tr, mask_bit) + 0.3f;
            }
            /* the pole, on the sidewalk's height */
            if (put_prism_clip_m(m, c, mask_bit, order, px, py, hx, hy, 0.02f, 0.02f, smp[j].z, smp[j].z, 0.0f, 1.35f, 0.0f, MAT_PROP) != 0)
                return -1;
            /* the arm, from the pole's top in over the road, rising a little */
            if (put_prism_clip_m(m, c, mask_bit, order, px - ax * 0.10f, py - ay * 0.10f, -ax, -ay, 0.20f, 0.014f, smp[j].z, smp[j].z + 0.03f, 1.33f, 1.35f, 0.0f, MAT_PROP) != 0)
                return -1;
            /* the cobra head, 0.8 m long, hung at the arm's end */
            if (put_prism_clip_m(m, c, mask_bit, order, px - ax * 0.19f, py - ay * 0.19f, -ax, -ay, 0.055f, 0.025f, smp[j].z + 0.03f, smp[j].z + 0.03f, 1.30f, 1.345f, 0.0f, MAT_PROP) != 0)
                return -1;
        }
    }
    if (f == F_RAIL && ns > 2)
    {
        float sig;
        int   side;
        for (side = 0; side < 2; ++side)
        {
            float sgn = side ? -1.0f : 1.0f; /* the right track (forward) then the left (back) */
            for (sig = 5.0f; sig < total - 0.5f; sig += 10.0f)
            {
                int j = 1;
                while (j < ns - 1 && smp[j].s < sig)
                    ++j;
                if (near_crossing(c, smp[j].pos))
                    continue;
                {
                    float   rx = -smp[j].dir.y * sgn * 0.33f, ry = smp[j].dir.x * sgn * 0.33f;
                    int32_t tc = (int32_t)floorf(smp[j].pos.x + rx), tr = (int32_t)floorf(smp[j].pos.y + ry);
                    if (tc < 0 || tr < 0 || tc >= R_MAP || tr >= R_MAP)
                        continue;
                    if (put_rail_signal(m, c, mask_bit, tile_order(c, tc, tr, mask_bit) + 0.3f, smp[j].pos.x + rx, smp[j].pos.y + ry, -smp[j].dir.x * sgn, -smp[j].dir.y * sgn, 0, smp[j].s, (int)sgn) != 0)
                        return -1;
                }
            }
            /* the absolute signal before the junction this track runs toward */
            if ((side == 0 && pin1) || (side == 1 && pin0))
            {
                float at = side == 0 ? total - 1.0f : 1.0f;
                int   j  = 1;
                if (total > 2.5f)
                {
                    while (j < ns - 1 && smp[j].s < at)
                        ++j;
                    if (!near_crossing(c, smp[j].pos))
                    {
                        float   rx = -smp[j].dir.y * sgn * 0.33f, ry = smp[j].dir.x * sgn * 0.33f;
                        int32_t tc = (int32_t)floorf(smp[j].pos.x + rx), tr = (int32_t)floorf(smp[j].pos.y + ry);
                        if (tc >= 0 && tr >= 0 && tc < R_MAP && tr < R_MAP &&
                            put_rail_signal(m, c, mask_bit, tile_order(c, tc, tr, mask_bit) + 0.3f, smp[j].pos.x + rx, smp[j].pos.y + ry, -smp[j].dir.x * sgn, -smp[j].dir.y * sgn, 1, smp[j].s, (int)sgn) != 0)
                            return -1;
                    }
                }
            }
        }
        /* whistle posts: two tiles before each crossing tile, each direction, on the right */
        for (i = 1; i < ns; ++i)
        {
            int32_t col = (int32_t)floorf(smp[i].pos.x), row = (int32_t)floorf(smp[i].pos.y), pc2 = (int32_t)floorf(smp[i - 1].pos.x), pr2 = (int32_t)floorf(smp[i - 1].pos.y);
            uint8_t b;
            if (col < 0 || row < 0 || col >= R_MAP || row >= R_MAP || (col == pc2 && row == pr2))
                continue;
            b = c->xbld[row * R_MAP + col];
            if (!(b == 0x45u || b == 0x46u))
                continue;
            for (side = 0; side < 2; ++side)
            {
                float   sgn = side ? -1.0f : 1.0f;
                float   at  = smp[i].s - sgn * 2.0f;
                int     j   = 1;
                float   rx, ry;
                int32_t tc, tr;
                if (at < 0.3f || at > total - 0.3f)
                    continue;
                while (j < ns - 1 && smp[j].s < at)
                    ++j;
                rx = -smp[j].dir.y * sgn * 0.30f;
                ry = smp[j].dir.x * sgn * 0.30f;
                tc = (int32_t)floorf(smp[j].pos.x + rx);
                tr = (int32_t)floorf(smp[j].pos.y + ry);
                if (tc < 0 || tr < 0 || tc >= R_MAP || tr >= R_MAP)
                    continue;
                if (put_box(m, c, mask_bit, tile_order(c, tc, tr, mask_bit) + 0.3f, smp[j].pos.x + rx, smp[j].pos.y + ry, 0.012f, 0.012f, 0.0f, 0.19f, MAT_LAMP, 0.0f) != 0)
                    return -1;
            }
        }
    }
    /*  The road's approach to a level crossing (spec 3.15): from the RXR
     *  stencil to the stop line the lines are solid; the quads within a
     *  tile and a half of a crossing tile's centre carry the approach
     *  material with the distance to the crossing along. */
    {
        static float xs[64];
        int          nx = 0;
        if (f == F_ROAD)
            for (i = 0; i < ns && nx < 64; ++i)
            {
                int32_t col = (int32_t)floorf(smp[i].pos.x), row = (int32_t)floorf(smp[i].pos.y);
                uint8_t b;
                if (col < 0 || row < 0 || col >= R_MAP || row >= R_MAP)
                    continue;
                b = c->xbld[row * R_MAP + col];
                if ((b == 0x45u || b == 0x46u) && fabsf(smp[i].pos.x - (float)col - 0.5f) < 0.07f && fabsf(smp[i].pos.y - (float)row - 0.5f) < 0.07f)
                    xs[nx++] = smp[i].s;
            }
        for (i = 0; i < ns; ++i)
        {
            float best = 1e9f;
            int   k2;
            for (k2 = 0; k2 < nx; ++k2)
                if (fabsf(smp[i].s - xs[k2]) < best)
                    best = fabsf(smp[i].s - xs[k2]);
            smp[i].xd = best;
        }
    }
    return 0;
}

/*  The corridor's own surface: the shelf the band lies on, notched out
 *  of the terrain rather than laid over it. */
/*  Heights only: this stage writes the shelf the corridor's tiles wear
 *  and emits no geometry of its own, which is why it needs neither the
 *  mesh nor the city. */
static int loft_surface(int comp, Family f, Sample *smp, int ns, float hw, float total)
{
    int i;
    (void)comp;
    (void)total;
    /*  The corridor's surface (the user: "you immediately notch out those
     *  tiles out of the terrain... you then create a surface that
     *  bisects these corridors... this surface defines the top of the
     *  column of the tiles that make up the corridor, which have
     *  engineered walls as their side texture").  Every grid corner the
     *  band reaches takes the graded height over it -- the lowest, where
     *  two segments meet at a corner -- and is marked as the corridor's.
     *  The next pass gives those corners that height outright instead of
     *  the terrain's average, so the corridor is a graded shelf notched
     *  into the hillside, the road lies ON it rather than over it, and
     *  the wall rule closes its sides.  Before this the road was a
     *  ribbon a hair above the ground with the corners merely capped, so
     *  the two fought wherever the ground moved between stations. */
    if (s_pass == 1)
        {
        const float shelf_grade = f == F_RAIL ? 0.12f : 0.25f; /* the profile's own ceiling */
    for (i = 0; i < ns; ++i)
        {
            int32_t capc = (int32_t)floorf(smp[i].pos.x), capr = (int32_t)floorf(smp[i].pos.y);
            int     cq;
            /*  The corridor's surface, sampled per corner of every tile
             *  the band passes through: each corner projects onto the
             *  centreline and takes the profile's height there.  It is
             *  one continuous surface -- neighbouring tiles ask the same
             *  question at their shared corner and get the same answer --
             *  and it is flat across, because the across coordinate never
             *  enters.  A tile keeps its own copy so that a road and a
             *  railway side by side stay two shelves with a wall between
             *  them. */
            {
                /*  Only the tile the station stands in: the band reaches
                 *  its neighbours through their own stations, and writing
                 *  a shelf for a tile the band never crosses hands this
                 *  segment's height to somebody else's road. */
                int32_t tc = capc, tr = capr;
                int     tq;
                for (tq = 0; tq < 4; ++tq)
                {
                    float   ccx = (float)tc + ((tq == NE || tq == SE) ? 1.0f : 0.0f);
                    float   ccy = (float)tr + ((tq == SW || tq == SE) ? 1.0f : 0.0f);
                    float   ax = ccx - smp[i].pos.x, ay = ccy - smp[i].pos.y;
                    float   cd = ax * ax + ay * ay;
                    int32_t sl = (tr * R_MAP + tc) * 4 + tq;
                    if (cd >= s_tiled[sl])
                        continue;
                    s_tiled[sl] = cd;
                    s_tilez[sl] = profile_at(smp, ns,
                                             smp[i].s + ax * smp[i].dir.x + ay * smp[i].dir.y);
                }
            }
            for (cq = 0; cq < 4; ++cq)
            {
                int32_t gc = capc + ((cq == NE || cq == SE) ? 1 : 0), gr = capr + ((cq == SW || cq == SE) ? 1 : 0);
                float   dx, dy;
                if (gc < 0 || gr < 0 || gc >= GRID || gr >= GRID)
                    continue;
                dx = (float)gc - smp[i].pos.x;
                dy = (float)gr - smp[i].pos.y;
                float d2 = dx * dx + dy * dy;
                if (d2 > (hw + 0.8f) * (hw + 0.8f))
                    continue;
                /*  The corners under the band itself carry its height; the
                 *  ring beyond them is the batter, half way back to the
                 *  hillside, so the shelf blends out instead of standing
                 *  on one wall of its full depth. */
                if (d2 > (hw + 0.3f) * (hw + 0.3f))
                {
                    if (!s_corr[gr * GRID + gc])
                        s_corr[gr * GRID + gc] = 2;
                    if (d2 < s_zdist[gr * GRID + gc])
                    {
                        float along = dx * smp[i].dir.x + dy * smp[i].dir.y;
                        float grade = 0.0f;
                        if (i > 0 && i + 1 < ns && smp[i + 1].s - smp[i - 1].s > 1e-3f)
                            grade = (smp[i + 1].z - smp[i - 1].z) / (smp[i + 1].s - smp[i - 1].s);
                        if (grade > shelf_grade)
                            grade = shelf_grade;
                        if (grade < -shelf_grade)
                            grade = -shelf_grade;
                        if (along > 0.75f)
                            along = 0.75f;
                        if (along < -0.75f)
                            along = -0.75f;
                        s_zdist[gr * GRID + gc] = d2;
                        s_zcap[gr * GRID + gc]  = smp[i].z + grade * along;
                    }
                    continue;
                }
                /*  The corner takes the road's height AT ITS OWN
                 *  PROJECTION on the centreline, not the height of the
                 *  nearest station.  The two corners across the band
                 *  project to the same point, so they come out at the
                 *  same height and the shelf is flat across the corridor
                 *  by construction; along it the profile's own grade
                 *  carries them (the user: "the surface isn't even flat
                 *  across the corridor").  The nearest station only says
                 *  WHICH stretch of road owns the corner. */
                if (d2 < s_zdist[gr * GRID + gc] || s_corr[gr * GRID + gc] != 1)
                {
                    /*  Held to the grade the profile itself may take, and
                     *  to the distance a corner can honestly be from its
                     *  own station: a local estimate over two close
                     *  stations can read far steeper than the road ever
                     *  goes, and extrapolating on it throws the corner a
                     *  level out. */
                    float along = dx * smp[i].dir.x + dy * smp[i].dir.y;
                    float grade = 0.0f, zc;
                    if (i > 0 && i + 1 < ns && smp[i + 1].s - smp[i - 1].s > 1e-3f)
                        grade = (smp[i + 1].z - smp[i - 1].z) / (smp[i + 1].s - smp[i - 1].s);
                    if (grade > shelf_grade)
                        grade = shelf_grade;
                    if (grade < -shelf_grade)
                        grade = -shelf_grade;
                    if (along > 0.75f)
                        along = 0.75f;
                    if (along < -0.75f)
                        along = -0.75f;
                    zc = smp[i].z + grade * along;
                    s_zdist[gr * GRID + gc] = d2;
                    s_zcap[gr * GRID + gc]  = zc;
                    if (s_corr[gr * GRID + gc] != 1 || zc < s_zlow[gr * GRID + gc])
                        s_zlow[gr * GRID + gc] = zc;
                }
                s_corr[gr * GRID + gc] = 1;
            }
        }
    }
    return 0;
}

/*  The fitted line drawn over the world it made: centreline, band edges
 *  and piece boundaries, when the tuning window asks to see them. */
static int loft_overlay(RMesh *m, const RCity *c, uint8_t mask_bit, int comp, Family f,
                        Sample *smp, int ns, float hw, const Piece *pc, int np)
{
    int i;
    (void)comp;
    (void)f;
    /*  The fitted centreline, drawn over the road it made, when the
     *  tuning window asks for it: a hairline down the middle of the band
     *  through every station, so the curve the fit produced can be seen
     *  against the ribbon that came out of it. */
    if (s_tune.show_curves > 0.5f && s_pass != 1)
        for (i = 1; i < ns; ++i)
        {
            float ax = smp[i - 1].pos.x, ay = smp[i - 1].pos.y;
            float bx = smp[i].pos.x, by = smp[i].pos.y;
            float dx = bx - ax, dy = by - ay, dl = sqrtf(dx * dx + dy * dy);
            float px, py, q0[2], q1[2], r0[2], r1[2];
            if (dl < 1e-5f)
                continue;
            px = -dy / dl * 0.02f;
            py = dx / dl * 0.02f;
            q0[0] = ax - px; q0[1] = ay - py;
            q1[0] = ax + px; q1[1] = ay + py;
            r0[0] = bx - px; r0[1] = by - py;
            r1[0] = bx + px; r1[1] = by + py;
            if (strip_quad_z(m, c, mask_bit, tile_order(c, (int32_t)floorf(ax), (int32_t)floorf(ay), mask_bit) + 0.45f,
                             q0, q1, r0, r1, smp[i - 1].z + 0.06f, smp[i].z + 0.06f,
                             4.0f, 4.0f, 0.0f, 0.0f, MAT_VEHICLE) != 0)
                return -1;
            /*  And the width it carries: the band's two edges, so the
             *  carriageway can be read against the corridor it sits in
             *  (the user: "I'd also like to see road width shown"). */
            {
                float ox = -dy / dl * hw, oy = dx / dl * hw, sgn;
                for (sgn = -1.0f; sgn < 2.0f; sgn += 2.0f)
                {
                    float e0[2] = {ax + ox * sgn - px * 0.5f, ay + oy * sgn - py * 0.5f};
                    float e1[2] = {ax + ox * sgn + px * 0.5f, ay + oy * sgn + py * 0.5f};
                    float f0[2] = {bx + ox * sgn - px * 0.5f, by + oy * sgn - py * 0.5f};
                    float f1[2] = {bx + ox * sgn + px * 0.5f, by + oy * sgn + py * 0.5f};
                    if (strip_quad_z(m, c, mask_bit,
                                     tile_order(c, (int32_t)floorf(ax), (int32_t)floorf(ay), mask_bit) + 0.44f,
                                     e0, e1, f0, f1, smp[i - 1].z + 0.05f, smp[i].z + 0.05f,
                                     7.0f, 7.0f, 0.0f, 0.0f, MAT_VEHICLE) != 0)
                        return -1;
                }
            }
        }
    /*  And the construction behind it: a mark at every piece boundary --
     *  where a straight hands over to an arc and back -- so the curve's
     *  extent is visible, not just its shape.  Blue where an arc begins,
     *  red where the line is straight through. */
    /*  Piece boundaries mean nothing under the spline fit either: every
     *  sample is its own straight, so the marks become a dotted line
     *  along the whole run. */
    if (s_tune.show_curves > 0.5f && s_pass != 1 && s_tune.spline < 0.5f)
    {
        float at = 0.0f;
        int   k2;
        for (k2 = 0; k2 <= np; ++k2)
        {
            V2      pos, dir;
            int32_t tc, tr;
            float   paint = 0.0f;
            if (k2 < np)
                piece_at(&pc[k2], 0.0f, &pos, &dir);
            else
                piece_at(&pc[np - 1], pc[np - 1].len, &pos, &dir);
            paint = (k2 < np && pc[k2].arc) ? 6.0f : 5.0f; /* an arc, or a straight */
            tc    = (int32_t)floorf(pos.x);
            tr    = (int32_t)floorf(pos.y);
            if (tc < 0 || tr < 0 || tc >= R_MAP || tr >= R_MAP)
                continue;
            {
                float z = surface_at_world(c, mask_bit, pos.x, pos.y) + 0.08f;
                if (put_box(m, c, mask_bit, tile_order(c, tc, tr, mask_bit) + 0.46f,
                            pos.x, pos.y, 0.05f, 0.05f, z, z + 0.04f, MAT_VEHICLE, paint) != 0)
                    return -1;
            }
            if (k2 < np)
                at += pc[k2].len;
        }
        (void)at;
    }
    return 0;
}


int loft(RMesh *m, const RCity *c, uint8_t mask_bit, int comp, Family f, const Piece *pc, int np, float total, float zeb0, float zeb1, int pin0, int pin1)
{
    static Sample smp[8192];
    static float  zraw[8192];
    float         hw  = s_hiway ? 1.0f : f == F_ROAD ? ROAD_W * 0.5f
                                                     : RAIL_W * 0.5f;
    float         mat = f == F_ROAD ? MAT_ROAD : MAT_RAIL;
    int           k, ns = 0, i;
    float         s = 0.0f;
    /* the samples */
    if (getenv("SC2K_LOFT_DUMP"))
    {
        float tl = 0.0f;
        for (k = 0; k < np; ++k)
            tl += pc[k].len;
        printf("loft: %d pieces, total %.2f (sum %.2f), first (%.2f,%.2f)\n", np, (double)total, (double)tl, (double)pc[0].a.x, (double)pc[0].a.y);
    }
    for (k = 0; k < np; ++k)
    {
        const Piece *p  = &pc[k];
        /*  How finely the strip is cut along its length.  An arc gets a
         *  station every twenty-fifth of a tile and a straight every
         *  sixteenth: fine enough that a curve reads as a curve at the
         *  closest zoom and against the map view's own grid (the user:
         *  "can you increase the resolution of the segmentation?"). */
        int          nd = p->arc ? (int)ceilf(p->len / 0.04f) : (int)ceilf(p->len / 0.0625f);
        if (nd < 1)
            nd = 1;
        for (i = (ns ? 1 : 0); i <= nd; ++i)
        {
            float t = p->len * (float)i / (float)nd;
            V2    pos, dir;
            if (ns >= 8190)
                break;
            piece_at(p, t, &pos, &dir);
            /*  A station on every tile edge the centreline crosses, found
             *  by bisection, so a crease in the ground -- a crest at the
             *  edge between a rising and a falling tile -- is a station
             *  and never a chord's underside. */
            if (ns > 0 && i > 0)
            {
                /*  The centreline and both edges of the band each cross
                 *  the tile edges at their own point; a station at every
                 *  crossing, in order along the piece, so a wall the band
                 *  crosses obliquely is met at a station on the side it
                 *  first reaches. */
                float t0 = p->len * (float)(i - 1) / (float)nd, t1 = t;
                float tc[17];
                int   ntc = 0, side, pass, q;
                for (side = -1; side <= 1; ++side)
                    for (pass = 0; pass < 4; ++pass)
                    {
                        /*  A station wherever the band crosses a line the
                         *  ground can crease on: the tile edges, and the
                         *  tile's own diagonal, which is where its two
                         *  triangles meet.  Without the diagonals a strip
                         *  crossing a tile corner to corner passed under
                         *  the crest between them, which is what the road
                         *  clip check found on four cities. */
                        float off = (float)side * hw;
                        V2    p0, d0, q0, q1;
                        piece_at(p, t0, &p0, &d0);
                        q0 = (V2){p0.x + d0.y * off, p0.y - d0.x * off};
                        q1 = (V2){pos.x + dir.y * off, pos.y - dir.x * off};
                        {
                            float a0 = pass == 0   ? q0.x
                                       : pass == 1 ? q0.y
                                       : pass == 2 ? q0.x + q0.y
                                                   : q0.x - q0.y;
                            float a1 = pass == 0   ? q1.x
                                       : pass == 1 ? q1.y
                                       : pass == 2 ? q1.x + q1.y
                                                   : q1.x - q1.y;
                            if (floorf(a0) != floorf(a1) && ntc < 17)
                            {
                                float lo = t0, hi = t1, edge = floorf(a1 > a0 ? a1 : a0);
                                V2    pb, db, qb;
                                int   it;
                                for (it = 0; it < 12; ++it)
                                {
                                    float mid = 0.5f * (lo + hi), v;
                                    piece_at(p, mid, &pb, &db);
                                    qb = (V2){pb.x + db.y * off, pb.y - db.x * off};
                                    v  = pass == 0   ? qb.x
                                         : pass == 1 ? qb.y
                                         : pass == 2 ? qb.x + qb.y
                                                     : qb.x - qb.y;
                                    if ((v < edge) == (a0 < edge))
                                        lo = mid;
                                    else
                                        hi = mid;
                                }
                                if (hi - t0 > 1e-3f && t1 - hi > 1e-3f)
                                    tc[ntc++] = hi;
                            }
                        }
                    }
                /* in order along the piece, each once */
                for (q = 1; q < ntc; ++q)
                {
                    float v = tc[q];
                    int   r = q;
                    while (r > 0 && tc[r - 1] > v)
                    {
                        tc[r] = tc[r - 1];
                        --r;
                    }
                    tc[r] = v;
                }
                for (q = 0; q < ntc && ns < 8190; ++q)
                {
                    V2    pb, db, pm, pp;
                    float zm, zp;
                    if (q > 0 && tc[q] - tc[q - 1] < 2e-3f)
                        continue;
                    piece_at(p, tc[q], &pb, &db);
                    /* both tiles meet here, maybe at a small wall: the higher */
                    pm          = (V2){pb.x - db.x * 0.004f, pb.y - db.y * 0.004f};
                    pp          = (V2){pb.x + db.x * 0.004f, pb.y + db.y * 0.004f};
                    zm          = section_height(c, mask_bit, pm, db, hw);
                    zp          = section_height(c, mask_bit, pp, db, hw);
                    smp[ns].pos = pb;
                    smp[ns].dir = db;
                    smp[ns].s   = s + tc[q];
                    zraw[ns]    = zm > zp ? zm : zp;
                    ++ns;
                }
            }
            smp[ns].pos = pos;
            smp[ns].dir = dir;
            smp[ns].s   = s + t;
            zraw[ns]    = section_height(c, mask_bit, pos, dir, hw);
            ++ns;
        }
        s += p->len;
    }
    /*  The profile: at each station the highest ground the cross-section
     *  spans, centre and both edges, and a hair of clearance, so the
     *  terrain never cuts through the road, on a straightened diagonal
     *  across the corner tiles beside the road tiles above all; a road
     *  along a slope lies on it, a road across one rides on its uphill
     *  edge with a skirt below the other (the user: "raise roadways if
     *  they're going to cut into the terrain"). */
    /*  The band lies ON the corridor's surface, not a hair over it: the
     *  corridor's tiles are graded to this same line in the second pass,
     *  so a clearance would only make the two fight.  The hair stays for
     *  a highway deck, which is a structure over the ground rather than
     *  a shelf cut into it. */
    /*  The band lies on the corridor's formation, a hair proud of it:
     *  the corridor's tiles are graded to this same line in the second
     *  pass, so the two can no longer cross -- which is what used to
     *  make them fight -- and the hair is the wearing surface over the
     *  formation, a few centimetres of it. */
    for (i = 0; i < ns; ++i)
        smp[i].z = zraw[i] + 0.03f;
    /*  On the second pass the corridor has already been cut: the shelf
     *  under the band IS the surface this segment was fitted to, so the
     *  band lays flat on it rather than measuring the ground again and
     *  riding the highest point across its own width (the user: "the
     *  whole point of this exercise was to have roads/rails lay flat on
     *  this new smooth terrain inside of the corridors", and "I see roads
     *  being lifted, why?").  The shelf is flat across, so a band on it
     *  is flat across too. */
    if (s_pass == 2)
        for (i = 0; i < ns; ++i)
        {
            int32_t tc = (int32_t)floorf(smp[i].pos.x), tr = (int32_t)floorf(smp[i].pos.y);
            if (tc < 0 || tr < 0 || tc >= R_MAP || tr >= R_MAP)
                continue;
            if (s_corr[tr * GRID + tc] != 1 || s_corr[tr * GRID + tc + 1] != 1 ||
                s_corr[(tr + 1) * GRID + tc] != 1 || s_corr[(tr + 1) * GRID + tc + 1] != 1)
                continue;
            /*  The band lays on the corridor, read through the very
             *  function the terrain is drawn with -- the tile's own two
             *  triangles, cut on its own diagonal.  Evaluating it as a
             *  bilinear instead left the band under the drawn surface by
             *  the quad's twist, which is what the clip check kept
             *  finding at a hundredth of a level. */
            smp[i].z = surface_at_world(c, mask_bit, smp[i].pos.x, smp[i].pos.y) + 0.02f;
        }

    /*  The corridor's profile: a ramp between the altitudes of the
     *  NODES at its ends, and through any level crossing on the way.
     *
     *  A node -- a junction, a dead end, a crossing -- stands at its own
     *  tile's levelled height, and every corridor that reaches it ramps
     *  to that one number.  Two segments meeting at a junction therefore
     *  agree without anything being solved between them, a road and a
     *  railway crossing agree because the crossing is a node they share,
     *  and an edit moves only the segments whose anchors moved (the user:
     *  "use the altitude of intersections and then ramp the corridors
     *  smoothly from those").  The ramp is eased at both ends, so a
     *  corridor leaves a node level and picks up its grade in between
     *  rather than kinking at the join. */
    {
        static float ax[66];
        static float az[66];
        int          na = 0, k2;
        float        z0 = smp[0].z, z1 = smp[ns - 1].z;
        if (pin0 || s_seg_kind[0] == 1)
            z0 = node_altitude(c, s_seg_node[0][0], s_seg_node[0][1]) + 0.03f;
        if (pin1 || s_seg_kind[1] == 1)
            z1 = node_altitude(c, s_seg_node[1][0], s_seg_node[1][1]) + 0.03f;
        ax[na] = 0.0f;
        az[na] = z0;
        ++na;
        /*  A level crossing is a node of both networks: the road and the
         *  railway are pinned to the same altitude there. */
        for (i = 1; i + 1 < ns && na < 64; ++i)
        {
            int32_t tc = (int32_t)floorf(smp[i].pos.x), tr = (int32_t)floorf(smp[i].pos.y);
            uint8_t b;
            if (tc < 0 || tr < 0 || tc >= R_MAP || tr >= R_MAP)
                continue;
            b = c->xbld[tr * R_MAP + tc];
            if (b != 0x45u && b != 0x46u)
                continue;
            if (na > 1 && smp[i].s - ax[na - 1] < 0.5f)
                continue;
            ax[na] = smp[i].s;
            az[na] = node_altitude(c, tc, tr) + 0.03f;
            ++na;
        }
        ax[na] = total;
        az[na] = z1;
        ++na;
        for (i = 0, k2 = 0; i < ns; ++i)
        {
            float t, u;
            while (k2 + 2 < na && smp[i].s > ax[k2 + 1])
                ++k2;
            t = ax[k2 + 1] - ax[k2];
            u = t > 1e-4f ? (smp[i].s - ax[k2]) / t : 0.0f;
            if (u < 0.0f)
                u = 0.0f;
            if (u > 1.0f)
                u = 1.0f;
            u = u * u * (3.0f - 2.0f * u); /* eased, so it leaves a node level */
            smp[i].z = az[k2] + (az[k2 + 1] - az[k2]) * u;
        }
    }
    for (i = 0; i < ns; ++i)
        s_zorig[i] = zraw[i] + 0.03f; /* the ground's own line: a station below it is in a cut */
    /*  The deck stands clear (spec 7.2): 5 m under the soffit plus the
     *  girder is about 7.5 m to the road surface, and the vertical unit
     *  here is the altitude level, seven to eight metres.  So a little
     *  over one level, applied after the profile is settled so the deck
     *  follows the ground's shape while riding above it. */
    /*  A deck is a structure, not a carpet: its support line may rise or
     *  fall no faster than a sixth of a level a tile, so it runs straight
     *  over what the ground does under it and the columns take up the
     *  difference. */
    if (s_hiway && ns > 2)
    {
        const float grade = 0.16f;
        for (i = 1; i < ns; ++i)
        {
            float lim = smp[i - 1].z - grade * (smp[i].s - smp[i - 1].s);
            if (smp[i].z < lim)
                smp[i].z = lim;
        }
        for (i = ns - 2; i >= 0; --i)
        {
            float lim = smp[i + 1].z - grade * (smp[i + 1].s - smp[i].s);
            if (smp[i].z < lim)
                smp[i].z = lim;
        }
    }
    /*  The lift, tapered over the ramp cells at each end so a ramp is a
     *  ramp and not a carriageway ending in mid-air. */
    if (s_hiway)
        for (i = 0; i < ns; ++i)
        {
            float lift = 1.0f;
            if (s_hiway_ramp0 > 0.0f && smp[i].s < s_hiway_ramp0)
                lift = smp[i].s / s_hiway_ramp0;
            if (s_hiway_ramp1 > 0.0f && total - smp[i].s < s_hiway_ramp1)
            {
                float back = (total - smp[i].s) / s_hiway_ramp1;
                if (back < lift)
                    lift = back;
            }
            smp[i].z += HIWAY_LIFT * lift;
        }
    /*  The piers.  One per segment boundary, every two tiles, which is
     *  the spec's 30 m span (7.2).  The type comes from what is under
     *  the deck there: over nothing, a lot or a verge, a single
     *  hammerhead on the centreline carrying a cap the full width of
     *  the deck; over a surface road, a two-column bent with the
     *  columns outside the carriageway, never in a lane.
     *
     *  The cap spans both tiles of the band, so it is laid as two
     *  halves, each carrying the painter's order of the tile it is in:
     *  one order for a piece that straddles the seam would put half the
     *  cap in front of the deck over the other tile. */
    if (s_hiway)
    {
        float next = 1.0f;
        for (i = 1; i < ns; ++i)
        {
            const Sample *sm = &smp[i];
            float         px = -sm->dir.y, py = sm->dir.x;
            int           ew = fabsf(px) < 0.5f; /* the deck runs east-west */
            int32_t       tc, tr;
            float         g, top, cap, cw, cd;
            int           j, bent;
            if (sm->s < next)
                continue;
            next += HIWAY_BENT;
            tc = (int32_t)floorf(sm->pos.x);
            tr = (int32_t)floorf(sm->pos.y);
            if (tc < 0 || tr < 0 || tc >= R_MAP || tr >= R_MAP)
                continue;
            g   = surface_at_world(c, mask_bit, sm->pos.x, sm->pos.y);
            top = sm->z - HIWAY_GIRDER; /* the cap's top, an absolute height */
            cap = top - HIWAY_CAP;
            if (top - g < 0.12f)
                continue; /* the deck has met the ground: no room to stand */
            /*  A road under the deck asks for a bent, its columns clear
             *  of the carriageway; anything else takes the hammerhead. */
            bent = road_under_deck(c, sm->pos.x, sm->pos.y, px, py);
            /*  The cap, the full width of the deck and 1.5 m along it,
             *  in two halves so each takes its own tile's order. */
            cw = ew ? HIWAY_CAP_D : 1.0f;
            cd = ew ? 1.0f : HIWAY_CAP_D;
            for (j = 0; j < 2; ++j)
            {
                float   o  = j ? 0.5f : -0.5f;
                float   hx = sm->pos.x + px * o, hy = sm->pos.y + py * o;
                int32_t hc = (int32_t)floorf(hx), hr = (int32_t)floorf(hy);
                if (hc < 0 || hr < 0 || hc >= R_MAP || hr >= R_MAP)
                    continue;
                /*  Each half against its own ground, at the deck's height:
                 *  a cap measured from the ground under the spine stepped
                 *  between its halves on a slope. */
                {
                    float hg = surface_at_world(c, mask_bit, hx, hy);
                    if (put_box(m, c, mask_bit, tile_order(c, hc, hr, mask_bit), hx, hy, cw, cd, cap - hg, top - hg, MAT_PIER, 0.0f) != 0)
                        return -1;
                }
            }
            /*  Then the column, or the bent's pair of them.  A column
             *  runs from the ground it stands on up to the cap. */
            for (j = 0; j < 2; ++j)
            {
                /*  Outside the carriageway when a road runs under the
                 *  deck (7.2: never in a lane), otherwise just inside
                 *  the deck's edges, where the art stands them. */
                float   o  = (bent ? 0.78f : 0.62f) * (j ? 1.0f : -1.0f);
                float   cx = sm->pos.x + px * o, cy = sm->pos.y + py * o;
                float   cg = surface_at_world(c, mask_bit, cx, cy);
                float   ch = cap - cg;
                int32_t bc = (int32_t)floorf(cx), br = (int32_t)floorf(cy);
                if (bc < 0 || br < 0 || bc >= R_MAP || br >= R_MAP || ch < 0.05f)
                    continue;
                /*  Round, and its base draped on the terrain rather than
                 *  a flat foot at the centre's height, which hung over
                 *  the downhill side of every slope. */
                if (put_cyl(m, c, mask_bit, tile_order(c, bc, br, mask_bit), cx, cy, HIWAY_COL * 0.5f, -0.35f, ch, MAT_PIER) != 0)
                    return -1;
            }
        }
    }
    if (f == F_ROAD && ns >= 2 && net_record(&m->net, smp, ns, total, (int)(s_seg_class + 0.5f), 0) != 0)
        return -1;
    if (f == F_RAIL && ns >= 2 && net_record(&m->railnet, smp, ns, total, 0, 1) != 0)
        return -1;
    if (loft_furniture(m, c, mask_bit, comp, f, smp, ns, hw, mat, total, pin0, pin1) != 0)
        return -1;
    if (loft_surface(comp, f, smp, ns, hw, total) != 0)
        return -1;
    if (loft_overlay(m, c, mask_bit, comp, f, smp, ns, hw, pc, np) != 0)
        return -1;

    /*  SC2K_PROF_DUMP=1 prints the finished profile of every segment:
     *  the distance along, the ground under the cross-section, and the
     *  height the band was given.  tools/profile.py draws it, which is
     *  how the grade smoothing is looked at (the user: "I want to see
     *  how the smoothing of grade is happening"). */
    if (getenv("SC2K_PROF_DUMP"))
    {
        int d;
        printf("PROF f=%d hiway=%d n=%d\n", (int)f, s_hiway, ns);
        for (d = 0; d < ns; ++d)
        {
            float g = section_height(c, mask_bit, smp[d].pos, smp[d].dir, hw);
            printf("  %.4f %.4f %.4f %.3f %.3f\n", (double)smp[d].s, (double)g, (double)smp[d].z,
                   (double)smp[d].pos.x, (double)smp[d].pos.y);
        }
    }
    /* the quads and their skirts */
    for (i = 1; i < ns; ++i)
    {
        const Sample *pv = &smp[i - 1], *cu = &smp[i];
        float         ha    = hw * width_factor(pv->dir.x, pv->dir.y, comp);
        float         hb    = hw * width_factor(cu->dir.x, cu->dir.y, comp);
        float         a0[2] = {pv->pos.x + pv->dir.y * ha, pv->pos.y - pv->dir.x * ha};
        float         a1[2] = {pv->pos.x - pv->dir.y * ha, pv->pos.y + pv->dir.x * ha};
        float         b0[2] = {cu->pos.x + cu->dir.y * hb, cu->pos.y - cu->dir.x * hb};
        float         b1[2] = {cu->pos.x - cu->dir.y * hb, cu->pos.y + cu->dir.x * hb};
        float         mx = 0.5f * (pv->pos.x + cu->pos.x), my = 0.5f * (pv->pos.y + cu->pos.y);
        int32_t       tc = (int32_t)floorf(mx), tr = (int32_t)floorf(my);
        float         order, ma = s_hiway ? MAT_HIWAY : mat, al_a = pv->s, al_b = cu->s;
        int           side;
        if (tc < 0)
            tc = 0;
        if (tr < 0)
            tr = 0;
        if (tc >= R_MAP)
            tc = R_MAP - 1;
        if (tr >= R_MAP)
            tr = R_MAP - 1;
        order        = tile_order(c, tc, tr, mask_bit);
        s_road_class = s_hiway ? 3.0f : f != F_ROAD ? 0.0f : s_seg_class >= 0.0f ? s_seg_class
                                                                : road_class(c, tc, tr);
        /* a deck carries no level crossing's markings: it flies over the line */
        if (f == F_ROAD && !s_hiway && pv->xd > 0.45f && 0.5f * (pv->xd + cu->xd) < 1.55f)
        {
            ma   = MAT_XAPPROACH; /* along: the distance to the crossing */
            al_a = pv->xd;
            al_b = cu->xd;
        }
        else if (f == F_ROAD && zeb0 > 0.0f && cu->s <= zeb0 + 1e-4f)
            ma = MAT_ZEBRA; /* the band's along runs from the box outward */
        else if (f == F_ROAD && zeb1 > 0.0f && pv->s >= total - zeb1 - 1e-4f)
        {
            ma   = MAT_ZEBRA;
            al_a = total - pv->s;
            al_b = total - cu->s;
        }
        {
            /*  A quad in a cut, the road below the ground at a corner, is
             *  flagged in its class (4 and up) so the clipping check knows
             *  the ground over it is meant, held back by the walls below. */
            float gc[4] = {surface_at_world(c, mask_bit, a0[0], a0[1]), surface_at_world(c, mask_bit, a1[0], a1[1]), surface_at_world(c, mask_bit, b0[0], b0[1]), surface_at_world(c, mask_bit, b1[0], b1[1])};
            int   cut   = pv->z < gc[0] - 0.015f || pv->z < gc[1] - 0.015f || cu->z < gc[2] - 0.015f || cu->z < gc[3] - 0.015f ||
                          pv->z < s_zorig[i - 1] - 0.005f || cu->z < s_zorig[i] - 0.005f;
            float cls   = s_road_class;
            if (cut)
                s_road_class += 4.0f;
            if (strip_quad_z(m, c, mask_bit, order, a0, a1, b0, b1, pv->z, cu->z, -1.0f, 1.0f, al_a, al_b, ma) != 0)
                return -1;
            s_road_class = cls;
        }
        /*  A deck is a solid: the road on top, a soffit under it and the
         *  fascias closing the sides (the user: "they're open faced",
         *  "a part of the highway is exposed with no road", of a deck
         *  that was one surface with two curtains hanging off it and
         *  nothing underneath, so the oblique view looked straight
         *  through it). */
        if (s_hiway)
        {
            /*  Concrete, the piers' material: it carries a depth bias of
             *  its own so the structure stays behind the deck it holds
             *  up, and every face goes through the tile clipper, since a
             *  soffit triangle straddling two tiles with one painter's
             *  order was drawn over the next tile's carriageway. */
            static const float conc[3] = {1.0f, 0.0f, MAT_PIER}; /* plain, as the platform is */
            float              ref[3]  = {1.0f, 1.0f, 1.0f};

            float              u0[3] = {a0[0], a0[1], pv->z - HIWAY_GIRDER}, u1[3] = {a1[0], a1[1], pv->z - HIWAY_GIRDER};
            float              v0[3] = {b0[0], b0[1], cu->z - HIWAY_GIRDER}, v1[3] = {b1[0], b1[1], cu->z - HIWAY_GIRDER};
            float              dn[3] = {0.0f, 0.0f, -1.0f}, tri[3][3];
            /*  Behind the deck in the painter's order: depth here is the
             *  sweep's slot, not a height, so a soffit at the same order
             *  as the road it hangs under drew through it in stripes. */
            const float        sof = order;
            memcpy(tri[0], u0, sizeof tri[0]);
            memcpy(tri[1], v1, sizeof tri[1]);
            memcpy(tri[2], u1, sizeof tri[2]);
            if (put_tri_road_n(m, c, mask_bit, sof, (const float (*)[3])tri, dn, conc, ref, ref) != 0)
                return -1;
            memcpy(tri[1], v0, sizeof tri[1]);
            memcpy(tri[2], v1, sizeof tri[2]);
            if (put_tri_road_n(m, c, mask_bit, sof, (const float (*)[3])tri, dn, conc, ref, ref) != 0)
                return -1;
            /*  The ends: where the deck begins and ends in the air, a
             *  wall across it from the road down to the soffit. */
            if (i == 1 || i == ns - 1)
            {
                const float *e0 = i == 1 ? a0 : b0, *e1 = i == 1 ? a1 : b1;
                float        zt = i == 1 ? pv->z : cu->z;
                float        t0[3] = {e0[0], e0[1], zt}, t1[3] = {e1[0], e1[1], zt};
                float        q0[3] = {e0[0], e0[1], zt - HIWAY_GIRDER}, q1[3] = {e1[0], e1[1], zt - HIWAY_GIRDER};
                float        ax = e1[1] - e0[1], ay = e0[0] - e1[0], al = sqrtf(ax * ax + ay * ay), en[3];
                en[0] = al > 1e-6f ? ax / al : 1.0f;
                en[1] = al > 1e-6f ? ay / al : 0.0f;
                en[2] = 0.0f;
                if (zt - HIWAY_GIRDER > surface_at_world(c, mask_bit, e0[0], e0[1]) &&
                    put_wall(m, t0, t1, q0, q1, en, sof, conc) != 0)
                    return -1;
            }
        }
        /* the skirts: where the road stands above the ground at an edge, blocks down to it;
         * where it lies below, a retaining wall from the ground down to its edge, facing in */
        for (side = 0; side < 2; ++side)
        {
            static const float blocks[3] = {0.0f, 0.0f, MAT_SKIRT};

            const float *ea = side ? a1 : a0, *eb = side ? b1 : b0;
            float        ga    = surface_at_world(c, mask_bit, ea[0], ea[1]);
            float        gb    = surface_at_world(c, mask_bit, eb[0], eb[1]);
            float        t0[3] = {ea[0], ea[1], pv->z}, t1[3] = {eb[0], eb[1], cu->z};
            float        q0[3] = {ea[0], ea[1], ga}, q1[3] = {eb[0], eb[1], gb};
            float        nrm[3];
            nrm[0] = side ? -cu->dir.y : cu->dir.y;
            nrm[1] = side ? cu->dir.x : -cu->dir.x;
            nrm[2] = 0.0f;
            if (s_hiway)
            {
                /*  A deck stands clear of the ground the whole way, so
                 *  the embankment below would be one long wall of earth
                 *  under it.  What belongs there instead is the edge of
                 *  the structure: the girder's fascia down from the deck
                 *  and the parapet up from it, the bents carrying the
                 *  weight down (spec 7.2). */
                /*  Cast concrete, plain (the user: "I don't want the
                 *  pattern on the platform, only the support pillars"),
                 *  which col.r says.  The parapet has two faces and a
                 *  top: one quad showed the viewer its back along the
                 *  far edge, an unlit band where the wall's inside
                 *  should be (the user: "the far edge of the highway
                 *  does not look right"). */
                static const float conc[3] = {1.0f, 0.0f, MAT_PIER};
                const float        pw      = 0.035f; /* the parapet's thickness */
                float              ia[2] = {ea[0] - nrm[0] * pw, ea[1] - nrm[1] * pw};
                float              ib[2] = {eb[0] - nrm[0] * pw, eb[1] - nrm[1] * pw};
                float              inn[3] = {-nrm[0], -nrm[1], 0.0f}, up[3] = {0.0f, 0.0f, 1.0f};
                float g0[3] = {ea[0], ea[1], pv->z - HIWAY_GIRDER};
                float g1[3] = {eb[0], eb[1], cu->z - HIWAY_GIRDER};
                float p0[3] = {ea[0], ea[1], pv->z + HIWAY_PARAPET};
                float p1[3] = {eb[0], eb[1], cu->z + HIWAY_PARAPET};
                float k0[3] = {ia[0], ia[1], pv->z + HIWAY_PARAPET};
                float k1[3] = {ib[0], ib[1], cu->z + HIWAY_PARAPET};
                float n0[3] = {ia[0], ia[1], pv->z};
                float n1[3] = {ib[0], ib[1], cu->z};
                {
                    /*  The girder's face is in the deck's own shadow, as
                     *  the art draws it: a dark line under the
                     *  carriageway, not a bright band of concrete. */
                    static const float shade[3] = {1.0f, 1.0f, MAT_PIER};
                    if (put_wall(m, t0, t1, g0, g1, nrm, order, shade) != 0)
                        return -1;
                }
                if (put_wall(m, p0, p1, t0, t1, nrm, order, conc) != 0)
                    return -1;
                if (put_wall(m, k0, k1, n0, n1, inn, order, conc) != 0)
                    return -1;
                if (put_wall(m, p0, p1, k0, k1, up, order, conc) != 0)
                    return -1;
                continue;
            }
            if (pv->z > ga + 0.035f || cu->z > gb + 0.035f)
            {
                if (q0[2] > t0[2])
                    q0[2] = t0[2];
                if (q1[2] > t1[2])
                    q1[2] = t1[2];
                if (put_wall(m, t0, t1, q0, q1, nrm, order, blocks) != 0)
                    return -1;
            }
            if (pv->z < ga - 0.015f || cu->z < gb - 0.015f)
            {
                float u0[3] = {ea[0], ea[1], ga > pv->z ? ga : pv->z}, u1[3] = {eb[0], eb[1], gb > cu->z ? gb : cu->z};
                float in[3] = {-nrm[0], -nrm[1], 0.0f};
                /* the embankment's material, which the watertight check leaves out with the strip */
                if (put_wall(m, u0, u1, t0, t1, in, order, blocks) != 0)
                    return -1;
            }
        }
    }
    return 0;
}





/*  ==================================================================
 *  Walking the network
 *
 *  From a node to the next node: the tiles a run covers, the fit, the
 *  trims its junctions ask for, and the geometry.
 *  ================================================================== */
/*  A lone piece no neighbour joins: a band across its own tile along the
 *  axis its art links, both ends capped, as the original's lone sprite. */
int build_island(RMesh *m, const RCity *c, const RAtlasLevel *l, uint8_t mask_bit, int comp, Family f, int32_t col, int32_t row)
{
    Piece pc;
    int   links = tile_links(c, l, col, row, f), ns;
    float cx = (float)col + 0.5f, cy = (float)row + 0.5f;
    if (!links)
        return 0;
    ns               = (links & (L_N | L_S)) ? 1 : 0;
    pc.arc           = 0;
    pc.a             = (V2){ns ? cx : cx - 0.49f, ns ? cy - 0.49f : cy}; /* a hair inside the tile, so the end stations read its surface */
    pc.b             = (V2){ns ? cx : cx + 0.49f, ns ? cy + 0.49f : cy};
    s_seg_node[0][0] = s_seg_node[1][0] = col;
    s_seg_node[0][1] = s_seg_node[1][1] = row;
    s_seg_kind[0] = s_seg_kind[1] = 0;
    s_seg_ctrl[0] = s_seg_ctrl[1] = 0;
    pc.c                          = pc.a;
    pc.len                        = 0.98f;
    pc.r                          = 0.0f;
    pc.t0 = pc.t1 = 0.0f;
    return loft(m, c, mask_bit, comp, f, &pc, 1, 0.98f, 0.0f, 0.0f, 0, 0);
}

/*  A node of a family's network: a junction (three or four links), an
 *  end (one), or nothing. */
int node_kind(const RCity *c, const RAtlasLevel *l, Family f, int32_t col, int32_t row)
{
    /*  A junction piece is a node whatever its neighbours return: its
     *  box is drawn and its dangling arms end at the box (Barcelona's
     *  crossing at column 101, row 0, two arms into buildings, was
     *  walked through as a bend to the map's edge).  Otherwise one
     *  returned link makes an end. */
    int n = link_count(eff_links(c, l, col, row, f));
    if (link_count(tile_links(c, l, col, row, f)) >= 3)
        return 2;
    return n == 1 ? 1 : 0;
}

/*  Where a segment ends on an end tile, and how.  The tile's art links
 *  that no neighbour returns point at the dead side; what stands there
 *  decides: against a building, a bridge, a tunnel end or a highway
 *  the road runs square to the tile's edge, as the original draws the
 *  straight piece whole and the carrier's sprite goes on from there;
 *  against open land the segment ends at the tile's centre and a road
 *  gets its turning head.  Returns 1 for a square end. */
static int end_point(const RCity *c, const RAtlasLevel *l, Family f, int32_t col, int32_t row, V2 *pt)
{
    int dead = tile_links(c, l, col, row, f) & ~eff_links(c, l, col, row, f), e;
    *pt      = (V2){(float)col + 0.5f, (float)row + 0.5f};
    for (e = 0; e < 4; ++e)
    {
        int32_t nc, nr;
        uint8_t b;
        if (!(dead & (1 << e)))
            continue;
        nc = col + (int32_t)ROAD_DU[e];
        nr = row + (int32_t)ROAD_DV[e];
        if (nc < 0 || nr < 0 || nc >= R_MAP || nr >= R_MAP)
            continue;
        b = c->xbld[nr * R_MAP + nc];
        if (b >= 0x3Bu) /* a tunnel end, a highway, a bridge, a building: a carrier */
        {
            pt->x += ROAD_DU[e] * 0.49f;
            pt->y += ROAD_DV[e] * 0.49f;
            return 1;
        }
    }
    return 0;
}

/*  Cut `s` of length off the front of a piece chain, or off its back
 *  when `back`.  The chain keeps its shape exactly: a line is shortened
 *  along itself and an arc keeps its centre and radius and gives up some
 *  of its angle.  This is how a strip starts at the junction outline it
 *  meets -- the same path the junction measured, cut where the junction
 *  said, so the mouth is square and the two meet with nothing between
 *  them. */
static void pieces_trim(Piece *pc, int *np, float s, int back)
{
    while (s > 1e-4f && *np > 0)
    {
        Piece *p = back ? &pc[*np - 1] : &pc[0];
        if (p->len <= s + 1e-4f)
        {
            s -= p->len;
            if (!back)
                memmove(pc, pc + 1, (size_t)(*np - 1) * sizeof *pc);
            --*np;
            continue;
        }
        if (p->arc)
        {
            float sweep = p->t1 - p->t0;
            float dt    = (sweep >= 0.0f ? 1.0f : -1.0f) * s / p->r;
            if (back)
                p->t1 -= dt;
            else
                p->t0 += dt;
        }
        else
        {
            float dx = p->b.x - p->a.x, dy = p->b.y - p->a.y;
            float f  = s / (p->len > 1e-6f ? p->len : 1.0f);
            if (back)
            {
                p->b.x -= dx * f;
                p->b.y -= dy * f;
            }
            else
            {
                p->a.x += dx * f;
                p->a.y += dy * f;
            }
        }
        p->len -= s;
        s = 0.0f;
    }
}

/*  Walk one segment of a family from a node tile out through link `e`,
 *  collect its centreline, straighten, fillet and loft it.  `visited`
 *  marks (tile, link) so each segment is walked once, from either end. */
int walk_segment(RMesh *m, const RCity *c, const RAtlasLevel *l, uint8_t mask_bit, int comp, Family f, int32_t col, int32_t row, int e, uint8_t *visited)
{
    static V2      pts[MAX_PTS];
    static V2      q[MAX_PTS];
    static float   rad[MAX_PTS];
    static int32_t tcol[MAX_PTS], trow[MAX_PTS];
    static Piece   pieces[MAX_PIECES];
    int            nt = 0;
    float          hw = f == F_ROAD ? ROAD_W * 0.5f : RAIL_W * 0.5f;
    int          n = 0, k, nk, np, kind0 = node_kind(c, l, f, col, row), kind1 = 0;
    int          square0 = 0, square1 = 0; /* an end that runs square to a carrier */
    int32_t      cc = col, cr = row, back = (e + 2) & 3, guard = 0;
    int          ee    = e;
    float        total = 0.0f;
    if (visited[(row * R_MAP + col) * 4 + e])
        return 0;
    visited[(row * R_MAP + col) * 4 + e] = 1;
    /*  The start: where the junction says this arm's strip begins -- out
     *  along the arm's own direction, at the distance its shape cleared
     *  -- or the end tile's centre.  Before the junctions are shaped, and
     *  for a family that keeps its box, that is the middle of the box's
     *  own side, as it always was. */
    if (kind0 == 2)
        pts[n++] = (V2){(float)col + 0.5f + ROAD_DU[e] * hw, (float)row + 0.5f + ROAD_DV[e] * hw};
    else
        square0 = end_point(c, l, f, col, row, &pts[n++]);
    tcol[nt]   = col;
    trow[nt++] = row;
    for (;;)
    {
        int links, other;
        cc += (int32_t)ROAD_DU[ee];
        cr += (int32_t)ROAD_DV[ee];
        back = (ee + 2) & 3;
        if (cc < 0 || cr < 0 || cc >= R_MAP || cr >= R_MAP)
        {
            /* off the map: the segment runs to the edge, the last tile's side */
            pts[n++] = (V2){(float)(cc - (int32_t)ROAD_DU[ee]) + 0.5f + ROAD_DU[ee] * 0.5f,
                            (float)(cr - (int32_t)ROAD_DV[ee]) + 0.5f + ROAD_DV[ee] * 0.5f};
            kind1    = 0;
            break;
        }
        links = eff_links(c, l, cc, cr, f);
        if (!(links & (1 << back)))
            break; /* cannot happen on effective links; kept as a guard */
        visited[(cr * R_MAP + cc) * 4 + back] = 1;
        if (n + 2 >= MAX_PTS || ++guard > 4096)
            break;
        kind1 = node_kind(c, l, f, cc, cr);
        if (kind1 != 0 || link_count(links) != 2)
        {
            /* a node: the far end */
            if (kind1 == 0)
                kind1 = 1;
            if (kind1 == 2)
                pts[n++] = (V2){(float)cc + 0.5f + ROAD_DU[back] * hw, (float)cr + 0.5f + ROAD_DV[back] * hw};
            else
                square1 = end_point(c, l, f, cc, cr, &pts[n++]);
            if (nt < MAX_PTS)
            {
                tcol[nt]   = cc;
                trow[nt++] = cr;
            }
            break;
        }
        pts[n++]                            = (V2){(float)cc + 0.5f, (float)cr + 0.5f};
        if (nt < MAX_PTS)
        {
            tcol[nt]   = cc;
            trow[nt++] = cr;
        }
        other                               = links & ~(1 << back);
        ee                                  = other == L_N ? 0 : other == L_E ? 1
                                                             : other == L_S   ? 2
                                                                              : 3;
        visited[(cr * R_MAP + cc) * 4 + ee] = 1;
        if (cc == col && cr == row)
            break; /* a loop back to the start */
    }
    if (n < 2)
        return 0;
    /*  One class for the whole segment, the median of its tiles', so an
     *  avenue's centre line does not start and stop mid-block with the
     *  traffic count of each tile (Bay View's shore road). */
    if (f == F_ROAD)
    {
        int cnt[3] = {0, 0, 0}, half, acc = 0, cls;
        for (k = 0; k < n; ++k)
        {
            int32_t tc = (int32_t)floorf(pts[k].x), tr = (int32_t)floorf(pts[k].y);
            if (tc < 0 || tr < 0 || tc >= R_MAP || tr >= R_MAP)
                continue;
            ++cnt[(int)road_class(c, tc, tr)];
        }
        half = (cnt[0] + cnt[1] + cnt[2] + 1) / 2;
        for (cls = 0; cls < 2; ++cls)
        {
            acc += cnt[cls];
            if (acc >= half)
                break;
        }
        s_seg_class = (float)cls;
    }
    else
        s_seg_class = -1.0f;
    /*  Stage one and two (mesh_path.c): the corridor is the
     *  segment's own tiles, its gates the crossable part of each shared
     *  edge, and the path is the taut string through them -- which cuts
     *  every corner of a staircase into one diagonal by itself -- with
     *  each corner then swept as wide as the corridor allows.  This
     *  replaced a straightener that tried to recognise staircases,
     *  jogs and zig-zags by their leg lengths, and read half of them
     *  differently from the eye. */
    /*  The corridor is tested against the road's own half width, not a
     *  fraction of it: what has to fit inside the corridor is the road. */
    nk = path_fit(c, tcol, trow, nt, hw, pts[0], pts[n - 1], f == F_ROAD ? s_tune.road_rmax : s_tune.rail_rmax,
                  f == F_ROAD ? ROAD_RMIN : RAIL_RMIN,
                  hw / ((f == F_ROAD ? 0.50f : 0.62f) * 0.5f),
                  kind0 == 2 ? (int32_t)(row * R_MAP + col) : -1,
                  kind1 == 2 ? (int32_t)(cr * R_MAP + cc) : -1, q, rad, MAX_PTS);
    if (nk < 2)
        return 0;
    {
        /*  SC2K_ROAD_DUMP=1 prints every segment of six tiles or more;
         *  SC2K_ROAD_DUMP=col,row every segment through that tile. */
        const char *dump = getenv("SC2K_ROAD_DUMP");
        int         dc = -1, dr = -1, show = 0;
        if (dump && sscanf(dump, "%d,%d", &dc, &dr) == 2)
        {
            for (k = 0; k < n; ++k)
                if ((int32_t)floorf(pts[k].x) == dc && (int32_t)floorf(pts[k].y) == dr)
                    show = 1;
        }
        else if (dump && n >= 6)
            show = 1;
        if (show)
        {
            printf("segment f%d from c%d r%d e%d: %d points, %d kept:", (int)f, (int)col, (int)row, e, n, nk);
            for (k = 0; k < nk; ++k)
                printf(" (%.2f,%.2f)", (double)q[k].x, (double)q[k].y);
            printf("\n  raw:");
            for (k = 0; k < n; ++k)
                printf(" (%.2f,%.2f)", (double)pts[k].x, (double)pts[k].y);
            printf("\n");
        }
    }
    /* stage two's sweeps, each corner on the radius its room allows */
    if (fillet_r(q, nk, rad, pieces, &np) != 0 || np == 0)
    {
        if (getenv("SC2K_PATH_DUMP"))
            printf("  fillet refused: %d points\n", nk);
        return 0;
    }
    for (k = 0; k < np; ++k)
        total += pieces[k].len;
    /*  Stage three's measurement: which way this segment leaves each
     *  junction it touches.  The direction is the fitted path's own at
     *  that end, so a segment that leaves at an angle says so. */
    if (kind0 == 2 || kind1 == 2)
    {
        V2 pos, dir;
        if (kind0 == 2)
        {
            RArm *a = &s_arm[FAMX(f)][(row * R_MAP + col) * 4 + e];
            piece_at(&pieces[0], 0.0f, &pos, &dir);
            a->ax   = pos.x;
            a->ay   = pos.y;
            a->dx   = dir.x;
            a->dy   = dir.y;
            a->have = 1;
        }
        if (kind1 == 2)
        {
            RArm *a = &s_arm[FAMX(f)][(cr * R_MAP + cc) * 4 + back];
            piece_at(&pieces[np - 1], pieces[np - 1].len, &pos, &dir);
            a->ax   = pos.x;
            a->ay   = pos.y;
            a->dx   = -dir.x;
            a->dy   = -dir.y;
            a->have = 1;
        }
    }
    if (s_measure)
    {
        /*  Where this path passes each tile it crosses, and its
         *  direction there: the nearest point to the tile's middle.  A
         *  level crossing is built from the road's and the rail's. */
        float    s;
        int      pi = 0;
        float    acc = 0.0f;
        for (s = 0.0f; s <= total; s += 0.05f)
        {
            V2      pos, dir;
            int32_t tc, tr;
            float   d2;
            RCross *x;
            while (pi + 1 < np && acc + pieces[pi].len < s)
            {
                acc += pieces[pi].len;
                ++pi;
            }
            piece_at(&pieces[pi], s - acc, &pos, &dir);
            tc = (int32_t)floorf(pos.x);
            tr = (int32_t)floorf(pos.y);
            if (tc < 0 || tr < 0 || tc >= R_MAP || tr >= R_MAP)
                continue;
            x  = &s_cross[FAMX(f)][tr * R_MAP + tc];
            d2 = (pos.x - ((float)tc + 0.5f)) * (pos.x - ((float)tc + 0.5f)) +
                 (pos.y - ((float)tr + 0.5f)) * (pos.y - ((float)tr + 0.5f));
            if (!x->have || d2 < (x->x - ((float)tc + 0.5f)) * (x->x - ((float)tc + 0.5f)) +
                                     (x->y - ((float)tr + 0.5f)) * (x->y - ((float)tr + 0.5f)))
            {
                x->x    = pos.x;
                x->y    = pos.y;
                x->dx   = dir.x;
                x->dy   = dir.y;
                x->have = 1;
            }
        }
        return 0; /* the measuring pass draws nothing */
    }
    /*  Cut the strip back to the outline each junction gave it.  The
     *  path itself is untouched -- the drawing pass fits exactly what
     *  the measuring pass measured -- so the cut lands on the mouth the
     *  junction cut for it. */
    if (kind0 == 2 || kind1 == 2)
    {
        float t0 = kind0 == 2 ? s_trim[FAMX(f)][(row * R_MAP + col) * 4 + e] : 0.0f;
        float t1 = kind1 == 2 ? s_trim[FAMX(f)][(cr * R_MAP + cc) * 4 + back] : 0.0f;
        /*  Never eat the segment.  Between two adjacent junctions a
         *  segment is about a tile long, and two trims of 0.45 leave a
         *  floating stub with nothing joining it to either end (the
         *  user, on Toronto 58,98: "shows a very clear bad").  Both
         *  trims are scaled back together so at least half the segment
         *  survives. */
        if (t0 + t1 > total * 0.5f && t0 + t1 > 1e-4f)
        {
            float k3 = (total * 0.5f) / (t0 + t1);
            t0 *= k3;
            t1 *= k3;
        }
        if (t0 > 0.0f)
            pieces_trim(pieces, &np, t0, 0);
        if (t1 > 0.0f)
            pieces_trim(pieces, &np, t1, 1);
        if (np < 1)
            return 0;
        total = 0.0f;
        for (k = 0; k < np; ++k)
            total += pieces[k].len;
    }
    s_seg_node[0][0] = col;
    s_seg_node[0][1] = row;
    s_seg_node[1][0] = cc;
    s_seg_node[1][1] = cr;
    s_seg_kind[0]    = kind0;
    s_seg_kind[1]    = kind1;
    s_seg_ctrl[0]    = (f == F_ROAD && kind0 == 2) ? (s_junc_ctrl[row * R_MAP + col] >> (2 * e)) & 3 : 0;
    s_seg_ctrl[1]    = (f == F_ROAD && kind1 == 2) ? (s_junc_ctrl[cr * R_MAP + cc] >> (2 * back)) & 3 : 0;
    /* the crosswalk and stop bar only on a controlled leg (spec 3.4) */
    /*  The nodes the fit produced, when the overlay is on: one mark per
     *  vertex of the polyline the pieces were built from, so the spacing
     *  between them can be read directly and every corner that came out
     *  hard is visible as such (the user: "I'd like to see the underlying
     *  data (segment nodes) as well").  Amber where the corner was swept
     *  into an arc, red where no legal radius fitted and the line simply
     *  turns. */
    /*  Not under the spline fit: it has no fillets, so every node would
     *  read as "no arc" and at that sampling the marks merge into a
     *  ribbon that hides the very curve they are there to explain. */
    if (s_tune.show_curves > 0.5f && s_pass != 1 && !s_last_splined)
    {
        int k3;
        for (k3 = 0; k3 < nk; ++k3)
        {
            int32_t tc = (int32_t)floorf(q[k3].x), tr = (int32_t)floorf(q[k3].y);
            float   paint, z, half;
            if (tc < 0 || tr < 0 || tc >= R_MAP || tr >= R_MAP)
                continue;
            /*  An end node has no corner to sweep, so it is neither. */
            paint = (k3 == 0 || k3 + 1 == nk) ? 7.0f : (rad[k3] > 0.001f ? 3.0f : 1.0f);
            half  = (k3 == 0 || k3 + 1 == nk) ? 0.05f : 0.075f;
            z     = surface_at_world(c, mask_bit, q[k3].x, q[k3].y) + 0.10f;
            if (put_box(m, c, mask_bit, tile_order(c, tc, tr, mask_bit) + 0.47f,
                        q[k3].x, q[k3].y, half, half, z, z + 0.04f, MAT_VEHICLE, paint) != 0)
                return 0;
        }
    }
    if (loft(m, c, mask_bit, comp, f, pieces, np, total, s_seg_ctrl[0] ? 0.2f : 0.0f, s_seg_ctrl[1] ? 0.2f : 0.0f, kind0 == 2, kind1 == 2) != 0)
        return -1;
    /* an end on open land: a road's round cap, the turning head of a dead end; a rail ends square */
    if (f == F_ROAD)
    {
        int which;
        for (which = 0; which < 2; ++which)
        {
            int   at_end = which == 1;
            int   end    = at_end ? np - 1 : 0;
            V2    pos, dir;
            float h, ang;
            if (at_end ? (kind1 != 1 || square1) : (kind0 != 1 || square0))
                continue;
            /*  Every dead end gets its round cap (the user: "all dead end
             *  roads should have endcaps"); spec 3.10's step 11 keeps the
             *  turning head for a local road with open land around it, and
             *  ends an avenue square, but a square end in the middle of a
             *  tile is a raw edge, so the cap is unconditional. */
            piece_at(&pieces[end], at_end ? pieces[end].len : 0.0f, &pos, &dir);
            if (!at_end)
            {
                dir.x = -dir.x;
                dir.y = -dir.y;
            }
            h   = hw * width_factor(dir.x, dir.y, comp);
            ang = atan2f(dir.y, dir.x);
            {
                int32_t tc = (int32_t)floorf(pos.x), tr = (int32_t)floorf(pos.y);
                if (tc < 0)
                    tc = 0;
                if (tr < 0)
                    tr = 0;
                if (tc >= R_MAP)
                    tc = R_MAP - 1;
                if (tr >= R_MAP)
                    tr = R_MAP - 1;
                if (strip_fan_z(m, c, mask_bit, tile_order(c, tc, tr, mask_bit), pos.x, pos.y, ang - 1.5707963f, ang + 1.5707963f, h, h, 0.0f, MAT_ROAD, 8, 0.03f) != 0)
                    return -1;
            }
        }
    }
    return 0;
}

/*  ==================================================================
 *  Junctions
 *
 *  An intersection is the polygon its arms cut out, not a square: the
 *  segments are walked once to measure and once to draw.
 *  ================================================================== */
/*  The control of a road junction's arms (spec 3.4), from the classes
 *  of the roads meeting there, read on the first tile of each arm: all
 *  local, four legs, a two-way stop on the quieter axis; three legs
 *  with a local stem, a stop on the stem; a local against an avenue,
 *  stop on the local legs; avenue against avenue, an all-way stop, or a
 *  signal where the junction is busy; anything against a boulevard, a
 *  signal.  Two bits per arm. */
static int junction_control(const RCity *c, int32_t col, int32_t row, int links)
{
    int cls[4], ctrl[4] = {0, 0, 0, 0}, e, n = 0, maxc = 0, minc = 9, busy;
    int traf[4];
    for (e = 0; e < 4; ++e)
    {
        int32_t nc = col + (int32_t)ROAD_DU[e], nr = row + (int32_t)ROAD_DV[e];
        cls[e]  = -1;
        traf[e] = 0;
        if (!(links & (1 << e)) || nc < 0 || nr < 0 || nc >= R_MAP || nr >= R_MAP)
            continue;
        cls[e]  = (int)road_class(c, nc, nr);
        traf[e] = c->xtrf[(nr >> 1) * R_HALF + (nc >> 1)];
        ++n;
        if (cls[e] > maxc)
            maxc = cls[e];
        if (cls[e] < minc)
            minc = cls[e];
    }
    busy = c->xtrf[(row >> 1) * R_HALF + (col >> 1)] > 0xAA;
    if (maxc >= 2)
    {
        for (e = 0; e < 4; ++e)
            if (cls[e] >= 0)
                ctrl[e] = 2;
    }
    else if (maxc == 1 && minc == 1)
    {
        for (e = 0; e < 4; ++e)
            if (cls[e] >= 0)
                ctrl[e] = busy ? 2 : 1;
    }
    else if (maxc == 1)
    {
        for (e = 0; e < 4; ++e)
            if (cls[e] == 0)
                ctrl[e] = 1;
    }
    else if (n == 3)
    {
        /* the stem: the arm whose opposite is missing */
        for (e = 0; e < 4; ++e)
            if (cls[e] >= 0 && cls[(e + 2) & 3] < 0)
                ctrl[e] = 1;
    }
    else
    {
        /* four local legs: a two-way stop on the axis with the lighter traffic */
        int ns = traf[0] > traf[2] ? traf[0] : traf[2];
        int ew = traf[1] > traf[3] ? traf[1] : traf[3];
        for (e = 0; e < 4; ++e)
            if (cls[e] >= 0 && ((e == 0 || e == 2) ? ns <= ew : ew < ns))
                ctrl[e] = 1;
    }
    return ctrl[0] | (ctrl[1] << 2) | (ctrl[2] << 4) | (ctrl[3] << 6);
}

/*  A STOP sign at the driver's right of an approach, 1.5 m behind the
 *  crosswalk (spec 3.4, 6.3): a post with the bottom of the sign at
 *  2.1 m and a 750 mm red octagon facing the driver. */
static int put_stop_sign(RMesh *m, const RCity *c, int32_t col, int32_t row, uint8_t mask_bit, float order, int e, float h)
{
    static const int right[4] = {3, 0, 1, 2};
    int              r        = right[e];
    float            cx = (float)col + 0.5f, cy = (float)row + 0.5f;
    float            x = cx + ROAD_DU[e] * (h + 0.32f) + ROAD_DU[r] * (h + 0.03f);
    float            y = cy + ROAD_DV[e] * (h + 0.32f) + ROAD_DV[r] * (h + 0.03f);
    float            g = surface_at_world(c, mask_bit, x, y);
    if (put_box(m, c, mask_bit, order, x, y, 0.008f, 0.008f, 0.0f, 0.31f, MAT_PROP, 0.0f) != 0)
        return -1;
    return put_lamp_face(m, order, x, y, g, 0.29f, -ROAD_DU[e], -ROAD_DV[e], 0.025f, 0.0f, 13.0f);
}

/*  A junction of a family: the box, its sidewalk corners rounded as
 *  curb returns where two arms meet and square where a side is free,
 *  the outline along a free side, and for a road a signal on every arm. */
/* ---- stage three: a junction takes its shape from its arms ------------- */

RArm   s_arm[2][R_MAP * R_MAP * 4];
float  s_trim[2][R_MAP * R_MAP * 4];
RCross s_cross[2][R_MAP * R_MAP];
int    s_measure;

/*  The outline of the junction at (col,row), and how far along each arm
 *  its strip should start.
 *
 *  Every arm leaves at its own angle with its own half width, so its two
 *  edges are two lines through the junction.  Where one arm's left edge
 *  meets the next arm's right edge is a corner of the junction; between
 *  two corners an arm has its mouth, cut square across the arm at
 *  whichever of its corners reaches further out.  Four arms leaving
 *  along the tile's own axes give back exactly the square this used to
 *  be, so nothing changes where nothing was wrong.
 *
 *  Returns the number of points written, or 0 if there is nothing to
 *  draw. */
int junction_poly(const RCity *c, Family f, int32_t col, int32_t row, int links,
                  V2 *out, uint8_t *mouth, int max, float trim[4])
{
    float cx = (float)col + 0.5f, cy = (float)row + 0.5f;
    float w  = (f == F_ROAD ? ROAD_W : RAIL_W) * 0.5f;
    /*  The junction is sized by the band that runs through it, not by a
     *  fixed number of tiles.  A wider road needs its mouth pushed
     *  further out -- otherwise the arms fatten while the intersection
     *  stays put and the strips overhang it (the user: "the intersection
     *  doesn't grow as the road size grows, the end should be pushed
     *  out").  Everything below is written against the family's own
     *  default half-width, so at the shipped widths the numbers are the
     *  ones that were tuned by eye, and only moving a width moves them. */
    float ref = (f == F_ROAD ? 0.50f : 0.62f) * 0.5f;
    float gro = getenv("SC2K_NOSCALE") ? 1.0f : w / ref; /* for before/after shots */
    float far = 0.62f * gro;
    typedef struct
    {
        float ang;
        V2    d, o; /* the way it leaves, and where its path starts */
        int   e;
    } Arm;
    Arm arm[4];
    V2  corner[4];
    int na = 0, i, e, n = 0;
    (void)c;
    for (e = 0; e < 4; ++e)
    {
        const RArm *a = &s_arm[FAMX(f)][(row * R_MAP + col) * 4 + e];
        V2          d, o;
        if (!(links & (1 << e)))
            continue;
        /*  The arm's own ray: where its path starts and the way it
         *  leaves.  Both come from the fit the drawing pass will repeat
         *  exactly, so the mouth this cuts is a point ON that path and
         *  the strip leaves it square. */
        d = a->have ? (V2){a->dx, a->dy} : (V2){ROAD_DU[e], ROAD_DV[e]};
        o = a->have ? (V2){a->ax, a->ay}
                    : (V2){cx + ROAD_DU[e] * w, cy + ROAD_DV[e] * w};
        arm[na].d   = d;
        arm[na].o   = o;
        arm[na].ang = atan2f(d.y, d.x);
        arm[na].e   = e;
        ++na;
    }
    if (na < 1)
        return 0;
    /* by angle, so "the next arm round" means what it says */
    for (i = 1; i < na; ++i)
    {
        int j = i;
        while (j > 0 && arm[j - 1].ang > arm[j].ang)
        {
            Arm t = arm[j - 1];
            arm[j - 1]       = arm[j];
            arm[j]           = t;
            --j;
        }
    }
    for (i = 0; i < na; ++i)
    {
        int   j  = (i + 1) % na;
        V2    di = arm[i].d, dj = arm[j].d;
        V2    pi = {-di.y, di.x}, pj = {-dj.y, dj.x};
        V2    a0 = {arm[i].o.x + pi.x * w, arm[i].o.y + pi.y * w}; /* arm i's left edge  */
        V2    b0 = {arm[j].o.x - pj.x * w, arm[j].o.y - pj.y * w}; /* arm j's right edge */
        V2    x;
        float dx, dy, len;
        if (na < 2 || !line_meet(a0, di, b0, dj, &x))
        {
            /*  One arm, or two facing each other: the edges never meet.
             *  The corner is then the corner of the strip itself. */
            x = (V2){a0.x + di.x * w, a0.y + di.y * w};
        }
        dx  = x.x - cx;
        dy  = x.y - cy;
        len = sqrtf(dx * dx + dy * dy);
        /*  A pair of arms that leave almost together sends their corner
         *  to infinity; a junction is a tile wide and no more. */
        if (len > far)
        {
            x.x = cx + dx / len * far;
            x.y = cy + dy / len * far;
        }
        corner[i] = x;
    }
    for (e = 0; e < 4; ++e)
        trim[e] = w;
    for (i = 0; i < na; ++i)
    {
        /*  Arm i's own corners: the one before it and the one after. */
        V2    d  = arm[i].d, o = arm[i].o;
        V2    ca = corner[(i + na - 1) % na], cb = corner[i];
        float ta = (ca.x - o.x) * d.x + (ca.y - o.y) * d.y;
        float tb = (cb.x - o.x) * d.x + (cb.y - o.y) * d.y;
        float t  = ta > tb ? ta : tb;
        /*  Measured along the arm's own path from where that path
         *  starts, so the number handed back is an arc length the strip
         *  can simply be cut at.  Never past the tile's own edge: the
         *  junction tile is a levelled pad, and a strip that starts
         *  beyond it starts on ground the pad's height does not
         *  describe. */
        if (t < 0.0f)
            t = 0.0f;
        if (t > s_tune.trim_cap * gro)
            t = s_tune.trim_cap * gro;
        trim[arm[i].e] = t;
    }
    if (getenv("SC2K_JUNC_DUMP"))
    {
        printf("JUNC %d %d", (int)col, (int)row);
        for (i = 0; i < na; ++i)
            printf(" %.3f,%.3f,%.3f,%.3f", (double)arm[i].d.x, (double)arm[i].d.y,
                   (double)trim[arm[i].e], (double)w);
        printf("\n");
    }
    /*  The outline is the hull of every arm's mouth and every corner
     *  between two arms.  Taking them in order instead spikes wherever a
     *  corner falls behind the mouth its arm was cleared to; a junction
     *  is a convex piece of ground, and the hull covers every mouth by
     *  construction, so no arm can start beyond it and leave a gap. */
    {
        V2      cand[12];
        uint8_t cmouth[12];
        int     nc = 0, j, hn = 0;
        for (i = 0; i < na && nc + 3 <= 12; ++i)
        {
            V2    d = arm[i].d, o = arm[i].o, p = {-arm[i].d.y, arm[i].d.x};
            float t = trim[arm[i].e];
            cmouth[nc] = 1;
            cand[nc++]  = (V2){o.x + d.x * t - p.x * w, o.y + d.y * t - p.y * w};
            cmouth[nc] = 1;
            cand[nc++]  = (V2){o.x + d.x * t + p.x * w, o.y + d.y * t + p.y * w};
            cmouth[nc] = 0;
            cand[nc++]  = corner[i];
        }
        /*  By angle about the middle, which for points that all lie
         *  outside it is the hull's own order; one pass of the turn test
         *  then drops what is inside. */
        for (i = 1; i < nc; ++i)
        {
            V2      vv = cand[i];
            uint8_t vm = cmouth[i];
            float   aa = atan2f(vv.y - cy, vv.x - cx);
            j          = i;
            while (j > 0 && atan2f(cand[j - 1].y - cy, cand[j - 1].x - cx) > aa)
            {
                cand[j]   = cand[j - 1];
                cmouth[j] = cmouth[j - 1];
                --j;
            }
            cand[j]   = vv;
            cmouth[j] = vm;
        }
        for (i = 0; i < nc; ++i)
        {
            while (hn >= 2)
            {
                V2    a = out[hn - 2], b = out[hn - 1], dd = cand[i];
                float cr = (b.x - a.x) * (dd.y - a.y) - (b.y - a.y) * (dd.x - a.x);
                if (cr > 1e-6f)
                    break; /* a left turn: b stays */
                --hn;
            }
            if (hn < max)
            {
                if (mouth)
                    mouth[hn] = cmouth[i];
                out[hn++] = cand[i];
            }
        }
        while (hn >= 3)
        {
            V2    a = out[hn - 2], b = out[hn - 1], dd = out[0];
            float cr = (b.x - a.x) * (dd.y - a.y) - (b.y - a.y) * (dd.x - a.x);
            if (cr > 1e-6f)
                break;
            --hn;
        }
        n = hn;
    }
    if (getenv("SC2K_JUNC_DUMP"))
    {
        printf("JPOLY %d %d", (int)col, (int)row);
        for (i = 0; i < n; ++i)
            printf(" %.3f,%.3f", (double)out[i].x, (double)out[i].y);
        printf("\n");
    }
    return n;
}

int build_junction(RMesh *m, const RCity *c, uint8_t mask_bit, Family f, int32_t col, int32_t row, int links, float order)
{
    float hw  = f == F_ROAD ? ROAD_W * 0.5f : RAIL_W * 0.5f;
    float mat = f == F_ROAD ? MAT_ROAD : MAT_RAIL;
    float cx = (float)col + 0.5f, cy = (float)row + 0.5f, h = hw;
    float sw    = h * 0.20f; /* the curb return's radius: the sidewalk's width      */
    float lw    = h * 0.20f; /* the sidewalk along a free side, across 0.8..1       */
    float a0[2] = {cx - h, cy - h}, a1[2] = {cx + h, cy - h}, b0[2] = {cx - h, cy + h}, b1[2] = {cx + h, cy + h};
    int   e;
    /*  The box stands the hair above the ground the strips do, 0.03 of a
     *  level: on the ground itself, under arms a hair higher, the oblique
     *  view showed a line of grass along every side of a rail box. */
    /*  The junction stands on the corridor's own surface, the same one
     *  its arms lay on, so the two meet flush.  Reading the terrain
     *  instead put the box a hair off every strip that reached it. */
    /*  The junction on the corridor's surface was tried and measured
     *  worse (655 samples against 73 on Toronto): where a junction tile
     *  has a corner the corridor never marked, corridor_at falls back to
     *  the terrain and the box tilts off the shelf.  Until the junction
     *  tiles are marked whole, the terrain's own height is the safer
     *  reference. */
    float zj = surface_at_world(c, mask_bit, cx, cy) + 0.03f;
    s_road_class = 0.0f; /* the box is plain asphalt; a median ends at the junction */
    if (f == F_ROAD)
        s_junc_ctrl[row * R_MAP + col] = (uint8_t)junction_control(c, col, row, links);
    if (f == F_RAIL)
    {
        /*  A rail junction within its box (the user: "train T crossing
         *  are incorrect"; three half strips to the centre had crossed
         *  the branch's rails and ties through the through track's).
         *  The through line runs across the box whole.  A T is a
         *  junction of two double-track lines: the branch's inbound
         *  track, the right-hand one, curves right into the near through
         *  track, and its outbound track runs on across the near track,
         *  a diamond, and curves left into the far one, each curve a
         *  quarter circle of the largest radius the box allows, 0.177 of
         *  a tile; the spec's turnout (5.4) takes two to four tiles the
         *  art does not give.  A crossing is two whole strips, a diamond. */
        int n = link_count(links), eb = -1, ea = -1;
        for (e = 0; e < 4; ++e)
            if ((links & (1 << e)) && !(links & (1 << ((e + 2) & 3))))
                eb = e; /* the branch: the arm without an opposite */
            else if (links & (1 << e))
                ea = e; /* a through arm */
        if (n >= 3 && ea >= 0)
        {
            /* the through strip: across along the through arm's normal, along the through axis */
            float tx = ROAD_DU[ea], ty = ROAD_DV[ea], nx = -ty, ny = tx;
            float q0[2] = {cx - nx * h - tx * h, cy - ny * h - ty * h}, q1[2] = {cx + nx * h - tx * h, cy + ny * h - ty * h};
            float p0[2] = {cx - nx * h + tx * h, cy - ny * h + ty * h}, p1[2] = {cx + nx * h + tx * h, cy + ny * h + ty * h};
            if (strip_quad_z(m, c, mask_bit, order, q0, q1, p0, p1, zj, zj, -1.0f, 1.0f, -h, h, mat) != 0)
                return -1;
        }
        if (n == 4)
        {
            /* the other line of the diamond, over the first */
            float tx = ROAD_DV[ea], ty = -ROAD_DU[ea], nx = -ty, ny = tx;
            float q0[2] = {cx - nx * h - tx * h, cy - ny * h - ty * h}, q1[2] = {cx + nx * h - tx * h, cy + ny * h - ty * h};
            float p0[2] = {cx - nx * h + tx * h, cy - ny * h + ty * h}, p1[2] = {cx + nx * h + tx * h, cy + ny * h + ty * h};
            if (strip_quad_z(m, c, mask_bit, order + 0.03f, q0, q1, p0, p1, zj, zj, -1.0f, 1.0f, -h, h, mat) != 0)
                return -1;
        }
        else if (n == 3 && eb >= 0)
        {
            const float tk = 0.133f, r = h - 0.133f, tw = 0.087f; /* the track offset, the curve, half a tie */
            float       ax = -ROAD_DU[eb], ay = -ROAD_DV[eb];     /* inbound, from the branch's edge to the centre */
            float       rx = -ay, ry = ax;                        /* to the right of inbound */
            float       cX, cY, along, L0[2], L1[2], R0[2], R1[2];
            int         k;
            /*  The inbound track (right of inbound) curves right into the
             *  near through track: a quarter circle about the box's corner
             *  on that side, from the box's edge to its side. */
            cX = cx + rx * h - ax * h;
            cY = cy + ry * h - ay * h;
            for (k = 0; k < 6; ++k)
            {
                float t0 = 1.5707963f * (float)k / 6.0f, t1 = 1.5707963f * (float)(k + 1) / 6.0f;
                float ex0 = -rx * cosf(t0) + ax * sinf(t0), ey0 = -ry * cosf(t0) + ay * sinf(t0);
                float ex1 = -rx * cosf(t1) + ax * sinf(t1), ey1 = -ry * cosf(t1) + ay * sinf(t1);
                L0[0] = cX + ex0 * (r - tw), L0[1] = cY + ey0 * (r - tw);
                R0[0] = cX + ex0 * (r + tw), R0[1] = cY + ey0 * (r + tw);
                L1[0] = cX + ex1 * (r - tw), L1[1] = cY + ey1 * (r - tw);
                R1[0] = cX + ex1 * (r + tw), R1[1] = cY + ey1 * (r + tw);
                if (strip_quad_z(m, c, mask_bit, order + 0.03f, L0, R0, L1, R1, zj, zj, 0.15f, 0.71f, r * t0, r * t1, mat) != 0)
                    return -1;
            }
            /*  The outbound track (left of inbound) runs straight across
             *  the near track, then curves left into the far one. */
            {
                float p0x = cx - rx * tk - ax * h, p0y = cy - ry * tk - ay * h;
                float p1x = cx - rx * tk + ax * (tk - r), p1y = cy - ry * tk + ay * (tk - r);
                along = sqrtf((p1x - p0x) * (p1x - p0x) + (p1y - p0y) * (p1y - p0y));
                L0[0] = p0x - rx * tw, L0[1] = p0y - ry * tw;
                R0[0] = p0x + rx * tw, R0[1] = p0y + ry * tw;
                L1[0] = p1x - rx * tw, L1[1] = p1y - ry * tw;
                R1[0] = p1x + rx * tw, R1[1] = p1y + ry * tw;
                if (strip_quad_z(m, c, mask_bit, order + 0.03f, L0, R0, L1, R1, zj, zj, 0.15f, 0.71f, 0.0f, along, mat) != 0)
                    return -1;
                cX = p1x - rx * r;
                cY = p1y - ry * r;
            }
            for (k = 0; k < 6; ++k)
            {
                float t0 = 1.5707963f * (float)k / 6.0f, t1 = 1.5707963f * (float)(k + 1) / 6.0f;
                float ex0 = rx * cosf(t0) + ax * sinf(t0), ey0 = ry * cosf(t0) + ay * sinf(t0);
                float ex1 = rx * cosf(t1) + ax * sinf(t1), ey1 = ry * cosf(t1) + ay * sinf(t1);
                L0[0] = cX + ex0 * (r - tw), L0[1] = cY + ey0 * (r - tw);
                R0[0] = cX + ex0 * (r + tw), R0[1] = cY + ey0 * (r + tw);
                L1[0] = cX + ex1 * (r - tw), L1[1] = cY + ey1 * (r - tw);
                R1[0] = cX + ex1 * (r + tw), R1[1] = cY + ey1 * (r + tw);
                if (strip_quad_z(m, c, mask_bit, order + 0.03f, L0, R0, L1, R1, zj, zj, 0.15f, 0.71f, along + r * t0, along + r * t1, mat) != 0)
                    return -1;
            }
        }
        return 0;
    }
    {
        /*  The asphalt: the outline the arms cut out, as a fan from the
         *  middle.  With every arm on a tile axis this is the square it
         *  has always been. */
        V2      poly[16];
        uint8_t mouth[16];
        float   trm[4];
        int     np = junction_poly(c, f, col, row, links, poly, mouth, 16, trm), i;
        float   rc[3] = {0.0f, 0.0f, mat}, ref[3] = {0.5f, 0.5f, 0.5f}, ref2[3] = {-1.0f, -1.0f, -1.0f};
        if (np < 3)
        {
            if (strip_quad_z(m, c, mask_bit, order, a0, a1, b0, b1, zj, zj, 0.5f, 0.5f, -1.0f, -1.0f, mat) != 0)
                return -1;
        }
        for (i = 0; i < np; ++i)
        {
            V2    p0 = poly[i], p1 = poly[(i + 1) % np];
            float tri[3][3];
            if (fabsf(p0.x - p1.x) < 1e-5f && fabsf(p0.y - p1.y) < 1e-5f)
                continue;
            /*  Each point at its own ground, and the fan split halfway
             *  out.  The junction's own tile is a levelled pad, so inside
             *  it every height here is the flat one it always was; where
             *  the outline reaches past the tile the asphalt has to
             *  follow the ground, and a triangle running from the middle
             *  straight to the rim cuts under it on the way (the road
             *  clip check: twelve tiles, terrain above by 0.02). */
            V2    m0 = {0.5f * (cx + p0.x), 0.5f * (cy + p0.y)};
            V2    m1 = {0.5f * (cx + p1.x), 0.5f * (cy + p1.y)};
            float z0  = surface_at_world(c, mask_bit, p0.x, p0.y) + 0.05f;
            float z1  = surface_at_world(c, mask_bit, p1.x, p1.y) + 0.05f;
            float zm0 = surface_at_world(c, mask_bit, m0.x, m0.y) + 0.05f;
            float zm1 = surface_at_world(c, mask_bit, m1.x, m1.y) + 0.05f;
            const float fan[3][3][3] = {
                {{cx, cy, zj}, {m0.x, m0.y, zm0}, {m1.x, m1.y, zm1}},
                {{m0.x, m0.y, zm0}, {p0.x, p0.y, z0}, {p1.x, p1.y, z1}},
                {{m0.x, m0.y, zm0}, {p1.x, p1.y, z1}, {m1.x, m1.y, zm1}}};
            int k;
            for (k = 0; k < 3; ++k)
            {
                memcpy(tri, fan[k], sizeof tri);
                if (put_tri_road_n(m, c, mask_bit, order, (const float (*)[3])tri, NULL, rc, ref, ref2) != 0)
                    return -1;
            }
        }
        /*  The footway, round the outline itself rather than round the
         *  square this used to be: a band inside every edge that is not
         *  an arm's mouth, which leaves the pavement open where a road
         *  comes in and closed everywhere else, and takes the outline's
         *  own corners as it goes. */
        if (f == F_ROAD && np >= 3)
            for (i = 0; i < np; ++i)
            {
                int   j  = (i + 1) % np;
                V2    p0 = poly[i], p1 = poly[j];
                float ex = p1.x - p0.x, ey = p1.y - p0.y;
                float el = sqrtf(ex * ex + ey * ey), nx, ny;
                float o0[2], o1[2], q0[2], q1[2];
                if (mouth[i] && mouth[j] && el < 2.05f * h && el > 1.95f * h)
                    continue; /* an arm's mouth: the road runs on through */
                if (el < 1e-4f)
                    continue;
                /*  Only where the outline is still over the junction's
                 *  own tile.  Past it the ground is whatever the
                 *  neighbour's is, and a level band laid over that cuts
                 *  into the hillside; the pavement simply stops there,
                 *  as it does at any kerb. */
                if ((int32_t)floorf(p0.x) != col || (int32_t)floorf(p0.y) != row ||
                    (int32_t)floorf(p1.x) != col || (int32_t)floorf(p1.y) != row)
                    continue;
                /* inward: toward the middle of the junction */
                nx = -ey / el;
                ny = ex / el;
                if (nx * (cx - p0.x) + ny * (cy - p0.y) < 0.0f)
                {
                    nx = -nx;
                    ny = -ny;
                }
                q0[0] = p0.x;
                q0[1] = p0.y;
                q1[0] = p1.x;
                q1[1] = p1.y;
                o0[0] = p0.x + nx * lw;
                o0[1] = p0.y + ny * lw;
                o1[0] = p1.x + nx * lw;
                o1[1] = p1.y + ny * lw;
                {
                    /*  Flat, at the highest ground any of its corners
                     *  stands on: a band this narrow reads as level, and
                     *  taking each end's own height tilted it into the
                     *  hillside where the outline reaches past the pad. */
                    float zf = zj;
                    float za = surface_at_world(c, mask_bit, p0.x, p0.y) + 0.05f;
                    float zb = surface_at_world(c, mask_bit, p1.x, p1.y) + 0.05f;
                    if (za > zf)
                        zf = za;
                    if (zb > zf)
                        zf = zb;
                    if (strip_quad_z(m, c, mask_bit, order + 0.02f, q0, q1, o0, o1,
                                     zf, zf, 1.0f, 0.80f, -1.0f, -1.0f, mat) != 0)
                        return -1;
                }
            }
    }
    for (e = 0; e < 4; ++e)
    {
        /* the side: a free side carries the sidewalk and its curb along its length */
        float su = cx + ROAD_DU[e] * h, sv = cy + ROAD_DV[e] * h;
        float px = -ROAD_DV[e], py = ROAD_DU[e];
        if (!(links & (1 << e)))
        {
            float o0[2] = {su - px * h - ROAD_DU[e] * lw, sv - py * h - ROAD_DV[e] * lw};
            float o1[2] = {su + px * h - ROAD_DU[e] * lw, sv + py * h - ROAD_DV[e] * lw};
            float q0[2] = {su - px * h, sv - py * h}, q1[2] = {su + px * h, sv + py * h};
            if (strip_quad_z(m, c, mask_bit, order + 0.02f, o0, o1, q0, q1, zj, zj, 0.80f, 1.0f, -1.0f, -1.0f, mat) != 0)
                return -1;
        }
        else if (f == F_ROAD)
        {
            int ctrl = (s_junc_ctrl[row * R_MAP + col] >> (2 * e)) & 3;
            if (ctrl == 2 && put_signal(m, c, col, row, mask_bit, order, e, h) != 0)
                return -1;
            if (ctrl == 1 && put_stop_sign(m, c, col, row, mask_bit, order, e, h) != 0)
                return -1;
        }
    }
    if (f == F_ROAD)
        for (e = 0; e < 4; ++e)
        {
            /*  The corner between side e and side e+1.  Two arms meet
             *  there: a curb return, the sidewalk's corner rounded on a
             *  fillet of radius sw.  Otherwise the sidewalk's square. */
            int   ea = e, eb = (e + 1) & 3;
            float ku = cx + (ROAD_DU[ea] + ROAD_DU[eb]) * h, kv = cy + (ROAD_DV[ea] + ROAD_DV[eb]) * h;
            float iu = ku - (ROAD_DU[ea] + ROAD_DU[eb]) * sw, iv = kv - (ROAD_DV[ea] + ROAD_DV[eb]) * sw;
            if ((links & (1 << ea)) && (links & (1 << eb)))
            {
                /* the return: a fan from the corner point over the quarter circle about (iu, iv)...
                 * drawn as the region between the corner and the arc, as a fan from the corner */
                float ang0 = atan2f(ROAD_DV[ea] * h, ROAD_DU[ea] * h);
                float ang1 = atan2f(ROAD_DV[eb] * h, ROAD_DU[eb] * h);
                float t0   = atan2f(-(ROAD_DV[ea] + ROAD_DV[eb]), -(ROAD_DU[ea] + ROAD_DU[eb]));
                int   i;
                (void)ang0;
                (void)ang1;
                /* the arc about (iu, iv), radius sw, spanning the 90 degrees that face the corner */
                for (i = 0; i < 6; ++i)
                {
                    float fa = (float)i / 6.0f, fb = (float)(i + 1) / 6.0f;
                    float ta = t0 - 0.7853982f + 1.5707963f * fa, tb = t0 - 0.7853982f + 1.5707963f * fb;
                    float tri[3][3], ref[3] = {0.95f, 0.95f, 0.95f}, ref2[3] = {-1.0f, -1.0f, -1.0f};
                    float rc[3] = {0.0f, 0.0f, MAT_WALK};
                    /* the region between the arc and the corner: a fan from the corner point */
                    tri[0][0] = ku;
                    tri[0][1] = kv;
                    tri[1][0] = iu + sw * cosf(ta);
                    tri[1][1] = iv + sw * sinf(ta);
                    tri[2][0] = iu + sw * cosf(tb);
                    tri[2][1] = iv + sw * sinf(tb);
                    tri[0][2] = tri[1][2] = tri[2][2] = zj;
                    /* over the box, which a sidewalk face otherwise lies under */
                    if (put_tri_r2(m, (const float (*)[3])tri, NULL, order + 0.15f, rc, ref, ref2, 0) != 0)
                        return -1;
                }
                (void)ku;
                (void)kv;
            }
        }
    return 0;
}

/*  ==================================================================
 *  The other things this file builds
 *
 *  Power lines and the clipped prisms they and the rails stand on.
 *  ================================================================== */
/*  A power line on one tile: the pole at the centre with its crossbar,
 *  and a wire from its top to each joined edge's midpoint, where the
 *  neighbour's wire meets it.  On a crossing over a road or rail there
 *  is no pole: the wire spans from edge to edge. */
int build_power_tile(RMesh *m, const RCity *c, int32_t col, int32_t row, uint8_t mask_bit, int links, float order, int crossing)
{
    int   e;
    float top = 1.45f, cx = (float)col + 0.5f, cy = (float)row + 0.5f;
    if (!crossing)
    {
        int ns = (links & (L_N | L_S)) && !(links & (L_E | L_W));
        if (put_box(m, c, mask_bit, order, cx, cy, 0.04f, 0.04f, 0.0f, top, MAT_PROP, 0.0f) != 0)
            return -1;
        if (put_box(m, c, mask_bit, order, cx, cy, ns ? 0.24f : 0.04f, ns ? 0.04f : 0.24f, top - 0.12f, top - 0.08f, MAT_PROP, 0.0f) != 0)
            return -1;
        for (e = 0; e < 4; ++e)
            if (links & (1 << e))
                if (put_wire(m, c, mask_bit, order, cx, cy, top - 0.1f, (float)col + ROAD_MU[e], (float)row + ROAD_MV[e], top - 0.1f, 0.05f) != 0)
                    return -1;
        return 0;
    }
    if ((links & (L_N | L_S)) == (L_N | L_S))
        return put_wire(m, c, mask_bit, order, cx, (float)row, top - 0.1f, cx, (float)row + 1.0f, top - 0.1f, 0.1f);
    return put_wire(m, c, mask_bit, order, (float)col, cy, top - 0.1f, (float)col + 1.0f, cy, top - 0.1f, 0.1f);
}

/*  A prism clipped to the tile grid, each piece in the depth slot of
 *  the tile under it (the user: "train cars get masked off once in a
 *  while, like they flicker").  A car had carried the order of the tile
 *  under its centre, and the part of it over the next tile toward the
 *  viewer lay under that tile's ground until the centre crossed, when
 *  the whole car stood up at once.  `order` is the centre tile's slot
 *  with the fraction the pieces keep above each tile's ground. */
static int put_prism_clip_m(RMesh *m, const RCity *c, uint8_t mask_bit, float order, float cx, float cy, float dx, float dy, float len, float wid, float zb, float zf, float z0, float z1, float paint, float mat)
{
    static const float up[3] = {0.0f, 0.0f, 1.0f};
    float              ax = dx * len * 0.5f, ay = dy * len * 0.5f, bx = -dy * wid * 0.5f, by = dx * wid * 0.5f;
    float              p[4][3], top[4][3], col[3] = {paint, 0.0f, mat}, nrm[3], t3[3][3];
    float              ref[3] = {paint, paint, paint}, ref2[3] = {0.0f, 0.0f, 0.0f};
    int                k;
    p[0][0] = cx - ax - bx;
    p[0][1] = cy - ay - by;
    p[1][0] = cx + ax - bx;
    p[1][1] = cy + ay - by;
    p[2][0] = cx + ax + bx;
    p[2][1] = cy + ay + by;
    p[3][0] = cx - ax + bx;
    p[3][1] = cy - ay + by;
    for (k = 0; k < 4; ++k)
    {
        float zg  = (k == 1 || k == 2) ? zf : zb;
        p[k][2]   = zg + z0;
        top[k][0] = p[k][0];
        top[k][1] = p[k][1];
        top[k][2] = zg + z1;
    }
    for (k = 0; k < 4; ++k)
    {
        const float *a = p[k], *b = p[(k + 1) & 3], *ta = top[k], *tb = top[(k + 1) & 3];
        float        ex = b[0] - a[0], ey = b[1] - a[1], el = sqrtf(ex * ex + ey * ey);
        if (el < 1e-6f)
            continue;
        nrm[0] = ey / el;
        nrm[1] = -ex / el;
        nrm[2] = 0.0f;
        memcpy(t3[0], ta, sizeof t3[0]);
        memcpy(t3[1], tb, sizeof t3[1]);
        memcpy(t3[2], b, sizeof t3[2]);
        if (put_tri_road_n(m, c, mask_bit, order, (const float (*)[3])t3, nrm, col, ref, ref2) != 0)
            return -1;
        memcpy(t3[0], ta, sizeof t3[0]);
        memcpy(t3[1], b, sizeof t3[1]);
        memcpy(t3[2], a, sizeof t3[2]);
        if (put_tri_road_n(m, c, mask_bit, order, (const float (*)[3])t3, nrm, col, ref, ref2) != 0)
            return -1;
    }
    memcpy(t3[0], top[0], sizeof t3[0]);
    memcpy(t3[1], top[1], sizeof t3[1]);
    memcpy(t3[2], top[2], sizeof t3[2]);
    if (put_tri_road_n(m, c, mask_bit, order, (const float (*)[3])t3, up, col, ref, ref2) != 0)
        return -1;
    memcpy(t3[0], top[0], sizeof t3[0]);
    memcpy(t3[1], top[2], sizeof t3[1]);
    memcpy(t3[2], top[3], sizeof t3[2]);
    return put_tri_road_n(m, c, mask_bit, order, (const float (*)[3])t3, up, col, ref, ref2);
}

int put_prism_clip(RMesh *m, const RCity *c, uint8_t mask_bit, float order, float cx, float cy, float dx, float dy, float len, float wid, float zb, float zf, float z0, float z1, float paint)
{
    return put_prism_clip_m(m, c, mask_bit, order, cx, cy, dx, dy, len, wid, zb, zf, z0, z1, paint, MAT_VEHICLE);
}

/*  Every network: the power lines per tile; the roads and rails as
 *  junctions and segments. */
