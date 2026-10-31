/*  network/shape.c: layer 3 of the road network, the shape (plans/network.md).
 *
 *  Roads linked across a seam are shaped as one RUN.  A run goes from the
 *  junction at one end to the junction at the other.  Its line from above is
 *  the tangent fit on the grid of its network's units.  That is the longest
 *  straights, and an arc at each vertex where they meet.  Its height is one
 *  solver, the smoothest profile between a floor and a ceiling.  Then the run
 *  is cut back into its roads, each with its own pieces of line and of height
 *  in its own direction.
 *
 *  Every number the shape is tuned by is in the block below. */
#include "network/shape.h"
#include "grow.h"
#include "network/solved.h"
#include "network/state.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "dump.h"
#include "log.h"
#include "network/network.h"
#include "network/topology.h"
#include "terrain.h"

/* ---- the knobs ----------------------------------------------------------- */

/*  A run of units is a straight when one line passes through every unit's
 *  square, with this half side in units. */
#define STAB 0.5
/*  The corridor: a road's units widened by this, in tiles. */
#define MARGIN 0.25
/*  A corner takes at most this part of each straight beside it. */
#define HALF 0.5
/*  A street stays within this many levels of its tiles' heights. */
#define STREET_HOLD 1.0
/*  How strongly a street keeps to its tiles' heights, and a deck to the level
 *  it holds, against bending. */
#define STREET_PULL 0.02
#define DECK_PULL   0.01
/*  A street never rises above the highest of its tiles' heights within this
 *  many tiles, nor falls below the lowest. */
#define REACH 2.0
/*  A level's height in tiles, as the mesh draws it. */
#define LEVEL_TILES 0.5
/*  A deck never climbs steeper than this angle, in degrees: one level for
 *  each tile and a half. */
#define MAX_GRADE 18.435
/*  A deck stands this many levels above the highest ground under a unit. */
#define CLEARANCE 1.0
/*  A vertical curve reaches at most this many tiles each way from a change
 *  of grade.  It passes the change at most VCURVE_DROP levels off it: that
 *  is its middle ordinate, the change of grade times its reach over 4. */
#define VCURVE_HALF 1.0
#define VCURVE_DROP 0.01
/*  A deck holds a level across a dip narrower than twice FLAT tiles and no
 *  deeper than EXTRA levels. */
#define FLAT  4.0
#define EXTRA 1.0
/*  A deck may rise this many levels above a level it holds, to ease onto it. */
#define SMOOTH 0.25
/*  The spacing of a street's heights and of a deck's, in tiles. */
#define STREET_STEP 0.25
#define DECK_STEP   0.5

/*  Per network: the size of its unit in cells and its largest corner radius,
 *  in tiles.  A size of nought is a network the shape does not give a line.
 *  The width the fit leaves for a road is its family's (layer 2). */
static const struct
{
    int    size;
    double radius;
} NET[RNET_COUNT] = {
    [RNET_ROAD] = {1, 1.5},
    [RNET_RAIL] = {1, 6.0}, /* a train's gentle curve */
    [RNET_BAND] = {2, 4.0},
};

/*  The least radius a railway's curve has, in tiles.  A railway leaves its
 *  own cells before it bends tighter. */
#define RAIL_MIN_RADIUS 1.5

/* ---- the store ----------------------------------------------------------- */

static SRoad  *s_road;
static int     s_nroad;
static SGeom  *s_geom;
static int     s_ngeom, s_capgeom;
static SElev  *s_elev;
static int     s_nelev, s_capelev;
static float  *s_map;
static int     s_nmap, s_capmap;
static float **s_bound; /* per junction, its outline */
static int    *s_nbound;
static int   **s_bmouth; /* per junction, the mouth each outline point belongs to: an edge is a mouth where both its ends share one */
static int   **s_barm;   /* per junction, for each outline point the arm it stands on, as 2 * road + contact, or -1 */
static float  *s_cut;    /*  per road and contact, 2 * road + contact: how far
                          *  its junction reaches along it from its end, in
                          *  tiles.  Below nought it stops short of the end */
static int s_njunction;

static void ix_free(void);

static void shape_free(void)
{
    ix_free();
    int i;
    for (i = 0; i < s_njunction; i++)
        free(s_bound[i]), free(s_bmouth ? s_bmouth[i] : NULL), free(s_barm ? s_barm[i] : NULL);
    free(s_barm), free(s_cut);
    s_barm = NULL, s_cut = NULL;
    free(s_road), free(s_geom), free(s_elev), free(s_map), free(s_bound), free(s_nbound), free(s_bmouth);
    s_road = NULL, s_geom = NULL, s_elev = NULL, s_map = NULL, s_bound = NULL, s_nbound = NULL, s_bmouth = NULL;
    s_nroad = s_ngeom = s_nelev = s_nmap = s_njunction = 0;
    s_capgeom = s_capelev = s_capmap = 0;
}

/* ---- the land ------------------------------------------------------------ */

static const RCity *s_city;

/*  A tile's four corner heights, in levels: at (col, row), (col, row + 1),
 *  (col + 1, row + 1) and (col + 1, row).  A tile of water is flat at its
 *  surface, so a road over it stands on the water. */
static void corners(int32_t col, int32_t row, double h[4])
{
    int32_t n  = s_city->n, k;
    col        = col < 0 ? 0 : col >= n ? n - 1
                                        : col;
    row        = row < 0 ? 0 : row >= n ? n - 1
                                        : row;
    unsigned a = s_city->map.altm[row * n + col];
    uint8_t  x = s_city->map.xter[row * n + col];
    if (is_water(x))
    {
        for (k = 0; k < 4; k++)
            h[k] = rcity_alt_surface(a, x);
        return;
    }
    int lift = corner_lifts(slope_code(x));
    for (k = 0; k < 4; k++)
        h[k] = rcity_alt_ground(a) + ((lift >> k) & 1);
}

/*  The height of a tile's own slope at a point (x, y) inside it. */
static double plane_at(int32_t col, int32_t row, double x, double y)
{
    double h[4], u = x - col, v = y - row;
    corners(col, row, h);
    return (1 - u) * (1 - v) * h[0] + u * (1 - v) * h[3] + (1 - u) * v * h[1] + u * v * h[2];
}

/* ---- the line from above: the tangent fit --------------------------------- */

#define ANGLES 720 /* a quarter degree apart, from nought to half a turn */

typedef struct
{
    double x, y;
} P;

typedef struct
{
    double theta, d; /* the line's angle in degrees, and its offset along its normal */
} Line;

/*  The lines through every square of a growing run of units.  For each angle,
 *  the offsets of the lines through every square so far are an interval: a
 *  line with unit normal n and offset d meets the square of center p and half
 *  side h when |n.p - d| <= h (|nx| + |ny|). */
typedef struct
{
    double lo[ANGLES], hi[ANGLES];
} Stab;

static void stab_start(Stab *t)
{
    int a;
    for (a = 0; a < ANGLES; a++)
        t->lo[a] = -1e18, t->hi[a] = 1e18;
}

static void stab_add(Stab *t, const P *u, double h)
{
    int a;
    for (a = 0; a < ANGLES; a++)
    {
        double r = (a * 0.25) * M_PI / 180.0, nx = -sin(r), ny = cos(r);
        double v = nx * (u->x + 0.5) + ny * (u->y + 0.5), reach = h * (fabs(nx) + fabs(ny));
        if (v - reach > t->lo[a])
            t->lo[a] = v - reach;
        if (v + reach < t->hi[a])
            t->hi[a] = v + reach;
    }
}

/*  The line with the most room through every square so far.  0 where no line
 *  passes through them all. */
static int stab_best(const Stab *t, Line *out)
{
    int    a, found = 0;
    double best = -1.0;
    for (a = 0; a < ANGLES; a++)
        if (t->lo[a] <= t->hi[a] && t->hi[a] - t->lo[a] > best)
        {
            best       = t->hi[a] - t->lo[a];
            out->theta = a * 0.25;
            out->d     = (t->lo[a] + t->hi[a]) / 2;
            found      = 1;
        }
    return found;
}

/*  How many steps a run takes across its main direction: one is a corner, two
 *  or more a staircase. */
static int risers(const P *u, int n)
{
    int k, dx = 0, dy = 0;
    for (k = 0; k + 1 < n; k++)
        dx += fabs(u[k + 1].x - u[k].x) > 0.5, dy += fabs(u[k + 1].y - u[k].y) > 0.5;
    return dx < dy ? dx : dy;
}

/*  Whether a run's steps keep a straight line's rhythm.  Every step across
 *  goes the same way, two never come together, and the runs of steps along
 *  between them differ by at most one.  The first and the last run may be
 *  shorter, as a line cut anywhere starts and ends part of the way into a run. */
static int rhythm(const P *u, int n)
{
    int    k, along = 0, nruns = 0, cur = 0, ok = 1, main_x, have_a = 0, have_r = 0;
    double ax = 0, ay = 0, rx = 0, ry = 0;
    int   *runs = (int *)malloc(sizeof *runs * (size_t)(n + 1));
    if (!runs)
        return 0;
    for (k = 0; k + 1 < n; k++)
        along += fabs(u[k + 1].x - u[k].x) > 0.5;
    main_x = along >= (n - 1) - along;
    for (k = 0; k + 1 < n && ok; k++)
    {
        double dx = u[k + 1].x - u[k].x, dy = u[k + 1].y - u[k].y;
        if (main_x ? fabs(dy) > 0.5 : fabs(dx) > 0.5)
        {
            if (have_r && (dx != rx || dy != ry))
                ok = 0;
            rx = dx, ry = dy, have_r = 1;
            runs[nruns++] = cur;
            cur           = 0;
        }
        else
        {
            if (have_a && (dx != ax || dy != ay))
                ok = 0;
            ax = dx, ay = dy, have_a = 1;
            cur++;
        }
    }
    runs[nruns++] = cur;
    if (ok && nruns > 2)
    {
        int lo = runs[1], hi = runs[1];
        for (k = 1; k < nruns - 1; k++)
        {
            ok = ok && runs[k] != 0;
            lo = runs[k] < lo ? runs[k] : lo;
            hi = runs[k] > hi ? runs[k] : hi;
        }
        ok = ok && hi - lo <= 1 && runs[0] <= hi && runs[nruns - 1] <= hi;
    }
    else if (ok && nruns == 2)
        ok = abs(runs[0] - runs[1]) <= 1 || (runs[0] < runs[1] ? runs[0] : runs[1]) == 0;
    free(runs);
    return ok;
}

/*  Whether a run alternates its two steps every time, over at least
 *  `least` units: a 45 degree diagonal, however often it steps across. */
static int diagonal(const P *u, int n, int least)
{
    int k;
    if (n < least)
        return 0;
    for (k = 0; k + 2 < n; k++)
        if (u[k + 1].x - u[k].x == u[k + 2].x - u[k + 1].x && u[k + 1].y - u[k].y == u[k + 2].y - u[k + 1].y)
            return 0;
    return 1;
}

typedef struct
{
    int  i, j;  /* its first and last unit in the run */
    int  fixed; /* 1 where a unit's piece fixes it (axis) */
    Line line;
} Straight;

/*  The straight along the axis from the center of unit a to the center
 *  of unit b beside it.  It crosses the edge they share square, at its
 *  middle. */
static Straight axis(const P *u, int a, int b)
{
    Straight st;
    double   dx = u[b].x - u[a].x, dy = u[b].y - u[a].y, r = atan2(dy, dx);
    st.i = a < b ? a : b, st.j = a < b ? b : a, st.fixed = 1;
    st.line.theta = r * 180.0 / M_PI;
    st.line.d     = -sin(r) * (u[a].x + 0.5) + cos(r) * (u[a].y + 0.5);
    return st;
}

/*  The longest straights over a run of units, greedily from its first.  A run
 *  that fails may pass again with more units: a second step across makes a
 *  staircase of a corner.  So a straight grows to the longest that passes.
 *  A unit whose piece fixes the road's line there is FIXED, and its straight
 *  is the axis through it, square to the edges it joins.  The run's end at a
 *  junction is one: the road keeps its angle into the junction, from that
 *  unit into the next.  A level meet inside the run is another: the road
 *  goes straight across it, from the unit before it to the unit after.  A
 *  free straight ends at the unit next to a fixed one.  It answers the
 *  count, or -1 out of memory. */
static int straights(const P *u, int n, const unsigned char *fixed, int diag_least, Straight **out)
{
    Straight *s = (Straight *)malloc(sizeof *s * (size_t)(n > 1 ? n : 1));
    Stab     *t = (Stab *)malloc(sizeof *t);
    int       i = 0, ns = 0, k;
    if (!s || !t)
    {
        free(s), free(t);
        return -1;
    }
    while (i < n - 1)
    {
        int  j = i + 1, end;
        Line fit, f2;
        if (n >= 3 && i == 0 && fixed[0])
        {
            s[ns++] = axis(u, 0, 1), i = 1;
            continue;
        }
        if (i + 2 < n && fixed[i + 1] && i + 1 < n - 1)
        {
            s[ns++] = axis(u, i, i + 2), i += 2;
            continue;
        }
        if (n >= 3 && i == n - 2 && fixed[n - 1])
        {
            s[ns++] = axis(u, n - 2, n - 1), i = n - 1;
            continue;
        }
        /*  As far as the unit before the next fixed one. */
        for (end = i + 1; end < n - 1 && !fixed[end + 1]; end++)
            ;
        if (end == n - 1 && n >= 3 && fixed[n - 1])
            end = n - 2;
        if (end <= i)
            end = i + 1;
        stab_start(t);
        stab_add(t, &u[i], STAB);
        stab_add(t, &u[i + 1], STAB);
        stab_best(t, &fit);
        for (k = i + 2; k <= end; k++)
        {
            stab_add(t, &u[k], STAB);
            if (!stab_best(t, &f2))
                break;
            int r = risers(u + i, k - i + 1);
            if ((r == 0 || r > 2 || diagonal(u + i, k - i + 1, diag_least)) && rhythm(u + i, k - i + 1))
                j = k, fit = f2;
        }
        s[ns].i = i, s[ns].j = j, s[ns].line = fit, s[ns].fixed = 0;
        ns++;
        i = j;
    }
    free(t);
    *out = s;
    return ns;
}

static P normal(Line l)
{
    double r = l.theta * M_PI / 180.0;
    P      n = {-sin(r), cos(r)};
    return n;
}

/*  Where two lines meet.  0 where they are parallel. */
static int meet(Line a, Line b, P *at)
{
    P      n1 = normal(a), n2 = normal(b);
    double det = n1.x * n2.y - n1.y * n2.x;
    if (fabs(det) < 1e-9)
        return 0;
    at->x = (a.d * n2.y - n1.y * b.d) / det;
    at->y = (n1.x * b.d - a.d * n2.x) / det;
    return 1;
}

/*  The point of a line nearest p. */
static P foot(Line l, P p)
{
    P      n = normal(l);
    double k = n.x * p.x + n.y * p.y - l.d;
    P      f = {p.x - k * n.x, p.y - k * n.y};
    return f;
}

/*  The polyline of the straights.  It starts at the foot of the first
 *  unit's center on the first straight and goes through each meeting
 *  point.  It ends at the foot of the last unit's center on the last
 *  straight.  Two parallel straights step across at the unit they share.
 *  Two straights that meet more than a unit from the unit they share are
 *  nearly parallel.  The polyline would run past that unit and back.
 *  They meet between the feet of that unit's center on each. */
static int polyline(const P *u, int n, const Straight *s, int ns, P *pts)
{
    int k, np = 0;
    P   c0 = {u[0].x + 0.5, u[0].y + 0.5}, c1 = {u[n - 1].x + 0.5, u[n - 1].y + 0.5};
    pts[np++] = foot(s[0].line, c0);
    for (k = 0; k + 1 < ns; k++)
    {
        P m, c = {u[s[k].j].x + 0.5, u[s[k].j].y + 0.5};
        if (!meet(s[k].line, s[k + 1].line, &m))
            m = c;
        else if (hypot(m.x - c.x, m.y - c.y) > 1.0)
        {
            P f0 = foot(s[k].line, c), f1 = foot(s[k + 1].line, c);
            m.x = 0.5 * (f0.x + f1.x), m.y = 0.5 * (f0.y + f1.y);
        }
        pts[np++] = m;
    }
    pts[np++] = foot(s[ns - 1].line, c1);
    return np;
}

/*  A corner at a vertex of the polyline: its two tangent points, the center of
 *  its arc, its radius, and the arc's angles.  A vertex that is straight on
 *  keeps no arc, and a vertex on top of its neighbor is passed over. */
typedef struct
{
    int    kind; /* 0 passed over, 1 straight on, 2 an arc */
    P      at, p0, p1, cen;
    double r, a0, da;
} Corner;

static Corner corner(P a, P b, P c, double R, double tcap)
{
    Corner k;
    memset(&k, 0, sizeof k);
    k.at     = b;
    P      u = {a.x - b.x, a.y - b.y}, v = {c.x - b.x, c.y - b.y};
    double lu = hypot(u.x, u.y), lv = hypot(v.x, v.y);
    if (lu < 1e-6 || lv < 1e-6)
        return k;
    u.x /= lu, u.y /= lu, v.x /= lv, v.y /= lv;
    double cosang = u.x * v.x + u.y * v.y;
    cosang        = cosang < -1 ? -1 : cosang > 1 ? 1
                                                  : cosang;
    double ang = acos(cosang), turn = M_PI - ang;
    if (turn < 1e-4)
    {
        k.kind = 1;
        return k;
    }
    double t = R * tan(turn / 2), tmax = tcap >= 0.0 ? tcap : HALF * (lu < lv ? lu : lv);
    if (t > tmax)
        t = tmax;
    double r   = t / tan(turn / 2);
    P      bis = {u.x + v.x, u.y + v.y};
    double bl  = hypot(bis.x, bis.y);
    bis.x /= bl, bis.y /= bl;
    k.kind = 2;
    k.r    = r;
    k.p0.x = b.x + u.x * t, k.p0.y = b.y + u.y * t;
    k.p1.x = b.x + v.x * t, k.p1.y = b.y + v.y * t;
    k.cen.x = b.x + bis.x * r / sin(ang / 2), k.cen.y = b.y + bis.y * r / sin(ang / 2);
    k.a0      = atan2(k.p0.y - k.cen.y, k.p0.x - k.cen.x);
    double a1 = atan2(k.p1.y - k.cen.y, k.p1.x - k.cen.x);
    k.da      = fmod(a1 - k.a0 + 3 * M_PI, 2 * M_PI) - M_PI;
    return k;
}

/*  How far a point lies outside the squares of a run's units, each widened to
 *  half side h. */
static double outside(P p, const P *u, int n, double h)
{
    double best = 1e18;
    int    k;
    for (k = 0; k < n; k++)
    {
        double dx = fabs(p.x - (u[k].x + 0.5)) - h, dy = fabs(p.y - (u[k].y + 0.5)) - h;
        double d = dx > dy ? dx : dy;
        d        = d > 0 ? d : 0;
        if (d < best)
            best = d;
    }
    return best;
}

/*  How far either edge of a corner lies outside the corridor.  The corner's
 *  path is its two neighbors, its tangent points and its arc between them,
 *  a point every tenth of a radian.  Each point is moved w/2 across the
 *  path, along the normal of the chord between the points beside it. */
static double corner_out(P prev, const Corner *k, P next, const P *u, int n, double w, double h)
{
    P      path[80];
    int    np = 0, steps = (int)(fabs(k->da) / 0.1), i, side;
    double worst = 0;
    steps        = steps < 2 ? 2 : steps > 70 ? 70
                                              : steps;
    path[np++]   = prev;
    path[np++]   = k->p0;
    for (i = 1; i < steps; i++)
    {
        double a   = k->a0 + k->da * i / steps;
        P      q   = {k->cen.x + k->r * cos(a), k->cen.y + k->r * sin(a)};
        path[np++] = q;
    }
    path[np++] = k->p1;
    path[np++] = next;
    for (i = 0; i < np; i++)
    {
        P      a = path[i > 0 ? i - 1 : 0], b = path[i + 1 < np ? i + 1 : np - 1];
        double tx = b.x - a.x, ty = b.y - a.y, l = hypot(tx, ty);
        l = l > 0 ? l : 1.0;
        for (side = -1; side <= 1; side += 2)
        {
            double d = side * w / 2;
            P      e = {path[i].x - ty / l * d, path[i].y + tx / l * d};
            double o = outside(e, u, n, h);
            worst    = o > worst ? o : worst;
        }
    }
    return worst;
}

/*  The run's line as pieces of OpenDRIVE geometry, in the units' grid. */
typedef struct
{
    double s, x, y, hdg, length, curvature;
    double cend; /* the curvature at its end: a spiral's differs from its start's */
} G;

/*  A point and heading at ds along a spiral (OpenDRIVE's), L long.  It
 *  starts from x, y with heading h0, and its curvature is c0 at its
 *  start and c1 at its end.  Its heading is the integral of its
 *  curvature, and its point the integral of its heading, taken by
 *  Simpson's rule. */
static void spiral_at(double x0, double y0, double h0, double c0, double c1, double L, double ds, double *x, double *y, double *h)
{
    int    n  = (int)ceil(ds / 0.005), k;
    double dk = L > 1e-12 ? (c1 - c0) / L : 0.0, sx = 0.0, sy = 0.0, step;
    n    = n < 2 ? 2 : n + (n & 1);
    step = ds / n;
    for (k = 0; k <= n; k++)
    {
        double t = k * step, a = h0 + c0 * t + 0.5 * dk * t * t, w = (k == 0 || k == n) ? 1.0 : (k & 1) ? 4.0
                                                                                                        : 2.0;
        sx += w * cos(a), sy += w * sin(a);
    }
    *x = x0 + sx * step / 3.0, *y = y0 + sy * step / 3.0, *h = h0 + c0 * ds + 0.5 * dk * ds * ds;
}

static void eval_g(const G *g, int ng, double s, double *x, double *y, double *hdg);

static int add_line(G *g, int ng, P a, P b, double *s)
{
    double l = hypot(b.x - a.x, b.y - a.y);
    if (l < 1e-9)
        return ng;
    g[ng].s = *s, g[ng].x = a.x, g[ng].y = a.y, g[ng].hdg = atan2(b.y - a.y, b.x - a.x);
    g[ng].length = l, g[ng].curvature = 0, g[ng].cend = 0;
    *s += l;
    return ng + 1;
}

/*  Where the line through a and b meets the line through c and d.  0 where
 *  they are parallel. */
static int cross_at(P a, P b, P c, P d, P *x)
{
    double ex = b.x - a.x, ey = b.y - a.y, fx = d.x - c.x, fy = d.y - c.y, den = ex * fy - ey * fx, t;
    if (fabs(den) < 1e-9)
        return 0;
    t    = ((c.x - a.x) * fy - (c.y - a.y) * fx) / den;
    x->x = a.x + ex * t, x->y = a.y + ey * t;
    return 1;
}

/*  Whether a railway may run over a point: on a unit of its own run, or
 *  on open land.  Open land is a cell with no zone and nothing built on
 *  it but trees or rubble. */
static int open_at(double x, double y, const P *u, int nu)
{
    int32_t side = topology_side(), c = (int32_t)floor(x), r = (int32_t)floor(y), cell;
    int     k;
    for (k = 0; k < nu; k++)
        if ((int32_t)floor(u[k].x) == c && (int32_t)floor(u[k].y) == r)
            return 1;
    if (c < 0 || r < 0 || c >= side || r >= side)
        return 0;
    cell = r * side + c;
    return s_city->xzon[cell] % 16 == 0 && s_city->xbld[cell] < 0x0E;
}

/*  Whether the corner at x between the lines from a and to b, at radius
 *  R, runs over open land or its own units only.  Both its edges, w
 *  apart, are held to it. */
static int path_clear(P a, P x, P b, double R, const P *u, int nu, double w)
{
    double ux = x.x - a.x, uy = x.y - a.y, vx = b.x - x.x, vy = b.y - x.y, lu = hypot(ux, uy), lv = hypot(vx, vy);
    double cs, turn, t;
    int    q;
    if (lu < 1e-9 || lv < 1e-9)
        return 1;
    ux /= lu, uy /= lu, vx /= lv, vy /= lv;
    cs   = ux * vx + uy * vy;
    turn = acos(fmax(-1.0, fmin(1.0, cs)));
    t    = fmin(R * tan(turn / 2), fmin(lu, lv));
    for (q = 0; q <= 64; q++)
    {
        /*  Along a's line to the curve, round it as a quadratic through x,
         *  and along b's line on. */
        double f  = q / 64.0, px, py, dx, dy, e;
        P      p0 = {x.x - ux * t, x.y - uy * t}, p1 = {x.x + vx * t, x.y + vy * t};
        if (f < 1.0 / 3.0)
        {
            double g = f * 3.0;
            px = a.x + (p0.x - a.x) * g, py = a.y + (p0.y - a.y) * g, dx = ux, dy = uy;
        }
        else if (f < 2.0 / 3.0)
        {
            double g = (f - 1.0 / 3.0) * 3.0, m = 1.0 - g;
            px = m * m * p0.x + 2 * m * g * x.x + g * g * p1.x, py = m * m * p0.y + 2 * m * g * x.y + g * g * p1.y;
            dx = m * ux + g * vx, dy = m * uy + g * vy;
        }
        else
        {
            double g = (f - 2.0 / 3.0) * 3.0;
            px = p1.x + (b.x - p1.x) * g, py = p1.y + (b.y - p1.y) * g, dx = vx, dy = vy;
        }
        e = hypot(dx, dy);
        if (e < 1e-9)
            continue;
        if (!open_at(px - dy / e * w / 2, py + dx / e * w / 2, u, nu) || !open_at(px + dy / e * w / 2, py - dx / e * w / 2, u, nu))
            return 0;
    }
    return 1;
}

/*  How much of each straight each corner of a polyline may take.  That
 *  goes by how much each needs, at the radius R given.  Where both do
 *  not fit, two corners that share a straight split it by the squares of
 *  their needs.  So the larger turn keeps the larger radius.  A straight at the run's end gives at most half of its length.  A fixed straight gives no more than FIXED_ROOM, the half unit outside the unit that fixes it.  So the line stays straight across that unit.
 *  tm[i] is the most corner i takes. */
#define FIXED_ROOM 0.5

static void share(const P *pts, int np, const unsigned char *seg_fixed, double R, double *tm)
{
    int    i;
    double want[1024];
    for (i = 1; i < np - 1 && i < 1024; i++)
    {
        double ux = pts[i].x - pts[i - 1].x, uy = pts[i].y - pts[i - 1].y, vx = pts[i + 1].x - pts[i].x, vy = pts[i + 1].y - pts[i].y;
        double lu = hypot(ux, uy), lv = hypot(vx, vy), cs, turn;
        if (lu < 1e-9 || lv < 1e-9)
        {
            want[i] = 0.0;
            continue;
        }
        cs      = (ux * vx + uy * vy) / (lu * lv);
        turn    = acos(fmax(-1.0, fmin(1.0, cs)));
        want[i] = R * tan(turn / 2);
    }
    for (i = 1; i < np - 1 && i < 1024; i++)
    {
        double lu = hypot(pts[i].x - pts[i - 1].x, pts[i].y - pts[i - 1].y);
        double lv = hypot(pts[i + 1].x - pts[i].x, pts[i + 1].y - pts[i].y), in, out;
        /*  Its share of the straight before it, and of the one after. */
        if (seg_fixed[i - 1])
            in = fmin(HALF * lu, FIXED_ROOM);
        else if (i == 1)
            in = HALF * lu;
        else
            in = want[i] + want[i - 1] <= lu ? want[i] : lu * want[i] * want[i] / (want[i] * want[i] + want[i - 1] * want[i - 1] + 1e-12);
        if (seg_fixed[i])
            out = fmin(HALF * lv, FIXED_ROOM);
        else if (i == np - 2)
            out = HALF * lv;
        else
            out = want[i] + want[i + 1] <= lv ? want[i] : lv * want[i] * want[i] / (want[i] * want[i] + want[i + 1] * want[i + 1] + 1e-12);
        tm[i] = fmin(in, out);
    }
}

/*  Drop the short straights a corner of radius rmin has no room beside.
 *  A corner's arc takes the share of each straight beside it that share()
 *  gives it.  Where that is too little for rmin, the shorter straight
 *  beside it goes, where the curve that takes its place runs over open land
 *  (path_clear).  It
 *  must have a corner at each end, and neither it nor a straight beside
 *  it may be fixed.  Its two corners become one, where the straights
 *  either side of it meet.  The line leaves its units there.  Answers
 *  the count of points left. */
static int widen(P *pts, int np, unsigned char *seg_fixed, double rmin, const P *u, int nu, double w)
{
    int    changed = 1, i;
    double room[1024];
    while (changed && np > 3)
    {
        changed = 0;
        share(pts, np, seg_fixed, rmin, room);
        for (i = 1; i < np - 1 && i < 1024 && !changed; i++)
        {
            P      a = pts[i - 1], b = pts[i], c = pts[i + 1], x;
            double lu = hypot(b.x - a.x, b.y - a.y), lv = hypot(c.x - b.x, c.y - b.y), cs, turn;
            int    k, ka = i - 1, kb = i;
            if (lu < 1e-9 || lv < 1e-9)
                continue;
            cs   = ((b.x - a.x) * (c.x - b.x) + (b.y - a.y) * (c.y - b.y)) / (lu * lv);
            turn = acos(fmax(-1.0, fmin(1.0, cs)));
            if (turn < 1e-4 || rmin * tan(turn / 2) <= room[i] + 1e-9)
                continue;
            /*  The straights beside the corner that may go: inner ones, not
             *  fixed.  Dropping a straight moves the ends of the straights
             *  either side of it, so neither of those may be fixed. */
            if (ka < 1 || seg_fixed[ka] || seg_fixed[ka - 1] || seg_fixed[ka + 1])
                ka = -1;
            if (kb + 1 > np - 2 || seg_fixed[kb] || seg_fixed[kb - 1] || seg_fixed[kb + 1])
                kb = -1;
            if (ka < 0 && kb < 0)
                continue;
            k = ka < 0 ? kb : kb < 0 ? ka
                                     : (lu <= lv ? ka : kb);
            if (!cross_at(pts[k - 1], pts[k], pts[k + 1], pts[k + 2], &x) ||
                hypot(x.x - (pts[k].x + pts[k + 1].x) / 2, x.y - (pts[k].y + pts[k + 1].y) / 2) > 3.0 ||
                !path_clear(pts[k - 1], x, pts[k + 2], rmin, u, nu, w))
                continue;
            pts[k] = x;
            memmove(&pts[k + 1], &pts[k + 2], sizeof *pts * (size_t)(np - k - 2));
            memmove(&seg_fixed[k], &seg_fixed[k + 1], (size_t)(np - k - 2));
            np--;
            changed = 1;
        }
    }
    return np;
}

/*  The longest a corner's transition spiral is, in units, and the share of
 *  its turn the two spirals take at most. */
#define SPIRAL_MOST  0.8
#define SPIRAL_SHARE 0.6

/*  A corner as a railway takes it (OpenDRIVE's spiral, arc, spiral).  A
 *  spiral runs from the straight into an arc of the corner's radius,
 *  then the arc, then a spiral out again.  The curvature changes evenly
 *  along each spiral, so it never jumps.  The spirals take at least a
 *  quarter of the turn, in the room the straights beside the corner
 *  leave it (share).  The radius is the arc's, or a tenth less.  A
 *  spiral is at most SPIRAL_MOST long.  The pieces are put after g[*ng]
 *  from cur, and *cur is left at the corner's end. 0 where no spiral
 *  fits, so the corner keeps its arc. */
static int clothoid(P a, P b, P c, const Corner *k, double tcap, G *g, int *ng, double *at, P *cur)
{
    double ux = b.x - a.x, uy = b.y - a.y, vx = c.x - b.x, vy = c.y - b.y, lu = hypot(ux, uy), lv = hypot(vx, vy);
    double turn, sg, R, Ls, room;
    if (k->kind != 2 || lu < 1e-9 || lv < 1e-9)
        return 0;
    ux /= lu, uy /= lu, vx /= lv, vy /= lv;
    turn = acos(fmax(-1.0, fmin(1.0, ux * vx + uy * vy)));
    sg   = ux * vy - uy * vx > 0 ? 1.0 : -1.0;
    room = tcap >= 0.0 ? tcap : HALF * fmin(lu, lv);
    /*  The arc's radius, or a tenth less, with a spiral taking at least a
     *  quarter of the turn in the room there is.  The radius matters more
     *  than the spiral, so a corner with no room for one keeps its arc. */
    for (R = k->r; R > k->r * 0.85; R *= 0.9)
        for (Ls = fmin(SPIRAL_MOST, SPIRAL_SHARE * R * turn); Ls > 0.05 && Ls >= 0.25 * R * turn; Ls *= 0.85)
        {
            /*  The curve in the frame of its start, heading along x, and where
             *  its two straights meet: T along each. */
            double tau = Ls / (2.0 * R), x, y, h, x2, y2, h2, T, arc = R * (turn - 2.0 * tau);
            if (arc < -1e-9)
                continue;
            spiral_at(0, 0, 0, 0, sg / R, Ls, Ls, &x, &y, &h);
            x2 = x + (sin(h + sg * arc / R) - sin(h)) * R * sg, y2 = y - (cos(h + sg * arc / R) - cos(h)) * R * sg;
            if (arc < 1e-9)
                x2 = x, y2 = y;
            h2 = h + sg * arc / R;
            spiral_at(x2, y2, h2, sg / R, 0, Ls, Ls, &x, &y, &h);
            T = x - y / tan(turn) * sg;
            if (T > room)
                continue;
            {
                /*  Lay it: from the point T before the corner along the way in. */
                P      ts = {b.x - ux * T, b.y - uy * T};
                double h0 = atan2(uy, ux), px, py, ph;
                *ng       = add_line(g, *ng, *cur, ts, at);
                g[*ng].s = *at, g[*ng].x = ts.x, g[*ng].y = ts.y, g[*ng].hdg = h0, g[*ng].length = Ls;
                g[*ng].curvature = 0, g[*ng].cend = sg / R;
                spiral_at(ts.x, ts.y, h0, 0, sg / R, Ls, Ls, &px, &py, &ph);
                *at += Ls, (*ng)++;
                if (arc > 1e-9)
                {
                    g[*ng].s = *at, g[*ng].x = px, g[*ng].y = py, g[*ng].hdg = ph, g[*ng].length = arc;
                    g[*ng].curvature = g[*ng].cend = sg / R;
                    eval_g(&g[*ng], 1, *at + arc, &px, &py, &ph);
                    *at += arc, (*ng)++;
                }
                g[*ng].s = *at, g[*ng].x = px, g[*ng].y = py, g[*ng].hdg = ph, g[*ng].length = Ls;
                g[*ng].curvature = sg / R, g[*ng].cend = 0;
                spiral_at(px, py, ph, sg / R, 0, Ls, Ls, &px, &py, &ph);
                *at += Ls, (*ng)++;
                cur->x = px, cur->y = py;
                return 1;
            }
        }
    return 0;
}

/*  Fit a run of units with the tangent fit.  Each corner's radius is the
 *  largest up to R at which both edges of the road stay inside the
 *  corridor, and never under rmin (widen).  The road's width is w, and the
 *  half side of a unit's square in the corridor is h.  A unit fixed keeps
 *  its own axis (straights).  It answers the count of pieces in g, which
 *  holds room for 4 n + 4, or -1 out of memory.  Where `spiral`, each corner
 *  is a railway's (clothoid). */
static int fit(const P *u, int n, double w, double h, double R, double rmin, int spiral, const unsigned char *fixed, G *g)
{
    Straight *s;
    int       ns = straights(u, n, fixed, spiral ? 3 : 5, &s), np, i, ng = 0;
    if (ns < 0)
        return -1;
    P      *pts = (P *)malloc(sizeof *pts * (size_t)(ns + 2));
    Corner *ks  = (Corner *)malloc(sizeof *ks * (size_t)(ns + 2));
    if (!pts || !ks)
    {
        free(s), free(pts), free(ks);
        return -1;
    }
    np                = polyline(u, n, s, ns, pts);
    unsigned char *sf = (unsigned char *)malloc((size_t)np + 1);
    double        *tm = spiral ? (double *)malloc(sizeof *tm * (size_t)(np + 1)) : NULL;
    if (!sf || (spiral && !tm))
    {
        free(s), free(pts), free(ks), free(sf), free(tm);
        return -1;
    }
    for (i = 0; i + 1 < np; i++)
        sf[i] = (unsigned char)s[i].fixed;
    if (rmin > 0.0)
        np = widen(pts, np, sf, rmin, u, n, w);
    if (tm)
        share(pts, np, sf, R, tm);
    for (i = 1; i < np - 1; i++)
    {
        /*  Each corner as large as the corridor allows, and never under
         *  rmin, where the corridor gives way. */
        double r = R, floor_r = rmin > 0.05 ? rmin : 0.05, cap = tm ? tm[i] : -1.0;
        ks[i] = corner(pts[i - 1], pts[i], pts[i + 1], r, cap);
        while (ks[i].kind == 2 && corner_out(pts[i - 1], &ks[i], pts[i + 1], u, n, w, h) >= 1e-3)
        {
            r -= 0.05;
            if (r <= floor_r)
            {
                ks[i] = corner(pts[i - 1], pts[i], pts[i + 1], floor_r, cap);
                break;
            }
            ks[i] = corner(pts[i - 1], pts[i], pts[i + 1], r, cap);
        }
    }
    double at  = 0;
    P      cur = pts[0];
    for (i = 1; i < np - 1; i++)
    {
        if (ks[i].kind == 0)
            continue;
        if (ks[i].kind == 1)
        {
            ng  = add_line(g, ng, cur, pts[i], &at);
            cur = pts[i];
            continue;
        }
        if (spiral && clothoid(pts[i - 1], pts[i], pts[i + 1], &ks[i], tm[i], g, &ng, &at, &cur))
            continue;
        ng         = add_line(g, ng, cur, ks[i].p0, &at);
        double hdg = ks[i].a0 + (ks[i].da > 0 ? M_PI / 2 : -M_PI / 2);
        g[ng].s = at, g[ng].x = ks[i].p0.x, g[ng].y = ks[i].p0.y, g[ng].hdg = hdg;
        g[ng].length    = ks[i].r * fabs(ks[i].da);
        g[ng].curvature = g[ng].cend = (ks[i].da > 0 ? 1 : -1) / ks[i].r;
        at += g[ng].length;
        ng++;
        cur = ks[i].p1;
    }
    ng = add_line(g, ng, cur, pts[np - 1], &at);
    free(s), free(pts), free(ks), free(sf), free(tm);
    return ng;
}

/*  A point and heading of a line of pieces at s along it. */
static void eval_g(const G *g, int ng, double s, double *x, double *y, double *hdg)
{
    int k = 0;
    while (k + 1 < ng && s >= g[k + 1].s)
        k++;
    double ds = s - g[k].s, c = g[k].curvature;
    if (g[k].cend != c)
    {
        spiral_at(g[k].x, g[k].y, g[k].hdg, c, g[k].cend, g[k].length, ds, x, y, hdg);
        return;
    }
    if (fabs(c) < 1e-12)
    {
        *x = g[k].x + ds * cos(g[k].hdg), *y = g[k].y + ds * sin(g[k].hdg), *hdg = g[k].hdg;
        return;
    }
    double a = g[k].hdg + ds * c;
    *x       = g[k].x + (sin(a) - sin(g[k].hdg)) / c;
    *y       = g[k].y - (cos(a) - cos(g[k].hdg)) / c;
    *hdg     = a;
}

/* ---- the height: one solver ---------------------------------------------- */

/*  Solve n equations of a band five wide, by elimination with no pivoting, one
 *  pass down and one up.  A[i][d] is row i's coefficient on h[i + d - 2].  A
 *  and b are overwritten. */
static void band_solve(int n, double (*A)[5], double *b, double *h)
{
    int i, k, d;
    for (i = 0; i < n; i++)
        for (k = 1; k <= 2 && i + k < n; k++)
        {
            if (A[i + k][2 - k] == 0.0)
                continue;
            double m = A[i + k][2 - k] / A[i][2];
            for (d = 0; d <= 2; d++)
                A[i + k][2 - k + d] -= m * A[i][2 + d];
            b[i + k] -= m * b[i];
        }
    for (i = n - 1; i >= 0; i--)
    {
        double v = b[i];
        for (d = 1; d <= 2 && i + d < n; d++)
            v -= A[i][2 + d] * h[i + d];
        h[i] = v / A[i][2];
    }
}

/*  The stationary condition of the profile, as row i of the band: its
 *  coefficients on h[i - 2] to h[i + 2].  A row near a level end takes its
 *  missing neighbor as its mirror inside, so the profile comes level there.  A
 *  row near a free end bends less. */
static void row(int n, int i, const int level[2], double pull, double c[5])
{
    int d;
    for (d = 0; d < 5; d++)
        c[d] = 0;
    if ((i >= 2 && i <= n - 3) || (i < 2 && level[0]) || (i > n - 3 && level[1]))
    {
        static const double W[5] = {1, -4, 6, -4, 1};
        for (d = 0; d < 5; d++)
        {
            int j = i + d - 2;
            j     = j < 0 ? 1 : j > n - 1 ? n - 2
                                          : j;
            c[j - i + 2] += W[d] + (d == 2 ? pull : 0);
        }
    }
    else
        c[1] = -1, c[2] = 2 + pull, c[3] = -1;
}

/*  The least bending (the squared second differences) plus pull times the
 *  squared distance from the target.  Each height stays in [lo, hi], and the
 *  ends are at h0 and h1.  An active set: the band is solved with every height
 *  held at a bound fixed there.  A height that crosses a bound joins the set,
 *  and one whose own row pulls it back inside leaves it, until nothing
 *  changes.  -1 out of memory. */
static int profile(int n, const double *lo, const double *hi, double h0, double h1, const int level[2], const double *target, double pull, double *h)
{
    double (*A)[5]    = (double (*)[5])malloc(sizeof *A * (size_t)n);
    double      *b    = (double *)malloc(sizeof *b * (size_t)n);
    signed char *held = (signed char *)calloc((size_t)n, 1); /* -1 at lo, 1 at hi */
    int          it, i, d;
    if (!A || !b || !held)
    {
        free(A), free(b), free(held);
        return -1;
    }
    for (it = 0; it < 4 * n + 4; it++)
    {
        int changed = 0;
        for (i = 0; i < n; i++)
        {
            for (d = 0; d < 5; d++)
                A[i][d] = 0;
            if (i == 0 || i == n - 1 || held[i])
            {
                A[i][2] = 1;
                b[i]    = i == 0 ? h0 : i == n - 1 ? h1
                                    : held[i] < 0  ? lo[i]
                                                   : hi[i];
            }
            else
            {
                row(n, i, level, pull, A[i]);
                b[i] = pull * target[i];
            }
        }
        band_solve(n, A, b, h);
        for (i = 1; i < n - 1; i++)
            if (!held[i] && (h[i] < lo[i] - 1e-12 || h[i] > hi[i] + 1e-12))
                held[i] = h[i] < lo[i] ? -1 : 1, changed = 1;
        if (changed)
            continue;
        for (i = 1; i < n - 1; i++)
            if (held[i])
            {
                double c[5], v = pull * target[i];
                row(n, i, level, pull, c);
                for (d = 0; d < 5; d++)
                    if (d != 2 && i + d - 2 >= 0 && i + d - 2 < n)
                        v -= c[d] * h[i + d - 2];
                v /= c[2];
                if ((held[i] < 0 && v > lo[i] + 1e-12) || (held[i] > 0 && v < hi[i] - 1e-12))
                    held[i] = 0, changed = 1;
            }
        if (!changed)
            break;
    }
    free(A), free(b), free(held);
    return 0;
}

/* ---- a run ----------------------------------------------------------------- */

/*  One unit of a run: its first cell, and the road it is of with its index in
 *  that road's own order.  The road is -1 for a unit of a junction, or for a
 *  unit off the map at the run's ends. */
typedef struct
{
    int32_t col, row;
    int     road, k, rev; /* rev: 1 where the road runs against the run */
} RUnit;

typedef struct
{
    RUnit  *u;
    int     n, cap;
    int     first, last; /* the run's own units: u[first] to u[last] */
    int     net, size;
    int     junction[2]; /* the junction at each end, else -1 */
    int     map_edge[2]; /* 1 where the run leaves the map at that end */
    int     ring;
    int32_t beyond[2]; /* at a region's gore, the region's unit across it, else -1 */
} Run;

static int run_push(Run *r, int32_t cell, int road, int k, int rev)
{
    if (arc_grow((void **)&r->u, &r->cap, r->n + 1, sizeof *r->u) != 0)
        return -1;
    int32_t side    = topology_side();
    r->u[r->n].col  = cell % side;
    r->u[r->n].row  = cell / side;
    r->u[r->n].road = road;
    r->u[r->n].k    = k;
    r->u[r->n].rev  = rev;
    r->n++;
    return 0;
}

/*  Whether a network road travels against the run, and the link at the side
 *  the run enters it by and the side it leaves by. */
static NLink entry_of(const NRoad *nr, int rev)
{
    return rev ? nr->succ : nr->pred;
}
static NLink exit_of(const NRoad *nr, int rev)
{
    return rev ? nr->pred : nr->succ;
}

static int *s_junction_of_end; /* per topology end, its junction, else -1 */

/*  The unit a junction's end lies at for an end of a road, else -1. */
static int32_t end_unit(const TopoRoad *t, int c)
{
    return t->at[c];
}

/*  The road of the same family as road `from` on the far side of
 *  junction j.  Here j is a level meet: one family's way across
 *  another's, which goes straight on through it.  The value at rev is 1
 *  where that road leaves the meet by its end. -1 where j is no meet or
 *  no road goes on. */
static int across_meet(int from, int j, int *rev)
{
    int i, net = network_road(from)->net;
    if (topology_end(network_junction(j)->end)->kind != END_MEET)
        return -1;
    for (i = 0; i < network_roads(); i++)
    {
        const NRoad *r = network_road(i);
        if (i == from || r->junction >= 0 || r->topo < 0 || r->net != net || topology_road(r->topo)->nunit == 0)
            continue;
        if (r->pred.kind == NLINK_JUNCTION && r->pred.index == j)
            return *rev = 0, i;
        if (r->succ.kind == NLINK_JUNCTION && r->succ.index == j)
            return *rev = 1, i;
    }
    return -1;
}

/*  The road a run goes on into from road `road`, run the way `rev` says.
 *  It goes by the link l at its far end: across a seam, or straight on
 *  through a level meet.  The value at meet is the meet's cell, or -1,
 *  and the value at mj its junction. -1 where the run ends there. */
static int run_next(int road, NLink l, int *rev, int32_t *meet, int *mj)
{
    int next;
    *meet = -1, *mj = -1;
    if (l.kind == NLINK_ROAD)
        return *rev = l.contact == 1, l.index;
    /*  A band's road goes straight on through a gore's direct junction
     *  into the band's road beyond, as across a seam.  A region's bundle
     *  and strands are no roads of a run: they lie on no cells. */
    if (l.kind == NLINK_JUNCTION && network_junction(l.index)->type == NJUNCTION_DIRECT && network_road(road)->net == RNET_BAND)
    {
        const NJunction *j = network_junction(l.index);
        int              k;
        for (k = j->conn0; k < j->conn0 + j->nconn; k++)
        {
            const NConnection *c = network_connection(k);
            const NRoad       *q;
            if (c->incoming != road || c->outgoing < 0)
                continue;
            q = network_road(c->outgoing);
            if (q->net != RNET_BAND || q->topo < 0)
                continue;
            *rev = !(q->pred.kind == NLINK_JUNCTION && q->pred.index == l.index);
            return c->outgoing;
        }
        return -1;
    }
    if (l.kind != NLINK_JUNCTION || (next = across_meet(road, l.index, rev)) < 0)
        return -1;
    *meet = topology_cell(topology_end(network_junction(l.index)->end)->cell0);
    *mj   = l.index;
    return next;
}

/*  The unit across gore `g` from unit `near`.  The gore is a band's of
 *  two units, and the far one is a region's.  Else -1. */
static int32_t gore_far(int g, int32_t near)
{
    const TopoEnd *e = topology_end(g);
    int32_t        a, b;
    if (!e || e->kind != END_GORE || e->net != RNET_BAND || e->ncell != 2)
        return -1;
    a = topology_cell(e->cell0), b = topology_cell(e->cell0 + 1);
    if (near != a && near != b)
        return -1;
    return network_cell_region(near == a ? b : a) >= 0 ? (near == a ? b : a) : -1;
}

/*  Gather the run that holds network road `start`: back along its links across
 *  seams and through level meets to the run's first road, then forward.  Every road of the run is
 *  marked in `done`.  -1 out of memory. */
static int gather(int start, Run *r, unsigned char *done)
{
    int first = start, rev = 0, k, i;
    r->n = 0, r->ring = 0;
    r->junction[0] = r->junction[1] = -1;
    r->map_edge[0] = r->map_edge[1] = 0;
    r->beyond[0] = r->beyond[1] = -1;
    for (k = 0; k < network_roads(); k++)
    {
        int32_t meet;
        int     mj, prev_rev, prev = run_next(first, entry_of(network_road(first), rev), &prev_rev, &meet, &mj);
        if (prev < 0)
            break;
        /*  The road before runs against the run where the way forward into
         *  it would. */
        prev_rev = !prev_rev;
        if (prev == start)
        {
            r->ring = 1;
            break;
        }
        first = prev, rev = prev_rev;
    }
    const NRoad    *nr = network_road(first);
    const TopoRoad *t  = topology_road(nr->topo);
    r->net             = nr->net;
    r->size            = NET[nr->net].size;
    /*  The unit before the run's first: its junction's, or a unit off the map. */
    {
        int     c  = rev ? 1 : 0;
        int32_t ju = r->ring ? -1 : end_unit(t, c);
        if (!r->ring && ju >= 0)
        {
            if (run_push(r, ju, -1, -1, 0) != 0)
                return -1;
            r->junction[0] = s_junction_of_end[t->end[c]];
        }
        else if (!r->ring && entry_of(nr, rev).kind == NLINK_JUNCTION)
        {
            /*  A gore's direct junction: a point.  A region's gore has the
             *  region's unit beyond it, which the deck goes on over. */
            r->junction[0] = entry_of(nr, rev).index;
            r->beyond[0]   = gore_far(t->end[c], topology_cell(t->unit0 + (rev ? t->nunit - 1 : 0)));
        }
        else if (!r->ring && topology_end(t->end[c])->kind == END_MAP_EDGE)
            r->map_edge[0] = 1;
    }
    r->first = r->n;
    int road = first;
    for (;;)
    {
        const NRoad    *q  = network_road(road);
        const TopoRoad *tq = topology_road(q->topo);
        done[road]         = 1;
        for (i = 0; i < tq->nunit; i++)
        {
            k = rev ? tq->nunit - 1 - i : i;
            if (run_push(r, topology_cell(tq->unit0 + k), road, k, rev) != 0)
                return -1;
        }
        NLink   l = exit_of(q, rev);
        int32_t meet;
        int     mj, next_rev, next = run_next(road, l, &next_rev, &meet, &mj);
        if (next < 0 || (r->ring && next == first) || done[next])
        {
            int     c  = rev ? 0 : 1;
            int32_t ju = r->ring ? -1 : end_unit(tq, c);
            r->last    = r->n - 1;
            if (!r->ring && ju >= 0)
            {
                if (run_push(r, ju, -1, -1, 0) != 0)
                    return -1;
                r->junction[1] = s_junction_of_end[tq->end[c]];
            }
            else if (!r->ring && l.kind == NLINK_JUNCTION)
            {
                r->junction[1] = l.index; /* a gore's direct junction: a point */
                r->beyond[1]   = gore_far(tq->end[c], topology_cell(tq->unit0 + (rev ? 0 : tq->nunit - 1)));
            }
            else if (!r->ring && topology_end(tq->end[c])->kind == END_MAP_EDGE)
                r->map_edge[1] = 1;
            break;
        }
        /*  A meet's cell is a unit of the run that is no road's.  It holds
         *  the meet's junction in place of a unit's index. */
        if (meet >= 0 && run_push(r, meet, -1, mj, 0) != 0)
            return -1;
        road = next, rev = next_rev;
    }
    return 0;
}

/*  A ring opens at the unit boundary in the middle of its longest straight run
 *  of steps, so its two ends meet on a straight.  The units are turned to
 *  start there, and a unit of the ring beyond each end gives the fit its
 *  tangent. */
static int open_ring(Run *r)
{
    int n = r->n, best = -1, at = 0, k;
    for (k = 0; k < n; k++)
    {
        RUnit a = r->u[(k + n - 1) % n], b = r->u[k], c = r->u[(k + 1) % n];
        int   s = (b.col - a.col == c.col - b.col) && (b.row - a.row == c.row - b.row);
        if (!s)
            continue;
        RUnit pa = r->u[(k + n - 2) % n];
        if ((a.col - pa.col == b.col - a.col) && (a.row - pa.row == b.row - a.row))
            continue; /* not the start of a straight run */
        int len = 0;
        while (len < n)
        {
            RUnit x = r->u[(k + len + n - 1) % n], y = r->u[(k + len) % n], z = r->u[(k + len + 1) % n];
            if (!((y.col - x.col == z.col - y.col) && (y.row - x.row == z.row - y.row)))
                break;
            len++;
        }
        if (len > best)
            best = len, at = (k + len / 2 + 1) % n;
    }
    RUnit *u = (RUnit *)malloc(sizeof *u * (size_t)(n + 2));
    if (!u)
        return -1;
    for (k = 0; k < n; k++)
        u[k + 1] = r->u[(at + k) % n];
    u[0]      = r->u[(at + n - 1) % n];
    u[n + 1]  = r->u[at];
    u[0].road = u[n + 1].road = -1;
    free(r->u);
    r->u     = u;
    r->cap   = n + 2;
    r->n     = n + 2;
    r->first = 1, r->last = n;
    return 0;
}

/* ---- shaping a run --------------------------------------------------------- */

/*  The units at index i and i + 1 of a run.  Past either end of the run's
 *  units, a unit is the mirror of the one inside. */
static RUnit unit_at(const Run *r, int i)
{
    if (i >= 0 && i < r->n)
        return r->u[i];
    RUnit e = r->u[i < 0 ? 0 : r->n - 1], f = r->n > 1 ? r->u[i < 0 ? 1 : r->n - 2] : e;
    RUnit o = {2 * e.col - f.col, 2 * e.row - f.row, -1, -1, 0};
    if (r->n == 1)
        o.col += i < 0 ? -r->size : r->size;
    return o;
}

static void neighbors(const Run *r, int i, RUnit *a, RUnit *b)
{
    *a = unit_at(r, i);
    *b = unit_at(r, i + 1);
}

#define SAMPLE     0.01 /* the spacing of the samples of a run's line, in tiles */
#define MOUTH_STEP 0.1  /* the spacing a mouth is found at, in tiles */

static int inside_units(double x, double y, const RUnit *u, int n, int size)
{
    int k;
    for (k = 0; k < n; k++)
        if (x >= u[k].col && x <= u[k].col + size && y >= u[k].row && y <= u[k].row + size)
            return 1;
    return 0;
}

/*  The `s` along the run's line where it leaves the units of a junction,
 *  walking in from one end.  `units` are the junction end's units. */
static double mouth(const G *g, int ng, double L, const RUnit *ju, int nju, int from_end, double step)
{
    double s;
    for (s = 0; s <= L; s += step)
    {
        double x, y, hdg, at = from_end ? L - s : s;
        eval_g(g, ng, at, &x, &y, &hdg);
        if (!inside_units(x, y, ju, nju, 1))
        {
            /*  The exact point where the line leaves the units, between the
             *  last sample inside and the first outside. */
            double in = from_end ? at + step : at - step, out = at;
            int    k;
            if (s == 0)
                return at;
            for (k = 0; k < 40; k++)
            {
                double mid = (in + out) / 2;
                eval_g(g, ng, mid, &x, &y, &hdg);
                if (inside_units(x, y, ju, nju, 1))
                    in = mid;
                else
                    out = mid;
            }
            return out;
        }
    }
    return from_end ? 0 : L;
}

/*  The units of a junction's topology end, as run units. */
static int junction_units(int junction, RUnit **out)
{
    const TopoEnd *e    = topology_end(network_junction(junction)->end);
    RUnit         *u    = (RUnit *)malloc(sizeof *u * (size_t)(e->ncell ? e->ncell : 1));
    int32_t        side = topology_side();
    int            k;
    if (!u)
        return -1;
    for (k = 0; k < e->ncell; k++)
    {
        int32_t c = topology_cell(e->cell0 + k);
        u[k].col = c % side, u[k].row = c / side, u[k].road = -1, u[k].k = -1, u[k].rev = 0;
    }
    *out = u;
    return e->ncell;
}

/*  One arm's mouth at a junction, kept for the junction's boundary. */
typedef struct
{
    int   junction;
    float x[2], y[2]; /* the two edges of the road at its mouth */
} Mouth;
static Mouth *s_mouths;
static int    s_nmouth, s_capmouth;

static int add_mouth(int junction, const G *g, int ng, double s, double w, double past)
{
    double x, y, hdg;
    if (arc_grow((void **)&s_mouths, &s_capmouth, s_nmouth + 1, sizeof *s_mouths) != 0)
        return -1;
    eval_g(g, ng, s, &x, &y, &hdg);
    x += cos(hdg) * past, y += sin(hdg) * past;
    Mouth *m    = &s_mouths[s_nmouth++];
    m->junction = junction;
    m->x[0] = (float)(x - sin(hdg) * w / 2), m->y[0] = (float)(y + cos(hdg) * w / 2);
    m->x[1] = (float)(x + sin(hdg) * w / 2), m->y[1] = (float)(y - cos(hdg) * w / 2);
    return 0;
}

/*  A road's width where it reaches junction `j` (layer 2): at its start
 *  where its start reaches it, at its end where its end does.  The width of
 *  its family where it has no lanes. */
static double width_at_junction(int road, int j, int net)
{
    const NRoad *nr = network_road(road);
    if (nr->nsec <= 0)
        return network_width(net, 0);
    return network_road_width(road, nr->pred.kind == NLINK_JUNCTION && nr->pred.index == j ? 0.0f : nr->length);
}

/*  A height profile over the run, as samples (s, height): s in tiles along the
 *  run's own units from the start of its first. */
typedef struct
{
    double *s, *h;
    int     n;
} Prof;

/*  A street's solve over its raw heights at s.  It stays within STREET_HOLD
 *  of them, never above the highest within REACH or below the lowest, and it
 *  comes level into its two ends.  A height where `held` is set stands at its
 *  raw height.  -1 out of memory. */
static int street_solve(int m, const double *s, const double *raw, const unsigned char *held, double *h)
{
    double *lo = (double *)malloc(sizeof *lo * (size_t)m * 2);
    int     i, k;
    if (!lo)
        return -1;
    double *hi = lo + m;
    for (i = 0; i < m; i++)
    {
        double mn = raw[i], mx = raw[i];
        for (k = 0; k < m; k++)
            if (fabs(s[k] - s[i]) <= REACH)
                mn = raw[k] < mn ? raw[k] : mn, mx = raw[k] > mx ? raw[k] : mx;
        lo[i] = raw[i] - STREET_HOLD > mn ? raw[i] - STREET_HOLD : mn;
        hi[i] = raw[i] + STREET_HOLD < mx ? raw[i] + STREET_HOLD : mx;
        if (held && held[i])
            lo[i] = hi[i] = raw[i];
    }
    static const int level[2] = {1, 1};
    int              rc       = profile(m, lo, hi, raw[0], raw[m - 1], level, raw, STREET_PULL, h);
    free(lo);
    return rc;
}

static int    ramp_end(const Run *r, int i);
static int    ramp_start(int road, int junction, double *x, double *y, double *hdg);
static double ramp_run(int road, int junction, double x, double y, double hdg);

/*  The height a ramp's road `road` meets at its end at junction `j`.  It is
 *  the height of a highway road it links with straight there, at that
 *  road's end.  NAN where it links with none, as where a gore's lane runs across
 *  the ramp's cell and climbs itself.  A ramp's road is shaped after every
 *  highway road, so the deck is filed. */
static double deck_at(int road, int j)
{
    const NRoad *r     = network_road(road);
    int          end   = r->pred.kind == NLINK_JUNCTION && r->pred.index == j ? 0 : 1, sec, l;
    int          first = -1, at = 0, at_end = 0;
    /*  Of every lane of the road at that end, the link filed first that
     *  reaches such a highway road. */
    for (sec = r->sec0; sec < r->sec0 + r->nsec; sec++)
        for (l = 0; l < network_section(sec)->nlane; l++)
        {
            int lane = network_section(sec)->lane0 + l, it, other, oe;
            for (it = network_end_link(lane, end, -1, &other, &oe, NULL); it >= 0; it = network_end_link(lane, end, it, &other, &oe, NULL))
            {
                int          o = network_lane(other)->road;
                const NRoad *q = network_road(o);
                if (q->net == RNET_BAND && q->topo >= 0 && s_road[o].ngeom && (first < 0 || it / 2 < first))
                    first = it / 2, at = o, at_end = oe;
            }
        }
    return first < 0 ? NAN : shape_height(at, at_end ? s_road[at].length : 0.0f);
}

/*  How far over the land a ramp's road stands `d` tiles along its line
 *  from its gore.  There the deck stands `rise` over it there
 *  (plans/network.md, layer 3, "A ramp's height").  Over a run of `run`
 *  tiles beside the band it comes down in a straight line, and it lies
 *  on the land after.  With no run it comes down across its cell, `span`
 *  tiles of line. */
static double ramp_rise(double d, double run, double span, double rise)
{
    double over = run > 1e-6 ? run : span;
    return over > 1e-6 && d < over ? rise * (1.0 - d / over) : 0.0;
}

/*  How far a ramp's line runs along the band at its end at junction `j`. */
static double ramp_run_at(int road, int j)
{
    double x, y, h;
    return ramp_start(road, j, &x, &y, &h) == 0 ? ramp_run(road, j, x, y, h) : 0.0;
}

/*  A street's raw heights: each cell's own slope under its center line, from
 *  its entry edge through its middle to its exit edge, STREET_STEP apart.  An
 *  end cell's outer edge mirrors its inner one.  A ramp's road comes down
 *  from the deck at its gore to the land (ramp_rise).  Its cell, the unit at
 *  its band end, is the line `B` maps there: the run beside the band, the
 *  turn-out and the cell.  Where a gore's lane runs across the cell, that
 *  lane climbs, and the road keeps to the land.  Then the street's solve. */
static int street_profile(const Run *r, Prof *p, const double *B)
{
    int     n = r->last - r->first + 1, per = (int)(1.0 / STREET_STEP + 0.5), m = n * per + 1, i, t;
    int     ramp0 = ramp_end(r, 0) >= 0, ramp1 = ramp_end(r, 1) >= 0;
    double  rise0 = 0.0, rise1 = 0.0, run0 = 0.0, run1 = 0.0, span0 = B[1] - B[0], span1 = B[n] - B[n - 1];
    double *raw = (double *)malloc(sizeof *raw * (size_t)m);
    /*  A ramp's own unit keeps its raw heights: its straight drop from the
     *  deck and its turn-out on the land (ramp_rise). */
    unsigned char *held = (unsigned char *)calloc((size_t)m, 1);
    p->s                = (double *)malloc(sizeof *p->s * (size_t)m);
    p->h                = (double *)malloc(sizeof *p->h * (size_t)m);
    p->n                = m;
    if (!raw || !held || !p->s || !p->h)
    {
        free(raw), free(held);
        return -1;
    }
    for (i = 0; i < n; i++)
    {
        RUnit        own = {0, 0, 0, 0, 0}, pa, pb;
        const RUnit *u   = &r->u[r->first + i];
        own              = *u;
        pa               = i > 0 ? u[-1] : n > 1 ? (RUnit){2 * own.col - u[1].col, 2 * own.row - u[1].row, -1, -1, 0}
                                                 : unit_at(r, r->first - 1);
        pb               = i < n - 1 ? u[1] : n > 1 ? (RUnit){2 * own.col - u[-1].col, 2 * own.row - u[-1].row, -1, -1, 0}
                                                    : unit_at(r, r->last + 1);
        const RUnit *a = &pa, *b = &pb;
        double       cx = u->col + 0.5, cy = u->row + 0.5;
        double       ex = cx + (a->col - u->col) * 0.5, ey = cy + (a->row - u->row) * 0.5;
        double       xx = cx + (b->col - u->col) * 0.5, xy = cy + (b->row - u->row) * 0.5;
        /*  How far the deck stands over the land at the ramp's edge with the
         *  highway.  That edge is the first unit's edge toward the run's
         *  start, or the last unit's toward its end. */
        if (i == 0 && ramp0)
        {
            double top = deck_at(u->road, ramp_end(r, 0)), land = plane_at(u->col, u->row, ex, ey);
            rise0 = isnan(top) ? 0.0 : top - land;
            run0  = ramp_run_at(u->road, ramp_end(r, 0));
        }
        if (i == n - 1 && ramp1)
        {
            double top = deck_at(u->road, ramp_end(r, 1)), land = plane_at(u->col, u->row, xx, xy);
            rise1 = isnan(top) ? 0.0 : top - land;
            run1  = ramp_run_at(u->road, ramp_end(r, 1));
        }
        for (t = 0; t < per; t++)
        {
            double f = (double)t / per, x, y;
            if (f < 0.5)
                x = ex + (cx - ex) * f * 2, y = ey + (cy - ey) * f * 2;
            else
                x = cx + (xx - cx) * (f - 0.5) * 2, y = cy + (xy - cy) * (f - 0.5) * 2;
            p->s[i * per + t] = i + f;
            raw[i * per + t]  = plane_at(u->col, u->row, x, y) + (i == 0 ? ramp_rise(f * span0, run0, span0, rise0) : 0.0) +
                                (i == n - 1 ? ramp_rise((1.0 - f) * span1, run1, span1, rise1) : 0.0);
            held[i * per + t] = (unsigned char)((i == 0 && ramp0 && rise0 != 0.0) || (i == n - 1 && ramp1 && rise1 != 0.0));
        }
        if (i == n - 1)
        {
            p->s[m - 1] = n;
            raw[m - 1]  = plane_at(u->col, u->row, xx, xy) + ramp_rise(0.0, run1, span1, rise1);
            held[m - 1] = (unsigned char)(ramp1 && rise1 != 0.0);
        }
    }
    int rc = street_solve(m, p->s, raw, held, p->h);
    free(raw), free(held);
    return rc;
}

/*  The deck's solve over its floor fy at the unit centers fx, n of them.
 *  An end with a pin set (not NAN) stands at that height, or at its floor
 *  where that is higher, and is never lifted.  With p NULL it solves
 *  nothing and answers each end's floor in ends.  -1 out of memory. */
static int deck_solve(int n, const double *fx, const double *fy, Prof *p, const double pin[2], double ends[2])
{
    int    i, k, it;
    double a = fx[0], b = fx[n - 1];
    int    m = n > 1 ? (int)((b - a) / DECK_STEP + 0.5) + 1 : 1;
    if (m < 3)
        m = 3;
    double *buf = (double *)malloc(sizeof *buf * (size_t)m * 5);
    if (!buf)
        return -1;
    double *xs = buf, *lo = buf + m, *hi = buf + 2 * m, *aim = buf + 3 * m, *unit = buf + 4 * m;
    double  half = n > 1 ? (fx[1] - fx[0]) / 2 : 1.0, g = tan(MAX_GRADE * M_PI / 180) / LEVEL_TILES;
    /*  The floor over the whole of each unit, then raised toward each step
     *  up no steeper than the grade. */
    for (k = 0; k < m; k++)
    {
        xs[k]   = a + (b - a) * k / (m - 1);
        unit[k] = -1e18;
        for (i = 0; i < n; i++)
            if (fabs(fx[i] - xs[k]) <= half + 1e-9 && fy[i] > unit[k])
                unit[k] = fy[i];
    }
    for (k = 0; k < m; k++)
    {
        lo[k] = -1e18;
        for (i = 0; i < m; i++)
        {
            double v = unit[i] - g * fabs(xs[i] - xs[k]);
            lo[k]    = v > lo[k] ? v : lo[k];
        }
    }
    if (ends)
        ends[0] = lo[0], ends[1] = lo[m - 1];
    if (!p)
    {
        free(buf);
        return 0;
    }
    int pinned[2] = {pin && !isnan(pin[0]), pin && !isnan(pin[1])};
    if (pinned[0] && pin[0] > lo[0])
        lo[0] = pin[0];
    if (pinned[1] && pin[1] > lo[m - 1])
        lo[m - 1] = pin[1];
    for (k = 0; k < m; k++)
    {
        double mx = -1e18;
        for (i = 0; i < m; i++)
            if (fabs(xs[i] - xs[k]) <= FLAT && lo[i] > mx)
                mx = lo[i];
        aim[k] = mx < lo[k] + EXTRA ? mx : lo[k] + EXTRA;
    }
    p->s = (double *)malloc(sizeof *p->s * (size_t)m);
    p->h = (double *)malloc(sizeof *p->h * (size_t)m);
    if (!p->s || !p->h)
    {
        free(buf);
        return -1;
    }
    memcpy(p->s, xs, sizeof *xs * (size_t)m);
    p->n = m;
    /*  The deck itself never climbs steeper than the grade.  Where it does,
     *  the lower of the two samples is raised to meet it, and the deck is
     *  solved again.  A deck is only ever lifted, never lowered. */
    static const int level[2] = {0, 0};
    for (it = 0; it < 4 * m; it++)
    {
        int raised = 0;
        for (k = 0; k < m; k++)
            hi[k] = aim[k] + SMOOTH > lo[k] ? aim[k] + SMOOTH : lo[k];
        if (profile(m, lo, hi, lo[0], lo[m - 1], level, aim, DECK_PULL, p->h) != 0)
        {
            free(buf);
            return -1;
        }
        for (k = 0; k + 1 < m; k++)
        {
            double rise = p->h[k + 1] - p->h[k], room = g * (xs[k + 1] - xs[k]);
            int    low  = rise > 0 ? k : k + 1;
            double need = (p->h[k] > p->h[k + 1] ? p->h[k] : p->h[k + 1]) - room;
            if ((low == 0 && pinned[0]) || (low == m - 1 && pinned[1]))
                continue;
            if (fabs(rise) > room + 1e-4 && need > lo[low] + 1e-6)
                lo[low] = need, raised = 1;
        }
        if (!raised)
            break;
    }
    free(buf);
    return 0;
}

/*  A highway's deck.  It stands CLEARANCE above the highest ground under
 *  each unit, over the whole of the unit.  It never climbs steeper than the
 *  angle MAX_GRADE gives.  Where the floor steps up, the floor it keeps to
 *  rises toward the step at that grade.  Wherever the deck itself climbs
 *  faster, it is lifted.  It holds flats: it aims at the highest floor
 *  within FLAT, never more than EXTRA above the floor there.  Its ceiling
 *  is SMOOTH above that aim.  It runs from the center of the unit at the run's first end
 *  to the center of the one at its last.  Where the run ends at a junction,
 *  that is the junction's unit, so each road lies over its floor to its end.
 *  The samples' s is in tiles. */
static double *s_jh; /* per junction, the height of a highway junction, else NAN */

/*  With p NULL, it solves nothing and raises the height of each junction at
 *  the run's ends to the floor the run needs there. */
static int deck_profile(const Run *r, Prof *p)
{
    /*  The junction's unit at each end, where the run holds one.  A
     *  gore's direct junction holds none, but a region's gore has the
     *  region's unit across it.  The deck goes on over it to its middle,
     *  as over a junction's unit. */
    int     a = r->first - (r->first > 0), b = r->last + (r->last + 1 < r->n);
    int     x0 = a == r->first && r->beyond[0] >= 0, x1 = b == r->last && r->beyond[1] >= 0;
    int     n = b - a + 1 + x0 + x1, size = r->size, i, k, dc, dr, e;
    int32_t side = topology_side();
    double *fx   = (double *)malloc(sizeof *fx * (size_t)n * 2);
    if (!fx)
        return -1;
    double *fy = fx + n;
    for (i = 0; i < n; i++)
    {
        int     at  = a + i - x0;
        int32_t col = at < a ? r->beyond[0] % side : at > b ? r->beyond[1] % side
                                                            : r->u[at].col;
        int32_t row = at < a ? r->beyond[0] / side : at > b ? r->beyond[1] / side
                                                            : r->u[at].row;
        double  top = -1e18, h[4];
        for (dc = 0; dc < size; dc++)
            for (dr = 0; dr < size; dr++)
            {
                corners(col + dc, row + dr, h);
                for (k = 0; k < 4; k++)
                    top = h[k] > top ? h[k] : top;
            }
        fx[i] = size * (at - r->first + 0.5);
        fy[i] = top + CLEARANCE;
    }
    double ends[2], pin[2] = {NAN, NAN};
    /*  A region is no one level.  A run that ends at a region's gore
     *  keeps its own height there.  The region's roads go between the
     *  heights of its arms (plans/interchanges.md, "The levels"). */
    for (e = 0; e < 2; e++)
        if (p && r->junction[e] >= 0 && r->beyond[e] < 0 && !isnan(s_jh[r->junction[e]]))
            pin[e] = s_jh[r->junction[e]];
    int rc = deck_solve(n, fx, fy, p, pin, ends);
    if (!p)
        for (e = 0; e < 2; e++)
            if (r->junction[e] >= 0 && r->beyond[e] < 0 && (isnan(s_jh[r->junction[e]]) || ends[e] > s_jh[r->junction[e]]))
                s_jh[r->junction[e]] = ends[e];
    free(fx);
    return rc;
}

static double prof_at(const Prof *p, double s)
{
    int k;
    if (s <= p->s[0])
        return p->h[0];
    for (k = 0; k + 1 < p->n; k++)
        if (s <= p->s[k + 1])
        {
            double d = p->s[k + 1] - p->s[k];
            return d > 0 ? p->h[k] + (p->h[k + 1] - p->h[k]) * (s - p->s[k]) / d : p->h[k];
        }
    return p->h[p->n - 1];
}

/*  The road pieces of one road, gathered as intervals of the run's line. */
typedef struct
{
    int    road, rev;
    double a, b; /* the interval of the run's `s` it covers, a < b */
} Span;

/*  The curvature of a run's piece at s along the run. */
static double curv_at(const G *g, double s)
{
    return g->length > 1e-12 ? g->curvature + (g->cend - g->curvature) * (s - g->s) / g->length : g->curvature;
}

/*  File the part [c0, c1] of a run's piece as a road's piece starting at
 *  `at`, walked the way dir says, scaled to tiles. */
static int file_piece(const G *gp, double c0, double c1, int dir, double at, double scale)
{
    double x, y, hdg, len = c1 - c0, k0 = curv_at(gp, dir > 0 ? c0 : c1), k1 = curv_at(gp, dir > 0 ? c1 : c0);
    SGeom *o;
    eval_g(gp, 1, dir > 0 ? c0 : c1, &x, &y, &hdg); /* from the piece itself, at its own end too */
    if (dir < 0)
        hdg += M_PI, k0 = -k0, k1 = -k1;
    if (arc_grow((void **)&s_geom, &s_capgeom, s_ngeom + 1, sizeof *s_geom) != 0)
        return -1;
    o = &s_geom[s_ngeom++];
    memset(o, 0, sizeof *o);
    o->s = (float)at, o->x = (float)(x * scale), o->y = (float)(y * scale), o->hdg = (float)remainder(hdg, 2 * M_PI);
    o->length = (float)(len * scale), o->curvature = (float)(k0 / scale), o->cend = (float)(k1 / scale);
    o->spiral = gp->cend != gp->curvature;
    return 0;
}

/*  How far out along its arm an interchange's gore lies at most, in tiles
 *  (plans/interchanges.md, "The shape"). */
#define IX_GORE_OUT 3.5

static int file_road(const Run *r, int road, const G *g, int ng, const double *B, const Prof *p, double scale)
{
    const NRoad    *nr = network_road(road);
    const TopoRoad *t  = topology_road(nr->topo);
    int             n  = t->nunit, k, i, rev;
    double         *lb = (double *)malloc(sizeof *lb * (size_t)(n + 1) * 2); /* each local boundary's run s */
    if (!lb)
        return -1;
    double *ls = lb + n + 1; /* each local boundary's run s along the units, in tiles */
    for (i = r->first; i <= r->last; i++)
    {
        if (r->u[i].road != road)
            continue;
        k                   = r->u[i].k;
        int ri              = i - r->first;
        rev                 = r->u[i].rev;
        lb[rev ? k + 1 : k] = B[ri], lb[rev ? k : k + 1] = B[ri + 1];
        ls[rev ? k + 1 : k] = ri * r->size, ls[rev ? k : k + 1] = (ri + 1) * r->size;
    }
    /*  The road's line: each of its units' intervals of the run's line in its
     *  own order, joined where they meet. */
    SRoad *sr = &s_road[road];
    sr->geom0 = s_ngeom, sr->elev0 = s_nelev, sr->map0 = s_nmap;
    double at = 0;
    for (k = 0; k < n; k++)
    {
        double a = lb[k], b = lb[k + 1];
        int    dir = b >= a ? 1 : -1;
        double lo = dir > 0 ? a : b, hi = dir > 0 ? b : a;
        /*  Each run piece that overlaps [lo, hi], walked in the road's
         *  direction. */
        for (i = 0; i < ng; i++)
        {
            int    gi = dir > 0 ? i : ng - 1 - i;
            double g0 = g[gi].s, g1 = g[gi].s + g[gi].length;
            double c0 = g0 > lo ? g0 : lo, c1 = g1 < hi ? g1 : hi;
            if (c1 - c0 < 1e-9)
                continue;
            double x, y, hdg, start = dir > 0 ? c0 : c1, len = c1 - c0;
            eval_g(&g[gi], 1, start, &x, &y, &hdg); /* from the piece itself, at its own end too */
            double curv = g[gi].curvature;
            if (dir < 0)
                hdg += M_PI, curv = -curv;
            SGeom *last = s_ngeom > sr->geom0 ? &s_geom[s_ngeom - 1] : NULL;
            /*  A line that goes straight on from a line is one piece. */
            if (last && !last->spiral && last->curvature == 0 && fabs(curv) < 1e-12 && g[gi].cend == g[gi].curvature &&
                fabs(remainder(last->hdg - hdg, 2 * M_PI)) < 1e-5 && fabs(last->s + last->length - at) < 1e-5)
            {
                last->length += (float)(len * scale);
                at += len * scale;
                continue;
            }
            if (file_piece(&g[gi], c0, c1, dir, at, scale) != 0)
            {
                free(lb);
                return -1;
            }
            at += len * scale;
        }
        /*  The map: each unit boundary's s along the road's line. */
        if (arc_grow((void **)&s_map, &s_capmap, s_nmap + 2, sizeof *s_map) != 0)
        {
            free(lb);
            return -1;
        }
        if (k == 0)
            s_map[s_nmap++] = 0;
        s_map[s_nmap++] = (float)at;
    }
    sr->ngeom  = s_ngeom - sr->geom0;
    sr->nmap   = s_nmap - sr->map0;
    sr->length = (float)at;
    /*  The height: the run's profile at each sample inside each unit,
     *  mapped onto the road's line unit by unit. */
    for (k = 0; k < n; k++)
    {
        double sa = ls[k], sb = ls[k + 1], la = s_map[sr->map0 + k], lb2 = s_map[sr->map0 + k + 1];
        int    j, np = 0;
        double pts_s[512], pts_h[512];
        double lo = sa < sb ? sa : sb, hi = sa < sb ? sb : sa;
        /*  The profile's own samples inside the unit, between its two ends, in
         *  the road's direction. */
        pts_s[np] = sa, pts_h[np++] = prof_at(p, sa);
        for (j = 0; j < p->n && np < 510; j++)
        {
            int jj = sa < sb ? j : p->n - 1 - j;
            if (p->s[jj] > lo + 1e-9 && p->s[jj] < hi - 1e-9)
                pts_s[np] = p->s[jj], pts_h[np++] = p->h[jj];
        }
        pts_s[np] = sb, pts_h[np++] = prof_at(p, sb);
        for (j = 0; j + 1 < np; j++)
        {
            double f0 = (pts_s[j] - sa) / (sb - sa), f1 = (pts_s[j + 1] - sa) / (sb - sa);
            double s0 = la + (lb2 - la) * f0, s1 = la + (lb2 - la) * f1, h0 = pts_h[j], h1 = pts_h[j + 1];
            if (s1 - s0 < 1e-9 || s1 <= 0.0 || s0 >= sr->length)
                continue;
            if (s0 < 0.0)
                h0 += (h1 - h0) * -s0 / (s1 - s0), s0 = 0.0;
            if (arc_grow((void **)&s_elev, &s_capelev, s_nelev + 1, sizeof *s_elev) != 0)
            {
                free(lb);
                return -1;
            }
            SElev *e = &s_elev[s_nelev++];
            e->s     = (float)s0;
            e->a     = (float)h0;
            e->b     = (float)((h1 - h0) / (s1 - s0));
            e->c = e->d = 0.0f;
        }
    }
    sr->nelev = s_nelev - sr->elev0;
    free(lb);
    return 0;
}

/*  Whether a road carries a lane a car drives. */
static int drives(int road)
{
    const NRoad *nr = network_road(road);
    int          k, l;
    for (k = 0; k < nr->nsec; k++)
    {
        const NSection *sec = network_section(nr->sec0 + k);
        for (l = 0; l < sec->nlane; l++)
            if (network_lane(sec->lane0 + l)->type != NLANE_SIDEWALK)
                return 1;
    }
    return 0;
}

/*  File the run's line and height over a level meet's unit, the run's
 *  unit ri.  They go onto each connecting road of the meet that carries
 *  the run's traffic across it.  That is from the road before the unit
 *  to the road after, or back.  The road goes straight on through the
 *  meet, so its line there is the run's own. */
static int file_span(const Run *r, int ri, const G *g, int ng, const double *B, const Prof *p, double scale)
{
    int i = r->first + ri, j = r->u[i].k, before = r->u[i - 1].road, after = r->u[i + 1].road, c, k;
    for (c = 0; c < network_roads(); c++)
    {
        const NRoad *nr = network_road(c);
        int          rev;
        SRoad       *sr = &s_road[c];
        if (nr->junction != j || nr->net != r->net || !drives(c) || nr->pred.kind != NLINK_ROAD || nr->succ.kind != NLINK_ROAD)
            continue;
        if (nr->pred.index == before && nr->succ.index == after)
            rev = 0;
        else if (nr->pred.index == after && nr->succ.index == before)
            rev = 1;
        else
            continue;
        double lo = B[ri], hi = B[ri + 1], at = 0;
        sr->geom0 = s_ngeom, sr->elev0 = s_nelev, sr->map0 = s_nmap;
        for (k = 0; k < ng; k++)
        {
            int    gi = rev ? ng - 1 - k : k;
            double g0 = g[gi].s, g1 = g[gi].s + g[gi].length;
            double c0 = g0 > lo ? g0 : lo, c1 = g1 < hi ? g1 : hi;
            if (c1 - c0 < 1e-9)
                continue;
            double len = c1 - c0;
            if (file_piece(&g[gi], c0, c1, rev ? -1 : 1, at, scale) != 0)
                return -1;
            at += len * scale;
        }
        sr->ngeom = s_ngeom - sr->geom0, sr->length = (float)at, sr->nmap = 0;
        /*  Its height: the run's profile across the unit, end to end. */
        {
            double sa = (rev ? ri + 1 : ri) * r->size, sb = (rev ? ri : ri + 1) * r->size;
            double ha = prof_at(p, sa), hb = prof_at(p, sb);
            if (arc_grow((void **)&s_elev, &s_capelev, s_nelev + 1, sizeof *s_elev) != 0)
                return -1;
            SElev *e = &s_elev[s_nelev++];
            e->s = 0.0f, e->a = (float)ha, e->b = at > 1e-9 ? (float)((hb - ha) / at) : 0.0f, e->c = e->d = 0.0f;
            sr->nelev = 1;
        }
    }
    return 0;
}

/*  Shape one run: fit its line, find its ends, map its unit boundaries onto
 *  the line, solve its height, and file each of its roads. */
/* ---- a ramp's line ---------------------------------------------------------- */

static int ramp_start(int road, int junction, double *x, double *y, double *hdg);

static int ramp_transition(double x0, double y0, double h0, double x1, double y1, double h1, G *out, double *R);
static int biarc(double x0, double y0, double h0, double x1, double y1, double h1, double at, G *out);

/*  A RAMP'S RUN beside the band (plans/network.md, layer 3, "A ramp's
 *  line is the highway's outer lane, carried on").  From the gore at x,
 *  y, heading `hdg`, it runs to a tile before the ramp's own cell.
 *  There it lies in the strip the band's outermost lane leaves, tangent
 *  to the band at both ends.  At the gore it lies on the lane it links
 *  with.  The band's road from the gore toward the ramp's unit holds
 *  that unit, and its line there sets the run's end.  The run is the
 *  transition curve between its two ends, so it follows the band where
 *  the band bends.  It is a biarc where the transition curve has none.
 *  Where no such road holds the unit, it is a straight line as far along
 *  as the unit lies.  It puts its pieces in `out`, room for 8.  Answers
 *  how many, 0 where the gore is that border. */
static int ramp_run_line(int road, int junction, double x, double y, double hdg, G *out)
{
    int i;
    for (i = 0; i < network_ramps(); i++)
    {
        const NRamp   *rp   = network_ramp(i);
        int32_t        side = topology_side();
        const TopoEnd *ge;
        double         gx, gy, bx, by, along;
        int            br, bc, k, n;
        if (rp->road != road || rp->band_junction != junction || !rp->built || rp->gore < 0 || rp->region >= 0)
            continue;
        /*  A unit is two tiles, its center a tile in from its first cell. */
        ge = topology_end(rp->gore);
        if (ge->ncell == 1)
        {
            /*  A gore at the map's edge: the border its one unit leaves
             *  the map by. */
            gx = topology_cell(ge->cell0) % side + 1.0 + rcity_edge_dc(ge->edge);
            gy = topology_cell(ge->cell0) / side + 1.0 + rcity_edge_dr(ge->edge);
        }
        else
        {
            gx = 0.5 * (topology_cell(ge->cell0) % side + topology_cell(ge->cell0 + 1) % side) + 1.0;
            gy = 0.5 * (topology_cell(ge->cell0) / side + topology_cell(ge->cell0 + 1) / side) + 1.0;
        }
        /*  The run ends a tile before the ramp's own cell, wherever in its
         *  band unit the cell lies.  So the turn-out has its tile
         *  (appendix-road-spec, 7.3, "The turn-out"). */
        bx = rp->cell % side + 0.5, by = rp->cell / side + 0.5;
        along = fabs((gx - bx) * cos(hdg) + (gy - by) * sin(hdg)) - 1.5;
        if (along < 1e-6)
            return 0;
        br = rp->on ? rp->in_road : rp->out_road, bc = rp->on ? rp->in_contact : rp->out_contact;
        if (network_road(br)->topo >= 0 && s_road[br].ngeom)
        {
            const TopoRoad *t = topology_road(network_road(br)->topo);
            for (k = 0; k < t->nunit && k + 1 < s_road[br].nmap; k++)
                if (topology_cell(t->unit0 + k) == rp->cell_band)
                {
                    float  px, py, ph, qx, qy, qh;
                    double lat, ex, ey, eh, near = bc ? s_map[s_road[br].map0 + k + 1] : s_map[s_road[br].map0 + k];
                    shape_at(br, bc ? s_road[br].length : 0.0f, &px, &py, &ph);
                    shape_at(br, (float)near, &qx, &qy, &qh);
                    /*  At the unit it lies in the strip the outermost
                     *  lane leaves, on the gore's side of the line.
                     *  That is inside the deck's full edge by its
                     *  shoulder and half the ramp's lane, a highway lane
                     *  wide. */
                    lat = (x - px) * -sin(ph) + (y - py) * cos(ph);
                    lat = (lat < 0.0 ? -1.0 : 1.0) * (0.5 * network_width(RNET_BAND, 0) - network_type_width(RNET_BAND, NLANE_SHOULDER) -
                                                      0.5 * network_lane_width(network_section(network_road(road)->sec0)->lane0, 0.0f));
                    ex = qx - sin(qh) * lat, ey = qy + cos(qh) * lat, eh = cos(hdg - qh) < 0.0 ? qh + M_PI : qh;
                    if ((n = ramp_transition(x, y, hdg, ex, ey, eh, out, NULL)) > 0 || (n = biarc(x, y, hdg, ex, ey, eh, 0.0, out)) > 0)
                        return n;
                }
        }
        {
            G line = {0.0, x, y, hdg, along, 0.0, 0.0};
            out[0] = line;
            return 1;
        }
    }
    return 0;
}

/*  How far a ramp's line runs along the band from its gore to its
 *  turn-out, in tiles: the length of its run (ramp_run_line). */
static double ramp_run(int road, int junction, double x, double y, double hdg)
{
    G   run[8];
    int n = ramp_run_line(road, junction, x, y, hdg, run);
    return n ? run[n - 1].s + run[n - 1].length : 0.0;
}

/*  How far along a ramp road's own line from its band end a ramp's line
 *  may rejoin it, in tiles.  Where no one arc reaches it, the ramp's line
 *  rejoins it RAMP_JOIN along. */
#define RAMP_REACH 4.0
#define RAMP_JOIN  1.5
/*  How much of the ramp's own line, in tiles, runs straight into its street
 *  at the far end (appendix-road-spec, 7.3, "The turn-out"). */
#define RAMP_STRAIGHT 0.5
/*  The least radius of a ramp's arc, in tiles. */
#define RAMP_MIN_RADIUS 0.5

/*  The part [a, b] of line `g` as pieces from `at` on, into `out`.  It
 *  answers their count. */
static int g_cut(const G *g, int ng, double a, double b, double at, G *out)
{
    int i, n = 0;
    for (i = 0; i < ng; i++)
    {
        double c0 = fmax(g[i].s, a), c1 = fmin(g[i].s + g[i].length, b);
        if (c1 - c0 < 1e-9)
            continue;
        out[n] = g[i];
        eval_g(&g[i], 1, c0, &out[n].x, &out[n].y, &out[n].hdg);
        out[n].curvature = curv_at(&g[i], c0), out[n].cend = curv_at(&g[i], c1);
        out[n].s = at, out[n].length = c1 - c0, at += c1 - c0;
        n++;
    }
    return n;
}

/*  Line `g` walked from its end to its start, its s from `at` on. */
static void g_reverse(const G *g, int ng, double at, G *out)
{
    int i;
    for (i = 0; i < ng; i++)
    {
        const G *q = &g[ng - 1 - i];
        G        o = *q;
        eval_g(q, 1, q->s + q->length, &o.x, &o.y, &o.hdg);
        o.hdg += M_PI, o.curvature = -q->cend, o.cend = -q->curvature, o.s = at, at += q->length;
        out[i] = o;
    }
}

/*  An arc from a point and heading through another point, or a line where
 *  the two lie on one heading.  It answers the heading at its end. */
static double arc_to(double x0, double y0, double h0, double x1, double y1, double at, G *out)
{
    double len = hypot(x1 - x0, y1 - y0), phi = remainder(atan2(y1 - y0, x1 - x0) - h0, 2.0 * M_PI);
    out->s = at, out->x = x0, out->y = y0, out->hdg = h0;
    if (fabs(phi) < 1e-9)
        out->length = len, out->curvature = out->cend = 0.0;
    else
        out->length = len * phi / sin(phi), out->curvature = out->cend = 2.0 * sin(phi) / len;
    return h0 + 2.0 * phi;
}

/*  A BIARC: the two arcs of equal tangent length from one place to
 *  another, tangent to both and to each other.  It is one arc where one
 *  arc joins them.  It answers the count of pieces, or 0 where the two
 *  places allow none. */
static int biarc(double x0, double y0, double h0, double x1, double y1, double h1, double at, G *out)
{
    double t1x = cos(h0), t1y = sin(h0), t2x = cos(h1), t2y = sin(h1), dx = x1 - x0, dy = y1 - y0;
    double vd = (t1x + t2x) * dx + (t1y + t2y) * dy, dd = dx * dx + dy * dy, den = 2.0 * (1.0 - (t1x * t2x + t1y * t2y));
    double al, mx, my, hm;
    if (fabs(den) < 1e-9)
    {
        double t1d = t1x * dx + t1y * dy;
        if (t1d < 1e-9)
            return 0;
        al = dd / (4.0 * t1d);
    }
    else
        al = (-vd + sqrt(vd * vd + den * dd)) / den;
    if (al <= 0.0)
        return 0;
    mx = 0.5 * (x0 + al * t1x + x1 - al * t2x), my = 0.5 * (y0 + al * t1y + y1 - al * t2y);
    hm = arc_to(x0, y0, h0, mx, my, at, &out[0]);
    arc_to(mx, my, hm, x1, y1, at + out[0].length, &out[1]);
    return 2;
}

/*  A RAMP'S LINE (plans/network.md, layer 3, "A ramp's line is the
 *  highway's outer lane, carried on").  Line `g`, fitted through the
 *  ramp road's units, begins at its band end where `end` is 0, else ends
 *  there.  The ramp's line runs from its gore along the band
 *  (ramp_run_line), and turns out from there to `g` by the transition
 *  curve.  It joins `g` no nearer the street than RAMP_STRAIGHT tiles,
 *  so it meets its street head-on.  It answers the count of pieces in
 *  `out`, which holds room for ng + 10, or 0 where the ramp's line is
 *  `g` itself. */

static int ramp_line(const G *g, int ng, int end, int road, int junction, G *out)
{
    double nx, ny, nh, L = g[ng - 1].s + g[ng - 1].length, s, sj = -1.0, prev = 0.0, run = 0.0;
    double jx, jy, jh;
    G      head[16], tail[64];
    int    nh2, nt, k, nr;
    if (ramp_start(road, junction, &nx, &ny, &nh) != 0 || ng + 10 > 64)
        return 0;
    /*  From the gore the line runs along the band to the near border of the
     *  ramp's unit, and turns out from there. */
    nr = ramp_run_line(road, junction, nx, ny, nh, head);
    if (nr)
    {
        run = head[nr - 1].s + head[nr - 1].length;
        eval_g(head, nr, run, &nx, &ny, &nh);
    }
    /*  Where one arc from the node reaches the line, heading along it away
     *  from the band end. */
    for (s = 0.0; s <= fmin(RAMP_REACH, fmax(L - RAMP_STRAIGHT, 0.1)) + 1e-9; s += 0.02)
    {
        double x, y, h, f, phi;
        eval_g(g, ng, end ? L - s : s, &x, &y, &h);
        if (end)
            h += M_PI;
        phi = remainder(atan2(y - ny, x - nx) - nh, 2.0 * M_PI);
        f   = remainder(2.0 * phi - (h - nh), 2.0 * M_PI);
        if (s > 0.0 && f * prev <= 0.0 && fabs(f) < 1.0 && fabs(phi) > 1e-6 &&
            hypot(x - nx, y - ny) / (2.0 * fabs(sin(phi))) >= RAMP_MIN_RADIUS)
        {
            sj = s;
            break;
        }
        prev = f;
    }
    if (sj < 0.0)
        sj = fmin(RAMP_JOIN, fmax(L - RAMP_STRAIGHT, 0.1));
    /*  The transition curve where one reaches the line: the join that
     *  gives it the widest arc. */
    {
        double best = 0.0, R;
        G      t[8];
        for (s = 0.1; s <= fmin(RAMP_REACH, fmax(L - RAMP_STRAIGHT, 0.1)) + 1e-9; s += 0.05)
        {
            eval_g(g, ng, end ? L - s : s, &jx, &jy, &jh);
            if (end)
                jh += M_PI;
            if (ramp_transition(nx, ny, nh, jx, jy, jh, t, &R) && R > best)
                best = R, sj = s;
        }
    }
    eval_g(g, ng, end ? L - sj : sj, &jx, &jy, &jh);
    if (end)
        jh += M_PI;
    nh2 = ramp_transition(nx, ny, nh, jx, jy, jh, head + nr, NULL);
    if (!nh2)
        nh2 = biarc(nx, ny, nh, jx, jy, jh, 0.0, head + nr);
    if (!nh2)
        return 0;
    /*  The turn-out follows the run. */
    for (k = nr; k < nr + nh2; k++)
        head[k].s += run;
    nh2 += nr;
    if (!end)
    {
        for (k = 0; k < nh2; k++)
            out[k] = head[k];
        return nh2 + g_cut(g, ng, sj, L, head[nh2 - 1].s + head[nh2 - 1].length, out + nh2);
    }
    nt = g_cut(g, ng, 0.0, L - sj, 0.0, tail);
    for (k = 0; k < nt; k++)
        out[k] = tail[k];
    g_reverse(head, nh2, L - sj, out + nt);
    return nt + nh2;
}

/*  The band junction at end `i` of a run of the road network, where the
 *  road there is a ramp's road, else -1. */
static int ramp_end(const Run *r, int i)
{
    int j = r->junction[i], road = r->u[i ? r->last : r->first].road, k;
    if (r->net != RNET_ROAD || r->size != 1 || j < 0 || topology_end(network_junction(j)->end)->net != RNET_BAND)
        return -1;
    for (k = 0; k < network_ramps(); k++)
        if (network_ramp(k)->road == road && network_ramp(k)->band_junction == j && network_ramp(k)->built)
            return j;
    return -1;
}

/*  Whether a run is a ramp's road that joins the highway at a gore. */
static int gore_ramp_run(const Run *r)
{
    int i, k;
    for (i = r->first; i <= r->last; i++)
        for (k = 0; k < network_ramps(); k++)
        {
            const NRamp *rp = network_ramp(k);
            if (rp->road == r->u[i].road && rp->built && rp->gore >= 0 && rp->region < 0)
                return 1;
        }
    return 0;
}

static int shape_run(Run *r)
{
    int size = r->size, n = r->n, i, ng, rc = -1;
    n              = r->n;
    P      *u      = (P *)malloc(sizeof *u * (size_t)n);
    G      *g      = (G *)malloc(sizeof *g * (size_t)(4 * n + 4));
    double *B      = (double *)malloc(sizeof *B * (size_t)(r->last - r->first + 2));
    Prof    p      = {NULL, NULL, 0};
    RUnit  *ju[2]  = {NULL, NULL};
    int     nju[2] = {0, 0};
    if (!u || !g || !B)
        goto out;
    for (i = 0; i < n; i++)
        u[i].x = r->u[i].col / size, u[i].y = r->u[i].row / size;
    {
        /*  The units that fix the line.  They are a junction's unit at
         *  each end that has one, and every level meet inside the run, a
         *  unit of no road. */
        unsigned char *fixed = (unsigned char *)calloc((size_t)n, 1);
        if (!fixed)
            goto out;
        /*  A junction's unit stands at an end of the run where the junction
         *  has one: a gore's direct junction is a point. */
        int pt0 = r->first > 0, pt1 = r->last < n - 1;
        int f0 = ramp_end(r, 0) >= 0 && pt0, f1 = n - (ramp_end(r, 1) >= 0 && pt1);
        for (i = 1; i + 1 < n; i++)
            fixed[i] = r->u[i].road < 0;
        /*  A ramp's road is fitted from its own cell, and its line from its
         *  node or its gore is put on after. */
        fixed[0] = pt0 && !f0, fixed[n - 1] = pt1 && f1 == n;
        if (f1 - f0 < 2)
        {
            free(fixed);
            rc = 0;
            goto out;
        }
        ng = fit(u + f0, f1 - f0, (double)network_width(r->net, 0) / size, 0.5 + MARGIN / size, NET[r->net].radius / size, r->net == RNET_RAIL ? RAIL_MIN_RADIUS / size : 0.0, r->net == RNET_RAIL, fixed + f0, g);
        free(fixed);
    }
    if (ng <= 0)
        goto out;
    for (i = 0; i < 2; i++)
        if (ramp_end(r, i) >= 0)
        {
            G  *rg = (G *)malloc(sizeof *rg * (size_t)(4 * n + 8));
            int nr;
            if (!rg)
                goto out;
            nr = ramp_line(g, ng, i, r->u[i ? r->last : r->first].road, r->junction[i], rg);
            if (nr > 0)
            {
                G *t = g;
                g = rg, rg = t, ng = nr;
            }
            free(rg);
        }
    double L = g[ng - 1].s + g[ng - 1].length;
    /*  The run's two ends, in the units' grid. */
    double S0 = 0, S1 = L;
    for (i = 0; i < 2; i++)
    {
        if (r->junction[i] >= 0 && network_junction(r->junction[i])->type != NJUNCTION_DIRECT)
        {
            nju[i] = junction_units(r->junction[i], &ju[i]);
            if (nju[i] < 0)
                goto out;
            int k;
            for (k = 0; k < nju[i]; k++)
                ju[i][k].col /= size, ju[i][k].row /= size;
            /*  A band junction a ramp's road reaches: each of its units is
             *  four of the road's cells. */
            if (ramp_end(r, i) >= 0)
            {
                RUnit *q = (RUnit *)realloc(ju[i], sizeof *q * (size_t)(4 * nju[i] + 1));
                if (!q)
                    goto out;
                ju[i] = q;
                for (k = nju[i] - 1; k >= 0; k--)
                {
                    RUnit b  = q[k];
                    q[4 * k] = b, q[4 * k + 1] = b, q[4 * k + 2] = b, q[4 * k + 3] = b;
                    q[4 * k + 1].col++, q[4 * k + 2].row++, q[4 * k + 3].col++, q[4 * k + 3].row++;
                }
                nju[i] *= 4;
            }
            double m = mouth(g, ng, L, ju[i], nju[i], i, MOUTH_STEP / size);
            if (i == 0)
                S0 = m;
            else
                S1 = m;
        }
    }
    /*  Each unit boundary of the run's own units maps to the nearest point of
     *  the line, in order. */
    {
        int    nb   = r->last - r->first + 2, b;
        double prev = 0;
        for (b = 0; b < nb; b++)
        {
            RUnit a, c;
            neighbors(r, r->first + b - 1, &a, &c);
            double px = ((a.col + c.col) / (double)size + 1) / 2, py = ((a.row + c.row) / (double)size + 1) / 2;
            double best = 1e18, bs = prev, s;
            for (s = prev; s <= L && s <= prev + 4.0; s += SAMPLE / size)
            {
                double x, y, hdg;
                eval_g(g, ng, s, &x, &y, &hdg);
                double d = hypot(x - px, y - py);
                if (d < best)
                    best = d, bs = s;
            }
            B[b] = bs;
            prev = bs;
        }
        if (r->junction[0] >= 0)
            B[0] = S0;
        if (r->junction[1] >= 0)
            B[nb - 1] = S1;
    }
    /*  The junctions' mouths, for their boundaries.  A gore's direct
     *  junction is a point and has none. */
    for (i = 0; i < 2; i++)
        if (r->junction[i] >= 0 && network_junction(r->junction[i])->type != NJUNCTION_DIRECT)
        {
            int k;
            G  *gs = (G *)malloc(sizeof *gs * (size_t)ng);
            if (!gs)
                goto out;
            for (k = 0; k < ng; k++)
            {
                gs[k] = g[k];
                gs[k].s *= size, gs[k].x *= size, gs[k].y *= size, gs[k].length *= size, gs[k].curvature /= size;
                gs[k].cend /= size;
            }
            double wj  = width_at_junction(r->u[i ? r->last : r->first].road, r->junction[i], r->net);
            int    bad = add_mouth(r->junction[i], gs, ng, (i ? S1 : S0) * size, wj, 0.0);
            /*  A dead end's surface is the road's own width across its
             *  whole tile.  It runs from its mouth past the end of its line,
             *  which is the tile's middle, to the tile's far edge. */
            if (!bad && topology_end(network_junction(r->junction[i])->end)->kind == END_DEAD)
                bad = add_mouth(r->junction[i], gs, ng, i ? L * size : 0, wj, (i ? 0.5 : -0.5) * size);
            free(gs);
            if (bad)
                goto out;
        }
    if ((r->net == RNET_BAND ? deck_profile(r, &p) : street_profile(r, &p, B)) != 0)
        goto out;
    /*  Each road of the run, filed once. */
    for (i = r->first; i <= r->last; i++)
    {
        int road = r->u[i].road, k, seen = road < 0;
        for (k = r->first; k < i; k++)
            if (r->u[k].road == road)
                seen = 1;
        if (!seen && file_road(r, road, g, ng, B, &p, size) != 0)
            goto out;
        /*  A level meet inside the run carries the road straight on. */
        if (road < 0 && i > r->first && i < r->last && file_span(r, i - r->first, g, ng, B, &p, size) != 0)
            goto out;
    }
    rc = 0;
out:
    free(u), free(g), free(B), free(ju[0]), free(ju[1]), free(p.s), free(p.h);
    return rc;
}

/* ---- the boundary -------------------------------------------------------- */

static double cross3(const float *o, const float *a, const float *b)
{
    return ((double)a[0] - o[0]) * ((double)b[1] - o[1]) - ((double)a[1] - o[1]) * ((double)b[0] - o[0]);
}

static int cmp_pt(const void *a, const void *b)
{
    const float *p = (const float *)a, *q = (const float *)b;
    return p[0] < q[0] ? -1 : p[0] > q[0] ? 1
                          : p[1] < q[1]   ? -1
                                          : p[1] > q[1];
}

/* ---- a street junction: its arms, its corners and its outline ------------- */

/*  One arm of a street junction.  It holds where the arm reaches the
 *  junction, the way out along it and its half width.  It holds its road and
 *  contact, -1 for an edge the tile's piece joins with no shaped road there.
 *  It holds its angle round the junction. */
typedef struct
{
    double px, py, dx, dy, half, ang;
    int    road, contact;
} Arm;

static int cmp_arm(const void *a, const void *b)
{
    double p = ((const Arm *)a)->ang, q = ((const Arm *)b)->ang;
    return p < q ? -1 : p > q;
}

/*  The spacing of the points a corner is drawn through, in tiles, and the
 *  most of them one corner has. */
#define CORNER_STEP 0.05
#define CORNER_MOST 32
/*  A street corner's curb return radius, in tiles. */
#define CURB_RADIUS 0.2

/*  The point and heading at `s` along a run of pieces of line, each a
 *  line, an arc or a paramPoly3. */
static void geoms_at(const SGeom *g, int n, double s, double *x, double *y, double *hdg)
{
    int k = 0;
    while (k + 1 < n && s >= g[k + 1].s)
        k++;
    g += k;
    double ds = s - g->s, c = g->curvature;
    if (g->spiral)
    {
        spiral_at(g->x, g->y, g->hdg, c, g->cend, g->length, ds, x, y, hdg);
        return;
    }
    if (g->poly)
    {
        double p = g->length > 0.0f ? ds / g->length : 0.0, ch = cos(g->hdg), sh = sin(g->hdg);
        double u  = g->u[0] + p * (g->u[1] + p * (g->u[2] + p * g->u[3]));
        double v  = g->v[0] + p * (g->v[1] + p * (g->v[2] + p * g->v[3]));
        double du = g->u[1] + p * (2.0 * g->u[2] + p * 3.0 * g->u[3]), dv = g->v[1] + p * (2.0 * g->v[2] + p * 3.0 * g->v[3]);
        *x = g->x + u * ch - v * sh, *y = g->y + u * sh + v * ch, *hdg = g->hdg + atan2(dv, du);
        return;
    }
    if (fabs(c) < 1e-9)
    {
        *x = g->x + ds * cos(g->hdg), *y = g->y + ds * sin(g->hdg), *hdg = g->hdg;
        return;
    }
    double a = g->hdg + ds * c;
    *x = g->x + (sin(a) - sin(g->hdg)) / c, *y = g->y - (cos(a) - cos(g->hdg)) / c, *hdg = a;
}

/*  A corner's line: up to three pieces, from s nought. */
typedef struct
{
    SGeom  g[3];
    int    n;
    double len;
} Curb;

static void curb_put(Curb *k, double x, double y, double hdg, double len, double curv)
{
    SGeom *g;
    if (len < 1e-6)
        return;
    g = &k->g[k->n++];
    memset(g, 0, sizeof *g);
    g->s = (float)k->len, g->x = (float)x, g->y = (float)y, g->hdg = (float)hdg;
    g->length = (float)len, g->curvature = (float)curv;
    k->len += len;
}

/*  The corner from arm a to arm b: from a's edge facing b at a's end to
 *  b's edge facing a at b's end.  It leaves a heading into the junction
 *  and reaches b heading out along it.  The two edges, carried into the
 *  junction, can meet ahead of both ends.  There it is a curb return:
 *  along a's edge, round an arc of radius R tangent to both edges, and
 *  out along b's edge.  R is cut to what the edges leave room for.
 *  Else, where the arms run on from one another, it is a smooth step
 *  from one edge to the other: a paramPoly3.  Nonzero where the two ends
 *  meet. */
static int corner_curve(const Arm *a, const Arm *b, double R, Curb *k)
{
    double e1x = -a->dx, e1y = -a->dy;                                 /* a's way into the junction */
    double ax = a->px - a->dy * a->half, ay = a->py + a->dx * a->half; /* a's edge facing b */
    double bx = b->px + b->dy * b->half, by = b->py - b->dx * b->half; /* b's edge facing a */
    double wx = bx - ax, wy = by - ay, w = hypot(wx, wy);
    double den = -(e1x * -b->dy - -b->dx * e1y), ta, tb;
    k->n = 0, k->len = 0.0;
    if (w < 1e-6)
        return -1;
    if (fabs(den) > 1e-6)
    {
        ta = (wx * b->dy - b->dx * wy) / den;
        tb = (e1x * wy - wx * e1y) / den;
        if (ta > 1e-4 && tb > 1e-4)
        {
            double turn = acos(fmax(-1.0, fmin(1.0, e1x * b->dx + e1y * b->dy)));
            double side = e1x * b->dy - e1y * b->dx > 0.0 ? 1.0 : -1.0, t, r;
            double cx = ax + e1x * ta, cy = ay + e1y * ta; /* where the edges meet */
            if (turn > 1e-3)
            {
                r = fmin(R, fmin(ta, tb) / tan(turn / 2.0));
                t = r * tan(turn / 2.0);
                curb_put(k, ax, ay, atan2(e1y, e1x), ta - t, 0.0);
                curb_put(k, cx - e1x * t, cy - e1y * t, atan2(e1y, e1x), r * turn, side / r);
                curb_put(k, cx + b->dx * t, cy + b->dy * t, atan2(b->dy, b->dx), tb - t, 0.0);
                return 0;
            }
        }
    }
    {
        /*  The smooth step: a cubic with its handles a third of the way. */
        double c[4][2], h = atan2(e1y, e1x), ch = cos(h), sh = sin(h), l[4][2], len = 0.0, px = ax, py = ay;
        int    q;
        SGeom *g;
        c[0][0] = ax, c[0][1] = ay, c[1][0] = ax + e1x * w / 3.0, c[1][1] = ay + e1y * w / 3.0;
        c[2][0] = bx - b->dx * w / 3.0, c[2][1] = by - b->dy * w / 3.0, c[3][0] = bx, c[3][1] = by;
        for (q = 0; q < 4; q++)
        {
            double dx = c[q][0] - ax, dy = c[q][1] - ay;
            l[q][0] = dx * ch + dy * sh, l[q][1] = -dx * sh + dy * ch;
        }
        for (q = 1; q <= 64; q++)
        {
            double p = q / 64.0, m = 1.0 - p, x, y;
            x = m * m * m * c[0][0] + 3 * m * m * p * c[1][0] + 3 * m * p * p * c[2][0] + p * p * p * c[3][0];
            y = m * m * m * c[0][1] + 3 * m * m * p * c[1][1] + 3 * m * p * p * c[2][1] + p * p * p * c[3][1];
            len += hypot(x - px, y - py), px = x, py = y;
        }
        curb_put(k, ax, ay, h, len, 0.0);
        g       = &k->g[0];
        g->poly = 1;
        for (q = 0; q < 2; q++)
        {
            (q ? g->v : g->u)[1] = (float)(3.0 * l[1][q]);
            (q ? g->v : g->u)[2] = (float)(3.0 * (l[2][q] - 2.0 * l[1][q]));
            (q ? g->v : g->u)[3] = (float)(l[3][q] - 3.0 * l[2][q] + 3.0 * l[1][q]);
        }
    }
    return 0;
}

/*  The corner the network files between arm a and arm b of junction j.
 *  It is the connecting road from a's road to b's that carries a
 *  sidewalk alone.  A car's turn joins the same two roads. -1 where
 *  there is none. */
static int corner_road(int j, const Arm *a, const Arm *b)
{
    int i;
    if (a->road < 0 || b->road < 0)
        return -1;
    for (i = 0; i < network_roads(); i++)
    {
        const NRoad *r = network_road(i);
        if (r->junction == j && r->pred.kind == NLINK_ROAD && r->succ.kind == NLINK_ROAD && r->pred.index == a->road &&
            r->pred.contact == a->contact && r->succ.index == b->road && r->succ.contact == b->contact &&
            (a->road != b->road || a->contact != b->contact) && !drives(i))
            return i;
    }
    return -1;
}

/*  The road's height at the end it meets a junction by, or NAN. */
static double arm_height(const Arm *a)
{
    const NRoad *r;
    if (a->road < 0)
        return NAN;
    /*  A road of no units stands at the height of the junction across it. */
    if (!s_road[a->road].nelev)
    {
        const NLink *far;
        r   = network_road(a->road);
        far = a->contact ? &r->pred : &r->succ;
        return r->topo >= 0 && topology_road(r->topo)->nunit == 0 && far->kind == NLINK_JUNCTION && far->index >= 0 &&
                       far->index < s_njunction && s_jh
                   ? s_jh[far->index]
                   : NAN;
    }
    return shape_height(a->road, a->contact ? s_road[a->road].length : 0.0f);
}

/*  File a line as a connecting road's pieces, and its height straight from
 *  za at its start to zb at its end.  A height unknown takes the other's,
 *  or the land's. */
static int line_file(int road, const Curb *k, double za, double zb)
{
    SRoad *sr = &s_road[road];
    SElev *e;
    int    q;
    if (arc_grow((void **)&s_geom, &s_capgeom, s_ngeom + k->n, sizeof *s_geom) != 0 ||
        arc_grow((void **)&s_elev, &s_capelev, s_nelev + 1, sizeof *s_elev) != 0)
        return -1;
    sr->geom0 = s_ngeom, sr->ngeom = k->n, sr->length = (float)k->len, sr->map0 = s_nmap, sr->nmap = 0;
    for (q = 0; q < k->n; q++)
        s_geom[s_ngeom++] = k->g[q];
    if (isnan(za))
        za = isnan(zb) ? shape_land_at(k->g[0].x, k->g[0].y) : zb;
    if (isnan(zb))
        zb = za;
    sr->elev0 = s_nelev, sr->nelev = 1;
    e    = &s_elev[s_nelev++];
    e->s = 0.0f, e->a = (float)za, e->b = k->len > 1e-6 ? (float)((zb - za) / k->len) : 0.0f, e->c = e->d = 0.0f;
    return 0;
}

static int corner_file(int road, const Curb *k, const Arm *a, const Arm *b)
{
    return line_file(road, k, arm_height(a), arm_height(b));
}

/*  A street junction's boundary, as OpenDRIVE's junction boundary is
 *  made.  It is a JOINT across each arm's road where it ends at the
 *  junction.  Between each two arms around it is the corner from one to
 *  the next (corner_curve).  Where the network files a corner there, the
 *  corner is that connecting road's line.  Each arm's road ends at its
 *  mouth. 0 where the junction is no street node or has fewer than two
 *  arms, so its outline is the hull. */
static int corner_outline(int j, float **opt, int **omouth, int **oarm, int *on)
{
    enum
    {
        MOST = 16
    };
    const TopoEnd *te = topology_end(network_junction(j)->end);
    Arm            arm[MOST];
    int            na = 0, i, k, side = topology_side(), n = 0, cap;
    double         cx = 0.0, cy = 0.0;
    float         *pt;
    int           *mo, *ar;
    if (te->net != RNET_ROAD || te->kind != END_NODE)
        return 0;
    /*  The shaped roads that reach it. */
    for (i = 0; i < network_roads() && na < MOST; i++)
    {
        const NRoad *nr = network_road(i);
        int          e;
        if (nr->junction >= 0 || nr->topo < 0 || !shape_road(i)->ngeom)
            continue;
        for (e = 0; e < 2 && na < MOST; e++)
        {
            const NLink *l = e ? &nr->succ : &nr->pred;
            float        x, y, h;
            if (l->kind != NLINK_JUNCTION || l->index != j)
                continue;
            shape_at(i, e ? shape_road(i)->length : 0.0f, &x, &y, &h);
            arm[na].px = x, arm[na].py = y;
            arm[na].dx = e ? -cos(h) : cos(h), arm[na].dy = e ? -sin(h) : sin(h);
            arm[na].half = width_at_junction(i, j, RNET_ROAD) * 0.5, arm[na].road = i, arm[na].contact = e;
            na++;
        }
    }
    /*  Each edge its tiles' pieces link across that leads out of it, where no
     *  shaped road comes in.  That is an edge to a ramp, a stub or the map's
     *  edge.  An edge a piece joins and nothing across it joins back is no
     *  link, so it is no arm. */
    for (k = 0; k < te->ncell; k++)
    {
        int32_t cell = topology_cell(te->cell0 + k), col = cell % side, row = cell / side;
        RPart   part[2];
        int     np = rcity_parts(s_city->xbld[cell], s_city->xbit[cell], part), q, e;
        for (q = 0; q < np; q++)
        {
            if (part[q].net != RNET_ROAD)
                continue;
            for (e = 0; e < 4 && na < MOST; e++)
            {
                double mx = col + 0.5 + 0.5 * rcity_edge_dc(1 << e), my = row + 0.5 + 0.5 * rcity_edge_dr(1 << e);
                int    c2, inside = 0, near = 0;
                if (!(part[q].edges & (1 << e)) || !topology_linked(RNET_ROAD, cell, 1 << e))
                    continue;
                for (c2 = 0; c2 < te->ncell; c2++)
                    inside |= topology_cell(te->cell0 + c2) == (row + rcity_edge_dr(1 << e)) * side + col + rcity_edge_dc(1 << e);
                /*  A shaped road that leaves by this edge anywhere along it
                 *  is its arm already. */
                for (i = 0; i < na; i++)
                {
                    double ex = rcity_edge_dc(1 << e) ? mx : fmin(fmax(arm[i].px, col), col + 1.0);
                    double ey = rcity_edge_dc(1 << e) ? fmin(fmax(arm[i].py, row), row + 1.0) : my;
                    near |= hypot(arm[i].px - ex, arm[i].py - ey) < 0.3;
                }
                if (inside || near)
                    continue;
                arm[na].px = mx, arm[na].py = my, arm[na].dx = rcity_edge_dc(1 << e), arm[na].dy = rcity_edge_dr(1 << e);
                arm[na].half = network_width(RNET_ROAD, 0) * 0.5, arm[na].road = -1, arm[na].contact = 0;
                na++;
            }
        }
        cx += col + 0.5, cy += row + 0.5;
    }
    if (na < 2)
        return 0;
    cx /= te->ncell, cy /= te->ncell;
    for (i = 0; i < na; i++)
        arm[i].ang = atan2(arm[i].py - cy, arm[i].px - cx);
    qsort(arm, (size_t)na, sizeof *arm, cmp_arm);
    cap = na * (2 + 3 * CORNER_MOST);
    pt  = (float *)malloc(sizeof *pt * (size_t)cap * 2);
    mo  = (int *)malloc(sizeof *mo * (size_t)cap);
    ar  = (int *)malloc(sizeof *ar * (size_t)cap);
    if (!pt || !mo || !ar)
    {
        free(pt), free(mo), free(ar);
        return -1;
    }
#define PUT(X, Y, M, A) (pt[2 * n] = (float)(X), pt[2 * n + 1] = (float)(Y), mo[n] = (M), ar[n] = (A), n++)
    for (i = 0; i < na; i++)
    {
        Arm *a = &arm[i], *b = &arm[(i + 1) % na];
        Curb cb;
        int  q, p, ns, road;
        /*  The joint across a's end, from its edge facing the arm before it
         *  to its edge facing b. */
        PUT(a->px + a->dy * a->half, a->py - a->dx * a->half, i, a->road >= 0 ? 2 * a->road + a->contact : -1);
        PUT(a->px - a->dy * a->half, a->py + a->dx * a->half, i, a->road >= 0 ? 2 * a->road + a->contact : -1);
        if (corner_curve(a, b, CURB_RADIUS, &cb) != 0)
            continue;
        road = corner_road(j, a, b);
        if (road >= 0)
        {
            /*  The corner's line is its curb, the inner edge of its
             *  sidewalk, so its lane (layer 2) lies on the sidewalk.
             *  That is the outline's corner moved in by the lane's
             *  width.  The arms' edges come in by it, and the curb
             *  return's radius grows by it, because its center lies out
             *  in the block. */
            const NRoad *cr = network_road(road);
            float        w  = cr->nsec > 0 ? network_lane_width(network_section(cr->sec0)->lane0, 0.0f) : 0.0f;
            Arm          ai = *a, bi = *b;
            Curb         ci;
            ai.half -= w, bi.half -= w;
            if (corner_file(road, ai.half > 0.0 && bi.half > 0.0 && corner_curve(&ai, &bi, CURB_RADIUS + w, &ci) == 0 ? &ci : &cb, a, b) != 0)
            {
                free(pt), free(mo), free(ar);
                return -1;
            }
        }
        /*  The corner, between the two joints: a line by its end, a curve
         *  through points along it. */
        for (p = 0; p < cb.n; p++)
        {
            const SGeom *g = &cb.g[p];
            ns             = g->poly || fabs(g->curvature) > 1e-9 ? (int)ceil(g->length / CORNER_STEP) : 1;
            ns             = ns < 1 ? 1 : ns > CORNER_MOST ? CORNER_MOST
                                                           : ns;
            for (q = 1; q <= ns; q++)
            {
                double x, y, h;
                if (p == cb.n - 1 && q == ns)
                    break; /* b's joint holds the corner's last point */
                geoms_at(cb.g, cb.n, (double)g->s + (double)g->length * q / ns, &x, &y, &h);
                PUT(x, y, 1000 + n, -1);
            }
        }
    }
#undef PUT
    /*  Every arm's road ends at its mouth. */
    for (i = 0; i < na; i++)
        if (arm[i].road >= 0)
            s_cut[2 * arm[i].road + arm[i].contact] = 0.0f;
    *opt = pt, *omouth = mo, *oarm = ar, *on = n;
    return 1;
}

/*  The arm a road makes where its `contact` reaches a junction: its line's
 *  end there and the way out along it, with no width.  Nonzero where the road
 *  has no line. */
static int arm_of(int road, int contact, Arm *a)
{
    float x, y, h;
    if (road < 0)
        return -1;
    /*  A road of no units is the edge it leaves its junction's unit by,
     *  toward the end beyond: the next unit, or the map's edge.  It reaches
     *  the junction at the middle of the edge the two units share, and
     *  goes out across it.  A unit is its end's own, a highway's two cells
     *  a side. */
    if (!s_road[road].ngeom && network_road(road)->topo >= 0 && topology_road(network_road(road)->topo)->nunit == 0)
    {
        const TopoRoad *t    = topology_road(network_road(road)->topo);
        int32_t         side = topology_side();
        double          mx[2], my[2], dc = 0.0, dr = 0.0, ox = 0.0, oy = 0.0;
        int             size[2] = {0, 0}, q;
        for (q = 0; q < 2; q++)
        {
            int     net = topology_end(t->end[q])->net, e = t->edge[q];
            int32_t u  = t->at[q];
            double  ec = (double)rcity_edge_dc(e),
                    er = (double)rcity_edge_dr(e);
            if (u < 0 || (ec == 0.0 && er == 0.0))
                continue;
            size[q] = net >= 0 && net < RNET_COUNT && NET[net].size > 0 ? NET[net].size : 1;
            mx[q] = u % side + 0.5 * size[q] * (1.0 + ec), my[q] = u / side + 0.5 * size[q] * (1.0 + er);
            if (q == contact)
                dc = ec, dr = er;
            else
                ox = ec, oy = er;
        }
        /*  At a gore, which holds no unit, the road lies across the one
         *  edge its other end's unit leaves by.  Both its ends are that
         *  edge's middle, and this end faces the other way. */
        if (!size[contact] && size[!contact])
        {
            a->px = mx[!contact], a->py = my[!contact], a->half = 0.0;
            a->dx = -ox, a->dy = -oy, a->road = road, a->contact = contact, a->ang = 0.0;
            return 0;
        }
        if (!size[contact])
            return -1;
        /*  The middle of the edge the two units share: the smaller unit's
         *  edge, where a cell meets a highway's unit two cells a side. */
        q     = size[!contact] && size[!contact] < size[contact] ? !contact : contact;
        a->px = mx[q], a->py = my[q], a->half = 0.0;
        a->dx = dc, a->dy = dr, a->road = road, a->contact = contact, a->ang = 0.0;
        return 0;
    }
    if (!s_road[road].ngeom || !shape_at(road, contact ? s_road[road].length : 0.0f, &x, &y, &h))
        return -1;
    a->px = x, a->py = y, a->half = 0.0;
    a->dx = contact ? -cos(h) : cos(h), a->dy = contact ? -sin(h) : sin(h);
    a->road = road, a->contact = contact, a->ang = 0.0;
    return 0;
}

/* ---- the places inside a junction ----------------------------------------- */

/*  How far into a junction an interchange arm's nodes lie along its way
 *  through it, in tiles. */
#define INNER_STEP 0.5

/*  A place a connecting road leaves from or arrives at: a point, the way
 *  along it, and its height. */
typedef struct
{
    double x, y, hdg, z;
} Pose;

/*  A line from one place to another: a paramPoly3 tangent to both ways, its
 *  tangents as long as the chord between them.  0, or -1 where the two
 *  places are one. */
static int pose_curve(const Pose *a, const Pose *b, Curb *k)
{
    double c = cos(a->hdg), sn = sin(a->hdg), dx = b->x - a->x, dy = b->y - a->y;
    double ux = dx * c + dy * sn, vy = -dx * sn + dy * c, L = hypot(dx, dy), tu, tv, len = 0.0, pu = 0.0, pv = 0.0;
    SGeom *g;
    int    q;
    memset(k, 0, sizeof *k);
    if (L < 1e-6)
        return -1;
    tu = L * cos(b->hdg - a->hdg), tv = L * sin(b->hdg - a->hdg);
    g       = &k->g[0];
    g->poly = 1, g->x = (float)a->x, g->y = (float)a->y, g->hdg = (float)a->hdg;
    g->u[0] = 0.0f, g->u[1] = (float)L, g->u[2] = (float)(3.0 * ux - 2.0 * L - tu), g->u[3] = (float)(-2.0 * ux + L + tu);
    g->v[0] = 0.0f, g->v[1] = 0.0f, g->v[2] = (float)(3.0 * vy - tv), g->v[3] = (float)(-2.0 * vy + tv);
    for (q = 1; q <= 64; q++)
    {
        double p = q / 64.0, u = g->u[1] * p + g->u[2] * p * p + g->u[3] * p * p * p;
        double v = g->v[2] * p * p + g->v[3] * p * p * p;
        len += hypot(u - pu, v - pv), pu = u, pv = v;
    }
    g->length = (float)len, k->n = 1, k->len = len;
    return 0;
}

/*  The connecting road of a junction with a line that runs straight
 *  through from one arm, turning least.  It runs from arm
 *  `road`/`contact` where `from`, else into it. -1 for none. */
static int way_through(int junction, int road, int contact, int from)
{
    int    i, best = -1;
    double least = 1e9;
    for (i = 0; i < network_roads(); i++)
    {
        const NRoad *r  = network_road(i);
        const NLink *me = from ? &r->pred : &r->succ, *it = from ? &r->succ : &r->pred;
        float        x0, y0, h0, x1, y1, h1;
        double       turn;
        if (r->junction != junction || !s_road[i].ngeom || me->kind != NLINK_ROAD || it->kind != NLINK_ROAD ||
            me->index != road || me->contact != contact || !drives(i))
            continue;
        shape_at(i, 0.0f, &x0, &y0, &h0);
        shape_at(i, s_road[i].length, &x1, &y1, &h1);
        turn = fabs(remainder((double)h1 - (double)h0, 2.0 * M_PI));
        if (turn < least)
            least = turn, best = i;
    }
    return best;
}

static int no_units(int lane);

/*  The `s` along its road's line of a lane's end `end`, at `u` units along
 *  the road.  A road of no units with a line of its own runs its line from
 *  its start to its end. */
static float lane_s(int lane, int end, float u)
{
    int road = network_lane(lane)->road;
    if (network_road(road)->length <= 0.0f && s_road[road].ngeom)
        return end ? s_road[road].length : 0.0f;
    return shape_s(road, u);
}

/*  The inner edge of a lane at one of its ends, its end 0 or 1 in `s`,
 *  as a place.  A place is the point, the heading of its traffic there
 *  and its height.  A lane of a road of no units lies across its mouth,
 *  heading along the way out across it. 0, or -1 where its road has no
 *  line and is no road of no units. */
static int lane_edge(int lane, int end, Pose *p)
{
    const NLane    *l = network_lane(lane);
    const NRoad    *r = network_road(l->road);
    const NSection *x = network_section(l->section);
    float           u = end ? (l->section + 1 < r->sec0 + r->nsec ? network_section(l->section + 1)->s : r->length) : x->s;
    float           ti, to, px, py, h;
    if (s_road[l->road].ngeom)
    {
        float sl = lane_s(lane, end, u);
        if (!shape_at(l->road, sl, &px, &py, &h))
            return -1;
        p->z = shape_height(l->road, sl);
    }
    else
    {
        Arm a;
        if (!no_units(lane) || arm_of(l->road, end, &a) != 0)
            return -1;
        px = (float)a.px, py = (float)a.py, h = (float)(end ? atan2(-a.dy, -a.dx) : atan2(a.dy, a.dx));
        p->z = arm_height(&a);
    }
    network_lane_t(lane, u - x->s, &ti, &to);
    p->x = px - sin(h) * ti, p->y = py + cos(h) * ti;
    p->hdg = network_lane_with_s(lane) ? h : h + M_PI;
    return 0;
}

/*  The order lanes take across the lane they part from or join side by
 *  side.  The way straight on comes first, then an exit or entry lane. */
static int side_order(int lane)
{
    int t = network_lane(lane)->type;
    return nlane_is_change(t);
}

/*  The place where traffic connecting road `road` leaves (end 0) or
 *  arrives (end 1).  It is the inner edge of the lane it links with
 *  there, the innermost where it links with several.  Where it parts
 *  from that lane side by side with other connecting roads, or joins it
 *  so, it lies outside those before it (side_order). 0, or -1 where it
 *  links with no lane that has a place. */
static int conn_place(int road, int end, Pose *p)
{
    const NRoad *c    = network_road(road);
    int          lane = network_section(c->sec0)->lane0, it, o = -1, oe = 0, other, ote;
    double       best = 1e18;
    Pose         q;
    for (it = network_end_link(lane, end, -1, &other, &ote, NULL); it >= 0; it = network_end_link(lane, end, it, &other, &ote, NULL))
    {
        float ti, to;
        if (network_lane(other)->road == road || lane_edge(other, ote, &q) != 0)
            continue;
        network_lane_t(other, ote ? 1e9f : 0.0f, &ti, &to);
        if (fabs(ti) < best)
            best = fabs(ti), o = other, oe = ote, *p = q;
    }
    if (o < 0)
        return -1;
    /*  Lanes that part from it or join it side by side add up to its
     *  width. */
    {
        double wo = network_lane_width(o, oe ? 1e9f : 0.0f), sum = 0.0, before = 0.0, side;
        int    n = 0, qn, qe;
        for (it = network_end_link(o, oe, -1, &qn, &qe, NULL); it >= 0; it = network_end_link(o, oe, it, &qn, &qe, NULL))
        {
            if (network_lane(qn)->road == network_lane(o)->road || !network_lane_in_graph(qn) || network_lane(qn)->type == NLANE_SIDEWALK)
                continue;
            sum += network_lane_width(qn, qe ? 1e9f : 0.0f), n++;
            if (network_lane(qn)->road != road &&
                (side_order(qn) < side_order(lane) || (side_order(qn) == side_order(lane) && network_lane(qn)->road < road)))
                before += network_lane_width(qn, qe ? 1e9f : 0.0f);
        }
        if (n > 1 && fabs(sum - wo) < 1e-3 && before > 0.0)
        {
            side = network_lane(lane)->id > 0 ? 1.0 : -1.0;
            p->x += -sin(p->hdg) * side * before, p->y += cos(p->hdg) * side * before;
        }
    }
    return 0;
}

/*  A node carried from a place along the way through to `at` along it, at
 *  the place's own distance across from it.  The place lies at the way's
 *  end where `back`, else at its start. */
static void carry(int way, const Pose *from, int back, double at, double z, Pose *p)
{
    float  L = s_road[way].length, x0, y0, h0, x, y, h;
    double d;
    shape_at(way, back ? L : 0.0f, &x0, &y0, &h0);
    d = (from->x - x0) * -sin(h0) + (from->y - y0) * cos(h0);
    shape_at(way, (float)fmin(fmax(at, 0.0), L), &x, &y, &h);
    p->x = x - sin(h) * d, p->y = y + cos(h) * d, p->hdg = h, p->z = z;
}

/*  How far before the point abreast of the middle of its cell a ramp's
 *  lane leaves the auxiliary lane, or after it joins, in tiles. */
#define RAMP_LEAD 1.0
/*  How far apart two nodes on one side lie at least, in tiles.  Where
 *  their leads bring them closer or past each other, they make a weave. */
#define WEAVE 0.5

/*  The place of ramp `i`'s node (plans/network.md, layer 3, "A node inside
 *  the junction").  It is the inner edge of the auxiliary lane there,
 *  heading along it.  The auxiliary lane runs from the place it leaves
 *  the arriving arm to the place it joins the leaving arm.  0, or -1 where
 *  the ramp has no node or its lanes have no place. */
static int ramp_node(int i, Pose *p)
{
    const NRamp *rp   = network_ramp(i);
    int32_t      side = topology_side();
    Pose         a, b;
    double       L, ux, uy, at[16], sb[16], t, lo;
    int          k, j, ti, m = 0, me = -1, idx[16];
    if (!rp->built || rp->endon || rp->aux_first < 0 || rp->lane_ramp < 0 ||
        conn_place(network_lane(rp->aux_first)->road, 0, &a) != 0 || conn_place(network_lane(rp->aux_last)->road, 1, &b) != 0)
        return -1;
    L = hypot(b.x - a.x, b.y - a.y);
    if (L < 1e-6)
        return -1;
    ux = (b.x - a.x) / L, uy = (b.y - a.y) / L;
    /*  Every ramp on the side, in order along it. */
    for (k = 0; k < network_ramps() && m < 16; k++)
    {
        const NRamp *q = network_ramp(k);
        if (q->aux_first != rp->aux_first || q->lane_ramp < 0)
            continue;
        sb[m]  = (q->cell % side + 0.5 - a.x) * ux + (q->cell / side + 0.5 - a.y) * uy;
        at[m]  = q->on ? sb[m] + RAMP_LEAD : sb[m] - RAMP_LEAD;
        idx[m] = k;
        for (j = m++; j > 0 && sb[j] < sb[j - 1]; j--)
        {
            t = sb[j], sb[j] = sb[j - 1], sb[j - 1] = t;
            t = at[j], at[j] = at[j - 1], at[j - 1] = t;
            ti = idx[j], idx[j] = idx[j - 1], idx[j - 1] = ti;
        }
    }
    /*  Two nodes closer than a weave, or out of the ramps' order, lie a
     *  weave apart about their midpoint. */
    for (k = 0; k + 1 < m; k++)
        if (at[k] > at[k + 1] - WEAVE)
        {
            double mid = 0.5 * (at[k] + at[k + 1]);
            at[k] = mid - 0.5 * WEAVE, at[k + 1] = mid + 0.5 * WEAVE;
        }
    /*  Inside the junction, in order. */
    lo = fmin(0.5 * WEAVE, L / (2.0 * (m + 1)));
    for (k = 0; k < m; k++)
    {
        at[k] = fmin(fmax(at[k], k ? at[k - 1] + lo : lo), L - lo * (m - k));
        if (idx[k] == i)
            me = k;
    }
    if (me < 0)
        return -1;
    p->x = a.x + ux * at[me], p->y = a.y + uy * at[me], p->hdg = atan2(uy, ux), p->z = a.z;
    return 0;
}

/*  Where the line of ramp road `road` begins at band junction `junction`.
 *  That is the middle of the auxiliary lane at the ramp's node, heading
 *  the way the ramp's line leaves it.  An off ramp's leaves away along the
 *  highway, and an on ramp's back along it.  0, or -1 where it has no
 *  node. */
static int ramp_start(int road, int junction, double *x, double *y, double *hdg)
{
    int i;
    for (i = 0; i < network_ramps(); i++)
    {
        const NRamp *rp   = network_ramp(i);
        int32_t      side = topology_side();
        Pose         n;
        double       w, w0, nx, ny;
        if (rp->road != road || rp->band_junction != junction || !rp->built)
            continue;
        /*  A ramp that joins at a gore begins there, on the middle of the
         *  band's outermost driving lane on its side, heading along the
         *  band. */
        if (rp->gore >= 0)
        {
            /*  The band's road beyond the gore, or at the map's edge the one
             *  road the gore has.  A lane arrives at the gore by it or
             *  leaves the gore by it. */
            int          arrive = rp->on ? rp->out_road < 0 : rp->in_road >= 0;
            int          br = arrive ? rp->in_road : rp->out_road, bc = arrive ? rp->in_contact : rp->out_contact, k;
            const NRoad *q   = network_road(br);
            int          sec = bc ? q->sec0 + q->nsec - 1 : q->sec0, lane = -1;
            float        px, py, ph, ti, to, sl;
            for (k = 0; k < network_section(sec)->nlane; k++)
            {
                const NLane *l = network_lane(network_section(sec)->lane0 + k);
                if (l->type == NLANE_DRIVING && network_lane_with_s(network_section(sec)->lane0 + k) == (arrive ? bc == 1 : bc == 0) &&
                    (lane < 0 || abs(l->id) > abs(network_lane(lane)->id)))
                    lane = network_section(sec)->lane0 + k;
            }
            if (lane < 0 || !s_road[br].ngeom)
                return -1;
            sl = bc ? s_road[br].length : 0.0f;
            if (!shape_at(br, sl, &px, &py, &ph))
                return -1;
            network_lane_t(lane, bc ? 1e9f : 0.0f, &ti, &to);
            /*  The lane's middle at its full width.  At the map's edge the
             *  one road is the one without the ramp's lane.  There the
             *  ramp's lane lies a lane further out, beside its outermost
             *  lane. */
            {
                const int edge = rp->on ? rp->out_road < 0 : rp->in_road < 0;
                float     lw = network_lane_width(lane, bc ? 1e9f : 0.0f), dir = to > ti ? 1.0f : -1.0f;
                float     mid = edge ? to + dir * 0.5f * lw : ti + dir * 0.5f * lw;
                *x = px - sin(ph) * mid, *y = py + cos(ph) * mid;
            }
            /*  Away along the band for an off ramp, back along it for an on
             *  ramp: the way the ramp's line leaves the gore. */
            *hdg = (network_lane_with_s(lane) == !rp->on) ? ph : ph + M_PI;
            return 0;
        }
        /*  A ramp at the band's end begins in the middle of the lanes it
         *  takes or feeds, where they reach the junction, heading into
         *  it. */
        if (rp->endon)
        {
            Arm a;
            int c, k;
            for (c = 0; c < network_roads(); c++)
            {
                const NRoad *q = network_road(c);
                if (q->junction == junction && q->nsec > 0 &&
                    ((q->pred.kind == NLINK_ROAD && q->pred.index == road) || (q->succ.kind == NLINK_ROAD && q->succ.index == road)))
                    break;
            }
            if (c == network_roads() || arm_of(rp->band, rp->band_contact, &a) != 0)
                return -1;
            k = network_road(c)->pred.kind == NLINK_ROAD && network_road(c)->pred.index == rp->band ? 0 : 1;
            if (conn_place(c, k, &n) != 0)
                return -1;
            w  = network_section_width(network_road(c)->sec0, k ? 1e9f : 0.0f);
            nx = n.x - a.px, ny = n.y - a.py, w0 = hypot(nx, ny);
            if (w0 < 1e-6)
                return -1;
            nx /= w0, ny /= w0;
            *x = n.x + nx * 0.5 * w, *y = n.y + ny * 0.5 * w, *hdg = atan2(-a.dy, -a.dx);
            return 0;
        }
        if (ramp_node(i, &n) != 0)
            continue;
        w  = network_lane_width(rp->lane_ex, 1e9f);
        nx = -sin(n.hdg), ny = cos(n.hdg);
        if (nx * (rp->cell % side + 0.5 - n.x) + ny * (rp->cell / side + 0.5 - n.y) < 0.0)
            nx = -nx, ny = -ny;
        *x = n.x + nx * 0.5 * w, *y = n.y + ny * 0.5 * w, *hdg = rp->on ? n.hdg + M_PI : n.hdg;
        return 0;
    }
    return -1;
}

/*  The place where connecting road `road` leaves (end 0) or arrives (end
 *  1), where that end is a ramp's node.  0, or -1 where it is none. */
static int ramp_node_of(int road, int end, Pose *p)
{
    int i;
    for (i = 0; i < network_ramps(); i++)
    {
        const NRamp *rp = network_ramp(i);
        if (rp->lane_ramp < 0)
            continue;
        if ((end == 1 && network_lane(rp->lane_ex)->road == road) || (end == 0 && network_lane(rp->lane_en)->road == road) ||
            (network_lane(rp->lane_ramp)->road == road && end == rp->on))
            return ramp_node(i, p);
    }
    return -1;
}

/*  Each connecting road that carries traffic through a junction from one
 *  road to another.  That is a car's way straight on or round a turn, a
 *  highway's way through an interchange, or a turnout's branch.  Its
 *  line runs along its lanes (plans/network.md, layer 3), from the place
 *  it leaves to the place it arrives (conn_place).  It is tangent to
 *  both (corner_curve with no width, at its network's radius).  A level
 *  meet's are the run's own line across it (file_span), and a junction's
 *  corners are its boundary's (corner_outline). */
static int connections(void)
{
    int i, pass;
    /*  The band's first. */
    for (pass = 0; pass < 2; pass++)
        for (i = 0; i < s_nroad; i++)
        {
            const NRoad *r = network_road(i);
            Arm          a, b;
            Curb         k;
            Pose         p0, p1;
            if ((r->net == RNET_BAND) != (pass == 0))
                continue;
            if (r->junction < 0 || NET[r->net].size == 0 || s_road[i].ngeom || !drives(i) || r->pred.kind != NLINK_ROAD ||
                r->succ.kind != NLINK_ROAD || r->nsec < 1)
                continue;
            if (conn_place(i, 0, &p0) == 0 && conn_place(i, 1, &p1) == 0)
            {
                memset(&a, 0, sizeof a), memset(&b, 0, sizeof b);
                a.px = p0.x, a.py = p0.y, a.dx = -cos(p0.hdg), a.dy = -sin(p0.hdg), a.road = -1;
                b.px = p1.x, b.py = p1.y, b.dx = cos(p1.hdg), b.dy = sin(p1.hdg), b.road = -1;
                if (corner_curve(&a, &b, NET[r->net].radius, &k) != 0)
                    continue;
                if (line_file(i, &k, p0.z, p1.z) != 0)
                    return -1;
                continue;
            }
            if (arm_of(r->pred.index, r->pred.contact, &a) != 0 || arm_of(r->succ.index, r->succ.contact, &b) != 0 ||
                corner_curve(&a, &b, NET[r->net].radius, &k) != 0)
                continue;
            if (corner_file(i, &k, &a, &b) != 0)
                return -1;
        }
    return 0;
}

/*  THE VERTICAL CURVES: each change of grade inside a road becomes a
 *  parabola, as a road's profile is laid out.  The curve starts on the
 *  grade before and ends on the grade after.  It reaches VCURVE_HALF
 *  each way, or half of either piece where that is shorter.  It reaches
 *  less where it would pass more than VCURVE_DROP off the change.  Its
 *  grade runs evenly from the one grade to the other, so it is never
 *  steeper than either.  A road's ends keep their heights and grades. 0,
 *  or -1 out of memory. */
static int vertical_curves(void)
{
    int i;
    for (i = 0; i < s_nroad; i++)
    {
        SRoad *sr = &s_road[i];
        int    n = sr->nelev, e0 = sr->elev0, k, m = 0;
        double L = sr->length;
        if (n < 2)
            continue;
        if (arc_grow((void **)&s_elev, &s_capelev, s_nelev + 2 * n, sizeof *s_elev) != 0)
            return -1;
        for (k = 0; k < n; k++)
        {
            const SElev *e  = &s_elev[e0 + k];
            double       s0 = e->s, s1 = k + 1 < n ? s_elev[e0 + k + 1].s : L, h0 = 0.0, h1 = 0.0;
            if (k > 0)
            {
                const SElev *p = &s_elev[e0 + k - 1];
                h0             = fmin(fmin(VCURVE_HALF, 4.0 * VCURVE_DROP / fmax(fabs(e->b - p->b), 1e-9)), 0.5 * fmin(s0 - p->s, s1 - s0));
                h0             = fabs(e->b - p->b) > 1e-6f && h0 > 1e-6 ? h0 : 0.0;
            }
            if (k + 1 < n)
            {
                const SElev *q  = &s_elev[e0 + k + 1];
                double       s2 = k + 2 < n ? s_elev[e0 + k + 2].s : L;
                h1              = fmin(fmin(VCURVE_HALF, 4.0 * VCURVE_DROP / fmax(fabs(q->b - e->b), 1e-9)), 0.5 * fmin(s1 - s0, s2 - s1));
                h1              = fabs(q->b - e->b) > 1e-6f && h1 > 1e-6 ? h1 : 0.0;
            }
            if (s1 - h1 - (s0 + h0) > 1e-6 || m == 0)
                s_elev[s_nelev + m++] = (SElev){(float)(s0 + h0), (float)(e->a + e->b * h0), e->b, 0.0f, 0.0f};
            if (h1 > 0.0)
            {
                const SElev *q        = &s_elev[e0 + k + 1];
                s_elev[s_nelev + m++] = (SElev){(float)(s1 - h1), (float)(q->a - e->b * h1), e->b, (float)((q->b - e->b) / (4.0 * h1)), 0.0f};
            }
        }
        s_elev[s_nelev].s = s_elev[e0].s;
        sr->elev0 = s_nelev, sr->nelev = m, s_nelev += m;
    }
    return 0;
}

/*  Each connecting road that carries traffic to or from a node inside
 *  its junction.  That is an auxiliary lane's lanes and a ramp's lane, and
 *  an interchange's exit, entry and turning lanes.  Its line runs along
 *  its lanes from the place it leaves to the place it arrives (pose_curve).
 *  A ramp's node has its own place (ramp_node).  Of the rest, the exit
 *  lanes are filed first, each node a step along the way through
 *  from where its lane leaves the arm (carry).  Then the entry lanes, from
 *  the exit lane's place at their node where one goes on into them, else
 *  carried back from their arm.  Then the rest, from the places at their
 *  nodes. */
static int inner_lines(void)
{
    int i, pass;
    for (pass = 0; pass < 3; pass++)
        for (i = 0; i < s_nroad; i++)
        {
            const NRoad *r = network_road(i);
            Pose         a, b;
            Curb         k;
            int          w, kind;
            double       z;
            if (r->junction < 0 || NET[r->net].size == 0 || s_road[i].ngeom || !drives(i) || r->nsec < 1 ||
                (r->pred.kind != NLINK_JUNCTION && r->succ.kind != NLINK_JUNCTION))
                continue;
            kind = r->pred.kind == NLINK_ROAD ? 0 : r->succ.kind == NLINK_ROAD ? 1
                                                                               : 2;
            /*  A ramp's node has its own place. */
            if (pass == 0 && (ramp_node_of(i, 0, &a) == 0 || ramp_node_of(i, 1, &b) == 0))
            {
                if ((ramp_node_of(i, 0, &a) != 0 && conn_place(i, 0, &a) != 0) ||
                    (ramp_node_of(i, 1, &b) != 0 && conn_place(i, 1, &b) != 0) || pose_curve(&a, &b, &k) != 0)
                    continue;
                if (line_file(i, &k, a.z, b.z) != 0)
                    return -1;
                continue;
            }
            if (kind != pass)
                continue;
            z = s_jh ? s_jh[r->junction] : NAN;
            if (kind == 0)
            {
                if (conn_place(i, 0, &a) != 0)
                    continue;
                w = way_through(r->junction, r->pred.index, r->pred.contact, 1);
                if (w >= 0)
                    carry(w, &a, 0, fmin(INNER_STEP, s_road[w].length * 0.5), isnan(z) ? a.z : z, &b);
                else
                    b = a, b.x += cos(a.hdg) * INNER_STEP, b.y += sin(a.hdg) * INNER_STEP;
            }
            else if (kind == 1)
            {
                if (conn_place(i, 1, &b) != 0)
                    continue;
                if (conn_place(i, 0, &a) != 0)
                {
                    w = way_through(r->junction, r->succ.index, r->succ.contact, 0);
                    if (w >= 0)
                        carry(w, &b, 1, fmax(s_road[w].length - INNER_STEP, s_road[w].length * 0.5), isnan(z) ? b.z : z, &a);
                    else
                        a = b, a.x -= cos(b.hdg) * INNER_STEP, a.y -= sin(b.hdg) * INNER_STEP;
                }
            }
            else if (conn_place(i, 0, &a) != 0 || conn_place(i, 1, &b) != 0)
                continue;
            if (pose_curve(&a, &b, &k) != 0)
                continue;
            if (line_file(i, &k, a.z, b.z) != 0)
                return -1;
        }
    return 0;
}

/* ---- an interchange's ramp roads (plans/interchanges.md, "The shape") ------- */

/*  The middle of a lane at one of its ends, heading the way its traffic
 *  runs, at its road's height there.  0, or -1 where its road has no line. */
static int lane_mid(int lane, int end, Pose *p)
{
    const NLane    *l = network_lane(lane);
    const NRoad    *r = network_road(l->road);
    const NSection *x = network_section(l->section);
    float           u = end ? (l->section + 1 < r->sec0 + r->nsec ? network_section(l->section + 1)->s : r->length) : x->s;
    float           ti, to, px, py, h, sl, mid;
    if (!s_road[l->road].ngeom)
    {
        /*  A road of no units lies across the edge it crosses. */
        Arm a;
        if (!no_units(lane) || arm_of(l->road, end, &a) != 0)
            return -1;
        px = (float)a.px, py = (float)a.py, h = (float)(end ? atan2(-a.dy, -a.dx) : atan2(a.dy, a.dx));
        network_lane_t(lane, u - x->s, &ti, &to);
        mid  = 0.5f * (ti + to);
        p->x = px - sin(h) * mid, p->y = py + cos(h) * mid;
        p->z = arm_height(&a), p->hdg = network_lane_with_s(lane) ? h : h + M_PI;
        return isnan(p->z) ? (p->z = shape_land_at(p->x, p->y), 0) : 0;
    }
    sl = lane_s(lane, end, u);
    if (!shape_at(l->road, sl, &px, &py, &h))
        return -1;
    network_lane_t(lane, u - x->s, &ti, &to);
    mid  = 0.5f * (ti + to);
    p->x = px - sin(h) * mid, p->y = py + cos(h) * mid;
    p->z = shape_height(l->road, sl), p->hdg = network_lane_with_s(lane) ? h : h + M_PI;
    return 0;
}

/*  The point of a curve at p in [0, 1], and its heading. */
static void curb_at(const Curb *k, double p, Pose *o)
{
    const SGeom *g = &k->g[0];
    double       u = g->u[1] * p + g->u[2] * p * p + g->u[3] * p * p * p, v = g->v[2] * p * p + g->v[3] * p * p * p;
    double       du = g->u[1] + 2.0 * g->u[2] * p + 3.0 * g->u[3] * p * p, dv = 2.0 * g->v[2] * p + 3.0 * g->v[3] * p * p;
    double       c = cos(g->hdg), sn = sin(g->hdg);
    o->x = g->x + u * c - v * sn, o->y = g->y + u * sn + v * c, o->hdg = g->hdg + atan2(dv, du);
}

/*  A WAY: a reference line of a few pieces, a line, an arc or a spiral
 *  each, as OpenDRIVE's planView holds them, or one paramPoly3. */
typedef struct
{
    SGeom  g[24];
    int    n;
    double len;
} Way;

/*  The point, heading and curvature at `d` along piece `g`. */
static void piece_at(const SGeom *g, double d, Pose *o, double *k)
{
    double x, y, h;
    if (g->poly)
    {
        Curb c;
        c.g[0] = *g, c.n = 1, c.len = g->length;
        curb_at(&c, g->length > 1e-9 ? d / g->length : 0.0, o);
        if (k)
            *k = 0.0;
        return;
    }
    /*  A line or an arc has its point in closed form, and only a spiral is
     *  summed along. */
    if (g->cend == g->curvature)
    {
        double c = g->curvature, h0 = g->hdg;
        if (fabs(c) < 1e-12)
            x = g->x + cos(h0) * d, y = g->y + sin(h0) * d, h = h0;
        else
            h = h0 + c * d, x = g->x + (sin(h) - sin(h0)) / c, y = g->y - (cos(h) - cos(h0)) / c;
    }
    else
        spiral_at(g->x, g->y, g->hdg, g->curvature, g->cend, g->length, d, &x, &y, &h);
    o->x = x, o->y = y, o->hdg = h;
    if (k)
        *k = g->curvature + (g->length > 1e-9 ? (g->cend - g->curvature) * d / g->length : 0.0);
}

/*  The end of a way, heading on. */
static void way_end(const Way *w, Pose *o)
{
    piece_at(&w->g[w->n - 1], w->g[w->n - 1].length, o, NULL);
}

/*  Add a piece from where the way ends, or from `from` for its first: a
 *  length whose curvature runs evenly from k0 to k1. */
static void way_put(Way *w, const Pose *from, double len, double k0, double k1)
{
    Pose   at;
    SGeom *g;
    if (len < 1e-6 || w->n == 24)
        return;
    if (w->n)
        way_end(w, &at);
    else
        at = *from;
    g = &w->g[w->n++];
    memset(g, 0, sizeof *g);
    g->s = (float)w->len, g->x = (float)at.x, g->y = (float)at.y, g->hdg = (float)at.hdg;
    g->length = (float)len, g->curvature = (float)k0, g->cend = (float)k1, g->spiral = k0 != k1;
    w->len += len;
}

/*  The share of a transition curve's turn its two spirals take together.
 *  The rest is its arc. */
#define IX_SPIRAL 0.8

/*  THE TRANSITION CURVE between two places (Vazquez-Mendez et al. 2021, the
 *  design standard): a spiral from a line, an arc, and a spiral back to a
 *  line.  The two places' lines meet ahead of the first and behind the
 *  second.  The spirals take the share IX_SPIRAL of the turn between them.  The curve is as
 *  wide as the nearer of the two leaves room for.  The rest of the
 *  farther one is a line before or after the curve.  0, or -1 where the
 *  lines do not meet so. */
static int transition(const Pose *a, const Pose *b, Way *w)
{
    double D = remainder(b->hdg - a->hdg, 2.0 * M_PI), A = fabs(D), sg = D > 0.0 ? 1.0 : -1.0;
    double dax = cos(a->hdg), day = sin(a->hdg), dbx = cos(b->hdg), dby = sin(b->hdg), det = dax * dby - day * dbx;
    double t1, t2, tu, R, T;
    Way    u;
    Pose   o = {0.0, 0.0, 0.0, 0.0}, e;
    if (A < 0.02 || A > 3.0 || fabs(det) < 1e-6)
        return -1;
    t1 = ((b->x - a->x) * dby - (b->y - a->y) * dbx) / det;
    t2 = (dax * (b->y - a->y) - day * (b->x - a->x)) / det;
    if (t1 < 1e-3 || t2 < 1e-3)
        return -1;
    /*  The curve for a radius of one, from the origin along the x axis. */
    memset(&u, 0, sizeof u);
    way_put(&u, &o, IX_SPIRAL * A, 0.0, sg), way_put(&u, NULL, (1.0 - IX_SPIRAL) * A, sg, sg);
    way_put(&u, NULL, IX_SPIRAL * A, sg, 0.0);
    way_end(&u, &e);
    tu = e.x - e.y * cos(D) / sin(D);
    if (tu < 1e-6)
        return -1;
    T = fmin(t1, t2), R = T / tu;
    memset(w, 0, sizeof *w);
    way_put(w, a, t1 - T, 0.0, 0.0);
    if (w->n)
        way_put(w, NULL, IX_SPIRAL * A * R, 0.0, sg / R);
    else
        way_put(w, a, IX_SPIRAL * A * R, 0.0, sg / R);
    way_put(w, NULL, (1.0 - IX_SPIRAL) * A * R, sg / R, sg / R);
    way_put(w, NULL, IX_SPIRAL * A * R, sg / R, 0.0);
    way_put(w, NULL, t2 - T, 0.0, 0.0);
    return 0;
}

/*  A way from one place to another: the transition curve, or else one
 *  paramPoly3. */
static int way_between(const Pose *a, const Pose *b, Way *w)
{
    Curb c;
    if (transition(a, b, w) == 0)
        return 0;
    if (pose_curve(a, b, &c) != 0)
        return -1;
    memset(w, 0, sizeof *w);
    w->g[0] = c.g[0], w->n = 1, w->len = c.len;
    return 0;
}

/*  The transition curve from one place to another as a run's pieces,
 *  from s nought.  It answers the count of pieces, or 0 where the two
 *  places allow none.  `R` gets its arc's radius. */
static int ramp_transition(double x0, double y0, double h0, double x1, double y1, double h1, G *out, double *R)
{
    Pose a = {x0, y0, h0, 0.0}, b = {x1, y1, h1, 0.0};
    Way  w;
    int  q;
    if (transition(&a, &b, &w) != 0)
        return 0;
    for (q = 0; q < w.n; q++)
    {
        out[q].s = w.g[q].s, out[q].x = w.g[q].x, out[q].y = w.g[q].y, out[q].hdg = w.g[q].hdg;
        out[q].length = w.g[q].length, out[q].curvature = w.g[q].curvature, out[q].cend = w.g[q].cend;
        if (R && fabs(w.g[q].curvature) > 1e-9 && fabs(w.g[q].curvature - w.g[q].cend) < 1e-9)
            *R = 1.0 / fabs(w.g[q].curvature);
    }
    return w.n;
}

/*  Whether a road is one of a region's.  That is a road of the band in
 *  no junction and on no units: a bundle, a strand or a pair. */
static int rg_road(int road)
{
    const NRoad *r = network_road(road);
    return r->net == RNET_BAND && r->topo < 0 && r->junction < 0 && r->nsec > 0;
}

/*  A road's own lane: a strand's or a pair's connectingRamp lane.  A
 *  bundle has none, -1. */
static int rg_main(int road)
{
    const NSection *x = network_section(network_road(road)->sec0);
    int             k;
    for (k = 0; k < x->nlane; k++)
        if (network_lane(x->lane0 + k)->type == NLANE_CONNRAMP)
            return x->lane0 + k;
    return -1;
}

/*  The lane linked with lane `lane` at its end `end` on another road that
 *  has a line, else -1. */
static int rg_neighbor(int lane, int end, int *other_end)
{
    int it, o, oe;
    for (it = network_end_link(lane, end, -1, &o, &oe, NULL); it >= 0; it = network_end_link(lane, end, it, &o, &oe, NULL))
        if (network_lane(o)->road != network_lane(lane)->road && (s_road[network_lane(o)->road].ngeom || no_units(o)))
        {
            *other_end = oe;
            return o;
        }
    return -1;
}

/*  The outermost driving lane of road `road` at its contact `c`.  It is
 *  of the lanes leaving the contact's junction into it where `leaving`,
 *  else of those arriving. */
static int rg_outer(int road, int c, int leaving)
{
    const NRoad    *r = network_road(road);
    const NSection *x = network_section(c ? r->sec0 + r->nsec - 1 : r->sec0);
    int             k, best = -1;
    for (k = 0; k < x->nlane; k++)
    {
        int lane = x->lane0 + k;
        if (network_lane(lane)->type != NLANE_DRIVING || network_lane_with_s(lane) != (leaving ? c == 0 : c == 1))
            continue;
        if (best < 0 || abs(network_lane(lane)->id) > abs(network_lane(best)->id))
            best = lane;
    }
    return best;
}

/*  Whether region road `road` is a cloverleaf's loop: from a weave's node
 *  on the way out to a weave's node on the way in. */
static int rg_loop(int road)
{
    const NRoad *r = network_road(road);
    int          a[7], b[7];
    return r->pred.kind == NLINK_JUNCTION && r->succ.kind == NLINK_JUNCTION &&
           network_node_chain(r->pred.index, &a[0], &a[1], &a[2], &a[3], &a[4], &a[5]) &&
           network_node_chain(r->succ.index, &b[0], &b[1], &b[2], &b[3], &b[4], &b[5]) && a[3] == NNODE_WEAVE && b[3] == NNODE_WEAVE &&
           a[2] && !b[2];
}

/*  How many of the regions' strands run against their own traffic
 *  (plans/interchanges.md, "What the shape keeps").  Such a strand's
 *  heading turns more than a quarter turn from the way from its start to
 *  its end.  A cloverleaf's loop turns three quarters of a turn, so it is
 *  not held to that. */
static int rg_backward(int say)
{
    int i, n = 0;
    for (i = 0; i < s_nroad; i++)
    {
        float  x0, y0, h0, x1, y1, h1, x, y, h, s;
        double cx, cy, L;
        if (!rg_road(i) || rg_main(i) < 0 || rg_loop(i) || !s_road[i].ngeom || !shape_at(i, 0.0f, &x0, &y0, &h0) ||
            !shape_at(i, s_road[i].length, &x1, &y1, &h1))
            continue;
        cx = x1 - x0, cy = y1 - y0, L = hypot(cx, cy);
        if (L < 1e-6)
            continue;
        for (s = 0.0f; s <= s_road[i].length; s += 0.02f)
        {
            shape_at(i, s, &x, &y, &h);
            if (cos(h) * cx + sin(h) * cy < 0.0)
            {
                n++;
                if (say)
                    dumpf("shape fault: strand %016llx runs against its traffic at %.2f,%.2f\n", (unsigned long long)network_road(i)->id, (double)x, (double)y);
                break;
            }
        }
    }
    return n;
}

/*  Whether a region's road links at its end `end` with the outermost
 *  driving lane of an arm, there at the arm's gore. */
static int rg_from_outer(int road, int end)
{
    int m = rg_main(road), o, oe = 0;
    if (m < 0 || (o = rg_neighbor(m, end, &oe)) < 0 || network_road(network_lane(o)->road)->topo < 0)
        return 0;
    return o == rg_outer(network_lane(o)->road, oe, end);
}

/*  Whether a region's road starts from a pair's side lane: a branch that
 *  splits off, which lies beside the lane it leaves, not along it. */
static int rg_from_side(int road)
{
    int m = rg_main(road), it, o, into;
    for (it = network_end_link(m, 0, -1, &o, NULL, &into); it >= 0; it = network_end_link(m, 0, it, &o, NULL, &into))
        if (into && network_lane(o)->type == NLANE_EXIT)
            return 1;
    return 0;
}

/* ---- the layout of a region (plans/interchanges.md, "The shape") ------------ */

/*  A region's layout follows from its PLACES, one vector of numbers that the
 *  search moves.  The gap from one node of an outermost lane to the next, in
 *  tiles: the least, the most and where the search starts. */
#define IX_GAP_LO 0.1
#define IX_GAP_HI 1.0
#define IX_GAP0   0.2
/*  The most a city ramp's node lies past the node before it, in tiles.  A
 *  ramp stands where its cell is, and its gore may lie well out along the
 *  arm.  So its node may need to lie far in, to join or leave the lane on
 *  the ramp's own side of its cell. */
#define IX_RAMP_GAP_HI 6.0
/*  How far past or short of its ramp a city ramp's node starts the search,
 *  along its lane, in tiles. */
#define IX_RAMP_SIDE 0.75
/*  How long the road of a split or a join runs along its lane, in tiles.
 *  On it an exit lane widens, or an entry lane narrows.  The least, the most
 *  and where the search starts.  Every pair of one lane runs one length. */
#define IX_PAIR_MIN 0.5
#define IX_PAIR_MAX 2.0
#define IX_PAIR0    0.5
/*  How far a left turn swings out past the middle, in tiles: the most, and
 *  where the search starts. */
#define IX_SWING_HI 2.0
#define IX_SWING0   1.0
/*  A cloverleaf loop's radius, in tiles: the least, the most, and where
 *  the search starts. */
#define IX_LOOP_R_LO 0.5
#define IX_LOOP_R_HI 2.0
#define IX_LOOP_R0   1.0
/*  A loop's radius this close under the least is the least.  The oval is
 *  solved from its lanes' ends, and they are found to about this. */
#define IX_LOOP_R_TOL 1e-4
/*  The score's weight for a tile by which a loop's radius falls short of
 *  the least. */
#define IX_W_MISS 200.0
/*  How far a left turn's middle moves along the way it turns, either way, in
 *  tiles at most. */
#define IX_SHIFT_HI 1.5
/*  How far a city ramp's strand reaches back along the ramp's road, in
 *  tiles.  The most, the share of that road it may take, and where the
 *  search starts. */
#define IX_REACH_HI    3.0
#define IX_REACH_SHARE 0.9
#define IX_REACH0      2.0
/*  How far a city ramp's strand runs on beside the highway before it
 *  bends away, in tiles.  Where the search starts, the most, and how far
 *  it runs where there is room.  The score's weight for a tile short of
 *  that. */
#define IX_RUN0   1.0
#define IX_RUN_HI 2.5
#define IX_RUN    1.5
#define IX_W_RUN  2.0
/*  How far apart the samples along a strand lie, in tiles.  Two strands
 *  closer than their half widths less IX_TOL lie in one place.  A strand lies
 *  on its own lane up to IX_PEEL from the end it peels at. */
#define IX_DS   0.15
#define IX_TOL  0.05
#define IX_PEEL 0.8
/*  The score's weights.  One weighs a degree over the grade, one a tile
 *  of strand over or under another.  One weighs a radian of turning
 *  beyond a strand's own turn.  The sharpest curve counts as one over
 *  its radius in tiles. */
#define IX_W_GRADE  400.0
#define IX_W_OVER   0.3
#define IX_W_CURVE  10.0
#define IX_W_WIGGLE 1.0
/*  And a strand that runs against its own traffic, which the shape never
 *  keeps (plans/interchanges.md, "What the shape keeps"). */
#define IX_W_BACK 10000.0
/*  The radius a turn, left or right, keeps where there is room, in tiles
 *  (plans/interchanges.md, "The shape").  Also the score's weight for
 *  each one over a radius of a tile by which it bends tighter. */
#define IX_TURN_R   1.0
#define IX_W_RADIUS 60.0
/*  How far under the greatest grade a layout aims, in degrees.  The steepest
 *  grade the height solve tries, in levels a tile. */
#define IX_MARGIN 0.5
#define IX_G_MOST 8.0
/*  The greatest grade of a city ramp's strand, in degrees: a level a tile, as
 *  a ramp's own cell climbs (plans/interchanges.md, "What the shape keeps").
 *  Every other way of a region keeps MAX_GRADE. */
#define IX_RAMP_GRADE 26.565
/*  The search: candidates for each place, the most candidates, and the
 *  generations. */
#define IX_POP_PER 3
#define IX_POP_MAX 48
#define IX_GENS    40
/*  The search ends once its best has not improved for this many
 *  generations.  A search again after a wish is dropped starts from the
 *  best found before, and has this many generations. */
#define IX_STALL      10
#define IX_RETRY_GENS 5
/*  The polish after the search: its first step, as a share of each place's
 *  range, and how many times the step halves. */
#define IX_POLISH_STEP   0.2
#define IX_POLISH_ROUNDS 4
/*  The halvings of the grade while the search runs, and for the layout
 *  filed. */
#define IX_HALVE_SEARCH 7
#define IX_HALVE_FILED  20

/*  What a region's road is, for its shape and its level. */
enum
{
    IX_BUNDLE = 0, /* the inner lanes straight on */
    IX_ALONG,      /* a piece of an outermost lane, between two of its nodes or its gore */
    IX_NEAR,       /* a turn to the side the world drives on */
    IX_FAR,        /* a turn across the other way: a left turn */
    IX_RAMP,       /* a city ramp's strand */
    IX_WEAVE,      /* a cloverleaf's weave, along its carriageway through the crossing */
    IX_LOOP,       /* a cloverleaf's left turn, three quarters of a turn the other way */
    IX_OTHER
};

/*  An arm of a region: its gore, its road by its contact there and its
 *  highway.  Also the point of its line at the gore, and the heading in. */
typedef struct
{
    int    junction, road, contact, hw;
    double x, y, hin;
    int    out;      /* the place that holds how far out along it its gore lies, else -1 */
    int    ap0, apn; /* its approach's samples in s_xap, from the farthest to the nearest its gore */
    double gfloor;   /* the floor its deck keeps at its gore */
} IxArm;

/*  A node of a region, by its junction.  A node of an outermost lane
 *  also has its chain, its order from the gore and its arm.  It has the
 *  place that holds its gap, and its lane's middle at the gore.  The
 *  rest have place -1. */
typedef struct
{
    int    junction, place, road, contact, leaving, order, arm, part; /* part: its NNODE_* */
    Pose   base;
    double pin; /* the height of the road a gore meets, else NAN */
    /*  A ramp's gore has the ramp's road, by its contact here, and its
     *  lane.  It has the place that holds how far back along that road
     *  the strand reaches. */
    int ramp_road, ramp_contact, ramp_lane, reach;
    /*  A weave's node has the loop that places it, else -1.  Its place
     *  holds the loop's radius, and `din` is how far it lies from the
     *  gore along its lane. */
    int    loop;
    double din;
} IxNode;

/*  A road of a region: its kind and level, its turn's arms and the place
 *  of a left turn's swing.  Also where its traffic lies across its line,
 *  half its width, its line and its samples. */
typedef struct
{
    int    road, kind, arm, from_arm, to_arm, swing, ok, s0, n;
    int    link[2], link_end[2]; /* the lane its own lane links with at each end, on another road, else -1 */
    int    run;                  /* a city ramp's strand: the place that holds how far it runs on beside the highway, else -1 */
    int    shift;                /* a left turn: the place that holds how far its middle moves along the turn, else -1 */
    double miss;                 /* a loop: how far either of its radii falls short of IX_LOOP_R_LO, in tiles */
    double gap_short;            /* a loop: how far its weaves' nodes fall short of IX_GAP_LO past the nodes before them */
    int    ends;                 /* a loop: 1 where its weaves' nodes are placed for the places just laid */
    double rank, c, half, kmax, wiggle;
    int    back; /* 1 where it runs against its own traffic somewhere */
    Way    w;
} IxRoad;

/*  A sample of a road's line: its point, its `s` and its floor there.  A
 *  city ramp's floor is the ground.  A highway's floor is s_xlift over it:
 *  the floor of its deck, CLEARANCE, unless the search gives way. */
typedef struct
{
    double x, y, s, floor, h; /* h: the line's heading there */
} IxSample;

/*  A difference: the height of `v` is at most that of `u` plus w0 and wg
 *  times the grade. */
typedef struct
{
    int    u, v;
    double w0, wg;
} IxEdge;

/*  The pairs of samples of two roads that lie in one place.  Sorted by the
 *  two roads. */
typedef struct
{
    int a, p, b, q;
} IxHit;
static _Thread_local IxHit *s_xh;
static _Thread_local int    s_nxh, s_cxh;

/*  How far back from its gore an arm's deck takes part in the region's
 *  height solve, in tiles (plans/interchanges.md, "The levels"). */
#define IX_APPROACH 4.0

/*  A sample of an arm's approach: its road, its `s` along it and the floor a
 *  deck keeps there.  The approach runs back from the gore along the arm's
 *  road, and on along each highway road that carries its way on. */
typedef struct
{
    int    road, v; /* v: its height's place among the approaches' heights */
    double s, floor;
} IxAp;
static _Thread_local IxAp *s_xap;
static _Thread_local int   s_nxap, s_cxap, s_nxapv; /* s_nxapv: how many heights the approaches have */

static _Thread_local IxArm         *s_xa;
static _Thread_local IxNode        *s_xn;
static _Thread_local IxRoad        *s_xr;
static _Thread_local IxSample      *s_xs;
static _Thread_local IxEdge        *s_xe;
static _Thread_local double        *s_plo, *s_phi, *s_p0;
static _Thread_local unsigned char *s_ppair; /* per place, 1 for a gap that holds a pair's length */
static _Thread_local double        *s_xz;    /* the heights, one a variable */
static _Thread_local int            s_cxz;
static _Thread_local double         s_wradius = IX_W_RADIUS; /* the weight of a turn's radius in the score, nought once it gives way */
static _Thread_local double         s_xlift   = CLEARANCE;   /* how far a highway's floor stands over the ground, less once it gives way */
static _Thread_local int            s_nxa, s_cxa, s_nxn, s_cxn, s_nxr, s_cxr, s_nxs, s_cxs, s_nxe, s_cxe, s_np, s_cp;
static _Thread_local double         s_xmid[2]; /* the middle of the region's gores */
static int                         *s_xph;     /* the ends of the regions the search cannot route: placeholders */
static int                          s_nxph, s_cxph;
static _Thread_local double         s_xover;  /* the length of strand over or under another */
static _Thread_local int            s_xshort; /* the crossings short of a level, in a check */

/*  The search's own arrays of this thread, freed. */
static void ix_scratch_free(void)
{
    free(s_xa), free(s_xn), free(s_xr), free(s_xs), free(s_xe), free(s_plo), free(s_phi), free(s_p0), free(s_xz), free(s_xh), free(s_ppair);
    free(s_xap), s_xap = NULL, s_nxap = s_cxap = 0;
    s_ppair = NULL;
    s_xa = NULL, s_xn = NULL, s_xr = NULL, s_xs = NULL, s_xe = NULL, s_plo = s_phi = s_p0 = s_xz = NULL, s_xh = NULL;
    s_cxa = s_cxn = s_cxr = s_cxs = s_cxe = s_cp = s_cxz = s_cxh = 0;
    s_nxa = s_nxn = s_nxr = s_nxs = s_nxe = s_np = s_nxh = 0;
}

static void ix_free(void)
{
    ix_scratch_free();
    free(s_xph);
    s_xph = NULL, s_nxph = s_cxph = 0;
}

/*  The pose at `s` along a way. */
static void way_at(const Way *w, double s, Pose *o)
{
    int q = 0;
    while (q + 1 < w->n && s > w->g[q + 1].s)
        q++;
    piece_at(&w->g[q], fmin(fmax(s - w->g[q].s, 0.0), w->g[q].length), o, NULL);
}

/*  A way goes on with the pieces of another.  0, or -1 where they do not fit. */
static int way_cat(Way *w, const Way *b)
{
    int q;
    if (w->n + b->n > 24)
        return -1;
    for (q = 0; q < b->n; q++)
    {
        SGeom g      = b->g[q];
        g.s          = (float)(w->len + b->g[q].s);
        w->g[w->n++] = g;
    }
    w->len += b->len;
    return 0;
}

/*  The region of a region's road: the region of a cell of the end of a
 *  junction it reaches.  -1 for none. */
static int ix_region_of(int road)
{
    const NRoad *r = network_road(road);
    int          q, k;
    for (q = 0; q < 2; q++)
    {
        const NLink *lk = q ? &r->succ : &r->pred;
        if (lk->kind != NLINK_JUNCTION || lk->index < 0)
            continue;
        const TopoEnd *te = topology_end(network_junction(lk->index)->end);
        for (k = 0; k < te->ncell; k++)
        {
            int e = network_cell_region(topology_cell(te->cell0 + k));
            if (e >= 0)
                return e;
        }
    }
    return -1;
}

/*  The node of junction `j` in the region, else -1. */
static int ix_node(int j)
{
    int k;
    for (k = 0; k < s_nxn; k++)
        if (s_xn[k].junction == j)
            return k;
    return -1;
}

/*  The arm of the region whose gore is junction `j`, else -1. */
static int ix_arm(int j);

/*  The node of arm `a`'s gore, else -1. */
static int ix_arm_node(int a)
{
    int k;
    for (k = 0; k < s_nxn; k++)
        if (s_xn[k].junction == s_xa[a].junction && s_xn[k].reach < 0)
            return k;
    return -1;
}

static int ix_arm(int j)
{
    int k;
    for (k = 0; k < s_nxa; k++)
        if (s_xa[k].junction == j)
            return k;
    return -1;
}

/*  A new place: its range and where the search starts.  Its index, or -1. */
static int ix_place(double lo, double hi, double x0)
{
    int cap = s_cp;
    if (arc_grow((void **)&s_plo, &s_cp, s_np + 1, sizeof *s_plo) != 0)
        return -1;
    if (s_cp != cap)
    {
        unsigned char *q = (unsigned char *)realloc(s_ppair, (size_t)s_cp);
        if (!q)
            return -1;
        s_ppair = q;
    }
    s_ppair[s_np] = 0;
    s_phi         = (double *)realloc(s_phi, sizeof *s_phi * (size_t)s_cp);
    s_p0          = (double *)realloc(s_p0, sizeof *s_p0 * (size_t)s_cp);
    if (!s_phi || !s_p0)
        return -1;
    s_plo[s_np] = lo, s_phi[s_np] = hi, s_p0[s_np] = x0;
    return s_np++;
}

/*  Where the traveled lanes of a road lie across its line at its start:
 *  their middle and half their width. */
static void ix_across(int road, double *c, double *half)
{
    const NSection *x  = network_section(network_road(road)->sec0);
    double          lo = 1e9, hi = -1e9;
    int             k;
    for (k = 0; k < x->nlane; k++)
    {
        int   l = x->lane0 + k, t = network_lane(l)->type;
        float ti, to;
        if (t != NLANE_DRIVING && t != NLANE_CONNRAMP)
            continue;
        network_lane_t(l, 0.0f, &ti, &to);
        lo = fmin(lo, fmin(ti, to)), hi = fmax(hi, fmax(ti, to));
    }
    if (lo > hi)
        lo = hi = 0.0;
    *c = 0.5 * (lo + hi), *half = 0.5 * (hi - lo);
}

/*  THE ROADS OF REGION `e`, gathered: its arms, its nodes, its roads with
 *  their kinds and levels, and its places.  The count of its roads, or -1
 *  out of memory. */
static void   ix_ramp_pose(const IxNode *n, const double *p, Pose *o);
static double ix_node_in(int k, const double *p);

static int ix_gather(int e)
{
    int i, k, q, clover;
    s_nxa = s_nxn = s_nxr = s_np = 0;
    for (i = 0; i < s_nroad; i++)
    {
        if (!rg_road(i) || ix_region_of(i) != e)
            continue;
        if (arc_grow((void **)&s_xr, &s_cxr, s_nxr + 1, sizeof *s_xr) != 0)
            return -1;
        memset(&s_xr[s_nxr], 0, sizeof s_xr[s_nxr]);
        s_xr[s_nxr].road = i, s_xr[s_nxr].swing = -1, s_xr[s_nxr].run = s_xr[s_nxr].shift = -1, s_xr[s_nxr].arm = s_xr[s_nxr].from_arm = s_xr[s_nxr].to_arm = -1;
        s_nxr++;
        for (q = 0; q < 2; q++)
        {
            const NLink *lk = q ? &network_road(i)->succ : &network_road(i)->pred;
            if (lk->kind != NLINK_JUNCTION || ix_node(lk->index) >= 0)
                continue;
            if (arc_grow((void **)&s_xn, &s_cxn, s_nxn + 1, sizeof *s_xn) != 0)
                return -1;
            memset(&s_xn[s_nxn], 0, sizeof s_xn[s_nxn]);
            s_xn[s_nxn].junction = lk->index, s_xn[s_nxn].place = -1, s_xn[s_nxn].arm = -1, s_xn[s_nxn].loop = -1;
            s_xn[s_nxn].ramp_road = s_xn[s_nxn].ramp_lane = s_xn[s_nxn].reach = -1;
            s_nxn++;
        }
    }
    if (!s_nxr)
        return 0;
    /*  The arms: each gore of the band, by the road of the band that reaches
     *  it. */
    for (k = 0; k < s_nxn; k++)
    {
        const TopoEnd *te = topology_end(network_junction(s_xn[k].junction)->end);
        if (te->kind != END_GORE || te->net != RNET_BAND)
            continue;
        for (i = 0; i < s_nroad; i++)
        {
            const NRoad *r = network_road(i);
            int          c = r->pred.kind == NLINK_JUNCTION && r->pred.index == s_xn[k].junction   ? 0
                             : r->succ.kind == NLINK_JUNCTION && r->succ.index == s_xn[k].junction ? 1
                                                                                                   : -1;
            Arm          am;
            if (c < 0 || r->topo < 0 || r->net != RNET_BAND || arm_of(i, c, &am) != 0)
                continue;
            if (arc_grow((void **)&s_xa, &s_cxa, s_nxa + 1, sizeof *s_xa) != 0)
                return -1;
            /*  The arm's way out of the junction, turned, is the way in. */
            s_xa[s_nxa] = (IxArm){s_xn[k].junction, i, c, -1, am.px, am.py, atan2(-am.dy, -am.dx), -1, 0, 0, 0.0};
            /*  The cut reaches out along an arm with a line, short of its
             *  other end.  It stops short of half way where a region's gore
             *  is there too. */
            if (s_road[i].ngeom && s_road[i].length > 1e-3f)
            {
                const NLink *far  = c ? &r->pred : &r->succ;
                int          both = far->kind == NLINK_JUNCTION && far->index >= 0 &&
                                    topology_end(network_junction(far->index)->end)->kind == END_GORE;
                double       most = fmin(IX_GORE_OUT, (both ? 0.45 : 0.9) * s_road[i].length);
                if ((s_xa[s_nxa].out = ix_place(0.0, most, 0.5 * most)) < 0)
                    return -1;
            }
            s_nxa++;
            break;
        }
    }
    s_xmid[0] = s_xmid[1] = 0.0;
    for (k = 0; k < s_nxa; k++)
        s_xmid[0] += s_xa[k].x / s_nxa, s_xmid[1] += s_xa[k].y / s_nxa;
    /*  The highways: two arms whose ways in are opposite are one. */
    for (q = 0, k = 0; k < s_nxa; k++)
    {
        int b;
        if (s_xa[k].hw >= 0)
            continue;
        s_xa[k].hw = q;
        for (b = k + 1; b < s_nxa; b++)
            if (s_xa[b].hw < 0 && fabs(remainder(s_xa[k].hin - s_xa[b].hin - M_PI, 2.0 * M_PI)) < 0.5)
                s_xa[b].hw = q;
        q++;
    }
    /*  The nodes of the outermost lanes, each with the place of its gap. */
    for (k = 0; k < s_nxn; k++)
    {
        IxNode *n = &s_xn[k];
        int     part, rank, lane, a;
        n->loop = -1;
        if (!network_node_chain(n->junction, &n->road, &n->contact, &n->leaving, &part, &rank, &n->order) ||
            (lane = rg_outer(n->road, n->contact, n->leaving)) < 0 || lane_mid(lane, n->contact, &n->base) != 0)
            continue;
        n->part = part;
        for (a = 0; a < s_nxa; a++)
            if (s_xa[a].road == n->road && s_xa[a].contact == n->contact)
                n->arm = a;
        if ((n->place = part == NNODE_WEAVE  ? ix_place(IX_LOOP_R_LO, IX_LOOP_R_HI, IX_LOOP_R0)
                        : part == NNODE_RAMP ? ix_place(IX_GAP_LO, IX_RAMP_GAP_HI, IX_GAP0)
                                             : ix_place(IX_GAP_LO, IX_GAP_HI, IX_GAP0)) < 0)
            return -1;
    }
    /*  The lane each road's own lane links with at each end, and the height
     *  each gore is pinned to: its arm's, or a ramp's road's. */
    for (k = 0; k < s_nxn; k++)
    {
        int a = ix_arm(s_xn[k].junction);
        Arm am;
        s_xn[k].pin = NAN;
        if (a >= 0 && arm_of(s_xa[a].road, s_xa[a].contact, &am) == 0)
        {
            /*  An arm with no deck of its own, a highway that ends at the
             *  gore, stands where a deck would: CLEARANCE over the ground. */
            s_xn[k].pin = arm_height(&am);
            s_xn[k].pin = isnan(s_xn[k].pin) ? shape_land_at(am.px, am.py) + CLEARANCE : s_xn[k].pin;
        }
    }
    for (i = 0; i < s_nxr; i++)
        s_xr[i].link[0] = s_xr[i].link[1] = -1;
    for (k = 0; k < network_links(); k++)
    {
        const NLaneLink *lk = network_link(k);
        for (q = 0; q < 2; q++)
        {
            int          l = q ? lk->to : lk->from, le = q ? lk->to_end : lk->from_end, o = q ? lk->from : lk->to, oe = q ? lk->from_end : lk->to_end;
            const NRoad *ro = network_road(network_lane(o)->road);
            for (i = 0; i < s_nxr; i++)
                if (network_lane(l)->road == s_xr[i].road && l == rg_main(s_xr[i].road) && network_lane(o)->road != s_xr[i].road &&
                    s_xr[i].link[le] < 0)
                    s_xr[i].link[le] = o, s_xr[i].link_end[le] = oe;
            /*  A ramp's road at a ramp's gore of the region: how far the
             *  strand may reach back along it. */
            if (ro->topo >= 0 && ro->net == RNET_ROAD && s_road[network_lane(o)->road].ngeom)
            {
                int j = oe ? ro->succ.index : ro->pred.index, nd, kind = oe ? ro->succ.kind : ro->pred.kind;
                if (kind == NLINK_JUNCTION && (nd = ix_node(j)) >= 0 && s_xn[nd].ramp_lane < 0 && ix_arm(j) < 0)
                {
                    double most        = fmin(IX_REACH_HI, IX_REACH_SHARE * s_road[network_lane(o)->road].length);
                    s_xn[nd].ramp_road = network_lane(o)->road, s_xn[nd].ramp_contact = oe, s_xn[nd].ramp_lane = o;
                    if ((s_xn[nd].reach = ix_place(0.0, most, fmin(IX_REACH0, most))) < 0)
                        return -1;
                }
            }
        }
    }
    /*  A cloverleaf has weaves: there a right turn rides over the rest. */
    for (clover = 0, k = 0; k < s_nxn; k++)
        clover |= s_xn[k].place >= 0 && s_xn[k].part == NNODE_WEAVE;
    /*  Each road's kind and level.  Each pair along a lane runs
     *  IX_PAIR_MIN at least: its gap is that of its node farther from
     *  the gore. */
    for (i = 0; i < s_nxr; i++)
    {
        IxRoad       *x = &s_xr[i];
        const NRoad  *r = network_road(x->road);
        int           a = ix_node(r->pred.index), b = ix_node(r->succ.index);
        const IxNode *na = a >= 0 ? &s_xn[a] : NULL, *nb = b >= 0 ? &s_xn[b] : NULL;
        int           ga = na ? ix_arm(na->junction) : -1, gb = nb ? ix_arm(nb->junction) : -1;
        int           ca = na && na->place >= 0, cb = nb && nb->place >= 0;
        ix_across(x->road, &x->c, &x->half);
        if (rg_main(x->road) < 0)
        {
            x->kind = IX_BUNDLE, x->arm = ga, x->rank = ga >= 0 ? s_xa[ga].hw : 0.0;
            continue;
        }
        if ((ca && cb && na->road == nb->road && na->contact == nb->contact && na->leaving == nb->leaving && !rg_from_side(x->road)) ||
            (ga >= 0 && cb && !nb->leaving && rg_from_outer(x->road, 0)) || (gb >= 0 && ca && na->leaving && rg_from_outer(x->road, 1)))
        {
            const NSection *sec = network_section(r->sec0);
            int             q2, pair = 0, far;
            x->kind = IX_ALONG, x->arm = ca ? na->arm : nb->arm;
            x->rank = x->arm >= 0 ? s_xa[x->arm].hw : 0.0;
            for (q2 = 0; q2 < sec->nlane; q2++)
                pair |= nlane_is_change(network_lane(sec->lane0 + q2)->type);
            far = ca && cb ? (na->order > nb->order ? a : b) : ca ? a
                                                                  : b;
            /*  Every pair of one lane, the ramp's and the turns', takes the
             *  one place that holds the lane's pair length. */
            if (pair && ca && cb && s_xn[far].place >= 0)
            {
                int q3, shared = -1;
                for (q3 = 0; q3 < s_nxn; q3++)
                    if (q3 != far && s_xn[q3].place >= 0 && s_ppair[s_xn[q3].place] && s_xn[q3].road == s_xn[far].road &&
                        s_xn[q3].contact == s_xn[far].contact && s_xn[q3].leaving == s_xn[far].leaving)
                        shared = s_xn[q3].place;
                if (shared >= 0)
                    s_xn[far].place = shared;
                else
                {
                    s_plo[s_xn[far].place] = IX_PAIR_MIN, s_phi[s_xn[far].place] = IX_PAIR_MAX, s_p0[s_xn[far].place] = IX_PAIR0;
                    s_ppair[s_xn[far].place] = 1;
                }
            }
            continue;
        }
        if ((r->pred.kind == NLINK_JUNCTION && topology_end(network_junction(r->pred.index)->end)->net == RNET_ROAD) ||
            (r->succ.kind == NLINK_JUNCTION && topology_end(network_junction(r->succ.index)->end)->net == RNET_ROAD))
        {
            x->kind = IX_RAMP, x->rank = -1.0;
            if ((x->run = ix_place(0.0, IX_RUN_HI, IX_RUN0)) < 0)
                return -1;
            continue;
        }
        /*  A cloverleaf's weave runs from its node on the way in to its node on
         *  the way out, and a loop the other way. */
        if (ca && cb && na->part == NNODE_WEAVE && nb->part == NNODE_WEAVE)
        {
            x->arm = na->arm;
            if (!na->leaving && nb->leaving)
                x->kind = IX_WEAVE, x->rank = na->arm >= 0 ? s_xa[na->arm].hw : 0.0;
            else
            {
                /*  The loop's radius is the place of the node it starts at,
                 *  and it places both nodes. */
                x->kind = IX_LOOP, x->rank = 0.5, x->from_arm = na->arm, x->to_arm = nb->arm;
                s_xn[b].place = s_xn[a].place, s_xn[a].loop = s_xn[b].loop = i;
            }
            continue;
        }
        if (ca && cb && !na->leaving && nb->leaving && na->arm >= 0 && nb->arm >= 0 && na->arm != nb->arm)
        {
            const IxArm *pa = &s_xa[na->arm], *pb = &s_xa[nb->arm];
            /*  The side the arriving lanes lie on, and the turn: a turn to that
             *  side is near. */
            double side = cos(pa->hin) * (na->base.y - pa->y) - sin(pa->hin) * (na->base.x - pa->x);
            double turn = remainder(pb->hin + M_PI - pa->hin, 2.0 * M_PI);
            x->from_arm = na->arm, x->to_arm = nb->arm;
            if (turn * side > 0.0)
                x->kind = IX_NEAR, x->rank = clover ? 3.0 : 0.5;
            else
            {
                x->kind = IX_FAR, x->rank = 2.0 + pa->hw;
                if ((x->swing = ix_place(0.0, IX_SWING_HI, IX_SWING0)) < 0 || (x->shift = ix_place(-IX_SHIFT_HI, IX_SHIFT_HI, 0.0)) < 0)
                    return -1;
            }
            continue;
        }
        x->kind = IX_OTHER, x->arm = ca ? na->arm : cb    ? nb->arm
                                                : ga >= 0 ? ga
                                                          : gb;
        x->rank = x->arm >= 0 ? s_xa[x->arm].hw : 0.0;
    }
    /*  Each city ramp's node starts the search on its ramp's own side.  An
     *  on ramp joins a lane on the way in past its ramp, nearer the middle.
     *  An off ramp leaves a lane on the way out there too.  The other
     *  two lie short of it, nearer the gore.  The distance is along the
     *  arm, from the gore where it starts. */
    for (i = 0; i < s_nxr; i++)
    {
        const IxRoad *x = &s_xr[i];
        const NRoad  *r = network_road(x->road);
        int           a = ix_node(r->pred.index), b = ix_node(r->succ.index), on, rn, cn, past, place;
        if (x->kind != IX_RAMP || a < 0 || b < 0)
            continue;
        on = s_xn[a].reach >= 0, rn = on ? a : b, cn = on ? b : a;
        if (s_xn[rn].reach < 0 || (place = s_xn[cn].place) < 0 || s_xn[cn].part != NNODE_RAMP || s_xn[cn].arm < 0)
            continue;
        past = on ? !s_xn[cn].leaving : s_xn[cn].leaving;
        {
            const IxArm *g  = &s_xa[s_xn[cn].arm];
            double       ux = cos(g->hin), uy = sin(g->hin), out = g->out >= 0 ? s_p0[g->out] : 0.0, at, prior, want;
            Pose         rp;
            ix_ramp_pose(&s_xn[rn], s_p0, &rp);
            at          = (rp.x - (g->x - ux * out)) * ux + (rp.y - (g->y - uy * out)) * uy;
            prior       = ix_node_in(cn, s_p0) - s_p0[place];
            want        = (past ? at + IX_RAMP_SIDE : at - IX_RAMP_SIDE) - prior;
            s_p0[place] = fmin(s_phi[place], fmax(s_plo[place], want));
        }
    }
    return s_nxr;
}

static int ix_lane_mid(int lane, int end, const double *p, Pose *o);

/*  The bundle beside the chain of node `k`, else -1. */
static int ix_chain_bundle(int k)
{
    const IxNode *n = &s_xn[k];
    int           q, b = -1;
    for (q = 0; n->arm >= 0 && q < s_nxr; q++)
        if (s_xr[q].kind == IX_BUNDLE && (network_road(s_xr[q].road)->pred.index == s_xa[n->arm].junction ||
                                          network_road(s_xr[q].road)->succ.index == s_xa[n->arm].junction))
            b = q;
    return b;
}

/*  The place `d` along the outermost lane of node `k`'s chain from the
 *  gore, for the places `p`.  It lies `out` farther across from the
 *  bundle than that lane.  Where the arm goes on straight, the lane runs
 *  beside the bundle's line, as far across from it as at the gore.  So
 *  the lanes straight on bend as one.  It heads the way the lane's
 *  traffic runs. 0, or -1 where it is no node of a lane, or where its
 *  bundle has no line yet. */
static int ix_chain_at(int k, double d, double out, const double *p, Pose *o)
{
    const IxNode *n = &s_xn[k];
    int           b;
    if (n->place < 0 || ix_lane_mid(rg_outer(n->road, n->contact, n->leaving), n->contact, p, o) != 0)
        return -1;
    if ((b = ix_chain_bundle(k)) < 0)
    {
        /*  With no bundle the lane runs straight in, and across is to its
         *  right as it runs in. */
        double ux = cos(o->hdg) * (n->leaving ? -1.0 : 1.0), uy = sin(o->hdg) * (n->leaving ? -1.0 : 1.0);
        o->x += ux * d + uy * out * (n->leaving ? -1.0 : 1.0), o->y += uy * d - ux * out * (n->leaving ? -1.0 : 1.0);
        return 0;
    }
    if (!s_xr[b].ok)
        return -1;
    {
        /*  The bundle's line from this gore inward, and the lane across it. */
        int    start = network_road(s_xr[b].road)->pred.index == s_xa[n->arm].junction;
        double len   = s_xr[b].w.len, ux, uy, lat;
        Pose   g, at;
        way_at(&s_xr[b].w, start ? 0.0 : len, &g);
        ux = start ? cos(g.hdg) : -cos(g.hdg), uy = start ? sin(g.hdg) : -sin(g.hdg);
        lat = ux * (o->y - g.y) - uy * (o->x - g.x);
        lat += lat < 0.0 ? -out : out;
        way_at(&s_xr[b].w, start ? fmin(fmax(d, 0.0), len) : fmax(len - fmax(d, 0.0), 0.0), &at);
        ux = start ? cos(at.hdg) : -cos(at.hdg), uy = start ? sin(at.hdg) : -sin(at.hdg);
        o->x = at.x - uy * lat, o->y = at.y + ux * lat;
        o->hdg = n->leaving ? atan2(-uy, -ux) : atan2(uy, ux);
    }
    return 0;
}

/*  How far node `k` lies from its gore along its lane: the gaps of its own
 *  and every node before it.  A weave's node lies where its loop places
 *  it, past the nodes before it. */
static double ix_node_in(int k, const double *p)
{
    const IxNode *n = &s_xn[k];
    double        d = 0.0;
    int           q;
    for (q = 0; q < s_nxn; q++)
        if (s_xn[q].place >= 0 && s_xn[q].loop < 0 && s_xn[q].road == n->road && s_xn[q].contact == n->contact &&
            s_xn[q].leaving == n->leaving && s_xn[q].order <= n->order)
            d += p[s_xn[q].place];
    if (n->loop >= 0)
        return s_xr[n->loop].ends ? n->din : d + IX_GAP_LO;
    return d;
}

/*  The nodes loop `i` meets, for the places `p`.  Each lies where a
 *  circle of the loop's radius touches the outer lane of a weave.  The
 *  circle lies in the corner between the two carriageways.  Each lane is
 *  taken as the straight line of its pose, and the pose is found again at
 *  the touch until it holds.  A lane runs no farther than its bundle.
 *  Past either end of the bundle, the lane goes on as the straight line
 *  of its pose there.  The bundles must have their lines. */
static void ix_loop_ends(int i, const double *p)
{
    IxRoad      *x = &s_xr[i];
    const NRoad *r = network_road(x->road);
    int          a = ix_node(r->pred.index), b = ix_node(r->succ.index), it;
    double       rad, da, db, ba, bb, lw, step = 1.0, la = INFINITY, lb = INFINITY;
    float        ti, to;
    if (a < 0 || b < 0 || s_xn[a].place < 0)
        return;
    rad          = p[s_xn[a].place];
    s_xr[i].ends = 0, ba = ix_node_in(a, p) - IX_GAP_LO, bb = ix_node_in(b, p) - IX_GAP_LO;
    network_lane_t(rg_main(x->road), 0.0f, &ti, &to), lw = fabs((double)to - (double)ti);
    if (ix_chain_bundle(a) >= 0)
        la = s_xr[ix_chain_bundle(a)].w.len;
    if (ix_chain_bundle(b) >= 0)
        lb = s_xr[ix_chain_bundle(b)].w.len;
    da = ba + rad, db = bb + rad;
    for (it = 0; it < 32 && step > 1e-9; it++)
    {
        Pose   pa, pb;
        double d, sg, uax, uay, ubx, uby, nax, nay, nbx, nby, rx, ry, det, ta, tb;
        if (ix_chain_at(a, da, lw, p, &pa) != 0 || ix_chain_at(b, db, lw, p, &pb) != 0)
            return;
        /*  The loop turns the way the oval does.  Its center lies the
         *  radius to that side of both lines: t along each line from its
         *  pose, by one linear system. */
        d = remainder(pb.hdg - pa.hdg, 2.0 * M_PI), sg = d > 0.0 ? -1.0 : 1.0;
        uax = cos(pa.hdg), uay = sin(pa.hdg), ubx = cos(pb.hdg), uby = sin(pb.hdg);
        nax = -sg * uay, nay = sg * uax, nbx = -sg * uby, nby = sg * ubx;
        rx = pb.x - pa.x + rad * (nbx - nax), ry = pb.y - pa.y + rad * (nby - nay);
        det = -uax * uby + uay * ubx;
        if (fabs(det) < 1e-6)
            break;
        /*  The loop leaves its first lane against the way that lane's
         *  places count, as its traffic runs out toward the gore. */
        ta = (-rx * uby + ry * ubx) / det, tb = (uax * ry - uay * rx) / det;
        step = fabs(fmin(fmax(da, 0.0), la) - ta - da) + fabs(fmin(fmax(db, 0.0), lb) + tb - db);
        da = fmin(fmax(da, 0.0), la) - ta, db = fmin(fmax(db, 0.0), lb) + tb;
    }
    x->gap_short = fmax(0.0, ba + IX_GAP_LO - da) + fmax(0.0, bb + IX_GAP_LO - db);
    s_xn[a].din = fmax(da, ba + IX_GAP_LO), s_xn[b].din = fmax(db, bb + IX_GAP_LO), x->ends = 1;
}

/*  The place of node `k` for the places `p`, on its outermost lane. */
static int ix_node_pose(int k, const double *p, Pose *o)
{
    return s_xn[k].place < 0 ? -1 : ix_chain_at(k, ix_node_in(k, p), 0.0, p, o);
}

/*  The middle of lane `lane` at its end `end`, heading the way its
 *  traffic runs.  It is read from the region's own line where its road
 *  is one of them, else from the shape.  An arm's lane at its gore lies as
 *  far out along the arm as the gore does for the places `p`.  0, or -1
 *  where its road has no line. */
static int ix_lane_mid(int lane, int end, const double *p, Pose *o)
{
    const NLane    *l = network_lane(lane);
    const NRoad    *r = network_road(l->road);
    const NSection *x = network_section(l->section);
    int             i;
    for (i = 0; p && i < s_nxa; i++)
        if (s_xa[i].out >= 0 && s_xa[i].road == l->road &&
            (s_xa[i].contact ? end == 1 && l->section == r->sec0 + r->nsec - 1 : end == 0 && l->section == r->sec0))
        {
            float  u  = end ? r->length : x->s, ti, to, px, py, h;
            double sl = s_xa[i].contact ? s_road[l->road].length - p[s_xa[i].out] : p[s_xa[i].out], mid;
            shape_at(l->road, (float)sl, &px, &py, &h);
            network_lane_t(lane, u - x->s, &ti, &to);
            mid  = 0.5 * (ti + to);
            o->x = px - sin(h) * mid, o->y = py + cos(h) * mid, o->z = shape_height(l->road, (float)sl);
            o->hdg = network_lane_with_s(lane) ? h : h + M_PI;
            return 0;
        }
    for (i = 0; i < s_nxr; i++)
        if (s_xr[i].road == l->road)
        {
            float  u = end ? (l->section + 1 < r->sec0 + r->nsec ? network_section(l->section + 1)->s : r->length) : x->s, ti, to;
            double h, mid;
            if (!s_xr[i].ok)
                return -1;
            way_at(&s_xr[i].w, r->length > 1e-6f ? u / r->length * s_xr[i].w.len : 0.0, o);
            network_lane_t(lane, u - x->s, &ti, &to);
            h = o->hdg, mid = 0.5 * (ti + to);
            o->x -= sin(h) * mid, o->y += cos(h) * mid, o->z = 0.0;
            o->hdg = network_lane_with_s(lane) ? h : h + M_PI;
            return 0;
        }
    return lane_mid(lane, end, o);
}

/*  The place of a ramp's gore for the places `p`.  It is the middle of
 *  the ramp's lane, the reach back along its road from its contact.  It
 *  heads the way its traffic runs, at the road's height there. */
static void ix_ramp_pose(const IxNode *n, const double *p, Pose *o)
{
    double s = p[n->reach], len = s_road[n->ramp_road].length;
    float  x, y, h, ti, to;
    s = n->ramp_contact ? len - s : s;
    shape_at(n->ramp_road, (float)s, &x, &y, &h);
    network_lane_t(n->ramp_lane, 0.0f, &ti, &to);
    o->x = x - sin(h) * 0.5 * (ti + to), o->y = y + cos(h) * 0.5 * (ti + to), o->z = shape_height(n->ramp_road, (float)s);
    o->hdg = network_lane_with_s(n->ramp_lane) ? h : h + M_PI;
}

/*  Where a region's road leaves (end 0) or arrives (end 1).  It is the
 *  middle of the lane its own lane links with there, else its node's
 *  place. -1 while that lane is one of a piece of an outermost lane or
 *  of a bundle, not laid yet. */
static int ix_end_pose(int i, int end, const double *p, Pose *o)
{
    const NRoad *r  = network_road(s_xr[i].road);
    int          nd = ix_node((end ? r->succ : r->pred).index), l = s_xr[i].link[end], k;
    if (nd >= 0 && s_xn[nd].reach >= 0)
    {
        ix_ramp_pose(&s_xn[nd], p, o);
        return 0;
    }
    if (l >= 0 && ix_lane_mid(l, s_xr[i].link_end[end], p, o) == 0)
        return 0;
    /*  A lane of a piece of an outermost lane, or of a bundle, not laid
     *  yet: the road waits for it. */
    for (k = 0; l >= 0 && k < s_nxr; k++)
        if (s_xr[k].road == network_lane(l)->road && (s_xr[k].kind == IX_ALONG || s_xr[k].kind == IX_BUNDLE || s_xr[k].kind == IX_WEAVE))
            return -1;
    return nd >= 0 ? ix_node_pose(nd, p, o) : -1;
}

/*  How much of a jog each of its two turns takes. */
#define IX_JOG_TURN 0.25

/*  A JOG from `a` to `b` where their headings are as one: a turn toward
 *  b's line, a straight, and a turn back onto it.  Each turn is two spirals,
 *  the curvature growing from nought and dying away again, over the share
 *  IX_JOG_TURN of the jog.  The angle of the straight is found by halving, so
 *  the jog steps across e over along J.  Else the way between them.  0, or
 *  -1 where there is none. */
static int way_jog(const Pose *a, const Pose *b, Way *w)
{
    double ux = cos(a->hdg), uy = sin(a->hdg), dx = b->x - a->x, dy = b->y - a->y;
    double J = dx * ux + dy * uy, e = -dx * uy + dy * ux, sg = e < 0.0 ? -1.0 : 1.0, L, lo = 0.0, hi = 1.2, th = 0.0, D = 0.0;
    int    it;
    Pose   o = {0.0, 0.0, 0.0, 0.0};
    if (fabs(remainder(b->hdg - a->hdg, 2.0 * M_PI)) > 1e-3 || J <= 1e-6)
        return way_between(a, b, w);
    memset(w, 0, sizeof *w);
    if (fabs(e) < 1e-6)
    {
        way_put(w, a, J, 0.0, 0.0);
        return w->n ? 0 : -1;
    }
    /*  A turn of angle th over two spirals of length L moves X along and Y
     *  across.  The straight between the two turns makes up the rest of J
     *  along, and the steeper th, the farther the jog steps across. */
    L = 0.5 * IX_JOG_TURN * J;
    for (it = 0; it < 40; it++)
    {
        Way    u;
        Pose   end;
        double X, Y;
        th = 0.5 * (lo + hi);
        memset(&u, 0, sizeof u);
        way_put(&u, &o, L, 0.0, th / L), way_put(&u, NULL, L, th / L, 0.0);
        way_end(&u, &end);
        X = end.x, Y = end.y;
        D = (J - 2.0 * X) / cos(th);
        if (D < 0.0 || 2.0 * Y + D * sin(th) > fabs(e))
            hi = th;
        else
            lo = th;
    }
    {
        double k = th / L;
        way_put(w, a, L, 0.0, sg * k), way_put(w, NULL, L, sg * k, 0.0);
        way_put(w, NULL, fmax(D, 0.0), 0.0, 0.0);
        way_put(w, NULL, L, 0.0, -sg * k), way_put(w, NULL, L, -sg * k, 0.0);
    }
    return 0;
}

/*  The line of a bundle, from the arm road's lanes at its two gores
 *  (plans/interchanges.md, "The shape").  Where its arms do not line up, it
 *  is one S from the one to the other. */
static int ix_bundle_way(int road, const double *p, Way *w)
{
    const NSection *x = network_section(network_road(road)->sec0);
    Pose            at[2];
    int             end, k;
    for (end = 0; end < 2; end++)
    {
        int found = 0;
        for (k = 0; k < x->nlane && !found; k++)
        {
            int   o, oe = 0, l = x->lane0 + k;
            Pose  q;
            float ti, to;
            if (network_lane(l)->type != NLANE_DRIVING || (o = rg_neighbor(l, end, &oe)) < 0 || ix_lane_mid(o, oe, p, &q) != 0)
                continue;
            network_lane_t(l, end ? 1e9f : 0.0f, &ti, &to);
            {
                double mid = 0.5 * (ti + to), h = network_lane_with_s(l) ? q.hdg : q.hdg + M_PI;
                at[end].x = q.x + sin(h) * mid, at[end].y = q.y - cos(h) * mid, at[end].z = q.z, at[end].hdg = h;
            }
            found = 1;
        }
        if (!found)
            return -1;
    }
    /*  One S from gore to gore: a turn toward the far arm's line and a turn
     *  back onto it. */
    return way_jog(&at[0], &at[1], w);
}

/*  EVERY ROAD'S LINE for the places `p`.  A bundle runs from gore to
 *  gore.  A piece of an outermost lane runs along it from node to node.
 *  Every other road is the transition curve from the middle of the lane
 *  it leaves to the middle of the lane it joins.  A left turn runs
 *  through a place beside the middle on its way.  A road waits for the
 *  lanes it links with. */
static void ix_ways(const double *p)
{
    int i, placed = 1, pass;
    for (i = 0; i < s_nxr; i++)
        s_xr[i].ok = 0, s_xr[i].ends = 0, s_xr[i].gap_short = 0.0;
    /*  The bundles first, then the nodes each loop places on the lanes
     *  beside them. */
    for (i = 0; i < s_nxr; i++)
        if (s_xr[i].kind == IX_BUNDLE)
            s_xr[i].ok = ix_bundle_way(s_xr[i].road, p, &s_xr[i].w) == 0;
    for (i = 0; i < s_nxr; i++)
        if (s_xr[i].kind == IX_LOOP)
            ix_loop_ends(i, p);
    for (pass = 0; placed && pass < 12; pass++)
        for (placed = 0, i = 0; i < s_nxr; i++)
        {
            IxRoad      *x = &s_xr[i];
            const NRoad *r = network_road(x->road);
            Pose         a, b;
            if (x->ok)
                continue;
            if (x->kind == IX_BUNDLE)
            {
                x->ok = ix_bundle_way(x->road, p, &x->w) == 0, placed += x->ok;
                continue;
            }
            if (x->kind == IX_WEAVE)
            {
                /*  Along its carriageway, beside the bundle's middle on its
                 *  way. */
                int    na = ix_node(r->pred.index), nb = ix_node(r->succ.index), bq;
                double half = 0.0;
                Pose   m;
                Way    w2;
                for (bq = 0; bq < s_nxr; bq++)
                    if (s_xr[bq].kind == IX_BUNDLE && s_xr[bq].ok && s_xn[na].arm >= 0 &&
                        (network_road(s_xr[bq].road)->pred.index == s_xa[s_xn[na].arm].junction ||
                         network_road(s_xr[bq].road)->succ.index == s_xa[s_xn[na].arm].junction))
                        half = 0.5 * s_xr[bq].w.len;
                {
                    /*  Its line lies between its two lanes: half a lane farther
                     *  out than the outermost lane it goes on from. */
                    float  ti, to;
                    double out;
                    network_lane_t(rg_main(x->road), 0.0f, &ti, &to), out = 0.5 * fabs((double)to - (double)ti);
                    if (na < 0 || nb < 0 || half <= 0.0 || ix_chain_at(na, ix_node_in(na, p), out, p, &a) != 0 ||
                        ix_chain_at(nb, ix_node_in(nb, p), out, p, &b) != 0 || ix_chain_at(na, half, out, p, &m) != 0 ||
                        way_between(&a, &m, &x->w) != 0 || way_between(&m, &b, &w2) != 0 || way_cat(&x->w, &w2) != 0)
                        continue;
                }
                x->ok = 1, placed++;
                continue;
            }
            if (x->kind == IX_ALONG)
            {
                int na = ix_node(r->pred.index), nb = ix_node(r->succ.index);
                if ((na >= 0 && s_xn[na].place >= 0 ? ix_node_pose(na, p, &a) : ix_end_pose(i, 0, p, &a)) != 0 ||
                    (nb >= 0 && s_xn[nb].place >= 0 ? ix_node_pose(nb, p, &b) : ix_end_pose(i, 1, p, &b)) != 0)
                    continue;
            }
            else if (ix_end_pose(i, 0, p, &a) != 0 || ix_end_pose(i, 1, p, &b) != 0)
                continue;
            /*  An end's pose heads the way its traffic runs.  The line runs
             *  along `s`, from end 0 to end 1.  Where the road's traffic runs
             *  against `s`, both ends head the other way along it. */
            if (x->kind != IX_ALONG && rg_main(x->road) >= 0 && !network_lane_with_s(rg_main(x->road)))
                a.hdg += M_PI, b.hdg += M_PI;
            if (x->kind == IX_FAR)
            {
                const IxArm *pa = &s_xa[x->from_arm], *pb = &s_xa[x->to_arm];
                double       ux = cos(pa->hin), uy = sin(pa->hin), wx = -cos(pb->hin), wy = -sin(pb->hin), sw = p[x->swing];
                double       sx = ux + wx, sy = uy + wy, sl = hypot(sx, sy), sh = sl > 1e-9 ? p[x->shift] / sl : 0.0;
                /*  Its middle: swung out across the middle of the region, and
                 *  moved along the way it turns. */
                Pose m = {s_xmid[0] - sw * (ux - wx) + sh * sx, s_xmid[1] - sw * (uy - wy) + sh * sy, atan2(sy, sx), 0.0};
                Way  w2;
                if (way_between(&a, &m, &x->w) != 0 || way_between(&m, &b, &w2) != 0 || way_cat(&x->w, &w2) != 0)
                    continue;
            }
            else if (x->kind == IX_LOOP)
            {
                /*  Two arcs of half the loop's turn each, the way the
                 *  loop turns, each of its own radius.  They make an
                 *  oval from a to b, tangent to both lanes.  The loop's
                 *  turn is the whole turn less the short turn from a to
                 *  b, about three quarters of a turn.  The two radii
                 *  follow from where a and b lie, by one linear system.
                 *  An arc of radius R from heading h through angle f
                 *  moves by R s times the change of (sin, -cos) of its
                 *  heading.  Here s is the way it turns.  A layout whose
                 *  oval would need a radius of nought or less has no
                 *  loop. */
                double d = remainder(b.hdg - a.hdg, 2.0 * M_PI), sg = d > 0.0 ? -1.0 : 1.0, f = 0.5 * (2.0 * M_PI - fabs(d));
                double h1 = a.hdg + sg * f;
                double ax = sg * (sin(a.hdg + sg * f) - sin(a.hdg)), ay = sg * (cos(a.hdg) - cos(a.hdg + sg * f));
                double bx = sg * (sin(h1 + sg * f) - sin(h1)), by = sg * (cos(h1) - cos(h1 + sg * f));
                double ex = b.x - a.x, ey = b.y - a.y, det = ax * by - ay * bx, r1, r2;
                if (fabs(det) < 1e-9)
                    continue;
                r1 = (ex * by - ey * bx) / det, r2 = (ax * ey - ay * ex) / det;
                x->miss = fmax(0.0, IX_LOOP_R_LO - IX_LOOP_R_TOL - r1) + fmax(0.0, IX_LOOP_R_LO - IX_LOOP_R_TOL - r2) + x->gap_short;
                if (r1 <= 1e-3 || r2 <= 1e-3)
                {
                    if (way_between(&a, &b, &x->w) != 0)
                        continue;
                    x->ok = 1, placed++;
                    continue;
                }
                memset(&x->w, 0, sizeof x->w);
                way_put(&x->w, &a, f * r1, sg / r1, sg / r1);
                way_put(&x->w, NULL, f * r2, sg / r2, sg / r2);
            }
            else if (x->kind == IX_RAMP)
            {
                /*  A city ramp runs on beside the highway from its node
                 *  there, or up to it, before it bends to its road.  It
                 *  runs a lane out from the outermost lane, and bends as
                 *  that lane does. */
                int    na = ix_node(r->pred.index), nb = ix_node(r->succ.index);
                int    nd  = na >= 0 && s_xn[na].place >= 0 ? na : nb;
                double run = p[x->run], lw, along;
                float  ti, to;
                Pose   m;
                Way    w2;
                /*  The run's end, a lane farther across, where the strand
                 *  rides.  Where the strand leaves the lane, the end lies the
                 *  run on from the node, the way the traffic runs.  Where the
                 *  strand joins the lane, the end lies the run back.  A lane's places count
                 *  in from the gore, so they count with the traffic on the
                 *  way in and against it on the way out. */
                along = (nd == na ? 1.0 : -1.0) * (s_xn[nd].leaving ? -1.0 : 1.0);
                network_lane_t(rg_main(x->road), 0.0f, &ti, &to), lw = fabs((double)to - (double)ti);
                if (run < 1e-3 || ix_chain_at(nd, ix_node_in(nd, p) + along * run, lw, p, &m) != 0)
                {
                    if (way_between(&a, &b, &x->w) != 0)
                        continue;
                }
                else if (way_between(&a, &m, &x->w) != 0 || way_between(&m, &b, &w2) != 0 || way_cat(&x->w, &w2) != 0)
                    continue;
            }
            else if (way_between(&a, &b, &x->w) != 0)
                continue;
            x->ok = 1, placed++;
        }
}

/*  EVERY ROAD'S SAMPLES: from the lines just laid, or from the filed shape
 *  where `filed`.  Each lies where its road's traffic lies across its line.
 *  0, or -1 out of memory. */
static int ix_sample(int filed)
{
    int i, k;
    s_nxs = 0;
    for (i = 0; i < s_nxr; i++)
    {
        IxRoad *x   = &s_xr[i];
        double  len = filed ? s_road[x->road].length : x->w.len, hp = 0.0, turn = 0.0;
        int     n;
        x->n = 0, x->kmax = 0.0, x->wiggle = 0.0;
        if (filed ? !s_road[x->road].ngeom : !x->ok)
            continue;
        n = (int)ceil(len / IX_DS) + 1;
        n = n < 2 ? 2 : n;
        if (arc_grow((void **)&s_xs, &s_cxs, s_nxs + n, sizeof *s_xs) != 0)
            return -1;
        x->s0 = s_nxs, x->n = n;
        for (k = 0; k < n; k++)
        {
            double s = len * k / (n - 1), px, py, h;
            if (filed)
            {
                float fx, fy, fh;
                shape_at(x->road, (float)s, &fx, &fy, &fh);
                px = fx, py = fy, h = fh;
            }
            else
            {
                Pose o;
                way_at(&x->w, s, &o);
                px = o.x, py = o.y, h = o.hdg;
            }
            if (k)
            {
                double dh = remainder(h - hp, 2.0 * M_PI);
                turn += dh, x->wiggle += fabs(dh), x->kmax = fmax(x->kmax, fabs(dh) / (len / (n - 1)));
            }
            hp = h;
            px -= sin(h) * x->c, py += cos(h) * x->c;
            s_xs[s_nxs++] = (IxSample){px, py, s, shape_land_at(px, py) + (x->kind == IX_RAMP ? 0.0 : s_xlift), h};
        }
        /*  Against its traffic: a heading more than a quarter turn from the
         *  way from its start to its end. */
        x->back = 0;
        for (k = 0; x->kind != IX_BUNDLE && x->kind != IX_LOOP && k < n; k++)
        {
            const IxSample *a = &s_xs[x->s0 + k], *f = &s_xs[x->s0], *l = &s_xs[x->s0 + n - 1];
            if (cos(a->h) * (l->x - f->x) + sin(a->h) * (l->y - f->y) < 0.0)
            {
                x->back = 1;
                break;
            }
        }
        x->wiggle -= fabs(turn);
    }
    return 0;
}

/*  The height variable of sample `k` of road `i`: its node's at its ends. */
static int ix_var(int i, int k)
{
    const NRoad *r = network_road(s_xr[i].road);
    if (k == 0)
        return s_nxs + ix_node(r->pred.index);
    if (k == s_xr[i].n - 1)
        return s_nxs + ix_node(r->succ.index);
    return s_xr[i].s0 + k;
}

static int ix_edge(int u, int v, double w0, double wg)
{
    if (arc_grow((void **)&s_xe, &s_cxe, s_nxe + 1, sizeof *s_xe) != 0)
        return -1;
    s_xe[s_nxe++] = (IxEdge){u, v, w0, wg};
    return 0;
}

/*  How far along road `i` sample `k` lies from its end at junction `j`. */
static double ix_from(int i, int k, int j)
{
    const IxRoad *x = &s_xr[i];
    double        s = s_xs[x->s0 + k].s;
    return network_road(x->road)->pred.index == j ? s : s_xs[x->s0 + x->n - 1].s - s;
}

/*  Whether road `i` lies on its own lane at sample `k`.  That is near an
 *  end of it at a node of the outermost lane that road `along` lies on. */
static int ix_on_lane(int i, int k, int along)
{
    const NRoad  *r = network_road(s_xr[i].road);
    const IxRoad *y = &s_xr[along];
    int           q, nd;
    if (y->kind != IX_ALONG)
        return 0;
    for (q = 0; q < 2; q++)
    {
        int j = q ? r->succ.index : r->pred.index;
        if ((nd = ix_node(j)) < 0)
            continue;
        if (s_xn[nd].place >= 0 ? (s_xn[nd].arm != y->arm) : ix_arm(j) != y->arm)
            continue;
        if (ix_from(i, k, j) < IX_PEEL)
            return 1;
    }
    return 0;
}

static int ix_hit_cmp(const void *x, const void *y)
{
    const IxHit *h = (const IxHit *)x, *g = (const IxHit *)y;
    return h->a != g->a ? h->a - g->a : h->b != g->b ? h->b - g->b
                                    : h->p != g->p   ? h->p - g->p
                                                     : h->q - g->q;
}

/*  EVERY CROSSING: each pair of samples of two roads in one place.  A
 *  pair riding side by side from a node they share is no crossing, and
 *  nor is a strand on its own lane.  Each gives the edge that holds the
 *  road of the lower level a level under the other, through `each`, with
 *  the two variables.  The length over or under is summed into s_xover.
 *  0, or -1 out of memory. */
static int ix_crossings(int (*each)(int hi, int lo))
{
    int k, m, i;
    s_nxh = 0, s_xover = 0.0;
    /*  Every pair of samples in one place, by brute force over a grid of
     *  cells half a tile wide. */
    for (i = 0; i < s_nxr; i++)
        for (m = i + 1; m < s_nxr; m++)
        {
            const IxRoad *A = &s_xr[i], *B = &s_xr[m];
            double        thr = A->half + B->half - IX_TOL, ax0 = 1e9, ax1 = -1e9, ay0 = 1e9, ay1 = -1e9;
            int           p, q;
            if (!A->n || !B->n || thr <= 0.0)
                continue;
            for (p = 0; p < A->n; p++)
            {
                const IxSample *s = &s_xs[A->s0 + p];
                ax0 = fmin(ax0, s->x), ax1 = fmax(ax1, s->x), ay0 = fmin(ay0, s->y), ay1 = fmax(ay1, s->y);
            }
            for (q = 0; q < B->n; q++)
            {
                const IxSample *s = &s_xs[B->s0 + q];
                if (s->x < ax0 - thr || s->x > ax1 + thr || s->y < ay0 - thr || s->y > ay1 + thr)
                    continue;
                for (p = 0; p < A->n; p++)
                {
                    const IxSample *t = &s_xs[A->s0 + p];
                    if (hypot(t->x - s->x, t->y - s->y) >= thr)
                        continue;
                    if (arc_grow((void **)&s_xh, &s_cxh, s_nxh + 1, sizeof *s_xh) != 0)
                        return -1;
                    s_xh[s_nxh++] = (IxHit){i, p, m, q};
                }
            }
        }
    qsort(s_xh, (size_t)s_nxh, sizeof *s_xh, ix_hit_cmp);
    for (k = 0; k < s_nxh; k = m)
    {
        int            a = s_xh[k].a, b = s_xh[k].b, h, kept = 0, na = 0, nb = 0, hi, lo;
        const IxRoad  *A = &s_xr[a], *B = &s_xr[b];
        const NRoad   *ra = network_road(A->road), *rb = network_road(B->road);
        int            shared[2], ns                   = 0, q;
        double         lim[2] = {0.0, 0.0}, room_a = 0.0, room_b = 0.0, thr = A->half + B->half - IX_TOL, ka, kb;
        unsigned char *pa, *pb;
        for (m = k; m < s_nxh && s_xh[m].a == a && s_xh[m].b == b; m++)
            ;
        /*  A node the two share: they ride side by side from it until they
         *  first part. */
        for (q = 0; q < 2; q++)
        {
            int j = q ? ra->succ.index : ra->pred.index;
            if (j >= 0 && (rb->pred.index == j || rb->succ.index == j) && (ns == 0 || shared[0] != j))
                shared[ns++] = j;
        }
        for (q = 0; q < ns; q++)
        {
            double best = -1.0;
            int    p, p2;
            lim[q] = 1e9;
            for (p2 = 0; p2 < A->n; p2++)
            {
                double dmin = 1e9, s;
                p           = p2;
                s           = ix_from(a, p, shared[q]);
                for (h = 0; h < B->n; h++)
                    dmin = fmin(dmin, hypot(s_xs[A->s0 + p].x - s_xs[B->s0 + h].x, s_xs[A->s0 + p].y - s_xs[B->s0 + h].y));
                if (dmin > thr && (best < 0.0 || s < best))
                    best = s;
            }
            lim[q] = (best < 0.0 ? 1e9 : best) + 0.1;
        }
        pa = (unsigned char *)calloc((size_t)A->n, 1), pb = (unsigned char *)calloc((size_t)B->n, 1);
        if (!pa || !pb)
        {
            free(pa), free(pb);
            return -1;
        }
        for (h = k; h < m; h++)
        {
            int p = s_xh[h].p, q2 = s_xh[h].q, skip = 0;
            for (q = 0; q < ns; q++)
                skip |= ix_from(a, p, shared[q]) < lim[q] && ix_from(b, q2, shared[q]) < lim[q] + 0.3;
            skip |= ix_on_lane(a, p, b) || ix_on_lane(b, q2, a);
            s_xh[h].a = skip ? -1 : a;
            if (skip)
                continue;
            kept++;
            na += !pa[p], nb += !pb[q2], pa[p] = pb[q2] = 1;
            room_a += fmin(ix_from(a, p, ra->pred.index), ix_from(a, p, ra->succ.index));
            room_b += fmin(ix_from(b, q2, rb->pred.index), ix_from(b, q2, rb->succ.index));
        }
        free(pa), free(pb);
        if (!kept)
            continue;
        s_xover += IX_DS * (na < nb ? na : nb);
        /*  Which goes over: by their levels, and where they share one, the
         *  one with more room to climb there. */
        ka = A->rank, kb = B->rank;
        if (ka == kb)
            ka = room_a, kb = room_b;
        hi = ka > kb ? a : b, lo = hi == a ? b : a;
        for (h = k; h < m; h++)
            if (s_xh[h].a >= 0 && each(hi == a ? ix_var(a, s_xh[h].p) : ix_var(b, s_xh[h].q),
                                       hi == a ? ix_var(b, s_xh[h].q) : ix_var(a, s_xh[h].p)) != 0)
                return -1;
    }
    return 0;
}

static int ix_clear_edge(int hi, int lo)
{
    return ix_edge(hi, lo, -1.0, 0.0);
}

/*  How steep road `i` may climb, as a share of a deck's grade.  That is
 *  its kind's greatest grade over MAX_GRADE.  The grade the solve finds
 *  is a deck's. */
static double ix_grade_share(int i)
{
    return s_xr[i].kind == IX_RAMP ? tan(IX_RAMP_GRADE * M_PI / 180) / tan(MAX_GRADE * M_PI / 180) : 1.0;
}

/*  The variables: every sample, every node, and the ground's own, last. */
static int ix_nvar(void)
{
    return s_nxs + s_nxn + s_nxapv + 1;
}

/*  The height variable of sample `k` of arm `a`'s approach.  The two
 *  samples where the way passes from one road to the next are one point,
 *  and share one height. */
static int ix_ap_var(int a, int k)
{
    return s_nxs + s_nxn + s_xap[s_xa[a].ap0 + k].v;
}

/*  Where an arm's gore lies along its road for the places `p`. */
static double ix_gore_s(int a, const double *p)
{
    return s_xa[a].contact ? s_road[s_xa[a].road].length - p[s_xa[a].out] : p[s_xa[a].out];
}

/*  Whether end `c` of road `r` is an interchange's gore: a gore one of whose
 *  cells lies in a region. */
static int ix_region_gore(int r, int c)
{
    const NLink   *l = c ? &network_road(r)->succ : &network_road(r)->pred;
    const TopoEnd *te;
    int            k;
    if (l->kind != NLINK_JUNCTION || l->index < 0)
        return 0;
    te = topology_end(network_junction(l->index)->end);
    if (te->kind != END_GORE || te->net != RNET_BAND)
        return 0;
    for (k = 0; k < te->ncell; k++)
        if (network_cell_region(topology_cell(te->cell0 + k)) >= 0)
            return 1;
    return 0;
}

/*  The floor a deck keeps at `s` along highway road `road`.  It is
 *  CLEARANCE over the highest ground under the road's own unit there.  A
 *  road of no units takes the unit at its point (x, y). */
static double ix_unit_floor(int road, double s, double x, double y)
{
    const NRoad *nr  = network_road(road);
    int32_t      col = (int32_t)floor(x) & ~1, row = (int32_t)floor(y) & ~1;
    double       top = -1e18, h[4];
    if (nr->topo >= 0 && topology_road(nr->topo)->nunit > 0)
    {
        const TopoRoad *t = topology_road(nr->topo);
        int             k = (int)floorf(shape_units(road, (float)s));
        int32_t         c = topology_cell(t->unit0 + (k < 0 ? 0 : k >= t->nunit ? t->nunit - 1
                                                                                : k));
        col = c % topology_side(), row = c / topology_side();
    }
    int dc, dr, k;
    for (dc = 0; dc < 2; dc++)
        for (dr = 0; dr < 2; dr++)
        {
            corners(col + dc, row + dr, h);
            for (k = 0; k < 4; k++)
                top = h[k] > top ? h[k] : top;
        }
    return top + CLEARANCE;
}

/*  The highway road that carries the way on past end `c` of road `r`, and
 *  the end of it the way comes in by.  The way goes on across a link to a
 *  road, or across a ramp's gore, whose ramp is shaped after.  -1 where
 *  none does, or where that end is a junction whose own roads stand at
 *  its height, or an interchange's gore. */
static int ix_way_on(int r, int c, int *c2)
{
    const NLink *l = c ? &network_road(r)->succ : &network_road(r)->pred;
    int          i;
    if (ix_region_gore(r, c) ||
        (l->kind == NLINK_JUNCTION && l->index >= 0 && topology_end(network_junction(l->index)->end)->kind != END_GORE))
        return -1;
    if (l->kind == NLINK_ROAD && l->index >= 0 && s_road[l->index].ngeom)
    {
        const NRoad *o = network_road(l->index);
        *c2            = o->pred.kind == NLINK_ROAD && o->pred.index == r ? 0 : 1;
        return l->index;
    }
    if (l->kind != NLINK_JUNCTION || l->index < 0)
        return -1;
    for (i = 0; i < s_nroad; i++)
    {
        const NRoad *o = network_road(i);
        if (i == r || o->net != RNET_BAND || o->junction >= 0 || !s_road[i].ngeom)
            continue;
        if (o->pred.kind == NLINK_JUNCTION && o->pred.index == l->index)
            return *c2 = 0, i;
        if (o->succ.kind == NLINK_JUNCTION && o->succ.index == l->index)
            return *c2 = 1, i;
    }
    return -1;
}

/*  Each arm's approach for the places `p`.  Its samples lie IX_DS apart,
 *  IX_APPROACH in all.  They run back from its gore along its road, and
 *  on along the roads that carry its way on.  Where the way passes from
 *  one road to the next, a sample stands at each road's end, and the two
 *  are one height.  They are kept from the farthest to the gore. 0, or
 *  -1 out of memory. */
static int ix_approaches(const double *p)
{
    int a, k;
    s_nxap = s_nxapv = 0;
    for (a = 0; a < s_nxa; a++)
    {
        IxArm *am   = &s_xa[a];
        int    road = am->road, dir = am->contact ? -1 : 1, n0 = s_nxap, v0 = s_nxapv, guard = 0;
        double sk, left = IX_APPROACH;
        am->ap0 = s_nxap, am->apn = 0;
        if (am->out < 0 || !s_road[road].ngeom)
            continue;
        sk = ix_gore_s(a, p);
        {
            float gx, gy, gh;
            shape_at(road, (float)sk, &gx, &gy, &gh);
            am->gfloor = ix_unit_floor(road, sk, gx, gy);
        }
        while (left > 1e-9 && guard++ < 64)
        {
            double L = s_road[road].length, step = fmin(IX_DS, left), nx = sk + dir * step;
            float  x, y, h;
            int    c2;
            /*  Short of half way along a road whose far end is another
             *  interchange's gore: that half is the other's approach. */
            if (ix_region_gore(road, dir > 0) && (dir > 0 ? nx > 0.5 * L : nx < 0.5 * L))
                break;
            if (nx >= 0.0 && nx <= L)
            {
                sk = nx, left -= step;
                shape_at(road, (float)sk, &x, &y, &h);
                if (arc_grow((void **)&s_xap, &s_cxap, s_nxap + 1, sizeof *s_xap) != 0)
                    return -1;
                s_xap[s_nxap++] = (IxAp){road, s_nxapv++, sk, ix_unit_floor(road, sk, x, y)};
                guard           = 0;
                continue;
            }
            /*  The road's end, then the next road's end where the way goes on. */
            left -= fabs((dir > 0 ? L : 0.0) - sk), sk = dir > 0 ? L : 0.0;
            shape_at(road, (float)sk, &x, &y, &h);
            if (arc_grow((void **)&s_xap, &s_cxap, s_nxap + 2, sizeof *s_xap) != 0)
                return -1;
            s_xap[s_nxap++] = (IxAp){road, s_nxapv++, sk, ix_unit_floor(road, sk, x, y)};
            if ((road = ix_way_on(road, dir > 0, &c2)) < 0)
                break;
            sk = c2 ? s_road[road].length : 0.0, dir = c2 ? -1 : 1;
            s_xap[s_nxap++] = (IxAp){road, s_nxapv - 1, sk, ix_unit_floor(road, sk, x, y)};
        }
        am->apn = s_nxap - n0;
        /*  Each keeps the higher floor of itself and the samples beside it,
         *  so the deck between two of them keeps both. */
        {
            double prev = s_nxap > n0 ? s_xap[n0].floor : 0.0;
            for (k = n0; k < s_nxap; k++)
            {
                double here = s_xap[k].floor, next = k + 1 < s_nxap ? s_xap[k + 1].floor : here;
                s_xap[k].floor = fmax(here, fmax(prev, next)), prev = here;
            }
        }
        /*  From the farthest to the gore. */
        for (k = 0; k < am->apn / 2; k++)
        {
            IxAp t        = s_xap[n0 + k];
            s_xap[n0 + k] = s_xap[n0 + am->apn - 1 - k], s_xap[n0 + am->apn - 1 - k] = t;
        }
        if (am->apn < 2)
            s_nxap = n0, s_nxapv = v0, am->apn = 0;
    }
    return 0;
}

/*  THE DIFFERENCES of the region.  Each sample climbs to the next within
 *  the grade, each crossing keeps a level apart, and nothing goes under
 *  the ground.  Each gore stands at its arm's height, and each ramp's
 *  gore at its road's. 0, or -1 out of memory. */
static int ix_differences(const double *p)
{
    int i, k, z0, q;
    if (ix_approaches(p) != 0)
        return -1;
    z0    = ix_nvar() - 1;
    s_nxe = 0;
    /*  Each approach holds its farthest sample at its arm's height.
     *  Each sample stays within the grade of the next, and the last of
     *  the gore, over a deck's floor. */
    for (q = 0; q < s_nxa; q++)
    {
        const IxArm *am = &s_xa[q];
        double       h0;
        int          j = ix_arm_node(q);
        if (am->apn < 2 || j < 0)
            continue;
        h0 = shape_height(s_xap[am->ap0].road, (float)s_xap[am->ap0].s);
        if (ix_edge(z0, ix_ap_var(q, 0), h0, 0.0) != 0 || ix_edge(ix_ap_var(q, 0), z0, -h0, 0.0) != 0 ||
            ix_edge(s_nxs + j, z0, -am->gfloor, 0.0) != 0)
            return -1;
        for (k = 0; k < am->apn; k++)
        {
            int         u = ix_ap_var(q, k), v = k + 1 < am->apn ? ix_ap_var(q, k + 1) : s_nxs + j;
            const IxAp *f = &s_xap[am->ap0 + k], *t = k + 1 < am->apn ? &s_xap[am->ap0 + k + 1] : NULL;
            double      ds = t ? fabs(t->s - f->s) : fabs(ix_gore_s(q, p) - f->s);
            if (t && t->v == f->v)
                continue;
            if (ix_edge(u, v, 0.0, ds) != 0 || ix_edge(v, u, 0.0, ds) != 0 || ix_edge(u, z0, -s_xap[am->ap0 + k].floor, 0.0) != 0)
                return -1;
        }
    }
    for (i = 0; i < s_nxr; i++)
        for (k = 0; k + 1 < s_xr[i].n; k++)
        {
            int    u = ix_var(i, k), v = ix_var(i, k + 1);
            double ds = (s_xs[s_xr[i].s0 + k + 1].s - s_xs[s_xr[i].s0 + k].s) * ix_grade_share(i);
            if (ix_edge(u, v, 0.0, ds) != 0 || ix_edge(v, u, 0.0, ds) != 0 || ix_edge(u, z0, -s_xs[s_xr[i].s0 + k].floor, 0.0) != 0)
                return -1;
        }
    if (ix_crossings(ix_clear_edge) != 0)
        return -1;
    for (k = 0; k < s_nxn; k++)
    {
        double pin = s_xn[k].pin;
        int    a   = ix_arm(s_xn[k].junction);
        /*  A gore with an approach is free: the approach holds it. */
        if (a >= 0 && s_xa[a].apn >= 2 && s_xn[k].reach < 0 && ix_arm_node(a) == k)
            continue;
        if (a >= 0 && s_xa[a].out >= 0)
            pin = shape_height(s_xa[a].road, (float)(s_xa[a].contact ? s_road[s_xa[a].road].length - p[s_xa[a].out] : p[s_xa[a].out]));
        if (s_xn[k].reach >= 0)
        {
            Pose o;
            ix_ramp_pose(&s_xn[k], p, &o);
            pin = o.z;
        }
        if (!isnan(pin) && (ix_edge(z0, s_nxs + k, pin, 0.0) != 0 || ix_edge(s_nxs + k, z0, -pin, 0.0) != 0))
            return -1;
    }
    return 0;
}

/*  Whether the differences hold at grade `g`, and the heights where they
 *  do.  They are the shortest paths (Bellman-Ford, as a queue), from the
 *  heights `from` where it is given, else from nought.  A cycle in the
 *  tree of last steps is a negative cycle (Tarjan 1981).  So the walk
 *  looks for one after each round of as many steps as there are
 *  variables. 1 where they hold. */
static int ix_holds(double g, double *z, const double *from, int *head, int *next, int *queue, int *parent, int *mark, unsigned char *in)
{
    int nv = ix_nvar(), i, qh = 0, qt = 0, steps = 0, stamp = 0;
    for (i = 0; i < nv; i++)
        z[i] = from ? from[i] : 0.0, parent[i] = -1, mark[i] = -1, in[i] = 1, queue[qt++] = i;
    qt %= nv + 1;
    while (qh != qt)
    {
        int u = queue[qh], e;
        qh = (qh + 1) % (nv + 1), in[u] = 0;
        for (e = head[u]; e >= 0; e = next[e])
        {
            int    v = s_xe[e].v;
            double d = z[u] + s_xe[e].w0 + s_xe[e].wg * g;
            if (d >= z[v] - 1e-9)
                continue;
            z[v] = d, parent[v] = u;
            if (++steps % nv == 0)
                for (i = 0; i < nv; i++)
                    mark[i] = -1;
            if (steps % nv == 0)
                for (i = 0; i < nv; i++, stamp++)
                {
                    int w = i;
                    while (w >= 0 && mark[w] < 0)
                        mark[w] = stamp, w = parent[w];
                    if (w >= 0 && mark[w] == stamp)
                        return 0;
                }
            if (steps > 256 * nv)
                return 0;
            if (!in[v])
                in[v] = 1, queue[qt] = v, qt = (qt + 1) % (nv + 1);
        }
    }
    return 1;
}

/*  THE LEAST GRADE at which the differences hold, in levels a tile, found by
 *  `halve` halvings.  It is no less than `floor_g`.  INFINITY where none up to
 *  IX_G_MOST does.  `z` gets the heights at the grade answered.  Each try
 *  starts from the heights of the last that held: they are an upper bound
 *  of the next. */
static double ix_least_grade(double floor_g, int halve, double *z)
{
    int            nv = ix_nvar(), i, *head, *next, *queue, *parent, *mark, it;
    unsigned char *in = (unsigned char *)malloc((size_t)nv);
    double        *ok, lo = floor_g, hi = IX_G_MOST, g = INFINITY;
    head   = (int *)malloc(sizeof *head * (size_t)nv);
    next   = (int *)malloc(sizeof *next * (size_t)(s_nxe ? s_nxe : 1));
    queue  = (int *)malloc(sizeof *queue * (size_t)(nv + 1));
    parent = (int *)malloc(sizeof *parent * (size_t)nv);
    mark   = (int *)malloc(sizeof *mark * (size_t)nv);
    ok     = (double *)malloc(sizeof *ok * (size_t)nv);
    if (!head || !next || !queue || !parent || !mark || !ok || !in)
        goto out;
    for (i = 0; i < nv; i++)
        head[i] = -1;
    for (i = 0; i < s_nxe; i++)
        next[i] = head[s_xe[i].u], head[s_xe[i].u] = i;
    if (ix_holds(lo, z, NULL, head, next, queue, parent, mark, in))
    {
        g = lo;
        goto out;
    }
    if (!ix_holds(hi, ok, NULL, head, next, queue, parent, mark, in))
        goto out;
    for (it = 0; it < halve; it++)
    {
        double mid = 0.5 * (lo + hi);
        if (ix_holds(mid, z, ok, head, next, queue, parent, mark, in))
            hi = mid, memcpy(ok, z, sizeof *z * (size_t)nv);
        else
            lo = mid;
    }
    memcpy(z, ok, sizeof *z * (size_t)nv);
    g = hi;
out:
    if (isfinite(g))
        for (i = 0; i < nv; i++)
            z[i] -= z[nv - 1];
    free(head), free(next), free(queue), free(parent), free(mark), free(ok), free(in);
    return g;
}

static double ix_limit(void)
{
    return tan(MAX_GRADE * M_PI / 180) / LEVEL_TILES;
}

/*  THE SCORE of the places `p` (plans/interchanges.md, "The shape").  It
 *  counts how far its least grade is over the greatest less IX_MARGIN,
 *  and the length of strand over or under another.  It counts its
 *  sharpest curve, its wiggles, its strands against their traffic, how far
 *  each city ramp's run beside the highway falls short of IX_RUN, and how
 *  much tighter than IX_TURN_R each turn bends.  s_xz
 *  gets the heights.  A layout that cannot be laid or held scores
 *  1e9. */
/*  How far apart an arm's deck is read for its grade, in tiles. */
#define IX_ARM_DS 0.05

/*  How steep the arms' decks are, in degrees, where they stay for the
 *  places `p`: from each gore out along its arm.  The arm's heights do
 *  not change, so only where its gore lies moves this. */
static double ix_arm_grade(const double *p)
{
    double deg = 0.0;
    int    i;
    for (i = 0; i < s_nxa; i++)
    {
        const IxArm *a   = &s_xa[i];
        double       len = s_road[a->road].length, s0, s1, s, dz;
        if (a->out < 0 || len <= 0.0)
            continue;
        s0 = a->contact ? 0.0 : p[a->out], s1 = a->contact ? len - p[a->out] : len;
        for (s = s0; s + IX_ARM_DS <= s1 + 1e-9; s += IX_ARM_DS)
        {
            dz  = fabs((double)shape_height(a->road, (float)(s + IX_ARM_DS)) - (double)shape_height(a->road, (float)s)) / IX_ARM_DS;
            deg = fmax(deg, atan(dz * LEVEL_TILES) * 180 / M_PI);
        }
    }
    return deg;
}

static double ix_score(const double *p, int halve, double *grade)
{
    int    i, back = 0;
    double g, deg, kmax = 0.0, wig = 0.0, run = 0.0, tight = 0.0, miss = 0.0, aim = tan((MAX_GRADE - IX_MARGIN) * M_PI / 180) / LEVEL_TILES;
    if (grade)
        *grade = INFINITY;
    ix_ways(p);
    for (i = 0; i < s_nxr; i++)
        if (!s_xr[i].ok)
            return 1e9;
    if (ix_sample(0) != 0 || ix_differences(p) != 0 || arc_grow((void **)&s_xz, &s_cxz, ix_nvar(), sizeof *s_xz) != 0)
        return 1e9;
    g = ix_least_grade(aim, halve, s_xz);
    if (grade)
        *grade = g;
    if (!isfinite(g))
        return 1e9;
    deg = atan(g * LEVEL_TILES) * 180 / M_PI;
    for (i = 0; i < s_nxr; i++)
    {
        if (s_xr[i].kind != IX_BUNDLE)
            kmax = fmax(kmax, s_xr[i].kmax), wig += s_xr[i].wiggle, back += s_xr[i].back;
        if (s_xr[i].run >= 0)
            run += fmax(0.0, IX_RUN - p[s_xr[i].run]);
        if (s_xr[i].kind == IX_NEAR || s_xr[i].kind == IX_FAR || s_xr[i].kind == IX_LOOP)
            tight += fmax(0.0, s_xr[i].kmax - 1.0 / IX_TURN_R);
        if (s_xr[i].kind == IX_LOOP)
            miss += s_xr[i].miss;
    }
    deg = fmax(deg, ix_arm_grade(p));
    return IX_W_GRADE * fmax(0.0, deg - (MAX_GRADE - IX_MARGIN)) + IX_W_OVER * s_xover + IX_W_CURVE * kmax + IX_W_WIGGLE * wig +
           IX_W_BACK * back + IX_W_RUN * run + s_wradius * tight + IX_W_MISS * miss;
}

/*  A number in [0, 1) from the search's own generator (xorshift). */
static double ix_rand(uint64_t *st)
{
    *st ^= *st << 13, *st ^= *st >> 7, *st ^= *st << 17;
    return (double)(*st >> 11) / 9007199254740992.0;
}

/*  THE SEARCH (plans/interchanges.md, "The shape"): differential
 *  evolution (Storn and Price 1997), best/1/bin.  It starts from a seed
 *  of the region's own, so a city always gets one shape.  With `warm`
 *  set, the places already in `best` are one of the starting layouts.
 *  It runs at most `gens` generations, and ends once its best has not
 *  improved for IX_STALL of them.  `best` gets the best places found, and
 *  `scored` their score.  0, or -1 out of memory. */
static int ix_search(uint64_t seed, double *best, int warm, int gens, double *scored)
{
    int      D = s_np, NP = D * IX_POP_PER, i, k, gen, ib = 0, stall = 0;
    double  *pop, *score, *trial;
    uint64_t st = seed ? seed : 1;
    NP          = NP > IX_POP_MAX ? IX_POP_MAX : NP < 4 ? 4
                                                        : NP;
    pop         = (double *)malloc(sizeof *pop * (size_t)(NP * (D ? D : 1)));
    score       = (double *)malloc(sizeof *score * (size_t)NP);
    trial       = (double *)malloc(sizeof *trial * (size_t)(D ? D : 1));
    if (!pop || !score || !trial)
    {
        free(pop), free(score), free(trial);
        return -1;
    }
    for (i = 0; i < NP; i++)
    {
        for (k = 0; k < D; k++)
            pop[i * D + k] = i == 1 && warm ? fmin(s_phi[k], fmax(s_plo[k], best[k]))
                             : i            ? s_plo[k] + ix_rand(&st) * (s_phi[k] - s_plo[k])
                                            : s_p0[k];
        score[i] = ix_score(pop + i * D, IX_HALVE_SEARCH, NULL);
        if (score[i] < score[ib])
            ib = i;
    }
    for (gen = 0; D && gen < gens && stall < IX_STALL; gen++)
    {
        double F = 0.5 + 0.5 * ix_rand(&st), was = score[ib];
        for (i = 0; i < NP; i++)
        {
            int    r1, r2, jr = (int)(ix_rand(&st) * D);
            double sc;
            do
                r1 = (int)(ix_rand(&st) * NP);
            while (r1 == i);
            do
                r2 = (int)(ix_rand(&st) * NP);
            while (r2 == i || r2 == r1);
            for (k = 0; k < D; k++)
            {
                double v = pop[i * D + k];
                if (k == jr || ix_rand(&st) < 0.7)
                    v = pop[ib * D + k] + F * (pop[r1 * D + k] - pop[r2 * D + k]);
                trial[k] = fmin(s_phi[k], fmax(s_plo[k], v));
            }
            sc = ix_score(trial, IX_HALVE_SEARCH, NULL);
            if (sc <= score[i])
            {
                memcpy(pop + i * D, trial, sizeof *trial * (size_t)D), score[i] = sc;
                if (sc < score[ib])
                    ib = i;
            }
        }
        stall = score[ib] < was ? 0 : stall + 1;
    }
    memcpy(best, pop + ib * D, sizeof *best * (size_t)D);
    /*  Then the best is POLISHED (a pattern search, Hooke and Jeeves
     *  1961).  Each place in turn tries a step either way and keeps one
     *  that scores better.  The step halves when none does. */
    {
        double sb = score[ib], frac = IX_POLISH_STEP;
        int    round;
        for (round = 0; D && round < IX_POLISH_ROUNDS; round++, frac *= 0.5)
            for (k = 0; k < D; k++)
            {
                int sg;
                for (sg = -1; sg <= 1; sg += 2)
                {
                    double keep = best[k], sc;
                    best[k]     = fmin(s_phi[k], fmax(s_plo[k], keep + sg * frac * (s_phi[k] - s_plo[k])));
                    if (best[k] == keep)
                        continue;
                    sc = ix_score(best, IX_HALVE_SEARCH, NULL);
                    if (sc < sb)
                    {
                        sb = sc;
                        break;
                    }
                    best[k] = keep;
                }
            }
        if (scored)
            *scored = sb;
    }
    free(pop), free(score), free(trial);
    return 0;
}

/*  Height `v` set to `t`, as far as its own differences at grade `g` let
 *  it.  An edge into it caps it, and an edge out of it floors it. */
static void ix_clamp(int v, double t, double g, double *z, const int *head, const int *nh, const int *tail, const int *nt)
{
    double lo = -1e9, hi = 1e9;
    int    e;
    for (e = head[v]; e >= 0; e = nh[e])
        hi = fmin(hi, z[s_xe[e].u] + s_xe[e].w0 + s_xe[e].wg * g);
    for (e = tail[v]; e >= 0; e = nt[e])
        lo = fmax(lo, z[s_xe[e].v] - s_xe[e].w0 - s_xe[e].wg * g);
    if (lo <= hi)
        z[v] = fmin(hi, fmax(lo, t));
}

/*  How many times the easing goes over every height. */
#define IX_EASE_SWEEPS 1500

/*  The heights EASED at grade `g`.  The easing makes each road's height
 *  as smooth as its differences let it.  It makes the sum of the squares
 *  of every bend along each road the least it can.  Each sample in turn
 *  takes the height that bends its road least.  A node takes the mean of
 *  the samples next to it on every road that meets there.  Each height
 *  then stays as far inside its own differences as they let it, so every
 *  rule still holds. */
static void ix_ease(double g, double *z)
{
    int     nv   = ix_nvar(), sweep, i, k, e, c;
    int    *head = (int *)malloc(sizeof *head * (size_t)nv), *tail = (int *)malloc(sizeof *tail * (size_t)nv);
    int    *nh = (int *)malloc(sizeof *nh * (size_t)(s_nxe ? s_nxe : 1)), *nt = (int *)malloc(sizeof *nt * (size_t)(s_nxe ? s_nxe : 1));
    double *sum = (double *)malloc(sizeof *sum * (size_t)nv), *cnt = (double *)malloc(sizeof *cnt * (size_t)nv);
    if (!head || !tail || !nh || !nt || !sum || !cnt)
    {
        free(head), free(tail), free(nh), free(nt), free(sum), free(cnt);
        return;
    }
    for (i = 0; i < nv; i++)
        head[i] = tail[i] = -1;
    for (e = 0; e < s_nxe; e++)
        nh[e] = head[s_xe[e].v], head[s_xe[e].v] = e, nt[e] = tail[s_xe[e].u], tail[s_xe[e].u] = e;
    for (sweep = 0; sweep < IX_EASE_SWEEPS; sweep++)
    {
        /*  The nodes first: each takes the mean of the samples next to it. */
        for (i = 0; i < nv; i++)
            sum[i] = cnt[i] = 0.0;
        for (i = 0; i < s_nxr; i++)
            if (s_xr[i].n >= 2)
            {
                int n = s_xr[i].n;
                sum[ix_var(i, 0)] += z[ix_var(i, 1)], cnt[ix_var(i, 0)] += 1.0;
                sum[ix_var(i, n - 1)] += z[ix_var(i, n - 2)], cnt[ix_var(i, n - 1)] += 1.0;
            }
        for (i = 0; i < s_nxa; i++)
            if (s_xa[i].apn >= 2 && ix_arm_node(i) >= 0)
                sum[s_nxs + ix_arm_node(i)] += z[ix_ap_var(i, s_xa[i].apn - 1)], cnt[s_nxs + ix_arm_node(i)] += 1.0;
        for (i = s_nxs; i < nv; i++)
            if (cnt[i] > 0.0)
                ix_clamp(i, sum[i] / cnt[i], g, z, head, nh, tail, nt);
        /*  Then each sample in turn, from the heights as they stand. */
        for (i = 0; i < s_nxr; i++)
            for (k = 1; k + 1 < s_xr[i].n; k++)
            {
                /*  The bends that hold sample k, each z[c-1] - 2 z[c] + z[c+1]:
                 *  the height that makes the sum of their squares least. */
                double num = 0.0, den = 0.0;
                int    n = s_xr[i].n;
                for (c = k - 1; c <= k + 1; c++)
                {
                    double a, r;
                    if (c < 1 || c > n - 2)
                        continue;
                    a = c == k ? -2.0 : 1.0;
                    r = z[ix_var(i, c - 1)] - 2.0 * z[ix_var(i, c)] + z[ix_var(i, c + 1)] - a * z[ix_var(i, k)];
                    num -= a * r, den += a * a;
                }
                if (den > 0.0)
                    ix_clamp(s_xr[i].s0 + k, num / den, g, z, head, nh, tail, nt);
            }
        /*  And each approach's samples but its farthest, pinned: each the
         *  mean of the two beside it, the last beside its gore. */
        for (i = 0; i < s_nxa; i++)
            for (k = 1; k < s_xa[i].apn && ix_arm_node(i) >= 0; k++)
            {
                int nk = k + 1, nx;
                if (ix_ap_var(i, k) == ix_ap_var(i, k - 1))
                    continue;
                while (nk < s_xa[i].apn && ix_ap_var(i, nk) == ix_ap_var(i, k))
                    nk++;
                nx = nk < s_xa[i].apn ? ix_ap_var(i, nk) : s_nxs + ix_arm_node(i);
                ix_clamp(ix_ap_var(i, k), 0.5 * (z[ix_ap_var(i, k - 1)] + z[nx]), g, z, head, nh, tail, nt);
            }
    }
    free(head), free(tail), free(nh), free(nt), free(sum), free(cnt);
}

/*  Splice heights onto road `road` between the first and the last of `n`
 *  points `ps`, `zs`, in order of `s`.  The road keeps its own pieces
 *  before and after them, with a piece from each point to the next
 *  between. 0, or -1 out of memory. */
static int elev_splice(int road, const double *ps, const double *zs, int n)
{
    SRoad *sr = &s_road[road];
    int    e0 = sr->elev0, ne = sr->nelev, q, k, at = -1;
    double lo = ps[0], hi = ps[n - 1];
    if (n < 2 || arc_grow((void **)&s_elev, &s_capelev, s_nelev + ne + n + 1, sizeof *s_elev) != 0)
        return n < 2 ? 0 : -1;
    sr->elev0 = s_nelev;
    for (q = 0; q < ne; q++)
    {
        if (s_elev[e0 + q].s < lo - 1e-9)
            s_elev[s_nelev++] = s_elev[e0 + q];
        if (s_elev[e0 + q].s <= hi + 1e-9)
            at = q;
    }
    for (k = 0; k + 1 < n; k++)
        s_elev[s_nelev++] = (SElev){(float)ps[k], (float)zs[k], ps[k + 1] - ps[k] > 1e-9 ? (float)((zs[k + 1] - zs[k]) / (ps[k + 1] - ps[k])) : 0.0f, 0.0f, 0.0f};
    s_elev[s_nelev++] = (SElev){(float)hi, (float)zs[n - 1], at >= 0 ? s_elev[e0 + at].b : 0.0f, 0.0f, 0.0f};
    for (q = 0; q < ne; q++)
        if (s_elev[e0 + q].s > hi + 1e-9)
            s_elev[s_nelev++] = s_elev[e0 + q];
    sr->nelev = s_nelev - sr->elev0;
    return 0;
}

/*  File arm `a`'s approach onto the roads it runs along.  Each road gets
 *  the eased heights `z` from the first sample on it to the last.  The
 *  arm's road gets them up to its gore. 0, or -1 out of memory. */
static int ix_ap_file(int a, const double *p, const double *z)
{
    const IxArm *am = &s_xa[a];
    double       ps[512], zs[512];
    int          k = 0, j = ix_arm_node(a);
    if (am->apn < 2 || j < 0)
        return 0;
    while (k < am->apn)
    {
        int road = s_xap[am->ap0 + k].road, n = 0, q;
        for (; k < am->apn && s_xap[am->ap0 + k].road == road && n < 511; k++, n++)
            ps[n] = s_xap[am->ap0 + k].s, zs[n] = z[ix_ap_var(a, k)];
        if (k == am->apn && road == am->road)
            ps[n] = ix_gore_s(a, p), zs[n++] = z[s_nxs + j];
        /*  In order of `s`. */
        if (n > 1 && ps[0] > ps[n - 1])
            for (q = 0; q < n / 2; q++)
            {
                double t0 = ps[q], t1 = zs[q];
                ps[q] = ps[n - 1 - q], zs[q] = zs[n - 1 - q], ps[n - 1 - q] = t0, zs[n - 1 - q] = t1;
            }
        if (elev_splice(road, ps, zs, n) != 0)
            return -1;
    }
    return 0;
}

/*  File a region's road: its line, and its height along it from its
 *  samples, one piece of height from each to the next. */
static int ix_file(int i, const double *z)
{
    const IxRoad *x  = &s_xr[i];
    SRoad        *sr = &s_road[x->road];
    int           q, k;
    if (arc_grow((void **)&s_geom, &s_capgeom, s_ngeom + x->w.n, sizeof *s_geom) != 0 ||
        arc_grow((void **)&s_elev, &s_capelev, s_nelev + x->n, sizeof *s_elev) != 0)
        return -1;
    sr->geom0 = s_ngeom, sr->ngeom = x->w.n, sr->length = (float)x->w.len, sr->map0 = s_nmap, sr->nmap = 0;
    for (q = 0; q < x->w.n; q++)
        s_geom[s_ngeom++] = x->w.g[q];
    sr->elev0 = s_nelev, sr->nelev = x->n - 1;
    for (k = 0; k + 1 < x->n; k++)
    {
        double s0 = s_xs[x->s0 + k].s, s1 = s_xs[x->s0 + k + 1].s, z0 = z[ix_var(i, k)], z1 = z[ix_var(i, k + 1)];
        s_elev[s_nelev++] = (SElev){(float)s0, (float)z0, s1 - s0 > 1e-9 ? (float)((z1 - z0) / (s1 - s0)) : 0.0f, 0.0f, 0.0f};
    }
    return 0;
}

/*  A cubic a + b p + c p^2 + d p^3 over [t0, t0 + m] of p, as a cubic over
 *  [0, 1]. */
static void cubic_part(const float in[4], double t0, double m, double out[4])
{
    out[0] = in[0] + in[1] * t0 + in[2] * t0 * t0 + in[3] * t0 * t0 * t0;
    out[1] = m * (in[1] + 2.0 * in[2] * t0 + 3.0 * in[3] * t0 * t0);
    out[2] = m * m * (in[2] + 3.0 * in[3] * t0);
    out[3] = m * m * m * in[3];
}

/*  Piece `g` cut to the part of it from `d0` to `d1` along it, which then
 *  starts at `s`. */
static void piece_part(SGeom *g, double d0, double d1, double s)
{
    double L = g->length, k0, k1;
    Pose   o;
    if (g->poly)
    {
        double u[4], v[4], ph, c, sn;
        int    q;
        cubic_part(g->u, L > 1e-9 ? d0 / L : 0.0, L > 1e-9 ? (d1 - d0) / L : 0.0, u);
        cubic_part(g->v, L > 1e-9 ? d0 / L : 0.0, L > 1e-9 ? (d1 - d0) / L : 0.0, v);
        /*  Its own frame: from its new start, along its tangent there. */
        ph = atan2(v[1], u[1]), c = cos(ph), sn = sin(ph);
        {
            double hx = cos(g->hdg), hy = sin(g->hdg);
            g->x = (float)(g->x + u[0] * hx - v[0] * hy), g->y = (float)(g->y + u[0] * hy + v[0] * hx);
        }
        u[0] = v[0] = 0.0;
        for (q = 0; q < 4; q++)
        {
            double uq = u[q], vq = v[q];
            g->u[q] = (float)(c * uq + sn * vq), g->v[q] = (float)(-sn * uq + c * vq);
        }
        g->v[1] = 0.0f, g->hdg = (float)(g->hdg + ph);
    }
    else
    {
        piece_at(g, d0, &o, &k0);
        piece_at(g, d1, &o, &k1);
        piece_at(g, d0, &o, NULL);
        g->x = (float)o.x, g->y = (float)o.y, g->hdg = (float)o.hdg, g->curvature = (float)k0, g->cend = (float)k1;
        g->spiral = fabs(k1 - k0) > 1e-9;
    }
    g->length = (float)(d1 - d0), g->s = (float)s;
}

/*  A road's line TRIMMED by `d` at its contact `c`: its pieces, its
 *  heights and its map of units.  So the line is `d` shorter at that
 *  end. */
static void ix_trim(int road, int c, double d)
{
    SRoad *sr = &s_road[road];
    double L = sr->length, a = c ? 0.0 : d, b = c ? L - d : L;
    int    q, n = 0, k;
    if (d < 1e-4 || d >= L)
        return;
    for (q = 0; q < sr->ngeom; q++)
    {
        SGeom  g  = s_geom[sr->geom0 + q];
        double g0 = g.s, g1 = g.s + g.length;
        if (g1 <= a + 1e-9 || g0 >= b - 1e-9)
            continue;
        piece_part(&g, fmax(a, g0) - g0, fmin(b, g1) - g0, fmax(a, g0) - a);
        s_geom[sr->geom0 + n++] = g;
    }
    sr->ngeom = n;
    for (q = 0, n = 0; q < sr->nelev; q++)
    {
        SElev  e  = s_elev[sr->elev0 + q];
        double e1 = q + 1 < sr->nelev ? s_elev[sr->elev0 + q + 1].s : L;
        /*  A record's `s` is a float, so a cut that falls on a record's
         *  start is held to a float's precision. */
        if (e1 <= a + 1e-5 || e.s >= b - 1e-5)
            continue;
        if (e.s < a)
        {
            double x = a - e.s;
            e.a = (float)(e.a + e.b * x + e.c * x * x + e.d * x * x * x), e.b = (float)(e.b + 2.0 * e.c * x + 3.0 * e.d * x * x);
            e.c = (float)(e.c + 3.0 * e.d * x), e.s = (float)a;
        }
        e.s                     = (float)(e.s - a);
        s_elev[sr->elev0 + n++] = e;
    }
    sr->nelev = n;
    for (k = 0; k < sr->nmap; k++)
        s_map[sr->map0 + k] = (float)fmin(fmax(s_map[sr->map0 + k] - a, 0.0), b - a);
    sr->length = (float)(b - a);
}

/*  Whether the layout just scored at grade `g` is routed: within the
 *  greatest grade, every road laid, and no strand against its traffic. */
static int ix_routed(double g)
{
    int i;
    if (!isfinite(g) || atan(g * LEVEL_TILES) * 180 / M_PI > MAX_GRADE + 1e-6)
        return 0;
    for (i = 0; i < s_nxr; i++)
        if (!s_xr[i].ok || s_xr[i].back || (s_xr[i].kind == IX_LOOP && s_xr[i].miss > 0.0))
            return 0;
    return 1;
}

/*  What the search of the region just gathered starts from, as one
 *  number.  It is how many of each thing the region has, the bounds of
 *  its places and where they start.  It is also each arm's place and the
 *  heights its arms and nodes keep, and the line and the heights of every
 *  road it reads.  The samples and the differences the search lays are
 *  left out: they come of the places it tries.  Nothing in it is the index of a record, so the
 *  same interchange gathered by another build gives the same number. */
static uint64_t ix_mix(uint64_t h, const void *p, size_t n)
{
    const unsigned char *b = (const unsigned char *)p;
    size_t               k;
    for (k = 0; k < n; ++k)
        h = (h ^ b[k]) * 1099511628211ull;
    return h;
}

/*  A road's line and heights, by what they are. */
static uint64_t ix_mix_road(uint64_t h, int road)
{
    const SRoad *r = road >= 0 && road < s_nroad ? &s_road[road] : NULL;
    if (!r)
        return ix_mix(h, &road, sizeof road);
    h = ix_mix(h, &r->length, sizeof r->length);
    h = ix_mix(h, &s_geom[r->geom0], sizeof *s_geom * (size_t)r->ngeom);
    h = ix_mix(h, &s_elev[r->elev0], sizeof *s_elev * (size_t)r->nelev);
    return ix_mix(h, &s_map[r->map0], sizeof *s_map * (size_t)r->nmap);
}

static uint64_t ix_key(void)
{
    uint64_t h         = 14695981039346656037ull;
    int      counts[4] = {s_np, s_nxa, s_nxn, s_nxr}, i;
    h                  = ix_mix(h, counts, sizeof counts);
    h                  = ix_mix(h, s_plo, sizeof *s_plo * (size_t)s_np);
    h                  = ix_mix(h, s_phi, sizeof *s_phi * (size_t)s_np);
    h                  = ix_mix(h, s_p0, sizeof *s_p0 * (size_t)s_np);
    for (i = 0; i < s_nxa; i++)
    {
        h = ix_mix(h, &s_xa[i].x, sizeof s_xa[i].x), h = ix_mix(h, &s_xa[i].y, sizeof s_xa[i].y);
        h = ix_mix(h, &s_xa[i].hin, sizeof s_xa[i].hin), h = ix_mix(h, &s_xa[i].gfloor, sizeof s_xa[i].gfloor);
        h = ix_mix(h, &s_xa[i].contact, sizeof s_xa[i].contact), h = ix_mix_road(h, s_xa[i].road);
    }
    for (i = 0; i < s_nxn; i++)
        h = ix_mix(h, &s_xn[i].pin, sizeof s_xn[i].pin);
    for (i = 0; i < s_nxr; i++)
        h = ix_mix_road(h, s_xr[i].road);
    return h;
}

/*  THE SEARCH OF THE REGION JUST GATHERED, from `key` (ix_key), with its
 *  wishes dropped one by one while the best is not routed.  `best` gets
 *  the places, then the bounds, the weight and the highway's floor the
 *  search ended with.  That is 4 * s_np + 2 numbers, and `g` gets the
 *  grade of the best.
 *  They are kept by the interchange's place (network/solved.h).  0, or -1
 *  out of memory. */
static int ix_solve(uint64_t id, uint64_t key, double *best, double *g)
{
    double scored = 0.0;
    int    i, rc = 0;
    /*  Every region's search starts with a turn's radius weighed, as the
     *  last region's retries may have left it at nought. */
    s_wradius = IX_W_RADIUS, s_xlift = CLEARANCE;
    if (ix_search(id ^ 0x9E3779B97F4A7C15ull, best, 0, IX_GENS, &scored) != 0)
        return -1;
    ix_score(best, IX_HALVE_FILED, g);
    /*  A city ramp's run beside the highway, a pair's least length and a
     *  turn's radius are wishes, not rules.  Where the best layout
     *  cannot be routed with them, the search runs again.  First every
     *  run is held at nothing, then there is no least length, then no
     *  radius.  Then a highway's floor gives way, to half its clearance
     *  and then to the ground.  So the decks of a crowded region stack
     *  within the grade.  A search that found no layout at all, every
     *  one scoring 1e9, finds none with a wish dropped either. */
    {
        int step;
        for (step = 0; step < 5 && rc == 0 && scored < 1e9 && !ix_routed(*g); step++)
        {
            int any = step >= 2;
            if (step == 2)
                s_wradius = 0.0;
            if (step >= 3)
                s_xlift = step == 3 ? 0.5 * CLEARANCE : 0.0;
            for (i = 0; step == 0 && i < s_nxr; i++)
                if (s_xr[i].run >= 0)
                    s_plo[s_xr[i].run] = s_phi[s_xr[i].run] = s_p0[s_xr[i].run] = 0.0, any = 1;
            for (i = 0; step == 1 && i < s_np; i++)
                if (s_ppair[i])
                    s_plo[i] = IX_GAP_LO, s_phi[i] = IX_GAP_HI, s_p0[i] = IX_GAP0, any = 1;
            if (any && ix_search(id ^ 0x9E3779B97F4A7C15ull, best, 1, IX_RETRY_GENS, &scored) != 0)
                rc = -1;
            else if (any)
                ix_score(best, IX_HALVE_FILED, g);
        }
    }
    if (rc != 0)
        return -1;
    memcpy(best + s_np, s_plo, sizeof *s_plo * (size_t)s_np);
    memcpy(best + 2 * s_np, s_phi, sizeof *s_phi * (size_t)s_np);
    memcpy(best + 3 * s_np, s_p0, sizeof *s_p0 * (size_t)s_np);
    best[4 * s_np] = s_wradius, best[4 * s_np + 1] = s_xlift;
    solved_put(id, key, best, 4 * s_np + 2);
    return 0;
}

/*  Whether end `e` is an interchange's region, the one each is searched by. */
static int ix_region(int e)
{
    const TopoEnd *te = topology_end(e);
    return te->kind == END_NODE && te->net == RNET_BAND && network_cell_region(topology_cell(te->cell0)) == e;
}

/*  THE INTERCHANGES SEARCHED SIDE BY SIDE, before the lines are filed.
 *  Each worker takes the next region and gathers it.  It searches it on
 *  its own scratch, since the search's arrays are each thread's own.  It
 *  keeps what it finds by the region's place.  The lines are then filed
 *  one region after another, and each finds its search kept.  A region
 *  can share a road with a region filed before it.  Once that road is
 *  trimmed, the region starts from other numbers and is searched again. */
typedef struct
{
    int          *region;
    int           n;
    SDL_AtomicInt next;
} IxWork;

static int ix_worker(void *ud)
{
    IxWork *w = (IxWork *)ud;
    for (;;)
    {
        int     k = SDL_AddAtomicInt(&w->next, 1), e;
        double *best, g;
        if (k >= w->n)
            break;
        e = w->region[k];
        if (ix_gather(e) <= 0)
            continue;
        best = (double *)malloc(sizeof *best * (size_t)(4 * s_np + 2));
        if (best)
        {
            uint64_t key = ix_key();
            if (!solved_get(topology_end(e)->id, key, best, 4 * s_np + 2))
                ix_solve(topology_end(e)->id, key, best, &g);
            free(best);
        }
    }
    ix_scratch_free();
    return 0;
}

static void ix_presolve(void)
{
    IxWork      w;
    SDL_Thread *th[32];
    int         e, nth = 0, k, cores = SDL_GetNumLogicalCPUCores();
    w.region = (int *)malloc(sizeof *w.region * (size_t)(topology_ends() + 1));
    w.n      = 0;
    SDL_SetAtomicInt(&w.next, 0);
    if (!w.region)
        return;
    for (e = 0; e < topology_ends(); e++)
        if (ix_region(e))
            w.region[w.n++] = e;
    for (k = 0; k + 1 < cores && k + 1 < w.n && nth < 32; k++)
        if ((th[nth] = SDL_CreateThread(ix_worker, "interchange", &w)) != NULL)
            nth++;
    if (nth)
        ix_worker(&w);
    for (k = 0; k < nth; k++)
        SDL_WaitThread(th[k], NULL);
    free(w.region);
}

/*  THE LINES AND HEIGHTS OF EVERY REGION'S ROADS (plans/interchanges.md, "The
 *  shape" and "The levels").  The places of each region are searched, and
 *  the best are filed. */
static int ix_lines(void)
{
    int e, rc = 0;
    for (e = 0; e < topology_ends() && rc == 0; e++)
    {
        const TopoEnd *te = topology_end(e);
        double        *best, g;
        uint64_t       key;
        int            i;
        if (!ix_region(e))
            continue;
        if ((i = ix_gather(e)) <= 0)
        {
            rc = i;
            continue;
        }
        best = (double *)malloc(sizeof *best * (size_t)(4 * s_np + 2));
        if (!best)
        {
            rc = -1;
            break;
        }
        key = ix_key();
        /*  An interchange searched before from the same takes the layout
         *  found then, with the bounds and the weight its search ended
         *  with. */
        if (solved_get(te->id, key, best, 4 * s_np + 2))
        {
            memcpy(s_plo, best + s_np, sizeof *s_plo * (size_t)s_np);
            memcpy(s_phi, best + 2 * s_np, sizeof *s_phi * (size_t)s_np);
            memcpy(s_p0, best + 3 * s_np, sizeof *s_p0 * (size_t)s_np);
            s_wradius = best[4 * s_np], s_xlift = best[4 * s_np + 1];
            ix_score(best, IX_HALVE_FILED, &g);
            goto searched;
        }
        if (ix_solve(te->id, key, best, &g) != 0)
        {
            free(best);
            rc = -1;
            break;
        }
    searched:
        {
            /*  A region the search cannot route is a placeholder: no line, and
             *  no road trimmed for it (plans/interchanges.md, "The shape"). */
            int bad = !ix_routed(g);
            if (bad)
            {
                if (arc_grow((void **)&s_xph, &s_cxph, s_nxph + 1, sizeof *s_xph) != 0)
                    rc = -1;
                else
                    s_xph[s_nxph++] = e;
                free(best);
                continue;
            }
        }
        ix_ease(fmax(g, ix_limit()), s_xz);
        for (i = 0; i < s_nxr && rc == 0; i++)
            if (s_xr[i].ok)
                rc = ix_file(i, s_xz);
        for (i = 0; i < s_nxa && rc == 0; i++)
            rc = ix_ap_file(i, best, s_xz);
        /*  Each arm's road ends at its gore, out along it, and each ramp's
         *  road starts where its strand reaches. */
        for (i = 0; i < s_nxa; i++)
            if (s_xa[i].out >= 0)
                ix_trim(s_xa[i].road, s_xa[i].contact, best[s_xa[i].out]);
        for (i = 0; i < s_nxn; i++)
            if (s_xn[i].reach >= 0)
                ix_trim(s_xn[i].ramp_road, s_xn[i].ramp_contact, best[s_xn[i].reach]);
        free(best);
    }
    return rc;
}

int shape_placeholder(int end)
{
    int k;
    for (k = 0; k < s_nxph; k++)
        if (s_xph[k] == end)
            return 1;
    return 0;
}

/*  How many regions have a crossing of their filed roads short of a
 *  level, or a road steeper than the grade.  That is what the shape
 *  keeps (plans/interchanges.md).  The crossings are found as the search
 *  finds them, on the filed lines and heights. */
static int ix_short_each(int hi, int lo)
{
    const IxSample *a = NULL, *b = NULL;
    int             i, q;
    float           zh = 0.0f, zl = 0.0f;
    for (i = 0; i < s_nxr && (!a || !b); i++)
        for (q = 0; q < s_xr[i].n; q++)
        {
            int v = ix_var(i, q);
            if (v == hi && !a)
                a = &s_xs[s_xr[i].s0 + q], zh = shape_height(s_xr[i].road, (float)a->s);
            if (v == lo && !b)
                b = &s_xs[s_xr[i].s0 + q], zl = shape_height(s_xr[i].road, (float)b->s);
        }
    if (a && b && zh - zl < 1.0f - 2e-2f)
        s_xshort++;
    return 0;
}

static int ix_check(int say)
{
    int e, n = 0;
    for (e = 0; e < topology_ends(); e++)
    {
        const TopoEnd *te = topology_end(e);
        if (te->kind != END_NODE || te->net != RNET_BAND || network_cell_region(topology_cell(te->cell0)) != e)
            continue;
        if (ix_gather(e) <= 0 || ix_sample(1) != 0)
            continue;
        s_xshort = 0;
        if (ix_crossings(ix_short_each) != 0)
            continue;
        if (s_xshort && say)
            dumpf("shape fault: region %016llx has %d samples of crossings short of a level\n", (unsigned long long)te->id, s_xshort);
        /*  And every road of it climbs no steeper than the grade of its kind. */
        {
            int i, k, steep = 0;
            for (i = 0; i < s_nxr; i++)
                for (k = s_road[s_xr[i].road].elev0; k < s_road[s_xr[i].road].elev0 + s_road[s_xr[i].road].nelev; k++)
                    if (fabs(s_elev[k].b) * LEVEL_TILES > ix_grade_share(i) * tan(MAX_GRADE * M_PI / 180) + 1e-3)
                    {
                        steep++;
                        break;
                    }
            if (steep && say)
                dumpf("shape fault: region %016llx has %d roads steeper than the grade\n", (unsigned long long)te->id, steep);
            s_xshort += steep;
        }
        n += s_xshort > 0;
    }
    return n;
}

/*  The outline round a junction's mouths: their convex hull, with every
 *  mouth's two points on it. */
static int boundaries(void)
{
    int j, i;
    s_njunction = network_junctions();
    s_bound     = (float **)calloc((size_t)(s_njunction ? s_njunction : 1), sizeof *s_bound);
    s_nbound    = (int *)calloc((size_t)(s_njunction ? s_njunction : 1), sizeof *s_nbound);
    s_bmouth    = (int **)calloc((size_t)(s_njunction ? s_njunction : 1), sizeof *s_bmouth);
    s_barm      = (int **)calloc((size_t)(s_njunction ? s_njunction : 1), sizeof *s_barm);
    s_cut       = (float *)calloc((size_t)(2 * network_roads() + 2), sizeof *s_cut);
    if (!s_bound || !s_nbound || !s_bmouth || !s_barm || !s_cut)
        return -1;
    for (j = 0; j < s_njunction; j++)
    {
        {
            float *cp;
            int   *cm, *ca, cn, got = corner_outline(j, &cp, &cm, &ca, &cn);
            if (got < 0)
                return -1;
            if (got)
            {
                s_bound[j] = cp, s_bmouth[j] = cm, s_barm[j] = ca, s_nbound[j] = cn;
                continue;
            }
        }
        int            n = 0, id = 0;
        const TopoEnd *te = topology_end(network_junction(j)->end);
        for (i = 0; i < s_nmouth; i++)
            n += 2 * (s_mouths[i].junction == j);
        if (!n)
            continue;
        if (te->net == RNET_ROAD)
            n += 8 * te->ncell;
        /*  Each point: x, y and its mouth. */
        float *pt = (float *)malloc(sizeof *pt * (size_t)n * 12);
        if (!pt)
            return -1;
        float *h  = pt + 3 * n;
        int    np = 0, k, m = 0;
        for (i = 0; i < s_nmouth; i++)
            if (s_mouths[i].junction == j)
            {
                for (k = 0; k < 2; k++)
                    pt[3 * np] = s_mouths[i].x[k], pt[3 * np + 1] = s_mouths[i].y[k], pt[3 * np + 2] = (float)id, np++;
                ++id;
            }
        /*  A street junction also reaches every edge its own tiles' pieces
         *  link across that leads out of it.  That holds whether or not a
         *  shaped road goes on there, as to a ramp.  An edge a piece joins
         *  and nothing across it joins back is no link, so it is no mouth.  Each such edge is a mouth a
         *  street's width wide at the edge's middle.  So the outline is the
         *  junction's own cells and stays square to its arms. */
        if (te->net == RNET_ROAD)
            for (k = 0; k < te->ncell; k++)
            {
                int32_t cell = topology_cell(te->cell0 + k), side = topology_side(), col = cell % side, row = cell / side;
                RPart   part[2];
                int     np2 = rcity_parts(s_city->xbld[cell], s_city->xbit[cell], part), q, e;
                for (q = 0; q < np2; q++)
                {
                    if (part[q].net != RNET_ROAD)
                        continue;
                    for (e = 0; e < 4; e++)
                    {
                        int32_t oc = col + rcity_edge_dc(1 << e), orr = row + rcity_edge_dr(1 << e), c2;
                        int     inside = 0;
                        double  mx = col + 0.5 + 0.5 * rcity_edge_dc(1 << e), my = row + 0.5 + 0.5 * rcity_edge_dr(1 << e), hw = network_width(RNET_ROAD, 0) / 2;
                        if (!(part[q].edges & (1 << e)) || !topology_linked(RNET_ROAD, cell, 1 << e))
                            continue;
                        for (c2 = 0; c2 < te->ncell; c2++)
                            inside |= topology_cell(te->cell0 + c2) == orr * side + oc;
                        if (inside)
                            continue;
                        pt[3 * np] = (float)(mx - rcity_edge_dr(1 << e) * hw), pt[3 * np + 1] = (float)(my + rcity_edge_dc(1 << e) * hw), pt[3 * np + 2] = (float)id, np++;
                        pt[3 * np] = (float)(mx + rcity_edge_dr(1 << e) * hw), pt[3 * np + 1] = (float)(my - rcity_edge_dc(1 << e) * hw), pt[3 * np + 2] = (float)id, np++;
                        ++id;
                    }
                }
            }
        qsort(pt, (size_t)np, sizeof(float) * 3, cmp_pt);
        /*  The same point twice is one point: a shaped road's mouth and the
         *  piece's edge give the same one. */
        {
            int w = 0;
            for (i = 0; i < np; i++)
                if (!w || fabsf(pt[3 * i] - pt[3 * (w - 1)]) > 1e-4f || fabsf(pt[3 * i + 1] - pt[3 * (w - 1) + 1]) > 1e-4f)
                    memcpy(&pt[3 * w], &pt[3 * i], sizeof(float) * 3), w++;
            np = w;
        }
        /*  The hull keeps every point on a straight side, so each mouth along
         *  a side stays an edge of its own. */
        for (i = 0; i < np; i++)
        {
            while (m >= 2 && cross3(h + 3 * (m - 2), h + 3 * (m - 1), pt + 3 * i) < -1e-9)
                m--;
            memcpy(h + 3 * m, pt + 3 * i, sizeof(float) * 3), m++;
        }
        int lower = m + 1;
        for (i = np - 2; i >= 0; i--)
        {
            while (m >= lower && cross3(h + 3 * (m - 2), h + 3 * (m - 1), pt + 3 * i) < -1e-9)
                m--;
            memcpy(h + 3 * m, pt + 3 * i, sizeof(float) * 3), m++;
        }
        m           = m > 1 ? m - 1 : m;
        s_bound[j]  = (float *)malloc(sizeof(float) * (size_t)m * 2);
        s_bmouth[j] = (int *)malloc(sizeof(int) * (size_t)m);
        if (!s_bound[j] || !s_bmouth[j])
        {
            free(pt);
            return -1;
        }
        for (i = 0; i < m; i++)
            s_bound[j][2 * i] = h[3 * i], s_bound[j][2 * i + 1] = h[3 * i + 1], s_bmouth[j][i] = (int)h[3 * i + 2];
        s_nbound[j] = m;
        free(pt);
    }
    return 0;
}

/* ---- the whole network ----------------------------------------------------- */

int shape_build(const RCity *c)
{
    int            i, nr = network_roads(), rc = 0;
    unsigned char *done;
    Run            r;
    shape_free();
    free(s_mouths);
    s_mouths = NULL, s_nmouth = s_capmouth = 0;
    s_city  = c;
    s_nroad = nr;
    s_road  = (SRoad *)calloc((size_t)(nr ? nr : 1), sizeof *s_road);
    done    = (unsigned char *)calloc((size_t)(nr ? nr : 1), 1);
    free(s_junction_of_end);
    s_junction_of_end = (int *)malloc(sizeof *s_junction_of_end * (size_t)(topology_ends() + 1));
    if (!s_road || !done || !s_junction_of_end)
    {
        free(done);
        return -1;
    }
    for (i = 0; i < topology_ends(); i++)
        s_junction_of_end[i] = -1;
    for (i = 0; i < network_junctions(); i++)
        if (s_junction_of_end[network_junction(i)->end] < 0)
            s_junction_of_end[network_junction(i)->end] = i;
    memset(&r, 0, sizeof r);
    /*  First, each highway junction's height: the highest floor any run
     *  that ends at it needs there.  Then every run is shaped, each highway
     *  run ending at its junctions' heights, so every arm meets its
     *  junction at one height. */
    free(s_jh);
    s_jh = (double *)malloc(sizeof *s_jh * (size_t)(network_junctions() + 1));
    if (!s_jh)
    {
        free(done);
        return -1;
    }
    for (i = 0; i < network_junctions(); i++)
        s_jh[i] = NAN;

    for (i = 0; i < nr && rc == 0; i++)
    {
        const NRoad *q = network_road(i);
        if (done[i] || q->topo < 0 || q->net != RNET_BAND || topology_road(q->topo)->nunit == 0)
            continue;
        if (gather(i, &r, done) != 0 || (r.ring && open_ring(&r) != 0) || deck_profile(&r, NULL) != 0)
            rc = -1;
    }
    /*  A highway's dead end that no run reaches stands CLEARANCE above the
     *  highest ground under its unit.  Across a road of no units it meets
     *  the junction beside it at one height: the higher of the two. */
    {
        int32_t side = topology_side();
        int     j, pass;
        for (j = 0; j < network_junctions(); j++)
        {
            const TopoEnd *te = topology_end(network_junction(j)->end);
            int            k, dc, dr, q;
            double         top = -1e18, h[4];
            if (!isnan(s_jh[j]) || te->net != RNET_BAND || te->kind != END_DEAD)
                continue;
            for (k = 0; k < te->ncell; k++)
                for (dc = 0; dc < 2; dc++)
                    for (dr = 0; dr < 2; dr++)
                    {
                        corners(topology_cell(te->cell0 + k) % side + dc, topology_cell(te->cell0 + k) / side + dr, h);
                        for (q = 0; q < 4; q++)
                            top = h[q] > top ? h[q] : top;
                    }
            if (te->ncell > 0)
                s_jh[j] = top + CLEARANCE;
        }
        for (pass = 0; pass < 2; pass++)
            for (i = 0; i < nr; i++)
            {
                const NRoad *q = network_road(i);
                int          a, b;
                if (q->net != RNET_BAND || q->topo < 0 || topology_road(q->topo)->nunit != 0 || q->pred.kind != NLINK_JUNCTION ||
                    q->succ.kind != NLINK_JUNCTION || q->pred.index < 0 || q->succ.index < 0)
                    continue;
                a = q->pred.index, b = q->succ.index;
                if (isnan(s_jh[a]) || isnan(s_jh[b]))
                    continue;
                if (topology_end(network_junction(a)->end)->kind == END_DEAD && s_jh[b] > s_jh[a])
                    s_jh[a] = s_jh[b];
                if (topology_end(network_junction(b)->end)->kind == END_DEAD && s_jh[a] > s_jh[b])
                    s_jh[b] = s_jh[a];
            }
    }
    /*  Then every run, in the order its heights are read.  The highway's
     *  runs first.  Then every other run but a ramp's road.  A ramp
     *  whose road reaches an interchange is shaped before the
     *  interchange, which starts its strand on it.  Then the
     *  interchanges, which set the highway's heights over each arm's
     *  approach.  Then each ramp's road whose line begins on the highway
     *  beside it: at a band junction, or at a gore.  Every height it
     *  reads is final. */
    {
        int pass;
        for (pass = 0; pass < 4 && rc == 0; pass++)
        {
            if (pass == 2)
            {
                ix_presolve();
                rc = ix_lines();
                ix_scratch_free();
                continue;
            }
            memset(done, 0, (size_t)(nr ? nr : 1));
            for (i = 0; i < nr && rc == 0; i++)
            {
                const NRoad *q = network_road(i);
                int          late;
                if (done[i] || q->topo < 0 || q->net < 0 || q->net >= RNET_COUNT || NET[q->net].size == 0 ||
                    topology_road(q->topo)->nunit == 0 || s_road[i].ngeom || (pass == 0) != (q->net == RNET_BAND))
                    continue;
                if (gather(i, &r, done) != 0 || (r.ring && open_ring(&r) != 0))
                {
                    rc = -1;
                    break;
                }
                late = !r.ring && (ramp_end(&r, 0) >= 0 || ramp_end(&r, 1) >= 0 || gore_ramp_run(&r));
                if ((pass == 0 || late == (pass == 3)) && shape_run(&r) != 0)
                    rc = -1;
            }
        }
    }
    free(r.u);
    free(done);
    if (rc == 0)
        rc = boundaries();
    if (rc == 0)
        rc = connections();
    if (rc == 0)
        rc = inner_lines();
    if (rc == 0)
        rc = vertical_curves();
    if (rc != 0)
        R_ERR("net", "the shape could not be built: out of memory");
    return rc;
}

/*  Profiles read from a file, through the same solves as a city's roads.
 *  Each line is `street` or `deck` and its samples `s,h`: a street's raw
 *  heights, or a deck's floor at its unit centers.  Each answer is said to
 *  the dump as `profile` and its samples.  0 on success. */
int shape_profiles(const char *path)
{
    FILE *f = fopen(path, "r");
    char  line[65536];
    int   rc = 0;
    if (!f)
    {
        R_ERR("net", "cannot read %s", path);
        return -1;
    }
    while (rc == 0 && fgets(line, sizeof line, f))
    {
        double s[4096], h[4096];
        int    n = 0, off = 0, used, deck;
        char   kind[16];
        if (sscanf(line, "%15s%n", kind, &off) != 1)
            continue;
        deck = strcmp(kind, "deck") == 0;
        if (!deck && strcmp(kind, "street") != 0)
            continue;
        while (n < 4096 && sscanf(line + off, " %lf,%lf%n", &s[n], &h[n], &used) == 2)
            n++, off += used;
        Prof p = {NULL, NULL, 0};
        if (deck)
            rc = deck_solve(n, s, h, &p, NULL, NULL);
        else
        {
            p.s = (double *)malloc(sizeof *p.s * (size_t)n);
            p.h = (double *)malloc(sizeof *p.h * (size_t)n);
            p.n = n;
            if (!p.s || !p.h)
                rc = -1;
            else
            {
                memcpy(p.s, s, sizeof *s * (size_t)n);
                rc = street_solve(n, s, h, NULL, p.h);
            }
        }
        if (rc == 0)
        {
            int k;
            dumpf("profile");
            for (k = 0; k < p.n; k++)
                dumpf(" %.4f,%.4f", p.s[k], p.h[k]);
            dumpf("\n");
        }
        free(p.s), free(p.h);
    }
    fclose(f);
    return rc;
}

static const SRoad NO_ROAD;

const SRoad *shape_road(int i)
{
    return i >= 0 && i < s_nroad ? &s_road[i] : &NO_ROAD;
}

int shape_at(int road, float s, float *x, float *y, float *hdg)
{
    const SRoad *r = shape_road(road);
    double       px, py, ph;
    if (!r->ngeom)
        return 0;
    geoms_at(&s_geom[r->geom0], r->ngeom, s, &px, &py, &ph);
    *x = (float)px, *y = (float)py, *hdg = (float)ph;
    return 1;
}

float shape_height(int road, float s)
{
    const SRoad *r = shape_road(road);
    int          k = r->elev0;
    if (!r->nelev)
        return 0;
    while (k + 1 < r->elev0 + r->nelev && s >= s_elev[k + 1].s)
        k++;
    {
        float ds = s - s_elev[k].s;
        return s_elev[k].a + ds * (s_elev[k].b + ds * (s_elev[k].c + ds * s_elev[k].d));
    }
}

double shape_land_at(double x, double y)
{
    int32_t n = topology_side(), c = (int32_t)floor(x), r = (int32_t)floor(y);
    c = c < 0 ? 0 : c >= n ? n - 1
                           : c;
    r = r < 0 ? 0 : r >= n ? n - 1
                           : r;
    return plane_at(c, r, x, y);
}

float shape_s(int road, float s_units)
{
    const SRoad *r = shape_road(road);
    /*  A road with no map of its own runs evenly over its units. */
    if (!r->nmap)
        return network_road(road)->length > 1e-6f ? s_units / network_road(road)->length * r->length : s_units;
    int k = (int)floorf(s_units);
    if (k < 0)
        return s_map[r->map0];
    if (k >= r->nmap - 1)
        return s_map[r->map0 + r->nmap - 1];
    float f = s_units - (float)k;
    return s_map[r->map0 + k] + (s_map[r->map0 + k + 1] - s_map[r->map0 + k]) * f;
}

/*  How far along its units a point `s` along a road's line lies: the
 *  inverse of shape_s.  A road with no map of its own runs evenly over its
 *  units. */
float shape_units(int road, float s)
{
    const SRoad *r = shape_road(road);
    int          k;
    if (!r->nmap)
        return r->length > 1e-6f ? s / r->length * network_road(road)->length : s;
    for (k = 0; k + 1 < r->nmap; k++)
        if (s <= s_map[r->map0 + k + 1] || k + 2 == r->nmap)
        {
            float a = s_map[r->map0 + k], b = s_map[r->map0 + k + 1];
            return (float)k + (b > a ? (s - a) / (b - a) : 0.0f);
        }
    return 0.0f;
}

/*  A road's width at `s` along its line: the sum of its lanes' widths
 *  there (layer 2). */
float shape_width(int road, float s)
{
    return network_road_width(road, shape_units(road, s));
}

float shape_junction_height(int junction)
{
    return junction >= 0 && junction < s_njunction && s_jh ? (float)s_jh[junction] : NAN;
}

int shape_boundary_arm(int junction, int k, int *road, int *contact)
{
    int a;
    if (junction < 0 || junction >= s_njunction || !s_barm || !s_barm[junction])
        return 0;
    a = s_barm[junction][k % s_nbound[junction]];
    if (a < 0)
        return 0;
    *road = a / 2, *contact = a % 2;
    return 1;
}

float shape_cut(int road, int contact)
{
    return s_cut && road >= 0 && road < s_nroad ? s_cut[2 * road + contact] : 0.0f;
}

int shape_boundary_mouth(int junction, int k)
{
    int n;
    if (junction < 0 || junction >= s_njunction || !s_bmouth || !s_bmouth[junction])
        return 0;
    n = s_nbound[junction];
    return s_bmouth[junction][k % n] == s_bmouth[junction][(k + 1) % n];
}

int shape_boundary(int junction, const float **xy)
{
    if (junction < 0 || junction >= s_njunction || !s_bound[junction])
        return 0;
    *xy = s_bound[junction];
    return s_nbound[junction];
}

/* ---- the check --------------------------------------------------------------- */

/*  The shape's properties on what it filed:
 *   - each road's line is continuous, piece to piece;
 *   - both edges of each road stay inside its corridor, said and not counted: its own units, the
 *     junction unit at each end or the next road's unit across a seam,
 *     widened by MARGIN;
 *   - two roads linked across a seam meet in place, heading and height;
 *   - a deck stands at least CLEARANCE above the ground under each unit,
 *     over the whole of the unit, and climbs no steeper than MAX_GRADE.
 *  The count of faults. */
int shape_check(int say)
{
    int     i, faults = 0, shaped = 0, stray = 0, gaps = 0, seams = 0, low = 0, steep = 0;
    int32_t side = topology_side();
    for (i = 0; i < s_nroad; i++)
    {
        const SRoad *r = &s_road[i];
        /*  A connecting road runs on no units, and the junction it is part
         *  of is held to its own check. */
        if (!r->ngeom || network_road(i)->topo < 0)
            continue;
        shaped++;
        const NRoad    *nr   = network_road(i);
        const TopoRoad *t    = topology_road(nr->topo);
        int             size = NET[nr->net].size, k;
        double          w = network_width(nr->net, 0), h = size * 0.5 + MARGIN;
        /*  Continuity. */
        for (k = r->geom0; k + 1 < r->geom0 + r->ngeom; k++)
        {
            const SGeom *g = &s_geom[k], *q = &s_geom[k + 1];
            float        x, y, hd;
            shape_at(i, q->s - 1e-4f, &x, &y, &hd);
            if (hypotf(x - q->x, y - q->y) > 2e-3f)
            {
                gaps++;
                if (say)
                    dumpf("shape fault: a gap in the line of road %016llx at s %.2f\n", (unsigned long long)nr->id, (double)q->s);
            }
            (void)g;
        }
        /*  The corridor. */
        int    nu = t->nunit + 4, m = 0;
        RUnit *cu = (RUnit *)malloc(sizeof *cu * (size_t)nu);
        if (!cu)
            return -1;
        for (k = 0; k < t->nunit; k++)
        {
            int32_t c = topology_cell(t->unit0 + k);
            cu[m].col = c % side, cu[m].row = c / side, m++;
        }
        for (k = 0; k < 2; k++)
        {
            NLink l = k ? nr->succ : nr->pred;
            if (t->at[k] >= 0)
                cu[m].col = t->at[k] % side, cu[m].row = t->at[k] / side, m++;
            else if (topology_end(t->end[k])->kind == END_GORE && !t->nunit)
            {
                /*  A road of no units at a gore lies on the units either
                 *  side of the gore's edge. */
                const TopoEnd *ge = topology_end(t->end[k]);
                int            q;
                for (q = 0; q < ge->ncell && q < 2; q++)
                {
                    int32_t c = topology_cell(ge->cell0 + q);
                    cu[m].col = c % side, cu[m].row = c / side, m++;
                }
            }
            else if (l.kind == NLINK_ROAD && network_road(l.index)->topo >= 0)
            {
                /*  Across a seam, the unit of the next road at the seam. */
                const TopoRoad *o = topology_road(network_road(l.index)->topo);
                if (o->nunit)
                {
                    int32_t c = topology_cell(o->unit0 + (l.contact ? o->nunit - 1 : 0));
                    cu[m].col = c % side, cu[m].row = c / side, m++;
                }
            }
        }
        double worst = 0, s;
        for (s = 0; s <= r->length; s += 0.05)
        {
            float x, y, hd;
            shape_at(i, (float)s, &x, &y, &hd);
            int sd;
            for (sd = -1; sd <= 1; sd += 2)
            {
                double ex = x - sin(hd) * sd * w / 2, ey = y + cos(hd) * sd * w / 2, best = 1e18;
                int    q;
                if (ex < 0 || ey < 0 || ex > side || ey > side)
                    continue; /* past the map's edge */
                for (q = 0; q < m; q++)
                {
                    double dx = fabs(ex - (cu[q].col + size * 0.5)) - h, dy = fabs(ey - (cu[q].row + size * 0.5)) - h;
                    double d = dx > dy ? dx : dy;
                    d        = d > 0 ? d : 0;
                    best     = d < best ? d : best;
                }
                worst = best > worst ? best : worst;
            }
        }
        free(cu);
        if (worst > 1e-2)
        {
            stray++;
            if (say)
                dumpf("shape note: road %016llx strays %.3f tiles outside its corridor\n", (unsigned long long)nr->id, worst);
        }
        /*  A seam: the road its end links to across it starts where it ends. */
        if (nr->succ.kind == NLINK_ROAD && shape_road(nr->succ.index)->ngeom)
        {
            int   o = nr->succ.index;
            float x0, y0, h0, x1, y1, h1;
            float so = nr->succ.contact ? shape_road(o)->length : 0;
            shape_at(i, r->length, &x0, &y0, &h0);
            shape_at(o, so, &x1, &y1, &h1);
            if (nr->succ.contact)
                h1 += (float)M_PI;
            double dh = fabs(remainder((double)h0 - h1, 2 * M_PI));
            double dz = fabs(shape_height(i, r->length) - shape_height(o, so));
            if (hypotf(x0 - x1, y0 - y1) > 1e-2f || dh > 1e-2 || dz > 1e-2)
            {
                seams++;
                if (say)
                    dumpf("shape fault: road %016llx and its next across a seam part by %.3f tiles, %.3f radians, "
                          "%.3f levels\n",
                          (unsigned long long)nr->id,
                          (double)hypotf(x0 - x1, y0 - y1),
                          dh,
                          dz);
            }
        }
        /*  A deck's clearance at each unit's center. */
        if (nr->net == RNET_BAND)
            for (k = 0; k < t->nunit; k++)
            {
                int32_t c   = topology_cell(t->unit0 + k);
                double  top = -1e18, hc[4];
                int     dc, dr, q;
                for (dc = 0; dc < size; dc++)
                    for (dr = 0; dr < size; dr++)
                    {
                        corners(c % side + dc, c / side + dr, hc);
                        for (q = 0; q < 4; q++)
                            top = hc[q] > top ? hc[q] : top;
                    }
                float z = shape_height(i, shape_s(i, (float)k)), q2;
                for (q2 = 0.1f; q2 <= 1.0f; q2 += 0.1f)
                {
                    float zq = shape_height(i, shape_s(i, (float)k + q2));
                    z        = zq < z ? zq : z;
                }
                if (z < top + CLEARANCE - 1e-2)
                {
                    low++;
                    if (say)
                        dumpf("shape fault: the deck of road %016llx stands %.2f levels over its unit %d, under %.0f\n",
                              (unsigned long long)nr->id,
                              (double)z - top,
                              k,
                              CLEARANCE);
                    break;
                }
            }
        if (nr->net == RNET_BAND)
            for (k = r->elev0; k < r->elev0 + r->nelev; k++)
                if (fabs(s_elev[k].b) * LEVEL_TILES > tan(MAX_GRADE * M_PI / 180) + 1e-3)
                {
                    steep++;
                    if (say)
                        dumpf("shape fault: the deck of road %016llx climbs at %.1f degrees\n", (unsigned long long)nr->id, atan(fabs(s_elev[k].b) * LEVEL_TILES) * 180 / M_PI);
                    break;
                }
    }
    /*  The fit holds the edges of each corner inside the corridor, not every
     *  point of a straight.  So an edge that strays is said, and is no fault. */
    {
        int back = rg_backward(say), cross = ix_check(say), k;
        faults = gaps + seams + low + steep + back + cross;
        for (k = 0; say && k < s_nxph; k++)
            dumpf("shape note: interchange %016llx cannot be routed, and stands as a placeholder\n",
                  (unsigned long long)topology_end(s_xph[k])->id);
        if (say)
            dumpf("shape: %d roads shaped; %d gaps, %d stray from their corridor, %d seams apart, %d decks low, %d decks "
                  "steep, %d strands against their traffic, %d regions with crossings short of a level or too steep, %d interchanges "
                  "placeholders\n",
                  shaped,
                  gaps,
                  stray,
                  seams,
                  low,
                  steep,
                  back,
                  cross,
                  s_nxph);
    }
    return faults;
}

/*  THE LANE LINKS IN THE WORLD.  A lane's two borders at one of its ends, where its section starts or ends, as points.  They lie at the `t` layer 2 gives them, across layer 3's line there.  Also the point of the line there, its
 *  heading and its height.  0 where its road has no line. */
static int lane_end(int lane, int end, double in[2], double out[2], double at[2], double *hdg, double *z)
{
    const NLane    *l = network_lane(lane);
    const NRoad    *r = network_road(l->road);
    const NSection *x = network_section(l->section);
    float           u, ti, to, px, py, h;
    if (!shape_road(l->road)->ngeom)
        return 0;
    u = end ? (l->section + 1 < r->sec0 + r->nsec ? network_section(l->section + 1)->s : r->length) : x->s;
    network_lane_t(lane, u - x->s, &ti, &to);
    shape_at(l->road, lane_s(lane, end, u), &px, &py, &h);
    at[0] = px, at[1] = py, *hdg = h, *z = shape_height(l->road, lane_s(lane, end, u));
    in[0] = px - sin(h) * ti, in[1] = py + cos(h) * ti;
    out[0] = px - sin(h) * to, out[1] = py + cos(h) * to;
    return 1;
}

/*  Whether a lane lies on a road of no units: the edge two ends share,
 *  with no line. */
static int no_units(int lane)
{
    const NRoad *r = network_road(network_lane(lane)->road);
    return r->topo >= 0 && topology_road(r->topo)->nunit == 0 && !s_road[network_lane(lane)->road].ngeom;
}

/*  The lists of each lane end's links (first, next, other), with every
 *  link into a road of no units carried through it.  A lane end that
 *  links into a lane of one is listed as linking with every lane end
 *  beyond it.  Those are the lane ends that lane goes on into at its
 *  other end.  A road of no units beside another is carried through too.
 *  0, or -1 out of memory. */
static int through_no_units(int nl, int *first, int **next_io, int **other_io)
{
    int *next = *next_io, *other = *other_io, *nf, *nn = NULL, *no = NULL, cap = 0, n = 0, a, e;
    nf = (int *)malloc(sizeof(int) * (size_t)(2 * nl + 1));
    if (!nf)
        return -1;
    for (a = 0; a < 2 * nl; a++)
    {
        int stack[64], depth = 0;
        nf[a] = -1;
        if (no_units(a / 2))
            continue;
        for (e = first[a]; e >= 0; e = next[e])
            if (depth < 64)
                stack[depth++] = other[e];
        while (depth > 0)
        {
            int b = stack[--depth], f, seen = 0, g;
            if (no_units(b / 2))
            {
                for (f = first[b ^ 1]; f >= 0 && depth < 64; f = next[f])
                    if (other[f] != (b ^ 1) && other[f] / 2 != b / 2)
                        stack[depth++] = other[f];
                continue;
            }
            for (g = nf[a]; g >= 0 && !seen; g = nn[g])
                seen = no[g] == b;
            if (seen || b == a)
                continue;
            if (n >= cap)
            {
                int  c2 = cap ? 2 * cap : 1024;
                int *x  = (int *)realloc(nn, sizeof(int) * (size_t)c2), *y;
                if (!x)
                {
                    free(nf), free(no);
                    return -1;
                }
                nn = x;
                y  = (int *)realloc(no, sizeof(int) * (size_t)c2);
                if (!y)
                {
                    free(nf), free(nn);
                    return -1;
                }
                no = y, cap = c2;
            }
            no[n] = b, nn[n] = nf[a], nf[a] = n, n++;
        }
    }
    memcpy(first, nf, sizeof(int) * (size_t)(2 * nl));
    free(nf), free(next), free(other);
    *next_io = nn, *other_io = no;
    return 0;
}

/*  Where lanes meet at a link, their borders meet.  At each end of a
 *  lane, take every lane of another section it links with.  Then take the
 *  lanes of its own section those link with.  Each side, taken together,
 *  spans what the other spans, and no lane of the first lies outside it.  So a lane that goes on into one lane
 *  meets it border to border.  Two lanes that merge into one lie side by
 *  side across it.  Measured across the lane's own end, and along it,
 *  within LINK_APART.  A link to a road with no line cannot be measured
 *  and is counted.  Answers how many lane ends are apart. */
#define LINK_APART 0.01
int shape_lane_check(int say)
{
    int  nl = network_lanes(), nk = network_links(), i, k, held = 0, apart = 0, unmeasured = 0, said = 0;
    int *first = (int *)calloc((size_t)(2 * nl + 1), sizeof(int)), *next = (int *)malloc(sizeof(int) * (size_t)(2 * nk + 1));
    int *other = (int *)malloc(sizeof(int) * (size_t)(2 * nk + 1));
    if (!first || !next || !other)
    {
        free(first), free(next), free(other);
        return -1;
    }
    /*  Each lane end's links, as lists: the lane end at the other side. */
    for (i = 0; i < 2 * nl; i++)
        first[i] = -1;
    for (k = 0; k < nk; k++)
    {
        const NLaneLink *lk = network_link(k);
        int              a = 2 * lk->from + lk->from_end, b = 2 * lk->to + lk->to_end;
        other[2 * k] = b, next[2 * k] = first[a], first[a] = 2 * k;
        other[2 * k + 1] = a, next[2 * k + 1] = first[b], first[b] = 2 * k + 1;
    }
    /*  A road of no units is the edge two ends share, of no length.  A
     *  lane that links into one is measured against the lanes it goes on
     *  into across it.  Its own lanes are passed over. */
    if (through_no_units(nl, first, &next, &other) != 0)
    {
        free(first), free(next), free(other);
        return -1;
    }
    for (i = 0; i < 2 * nl; i++)
    {
        double in[2], out[2], at[2], h, z, nx, ny, tx, ty;
        int    e, done[64], nd = 0, mine = network_lane(i / 2)->section;
        if (first[i] < 0 || no_units(i / 2))
            continue;
        if (!lane_end(i / 2, i & 1, in, out, at, &h, &z))
        {
            unmeasured++;
            if (say > 1)
                dumpf("lane links  not measured: lane %d of road %016llx at its %s: its road has no line\n",
                      network_lane(i / 2)->id,
                      (unsigned long long)network_road(network_lane(i / 2)->road)->id,
                      (i & 1) ? "end" : "start");
            continue;
        }
        nx = -sin(h), ny = cos(h), tx = cos(h), ty = sin(h);
        /*  The two sides of the node it ends at: every lane end linked
         *  with it, and every lane end linked with those, back and
         *  forth.  The two sides span the same, and each lane of the far
         *  side lies inside that span.  Lanes that part or join side by
         *  side add up, and lanes that part or join as alternatives each
         *  cover it. */
        for (e = first[i]; e >= 0; e = next[e])
        {
            int    sec = network_lane(other[e] / 2)->section, q, f, g, seen = 0, ok = 1;
            double lo[2] = {1e18, 1e18}, hi[2] = {-1e18, -1e18}, along = 0.0, gap, rise = 0.0;
            double inner = 0.0;
            (void)q;
            if (nd > 0 || sec == mine)
                continue;
            done[nd++] = sec;
            (void)seen;
            /*  The lane ends of other sections it links with, then the lane
             *  ends of its own section those link with. */
            {
                int    set[2][64], n[2] = {0, 0}, x, y, side;
                double slo[64], shi[64];
                /*  The two sides of the node are the lane ends linked
                 *  with this one, then every lane end linked with those.
                 *  It goes back and forth until neither side grows. */
                int grew       = 1;
                set[1][n[1]++] = i;
                while (grew)
                {
                    grew = 0;
                    for (side = 0; side < 2; side++)
                        for (x = 0; x < n[!side]; x++)
                            for (g = first[set[!side][x]]; g >= 0; g = next[g])
                            {
                                int dup = 0;
                                for (y = 0; y < n[side]; y++)
                                    dup |= set[side][y] == other[g];
                                if (!dup && n[side] < 64)
                                    set[side][n[side]++] = other[g], grew = 1;
                            }
                }
                (void)f;
                for (side = 0; side < 2 && ok; side++)
                    for (x = 0; x < n[side] && ok; x++)
                    {
                        double gin[2], gout[2], gat[2], gh, gz;
                        int    p;
                        if (!lane_end(set[side][x] / 2, set[side][x] & 1, gin, gout, gat, &gh, &gz))
                        {
                            ok = 0;
                            if (say > 1)
                            {
                                const NRoad *o  = network_road(network_lane(set[side][x] / 2)->road);
                                int          jk = o->junction >= 0 ? topology_end(network_junction(o->junction)->end)->kind : -1;
                                dumpf("lane links  not measured: lane %d of road %016llx at its %s: road %016llx it links "
                                      "with has no line, in a junction of kind %d\n",
                                      network_lane(i / 2)->id,
                                      (unsigned long long)network_road(network_lane(i / 2)->road)->id,
                                      (i & 1) ? "end" : "start",
                                      (unsigned long long)o->id,
                                      jk);
                            }
                            break;
                        }
                        if (side == 0)
                            slo[x] = 1e18, shi[x] = -1e18;
                        /*  Every lane end at the node stands at its height, in
                         *  tiles as the rest is. */
                        rise = fmax(rise, fabs(gz - z) * LEVEL_TILES);
                        for (p = 0; p < 2; p++)
                        {
                            const double *pt = p ? gout : gin;
                            double        v  = (pt[0] - at[0]) * nx + (pt[1] - at[1]) * ny;
                            lo[side] = fmin(lo[side], v), hi[side] = fmax(hi[side], v);
                            along = fmax(along, fabs((pt[0] - at[0]) * tx + (pt[1] - at[1]) * ty));
                            if (side == 0)
                                slo[x] = fmin(slo[x], v), shi[x] = fmax(shi[x], v);
                        }
                    }
                /*  How far a lane of the far side lies outside the span. */
                for (x = 0; ok && x < n[0]; x++)
                    inner = fmax(inner, fmax(lo[1] - slo[x], shi[x] - hi[1]));
            }
            if (!ok)
            {
                unmeasured++;
                continue;
            }
            gap = fmax(fmax(fmax(along, inner), fmax(fabs(lo[0] - lo[1]), fabs(hi[0] - hi[1]))), rise);
            if (gap <= LINK_APART)
            {
                held++;
                continue;
            }
            apart++;
            if (say && (say > 1 || said++ < 40))
            {
                const NLane *l = network_lane(i / 2);
                dumpf("lane links  apart by %.3f: lane %d of road %016llx at its %s, near %.2f,%.2f, against road %016llx: "
                      "its section's lanes span %.3f to %.3f, the lanes it links with %.3f to %.3f, %.3f along, %.3f in height\n",
                      gap,
                      l->id,
                      (unsigned long long)network_road(l->road)->id,
                      (i & 1) ? "end" : "start",
                      at[0],
                      at[1],
                      (unsigned long long)network_road(network_lane(other[e] / 2)->road)->id,
                      lo[1],
                      hi[1],
                      lo[0],
                      hi[0],
                      along,
                      rise);
            }
        }
    }
    if (say)
        dumpf("lane links  %d held, %d apart, %d not measured: a road there has no line\n", held, apart, unmeasured);
    free(first), free(next), free(other);
    return apart;
}

void shape_dump(void)
{
    int i, k;
    for (i = 0; i < s_nroad; i++)
    {
        const SRoad *r = &s_road[i];
        if (!r->ngeom)
            continue;
        dumpf("sroad %016llx length %.3f\n", (unsigned long long)network_road(i)->id, (double)r->length);
        for (k = r->geom0; k < r->geom0 + r->ngeom; k++)
        {
            const SGeom *g = &s_geom[k];
            if (g->poly)
                dumpf("  geometry s %.3f x %.3f y %.3f hdg %.4f length %.3f paramPoly3 u %.4f %.4f %.4f %.4f v %.4f %.4f %.4f %.4f\n",
                      (double)g->s,
                      (double)g->x,
                      (double)g->y,
                      (double)g->hdg,
                      (double)g->length,
                      (double)g->u[0],
                      (double)g->u[1],
                      (double)g->u[2],
                      (double)g->u[3],
                      (double)g->v[0],
                      (double)g->v[1],
                      (double)g->v[2],
                      (double)g->v[3]);
            else if (g->spiral)
                dumpf("  geometry s %.3f x %.3f y %.3f hdg %.4f length %.3f spiral %.4f %.4f\n", (double)g->s, (double)g->x, (double)g->y, (double)g->hdg, (double)g->length, (double)g->curvature, (double)g->cend);
            else
                dumpf("  geometry s %.3f x %.3f y %.3f hdg %.4f length %.3f curvature %.4f\n", (double)g->s, (double)g->x, (double)g->y, (double)g->hdg, (double)g->length, (double)g->curvature);
        }
        for (k = r->elev0; k < r->elev0 + r->nelev; k++)
            dumpf("  elevation s %.3f a %.3f b %.4f c %.4f d %.4f\n", (double)s_elev[k].s, (double)s_elev[k].a, (double)s_elev[k].b, (double)s_elev[k].c, (double)s_elev[k].d);
        dumpf("  map");
        for (k = r->map0; k < r->map0 + r->nmap; k++)
            dumpf(" %.3f", (double)s_map[k]);
        dumpf("\n");
    }
    for (i = 0; i < s_njunction; i++)
    {
        if (!s_nbound[i])
            continue;
        dumpf("sjunction %016llx height %.3f boundary", (unsigned long long)network_junction(i)->id, (double)shape_junction_height(i));
        for (k = 0; k < s_nbound[i]; k++)
            dumpf(" %.3f,%.3f", (double)s_bound[i][2 * k], (double)s_bound[i][2 * k + 1]);
        dumpf("\n");
    }
}

/* ---- the state ----------------------------------------------------------- */

/*  Every variable of this file, as the store saves and loads it whole
 *  (store.h).  The release frees everything the variables own. */
/* clang-format off */
#define SHAPE_STATE(X) \
    X(s_road) X(s_nroad) X(s_geom) X(s_ngeom) X(s_capgeom) X(s_elev) X(s_nelev) X(s_capelev) \
    X(s_map) X(s_nmap) X(s_capmap) X(s_bound) X(s_nbound) X(s_bmouth) X(s_barm) X(s_cut) \
    X(s_njunction) X(s_city) X(s_junction_of_end) X(s_mouths) X(s_nmouth) X(s_capmouth) X(s_jh) \
    X(s_xph) X(s_nxph) X(s_cxph)
/* clang-format on */
NET_STATE(shape, SHAPE_STATE)

void shape_release(void)
{
    shape_free();
    free(s_junction_of_end), free(s_mouths), free(s_jh);
    s_junction_of_end = NULL, s_mouths = NULL, s_jh = NULL;
    s_nmouth = s_capmouth = 0;
}
