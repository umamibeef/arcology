/*  Junctions: the outline a node takes from its arms (the trims it hands
 *  back), and the box built on it; the drawing of a road's box is
 *  road.c's, a rail's rail.c's. */
#include <math.h>
#include <string.h>

#include "dump.h"
#include "mesh/internal.h"
#include "log.h"
#include "net/internal.h"
#include "script.h"
#include "opt.h"

/*  The numbers this file reads, each remembering where the store
 *  put it (net/geo.c).  A name here is a name a script must set. */


static int gix_junc_spur_angle = -1;

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
/*  THE RINGS, worked out once a pass and kept.
 *
 *  A junction's outline is the script's, and the script is asked for it
 *  in a pass of its own (scripts/compose/world.lua) between the measure
 *  that fills the arm table and the trims that read the ring.  Nothing
 *  in the pipeline asks for one: it looks the answer up.
 *
 *  Keeping them is what lets the ring be worked out once and read twice
 *  -- the trims want it, and so does the box drawn later -- and it is
 *  what makes the ask a pass rather than a call from inside three
 *  different phases. */
#define RINGS_MAX 4096
static struct
{
    int32_t col, row;
    int     f, n, na;
    struct
    {
        float ox, oy, dx, dy, ang;
        int   e;
    } arm[4];
    V2      out[JUNC_MAX];
    uint8_t mouth[JUNC_MAX];
    float   trim[4];
} s_ring[RINGS_MAX];
static int s_n_ring;

void junction_rings_reset(void)
{
    s_n_ring = 0;
}

static int ring_find(int f, int32_t col, int32_t row)
{
    int i;
    for (i = 0; i < s_n_ring; ++i)
        if (s_ring[i].f == f && s_ring[i].col == col && s_ring[i].row == row)
            return i;
    return -1;
}

/*  The ring the script left for this junction, into the outline it is
 *  being asked about.  Answers 0 where the script left none, which is a
 *  junction with no outline and nothing drawn on it. */
static int ring_take(OutlineFan *o)
{
    int i = ring_find(o->f, o->col, o->row), k;
    if (i < 0)
        return 0;
    o->n  = s_ring[i].n;
    o->na = s_ring[i].na;
    for (k = 0; k < 4; ++k)
    {
        o->arm[k].ox = s_ring[i].arm[k].ox, o->arm[k].oy = s_ring[i].arm[k].oy;
        o->arm[k].dx = s_ring[i].arm[k].dx, o->arm[k].dy = s_ring[i].arm[k].dy;
        o->arm[k].ang = s_ring[i].arm[k].ang, o->arm[k].e = s_ring[i].arm[k].e;
        if (o->trim)
            o->trim[k] = s_ring[i].trim[k];
    }
    for (k = 0; k < o->n && k < o->max; ++k)
    {
        o->out[k] = s_ring[i].out[k];
        if (o->mouth)
            o->mouth[k] = s_ring[i].mouth[k];
    }
    return 1;
}

/*  And what the script answered, kept. */
void junction_ring_keep(const OutlineFan *o)
{
    int i = ring_find(o->f, o->col, o->row), k;
    if (i < 0)
    {
        if (s_n_ring >= RINGS_MAX)
        {
            /*  A junction whose ring is not kept has no outline and
             *  nothing drawn on it, which is a hole in the city and not
             *  a thing to pass over quietly. */
            R_ERR("net", "no room for the ring at %d,%d: %d junctions is the most kept",
                  (int)o->col, (int)o->row, RINGS_MAX);
            return;
        }
        i = s_n_ring++;
    }
    s_ring[i].f = o->f, s_ring[i].col = o->col, s_ring[i].row = o->row;
    s_ring[i].n  = o->n < JUNC_MAX ? o->n : JUNC_MAX;
    s_ring[i].na = o->na;
    for (k = 0; k < 4; ++k)
    {
        s_ring[i].arm[k].ox = o->arm[k].ox, s_ring[i].arm[k].oy = o->arm[k].oy;
        s_ring[i].arm[k].dx = o->arm[k].dx, s_ring[i].arm[k].dy = o->arm[k].dy;
        s_ring[i].arm[k].ang = o->arm[k].ang, s_ring[i].arm[k].e = o->arm[k].e;
        s_ring[i].trim[k] = o->trim ? o->trim[k] : 0.0f;
    }
    for (k = 0; k < s_ring[i].n; ++k)
    {
        s_ring[i].out[k] = o->out[k];
        s_ring[i].mouth[k] = o->mouth ? o->mouth[k] : 0;
    }
}

/*  ------------------------------------------------------------------
 *  What stage three measured, held for the drive
 *
 *  A junction's control and a mouth's crosswalk depth are both the
 *  scripts', and both are settled before any of the geometry that reads
 *  them is laid.  Stage three measures them and keeps the measurements;
 *  the drive walks what it kept, asks the rules, and hands each answer
 *  straight back.  Nothing between the two decides anything.
 *  ------------------------------------------------------------------ */
#define ASKS_MAX 16384

static struct
{
    const char *rule;                 /* the rule that answers it */
    int32_t     col, row;
    int         links, measured;
    int         cls[4], traf[4], busy; /* what a primitive read off the map, where one did */
} s_ctrl_ask[ASKS_MAX];
static int s_n_ctrl_ask;

static struct
{
    int32_t col, row;
    int     e, fx, ctrl;
    float   want, room, straight, cap;
} s_xw_ask[ASKS_MAX];
static int s_n_xw_ask;

void net_control_asks_reset(void)
{
    s_n_ctrl_ask = 0;
}

void net_xwalk_asks_reset(void)
{
    s_n_xw_ask = 0;
}

/*  One junction's control, to be answered.  `cls` and `traf` are what a
 *  family's own primitive read off the map; a family whose control is a
 *  rule and nothing else passes neither. */
void net_control_ask(const char *rule, int32_t col, int32_t row, int links,
                     const int *cls, const int *traf, int busy)
{
    int k;
    if (s_n_ctrl_ask >= ASKS_MAX)
    {
        R_ERR("net", "no room for the control at %d,%d: %d junctions is the most measured",
              (int)col, (int)row, ASKS_MAX);
        return;
    }
    k                          = s_n_ctrl_ask++;
    s_ctrl_ask[k].rule         = rule;
    s_ctrl_ask[k].col          = col;
    s_ctrl_ask[k].row          = row;
    s_ctrl_ask[k].links        = links;
    s_ctrl_ask[k].measured     = cls != NULL;
    s_ctrl_ask[k].busy         = busy;
    for (int e = 0; e < 4; ++e)
    {
        s_ctrl_ask[k].cls[e]  = cls ? cls[e] : -1;
        s_ctrl_ask[k].traf[e] = traf ? traf[e] : 0;
    }
}

int net_control_asked(void)
{
    return s_n_ctrl_ask;
}

/*  What was measured at one of them: the rule to ask, and the reading. */
const char *net_control_ask_at(int i, int32_t *col, int32_t *row, int *links,
                               const int **cls, const int **traf, int *busy)
{
    if (i < 0 || i >= s_n_ctrl_ask)
        return NULL;
    *col   = s_ctrl_ask[i].col;
    *row   = s_ctrl_ask[i].row;
    *links = s_ctrl_ask[i].links;
    *cls   = s_ctrl_ask[i].measured ? s_ctrl_ask[i].cls : NULL;
    *traf  = s_ctrl_ask[i].measured ? s_ctrl_ask[i].traf : NULL;
    *busy  = s_ctrl_ask[i].busy;
    return s_ctrl_ask[i].rule;
}

/*  And the answer, two bits an arm, on to the tile itself. */
void net_control_is(int i, int ctrl)
{
    if (i >= 0 && i < s_n_ctrl_ask)
        s_junc_ctrl[s_ctrl_ask[i].row * R_MAP + s_ctrl_ask[i].col] = (uint8_t)ctrl;
}

/*  One mouth's crosswalk, to be answered: what the outline asked for,
 *  the road there is to give up, how much of it runs straight from the
 *  mouth, and the most of that road a band may take. */
void net_xwalk_ask(int32_t col, int32_t row, int e, int fx, int ctrl,
                   float want, float room, float straight, float cap)
{
    int k;
    if (s_n_xw_ask >= ASKS_MAX)
    {
        R_ERR("net", "no room for the crossing at %d,%d: %d mouths is the most measured",
              (int)col, (int)row, ASKS_MAX);
        return;
    }
    k               = s_n_xw_ask++;
    s_xw_ask[k].col = col, s_xw_ask[k].row = row, s_xw_ask[k].e = e;
    s_xw_ask[k].fx  = fx, s_xw_ask[k].ctrl = ctrl;
    s_xw_ask[k].want = want, s_xw_ask[k].room = room;
    s_xw_ask[k].straight = straight, s_xw_ask[k].cap = cap;
}

int net_xwalk_asked(void)
{
    return s_n_xw_ask;
}

int net_xwalk_ask_at(int i, int32_t *col, int32_t *row, int *e, int *ctrl,
                     float *want, float *room, float *straight)
{
    if (i < 0 || i >= s_n_xw_ask)
        return 0;
    *col = s_xw_ask[i].col, *row = s_xw_ask[i].row, *e = s_xw_ask[i].e;
    *ctrl = s_xw_ask[i].ctrl, *want = s_xw_ask[i].want;
    *room = s_xw_ask[i].room, *straight = s_xw_ask[i].straight;
    return 1;
}

/*  The depth the rule answered, held to the road so one crossing can
 *  never eat the segment.  A mouth the rule leaves unanswered keeps no
 *  band at all. */
void net_xwalk_deep(int i, float d)
{
    if (i < 0 || i >= s_n_xw_ask)
        return;
    if (d < 0.0f)
        d = 0.0f;
    if (d > s_xw_ask[i].cap)
        d = s_xw_ask[i].cap;
    s_xwalk[s_xw_ask[i].fx][(s_xw_ask[i].row * R_MAP + s_xw_ask[i].col) * 4 + s_xw_ask[i].e] = d;
}

/*  ------------------------------------------------------------------
 *  The strips a box lofts, held for the drive
 *
 *  A rail junction's box is its tracks, and each of them is lofted as a
 *  segment's strip is.  The box gathers them and draws none; the drive
 *  lofts each in turn and composes it, which is what keeps the strip's
 *  composition in one place instead of inside whichever box made it.
 *  ------------------------------------------------------------------ */
#define BOX_LOFTS_MAX 16

static struct
{
    RMesh       *m;
    const RCity *c;
    uint8_t      mask_bit;
    int          comp;
    RLoft        d;
    Piece        pc[MAX_PIECES];
    int          np;
    float        total;
} s_box_loft[BOX_LOFTS_MAX];
static int s_n_box_loft;

void net_box_lofts_reset(void)
{
    s_n_box_loft = 0;
}

int net_box_loft_add(const JBox *jb, const RLoft *d, const Piece *pc, int np, float total)
{
    int k;
    if (s_n_box_loft >= BOX_LOFTS_MAX)
    {
        R_ERR("net", "no room for the box's strip at %d,%d: %d is the most one box lofts",
              (int)jb->col, (int)jb->row, BOX_LOFTS_MAX);
        return -1;
    }
    k                      = s_n_box_loft++;
    s_box_loft[k].m        = jb->m;
    s_box_loft[k].c        = jb->c;
    s_box_loft[k].mask_bit = jb->mask_bit;
    s_box_loft[k].comp     = jb->comp;
    s_box_loft[k].d        = *d;
    s_box_loft[k].np       = np < MAX_PIECES ? np : MAX_PIECES;
    s_box_loft[k].total    = total;
    memcpy(s_box_loft[k].pc, pc, sizeof(Piece) * (size_t)s_box_loft[k].np);
    return 0;
}

int net_box_lofts(void)
{
    return s_n_box_loft;
}

/*  One of them lofted, ready for the drive to compose. */
int net_box_loft(int i)
{
    if (i < 0 || i >= s_n_box_loft)
        return 0;
    return loft(s_box_loft[i].m, s_box_loft[i].c, s_box_loft[i].mask_bit, s_box_loft[i].comp,
                &s_box_loft[i].d, s_box_loft[i].pc, s_box_loft[i].np, s_box_loft[i].total);
}

static void jp_fill(const Junc *jx, OutlineFan *o);

/*  A junction set up and its arms read off the map, ready for the
 *  script to walk a ring from.  Answers 0 where there is no junction
 *  there, or no arm to make one of. */
int junction_ask(const RCity *c, Family f, int32_t col, int32_t row, int links,
                 OutlineFan *o, V2 *out, uint8_t *mouth, float *trim)
{
    Junc x;
    memset(&x, 0, sizeof x);
    x.c = c, x.f = f, x.col = col, x.row = row, x.links = links;
    x.out = out, x.mouth = mouth, x.max = JUNC_MAX, x.trim = trim;
    x.cx  = (float)col + 0.5f;
    x.cy  = (float)row + 0.5f;
    x.w   = *net_family(f)->width * 0.5f;
    x.ref = net_family(f)->ref_width * 0.5f;
    x.gro = g_dev.noscale ? 1.0f : x.w / x.ref;
    x.far = net_family_rules(f)->junc_far * x.gro;
    if (jp_arms(&x) != 0)
        return 0;
    jp_fill(&x, o);
    return 1;
}

/*  The junction, as the outline the script is asked about: everything it
 *  needs to walk a ring, and the arrays it walks it into. */
static void jp_fill(const Junc *jx, OutlineFan *o)
{
    int i;
    memset(o, 0, sizeof *o);
    o->f = (int)jx->f;
    o->col = jx->col, o->row = jx->row;
    o->cx = jx->cx, o->cy = jx->cy;
    o->w = jx->w, o->far = jx->far, o->gro = jx->gro;
    o->cap   = s_tune.trim_cap;
    o->curbs = net_family(jx->f)->curbs;
    o->na    = jx->na;
    for (i = 0; i < jx->na && i < 4; ++i)
    {
        o->arm[i].ox = jx->arm[i].o.x, o->arm[i].oy = jx->arm[i].o.y;
        o->arm[i].dx = jx->arm[i].d.x, o->arm[i].dy = jx->arm[i].d.y;
        o->arm[i].ang = jx->arm[i].ang;
        o->arm[i].e   = jx->arm[i].e;
    }
    o->out = jx->out, o->mouth = jx->mouth, o->trim = jx->trim, o->max = jx->max;
}

static int jp_ring(Junc *jx)
{
    OutlineFan o;
    int        i;
    jp_fill(jx, &o);
    for (i = 0; i < 4; ++i)
        jx->trim[i] = jx->w;
    ring_take(&o);
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
        if (fabsf(ang) > net_geo(&gix_junc_spur_angle, "junc_spur_angle"))
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
/*  The box being built, held between its two halves: the drive lays the
 *  asphalt on the outline the first half gathered, and the second half
 *  takes up the footway, the signs and the corners.  The shape stays
 *  open across the pair, so everything drawn belongs to the junction. */
static JBox    s_jbox;
static ShapeId s_jbox_sh;
static int     s_jbox_stop; /* the grading pass: the box grades nothing past its own tracks */

int build_junction_at(const char *where, const char *who, RMesh *m, const RCity *c, uint8_t mask_bit, Family f, int32_t col, int32_t row, int links, float order)
{
    s_jbox_sh = shape_open_at(where, who, "%s junction at %d,%d", net_family(f)->name, (int)col, (int)row);
    return build_junction_body(m, c, mask_bit, f, col, row, links, order);
}

int build_junction_done(void)
{
    int rc = s_jbox_stop ? 0 : net_family_box_done(net_family(s_jbox.f), &s_jbox);
    shape_close(s_jbox_sh);
    s_jbox_sh = SHAPE_NONE;
    return rc;
}

static int build_junction_body(RMesh *m, const RCity *c, uint8_t mask_bit, Family f, int32_t col, int32_t row, int links, float order)
{
    JBox             x;
    const NetFamily *fam = net_family(f);
    memset(&x, 0, sizeof x);
    s_jbox_stop = 0;
    net_box_lofts_reset();
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
    {
        /*  a turnout's tracks are lofted (rail.c): the grading pass
         *  grades their ground as a segment's */
        s_jbox_stop = 1;
        s_jbox      = x;
        return fam->turnout > 0.0f ? net_family_box(fam, &s_jbox) : 0;
    }
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
        int rc;
        s_jbox = x;
        rc     = net_family_box(fam, &s_jbox); /* the family's drawing on the outline: road.c's, rail.c's */
        net_prof_add(NET_PROF_JUNC_BOX, prof_now() - tp);
        return rc;
    }
}
