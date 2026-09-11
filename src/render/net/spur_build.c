/*  spur_build.c: A SPUR'S DESCENT, POSED, ROUTED AND BUILT.
 *
 *  Where the descent runs along the slab it leaves.  Where its foot aims
 *  on the line below.  How its join slides along both, and the loft that
 *  draws it.  The spur tiles themselves are read in spur.c, which also
 *  holds what the rules answered about each one.
 *
 *  Nothing here decides any of it.  Arc.rules.spur_span says how far
 *  along the slab the descent runs.  Arc.rules.spur_target says where
 *  its foot aims.  Arc.rules.slide says how the join slides, and
 *  arc.rules.spur_lane which lane it fastens to.  What is left is the
 *  arithmetic that turns an answer into a pose. */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "dump.h"
#include "log.h"
#include "mesh/internal.h"
#include "mesh/model.h"
#include "net/internal.h"
#include "net/net.h"
#include "opt.h"
#include "pipeline.h"

static int gix_spur_head_back = -1, gix_spur_probe_step = -1;

/*  ------------------------------------------------------------------
 *  Where each spur's descent runs along its slab
 *
 *  It follows from the station of the slab the spur stands nearest, the
 *  taper the spur was given and which way round it runs: all settled
 *  by the time the bands are lofted: so every spur's is read here and
 *  the spur builder looks the answer up.
 *  ------------------------------------------------------------------ */
static struct
{
    int32_t col, row;
    float   at;
    int     len, leaves, sgn;
    float   top, foot, total, ds;
    int     have;
} s_span[ORIENTS_MAX];
static int s_n_span;

/*  The slab station a spur stands nearest, by the same search the spur
 *  builder makes. */
static int spur_station(const BandSpur *rp)
{
    float best = s_tune.band_reach;
    int   k, iref = -1;
    for (k = 0; k < s_band_nst; ++k)
    {
        float d = v2len((V2){s_band_st[k].pos.x - rp->c0.x, s_band_st[k].pos.y - rp->c0.y});
        if (d < best)
            best = d, iref = k;
    }
    return iref;
}

int net_spur_spans(void)
{
    int r;
    s_n_span = 0;
    for (r = 0; r < s_band_nspurs && s_n_span < ORIENTS_MAX; ++r)
    {
        const BandSpur *rp   = &s_band_spurs[r];
        int           iref = spur_station(rp);
        if (iref < 0)
            continue;
        s_span[s_n_span].col    = rp->rc;
        s_span[s_n_span].row    = rp->rr;
        s_span[s_n_span].at     = s_band_st[iref].s;
        s_span[s_n_span].len    = rp->len;
        s_span[s_n_span].leaves = rp->off;
        s_span[s_n_span].sgn    = s_band_st[iref].dir.x * rp->along.x + s_band_st[iref].dir.y * rp->along.y > 0.0f ? 1 : -1;
        s_span[s_n_span].have   = 0;
        ++s_n_span;
    }
    return s_n_span;
}

int net_spur_span_at(int i, float *at, int *len, int *leaves, int *sgn)
{
    if (i < 0 || i >= s_n_span)
        return 0;
    *at = s_span[i].at, *len = s_span[i].len, *leaves = s_span[i].leaves, *sgn = s_span[i].sgn;
    return 1;
}

void net_spur_span_is(int i, int have, float top, float foot, float total, float ds)
{
    if (i < 0 || i >= s_n_span)
        return;
    s_span[i].have  = have;
    s_span[i].top   = top;
    s_span[i].foot  = foot;
    s_span[i].total = total;
    s_span[i].ds    = ds;
}

static int spur_span_of(int32_t col, int32_t row, float *top, float *foot, float *total, float *ds)
{
    int i;
    for (i = 0; i < s_n_span; ++i)
        if (s_span[i].col == col && s_span[i].row == row)
        {
            if (!s_span[i].have)
                return 0;
            *top = s_span[i].top, *foot = s_span[i].foot;
            *total = s_span[i].total, *ds = s_span[i].ds;
            return 1;
        }
    return 0;
}

/*  The band, the side the spur lies on, and the gore and foot along the slab. */
static int spur_geometry(Spur *x)
{
    int32_t       col       = x->col;
    int32_t       row       = x->row;
    const BandSpur *rp        = x->rp;
    int           iref      = x->iref;
    int           side;
    int           sgn;
    int           band;
    int           off       = x->off;
    float         ds;
    float         s_top;
    float         s_foot;
    float         total_len;
    V2            along     = x->along;
    band                    = s_band_st[iref].band;
    side                    = (((float)col + 0.5f - s_band_st[iref].pos.x) * s_band_st[iref].dir.y - ((float)row + 0.5f - s_band_st[iref].pos.y) * s_band_st[iref].dir.x) > 0.0f ? 1 : -1;
    sgn                     = s_band_st[iref].dir.x * rp->along.x + s_band_st[iref].dir.y * rp->along.y > 0.0f ? 1 : -1;
    /*  Where the descent runs along the band: arc.rules.spur_span's,
     *  read before any spur was built. */
    s_top = s_foot = total_len = ds = 0.0f;
    spur_span_of(col, row, &s_top, &s_foot, &total_len, &ds);
    x->rp        = rp;
    x->iref      = iref;
    x->side      = side;
    x->sgn       = sgn;
    x->band      = band;
    x->off       = off;
    x->ds        = ds;
    x->s_top     = s_top;
    x->s_foot    = s_foot;
    x->total_len = total_len;
    x->along     = along;
    return 0;
}

/*  The two poses of the descent.  A sits on the slab's outer lane at the gore.  F sits at the spur tile's line edge, snapped onto the recorded slab lane. */
/*  The slab's pose at an arc length along a band: interpolated between
 *  the two recorded stations either side of it.  0 when the band has
 *  no stations at all. */
static int band_station_at(int band, float s_at, V2 *pos, V2 *dir)
{
    const BandSt *p0 = NULL, *p1 = NULL;
    float       u;
    int         q;
    for (q = 0; q < s_band_nst; ++q)
    {
        const BandSt *st = &s_band_st[q];
        if (st->band != band)
            continue;
        if (st->s <= s_at + 1e-4f && (!p0 || st->s > p0->s))
            p0 = st;
        if (st->s >= s_at - 1e-4f && (!p1 || st->s < p1->s))
            p1 = st;
    }
    if (!p0)
        p0 = p1;
    if (!p1)
        p1 = p0;
    if (!p0)
        return 0;
    u    = p1->s > p0->s + 1e-6f ? (s_at - p0->s) / (p1->s - p0->s) : 0.0f;
    *pos = (V2){p0->pos.x + (p1->pos.x - p0->pos.x) * u, p0->pos.y + (p1->pos.y - p0->pos.y) * u};
    *dir = (V2){p0->dir.x + (p1->dir.x - p0->dir.x) * u, p0->dir.y + (p1->dir.y - p0->dir.y) * u};
    {
        float dl = v2len(*dir);
        if (dl < 1e-6f)
            *dir = p0->dir;
        else
            *dir = (V2){dir->x / dl, dir->y / dl};
    }
    return 1;
}

/*  The foot F is the middle of the spur tile's line-side edge, heading
 *  at the line.  With the line opposite the slab it is the far edge
 *  across the tile.  A through line (fork 2) is met at its side at an
 *  angle, thirty degrees off its line toward the lane the spur merges
 *  into. */
static void spur_foot_pose(const Spur *x, V2 *F, V2 *tF)
{
    const BandSpur *rp   = x->rp;
    int32_t       col  = x->col;
    int32_t       row  = x->row;
    int           fork = x->fork;
    if (rp->opp)
    {
        /* across the spur tile to the line on its far side */
        *F  = (V2){(float)col + 0.5f - rp->toward.x * 0.5f, (float)row + 0.5f - rp->toward.y * 0.5f};
        *tF = (V2){-rp->toward.x, -rp->toward.y};
    }
    else
    {
        *F  = rp->off ? (V2){(float)col + 0.5f + rp->along.x * 0.5f, (float)row + 0.5f + rp->along.y * 0.5f}
                      : (V2){(float)col + 0.5f - rp->along.x * 0.5f, (float)row + 0.5f - rp->along.y * 0.5f};
        *tF = rp->off ? rp->along : (V2){-rp->along.x, -rp->along.y}; /* at the line, square on */
        if (fork == 2)
        {
            /*  A through line is met at its side at an angle off its line,
             *  toward the lane the spur merges into. */
            V2 dm = {tF->y, -tF->x}; /* the line direction whose right-hand lane is on the spur's side */
            /*  Both are built slab-to-line.  An OFF spur's traffic runs
             *  with the lane here, and an ON spur's against it, because
             *  the spur is reversed afterwards. */
            float ca = net_line_rules()->spur_meet_cos, sa = net_line_rules()->spur_meet_sin, tr = rp->off ? 1.0f : -1.0f;
            *tF = (V2){dm.x * ca * tr + tF->x * sa, dm.y * ca * tr + tF->y * sa};
        }
    }
}

static int spur_poses(Spur *x)
{
    const BandSpur *rp        = x->rp;
    int           side      = x->side;
    int           sgn       = x->sgn;
    int           band      = x->band;
    int           fork      = x->fork;
    int           np1       = x->np1;
    int           np2       = x->np2;
    int           line_lane = x->line_lane;
    int           off       = x->off;
    int           r         = x->r;
    float         s_top     = x->s_top;
    float         rmin      = x->rmin;
    V2            A;
    V2            tA;
    V2            F         = x->F;
    V2            tF        = x->tF;
    V2            B         = x->B;
    V2            tB        = x->tB;
    V2            along     = x->along;
    V2            toward    = x->toward;
    /*  The spur as ONE ROUTED LANE (lane.c, stage 2 of the lane
     *  primitives), lofted below by the same loft() as a line strip.
     *  Two legs, each the router's biarc between two poses.  The descent
     *  runs from the slab's outer lane at the gore's near end to the
     *  foot F.  A heads the slab's way.  The foot is the middle of the
     *  spur tile's line-side edge, heading at the line.  And the join,
     *  from F into the line.
     *
     *      A STUB's lane on the spur's side.  A quarter of the line's
     *      width off its centerline.  Out to the line tile's far edge.
     *
     *  The line's lanes are the spurs'.  Or a THROUGH line's side, a
     *  short way along it at the merge.  A line opposite the slab: the
     *  descent runs to the spur tile's far edge instead.  Built
     *  slab-to-line.  An ON spur is reversed afterwards. */
    {
        const float o = net_line_rules()->spur_outer * s_tune.band_w; /* the outer lane's center, across the slab */
        V2          pos, dir;
        if (!band_station_at(band, s_top, &pos, &dir))
        {
            if (s_pass != 1)
                ++s_spur_forms[4];
            band_spur_lost_is(x->col, x->row, 3);
            if (g_dev.lane_dump && s_pass != 1)
                dumpf("SPUR %d,%d lost: %s\n", (int)x->col, (int)x->row, "no slab station at the top of the descent");
            return 1; /* not this tile */
        }
        A = (V2){pos.x + (float)side * dir.y * o, pos.y - (float)side * dir.x * o};
        /*  Both legs are built slab-to-line, so the slab end heads AWAY
         *  from the gore along the slab.  With the travel direction (the
         *  slab's own way) an ON spur's biarc had to double back and
         *  hooked at the slab.  An OFF spur
         *  travels this way.  An ON spur is reversed below. */
        tA = rp->off ? (V2){dir.x * (float)sgn, dir.y * (float)sgn} : (V2){-dir.x * (float)sgn, -dir.y * (float)sgn};
        spur_foot_pose(x, &F, &tF);
    }
    /*  The ends stand ON A RECORDED LANE.  A takes a station of the
     *  slab's own band.  The join's end, or the foot, takes a station of
     *  the line.  Which lane.  Which station of it, is
     *  arc.rules.spur_lane's (scripts/compose/snap.lua): the lanes
     *  within reach are measured here and left for the drive to have one
     *  picked.  An end the rule picks nothing for keeps its computed
     *  pose and is counted, in the lanes line as "spur ends on no lane". */
    {
        V2 trav = rp->off ? tA : (V2){-tA.x, -tA.y};
        lane_snap_ask(A, trav, net_line_rules()->spur_snap);
        net_spur_snap_is("slab", band);
    }
    x->rp        = rp;
    x->side      = side;
    x->sgn       = sgn;
    x->band      = band;
    x->fork      = fork;
    x->np1       = np1;
    x->np2       = np2;
    x->line_lane = line_lane;
    x->off       = off;
    x->r         = r;
    x->s_top     = s_top;
    x->rmin      = rmin;
    x->A         = A;
    x->tA        = tA;
    x->F         = F;
    x->tF        = tF;
    x->B         = B;
    x->tB        = tB;
    x->along     = along;
    x->toward    = toward;
    return 0;
}

/*  The slab lane the script picked, taken, and then the descent routed
 *  from the station it picked it at. */
static int spur_route(Spur *x)
{
    Piece *pc   = x->pc;
    int    np;
    int    np1  = x->np1;
    float  rmin = x->rmin;
    V2     A    = x->A;
    V2     tA   = x->tA;
    V2     F    = x->F;
    V2     tF   = x->tF;
    {
        V2    pos, d2;
        float dist;
        int   slab_lane = lane_snap_take(&pos, &d2, &dist);
        x->slab_lane    = slab_lane;
        if (slab_lane >= 0)
        {
            A     = pos;
            tA    = x->rp->off ? d2 : (V2){-d2.x, -d2.y};
            x->A  = A;
            x->tA = tA;
        }
    }
    /*  The descent's chain, queued to be cut: the cut is
     *  arc.rules.pieces's and the drive makes it, so this stops here and
     *  spur_routed takes the pieces. */
    {
        V2    q[MAX_PTS];
        float rad[MAX_PTS], tl[MAX_PTS];
        int   n;
        net_cut_reset();
        n      = pose_chain(A, tA, F, tF, q, rad, tl);
        x->cut = n >= 2 ? net_cut_add(q, n, rad, tl) : -1;
    }
    x->A  = A;
    x->tA = tA;
    x->F  = F;
    x->tF = tF;
    (void)pc, (void)np, (void)np1, (void)rmin;
    return 0;
}

/*  And the descent built from the pieces the drive cut. */
static int spur_routed(Spur *x)
{
    Piece *pc   = x->pc;
    int    np1  = 0;
    float  rmin = 1e9f;
    int    k;
    if (x->cut < 0 || net_cut_pieces(x->cut, pc, MAX_PIECES, &np1) != 0 || np1 < 1)
    {
        if (s_pass != 1)
            ++s_spur_forms[4];
        band_spur_lost_is(x->col, x->row, 4);
        if (g_dev.lane_dump && s_pass != 1)
            dumpf("SPUR %d,%d lost: %s\n", (int)x->col, (int)x->row, "the descent could not be routed");
        return 1; /* not this tile */
    }
    for (k = 0; k < np1; ++k)
        if (pc[k].arc && pc[k].r < rmin)
            rmin = pc[k].r;
    x->np   = np1;
    x->np1  = np1;
    x->rmin = rmin;
    return 0;
}

/*  THE TWO LEGS THROUGH THE FOOT.  A spur falls back on this join where
 *  no lane can be slid along.  It also falls back on it where no placing
 *  of the slide held.  The chain is queued for the drive to cut like
 *  every other path, and taken once it has. */
static int spur_legs_ask(V2 F, V2 tF, V2 B, V2 tB)
{
    V2    q[MAX_PTS];
    float rad[MAX_PTS], tl[MAX_PTS];
    int   n = pose_chain(F, tF, B, tB, q, rad, tl);
    return n >= 2 ? net_cut_add(q, n, rad, tl) : -1;
}

static int spur_legs_take(Spur *x, Piece *out, int *np, float *rmin)
{
    int k;
    if (x->legs < 0 || net_cut_pieces(x->legs, out, MAX_PIECES, np) != 0 || *np < 1)
        return -1;
    *rmin = 1e9f;
    for (k = 0; k < *np; ++k)
        if (out[k].arc && out[k].r < *rmin)
            *rmin = out[k].r;
    return 0;
}

/*  The join into the line: straight on, into a stub's lane, or merging into a through line's near lane: snapped onto the recorded line lane. */
/*  ---- WHERE A SPUR AIMS ON THE LINE IT COMES DOWN TO
 *  --------------------
 *
 *  ... is arc.rules.spur_target's.  The reading is the fork the meet was
 *  classified as, which way the spur runs and where its foot is.  The
 *  answer is the point the join is drawn to, the tangent it is drawn
 *  with, and the direction the line's lane travels there.  Which is what
 *  the lane lookup is then made against.
 *
 *  Asked once per spur, between the descent and the join, so nothing
 *  reaches up in the middle of a build.  No rule is a spur that aims at
 *  nothing and falls back on its legs. */
static struct
{
    const Spur *x;
    V2          B, tB, trav;
    int         have;
} s_spur_target;

int net_spur_target_at(int *fork, int *off, float *rdx, float *rdy, float *mdx, float *mdy,
                       float *fx, float *fy, float *lane_off, float *merge_along)
{
    const Spur *x = s_spur_target.x;
    if (!x)
        return 0;
    *fork        = x->fork;
    *off         = x->rp->off ? 1 : 0;
    *rdx         = x->rd.x, *rdy = x->rd.y;
    *mdx         = x->mdir.x, *mdy = x->mdir.y;
    *fx          = x->F.x, *fy = x->F.y;
    *lane_off    = s_tune.line_w * net_line_rules()->spur_lane_off;
    *merge_along = net_line_rules()->spur_merge_along;
    return 1;
}

void net_spur_target_is(float bx, float by, float tbx, float tby, float tvx, float tvy)
{
    s_spur_target.B    = (V2){bx, by};
    s_spur_target.tB   = (V2){tbx, tby};
    s_spur_target.trav = (V2){tvx, tvy};
    s_spur_target.have = 1;
}

static void spur_join_target(const Spur *x, V2 *B, V2 *tB, V2 *trav)
{
    (void)x;
    *B    = s_spur_target.B;
    *tB   = s_spur_target.tB;
    *trav = s_spur_target.trav;
}

/*  Whether a route leaves the spur tile across its line edge, which is
 *  the edge in direction `rd` from the tile's center.  It also gives the
 *  length of the route beyond that meet.  The spur tile is the hard
 *  rule: a lane that misses that edge is not this spur's. */
static int spur_exits(const Piece *pc, int np, V2 centre, V2 rd, float *beyond)
{
    float total = 0.0f, run = 0.0f, ps = 0.0f, pl = 0.0f;
    int   k, first = 1;
    for (k = 0; k < np; ++k)
        total += pc[k].len;
    for (k = 0; k < np; ++k)
    {
        float t;
        for (t = 0.0f;; t += geo_num(&gix_spur_probe_step, "spur_probe_step"))
        {
            V2    pos, dir;
            float side, lat;
            if (t > pc[k].len)
                t = pc[k].len;
            piece_at(&pc[k], t, &pos, &dir);
            side = (pos.x - centre.x) * rd.x + (pos.y - centre.y) * rd.y - 0.5f;
            lat  = (pos.x - centre.x) * rd.y - (pos.y - centre.y) * rd.x;
            if (!first && side > 0.0f && ps <= 0.0f)
            {
                float f = ps / (ps - side), l = pl + (lat - pl) * f;
                if (fabsf(l) > 0.5f)
                    return 0; /* out past the tile's corner, not across its line edge */
                *beyond = total - (run + t);
                return 1;
            }
            first = 0, ps = side, pl = lat;
            if (t >= pc[k].len)
                break;
        }
        run += pc[k].len;
    }
    return 0;
}

/*  The join as a slide.  The route runs from the slab pose to the line's
 *  lane.  Where it meets the lane it slides along it, out to
 *  `spur_merge` tiles from the aimed point.  At the same time, the lane
 *  there is still the one aimed at.  The slab pose may first run
 *  straight, as a parallel lane beside the slab.  It runs up to half a
 *  tile short of the line's lane line, as a real spur runs before it
 *  turns.  A candidate must leave the spur tile across its line edge.
 *  The widest tightest arc wins, and the shortest run and slide among
 *  equals.  It is one arc where the slab crosses the line, and an easy S
 *  where it runs beside it.  Returns 1 with the pieces and the tightest
 *  radius.  It also answers the end pose, the slide.  The taper (the
 *  line part and a little of the spur tile, as the two-leg join's was). */
/*  One placing: the descent starting `u` along the slab and the join
 *  sitting `at` along the line.  The radius the route holds, or why it
 *  cannot be had: "off" the lane it aimed at.  This ends the slide along
 *  the line, or "unroutable". */
/*  One placing of the join, as far as the CHAIN it would be cut from.
 *  Cutting it is arc.rules.pieces's and the script does it.  So this
 *  stops here and band_slide_routed takes the pieces back. */
/*  The lanes within reach of the join at `at` along the line, measured
 *  for the rule that picks one.  A join slides along the SAME lane the
 *  spur's foot fastened to.  So it asks the same question of the same
 *  rule: which lane is the lip-side one is arc.rules.spur_lane's, and
 *  nothing here repeats it. */
int band_slide_snap(SlideFan *s, float at)
{
    V2 P        = {s->B0.x + s->tB0.x * at, s->B0.y + s->tB0.y * at};
    net_spur_snap_is("line", -1);
    return lane_snap_ask(P, s->trav, s->snap);
}

const char *band_slide_chain(SlideFan *s, float u, float at, V2 *q, float *rad, float *tlim, int *n)
{
    const Spur *x = (const Spur *)s->spur;
    V2          Q = {x->A.x + x->tA.x * u, x->A.y + x->tA.y * u};
    V2          pos, d2, tb;
    float       dist;
    s->r = 1e9f;
    /*  The lane the rule picked at this placing, from the candidates
     *  band_slide_snap offered it.  Another lane than the one aimed at
     *  ends the slide: further along the line only goes further off. */
    if (lane_snap_take(&pos, &d2, &dist) != s->lane)
        return "off";
    tb = x->rp->off ? d2 : (V2){-d2.x, -d2.y}; /* construction: against an ON spur's travel */
    *n = pose_chain(Q, x->tA, pos, tb, q, rad, tlim);
    if (*n < 2)
        return "unroutable";
    s->lead = u;
    s->Q    = Q;
    s->pos  = pos;
    s->dir  = tb;
    s->at   = at;
    return NULL;
}

/*  And the placing built from the pieces the script cut: the descent's
 *  own straight ahead of them where it starts further along the slab.
 *  Answers the tightest arc's radius, or 0 for a placing with none. */
float band_slide_routed(SlideFan *s, const Piece *pc, int np)
{
    const Spur *x    = (const Spur *)s->spur;
    int         lead = s->lead > 0.0f, k;
    if (np < 1 || np + lead > MAX_PIECES)
        return 0.0f;
    if (lead)
    {
        s->tmp[0].arc = 0;
        s->tmp[0].a   = x->A;
        s->tmp[0].b   = s->Q;
        s->tmp[0].len = s->lead;
    }
    for (k = 0; k < np; ++k)
        s->tmp[lead + k] = pc[k];
    s->n = np + lead;
    s->r = 1e9f;
    for (k = 0; k < s->n; ++k)
        if (s->tmp[k].arc && s->tmp[k].r < s->r)
            s->r = s->tmp[k].r;
    return s->r;
}

/*  Does the placing leave by the spur tile's own line edge?  The spur
 *  tile is the hard rule: a lane that misses that edge is not this
 *  spur's. */
int band_slide_exits(SlideFan *s)
{
    const Spur *x      = (const Spur *)s->spur;
    V2          centre = {(float)x->col + 0.5f, (float)x->row + 0.5f};
    return spur_exits(s->tmp, s->n, centre, x->rd, &s->beyond);
}

/*  The placing kept as the best so far. */
void band_slide_keep(SlideFan *s, float taper)
{
    s->best       = s->r;
    *s->np        = s->n;
    *s->rmin      = s->r;
    *s->B         = s->pos;
    *s->tB        = s->dir;
    *s->out_merge = s->at;
    *s->out_taper = s->beyond + taper;
    memcpy(s->pc, s->tmp, sizeof(Piece) * (size_t)s->n);
}

/*  Why no placing was found, for --lane-dump. */
void band_slide_note(const SlideFan *s, int tried, int off, int unroutable, int missed)
{
    const Spur *x = (const Spur *)s->spur;
    if (g_dev.lane_dump && s_pass != 1)
        dumpf("SPUR %d,%d slide: none of %d candidates (%d off the lane, %d unroutable, %d miss the line edge)\n", (int)x->col, (int)x->row, tried, off, unroutable, missed);
}

/*  The join slid along the slab and along the line: arc.rules.slide
 *  walks the placings. */
static SlideFan s_slide;

static SlideFan *spur_slide_ask(const Spur *x, int lane, V2 B0, V2 tB0, V2 trav, Piece *pc, int *np, float *rmin, V2 *B, V2 *tB, float *merge, float *taper)
{
    static Piece tmp[MAX_PIECES];
    SlideFan     s;
    V2           nB  = {-tB0.y, tB0.x};
    float        den = x->tA.x * nB.x + x->tA.y * nB.y;
    memset(&s, 0, sizeof s);
    s.spur      = (void *)x;
    s.lane      = lane;
    s.B0        = B0;
    s.tB0       = tB0;
    s.trav      = trav;
    s.parallel  = fabsf(den) <= 1e-3f;
    s.reach     = s.parallel ? 0.0f : ((B0.x - x->A.x) * nB.x + (B0.y - x->A.y) * nB.y) / den;
    s.merge     = s_tune.spur_merge;
    s.snap      = net_line_rules()->spur_snap;
    s.taper     = net_line_rules()->spur_taper;
    s.tmp       = tmp;
    s.pc        = pc;
    s.np        = np;
    s.rmin      = rmin;
    s.B         = B;
    s.tB        = tB;
    s.out_merge = merge;
    s.out_taper = taper;
    s_slide     = s;
    return &s_slide;
}

/*  And whether the rule found a placing that held. */
static int spur_slide_took(void)
{
    return s_slide.best > 0.0f;
}

/*  The join's own working, held while the drive slides it: the rule
 *  walks the placings along the line's lane and answers the best. */
static struct
{
    float taper, merge, rmin;
    int   np, np1, np2, line_lane, fork, line_port, off, side, slid, asked;
    V2    B, tB, F, tF, A, rd, mdir, along, toward;
    const BandSpur *rp;
    Piece        *pc;
} s_join;

static void spur_join_finish(Spur *x);

static SlideFan *spur_join_ask(Spur *x, int take)
{
    float              taper     = 0.0f, merge = -1.0f;
    const RCity       *c         = x->c;
    const RAtlasLevel *l         = x->l;
    int                line_port = x->line_port;
    Piece             *pc        = x->pc;
    const BandSpur      *rp        = x->rp;
    int                side      = x->side;
    int                fork      = x->fork;
    int                np        = x->np;
    int                np1       = x->np1;
    int                np2       = x->np2;
    int                line_lane;
    int                off       = x->off;
    float              rmin      = x->rmin;
    V2                 rd        = x->rd;
    V2                 mdir      = x->mdir;
    V2                 A         = x->A;
    V2                 F         = x->F;
    V2                 tF        = x->tF;
    V2                 B         = x->B;
    V2                 tB        = x->tB;
    V2                 along     = x->along;
    V2                 toward    = x->toward;
    /*  The line tile is a JUNCTION.  The spur is one of its arms.  It
     *  has a one-way port the junction's connectors feed (an ON spur) or
     *  drain (an OFF spur).  So the spur ends at the foot, the tile
     *  edge, and no join of its own runs on into the box.  A real
     *  junction takes the spur as an arm.  So does a stub ending in the
     *  spurs, and a bend with the spur straight ahead of one arm.  Each
     *  takes the spur as an arm of the box (lane_spur_arm).  A line
     *  passing the spur at its side does not, and there its near lane
     *  alone meets the spur, below. */
    int32_t jc0 = (int32_t)floorf(F.x + rd.x * 0.5f), jr0 = (int32_t)floorf(F.y + rd.y * 0.5f), ej0;
    for (ej0 = 0; ej0 < 4; ++ej0)
        if ((int)lroundf(SIDE_DU[ej0]) == -(int)lroundf(rd.x) && (int)lroundf(SIDE_DV[ej0]) == -(int)lroundf(rd.y))
            break;
    if (node_kind(c, l, net_line->f, jc0, jr0) == 2 && ej0 < 4 && rp->arm)
    {
        int32_t jc = jc0, jr = jr0, ej = ej0;
        line_port = lane_port_id(jc, jr, ej, rp->off ? 0 : 1, 0);
        fork      = 0;
    }
    /*  A spur aims where arc.rules.spur_target says.  No answer is a spur
     *  that aims at nothing, and it falls back on its legs below. */
    if (fork && s_spur_target.have)
    {
        V2 trav = {0.0f, 0.0f}; /* the line lane's own direction of travel at the join */
        spur_join_target(x, &B, &tB, &trav);
        {
            /* the line's own lane there, traveling the join's way */
            V2    pos, d2;
            float dist;
            /*  The lanes within reach of the join's end, measured and
             *  left for the drive on the first pass.  The second takes
             *  the one it picked.  What leads up to this is worked out
             *  again rather than held: it is a reading of the line and
             *  of nothing the pick changes. */
            if (!take)
            {
                lane_snap_ask(B, trav, net_line_rules()->spur_snap);
                net_spur_snap_is("line", -1);
                return NULL;
            }
            line_lane = lane_snap_take(&pos, &d2, &dist);
            if (line_lane >= 0)
            {
                B  = pos;
                tB = rp->off ? d2 : (V2){-d2.x, -d2.y}; /* construction: against an ON spur's travel */
            }
        }
        /*  ONE LANE from the slab to the line.  The route runs from the
         *  slab pose to the line's lane.  Where it meets the lane it
         *  slides along it, to wherever the tightest arc is widest
         *  (spur_slide).  The foot stays the tile logic's anchor.  The
         *  two legs through it, which turned inside half a tile, are the
         *  fallback when no slide leaves the spur tile across its line
         *  edge. */
        s_join.taper = taper, s_join.merge = merge, s_join.rmin = rmin;
        s_join.np = np, s_join.np1 = np1, s_join.np2 = np2;
        s_join.line_lane = line_lane, s_join.fork = fork, s_join.line_port = line_port;
        s_join.off = off, s_join.side = side, s_join.slid = 0, s_join.asked = 1;
        s_join.B = B, s_join.tB = tB, s_join.F = F, s_join.tF = tF, s_join.A = A;
        s_join.rd = rd, s_join.mdir = mdir, s_join.along = along, s_join.toward = toward;
        s_join.rp = rp, s_join.pc = pc;
        if (line_lane >= 0)
            return spur_slide_ask(x, line_lane, B, tB, trav, pc, &s_join.np1, &s_join.rmin,
                                  &s_join.B, &s_join.tB, &s_join.merge, &s_join.taper);
        /*  There is no lane to slide along, so there is nothing to
         *  slide.  The join falls back on the two legs through the foot,
         *  which the drive queues and cuts next. */
        memset(&s_slide, 0, sizeof s_slide);
        return NULL;
    }
    else
    {
        /* no join: the line's lane at the foot itself, if one runs there */
        V2    pos, d2;
        float dist;
        if (!take)
        {
            lane_snap_ask(F, tF, net_line_rules()->spur_snap);
            net_spur_snap_is("line", -1);
            return NULL;
        }
        line_lane = lane_snap_take(&pos, &d2, &dist);
    }
    s_join.asked = 0;
    x->rp        = rp;
    x->side      = side;
    x->fork      = fork;
    x->np        = np;
    x->np1       = np1;
    x->np2       = np2;
    x->line_lane = line_lane;
    x->off       = off;
    x->rmin      = rmin;
    x->rd        = rd;
    x->mdir      = mdir;
    x->A         = A;
    x->F         = F;
    x->tF        = tF;
    x->B         = B;
    x->tB        = tB;
    x->along     = along;
    x->toward    = toward;
    x->line_port = line_port;
    x->taper     = taper;
    x->merge     = merge;
    return NULL;
}

/*  Where no placing of the slide held, the two legs through the foot are
 *  what the join falls back on.  So they are queued here, after the
 *  slide has answered and before the drive cuts, like every other path. */
static void spur_legs_queue(Spur *x)
{
    x->legs = -1;
    if (!s_join.asked || spur_slide_took() || s_join.np1 >= MAX_PIECES)
        return;
    net_cut_reset();
    x->legs = spur_legs_ask(s_join.F, s_join.tF, s_join.B, s_join.tB);
}

/*  What the join comes to once the rule has slid it.  It is the placing
 *  the rule found, or the two legs through the foot where it found none. */
static void spur_join_finish(Spur *x)
{
    Piece *pc = s_join.pc;
    float  taper = s_join.taper, merge = s_join.merge, rmin = s_join.rmin;
    int    np = s_join.np, np1 = s_join.np1, np2 = s_join.np2;
    V2     B = s_join.B, tB = s_join.tB, F = s_join.F, tF = s_join.tF;
    if (spur_slide_took())
    {
        np2 = 0;
        np  = np1;
    }
    else if (np1 < MAX_PIECES && spur_legs_take(x, pc + np1, &np2, &rmin) == 0)
    {
        int q2;
        np    = np1 + np2;
        taper = net_line_rules()->spur_taper; /* the join and a little of the descent */
        for (q2 = np1; q2 < np; ++q2)
            taper += pc[q2].len;
    }
    x->rp        = s_join.rp;
    x->side      = s_join.side;
    x->fork      = s_join.fork;
    x->np        = np;
    x->np1       = np1;
    x->np2       = np2;
    x->line_lane = s_join.line_lane;
    x->off       = s_join.off;
    x->rmin      = rmin;
    x->rd        = s_join.rd;
    x->mdir      = s_join.mdir;
    x->A         = s_join.A;
    x->F         = F;
    x->tF        = tF;
    x->B         = B;
    x->tB        = tB;
    x->along     = s_join.along;
    x->toward    = s_join.toward;
    x->line_port = s_join.line_port;
    x->taper     = taper;
    x->merge     = merge;
}

/*  An ON spur was built slab-to-line: reversed into travel order. */
static int spur_reverse(Spur *x)
{
    Piece        *pc  = x->pc;
    const BandSpur *rp  = x->rp;
    int           np  = x->np;
    int           off = x->off;
    int           q   = x->q;
    if (!rp->off)
    {
        /* built top-down.  An ON spur is traveled from the line up: reverse */
        for (q = 0; q < np / 2; ++q)
        {
            Piece t        = pc[q];
            pc[q]          = pc[np - 1 - q];
            pc[np - 1 - q] = t;
        }
        for (q = 0; q < np; ++q)
        {
            V2 t    = pc[q].a;
            pc[q].a = pc[q].b;
            pc[q].b = t;
            if (pc[q].arc)
            {
                float t2 = pc[q].t0;
                pc[q].t0 = pc[q].t1;
                pc[q].t1 = t2;
            }
        }
    }
    x->rp  = rp;
    x->np  = np;
    x->off = off;
    x->q   = q;
    return 0;
}

/*  The spur as a lane of its own, its dump, and what the loft is told. */
static int spur_finish(Spur *x)
{
    int           line_port = x->line_port;
    RMesh        *m         = x->m;
    const RCity  *c         = x->c;
    uint8_t       mask_bit  = x->mask_bit;
    int32_t       col       = x->col;
    int32_t       row       = x->row;
    Piece        *pc        = x->pc;
    const BandSpur *rp        = x->rp;
    int           side      = x->side;
    int           sgn       = x->sgn;
    int           fork      = x->fork;
    int           np        = x->np;
    int           np1       = x->np1;
    int           np2       = x->np2;
    int           slab_lane = x->slab_lane;
    int           line_lane = x->line_lane;
    int           flat;
    int           form;
    int           off;
    int           lane_off;
    float         z0;
    V2            A         = x->A;
    V2            tA        = x->tA;
    V2            F         = x->F;
    V2            tF        = x->tF;
    V2            along     = x->along;
    V2            toward    = x->toward;
    if (lane_spur(m, c, mask_bit, pc, np, SPUR_HW, slab_lane, line_lane, line_port, rp->off) != 0)
        return -1;
    if (g_dev.lane_dump && s_pass != 1)
    {
        dumpf("LANE spur %d,%d %s fork %d opp %d len %d: c0 %.2f,%.2f along %.2f,%.2f toward %.2f,%.2f side %d sgn %d | A %.3f,%.3f "
              "heading %.2f,%.2f | F %.3f,%.3f heading %.2f,%.2f | %d+%d pieces\n",
              (int)col,
              (int)row,
              rp->off ? "OFF" : "ON",
              fork,
              rp->opp,
              rp->len,
              (double)rp->c0.x,
              (double)rp->c0.y,
              (double)rp->along.x,
              (double)rp->along.y,
              (double)rp->toward.x,
              (double)rp->toward.y,
              side,
              sgn,
              (double)A.x,
              (double)A.y,
              (double)tA.x,
              (double)tA.y,
              (double)F.x,
              (double)F.y,
              (double)tF.x,
              (double)tF.y,
              np1,
              np2);
        dumpf("    slab lane %d, line lane %d, merge %.1f, taper %.2f\n", slab_lane, line_lane, (double)x->merge, (double)x->taper);
        lane_dump_pieces(pc, np);
    }
    flat     = 1;
    form     = 5;
    off      = rp->off;
    lane_off = off;
    z0       = BAND_LIFT;
    if (rp->len < 2 && s_pass != 1)
        ++s_spur_forms[2];
    off          = 0; /* in travel order already: no lift to fall */
    x->rp        = rp;
    x->side      = side;
    x->sgn       = sgn;
    x->fork      = fork;
    x->np        = np;
    x->np1       = np1;
    x->np2       = np2;
    x->slab_lane = slab_lane;
    x->line_lane = line_lane;
    x->flat      = flat;
    x->form      = form;
    x->off       = off;
    x->lane_off  = lane_off;
    x->z0        = z0;
    x->A         = A;
    x->tA        = tA;
    x->F         = F;
    x->tF        = tF;
    x->along     = along;
    x->toward    = toward;
    return 0;
}

/*  ------------------------------------------------------------------
 *  The spurs' lofts, held for the drive
 *
 *  Reading a spur draws nothing, so the pass reads every one of them and
 *  then the drive lofts and composes each in the order they were read.
 *  ------------------------------------------------------------------ */
#define SPUR_LOFTS_MAX 2048
#define SPUR_PIECES    64

static struct
{
    RLoft d;
    Piece pc[SPUR_PIECES];
    int   np;
    float total;
} s_spur_loft[SPUR_LOFTS_MAX];
static int          s_n_spur_loft;
static RMesh       *s_spur_m;
static const RCity *s_spur_c;
static uint8_t      s_spur_mask;
static int          s_spur_comp;

static int spur_loft_add(const RLoft *d, const Piece *pc, int np, float total)
{
    int k;
    if (s_n_spur_loft >= SPUR_LOFTS_MAX || np > SPUR_PIECES)
    {
        R_ERR("net", "no room for a spur's strip: %d spurs of %d pieces is the most held",
              SPUR_LOFTS_MAX, SPUR_PIECES);
        return -1;
    }
    k                  = s_n_spur_loft++;
    s_spur_loft[k].d   = *d;
    s_spur_loft[k].np  = np;
    s_spur_loft[k].total = total;
    memcpy(s_spur_loft[k].pc, pc, sizeof(Piece) * (size_t)np);
    return 0;
}

int build_spur_lofts(void)
{
    return s_n_spur_loft;
}

int build_spur_loft(int i)
{
    double tp = prof_now();
    int    rc;
    if (i < 0 || i >= s_n_spur_loft)
        return 0;
    rc = loft(s_spur_m, s_spur_c, s_spur_mask, s_spur_comp, &s_spur_loft[i].d,
              s_spur_loft[i].pc, s_spur_loft[i].np, s_spur_loft[i].total);
    net_prof_add(NET_PROF_SPUR_LOFT, prof_now() - tp);
    return rc;
}

/*  ------------------------------------------------------------------
 *  The spurs, one at a time
 *
 *  The grading pass builds them too: a spur grades no shelf, but its
 *  loft caps the terrain field under it, and the field is smoothed as a
 *  whole.  Each is read up to the join the rule slides along the line,
 *  and taken up again once it has answered.
 *  ------------------------------------------------------------------ */
static Piece s_rt_pc[MAX_PIECES];
static struct
{
    RMesh             *m;
    const RCity       *c;
    const RAtlasLevel *l;
    uint8_t            mask_bit;
    int                comp;
    int32_t            row, col;
    Spur               x;
    SlideFan          *fan;
    int                joined, np, off, form, flat, lane_off;
    float              taper, z0, climb;
    int          live; /* a spur in hand, between the drive's two picks */
} s_rt;

void build_spurs_begin(RMesh *m, const RCity *c, const RAtlasLevel *l, uint8_t mask_bit, int comp)
{
    memset(&s_rt, 0, sizeof s_rt);
    s_rt.m = m, s_rt.c = c, s_rt.l = l, s_rt.mask_bit = mask_bit, s_rt.comp = comp;
    s_n_spur_loft = 0;
    s_spur_m = m, s_spur_c = c, s_spur_mask = mask_bit, s_spur_comp = comp;
    if (s_pass != 1)
    {
        memset(s_spur_forms, 0, sizeof s_spur_forms);
        band_spur_lost_is(0, 0, -1);
    }
}

/*  One spur read.  Answers 1 with one in hand.  Ask net_spur_slide for
 *  the join it wants slid, which is nothing where it wants none.  And 0
 *  when there are no more. */
static int spur_tile(int32_t col, int32_t row)
{
    const RCity         *c     = s_rt.c;
    const RAtlasLevel   *l     = s_rt.l;
    static const int32_t DC[4] = {0, 1, 0, -1}, DR[4] = {-1, 0, 1, 0}; /* N E S W */
    uint8_t              b     = c->xbld[row * R_MAP + col];
    int                  dside, rside, eside, off, lines, how;
    V2                   along, toward;
    s_rt.fan = NULL, s_rt.joined = 0;
    s_rt.np = 0, s_rt.form = 0, s_rt.flat = 0, s_rt.lane_off = 0;
    s_rt.taper = 0.0f, s_rt.z0 = 0.0f, s_rt.climb = 0.0f;
    if (!net_band_spur(b))
        return 0;
    how = spur_orient(c, col, row, &dside, &rside, &eside, &along, &toward, &off, &lines);
    if (how == 2)
    {
        /*  At a band's end, head-on: straight across from the far edge to
         *  the slab's, climbing. */
        float back     = geo_num(&gix_spur_head_back, "spur_head_back");
        V2 from        = {(float)col + 0.5f - (float)DC[eside] * back, (float)row + 0.5f - (float)DR[eside] * back};
        V2 to          = {(float)col + 0.5f + (float)DC[eside] * 0.5f, (float)row + 0.5f + (float)DR[eside] * 0.5f};
        s_rt_pc[0].arc = 0;
        s_rt_pc[0].a   = from;
        s_rt_pc[0].b   = to;
        s_rt_pc[0].len = v2len((V2){to.x - from.x, to.y - from.y});
        s_rt.np        = 1;
        s_rt.climb     = s_rt_pc[0].len;
        s_rt.off       = 0;
        s_rt.form      = 0;
        if (g_dev.path_dump)
            dumpf("PATH hw=%.3f\nTILES %d,%d\nGATES\nPTS %.3f,%.3f %.3f,%.3f\nRAD 0.000 0.000\nTLIM 0.000 0.000\n", (double)SPUR_HW, (int)col, (int)row, (double)from.x, (double)from.y, (double)to.x, (double)to.y);
        return 1;
    }
    if (how == 0)
    {
        if (s_pass != 1)
            ++s_spur_forms[4];
        band_spur_lost_is(col, row, 5);
        if (g_dev.lane_dump && s_pass != 1)
            dumpf("SPUR %d,%d lost: %s\n", (int)col, (int)row, "no slab touching the tile");
        return 0; /* no slab touching it: left to its sprite */
    }
    /*  The spur (spec 7.3), as ONE ROUTED LANE (lane.c).  The stages
     *  below find the slab beside the tile and say what the line does.
     *  They pose the descent, route it, join the line, and hand the
     *  pieces to the loft as a strip like any other. */
    memset(&s_rt.x, 0, sizeof s_rt.x);
    s_rt.x.m = s_rt.m, s_rt.x.c = c, s_rt.x.mask_bit = s_rt.mask_bit, s_rt.x.comp = s_rt.comp;
    s_rt.x.col = col, s_rt.x.row = row, s_rt.x.pc = s_rt_pc;
    s_rt.x.iref = -1, s_rt.x.best = s_tune.band_reach, s_rt.x.slab_lane = -1;
    s_rt.x.line_lane = -1, s_rt.x.line_port = -1, s_rt.x.l = l;
    s_rt.x.along = along, s_rt.x.toward = toward, s_rt.x.off = off;
    s_rt.x.dside = dside, s_rt.x.rside = rside, s_rt.x.eside = eside;
    s_rt.x.how = how, s_rt.x.lines = lines;
    if (spur_find(&s_rt.x) != 0)
        return 0;
    s_rt.fan = NULL, s_rt.joined = 0;
    spur_classify(&s_rt.x);
    spur_geometry(&s_rt.x);
    /*  The spur stops at its SLAB end's lanes, measured and left for the
     *  drive to have one picked.  Build_spur_routed takes it up. */
    return spur_poses(&s_rt.x) == 0 ? 1 : 0;
}

/*  The slab lane picked, and the descent routed from the station it was
 *  picked at.  Then the LINE end's lanes are measured and left for the
 *  drive in the same way. */
int build_spur_routed(void)
{
    if (!s_rt.live)
        return 0;
    if (spur_route(&s_rt.x) != 0)
        s_rt.live = 0;
    return s_rt.live;
}

/*  The descent taken from the pieces the drive cut, and then the LINE
 *  end's lanes measured and left for the drive in the same way. */
/*  The spur in hand offered to arc.rules.spur_target, once its descent
 *  is cut and before its join is drawn. */
int build_spur_target(void)
{
    if (!s_rt.live)
        return 0;
    s_spur_target.x    = &s_rt.x;
    s_spur_target.have = 0;
    s_spur_target.B = s_spur_target.tB = s_spur_target.trav = (V2){0.0f, 0.0f};
    return 1;
}

int build_spur_joined(void)
{
    if (!s_rt.live)
        return 0;
    if (spur_routed(&s_rt.x) != 0)
    {
        s_rt.live = 0;
        return 0;
    }
    spur_join_ask(&s_rt.x, 0);
    return 1;
}

int build_spur_next(void)
{
    while (s_rt.row < R_MAP)
    {
        const int32_t col = s_rt.col, row = s_rt.row;
        if (++s_rt.col >= R_MAP)
            s_rt.col = 0, ++s_rt.row;
        s_rt.live = spur_tile(col, row);
        if (s_rt.live)
            return 1;
    }
    return 0;
}

/*  And the line lane picked, with the join settled on it.  The slide is
 *  left for the rule that decides how far along it the spur meets. */
SlideFan *net_spur_slide(void)
{
    if (!s_rt.live)
        return NULL;
    s_rt.fan    = spur_join_ask(&s_rt.x, 1);
    s_rt.joined = 1;
    return s_rt.fan;
}

/*  The slide answered, and the legs the join falls back on queued for the
 *  drive to cut where it came to nothing. */
int build_spur_slid(void)
{
    if (!s_rt.live || !s_rt.joined)
        return 0;
    spur_legs_queue(&s_rt.x);
    return 1;
}

/*  And the spur laid: concrete from the line to the slab, it grades
 *  nothing.  A lane drop's is flat, one lane wide, drawn as the slab's
 *  outer lane, its height eased by the loft.  An older form lifts over
 *  `climb` at the slab end. */
int build_spur_done(void)
{
    RLoft d = {0};
    float total = 0.0f;
    int   k;
    if (s_rt.joined)
    {
        if (s_join.asked)
            spur_join_finish(&s_rt.x);
        spur_reverse(&s_rt.x);
        if (spur_finish(&s_rt.x) != 0)
            return -1;
        s_rt.np = s_rt.x.np, s_rt.flat = s_rt.x.flat, s_rt.form = s_rt.x.form;
        s_rt.off = s_rt.x.off, s_rt.lane_off = s_rt.x.lane_off;
        s_rt.z0 = s_rt.x.z0, s_rt.climb = s_rt.x.climb, s_rt.taper = s_rt.x.taper;
    }
    s_rt.fan = NULL, s_rt.joined = 0;
    for (k = 0; k < s_rt.np; ++k)
        total += s_rt_pc[k].len;
    d.f       = net_line->f;
    d.fam     = net_band;
    d.hw      = SPUR_HW;
    d.mat     = s_rt.flat ? MAT_BAND_LANE : MAT_BAND; /* a lane piece is drawn as the slab's outer lane */
    d.kind    = LOFT_SPUR;
    d.struct_ = 1;
    d.flat = d.lane_piece = s_rt.flat;
    d.lane_off            = s_rt.lane_off;
    d.z0                  = s_rt.flat ? s_rt.z0 : 0.0f;
    d.spur0               = s_rt.flat ? 0.0f : s_rt.off ? 0.0f
                                                        : s_rt.climb; /* the lift climbs the first `climb` */
    d.spur1               = s_rt.flat ? 0.0f : s_rt.off ? s_rt.climb
                                                        : 0.0f; /* ... or falls over the end */
    d.pin0 = d.pin1 = 1;
    d.cls           = -1.0f;
    d.taper         = s_rt.taper;
    d.hw_end        = LINE_W * geo_num(&gix_lane_line, "lane_line"); /* a line lane's half width, where its center lies */
    d.taper_start   = !s_rt.off && s_rt.taper > 0.0f;   /* an ON spur starts at the line */
    if (spur_loft_add(&d, s_rt_pc, s_rt.np, total) != 0)
        return -1;
    if (s_pass != 1)
        ++s_spur_forms[s_rt.form];
    return 0;
}

/*  Every band in the city, each walked once from an end. */
