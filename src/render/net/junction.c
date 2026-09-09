/*  Junctions: the outline a node takes from its arms (the trims it hands
 *  back), and the box built on it; the drawing of a road's box is
 *  road.c's, a rail's rail.c's. */
#include <math.h>
#include <string.h>

#include "dump.h"
#include "mesh/internal.h"
#include "net/internal.h"
#include "script.h"
#include "opt.h"

/*  The numbers this file reads, each remembering where the store
 *  put it (net/geo.c).  A name here is a name a script must set. */


uint8_t s_junc_ctrl[R_MAP * R_MAP]; /* per junction tile, two bits per arm: 0 none, 1 stop, 2 signal */
float   s_trim[2][R_MAP * R_MAP * 4];
float   s_xwalk[2][R_MAP * R_MAP * 4];
RArm    s_arm[2][R_MAP * R_MAP * 4];

/*  Walk one segment of a family from a node tile out through link `e`,
 *  collect its centreline, straighten, fillet and loft it.  `visited`
 *  marks (tile, link) so each segment is walked once, from either end. */
/*  Which way a run leaves a junction, measured over a real baseline
 *  rather than from the tangent of its first piece.  How long a piece
 *  must be to give its own tangent, and how far along a shorter run the
 *  heading is measured, are arc.geo's (arm_own, arm_base). */

void arm_heading(const Piece *pc, int np, float total, int from_end, V2 *pos, V2 *dir)
{
    const ScriptFamily *fr    = net_family_rules(F_ROAD);
    const float         base  = total < 1.0f ? total * 0.5f : fr->arm_base;
    const Piece        *first = from_end ? &pc[np - 1] : &pc[0];
    if (first->len >= fr->arm_own)
    {
        piece_at(first, from_end ? first->len : 0.0f, pos, dir);
        if (from_end)
        {
            dir->x = -dir->x;
            dir->y = -dir->y;
        }
        return;
    }
    float at, seek, acc = 0.0f;
    V2    at_base, d0; /* the point at the baseline: only its heading is wanted */
    int   i;
    if (from_end)
    {
        piece_at(&pc[np - 1], pc[np - 1].len, pos, &d0);
        seek = total - base;
    }
    else
    {
        piece_at(&pc[0], 0.0f, pos, &d0);
        seek = base;
    }
    at_base = *pos;
    for (i = 0; i < np; ++i)
    {
        if (acc + pc[i].len >= seek || i == np - 1)
        {
            at = seek - acc;
            at = at < 0.0f ? 0.0f : at > pc[i].len ? pc[i].len
                                                   : at;
            piece_at(&pc[i], at, &at_base, dir);
            break;
        }
        acc += pc[i].len;
    }
    /*  The tangent where the run has gone far enough to mean it, rather
     *  than the chord to there: a chord over half a tile turns a curving
     *  approach into a straight one and moves the junction with it. */
    if (from_end)
    {
        dir->x = -dir->x;
        dir->y = -dir->y;
    }
    {
        float l = sqrtf(dir->x * dir->x + dir->y * dir->y);
        if (l < 1e-4f)
        {
            *dir = d0; /* a run with no length to speak of keeps its tangent */
            if (from_end)
            {
                dir->x = -dir->x;
                dir->y = -dir->y;
            }
            return;
        }
        dir->x /= l;
        dir->y /= l;
    }
}

/*  The ground under a junction box's outline point: the highest of the
 *  box's own plane, the ground where the point is, and the junction tile's
 *  ground at the nearest spot inside it.  Each of the three has been the
 *  one that mattered: - the box's plane: a spot of the junction tile the
 *  corridor never graded reads the raw ground a hair under the shelf, and
 *  the outline sloping down to it cut under the shelf between two samples
 *  (Atlanta 98,56 and 102,85, 0.02); - the ground where it is: an arm's
 *  outline reaching PAST the tile into a neighbour whose shelf stands
 *  higher (Four Cities 42,108, River5 57,19: pinned to the junction tile it
 *  cut 0.04 under); - the tile's own ground inside: a point ON the edge
 *  read the neighbour's raw ground and tilted that side of the box 0.03
 *  under its own tile (Atlanta 90,80's kin); and a point past the edge into
 *  a LOWER neighbour let the fan's spoke dip under the junction tile's own
 *  edge on the way out (River5 29,47, a sloped junction with a 0.08 wall to
 *  the south).  Uphill the box follows the ground; downhill it stays level
 *  and hovers the hair, as the old +0.05 outline did everywhere. */
float junc_surface(Family f, const RCity *c, uint8_t mask_bit, int col, int row, float x, float y, float zj)
{
    const float in = net_family_rules(f)->junc_inset;
    float       xc = fminf(fmaxf(x, (float)col + in), (float)(col + 1) - in);
    float       yc = fminf(fmaxf(y, (float)row + in), (float)(row + 1) - in);
    float       xo = x, yo = y, z;
    /*  a point on the edge itself is read a hundredth OUTSIDE too */
    if (fabsf(x - (float)col) < in)
        xo = (float)col - in;
    else if (fabsf(x - (float)(col + 1)) < in)
        xo = (float)(col + 1) + in;
    if (fabsf(y - (float)row) < in)
        yo = (float)row - in;
    else if (fabsf(y - (float)(row + 1)) < in)
        yo = (float)(row + 1) + in;
    z = fmaxf(zj, surface_at_world(c, mask_bit, xc, yc));
    return fmaxf(z, surface_at_world(c, mask_bit, xo, yo));
}

/*  An arm of a junction, as junction_poly sees it: the way it leaves,
 *  where its path starts, and its edge. */
typedef struct
{
    float ang;
    V2    d, o; /* the way it leaves, and where its path starts */
    int   e;
} JArm;

/*  One junction outline's working state, handed to the stages below so
 *  each can be read on its own: the arms by angle, the corners between
 *  them, the trims, and the outline being built. */
typedef struct
{
    const RCity *c;
    Family       f;
    int32_t      col, row;
    int          links;
    V2          *out;
    uint8_t     *mouth;
    int          max;
    float       *trim;
    float        cx, cy, w, ref, gro, far;
    JArm         arm[4];
    V2           corner[4];
    int          met[4];
    int          na, i, e, n;
} Junc;

/*  The arms: each linked edge's own ray -- where its path starts and the way it leaves -- from the fit the drawing pass will repeat. */
static int jp_arms(Junc *jx)
{
    Family  f     = jx->f;
    int32_t col   = jx->col;
    int32_t row   = jx->row;
    int     links = jx->links;
    float   cx    = jx->cx;
    float   cy    = jx->cy;
    float   w     = jx->w;
    JArm   *arm   = jx->arm;
    int     na    = jx->na;
    int     e;
    for (e = 0; e < 4; ++e)
    {
        const RArm *a = &s_arm[FAMX(f)][(row * R_MAP + col) * 4 + e];
        V2          d, o;
        int         rampside = net_family(f)->ramps && lane_ramp_tile(col + (int32_t)lroundf(ROAD_DU[e]), row + (int32_t)lroundf(ROAD_DV[e])) != NULL;
        if (!(links & (1 << e)) && !rampside)
            continue; /* a ramp tile returns no road link, and is an arm all the same */
        /*  The arm's own ray: where its path starts and the way it
         *  leaves.  Both come from the fit the drawing pass will repeat
         *  exactly, so the mouth this cuts is a point ON that path and
         *  the strip leaves it square. */
        d = a->have ? (V2){a->dx, a->dy} : (V2){ROAD_DU[e], ROAD_DV[e]};
        o = a->have ? (V2){a->ax, a->ay}
                    : (V2){cx + ROAD_DU[e] * w, cy + ROAD_DV[e] * w};
        /*  A ramp arm has no fitted segment, but its foot is at the tile
         *  edge's middle: the box reaches it, or grass shows between the
         *  box and the ramp. */
        if (!a->have && rampside)
            o = (V2){cx + ROAD_DU[e] * 0.5f, cy + ROAD_DV[e] * 0.5f};
        arm[na].d   = d;
        arm[na].o   = o;
        arm[na].ang = atan2f(d.y, d.x);
        arm[na].e   = e;
        ++na;
    }
    if (na < 1)
        return 1; /* nothing to draw */
    /* by angle, so "the next arm round" means what it says */
    jx->cx = cx;
    jx->cy = cy;
    jx->w  = w;
    jx->na = na;
    jx->e  = e;
    return 0;
}

/*  The outline itself is the SCRIPT'S (arc.rules.outline): the arms are
 *  read off the map above, and everything worked out from them -- their
 *  order round the junction, the corner between each pair, how far each
 *  mouth is cut back, the kerb returns and the ring itself -- is
 *  transcribed in scripts/compose/outline.lua.  With no rule a junction
 *  has no outline and nothing is drawn on it. */
static int jp_ring(Junc *jx)
{
    OutlineFan o;
    int        i;
    memset(&o, 0, sizeof o);
    o.f = (int)jx->f;
    o.col = jx->col, o.row = jx->row;
    o.cx = jx->cx, o.cy = jx->cy;
    o.w = jx->w, o.far = jx->far, o.gro = jx->gro;
    o.cap = s_tune.trim_cap;
    o.curbs = net_family(jx->f)->curbs;
    o.na = jx->na;
    for (i = 0; i < jx->na && i < 4; ++i)
    {
        o.arm[i].ox = jx->arm[i].o.x, o.arm[i].oy = jx->arm[i].o.y;
        o.arm[i].dx = jx->arm[i].d.x, o.arm[i].dy = jx->arm[i].d.y;
        o.arm[i].ang = jx->arm[i].ang;
        o.arm[i].e   = jx->arm[i].e;
    }
    o.out = jx->out, o.mouth = jx->mouth, o.trim = jx->trim, o.max = jx->max;
    for (i = 0; i < 4; ++i)
        jx->trim[i] = jx->w;
    script_rule_object("outline", "outline", &o);
    jx->n = o.n;
    /*  The arms as the script ordered them, so the mouths the box hands
     *  back are the ones the ring was walked from. */
    for (i = 0; i < o.na && i < 4; ++i)
    {
        jx->arm[i].o   = (V2){o.arm[i].ox, o.arm[i].oy};
        jx->arm[i].d   = (V2){o.arm[i].dx, o.arm[i].dy};
        jx->arm[i].ang = o.arm[i].ang;
        jx->arm[i].e   = o.arm[i].e;
    }
    jx->na = o.na;
    return 0;
}



/*  A junction's outline must be a simple ring.  Two faults make it not
 *  one, and both come from the same place: a corner that falls almost on
 *  top of a mouth.
 *
 *  A SPUR is a vertex where the ring turns back on itself -- out and
 *  straight back along its own path.  Its two edges face opposite ways,
 *  so the inward side of one of them points OUT of the junction, and
 *  everything taken from the outline afterwards inherits that: the
 *  footway offset lands outside, and the asphalt drawn inside the footway
 *  crosses itself and covers the band it was meant to stop at.
 *
 *  A CROSSING is two edges of the ring meeting away from their shared
 *  vertex.  A ring that crosses itself has no inside, so a fan drawn from
 *  its middle paints outside it.
 *
 *  Neither may exist.  They are counted for every junction the build lays
 *  and reported by the mesh check. */
static int s_out_n, s_out_spur, s_out_cross, s_out_bad;
static int s_out_worst_c, s_out_worst_r;
static float s_out_worst;

/*  How many corners the script pulled in for standing further out than a
 *  junction reaches, and how far the furthest stood: the outline
 *  report's own count. */
static int   s_clamped;
static float s_clamped_far;

void junction_outline_clamped(float len)
{
    ++s_clamped;
    if (len > s_clamped_far)
        s_clamped_far = len;
}

void junction_outline_reset(void)
{
    s_out_n = s_out_spur = s_out_cross = s_out_bad = s_clamped = 0;
    s_clamped_far = 0.0f;
    s_out_worst = 0.0f;
    s_out_worst_c = s_out_worst_r = -1;
}

static int seg_cross(V2 a, V2 b, V2 c, V2 d)
{
    float d1 = (b.x - a.x) * (c.y - b.y) - (b.y - a.y) * (c.x - b.x);
    float d2 = (b.x - a.x) * (d.y - b.y) - (b.y - a.y) * (d.x - b.x);
    float d3 = (d.x - c.x) * (a.y - d.y) - (d.y - c.y) * (a.x - d.x);
    float d4 = (d.x - c.x) * (b.y - d.y) - (d.y - c.y) * (b.x - d.x);
    return (d1 > 0.0f) != (d2 > 0.0f) && (d3 > 0.0f) != (d4 > 0.0f);
}

/*  Check one finished outline.  `turn_max` is how far a ring may turn at
 *  a vertex before it counts as doubling back: a mouth corner turns a
 *  right angle and a kerb return a little at a time, so anything past
 *  150 degrees is the boundary reversing, not a corner. */
static void junction_outline_check(const Junc *jx)
{
    const V2 *p = jx->out;
    int       n = jx->n, i, j, spur = 0, cross = 0;
    ++s_out_n;
    if (n < 3)
        return;
    for (i = 0; i < n; ++i)
    {
        V2    a = p[(i + n - 1) % n], b = p[i], c = p[(i + 1) % n];
        V2    u = {b.x - a.x, b.y - a.y}, v = {c.x - b.x, c.y - b.y};
        float lu = sqrtf(u.x * u.x + u.y * u.y), lv = sqrtf(v.x * v.x + v.y * v.y), ang;
        if (lu < 1e-6f || lv < 1e-6f)
            continue;
        ang = atan2f(u.x * v.y - u.y * v.x, u.x * v.x + u.y * v.y) * 57.29578f;
        if (fabsf(ang) > fabsf(s_out_worst))
        {
            s_out_worst   = ang;
            s_out_worst_c = jx->col;
            s_out_worst_r = jx->row;
        }
        if (fabsf(ang) > 150.0f)
        {
            ++spur;
            if (g_dev.junc_dump)
                dumpf("SPUR %d %d at %.3f,%.3f turns %.0f degrees\n", (int)jx->col, (int)jx->row, (double)b.x, (double)b.y, (double)ang);
        }
    }
    for (i = 0; i < n; ++i)
        for (j = i + 2; j < n; ++j)
        {
            if (i == 0 && j == n - 1)
                continue;
            if (seg_cross(p[i], p[(i + 1) % n], p[j], p[(j + 1) % n]))
            {
                ++cross;
                if (g_dev.junc_dump)
                    dumpf("CROSS %d %d edge %d and edge %d\n", (int)jx->col, (int)jx->row, i, j);
            }
        }
    s_out_spur += spur;
    s_out_cross += cross;
    if (spur || cross)
        ++s_out_bad;
}

void junction_outline_print(void)
{
    dumpf("outlines  %d junctions; %d spurs, %d self-crossings, on %d of them; the sharpest turn is %.0f degrees at %d,%d\n",
          s_out_n, s_out_spur, s_out_cross, s_out_bad, (double)s_out_worst, s_out_worst_c, s_out_worst_r);
    dumpf("outlines    %d corners pulled in for standing further out than a junction reaches, the furthest at %.3f tiles from the middle\n",
          s_clamped, (double)s_clamped_far);
}

int junction_outline_faults(void)
{
    return s_out_spur + s_out_cross;
}

int junction_poly(const RCity *c, Family f, int32_t col, int32_t row, int links, V2 *out, uint8_t *mouth, int max, float trim[4], JuncArm arms[4])
{
    Junc x;
    int  i;
    if (arms)
        memset(arms, 0, 4 * sizeof arms[0]);
    memset(&x, 0, sizeof x);
    x.c = c, x.f = f, x.col = col, x.row = row, x.links = links, x.out = out, x.mouth = mouth, x.max = max, x.trim = trim;
    x.cx = (float)col + 0.5f;
    x.cy = (float)row + 0.5f;
    x.w  = *net_family(f)->width * 0.5f;
    /*  The junction is sized by the band that runs through it, not by a
     *  fixed number of tiles.  A wider road needs its mouth pushed further
     *  out -- otherwise the arms fatten while the intersection stays put
     *  and the strips overhang it.  Everything below is written against the
     *  family's own default half-width, so at the shipped widths the
     *  numbers are the ones that were tuned by eye, and only moving a width
     *  moves them. */
    x.ref = net_family(f)->ref_width * 0.5f;
    x.gro = g_dev.noscale ? 1.0f : x.w / x.ref; /* for before/after shots */
    /*  How far from its middle a junction's corner may stand.  Two arms
     *  leaving at a shallow angle meet a long way out, and pulling that
     *  corner in costs it its kerb return: the boundary then turns the
     *  whole angle at a point.  A junction of a diagonal road wants the
     *  room, and the widest corner any shipped city asks for is under
     *  0.8 of a tile from the middle. */
    x.far = net_family_rules(f)->junc_far * x.gro;
    /*  Two stages: the arms this junction has, read off the map, and
     *  the ring the script walks from them. */
    if (jp_arms(&x) != 0)
        return 0; /* no arm: nothing to draw */
    jp_ring(&x);
    junction_outline_check(&x);

    if (g_dev.junc_dump)
    {
        dumpf("JUNC %d %d", (int)col, (int)row);
        for (i = 0; i < x.na; ++i)
            dumpf(" %.3f,%.3f,%.3f,%.3f", (double)x.arm[i].d.x, (double)x.arm[i].d.y, (double)trim[x.arm[i].e], (double)x.w);
        dumpf("\n");
    }
    /*  Each arm's mouth, before the hull has its way with the outline:
     *  the middle of the cut, at the trim the ring asked for, and the way
     *  the arm leaves. */
    if (arms)
        for (i = 0; i < x.na; ++i)
        {
            int   e2 = x.arm[i].e;
            float t2 = trim[e2];
            V2    d2 = x.arm[i].d, p2 = {d2.y, -d2.x}; /* the right hand looking out */
            V2    m2 = {x.arm[i].o.x + d2.x * t2, x.arm[i].o.y + d2.y * t2};
            arms[e2].mid  = m2;
            arms[e2].dir  = d2;
            arms[e2].a    = (V2){m2.x + p2.x * x.w, m2.y + p2.y * x.w};
            arms[e2].b    = (V2){m2.x - p2.x * x.w, m2.y - p2.y * x.w};
            arms[e2].have = 1;
        }

    if (g_dev.junc_dump)
    {
        dumpf("JPOLY %d %d", (int)col, (int)row);
        for (i = 0; i < x.n; ++i)
            dumpf(" %.3f,%.3f%s%d", (double)out[i].x, (double)out[i].y, mouth && mouth[i] ? "/m" : "", mouth ? (int)mouth[i] : 0);
        dumpf("\n");
    }
    return x.n;
}

static int build_junction_body(RMesh *m, const RCity *c, uint8_t mask_bit, Family f, int32_t col, int32_t row, int links, float order);

/*  The junction, and who asked for it (as loft_at does for a strip). */
int build_junction_at(const char *where, const char *who, RMesh *m, const RCity *c, uint8_t mask_bit, Family f, int32_t col, int32_t row, int links, float order)
{
    ShapeId sh;
    int     rc;
    sh = shape_open_at(where, who, "%s junction at %d,%d", net_family(f)->name, (int)col, (int)row);
    rc = build_junction_body(m, c, mask_bit, f, col, row, links, order);
    shape_close(sh);
    return rc;
}

static int build_junction_body(RMesh *m, const RCity *c, uint8_t mask_bit, Family f, int32_t col, int32_t row, int links, float order)
{
    JBox             x;
    const NetFamily *fam = net_family(f);
    memset(&x, 0, sizeof x);
    x.m = m, x.c = c, x.mask_bit = mask_bit, x.f = f, x.col = col, x.row = row, x.links = links, x.order = order;
    x.hw           = *fam->width * 0.5f;
    x.mat          = fam->mat;
    x.records_only = !mesh_want_tile(col, row); /* an edit's build: the box reaches no chunk it draws */
    if (!x.records_only) /* what it is, for the inspector */
        shape_note("links\t%s%s%s%s\nwidth\thalf %.2f tiles", links & L_N ? "north " : "", links & L_E ? "east " : "", links & L_S ? "south " : "", links & L_W ? "west " : "", (double)x.hw);
    x.cx           = (float)col + 0.5f;
    x.cy           = (float)row + 0.5f;
    x.h            = x.hw;
    x.sw           = x.h * net_family_rules(f)->at_junction; /* the curb return's radius: the sidewalk's width */
    x.lw           = x.h * net_family_rules(f)->at_junction; /* the sidewalk along a free side, across 0.8..1  */
    x.a0[0] = x.cx - x.h, x.a0[1] = x.cy - x.h;
    x.a1[0] = x.cx + x.h, x.a1[1] = x.cy - x.h;
    x.b0[0] = x.cx - x.h, x.b0[1] = x.cy + x.h;
    x.b1[0] = x.cx + x.h, x.b1[1] = x.cy + x.h;
    /*  The box stands FLUSH on the graded surface, no hair: the ground
     *  under a junction is graded to the road's line like the ground
     *  under its arms, and the arms lie flush on that.  A hair here, or
     *  0.05 at the outline, stands every box 0.03 to 0.05 above its
     *  roads (Atlanta 90,80: box 5.08, arms 5.03). */
    x.zj           = surface_at_world(c, mask_bit, x.cx, x.cy);
    shelf_node(col, row); /* a node of the graph: every edge that meets here is at one level */
    x.comp         = net_compensate();
    m->strip_class = 0.0f; /* the box is plain asphalt; a median ends at the junction */
    /*  The control -- which arms carry a signal or a stop sign -- is
     *  stage three's, worked out with the trims and the crossings that
     *  turn on it (walk.c net_stage_three); the box only reads it.
     *  The grading pass stops here: a box grades nothing. */
    if (grade_only(g_dev.grade_junc))
        return fam->turnout > 0.0f ? fam->box(&x) : 0; /* a turnout's tracks are lofted (rail.c): the grading pass grades their ground as a segment's */
    /*  The stages: a rail junction is its own thing and done; a road's
     *  gets its lanes' connectors, the asphalt, the sides, the corners.
     *  The connectors run from every inbound lane to every outbound lane
     *  of the other arms (lane.c), a rail junction's included, so that
     *  every port its box draws is served (Atlanta 9,12). */
    double tp = prof_now();
    if (lane_junction(m, c, mask_bit, f, col, row, links) != 0)
        return -1;
    net_prof_add(NET_PROF_JUNC_LANES, prof_now() - tp), tp = prof_now();
    {
        int rc = fam->box(&x); /* the family's drawing on the outline: road.c's, rail.c's */
        net_prof_add(NET_PROF_JUNC_BOX, prof_now() - tp);
        return rc;
    }
}
