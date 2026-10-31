/*  draw/net_graph.c: THE LANE GRAPH LAID OUT ON THE MAP, from the road
 *  network's records alone (plans/network.md, layers 1 and 2).  Nothing
 *  is fitted, and nothing here decides what the network holds.
 *
 *      Every lane and every sidewalk of a road the topology found lies
 *      across the road's line by its signed id, and a section's lanes are
 *      centered on the line.  So each lane crosses each cell border at a
 *      PORT of its own.  Inside a cell it runs from its port on one border
 *      to its port on the next, rounded where the road turns.  Every node
 *      of the lane graph is placed.  A node at a road's contact lies at
 *      the port of a lane that ends there.  A node inside a junction where
 *      an exit lane divides lies a step into the junction from the arm the
 *      exit lane leaves.  A node where lanes join an entry lane lies a
 *      step back from the arm it enters.  A node that a lane from another
 *      arm also meets lies level with that arm's mouth.  A lane that ends
 *      at a junction with nothing beyond is a dead end, drawn on across
 *      the junction to a node on its far side.  Every lane of a connecting
 *      road runs, rounded, from the node at its start to the node at its
 *      end.  A lane link between two ends that do not lie in the same
 *      place is a gray line between them.  A junction's cells have one
 *      outline round them all, and a thin line between two of its own
 *      cells.  Each unit of a road has a thin border.
 *
 *  A lane is drawn in the color of its type, with an arrow the way it
 *  runs.  A sidewalk is walked both ways and has none.  A lane the check
 *  fails is drawn in the fault's color. */
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "net_graph.h"
#include "network/network.h"
#include "network/shape.h"
#include "network/topology.h"

const uint32_t    NG_RGB[NG_COLORS]  = {0x1a1a1a, 0x299e3b, 0xbf7a00, 0x2e70d6, 0x8a592b, 0xe60000, 0x8c8c8c, 0xffb800, 0xc4c4c4};
const char *const NG_NAME[NG_COLORS] = {"driving lane", "entry lane", "exit lane", "sidewalk", "rail lane", "fails the lane check", "lane link", "junction", "road cell"};

#define GAP        0.075f /* how far apart across a road of one-cell units its ports lie */
#define LANE_HALF  0.015f /* half a lane's line */
#define NODE_HALF  0.04f  /* half a junction outline's line */
#define ARROW      0.07f  /* an arrow's length */
#define DEPTH      0.35f  /* how far into a junction a node inside it lies */
#define INSET      0.06f  /* how far inside its cells a junction's outline lies */
#define STRETCH    0.05f  /* half the line of a road of no units, across the border it lies on */
#define LINK_APART 0.01f  /* two ends nearer than this lie in the same place */
#define ROUND      8      /* the straights a rounded corner is drawn with */

typedef struct
{
    NgP   *p;
    float *lim; /* at each point, how far before and after it its corner is rounded */
    int   *mid; /* at each point, 1 where it is the middle of a unit */
    int    n, cap;
} Line;

static NgP v(float x, float y)
{
    NgP p = {x, y};
    return p;
}

static NgP sub(NgP a, NgP b)
{
    return v(a.x - b.x, a.y - b.y);
}

static float len(NgP a)
{
    return sqrtf(a.x * a.x + a.y * a.y);
}

static NgP unit(NgP a)
{
    float l = len(a);
    return l > 1e-9f ? v(a.x / l, a.y / l) : v(1.0f, 0.0f);
}

static float dot(NgP a, NgP b)
{
    return a.x * b.x + a.y * b.y;
}

/*  Right of the way (dx, dy) runs is (dy, -dx) on the map, as the
 *  network places a lane by its signed id. */
static NgP right(NgP d)
{
    return v(d.y, -d.x);
}

static int push(Line *l, NgP p, float lim, int mid)
{
    if (l->n >= l->cap)
    {
        int    nc = l->cap ? l->cap * 2 : 64;
        NgP   *np = (NgP *)realloc(l->p, sizeof *np * (size_t)nc);
        float *nl = np ? (float *)realloc(l->lim, sizeof *nl * (size_t)nc) : NULL;
        int   *nm = nl ? (int *)realloc(l->mid, sizeof *nm * (size_t)nc) : NULL;
        if (np)
            l->p = np;
        if (nl)
            l->lim = nl;
        if (!nm)
            return -1;
        l->mid = nm, l->cap = nc;
    }
    l->p[l->n] = p, l->lim[l->n] = lim, l->mid[l->n] = mid;
    l->n++;
    return 0;
}

static void line_free(Line *l)
{
    free(l->p), free(l->lim), free(l->mid);
    memset(l, 0, sizeof *l);
}

/*  How far a unit reaches from its middle to its border: a band's unit
 *  is two cells by two. */
static float ext(int net)
{
    return net == RNET_BAND ? 1.0f : 0.5f;
}

static NgP middle(int net, int32_t cell)
{
    int32_t side = topology_side();
    return v((float)(cell % side) + ext(net), (float)(cell / side) + ext(net));
}

static NgP step_of(int edge)
{
    return v((float)rcity_edge_dc(edge),
             (float)rcity_edge_dr(edge));
}

/*  A lane's place across its road.  A negative id lies right of the way
 *  the road runs, and a positive id left of it, a port apart for each. */
static float across(int net, int id)
{
    return (id < 0 ? 1.0f : -1.0f) * ((float)abs(id) - 0.5f) * GAP * ext(net) * 2.0f;
}

/*  A road's line through its cells: from the border at its first end,
 *  through the middle of each unit, to the border at its last.  A road of
 *  no units lies across the border between the units at its two ends. */
static int road_line(const TopoRoad *t, Line *out)
{
    float e = ext(t->net);
    int   k;
    out->n = 0;
    if (t->nunit > 0)
    {
        NgP a = middle(t->net, topology_cell(t->unit0)), b = middle(t->net, topology_cell(t->unit0 + t->nunit - 1));
        NgP s0 = step_of(t->edge[0]), s1 = step_of(t->edge[1]);
        if (push(out, v(a.x + s0.x * e, a.y + s0.y * e), 0.0f, 0) != 0)
            return -1;
        for (k = 0; k < t->nunit; k++)
            if (push(out, middle(t->net, topology_cell(t->unit0 + k)), 0.0f, 1) != 0)
                return -1;
        return push(out, v(b.x + s1.x * e, b.y + s1.y * e), 0.0f, 0);
    }
    if (t->at[0] >= 0 && t->at[1] >= 0)
    {
        /*  The border lies half a unit from the middle of the unit at an
         *  end of the road's own network, toward the other end. */
        int c  = topology_end(t->end[0])->net == RNET_BAND && t->net != RNET_BAND;
        NgP ma = middle(t->net, t->at[c]), d = step_of(t->edge[c]), m = v(ma.x + d.x * ext(t->net), ma.y + d.y * ext(t->net));
        if (c)
            d = v(-d.x, -d.y);
        if (push(out, v(m.x - d.x * STRETCH, m.y - d.y * STRETCH), 0.0f, 0) != 0)
            return -1;
        return push(out, v(m.x + d.x * STRETCH, m.y + d.y * STRETCH), 0.0f, 0);
    }
    if (t->at[0] >= 0 || t->at[1] >= 0)
    {
        int c = t->at[0] >= 0 ? 0 : 1;
        NgP m = middle(t->net, t->at[c]), s = step_of(t->edge[1 - c]), far = v(m.x + 2.0f * e * s.x, m.y + 2.0f * e * s.y);
        if (push(out, c == 0 ? m : far, 0.0f, 0) != 0)
            return -1;
        return push(out, c == 0 ? far : m, 0.0f, 0);
    }
    return 0;
}

/*  Whether road i is a ramp's road that leaves or joins the band at a gore
 *  along it.  Its road runs from the gore (plans/network.md, "A ramp's
 *  road"), so its line is its shaped line, not the line of its cell. */
static int gore_ramp(int i)
{
    int k;
    for (k = 0; k < network_ramps(); k++)
    {
        const NRamp *rp = network_ramp(k);
        if (rp->road == i && rp->built && rp->gore >= 0 && rp->region < 0)
            return 1;
    }
    return 0;
}

/*  A road's shaped line, sampled.  0 where it has none. */
static int shaped_line(int road, Line *out)
{
    const SRoad *sr = shape_road(road);
    int          n, k;
    out->n = 0;
    if (!sr || !sr->ngeom || sr->length <= 1e-6f)
        return 0;
    n = (int)ceilf(sr->length / 0.1f);
    n = n < 2 ? 2 : n;
    for (k = 0; k <= n; k++)
    {
        float x, y, h;
        if (!shape_at(road, sr->length * (float)k / (float)n, &x, &y, &h))
            return 0;
        if (push(out, v(x, y), 0.0f, 0) != 0)
            return -1;
    }
    return 0;
}

/*  The stretch of a line between fractions f0 and f1 of its length. */
static int cut(const Line *in, float f0, float f1, Line *out)
{
    float total = 0.0f, acc, want;
    int   i, k;
    out->n = 0;
    for (i = 1; i < in->n; i++)
        total += len(sub(in->p[i], in->p[i - 1]));
    for (k = 0; k < 2; k++)
    {
        want = (k ? f1 : f0) * total;
        acc  = 0.0f;
        for (i = 1; i < in->n; i++)
        {
            float l = len(sub(in->p[i], in->p[i - 1]));
            if (k == 1 && acc > f0 * total + 1e-6f && acc < f1 * total - 1e-6f)
                if (push(out, in->p[i - 1], 0.0f, in->mid[i - 1]) != 0)
                    return -1;
            if (want <= acc + l + 1e-6f || i == in->n - 1)
            {
                float t = l > 1e-9f ? fminf(fmaxf((want - acc) / l, 0.0f), 1.0f) : 0.0f;
                NgP   p = v(in->p[i - 1].x + (in->p[i].x - in->p[i - 1].x) * t, in->p[i - 1].y + (in->p[i].y - in->p[i - 1].y) * t);
                if (push(out, p, 0.0f, 0) != 0)
                    return -1;
                break;
            }
            acc += l;
        }
    }
    return 0;
}

/*  A line offset by t to its right, cornered on the miter.  Each corner
 *  at a unit's middle is rounded from the port where the lane enters the
 *  unit to the port where it leaves it. */
static int offset(const Line *in, float t, float e, Line *out)
{
    int i;
    out->n = 0;
    for (i = 0; i < in->n; i++)
    {
        int   has_in = i > 0, has_out = i + 1 < in->n;
        NgP   din = has_in ? unit(sub(in->p[i], in->p[i - 1])) : v(0, 0), dout = has_out ? unit(sub(in->p[i + 1], in->p[i])) : v(0, 0);
        NgP   n, at;
        float lim = 0.0f;
        if (has_in && has_out)
        {
            NgP   a = right(din), b = right(dout);
            float k = 1.0f + dot(a, b);
            n       = k > 1e-6f ? v((a.x + b.x) / k, (a.y + b.y) / k) : a;
        }
        else
            n = right(has_in ? din : dout);
        at = v(in->p[i].x + n.x * t, in->p[i].y + n.y * t);
        if (has_in && has_out && in->mid[i] && dot(din, dout) < 0.999f)
        {
            NgP c = sub(at, in->p[i]);
            lim   = fminf(e + dot(c, din), e - dot(c, dout));
        }
        if (push(out, at, lim, in->mid[i]) != 0)
            return -1;
    }
    return 0;
}

/*  A run from `a`, leaving along `da`, to `b`, arriving along `db`.  A
 *  turn rounds the corner where the two ways cross.  Two ways side by
 *  side are joined by one smooth S curve.  Two ways back to back are
 *  joined by a half round about a point `reach` along `da`.  Turns about
 *  one point nest and do not cross.  Answers 1, or 0 where the two
 *  points are one. */
static int bend(NgP a, NgP da, NgP b, NgP db, float reach, Line *out)
{
    NgP   gap = sub(b, a), p, q;
    float det = da.x * db.y - da.y * db.x, d, h, l1, l2, l3;
    out->n    = 0;
    if (fabsf(det) > 1e-4f)
    {
        float s = (gap.x * db.y - gap.y * db.x) / det, u = (gap.x * da.y - gap.y * da.x) / det;
        if (s > 1e-3f && u > 1e-3f)
        {
            if (push(out, a, 0.0f, 0) != 0 || push(out, v(a.x + da.x * s, a.y + da.y * s), fminf(s, u), 0) != 0 ||
                push(out, b, 0.0f, 0) != 0)
                return -1;
            return 1;
        }
    }
    d = len(gap);
    if (d < 1e-4f)
        return 0;
    if (dot(da, db) >= -0.5f)
    {
        /*  One smooth curve, with a tangent half the gap long at each end. */
        int i;
        h = d * 0.5f;
        p = v(a.x + da.x * h, a.y + da.y * h), q = v(b.x - db.x * h, b.y - db.y * h);
        for (i = 0; i <= 4 * ROUND; i++)
        {
            float t = (float)i / (float)(4 * ROUND), w0 = (1 - t) * (1 - t) * (1 - t), w1 = 3 * t * (1 - t) * (1 - t);
            float w2 = 3 * t * t * (1 - t), w3 = t * t * t;
            if (push(out, v(w0 * a.x + w1 * p.x + w2 * q.x + w3 * b.x, w0 * a.y + w1 * p.y + w2 * q.y + w3 * b.y), 0.0f, 0) != 0)
                return -1;
        }
        return 1;
    }
    h = reach + d * 0.5f;
    p = v(a.x + da.x * h, a.y + da.y * h), q = v(b.x - db.x * h, b.y - db.y * h);
    l1 = len(sub(p, a)), l2 = len(sub(q, p)), l3 = len(sub(b, q));
    if (push(out, a, 0.0f, 0) != 0 || push(out, p, fminf(l1, l2) * 0.5f, 0) != 0 || push(out, q, fminf(l2, l3) * 0.5f, 0) != 0 ||
        push(out, b, 0.0f, 0) != 0)
        return -1;
    return 1;
}

/*  A line's corners rounded as a chain of short straights.  Each corner
 *  is a curve from the point `lim` before it to the point `lim` after it. */
static int rounded(const Line *in, Line *out)
{
    int i, s;
    out->n = 0;
    if (in->n < 1)
        return 0;
    if (push(out, in->p[0], 0.0f, 0) != 0)
        return -1;
    for (i = 1; i + 1 < in->n; i++)
    {
        float t = in->lim[i];
        NgP   c = in->p[i];
        if (t > 1e-4f)
        {
            NgP din = unit(sub(c, in->p[i - 1])), dout = unit(sub(in->p[i + 1], c));
            NgP t1 = v(c.x - din.x * t, c.y - din.y * t), t2 = v(c.x + dout.x * t, c.y + dout.y * t);
            for (s = 0; s <= ROUND; s++)
            {
                float u = (float)s / ROUND, w0 = (1 - u) * (1 - u), w1 = 2 * u * (1 - u), w2 = u * u;
                if (push(out, v(w0 * t1.x + w1 * c.x + w2 * t2.x, w0 * t1.y + w1 * c.y + w2 * t2.y), 0.0f, 0) != 0)
                    return -1;
            }
        }
        else if (push(out, c, 0.0f, 0) != 0)
            return -1;
    }
    return push(out, in->p[in->n - 1], 0.0f, 0);
}

/*  The midpoint of a line by length, and the way it runs there. */
static void halfway(const Line *l, NgP *at, NgP *dir)
{
    float total = 0.0f, want;
    int   i;
    for (i = 1; i < l->n; i++)
        total += len(sub(l->p[i], l->p[i - 1]));
    want = total * 0.5f;
    for (i = 1; i < l->n; i++)
    {
        float s = len(sub(l->p[i], l->p[i - 1]));
        if (want <= s || i == l->n - 1)
        {
            float t = s > 1e-9f ? want / s : 0.0f;
            *dir    = unit(sub(l->p[i], l->p[i - 1]));
            *at     = v(l->p[i - 1].x + (l->p[i].x - l->p[i - 1].x) * t, l->p[i - 1].y + (l->p[i].y - l->p[i - 1].y) * t);
            return;
        }
        want -= s;
    }
    *at = l->n ? l->p[0] : v(0, 0), *dir = v(1, 0);
}

/*  One end of a lane of a road the topology found: its port, and the way
 *  out of the road there. */
typedef struct
{
    NgP port[2], out[2];
    int has;
} End;

/*  A node, where it is drawn: where it lies, and the way the lanes that
 *  leave it and that arrive at it run there.  `placed` is 1 at a port and
 *  2 inside a junction. */
typedef struct
{
    NgP at, leave, arrive;
    int placed;
    int inner_start, inner_end; /* a traffic lane of a connecting road starts or ends here */
    int beside;                 /* 1 + the node at an arm's mouth a lane of the junction joins this one to, else 0 */
    int entry_to;               /* 1 + the node at an arm's mouth an entry lane from this one reaches, else 0 */
} Place;

static int color_of(int lane)
{
    const NLane *l = network_lane(lane);
    if (network_lane_fault(lane) != NFAULT_NONE)
        return NG_FAULT;
    /*  A ramp's lane is drawn as the exit or entry lane it goes on from or
     *  into. */
    if (l->type == NLANE_OFFRAMP)
        return NLANE_EXIT;
    if (l->type == NLANE_ONRAMP)
        return NLANE_ENTRY;
    return l->type <= NLANE_RAIL ? l->type : NLANE_DRIVING;
}

/*  A drawn line and, for a lane of travel, its arrow halfway along. */
static int draw(const NgSink *k, const char *what, const Line *pts, Line *tmp, float half, int color, int arrow, int flip)
{
    NgP at, dir;
    if (rounded(pts, tmp) != 0)
        return -1;
    if (tmp->n < 2 || k->line(k->user, what, tmp->p, tmp->n, half, color) != 0)
        return tmp->n < 2 ? 0 : -1;
    if (!arrow)
        return 0;
    halfway(tmp, &at, &dir);
    if (flip)
        dir = v(-dir.x, -dir.y);
    return k->arrow(k->user, at, dir, ARROW, color);
}

/*  Whether a junction holds the unit whose first cell is at (x, y). */
static int holds(const TopoEnd *e, int x, int y)
{
    int32_t side = topology_side();
    int     j;
    if (x < 0 || y < 0 || x >= side || y >= side)
        return 0;
    for (j = 0; j < e->ncell; j++)
        if (topology_cell(e->cell0 + j) == (int32_t)y * side + x)
            return 1;
    return 0;
}

/*  A junction's cells outlined.  The outline runs round them all, a
 *  little inside them.  The edges between two of its own cells are thin
 *  lines, so the cells it is merged from still show. */
static int outline(const NgSink *k, const TopoEnd *e, int color)
{
    static const int NX[4] = {0, 1, 0, -1}, NY[4] = {-1, 0, 1, 0}; /* outward, per side */
    static const int TX[4] = {1, 0, 1, 0}, TY[4] = {0, 1, 0, 1};   /* along, per side */
    int32_t          side = topology_side();
    int              s    = e->net == RNET_BAND ? 2 : 1, j, d;
    for (j = 0; j < e->ncell; j++)
    {
        int32_t cell = topology_cell(e->cell0 + j);
        int     x = (int)(cell % side), y = (int)(cell / side);
        for (d = 0; d < 4; d++)
        {
            /*  The corner the side starts at, and the side's length. */
            float cx = (float)x + (NX[d] > 0 ? (float)s : 0.0f), cy = (float)y + (NY[d] > 0 ? (float)s : 0.0f), t0, t1;
            NgP   seg[2];
            float half = NODE_HALF;
            if (holds(e, x + NX[d] * s, y + NY[d] * s))
            {
                /*  An edge between two of its own cells, drawn once. */
                if (d != 1 && d != 2)
                    continue;
                t0 = INSET, t1 = (float)s - INSET, half = NODE_HALF * 0.3f;
            }
            else
            {
                float in = -INSET;
                int   e0, e1;
                /*  Each end meets the next side of the outline.  At an outer
                 *  corner it meets it a little inside the cell.  At an inner
                 *  corner it meets it a little past the cell.  A straight
                 *  run meets it at the cell's corner.  A line runs on past a
                 *  meeting by half its width. */
                e0 = !holds(e, x - TX[d] * s, y - TY[d] * s)                          ? 1
                     : holds(e, x - TX[d] * s + NX[d] * s, y - TY[d] * s + NY[d] * s) ? -1
                                                                                      : 0;
                e1 = !holds(e, x + TX[d] * s, y + TY[d] * s)                          ? 1
                     : holds(e, x + TX[d] * s + NX[d] * s, y + TY[d] * s + NY[d] * s) ? -1
                                                                                      : 0;
                t0 = e0 ? (float)e0 * INSET - NODE_HALF : 0.0f;
                t1 = (float)s - (e1 ? (float)e1 * INSET - NODE_HALF : 0.0f);
                cx += (float)NX[d] * in, cy += (float)NY[d] * in;
            }
            seg[0] = v(cx + (float)TX[d] * t0, cy + (float)TY[d] * t0);
            seg[1] = v(cx + (float)TX[d] * t1, cy + (float)TY[d] * t1);
            if (k->line(k->user, "lane graph junction", seg, 2, half, color) != 0)
                return -1;
        }
    }
    return 0;
}

/*  Every unit of every road the topology found, bordered thinly. */
static int road_cells(const NgSink *k)
{
    int32_t side = topology_side();
    int     i, j;
    for (i = 0; i < topology_roads(); i++)
    {
        const TopoRoad *t = topology_road(i);
        float           s = t->net == RNET_BAND ? 2.0f : 1.0f;
        for (j = 0; j < t->nunit; j++)
        {
            int32_t cell = topology_cell(t->unit0 + j);
            float   x = (float)(cell % side), y = (float)(cell / side);
            NgP     ring[5];
            ring[0] = v(x, y), ring[1] = v(x + s, y), ring[2] = v(x + s, y + s), ring[3] = v(x, y + s), ring[4] = ring[0];
            if (k->line(k->user, "lane graph road cell", ring, 5, NODE_HALF * 0.3f, NG_CELL) != 0)
                return -1;
        }
    }
    return 0;
}

static int junctions(const NgSink *k)
{
    int i;
    if (!k->cells)
        return 0;
    if (road_cells(k) != 0)
        return -1;
    for (i = 0; i < network_junctions(); i++)
    {
        const TopoEnd *e = topology_end(network_junction(i)->end);
        if (outline(k, e, NG_JUNCTION) != 0)
            return -1;
    }
    return 0;
}

/*  Every lane of every road the topology found: its stretch of the road's
 *  line, offset to its ports.  Each lane's two ends are kept. */
static int roads(const NgSink *k, End *ends, Line *line, Line *part, Line *off, Line *tmp)
{
    int i, q, j;
    for (i = 0; i < network_roads(); i++)
    {
        const NRoad    *r = network_road(i);
        const TopoRoad *t;
        if (r->topo < 0)
            continue;
        t = topology_road(r->topo);
        if (gore_ramp(i) ? shaped_line(i, line) != 0 : road_line(t, line) != 0)
            return -1;
        if (gore_ramp(i) && line->n < 2 && road_line(t, line) != 0)
            return -1;
        if (line->n < 2)
            continue;
        for (q = 0; q < r->nsec; q++)
        {
            const NSection *x   = network_section(r->sec0 + q);
            float           f0  = r->length > 1e-6f ? x->s / r->length : 0.0f;
            float           f1  = q + 1 < r->nsec && r->length > 1e-6f ? network_section(r->sec0 + q + 1)->s / r->length : 1.0f;
            float           mid = 0.0f;
            if (cut(line, f0, f1, part) != 0)
                return -1;
            /*  A section's lanes are centered on the road's line, so its
             *  ports are centered on every border it crosses. */
            for (j = 0; j < x->nlane; j++)
                mid += across(r->net, network_lane(x->lane0 + j)->id) / (float)x->nlane;
            for (j = 0; j < x->nlane; j++)
            {
                int          lane = x->lane0 + j;
                const NLane *l    = network_lane(lane);
                End         *e    = &ends[lane];
                /*  A median, a border and a shoulder are in no lane graph. */
                if (!network_lane_in_graph(lane))
                    continue;
                if (offset(part, across(r->net, l->id) - mid, ext(r->net), off) != 0)
                    return -1;
                if (off->n < 2)
                    continue;
                e->has     = 1;
                e->port[0] = off->p[0], e->out[0] = unit(sub(off->p[0], off->p[1]));
                e->port[1] = off->p[off->n - 1], e->out[1] = unit(sub(off->p[off->n - 1], off->p[off->n - 2]));
                if (draw(k, "lane graph road", off, tmp, LANE_HALF, color_of(lane), l->type != NLANE_SIDEWALK, !network_lane_with_s(lane)) != 0)
                    return -1;
            }
        }
    }
    return 0;
}

/*  How far a line from `at` along the unit `dir` runs through a junction's
 *  cells before it leaves them.  A cell of a band's junction is two cells
 *  on a side. */
static float through(int junction, NgP at, NgP dir)
{
    const TopoEnd *e    = topology_end(network_junction(junction)->end);
    int32_t        side = topology_side();
    float          far  = 0.0f;
    int            j;
    for (j = 0; j < e->ncell; j++)
    {
        int32_t cell  = topology_cell(e->cell0 + j);
        float   size  = e->net == RNET_BAND ? 2.0f : 1.0f;
        float   lo[2] = {(float)(cell % side), (float)(cell / side)}, p[2] = {at.x, at.y}, d[2] = {dir.x, dir.y};
        float   t0 = -1e9f, t1 = 1e9f;
        int     k;
        for (k = 0; k < 2; k++)
        {
            if (fabsf(d[k]) < 1e-6f)
            {
                if (p[k] < lo[k] || p[k] > lo[k] + size)
                    t1 = -1.0f;
                continue;
            }
            t0 = fmaxf(t0, fminf((lo[k] - p[k]) / d[k], (lo[k] + size - p[k]) / d[k]));
            t1 = fminf(t1, fmaxf((lo[k] - p[k]) / d[k], (lo[k] + size - p[k]) / d[k]));
        }
        if (t1 > t0 && t1 > far)
            far = t1;
    }
    return far;
}

/*  Where each node is drawn.  First at the port of a lane of a road that
 *  ends there.  Then a node where an exit lane from an arm divides into
 *  other lanes of the junction lies a step in from that arm.  Last, a
 *  node where lanes of the junction join an entry lane into an arm lies a
 *  step back from that arm.  A node that a lane from another arm's mouth
 *  also meets lies level with that mouth instead of a step in. */
static void places(const End *ends, Place *pl)
{
    int lane, e, pass, nl = network_lanes(), side = network_side();
    for (lane = 0; lane < nl; lane++)
    {
        const NRoad *r = network_road(network_lane(lane)->road);
        if (r->junction < 0 || network_lane(lane)->type == NLANE_SIDEWALK)
            continue;
        pl[network_lane_node(lane, !network_lane_with_s(lane))].inner_start = 1;
        pl[network_lane_node(lane, network_lane_with_s(lane))].inner_end    = 1;
    }
    for (lane = 0; lane < nl; lane++)
        for (e = 0; e < 2; e++)
        {
            Place *p = &pl[network_lane_node(lane, e)];
            if (!ends[lane].has || p->placed)
                continue;
            p->at = ends[lane].port[e], p->leave = ends[lane].out[e];
            p->arrive = v(-p->leave.x, -p->leave.y), p->placed = 1;
        }
    for (lane = 0; lane < nl; lane++)
    {
        const NLane *l = network_lane(lane);
        int          a = network_lane_node(lane, 0), b = network_lane_node(lane, 1);
        if (network_road(l->road)->junction < 0 || l->type == NLANE_SIDEWALK || nlane_is_change(l->type))
            continue;
        if (pl[a].placed == 1 && !pl[b].placed)
            pl[b].beside = a + 1;
        if (pl[b].placed == 1 && !pl[a].placed)
            pl[a].beside = b + 1;
    }
    for (lane = 0; lane < nl; lane++)
    {
        int f = network_lane_node(lane, network_lane_with_s(lane));
        if (network_road(network_lane(lane)->road)->junction >= 0 && network_lane(lane)->type == NLANE_ENTRY && pl[f].placed == 1)
            pl[network_lane_node(lane, !network_lane_with_s(lane))].entry_to = f + 1;
    }
    for (pass = 0; pass < 2; pass++)
        for (lane = 0; lane < nl; lane++)
        {
            const NLane *l = network_lane(lane);
            const NRoad *r = network_road(l->road);
            Place       *s, *f;
            float        gap = GAP * ext(r->net) * 2.0f;
            if (r->junction < 0)
                continue;
            s = &pl[network_lane_node(lane, !network_lane_with_s(lane))];
            f = &pl[network_lane_node(lane, network_lane_with_s(lane))];
            if (pass == 0 && l->type == NLANE_EXIT && s->placed == 1 && f->inner_start)
            {
                NgP   d = s->leave, u = right(d);
                float t = f->beside ? dot(sub(pl[f->beside - 1].at, s->at), d) : DEPTH, w;
                /*  A gap outside the outer of the lane the exit lane leaves
                 *  and the lane an entry lane from here reaches. */
                u        = v(u.x * (float)side, u.y * (float)side);
                w        = gap + (f->entry_to ? fmaxf(0.0f, dot(sub(pl[f->entry_to - 1].at, s->at), u)) : 0.0f);
                f->at    = v(s->at.x + d.x * t + u.x * w, s->at.y + d.y * t + u.y * w);
                f->leave = f->arrive = d;
                f->placed            = 2;
            }
            if (pass == 1 && l->type == NLANE_ENTRY && f->placed == 1 && s->placed != 2 && s->inner_end)
            {
                NgP   d = f->arrive, o = right(d);
                float t  = s->beside ? dot(sub(f->at, pl[s->beside - 1].at), d) : DEPTH;
                s->at    = v(f->at.x - d.x * t + o.x * gap * (float)side, f->at.y - d.y * t + o.y * gap * (float)side);
                s->leave = s->arrive = d;
                s->placed            = 2;
            }
        }
}

/*  A CROSSWALK: a sidewalk's connecting road across the mouth of an arm
 *  that carries a stop or a signal (plans/network.md, layer 2). */
static int crosswalk(const NRoad *r)
{
    int i;
    if (r->pred.kind != NLINK_ROAD || r->succ.kind != NLINK_ROAD || r->pred.index != r->succ.index ||
        r->pred.contact != r->succ.contact)
        return 0;
    for (i = 0; i < network_controls(); i++)
    {
        const NControl *c = network_control(i);
        if (c->junction == r->junction && c->road == r->pred.index && c->contact == r->pred.contact &&
            (c->control == NCONTROL_STOP || c->control == NCONTROL_SIGNAL))
            return 1;
    }
    return 0;
}

/*  Every lane of a connecting road, from the node at its start to the
 *  node at its end.  A sidewalk runs from its end at `s` nought.  A lane
 *  that turns back to the arm it leaves turns about a point halfway
 *  across the junction, and a crosswalk crosses the mouth. */
static int connecting(const NgSink *k, const Place *pl, Line *pts, Line *tmp, int *unplaced)
{
    int lane;
    for (lane = 0; lane < network_lanes(); lane++)
    {
        const NLane *l    = network_lane(lane);
        const NRoad *r    = network_road(l->road);
        int          walk = l->type == NLANE_SIDEWALK, from = walk ? 0 : !network_lane_with_s(lane);
        const Place *a, *b;
        float        reach;
        int          got;
        if (r->junction < 0)
            continue;
        a = &pl[network_lane_node(lane, from)], b = &pl[network_lane_node(lane, !from)];
        if (!a->placed || !b->placed)
        {
            (*unplaced)++;
            continue;
        }
        reach = walk && crosswalk(r) ? 0.0f : 0.5f * through(r->junction, a->at, a->leave);
        got   = bend(a->at, a->leave, b->at, b->arrive, reach, pts);
        if (got < 0)
            return -1;
        if (got && draw(k, "lane graph connecting road", pts, tmp, LANE_HALF, color_of(lane), !walk, 0) != 0)
            return -1;
    }
    return 0;
}

/*  A lane link whose two ends do not lie in the same place is a gray
 *  line.  It runs from the end one lane runs out at to the end the next
 *  runs in at.  A sidewalk's link is walked both ways and drawn once. */
static int links(const NgSink *k, const End *ends, const Place *pl, Line *pts, Line *tmp)
{
    int i;
    for (i = 0; i < network_links(); i++)
    {
        const NLaneLink *n  = network_link(i);
        const End       *fa = &ends[n->from], *fb = &ends[n->to];
        const Place     *pa = &pl[network_lane_node(n->from, n->from_end)];
        NgP              a, da, b, db;
        int              got;
        if (network_lane(n->from)->type == NLANE_SIDEWALK && n->from > n->to)
            continue;
        if (fa->has && fb->has)
            a = fa->port[n->from_end], da = fa->out[n->from_end], b = fb->port[n->to_end], db = v(-fb->out[n->to_end].x, -fb->out[n->to_end].y);
        else if (!fa->has && fb->has && pa->placed == 2)
            a = pa->at, da = pa->leave, b = fb->port[n->to_end], db = v(-fb->out[n->to_end].x, -fb->out[n->to_end].y);
        else if (fa->has && !fb->has && pa->placed == 2)
            a = fa->port[n->from_end], da = fa->out[n->from_end], b = pa->at, db = pa->arrive;
        else
            continue;
        if (len(sub(b, a)) <= LINK_APART)
            continue;
        got = bend(a, da, b, db, 0.0f, pts);
        if (got < 0)
            return -1;
        if (got && draw(k, "lane graph link", pts, tmp, LANE_HALF, NG_LINK, 0, 0) != 0)
            return -1;
    }
    return 0;
}

/*  Every node that is placed, for a sink that draws them. */
static int nodes(const NgSink *k, const Place *pl)
{
    int i;
    for (i = 0; k->node && i < network_nodes(); i++)
        if (pl[i].placed && k->node(k->user, pl[i].at, pl[i].placed == 2) != 0)
            return -1;
    return 0;
}

/*  A DEAD END of the lane graph at a junction: a road's traffic lane
 *  whose end there links to nothing.  It is drawn on across the
 *  junction's cells, and its node lies on the far side, so the dead end
 *  shows inside the junction. */
static int dead_ends(const NgSink *k, const End *ends, Place *pl, Line *pts, Line *tmp)
{
    int lane, e;
    for (lane = 0; lane < network_lanes(); lane++)
    {
        const NLane *l = network_lane(lane);
        const NRoad *r = network_road(l->road);
        for (e = 0; e < 2; e++)
        {
            const NLink *at   = e ? &r->succ : &r->pred;
            int          node = network_lane_node(lane, e), last = r->sec0 + r->nsec - 1;
            const NNode *nd = network_node(node);
            NgP          p, d;
            float        t;
            if (r->topo < 0 || l->type == NLANE_SIDEWALK || !ends[lane].has || at->kind != NLINK_JUNCTION ||
                l->section != (e ? last : r->sec0) || !nd || nd->nin + nd->nout != 1)
                continue;
            p = ends[lane].port[e], d = ends[lane].out[e], t = through(at->index, p, d);
            pts->n = 0;
            if (push(pts, p, 0.0f, 0) != 0 || push(pts, v(p.x + d.x * t, p.y + d.y * t), 0.0f, 0) != 0)
                return -1;
            /*  The arrow runs the way the lane does: into the junction
             *  where the lane ends there. */
            if (draw(k, "lane graph dead end", pts, tmp, LANE_HALF, color_of(lane), 1, e != network_lane_with_s(lane)) != 0)
                return -1;
            pl[node].at = pts->p[1];
        }
    }
    return 0;
}

int net_graph_draw(const NgSink *k)
{
    int    nl = network_lanes(), unplaced = 0, rc = -1;
    End   *ends = (End *)calloc((size_t)nl + 1, sizeof *ends);
    Place *pl   = (Place *)calloc((size_t)(network_nodes() + 1), sizeof *pl);
    Line   line = {0}, part = {0}, off = {0}, tmp = {0};
    if (ends && pl && junctions(k) == 0 && roads(k, ends, &line, &part, &off, &tmp) == 0)
    {
        places(ends, pl);
        if (dead_ends(k, ends, pl, &off, &tmp) == 0 && connecting(k, pl, &off, &tmp, &unplaced) == 0 && links(k, ends, pl, &off, &tmp) == 0 && nodes(k, pl) == 0)
            rc = unplaced;
    }
    line_free(&line), line_free(&part), line_free(&off), line_free(&tmp);
    free(ends), free(pl);
    return rc;
}
