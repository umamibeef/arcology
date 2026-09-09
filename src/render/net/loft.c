/*  The loft: one strip of any family from its pieces -- stations,
 *  ground, profile, the records the traffic and the passes read, and
 *  the slab. */
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dump.h"
#include "mesh/internal.h"
#include "net/internal.h"
#include "opt.h"

/*  The numbers this file reads, each remembering where the store
 *  put it (net/geo.c).  A name here is a name a script must set. */


static RLoft        s_ldv;
static const RLoft *s_ld = &s_ldv; /* the strip being lofted, for this module's helpers; other modules read Loft.d */

/*  ==================================================================
 *  Stations, records and the knobs
 *
 *  What the cross-section stands on, what the traffic model is told, and
 *  the live tuning values.
 *  ================================================================== */
/*  The height a cross-section sits at: the drawn ground under its centre,
 *  and nothing more: the corridor is graded, the terrain is raised or
 *  lowered to the line, and the band lies on the terrain. */
float section_height(const RCity *c, uint8_t mask_bit, V2 pos, V2 dir, float h)
{
    (void)dir;
    (void)h;
    return surface_at_world(c, mask_bit, pos.x, pos.y);
}

/*  Loft one strip along the pieces: cross-sections by arc length, their
 *  width the class's scaled by the direction (the snap view's
 *  compensation), one quad per pair, each with the painter's order of
 *  the tile under it. */
/*  Record a road segment's stations in the mesh's network, for the
 *  traffic.  The lane centres by class, tiles from the centreline: a
 *  road's one lane each way at half the carriageway, an avenue's outer
 *  and inner lanes, a boulevard's outer and inner between median and
 *  curb, where the material draws its lines. */
int net_record(RRoadNet *net, const Sample *smp, int ns, float total, int cls, int rail, const RLoft *d)
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
     *  the road's own width, so a change of width carries the lanes with
     *  it instead of leaving the cars off the asphalt. */
    sg->lane_out = sg->lane_in = 0.0f;
    if (d->fam->traffic_lanes)
        d->fam->traffic_lanes(d, cls, &sg->lane_in, &sg->lane_out);
    {
    }
    if (rail)
        {
            /*  A rail: the track to the right of travel, where the
             *  connectors run it. */
            float off[2] = {0.0f, 0.0f};
            net_lane_offsets(F_RAIL, 0, off, 2);
            sg->lane_out = sg->lane_in = off[0];
        }
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

/*  The point and unit direction at distance `t` along a chain of pieces,
 *  clamped to its ends. */
void pieces_at(const Piece *pc, int np, float t, V2 *pos, V2 *dir)
{
    int k;
    if (np < 1)
    {
        *pos = (V2){0.0f, 0.0f};
        *dir = (V2){1.0f, 0.0f};
        return;
    }
    for (k = 0; k < np - 1 && t > pc[k].len; ++k)
        t -= pc[k].len;
    piece_at(&pc[k], t < 0.0f ? 0.0f : (t > pc[k].len ? pc[k].len : t), pos, dir);
}

static float s_zorig[LOFT_MAX_ST];

/*  The profile's own height at an arc length, interpolated between the
 *  stations either side of it.  The shelf is this function of distance
 *  along the segment and nothing else, so it is ONE continuous surface the
 *  whole length of the corridor -- flat across it, because the across
 *  coordinate does not appear.  Two tiles that share a corner ask for the
 *  same arc length and get the same answer. */
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

/*  The fitted line drawn over the world it made -- centreline, band
 *  edges and piece boundaries -- when the tuning window asks to see
 *  them.  It is the SCRIPT'S (arc.rules.curves), from the same stations
 *  and pieces the strip itself is laid from. */
static int loft_overlay(Loft *x)
{
    if (s_tune.show_curves <= 0.5f || s_pass == 1)
        return 0;
    script_rule_object("curves", "strip", x);
    return 0;
}

/*  The stations: along every piece, finely on arcs, and one on every tile edge the band crosses. */
/*  Where the band crosses a line the ground can crease on between two
 *  stations of a piece -- the tile edges, and the tile's own diagonal,
 *  where its two triangles meet -- for the centreline and both edges, each
 *  found by bisection; in order along the piece, each once.  Without the
 *  diagonals a strip crossing a tile corner to corner passed under the
 *  crest between them, which is what the road clip check found on four
 *  cities.  Returns how many, at most `cap`. */
static int sample_crossings(const Piece *p, float t0, float t1, V2 pos, V2 dir, float hw, float *tc, int cap)
{
    int ntc = 0, side, pass, q;
    for (side = -1; side <= 1; ++side)
        for (pass = 0; pass < 4; ++pass)
        {
            /*  A station wherever the band crosses a line the
             *  ground can crease on: the tile edges, and the
             *  tile's own diagonal, which is where its two
             *  triangles meet.  Without the diagonals a strip
             *  crossing a tile corner to corner passed under
             *  the crest between them, which is what the road
             *  clip check found on four cities. */
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
         *  sixteenth: fine enough that a curve reads as a curve at the
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
            /*  A station on every tile edge the centreline crosses, found
             *  by bisection, so a crease in the ground -- a crest at the
             *  edge between a rising and a falling tile -- is a station
             *  and never a chord's underside. */
            if (ns > 0 && i > 0)
            {
                /*  The centreline and both edges of the band each cross
                 *  the tile edges at their own point; a station at every
                 *  crossing, in order along the piece, so a wall the band
                 *  crosses obliquely is met at a station on the side it
                 *  first reaches. */
                float t0 = p->len * (float)(i - 1) / (float)nd, t1 = t;
                float tc[17];
                int   ntc = sample_crossings(p, t0, t1, pos, dir, hw, tc, 17), q;
                for (q = 0; q < ntc && ns < 8190; ++q)
                {
                    V2 pb, db;
                    if (q > 0 && tc[q] - tc[q - 1] < 2e-3f)
                        continue;
                    piece_at(p, tc[q], &pb, &db);
                    smp[ns].pos   = pb;
                    smp[ns].dir   = db;
                    smp[ns].s     = s + tc[q];
                    smp[ns].split = 1; /* both tiles meet here: loft_ground reads the higher */
                    smp[ns].xd    = 1e9f;
                    ++ns;
                }
            }
            smp[ns].pos   = pos;
            smp[ns].dir   = dir;
            smp[ns].s     = s + t;
            smp[ns].split = 0;
            smp[ns].wr = smp[ns].wl = 1.0f;
            smp[ns].lane            = 0;
            smp[ns].xd              = 1e9f; /* no crossing near: a road's record measures them */
            ++ns;
        }
        s += p->len;
    }
    x->ns = ns;
    return 0;
}

/*  The profile: the height of every station -- a road on its shelf, a deck on its columns, with the lift and the lane drop. */
/*  The raw ground under every station, read from the surface as it is now
 *  -- in the grading pass, as far as the segments walked so far have
 *  graded it; in the building pass, graded whole -- so it is never cached
 *  with the geometry.  A deck reads the ground a hair beyond its own edges:
 *  on a cross-slope its uphill edge sits on the tile boundary, and the
 *  ground there is the higher neighbour's corner.  The deck rises to clear
 *  it, rather than the hillside being cut (flarange 3,17: an empty tile
 *  beside a band's end, 0.15 over the deck's edge).  Where two tiles meet,
 *  maybe at a small wall: the higher. */
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
            V2    pm   = {sm->pos.x - sm->dir.x * 0.004f, sm->pos.y - sm->dir.y * 0.004f};
            V2    pp   = {sm->pos.x + sm->dir.x * 0.004f, sm->pos.y + sm->dir.y * 0.004f};
            float zm   = section_height(c, mask_bit, pm, sm->dir, h);
            float zp   = section_height(c, mask_bit, pp, sm->dir, h);
            x->zraw[i] = zm > zp ? zm : zp;
        }
        else
            x->zraw[i] = section_height(c, mask_bit, sm->pos, sm->dir, h);
    }
}

/*  The corridor's profile of a strip on the ground: a ramp between the
 *  altitudes of the NODES at its ends, and through any level crossing on
 *  the way.  A node -- a junction, a dead end, a crossing -- stands at its
 *  own tile's levelled height, and every corridor that reaches it ramps to
 *  that one number.  Two segments meeting at a junction therefore agree
 *  without anything being solved between them, a road and a railway
 *  crossing agree because the crossing is a node they share, and an edit
 *  moves only the segments whose anchors moved.  The ramp is eased at both
 *  ends, so a corridor leaves a node level and picks up its grade in
 *  between rather than kinking at the join. */
/*  Station i: how far along the strip it is.  The altitude an end is
 *  pinned to, and the altitude a level crossing under station i pins it
 *  to -- a crossing is a node of BOTH networks, so the road and the
 *  railway are pinned to the same number there. */
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

int loft_ground_crossing(const GroundFan *g, int i, float *z)
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
    if (!net_road_over_rail(c->xbld[tr * R_MAP + tc]))
        return 0;
    *z = node_altitude(c, tc, tr) + g->lift;
    return 1;
}

void loft_ground_set(GroundFan *g, int i, float z)
{
    if (i >= 0 && i < g->n)
        ((Sample *)g->smp)[i].z = z;
}

/*  The corridor's profile over the ground: arc.rules.ground_profile
 *  ramps it between the nodes at its ends. */
static int loft_profile_ground(Loft *x)
{
    GroundFan g;
    memset(&g, 0, sizeof g);
    g.smp      = x->smp;
    g.city     = x->c;
    g.n        = x->ns;
    g.total    = x->total;
    g.pin0     = x->pin0;
    g.pin1     = x->pin1;
    g.dead0    = s_ld->nkind[0] == 1;
    g.dead1    = s_ld->nkind[1] == 1;
    g.pin_node = s_ld->fam->turnout <= 0.0f;
    g.lift     = net_family_rules(s_ld->fam->f)->lift;
    script_rule_object("ground_profile", "ground", &g);
    return 0;
}

static int loft_profile(Loft *x)
{
    const RCity *c        = x->c;
    uint8_t      mask_bit = x->mask_bit;
    Sample      *smp      = x->smp;
    float       *zraw     = x->zraw;
    int          ns       = x->ns;
    int          i;
    /*  The profile: at each station the ground under the cross-section's
     *  centre, and the band lies on the corridor's formation a hair proud
     *  of it.  The corridor's tiles are graded to this same line in the
     *  second pass, so the two cannot cross, and the hair is the wearing
     *  surface over the formation, a few centimetres of it. */
    for (i = 0; i < ns; ++i)
        smp[i].z = zraw[i] + net_family_rules(s_ld->fam->f)->lift + s_ld->raise;
    /*  On the second pass the corridor has already been cut: the shelf
     *  under the band IS the surface this segment was fitted to, so the
     *  band lays flat on it rather than measuring the ground again and
     *  riding the highest point across its own width.  The shelf is flat
     *  across, so a band on it is flat across too. */
    if (s_pass == 2)
        for (i = 0; i < ns; ++i)
        {
            int32_t tc = (int32_t)floorf(smp[i].pos.x), tr = (int32_t)floorf(smp[i].pos.y);
            if (tc < 0 || tr < 0 || tc >= R_MAP || tr >= R_MAP)
                continue;
            /*  The band lays on the corridor, read through the very
             *  function the terrain is drawn with -- the tile's own two
             *  triangles, cut on its own diagonal.  Evaluating it as a
             *  bilinear instead left the band under the drawn surface by
             *  the quad's twist, which is what the clip check kept
             *  finding at a hundredth of a level.  Off its own corridor
             *  -- a level crossing is a tile with two shelves, and the
             *  terrain draws one -- the band still never lies under the
             *  drawn ground (Hippivil 52,36, a railway five centimetres
             *  under the road's shelf the day it was let off its tiles). */
            float zg = surface_at_world(c, mask_bit, smp[i].pos.x, smp[i].pos.y) + net_family_rules(s_ld->fam->f)->lift_min + s_ld->raise;
            if (s_corr[tr * GRID + tc] == 1 && s_corr[tr * GRID + tc + 1] == 1 &&
                s_corr[(tr + 1) * GRID + tc] == 1 && s_corr[(tr + 1) * GRID + tc + 1] == 1)
                smp[i].z = zg;
            else if (smp[i].z < zg)
                smp[i].z = zg;
        }

    /*  The corridor's profile: a ramp between the altitudes of the NODES
     *  at its ends, and through any level crossing on the way.  A node --
     *  a junction, a dead end, a crossing -- stands at its own tile's
     *  levelled height, and every corridor that reaches it ramps to that
     *  one number.  Two segments meeting at a junction therefore agree
     *  without anything being solved between them, a road and a railway
     *  crossing agree because the crossing is a node they share, and an
     *  edit moves only the segments whose anchors moved.  The ramp is eased
     *  at both ends, so a corridor leaves a node level and picks up its
     *  grade in between rather than kinking at the join.
     *
     *  A highway takes no part in it: the highway walk sets no nodes, so a
     *  deck must never be pinned to them.  A deck's heights are the ground
     *  under it and the lift over that, a ramp's its own straight line
     *  (hiway.c). */
    if (s_ld->fam->profile ? s_ld->fam->profile(x) != 0 : loft_profile_ground(x) != 0)
        return -1;
    for (i = 0; i < ns; ++i)
        s_zorig[i] = zraw[i] + net_family_rules(s_ld->fam->f)->lift; /* the ground's own line: a station below it is in a cut */
    /*  The deck stands clear (spec 7.2): 5 m under the soffit plus the
     *  girder is about 7.5 m to the road surface, and the vertical unit
     *  here is the altitude level, seven to eight metres.  So a little over
     *  one level, applied after the profile is settled so the deck follows
     *  the ground's shape while riding above it -- and it is a structure,
     *  not a carpet: its support line may rise or fall no faster than a
     *  sixth of a level a tile, so it runs straight over what the ground
     *  does under it and the columns take up the difference.
     *
     *  The piers: one per segment boundary, every two tiles, which is the
     *  spec's 30 m span (7.2).  The type comes from what is under the deck
     *  there -- over nothing, a lot or a verge, a single hammerhead on the
     *  centreline carrying a cap the full width of the deck; over a surface
     *  road, a two-column bent with the columns outside the carriageway,
     *  never in a lane.  The cap spans both tiles of the band, so it is
     *  laid as two halves, each carrying the painter's order of the tile it
     *  is in: one order for a piece that straddles the seam would put half
     *  the cap in front of the deck over the other tile. */
    return 0;
}

/*  The network record for the traffic, the furniture, the corridor surface and the curve overlay. */
static int loft_record(Loft *x)
{
    const RCity *c        = x->c;
    uint8_t      mask_bit = x->mask_bit;
    Sample      *smp      = x->smp;
    float        hw       = x->hw;
    int          ns       = x->ns;
    if (grade_only(g_dev.grade_loft))
        return loft_surface(c, mask_bit, smp, ns, hw, s_ld); /* the grading pass: the corridor and nothing else */
    /*  What the strip records for the traffic and the passes is the
     *  family's: a road's graph edge and sidewalks, a rail's track, a
     *  deck's edge under its own class (road.c, rail.c, hiway.c). */
    if (ns >= 2 && s_ld->fam->record && s_ld->fam->record(x) != 0)
        return -1;
    if (loft_furniture(x) != 0)
        return -1;
    if (loft_surface(c, mask_bit, smp, ns, hw, s_ld) != 0)
        return -1;
    if (!s_ld->records_only && loft_overlay(x) != 0)
        return -1;

    return 0;
}

/*  --prof-dump: the finished profile of every segment. */
static int loft_prof_dump(Loft *x)
{
    const RCity *c        = x->c;
    uint8_t      mask_bit = x->mask_bit;
    const Family f        = x->f;
    Sample      *smp      = x->smp;
    float        hw       = x->hw;
    int          ns       = x->ns;
    /*  --prof-dump 1 prints the finished profile of every segment: the
     *  distance along, the ground under the cross-section, and the height
     *  the band was given. tools/profile.py draws it, which is how the
     *  grade smoothing is looked at. */
    if (g_dev.prof_dump)
    {
        int d;
        dumpf("PROF f=%d hiway=%d n=%d\n", (int)f, s_ld->fam == &net_hiway, ns);
        for (d = 0; d < ns; ++d)
        {
            float g = section_height(c, mask_bit, smp[d].pos, smp[d].dir, hw);
            dumpf("  %.4f %.4f %.4f %.3f %.3f  dir %.3f,%.3f  w %.3f/%.3f\n", (double)smp[d].s, (double)g, (double)smp[d].z, (double)smp[d].pos.x, (double)smp[d].pos.y, (double)smp[d].dir.x, (double)smp[d].dir.y, (double)smp[d].wl, (double)smp[d].wr);
        }
    }
    return 0;
}


/*  The slab: the strip's own surface, one quad per pair of stations.
 *  It is COMPOSED BY THE SCRIPT (arc.rules.strip): the fit gives the
 *  centreline and the grading the heights, and the script walks the
 *  stations and lays the ribbon between them through the same emitter
 *  every other band goes through.  Nothing here decides how wide the
 *  band is, what it is made of or where it sits in the stack. */
static int loft_slab(Loft *x)
{
    if (grade_only(g_dev.grade_loft))
        return 0; /* the grading pass: the slab is triangles and nothing else */
    script_rule_object("strip", "strip", x);
    return 0;
}

/*  One pair of stations as the family's own stages want it, which the
 *  script asks for when a family builds beside its quads. */
static int loft_pair(Loft *x, int i, LoftPair *p);

int script_strip_pair(void *loft, int i, void *pair)
{
    return loft_pair((Loft *)loft, i, (LoftPair *)pair);
}

/*  One station between two, where a junction's crossing band cuts the
 *  pair that straddles its edge.  The composition does the same in the
 *  mesh's own precision (scripts/compose/strip.lua); this is here for
 *  the stages a family builds beside its quads, which want the pair as
 *  the quad itself was cut. */
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
    float         inset = (s_ld->fam->curbs && sidewalk_on()) ? net_family_rules(s_ld->fam->f)->inner : 1.0f;
    float         ha;
    /*  Cut at the crossing band's edge, as the quad itself is. */
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
static void note_add(char *buf, size_t cap, size_t *n, const char *fmt, ...)
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

/*  What the strip is, in the loft's own terms, for the inspector: its
 *  kind and its ends, the pieces it was fitted as, its stations and the
 *  heights they were given, its width, and what its ends were told.
 *  Lines of key, TAB, value; the first names the thing. */
static void loft_note(const Loft *x)
{
    static const char *const KIND[4] = {"open", "dead end", "junction", "?"};
    static const char *const CTRL[4] = {"", ", stop", ", signal", ""};
    const RLoft             *d       = x->d;
    const char              *seat;
    char                     buf[1024];
    size_t                   n   = 0;
    float                    zlo = 1e9f, zhi = -1e9f;
    int                      k;
    if (d->kind == LOFT_ROAD || d->kind == LOFT_RAIL)
        note_add(buf, sizeof buf, &n, "ends\t%s %d,%d%s to %s %d,%d%s", KIND[d->nkind[0] & 3], (int)d->node[0][0], (int)d->node[0][1], CTRL[d->ctrl[0] & 3], KIND[d->nkind[1] & 3], (int)d->node[1][0], (int)d->node[1][1], CTRL[d->ctrl[1] & 3]);
    if (d->cls >= 0.0f)
        note_add(buf, sizeof buf, &n, "\nclass\t%g", (double)d->cls);
    note_add(buf, sizeof buf, &n, "\npieces\t%d over %.2f tiles:", x->np, (double)x->total);
    for (k = 0; k < x->np && k < 8; ++k)
    {
        const Piece *p = &x->pc[k];
        if (p->arc)
            note_add(buf, sizeof buf, &n, "%s arc r %.2f over %.0f deg", k ? "," : "", (double)p->r, (double)(fabsf(p->t1 - p->t0) * 57.29578f));
        else
            note_add(buf, sizeof buf, &n, "%s run %.2f", k ? "," : "", (double)p->len);
    }
    if (x->np > 8)
        note_add(buf, sizeof buf, &n, ", and %d more", x->np - 8);
    for (k = 0; k < x->ns; ++k)
    {
        if (x->smp[k].z < zlo)
            zlo = x->smp[k].z;
        if (x->smp[k].z > zhi)
            zhi = x->smp[k].z;
    }
    if (x->ns > 1)
        note_add(buf, sizeof buf, &n, "\nstations\t%d, %.3f tiles apart", x->ns, (double)(x->total / (float)(x->ns - 1)));
    seat = d->kind == LOFT_DECK ? "carried on its columns" : d->kind == LOFT_RAMP ? "a structure between road and deck" : "on the graded ground";
    if (x->ns > 0)
        note_add(buf, sizeof buf, &n, "\nprofile\t%.2f to %.2f, %s", (double)zlo, (double)zhi, seat);
    if (d->ramp0 > 0.0f || d->ramp1 > 0.0f)
        note_add(buf, sizeof buf, &n, "\nlift\ttapers over %.2f tiles at the start, %.2f at the end", (double)d->ramp0, (double)d->ramp1);
    if (d->raise > 0.0f)
        note_add(buf, sizeof buf, &n, "\nraise\t%.3f over its usual seat", (double)d->raise);
    note_add(buf, sizeof buf, &n, "\nwidth\thalf %.2f tiles", (double)x->hw);
    if (d->taper > 0.0f)
        note_add(buf, sizeof buf, &n, ", narrowing to %.2f over %.2f at the %s", (double)d->hw_end, (double)d->taper, d->taper_start ? "start" : "end");
    note_add(buf, sizeof buf, &n, "\npinned\t%s", d->pin0 && d->pin1 ? "both ends" : d->pin0 ? "the start" : d->pin1 ? "the end" : "neither end");
    shape_note("%s", buf);
}

/*  The strip, and who asked for it: the caller's name is in force while
 *  this runs, so every component the loft makes records what generated
 *  it and not merely the stage that pushed the triangles. */
static int loft_body(RMesh *m, const RCity *c, uint8_t mask_bit, int comp, const RLoft *desc, const Piece *pc, int np, float total);

int loft_at(const char *where, const char *who, RMesh *m, const RCity *c, uint8_t mask_bit, int comp, const RLoft *desc, const Piece *pc, int np, float total)
{
    ShapeId sh;
    int     rc;
    /*  The strip is a shape, named for what it is.  Everything the loft
     *  draws into it -- the slab, a deck's gores and soffit, its piers --
     *  is part of that shape or of one opened inside it. */
    switch (desc->kind)
    {
    case LOFT_DECK:
        sh = shape_open_at(where, who, "highway deck, band %d", desc->band);
        break;
    case LOFT_RAMP:
        sh = shape_open_at(where, who, "%s ramp%s", desc->lane_off ? "off" : "on", desc->lane_piece ? " lane" : "");
        break;
    default:
        if (desc->node[0][0] == desc->node[1][0] && desc->node[0][1] == desc->node[1][1])
            sh = shape_open_at(where, who, "%s island at %d,%d", desc->fam->name, (int)desc->node[0][0], (int)desc->node[0][1]);
        else
            sh = shape_open_at(where, who, "%s strip %d,%d to %d,%d", desc->fam->name, (int)desc->node[0][0], (int)desc->node[0][1], (int)desc->node[1][0], (int)desc->node[1][1]);
        break;
    }
    rc = loft_body(m, c, mask_bit, comp, desc, pc, np, total);
    shape_close(sh);
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
    x.hw       = desc->hw; /* the family's half width, or a deck's */
    x.mat      = desc->mat;
    x.total    = total;
    x.pc       = pc;
    x.np       = np;
    x.ns       = 0;
    /*  The stages, in order: stations along the pieces; the profile
     *  (the height at each station: a road's on its shelf, a deck's on
     *  its columns, with the lift and the lane drop); a deck's works;
     *  the network record for the traffic, the furniture, the corridor
     *  surface and the curve overlay; and the slab itself. */
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
    /*  Every station full width until something narrows it: the buffer is
     *  static, and a deck's lane drop is not the only thing that writes
     *  them, so a ramp lofted after a deck must not inherit the deck's
     *  gore taper at whatever stations share an index (Atlanta 87,80). */
    {
        int q;
        for (q = 0; q < x.ns; ++q)
            x.smp[q].wl = x.smp[q].wr = 1.0f;
    }
    if (s_ldv.fam->taper)
        s_ldv.fam->taper(&x); /* a ramp's narrowing (hiway.c) */
    if (loft_profile(&x) != 0)
        return -1;
    tq = prof_now(), net_prof_add(NET_PROF_PROFILE, tq - tp), tp = tq;
    if (!s_ldv.records_only)
        loft_note(&x); /* what it is, for the inspector, before anything of it is drawn */
    if (s_ldv.fam->works && s_ldv.fam->works(&x) != 0)
        return -1; /* piers and the like, before the slab (hiway.c) */
    tq = prof_now(), net_prof_add(NET_PROF_DECK_WORKS, tq - tp), tp = tq;
    if (loft_record(&x) != 0)
        return -1;
    tq = prof_now(), net_prof_add(NET_PROF_RECORD, tq - tp), tp = tq;
    if (s_ldv.records_only)
        return 0; /* nothing of it is drawn this build */
    if (loft_prof_dump(&x) != 0)
        return -1;
    {
        int rc = loft_slab(&x);
        tq     = prof_now(), net_prof_add(NET_PROF_SLAB, tq - tp);
        if (s_ldv.fam == &net_hiway)
            net_prof_add(NET_PROF_DECK_SLAB, tq - tp);
        return rc;
    }
}

/*  The strip is one width in the world whatever direction it runs; the
 *  oblique camera then draws a road toward it wider than one across
 *  it, as it draws everything else.  (A build that scaled the world
 *  width by direction to equalise the screen width was rejected: "you
 *  are not keeping road widths consistent".) */
float width_factor(float dx, float dy, int compensate)
{
    (void)dx;
    (void)dy;
    (void)compensate;
    return 1.0f;
}
