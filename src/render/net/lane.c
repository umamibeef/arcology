/*  lane.c: THE LANE MODEL, and the store it is kept in.
 *
 *  A lane is a directed run of pieces with a width, a class, and two
 *  ends that each name what they join.  This holds them.  The table,
 *  their pieces, and the index that finds a lane near a point.  It
 *  offers them to the rules that decide what joins what.
 *  arc.rules.turns settles a junction's pattern, arc.rules.cap settles a
 *  dead end, arc.rules.links for a slab's lanes, arc.rules.cross for a
 *  meet, arc.rules.spur_lane for a spur's ends.
 *
 *  Nothing here decides which lane meets which.  The arithmetic a lane
 *  is made of is mesh/piece.c's, the choices are the scripts'.  What is
 *  left in this file is the keeping and the offering.  Which is why it
 *  sits with the segment table and the discovered network rather than
 *  with the geometry.
 *
 *  A LANE END NAMES ONE OTHER, and one only.  Every pass that joins two
 *  ends goes through lane_ends_take.  So no two of them can claim the
 *  same end and leave a lane hanging from an end nothing arrives at. */
#include "dump.h"
#include "mesh/internal.h"
#include "log.h"
#include "pipeline.h"
#include "net/net.h"
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
 *  it tight.  It is a quarter tile, or about four meters at the tile's
 *  scale.  A line's right turn sweeps junc_lip plus half a lane, which
 *  is 0.325 at the default width.  This holds once the junction has
 *  pushed its mouths out for the lip returns.  Before that it pivoted on
 *  a point (0.125). */
#define LANE_RMIN (net_line_rules()->lane_rmin)

enum
{
    LC_LINE = 0,
    LC_TURN = 1,
    LC_SLAB = 2,
    LC_SPUR = 3,
    LC_LINK = 4 /* a band's lane into a line's, or into its own inner lane, at a band's end */
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
    float   w, rmin, len; /* the band's half width.  The tightest arc.  The length */
    int     band;         /* a slab lane's band, else 0 */
    float   off;          /* its offset from the band's centerline, viewer's right positive */
} Lane;

/*  A LANE END NAMES ONE OTHER, and one only: the two ends a join is
 *  about, taken together or not at all.  Every pass that joins two lane
 *  ends goes through this, so no two of them can claim the same end. */

static Lane  s_lane[L_MAX];
static Piece s_lp[L_PIECES];
static int   s_nl, s_nlp;
/*  The lanes by tile.  Every lane is filed under each tile its pieces'
 *  extents cover, as it is added.  A search then visits only the lanes
 *  filed within reach of its point.  Without the index a search reads
 *  every lane in the city.  A spur makes three of them, which is
 *  milliseconds a building pass rather than nothing.  A filing is a lane
 *  at a tile.  A tile's filings are a list. */
#define L_FILED (4 * L_MAX)
static int s_lx_head[R_MAP * R_MAP]; /* a tile's first filing, or -1 */
static int s_lx_lane[L_FILED], s_lx_next[L_FILED], s_nlx;
static int s_lx_seen[L_MAX], s_lx_stamp; /* a search's stamp on the lanes it has gathered */
static int s_lx_cand[L_MAX];             /* ... and the lanes it gathered, in lane order */
static struct
{
    int junctions, ports, conns, tight, failed, segs, wide, mismatch, slabs, spurs, spur_miss, band_ends, band_open, band_links, links, link_fail, caps, cap_fail, nowhere, nothing, meets;
    int slab_nowhere, slab_nothing;          /* a SLAB lane's ends, counted apart */
    int broken, kinked, rtight, stubby;      /* and what its curves do */
    int doubled;                             /* an end named by more than one link */
    int refused;                             /* ... and a link refused for asking */
} s_ls;

static void net_wires_reset(void);

void lane_reset(void)
{
    net_wires_reset();
    s_nl = s_nlp = 0;
    memset(&s_ls, 0, sizeof s_ls);
    s_nlx = s_lx_stamp = 0;
    memset(s_lx_head, -1, sizeof s_lx_head);
    memset(s_lx_seen, 0, sizeof s_lx_seen);
}

/*  The lane centers by class, from the centerline, inner first.  Where
 *  they run is the SCRIPT'S (arc.rules.lanes).  It is settled for every
 *  family and class the pipeline can present, before any of it is built:
 *  no answer is no lanes.  This is what a run that cannot find the
 *  scripts draws.  Every part of the pipeline that wants a lane comes
 *  through here, the connectors, the paint and the traffic, so the three
 *  cannot part company.
 *
 *  A class runs from LANE_CLS_LO, which is the family that has none, up
 *  to the widest line there is. */
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
    return (f == net_thread->f ? 1 : 0) * (LANE_CLS_HI - LANE_CLS_LO + 1) + (cls - LANE_CLS_LO);
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
    *fam = i >= LANE_CLS_HI - LANE_CLS_LO + 1 ? "thread" : "line";
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

static int net_lane_offsets(Family f, int cls, float *off, int max)
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

static int port_id(int col, int row, int e, int out, int k)
{
    return (((row * R_MAP + col) * 4 + e) * 2 + out) * 2 + k; /* k: the lane, inner first */
}

int lane_port_id(int col, int row, int e, int out, int k)
{
    return port_id(col, row, e, out, k);
}

static int32_t tile_clamp(float v)
{
    int32_t t = (int32_t)floorf(v);
    return t < 0 ? 0 : t > R_MAP - 1 ? R_MAP - 1
                                     : t;
}

/*  A lane into the index.  It goes under every tile each of its pieces
 *  covers, once per tile.  A tile's last filing is this lane's when the
 *  piece before covered it too).  Full is an error: a lane the index
 *  cannot find would be a lane the city does not have. */
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

/*  The lanes filed within `maxd` of a point, each once, in lane order.
 *  The tiles within ceil(maxd) of the point's own hold every lane a
 *  point that near could lie on.  A point past the map's edge reads the
 *  edge tile, where everything past it was filed. */
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

static int lane_ends_take(int from, int to, int by);

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

/*  The lane drawn as a colored wire a hair above the ground, under "show
 *  curves".  It reads like the corridor highlights and the fit's nodes.
 *  The lane model on screen before it shapes anything.  Red for a
 *  segment's lanes, blue for a junction's connectors. */
/*  The slab's height near a point, from the band's recorded stations.
 *  The ground where the band has none. */
float slab_z_near(const RCity *c, uint8_t mask_bit, int band, V2 p)
{
    float best = 1e9f, z = 0.0f;
    int   k, have        = 0;
    for (k = 0; k < s_band_nst; ++k)
    {
        float dx, dy, d;
        if (s_band_st[k].band != band)
            continue;
        dx = s_band_st[k].pos.x - p.x;
        dy = s_band_st[k].pos.y - p.y;
        d  = dx * dx + dy * dy;
        if (d < best)
            best = d, z = s_band_st[k].z, have = 1;
    }
    return have ? z : surface_at_world(c, mask_bit, p.x, p.y);
}

/*  ------------------------------------------------------------------
 *  The lane overlay's wires, gathered
 *
 *  The hairline over a lane is drawn only while the tuning window asks
 *  to see the curves, and it is drawn from four different passes.  Each
 *  of them gathers what it wants drawn, with the shape it belongs to.
 *  The drive lays them all at the end: entering each shape again.  So
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

/*  The wires a build gathers, emptied with the lanes they belong to. */
static void net_wires_reset(void)
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
    f.spur = 0, f.off = 0;
    f.step = 0.0f;
    return wire_add(&f);
}

/* The trim an arm's segment will actually be cut at.  walk_segment
 * scales both of a short segment's trims back together.  So at least
 * half of it survives.  The port has to land where the lane ends .  The
 * thread box draws to the same cut. */
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
    V2    d = a->have ? (V2){a->dx, a->dy} : (V2){SIDE_DU[e], SIDE_DV[e]};
    V2    o = a->have ? (V2){a->ax, a->ay} : (V2){cx + SIDE_DU[e] * w, cy + SIDE_DV[e] * w};
    float t = arm_cut(f, col, row, e);
    V2    M, r;
    M = (V2){o.x + d.x * t, o.y + d.y * t};
    /*  A turnout's arm is cut along its own path, and the port is on the
     *  path, heading its way.  An arm that BENDS within the reach is met
     *  by the turnout instead.  A staircase of thread hooking away a
     *  tile from the junction bends.  The turnout's routed threads then
     *  meet it where its thread really is.  They have the whole reach to
     *  curve in, rather than on a ray it never runs along. */
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
 *  lanes of the segments that reach it.  A bare --lane-dump prints only
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

/*  A spur tile, when one lies at col,row. */
static const BandSpur *spur_tile(int32_t col, int32_t row)
{
    int r;
    for (r = 0; r < s_band_nspurs; ++r)
        if (s_band_spurs[r].rc == col && s_band_spurs[r].rr == row)
            return &s_band_spurs[r];
    return NULL;
}

/*  The same, for the box builder in junction.c. */
const BandSpur *lane_spur_tile(int32_t col, int32_t row)
{
    return spur_tile(col, row);
}


/*  The connectors of a junction: from each arm's inbound lane to each
 *  other arm's outbound lane, routed. */
/*  A junction's lanes as they are built: the arms' ports, in and out,
 *  each lane's pose at them.  Which arm is a spur with its one port. */
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
    int          nl[4], has_in[4], has_out[4], is_spur[4];
} LaneJunc;

static const char EN[4] = {'N', 'E', 'S', 'W'};

/*  The arms' ports: each linked arm's lanes by its class, and the pose
 *  of every lane's inbound and outbound port on it.  A spur tile beside
 *  the junction gets no line lanes here: a real junction gives the spur
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
        if (col + (int32_t)lroundf(SIDE_DU[e]) < 0 || row + (int32_t)lroundf(SIDE_DV[e]) < 0 || col + (int32_t)lroundf(SIDE_DU[e]) >= R_MAP ||
            row + (int32_t)lroundf(SIDE_DV[e]) >= R_MAP)
            continue; /* an arm off the map's edge: the world beyond has no lanes */
        if (net_family(f)->spurs && spur_tile(col + (int32_t)lroundf(SIDE_DU[e]), row + (int32_t)lroundf(SIDE_DV[e])))
            continue; /* a spur tile: no line lanes there.  A real junction gives the spur its one port below */
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

/*  A SPUR beside the junction is an arm of it with ONE port: an ON
 *  spur's, outbound, that the connectors feed.  An OFF spur's, inbound,
 *  that they drain.  Its pose is the tile edge's middle, where the
 *  spur's own lane ends. */
static void lj_spur_ports(LaneJunc *x)
{
    int32_t      col   = x->col;
    int32_t      row   = x->row;
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
    int *is_spur = x->is_spur;
    int  e;
    for (e = 0; e < 4; ++e)
    {
        int32_t nc = col + (int32_t)lroundf(SIDE_DU[e]), nr = row + (int32_t)lroundf(SIDE_DV[e]);
        int     r, mode = 0;
        if (nc < 0 || nr < 0 || nc >= R_MAP || nr >= R_MAP)
            continue; /* linked or not: a spur tile counts as a line link in the data, and is one-way all the same */
        for (r = 0; r < s_band_nspurs; ++r)
        {
            const BandSpur *rp = &s_band_spurs[r];
            V2            rd;
            if (rp->rc != nc || rp->rr != nr)
                continue;
            /*  How it meets this meet is the SPUR'S own: what
             *  arc.rules.orient made of the lines round its line tile.
             *  Nought is a spur whose join meets the near lane beside
             *  the box rather than going through it. */
            mode = rp->arm;
            if (!mode)
                break;
            rd = rp->opp ? (V2){-rp->toward.x, -rp->toward.y} : rp->off ? rp->along
                                                                        : (V2){-rp->along.x, -rp->along.y};
            if (nc + (int32_t)lroundf(rd.x) != col || nr + (int32_t)lroundf(rd.y) != row)
                continue; /* its line tile is not this one */
            nl[e]      = 1;
            is_spur[e] = mode;
            has_in[e]  = rp->off ? 1 : 0;
            has_out[e] = rp->off ? 0 : 1;
            pin[e][0] = pout[e][0] = (V2){(float)col + 0.5f + SIDE_DU[e] * 0.5f, (float)row + 0.5f + SIDE_DV[e] * 0.5f};
            dout[e][0]             = (V2){SIDE_DU[e], SIDE_DV[e]};
            din[e][0]              = (V2){-SIDE_DU[e], -SIDE_DV[e]};
            s_ls.ports += 1;
            if (dump)
                dumpf("LANE port %d,%d %c spur %s at %.3f,%.3f\n", (int)col, (int)row, EN[e], rp->off ? "in" : "out", (double)pin[e][0].x, (double)pin[e][0].y);
            break;
        }
    }
}

/*  THE PATTERN AN INTERSECTION DRAWS, which arm's inbound lane joins
 *  which arm's outbound lane, is arc.rules.turns's
 *  (scripts/compose/turns.lua).  There is no matcher in C behind it:
 *  take the rule away and every arm of every junction meets nothing.
 *
 *  The pairs the rule asked for, queued for the drive to cut. */
static struct
{
    int e, e2, k, k2, cut;
} s_conn[4 * 4 * 2 * 2];
static int s_n_conn;

static int lj_connectors_take(LaneJunc *x)
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
    int          i;
    for (i = 0; i < s_n_conn; ++i)
    {
        int   e = s_conn[i].e, e2 = s_conn[i].e2, k = s_conn[i].k, k2 = s_conn[i].k2;
        int   np, tight, q;
        float rmin = 1e9f;
        if (s_conn[i].cut < 0 || net_cut_pieces(s_conn[i].cut, tmp, MAX_PIECES, &np) != 0 || np < 1)
        {
            ++s_ls.failed;
            if (dump)
                dumpf("LANE conn %d,%d %c%d in -> %c%d out: no route\n", (int)col, (int)row, EN[e], k, EN[e2], k2);
            continue;
        }
        for (q = 0; q < np; ++q)
            if (tmp[q].arc && tmp[q].r < rmin)
                rmin = tmp[q].r;
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
        if (lane_wires(m, c, mask_bit, tmp, np, net_line_rules()->lane_wire, 6.0f, 0) != 0) /* a connector in blue */
            return -1;
    }
    return 0;
}

/*  The connectors' chains QUEUED for the drive to cut.
 *  lane_junction_take takes them up once it has.  They are the box's two
 *  halves, with the cut the script's own between them. */
static LaneJunc s_lj;
static int      s_lj_have; /* a junction is in hand and its arms are measured */

void lane_junction_ask(RMesh *m, const RCity *c, uint8_t mask_bit, Family f, int32_t col, int32_t row, int links)
{
    static Piece tmp[MAX_PIECES];
    s_n_conn = 0;
    s_lj_have = 0;
    if (grade_only(g_dev.grade_lanes))
        return; /* the grading pass: lanes are the building pass's */
    memset(&s_lj, 0, sizeof s_lj);
    s_lj.m = m, s_lj.c = c, s_lj.mask_bit = mask_bit, s_lj.f = f, s_lj.col = col, s_lj.row = row, s_lj.links = links, s_lj.tmp = tmp;
    s_lj.w    = *net_family(f)->width * 0.5f;
    s_lj.dump = dump_here(col, row);
    /* the stages: the arms' ports and a spur's one port.  Which of them
     * joins which is arc.rules.turns's, asked between this and the take. */
    lj_ports(&s_lj);
    if (net_family(f)->spurs)
        lj_spur_ports(&s_lj);
    ++s_ls.junctions;
    s_lj_have = 1;
}

int lane_junction_take(void)
{
    return s_n_conn > 0 ? lj_connectors_take(&s_lj) : 0;
}

/*  ---- THE ARMS OF THE JUNCTION IN HAND ----------------------------------
 *
 *  What arc.rules.turns reads and lays its pattern with.  This offers
 *  the arms as the ports stage measured them and routes the pair the
 *  rule asks for.  It pairs nothing itself.  A junction whose rule asks
 *  for no pair draws no connector, which is a pattern too. */
int lane_turns_ask(void)
{
    return s_lj_have;
}

void lane_turns_info(int *col, int *row, int *arms, const char **fam)
{
    *col  = (int)s_lj.col;
    *row  = (int)s_lj.row;
    *arms = 4;
    *fam  = net_family(s_lj.f)->name;
}

/*  One arm: how many lanes it carries, whether a lane may enter the
 *  junction along it and whether one may leave.  Whether it is a spur.
 *  1 is an arm every other arm's outermost lane may use.  2 is one only
 *  the lane arriving straight at it uses. 0 for an arm that is not there
 *  at all. */
int lane_turns_arm(int e, int *lanes, int *into, int *out, int *spur)
{
    if (!s_lj_have || e < 0 || e > 3 || s_lj.nl[e] < 1)
        return 0;
    *lanes = s_lj.nl[e];
    *into  = s_lj.has_in[e];
    *out   = s_lj.has_out[e];
    *spur  = s_lj.is_spur[e];
    return 1;
}

/*  A connector wanted, from arm e's inbound lane k to arm e2's outbound
 *  lane k2.  It is the chain between the two port poses, QUEUED for the
 *  drive to cut like every other path.  Answers 0 for a pair the
 *  junction does not have, or when the queue is full. */
int lane_turns_want(int e, int k, int e2, int k2)
{
    if (!s_lj_have || e < 0 || e > 3 || e2 < 0 || e2 > 3)
        return 0;
    if (k < 0 || k >= s_lj.nl[e] || k2 < 0 || k2 >= s_lj.nl[e2])
        return 0;
    if (s_n_conn >= (int)(sizeof s_conn / sizeof s_conn[0]))
        return 0;
    s_conn[s_n_conn].e     = e;
    s_conn[s_n_conn].e2    = e2;
    s_conn[s_n_conn].k     = k;
    s_conn[s_n_conn].k2    = k2;
    s_conn[s_n_conn++].cut = net_cut_add_poses(s_lj.pin[e][k], s_lj.din[e][k], s_lj.pout[e2][k2], s_lj.dout[e2][k2]);
    return 1;
}

/*  A segment's two lanes from its fitted, trimmed pieces.
 *
 *      One with the path on the viewer's right of it.
 *      One against the path on the other side.
 *      Reversed so every lane runs the way its traffic does.
 *
 *  Each end is the port the segment's node gives it, and its pose is
 *  checked against the port's own. */
/*  A segment's lanes as they are built: the segment as the walk gave it,
 *  the lane offsets its class allows.  Each lane's end poses and table
 *  index for the dead ends' caps. */
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
        if (dist > net_line_rules()->lane_join || dot < net_line_rules()->lane_aim)
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

/*  One lane of the segment: the pieces offset to its side (reversed for
 *  the far side.  So every lane runs its own way.  Its ends go in the
 *  table, a port at a junction and open elsewhere.  Its end poses are
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
    if (lane_add(LC_LINE, f, tmp, np, offs[nlane - 1], rmin, rmin < LANE_RMIN, k0, p0, k1, p1, 0, side ? -h : h) != 0)
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
    if (lane_wires(m, c, mask_bit, tmp, np, net_line_rules()->lane_wire, net_family(f)->lane_paint, 0) != 0)
        return -1;
    return 0;
}

/*  ---- A SEGMENT'S DEAD ENDS ---------------------------------------------
 *
 *  What a lane does where its segment simply stops is arc.rules.cap's.
 *  The family declares WHICH kind of ending it has.  A line caps round,
 *  a family whose lane runs both ways reverses.  And the rule decides
 *  the SHAPE: whether the lane arriving at the end is drawn round to the
 *  one leaving it.  The two simply name each other with no piece between
 *  them, or neither and the lane stops there.
 *
 *  A pair is offered for each lane of each dead end: the lane that
 *  arrives and the lane that leaves.  No rule is a segment whose dead
 *  ends go nowhere. */
/*  The chains the rule asked to be drawn round a cap, QUEUED for the
 *  drive to cut, and laid in lane_segment_caps once it has. */
static struct
{
    int sa, sb, li, cut;
} s_cap[2 * 4];
static int     s_n_cap;
static LaneSeg s_lseg;

typedef struct
{
    int end, li, sa, sb; /* the end, the lane, and the two lanes' indices */
} LCap;
static LCap s_lcap[2 * 4];
static int  s_n_lcap;

/*  The dead ends of the segment in hand, recorded as its lanes are
 *  built. */
static void ls_caps_note(LaneSeg *x)
{
    int end, li;
    for (end = 0; end < 2; ++end)
    {
        int kind = end ? x->kind1 : x->kind0; /* 1: an end on open land */
        if (kind != 1)
            continue;
        for (li = 0; li < x->nlane; ++li)
        {
            /* the lane arriving at this end: side 0 arrives at node 1, side 1 at node 0 */
            int sa = end ? 0 : 1, sb = end ? 1 : 0;
            if (s_n_lcap >= (int)(sizeof s_lcap / sizeof s_lcap[0]))
                return;
            s_lcap[s_n_lcap].end = end;
            s_lcap[s_n_lcap].li  = li;
            s_lcap[s_n_lcap].sa  = sa;
            s_lcap[s_n_lcap].sb  = sb;
            ++s_n_lcap;
        }
    }
    s_lseg = *x;
}

int lane_caps_ask(void)
{
    return s_n_lcap;
}

/*  What kind of ending the family declares, and how many pairs there
 *  are to answer for. */
void lane_caps_info(int *n, const char **ends, const char **fam)
{
    static const char *ENDS[] = {"open", "cap", "reverse"};
    int                k      = s_n_lcap ? net_family(s_lseg.f)->lane_ends : 0;
    *n    = s_n_lcap;
    *ends = k >= 0 && k <= 2 ? ENDS[k] : "open";
    *fam  = s_n_lcap ? net_family(s_lseg.f)->name : "";
}

/*  One pair: which end of the segment it is, which lane of it, and the
 *  two lanes: the one arriving and the one leaving. */
int lane_caps_at(int i, int *end, int *lane, int *from, int *to)
{
    if (i < 0 || i >= s_n_lcap)
        return 0;
    *end  = s_lcap[i].end;
    *lane = s_lcap[i].li;
    *from = s_lseg.lid[s_lcap[i].sa][s_lcap[i].li];
    *to   = s_lseg.lid[s_lcap[i].sb][s_lcap[i].li];
    return 1;
}

/*  The two named each other, with no piece between them: the train
 *  reverses, and a car reaching the end turns on the spot. */
int lane_caps_merge(int i)
{
    int end, lane, from, to;
    if (!lane_caps_at(i, &end, &lane, &from, &to))
        return 0;
    if (lane_ends_take(from, to, to) != 0)
        return 0;
    ++s_ls.caps;
    return 1;
}

/*  Or a lane drawn round the cap from the one to the other.  The chain
 *  is QUEUED for the drive to cut, like every other path. */
int lane_caps_link(int i)
{
    int end, lane, from, to;
    int sa, sb, li;
    if (!lane_caps_at(i, &end, &lane, &from, &to))
        return 0;
    if (s_n_cap >= (int)(sizeof s_cap / sizeof s_cap[0]))
        return 0;
    sa = s_lcap[i].sa, sb = s_lcap[i].sb, li = s_lcap[i].li;
    s_cap[s_n_cap].sa    = sa;
    s_cap[s_n_cap].sb    = sb;
    s_cap[s_n_cap].li    = li;
    s_cap[s_n_cap++].cut = net_cut_add_poses(s_lseg.ep[sa][li][1], s_lseg.ed[sa][li][1], s_lseg.ep[sb][li][0], s_lseg.ed[sb][li][0]);
    return 1;
}

static int ls_caps_line_take(void)
{
    LaneSeg     *x        = &s_lseg;
    RMesh       *m        = x->m;
    const RCity *c        = x->c;
    uint8_t      mask_bit = x->mask_bit;
    Family       f        = x->f;
    int          nlane    = x->nlane;
    const float *offs     = x->offs;
    Piece       *tmp      = x->tmp;
    int (*lid)[2]         = x->lid;
    int i;
    for (i = 0; i < s_n_cap; ++i)
    {
        int   sa = s_cap[i].sa, sb = s_cap[i].sb, li = s_cap[i].li, np2, q;
        float rmin = 1e9f;
        if (s_cap[i].cut < 0 || net_cut_pieces(s_cap[i].cut, tmp, MAX_PIECES, &np2) != 0 || np2 < 1)
        {
            ++s_ls.cap_fail;
            continue;
        }
        for (q = 0; q < np2; ++q)
            if (tmp[q].arc && tmp[q].r < rmin)
                rmin = tmp[q].r;
        if (lane_ends_take(lid[sa][li], lid[sb][li], s_nl) != 0)
            continue;
        ++s_ls.caps;
        if (lane_add(LC_TURN, f, tmp, np2, offs[nlane - 1], rmin, 0, LE_LANE, lid[sa][li], LE_LANE, lid[sb][li], 0, 0.0f) != 0)
            return -1;
        if (lane_wires(m, c, mask_bit, tmp, np2, net_line_rules()->lane_wire, 6.0f, 0) != 0)
            return -1;
    }
    return 0;
}

/*  And the caps taken from the pieces the drive cut.  Nothing is waiting
 *  where the thing drawn was not a segment, or had no dead end. */
int lane_segment_caps(void)
{
    int rc = s_n_cap > 0 ? ls_caps_line_take() : 0;
    s_n_cap = 0;
    return rc;
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
    s_n_cap = s_n_lcap = 0;
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
    ls_caps_note(&x);
    return 0;
}

/*  A slab's lanes from its fitted pieces, each way at the centers the
 *  script names, as a fraction of the half width.  HOW MANY there are is
 *  the script's too.  The offsets are a run, and its length is the lane
 *  count.  So a way of another width is a script edit and not a compile.
 *  No offsets is a slab carrying no lanes.  Recorded under the band the
 *  loft records its stations under, so a spur can find the outer lane it
 *  leaves. */
int lane_slab(RMesh *m, const RCity *c, uint8_t mask_bit, const Piece *pc, int np, float hw, int band)
{
    if (grade_only(g_dev.grade_lanes))
        return 0; /* the grading pass: lanes are the building pass's */
    const float *OFF = net_line_rules()->slab_lane;
    const int    n   = net_line_rules()->slab_lanes;
    static Piece       tmp[MAX_PIECES];
    int                side, k, q;
    if (np < 1 || np > MAX_PIECES)
        return 0;
    ++s_ls.slabs;
    for (side = 0; side < 2; ++side)
        for (k = 0; k < n; ++k)
        {
            float off = OFF[k] * hw * (side ? -1.0f : 1.0f), rmin = 1e9f;
            for (q = 0; q < np; ++q)
                piece_offset(&pc[q], off, &tmp[q]);
            if (side)
                pieces_reverse(tmp, np);
            for (q = 0; q < np; ++q)
                if (tmp[q].arc && tmp[q].r < rmin)
                    rmin = tmp[q].r;
            if (lane_add(LC_SLAB, net_band->f, tmp, np, hw, rmin, rmin < LANE_RMIN, LE_OPEN, -1, LE_OPEN, -1, band, off) != 0)
                return -1;
            if (lane_wires(m, c, mask_bit, tmp, np, net_line_rules()->lane_wire, net_band->lane_paint, band) != 0)
                return -1;
        }
    return 0;
}

/*  ---- THE LANES WITHIN REACH OF A POINT ---------------------------------
 *
 *  Which lane a spur's end fastens to.  It is the nearest of its own
 *  band's slab lanes.  Or it is the lip-side lane of the line it comes
 *  down to.  Or it is the turn it lands on inside a junction.  The
 *  choice is the SCRIPT'S.  This measures and offers.  It picks nothing.
 *
 *  A candidate is one lane's nearest STATION to the point asked about:
 *  where that station is.  This way the lane runs there, how far off it
 *  is and how nearly it runs the way the spur does.  Finding the nearest
 *  point on an arc is geometry and stays here.  The policy on top of it:
 *  nearest, or lip-side, or of this band and no other: does not. */
#define SNAP_MAX 4096

static struct
{
    struct
    {
        int   lane, cls, band;
        float off, dist, dot;
        V2    pos, dir;
    } c[SNAP_MAX];
    int n, pick;
} s_snap;

/*  Every lane within `maxd` of the point, measured.  Answers how many,
 *  in the order the lane model holds them. */
int lane_snap_ask(V2 p, V2 dir, float maxd)
{
    int li, ci, n = lane_candidates(p, maxd);
    s_snap.n = 0;
    s_snap.pick = -1;
    /*  One candidate per PIECE, not per lane.  A lane's nearest station
     *  may run the wrong way, where a station further along it runs the
     *  right way, and the two are different answers. */
    for (ci = 0; ci < n; ++ci)
    {
        const Lane *l;
        int         k;
        li = s_lx_cand[ci];
        l  = &s_lane[li];
        for (k = 0; k < l->np && s_snap.n < SNAP_MAX; ++k)
        {
            V2    q, dq;
            float d;
            piece_near(&s_lp[l->first + k], p, &q, &dq);
            d = sqrtf((p.x - q.x) * (p.x - q.x) + (p.y - q.y) * (p.y - q.y));
            s_snap.c[s_snap.n].lane = li;
            s_snap.c[s_snap.n].cls  = l->cls;
            s_snap.c[s_snap.n].band = l->band;
            s_snap.c[s_snap.n].off  = l->off;
            s_snap.c[s_snap.n].dist = d;
            s_snap.c[s_snap.n].dot  = dq.x * dir.x + dq.y * dir.y;
            s_snap.c[s_snap.n].pos  = q;
            s_snap.c[s_snap.n].dir  = dq;
            ++s_snap.n;
        }
    }
    return s_snap.n;
}

int lane_snap_count(void)
{
    return s_snap.n;
}

int lane_snap_at(int i, int *lane, int *cls, int *band, float *off, float *dist, float *dot,
                 float *x, float *y, float *dx, float *dy)
{
    if (i < 0 || i >= s_snap.n)
        return 0;
    *lane = s_snap.c[i].lane, *cls = s_snap.c[i].cls, *band = s_snap.c[i].band;
    *off = s_snap.c[i].off, *dist = s_snap.c[i].dist, *dot = s_snap.c[i].dot;
    *x = s_snap.c[i].pos.x, *y = s_snap.c[i].pos.y;
    *dx = s_snap.c[i].dir.x, *dy = s_snap.c[i].dir.y;
    return 1;
}

void lane_snap_is(int i)
{
    s_snap.pick = i >= 0 && i < s_snap.n ? i : -1;
}

/*  The lane the script picked, and the station it picked it at.  -1 for
 *  none, which is an end that fastens to nothing. */
int lane_snap_take(V2 *pos, V2 *odir, float *dist)
{
    if (s_snap.pick < 0)
    {
        *dist = 1e9f;
        return -1;
    }
    *pos  = s_snap.c[s_snap.pick].pos;
    *odir = s_snap.c[s_snap.pick].dir;
    *dist = s_snap.c[s_snap.pick].dist;
    return s_snap.c[s_snap.pick].lane;
}

/*  A spur, once built, as a lane of its own: its ends name the slab lane
 *  it leaves and the line lane it joins.  An end that found none is
 *  counted. */
/*  A spur's wire: its height eases from the slab at the gore to the
 *  ground at the line, as the loft's does (ease_smooth).  So the overlay
 *  shows the lane meeting the slab's lane where it does. */
static int spur_wires(RMesh *m, const RCity *c, uint8_t mask_bit, const Piece *pc, int np, int off, float paint)
{
    LaneFan f;
    if (!(s_tune.show_curves > 0.5f) || s_pass == 1)
        return 0;
    f.m = m, f.c = c, f.mask_bit = mask_bit;
    f.pc = pc, f.np = np;
    f.lift = net_line_rules()->lane_lift, f.paint = paint, f.band = 0;
    f.spur = 1, f.off = off, f.step = net_line_rules()->lane_step_spur;
    return wire_add(&f);
}

int lane_spur(RMesh *m, const RCity *c, uint8_t mask_bit, const Piece *pc, int np, float hw, int slab_lane, int line_lane, int line_port, int off)
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
    ++s_ls.spurs;
    s_ls.spur_miss += (slab_lane < 0) + (line_lane < 0 && line_port < 0);
    /*  In travel order: an ON spur starts at the line (a lane, or a
     *  junction's port) and ends on the slab lane.  An OFF spur the
     *  other way round. */
    {
        int rk = line_port >= 0 ? LE_PORT : line_lane >= 0 ? LE_LANE
                                                           : LE_OPEN,
            rv = line_port >= 0 ? line_port : line_lane;
        int dk = slab_lane >= 0 ? LE_LANE : LE_OPEN;
        if (lane_add(LC_SPUR, net_band->f, pc, np, hw, rmin, rmin < LANE_RMIN, off ? dk : rk, off ? slab_lane : rv, off ? rk : dk, off ? rv : slab_lane, 0, 0.0f) != 0)
            return -1;
    }
    return spur_wires(m, c, mask_bit, pc, np, off, 5.0f); /* a spur is a lane: red like a line's */
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



/*  Where a band comes down to grade and becomes a line, its six
 *  lanes have to become the line's two.  At each band end, per side: the
 *  INNER slab lane routes into the line's lane on that side.  The line
 *  lane whose open end lies nearest the slab lane's, running the same
 *  way.  And the middle and outer lanes taper into the inner lane.  They
 *  do it at stations a tile and a half and three tiles before the end.
 *  On the side traveling onto the slab).  A band end that meets no line
 *  stays open and is counted. */
/*  Is lane i an open end that could carry on across a meet?  A line lane
 *  whose far end is open, and not at the map's edge: an end there faces
 *  nothing. */
int xlane_end(const XLaneFan *x, int i)
{
    const Lane *l = &s_lane[i];
    V2          pa, da;
    float       edge = net_line_rules()->lane_edge;
    if (i < 0 || i >= x->n || l->cls != LC_LINE || l->kind1 != LE_OPEN)
        return 0;
    lane_end_pose(l, 1, &pa, &da);
    return !(pa.x < edge || pa.y < edge || pa.x > (float)R_MAP - edge || pa.y > (float)R_MAP - edge);
}

/*  How lane lb's start lies from lane la's end.
 *
 *      How far their offsets differ.
 *      How nearly they run the same way.
 *      How far ahead.
 *      Aside and away it is.
 *
 *  0 for a lane that is not a candidate at all. */
int xlane_measure(const XLaneFan *x, int la, int lb, float *off, float *dot, float *ahead, float *aside, float *dist)
{
    const Lane *l = &s_lane[la], *r = &s_lane[lb];
    V2          pa, da, pb, db, v;
    if (lb == la || lb < 0 || lb >= x->n || r->cls != LC_LINE || r->fam != l->fam || r->kind0 != LE_OPEN)
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
 *  middle of it.  The two are one lane.  Each end names the other, and
 *  no piece is drawn between them. */
void xlane_merge(XLaneFan *x, int la, int lb)
{
    ++s_ls.meets;
    (void)x;
    s_lane[la].kind1  = LE_LANE;
    s_lane[la].port1  = lb;
    s_lane[lb].kind0  = LE_LANE;
    s_lane[lb].port0  = la;
}

/*  A lane laid between two open ends, for the meet that carries one
 *  lane on into the one facing it.  The chain is QUEUED for the drive to
 *  cut, like every other path, and the lane laid on the pieces it comes
 *  back with (lane_cross_take).  The band's own ends are joined by
 *  arc.rules.links instead, which queues its own. */
#define XLINK_MAX 1024
static struct
{
    V2    A, tA, B, tB;
    float w;
    int   from, to, cut;
} s_xlink[XLINK_MAX];
static int s_n_xlink;

static int link_ask(V2 A, V2 tA, V2 B, V2 tB, float w, int from, int to)
{
    if (s_n_xlink >= XLINK_MAX)
        return -1;
    s_xlink[s_n_xlink].A    = A, s_xlink[s_n_xlink].tA = tA;
    s_xlink[s_n_xlink].B    = B, s_xlink[s_n_xlink].tB = tB;
    s_xlink[s_n_xlink].w    = w;
    s_xlink[s_n_xlink].from = from, s_xlink[s_n_xlink].to = to;
    s_xlink[s_n_xlink++].cut = net_cut_add_poses(A, tA, B, tB);
    return 0;
}

/*  Otherwise a straight link drawn from the one end to the other. */
int xlane_link(XLaneFan *x, int la, int lb)
{
    V2 pa, da, pb, db;
    ++s_ls.meets;
    lane_end_pose(&s_lane[la], 1, &pa, &da);
    lane_end_pose(&s_lane[lb], 0, &pb, &db);
    if (link_ask(pa, da, pb, db, s_lane[la].w, la, lb) != 0)
    {
        x->fail = 1;
        return 0;
    }
    return 1;
}

/*  ACROSS A MEET: the open ends as they stand, for arc.rules.cross
 *  to carry each of them on into the lane facing it.  The drive composes
 *  it before the bands' own ends are joined. */
static XLaneFan s_xlane;

XLaneFan *lane_cross_ask(RMesh *m, const RCity *c, uint8_t mask_bit)
{
    memset(&s_xlane, 0, sizeof s_xlane);
    s_n_xlink = 0;
    net_cut_reset();
    if (s_pass == 1)
        return NULL; /* the grading pass: lanes are the building pass's */
    s_xlane.m        = m;
    s_xlane.c        = c;
    s_xlane.mask_bit = mask_bit;
    s_xlane.n        = s_nl;
    return &s_xlane;
}

/*  And the links laid on the pieces the drive cut.  A chain the cut
 *  refused leaves the two ends as they were: a car reaching one turns
 *  round rather than meet. */
int lane_cross_take(void)
{
    RMesh       *m        = (RMesh *)s_xlane.m;
    const RCity *c        = (const RCity *)s_xlane.c;
    uint8_t      mask_bit = s_xlane.mask_bit;
    static Piece tmp[MAX_PIECES];
    int          i, rc = 0;
    for (i = 0; i < s_n_xlink; ++i)
    {
        int   np, q;
        float rmin = 1e9f;
        if (s_xlink[i].cut < 0 || net_cut_pieces(s_xlink[i].cut, tmp, MAX_PIECES, &np) != 0 || np < 1)
        {
            ++s_ls.link_fail;
            if (dump_misses())
                dumpf("LANE link failed: %.2f,%.2f heading %.2f,%.2f -> %.2f,%.2f heading %.2f,%.2f (lanes %d -> %d)\n", (double)s_xlink[i].A.x, (double)s_xlink[i].A.y, (double)s_xlink[i].tA.x, (double)s_xlink[i].tA.y, (double)s_xlink[i].B.x, (double)s_xlink[i].B.y, (double)s_xlink[i].tB.x, (double)s_xlink[i].tB.y, s_xlink[i].from, s_xlink[i].to);
            continue;
        }
        for (q = 0; q < np; ++q)
            if (tmp[q].arc && tmp[q].r < rmin)
                rmin = tmp[q].r;
        if (lane_ends_take(s_xlink[i].from, s_xlink[i].to, s_nl) != 0)
            continue;
        ++s_ls.links;
        if (lane_add(LC_LINK, net_line->f, tmp, np, s_xlink[i].w, rmin, rmin < LANE_RMIN, LE_LANE, s_xlink[i].from, LE_LANE, s_xlink[i].to, 0, 0.0f) != 0)
            rc = -1;
        else if (lane_wires(m, c, mask_bit, tmp, np, net_line_rules()->lane_wire, 5.0f, 0) != 0)
            rc = -1;
    }
    s_n_xlink = 0;
    return rc;
}

/*  The band's lanes joined.  Which slab lane goes on to which.  It is
 *  the lane of another band round an interchange, the line's lane where
 *  the slab comes down.  The inner lane of its own band where it tapers
 *  out: is arc.rules.links's (scripts/compose/links.lua).  There is no
 *  matcher in C behind it: take the rule away and a slab's lanes end in
 *  mid-air.
 *
 *  This hands the lanes over as they stand.  The accessors below are
 *  what the rule reads them through and lays its links with. */
static struct
{
    RMesh       *m;
    const RCity *c;
    uint8_t      mask_bit;
    int          nl; /* the lanes as they stand.  The links are appended */
} s_links;

void *net_links_fan(RMesh *m, const RCity *c, uint8_t mask_bit)
{
    if (grade_only(g_dev.grade_lanes) || s_xlane.fail)
        return NULL; /* the grading pass: lanes are the building pass's */
    s_links.m = m, s_links.c = c, s_links.mask_bit = mask_bit;
    s_links.nl = s_nl;
    return &s_links;
}

int net_links_count(void)
{
    return s_links.nl;
}

/*  One lane as it stands: what it is.  This band it belongs to, how far
 *  off that band's centerline it lies, and whether each end is open. */
int net_links_lane(int i, int *slab, int *line, int *band, float *off, float *w, float *len, int *open0, int *open1)
{
    const Lane *l;
    if (i < 0 || i >= s_links.nl)
        return 0;
    l      = &s_lane[i];
    *slab  = l->cls == LC_SLAB;
    *line  = l->cls == LC_LINE && l->fam == net_line->f;
    *band  = l->band;
    *off   = l->off;
    *w     = l->w;
    *len   = l->len;
    *open0 = l->kind0 == LE_OPEN;
    *open1 = l->kind1 == LE_OPEN;
    return 1;
}

int net_links_pose(int i, int which, float *x, float *y, float *dx, float *dy)
{
    V2 p, d;
    if (i < 0 || i >= s_links.nl || s_lane[i].np < 1)
        return 0;
    lane_end_pose(&s_lane[i], which, &p, &d);
    *x = p.x, *y = p.y, *dx = d.x, *dy = d.y;
    return 1;
}

/*  The station on lane i at arc length `back` before its finish or after
 *  its start: where a lane tapering into its neighbor leaves. */
int net_links_station(int i, int which, float back, float *x, float *y, float *dx, float *dy)
{
    V2 p, d;
    if (i < 0 || i >= s_links.nl)
        return 0;
    lane_station_back(i, which, back, &p, &d);
    *x = p.x, *y = p.y, *dx = d.x, *dy = d.y;
    return 1;
}

/*  And the link laid, from the pieces the script cut for it. */
/*  Answers 0 with both ends marked, -1 where either is already named.
 *  `from` is the end a lane is left by, and `to` the start it is entered
 *  at.  An end the join does not claim is -1. */
static int lane_ends_take(int from, int to, int by)
{
    if ((from >= 0 && from < s_nl && s_lane[from].kind1 != LE_OPEN) ||
        (to >= 0 && to < s_nl && s_lane[to].kind0 != LE_OPEN))
    {
        ++s_ls.refused;
        return -1;
    }
    if (from >= 0 && from < s_nl)
        s_lane[from].kind1 = LE_LANE, s_lane[from].port1 = by;
    if (to >= 0 && to < s_nl)
        s_lane[to].kind0 = LE_LANE, s_lane[to].port0 = by;
    return 0;
}

/*  A LANE END NAMES ONE OTHER, and one only.  The end carries a single
 *  port.  A second link naming it would overwrite the first, and leave
 *  that one hanging from an end nothing arrives at.  Which reads as a
 *  lane that simply stops.  So the ends a link uses are marked taken as
 *  it is laid, and a link asking for an end already taken is refused.
 *
 *  Marking them is what makes `open0` and `open1` true.  A pass reading
 *  the lanes after another has joined some of them sees what is left.
 *  The passes cannot fight over the same end without knowing. */
int net_links_add(const Piece *pc, int np, float w, int from, int to, int band)
{
    float rmin = 1e9f;
    int   k;
    if (np < 1)
        return 0;
    if (lane_ends_take(from, to, s_nl) != 0)
        return 0;
    for (k = 0; k < np; ++k)
        if (pc[k].arc && pc[k].r < rmin)
            rmin = pc[k].r;
    ++s_ls.links;
    if (lane_add(LC_LINK, net_line->f, pc, np, w, rmin, rmin < LANE_RMIN, LE_LANE, from, LE_LANE, to, 0, 0.0f) != 0)
        return -1;
    return lane_wires(s_links.m, s_links.c, s_links.mask_bit, pc, np, net_line_rules()->lane_wire, 5.0f, band);
}

/*  What the script counted while it joined them, for the report. */
void net_links_note(const char *what, int n)
{
    if (!what)
        return;
    if (strcmp(what, "band_links") == 0)
        s_ls.band_links += n;
    else if (strcmp(what, "band_ends") == 0)
        s_ls.band_ends += n;
    else if (strcmp(what, "band_open") == 0)
        s_ls.band_open += n;
    else if (strcmp(what, "link_fail") == 0)
        s_ls.link_fail += n;
}

/*  Every lane end goes somewhere and every start has something arriving.
 *  A port an inbound lane ends at must have a connector leaving it.  A
 *  port an outbound lane starts at must have one arriving.  A
 *  connector's ports must have those lanes.  An open end must be the end
 *  of a link or a cap, unless it leaves the map. */
static uint8_t s_pend[R_MAP * R_MAP * 4 * 4], s_pstart[R_MAP * R_MAP * 4 * 4], s_cfrom[R_MAP * R_MAP * 4 * 4], s_cto[R_MAP * R_MAP * 4 * 4];
/*  THE LANES' CURVES: what a car has to be able to drive along.  A lane
 *  is a run of pieces laid end to end, and four things make one
 *  undrivable, each counted over every piece of every lane:
 *
 *    broken   piece k ends where piece k+1 does not start: a gap, and
 *             a car reaching it has nowhere to be next
 *    kinked   they meet, but not tangentially: a corner in the line,
 *             which is a turn of no radius at all
 *    tight    an arc under the minimum radius, which no vehicle of the
 *             band's width can take
 *    stubby   a piece under the minimum length.  This is how a tight
 *             turn HIDES: a hair of arc between two straights swings the
 *             line through a large angle while showing a radius that
 *             passes the test, because the arc is too short to be seen.
 *             Without it the other three can all be satisfied by a line
 *             no one could drive.
 *
 *  The tolerances are the script's (arc.geo), because what counts as a
 *  kink is a property of the world's scale and not of this file. */
static void lane_check_curves(int dump)
{
    static int gix_lmin = -1, gix_gap = -1, gix_kink = -1;
    const float lmin = geo_num(&gix_lmin, "lane_min_len");
    const float gap  = geo_num(&gix_gap, "lane_join_gap");
    const float kink = geo_num(&gix_kink, "lane_join_dot");
    int         li;
    for (li = 0; li < s_nl; ++li)
    {
        const Lane  *l  = &s_lane[li];
        const Piece *pc = &s_lp[l->first];
        int          k;
        for (k = 0; k < l->np; ++k)
        {
            if (pc[k].len < lmin)
            {
                ++s_ls.stubby;
                if (dump)
                    dumpf("LANE stubby: lane %d class %d piece %d of %d is %.4f long, under %.4f\n", li, l->cls, k, l->np, (double)pc[k].len, (double)lmin);
            }
            if (pc[k].arc && fabsf(pc[k].r) < LANE_RMIN)
            {
                ++s_ls.rtight;
                if (dump)
                    dumpf("LANE tight: lane %d class %d piece %d radius %.4f, under %.4f\n", li, l->cls, k, (double)fabsf(pc[k].r), (double)LANE_RMIN);
            }
            if (k + 1 < l->np)
            {
                V2    pe, te, ps, ts;
                float d2, dt;
                pieces_at(&pc[k], 1, pc[k].len, &pe, &te);
                pieces_at(&pc[k + 1], 1, 0.0f, &ps, &ts);
                d2 = (pe.x - ps.x) * (pe.x - ps.x) + (pe.y - ps.y) * (pe.y - ps.y);
                dt = te.x * ts.x + te.y * ts.y;
                if (d2 > gap * gap)
                {
                    ++s_ls.broken;
                    if (dump)
                        dumpf("LANE broken: lane %d class %d between pieces %d and %d, %.4f apart at %.2f,%.2f\n", li, l->cls, k, k + 1, (double)sqrtf(d2), (double)pe.x, (double)pe.y);
                }
                else if (dt < kink)
                {
                    ++s_ls.kinked;
                    if (dump)
                        dumpf("LANE kinked: lane %d class %d between pieces %d and %d, %.1f degrees at %.2f,%.2f\n", li, l->cls, k, k + 1, (double)(acosf(dt < -1.0f ? -1.0f : dt > 1.0f ? 1.0f : dt) * 57.29578f), (double)pe.x, (double)pe.y);
                }
            }
        }
    }
}

void           lane_check_ends(void)
{
    /*  How many links or caps name each open start and each open end.  A
     *  COUNT and not a flag: a lane end carries one port.  So a second
     *  link naming it overwrites the first and leaves that one hanging
     *  from an end nothing arrives at.  At an interchange, where many
     *  movements want the same few ends, that is the failure to watch
     *  for.  A lane may connect to ONE lane, never to several. */
    static uint8_t cov0[L_MAX], cov1[L_MAX];
    int            li, nowhere = 0, nothing = 0, dump = dump_misses();
    memset(s_pend, 0, sizeof s_pend);
    memset(s_pstart, 0, sizeof s_pstart);
    memset(s_cfrom, 0, sizeof s_cfrom);
    memset(s_cto, 0, sizeof s_cto);
    memset(cov0, 0, sizeof cov0);
    memset(cov1, 0, sizeof cov1);
    lane_check_curves(dump);
    for (li = 0; li < s_nl; ++li)
    {
        const Lane *l = &s_lane[li];
        if (l->cls == LC_LINE || l->cls == LC_SPUR || l->cls == LC_SLAB)
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
            {
                if (cov1[l->port0] && ++s_ls.doubled && dump) /* it leaves that lane's end */
                    dumpf("LANE end named twice: lane %d's end, by link %d and another\n", l->port0, li);
                if (cov1[l->port0] < 255)
                    ++cov1[l->port0];
            }
            if (l->kind1 == LE_LANE && l->port1 >= 0 && l->port1 < L_MAX)
            {
                if (cov0[l->port1] && ++s_ls.doubled && dump) /* it arrives at that lane's start */
                    dumpf("LANE start named twice: lane %d's start, by link %d and another\n", l->port1, li);
                if (cov0[l->port1] < 255)
                    ++cov0[l->port1];
            }
        }
    }
    for (li = 0; li < s_nl; ++li)
    {
        const Lane *l = &s_lane[li];
        V2          p, d;
        int         edge;
        /*  A SLAB lane's open end is a fault like any other.  A way that
         *  stops in mid-air is exactly what a driver cannot do.  So it
         *  is counted, apart from the rest because the corpus is not at
         *  zero and the two numbers move for different reasons. */
        if (l->cls == LC_SLAB)
        {
            lane_end_pose(l, 1, &p, &d);
            edge = p.x < net_line_rules()->lane_edge || p.y < net_line_rules()->lane_edge || p.x > (float)R_MAP - net_line_rules()->lane_edge || p.y > (float)R_MAP - net_line_rules()->lane_edge;
            if (l->kind1 == LE_OPEN && !cov1[li] && !edge)
            {
                ++s_ls.slab_nowhere;
                if (dump)
                    dumpf("LANE slab nowhere to go: lane %d band %d off %.2f ends at %.2f,%.2f heading %.2f,%.2f\n", li, l->band, (double)l->off, (double)p.x, (double)p.y, (double)d.x, (double)d.y);
            }
            lane_end_pose(l, 0, &p, &d);
            edge = p.x < net_line_rules()->lane_edge || p.y < net_line_rules()->lane_edge || p.x > (float)R_MAP - net_line_rules()->lane_edge || p.y > (float)R_MAP - net_line_rules()->lane_edge;
            if (l->kind0 == LE_OPEN && !cov0[li] && !edge)
            {
                ++s_ls.slab_nothing;
                if (dump)
                    dumpf("LANE slab nothing arrives: lane %d band %d off %.2f starts at %.2f,%.2f heading %.2f,%.2f\n", li, l->band, (double)l->off, (double)p.x, (double)p.y, (double)d.x, (double)d.y);
            }
            continue;
        }
        if (l->cls == LC_LINE || l->cls == LC_SPUR || l->cls == LC_SLAB)
        {
            lane_end_pose(l, 1, &p, &d);
            edge = p.x < net_line_rules()->lane_edge || p.y < net_line_rules()->lane_edge || p.x > (float)R_MAP - net_line_rules()->lane_edge || p.y > (float)R_MAP - net_line_rules()->lane_edge;
            if ((l->kind1 == LE_PORT && !s_cfrom[l->port1]) || (l->kind1 == LE_OPEN && !cov1[li] && !edge))
            {
                ++nowhere;
                if (dump)
                    dumpf("LANE nowhere to go: lane %d class %d ends at %.2f,%.2f heading %.2f,%.2f\n", li, l->cls, (double)p.x, (double)p.y, (double)d.x, (double)d.y);
            }
            lane_end_pose(l, 0, &p, &d);
            edge = p.x < net_line_rules()->lane_edge || p.y < net_line_rules()->lane_edge || p.x > (float)R_MAP - net_line_rules()->lane_edge || p.y > (float)R_MAP - net_line_rules()->lane_edge;
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
           "ends off their port; %d slabs, %d spurs, %d spur ends on no lane; %d band ends (%d meeting no line, %d on to another band), %d links (%d failed), %d through a meet; %d dead-end caps (%d failed); %d ends with nowhere to go, %d starts with nothing arriving\n"
           "lanes  a SLAB lane's own ends: %d with nowhere to go, %d with nothing arriving\n"
           "lanes  the curves: %d broken between pieces, %d kinked, %d arcs under the minimum radius, %d pieces under the minimum length\n"
           "lanes  %d lane ends named by more than one link, %d links refused for asking\n",
           s_ls.junctions,
           s_ls.ports,
           s_ls.conns,
           s_ls.tight,
           s_ls.failed,
           s_ls.segs,
           s_ls.wide,
           s_ls.mismatch,
           s_ls.slabs,
           s_ls.spurs,
           s_ls.spur_miss,
           s_ls.band_ends,
           s_ls.band_open,
           s_ls.band_links,
           s_ls.links,
           s_ls.link_fail,
           s_ls.meets,
           s_ls.caps,
           s_ls.cap_fail,
           s_ls.nowhere,
           s_ls.nothing,
           s_ls.slab_nowhere,
           s_ls.slab_nothing,
           s_ls.broken,
           s_ls.kinked,
           s_ls.rtight,
           s_ls.stubby,
           s_ls.doubled,
           s_ls.refused);
    if (g_dev.times)
        dumpf("time    lane index: %d filings of %d, %d lanes of %d, %d pieces of %d\n", s_nlx, L_FILED, s_nl, L_MAX, s_nlp, L_PIECES);
}

/*  The exported face of port_pose, for the families' own drawings. */
int lane_port(Family f, int col, int row, int e, int out, V2 *pos, V2 *dir)
{
    port_pose(f, col, row, e, out, 0, pos, dir); /* the first lane of the port */
    return 0;
}

/*  The lane table, for the inspector: a spur, a junction's connector and
 *  a slab's lane are lanes and nothing else.  They are in no segment
 *  table, so this is the only way to point at one. */
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
