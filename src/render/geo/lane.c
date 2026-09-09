/*  lane.c -- lanes as primitives.  A lane is a directed run of pieces --
 *  straights and arcs, the same Piece the road fit makes -- with a width
 *  and a class, and an end that names what it joins.  The primitive on top
 *  of them is the ROUTER, lane_route: the lane between the end of one lane
 *  and the start of another, fitted at the class's radius.  Everything that
 *  is today a hand-built strip with its own bugs is that one routing task:
 *  a deck's outer lane down to a road, a branch track into a through line,
 *  a two-lane road meeting a four-lane one, the turns inside a junction
 *  (docs/future.rst, "Lanes as primitives").
 *  This first stage puts lanes at the intersections: every road segment's
 *  two lanes end at a junction PORT, and a connector is routed from each
 *  inbound lane to each outbound lane of the other arms.  The renderer gets
 *  it right first; traffic comes last.  Sides.  The map's own right-hand
 *  normal of a direction d is (-d.y, d.x), and the map is drawn reflected
 *  (traffic.c), so the viewer's right is (d.y, -d.x).  Cars drive on the
 *  viewer's right ("right hand drive please"): a lane running along d lies
 *  at (d.y, -d.x) times half the band's half width. */
#include "dump.h"
#include "mesh/internal.h"
#include "log.h"
#include "net/internal.h"
#include "script.h"
#include "opt.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*  The numbers this file reads, each remembering where the store
 *  put it (net/geo.c).  A name here is a name a script must set. */


#define L_MAX    40000  /* lanes in a city: two a segment, up to twelve a junction */
#define L_PIECES 240000 /* their pieces */

/*  How tight a turn a lane may take before the dump and the check call
 *  it tight: a quarter tile, about four metres at the tile's scale.  A
 *  road's right turn sweeps junc_curb plus half a lane, 0.325 at the
 *  default width, once the junction has pushed its mouths out for the
 *  curb returns; before that it pivoted on a point (0.125). */
#define LANE_RMIN (net_family_rules(F_ROAD)->lane_rmin)

enum
{
    LC_ROAD = 0,
    LC_TURN = 1,
    LC_DECK = 2,
    LC_RAMP = 3,
    LC_LINK = 4 /* a highway's lane into a road's, or into its own inner lane, at a band's end */
};
enum
{
    LE_OPEN = 0,
    LE_PORT = 1,
    LE_LANE = 2 /* into another lane: port0/1 is its index */
};

typedef struct
{
    uint8_t cls, fam, kind0, kind1, tight;
    int32_t port0, port1; /* ((tile * 4 + edge) * 2 + out), for LE_PORT */
    int     first, np;    /* pieces in s_lp */
    float   w, rmin, len; /* the band's half width; the tightest arc; the length */
    int     band;         /* a deck lane's band, else 0 */
    float   off;          /* its offset from the band's centreline, viewer's right positive */
} Lane;

static Lane  s_lane[L_MAX];
static Piece s_lp[L_PIECES];
static int   s_nl, s_nlp;
/*  The lanes by tile: every lane is filed under each tile its pieces'
 *  extents cover as it is added, and a search visits only the lanes filed
 *  within reach of its point -- the scan over every lane in the city cost a
 *  ramp's three searches 3.6 ms a building pass in Atlanta.  A filing is a
 *  lane at a tile; a tile's filings are a list. */
#define L_FILED (4 * L_MAX)
static int s_lx_head[R_MAP * R_MAP]; /* a tile's first filing, or -1 */
static int s_lx_lane[L_FILED], s_lx_next[L_FILED], s_nlx;
static int s_lx_seen[L_MAX], s_lx_stamp; /* a search's stamp on the lanes it has gathered */
static int s_lx_cand[L_MAX];             /* ... and the lanes it gathered, in lane order */
static struct
{
    int junctions, ports, conns, tight, failed, segs, wide, mismatch, decks, ramps, ramp_miss, band_ends, band_open, band_links, links, link_fail, caps, cap_fail, nowhere, nothing, crossings;
} s_ls;

void lane_reset(void)
{
    net_wires_reset();
    s_nl = s_nlp = 0;
    memset(&s_ls, 0, sizeof s_ls);
    s_nlx = s_lx_stamp = 0;
    memset(s_lx_head, -1, sizeof s_lx_head);
    memset(s_lx_seen, 0, sizeof s_lx_seen);
}

/*  The lane centres by class, from the centreline, inner first.  Where
 *  they run is the SCRIPT'S (arc.rules.lanes), settled for every family
 *  and class the pipeline can present before any of it is built: no
 *  answer is no lanes, which is what a run that cannot find the scripts
 *  draws.  Every part of the pipeline that wants a lane comes through
 *  here -- the connectors, the paint and the traffic -- so the three
 *  cannot part company.
 *
 *  A class runs from LANE_CLS_LO, which is the family that has none, up
 *  to the widest road there is. */
#define LANE_CLS_LO (-1)
#define LANE_CLS_HI 2
#define LANE_RUNS   (2 * (LANE_CLS_HI - LANE_CLS_LO + 1))

static struct
{
    float off[4];
    int   n, have;
} s_lane_run[LANE_RUNS];

static int lane_run_ix(Family f, int cls)
{
    if (cls < LANE_CLS_LO || cls > LANE_CLS_HI)
        return -1;
    return (f == F_RAIL ? 1 : 0) * (LANE_CLS_HI - LANE_CLS_LO + 1) + (cls - LANE_CLS_LO);
}

void net_lane_runs_reset(void)
{
    memset(s_lane_run, 0, sizeof s_lane_run);
}

int net_lane_runs(void)
{
    return LANE_RUNS;
}

/*  Which family and class one of them is: the drive asks the rule about
 *  each in turn and hands the answer straight back. */
void net_lane_run_at(int i, const char **fam, int *cls)
{
    *fam = i >= LANE_CLS_HI - LANE_CLS_LO + 1 ? "rail" : "road";
    *cls = LANE_CLS_LO + i % (LANE_CLS_HI - LANE_CLS_LO + 1);
}

void net_lane_run_is(int i, const float *off, int n)
{
    int k;
    if (i < 0 || i >= LANE_RUNS)
        return;
    if (n > (int)(sizeof s_lane_run[0].off / sizeof s_lane_run[0].off[0]))
        n = (int)(sizeof s_lane_run[0].off / sizeof s_lane_run[0].off[0]);
    for (k = 0; k < n; ++k)
        s_lane_run[i].off[k] = off[k];
    s_lane_run[i].n    = n;
    s_lane_run[i].have = 1;
}

int net_lane_offsets(Family f, int cls, float *off, int max)
{
    int i = lane_run_ix(f, cls), k, n;
    if (i < 0 || !s_lane_run[i].have)
        return 0;
    n = s_lane_run[i].n < max ? s_lane_run[i].n : max;
    for (k = 0; k < n; ++k)
        off[k] = s_lane_run[i].off[k];
    return n;
}

static int lane_offsets(Family f, int cls, float *off)
{
    /*  Two each way is what every caller has room for, and a class that
     *  runs only one leaves the second at nought. */
    off[0] = off[1] = 0.0f;
    return net_lane_offsets(f, cls, off, 2);
}

static V2 l_right(V2 d)
{
    return (V2){d.y, -d.x};
}

static int port_id(int col, int row, int e, int out, int k)
{
    return (((row * R_MAP + col) * 4 + e) * 2 + out) * 2 + k; /* k: the lane, inner first */
}

int lane_port_id(int col, int row, int e, int out, int k)
{
    return port_id(col, row, e, out, k);
}

/*  A point on a piece at arc length t, and the direction of travel
 *  there (net/loft.c's piece_at). */
static void l_piece_at(const Piece *p, float t, V2 *pos, V2 *dir)
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

/*  The same piece shifted sideways by `off`, to the viewer's right of
 *  travel when positive: a straight moves over, an arc keeps its centre
 *  and changes radius (the viewer's right of the travel direction on an
 *  arc is the radial direction times the arc's sign). */
static void piece_offset(const Piece *p, float off, Piece *o)
{
    *o = *p;
    if (!p->arc)
    {
        float l = p->len > 1e-6f ? p->len : 1.0f;
        V2    u = {(p->b.x - p->a.x) / l, (p->b.y - p->a.y) / l}, r = l_right(u);
        o->a = (V2){p->a.x + r.x * off, p->a.y + r.y * off};
        o->b = (V2){p->b.x + r.x * off, p->b.y + r.y * off};
    }
    else
    {
        static int gix_lane_arc_min = -1;
        float      s   = p->t1 > p->t0 ? 1.0f : -1.0f;
        float      lo  = net_geo(&gix_lane_arc_min, "lane_arc_min");
        float      r2  = p->r + off * s;
        if (r2 < lo)
            r2 = lo;
        o->r   = r2;
        o->a   = (V2){p->c.x + r2 * cosf(p->t0), p->c.y + r2 * sinf(p->t0)};
        o->b   = (V2){p->c.x + r2 * cosf(p->t1), p->c.y + r2 * sinf(p->t1)};
        o->len = p->r > 1e-6f ? p->len * r2 / p->r : p->len;
    }
}

static void pieces_reverse(Piece *p, int np)
{
    int i;
    for (i = 0; i < np / 2; ++i)
    {
        Piece t       = p[i];
        p[i]          = p[np - 1 - i];
        p[np - 1 - i] = t;
    }
    for (i = 0; i < np; ++i)
    {
        V2 t   = p[i].a;
        p[i].a = p[i].b;
        p[i].b = t;
        if (p[i].arc)
        {
            float u = p[i].t0;
            p[i].t0 = p[i].t1;
            p[i].t1 = u;
        }
    }
}

/*  The router.  From A heading tA to B heading tB: a straight when B
 *  lies dead ahead, else the equal-tangent biarc (net/fit.c's tf_biarc:
 *  two fillets with the same tangent length d, at A + d tA and B - d tB)
 *  built by the piece builder from those four points.  Returns 0 and
 *  the pieces, with the tightest arc's radius in *rmin; -1 when there
 *  is no such lane, which is B behind A. */
int lane_route(V2 A, V2 tA, V2 B, V2 tB, Piece *out, int *np, float *rmin)
{
    V2    v  = {B.x - A.x, B.y - A.y};
    float vv = v.x * v.x + v.y * v.y, vl = sqrtf(vv);
    float cr = tA.x * v.y - tA.y * v.x, dt = tA.x * tB.x + tA.y * tB.y, dv = tA.x * v.x + tA.y * v.y;
    int   k;
    *np   = 0;
    *rmin = 1e9f;
    if (vv < 1e-10f)
        return -1;
    if (fabsf(cr) < 1e-4f * vl && dt > 0.9999f && dv > 0.0f)
    {
        out[0].arc = 0;
        out[0].a   = A;
        out[0].b   = B;
        out[0].len = vl;
        *np        = 1;
        return 0;
    }
    {
        V2    s = {tA.x + tB.x, tA.y + tB.y};
        float c = dt, vs = v.x * s.x + v.y * s.y, kk = 2.0f * (1.0f - c), d;
        static int gix_lane_route_rmax = -1;
        float      cap = net_geo(&gix_lane_route_rmax, "lane_route_rmax");
        V2         q[4];
        float      rad[4] = {0.0f, cap, cap, 0.0f}, tl[4];
        if (kk < 1e-5f)
        {
            if (vs <= 1e-6f)
                return -1;
            d = vv / (2.0f * vs);
        }
        else
            d = (-vs + sqrtf(vs * vs + kk * vv)) / kk;
        if (d <= 1e-4f)
            return -1;
        q[0]  = A;
        q[1]  = (V2){A.x + tA.x * d, A.y + tA.y * d};
        q[2]  = (V2){B.x - tB.x * d, B.y - tB.y * d};
        q[3]  = B;
        tl[0] = tl[3] = 0.0f;
        tl[1] = tl[2] = d;
        if (fillet_t(q, 4, rad, tl, out, np) != 0 || *np < 1)
            return -1;
    }
    for (k = 0; k < *np; ++k)
        if (out[k].arc && out[k].r < *rmin)
            *rmin = out[k].r;
    return 0;
}

static void extent_add(float x, float y, float *x0, float *y0, float *x1, float *y1)
{
    if (x < *x0)
        *x0 = x;
    if (x > *x1)
        *x1 = x;
    if (y < *y0)
        *y0 = y;
    if (y > *y1)
        *y1 = y;
}

/*  The extent of a piece: a straight's two ends; an arc's two ends and
 *  every quadrant point it sweeps through, in the search's own angular
 *  convention.  A hair wider, so a point the search finds on the piece
 *  is never a rounding outside it. */
static void piece_extent(const Piece *pc, float *x0, float *y0, float *x1, float *y1)
{
    if (!pc->arc)
    {
        *x0 = *x1 = pc->a.x;
        *y0 = *y1 = pc->a.y;
        extent_add(pc->b.x, pc->b.y, x0, y0, x1, y1);
    }
    else
    {
        float sg = pc->t1 > pc->t0 ? 1.0f : -1.0f, sw = fabsf(pc->t1 - pc->t0);
        int   q;
        *x0 = *x1 = pc->c.x + pc->r * cosf(pc->t0);
        *y0 = *y1 = pc->c.y + pc->r * sinf(pc->t0);
        extent_add(pc->c.x + pc->r * cosf(pc->t1), pc->c.y + pc->r * sinf(pc->t1), x0, y0, x1, y1);
        for (q = 0; q < 4; ++q)
        {
            float th = (float)q * 1.5707963f, rel = (th - pc->t0) * sg;
            while (rel < 0.0f)
                rel += 6.2831853f;
            while (rel >= 6.2831853f)
                rel -= 6.2831853f;
            if (rel <= sw)
                extent_add(pc->c.x + pc->r * cosf(th), pc->c.y + pc->r * sinf(th), x0, y0, x1, y1);
        }
    }
    *x0 -= 0.01f, *y0 -= 0.01f, *x1 += 0.01f, *y1 += 0.01f;
}

static int32_t tile_clamp(float v)
{
    int32_t t = (int32_t)floorf(v);
    return t < 0 ? 0 : t > R_MAP - 1 ? R_MAP - 1
                                     : t;
}

/*  A lane into the index: under every tile each of its pieces covers,
 *  once per tile (a tile's last filing is this lane's when the piece
 *  before covered it too).  Full is an error: a lane the index cannot
 *  find would be a lane the city does not have. */
static int lane_file(int li)
{
    const Lane *l = &s_lane[li];
    int         k;
    for (k = 0; k < l->np; ++k)
    {
        float   x0, y0, x1, y1;
        int32_t c0, r0, c1, r1, col, row;
        piece_extent(&s_lp[l->first + k], &x0, &y0, &x1, &y1);
        c0 = tile_clamp(x0), c1 = tile_clamp(x1), r0 = tile_clamp(y0), r1 = tile_clamp(y1);
        for (row = r0; row <= r1; ++row)
            for (col = c0; col <= c1; ++col)
            {
                int t = row * R_MAP + col;
                if (s_lx_head[t] >= 0 && s_lx_lane[s_lx_head[t]] == li)
                    continue;
                if (s_nlx >= L_FILED)
                    return -1;
                s_lx_lane[s_nlx] = li;
                s_lx_next[s_nlx] = s_lx_head[t];
                s_lx_head[t]     = s_nlx++;
            }
    }
    return 0;
}

static int int_cmp(const void *a, const void *b)
{
    int x = *(const int *)a, y = *(const int *)b;
    return (x > y) - (x < y);
}

/*  The lanes filed within `maxd` of a point, each once, in lane order:
 *  the tiles within ceil(maxd) of the point's own hold every lane a
 *  point that near could lie on, and a point past the map's edge reads
 *  the edge tile, where everything past it was filed. */
static int lane_candidates(V2 p, float maxd)
{
    int     reach = (int)ceilf(maxd), n = 0;
    int32_t c0 = tile_clamp(p.x - (float)reach), c1 = tile_clamp(p.x + (float)reach);
    int32_t r0 = tile_clamp(p.y - (float)reach), r1 = tile_clamp(p.y + (float)reach), col, row;
    ++s_lx_stamp;
    for (row = r0; row <= r1; ++row)
        for (col = c0; col <= c1; ++col)
        {
            int e;
            for (e = s_lx_head[row * R_MAP + col]; e >= 0; e = s_lx_next[e])
            {
                int li = s_lx_lane[e];
                if (s_lx_seen[li] == s_lx_stamp)
                    continue;
                s_lx_seen[li]  = s_lx_stamp;
                s_lx_cand[n++] = li;
            }
        }
    qsort(s_lx_cand, (size_t)n, sizeof *s_lx_cand, int_cmp);
    return n;
}

static int lane_add(uint8_t cls, Family f, const Piece *pc, int np, float w, float rmin, int tight, int k0, int p0, int k1, int p1, int band, float off)
{
    Lane *l;
    int   k;
    if (s_nl >= L_MAX || s_nlp + np > L_PIECES)
        return -1;
    l        = &s_lane[s_nl++];
    l->cls   = cls;
    l->fam   = (uint8_t)f;
    l->kind0 = (uint8_t)k0;
    l->kind1 = (uint8_t)k1;
    l->port0 = p0;
    l->port1 = p1;
    l->first = s_nlp;
    l->np    = np;
    l->w     = w;
    l->rmin  = rmin;
    l->tight = (uint8_t)tight;
    l->band  = band;
    l->off   = off;
    l->len   = 0.0f;
    for (k = 0; k < np; ++k)
    {
        s_lp[s_nlp++] = pc[k];
        l->len += pc[k].len;
    }
    return lane_file(s_nl - 1);
}

/*  The lane drawn as a coloured wire a hair above the ground, under
 *  "show curves" like the corridor highlights and the fit's nodes: the
 *  lane model on screen before it shapes anything.  Red for a
 *  segment's lanes, blue for a junction's connectors. */
/*  The deck's height near a point, from the band's recorded stations;
 *  the ground where the band has none. */
float deck_z_near(const RCity *c, uint8_t mask_bit, int band, V2 p)
{
    float best = 1e9f, z = 0.0f;
    int   k, have        = 0;
    for (k = 0; k < s_hw_nst; ++k)
    {
        float dx, dy, d;
        if (s_hw_st[k].band != band)
            continue;
        dx = s_hw_st[k].pos.x - p.x;
        dy = s_hw_st[k].pos.y - p.y;
        d  = dx * dx + dy * dy;
        if (d < best)
            best = d, z = s_hw_st[k].z, have = 1;
    }
    return have ? z : surface_at_world(c, mask_bit, p.x, p.y);
}

/*  ------------------------------------------------------------------
 *  The lane overlay's wires, gathered
 *
 *  The hairline over a lane is drawn only while the tuning window asks
 *  to see the curves, and it is drawn from four different passes.  Each
 *  of them gathers what it wants drawn, with the shape it belongs to, and
 *  the drive lays them all at the end -- entering each shape again, so
 *  the inspector still names the lane a wire runs over. */
#define WIRES_MAX 65536

static struct
{
    LaneFan f;
    ShapeId sh;
    int     at;
} *s_wire;
static int    s_n_wire, s_wire_cap;
static Piece *s_wire_pc;
static int    s_wire_pc_n, s_wire_pc_cap;

void net_wires_reset(void)
{
    s_n_wire = s_wire_pc_n = 0;
}

static int wire_add(const LaneFan *f)
{
    int i;
    if (s_n_wire >= s_wire_cap)
    {
        int   cap = s_wire_cap ? s_wire_cap * 2 : 4096;
        void *p;
        if (cap > WIRES_MAX)
            cap = WIRES_MAX;
        if (s_n_wire >= cap || (p = realloc(s_wire, (size_t)cap * sizeof *s_wire)) == NULL)
        {
            R_ERR("net", "no room for the lane overlay: %d wires is the most held", WIRES_MAX);
            return -1;
        }
        s_wire = p, s_wire_cap = cap;
    }
    if (s_wire_pc_n + f->np > s_wire_pc_cap)
    {
        int   cap = s_wire_pc_cap ? s_wire_pc_cap * 2 : 65536;
        void *p;
        while (cap < s_wire_pc_n + f->np)
            cap *= 2;
        if ((p = realloc(s_wire_pc, (size_t)cap * sizeof *s_wire_pc)) == NULL)
        {
            R_ERR("net", "no room for the lane overlay's pieces");
            return -1;
        }
        s_wire_pc = p, s_wire_pc_cap = cap;
    }
    i             = s_n_wire++;
    s_wire[i].f   = *f;
    s_wire[i].sh  = shape_current();
    s_wire[i].at  = s_wire_pc_n;
    for (int k = 0; k < f->np; ++k)
        s_wire_pc[s_wire_pc_n + k] = f->pc[k];
    s_wire_pc_n += f->np;
    return 0;
}

int net_wires(void)
{
    return s_n_wire;
}

/*  One of them, with its own shape entered again. */
LaneFan *net_wire_at(int i)
{
    if (i < 0 || i >= s_n_wire)
        return NULL;
    s_wire[i].f.pc = s_wire_pc + s_wire[i].at;
    shape_use(s_wire[i].sh);
    return &s_wire[i].f;
}

void net_wire_done(int i)
{
    if (i >= 0 && i < s_n_wire)
        shape_close(s_wire[i].sh);
}

static int lane_wires(RMesh *m, const RCity *c, uint8_t mask_bit, const Piece *pc, int np, float lift, float paint, int band)
{
    LaneFan f;
    if (!(s_tune.show_curves > 0.5f) || s_pass == 1)
        return 0;
    f.m = m, f.c = c, f.mask_bit = mask_bit;
    f.pc = pc, f.np = np;
    f.lift = lift, f.paint = paint, f.band = band;
    f.ramp = 0, f.off = 0;
    f.step = 0.0f;
    return wire_add(&f);
}

/*  The trim an arm's segment will actually be cut at: walk_segment scales
 *  both of a short segment's trims back together so at least half of it
 *  survives, and the port has to land where the lane ends (Atlanta: 328
 *  lane ends an eighth short of their port, every segment between two
 *  adjacent junctions).  The rail box draws to the same cut. */
float arm_cut(Family f, int col, int row, int e)
{
    const RArm *a = &s_arm[FAMX(f)][(row * R_MAP + col) * 4 + e];
    float       t = s_trim[FAMX(f)][(row * R_MAP + col) * 4 + e];
    if (a->have)
    {
        float t1 = a->fkind == 2 ? s_trim[FAMX(f)][(a->frow * R_MAP + a->fcol) * 4 + a->fe] : 0.0f;
        if (t + t1 > a->len * 0.5f && t + t1 > 1e-4f)
            t *= (a->len * 0.5f) / (t + t1);
    }
    return t;
}

/*  The pose of a junction's port: where arm e's lane crosses the mouth
 *  the junction cut for it, and which way it heads.  The arm's own ray
 *  and trim are the ones junction_poly used, so the segment's lane,
 *  cut at that trim on that path, ends exactly here. */
static void port_pose(Family f, int col, int row, int e, int out, int k, V2 *pos, V2 *dir)
{
    const RArm *a = &s_arm[FAMX(f)][(row * R_MAP + col) * 4 + e];
    float       w = *net_family(f)->width * 0.5f, offs[2], h;
    lane_offsets(f, a->have ? a->cls : (net_family(f)->classed ? 0 : -1), offs);
    h        = offs[k];
    float cx = (float)col + 0.5f, cy = (float)row + 0.5f;
    V2    d = a->have ? (V2){a->dx, a->dy} : (V2){ROAD_DU[e], ROAD_DV[e]};
    V2    o = a->have ? (V2){a->ax, a->ay} : (V2){cx + ROAD_DU[e] * w, cy + ROAD_DV[e] * w};
    float t = arm_cut(f, col, row, e);
    V2    M, r;
    M = (V2){o.x + d.x * t, o.y + d.y * t};
    /*  A turnout's arm is cut along its own path, and the port is on the
     *  path, heading its way: an arm that bends within the reach (the
     *  staircase rail at Atlanta 72,124, hooking south a tile from the
     *  junction) is then met by the turnout's routed tracks where its track
     *  is, with the whole reach to curve in, not on a ray it never runs
     *  along. */
    if (net_family(f)->turnout > 0.0f && a->have)
    {
        static Piece pc[MAX_PIECES];
        int          np;
        if (seg_table_pieces_from(col, row, e, pc, MAX_PIECES, &np) == 0)
            pieces_at(pc, np, t, &M, &d);
    }
    if (!out)
        d = (V2){-d.x, -d.y}; /* inbound: heading into the junction */
    r    = l_right(d);
    *pos = (V2){M.x + r.x * h, M.y + r.y * h};
    *dir = d;
}

/*  --lane-dump C,R prints that junction's ports and connectors and the
 *  lanes of the segments that reach it; a bare --lane-dump prints only
 *  the lane ends that miss their port, city-wide. */
static int dump_here(int col, int row)
{
    const char *s = g_dev.lane_dump_at;
    int         dc, dr;
    return s && sscanf(s, "%d,%d", &dc, &dr) == 2 && dc == col && dr == row;
}

static int dump_misses(void)
{
    const char *s = g_dev.lane_dump_at;
    int         dc, dr;
    return g_dev.lane_dump && !(s && sscanf(s, "%d,%d", &dc, &dr) == 2);
}

void lane_dump_pieces(const Piece *pc, int np)
{
    int k;
    for (k = 0; k < np; ++k)
    {
        V2 a, b, d;
        l_piece_at(&pc[k], 0.0f, &a, &d);
        l_piece_at(&pc[k], pc[k].len, &b, &d);
        if (pc[k].arc)
            dumpf("    arc  r %.3f  %.3f,%.3f -> %.3f,%.3f  len %.3f\n", (double)pc[k].r, (double)a.x, (double)a.y, (double)b.x, (double)b.y, (double)pc[k].len);
        else
            dumpf("    run  %.3f,%.3f -> %.3f,%.3f  len %.3f\n", (double)a.x, (double)a.y, (double)b.x, (double)b.y, (double)pc[k].len);
    }
}

/*  A ramp tile, when one lies at col,row. */
static const HwRamp *ramp_tile(int32_t col, int32_t row)
{
    int r;
    for (r = 0; r < s_hw_nramps; ++r)
        if (s_hw_ramps[r].rc == col && s_hw_ramps[r].rr == row)
            return &s_hw_ramps[r];
    return NULL;
}

/*  The same, for the box builder in junction.c. */
const HwRamp *lane_ramp_tile(int32_t col, int32_t row)
{
    return ramp_tile(col, row);
}

/*  A road tile in the data: a road piece, a road crossing, or a bridge
 *  or deck with a road under it. */
static int roadish(uint8_t b)
{
    return net_road_near(b);
}

/*  How the ramp at arm `e` of this crossing piece meets the box's lanes: 0,
 *  not through the box at all -- the ramp's own join peels off or merges
 *  into the road's near lane beside it; 1, as an arm of the box with one
 *  port that every other arm's outermost lane may use; 2, as an arm with
 *  one port that only the lane arriving straight at it uses.  The data puts
 *  a crossing piece beside every ramp, and which of these it is depends on
 *  the roads: three or more road arms is a real junction, the crossing with
 *  lights (1); one road arm is a stub, the road ending in its ramps (1);
 *  two road arms with the ramp straight ahead of one of them is a bend
 *  whose through movement is the ramp; two road arms passing the ramp at
 *  its side is a road, and its near lane alone meets the ramp (0: "unless
 *  there's already an intersection with lights, that shouldn't be
 *  possible"). */
int lane_ramp_arm(const RCity *c, int32_t col, int32_t row, int links, int e)
{
    int k, n = 0, road[4] = {0, 0, 0, 0};
    for (k = 0; k < 4; ++k)
    {
        int32_t nc = col + (int32_t)lroundf(ROAD_DU[k]), nr = row + (int32_t)lroundf(ROAD_DV[k]);
        if (!(links & (1 << k)) || nc < 0 || nr < 0 || nc >= R_MAP || nr >= R_MAP)
            continue;
        if (roadish(c->xbld[nr * R_MAP + nc]) && !ramp_tile(nc, nr))
            road[k] = 1, ++n;
    }
    if (n != 2)
        return 1;
    return road[(e + 2) & 3] ? 2 : 0;
}
/*  The connectors of a junction: from each arm's inbound lane to each
 *  other arm's outbound lane, routed. */
/*  A junction's lanes as they are built: the arms' ports, in and out,
 *  each lane's pose at them, and which arm is a ramp with its one port. */
typedef struct
{
    RMesh       *m;
    const RCity *c;
    uint8_t      mask_bit;
    Family       f;
    int32_t      col, row;
    int          links;
    float        w; /* the family's half width, the connectors' */
    int          dump;
    Piece       *tmp;
    V2           pin[4][2], din[4][2], pout[4][2], dout[4][2]; /* per arm and lane: the inbound port's pose, the outbound's */
    int          nl[4], has_in[4], has_out[4], is_ramp[4];
} LaneJunc;

static const char EN[4] = {'N', 'E', 'S', 'W'};

/*  The arms' ports: each linked arm's lanes by its class, and the pose
 *  of every lane's inbound and outbound port on it.  A ramp tile beside
 *  the junction gets no road lanes here: a real junction gives the ramp
 *  its one port below. */
static void lj_ports(LaneJunc *x)
{
    Family  f     = x->f;
    int32_t col   = x->col;
    int32_t row   = x->row;
    int     links = x->links;
    int     dump  = x->dump;
    V2(*pin)
    [2] = x->pin;
    V2(*din)
    [2] = x->din;
    V2(*pout)
    [2] = x->pout;
    V2(*dout)
    [2]          = x->dout;
    int *nl      = x->nl;
    int *has_in  = x->has_in;
    int *has_out = x->has_out;
    int  e, k;
    for (e = 0; e < 4; ++e)
    {
        const RArm *a = &s_arm[FAMX(f)][(row * R_MAP + col) * 4 + e];
        float       offs[2];
        if (!(links & (1 << e)))
            continue;
        if (col + (int32_t)lroundf(ROAD_DU[e]) < 0 || row + (int32_t)lroundf(ROAD_DV[e]) < 0 || col + (int32_t)lroundf(ROAD_DU[e]) >= R_MAP ||
            row + (int32_t)lroundf(ROAD_DV[e]) >= R_MAP)
            continue; /* an arm off the map's edge: the world beyond has no lanes */
        if (net_family(f)->ramps && ramp_tile(col + (int32_t)lroundf(ROAD_DU[e]), row + (int32_t)lroundf(ROAD_DV[e])))
            continue; /* a ramp tile: no road lanes there; a real junction gives the ramp its one port below */
        nl[e]      = lane_offsets(f, a->have ? a->cls : (net_family(f)->classed ? 0 : -1), offs);
        has_in[e]  = 1;
        has_out[e] = 1;
        for (k = 0; k < nl[e]; ++k)
        {
            port_pose(f, col, row, e, 0, k, &pin[e][k], &din[e][k]);
            port_pose(f, col, row, e, 1, k, &pout[e][k], &dout[e][k]);
            s_ls.ports += 2;
            if (dump)
                dumpf("LANE port %d,%d %c lane %d  in %.3f,%.3f heading %.2f,%.2f  out %.3f,%.3f heading %.2f,%.2f\n", (int)col, (int)row, EN[e], k, (double)pin[e][k].x, (double)pin[e][k].y, (double)din[e][k].x, (double)din[e][k].y, (double)pout[e][k].x, (double)pout[e][k].y, (double)dout[e][k].x, (double)dout[e][k].y);
        }
    }
}

/*  A RAMP beside the junction is an arm of it with ONE port: an ON ramp's,
 *  outbound, that the connectors feed; an OFF ramp's, inbound, that they
 *  drain.  Its pose is the tile edge's middle, where the ramp's own lane
 *  ends. */
static void lj_ramp_ports(LaneJunc *x)
{
    const RCity *c     = x->c;
    int32_t      col   = x->col;
    int32_t      row   = x->row;
    int          links = x->links;
    int          dump  = x->dump;
    V2(*pin)
    [2] = x->pin;
    V2(*din)
    [2] = x->din;
    V2(*pout)
    [2] = x->pout;
    V2(*dout)
    [2]          = x->dout;
    int *nl      = x->nl;
    int *has_in  = x->has_in;
    int *has_out = x->has_out;
    int *is_ramp = x->is_ramp;
    int  e;
    for (e = 0; e < 4; ++e)
    {
        int32_t nc = col + (int32_t)lroundf(ROAD_DU[e]), nr = row + (int32_t)lroundf(ROAD_DV[e]);
        int     r, mode;
        if (nc < 0 || nr < 0 || nc >= R_MAP || nr >= R_MAP)
            continue; /* linked or not: a ramp tile counts as a road link in the data, and is one-way all the same */
        mode = lane_ramp_arm(c, col, row, links, e);
        if (!mode)
            continue; /* the ramp's own join meets the near lane beside the box */
        for (r = 0; r < s_hw_nramps; ++r)
        {
            const HwRamp *rp = &s_hw_ramps[r];
            V2            rd;
            if (rp->rc != nc || rp->rr != nr)
                continue;
            rd = rp->opp ? (V2){-rp->toward.x, -rp->toward.y} : rp->off ? rp->along
                                                                        : (V2){-rp->along.x, -rp->along.y};
            if (nc + (int32_t)lroundf(rd.x) != col || nr + (int32_t)lroundf(rd.y) != row)
                continue; /* its road tile is not this one */
            nl[e]      = 1;
            is_ramp[e] = mode;
            has_in[e]  = rp->off ? 1 : 0;
            has_out[e] = rp->off ? 0 : 1;
            pin[e][0] = pout[e][0] = (V2){(float)col + 0.5f + ROAD_DU[e] * 0.5f, (float)row + 0.5f + ROAD_DV[e] * 0.5f};
            dout[e][0]             = (V2){ROAD_DU[e], ROAD_DV[e]};
            din[e][0]              = (V2){-ROAD_DU[e], -ROAD_DV[e]};
            s_ls.ports += 1;
            if (dump)
                dumpf("LANE port %d,%d %c ramp %s at %.3f,%.3f\n", (int)col, (int)row, EN[e], rp->off ? "in" : "out", (double)pin[e][0].x, (double)pin[e][0].y);
            break;
        }
    }
}

/*  A connector from every inbound lane to every outbound lane of the
 *  other arms.  With two lanes each way the inner lane goes to the
 *  inner, the outer to the outer, as the markings would have it. */
static int lj_connectors(LaneJunc *x)
{
    RMesh       *m        = x->m;
    const RCity *c        = x->c;
    uint8_t      mask_bit = x->mask_bit;
    Family       f        = x->f;
    int32_t      col      = x->col;
    int32_t      row      = x->row;
    float        w        = x->w;
    int          dump     = x->dump;
    Piece       *tmp      = x->tmp;
    V2(*pin)
    [2] = x->pin;
    V2(*din)
    [2] = x->din;
    V2(*pout)
    [2] = x->pout;
    V2(*dout)
    [2]          = x->dout;
    int *nl      = x->nl;
    int *has_in  = x->has_in;
    int *has_out = x->has_out;
    int *is_ramp = x->is_ramp;
    int  e, e2, k, k2;
    for (e = 0; e < 4; ++e)
        for (e2 = 0; e2 < 4; ++e2)
            for (k = 0; k < nl[e]; ++k)
                for (k2 = 0; k2 < nl[e2]; ++k2)
                {
                    int   np, tight;
                    float rmin;
                    if (e == e2 || !has_in[e] || !has_out[e2])
                        continue;
                    if (nl[e] == 2 && nl[e2] == 2 && k != k2)
                        continue;
                    /*  A ramp has one lane, so one lane per arm feeds it or
                     *  leaves it: the outermost. */
                    if ((is_ramp[e2] && k != nl[e] - 1) || (is_ramp[e] && k2 != nl[e2] - 1))
                        continue;
                    /*  A ramp straight ahead of a bend's arm is that arm's
                     *  through movement and nobody else's: the other road
                     *  turns, it does not take the ramp. */
                    if ((is_ramp[e2] == 2 && e != ((e2 + 2) & 3)) || (is_ramp[e] == 2 && e2 != ((e + 2) & 3)))
                        continue;
                    if (lane_route(pin[e][k], din[e][k], pout[e2][k2], dout[e2][k2], tmp, &np, &rmin) != 0)
                    {
                        ++s_ls.failed;
                        if (dump)
                            dumpf("LANE conn %d,%d %c%d in -> %c%d out: no route\n", (int)col, (int)row, EN[e], k, EN[e2], k2);
                        continue;
                    }
                    tight = rmin < LANE_RMIN;
                    ++s_ls.conns;
                    s_ls.tight += tight;
                    if (tight && dump_misses())
                        dumpf("LANE tight %d,%d %c%d in -> %c%d out: rmin %.3f\n", (int)col, (int)row, EN[e], k, EN[e2], k2, (double)rmin);
                    if (lane_add(LC_TURN, f, tmp, np, w, rmin, tight, LE_PORT, port_id(col, row, e, 0, k), LE_PORT, port_id(col, row, e2, 1, k2), 0, 0.0f) != 0)
                        return -1;
                    if (dump)
                    {
                        dumpf("LANE conn %d,%d %c%d in -> %c%d out: %d pieces, rmin %.3f%s\n", (int)col, (int)row, EN[e], k, EN[e2], k2, np, rmin < 1e8f ? (double)rmin : 0.0, tight ? " TIGHT" : "");
                        lane_dump_pieces(tmp, np);
                    }
                    if (lane_wires(m, c, mask_bit, tmp, np, net_family_rules(F_ROAD)->lane_wire, 6.0f, 0) != 0) /* a connector in blue */
                        return -1;
                }
    return 0;
}

int lane_junction(RMesh *m, const RCity *c, uint8_t mask_bit, Family f, int32_t col, int32_t row, int links)
{
    if (grade_only(g_dev.grade_lanes))
        return 0; /* the grading pass: lanes are the building pass's */
    static Piece tmp[MAX_PIECES];
    LaneJunc     x;
    memset(&x, 0, sizeof x);
    x.m = m, x.c = c, x.mask_bit = mask_bit, x.f = f, x.col = col, x.row = row, x.links = links, x.tmp = tmp;
    x.w    = *net_family(f)->width * 0.5f;
    x.dump = dump_here(col, row);
    /* the stages: the arms' ports, a ramp's one port, the connectors between them */
    lj_ports(&x);
    if (net_family(f)->ramps)
        lj_ramp_ports(&x);
    ++s_ls.junctions;
    return lj_connectors(&x);
}

/*  A segment's two lanes from its fitted, trimmed pieces: one with the
 *  path on the viewer's right of it, one against the path on the other
 *  side, reversed so every lane runs the way its traffic does.  Each
 *  end is the port the segment's node gives it, and its pose is checked
 *  against the port's own. */
/*  A segment's lanes as they are built: the segment as the walk gave
 *  it, the lane offsets its class allows, and each lane's end poses and
 *  table index for the dead ends' caps. */
typedef struct
{
    RMesh       *m;
    const RCity *c;
    uint8_t      mask_bit;
    Family       f;
    const Piece *pc;
    int          np;
    int32_t      col, row, cc, cr;
    int          e, back, kind0, kind1, cls;
    int          nlane;
    float        offs[2];
    int          dump;
    Piece       *tmp;
    V2           ep[2][2][2], ed[2][2][2]; /* [side][lane][start/end]: each lane's end poses */
    int          lid[2][2];                /* and their indices in the table */
} LaneSeg;

/*  Does a lane's end meet its port?  A miss is counted and, under the
 *  dumps, said. */
static void ls_port_check(const LaneSeg *x, int side, int li, int k0, int k1, int n0c, int n0r, int n0e, int n1c, int n1r, int n1e, const Piece *tmp)
{
    Family  f    = x->f;
    int     np   = x->np;
    int32_t col  = x->col;
    int32_t row  = x->row;
    int32_t cc   = x->cc;
    int32_t cr   = x->cr;
    int     dump = x->dump;
    int     which;
    for (which = 0; which < 2; ++which)
    {
        V2    pp, pd, lp, ld;
        float dx, dy, dist, dot;
        if (which ? k1 != LE_PORT : k0 != LE_PORT)
            continue;
        port_pose(f, which ? n1c : n0c, which ? n1r : n0r, which ? n1e : n0e, which ? 0 : 1, li, &pp, &pd);
        if (which)
            l_piece_at(&tmp[np - 1], tmp[np - 1].len, &lp, &ld);
        else
            l_piece_at(&tmp[0], 0.0f, &lp, &ld);
        dx   = lp.x - pp.x;
        dy   = lp.y - pp.y;
        dist = sqrtf(dx * dx + dy * dy);
        dot  = ld.x * pd.x + ld.y * pd.y;
        if (dist > net_family_rules(F_ROAD)->lane_join || dot < net_family_rules(F_ROAD)->lane_aim)
        {
            ++s_ls.mismatch;
            if (dump || dump_misses())
                dumpf("LANE seg %d,%d->%d,%d side %d lane %d %s: lane at %.3f,%.3f (%.2f,%.2f), port at %.3f,%.3f (%.2f,%.2f): off by "
                      "%.3f\n",
                      (int)col,
                      (int)row,
                      (int)cc,
                      (int)cr,
                      side,
                      li,
                      which ? "end" : "start",
                      (double)lp.x,
                      (double)lp.y,
                      (double)ld.x,
                      (double)ld.y,
                      (double)pp.x,
                      (double)pp.y,
                      (double)pd.x,
                      (double)pd.y,
                      (double)dist);
        }
    }
}

/*  One lane of the segment: the pieces offset to its side (reversed
 *  for the far side, so every lane runs its own way), its ends -- a
 *  port at a junction, open elsewhere -- in the table, its end poses
 *  kept for the caps, and its wire. */
static int ls_lane(LaneSeg *x, int side, int li)
{
    RMesh       *m        = x->m;
    const RCity *c        = x->c;
    uint8_t      mask_bit = x->mask_bit;
    Family       f        = x->f;
    const Piece *pc       = x->pc;
    int          np       = x->np;
    int32_t      col      = x->col;
    int32_t      row      = x->row;
    int          e        = x->e;
    int          kind0    = x->kind0;
    int32_t      cc       = x->cc;
    int32_t      cr       = x->cr;
    int          back     = x->back;
    int          kind1    = x->kind1;
    int          cls      = x->cls;
    int          nlane    = x->nlane;
    const float *offs     = x->offs;
    int          dump     = x->dump;
    Piece       *tmp      = x->tmp;
    V2(*ep)
    [2][2] = x->ep;
    V2(*ed)
    [2][2]        = x->ed;
    int (*lid)[2] = x->lid;
    int   k;
    int   k0, p0, k1, p1, n0c, n0r, n0e, n0k, n1c, n1r, n1e, n1k;
    float h = offs[li], rmin = 1e9f;
    for (k = 0; k < np; ++k)
        piece_offset(&pc[k], side ? -h : h, &tmp[k]);
    if (side)
        pieces_reverse(tmp, np);
    for (k = 0; k < np; ++k)
        if (tmp[k].arc && tmp[k].r < rmin)
            rmin = tmp[k].r;
    if (!side)
    {
        n0c = col, n0r = row, n0e = e, n0k = kind0;
        n1c = cc, n1r = cr, n1e = back, n1k = kind1;
    }
    else
    {
        n0c = cc, n0r = cr, n0e = back, n0k = kind1;
        n1c = col, n1r = row, n1e = e, n1k = kind0;
    }
    k0 = n0k == 2 ? LE_PORT : LE_OPEN;
    p0 = n0k == 2 ? port_id(n0c, n0r, n0e, 1, li) : -1;
    k1 = n1k == 2 ? LE_PORT : LE_OPEN;
    p1 = n1k == 2 ? port_id(n1c, n1r, n1e, 0, li) : -1;
    if (lane_add(LC_ROAD, f, tmp, np, offs[nlane - 1], rmin, rmin < LANE_RMIN, k0, p0, k1, p1, 0, side ? -h : h) != 0)
        return -1;
    lid[side][li] = s_nl - 1;
    l_piece_at(&tmp[0], 0.0f, &ep[side][li][0], &ed[side][li][0]);
    l_piece_at(&tmp[np - 1], tmp[np - 1].len, &ep[side][li][1], &ed[side][li][1]);
    ls_port_check(x, side, li, k0, k1, n0c, n0r, n0e, n1c, n1r, n1e, tmp);
    if (dump)
    {
        dumpf("LANE seg %d,%d->%d,%d class %d side %d lane %d: %d pieces, rmin %.3f\n", (int)col, (int)row, (int)cc, (int)cr, cls, side, li, np, rmin < 1e8f ? (double)rmin : 0.0);
        lane_dump_pieces(tmp, np);
    }
    if (dump_misses() && net_family(f)->classed && cls > 0 && side == 0 && li == 0)
        dumpf("LANE wide %d,%d->%d,%d class %d\n", (int)col, (int)row, (int)cc, (int)cr, cls);
    if (lane_wires(m, c, mask_bit, tmp, np, net_family_rules(F_ROAD)->lane_wire, net_family(f)->lane_paint, 0) != 0)
        return -1;
    return 0;
}

/*  A rail terminus: the train reverses.  The arriving track's end names
 *  the leaving track's start and no piece is drawn (Atlanta 89,74 and
 *  90,76 had two tracks ending at a tile's centre). */
static void ls_caps_rail(LaneSeg *x)
{
    int kind0     = x->kind0;
    int kind1     = x->kind1;
    int nlane     = x->nlane;
    int (*lid)[2] = x->lid;
    int end;
    for (end = 0; end < 2; ++end)
    {
        int kind = end ? kind1 : kind0;
        int sa = end ? 0 : 1, sb = end ? 1 : 0;
        if (kind != 1 || nlane < 1)
            continue;
        s_lane[lid[sa][0]].kind1 = LE_LANE;
        s_lane[lid[sa][0]].port1 = lid[sb][0];
        s_lane[lid[sb][0]].kind0 = LE_LANE;
        s_lane[lid[sb][0]].port0 = lid[sa][0];
        ++s_ls.caps;
    }
}

/*  A dead end's lanes connect round its cap: from the lane arriving at the
 *  end round to the one leaving it, lane for lane, the biarc between two
 *  facing poses a lane's offset apart -- a semicircle of that offset,
 *  inside the round cap the loft draws there. kind 1 is an end on open
 *  land. */
static int ls_caps_road(LaneSeg *x)
{
    RMesh       *m        = x->m;
    const RCity *c        = x->c;
    uint8_t      mask_bit = x->mask_bit;
    Family       f        = x->f;
    int          kind0    = x->kind0;
    int          kind1    = x->kind1;
    int          nlane    = x->nlane;
    const float *offs     = x->offs;
    Piece       *tmp      = x->tmp;
    V2(*ep)
    [2][2] = x->ep;
    V2(*ed)
    [2][2]        = x->ed;
    int (*lid)[2] = x->lid;
    int li;
    int end;
    for (end = 0; end < 2; ++end)
    {
        int kind = end ? kind1 : kind0;
        if (kind != 1)
            continue;
        for (li = 0; li < nlane; ++li)
        {
            /* the lane arriving at this end: side 0 arrives at node 1, side 1 at node 0 */
            int   sa = end ? 0 : 1, sb = end ? 1 : 0, np2;
            float rmin;
            if (lane_route(ep[sa][li][1], ed[sa][li][1], ep[sb][li][0], ed[sb][li][0], tmp, &np2, &rmin) != 0 || np2 < 1)
            {
                ++s_ls.cap_fail;
                continue;
            }
            ++s_ls.caps;
            if (lane_add(LC_TURN, f, tmp, np2, offs[nlane - 1], rmin, 0, LE_LANE, lid[sa][li], LE_LANE, lid[sb][li], 0, 0.0f) != 0)
                return -1;
            if (lane_wires(m, c, mask_bit, tmp, np2, net_family_rules(F_ROAD)->lane_wire, 6.0f, 0) != 0)
                return -1;
        }
    }
    return 0;
}

int lane_segment(RMesh *m, const RCity *c, uint8_t mask_bit, Family f, const Piece *pc, int np, int32_t col, int32_t row, int e, int kind0, int32_t cc, int32_t cr, int back, int kind1, float hw, int cls)
{
    if (grade_only(g_dev.grade_lanes))
        return 0; /* the grading pass: lanes are the building pass's */
    static Piece tmp[MAX_PIECES];
    LaneSeg      x;
    int          side, li;
    (void)hw;
    memset(&x, 0, sizeof x);
    x.m = m, x.c = c, x.mask_bit = mask_bit, x.f = f, x.pc = pc, x.np = np, x.col = col, x.row = row, x.e = e;
    x.kind0 = kind0, x.cc = cc, x.cr = cr, x.back = back, x.kind1 = kind1, x.cls = cls, x.tmp = tmp;
    x.nlane = lane_offsets(f, cls, x.offs);
    x.dump  = dump_here(col, row) || dump_here(cc, cr);
    if (np < 1 || np > MAX_PIECES)
        return 0;
    ++s_ls.segs;
    if (net_family(f)->classed && cls > 0)
        ++s_ls.wide;
    /* the stages: each lane of each side, then the dead ends' caps */
    for (side = 0; side < 2; ++side)
        for (li = 0; li < x.nlane; ++li)
            if (ls_lane(&x, side, li) != 0)
                return -1;
    switch (net_family(f)->lane_ends)
    {
        case NET_LANE_ENDS_REVERSE:
            ls_caps_rail(&x);
            break;
        case NET_LANE_ENDS_CAP:
            if (ls_caps_road(&x) != 0)
                return -1;
            break;
        default:
            break;
    }
    return 0;
}

/*  A deck's six lanes from its fitted pieces, three each way at the centres
 *  terrain.frag marks: the barrier to 0.11 of the half width, dashes at
 *  0.40 and 0.70, the edge line at 0.94. Recorded under the band the loft
 *  records its stations under, so a ramp can find the outer lane it leaves. */
int lane_deck(RMesh *m, const RCity *c, uint8_t mask_bit, const Piece *pc, int np, float hw, int band)
{
    if (grade_only(g_dev.grade_lanes))
        return 0; /* the grading pass: lanes are the building pass's */
    const float *OFF = net_family_rules(F_ROAD)->deck_lane;
    static Piece       tmp[MAX_PIECES];
    int                side, k, q;
    if (np < 1 || np > MAX_PIECES)
        return 0;
    ++s_ls.decks;
    for (side = 0; side < 2; ++side)
        for (k = 0; k < 3; ++k)
        {
            float off = OFF[k] * hw * (side ? -1.0f : 1.0f), rmin = 1e9f;
            for (q = 0; q < np; ++q)
                piece_offset(&pc[q], off, &tmp[q]);
            if (side)
                pieces_reverse(tmp, np);
            for (q = 0; q < np; ++q)
                if (tmp[q].arc && tmp[q].r < rmin)
                    rmin = tmp[q].r;
            if (lane_add(LC_DECK, net_hiway->f, tmp, np, hw, rmin, rmin < LANE_RMIN, LE_OPEN, -1, LE_OPEN, -1, band, off) != 0)
                return -1;
            if (lane_wires(m, c, mask_bit, tmp, np, net_family_rules(F_ROAD)->lane_wire, net_hiway->lane_paint, band) != 0)
                return -1;
        }
    return 0;
}

/*  The nearest lane of a class (and band, when not -1) to a point,
 *  running roughly the way `dir` does: the lane's index, or -1 when
 *  none lies within `maxd`; the station on it nearest the point, its
 *  direction of travel there, and the distance. */
static int lane_pick(V2 p, V2 dir, int cls, int band, float maxd, int outer, V2 *pos, V2 *odir, float *dist)
{
    int   li, best = -1, ci, n = lane_candidates(p, maxd);
    float bd = maxd, boff = -1.0f;
    for (ci = 0; ci < n; ++ci)
    {
        const Lane *l;
        int         k;
        li = s_lx_cand[ci];
        l  = &s_lane[li];
        if (l->cls != cls || (band >= 0 && l->band != band))
            continue;
        for (k = 0; k < l->np; ++k)
        {
            const Piece *pc = &s_lp[l->first + k];
            V2           q, dq;
            float        d;
            if (!pc->arc)
            {
                float len = pc->len > 1e-6f ? pc->len : 1.0f;
                V2    u   = {(pc->b.x - pc->a.x) / len, (pc->b.y - pc->a.y) / len};
                float t   = (p.x - pc->a.x) * u.x + (p.y - pc->a.y) * u.y;
                if (t < 0.0f)
                    t = 0.0f;
                if (t > pc->len)
                    t = pc->len;
                q  = (V2){pc->a.x + u.x * t, pc->a.y + u.y * t};
                dq = u;
            }
            else
            {
                float sg = pc->t1 > pc->t0 ? 1.0f : -1.0f, sw = fabsf(pc->t1 - pc->t0);
                float an = atan2f(p.y - pc->c.y, p.x - pc->c.x), rel = (an - pc->t0) * sg, th;
                while (rel < 0.0f)
                    rel += 6.2831853f;
                while (rel >= 6.2831853f)
                    rel -= 6.2831853f;
                if (rel > sw)
                    rel = (rel - sw) < (6.2831853f - rel) ? sw : 0.0f; /* past the arc: the nearer end */
                th = pc->t0 + rel * sg;
                q  = (V2){pc->c.x + pc->r * cosf(th), pc->c.y + pc->r * sinf(th)};
                dq = (V2){-sinf(th) * sg, cosf(th) * sg};
            }
            d = sqrtf((p.x - q.x) * (p.x - q.x) + (p.y - q.y) * (p.y - q.y));
            /*  Nearest; or, for `outer`, the lane farthest from its road's
             *  centreline among those within reach -- a ramp takes the
             *  kerb-side lane, whatever lies nearer the point asked for. */
            if (dq.x * dir.x + dq.y * dir.y > net_family_rules(F_ROAD)->lane_pick_dot && d < maxd &&
                (outer ? (fabsf(l->off) > boff + 1e-4f || (fabsf(l->off) > boff - 1e-4f && d < bd)) : d < bd))
            {
                bd    = d;
                boff  = fabsf(l->off);
                best  = li;
                *pos  = q;
                *odir = dq;
            }
        }
    }
    *dist = bd;
    return best;
}

int lane_nearest(V2 p, V2 dir, int cls, int band, float maxd, V2 *pos, V2 *odir, float *dist)
{
    return lane_pick(p, dir, cls, band, maxd, 0, pos, odir, dist);
}

/*  The outermost lane of a class within reach, running the way `dir`
 *  does: the one a ramp joins. */
int lane_nearest_outer(V2 p, V2 dir, int cls, float maxd, V2 *pos, V2 *odir, float *dist)
{
    return lane_pick(p, dir, cls, -1, maxd, 1, pos, odir, dist);
}

/*  A ramp, once built, as a lane of its own: its ends name the deck lane it
 *  leaves and the road lane it joins, and an end that found none is
 *  counted. */
/*  A ramp's wire: its height eases from the deck at the gore to the
 *  ground at the road, as the loft's does (hiway_lane_ease), so the
 *  overlay shows the lane meeting the deck's lane where it does. */
static int ramp_wires(RMesh *m, const RCity *c, uint8_t mask_bit, const Piece *pc, int np, int off, float paint)
{
    LaneFan f;
    if (!(s_tune.show_curves > 0.5f) || s_pass == 1)
        return 0;
    f.m = m, f.c = c, f.mask_bit = mask_bit;
    f.pc = pc, f.np = np;
    f.lift = net_family_rules(F_ROAD)->lane_lift, f.paint = paint, f.band = 0;
    f.ramp = 1, f.off = off, f.step = net_family_rules(F_ROAD)->lane_step_ramp;
    return wire_add(&f);
}

int lane_ramp(RMesh *m, const RCity *c, uint8_t mask_bit, const Piece *pc, int np, float hw, int deck_lane, int road_lane, int road_port, int off)
{
    if (grade_only(g_dev.grade_lanes))
        return 0; /* the grading pass: lanes are the building pass's */
    float rmin = 1e9f;
    int   k;
    if (np < 1 || np > MAX_PIECES)
        return 0;
    for (k = 0; k < np; ++k)
        if (pc[k].arc && pc[k].r < rmin)
            rmin = pc[k].r;
    ++s_ls.ramps;
    s_ls.ramp_miss += (deck_lane < 0) + (road_lane < 0 && road_port < 0);
    /*  In travel order: an ON ramp starts at the road (a lane, or a
     *  junction's port) and ends on the deck lane; an OFF ramp the other
     *  way round. */
    {
        int rk = road_port >= 0 ? LE_PORT : road_lane >= 0 ? LE_LANE
                                                           : LE_OPEN,
            rv = road_port >= 0 ? road_port : road_lane;
        int dk = deck_lane >= 0 ? LE_LANE : LE_OPEN;
        if (lane_add(LC_RAMP, net_hiway->f, pc, np, hw, rmin, rmin < LANE_RMIN, off ? dk : rk, off ? deck_lane : rv, off ? rk : dk, off ? rv : deck_lane, 0, 0.0f) != 0)
            return -1;
    }
    return ramp_wires(m, c, mask_bit, pc, np, off, 5.0f); /* a ramp is a lane: red like a road's */
}

/*  The pose of a lane's end: 0 its start, 1 its finish, in its own
 *  direction of travel. */
static void lane_end_pose(const Lane *l, int which, V2 *pos, V2 *dir)
{
    const Piece *pc = &s_lp[l->first + (which ? l->np - 1 : 0)];
    l_piece_at(pc, which ? pc->len : 0.0f, pos, dir);
}

/*  The station on lane `li` at arc length `back` before its finish
 *  (which = 1) or after its start (which = 0). */
static void lane_station_back(int li, int which, float back, V2 *pos, V2 *dir)
{
    const Lane *l = &s_lane[li];
    float       s = 0.0f, want;
    int         k;
    /*  A lane of no pieces has no station to stand at, and the caller
     *  gets the map's own origin rather than whatever it was holding. */
    *pos = (V2){0.0f, 0.0f}, *dir = (V2){1.0f, 0.0f};
    want = which ? l->len - back : back;
    if (want < 0.0f)
        want = 0.0f;
    if (want > l->len)
        want = l->len;
    for (k = 0; k < l->np; ++k)
    {
        const Piece *pc = &s_lp[l->first + k];
        if (want <= s + pc->len || k == l->np - 1)
        {
            l_piece_at(pc, want - s, pos, dir);
            return;
        }
        s += pc->len;
    }
}

static int link_add(RMesh *m, const RCity *c, uint8_t mask_bit, V2 A, V2 tA, V2 B, V2 tB, float w, int from, int to, int band)
{
    static Piece tmp[MAX_PIECES];
    int          np;
    float        rmin;
    if (lane_route(A, tA, B, tB, tmp, &np, &rmin) != 0 || np < 1)
    {
        ++s_ls.link_fail;
        if (dump_misses())
            dumpf("LANE link failed: %.2f,%.2f heading %.2f,%.2f -> %.2f,%.2f heading %.2f,%.2f (lanes %d -> %d)\n", (double)A.x, (double)A.y, (double)tA.x, (double)tA.y, (double)B.x, (double)B.y, (double)tB.x, (double)tB.y, from, to);
        return 0;
    }
    ++s_ls.links;
    if (lane_add(LC_LINK, net_road->f, tmp, np, w, rmin, rmin < LANE_RMIN, LE_LANE, from, LE_LANE, to, 0, 0.0f) != 0)
        return -1;
    return lane_wires(m, c, mask_bit, tmp, np, net_family_rules(F_ROAD)->lane_wire, 5.0f, band);
}

/*  The lane a deck lane goes on as after the walk cut the deck: the
 *  lane of ANOTHER band at the same offset from its own centreline, on
 *  the same side, whose open end faces this one -- ahead of it, or level
 *  with it round a corner, within six tiles, running on or turning up to
 *  a right angle; the nearest first.  Returns its index or -1, with the
 *  pose of its facing end. */
static int band_continuation(int li, int which, V2 pd, V2 dd, V2 *pr, V2 *dr)
{
    const ScriptFamily *fr = net_family_rules(F_ROAD);
    const Lane         *l  = &s_lane[li];
    int                 lj, best = -1;
    float               ba = fr->band_reach;
    /*  Answered whatever comes of the search: an answer of -1 is no
     *  continuation, and the pose then stands at the end itself rather
     *  than at whatever the caller happened to be holding. */
    *pr = pd, *dr = dd;
    for (lj = 0; lj < s_nl; ++lj)
    {
        const Lane *r = &s_lane[lj];
        V2          p2, d2, v;
        float       ahead, aside, dist;
        /*  Every lane is the right-hand lane of its own travel; the sign of
         *  `off` only says which way the walk ran its band, so the match
         *  is by offset magnitude and heading alone. */
        if (r->cls != LC_DECK || r->band == l->band || fabsf(fabsf(r->off) - fabsf(l->off)) > fr->band_off)
            continue;
        if (which ? r->kind0 != LE_OPEN : r->kind1 != LE_OPEN)
            continue;
        lane_end_pose(r, which ? 0 : 1, &p2, &d2);
        if (d2.x * dd.x + d2.y * dd.y < fr->band_dot)
            continue;
        v     = which ? (V2){p2.x - pd.x, p2.y - pd.y} : (V2){pd.x - p2.x, pd.y - p2.y};
        ahead = v.x * dd.x + v.y * dd.y;
        aside = fabsf(v.x * dd.y - v.y * dd.x);
        dist  = sqrtf(v.x * v.x + v.y * v.y);
        if (ahead < fr->band_ahead || dist > ba || aside > fr->band_aside || dist < fr->band_apart)
            continue;
        /*  And the router must be able to build it.  The window above
         *  admits a band end that lies BESIDE this one -- two carriageways
         *  of an interchange abreast, both heading the same way, a couple
         *  of tiles apart (Junetown 81..83,26: ahead 0, aside 2) -- which
         *  is not a continuation at all, and the router refused it: thirty
         *  of that city's sixty-six links failed there and drew nothing.
         *  Asking the router first costs a biarc and keeps the matcher from
         *  proposing what cannot be drawn; the lane then tapers into the
         *  inner lane instead, as a lane with no continuation does. */
        {
            static Piece probe[MAX_PIECES];
            int          pn;
            float        prmin;
            if ((which ? lane_route(pd, dd, p2, d2, probe, &pn, &prmin) : lane_route(p2, d2, pd, dd, probe, &pn, &prmin)) != 0)
                continue;
        }
        ba = dist, best = lj, *pr = p2, *dr = d2;
    }
    return best;
}

/*  Where a highway band comes down to grade and becomes a road, its six
 *  lanes have to become the road's two.  At each band end, per side: the
 *  INNER deck lane routes into the road's lane on that side -- the road
 *  lane whose open end lies nearest the deck lane's, running the same way
 *  -- and the middle and outer lanes taper into the inner lane at stations
 *  a tile and a half and three tiles before the end (or after the start, on
 *  the side travelling onto the deck).  A band end that meets no road stays
 *  open and is counted. */
/*  Is lane i an open end that could carry on across a crossing?  A road
 *  lane whose far end is open, and not at the map's edge -- an end there
 *  faces nothing. */
int xlane_end(const XLaneFan *x, int i)
{
    const Lane *l = &s_lane[i];
    V2          pa, da;
    float       edge = net_family_rules(F_ROAD)->lane_edge;
    if (i < 0 || i >= x->n || l->cls != LC_ROAD || l->kind1 != LE_OPEN)
        return 0;
    lane_end_pose(l, 1, &pa, &da);
    return !(pa.x < edge || pa.y < edge || pa.x > (float)R_MAP - edge || pa.y > (float)R_MAP - edge);
}

/*  How lane lb's start lies from lane la's end: how far their offsets
 *  differ, how nearly they run the same way, and how far ahead, aside
 *  and away it is.  0 for a lane that is not a candidate at all. */
int xlane_measure(const XLaneFan *x, int la, int lb, float *off, float *dot, float *ahead, float *aside, float *dist)
{
    const Lane *l = &s_lane[la], *r = &s_lane[lb];
    V2          pa, da, pb, db, v;
    if (lb == la || lb < 0 || lb >= x->n || r->cls != LC_ROAD || r->fam != l->fam || r->kind0 != LE_OPEN)
        return 0;
    lane_end_pose(l, 1, &pa, &da);
    lane_end_pose(r, 0, &pb, &db);
    *off   = fabsf(fabsf(r->off) - fabsf(l->off));
    *dot   = db.x * da.x + db.y * da.y;
    v      = (V2){pb.x - pa.x, pb.y - pa.y};
    *ahead = v.x * da.x + v.y * da.y;
    *aside = fabsf(v.x * da.y - v.y * da.x);
    *dist  = sqrtf(v.x * v.x + v.y * v.y);
    return 1;
}

/*  End to start on the spot: a line the walk broke at a node in the
 *  middle of it.  The two are one lane; each end names the other, and no
 *  piece is drawn between them. */
void xlane_merge(XLaneFan *x, int la, int lb)
{
    ++s_ls.crossings;
    (void)x;
    s_lane[la].kind1  = LE_LANE;
    s_lane[la].port1  = lb;
    s_lane[lb].kind0  = LE_LANE;
    s_lane[lb].port0  = la;
}

/*  Otherwise a straight link drawn from the one end to the other. */
int xlane_link(XLaneFan *x, int la, int lb)
{
    V2 pa, da, pb, db;
    ++s_ls.crossings;
    lane_end_pose(&s_lane[la], 1, &pa, &da);
    lane_end_pose(&s_lane[lb], 0, &pb, &db);
    if (link_add((RMesh *)x->m, (const RCity *)x->c, x->mask_bit, pa, da, pb, db, s_lane[la].w, la, lb, 0) != 0)
    {
        x->fail = 1;
        return 0;
    }
    return 1;
}

/*  ACROSS A CROSSING: the open ends as they stand, for arc.rules.cross
 *  to carry each of them on into the lane facing it.  The drive composes
 *  it between the two halves of the transitions. */
static XLaneFan s_xlane;

XLaneFan *lane_cross_ask(RMesh *m, const RCity *c, uint8_t mask_bit)
{
    memset(&s_xlane, 0, sizeof s_xlane);
    if (s_pass == 1)
        return NULL; /* the grading pass: lanes are the building pass's */
    s_xlane.m        = m;
    s_xlane.c        = c;
    s_xlane.mask_bit = mask_bit;
    s_xlane.n        = s_nl;
    return &s_xlane;
}

int lane_transitions(RMesh *m, const RCity *c, uint8_t mask_bit)
{
    if (s_pass == 1)
        return 0; /* the grading pass: lanes are the building pass's */
    if (s_xlane.fail)
        return -1;
    int li, nl = s_nl; /* the lanes as they stand; links are appended */
    for (li = 0; li < nl; ++li)
    {
        const Lane *l = &s_lane[li];
        int         which;
        if (l->cls != LC_DECK)
            continue;
        for (which = 0; which < 2; ++which)
        {
            /*  which = 1: the lane's travel leaves the deck here; 0: it
             *  arrives.  Only the inner lane (the smallest |off| of its
             *  band and side) looks for the road; the others go to it. */
            V2    pd, dd, pr, dr;
            int   inner = 1, lj, best = -1, k;
            float bd = net_family_rules(F_ROAD)->lane_reach;
            if (which ? l->kind1 != LE_OPEN : l->kind0 != LE_OPEN)
                continue;
            lane_end_pose(l, which, &pd, &dd);
            for (lj = 0; lj < nl; ++lj)
                if (s_lane[lj].cls == LC_DECK && s_lane[lj].band == l->band && lj != li && (s_lane[lj].off > 0.0f) == (l->off > 0.0f) &&
                    fabsf(s_lane[lj].off) < fabsf(l->off))
                {
                    V2 p2, d2;
                    lane_end_pose(&s_lane[lj], which, &p2, &d2);
                    if (fabsf(p2.x - pd.x) + fabsf(p2.y - pd.y) < net_family_rules(F_ROAD)->band_abreast)
                        inner = 0; /* a lane nearer the centreline ends beside this one */
                }
            if (!inner)
            {
                /*  A middle or outer lane: on to the next band's lane of
                 *  its own offset when the deck goes on, else into the
                 *  inner lane's station up the band (which = 1) or down it
                 *  (which = 0), if the band is long enough to taper. */
                const ScriptFamily *fr = net_family_rules(F_ROAD);
                float back   = fabsf(l->off) > fr->band_outer * l->w ? fr->band_taper_far : fr->band_taper_near;
                int   in_li  = -1, nx;
                float in_off = 1e9f;
                V2    ps, ds, pn, dn;
                nx = band_continuation(li, which, pd, dd, &pn, &dn);
                if (nx >= 0)
                {
                    ++s_ls.band_links;
                    if (which ? link_add(m, c, mask_bit, pd, dd, pn, dn, l->w, li, nx, l->band)
                              : link_add(m, c, mask_bit, pn, dn, pd, dd, l->w, nx, li, l->band))
                        return -1;
                    continue;
                }
                if (l->len < back + fr->band_taper_room)
                    continue; /* too short to taper: a connector piece */
                for (lj = 0; lj < nl; ++lj)
                    if (s_lane[lj].cls == LC_DECK && s_lane[lj].band == l->band && (s_lane[lj].off > 0.0f) == (l->off > 0.0f) &&
                        fabsf(s_lane[lj].off) < in_off)
                        in_off = fabsf(s_lane[lj].off), in_li = lj;
                if (in_li < 0)
                    continue;
                lane_station_back(in_li, which, back, &ps, &ds);
                if (which)
                {
                    V2 pa, da;
                    lane_station_back(li, 1, back + fr->band_taper_gap, &pa, &da);
                    if (link_add(m, c, mask_bit, pa, da, ps, ds, l->w, li, in_li, l->band) != 0)
                        return -1;
                }
                else
                {
                    V2 pa, da;
                    lane_station_back(li, 0, back + fr->band_taper_gap, &pa, &da);
                    if (link_add(m, c, mask_bit, ps, ds, pa, da, l->w, in_li, li, l->band) != 0)
                        return -1;
                }
                continue;
            }
            ++s_ls.band_ends;
            /* the road lane whose open end faces this one */
            for (lj = 0; lj < nl; ++lj)
            {
                const Lane *r = &s_lane[lj];
                V2          p2, d2;
                float       d;
                if (r->cls != LC_ROAD || r->fam != net_road->f)
                    continue;
                if (which ? r->kind0 != LE_OPEN : r->kind1 != LE_OPEN)
                    continue;
                lane_end_pose(r, which ? 0 : 1, &p2, &d2);
                d = sqrtf((p2.x - pd.x) * (p2.x - pd.x) + (p2.y - pd.y) * (p2.y - pd.y));
                if (d < bd && d2.x * dd.x + d2.y * dd.y > net_family_rules(F_ROAD)->band_road_dot)
                    bd = d, best = lj, pr = p2, dr = d2;
            }
            if (best < 0)
            {
                /*  No road: the deck may go on as ANOTHER BAND -- the walk
                 *  splits a deck at an interchange or a corner, and the
                 *  lanes either side end open facing each other. */
                V2  pn, dn;
                int nx = band_continuation(li, which, pd, dd, &pn, &dn);
                if (nx >= 0)
                {
                    ++s_ls.band_links;
                    if (which ? link_add(m, c, mask_bit, pd, dd, pn, dn, l->w, li, nx, l->band)
                              : link_add(m, c, mask_bit, pn, dn, pd, dd, l->w, nx, li, l->band))
                        return -1;
                    continue;
                }
                ++s_ls.band_open;
                if (dump_misses())
                    dumpf("LANE band end open at %.2f,%.2f heading %.2f,%.2f (band %d, off %.2f)\n", (double)pd.x, (double)pd.y, (double)dd.x, (double)dd.y, l->band, (double)l->off);
                continue;
            }
            for (k = 0; k < 1; ++k)
            {
                if (which)
                {
                    if (link_add(m, c, mask_bit, pd, dd, pr, dr, l->w, li, best, 0) != 0)
                        return -1;
                }
                else
                {
                    if (link_add(m, c, mask_bit, pr, dr, pd, dd, l->w, best, li, 0) != 0)
                        return -1;
                }
            }
        }
    }
    return 0;
}

/*  Every lane end goes somewhere and every start has something arriving.  A
 *  port an inbound lane ends at must have a connector leaving it; a port an
 *  outbound lane starts at must have one arriving; a connector's ports must
 *  have those lanes; an open end must be the end of a link or a cap, unless
 *  it leaves the map. */
static uint8_t s_pend[R_MAP * R_MAP * 4 * 4], s_pstart[R_MAP * R_MAP * 4 * 4], s_cfrom[R_MAP * R_MAP * 4 * 4], s_cto[R_MAP * R_MAP * 4 * 4];
void           lane_check_ends(void)
{
    static uint8_t cov0[L_MAX], cov1[L_MAX]; /* an open start / end covered by a link or cap */
    int            li, nowhere = 0, nothing = 0, dump = dump_misses();
    memset(s_pend, 0, sizeof s_pend);
    memset(s_pstart, 0, sizeof s_pstart);
    memset(s_cfrom, 0, sizeof s_cfrom);
    memset(s_cto, 0, sizeof s_cto);
    memset(cov0, 0, sizeof cov0);
    memset(cov1, 0, sizeof cov1);
    for (li = 0; li < s_nl; ++li)
    {
        const Lane *l = &s_lane[li];
        if (l->cls == LC_ROAD || l->cls == LC_RAMP || l->cls == LC_DECK)
        {
            if (l->kind0 == LE_PORT && l->port0 >= 0)
                s_pstart[l->port0] = 1;
            if (l->kind1 == LE_PORT && l->port1 >= 0)
                s_pend[l->port1] = 1;
        }
        else /* a connector, a link or a cap */
        {
            if (l->kind0 == LE_PORT && l->port0 >= 0)
                s_cfrom[l->port0] = 1;
            if (l->kind1 == LE_PORT && l->port1 >= 0)
                s_cto[l->port1] = 1;
            if (l->kind0 == LE_LANE && l->port0 >= 0 && l->port0 < L_MAX)
                cov1[l->port0] = 1; /* it leaves that lane's end */
            if (l->kind1 == LE_LANE && l->port1 >= 0 && l->port1 < L_MAX)
                cov0[l->port1] = 1; /* it arrives at that lane's start */
        }
    }
    for (li = 0; li < s_nl; ++li)
    {
        const Lane *l = &s_lane[li];
        V2          p, d;
        int         edge;
        if (l->cls == LC_DECK && (l->kind0 == LE_OPEN || l->kind1 == LE_OPEN))
            continue; /* a band's open end is counted as such above (meeting no road) */
        if (l->cls == LC_ROAD || l->cls == LC_RAMP || l->cls == LC_DECK)
        {
            lane_end_pose(l, 1, &p, &d);
            edge = p.x < net_family_rules(F_ROAD)->lane_edge || p.y < net_family_rules(F_ROAD)->lane_edge || p.x > (float)R_MAP - net_family_rules(F_ROAD)->lane_edge || p.y > (float)R_MAP - net_family_rules(F_ROAD)->lane_edge;
            if ((l->kind1 == LE_PORT && !s_cfrom[l->port1]) || (l->kind1 == LE_OPEN && !cov1[li] && !edge))
            {
                ++nowhere;
                if (dump)
                    dumpf("LANE nowhere to go: lane %d class %d ends at %.2f,%.2f heading %.2f,%.2f\n", li, l->cls, (double)p.x, (double)p.y, (double)d.x, (double)d.y);
            }
            lane_end_pose(l, 0, &p, &d);
            edge = p.x < net_family_rules(F_ROAD)->lane_edge || p.y < net_family_rules(F_ROAD)->lane_edge || p.x > (float)R_MAP - net_family_rules(F_ROAD)->lane_edge || p.y > (float)R_MAP - net_family_rules(F_ROAD)->lane_edge;
            if ((l->kind0 == LE_PORT && !s_cto[l->port0]) || (l->kind0 == LE_OPEN && !cov0[li] && !edge))
            {
                ++nothing;
                if (dump)
                    dumpf("LANE nothing arrives: lane %d class %d starts at %.2f,%.2f heading %.2f,%.2f\n", li, l->cls, (double)p.x, (double)p.y, (double)d.x, (double)d.y);
            }
        }
        else if (l->cls == LC_TURN)
        {
            if (l->kind0 == LE_PORT && !s_pend[l->port0])
            {
                ++nothing;
                if (dump)
                    dumpf("LANE connector from no lane: %d at port %d\n", li, l->port0);
            }
            if (l->kind1 == LE_PORT && !s_pstart[l->port1])
            {
                ++nowhere;
                if (dump)
                    dumpf("LANE connector to no lane: %d at port %d\n", li, l->port1);
            }
        }
    }
    s_ls.nowhere = nowhere;
    s_ls.nothing = nothing;
}

void lane_stats_print(void)
{
    {
        int   nsteps;
        float worst;
        shelf_steps(&nsteps, &worst);
        dumpf("corridor shelves  %d shared corners disagree along an edge or at a node, worst %.2f of a level\n", nsteps, (double)worst);
    }
    dumpf("lanes  %d junctions, %d ports, %d connectors (%d tight, %d without a route); %d segments (%d avenues or boulevards), %d lane "
           "ends off their port; %d decks, %d ramps, %d ramp ends on no lane; %d band ends (%d meeting no road, %d on to another band), %d links (%d failed), %d through a crossing; %d dead-end caps (%d failed); %d ends with nowhere to go, %d starts with nothing arriving\n",
           s_ls.junctions,
           s_ls.ports,
           s_ls.conns,
           s_ls.tight,
           s_ls.failed,
           s_ls.segs,
           s_ls.wide,
           s_ls.mismatch,
           s_ls.decks,
           s_ls.ramps,
           s_ls.ramp_miss,
           s_ls.band_ends,
           s_ls.band_open,
           s_ls.band_links,
           s_ls.links,
           s_ls.link_fail,
           s_ls.crossings,
           s_ls.caps,
           s_ls.cap_fail,
           s_ls.nowhere,
           s_ls.nothing);
    if (g_dev.times)
        dumpf("time    lane index: %d filings of %d, %d lanes of %d, %d pieces of %d\n", s_nlx, L_FILED, s_nl, L_MAX, s_nlp, L_PIECES);
}

/*  The exported face of port_pose, for the families' own drawings. */
int lane_port(Family f, int col, int row, int e, int out, int k, V2 *pos, V2 *dir)
{
    port_pose(f, col, row, e, out, k, pos, dir);
    return 0;
}

/*  The lane table, for the inspector: a ramp, a junction's connector and a
 *  deck's lane are lanes and nothing else -- they are in no segment table,
 *  so this is the only way to point at one. */
int lane_table_count(void)
{
    return s_nl;
}

int lane_table_get(int i, int *cls, int *fam, const Piece **pc, int *np, float *w)
{
    const Lane *l;
    if (i < 0 || i >= s_nl)
        return -1;
    l    = &s_lane[i];
    *cls = l->cls;
    *fam = l->fam;
    *pc  = &s_lp[l->first];
    *np  = l->np;
    *w   = l->w;
    return 0;
}

