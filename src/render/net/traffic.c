/*  traffic.c: THE MOVING WORLD'S STORES.
 *
 *  The movers a beat steps, the signals and gates they answer to, and
 *  the mesh they are drawn into.  A beat is offered to the script as a
 *  reading.  It says how far a mover may go, what stands in its way and
 *  which lap holds it.  The answer is taken back.
 *
 *  Nothing here decides any of it.  How fast a mover runs, which way it
 *  turns at a junction, what a signal shows and when a gate falls are
 *  the scripts'.  What is left in this file is the keeping and the
 *  offering. */
#include <string.h>

#include "dump.h"
#include "mesh/internal.h"
#include "pipeline.h"
#include "net/net.h"
#include "mesh/model.h"
#include "script.h"

/*  How many meets one car may be held by at once.  A car is on one
 *  segment and the meets that reach it are the ones on that segment. */
#define BEAT_LAPS 16

/*  Where a gate settles with nothing near it, as the build's drive had
 *  it answered: every gate in a new world starts there. */
static float s_gate_rest;

void net_gate_rest_is(float angle)
{
    s_gate_rest = angle;
}

static struct
{
    RTraffic    *t;
    const RMesh *m;
    const RCity *c;
    uint8_t      mask_bit;
    int          rc, owed, draw;
    float        dt, time;
    /*  The car in hand, and what it sees. */
    uint32_t     at;
    int          live;
    float        speed;
    float        gap;
    int          have_gap;
    float        ahead;
    int          held, have_ctrl;
    /*  A SIGNAL's facts, where the control at the junction ahead is one:
     *  which junction, the way the car faces it, and the clock.  Whether
     *  that reads red is arc.rules.signal's. */
    int          sig;
    int32_t      sig_col, sig_row;
    float        sig_hx, sig_hy;
    float        xa[BEAT_LAPS];
    int          nx;
    /*  The arms at the junction the car in hand is about to reach, for
     *  the rule that chooses between them.  How many, the draw the world
     *  made for it, and each arm's heading away from the node. */
    int          turn_n, have_turn;
    uint32_t     turn_draw;
    int          turn_cand[16], turn_cdir[16];
    float        turn_dx[16], turn_dy[16];
} s_beat;
#include "opt.h"
#include "project.h"

/*  How the moving world behaves, asked of the script once and kept until
 *  the scripts are read again.  A frame steps thousands of things and
 *  none of them is worth a call of its own. */
static int gix_gate_arm = -1, gix_gate_arm_wide = -1;

static const ScriptTraffic *tr_rules(void)
{
    /*  The names are the struct's own order, so the two read as one
     *  list.  A name the script leaves out keeps the default set below. */
    static const char *const KEYS[] = {
        "blink",      "train_speed", "train_spread", "train_len",  "trail_step",
        "lap_find",  "gate_up",     "gate_watch",   "density",    "car_len",
        "gap_stop",   "gap_free",    "stop_junc",    "stop_hold",  "creep",
        "probe",      "step_max",    "slot",         "block_back", "block_ahead"};
    static ScriptTraffic s_tr;
    static int           s_gen = -1;
    if (s_gen != script_generation())
    {
        memset(&s_tr, 0, sizeof s_tr);
        s_tr.gap_free = s_tr.step_max = s_tr.trail_step = 1.0f;
        script_numbers("traffic", KEYS, &s_tr.blink,
                       (int)(sizeof KEYS / sizeof KEYS[0]));
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
static void net_at(const RNet *net, const RNetSeg *sg, float s, float *x, float *y, float *z, float *dx, float *dy)
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

/*  A car's place on the line.
 *
 *      The band at s offset into its lane.
 *      To the right of its direction of travel AS SEEN.
 *      Its heading.
 *
 *  The map's frame (col east, row south) reaches the screen through a
 *  reflection.  Col runs down-left, row down-right: so the map's
 *  right-hand normal (-dy, dx) is the viewer's LEFT.  (dy, -dx) is the
 *  viewer's right. */
static void car_place(const RNet *net, const RCar *car, float *x, float *y, float *z, float *hx, float *hy)
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


/*  The meet's approach a (0 or 1): the mast at the driver's right
 *  and the direction of travel, as the mesh placed the mast. */
static void lap_approach(const RLap *x, int a, float *mx, float *my, float *fx, float *fy)
{
    float h = LINE_W * 0.5f + 0.08f, rh = THREAD_W * 0.5f + 0.10f, cx = (float)x->col + 0.5f, cy = (float)x->row + 0.5f;
    *fx = x->ns ? (a ? -1.0f : 1.0f) : 0.0f;
    *fy = x->ns ? 0.0f : (a ? -1.0f : 1.0f);
    *mx = cx - *fx * rh + (*fy) * h;
    *my = cy - *fy * rh + (-*fx) * h;
}

/*  Append a point to a train's path ring. */
/*  A train car: 0.42 of a tile long, 0.10 wide, coupled 0.48 apart along
 *  the path, its bogies 0.14 each side of its center. */

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

/*  The engine's place on the thread: the band at s offset onto the thread
 *  to the right of its direction of travel. */
static void train_place(const RNet *net, const RTrain *tr, float *x, float *y, float *z, float *hx, float *hy)
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

static struct
{
    uint32_t i;
    float    step;
    int      hops;
    int      live;
} s_tstep;


/*  THE ARMS AT A THREAD NODE, gathered for the rule that chooses between
 *  them.
 *
 *      Every arm but the one the train arrived by.
 *      Each with the way it heads AWAY from the node.
 *      The heading the train arrived on.
 *
 *  Answers 0 where the node has no arm to take. */
static struct
{
    int   n;
    int   cand[16], cdir[16];
    float dx[16], dy[16];
    float hx, hy;
} s_tarms;

static int train_arms(const RNet *net, int32_t nc, int32_t nr, int from_seg, float hx, float hy)
{
    uint32_t i;
    s_tarms.n  = 0;
    s_tarms.hx = hx, s_tarms.hy = hy;
    for (i = 0; i < net->n_segs; ++i)
    {
        const RNetSeg *sg = &net->segs[i];
        int            e;
        for (e = 0; e < 2; ++e)
        {
            const RNetPt *q;
            if (sg->node[e][0] != nc || sg->node[e][1] != nr)
                continue;
            if ((int)i == from_seg && net->n_segs > 1u)
                continue;
            if (s_tarms.n >= 16)
                break;
            q                    = &net->pts[sg->first + (e == 0 ? 0u : sg->count - 1u)];
            s_tarms.dx[s_tarms.n] = e == 0 ? q->dx : -q->dx;
            s_tarms.dy[s_tarms.n] = e == 0 ? q->dy : -q->dy;
            s_tarms.cand[s_tarms.n] = (int)i;
            s_tarms.cdir[s_tarms.n] = e == 0 ? 1 : -1;
            ++s_tarms.n;
        }
    }
    return s_tarms.n > 0;
}

/*  What the rule reads, and the arm it chose. */
int net_beat_arms(int *n, float *hx, float *hy)
{
    if (!s_tarms.n)
        return 0;
    *n = s_tarms.n, *hx = s_tarms.hx, *hy = s_tarms.hy;
    return 1;
}

int net_beat_arm_at(int k, float *dx, float *dy)
{
    if (k < 0 || k >= s_tarms.n)
        return 0;
    *dx = s_tarms.dx[k], *dy = s_tarms.dy[k];
    return 1;
}

void net_beat_arm_is(int k)
{
    RTraffic *t = s_beat.t;
    RTrain   *tr;
    if (!t || !s_tstep.live || s_tstep.i >= t->n_trains || k < 0 || k >= s_tarms.n)
        return;
    tr             = &t->trains[s_tstep.i];
    tr->turn_seg   = s_tarms.cand[k];
    tr->turn_dir   = s_tarms.cdir[k];
    tr->turn_have  = 1;
}

/*  Onward from a thread node: the arm arc.rules.train_turn chose when
 *  the train reached it.  Nothing decides it here.  Take the rule away
 *  and a train reaching a junction runs on to the end of its own thread
 *  and reverses there.  This is what a terminus does anyway. */
static int train_turn(RTrain *tr, int *out_seg, int *out_dir)
{
    if (!tr->turn_have)
        return 0;
    *out_seg      = tr->turn_seg;
    *out_dir      = tr->turn_dir;
    tr->turn_have = 0;
    return 1;
}

/*  The path across a junction box, from the entry point heading (hx, hy)
 *  to the exit point heading (gx, gy).  It is straight through when the
 *  headings agree.  Otherwise it is a quadratic curve.  It is tangent to
 *  both arms through the corner where their lines meet.  That is the
 *  junction's own curve.  So a turning vehicle bends through the box as
 *  the T's threads and the line's lip returns do.  It does not cutting
 *  the corner on a chord.  Returns 1 and the corner for a turn. */
static int box_curve(float ex, float ey, float hx, float hy, float xx, float xy, float gx, float gy, float *cx, float *cy)
{
    static int gix_box_straight = -1;
    V2         c;
    /*  How nearly the two arms must agree before the meet counts as
     *  straight through rather than a turn is the SCRIPT'S
     *  (arc.geo.box_straight_dot).  It is a property of what reads as a
     *  turn, not of this arithmetic. */
    if (hx * gx + hy * gy > geo_num(&gix_box_straight, "box_straight_dot"))
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


/*  The nearest station of the thread net to a point: its segment and s. */
static int threadnet_nearest(const RNet *net, float x, float y, int32_t *seg, float *s, float *dx, float *dy)
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
static int train_prefill(RTrain *tr, const RNet *net)
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

/*  HOW NEAR THE NEAREST TRAIN CAR IS to a signal, along the thread it
 *  governs.
 *
 *      Ahead of it in the direction it faces.
 *      Behind it.
 *      In tiles.
 *
 *  A long way off where there is none either way.
 *
 *  What the signal then does about it is arc.rules.thread_signal's: the
 *  same division the level meet's gate is under (lap_near and
 *  arc.rules.gate).  Measuring is a reading of where the trains are.
 *  How much of the block has to be clear is not. */
/*  The aspect each thread signal was given, kept for the build. */
#define RSIG_MAX 4096
/*  The MODEL each signal shows, as arc.rules.thread_signal named it,
 *  interned so a per-frame read is an index and not a string.  Slot 0
 *  is the empty name: a signal the rule answered nothing for shows
 *  nothing. */
static uint8_t s_rsig_aspect[RSIG_MAX];
static char    s_aspect_name[8][24];
static int     s_n_aspect = 1;

static void thread_block_near(const RTraffic *t, const RSignal *sg2, int skip, float *ahead, float *back)
{
    uint32_t k;
    *ahead = *back = 1e9f;
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
                float d = (float)sg2->dir * (pt->s - sg2->s);
                if (d >= 0.0f)
                {
                    if (d < *ahead)
                        *ahead = d;
                }
                else if (-d < *back)
                    *back = -d;
            }
        }
    }
}

/*  The trains from the save.  A run of consecutive records of types 10
 *  and 11 on adjacent tiles is one train.  The type-10 engine its head. */
static int trains_init(RTraffic *t, const RMesh *m, const RCity *c)
{
    static int32_t  tile_of[R_MAX_THINGS];
    const RNet *net = &m->threadnet;
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
        if (threadnet_nearest(net, sx, sy, &seg, &s, &dx, &dy))
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
    t->gate = (float *)calloc(m->n_laps ? m->n_laps : 1u, sizeof *t->gate);
    /*  Each meet's place on the line network, the segment through its
     *  tile and the distance along it, so the cars can stop at its line. */
    t->xseg = (int32_t *)malloc((m->n_laps ? m->n_laps : 1u) * sizeof *t->xseg);
    t->xs   = (float *)malloc((m->n_laps ? m->n_laps : 1u) * sizeof *t->xs);
    if (t->xseg && t->xs)
    {
        uint32_t xi, xk, xj;
        for (xi = 0; xi < m->n_laps; ++xi)
        {
            const RLap *x    = &m->meets[xi];
            float        best = tr_rules()->lap_find; /* within half a tile of the meet's center */
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
    for (k = 0; k < (int32_t)m->n_laps; ++k)
        /*  A gate starts wherever it settles with no train anywhere
         *  near.  The drive asks the rule that swings it for that one
         *  angle when the world is built, and it is read back here. */
        t->gate[k] = s_gate_rest;
    return 0;
}

/*  Across the junction tile onto the next arm: the entry point at this
 *  arm's end, the path point at the box's far side.  The curve through
 *  the corner between them (eight points) or the straight run, every
 *  point on the trail.  The train's distance grows by the run to the end
 *  plus the box, which is what the frame has spent. */
static void train_cross_box(const RNet *net, RTrain *tr, const RNetSeg *sg, float to_end, int nseg, int ndir)
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

/*  A dead end or a carrier: the train reverses, its tail the new head.
 *  The cars' places are read off the trail.  The trail is rebuilt from
 *  the old tail first, with the headings turned, and the new head's
 *  place on the thread found afresh. */
static void train_reverse(const RNet *net, RTrain *tr)
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
    /* the new head: the old tail's place on the thread, found afresh */
    {
        int32_t seg2;
        float   s2, dx2, dy2;
        if (threadnet_nearest(net, px[nc - 1], py[nc - 1], &seg2, &s2, &dx2, &dy2))
        {
            tr->seg = seg2;
            tr->s   = s2;
            tr->dir = (-phx[nc - 1] * dx2 - phy[nc - 1] * dy2) >= 0.0f ? 1 : -1;
        }
        else
            tr->dir = -tr->dir;
    }
}

/*  ---- THE BEAT, STOPPED AT EACH JUNCTION --------------------------------
 *
 *  A train's travel is spent piece by piece.  It may cross SEVERAL
 *  junctions in one beat, each arrived at on a heading of its own.  So
 *  the arm it takes cannot be settled a beat ahead, the way a car's is.
 *  The step stops where the train reaches a node, arc.rules.train_turn
 *  is asked with the heading it actually arrived on.  The step carries
 *  on from where it stopped with what is left of the travel.
 *
 *  What is carried across the stop is the train it stopped on and what
 *  that train had left to spend. */
static int trains_step(RTraffic *t, const RMesh *m, float dt)
{
    const RNet *net = &m->threadnet;
    uint32_t        i   = s_tstep.live ? s_tstep.i : 0u;
    for (; i < t->n_trains; ++i)
    {
        RTrain        *tr = &t->trains[i];
        const RNetSeg *sg;
        float          x, y, z, hx, hy, to_end;
        float          step = s_tstep.live ? s_tstep.step : tr->speed * dt;
        int            end, hops = s_tstep.live ? s_tstep.hops : 0;
        float          d0;
        s_tstep.live = 0;
        if (tr->seg < 0 || (uint32_t)tr->seg >= net->n_segs)
            continue;
        /*  A frame's travel is spent piece by piece, not all on the
         *  segment the train started on.  Reaching a junction must not
         *  `continue`.  That lands on the NEXT TRAIN, meet the box whole
         *  in one frame and throwing away what is left of the step.
         *  This at a T reads as the train jumping the junction.  The
         *  loop keeps going with what is left, and the box costs its own
         *  arc length like any other stretch of thread.  The hop count
         *  is a guard: a train cannot cross more than a few junctions in
         *  one frame unless the network is degenerate.  Spinning here
         *  would hang the renderer. */
        while (step > 0.0f && hops++ < 8)
        {
            sg     = &net->segs[tr->seg];
            end    = tr->dir > 0 ? 1 : 0;
            to_end = tr->dir > 0 ? sg->total - tr->s : tr->s;
            if (step >= to_end && step > 0.0f)
            {
                int nseg, ndir;
                d0 = tr->d; /* what the meet costs, measured after */
                train_place(net, tr, &x, &y, &z, &hx, &hy);
                /*  A node: stop and let the rule choose the arm, unless it
                 *  has already answered for this meet. */
                if (sg->kind[end] == 2 && !tr->turn_have &&
                    train_arms(net, sg->node[end][0], sg->node[end][1], tr->seg, hx, hy))
                {
                    s_tstep.i    = i;
                    s_tstep.step = step;
                    s_tstep.hops = hops - 1;
                    s_tstep.live = 1;
                    return 1;
                }
                if (sg->kind[end] == 2 && train_turn(tr, &nseg, &ndir))
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
    return 0;
}

/*  How near the nearest train car is to a lap along the thread's axis,
 *  in tiles.  It counts both threads, answering a long way off when none
 *  is on the approach at all.  `wide` is how far off that axis a car
 *  still stands on the approach.  What the gate then does about it is
 *  arc.rules.gate's (spec 3.15). */
static float lap_near(const RTraffic *t, const RLap *x, float wide)
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



/*  How many cars one tile's traffic is worth, for every byte there is:
 *  arc.rules.car_density's answer, settled by the drive. */
static uint8_t s_car_density[256];

void net_car_density_is(int tv, int cars)
{
    if (tv >= 0 && tv < 256)
        s_car_density[tv] = (uint8_t)(cars < 0 ? 0 : cars > 255 ? 255 : cars);
}

int traffic_init(RTraffic *t, const RMesh *m, const RCity *c)
{
    const RNet *net = &m->net;
    uint32_t        i, total_cap = 2500u;
    uint32_t        rng = 12345u;
    traffic_free(t);
    for (i = 0; i < net->n_segs; ++i)
    {
        const RNetSeg *sg = &net->segs[i];
        int            n1 = 0, n2 = 0, k, cars;
        int32_t        lc = -1, lr = -1;
        uint32_t       j;
        /*  HOW MANY CARS a segment carries is a function of the traffic
         *  on it.  What that function is is arc.rules.car_density's:
         *  each tile of the run offers its traffic byte and the rule
         *  says how many cars it is worth.  All two hundred and
         *  fifty-six answers are settled before anything is built, so a
         *  tile costs a load.  No rule is a city with no cars in it. */
        for (j = 0; j < sg->count; ++j)
        {
            const RNetPt *q  = &net->pts[sg->first + j];
            int32_t       tc = (int32_t)floorf(q->x), tr = (int32_t)floorf(q->y);
            if (tc < 0 || tr < 0 || tc >= R_MAP || tr >= R_MAP || (tc == lc && tr == lr))
                continue;
            lc = tc;
            lr = tr;
            n1 += s_car_density[c->xtrf[(tr >> 1) * R_HALF + (tc >> 1)]];
        }
        (void)n2;
        cars = n1;
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

/*  Onward from a junction: the arm arc.rules.car_turn chose when the car
 *  was a beat away from the node.  Nothing decides it here: take the
 *  rule away and a car reaching a junction turns nowhere and stops. */
static int car_turn(const RNet *net, RCar *car, int32_t nc, int32_t nr, int from_seg, int *out_seg, int *out_dir)
{
    (void)net, (void)nc, (void)nr, (void)from_seg;
    if (!car->turn_have)
        return 0;
    *out_seg       = car->turn_seg;
    *out_dir       = car->turn_dir;
    car->turn_have = 0;
    return 1;
}

static int s_lap_held;




/*  At a junction it gives the next segment the car takes, and the box it
 *  crosses to reach it.  That is a curve from where this segment ends to
 *  where the next begins. 1 when the car has entered the box. */
static int car_enter_box(const RNet *net, RCar *car, const RNetSeg *sg, int end)
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

/*  ------------------------------------------------------------------
 *  The heartbeat
 *
 *  The world moves on a beat of its own, sixty a second, and the frame
 *  only looks at it.  A frame that takes longer than a beat runs several
 *  of them and one that takes less runs none.  What a car does between
 *  two beats is the same however often the picture is drawn.  This is
 *  the whole point: a stopping distance measured against the frame is a
 *  stopping distance that changes with the machine.
 *
 *  A stall runs at most BEATS_MAX of them and drops the rest.  Trying to
 *  catch up on a second of lost time would take longer than the second,
 *  and the world would never catch up at all.
 *  ------------------------------------------------------------------ */
#define BEAT      (1.0f / 60.0f)
#define BEATS_MAX 8

static float s_beat_owed;

/*  THE MOVING WORLD'S OWN DOOR, and the only place the world that moves
 *  enters a script.  It is entered whenever the world advances or is
 *  drawn.  The script runs the beats the clock owes, and where the frame
 *  wants it drawn, lays the geometry of everything that moves.
 *
 *  A stall owes more beats than the world will run at once.  The rest
 *  are dropped rather than owed, or a frame that took a second would
 *  spend the next one catching up. */
int traffic_moving(RTraffic *t, const RMesh *m, const RCity *c, float dt, float time, int draw)
{
    if (dt > tr_rules()->step_max)
        dt = tr_rules()->step_max;
    s_beat_owed += dt;
    s_beat.owed = 0;
    while (s_beat_owed >= BEAT && s_beat.owed < BEATS_MAX)
        s_beat_owed -= BEAT, ++s_beat.owed;
    if (s_beat_owed >= BEAT)
        s_beat_owed = 0.0f;
    s_beat.t = t, s_beat.m = m, s_beat.c = c, s_beat.dt = BEAT, s_beat.time = time;
    s_beat.mask_bit = city_corner_mask(c->rotation);
    s_beat.draw     = draw;
    s_beat.rc       = 0;
    s_beat.live     = 0;
    return net_drive_move();
}

/*  One beat of it, which the script asks for as many of as the clock
 *  owes.
 *
 *      The trains first.
 *      On their own threads.
 *      Then the gates and the cars.
 *      Which are the script's. */
/*  One beat of the moving world, or as much of it as runs before a train
 *  reaches a junction.  Answers 1 where it stopped to have an arm chosen
 *  and must be entered again once the rule has answered. */
int net_beat_run(void)
{
    return s_beat.t ? trains_step(s_beat.t, s_beat.m, s_beat.dt) : 0;
}

/*  ---- THE BEAT, as the script that drives it sees one
 *  -------------------
 *
 *  The moving world runs on its own clock.  So this is where it enters
 *  Lua: once a beat, the way the build enters once a pass.  What the
 *  beat DECIDES is the script's: how far a gate swings.  How fast a car
 *  goes for the car ahead of it.  It also answers for the control at the
 *  junction it is running towards, and for every meet on its way.  This
 *  offers the readings and moves what it is told to move.  There is no
 *  ladder in C behind it, so a build whose script drives no beat has a
 *  world that stands still.
 *
 *  The cars are read and moved ONE AT A TIME, in the order they are
 *  sorted into.  A car looks at the one ahead of it, which has already
 *  moved this beat.  So reading them all first would be reading a world
 *  that never exists. */

/*  How many gates there are to swing, and what one of them sees: the
 *  angle it stands at and how near the nearest train is. */
/*  The world that moves, for the drive to hand over. */
void *net_moving_fan(void)
{
    return s_beat.t ? (void *)&s_beat : NULL;
}

int net_beat_owed(void)
{
    return s_beat.owed;
}

int net_beat_draws(void)
{
    return s_beat.draw;
}

static int traffic_build(RTraffic *t, const RMesh *m, const RCity *c);

/*  The geometry of everything that moves, laid when the frame asks for
 *  it: the cars, the trains, the thread signals' aspects. */
int net_beat_build(void)
{
    return s_beat.t ? traffic_build(s_beat.t, s_beat.m, s_beat.c) : 0;
}

int net_beat_gates(void)
{
    return s_beat.t && s_beat.t->gate ? (int)s_beat.m->n_laps : 0;
}

int net_beat_gate(int i, float *angle, float *near, float *dt)
{
    if (i < 0 || i >= net_beat_gates())
        return 0;
    *angle = s_beat.t->gate[i];
    *near  = lap_near(s_beat.t, &s_beat.m->meets[i], tr_rules()->gate_watch);
    *dt    = s_beat.dt;
    return 1;
}

void net_beat_gate_is(int i, float angle)
{
    if (i >= 0 && i < net_beat_gates())
        s_beat.t->gate[i] = angle;
}

/*  What the gates having swung leaves: the debug line, and the cars put
 *  in the order they are read in.  Answers how many there are. */
int net_beat_cars(void)
{
    RTraffic       *t   = s_beat.t;
    const RMesh    *m   = s_beat.m;
    const RNet *net = &m->net;
    if (g_dev.lap_debug)
    {
        /* --meet-debug 1: per step, the meets with their gates off rest and the cars held at a line */
        uint32_t q, down = 0;
        for (q = 0; q < m->n_laps && t->gate; ++q)
            if (t->gate[q] < tr_rules()->gate_up - 0.5f)
            {
                if (down < 4)
                    dumpf("meet down: c%d r%d angle %.0f\n", (int)m->meets[q].col, (int)m->meets[q].row, (double)t->gate[q]);
                ++down;
            }
        dumpf("meet: t %.2f gates down %u cars held %d\n", (double)s_beat.time, down, s_lap_held);
    }
    s_lap_held = 0;
    if (!t->n || !net->n_segs)
        return 0;
    qsort(t->cars, t->n, sizeof *t->cars, car_order_cmp);
    return (int)t->n;
}

/*  What car i sees.
 *
 *      Its own speed.
 *      The gap to the car ahead in its lane.
 *      The control at the end it runs towards.
 *      Every meet on its way.
 *
 *  Answers 0 for a car with no decision to make, one meet a junction
 *  box, or off the network, which this moves itself.  The seconds a stop
 *  sign counts are part of what the car sees, so they are counted here. */
int net_beat_car(int i)
{
    RTraffic       *t   = s_beat.t;
    const RMesh    *m   = s_beat.m;
    const RNet *net = &m->net;
    const float     dt  = s_beat.dt;
    RCar           *car;
    const RNetSeg  *sg;
    float           to_end;
    int             end;
    s_beat.live = 0;
    s_beat.at   = (uint32_t)i;
    if (i < 0 || (uint32_t)i >= t->n)
        return 0;
    car = &t->cars[i];
    if (car->in_box)
    {
        car->bt += car->speed * dt;
        if (car->bt >= car->blen)
        {
            car->in_box = 0;
            car->hold   = 0.0f;
            car->seg    = car->next_seg;
            car->dir    = car->next_dir;
            sg          = &net->segs[car->seg];
            car->s      = car->dir > 0 ? 0.0f : sg->total;
        }
        return 0;
    }
    if (car->seg < 0 || (uint32_t)car->seg >= net->n_segs)
        return 0;
    sg     = &net->segs[car->seg];
    end    = car->dir > 0 ? 1 : 0;
    to_end = car->dir > 0 ? sg->total - car->s : car->s;
    s_beat.speed    = car->speed;
    s_beat.have_gap = 0;
    s_beat.have_ctrl = 0;
    s_beat.nx        = 0;
    s_beat.have_turn = 0;
    s_beat.sig       = 0;
    /*  The car ahead in the same lane: the next in the sorted order
     *  forward, the previous back. */
    {
        uint32_t j = car->dir > 0 ? (uint32_t)i + 1u : (uint32_t)i - 1u;
        if (car->dir > 0 ? (uint32_t)i + 1u < t->n : i > 0)
        {
            const RCar *o = &t->cars[j];
            if (o->seg == car->seg && o->dir == car->dir && o->lane == car->lane && !o->in_box)
            {
                s_beat.gap      = fabsf(o->s - car->s);
                s_beat.have_gap = 1;
            }
        }
    }
    /*  The control at the junction ahead (spec 3.4): a signal holds the
     *  car at the line while it is not green.  A stop sign holds it a
     *  second, then it goes. */
    if (sg->kind[end] == 2 && sg->ctrl[end] && to_end <= tr_rules()->stop_junc)
    {
        float x, y, z, hx, hy;
        int   hold = 0;
        if (sg->ctrl[end] == 2)
        {
            /*  A signal: the rule reads the clock and says whether it is
             *  red.  No answer is a signal that never stops anyone. */
            car_place(net, car, &x, &y, &z, &hx, &hy);
            s_beat.sig     = 1;
            s_beat.sig_col = sg->node[end][0];
            s_beat.sig_row = sg->node[end][1];
            s_beat.sig_hx  = hx;
            s_beat.sig_hy  = hy;
        }
        else if (car->hold < 1.0f)
        {
            hold = 1;
            if (to_end <= tr_rules()->stop_hold)
                car->hold += dt;
        }
        s_beat.ahead     = to_end;
        s_beat.held      = hold;
        s_beat.have_ctrl = 1;
    }
    /*  THE ARMS AT THE JUNCTION AHEAD, gathered once for the rule that
     *  chooses between them (arc.rules.car_turn).  It is asked on the
     *  beat the car would reach the node and not before.  So the world
     *  draws for it exactly once a turn.  And a car held at the line
     *  keeps the answer it was given rather than asking again. */
    if (sg->kind[end] == 2 && !car->turn_have && to_end <= car->speed * dt)
    {
        int32_t  nc = sg->node[end][0], nr = sg->node[end][1];
        uint32_t q;
        s_beat.turn_n = 0;
        for (q = 0; q < net->n_segs && s_beat.turn_n < 16; ++q)
        {
            const RNetSeg *o = &net->segs[q];
            int            at = -1;
            if ((int)q == car->seg && net->n_segs > 1u)
                continue;
            if (o->node[0][0] == nc && o->node[0][1] == nr && o->kind[0] == 2)
                at = 0;
            else if (o->node[1][0] == nc && o->node[1][1] == nr && o->kind[1] == 2)
                at = 1;
            if (at < 0)
                continue;
            {
                float ax, ay, az, adx, ady;
                net_at(net, o, at ? o->total : 0.0f, &ax, &ay, &az, &adx, &ady);
                s_beat.turn_dx[s_beat.turn_n] = at ? -adx : adx; /* away from the node */
                s_beat.turn_dy[s_beat.turn_n] = at ? -ady : ady;
            }
            s_beat.turn_cand[s_beat.turn_n] = (int)q;
            s_beat.turn_cdir[s_beat.turn_n] = at ? -1 : 1;
            ++s_beat.turn_n;
        }
        if (s_beat.turn_n > 0)
        {
            s_beat.turn_draw = car_rand(&car->rng);
            s_beat.have_turn = 1;
        }
    }

    /*  And every level meet ahead on this segment (spec 3.15).  While
     *  its flashers are on the car stops with its front at the stop
     *  line, 4.5 m before the nearest thread. */
    if (t->xseg && t->xs && t->gate)
    {
        uint32_t q;
        for (q = 0; q < m->n_laps; ++q)
        {
            float ahead;
            if (t->xseg[q] != car->seg || t->gate[q] >= tr_rules()->gate_up - 0.5f)
                continue;
            ahead = (t->xs[q] - (float)car->dir * tr_rules()->car_len - car->s) * (float)car->dir;
            if (ahead < -tr_rules()->creep || ahead > tr_rules()->gap_free)
                continue;
            if (ahead <= tr_rules()->creep)
                ++s_lap_held; /* --meet-debug counts these per step */
            if (s_beat.nx < BEAT_LAPS)
                s_beat.xa[s_beat.nx++] = ahead;
        }
    }
    s_beat.live = 1;
    return 1;
}

/*  THE ARMS AT THE JUNCTION the car in hand is about to reach.  Answers
 *  how many there are and the draw the world made, or 0 where this car
 *  has no turn to choose this beat. */
int net_beat_turn(int *n, unsigned *draw)
{
    if (!s_beat.live || !s_beat.have_turn)
        return 0;
    *n    = s_beat.turn_n;
    *draw = s_beat.turn_draw;
    return 1;
}

/*  One of them: the way it heads away from the node, so a rule may
 *  prefer the arm that carries straight on. */
int net_beat_turn_at(int k, float *dx, float *dy)
{
    if (!s_beat.live || !s_beat.have_turn || k < 0 || k >= s_beat.turn_n)
        return 0;
    *dx = s_beat.turn_dx[k];
    *dy = s_beat.turn_dy[k];
    return 1;
}

/*  And the arm the rule chose, kept on the car until it reaches the
 *  node.  An answer of nothing is a car that turns nowhere, which is a
 *  car that stops at the junction. */
void net_beat_turn_is(int k)
{
    RTraffic *t = s_beat.t;
    RCar     *car;
    if (!s_beat.live || !s_beat.have_turn || s_beat.at >= t->n)
        return;
    car = &t->cars[s_beat.at];
    if (k < 0 || k >= s_beat.turn_n)
        return;
    car->turn_seg  = s_beat.turn_cand[k];
    car->turn_dir  = s_beat.turn_cdir[k];
    car->turn_have = 1;
}

/*  What the car sees, and the numbers its decisions are measured
 *  against.  Those are the SCRIPT'S own, but they are handed back here
 *  rather than read again on the other side.  The world holds them as
 *  floats.  A rule given the number to more places than the world keeps
 *  answers the wrong speed.  It is a hair from the one the world would
 *  move the car by. */
int net_beat_reading(float *speed, int *have_gap, float *gap, int *have_ctrl, float *ahead, int *held, int *nx,
                     float *stop, float *free_, float *line, float *creep, float *step)
{
    if (!s_beat.live)
        return 0;
    *speed = s_beat.speed;
    *have_gap = s_beat.have_gap, *gap = s_beat.gap;
    *have_ctrl = s_beat.have_ctrl, *ahead = s_beat.ahead, *held = s_beat.held;
    *nx    = s_beat.nx;
    *stop  = tr_rules()->gap_stop;
    *free_ = tr_rules()->gap_free;
    *line  = tr_rules()->stop_hold;
    *creep = tr_rules()->creep;
    *step  = s_beat.dt;
    return 1;
}

/*  WHICH OF THE STAGGERS a junction takes.  Neighboring junctions must
 *  not all go green together, so each takes one of eight from where it
 *  stands.  This is the READING.  What phase of the cycle a stagger
 *  means is the rule's.  So is which group of arms an edge belongs to.
 *  They are the script's (arc.rules.signal_phase,
 *  arc.rules.signal_group).  Both are settled before anything is built,
 *  so the shader's signal and the rule that holds a car cannot drift
 *  apart. */
static int net_signal_stagger(int32_t col, int32_t row)
{
    return (int)((col * 7 + row * 13) % 8);
}

static float s_sig_phase[8];
static float s_sig_group[4];

void net_signal_phase_is(int k, float phase)
{
    if (k >= 0 && k < 8)
        s_sig_phase[k] = phase;
}

void net_signal_group_is(int e, float group)
{
    if (e >= 0 && e < 4)
        s_sig_group[e] = group;
}

/*  The signal at the junction the car in hand faces.  There the control
 *  there is one: which junction it is, the way the car faces it, and the
 *  clock the rule reads. */
int net_beat_signal(int32_t *col, int32_t *row, float *hx, float *hy, float *time, int *stagger)
{
    if (!s_beat.live || !s_beat.sig)
        return 0;
    *stagger = net_signal_stagger(s_beat.sig_col, s_beat.sig_row);
    *col = s_beat.sig_col, *row = s_beat.sig_row;
    *hx = s_beat.sig_hx, *hy = s_beat.sig_hy;
    *time = s_beat.time;
    return 1;
}

float net_beat_lap(int k)
{
    return s_beat.live && k >= 0 && k < s_beat.nx ? s_beat.xa[k] : 0.0f;
}

/*  And the car moved at the speed the script settled.
 *
 *      On along its segment.
 *      Into the junction box at its end.
 *      Or back the way it came. */
void net_beat_car_is(int i, float v)
{
    RTraffic       *t   = s_beat.t;
    const RNet *net = &s_beat.m->net;
    RCar           *car;
    const RNetSeg  *sg;
    float           to_end;
    int             end;
    if (!s_beat.live || i < 0 || (uint32_t)i != s_beat.at || (uint32_t)i >= t->n)
        return;
    s_beat.live = 0;
    car         = &t->cars[i];
    sg          = &net->segs[car->seg];
    end         = car->dir > 0 ? 1 : 0;
    car->s += (float)car->dir * v * s_beat.dt;
    to_end = car->dir > 0 ? sg->total - car->s : car->s;
    if (to_end > 0.0f)
        return;
    if (sg->kind[end] == 2 && car_enter_box(net, car, sg, end))
        return;
    /* a dead end, the map's edge, a bridge or a tunnel: turn back */
    car->hold = 0.0f;
    car->dir  = -car->dir;
    car->s    = car->dir > 0 ? 0.0f : sg->total;
}

/*  The cars as geometry (spec 6).  A car is 4.5 by 1.8 m and 1.5 m tall,
 *  at the line's scale of about seventeen meters to the tile.  It is a
 *  body and a cabin set back on it, painted by the car. */
/*  THE MOVING WORLD, hashed: every car's segment, direction, lane,
 *  distance, speed and waiting, and every gate's angle, in the order the
 *  world holds them.  A headless frame does not draw the movers, so this
 *  is the only thing that can tell one beat from another. */
void traffic_digest(const RTraffic *t, const RMesh *m)
{
    unsigned long long   h = 1469598103934665603ull;
    uint32_t             i;
    const unsigned char *p;
    size_t               k;
#define FEED(v)                                    \
    do                                             \
    {                                              \
        p = (const unsigned char *)&(v);           \
        for (k = 0; k < sizeof(v); ++k)            \
            h = (h ^ p[k]) * 1099511628211ull;     \
    } while (0)
    for (i = 0; i < t->n; ++i)
    {
        FEED(t->cars[i].seg);
        FEED(t->cars[i].dir);
        FEED(t->cars[i].lane);
        FEED(t->cars[i].s);
        FEED(t->cars[i].speed);
        FEED(t->cars[i].in_box);
        FEED(t->cars[i].hold);
    }
    if (t->gate)
        for (i = 0; i < m->n_laps; ++i)
            FEED(t->gate[i]);
    for (i = 0; i < t->n_trains; ++i)
    {
        FEED(t->trains[i].seg);
        FEED(t->trains[i].s);
        FEED(t->trains[i].speed);
        FEED(t->trains[i].dir);
    }
    /*  And the movers as they were DRAWN: the cars, the trains and the
     *  gates' moving parts all go into this one scratch mesh. */
    for (i = 0; i < t->scratch.n_land; ++i)
    {
        FEED(t->scratch.land[i].pos[0]);
        FEED(t->scratch.land[i].pos[1]);
        FEED(t->scratch.land[i].pos[2]);
        FEED(t->scratch.land[i].col[0]);
    }
#undef FEED
    dumpf("traffic %u cars, %u trains, %u vertices, digest %016llx\n",
          (unsigned)t->n, (unsigned)t->n_trains, (unsigned)t->scratch.n_land, h);
}

static int traffic_build(RTraffic *t, const RMesh *m, const RCity *c)
{
    const RNet *net      = &m->net;
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
            /*  The car's own shape is the model's (scripts/models).
             *  Where it stands and which way it points is this. */
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
            /*  A rigid car on the chord between its two bogies.  The
             *  art's one-tile bends and half circles are under any
             *  radius the spec allows a class (5.4: 1.5 tiles for a
             *  yard).  A car of 0.85 cut the inside of such a bend and
             *  its neighbors, so the cars are short.  Set along the
             *  tangent at its center instead a car had swung its ends
             *  out and into its neighbors. */
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
    /* the thread signals' aspects (spec 5.6).  A signal is red while any
     * car of a train stands in the block ahead of it.  The block runs in
     * the direction it governs, ten tiles, green otherwise */
    for (i = 0; i < m->n_rsigs; ++i)
    {
        const RSignal *sg2 = &m->rsigs[i];
        float           g   = surface_at_world(c, mask_bit, sg2->x, sg2->y);
        int32_t         tc = (int32_t)floorf(sg2->x), tr2 = (int32_t)floorf(sg2->y);
        if (tc < 0 || tr2 < 0 || tc >= R_MAP || tr2 >= R_MAP)
            continue;
        /*  The model the rule named for this signal (net_signal_*), stood
         *  on the ground at the signal's own place.  No answer is a
         *  signal that shows nothing, and the furniture switch takes the
         *  aspect with everything else that stands. */
        const char *aspect = s_aspect_name[s_rsig_aspect[i]];
        if (furniture_on() && aspect[0] &&
            net_model_put_on(net_model_find(aspect), &t->scratch, c, mask_bit,
                             tile_order(c, tc, tr2, mask_bit), sg2->x, sg2->y, sg2->fx, sg2->fy,
                             0.0f, 0.0f, 0.0f, g, g, 1) != 0)
            return -1;
    }
    /*  The meets' gates and flashers are the script's own: it lays
     *  them itself, from this same drive (scripts/compose/beat.lua). */
    return 0;
}

/*  How many gate arms there are to draw: one at each of a meet's two
 *  approaches. */
/*  THE THREAD SIGNALS, offered to the rule before the movers are drawn.
 *  How near the nearest car is each way along the block, and the aspect
 *  it answered with. */
int net_signals(void)
{
    return s_beat.m ? (int)s_beat.m->n_rsigs : 0;
}

int net_signal_at(int i, float *ahead, float *back)
{
    if (!s_beat.t || !s_beat.m || i < 0 || (uint32_t)i >= s_beat.m->n_rsigs)
        return 0;
    thread_block_near(s_beat.t, &s_beat.m->rsigs[i], -1, ahead, back);
    return 1;
}

void net_signal_is(int i, const char *model)
{
    int k;
    if (i < 0 || i >= RSIG_MAX)
        return;
    if (!model || !model[0])
    {
        s_rsig_aspect[i] = 0u;
        return;
    }
    for (k = 1; k < s_n_aspect; ++k)
        if (strcmp(s_aspect_name[k], model) == 0)
        {
            s_rsig_aspect[i] = (uint8_t)k;
            return;
        }
    if (s_n_aspect >= (int)(sizeof s_aspect_name / sizeof s_aspect_name[0]))
    {
        s_rsig_aspect[i] = 0u;
        return;
    }
    snprintf(s_aspect_name[s_n_aspect], sizeof s_aspect_name[0], "%s", model);
    s_rsig_aspect[i] = (uint8_t)s_n_aspect++;
}

int net_movers_gates(void)
{
    return s_beat.t && s_beat.t->gate ? (int)s_beat.m->n_laps * 2 : 0;
}

/*  Gate arm i: where it stands, which way it faces the driver it stops,
 *  the angle it has swung to and how far it reaches. */
int net_movers_gate(int i, float *x, float *y, float *fx, float *fy, float *angle, float *len, float *order)
{
    const RLap *g;
    float        mx, my, gx, gy;
    if (i < 0 || i >= net_movers_gates())
        return 0;
    g      = &s_beat.m->meets[i / 2];
    lap_approach(g, i % 2, &mx, &my, &gx, &gy);
    *x     = mx, *y = my, *fx = gx, *fy = gy;
    *angle = s_beat.t->gate[i / 2];
    /*  The arm reaches across the line it stops, so a line that
     *  carries more lanes gets the longer one. */
    *len   = line_class(s_beat.c, g->col, g->row) > 0.5f ? geo_num(&gix_gate_arm_wide, "gate_arm_wide")
                                                        : geo_num(&gix_gate_arm, "gate_arm");
    *order = tile_order(s_beat.c, g->col, g->row, s_beat.mask_bit) + 0.3f;
    return 1;
}

/*  And the mesh those parts go into: the movers' own scratch, which the
 *  frame uploads. */
void *net_movers_mesh(const void **city, uint8_t *mask_bit)
{
    if (!s_beat.t)
        return NULL;
    *city     = s_beat.c;
    *mask_bit = s_beat.mask_bit;
    return &s_beat.t->scratch;
}
