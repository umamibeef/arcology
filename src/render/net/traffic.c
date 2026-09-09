/*  traffic.c -- the traffic: cars, trains, signals and gates.  Split out of
 *  mesh.c; see mesh/internal.h. */
#include <string.h>

#include "dump.h"
#include "mesh/internal.h"
#include "net/internal.h"
#include "net/model.h"
#include "script.h"
#include "opt.h"
#include "project.h"

/*  How the moving world behaves, asked of the script once and kept
 *  until the scripts are read again: a frame steps thousands of things
 *  and none of them is worth a call of its own. */
static const ScriptTraffic *tr_rules(void)
{
    static ScriptTraffic s_tr;
    static int           s_gen = -1;
    if (s_gen != script_generation())
    {
        memset(&s_tr, 0, sizeof s_tr);
        s_tr.gap_free = s_tr.step_max = s_tr.trail_step = 1.0f;
        script_rule_traffic(&s_tr);
        s_gen = script_generation();
    }
    return &s_tr;
}




/* ---- the traffic ------------------------------------------------------ */

static uint32_t car_rand(uint32_t *st)
{
    *st = *st * 1664525u + 1013904223u;
    return *st >> 8;
}

static float car_frand(uint32_t *st)
{
    return (float)(car_rand(st) & 0xFFFFu) / 65535.0f;
}

/*  The band at distance s along a segment: position, height, direction. */
static void net_at(const RRoadNet *net, const RNetSeg *sg, float s, float *x, float *y, float *z, float *dx, float *dy)
{
    const RNetPt *p = net->pts + sg->first;
    uint32_t      n = sg->count, lo = 0, hi = n - 1;
    float         t, l;
    if (n == 1 || s <= p[0].s)
    {
        *x  = p[0].x;
        *y  = p[0].y;
        *z  = p[0].z;
        *dx = p[0].dx;
        *dy = p[0].dy;
        return;
    }
    if (s >= p[n - 1].s)
    {
        *x  = p[n - 1].x;
        *y  = p[n - 1].y;
        *z  = p[n - 1].z;
        *dx = p[n - 1].dx;
        *dy = p[n - 1].dy;
        return;
    }
    while (hi - lo > 1)
    {
        uint32_t mid = (lo + hi) / 2;
        if (p[mid].s <= s)
            lo = mid;
        else
            hi = mid;
    }
    t   = p[hi].s > p[lo].s ? (s - p[lo].s) / (p[hi].s - p[lo].s) : 0.0f;
    *x  = p[lo].x + (p[hi].x - p[lo].x) * t;
    *y  = p[lo].y + (p[hi].y - p[lo].y) * t;
    *z  = p[lo].z + (p[hi].z - p[lo].z) * t;
    *dx = p[lo].dx + (p[hi].dx - p[lo].dx) * t;
    *dy = p[lo].dy + (p[hi].dy - p[lo].dy) * t;
    l   = sqrtf(*dx * *dx + *dy * *dy);
    if (l > 1e-6f)
    {
        *dx /= l;
        *dy /= l;
    }
}

/*  A car's place on the road: the band at s offset into its lane, to the
 *  right of its direction of travel AS SEEN, and its heading.  The map's
 *  frame (col east, row south) reaches the screen through a reflection --
 *  col runs down-left, row down-right -- so the map's right-hand normal
 *  (-dy, dx) is the viewer's LEFT; (dy, -dx) is the viewer's right. */
static void car_place(const RRoadNet *net, const RCar *car, float *x, float *y, float *z, float *hx, float *hy)
{
    const RNetSeg *sg = &net->segs[car->seg];
    float          bx, by, bz, dx, dy, lane = car->lane ? sg->lane_in : sg->lane_out;
    net_at(net, sg, car->s, &bx, &by, &bz, &dx, &dy);
    if (car->dir < 0)
    {
        dx = -dx;
        dy = -dy;
    }
    *hx = dx;
    *hy = dy;
    *x  = bx + dy * lane;
    *y  = by - dx * lane;
    *z  = bz;
}

/*  The signal an arm faces: the junction's phase and the arm's axis, as
 *  the lamp material draws it: north-south arms run green for the first
 *  two fifths of the twelve-second cycle from the phase, east-west arms
 *  half a cycle later; amber and red both hold a car at the line. */
static int signal_red(int32_t col, int32_t row, float hx, float hy, float time)
{
    /*  Twenty seconds round (spec 3.4's timing, scaled to the sim): each
     *  group runs green six seconds, amber two, then an all-red
     *  clearance of two before the other group's green. */
    float phase = (float)((col * 7 + row * 13) % 8) / 8.0f;
    float t     = time / 20.0f + phase;
    int   ew    = fabsf(hx) > fabsf(hy);
    t           = t - floorf(t);
    if (ew)
    {
        t += 0.5f;
        t -= floorf(t);
    }
    return t >= tr_rules()->blink;
}

/*  The crossing's approach a (0 or 1): the mast at the driver's right
 *  and the direction of travel, as the mesh placed the mast. */
static void xing_approach(const RXing *x, int a, float *mx, float *my, float *fx, float *fy)
{
    float h = ROAD_W * 0.5f + 0.08f, rh = RAIL_W * 0.5f + 0.10f, cx = (float)x->col + 0.5f, cy = (float)x->row + 0.5f;
    *fx = x->ns ? (a ? -1.0f : 1.0f) : 0.0f;
    *fy = x->ns ? 0.0f : (a ? -1.0f : 1.0f);
    *mx = cx - *fx * rh + (*fy) * h;
    *my = cy - *fy * rh + (-*fx) * h;
}

/*  Append a point to a train's path ring. */
/*  A train car: 0.42 of a tile long, 0.10 wide, coupled 0.48 apart along
 *  the path, its bogies 0.14 each side of its centre. */

static int trail_push(RTrain *tr, float x, float y, float z, float hx, float hy, float d, int32_t seg, float s, int dir)
{
    RTrailPt *q;
    /*  The ring and its size come together: a ring with no size is one
     *  every step round it would divide by. */
    if (!tr->trail || !tr->trail_cap)
    {
        free(tr->trail);
        tr->trail_cap = 2048u;
        tr->trail     = (RTrailPt *)malloc(tr->trail_cap * sizeof *tr->trail);
        if (!tr->trail)
            return -1;
        tr->trail_n = tr->trail_head = 0;
    }
    tr->trail_head = tr->trail_n < tr->trail_cap ? tr->trail_n : (tr->trail_head + 1u) % tr->trail_cap;
    if (tr->trail_n < tr->trail_cap)
        ++tr->trail_n;
    q      = &tr->trail[tr->trail_n < tr->trail_cap ? tr->trail_n - 1u : (tr->trail_head + tr->trail_cap - 1u) % tr->trail_cap];
    q->x   = x;
    q->y   = y;
    q->z   = z;
    q->hx  = hx;
    q->hy  = hy;
    q->d   = d;
    q->s   = s;
    q->seg = seg;
    q->dir = dir;
    return 0;
}

/*  The ring's k-th newest point, 0 the newest, or nothing at all where
 *  the train has not moved yet and the ring holds none. */
static const RTrailPt *trail_at(const RTrain *tr, uint32_t k)
{
    uint32_t idx;
    if (!tr->trail || !tr->trail_n || !tr->trail_cap)
        return NULL;
    if (k >= tr->trail_n)
        k = tr->trail_n - 1u;
    if (tr->trail_n < tr->trail_cap)
        idx = tr->trail_n - 1u - k;
    else
        idx = (tr->trail_head + tr->trail_cap - 1u - k) % tr->trail_cap;
    return &tr->trail[idx];
}

/*  Where the train's car k stands: `back` tiles behind the engine along
 *  its path, interpolated between the two ring points around it. */
static void train_car_at(const RTrain *tr, float back, float *x, float *y, float *z, float *hx, float *hy)
{
    const RTrailPt *a    = trail_at(tr, 0), *b;
    float           want = tr->d - back, t;
    uint32_t        k;
    if (tr->trail_n == 0)
    {
        *x = *y = *z = *hx = *hy = 0.0f;
        return;
    }
    for (k = 1; k < tr->trail_n; ++k)
    {
        b = trail_at(tr, k);
        if (b->d <= want)
        {
            a   = b;
            b   = trail_at(tr, k - 1);
            t   = b->d > a->d ? (want - a->d) / (b->d - a->d) : 0.0f;
            *x  = a->x + (b->x - a->x) * t;
            *y  = a->y + (b->y - a->y) * t;
            *z  = a->z + (b->z - a->z) * t;
            *hx = a->hx + (b->hx - a->hx) * t;
            *hy = a->hy + (b->hy - a->hy) * t;
            return;
        }
        a = b;
    }
    *x  = a->x;
    *y  = a->y;
    *z  = a->z;
    *hx = a->hx;
    *hy = a->hy;
}

/*  The engine's place on the track: the band at s offset onto the track
 *  to the right of its direction of travel. */
static void train_place(const RRoadNet *net, const RTrain *tr, float *x, float *y, float *z, float *hx, float *hy)
{
    const RNetSeg *sg = &net->segs[tr->seg];
    float          bx, by, bz, dx, dy;
    net_at(net, sg, tr->s, &bx, &by, &bz, &dx, &dy);
    if (tr->dir < 0)
    {
        dx = -dx;
        dy = -dy;
    }
    *hx = dx;
    *hy = dy;
    *x  = bx + dy * sg->lane_out; /* the viewer's right, as car_place */
    *y  = by - dx * sg->lane_out;
    *z  = bz;
}

/*  Onward from a rail node: the arm that continues straightest. */
/*  The path across a junction box from the entry point, heading (hx, hy),
 *  to the exit point, heading (gx, gy): straight through when the headings
 *  agree, else a quadratic curve tangent to both arms through the corner
 *  where their lines meet, the junction's own curve, so a turning vehicle
 *  bends through the box as the T's rails and the road's curb returns do
 *  rather than cutting the corner on a chord.  Returns 1 and the corner for
 *  a turn. */
static int box_curve(float ex, float ey, float hx, float hy, float xx, float xy, float gx, float gy, float *cx, float *cy)
{
    V2 c;
    if (hx * gx + hy * gy > 0.7f)
        return 0;
    if (!line_meet((V2){ex, ey}, (V2){hx, hy}, (V2){xx, xy}, (V2){gx, gy}, &c))
        return 0;
    *cx = c.x;
    *cy = c.y;
    return 1;
}

static void box_point(float ex, float ey, float cx, float cy, float xx, float xy, float t, float *x, float *y, float *dx, float *dy)
{
    float u = 1.0f - t;
    *x      = u * u * ex + 2.0f * u * t * cx + t * t * xx;
    *y      = u * u * ey + 2.0f * u * t * cy + t * t * xy;
    *dx     = 2.0f * u * (cx - ex) + 2.0f * t * (xx - cx);
    *dy     = 2.0f * u * (cy - ey) + 2.0f * t * (xy - cy);
}

static float box_length(float ex, float ey, float cx, float cy, float xx, float xy)
{
    float len = 0.0f, px = ex, py = ey, x, y, dx, dy;
    int   k;
    for (k = 1; k <= 8; ++k)
    {
        box_point(ex, ey, cx, cy, xx, xy, (float)k / 8.0f, &x, &y, &dx, &dy);
        len += sqrtf((x - px) * (x - px) + (y - py) * (y - py));
        px = x;
        py = y;
    }
    return len;
}

static int train_turn(const RRoadNet *net, int32_t nc, int32_t nr, int from_seg, float hx, float hy, int *out_seg, int *out_dir)
{
    float    best = -2.0f;
    uint32_t i;
    int      found = 0;
    for (i = 0; i < net->n_segs; ++i)
    {
        const RNetSeg *sg = &net->segs[i];
        int            e;
        for (e = 0; e < 2; ++e)
        {
            const RNetPt *q;
            float         dx, dy, dot;
            if (sg->node[e][0] != nc || sg->node[e][1] != nr)
                continue;
            if ((int)i == from_seg && net->n_segs > 1u)
                continue;
            q   = &net->pts[sg->first + (e == 0 ? 0u : sg->count - 1u)];
            dx  = e == 0 ? q->dx : -q->dx;
            dy  = e == 0 ? q->dy : -q->dy;
            dot = dx * hx + dy * hy;
            if (dot > best)
            {
                best     = dot;
                *out_seg = (int)i;
                *out_dir = e == 0 ? 1 : -1;
                found    = 1;
            }
        }
    }
    return found;
}

/*  The nearest station of the rail net to a point: its segment and s. */
static int railnet_nearest(const RRoadNet *net, float x, float y, int32_t *seg, float *s, float *dx, float *dy)
{
    float    best = 1e9f;
    uint32_t i;
    int      found = 0;
    for (i = 0; i < net->n_segs; ++i)
    {
        const RNetSeg *sg = &net->segs[i];
        uint32_t       j;
        for (j = 0; j < sg->count; ++j)
        {
            const RNetPt *q = &net->pts[sg->first + j];
            float         d = (q->x - x) * (q->x - x) + (q->y - y) * (q->y - y);
            if (d < best)
            {
                best  = d;
                *seg  = (int32_t)i;
                *s    = q->s;
                *dx   = q->dx;
                *dy   = q->dy;
                found = 1;
            }
        }
    }
    return found && best < 1.0f;
}

/*  Fill a train's path backward along its segment, so its cars start
 *  coupled behind the engine. */
static int train_prefill(RTrain *tr, const RRoadNet *net)
{
    float len   = (float)tr->n_cars * TRAIN_PITCH + 0.5f, back;
    tr->trail_n = tr->trail_head = 0;
    for (back = len; back >= 0.0f; back -= 0.05f)
    {
        RTrain probe = *tr;
        float  x, y, z, hx, hy;
        probe.s = tr->s - (float)tr->dir * back;
        if (probe.s < 0.0f)
            probe.s = 0.0f;
        if (probe.s > net->segs[tr->seg].total)
            probe.s = net->segs[tr->seg].total;
        train_place(net, &probe, &x, &y, &z, &hx, &hy);
        if (trail_push(tr, x, y, z, hx, hy, tr->d - back, tr->seg, probe.s, tr->dir) != 0)
            return -1;
    }
    return 0;
}

/*  Whether a signal's block is occupied (spec 5.6): any car of any
 *  train but `skip` stands within the ten tiles ahead of the signal on
 *  its segment in the direction it governs, or just behind it.  Lit red
 *  for the lamp; the trains do not obey it. */
static int rail_block_occupied(const RTraffic *t, const RRailSig *sg2, int skip)
{
    uint32_t k;
    for (k = 0; k < t->n_trains; ++k)
    {
        const RTrain *tr = &t->trains[k];
        int           q;
        if ((int)k == skip)
            continue;
        for (q = 0; q < tr->n_cars; ++q)
        {
            const RTrailPt *pt   = NULL;
            float           want = tr->d - (float)q * TRAIN_PITCH;
            uint32_t        j;
            for (j = 0; j < tr->trail_n; ++j)
            {
                pt = trail_at(tr, j);
                if (pt->d <= want)
                    break;
            }
            if (!pt || j >= tr->trail_n)
                continue;
            if (pt->seg == sg2->seg)
            {
                float ahead = (float)sg2->dir * (pt->s - sg2->s);
                if (ahead > -tr_rules()->block_back && ahead < tr_rules()->block_ahead)
                    return 1;
            }
        }
    }
    return 0;
}

/*  The trains from the save: a run of consecutive records of types 10 and
 *  11 on adjacent tiles is one train, the type-10 engine its head. */
static int trains_init(RTraffic *t, const RMesh *m, const RCity *c)
{
    static int32_t  tile_of[R_MAX_THINGS];
    const RRoadNet *net = &m->railnet;
    int32_t         k, i, n = c->n_things;
    uint32_t        rng = 777u;
    if (n > R_MAX_THINGS)
        n = R_MAX_THINGS;
    for (i = 0; i < n; ++i)
        tile_of[i] = -1;
    for (k = 0; k < R_MAP * R_MAP; ++k)
    {
        int32_t v = c->xtxt[k];
        if (v >= 0xC9 && v <= 0xF0 && v - 0xC9 < n)
            tile_of[v - 0xC9] = k;
    }
    for (i = 0; i < n; ++i)
    {
        const uint8_t *rec = c->xthg + (size_t)i * 12u;
        int32_t        j, head = i, cars = 0;
        RTrain        *tr;
        float          hx, hy, sx, sy;
        int32_t        seg;
        float          s, dx, dy;
        if ((rec[0] != 10 && rec[0] != 11) || tile_of[i] < 0)
            continue;
        /* the run: adjacent tiles, consecutive indices */
        for (j = i; j < n; ++j)
        {
            const uint8_t *r2 = c->xthg + (size_t)j * 12u;
            if ((r2[0] != 10 && r2[0] != 11) || tile_of[j] < 0)
                break;
            if (j > i)
            {
                int32_t a = tile_of[j - 1], b = tile_of[j];
                if (abs((int)(a % R_MAP) - (int)(b % R_MAP)) + abs((int)(a / R_MAP) - (int)(b / R_MAP)) != 1)
                    break;
            }
            if (r2[0] == 10)
                head = j;
            ++cars;
        }
        if (cars > 32)
            cars = 32;
        {
            const uint8_t     *hr    = c->xthg + (size_t)head * 12u;
            static const float HX[8] = {0.0f, 0.707f, 1.0f, 0.707f, 0.0f, -0.707f, -1.0f, -0.707f};
            static const float HY[8] = {-1.0f, -0.707f, 0.0f, 0.707f, 1.0f, 0.707f, 0.0f, -0.707f};
            hx                       = HX[hr[1] & 7];
            hy                       = HY[hr[1] & 7];
            sx                       = (float)(tile_of[head] % R_MAP) + 0.5f;
            sy                       = (float)(tile_of[head] / R_MAP) + 0.5f;
        }
        if (railnet_nearest(net, sx, sy, &seg, &s, &dx, &dy))
        {
            RTrain *nt = (RTrain *)realloc(t->trains, (t->n_trains + 1u) * sizeof *nt);
            int     q;
            if (!nt)
                return -1;
            t->trains = nt;
            tr        = &t->trains[t->n_trains++];
            memset(tr, 0, sizeof *tr);
            tr->seg    = seg;
            tr->s      = s;
            tr->dir    = (hx * dx + hy * dy) >= 0.0f ? 1 : -1;
            tr->speed  = tr_rules()->train_speed + tr_rules()->train_spread * car_frand(&rng); /* tiles a second */
            tr->n_cars = cars;
            tr->d      = 0.0f;
            tr->rng    = rng;
            for (q = 0; q < cars; ++q)
                tr->paint[q] = q == 0 ? 0.0f : (float)(1 + (int)(car_rand(&rng) % 3u));
            if (train_prefill(tr, net) != 0)
                return -1;
        }
        i += cars - 1;
    }
    t->gate = (float *)calloc(m->n_xings ? m->n_xings : 1u, sizeof *t->gate);
    /*  Each crossing's place on the road network, the segment through its
     *  tile and the distance along it, so the cars can stop at its line. */
    t->xseg = (int32_t *)malloc((m->n_xings ? m->n_xings : 1u) * sizeof *t->xseg);
    t->xs   = (float *)malloc((m->n_xings ? m->n_xings : 1u) * sizeof *t->xs);
    if (t->xseg && t->xs)
    {
        uint32_t xi, xk, xj;
        for (xi = 0; xi < m->n_xings; ++xi)
        {
            const RXing *x    = &m->xings[xi];
            float        best = tr_rules()->xing_find; /* within half a tile of the crossing's centre */
            t->xseg[xi]       = -1;
            t->xs[xi]         = 0.0f;
            for (xk = 0; xk < m->net.n_segs; ++xk)
            {
                const RNetSeg *sg = &m->net.segs[xk];
                for (xj = 0; xj < sg->count; ++xj)
                {
                    const RNetPt *q  = &m->net.pts[sg->first + xj];
                    float         dx = q->x - ((float)x->col + 0.5f), dy = q->y - ((float)x->row + 0.5f), d = dx * dx + dy * dy;
                    if (d < best)
                    {
                        best        = d;
                        t->xseg[xi] = (int32_t)xk;
                        t->xs[xi]   = q->s;
                    }
                }
            }
        }
    }
    if (!t->gate)
        return -1;
    for (k = 0; k < (int32_t)m->n_xings; ++k)
        /*  A gate starts wherever it settles with no train anywhere
         *  near: the rule that swings it says where that is. */
        t->gate[k] = script_rule_gate(0.0f, 1e9f, 1000.0f);
    return 0;
}

/*  Across the junction tile onto the next arm: the entry point at this
 *  arm's end, the path point at the box's far side, and the curve
 *  through the corner between them (eight points) or the straight run,
 *  every point on the trail; the train's distance grows by the run to
 *  the end plus the box, which is what the frame has spent. */
static void train_cross_box(const RRoadNet *net, RTrain *tr, const RNetSeg *sg, float to_end, int nseg, int ndir)
{
    RTrain probe = *tr;
    float  x1, y1, z1, h1x, h1y, ex, ey, ez, ehx, ehy, ccx, ccy;
    /* the entry point: this arm's end */
    probe.s = tr->dir > 0 ? sg->total : 0.0f;
    train_place(net, &probe, &ex, &ey, &ez, &ehx, &ehy);
    probe.seg = nseg;
    probe.dir = ndir;
    probe.s   = ndir > 0 ? 0.0f : net->segs[nseg].total;
    train_place(net, &probe, &x1, &y1, &z1, &h1x, &h1y);
    tr->d += to_end;
    trail_push(tr, ex, ey, ez, ehx, ehy, tr->d, tr->seg, tr->dir > 0 ? sg->total : 0.0f, tr->dir);
    tr->seg = nseg;
    tr->dir = ndir;
    tr->s   = probe.s;
    if (box_curve(ex, ey, ehx, ehy, x1, y1, h1x, h1y, &ccx, &ccy))
    {
        /* a turn: the curve through the corner, eight points */
        float px = ex, py = ey;
        int   k;
        for (k = 1; k <= 8; ++k)
        {
            float qx, qy, qdx, qdy, ql, tt = (float)k / 8.0f;
            box_point(ex, ey, ccx, ccy, x1, y1, tt, &qx, &qy, &qdx, &qdy);
            ql = sqrtf(qdx * qdx + qdy * qdy);
            if (ql > 1e-6f)
            {
                qdx /= ql;
                qdy /= ql;
            }
            tr->d += sqrtf((qx - px) * (qx - px) + (qy - py) * (qy - py));
            trail_push(tr, qx, qy, ez + (z1 - ez) * tt, qdx, qdy, tr->d, tr->seg, tr->s, tr->dir);
            px = qx;
            py = qy;
        }
    }
    else
    {
        tr->d += sqrtf((x1 - ex) * (x1 - ex) + (y1 - ey) * (y1 - ey));
        trail_push(tr, x1, y1, z1, h1x, h1y, tr->d, tr->seg, tr->s, tr->dir);
    }
    /*  tr->d has grown by the run to the end plus the box:
     *  that is exactly what this frame has spent. */
}

/*  A dead end or a carrier: the train reverses, its tail the new head.  The
 *  cars' places are read off the trail, the trail rebuilt from the old tail
 *  first with the headings turned, and the new head's place on the track
 *  found afresh. */
static void train_reverse(const RRoadNet *net, RTrain *tr)
{
    float px[32], py[32], pz[32], phx[32], phy[32];
    int   q, nc = tr->n_cars;
    for (q = 0; q < nc; ++q)
        train_car_at(tr, (float)q * TRAIN_PITCH, &px[q], &py[q], &pz[q], &phx[q], &phy[q]);
    tr->trail_n = tr->trail_head = 0;
    tr->d                        = 0.0f;
    for (q = 0; q < nc; ++q)
    {
        int r = nc - 1 - q; /* the old tail first, the old head last */
        trail_push(tr, px[r], py[r], pz[r], -phx[r], -phy[r], (float)q * TRAIN_PITCH, tr->seg, tr->s, -tr->dir);
    }
    tr->d = (float)(nc - 1) * TRAIN_PITCH;
    /* the new head: the old tail's place on the track, found afresh */
    {
        int32_t seg2;
        float   s2, dx2, dy2;
        if (railnet_nearest(net, px[nc - 1], py[nc - 1], &seg2, &s2, &dx2, &dy2))
        {
            tr->seg = seg2;
            tr->s   = s2;
            tr->dir = (-phx[nc - 1] * dx2 - phy[nc - 1] * dy2) >= 0.0f ? 1 : -1;
        }
        else
            tr->dir = -tr->dir;
    }
}

static void trains_step(RTraffic *t, const RMesh *m, float dt)
{
    const RRoadNet *net = &m->railnet;
    uint32_t        i;
    for (i = 0; i < t->n_trains; ++i)
    {
        RTrain        *tr = &t->trains[i];
        const RNetSeg *sg;
        float          step = tr->speed * dt, x, y, z, hx, hy, to_end;
        int            end, hops = 0;
        float          d0;
        if (tr->seg < 0 || (uint32_t)tr->seg >= net->n_segs)
            continue;
        /*  A frame's travel is spent piece by piece, not all on the segment
         *  the train started on.  Reaching a junction must not `continue` --
         *  that lands on the NEXT TRAIN, crossing the box whole in one
         *  frame and throwing away what is left of the step, which at a T
         *  reads as the train jumping the junction.  The loop keeps going
         *  with what is left, and the box costs its own arc length like
         *  any other stretch of track.  The hop count is a
         *  guard: a train cannot cross more than a few junctions in one
         *  frame unless the network is degenerate, and spinning here would
         *  hang the renderer. */
        while (step > 0.0f && hops++ < 8)
        {
            sg     = &net->segs[tr->seg];
            end    = tr->dir > 0 ? 1 : 0;
            to_end = tr->dir > 0 ? sg->total - tr->s : tr->s;
            if (step >= to_end && step > 0.0f)
            {
                int nseg, ndir;
                d0 = tr->d; /* what the crossing costs, measured after */
                train_place(net, tr, &x, &y, &z, &hx, &hy);
                if (sg->kind[end] == 2 && train_turn(net, sg->node[end][0], sg->node[end][1], tr->seg, hx, hy, &nseg, &ndir))
                {
                    train_cross_box(net, tr, sg, to_end, nseg, ndir);
                    step -= tr->d - d0; /* the run to the end plus the box: what this frame has spent */
                    continue;
                }
                train_reverse(net, tr);
                step = 0.0f; /* reversing ends the frame */
                continue;
            }
            tr->s += (float)tr->dir * step;
            tr->d += step;
            train_place(net, tr, &x, &y, &z, &hx, &hy);
            if (tr->trail_n == 0 || tr->d - trail_at(tr, 0)->d >= tr_rules()->trail_step)
                trail_push(tr, x, y, z, hx, hy, tr->d, tr->seg, tr->s, tr->dir);
            step = 0.0f;
        }
    }
}

/*  How near the nearest train car is to a crossing along the rail's
 *  axis, in tiles, counting both tracks and answering a long way off
 *  when none is on the approach at all.  `wide` is how far off that axis
 *  a car still stands on the approach.  What the gate then does about it
 *  is arc.rules.gate's (spec 3.15). */
static float xing_near(const RTraffic *t, const RXing *x, float wide)
{
    float    cx = (float)x->col + 0.5f, cy = (float)x->row + 0.5f, best = 1e9f;
    uint32_t i;
    for (i = 0; i < t->n_trains; ++i)
    {
        const RTrain *tr = &t->trains[i];
        int           q;
        for (q = 0; q < tr->n_cars; ++q)
        {
            float px, py, pz, hx, hy, along, off;
            train_car_at(tr, (float)q * TRAIN_PITCH, &px, &py, &pz, &hx, &hy);
            along = x->ns ? fabsf(py - cy) : fabsf(px - cx);
            off   = x->ns ? fabsf(px - cx) : fabsf(py - cy);
            if (off < wide && along < best)
                best = along;
        }
    }
    return best;
}

static void gates_step(RTraffic *t, const RMesh *m, float dt)
{
    uint32_t i;
    if (!t->gate)
        return;
    for (i = 0; i < m->n_xings; ++i)
        t->gate[i] = script_rule_gate(t->gate[i], xing_near(t, &m->xings[i], tr_rules()->gate_watch), dt);
}

int traffic_init(RTraffic *t, const RMesh *m, const RCity *c)
{
    const RRoadNet *net = &m->net;
    uint32_t        i, total_cap = 2500u;
    uint32_t        rng = 12345u;
    traffic_free(t);
    for (i = 0; i < net->n_segs; ++i)
    {
        const RNetSeg *sg = &net->segs[i];
        int            n1 = 0, n2 = 0, k, cars;
        int32_t        lc = -1, lr = -1;
        uint32_t       j;
        /* the original's density: a tile whose traffic passes 0x55 carries a car, past 0xAA two */
        for (j = 0; j < sg->count; ++j)
        {
            const RNetPt *q  = &net->pts[sg->first + j];
            int32_t       tc = (int32_t)floorf(q->x), tr = (int32_t)floorf(q->y);
            int32_t       tv;
            if (tc < 0 || tr < 0 || tc >= R_MAP || tr >= R_MAP || (tc == lc && tr == lr))
                continue;
            lc = tc;
            lr = tr;
            tv = c->xtrf[(tr >> 1) * R_HALF + (tc >> 1)];
            if (tv > 0x55)
                ++n1;
            if (tv > 0xAA)
                ++n2;
        }
        cars = n1 + n2;
        if (cars > (int)(sg->total * tr_rules()->density) + 1)
            cars = (int)(sg->total * tr_rules()->density) + 1;
        for (k = 0; k < cars && t->n < total_cap; ++k)
        {
            RCar *car;
            if (t->n + 1u > t->cap)
            {
                uint32_t nc = t->cap ? t->cap * 2u : 512u;
                RCar    *nl = (RCar *)realloc(t->cars, nc * sizeof *nl);
                if (!nl)
                    return -1;
                t->cars = nl;
                t->cap  = nc;
            }
            car = &t->cars[t->n++];
            memset(car, 0, sizeof *car);
            car->seg   = (int32_t)i;
            car->s     = car_frand(&rng) * sg->total;
            car->dir   = (k & 1) ? -1 : 1;
            car->lane  = (sg->cls > 0 && (k % 3) == 2) ? 1 : 0;
            car->speed = 0.8f + 0.5f * car_frand(&rng); /* tiles a second */
            car->paint = 4.0f + (float)(car_rand(&rng) % 5u);
            car->rng   = rng ^ ((uint32_t)k * 2654435761u);
        }
    }
    return trains_init(t, m, c);
}

void traffic_free(RTraffic *t)
{
    uint32_t i;
    free(t->cars);
    for (i = 0; i < t->n_trains; ++i)
        free(t->trains[i].trail);
    free(t->trains);
    free(t->gate);
    free(t->xseg);
    free(t->xs);
    mesh_free(&t->scratch);
    t->cars   = NULL;
    t->trains = NULL;
    t->gate   = NULL;
    t->xseg   = NULL;
    t->xs     = NULL;
    t->n = t->cap = t->n_trains = 0;
}

static int car_order_cmp(const void *a, const void *b)
{
    const RCar *x = (const RCar *)a, *y = (const RCar *)b;
    if (x->seg != y->seg)
        return x->seg < y->seg ? -1 : 1;
    if (x->dir != y->dir)
        return x->dir < y->dir ? -1 : 1;
    if (x->lane != y->lane)
        return x->lane < y->lane ? -1 : 1;
    return x->s < y->s ? -1 : x->s > y->s ? 1
                                          : 0;
}

/*  Onward from a junction: a random arm at the node other than the one
 *  arrived by, unless it is the only one. */
static int car_turn(const RRoadNet *net, RCar *car, int32_t nc, int32_t nr, int from_seg, int *out_seg, int *out_dir)
{
    int      cand[16], cdir[16], n = 0, pick;
    uint32_t i;
    for (i = 0; i < net->n_segs && n < 16; ++i)
    {
        const RNetSeg *sg = &net->segs[i];
        if ((int)i == from_seg && net->n_segs > 1u)
            continue;
        if (sg->node[0][0] == nc && sg->node[0][1] == nr && sg->kind[0] == 2)
        {
            cand[n] = (int)i;
            cdir[n] = 1;
            ++n;
        }
        else if (sg->node[1][0] == nc && sg->node[1][1] == nr && sg->kind[1] == 2)
        {
            cand[n] = (int)i;
            cdir[n] = -1;
            ++n;
        }
    }
    if (!n)
        return 0;
    pick     = (int)(car_rand(&car->rng) % (uint32_t)n);
    *out_seg = cand[pick];
    *out_dir = cdir[pick];
    return 1;
}

static int s_xing_held;

/*  The car ahead in the same lane -- the next in the sorted order
 *  forward, the previous back -- and the speed it leaves this one. */
static float car_speed_behind(const RTraffic *t, uint32_t i, const RCar *car, float v)
{
    uint32_t j = car->dir > 0 ? i + 1u : i - 1u;
    if ((car->dir > 0 ? i + 1u < t->n : i > 0))
    {
        const RCar *o = &t->cars[j];
        if (o->seg == car->seg && o->dir == car->dir && o->lane == car->lane && !o->in_box)
        {
            v = script_rule_car_follow(fabsf(o->s - car->s), v, tr_rules()->gap_stop, tr_rules()->gap_free);
        }
    }
    return v;
}

/*  The control at the junction ahead (spec 3.4): a signal holds the car
 *  at the line while it is not green; a stop sign holds it a second,
 *  then it goes.  The speed that leaves. */
static float car_speed_control(const RRoadNet *net, RCar *car, const RNetSeg *sg, int end, float to_end, float v, float dt, float time)
{
    float x, y, z, hx, hy;
    if (sg->kind[end] == 2 && sg->ctrl[end] && to_end <= tr_rules()->stop_junc)
    {
        int hold = 0;
        if (sg->ctrl[end] == 2)
        {
            car_place(net, car, &x, &y, &z, &hx, &hy);
            hold = signal_red(sg->node[end][0], sg->node[end][1], hx, hy, time);
        }
        else if (car->hold < 1.0f)
        {
            hold = 1;
            if (to_end <= tr_rules()->stop_hold)
                car->hold += dt;
        }
        v = script_rule_car_hold(to_end, hold, tr_rules()->stop_hold, v, dt);
    }
    return v;
}

/*  A level crossing ahead on this segment (spec 3.15): while its
 *  flashers are on the car stops with its front at the stop line, 4.5 m
 *  before the nearest rail.  The speed that leaves. */
static float car_speed_crossing(const RTraffic *t, const RMesh *m, const RCar *car, float v, float dt)
{
    if (t->xseg && t->xs && t->gate)
    {
        uint32_t q;
        for (q = 0; q < m->n_xings; ++q)
        {
            float ahead;
            if (t->xseg[q] != car->seg || t->gate[q] >= tr_rules()->gate_up - 0.5f)
                continue;
            ahead = (t->xs[q] - (float)car->dir * tr_rules()->car_len - car->s) * (float)car->dir;
            if (ahead < -tr_rules()->creep || ahead > tr_rules()->gap_free)
                continue;
            if (ahead <= tr_rules()->creep)
                ++s_xing_held; /* --xing-debug counts these per step */
            v = script_rule_car_hold(ahead, 1, tr_rules()->creep, v, dt);
        }
    }
    return v;
}

/*  At a junction: the next segment the car takes and the box it crosses
 *  to reach it, a curve from where this segment ends to where the next
 *  begins.  1 when the car has entered the box. */
static int car_enter_box(const RRoadNet *net, RCar *car, const RNetSeg *sg, int end)
{
    float hx, hy;
    int   nseg, ndir;
    if (car_turn(net, car, sg->node[end][0], sg->node[end][1], car->seg, &nseg, &ndir))
    {
        const RNetSeg *ng    = &net->segs[nseg];
        RCar           probe = *car;
        float          x1, y1, z1, h1x, h1y;
        car->s = car->dir > 0 ? sg->total : 0.0f;
        car_place(net, car, &car->bx0, &car->by0, &car->bz0, &hx, &hy);
        probe.seg = nseg;
        probe.dir = ndir;
        probe.s   = ndir > 0 ? 0.0f : ng->total;
        car_place(net, &probe, &x1, &y1, &z1, &h1x, &h1y);
        car->bx1      = x1;
        car->by1      = y1;
        car->bz1      = z1;
        car->bcurve   = box_curve(car->bx0, car->by0, hx, hy, x1, y1, h1x, h1y, &car->bcx, &car->bcy);
        car->blen     = car->bcurve ? box_length(car->bx0, car->by0, car->bcx, car->bcy, x1, y1)
                                    : sqrtf((x1 - car->bx0) * (x1 - car->bx0) + (y1 - car->by0) * (y1 - car->by0));
        car->bt       = 0.0f;
        car->in_box   = 1;
        car->next_seg = nseg;
        car->next_dir = ndir;
        if (car->blen < 1e-4f)
            car->blen = 1e-4f;
        return 1;
    }
    return 0;
}

void traffic_step(RTraffic *t, const RMesh *m, float dt, float time)
{
    const RRoadNet *net = &m->net;
    uint32_t        i;
    if (dt > tr_rules()->step_max)
        dt = tr_rules()->step_max;
    trains_step(t, m, dt);
    gates_step(t, m, dt);
    if (g_dev.xing_debug)
    {
        /* --xing-debug 1: per step, the crossings with their gates off rest and the cars held at a line */
        uint32_t q, down = 0;
        for (q = 0; q < m->n_xings && t->gate; ++q)
            if (t->gate[q] < tr_rules()->gate_up - 0.5f)
            {
                if (down < 4)
                    dumpf("xing down: c%d r%d angle %.0f\n", (int)m->xings[q].col, (int)m->xings[q].row, (double)t->gate[q]);
                ++down;
            }
        dumpf("xing: t %.2f gates down %u cars held %d\n", (double)time, down, s_xing_held);
    }
    s_xing_held = 0;
    if (!t->n || !net->n_segs)
        return;
    qsort(t->cars, t->n, sizeof *t->cars, car_order_cmp);
    for (i = 0; i < t->n; ++i)
    {
        RCar          *car = &t->cars[i];
        const RNetSeg *sg;
        float          to_end, v = car->speed;
        int            end;
        if (car->in_box)
        {
            car->bt += v * dt;
            if (car->bt >= car->blen)
            {
                car->in_box = 0;
                car->hold   = 0.0f;
                car->seg    = car->next_seg;
                car->dir    = car->next_dir;
                sg          = &net->segs[car->seg];
                car->s      = car->dir > 0 ? 0.0f : sg->total;
            }
            continue;
        }
        if (car->seg < 0 || (uint32_t)car->seg >= net->n_segs)
            continue;
        sg     = &net->segs[car->seg];
        end    = car->dir > 0 ? 1 : 0;
        to_end = car->dir > 0 ? sg->total - car->s : car->s;
        v      = car_speed_behind(t, i, car, v);
        v      = car_speed_control(net, car, sg, end, to_end, v, dt, time);
        v      = car_speed_crossing(t, m, car, v, dt);
        car->s += (float)car->dir * v * dt;
        to_end = car->dir > 0 ? sg->total - car->s : car->s;
        if (to_end <= 0.0f)
        {
            if (sg->kind[end] == 2 && car_enter_box(net, car, sg, end))
                continue;
            /* a dead end, the map's edge, a bridge or a tunnel: turn back */
            car->hold = 0.0f;
            car->dir  = -car->dir;
            car->s    = car->dir > 0 ? 0.0f : sg->total;
        }
    }
}

/*  The cars as geometry (spec 6, a car 4.5 by 1.8 m, 1.5 m tall, at the
 *  road's scale of about seventeen metres to the tile): a body and a
 *  cabin set back on it, painted by the car. */
int traffic_build(RTraffic *t, const RMesh *m, const RCity *c)
{
    const RRoadNet *net      = &m->net;
    const uint8_t   mask_bit = city_corner_mask(c->rotation);
    uint32_t        i;
    t->scratch.n_land  = 0;
    t->scratch.n_water = 0;
    for (i = 0; i < t->n; ++i)
    {
        const RCar *car = &t->cars[i];
        float       x, y, z, hx, hy, order;
        int32_t     tc, tr;
        if (car->in_box)
        {
            float f = car->blen > 0.0f ? car->bt / car->blen : 1.0f;
            if (f > 1.0f)
                f = 1.0f;
            if (car->bcurve)
                box_point(car->bx0, car->by0, car->bcx, car->bcy, car->bx1, car->by1, f, &x, &y, &hx, &hy);
            else
            {
                x  = car->bx0 + (car->bx1 - car->bx0) * f;
                y  = car->by0 + (car->by1 - car->by0) * f;
                hx = car->bx1 - car->bx0;
                hy = car->by1 - car->by0;
            }
            z = car->bz0 + (car->bz1 - car->bz0) * f;
            if (car->bcurve && g_dev.box_debug)
                dumpf("box car %u f %.2f at (%.3f,%.3f) h (%.2f,%.2f) entry (%.3f,%.3f) corner (%.3f,%.3f) exit (%.3f,%.3f)\n", i, (double)f, (double)x, (double)y, (double)hx, (double)hy, (double)car->bx0, (double)car->by0, (double)car->bcx, (double)car->bcy, (double)car->bx1, (double)car->by1);
            {
                float l = sqrtf(hx * hx + hy * hy);
                if (l > 1e-5f)
                {
                    hx /= l;
                    hy /= l;
                }
                else
                {
                    hx = 1.0f;
                    hy = 0.0f;
                }
            }
        }
        else
            car_place(net, car, &x, &y, &z, &hx, &hy);
        tc = (int32_t)floorf(x);
        tr = (int32_t)floorf(y);
        if (tc < 0 || tr < 0 || tc >= R_MAP || tr >= R_MAP)
            continue;
        order = tile_order(c, tc, tr, mask_bit) + tr_rules()->slot;
        /* the grade: the band's height a car's half length behind and ahead */
        {
            float zb = z, zf = z;
            if (!car->in_box)
            {
                RCar  probe = *car;
                float px, py, pz, phx, phy;
                probe.s = car->s - (float)car->dir * tr_rules()->probe;
                car_place(net, &probe, &px, &py, &pz, &phx, &phy);
                zb      = pz;
                probe.s = car->s + (float)car->dir * tr_rules()->probe;
                car_place(net, &probe, &px, &py, &pz, &phx, &phy);
                zf = pz;
            }
            /*  The car's own shape is the model's (scripts/models.lua);
             *  where it stands and which way it points is this. */
            if (net_model_put_on(net_model_find("car"), &t->scratch, c, mask_bit, order,
                                 x, y, hx, hy, 0.0f, car->paint, 0.0f, zb, zf, 1) != 0)
                return -1;
        }
    }
    /* the trains: each car on the engine's path, coupled a tenth of a tile apart */
    for (i = 0; i < t->n_trains; ++i)
    {
        const RTrain *tr = &t->trains[i];
        int           q;
        for (q = 0; q < tr->n_cars; ++q)
        {
            /*  A rigid car on the chord between its two bogies.  The art's
             *  one-tile bends and half circles are under any radius the
             *  spec allows a class (5.4: 1.5 tiles for a yard); a car of
             *  0.85 cut the inside of such a bend and its neighbours, so
             *  the cars are short.  Set along the tangent at its centre
             *  instead a car had swung its ends out and into its
             *  neighbours. */
            float   back = (float)q * TRAIN_PITCH, x, y, hx, hy, order, l, slope, zc;
            float   bx2, by2, bz2, bhx, bhy, fx2, fy2, fz2, fhx, fhy;
            int32_t tc, tr2;
            train_car_at(tr, back + TRAIN_BOGIE, &bx2, &by2, &bz2, &bhx, &bhy);
            train_car_at(tr, back - TRAIN_BOGIE, &fx2, &fy2, &fz2, &fhx, &fhy);
            hx = fx2 - bx2;
            hy = fy2 - by2;
            l  = sqrtf(hx * hx + hy * hy);
            if (l < 1e-4f)
                continue;
            hx /= l;
            hy /= l;
            x     = 0.5f * (bx2 + fx2);
            y     = 0.5f * (by2 + fy2);
            zc    = 0.5f * (bz2 + fz2);
            slope = (fz2 - bz2) / l;
            tc    = (int32_t)floorf(x);
            tr2   = (int32_t)floorf(y);
            if (tc < 0 || tr2 < 0 || tc >= R_MAP || tr2 >= R_MAP)
                continue;
            order = tile_order(c, tc, tr2, mask_bit) + 0.3f;
            if (net_model_put_on(net_model_find(q == 0 ? "train_engine" : "train_car"),
                                 &t->scratch, c, mask_bit, order, x, y, hx, hy, 0.0f,
                                 tr->paint[q], 0.0f,
                                 zc - slope * 0.5f * tr_rules()->train_len,
                                 zc + slope * 0.5f * tr_rules()->train_len, 1) != 0)
                return -1;
        }
    }
    /* the rail signals' aspects (spec 5.6): red while any car of a train stands in the
     * block ahead of the signal in the direction it governs, ten tiles, green otherwise */
    for (i = 0; i < m->n_rsigs; ++i)
    {
        const RRailSig *sg2 = &m->rsigs[i];
        float           g   = surface_at_world(c, mask_bit, sg2->x, sg2->y);
        int32_t         tc = (int32_t)floorf(sg2->x), tr2 = (int32_t)floorf(sg2->y);
        int             red;
        if (tc < 0 || tr2 < 0 || tc >= R_MAP || tr2 >= R_MAP)
            continue;
        red = rail_block_occupied(t, sg2, -1);
        if (furniture_rail_aspect(&t->scratch, c, mask_bit, sg2, g, red) != 0)
            return -1;
    }
    /* the crossings' gates and flashers */
    for (i = 0; i < m->n_xings && t->gate; ++i)
    {
        const RXing *x     = &m->xings[i];
        float        order = tile_order(c, x->col, x->row, mask_bit) + 0.3f;
        int          a;
        for (a = 0; a < 2; ++a)
        {
            float mx, my, fx, fy;
            xing_approach(x, a, &mx, &my, &fx, &fy);
            if (furniture_gate_state(&t->scratch, c, mask_bit, order, mx, my, fx, fy, t->gate[i], road_class(c, x->col, x->row) > 0.5f ? 0.36f : 0.29f, 0.0f) != 0)
                return -1;
        }
    }
    return 0;
}
