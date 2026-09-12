/*  The thread family: the thread junction, level meets with lines, and
 *  what the markings need to know about them. */
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "mesh/internal.h"
#include "pipeline.h"
#include "net/net.h"
#include "mesh/model.h"
#include "script.h"
#include "dump.h"
#include "opt.h"

/*  The numbers this file reads, each remembering where the store
 *  put it (net/geo.c).  A name here is a name a script must set. */


RCross s_cross[2][R_MAP * R_MAP];

/*  Whether a station's tile is a level meet or within a tile of one. */
int marking_near_lap(const RCity *c, V2 pos)
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
            if (net_thread_lap(b))
                return 1;
        }
    return 0;
}

/*  The measuring pass: where this path passes each tile, for the level meets.  It draws nothing. */
int seg_measure_laps(Seg *x)
{
    Family f      = x->f;
    Piece *pieces = x->pieces;
    int    np     = x->np;
    float  total  = x->total;
    /*  Where this path passes each tile it crosses, and its
     *  direction there: the nearest point to the tile's middle.  A
     *  level meet is built from the line's and the thread's. */
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

/*  One junction box's working state, handed to the stages below so each
 *  can be read on its own.
 *
 *      The tile.
 *      Its half width and the box's corners.
 *      The margin's widths.
 *
 *  The height the box stands at. */

/*  A thread of the box, lofted as a segment's strip is (loft.c).  It has
 *  the family's own bed, ties and threads along the routed pieces.  Its
 *  ground is graded in the grading pass.  It is read at every tile edge
 *  in the drawing pass.  So a curve across a corner tile no segment
 *  grades sits on ground cut for it.  `raise` seats it a hair over the
 *  usual: the through thread over the wye's curves, the second curve
 *  under the first.  So where they run together the higher one shows and
 *  nothing flickers. */
static int thread_loft(JBox *jb, const Piece *pc, int np, float raise)
{
    const NetFamily *fam   = net_thread;
    RLoft            d     = {0};
    float            total = 0.0f;
    V2               p, t;
    int              k;
    for (k = 0; k < np; ++k)
        total += pc[k].len;
    d.f    = net_thread->f;
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

/*  An arm's two threads at its port.  One lane has its in-port on one
 *  thread and its out-port on the other.  In lane.c the lane sits a
 *  thread's half gauge right of each heading), each with the heading
 *  away from the junction. */
typedef struct
{
    V2 p, away;
} ThreadEnd;

static void thread_ends(const JBox *jb, int e, ThreadEnd out[2])
{
    V2 d;
    lane_port(net_thread->f, jb->col, jb->row, e, 0, &out[0].p, &d); /* the in-port: heads into the junction */
    out[0].away = (V2){-d.x, -d.y};
    lane_port(net_thread->f, jb->col, jb->row, e, 1, &out[1].p, &out[1].away);
}

static V2 thread_mid(const ThreadEnd r[2])
{
    return (V2){(r[0].p.x + r[1].p.x) * 0.5f, (r[0].p.y + r[1].p.y) * 0.5f};
}

/*  A route between two poses: the equal-tangent biarc lane.c chains:
 *  QUEUED for the drive to cut, in the order the threads are laid.  A
 *  junction's threads are paths like any other, so the pieces they are
 *  drawn from are arc.rules.pieces's and nobody else's.  A route the cut
 *  refuses comes back as the straight between the two ends. */
#define THREADS 8
static struct
{
    V2  A, B;
    int cut;
} s_thread[THREADS];
static int s_n_thread, s_thread_at;

static void thread_route_ask(V2 A, V2 tA, V2 B, V2 tB)
{
    if (s_n_thread >= THREADS)
        return;
    s_thread[s_n_thread].A     = A;
    s_thread[s_n_thread].B     = B;
    s_thread[s_n_thread++].cut = net_cut_add_poses(A, tA, B, tB);
}

static float thread_route(Piece *pc, int *np)
{
    float rmin = 1e9f;
    int   k, i = s_thread_at < s_n_thread ? s_thread_at++ : -1;
    if (i >= 0 && s_thread[i].cut >= 0 && net_cut_pieces(s_thread[i].cut, pc, MAX_PIECES, np) == 0 && *np > 0)
    {
        for (k = 0; k < *np; ++k)
            if (pc[k].arc && pc[k].r < rmin)
                rmin = pc[k].r;
        return rmin;
    }
    pc[0].arc = 0;
    pc[0].a   = i >= 0 ? s_thread[i].A : (V2){0.0f, 0.0f};
    pc[0].b   = i >= 0 ? s_thread[i].B : (V2){0.0f, 0.0f};
    pc[0].len = hypotf(pc[0].b.x - pc[0].a.x, pc[0].b.y - pc[0].a.y);
    *np       = 1;
    return 0.0f;
}

/*  A thread junction within its box.  Every thread is routed between the
 *  arms' ports. lane.c port_pose puts each on its arm's path.  The
 *  family's turnout runs along it.  It is lofted like a segment's strip,
 *  in both passes.  The grading pass cuts the ground under a curve as
 *  under a segment (junction.c runs a turnout's box there).  A through
 *  line runs port to port whole, a fiftieth over the wye's curves.  A
 *  second one, a meet's, over the first, a diamond.  A branch is a wye,
 *  a thread curving into each through arm.  Over a level meet's panel
 *  the emitter paints the threads alone (mesh/shapes.c).
 *
 *  The two halves walk the arms the same way.  The first queues every
 *  thread's path for the drive to cut.  The second draws each on the
 *  pieces that came back. */
/*  ---- THE THREADS A THREAD JUNCTION HAS ------------------------------------
 *
 *  Which arm runs into which, and how far each thread is raised over the
 *  one before it, is arc.rules.node_threads's.  There is no pattern in C
 *  behind it: take the rule away and a thread junction carries no thread
 *  at all.
 *
 *  A thread is asked for as a PAIR OF ARMS.  The route between their two
 *  ends is the lane router's, cut like every other path.  The raise
 *  comes with the pair, because how threads stack is the rule's.  A
 *  second through line lies over the first as a diamond, a wye's threads
 *  a gauge apart, is part of the same answer. */
static struct
{
    int   from, to;
    float raise;
} s_tk[16];
static int   s_n_tk;
static JBox *s_tk_jb;

/*  THE THREADS a junction of this family carries, which are the whole of
 *  what its box is.  The gather clears the list and takes the box in
 *  hand for arc.rules.node_threads to fill.  The loft below lays what it
 *  asked for.  A family that declares no `threads` answers nothing here
 *  and its junction lofts none. */
int net_threads_ask(void)
{
    JBox *jb = net_junction_box_now();
    if (!net_junction_composing() || !net_family_threads(net_family(jb->f)))
        return 0;
    s_n_thread = s_thread_at = 0;
    s_n_tk    = 0;
    s_tk_jb   = jb;
    return 1;
}

void net_threads_info(int *col, int *row, int *links)
{
    *col   = s_tk_jb ? (int)s_tk_jb->col : 0;
    *row   = s_tk_jb ? (int)s_tk_jb->row : 0;
    *links = s_tk_jb ? s_tk_jb->links : 0;
}

/*  One thread wanted, from arm `from` to arm `to`, raised by `raise`.
 *  The route is queued for the drive to cut.  The loft takes it back in
 *  the order it was asked for. */
int net_thread_want(int from, int to, float raise)
{
    ThreadEnd a[2], b[2];
    if (!s_tk_jb || from < 0 || from > 3 || to < 0 || to > 3 || from == to)
        return 0;
    if (!(s_tk_jb->links & (1 << from)) || !(s_tk_jb->links & (1 << to)))
        return 0;
    if (s_n_tk >= (int)(sizeof s_tk / sizeof s_tk[0]))
        return 0;
    thread_ends(s_tk_jb, from, a);
    thread_ends(s_tk_jb, to, b);
    thread_route_ask(thread_mid(a), (V2){-a[0].away.x, -a[0].away.y}, thread_mid(b), b[0].away);
    s_tk[s_n_tk].from  = from;
    s_tk[s_n_tk].to    = to;
    s_tk[s_n_tk].raise = raise;
    ++s_n_tk;
    return 1;
}

/*  And the threads laid, in the order the rule asked for them, each from
 *  the pieces the drive cut for it. */
int net_threads_done(void)
{
    static Piece pc[MAX_PIECES];
    JBox        *jb = net_junction_box_now();
    int          i, np;
    if (!s_tk_jb)
        return 0;
    for (i = 0; i < s_n_tk; ++i)
    {
        float rmin = thread_route(pc, &np);
        if (g_dev.junc_dump)
            dumpf("THREAD thread %d,%d %c-%c: %d pieces, rmin %.2f, raise %.3f\n", jb->col, jb->row,
                  "NESW"[s_tk[i].from], "NESW"[s_tk[i].to], np, (double)rmin, (double)s_tk[i].raise);
        if (thread_loft(jb, pc, np, s_tk[i].raise) != 0)
            return 0;
    }
    s_tk_jb = NULL;
    return 1;
}

/*  On a level meet, the thread is drawn as its threads alone.  The
 *  meet's own surface stands in for the ballast.  But only where that
 *  surface actually is.  This is the width of the line it crosses.
 *  Beyond the fill the family draws its own bed again: gravel and
 *  sleepers, right up to the line.  The line's own line through the tile
 *  says where that is. */
int on_lap_panel(const RCity *c, int32_t tc, int32_t tr, float x, float y)
{
    const RCross *xr;
    uint8_t       b = c->xbld[tr * R_MAP + tc];
    float         dx, dy, across;
    if (!net_line_lapped(b))
        return 0;
    xr = &s_cross[FAMX(net_line->f)][tr * R_MAP + tc];
    if (!xr->have)
        return 1; /* no line to measure against: the tile, as it was */
    dx     = x - xr->x;
    dy     = y - xr->y;
    across = fabsf(dx * -xr->dy + dy * xr->dx);
    return across <= LINE_W * 0.5f;
}

/*  A level meet (spec 3.15), lifted out of mesh.c.  It is a hundred and
 *  fifty lines of geometry that sat inline in the middle of
 *  build_networks.  This is otherwise a driver: it walks the families
 *  and calls out.  The meet belongs here with the rest of the furniture
 *  that stands on one, the gates and the signals.  It needs nothing from
 *  the walk but the tile it is on and the piece that crosses there. */
/*  A level meet as it is built: the tile, the two paths that cross
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
    const RCross      *xr, *xl;                /* the line's fitted path through the tile, and the thread's */
    int                ns;                     /* the thread runs north-south */
    float              cx, cy;                 /* the middle of the panel */
    float              ox, oy, lx, ly, rx, ry; /* the line's way through, the thread's, and across the line */
    float              rh, hb, h;              /* the panel's reach along the line, the line's half width, the mast's offset */
    float              sine;                   /* of the angle the line and the line cross at */
    ScriptLap         fr;                     /* the meet's own measurements, the script's */
} Meet;

static int gix_lap_point = -1;

/*  The meet's frame: the line's way through and the thread's, from the
 *  two paths as stage two fitted them.  Across the line.  The middle of
 *  the panel where the centerlines meet, or the tile's own middle
 *  failing that.  And how far along the line the thread bed reaches.  It
 *  is its half width over the sine of the angle they cross at, held to
 *  something sane where they cross obliquely. */
static void lap_frame(Meet *x)
{
    const RCity       *c      = x->c;
    const RAtlasLevel *l      = x->l;
    int32_t            col    = x->col;
    int32_t            row    = x->row;
    int                second = x->second;
    int32_t            idx    = x->idx;
    const RCross      *xr = x->xr, *xl = x->xl;
    int                links2 = piece_links(l, second, c->xter[idx]);
    int                ns     = (links2 & (L_N | L_S)) == (L_N | L_S); /* the thread runs north-south */
    float              cx = (float)col + 0.5f, cy = (float)row + 0.5f;
    /* the line's way through, and the thread's */
    float ox = xr->have ? xr->dx : (ns ? 1.0f : 0.0f);
    float oy = xr->have ? xr->dy : (ns ? 0.0f : 1.0f);
    float lx = xl->have ? xl->dx : (ns ? 0.0f : 1.0f);
    float ly = xl->have ? xl->dy : (ns ? 1.0f : 0.0f);
    float rx = -oy, ry = ox; /* across the line, for the panel's two edges */
    float sinang, hb = LINE_W * 0.5f;
    /*  Where the two centerlines meet: the middle of the panel.  Failing
     *  that, the tile's own middle. */
    if (xr->have && xl->have)
    {
        V2 met;
        if (line_meet((V2){xr->x, xr->y}, (V2){ox, oy}, (V2){xl->x, xl->y}, (V2){lx, ly}, &met))
        {
            float dx = met.x - cx, dy = met.y - cy;
            float r  = geo_num(&gix_lap_point, "lap_point");
            if (dx * dx + dy * dy < r * r) /* still near enough this tile */
            {
                cx = met.x;
                cy = met.y;
            }
        }
    }
    /*  Every measurement of the meet follows from the angle the two
     *  cross at.  Arc.rules.lap_frame is given its sine.  It answers how
     *  far along the line the thread bed reaches, and how far out the
     *  masts stand.  It also answers how wide the panel is across the
     *  thread, and the panel's own lift and slot. */
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

/*  The meet in the mesh's list, for its gates. */
static int lap_record(Meet *x)
{
    RMesh  *m   = x->m;
    int32_t col = x->col;
    int32_t row = x->row;
    int     ns  = x->ns;
    if (m->n_laps + 1u > m->cap_laps)
    {
        uint32_t nc = m->cap_laps ? m->cap_laps * 2u : 64u;
        RLap   *nx = (RLap *)realloc(m->meets, nc * sizeof *nx);
        if (!nx)
            return -1;
        m->meets     = nx;
        m->cap_laps = nc;
    }
    m->meets[m->n_laps].col = col;
    m->meets[m->n_laps].row = row;
    m->meets[m->n_laps].ns  = ns;
    ++m->n_laps;
    return 0;
}

/*  The panel: where the two bands actually overlap: the line's full
 *  width, cut by the thread bed's two sides.  A rectangle in the line's
 *  own frame covers too much at an angle.  It buries the ballast either
 *  side of the line, when the gravel should run up to the fill and stop.
 *  Four corners, each where a line edge meets a thread edge. */
/*  The meet being measured: gathered by the ask, finished by the
 *  draw, with the script's own answers set between them. */
static Meet    s_lap;
static ShapeId s_lap_sh;

/*  The panel's four corners, once they are worked out: the script lays
 *  the panel over them. */
static LapFan s_lap_fan;
static int     s_lap_has_fan;

static int lap_panel(Meet *x)
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
        LapFan *f = &s_lap_fan;
        int      k2;
        f->m = m, f->c = c, f->mask_bit = mask_bit;
        for (k2 = 0; k2 < 4; ++k2)
        {
            f->q[k2][0] = q[k2].x, f->q[k2][1] = q[k2].y;
            f->ground[k2] = surface_at_world(c, mask_bit, q[k2].x, q[k2].y);
        }
        f->order = order, f->lift = x->fr.lift, f->slot = x->fr.slot;
        s_lap_has_fan = 1;
    }
    return 0;
}

/*  How far the line runs from the panel's middle along one approach,
 *  before something else owns the surface.  That is the mouth of the
 *  first junction the line reaches within two tiles, or two tiles where
 *  it reaches none.  A marking laid past that is drawn inside a
 *  junction. */
static float lap_line_limit(const Meet *x, float fx, float fy)
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
        if (!tile_links(c, l, nc, nr, net_line->f) || node_kind(c, l, net_line->f, nc, nr) != 2)
            continue;
        /*  The arm this junction faces the meet by, and where its
         *  strip starts: that is the mouth, and the line ends there. */
        e = fx > 0.5f ? 1 : fx < -0.5f ? 3
                          : fy > 0.5f  ? 2
                                       : 0;
        d = ((float)nc + 0.5f - x->cx) * -fx + ((float)nr + 0.5f - x->cy) * -fy;
        /*  The mouth stands the line's half width plus the arm's own cut
         *  out from the junction's middle.  Past that the junction may
         *  have laid a meet on the line as well.  The way a marking may
         *  be painted on starts beyond both. */
        d -= *net_line->width * 0.5f + s_trim[FAMX(net_line->f)][(nr * R_MAP + nc) * 4 + e] + net_cross_depth(net_line->f, nc, nr, e);
        return d < 0.0f ? 0.0f : d;
    }
    return lim;
}

/*  The two line approaches, along the line's own line.  What each one
 *  carries: the gate's mast, the stop line, the second-train signs: and
 *  where each stands is arc.rules.lap_marks's.  What is left here is the
 *  frame it is placed in, and the register a gate joins so the traffic
 *  can swing its arm. */
/*  ONE APPROACH of a meet, gathered: where its middle is.  This way the
 *  line runs and which way across it, how far the bed reaches, how far
 *  out the masts stand.  How far the line runs before a junction owns
 *  it.  Answers 0 past the second approach.
 *
 *  The mesh is opened for the marks the script decides on.  Net_meet_
 *  place puts each of them where it said. */
static int s_ap;

int net_lap_approach(int i, ScriptApproachAsk *out)
{
    Meet        *x  = &s_lap;
    const RCity *c  = x->c;
    float        cx = x->cx, cy = x->cy;
    float        fx, fy, gx, gy;
    if (i < 0 || i > 1)
        return 0;
    fx = i ? -x->ox : x->ox;
    fy = i ? -x->oy : x->oy;
    /*  The driver's right on this approach.  East is DECREASING column
     *  here, so the right hand of a direction d is (d.y, -d.x).  The
     *  other turn puts the gate and the stop line across the oncoming
     *  lane. */
    gx = fy, gy = -fx;
    out->reach = x->rh, out->mast = x->h;
    out->limit = lap_line_limit(x, fx, fy);
    out->line  = LINE_W;
    out->x = cx, out->y = cy;
    out->fx = fx, out->fy = fy;
    out->gx = gx, out->gy = gy;
    out->ns = x->ns;
    s_ap = i;
    (void)c;
    return 1;
}

/*  Where in the stack a meet's marks stand, for the drive to open
 *  the mesh at before it asks for them. */
float net_lap_order(void)
{
    return s_lap.order;
}

/*  One mark where the script put it, facing the way it said: `out` along
 *  the approach from the middle and `across` from its centerline.  Which
 *  model stands there is the rule's and this reads the name it gave: a
 *  crossbuck, a gate, a second-train sign are all one call.  The
 *  furniture switch is honoured here: a mark is street furniture, and
 *  the stop line beside it is a marking and is not. */
int net_lap_place(const ScriptApproach *mk)
{
    Meet *x  = &s_lap;
    float fx = s_ap ? -x->ox : x->ox;
    float fy = s_ap ? -x->oy : x->oy;
    float gx = fy, gy = -fx;
    float px = x->cx - fx * mk->out + gx * mk->across;
    float py = x->cy - fy * mk->out + gy * mk->across;
    if (!furniture_on())
        return 0;
    return net_model_put(net_model_find(mk->model), x->m, x->c, x->mask_bit, x->order,
                         px, py, mk->fx, mk->fy);
}




int build_lap(RMesh *m, const RCity *c, const RAtlasLevel *l, uint8_t mask_bit, int32_t col, int32_t row, int second)
{
    /*  A level meet (spec 3.15): every thread is a double- thread
     *  mainline, so every line class gets gates.  It is built from the
     *  two paths that actually cross here, one family's and the other's,
     *  as stage two fitted them.  Not from the tile's axes, so a line
     *  meeting the line at an angle gets a panel that lies along it.
     *  The panel covers the whole of the line it interrupts.  It is the
     *  line's full width across, and along the line as far as the thread
     *  bed reaches.  This at an angle is further than the thread is
     *  wide.  The ballast and the sleepers run under it right up to the
     *  line.  This is because the family whose threads run through does
     *  not stop at a meet.  On each approach the mast stands at the
     *  driver's right, a line's half width from the centerline and the
     *  panel's reach from the middle.  The stop line crosses the
     *  approach lane 4.5 m before it.  A second-train sign faces each
     *  margin.  The gates are down and the flashers lit while a train
     *  stands within three tiles along the line. */
    Meet    x;
    ShapeId sh;
    memset(&x, 0, sizeof x);
    x.m = m, x.c = c, x.l = l, x.mask_bit = mask_bit, x.col = col, x.row = row, x.second = second;
    x.idx   = row * R_MAP + col;
    x.order = tile_order(c, col, row, mask_bit);
    x.xr    = &s_cross[FAMX(net_line->f)][x.idx];
    x.xl    = &s_cross[FAMX(net_thread->f)][x.idx];
    /*  The stages: the frame, then the record, the panel and the
     *  approaches.  The frame is the SCRIPT'S and sits between them, so
     *  this is where the meet is handed over and taken back. */
    sh = shape_open("level meet at %d,%d", (int)col, (int)row);
    lap_frame(&x);
    s_lap    = x;
    s_lap_sh = sh;
    return 0;
}

/*  What the script is told about the meet it is being asked to
 *  measure: the angle the two cross at, and the two widths. */
void net_lap_ask(int32_t *col, int32_t *row, float *sine, float *line, float *thread)
{
    *col = s_lap.col, *row = s_lap.row;
    *sine = s_lap.sine, *line = LINE_W, *thread = THREAD_W;
}

/*  And what it answered: how far the bed reaches, how far out the masts
 *  stand, and the panel's width, lift and slot. */
void net_lap_frame(const ScriptLap *fr)
{
    s_lap.fr = *fr;
    s_lap.rh = fr->reach;
    s_lap.h  = fr->mast;
}

/*  The record, and the panel's four corners worked out from the two
 *  paths.  Answers the corners for the script to lay the panel over.  0
 *  where the lines do not meet and there is no panel to lay. */
const LapFan *net_lap_panel(void)
{
    s_lap_has_fan = 0;
    if (lap_record(&s_lap) != 0 || lap_panel(&s_lap) != 0)
        return NULL;
    return s_lap_has_fan ? &s_lap_fan : NULL;
}

/*  And the approaches: the masts, the stop lines and the signs, once the
 *  panel they stand off is laid. */
int net_lap_approaches(void)
{
    script_emit_close();
    shape_close(s_lap_sh);
    s_lap_sh = SHAPE_NONE;
    return 0;
}

/* ---- the thread as a family ------------------------------------------------ */

/*  A wayside signal registered on the mesh, so the block it watches can
 *  light it each frame.  What model stands there, and where, is the
 *  script's.  This only keeps the record the traffic reads. */
int mesh_signal_add(RMesh *m, float x, float y, float fx, float fy, float s_along, int dir, int absolute)
{
    RSignal *sig;
    if (m->n_rsigs + 1u > m->cap_rsigs)
    {
        uint32_t  nc = m->cap_rsigs ? m->cap_rsigs * 2u : 128u;
        RSignal *ns = (RSignal *)realloc(m->rsigs, nc * sizeof *ns);
        if (!ns)
            return -1;
        m->rsigs     = ns;
        m->cap_rsigs = nc;
    }
    sig           = &m->rsigs[m->n_rsigs++];
    sig->x        = x;
    sig->y        = y;
    sig->fx       = fx;
    sig->fy       = fy;
    sig->s        = s_along;
    sig->seg      = (int32_t)m->threadnet.n_segs - 1;
    sig->dir      = dir;
    sig->absolute = absolute;
    return 0;
}
