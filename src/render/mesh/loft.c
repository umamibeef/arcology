/*  The loft: one strip of any family from its pieces: stations, ground,
 *  profile, the records the traffic and the passes read, and the slab. */
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dump.h"
#include "mesh/internal.h"
#include "pipeline.h"

#include "net/net.h"
static int gix_loft_split_probe = -1;
#include "opt.h"

/*  The numbers this file reads, each remembering where the store
 *  put it (net/geo.c).  A name here is a name a script must set. */


RLoft               s_ldv; /* the strip being lofted.  Net/loft.c drives its stages */
const RLoft        *s_ld = &s_ldv; /* the strip being lofted, for this module's helpers.  Other modules read Loft.d */

/*  ==================================================================
 *  Stations, records and the knobs
 *
 *  What the cross-section stands on, what the traffic model is told, and
 *  the live tuning values.
 *  ================================================================== */
/*  The height a cross-section sits at: the drawn ground under its
 *  center.  Nothing more: the corridor is graded, the terrain is raised
 *  or lowered to the line, and the band lies on the terrain. */
float section_height(const RCity *c, uint8_t mask_bit, V2 pos, V2 dir, float h)
{
    (void)dir;
    (void)h;
    return surface_at_world(c, mask_bit, pos.x, pos.y);
}

/*  Loft one strip along the pieces.  The cross-sections come by arc
 *  length.  Their width is the class's, scaled by the direction, which
 *  is the snap view's compensation.  There is one quad per pair, each
 *  with the painter's order of the tile under it. */
/*  Record a line segment's stations in the mesh's network, for the
 *  traffic.  The lane centers by class, tiles from the centerline.
 *
 *      A line's one lane each way at half the way.  An avenue's outer
 *      and inner lanes.  A boulevard's outer and inner between median
 *      and lip.  Where the material draws its lines. */
int net_record(RNet *net, const Sample *smp, int ns, float total, int cls, const RLoft *d)
{
    RNetSeg *sg;
    int      i;
    if (net->n_pts + (uint32_t)ns > net->cap_pts)
    {
        uint32_t nc = net->cap_pts ? net->cap_pts * 2u : 4096u;
        RNetPt  *np;
        while (nc < net->n_pts + (uint32_t)ns)
            nc *= 2u;
        np = (RNetPt *)realloc(net->pts, nc * sizeof *np);
        if (!np)
            return -1;
        net->pts     = np;
        net->cap_pts = nc;
    }
    if (net->n_segs + 1u > net->cap_segs)
    {
        uint32_t nc = net->cap_segs ? net->cap_segs * 2u : 256u;
        RNetSeg *np = (RNetSeg *)realloc(net->segs, nc * sizeof *np);
        if (!np)
            return -1;
        net->segs     = np;
        net->cap_segs = nc;
    }
    sg        = &net->segs[net->n_segs++];
    sg->first = net->n_pts;
    sg->count = (uint32_t)ns;
    sg->total = total;
    sg->cls   = cls;
    memcpy(sg->node, d->node, sizeof sg->node);
    sg->kind[0] = d->nkind[0];
    sg->kind[1] = d->nkind[1];
    sg->ctrl[0] = d->ctrl[0];
    sg->ctrl[1] = d->ctrl[1];
    /*  Where the traffic runs, across the strip.  These are fractions of
     *  the line's own width.  So a change of width carries the lanes
     *  with it instead of leaving the cars off the fill.  Which
     *  fractions they are is the FAMILY'S stage and every family answers
     *  it with a rule.  So a lane the cars run on and a lane the paint
     *  marks cannot part company.  No stage is no traffic on the strip. */
    sg->lane_out = sg->lane_in = 0.0f;
    if (net_family_has(d->fam, NH_TRAFFIC))
        net_family_traffic(d->fam, d, cls, &sg->lane_in, &sg->lane_out);
    for (i = 0; i < ns; ++i)
    {
        RNetPt *q = &net->pts[net->n_pts++];
        q->x      = smp[i].pos.x;
        q->y      = smp[i].pos.y;
        q->z      = smp[i].z;
        q->dx     = smp[i].dir.x;
        q->dy     = smp[i].dir.y;
        q->s      = smp[i].s;
    }
    return 0;
}

/*  The point and unit direction at distance `t` along a piece. */
void piece_at(const Piece *p, float t, V2 *pos, V2 *dir)
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


float        s_zorig[LOFT_MAX_ST];

/*  The profile's own height at an arc length, interpolated between the
 *  stations either side of it.  The shelf is this function of distance
 *  along the segment and nothing else.  So it is ONE continuous surface
 *  the whole length of the corridor: flat across it, because the across
 *  coordinate does not appear.  Two tiles that share a corner ask for
 *  the same arc length and get the same answer. */
float profile_at(const Sample *smp, int ns, float at)
{
    int lo = 0, hi = ns - 1, mid;
    if (ns < 2)
        return ns ? smp[0].z : 0.0f;
    if (at <= smp[0].s)
        return smp[0].z;
    if (at >= smp[ns - 1].s)
        return smp[ns - 1].z;
    while (hi - lo > 1)
    {
        mid = (lo + hi) / 2;
        if (smp[mid].s <= at)
            lo = mid;
        else
            hi = mid;
    }
    {
        float d = smp[hi].s - smp[lo].s;
        float t = d > 1e-6f ? (at - smp[lo].s) / d : 0.0f;
        return smp[lo].z + (smp[hi].z - smp[lo].z) * t;
    }
}


/*  The stations: along every piece, finely on arcs, and one on every tile edge the band crosses. */
/*  Where the band crosses a line the ground can crease on, between two
 *  stations of a piece.  Those lines are the tile edges.  They are also
 *  the tile's own diagonal, where its two triangles meet, for the
 *  centerline and both edges, each found by bisection.  In order along
 *  the piece, each once.  Without the diagonals a strip meet a tile
 *  corner to corner passed under the crest between them.  This is what
 *  the line clip check found on four cities.  Returns how many, at most
 *  `cap`. */
static int sample_laps(const Piece *p, float t0, float t1, V2 pos, V2 dir, float hw, float *tc, int cap)
{
    int ntc = 0, side, pass, q;
    for (side = -1; side <= 1; ++side)
        for (pass = 0; pass < 4; ++pass)
        {
            /*  A station wherever the band crosses a line the ground can
             *  crease on: the tile edges, and the tile's own diagonal.
             *  This is where its two triangles meet.  Without the
             *  diagonals a strip meet a tile corner to corner passed
             *  under the crest between them.  This is what the line clip
             *  check found on four cities. */
            float off = (float)side * hw;
            V2    p0, d0, q0, q1;
            piece_at(p, t0, &p0, &d0);
            q0 = (V2){p0.x + d0.y * off, p0.y - d0.x * off};
            q1 = (V2){pos.x + dir.y * off, pos.y - dir.x * off};
            {
                float a0 = pass == 0   ? q0.x
                           : pass == 1 ? q0.y
                           : pass == 2 ? q0.x + q0.y
                                       : q0.x - q0.y;
                float a1 = pass == 0   ? q1.x
                           : pass == 1 ? q1.y
                           : pass == 2 ? q1.x + q1.y
                                       : q1.x - q1.y;
                if (floorf(a0) != floorf(a1) && ntc < cap)
                {
                    float lo = t0, hi = t1, edge = floorf(a1 > a0 ? a1 : a0);
                    V2    pb, db, qb;
                    int   it;
                    for (it = 0; it < 12; ++it)
                    {
                        float mid = 0.5f * (lo + hi), v;
                        piece_at(p, mid, &pb, &db);
                        qb = (V2){pb.x + db.y * off, pb.y - db.x * off};
                        v  = pass == 0   ? qb.x
                             : pass == 1 ? qb.y
                             : pass == 2 ? qb.x + qb.y
                                         : qb.x - qb.y;
                        if ((v < edge) == (a0 < edge))
                            lo = mid;
                        else
                            hi = mid;
                    }
                    if (hi - t0 > 1e-3f && t1 - hi > 1e-3f)
                        tc[ntc++] = hi;
                }
            }
        }
    /* in order along the piece, each once */
    for (q = 1; q < ntc; ++q)
    {
        float v = tc[q];
        int   r = q;
        while (r > 0 && tc[r - 1] > v)
        {
            tc[r] = tc[r - 1];
            --r;
        }
        tc[r] = v;
    }
    return ntc;
}

static int loft_sample(Loft *x)
{
    Sample      *smp   = x->smp;
    float        hw    = x->hw;
    float        total = x->total;
    const Piece *pc    = x->pc;
    int          np    = x->np;
    int          ns    = x->ns;
    int          k;
    int          i;
    float        s = 0.0f;
    if (g_dev.loft_dump)
    {
        float tl = 0.0f;
        for (k = 0; k < np; ++k)
            tl += pc[k].len;
        dumpf("loft: %d pieces, total %.2f (sum %.2f), first (%.2f,%.2f)\n", np, (double)total, (double)tl, (double)pc[0].a.x, (double)pc[0].a.y);
        /* every piece to the bit, for a diff between two builds' fits */
        dumpf("  pieces total %a:", (double)total);
        for (k = 0; k < np; ++k)
            if (pc[k].arc)
                dumpf(" [arc c %a %a r %a t %a %a len %a]", (double)pc[k].c.x, (double)pc[k].c.y, (double)pc[k].r, (double)pc[k].t0, (double)pc[k].t1, (double)pc[k].len);
            else
                dumpf(" [line %a %a to %a %a len %a]", (double)pc[k].a.x, (double)pc[k].a.y, (double)pc[k].b.x, (double)pc[k].b.y, (double)pc[k].len);
        dumpf("\n");
    }
    for (k = 0; k < np; ++k)
    {
        const Piece *p = &pc[k];
        /*  How finely the strip is cut along its length.  An arc gets a
         *  station every twenty-fifth of a tile and a straight every
         *  sixteenth.  Fine enough that a curve reads as a curve at the
         *  closest zoom and against the map view's own grid. */
        int nd = p->arc ? (int)ceilf(p->len / net_family_rules(s_ld->fam->f)->step_arc) : (int)ceilf(p->len / net_family_rules(s_ld->fam->f)->step_run);
        if (nd < 1)
            nd = 1;
        for (i = (ns ? 1 : 0); i <= nd; ++i)
        {
            float t = p->len * (float)i / (float)nd;
            V2    pos, dir;
            if (ns >= LOFT_MAX_ST - 2)
                break;
            piece_at(p, t, &pos, &dir);
            /*  A station on every tile edge the centerline crosses,
             *  found by bisection.  So a crease in the ground is a
             *  station and never a chord's underside.  A crest at the
             *  edge between a rising and a falling tile is such a
             *  crease, and never a chord's underside. */
            if (ns > 0 && i > 0)
            {
                /*  The centerline and both edges of the band each cross
                 *  the tile edges at their own point.  A station at
                 *  every meet, in order along the piece.  So a wall the
                 *  band crosses obliquely is met at a station on the
                 *  side it first reaches. */
                float t0 = p->len * (float)(i - 1) / (float)nd, t1 = t;
                float tc[17];
                int   ntc = sample_laps(p, t0, t1, pos, dir, hw, tc, 17), q;
                for (q = 0; q < ntc && ns < 8190; ++q)
                {
                    V2 pb, db;
                    if (q > 0 && tc[q] - tc[q - 1] < 2e-3f)
                        continue;
                    piece_at(p, tc[q], &pb, &db);
                    smp[ns].pos   = pb;
                    smp[ns].dir   = db;
                    smp[ns].s     = s + tc[q];
                    smp[ns].xd    = 1e9f;
                    smp[ns].split = 1; /* both tiles meet here: loft_ground reads the higher */
                    ++ns;
                }
            }
            smp[ns].pos   = pos;
            smp[ns].dir   = dir;
            smp[ns].s     = s + t;
            smp[ns].split = 0;
            smp[ns].xd              = 1e9f; /* no meet near: a strip that is crossed measures them */
            smp[ns].wr = smp[ns].wl = 1.0f;
            smp[ns].lane            = 0;
            ++ns;
        }
        s += p->len;
    }
    x->ns = ns;
    return 0;
}

/*  The profile: the height of every station: a line on its shelf, a slab on its columns, with the lift and the lane drop. */
/*  The raw ground under every station, read from the surface as it is
 *  now.  In the grading pass, as far as the segments walked so far have
 *  graded it.  In the building pass, graded whole.  So it is never
 *  cached with the geometry.  A slab reads the ground a hair beyond its
 *  own edges: on a cross-slope its uphill edge sits on the tile
 *  boundary.  The ground there is the higher neighbor's corner.  The
 *  slab rises to clear it, rather than the hillside being cut.  An empty
 *  tile beside a band's end can stand 0.15 over the slab's edge).  Where
 *  two tiles meet, maybe at a small wall: the higher. */
static void loft_ground(Loft *x)
{
    const RCity *c        = x->c;
    uint8_t      mask_bit = x->mask_bit;
    float        h        = x->hw + s_ld->ground_margin;
    int          i;
    for (i = 0; i < x->ns; ++i)
    {
        const Sample *sm = &x->smp[i];
        if (sm->split)
        {
            float d0   = geo_num(&gix_loft_split_probe, "loft_split_probe");
            V2    pm   = {sm->pos.x - sm->dir.x * d0, sm->pos.y - sm->dir.y * d0};
            V2    pp   = {sm->pos.x + sm->dir.x * d0, sm->pos.y + sm->dir.y * d0};
            float zm   = section_height(c, mask_bit, pm, sm->dir, h);
            float zp   = section_height(c, mask_bit, pp, sm->dir, h);
            x->zraw[i] = zm > zp ? zm : zp;
        }
        else
            x->zraw[i] = section_height(c, mask_bit, sm->pos, sm->dir, h);
    }
}

/*  The corridor's profile of a strip on the ground.  It is a spur
 *  between the altitudes of the NODES at its ends, and through any level
 *  meet on the way.  A node is a junction, a dead end or a lap.  It
 *  stands at its own tile's leveled height, and every corridor that
 *  reaches it spurs to that one number.  Two segments meeting at a
 *  junction therefore agree, without anything being solved between them.
 *  Two families agree at a lap because the meet is a node they share.
 *  An edit moves only the segments whose anchors moved.  The spur is
 *  eased at both ends.  So a corridor leaves a node level and picks up
 *  its grade in between rather than kinking at the join. */
/*  Station i: how far along the strip it is.  The altitude an end is
 *  pinned to, and the altitude a level meet under station i pins it to.
 *  A meet is a node of BOTH networks, so the line and the families are
 *  pinned to the same number there. */
int loft_ground_at(const GroundFan *g, int i, float *at, float *z)
{
    const Sample *smp = (const Sample *)g->smp;
    if (i < 0 || i >= g->n)
        return 0;
    *at = smp[i].s;
    *z  = smp[i].z;
    return 1;
}

int loft_ground_node(const GroundFan *g, int which, float *z)
{
    const RCity *c = (const RCity *)g->city;
    *z             = node_altitude(c, s_ld->node[which][0], s_ld->node[which][1]) + g->lift;
    return 1;
}

int loft_ground_lap(const GroundFan *g, int i, float *z)
{
    const RCity  *c   = (const RCity *)g->city;
    const Sample *smp = (const Sample *)g->smp;
    int32_t       tc, tr;
    if (i < 0 || i >= g->n)
        return 0;
    tc = (int32_t)floorf(smp[i].pos.x);
    tr = (int32_t)floorf(smp[i].pos.y);
    if (tc < 0 || tr < 0 || tc >= R_MAP || tr >= R_MAP)
        return 0;
    if (!net_line_lapped(c->xbld[tr * R_MAP + tc]))
        return 0;
    *z = node_altitude(c, tc, tr) + g->lift;
    return 1;
}

void loft_ground_set(GroundFan *g, int i, float z)
{
    if (i >= 0 && i < g->n)
        ((Sample *)g->smp)[i].z = z;
}







/*  The slab: the strip's own surface, one quad per pair of stations.  It
 *  is COMPOSED BY THE SCRIPT (arc.rules.strip).  The fit gives the
 *  centerline and the grading the heights.  The script walks the
 *  stations and lays the ribbon between them through the same emitter
 *  every other band goes through.  Nothing here decides how wide the
 *  band is, what it is made of or where it sits in the stack. */

/*  And the loft still in hand, between the stages the drive walks. */
Loft             s_lx;      /* the strip in flight, for the drive in net/strip.c */
int              s_lx_live;
double           s_lx_tp;

/*  One pair of stations as the family's own stages want it, which the
 *  script asks for when a family builds beside its quads. */
static int loft_pair(Loft *x, int i, LoftPair *p);

int script_strip_pair(void *loft, int i, void *pair)
{
    return loft_pair((Loft *)loft, i, (LoftPair *)pair);
}

/*  One station between two, where a junction's meet band cuts the pair
 *  that straddles its edge.  The composition does the same in the mesh's
 *  own precision (scripts/compose/strip.lua).  This is here for the
 *  stages a family builds beside its quads, which want the pair as the
 *  quad itself was cut. */
static void sample_lerp(const Sample *a, const Sample *b, float s, Sample *out)
{
    float d = b->s - a->s, t = d > 1e-6f ? (s - a->s) / d : 0.0f;
    if (t < 0.0f)
        t = 0.0f;
    if (t > 1.0f)
        t = 1.0f;
    *out     = *a;
    out->pos = (V2){a->pos.x + (b->pos.x - a->pos.x) * t, a->pos.y + (b->pos.y - a->pos.y) * t};
    out->dir = (V2){a->dir.x + (b->dir.x - a->dir.x) * t, a->dir.y + (b->dir.y - a->dir.y) * t};
    out->z   = a->z + (b->z - a->z) * t;
    out->s   = s;
    out->wl  = a->wl + (b->wl - a->wl) * t;
    out->wr  = a->wr + (b->wr - a->wr) * t;
    out->xd  = a->xd + (b->xd - a->xd) * t;
    {
        float l = sqrtf(out->dir.x * out->dir.x + out->dir.y * out->dir.y);
        if (l > 1e-6f)
            out->dir = (V2){out->dir.x / l, out->dir.y / l};
    }
}

static int loft_pair(Loft *x, int i, LoftPair *p)
{
    static Sample cut0, cut1;
    const Sample *pv = &x->smp[i - 1], *cu = &x->smp[i];
    float         inset = (s_ld->fam->lips && margin_on()) ? net_family_rules(s_ld->fam->f)->inner : 1.0f;
    float         ha;
    /*  Cut at the meet band's edge, as the quad itself is. */
    if (s_ld->xw0 > 0.0f && pv->s < s_ld->xw0)
        sample_lerp(pv, cu, s_ld->xw0, &cut0), pv = &cut0;
    if (s_ld->xw1 > 0.0f && cu->s > x->total - s_ld->xw1)
        sample_lerp(pv, cu, x->total - s_ld->xw1, &cut1), cu = &cut1;
    ha = x->hw * width_factor(pv->dir.x, pv->dir.y, x->comp) * inset;
    float         hb = x->hw * width_factor(cu->dir.x, cu->dir.y, x->comp) * inset;
    float         mx = 0.5f * (pv->pos.x + cu->pos.x), my = 0.5f * (pv->pos.y + cu->pos.y);
    int32_t       tc = (int32_t)floorf(mx), tr = (int32_t)floorf(my);
    if (tc < 0)
        tc = 0;
    if (tr < 0)
        tr = 0;
    if (tc >= R_MAP)
        tc = R_MAP - 1;
    if (tr >= R_MAP)
        tr = R_MAP - 1;
    memset(p, 0, sizeof *p);
    p->i = i, p->pv = pv, p->cu = cu, p->ha = ha, p->hb = hb;
    p->a0[0] = pv->pos.x + pv->dir.y * ha * pv->wr, p->a0[1] = pv->pos.y - pv->dir.x * ha * pv->wr;
    p->a1[0] = pv->pos.x - pv->dir.y * ha * pv->wl, p->a1[1] = pv->pos.y + pv->dir.x * ha * pv->wl;
    p->b0[0] = cu->pos.x + cu->dir.y * hb * cu->wr, p->b0[1] = cu->pos.y - cu->dir.x * hb * cu->wr;
    p->b1[0] = cu->pos.x - cu->dir.y * hb * cu->wl, p->b1[1] = cu->pos.y + cu->dir.x * hb * cu->wl;
    p->tc = tc, p->tr = tr;
    p->order = tile_order(x->c, tc, tr, x->mask_bit);
    p->acr = -0.5f * (pv->wr + cu->wr) * inset;
    p->acl = 0.5f * (pv->wl + cu->wl) * inset;
    p->ma = x->mat, p->al_a = pv->s, p->al_b = cu->s;
    return 0;
}

/*  The ground's own line at a station: a station below it is in a cut. */
float script_strip_zorig(void *loft, int i)
{
    Loft *x = (Loft *)loft;
    return x && i >= 0 && i < x->ns ? s_zorig[i] : 0.0f;
}

/*  A line of the note, appended while there is room. */
void note_add(char *buf, size_t cap, size_t *n, const char *fmt, ...)
{
    va_list ap;
    int     r;
    if (*n >= cap)
        return;
    va_start(ap, fmt);
    r = vsnprintf(buf + *n, cap - *n, fmt, ap);
    va_end(ap);
    if (r < 0)
        return;
    *n += (size_t)r;
    if (*n > cap)
        *n = cap;
}


/*  The strip, and who asked for it: the caller's name is in force while
 *  this runs.  So every component the loft makes records what generated
 *  it and not merely the stage that pushed the triangles. */
static int loft_body(RMesh *m, const RCity *c, uint8_t mask_bit, int comp, const RLoft *desc, const Piece *pc, int np, float total);

int loft_at(const char *where, const char *who, RMesh *m, const RCity *c, uint8_t mask_bit, int comp, const RLoft *desc, const Piece *pc, int np, float total)
{
    ShapeId sh;
    int     rc;
    /*  The strip is a shape, named for what it is.  Everything the loft
     *  draws into it is part of that shape, or of one opened inside it.
     *  That is the slab, a slab's gores and soffit, and its piers.  One
     *  opened inside it. */
    switch (desc->kind)
    {
    case LOFT_SLAB:
        sh = shape_open_at(where, who, "band slab, band %d", desc->band);
        break;
    case LOFT_SPUR:
        sh = shape_open_at(where, who, "%s spur%s", desc->lane_off ? "off" : "on", desc->lane_piece ? " lane" : "");
        break;
    default:
        if (desc->node[0][0] == desc->node[1][0] && desc->node[0][1] == desc->node[1][1])
            sh = shape_open_at(where, who, "%s island at %d,%d", desc->fam->name, (int)desc->node[0][0], (int)desc->node[0][1]);
        else
            sh = shape_open_at(where, who, "%s strip %d,%d to %d,%d", desc->fam->name, (int)desc->node[0][0], (int)desc->node[0][1], (int)desc->node[1][0], (int)desc->node[1][1]);
        break;
    }
    rc = loft_body(m, c, mask_bit, comp, desc, pc, np, total);
    /*  The shape stays OPEN.  The slab is laid outside the loft.  Its
     *  triangles belong to the strip like every other stage's, so
     *  net_loft_compose is what closes it. */
    s_slab_sh = sh;
    if (rc != 0)
    {
        shape_close(sh);
        s_slab_sh    = SHAPE_NONE;
        s_slab_ready = 0;
    }
    return rc;
}

static int loft_body(RMesh *m, const RCity *c, uint8_t mask_bit, int comp, const RLoft *desc, const Piece *pc, int np, float total)
{
    static Sample smp[LOFT_MAX_ST];
    static float  zraw[LOFT_MAX_ST];
    Loft          x;
    s_ldv      = *desc;
    x.d        = &s_ldv;
    x.m        = m;
    x.c        = c;
    x.mask_bit = mask_bit;
    x.comp     = comp;
    x.f        = desc->f;
    x.pin0     = desc->pin0;
    x.pin1     = desc->pin1;
    x.smp      = smp;
    x.zraw     = zraw;
    x.hw       = desc->hw; /* the family's half width, or a slab's */
    x.mat      = desc->mat;
    x.total    = total;
    x.pc       = pc;
    x.np       = np;
    x.ns       = 0;
    /*  The stages, in order: stations along the pieces.  The profile
     *  (the height at each station: a line's on its shelf, a slab's on
     *  its columns, with the lift and the lane drop).  A slab's works.
     *  The network record for the traffic, the furniture, the corridor
     *  surface and the curve overlay.  And the slab itself. */
    double tp = prof_now(), tq;
    if (!g_dev.no_smp_cache && loft_cached(&x) == 0)
        net_prof_add(NET_PROF_CACHED, 1.0);
    else
    {
        if (loft_sample(&x) != 0)
            return -1;
        loft_keep(&x);
        net_prof_add(NET_PROF_SAMPLED, 1.0);
    }
    net_prof_add(NET_PROF_STATIONS, (double)x.ns);
    tq = prof_now(), net_prof_add(NET_PROF_SAMPLE, tq - tp), tp = tq;
    loft_ground(&x);
    tq = prof_now(), net_prof_add(NET_PROF_GROUND, tq - tp), tp = tq;
    /*  Every station full width until something narrows it: the buffer
     *  is static.  A slab's lane drop is not the only thing that writes
     *  them.  So a spur lofted after a slab must not inherit the slab's
     *  gore taper at whatever stations share an index. */
    {
        int q;
        for (q = 0; q < x.ns; ++q)
            x.smp[q].wl = x.smp[q].wr = 1.0f;
    }
    /*  The loft stops here.  What is left of it, the narrowing, the
     *  profile, the works, the record and the slab, is a stage the
     *  scripts may answer.  So the drive walks them one at a time with
     *  everything the loft has worked out still standing. */
    s_lx      = x;
    s_lx_tp   = tp;
    s_lx_live = 1;
    return 0;
}

