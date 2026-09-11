/*  margin.c: THE MARGIN BANDS, gathered and offered.
 *
 *  Where they run and what they join is the network's (walk/walkway.c).
 *  This gathers the bands that network holds for arc.rules.margin to
 *  lay, and measures the two things a junction can only take from its
 *  own outline.  Where the fill must stop so the margin is not paved
 *  over.  Which of its mouths carry a meet: which one is
 *  arc.rules.meet's.
 *
 *  A margin is a path with two ends and every end meets another's.  A
 *  strip carries one along each side, ending at the mouths of the
 *  junctions it joins.  The junction's own runs round the box from one
 *  mouth to the next, lip returns and free sides alike.  A dead end's
 *  goes round its cap.  The `margins` and `walkways` lines of the mesh
 *  check count the ends that meet nothing.
 *
 *  A MEET is the junction's, not the line's.  It is laid on the approach
 *  just outside a mouth.  It runs square to the arm's own cut, whatever
 *  the line does further out, spanning the way from one margin to the
 *  other.  The strip leaves that much of its slab bare, so the two meet
 *  along the mouth rather than one lapping the other.
 *
 *  The margin is also a PASS of its own, like the markings and the
 *  furniture.  The frame draws the line strips a second time under pass
 *  1.  The line material then paints their outer fifth, lip and
 *  concrete.  It does it on that draw alone (gpu/frame.c, terrain.frag),
 *  so no extra vertex exists for it and the switch is a draw skipped.
 *  Off, the bands are hidden under the frame's margin flag and nothing
 *  is registered. */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "dump.h"
#include "mesh/internal.h"
#include "pipeline.h"
#include "net/net.h"
#include "script.h"
#include "opt.h"

/*  The numbers this file reads, each remembering where the store
 *  put it (net/geo.c).  A name here is a name a script must set. */


#define WALK_MAX 65536
static int gix_walk_mouth_eps = -1;
#define WALK_EPS geo_num(&gix_walk_mouth_eps, "walk_mouth_eps") /* how much wider an avenue's mouth may be than the box's */

typedef struct
{
    uint8_t kind;
    V2      a, b;   /* the two ends                                        */
    V2      oa, ob; /* the way out past each end, for what stands there.  Zero for none */
} Walk;

static Walk         s_walk[WALK_MAX];
static int          s_nw;
static const RCity *s_city;   /* the build's city, for what stands beyond an open end */
static int          s_on = 1; /* the margin pass: View > Margins, --no-margins */

void margin_enable(int on)
{
    s_on = on ? 1 : 0;
}

int margin_on(void)
{
    return s_on;
}

/*  What is true of every strip of one family, asked of the script once
 *  The script PUSHED these when it was read (arc.family.rules).  So this
 *  is a lookup by the family's name and nothing is asked of a script
 *  from inside the walk. */
const ScriptFamily *net_family_rules(Family f)
{
    return script_family_rules(net_family(f)->name);
}

const ScriptFamily *net_line_rules(void)
{
    return script_family_rules(net_line->name);
}

void margin_reset(const RCity *c)
{
    s_nw   = 0;
    s_city = c;
}

int margin_add(int kind, V2 a, V2 b, V2 oa, V2 ob)
{
    if (!s_on)
        return 0; /* the pass is off: no margin exists to register */
    if (grade_only(0))
        return 0; /* the grading pass: the building pass registers */
    if (s_nw >= WALK_MAX)
        return 0;
    s_walk[s_nw].kind = (uint8_t)kind;
    s_walk[s_nw].a    = a;
    s_walk[s_nw].b    = b;
    s_walk[s_nw].oa   = oa;
    s_walk[s_nw].ob   = ob;
    ++s_nw;
    return 0;
}

/*  Does the tile just past the end, the way the walk leaves it, carry a
 *  carrier.  A bridge, a tunnel end, a meet, a band.  That the line runs
 *  on into?  Only an end that knows its way out can say. */
static int past_carrier(const RCity *c, V2 p, V2 o)
{
    int32_t x, y;
    uint8_t b;
    if (fabsf(o.x) + fabsf(o.y) < 0.5f)
        return 0;
    x = (int32_t)floorf(p.x + o.x * net_line_rules()->look);
    y = (int32_t)floorf(p.y + o.y * net_line_rules()->look);
    if (x < 0 || y < 0 || x >= R_MAP || y >= R_MAP)
        return 0;
    b = c->xbld[y * R_MAP + x];
    return net_carrier(b);
}

/*  Is the tile just past the end a spur's?  A spur carries no margin. */
static int past_spur(V2 p, V2 o)
{
    if (fabsf(o.x) + fabsf(o.y) < 0.5f)
        return 0;
    return lane_spur_tile((int32_t)floorf(p.x + o.x * net_line_rules()->look),
                          (int32_t)floorf(p.y + o.y * net_line_rules()->look)) != NULL;
}

/*  Every end against every other: the table is a few thousand walks, and
 *  a bucket by tile keeps it linear. */
void margin_stats_print(void)
{
    const RCity *c = s_city;
    if (!s_on)
    {
        dumpf("margins  off (the pass is switched off)\n");
        return;
    }
    static int32_t head[R_MAP * R_MAP];
    static int32_t next[2 * WALK_MAX];
    int            i, e, strips = 0, links = 0, caps = 0, open = 0, carrier = 0, spur = 0, edge = 0;
    int            dump = g_dev.margin_dump;
    memset(head, -1, sizeof head);
    for (i = 0; i < s_nw; ++i)
        for (e = 0; e < 2; ++e)
        {
            V2      p  = e ? s_walk[i].b : s_walk[i].a;
            int32_t tc = (int32_t)floorf(p.x), tr = (int32_t)floorf(p.y);
            int32_t id = 2 * i + e;
            if (tc < 0 || tr < 0 || tc >= R_MAP || tr >= R_MAP)
                continue;
            next[id]              = head[tr * R_MAP + tc];
            head[tr * R_MAP + tc] = id;
        }
    for (i = 0; i < s_nw; ++i)
    {
        if (s_walk[i].kind == MARGIN_STRIP)
            ++strips;
        else if (s_walk[i].kind == MARGIN_LINK)
            ++links;
        else
            ++caps;
        for (e = 0; e < 2; ++e)
        {
            V2      p  = e ? s_walk[i].b : s_walk[i].a;
            int32_t tc = (int32_t)floorf(p.x), tr = (int32_t)floorf(p.y), dc, dr;
            int     met = 0;
            if (p.x < WALK_EPS || p.y < WALK_EPS || p.x > (float)R_MAP - WALK_EPS || p.y > (float)R_MAP - WALK_EPS)
            {
                ++edge; /* the world beyond the map's edge has no margins */
                continue;
            }
            for (dr = -1; dr <= 1 && !met; ++dr)
                for (dc = -1; dc <= 1 && !met; ++dc)
                {
                    int32_t x = tc + dc, y = tr + dr, id;
                    if (x < 0 || y < 0 || x >= R_MAP || y >= R_MAP)
                        continue;
                    for (id = head[y * R_MAP + x]; id >= 0 && !met; id = next[id])
                    {
                        V2 q;
                        if (id / 2 == i)
                            continue;
                        q   = (id & 1) ? s_walk[id / 2].b : s_walk[id / 2].a;
                        met = fabsf(q.x - p.x) < WALK_EPS && fabsf(q.y - p.y) < WALK_EPS;
                    }
                }
            if (met)
                continue;
            if (past_carrier(c, p, e ? s_walk[i].ob : s_walk[i].oa))
            {
                ++carrier; /* the line runs on to a bridge, a tunnel, a meet or a band: the margin stops */
                continue;
            }
            if (past_spur(p, e ? s_walk[i].ob : s_walk[i].oa))
            {
                ++spur; /* a spur has no margin: it stops at the spur's mouth */
                continue;
            }
            ++open;
            if (dump)
            {
                /* and the nearest other end within a tile, to say how far off it is */
                float bd = 1.0f;
                int   bk = -1;
                V2    bq = {0.0f, 0.0f};
                for (dr = -1; dr <= 1; ++dr)
                    for (dc = -1; dc <= 1; ++dc)
                    {
                        int32_t x = tc + dc, y = tr + dr, id;
                        if (x < 0 || y < 0 || x >= R_MAP || y >= R_MAP)
                            continue;
                        for (id = head[y * R_MAP + x]; id >= 0; id = next[id])
                        {
                            V2    q;
                            float d;
                            if (id / 2 == i)
                                continue;
                            q = (id & 1) ? s_walk[id / 2].b : s_walk[id / 2].a;
                            d = fabsf(q.x - p.x) + fabsf(q.y - p.y);
                            if (d < bd)
                            {
                                bd = d;
                                bk = s_walk[id / 2].kind;
                                bq = q;
                            }
                        }
                    }
                dumpf("WALK open end: %s at %.2f,%.2f", s_walk[i].kind == MARGIN_STRIP ? "strip" : s_walk[i].kind == MARGIN_LINK ? "junction link"
                                                                                                                                     : "cap",
                      (double)p.x,
                      (double)p.y);
                if (bk >= 0)
                    dumpf("  nearest %s end at %.2f,%.2f (%.2f off)", bk == MARGIN_STRIP ? "strip" : bk == MARGIN_LINK ? "link"
                                                                                                                           : "cap",
                          (double)bq.x,
                          (double)bq.y,
                          (double)bd);
                dumpf("\n");
            }
        }
    }
    dumpf("margins  %d strips, %d junction links, %d caps; %d open ends (%d stop at a carrier, %d at a spur, %d at the map's edge)\n", strips, links, caps, open, carrier, spur, edge);
}

/*  The junction's margin.  It is the band round the box's outline, from
 *  the end of one mouth to the start of the next.  Every edge that is
 *  not a mouth, mitred at the outline's vertices.  It is in the margin
 *  material, with the line's across in it so the material draws the lip
 *  along the inner edge.  Over the box, and registered run by run. */
/*  Which edges of the outline carry a margin, and the inward normal of
 *  each: not an arm's mouth, not a sliver.  Only while the outline is
 *  still over the junction's own tile.  Past it the ground is whatever
 *  the neighbor's is, and a level band laid over that cuts into the
 *  hillside.  The margin simply stops there, as it does at any lip. */
/*  Which edges of a junction's ring carry a margin, which way each
 *  faces into the junction, and which arm's mouth each is: the SCRIPT'S
 *  (arc.rules.band).  The same call answers the ring moved in
 *  by the margin's width, since the two are one reading of the ring.
 *
 *  `inset` may be NULL where only the band is wanted. */
static V2 s_mitre0[JUNC_MAX], s_mitre1[JUNC_MAX];

/*  THE MARGIN ROUND ONE JUNCTION, as the drive had it answered.
 *
 *  How the margin sits on the ring is arc.rules.band's.  The DRIVE asks
 *  for it: once for the pass that measures the trims and once for the
 *  pass that draws the box.  This is because the two measure the band at
 *  widths a few millionths apart and each must read its own.  Nothing
 *  here calls up.  A script that answers nothing leaves the store empty
 *  and the junction has no margin at all.  Which is what lets the whole
 *  pass be left out from a script.
 *
 *  One junction's answer stands at a time: the drive asks for it
 *  immediately before the pass that reads it.  No two junctions are in
 *  hand at once. */
static struct
{
    int     have;
    Family  f;
    int32_t col, row;
    int     np;
    uint8_t band[JUNC_MAX];
    V2      nrm[JUNC_MAX];
    int8_t  edge_arm[JUNC_MAX];
    V2      mitre0[JUNC_MAX], mitre1[JUNC_MAX];
    V2      inset[JUNC_MAX];
    int     inset_n;
    BandFan fan;
} s_band;

/*  The ring, ready to be read as a margin: the handle the drive hands
 *  the rule, or nothing where this junction wants no margin. */
void *margin_band_ask(const JBox *jb, const V2 *poly, const JuncArm *arms, int np, float lw)
{
    int i;
    s_band.have = 0;
    if (!jb || np < 3 || np > JUNC_MAX || !s_on || !net_family(jb->f)->lips)
        return NULL;
    s_band.f = jb->f, s_band.col = jb->col, s_band.row = jb->row, s_band.np = np;
    for (i = 0; i < np; ++i)
    {
        s_band.band[i]     = 0;
        s_band.nrm[i]      = (V2){0.0f, 0.0f};
        s_band.edge_arm[i] = -1;
        s_band.mitre0[i] = s_band.mitre1[i] = (V2){0.0f, 0.0f};
    }
    s_band.inset_n = 0;
    memset(&s_band.fan, 0, sizeof s_band.fan);
    s_band.fan.jb = jb, s_band.fan.poly = poly, s_band.fan.arms = arms, s_band.fan.np = np;
    s_band.fan.lw       = lw;
    s_band.fan.band     = s_band.band;
    s_band.fan.nrm      = s_band.nrm;
    s_band.fan.edge_arm = s_band.edge_arm;
    s_band.fan.mitre0   = s_band.mitre0;
    s_band.fan.mitre1   = s_band.mitre1;
    s_band.fan.inset    = s_band.inset;
    s_band.fan.inset_max = JUNC_MAX;
    return &s_band.fan;
}

/*  And the answer taken: what the rule wrote is the junction's until the
 *  drive asks for another. */
void margin_band_answered(void)
{
    int i;
    s_band.inset_n = s_band.fan.inset_n;
    s_band.have    = 1;
    if (!g_dev.margin_dump)
        return;
    for (i = 0; i < s_band.np; ++i)
        dumpf("BAND %d,%d edge %2d band %d arm %d\n", (int)s_band.col, (int)s_band.row, i,
              s_band.band[i], s_band.edge_arm[i]);
}

/*  The answer, read back for one of the passes that needs it.  A ring
 *  the drive did not have answered, a different junction, or none, reads
 *  as no margin. */
static int junc_band_at(const JBox *jb, const V2 *poly, const JuncArm *arms, int np,
                        uint8_t *band, V2 *nrm, int8_t *edge_arm, V2 *inset, int inset_max)
{
    int i, n;
    (void)poly;
    (void)arms;
    for (i = 0; i < np; ++i)
    {
        band[i]     = 0;
        nrm[i]      = (V2){0.0f, 0.0f};
        edge_arm[i] = -1;
    }
    if (!s_band.have || s_band.f != jb->f || s_band.col != jb->col ||
        s_band.row != jb->row || s_band.np != np)
        return 0;
    for (i = 0; i < np; ++i)
    {
        band[i]     = s_band.band[i];
        nrm[i]      = s_band.nrm[i];
        edge_arm[i] = s_band.edge_arm[i];
        s_mitre0[i] = s_band.mitre0[i];
        s_mitre1[i] = s_band.mitre1[i];
    }
    n = s_band.inset_n < inset_max ? s_band.inset_n : inset_max;
    if (!inset)
        return 0;
    for (i = 0; i < n; ++i)
        inset[i] = s_band.inset[i];
    return n;
}

static void junc_band(const JBox *jb, const V2 *poly, const JuncArm *arms, int np, uint8_t *band, V2 *nrm, int8_t *edge_arm)
{
    junc_band_at(jb, poly, arms, np, band, nrm, edge_arm, NULL, 0);
}


/*  The margins, drawn from the NETWORK: every path's band, quad by quad
 *  between its cross-sections.  The network decided where they run and
 *  what they join (walk/walkway.c).  This knows only the two edges of a
 *  band and the height it lies at.
 *
 *  One quad in the margin material, the line's across in it (1.0 at the
 *  outline, 0.80 inside).  The material draws the lip along the inner
 *  edge itself, never under a pixel.  This a separate lip quad cannot do
 *  in the map view: it rasterises to dashes.  A path whose node reaches
 *  no chunk this build draws is passed over.  Its place in the network
 *  stands either way. */
/*  The network's own outline, drawn as the fitted curves are.  So what
 *  the network holds can be seen rather than inferred from the bands.
 *  It is the SCRIPT'S (arc.rules.walk_curves), one band at a time. */
/*  ONE MARGIN, gathered and handed over: which path it is, where its
 *  stations are and what it joins.  Answers 0 where there is nothing at
 *  that place to draw.
 *
 *  The shape is opened here, because only this knows what to call the
 *  path.  It runs beside a line, round a junction, round a terminus, or
 *  a meet, and what to note about it.  Nothing is drawn: the band is the
 *  script's, and with no rule there is no margin.
 *
 *  In outline the bands stand aside with the rest of the line works and
 *  the network is drawn in their place.  This is a different rule over
 *  the same paths.  `outline` says which the script should ask for. */
int margin_count(void)
{
    return s_on ? walk_net_count() : 0;
}

int margin_outline(void)
{
    return s_tune.show_curves > 0.5f;
}

int margin_gather(RMesh *m, const RCity *c, uint8_t mask_bit, int i, WalkFan *out, ShapeId *sh)
{
    const WalkPath *w  = walk_net_get(i);
    const WalkSt   *st = walk_net_st(w);
    char            pn0[48], pn1[48];
    *sh = SHAPE_NONE;
    if (!w)
        return 0;
    out->m = m, out->c = c, out->mask_bit = mask_bit, out->w = w, out->st = st;
    if (margin_outline())
        return 1; /* the outline draws every path, stations or not */
    if (!st || w->nst < 2)
        return 0; /* which chunks a band reaches is the emitter's to judge: a band crosses tiles */
    /*  Each margin is a thing of its own, named and asked about: the
     *  inspector points at a margin and is told which margin it is.
     *  This node and arm it belongs to and what it joins. */
    *sh = shape_open_under(w->owner, "%s at %d,%d%s",
                           w->kind == WALK_SIDE     ? "margin beside a line"
                           : w->kind == WALK_CORNER ? "margin round a junction"
                           : w->kind == WALK_CROSS  ? "meet"
                                                    : "margin round a terminus",
                           (int)w->col, (int)w->row, walk_arm_name(w->e));
    walk_port_name(w->port[0], pn0, sizeof pn0);
    walk_port_name(w->port[1], pn1, sizeof pn1);
    if (g_dev.margin_dump && w->kind == WALK_CORNER)
    {
        int q;
        dumpf("WALKST %d,%d %d stations:", (int)w->col, (int)w->row, w->nst);
        for (q = 0; q < w->nst; ++q)
            dumpf(" %.3f,%.3f->%.3f,%.3f", (double)st[q].outer.x, (double)st[q].outer.y, (double)st[q].inner.x, (double)st[q].inner.y);
        dumpf("\n");
    }
    if (w->kind == WALK_CROSS)
        shape_note("depth\t%.3f tiles of the %.3f it asked for\nfrom\t%s\nto\t%s", (double)w->w, (double)w->ask, pn0, pn1);
    else
        shape_note("width\t%.3f tiles\nstations\t%d\nfrom\t%s\nto\t%s", (double)w->w, w->nst, pn0, pn1);
    return 1;
}

/*  The port a margin names where it stops on an arm's mouth: the arm is
 *  the mouth's own tag.  The side is read off the mouth edge itself.
 *  The way OUT along the arm is from the middle of the box to the middle
 *  of that edge.  Side 0 is the right hand looking that way.  This is
 *  how a segment names its own sides. -1 where the edge is not a mouth. */
static int mouth_port(const JBox *jb, const JuncArm *arms, const int8_t *edge_arm, int np, int edge, V2 at)
{
    V2    d, mid;
    float side;
    int   e;
    if (!arms || !edge_arm || edge < 0 || edge >= np || edge_arm[edge] < 0)
        return -1;
    e    = edge_arm[edge];
    d    = arms[e].dir;
    mid  = arms[e].mid;
    side = (at.x - mid.x) * d.y - (at.y - mid.y) * d.x; /* positive: the right hand looking out */
    return walk_port(jb->col, jb->row, e, side > 0.0f ? 0 : 1);
}


/*  Where two lines meet, or 0 when they run parallel. */
static int line_cross(V2 a, V2 da, V2 b, V2 db, V2 *out)
{
    float den = da.x * db.y - da.y * db.x;
    float t;
    if (fabsf(den) < 1e-7f)
        return 0;
    t    = ((b.x - a.x) * db.y - (b.y - a.y) * db.x) / den;
    *out = (V2){a.x + da.x * t, a.y + da.y * t};
    return 1;
}

/*  Why a mouth carries no meet, in the order the tests are made. */
enum
{
    XR_MARKED = 0,
    XR_UNCONTROLLED,
    XR_NO_MARGIN,
    XR_NOT_PARALLEL,
    XR_NO_LINE, /* the line beyond the mouth is too short, or turns too soon, to carry a band */
    XR_REASONS
};

static int mouth_ends(const V2 *poly, const V2 *nrm, int np, int i, float lw, V2 *a, float *span);

/*  THE MOUTHS OF ONE JUNCTION, and what the drive answered about each.
 *
 *  Whether a mouth carries a meet is arc.rules.lap_at's.  It is asked
 *  once a mouth by the drive, off the band it has just had answered.
 *  The reading is made here and kept.  Nothing here calls up, so a
 *  script that answers nothing marks no meets anywhere. */
static struct
{
    int     n;
    Family  f;
    int32_t col, row;
    struct
    {
        int   arm, ctrl, pave;
        float cs, span, deep;
        int   marked;
    } m[JUNC_MAX];
} s_mouth;

/*  Every mouth of the junction whose band the drive has just had
 *  answered, read and made ready to be asked about.  Answers how many
 *  there are. */
int margin_mouths_ask(const V2 *poly, int np, float lw)
{
    int i;
    s_mouth.n = 0;
    if (!s_band.have || np != s_band.np)
        return 0;
    s_mouth.f = s_band.f, s_mouth.col = s_band.col, s_mouth.row = s_band.row;
    for (i = 0; i < np && s_mouth.n < JUNC_MAX; ++i)
    {
        int   ip = (i + np - 1) % np, j = (i + 1) % np, j2 = (j + 1) % np;
        V2    a[2], u, v;
        float lu, lv, span = 0.0f;
        if (s_band.edge_arm[i] < 0 || !mouth_ends(poly, s_band.nrm, np, i, lw, a, &span))
            continue;
        u  = (V2){poly[i].x - poly[ip].x, poly[i].y - poly[ip].y};
        v  = (V2){poly[j2].x - poly[j].x, poly[j2].y - poly[j].y};
        lu = sqrtf(u.x * u.x + u.y * u.y), lv = sqrtf(v.x * v.x + v.y * v.y);
        s_mouth.m[s_mouth.n].arm  = s_band.edge_arm[i];
        s_mouth.m[s_mouth.n].ctrl = net_family_has(net_family(s_band.f), NH_CONTROL)
                                        ? (s_junc_ctrl[s_band.row * R_MAP + s_band.col] >> (2 * s_band.edge_arm[i])) & 3
                                        : 0;
        s_mouth.m[s_mouth.n].pave = s_band.band[ip] && s_band.band[j];
        s_mouth.m[s_mouth.n].cs   = lu > 1e-5f && lv > 1e-5f ? (u.x * v.x + u.y * v.y) / (lu * lv) : 1.0f;
        s_mouth.m[s_mouth.n].span = span;
        s_mouth.m[s_mouth.n].deep = 0.0f;
        s_mouth.m[s_mouth.n].marked = 0;
        ++s_mouth.n;
    }
    return s_mouth.n;
}

int margin_mouth_at(int k, int32_t *col, int32_t *row, int *arm, int *ctrl, int *pave, float *cs, float *span)
{
    if (k < 0 || k >= s_mouth.n)
        return 0;
    *col = s_mouth.col, *row = s_mouth.row, *arm = s_mouth.m[k].arm;
    *ctrl = s_mouth.m[k].ctrl, *pave = s_mouth.m[k].pave;
    *cs = s_mouth.m[k].cs, *span = s_mouth.m[k].span;
    return 1;
}

void margin_mouth_is(int k, int marked, float deep)
{
    if (k < 0 || k >= s_mouth.n)
        return;
    s_mouth.m[k].marked = marked;
    s_mouth.m[k].deep   = deep;
}

/*  Whether a junction's mouth may carry a meet, from the outline alone.
 *  A stripe runs between two margins.  So the mouth needs a controlled
 *  arm and a margin on each side of it, the two running OPPOSITE ways
 *  round the ring.  Which is what one margin facing the other across a
 *  line looks like once the outline is walked in order.  Answers one of
 *  the reasons above.  XR_MARKED is a yes. */
static int mouth_wants(const RCity *c, Family f, int32_t col, int32_t row, const V2 *poly, int np,
                       const uint8_t *band, const int8_t *edge_arm, int i, float span, float *ask)
{
    int k;
    (void)c;
    (void)f;
    (void)poly;
    (void)np;
    (void)band;
    (void)span;
    if (edge_arm[i] < 0)
        return -1; /* not a mouth at all */
    for (k = 0; k < s_mouth.n; ++k)
    {
        if (s_mouth.col != col || s_mouth.row != row || s_mouth.m[k].arm != edge_arm[i])
            continue;
        if (!s_mouth.m[k].marked)
            return !s_mouth.m[k].ctrl ? XR_UNCONTROLLED : !s_mouth.m[k].pave ? XR_NO_MARGIN
                                                                             : XR_NOT_PARALLEL;
        if (ask)
            *ask = s_mouth.m[k].deep;
        return XR_MARKED;
    }
    return XR_UNCONTROLLED; /* the drive asked about no such mouth */
}

/*  The two ends of a meet at one mouth: where the mouth's own line meets
 *  the inner edge of the margin either side of it.  This are the points
 *  the junction's fill turns at.  So the band spans the way exactly,
 *  margin to margin.  How wide that span comes out is the mouth's own
 *  measure, in margin widths, and the rule that decides on the meet is
 *  given it. */
static int mouth_ends(const V2 *poly, const V2 *nrm, int np, int i, float lw, V2 *a, float *span)
{
    int ip = (i + np - 1) % np, j = (i + 1) % np, j2 = (j + 1) % np;
    V2  mo = poly[i], md = {poly[j].x - poly[i].x, poly[j].y - poly[i].y};
    V2  o0 = {poly[ip].x + nrm[ip].x * lw, poly[ip].y + nrm[ip].y * lw};
    V2  d0 = {poly[i].x - poly[ip].x, poly[i].y - poly[ip].y};
    V2  o1 = {poly[j].x + nrm[j].x * lw, poly[j].y + nrm[j].y * lw};
    V2  d1 = {poly[j2].x - poly[j].x, poly[j2].y - poly[j].y};
    if (!line_cross(mo, md, o0, d0, &a[0]) || !line_cross(mo, md, o1, d1, &a[1]))
        return 0;
    *span = lw > 1e-5f ? hypotf(a[1].x - a[0].x, a[1].y - a[0].y) / lw : 0.0f;
    return 1;
}

int margin_junction_wants(const RCity *c, Family f, int32_t col, int32_t row, const V2 *poly, const JuncArm *arms, int np, float lw, float *want)
{
    static uint8_t band[JUNC_MAX];
    static V2      nrm[JUNC_MAX];
    static int8_t  edge_arm[JUNC_MAX];
    JBox           jb = {0};
    int            i, n = 0;
    for (i = 0; i < 4; ++i)
        want[i] = 0.0f;
    if (!net_family(f)->lips || np < 3 || np > JUNC_MAX || !s_on)
        return 0;
    jb.f = f, jb.col = col, jb.row = row, jb.lw = lw, jb.c = c;
    junc_band(&jb, poly, arms, np, band, nrm, edge_arm);
    for (i = 0; i < np; ++i)
    {
        V2    a[2];
        float span = 0.0f, ask = 0.0f;
        if (edge_arm[i] < 0 || !mouth_ends(poly, nrm, np, i, lw, a, &span))
            continue;
        if (mouth_wants(c, f, col, row, poly, np, band, edge_arm, i, span, &ask) != XR_MARKED)
            continue;
        want[edge_arm[i]] = ask;
        ++n;
    }
    return n;
}

int margin_junction_inset(const JBox *jb, const V2 *poly, const JuncArm *arms, int np, V2 *out, int max)
{
    static uint8_t band[JUNC_MAX];
    static V2      nrm[JUNC_MAX];
    static int8_t  edge_arm[JUNC_MAX];
    int            n;
    if (!net_family(jb->f)->lips || np < 3 || np > JUNC_MAX || np > max || !s_on)
        return 0; /* no margin: the fill is laid on the outline itself */
    n = junc_band_at(jb, poly, arms, np, band, nrm, edge_arm, out, max);
    if (g_dev.margin_dump)
    {
        int q;
        dumpf("INSET %d,%d %d points:", (int)jb->col, (int)jb->row, n);
        for (q = 0; q < n; ++q)
            dumpf(" %.3f,%.3f", (double)out[q].x, (double)out[q].y);
        dumpf("\n");
    }
    return n;
}

int margin_junction(const JBox *jb, const V2 *poly, const JuncArm *arms, int np)
{
    const RCity *c        = jb->c;
    uint8_t      mask_bit = jb->mask_bit;
    float        order    = jb->order;
    int32_t      col      = jb->col;
    int32_t      row      = jb->row;
    Family       f        = jb->f;
    float        cx       = jb->cx;
    float        cy       = jb->cy;
    float        lw       = jb->lw;
    float        zj       = jb->zj;
    int          i;
    if (!net_family(f)->lips || np < 3 || !s_on) /* a margin rides on a lip: a family without has none */
        return 0;
    {
        static V2      nrm[JUNC_MAX];
        static uint8_t band[JUNC_MAX];
        static int8_t  edge_arm[JUNC_MAX];
        if (np > JUNC_MAX)
            return 0;
        junc_band(jb, poly, arms, np, band, nrm, edge_arm);
        /*  The meets this junction marks.  The band runs OUT from the
         *  mouth, over line the arm's trim has already given up for it.
         *  So the two meet along the mouth rather than one lying over
         *  the other.  Square to the arm by construction: the mouth is
         *  the cut across it, whatever the line does further out.
         *
         *  How deep a band the arm can spare was settled with the trims.
         *  This is the only place the length of the line beyond the
         *  mouth is known.  Here it is read back. */
        int tal[XR_REASONS] = {0, 0, 0, 0, 0}, mouths = 0, narrow = 0;
        for (i = 0; i < np; ++i)
        {
            int      e = edge_arm[i], why;
            float    deep, span = 0.0f, ask = 0.0f;
            V2       a[2], b[2];
            WalkPath w;
            WalkSt   st[2];
            if (e < 0)
                continue;
            ++mouths;
            if (!mouth_ends(poly, nrm, np, i, lw, a, &span))
                why = XR_NO_MARGIN;
            else
            {
                why  = mouth_wants(c, f, col, row, poly, np, band, edge_arm, i, span, &ask);
                deep = net_cross_depth(f, col, row, e);
                if (why == XR_MARKED && !(deep > 0.0f))
                    why = XR_NO_LINE;
            }
            ++tal[why];
            if (why != XR_MARKED)
                continue;
            if (deep < ask - 1e-4f)
                ++narrow;
            b[0] = (V2){a[0].x - nrm[i].x * deep, a[0].y - nrm[i].y * deep};
            b[1] = (V2){a[1].x - nrm[i].x * deep, a[1].y - nrm[i].y * deep};
            memset(&w, 0, sizeof w);
            w.kind    = WALK_CROSS;
            w.col     = col;
            w.row     = row;
            w.e       = e;
            w.w       = deep;
            w.ask     = ask;
            w.order   = order;
            w.drape   = 1; /* it lies on the arm's own graded ground, as the strip beyond it does */
            w.end[0]  = a[0];
            w.end[1]  = a[1];
            w.port[0] = mouth_port(jb, arms, edge_arm, np, i, a[0]);
            w.port[1] = mouth_port(jb, arms, edge_arm, np, i, a[1]);
            w.owner   = shape_current();
            /*  The mouth end first, so the along runs out from the
             *  junction and the stop line lands at the far end. */
            st[0]     = (WalkSt){a[0], a[1], zj};
            st[1]     = (WalkSt){b[0], b[1], zj};
            walk_net_add(&w, st, 2);
        }
        walk_cross_tally(mouths, tal[XR_UNCONTROLLED], tal[XR_NO_MARGIN], tal[XR_NOT_PARALLEL], tal[XR_NO_LINE], narrow);
        /*  The junction's MARGIN is one path from the end of a mouth to
         *  the start of the next.  It is one path whatever the outline
         *  does between them: a run of band edges.  Walked from just
         *  after an edge that is not banded, so a run never wraps in two
         *  halves.  A box with no mouth at all is one closed loop. */
        int           s0 = -1, k, run = 0, run_p = -1, nst = 0;
        V2            run_a = {0.0f, 0.0f}, run_o = {0.0f, 0.0f};
        static WalkSt st[JUNC_MAX + 2];
        for (i = 0; i < np; ++i)
            if (!band[i])
            {
                s0 = i;
                break;
            }
        for (k = 0; k < np; ++k)
        {
            int j, ip;
            i        = s0 < 0 ? k : (s0 + 1 + k) % np;
            j        = (i + 1) % np;
            ip       = (i + np - 1) % np;
            V2    p0 = poly[i], p1 = poly[j], m0, m1;
            float o0[2], o1[2], q0[2], q1[2];
            if (!band[i])
            {
                run = 0;
                nst = 0;
                continue;
            }
            m0 = s_mitre0[i];
            m1 = s_mitre1[i];
            {
                /*  Flat, at the highest ground any of its corners stands
                 *  on.  A band this narrow reads as level, and taking
                 *  each end's own height tilted it into the hillside
                 *  where the outline reaches past the pad. */
                float zf = zj;
                float za = junc_surface(jb->f, c, mask_bit, col, row, p0.x, p0.y, zj);
                float zb = junc_surface(jb->f, c, mask_bit, col, row, p1.x, p1.y, zj);
                if (za > zf)
                    zf = za;
                if (zb > zf)
                    zf = zb;
                /*  The MARGIN, the same one the arms carry, concrete
                 *  behind a lip, the line's outer fifth, so it meets
                 *  theirs at the mouths.  Not as one quad of the line
                 *  material.  That material draws its lip and its
                 *  scoring line at widths taken from screen-space
                 *  derivatives.  Those widths swell on this band's thin
                 *  mitred triangles and blotched it ("Z fighting on
                 *  their borders").  So there are two quads.  The
                 *  concrete is in the margin material, scored in world
                 *  space and the line's own walk color.  A lip along the
                 *  inner edge in the line material held inside its lip
                 *  zone, where the color is fixed.  Over the box, as the
                 *  old square corners were. */
                q0[0] = p0.x;
                q0[1] = p0.y;
                q1[0] = p1.x;
                q1[1] = p1.y;
                o0[0] = p0.x + m0.x * lw;
                o0[1] = p0.y + m0.y * lw;
                o1[0] = p1.x + m1.x * lw;
                o1[1] = p1.y + m1.y * lw;
                /*  The band's cross-sections, which is all the drawing
                 *  needs: the outline edge, the inner edge the offset
                 *  reaches, and the height the band lies at.  The first
                 *  station opens the run and every edge closes one. */
                if (nst == 0 && nst < (int)(sizeof st / sizeof st[0]))
                {
                    st[nst].outer = (V2){q0[0], q0[1]};
                    st[nst].inner = (V2){o0[0], o0[1]};
                    st[nst].z     = zf;
                    ++nst;
                }
                if (nst < (int)(sizeof st / sizeof st[0]))
                {
                    st[nst].outer = (V2){q1[0], q1[1]};
                    st[nst].inner = (V2){o1[0], o1[1]};
                    st[nst].z     = zf;
                    ++nst;
                }
                /*  The run's ends, on the band's own centerline.  Each
                 *  carries the way OUT through the mouth it stops at,
                 *  the mouth edge's normal away from the middle.  So the
                 *  check can see a spur or a carrier beyond it, where a
                 *  margin rightly stops. */
                if (!run)
                {
                    V2    pp = poly[ip], e2 = {p0.x - pp.x, p0.y - pp.y};
                    float el2 = sqrtf(e2.x * e2.x + e2.y * e2.y);
                    run       = 1;
                    run_a     = (V2){p0.x + m0.x * lw * 0.5f, p0.y + m0.y * lw * 0.5f};
                    run_o     = el2 > 1e-5f ? (V2){-e2.y / el2, e2.x / el2} : (V2){0.0f, 0.0f};
                    if (run_o.x * (p0.x - cx) + run_o.y * (p0.y - cy) < 0.0f)
                        run_o = (V2){-run_o.x, -run_o.y};
                    run_p = mouth_port(jb, arms, edge_arm, np, ip, poly[i]); /* the arm this corner starts at */
                }
                if (!band[j] || k + 1 == np)
                {
                    V2    pn = poly[(j + 1) % np], e2 = {pn.x - p1.x, pn.y - p1.y};
                    float el2 = sqrtf(e2.x * e2.x + e2.y * e2.y);
                    V2    ob  = el2 > 1e-5f ? (V2){-e2.y / el2, e2.x / el2} : (V2){0.0f, 0.0f};
                    V2    rb  = {p1.x + m1.x * lw * 0.5f, p1.y + m1.y * lw * 0.5f};
                    if (ob.x * (p1.x - cx) + ob.y * (p1.y - cy) < 0.0f)
                        ob = (V2){-ob.x, -ob.y};
                    margin_add(MARGIN_LINK, run_a, rb, run_o, ob);
                    {
                        /*  The corner, as the network holds it: from the
                         *  arm it left to the arm it reaches.  Both ports
                         *  are the segments' own, so a corner and the two
                         *  sides it joins are one walk by name. */
                        WalkPath w;
                        memset(&w, 0, sizeof w);
                        w.kind    = WALK_CORNER;
                        w.col     = col;
                        w.row     = row;
                        w.e       = -1;
                        w.w       = lw;
                        w.end[0]  = run_a;
                        w.end[1]  = rb;
                        w.out[0]  = run_o;
                        w.out[1]  = ob;
                        w.port[0] = run_p;
                        w.port[1] = mouth_port(jb, arms, edge_arm, np, j, p1); /* the corner stops at the mouth's near end, poly[j] */
                        w.order   = order;
                        w.owner   = shape_current(); /* the junction it is part of, laid later */
                        walk_net_add(&w, st, nst);
                    }
                    run = 0;
                    nst = 0;
                }
            }
        }
    }
    return 0;
}
