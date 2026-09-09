/*  The road family: its knobs, its classes, its junction control and the
 *  drawing of its junction box. */
#include <math.h>
#include <stddef.h>
#include <string.h>

#include "mesh/internal.h"
#include "net/internal.h"
#include "net/model.h"
#include "script.h"

/*  The numbers this file reads, each remembering where the store
 *  put it (net/geo.c).  A name here is a name a script must set. */


/*  The look's knobs.  Named, not counted, for the reason the road works'
 *  numbers are: a list of nineteen bare floats orders
 *  itself by position, and a field moved hands every later number to its
 *  neighbour without a word. */
RRoadTune s_tune = {
    .road_w       = 0.50f,
    .rail_w       = 0.62f,
    .road_rmin    = 0.90f,
    .rail_rmin    = 3.00f,
    .road_rmax    = 6.00f,
    .rail_rmax    = 8.00f,
    .approach     = 0.80f,
    .margin       = 0.04f,
    .trim_cap     = 0.32f, /* a box is the crossing and its curb returns, not the tile */
    .show_curves  = 0.00f,
    .hiway_w      = 0.60f,
    .hiway_rmin   = 2.50f,
    .hiway_rmax   = 8.00f,
    .hiway_reach  = 1.60f,
    .hiway_stair  = 2.00f,
    .corner_share = 0.50f,
    .ramp_merge   = 2.50f,
    .hiway_grade  = 0.10f,
    .hiway_stiff  = 2.00f,
};

/*  And by name, so a script and a report reach them the way the road
 *  works' numbers are reached.  Each says where it sits rather than
 *  being counted into place. */
static const struct
{
    const char *name;
    size_t      at;
    float       lo, hi;
} TUNE[] = {
    {"road_w",        offsetof(RRoadTune, road_w), 0.15f, 1.00f},
    {"rail_w",        offsetof(RRoadTune, rail_w), 0.15f, 1.00f},
    {"road_rmin",     offsetof(RRoadTune, road_rmin), 0.05f, 4.00f},
    {"rail_rmin",     offsetof(RRoadTune, rail_rmin), 0.05f, 8.00f},
    {"road_rmax",     offsetof(RRoadTune, road_rmax), 0.50f, 12.00f},
    {"rail_rmax",     offsetof(RRoadTune, rail_rmax), 0.50f, 16.00f},
    {"approach",      offsetof(RRoadTune, approach), 0.00f, 2.00f},
    {"margin",        offsetof(RRoadTune, margin), 0.00f, 0.30f},
    {"trim_cap",      offsetof(RRoadTune, trim_cap), 0.10f, 0.60f},
    {"show_curves",   offsetof(RRoadTune, show_curves), 0.00f, 1.00f},
    {"hiway_w",       offsetof(RRoadTune, hiway_w), 0.30f, 1.00f},
    {"hiway_rmin",    offsetof(RRoadTune, hiway_rmin), 0.50f, 6.00f},
    {"hiway_rmax",    offsetof(RRoadTune, hiway_rmax), 1.00f, 16.00f},
    {"hiway_reach",   offsetof(RRoadTune, hiway_reach), 0.50f, 3.00f},
    {"hiway_stair",   offsetof(RRoadTune, hiway_stair), 0.00f, 6.00f},
    {"corner_share",  offsetof(RRoadTune, corner_share), 0.25f, 1.00f},
    {"ramp_merge",    offsetof(RRoadTune, ramp_merge), 0.00f, 4.00f},
    {"hiway_grade",   offsetof(RRoadTune, hiway_grade), 0.02f, 0.50f},
    {"hiway_stiff",   offsetof(RRoadTune, hiway_stiff), 0.00f, 8.00f},
};

int net_tune_set(const char *name, float v)
{
    size_t i;
    for (i = 0; i < sizeof TUNE / sizeof TUNE[0]; ++i)
    {
        if (!name || strcmp(name, TUNE[i].name) != 0)
            continue;
        if (v < TUNE[i].lo)
            v = TUNE[i].lo;
        if (v > TUNE[i].hi)
            v = TUNE[i].hi;
        *(float *)((char *)&s_tune + TUNE[i].at) = v;
        return 1;
    }
    return 0;
}

const char *net_tune_name(int i, float *v)
{
    if (i < 0 || (size_t)i >= sizeof TUNE / sizeof TUNE[0])
        return NULL;
    if (v)
        *v = *(const float *)((const char *)&s_tune + TUNE[i].at);
    return TUNE[i].name;
}

float *mesh_tune(void)
{
    return &s_tune.road_w; /* nineteen floats, in the struct's own order */
}

/*  One class for the whole segment, the median of its tiles'. */
int seg_class(Seg *x)
{
    const RCity *c   = x->c;
    Family       f   = x->f;
    V2          *pts = x->pts;
    int          n   = x->n;
    int          k   = x->k;
    /*  One class for the whole segment, from how many of its tiles read
     *  as each: arc.rules.seg_class settles it. */
    if (f == F_ROAD)
    {
        int cnt[3] = {0, 0, 0}, cls;
        for (k = 0; k < n; ++k)
        {
            int32_t tc = (int32_t)floorf(pts[k].x), tr = (int32_t)floorf(pts[k].y);
            if (tc < 0 || tr < 0 || tc >= R_MAP || tr >= R_MAP)
                continue;
            ++cnt[(int)road_class(c, tc, tr)];
        }
        cls    = script_rule_seg_class(cnt);
        x->cls = (float)cls;
    }
    x->n = n;
    x->k = k;
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
    int cls[4], ctrl[1] = {0}, e, n = 0, maxc = 0, minc = 9, busy;
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
    /*  The decision is the SCRIPT'S (scripts/rules.lua).  There is no
     *  ladder here to fall back on: a rule the scripts do not set is a
     *  junction nothing controls, which is what a run that cannot find
     *  them draws.  Two copies of one policy, one in C and one in Lua,
     *  is the thing this layer exists to prevent. */
    if (!script_rule_control(col, row, cls, traf, busy, ctrl))
        return 0;
    (void)n, (void)maxc, (void)minc;
    return *ctrl;
}

/*  The asphalt: the outline the arms cut out, as a fan from the middle, the mouths' returns, and the footway round it. */
static int jb_asphalt(JBox *jb)
{
    const RCity *c        = jb->c;
    Family       f        = jb->f;
    int32_t      col      = jb->col;
    int32_t      row      = jb->row;
    int          links    = jb->links;
    float        order    = jb->order;
    float        mat      = jb->mat;
    float        cx       = jb->cx;
    float        cy       = jb->cy;
    float        h        = jb->h;
    float        lw       = jb->lw;
    float       *a0       = jb->a0;
    float       *a1       = jb->a1;
    float       *b0       = jb->b0;
    float       *b1       = jb->b1;
    float        zj       = jb->zj;
    /*  The asphalt: the outline the arms cut out, as a fan from the
     *  middle.  With every arm on a tile axis this is the square it
     *  has always been. */
    V2      poly[JUNC_MAX];
    uint8_t mouth[JUNC_MAX];
    JuncArm arms[4];
    float   trm[4];
    int     np    = junction_poly(c, f, col, row, links, poly, mouth, JUNC_MAX, trm, arms);
    /*  A box that reaches no chunk this build draws: the sidewalk's
     *  records, made round this outline, and no asphalt. */
    if (jb->records_only)
        return sidewalk_junction(jb, poly, arms, np);
    /*  The asphalt is laid INSIDE the footway, not under it: the band
     *  takes the outline's outer `lw` and the fan stops on the band's own
     *  inner edge, so the two meet edge to edge.  A mouth carries no band,
     *  so there the fan reaches the outline and the arm meets it.
     *
     *  What is laid over that polygon is the SCRIPT'S
     *  (arc.rules.junction): the outline is a solver's work -- arms,
     *  corners, trims and kerb returns -- and the asphalt drawn on it is
     *  not.  No rule is no junction surface at all. */
    V2  apoly[2 * JUNC_MAX];
    int anp                = sidewalk_junction_inset(jb, poly, arms, np, apoly, (int)(sizeof apoly / sizeof apoly[0]));
    {
        JuncFan fan;
        fan.jb    = jb;
        fan.poly  = anp >= 3 ? apoly : poly;
        fan.np    = anp >= 3 ? anp : np;
        fan.cx    = cx;
        fan.cy    = cy;
        fan.zj    = zj;
        fan.mat   = mat;
        fan.order = order;
        fan.square = np < 3;
        fan.a0 = a0, fan.a1 = a1, fan.b0 = b0, fan.b1 = b1;
        script_rule_object("junction", "junction", &fan);
    }
    /*  The junction's sidewalk is the sidewalk pass's (sidewalk.c): a
     *  footway band round this outline, mouth to mouth, which leaves the
     *  pavement open where a road comes in and closed everywhere else, and
     *  takes the outline's own corners as it goes. */
    if (sidewalk_junction(jb, poly, arms, np) != 0)
        return -1;
    jb->mat = mat;
    jb->cx  = cx;
    jb->cy  = cy;
    jb->h   = h;
    jb->lw  = lw;
    jb->zj  = zj;
    return 0;
}

/*  The four sides: a free side carries the sidewalk and its curb, a linked one its signal or stop sign. */
static int jb_sides(JBox *jb)
{
    RMesh       *m        = jb->m;
    const RCity *c        = jb->c;
    uint8_t      mask_bit = jb->mask_bit;
    Family       f        = jb->f;
    int32_t      col      = jb->col;
    int32_t      row      = jb->row;
    int          links    = jb->links;
    float        order    = jb->order;
    float        mat      = jb->mat;
    float        cx       = jb->cx;
    float        cy       = jb->cy;
    float        h        = jb->h;
    float        lw       = jb->lw;
    int          e;
    float        zj       = jb->zj;
    for (e = 0; e < 4; ++e)
    {
        /*  Only the signs and signals are the sides': the footway band
         *  in jb_asphalt follows the box's outline, free sides and curb
         *  returns alike, so a second sidewalk drawn along the tile's own
         *  side would lie on it at the same height and the same slot. */
        if ((links & (1 << e)) && f == F_ROAD)
        {
            int ctrl = (s_junc_ctrl[row * R_MAP + col] >> (2 * e)) & 3;
            if (ctrl == 2 && put_signal(m, c, col, row, mask_bit, order, e, h) != 0)
                return -1;
            if (ctrl == 1 && put_stop_sign(m, c, col, row, mask_bit, order, e, h) != 0)
                return -1;
        }
    }
    jb->mat = mat;
    jb->cx  = cx;
    jb->cy  = cy;
    jb->h   = h;
    jb->lw  = lw;
    jb->e   = e;
    jb->zj  = zj;
    return 0;
}

/*  Nothing is drawn; the stage stays for the driver's sake.  A junction
 *  box is not a square: junction_poly gives it an outline with its own
 *  curb returns, and jb_asphalt's footway band follows that outline round,
 *  so a corner piece drawn here would lie over the band in a shape of its
 *  own. */
static int jb_corners(JBox *jb)
{
    (void)jb;
    return 0;
}

/* ---- roads, rails and power lines -------------------------------------- */

/*  The networks as geometry.  Three families share one layout of fifteen
 *  pieces: two straights, four slopes, four corners, four tees and a
 *  crossing -- power lines at XBLD 0x0E, roads at 0x1D, rails at 0x2C.
 *  Which edges a piece joins is read once from the road art, the asphalt at
 *  each edge's midpoint, and the other two families take the road piece at
 *  the same offset.  The four crossings 0x44..0x47 carry two families on
 *  one tile.  A road or rail is a strip along the piece's connections: a
 *  straight piece from edge midpoint to edge midpoint; a lone corner a
 *  quarter circle centred on the tile corner between its two edges, tangent
 *  to both; a corner on a staircase the chord between its two midpoints, so
 *  the staircase draws as one straight diagonal; a junction a box at the
 *  centre with an arm to each joined edge.  Where two pieces meet at an
 *  angle the inner corners are mitred to one point and a fan fills the
 *  outside, so the edge line runs unbroken round the turn.  The strips lie
 *  on the surface the tiles draw, sampled at every vertex's world position.
 *  A power line is a pole at the tile's centre with a wire to each joined
 *  edge, meeting the neighbour's wire at the midpoint.  Width.  The oblique
 *  camera stretches the diagonal that runs toward it and squashes the
 *  other, so a road at one world width draws almost twice as wide on screen
 *  one way as the other; in the snap view every strip's world width is
 *  scaled by the direction it runs so that it reads the same width on
 *  screen everywhere, and the turned inspection view uses the true width. */

/*  A road's class, from the traffic on it: 0 a two-lane road, 1 an avenue
 *  with a double centre line and four lanes, 2 a boulevard with a planted
 *  median.  Carried to the material in the normal's fourth component, where
 *  a ground vertex carries its curvature. */
float road_class(const RCity *c, int32_t col, int32_t row)
{
    const ScriptFamily *fr = net_family_rules(F_ROAD);
    float               t  = (float)c->xtrf[(row >> 1) * R_HALF + (col >> 1)];
    return t >= fr->class_boulevard ? 2.0f : t >= fr->class_avenue ? 1.0f
                                                                   : 0.0f;
}

/* ---- the road as a family ------------------------------------------------ */

/*  The box on the outline: the asphalt, the sides, the corners. */
static int road_box(JBox *jb)
{
    if (jb_asphalt(jb) != 0)
        return -1;
    if (jb->records_only) /* the signs and the corners are drawing only */
        return 0;
    if (jb_sides(jb) != 0)
        return -1;
    return jb_corners(jb);
}

/*  A road strip records its edge in the traffic's graph under the class
 *  its tiles gave it (an island's, with none, as a local road), and its
 *  two sidewalks -- the outer fifth each side, from its first station
 *  to its last -- so the sidewalk check can see them meet the junctions'
 *  and the caps'. */
/*  Each station's distance along to the nearest level crossing on the
 *  segment, for the road's approach to it (spec 3.15): from the RXR
 *  stencil to the stop line the lines are solid, and the quads within a
 *  tile and a half of a crossing tile's centre carry the approach
 *  material with that distance along.  It is computed here and not by the
 *  furniture pass, which the driver can switch off. */
static void road_crossing_distances(Loft *x)
{
    const RCity *c   = x->c;
    Sample      *smp = x->smp;
    int          ns  = x->ns;
    int          i;
    {
        static float xs[64];
        int          nx = 0;
        for (i = 0; i < ns && nx < 64; ++i)
        {
            int32_t col = (int32_t)floorf(smp[i].pos.x), row = (int32_t)floorf(smp[i].pos.y);
            uint8_t b;
            if (col < 0 || row < 0 || col >= R_MAP || row >= R_MAP)
                continue;
            b = c->xbld[row * R_MAP + col];
            if (net_road_over_rail(b) && fabsf(smp[i].pos.x - (float)col - 0.5f) < net_family_rules(F_ROAD)->crossing_centre &&
                fabsf(smp[i].pos.y - (float)row - 0.5f) < net_family_rules(F_ROAD)->crossing_centre)
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
}

/*  The footway's two bands as the composing script lays them: the outer
 *  edge, the inner one, and the height, station by station.  The strip's
 *  own rule fills these, and the record below takes them up and names
 *  them into the network. */
#define WALK_MAX LOFT_MAX_ST
static float s_wk[2][WALK_MAX][5];
static int   s_wk_n[2];
static float s_wk_end[2][4];

void script_walk_reset(void)
{
    s_wk_n[0] = s_wk_n[1] = 0;
    memset(s_wk_end, 0, sizeof s_wk_end);
}

int script_walk_at(int side, float ox, float oy, float ix, float iy, float z)
{
    if (side < 0 || side > 1 || s_wk_n[side] >= WALK_MAX)
        return 0;
    s_wk[side][s_wk_n[side]][0] = ox;
    s_wk[side][s_wk_n[side]][1] = oy;
    s_wk[side][s_wk_n[side]][2] = ix;
    s_wk[side][s_wk_n[side]][3] = iy;
    s_wk[side][s_wk_n[side]][4] = z;
    ++s_wk_n[side];
    return 1;
}

void script_walk_ends(int side, float ax, float ay, float bx, float by)
{
    if (side < 0 || side > 1)
        return;
    s_wk_end[side][0] = ax, s_wk_end[side][1] = ay;
    s_wk_end[side][2] = bx, s_wk_end[side][3] = by;
}

int script_walk_count(int side)
{
    return side >= 0 && side <= 1 ? s_wk_n[side] : 0;
}

void script_walk_station(int side, int k, float *out)
{
    if (side >= 0 && side <= 1 && k >= 0 && k < s_wk_n[side])
        memcpy(out, s_wk[side][k], sizeof s_wk[0][0]);
}

void script_walk_end_pts(int side, float *out)
{
    if (side >= 0 && side <= 1)
        memcpy(out, s_wk_end[side], sizeof s_wk_end[0]);
}

static int road_record(Loft *x)
{
    const RLoft  *d   = x->d;
    const Sample *smp = x->smp;
    V2            oa  = {-smp[0].dir.x, -smp[0].dir.y}, ob = smp[x->ns - 1].dir;
    V2            ra, rb, la, lb;
    float         e[4];
    road_crossing_distances(x);
    if (net_record(&x->m->net, smp, x->ns, x->total, d->cls >= 0.0f ? (int)(d->cls + 0.5f) : 0, 0, d) != 0)
        return -1;
    /*  The footway's two bands are the SCRIPT'S, composed from the same
     *  stations and by the same expression as the carriageway's own
     *  edge, so the two meet along the band's inner line exactly.  With
     *  no rule a road has no footway at all. */
    script_walk_reset();
    if (!script_rule_object("walks", "strip", x))
        return 0;
    script_walk_end_pts(0, e), ra = (V2){e[0], e[1]}, rb = (V2){e[2], e[3]};
    script_walk_end_pts(1, e), la = (V2){e[0], e[1]}, lb = (V2){e[2], e[3]};
    sidewalk_add(SIDEWALK_STRIP, ra, rb, oa, ob);
    sidewalk_add(SIDEWALK_STRIP, la, lb, oa, ob);
    /*  The two sides, as the network holds them.  Side 0 is the right hand
     *  looking OUT from a node along its arm, so the strip's right side is
     *  side 0 at the first node and side 1 at the far one: the same ground,
     *  named from each end's own point of view.  The junction's corner
     *  names the same port, and that is what makes the two one walk. */
    {
        static WalkSt st[LOFT_MAX_ST];
        WalkPath      w;
        int           s, k, nst;
        for (s = 0; s < 2; ++s)
        {
            nst = script_walk_count(s);
            for (k = 0; k < nst && k < (int)(sizeof st / sizeof st[0]); ++k)
            {
                float v[5];
                script_walk_station(s, k, v);
                st[k].outer = (V2){v[0], v[1]};
                st[k].inner = (V2){v[2], v[3]};
                st[k].z     = v[4];
            }
            memset(&w, 0, sizeof w);
            w.kind    = WALK_SIDE;
            w.drape   = 1; /* a strip lies on the graded ground, and its footway with it */
            w.col     = d->node[0][0];
            w.row     = d->node[0][1];
            w.e       = d->arm[0];
            w.w       = x->hw * (1.0f - net_family_rules(F_ROAD)->inner);
            w.order   = tile_order(x->c, d->node[0][0], d->node[0][1], x->mask_bit);
            w.owner   = shape_current(); /* the strip it runs beside */
            w.end[0]  = s ? la : ra;
            w.end[1]  = s ? lb : rb;
            w.out[0]  = oa;
            w.out[1]  = ob;
            /*  Only a node that IS one: a junction or a dead end.  A
             *  segment that runs on through a bend has no arm to name
             *  there, and its footway simply carries on. */
            w.port[0] = d->nkind[0] ? walk_port(d->node[0][0], d->node[0][1], d->arm[0], s) : -1;
            w.port[1] = d->nkind[1] ? walk_port(d->node[1][0], d->node[1][1], d->arm[1], s ? 0 : 1) : -1;
            walk_net_add(&w, st, nst);
        }
    }
    return 0;
}

/*  A road's pair: its class, and its one marking -- the approach to a
 *  level crossing, whose along is the distance to the crossing.  A
 *  crosswalk is not the strip's: it belongs to the junction's mouth and
 *  is laid there (net/sidewalk.c), square to the arm however the road
 *  curves on its way in. */
static int road_pair(Loft *x, LoftPair *p)
{
    const Sample *pv = p->pv, *cu = p->cu;
    float        *ma = &p->ma, *al_a = &p->al_a, *al_b = &p->al_b;
    /* the class: the segment's from its tiles, or an island's from the tile under the pair */
    p->cls = x->d->cls >= 0.0f ? x->d->cls : road_class(x->c, p->tc, p->tr);
    if (pv->xd > net_family_rules(F_ROAD)->app_near && 0.5f * (pv->xd + cu->xd) < net_family_rules(F_ROAD)->app_far)
    {
        *ma   = MAT_XAPPROACH;
        *al_a = pv->xd;
        *al_b = cu->xd;
        return 0;
    }
    return 0;
}

/*  Where the traffic runs across a road, by class: the innermost lane
 *  and the outermost of arc.rules.lanes's answer, which is where the
 *  paint marks them too. */
static void road_traffic_lanes(const RLoft *d, int cls, float *lane_in, float *lane_out)
{
    float off[2] = {0.0f, 0.0f};
    int   n      = net_lane_offsets(F_ROAD, cls, off, 2);
    (void)d;
    *lane_in  = n > 0 ? off[0] : 0.0f;
    *lane_out = n > 1 ? off[1] : *lane_in;
}

/*  Street lighting (spec 1.6, 6.4).  Which strips are lit and how the
 *  lamps are spaced along them is arc.rules.lamps's, and their shape
 *  the street_lamp model's; what is left here is the walk that turns a
 *  distance along the strip into a place on the map.  A lamp that would
 *  stand at a level crossing is dropped: the crossing's own protection
 *  is there. */
static int road_furniture(Loft *x)
{
    RMesh       *m        = x->m;
    const RCity *c        = x->c;
    uint8_t      mask_bit = x->mask_bit;
    Sample      *smp      = x->smp;
    float        hw       = x->hw;
    int          ns       = x->ns;
    ScriptLamp   lamp[64];
    int          n = script_rule_lamps(x->d->cls, x->total, lamp, 64), k;
    for (k = 0; k < n; ++k)
    {
        int   j = 1;
        float px, py, ax, ay, hx, hy, order;
        while (j < ns - 1 && smp[j].s < lamp[k].at)
            ++j;
        if (marking_near_crossing(c, smp[j].pos))
            continue;
        hx = smp[j].dir.x;
        hy = smp[j].dir.y;
        ax = -hy * lamp[k].side; /* across, toward the pole's side */
        ay = hx * lamp[k].side;
        px = smp[j].pos.x + ax * hw * lamp[k].in;
        py = smp[j].pos.y + ay * hw * lamp[k].in;
        {
            int32_t tc = (int32_t)floorf(px), tr = (int32_t)floorf(py);
            if (tc < 0 || tr < 0 || tc >= R_MAP || tr >= R_MAP)
                continue;
            order = tile_order(c, tc, tr, mask_bit);
        }
        /*  The lamp, from its model (scripts/models.lua), standing on
         *  the strip's own height rather than on the ground under the
         *  kerb, and facing in over the road.  Where along the strip it
         *  stands, which side it is on and how far in it sits are
         *  arc.rules.lamps's; the model carries its own slot. */
        if (net_model_put_on(net_model_find("street_lamp"), m, c, mask_bit, order,
                             px, py, -ax, -ay, 0.0f, 0.0f, 0.0f, smp[j].z, smp[j].z, 1) != 0)
            return -1;
    }
    return 0;
}

/*  The lane centres by class, from the centreline: arc.rules.lanes's,
 *  the same answer the connectors are routed along, so a lane the cars
 *  run on and a lane the paint marks cannot part company.  The count
 *  each way; `off` inner first. */
static int road_lanes(int cls, float *off)
{
    return net_lane_offsets(F_ROAD, cls, off, 2);
}

const NetFamily net_road = {
    "road",
    F_ROAD,
    &s_tune.road_w,
    &s_tune.road_rmin,
    &s_tune.road_rmax,
    0.50f, /* the width the junction outline was tuned at */
    MAT_ROAD,
    LOFT_ROAD,
    0, /* the fit's family code */
    0.0f,
    0.25f, /* the shelf's grade ceiling */
    1,     /* curbs */
    1,     /* ramps */
    1,     /* buildings end it */
    1,     /* caps */
    1,     /* classed */
    junction_control,
    road_box,
    road_record,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    road_pair,
    road_traffic_lanes,
    road_furniture,
    NULL,
    road_lanes,
    5.0f, /* lane wires in red */
    NET_LANE_ENDS_CAP,
    0, /* keeps to its tiles */
    0.0f, /* no turnout: its junctions hand back curb trims */
    "slot_strip", /* the carriageway, over the junction it runs into */
    0,            /* not a deck */
};
