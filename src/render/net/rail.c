/*  The rail family: the rail junction, level crossings with roads, and
 *  what the markings need to know about them. */
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "mesh/internal.h"
#include "net/internal.h"
#include "geo/model.h"
#include "script.h"
#include "dump.h"
#include "opt.h"

/*  The numbers this file reads, each remembering where the store
 *  put it (net/geo.c).  A name here is a name a script must set. */


RCross s_cross[2][R_MAP * R_MAP];

/*  Whether a station's tile is a level crossing or within a tile of one. */
int marking_near_crossing(const RCity *c, V2 pos)
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
            if (net_rail_crossing(b))
                return 1;
        }
    return 0;
}

/*  The measuring pass: where this path passes each tile, for the level crossings; it draws nothing. */
int seg_measure_crossings(Seg *x)
{
    Family f      = x->f;
    Piece *pieces = x->pieces;
    int    np     = x->np;
    float  total  = x->total;
    /*  Where this path passes each tile it crosses, and its
     *  direction there: the nearest point to the tile's middle.  A
     *  level crossing is built from the road's and the rail's. */
    float s;
    int   pi  = 0;
    float acc = 0.0f;
    for (s = 0.0f; s <= total; s += 0.05f)
    {
        V2      pos, dir;
        int32_t tc, tr;
        float   d2;
        RCross *xc;
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
        xc = &s_cross[FAMX(f)][tr * R_MAP + tc];
        d2 = (pos.x - ((float)tc + 0.5f)) * (pos.x - ((float)tc + 0.5f)) +
             (pos.y - ((float)tr + 0.5f)) * (pos.y - ((float)tr + 0.5f));
        if (!xc->have || d2 < (xc->x - ((float)tc + 0.5f)) * (xc->x - ((float)tc + 0.5f)) +
                                  (xc->y - ((float)tr + 0.5f)) * (xc->y - ((float)tr + 0.5f)))
        {
            xc->x    = pos.x;
            xc->y    = pos.y;
            xc->dx   = dir.x;
            xc->dy   = dir.y;
            xc->have = 1;
        }
    }
    return 0; /* the measuring pass draws nothing */
}

/*  One junction box's working state, handed to the stages below so
 *  each can be read on its own: the tile, its half width and the box's
 *  corners, the sidewalk's widths, and the height the box stands at. */

/*  A track of the box lofted as a segment's strip is (loft.c): the
 *  family's ballast, ties and rails along the routed pieces, its ground
 *  graded in the grading pass and read at every tile edge in the
 *  drawing pass, so a curve across a corner tile no segment grades sits
 *  on ground cut for it.  `raise` seats it a hair over the usual: the
 *  through track over the wye's curves, the second curve under the
 *  first, so where they run together the higher one shows and nothing
 *  flickers. */
static int rail_loft(JBox *jb, const Piece *pc, int np, float raise)
{
    const NetFamily *fam   = net_family(F_RAIL);
    RLoft            d     = {0};
    float            total = 0.0f;
    V2               p, t;
    int              k;
    for (k = 0; k < np; ++k)
        total += pc[k].len;
    d.f    = F_RAIL;
    d.fam  = fam;
    d.hw   = jb->h;
    d.mat  = jb->mat;
    d.kind = fam->loft;
    d.cls  = -1.0f;
    pieces_at(pc, np, 0.0f, &p, &t);
    d.node[0][0] = (int32_t)floorf(p.x), d.node[0][1] = (int32_t)floorf(p.y);
    pieces_at(pc, np, total, &p, &t);
    d.node[1][0] = (int32_t)floorf(p.x), d.node[1][1] = (int32_t)floorf(p.y);
    d.raise        = raise;
    d.records_only = jb->records_only;
    return net_box_loft_add(jb, &d, pc, np, total);
}

/*  An arm's two rails at its port: a rail's one lane has its in-port on
 *  one rail and its out-port on the other (lane.c: the lane sits a
 *  rail's half gauge right of each heading), each with the heading away
 *  from the junction. */
typedef struct
{
    V2 p, away;
} RailEnd;

static void rail_ends(const JBox *jb, int e, RailEnd out[2])
{
    V2 d;
    lane_port(F_RAIL, jb->col, jb->row, e, 0, 0, &out[0].p, &d); /* the in-port: heads into the junction */
    out[0].away = (V2){-d.x, -d.y};
    lane_port(F_RAIL, jb->col, jb->row, e, 1, 0, &out[1].p, &out[1].away);
}

static V2 rail_mid(const RailEnd r[2])
{
    return (V2){(r[0].p.x + r[1].p.x) * 0.5f, (r[0].p.y + r[1].p.y) * 0.5f};
}

/*  A route between two poses (lane.c lane_route, the equal-tangent
 *  biarc), or the straight between them when the router has none. */
static float rail_route(V2 A, V2 tA, V2 B, V2 tB, Piece *pc, int *np)
{
    float rmin;
    if (lane_route(A, tA, B, tB, pc, np, &rmin) == 0 && *np > 0)
        return rmin;
    pc[0].arc = 0;
    pc[0].a   = A;
    pc[0].b   = B;
    pc[0].len = hypotf(B.x - A.x, B.y - A.y);
    *np       = 1;
    return 0.0f;
}

/*  The through track between two opposite arms, from one's port to the
 *  other's along the router's path: a straight where both arms run
 *  straight, a curve where one bends within the reach (the staircase
 *  rail at Atlanta 72,124), lofted at the strip's width. */
static int rail_through(JBox *jb, int e, float raise)
{
    static Piece pc[MAX_PIECES];
    RailEnd      a[2], b[2];
    int          np;
    float        rmin;
    rail_ends(jb, e, a);
    rail_ends(jb, (e + 2) & 3, b);
    rmin = rail_route(rail_mid(a), (V2){-a[0].away.x, -a[0].away.y}, rail_mid(b), b[0].away, pc, &np);
    if (g_dev.junc_dump)
        dumpf("RAIL through %d,%d %c-%c: %d pieces, rmin %.2f\n", jb->col, jb->row, "NESW"[e], "NESW"[(e + 2) & 3], np, (double)rmin);
    return rail_loft(jb, pc, np, raise);
}

/*  A branch, the arm without an opposite, is a wye: two tracks from its
 *  port, one curving into each through arm's port along the router's
 *  path, at whatever radius the reaches give -- a branch that bends
 *  within the reach is met where its track is, since its port lies on
 *  its path.  Each is lofted as a strip under the through track's, the
 *  second a hair under the first, so where they run together the higher
 *  one's ties and rails show: the curves vanish into the through track
 *  where they merge, as they do at the branch's port where they part. */
static int rail_branch(JBox *jb, int eb)
{
    static Piece pc[MAX_PIECES];
    RailEnd      br[2], ar[2];
    V2           from, into;
    int          e, k = 0, np;
    rail_ends(jb, eb, br);
    from = rail_mid(br);
    into = (V2){-br[0].away.x, -br[0].away.y};
    for (e = 0; e < 4; ++e)
        if (e != eb && (jb->links & (1 << e)))
        {
            float rmin;
            rail_ends(jb, e, ar);
            rmin = rail_route(from, into, rail_mid(ar), ar[0].away, pc, &np);
            ++k;
            if (g_dev.junc_dump)
                dumpf("RAIL branch %d,%d %c into %c: %d pieces, rmin %.2f\n", jb->col, jb->row, "NESW"[eb], "NESW"[e], np, (double)rmin);
            if (g_dev.junc_dump)
            {
                int q;
                for (q = 0; q < np; ++q)
                    dumpf("  %s %.2f,%.2f -> %.2f,%.2f len %.2f\n", pc[q].arc ? "arc" : "run", (double)pc[q].a.x, (double)pc[q].a.y, (double)pc[q].b.x, (double)pc[q].b.y, (double)pc[q].len);
            }
            if (rail_loft(jb, pc, np, -net_family_rules(F_RAIL)->gauge * (float)(k - 1)) != 0)
                return -1;
        }
    return 0;
}

/*  A rail junction within its box: every track routed between the
 *  arms' ports (lane.c port_pose puts each on its arm's path, the
 *  family's turnout along it) and lofted like a segment's strip, in
 *  both passes -- the grading pass cuts the ground under a curve as
 *  under a segment (junction.c runs a turnout's box there).  A through
 *  line runs port to port whole, a fiftieth over the wye's curves; a
 *  second one, a crossing's, over the first, a diamond; a branch is a
 *  wye, a track curving into each through arm.  Over a level crossing's
 *  panel the emitter paints the rails alone (mesh/shapes.c). */
static int rail_box(JBox *jb)
{
    int links = jb->links, e, second = 0;
    for (e = 0; e < 2; ++e)
        if ((links & (1 << e)) && (links & (1 << (e + 2))))
        {
            if (rail_through(jb, e, net_family_rules(F_RAIL)->through + net_family_rules(F_RAIL)->second * (float)second) != 0)
                return -1;
            ++second;
        }
    for (e = 0; e < 4; ++e)
        if ((links & (1 << e)) && !(links & (1 << ((e + 2) & 3))))
            if (rail_branch(jb, e) != 0)
                return -1;
    return 0;
}

/*  On a level crossing, the rail is drawn as its rails alone -- the
 *  crossing's own surface stands in for the ballast -- but only where that
 *  surface actually is, which is the width of the road it crosses.  Beyond
 *  the asphalt the railway is a railway again: gravel and sleepers, right
 *  up to the road.  The road's own line through the tile says where that
 *  is. */
int on_crossing_panel(const RCity *c, int32_t tc, int32_t tr, float x, float y)
{
    const RCross *xr;
    uint8_t       b = c->xbld[tr * R_MAP + tc];
    float         dx, dy, across;
    if (!net_road_over_rail(b))
        return 0;
    xr = &s_cross[FAMX(F_ROAD)][tr * R_MAP + tc];
    if (!xr->have)
        return 1; /* no line to measure against: the tile, as it was */
    dx     = x - xr->x;
    dy     = y - xr->y;
    across = fabsf(dx * -xr->dy + dy * xr->dx);
    return across <= ROAD_W * 0.5f;
}

/*  A level crossing (spec 3.15), lifted out of mesh.c.  It is a hundred and
 *  fifty lines of geometry that sat inline in the middle of build_networks,
 *  which is otherwise a driver: it walks the families and calls out.  The
 *  crossing belongs here with the rest of the furniture that stands on one
 *  -- the gates and the signals -- and it needs nothing from the walk but
 *  the tile it is on and the piece that crosses there. */
/*  A level crossing as it is built: the tile, the two paths that cross
 *  there, and the frame the stages below share. */
typedef struct
{
    RMesh             *m;
    const RCity       *c;
    const RAtlasLevel *l;
    uint8_t            mask_bit;
    int32_t            col, row, idx;
    int                second;
    float              order;
    const RCross      *xr, *xl;                /* the road's fitted path through the tile, and the rail's */
    int                ns;                     /* the rail runs north-south */
    float              cx, cy;                 /* the middle of the panel */
    float              ox, oy, lx, ly, rx, ry; /* the road's way through, the rail's, and across the road */
    float              rh, hb, h;              /* the panel's reach along the road, the road's half width, the mast's offset */
    float              sine;                   /* of the angle the road and the line cross at */
    ScriptXing         fr;                     /* the crossing's own measurements, the script's */
} Xing;

/*  The crossing's frame: the road's way through and the rail's, from
 *  the two paths as stage two fitted them; across the road; the
 *  middle of the panel where the centrelines meet, or the tile's own
 *  middle failing that; and how far along the road the track bed
 *  reaches -- its half width over the sine of the angle they cross at,
 *  held to something sane where they cross obliquely. */
static void xing_frame(Xing *x)
{
    const RCity       *c      = x->c;
    const RAtlasLevel *l      = x->l;
    int32_t            col    = x->col;
    int32_t            row    = x->row;
    int                second = x->second;
    int32_t            idx    = x->idx;
    const RCross      *xr = x->xr, *xl = x->xl;
    int                links2 = piece_links(l, second, c->xter[idx]);
    int                ns     = (links2 & (L_N | L_S)) == (L_N | L_S); /* the rail runs north-south */
    float              cx = (float)col + 0.5f, cy = (float)row + 0.5f;
    /* the road's way through, and the rail's */
    float ox = xr->have ? xr->dx : (ns ? 1.0f : 0.0f);
    float oy = xr->have ? xr->dy : (ns ? 0.0f : 1.0f);
    float lx = xl->have ? xl->dx : (ns ? 0.0f : 1.0f);
    float ly = xl->have ? xl->dy : (ns ? 1.0f : 0.0f);
    float rx = -oy, ry = ox; /* across the road, for the panel's two edges */
    float sinang, hb = ROAD_W * 0.5f;
    /*  Where the two centrelines meet: the middle of the
     *  panel.  Failing that, the tile's own middle. */
    if (xr->have && xl->have)
    {
        V2 met;
        if (line_meet((V2){xr->x, xr->y}, (V2){ox, oy}, (V2){xl->x, xl->y}, (V2){lx, ly}, &met))
        {
            float dx = met.x - cx, dy = met.y - cy;
            if (dx * dx + dy * dy < 0.36f) /* still on this tile */
            {
                cx = met.x;
                cy = met.y;
            }
        }
    }
    /*  Every measurement of the crossing follows from the angle the two
     *  cross at: arc.rules.crossing_frame is given its sine and answers
     *  how far along the road the track bed reaches, how far out the
     *  masts stand, how wide the panel is across the rail, and the
     *  panel's own lift and slot. */
    sinang = fabsf(ox * ly - oy * lx);
    memset(&x->fr, 0, sizeof x->fr);
    x->sine = sinang;
    x->ns   = ns;
    x->cx = cx;
    x->cy = cy;
    x->ox = ox;
    x->oy = oy;
    x->lx = lx;
    x->ly = ly;
    x->rx = rx;
    x->ry = ry;
    x->hb = hb;
}

/*  The crossing in the mesh's list, for its gates. */
static int xing_record(Xing *x)
{
    RMesh  *m   = x->m;
    int32_t col = x->col;
    int32_t row = x->row;
    int     ns  = x->ns;
    if (m->n_xings + 1u > m->cap_xings)
    {
        uint32_t nc = m->cap_xings ? m->cap_xings * 2u : 64u;
        RXing   *nx = (RXing *)realloc(m->xings, nc * sizeof *nx);
        if (!nx)
            return -1;
        m->xings     = nx;
        m->cap_xings = nc;
    }
    m->xings[m->n_xings].col = col;
    m->xings[m->n_xings].row = row;
    m->xings[m->n_xings].ns  = ns;
    ++m->n_xings;
    return 0;
}

/*  The panel: where the two bands actually overlap -- the road's full
 *  width, cut by the track bed's two sides.  A rectangle in the road's own
 *  frame covers too much at an angle: it buries the ballast either side of
 *  the road, when the gravel should run up to the asphalt and stop.  Four
 *  corners, each where a road edge meets a rail edge. */
/*  The crossing being measured: gathered by the ask, finished by the
 *  draw, with the script's own answers set between them. */
static Xing    s_xing;
static ShapeId s_xing_sh;

/*  The panel's four corners, once they are worked out: the script lays
 *  the panel over them. */
static XingFan s_xing_fan;
static int     s_xing_has_fan;

static int xing_panel(Xing *x)
{
    RMesh             *m        = x->m;
    const RCity       *c        = x->c;
    uint8_t            mask_bit = x->mask_bit;
    float              order    = x->order;
    const RCross      *xr       = x->xr;
    const RCross      *xl       = x->xl;
    float              cx       = x->cx;
    float              cy       = x->cy;
    float              ox       = x->ox;
    float              oy       = x->oy;
    float              lx       = x->lx;
    float              ly       = x->ly;
    float              rx       = x->rx;
    float              ry       = x->ry;
    float              hb       = x->hb;
    float              rw       = x->fr.bed;
    V2                 op       = {xr->have ? xr->x : cx, xr->have ? xr->y : cy};
    V2                 rp       = {xl->have ? xl->x : cx, xl->have ? xl->y : cy};
    V2                 od = {ox, oy}, rd = {lx, ly};
    V2                 rn = {-ly, lx};
    V2                 q[4];
    int                ok       = 1, k;
    static const float es[4][2] = {
        {-1.0f, -1.0f},
        {1.0f,  -1.0f},
        {-1.0f, 1.0f },
        {1.0f,  1.0f }
    };
    for (k = 0; k < 4 && ok; ++k)
    {
        V2 ea = {op.x + rx * hb * es[k][0], op.y + ry * hb * es[k][0]};
        V2 eb = {rp.x + rn.x * rw * es[k][1], rp.y + rn.y * rw * es[k][1]};
        ok    = line_meet(ea, od, eb, rd, &q[k]);
    }
    if (ok)
    {
        /*  The two paths' own lines settle where the panel's four
         *  corners are.  What is LAID over them is the script's, so the
         *  corners are handed over rather than drawn on. */
        XingFan *f = &s_xing_fan;
        int      k2;
        f->m = m, f->c = c, f->mask_bit = mask_bit;
        for (k2 = 0; k2 < 4; ++k2)
        {
            f->q[k2][0] = q[k2].x, f->q[k2][1] = q[k2].y;
            f->ground[k2] = surface_at_world(c, mask_bit, q[k2].x, q[k2].y);
        }
        f->order = order, f->lift = x->fr.lift, f->slot = x->fr.slot;
        s_xing_has_fan = 1;
    }
    return 0;
}

/*  How far the road runs from the panel's middle along one approach
 *  before something else owns the surface: the mouth of the first
 *  junction the road reaches within two tiles, or two tiles where it
 *  reaches none.  A marking laid past that is drawn inside a junction. */
static float xing_road_limit(const Xing *x, float fx, float fy)
{
    const RCity       *c = x->c;
    const RAtlasLevel *l = x->l;
    float              lim = 2.0f;
    int                k;
    for (k = 1; k <= 2; ++k)
    {
        /*  The approach runs the way the driver came, which is back
         *  along -f from the panel. */
        int32_t nc = x->col - (int32_t)lroundf(fx * (float)k);
        int32_t nr = x->row - (int32_t)lroundf(fy * (float)k);
        int     e;
        float   d;
        if (nc < 0 || nr < 0 || nc >= R_MAP || nr >= R_MAP)
            return lim;
        if (!tile_links(c, l, nc, nr, F_ROAD) || node_kind(c, l, F_ROAD, nc, nr) != 2)
            continue;
        /*  The arm this junction faces the crossing by, and where its
         *  strip starts: that is the mouth, and the road ends there. */
        e = fx > 0.5f ? 1 : fx < -0.5f ? 3
                          : fy > 0.5f  ? 2
                                       : 0;
        d = ((float)nc + 0.5f - x->cx) * -fx + ((float)nr + 0.5f - x->cy) * -fy;
        /*  The mouth stands the road's half width plus the arm's own cut
         *  out from the junction's middle, and past that the junction
         *  may have laid a crossing on the road as well; the carriageway
         *  a marking may be painted on starts beyond both. */
        d -= *net_road->width * 0.5f + s_trim[FAMX(F_ROAD)][(nr * R_MAP + nc) * 4 + e] + net_cross_depth(F_ROAD, nc, nr, e);
        return d < 0.0f ? 0.0f : d;
    }
    return lim;
}

/*  The two road approaches, along the road's own line.  What each one
 *  carries -- the gate's mast, the stop line, the second-train signs --
 *  and where each stands is arc.rules.crossing_marks's; what is left
 *  here is the frame it is placed in, and the register a gate joins so
 *  the traffic can swing its arm. */
/*  ONE APPROACH of a crossing, gathered: where its middle is, which way
 *  the road runs and which way across it, how far the bed reaches, how
 *  far out the masts stand, and how far the road runs before a junction
 *  owns it.  Answers 0 past the second approach.
 *
 *  The mesh is opened for the marks the script decides on; net_crossing_
 *  place puts each of them where it said. */
static int s_ap;

int net_crossing_approach(int i, ScriptApproachAsk *out)
{
    Xing        *x  = &s_xing;
    const RCity *c  = x->c;
    float        cx = x->cx, cy = x->cy;
    float        fx, fy, gx, gy;
    if (i < 0 || i > 1)
        return 0;
    fx = i ? -x->ox : x->ox;
    fy = i ? -x->oy : x->oy;
    /*  The driver's right on this approach.  East is DECREASING column
     *  here, so the right hand of a direction d is (d.y, -d.x) -- the
     *  other turn puts the gate and the stop line across the oncoming
     *  lane. */
    gx = fy, gy = -fx;
    out->reach = x->rh, out->mast = x->h;
    out->limit = xing_road_limit(x, fx, fy);
    out->road  = ROAD_W;
    out->x = cx, out->y = cy;
    out->fx = fx, out->fy = fy;
    out->gx = gx, out->gy = gy;
    s_ap = i;
    (void)c;
    return 1;
}

/*  Where in the stack a crossing's marks stand, for the drive to open
 *  the mesh at before it asks for them. */
float net_crossing_order(void)
{
    return s_xing.order;
}

/*  One mark where the script put it: a gate arm across the road, or a
 *  second-train sign facing the footway. */
int net_crossing_place(const ScriptApproach *mk)
{
    Xing *x  = &s_xing;
    float fx = s_ap ? -x->ox : x->ox;
    float fy = s_ap ? -x->oy : x->oy;
    float gx = fy, gy = -fx;
    float px = x->cx - fx * mk->out + gx * mk->across;
    float py = x->cy - fy * mk->out + gy * mk->across;
    if (strcmp(mk->model, "gate") == 0)
        return put_gate(x->m, x->c, x->mask_bit, x->order, px, py, fx, fy, x->ns);
    return put_second_train_sign(x->m, x->c, x->mask_bit, x->order, px, py, -fx, -fy);
}




int build_crossing(RMesh *m, const RCity *c, const RAtlasLevel *l, uint8_t mask_bit, int32_t col, int32_t row, int second)
{
    /*  A level crossing (spec 3.15): every rail is a double- track
     *  mainline, so every road class gets gates.  It is built from the two
     *  paths that actually cross here -- the road's and the railway's, as
     *  stage two fitted them -- and not from the tile's axes, so a road
     *  meeting the line at an angle gets a panel that lies along it.  The
     *  panel covers the whole of the road it interrupts: the road's full
     *  width across, and along the road as far as the track bed reaches,
     *  which at an angle is further than the track is wide.  The ballast
     *  and the sleepers run under it right up to the road, since the
     *  railway does not stop at a crossing.  On each approach the mast
     *  stands at the driver's right, a road's half width from the
     *  centreline and the panel's reach from the middle; the stop line
     *  crosses the approach lane 4.5 m before it; a second-train sign faces
     *  each footway.  The gates are down and the flashers lit while a train
     *  stands within three tiles along the line. */
    Xing    x;
    ShapeId sh;
    memset(&x, 0, sizeof x);
    x.m = m, x.c = c, x.l = l, x.mask_bit = mask_bit, x.col = col, x.row = row, x.second = second;
    x.idx   = row * R_MAP + col;
    x.order = tile_order(c, col, row, mask_bit);
    x.xr    = &s_cross[FAMX(F_ROAD)][x.idx];
    x.xl    = &s_cross[FAMX(F_RAIL)][x.idx];
    /*  The stages: the frame, then the record, the panel and the
     *  approaches.  The frame is the SCRIPT'S and sits between them, so
     *  this is where the crossing is handed over and taken back. */
    sh = shape_open("level crossing at %d,%d", (int)col, (int)row);
    xing_frame(&x);
    s_xing    = x;
    s_xing_sh = sh;
    return 0;
}

/*  What the script is told about the crossing it is being asked to
 *  measure: the angle the two cross at, and the two widths. */
void net_crossing_ask(int32_t *col, int32_t *row, float *sine, float *road, float *rail)
{
    *col = s_xing.col, *row = s_xing.row;
    *sine = s_xing.sine, *road = ROAD_W, *rail = RAIL_W;
}

/*  And what it answered: how far the bed reaches, how far out the masts
 *  stand, and the panel's width, lift and slot. */
void net_crossing_frame(const ScriptXing *fr)
{
    s_xing.fr = *fr;
    s_xing.rh = fr->reach;
    s_xing.h  = fr->mast;
}

/*  The record, and the panel's four corners worked out from the two
 *  paths: answers the corners for the script to lay the panel over, or 0
 *  where the lines do not meet and there is no panel to lay. */
const XingFan *net_crossing_panel(void)
{
    s_xing_has_fan = 0;
    if (xing_record(&s_xing) != 0 || xing_panel(&s_xing) != 0)
        return NULL;
    return s_xing_has_fan ? &s_xing_fan : NULL;
}

/*  And the approaches: the masts, the stop lines and the signs, once the
 *  panel they stand off is laid. */
int net_crossing_approaches(void)
{
    script_emit_close();
    shape_close(s_xing_sh);
    s_xing_sh = SHAPE_NONE;
    return 0;
}

/* ---- the rail as a family ------------------------------------------------ */

/*  A track records its edge in the trains' graph; it has no class and
 *  no sidewalk. */
static int rail_record(Loft *x)
{
    return net_record(&x->m->railnet, x->smp, x->ns, x->total, 0, 1, x->d);
}

/*  Where the trains run across the right of way: what the road's
 *  local-class rule gave a track before the families had their own say,
 *  a fifth of a road's width either side, kept as it was. */
static void rail_traffic_lanes(const RLoft *d, int cls, float *lane_in, float *lane_out)
{
    (void)d;
    (void)cls;
    *lane_out = ROAD_W * 0.200f;
    *lane_in  = ROAD_W * 0.200f;
}

/*  A rail's signalling (spec 5.6), right-hand running: the track to the
 *  right of the walk carries traffic forward and its signals stand on
 *  its outer side facing back; the other track's face forward on the
 *  other side.  Which signals there are and where they stand is
 *  arc.rules.rail_marks's; what is left here is the walk that turns a
 *  distance along the strip into a place on the map, and the register a
 *  signal joins so its block can light it.  The signals show their
 *  block's occupancy and nothing more: the trains do not obey them. */
static float      s_cross_at[64];
static int        s_n_cross;
static ScriptMark s_mark[192];
static int        s_n_mark;

/*  Where along the strip a road crosses it: the rule places the whistle
 *  posts against these, and nothing is placed until it answers. */
static int rail_furniture(Loft *x)
{
    RMesh       *m        = x->m;
    const RCity *c        = x->c;
    uint8_t      mask_bit = x->mask_bit;
    Sample      *smp      = x->smp;
    int          ns       = x->ns;
    int          i, ncross = 0;
    (void)m, (void)mask_bit;
    s_n_mark = s_n_cross = 0;
    if (ns <= 2)
        return 0;
    /*  Where along the strip a road crosses it: the rule places the
     *  whistle posts against these. */
    for (i = 1; i < ns && ncross < 64; ++i)
    {
        int32_t col = (int32_t)floorf(smp[i].pos.x), row = (int32_t)floorf(smp[i].pos.y);
        int32_t pc2 = (int32_t)floorf(smp[i - 1].pos.x), pr2 = (int32_t)floorf(smp[i - 1].pos.y);
        uint8_t b;
        if (col < 0 || row < 0 || col >= R_MAP || row >= R_MAP || (col == pc2 && row == pr2))
            continue;
        b = c->xbld[row * R_MAP + col];
        if (net_road_over_rail(b))
            s_cross_at[ncross++] = smp[i].s;
    }
    s_n_cross = ncross;
    return 0;
}

/*  Where along the strip a road crosses it, for the rule to place the
 *  whistle posts against. */
void net_rail_cross_ask(const float **cross, int *n)
{
    *cross = s_cross_at;
    *n     = s_n_cross;
}

void net_rail_marks_are(const ScriptMark *mk, int n)
{
    s_n_mark = n < (int)(sizeof s_mark / sizeof s_mark[0]) ? n : (int)(sizeof s_mark / sizeof s_mark[0]);
    if (s_n_mark > 0)
        memcpy(s_mark, mk, sizeof s_mark[0] * (size_t)s_n_mark);
}

/*  And the walk that turns each mark's distance along the strip into a
 *  place on the map, and the register a signal joins so its block can
 *  light it. */
static int rail_furniture_done(Loft *x)
{
    RMesh       *m        = x->m;
    const RCity *c        = x->c;
    uint8_t      mask_bit = x->mask_bit;
    Sample      *smp      = x->smp;
    int          ns       = x->ns;
    ScriptMark  *mk       = s_mark;
    int          k, n = s_n_mark;
    if (ns <= 2)
        return 0;
    for (k = 0; k < n; ++k)
    {
        float   sgn = mk[k].side, rx, ry;
        int     j   = 1;
        int32_t tc, tr;
        while (j < ns - 1 && smp[j].s < mk[k].at)
            ++j;
        if (mk[k].clear && marking_near_crossing(c, smp[j].pos))
            continue;
        rx = -smp[j].dir.y * sgn * mk[k].out;
        ry = smp[j].dir.x * sgn * mk[k].out;
        tc = (int32_t)floorf(smp[j].pos.x + rx);
        tr = (int32_t)floorf(smp[j].pos.y + ry);
        if (tc < 0 || tr < 0 || tc >= R_MAP || tr >= R_MAP)
            continue;
        if (mk[k].signal >= 0)
        {
            /*  A signal is the traffic's as well as the mesh's: it is
             *  registered so the block it watches can light it. */
            if (put_rail_signal(m, c, mask_bit, tile_order(c, tc, tr, mask_bit),
                                smp[j].pos.x + rx, smp[j].pos.y + ry,
                                -smp[j].dir.x * sgn, -smp[j].dir.y * sgn,
                                mk[k].signal, smp[j].s, (int)sgn) != 0)
                return -1;
        }
        else if (net_model_put(net_model_find(mk[k].model), m, c, mask_bit,
                               tile_order(c, tc, tr, mask_bit), smp[j].pos.x + rx, smp[j].pos.y + ry,
                               mk[k].to_map ? 1.0f : -smp[j].dir.x * sgn,
                               mk[k].to_map ? 0.0f : -smp[j].dir.y * sgn, 0.0f, 0.0f, 0.0f) != 0)
            return -1;
    }
    return 0;
}

/*  What this file lends the declarations: the rail's stages, under the
 *  names scripts/families/rail.lua reaches them by. */
void rail_primitives(void)
{
    net_hook_add(NH_BOX, "rail_box", (NetHookFn)rail_box);
    net_hook_add(NH_RECORD, "rail_record", (NetHookFn)rail_record);
    net_hook_add(NH_CROSSING, "level_crossing", (NetHookFn)build_crossing);
    net_hook_add(NH_TRAFFIC, "rail_traffic", (NetHookFn)rail_traffic_lanes);
    net_hook_add_split(NH_FURNITURE, "rail_signals", (NetHookFn)rail_furniture, (NetHookFn)rail_furniture_done, "rail_marks");
}
