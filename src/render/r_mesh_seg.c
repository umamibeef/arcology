/*  r_mesh_seg.c -- the segment pipeline (the road spec, part 3.10).
 *
 *  Split out of r_mesh.c; see r_mesh_int.h.
 */
#include "r_mesh_int.h"

/* ---- the segment pipeline (the road spec, part 3.10) ------------------ */

/*  A network is walked as segments between its nodes, the junction and
 *  end tiles; every other tile has two links and lies on one segment.
 *  A segment's centreline is the polyline through its tiles' centres,
 *  from the side of the junction box it leaves to the side it reaches.
 *  The polyline is straightened -- a staircase of corners becomes one
 *  straight line, at 45 degrees or 2:1 -- and every remaining bend is
 *  filleted with an arc, the quarter circle of a lone corner or the
 *  gentler sweep where a diagonal meets the grid.  Then one strip is
 *  lofted along the whole path by arc length, its width a function of
 *  the direction at each sample, its dashes and crosswalks placed by
 *  the distance from the junction.  There is no join inside a segment
 *  to get wrong, and a segment meets its junction box square on. */




static float v2len(V2 v)
{
    return sqrtf(v.x * v.x + v.y * v.y);
}

/*  Fillet the polyline into straights and arcs.  At each bend the arc's
 *  radius is the class's, clamped so the tangent points stay within the
 *  adjacent edges (spec 3.10, step 3). */
int fillet(const V2 *q, int n, float rmax, Piece *out, int *count)
{
    int i, np = 0;
    V2  cur = q[0];
    for (i = 1; i < n - 1; ++i)
    {
        V2    u_in  = {q[i].x - q[i - 1].x, q[i].y - q[i - 1].y};
        V2    u_out = {q[i + 1].x - q[i].x, q[i + 1].y - q[i].y};
        float lin = v2len(u_in), lout = v2len(u_out);
        float cross, dot, theta, d, R, cos_h, sin_h;
        V2    t1, t2, cen, n1;
        if (lin < 1e-6f || lout < 1e-6f)
            continue;
        u_in.x /= lin;
        u_in.y /= lin;
        u_out.x /= lout;
        u_out.y /= lout;
        cross = u_in.x * u_out.y - u_in.y * u_out.x;
        dot   = u_in.x * u_out.x + u_in.y * u_out.y;
        if (dot > 0.9999f)
            continue;                             /* straight on: no vertex */
        theta = acosf(dot < -1.0f ? -1.0f : dot); /* the turn, 0..pi */
        /* the tangent points at distance d from the vertex; R clamped to the edges */
        R = rmax;
        {
            float lim   = 0.5f * (lin < lout ? lin : lout);
            float tan_h = tanf(0.5f * theta);
            if (R * tan_h > lim)
                R = lim / tan_h;
        }
        if (R < 0.04f)
            R = 0.04f;
        d  = R * tanf(0.5f * theta);
        t1 = (V2){q[i].x - u_in.x * d, q[i].y - u_in.y * d};
        t2 = (V2){q[i].x + u_out.x * d, q[i].y + u_out.y * d};
        /* the arc's centre: off t1 along the inward normal of u_in, toward the turn */
        n1    = cross > 0.0f ? (V2){-u_in.y, u_in.x} : (V2){u_in.y, -u_in.x};
        cen   = (V2){t1.x + n1.x * R, t1.y + n1.y * R};
        cos_h = cosf(0.5f * theta);
        sin_h = sinf(0.5f * theta);
        (void)cos_h;
        (void)sin_h;
        if (np + 2 > MAX_PIECES)
            return -1;
        if (v2len((V2){t1.x - cur.x, t1.y - cur.y}) > 1e-4f)
        {
            out[np].arc = 0;
            out[np].a   = cur;
            out[np].b   = t1;
            out[np].len = v2len((V2){t1.x - cur.x, t1.y - cur.y});
            ++np;
        }
        out[np].arc = 1;
        out[np].c   = cen;
        out[np].r   = R;
        out[np].t0  = atan2f(t1.y - cen.y, t1.x - cen.x);
        out[np].t1  = out[np].t0 + (cross > 0.0f ? theta : -theta);
        out[np].len = R * theta;
        ++np;
        cur = t2;
    }
    if (np + 1 > MAX_PIECES)
        return -1;
    if (v2len((V2){q[n - 1].x - cur.x, q[n - 1].y - cur.y}) > 1e-4f)
    {
        out[np].arc = 0;
        out[np].a   = cur;
        out[np].b   = q[n - 1];
        out[np].len = v2len((V2){q[n - 1].x - cur.x, q[n - 1].y - cur.y});
        ++np;
    }
    *count = np;
    return 0;
}

/*  The point and unit direction at distance `t` along a piece. */
static void piece_at(const Piece *p, float t, V2 *pos, V2 *dir)
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

typedef struct
{
    V2    pos, dir;
    float s, z; /* z: the section's height, the highest ground it spans */
    float xd;   /* the distance along to the nearest level crossing on the segment */
} Sample;

/*  The height a cross-section sits at: the highest of the ground under
 *  its centre and its two edges, so the terrain never cuts through the
 *  road (the user: "raise roadways if they're going to cut into the
 *  terrain"); where that lifts it, the skirts below make the fill. */
static float section_height(const RCity *c, uint8_t mask_bit, V2 pos, V2 dir, float h)
{
    float zc = surface_at_world(c, mask_bit, pos.x, pos.y);
    float zl = surface_at_world(c, mask_bit, pos.x + dir.y * h, pos.y - dir.x * h);
    float zr = surface_at_world(c, mask_bit, pos.x - dir.y * h, pos.y + dir.x * h);
    float z  = zc > zl ? zc : zl;
    return z > zr ? z : zr;
}

/*  Loft one strip along the pieces: cross-sections by arc length, their
 *  width the class's scaled by the direction (the snap view's
 *  compensation), one quad per pair, each with the painter's order of
 *  the tile under it.  `zeb0`/`zeb1`: the length of the crosswalk band
 *  at either end, 0 for none. */
/*  Record a road segment's stations in the mesh's network, for the
 *  traffic.  The lane centres by class, tiles from the centreline: a
 *  road's one lane each way at half the carriageway, an avenue's outer
 *  and inner lanes, a boulevard's outer and inner between median and
 *  curb, where the material draws its lines. */
static int net_record(RRoadNet *net, const Sample *smp, int ns, float total, int cls, int rail)
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
    memcpy(sg->node, s_seg_node, sizeof sg->node);
    sg->kind[0]  = s_seg_kind[0];
    sg->kind[1]  = s_seg_kind[1];
    sg->ctrl[0]  = s_seg_ctrl[0];
    sg->ctrl[1]  = s_seg_ctrl[1];
    sg->lane_out = cls == 0 ? 0.142f : cls == 1 ? 0.217f
                                                : 0.225f;
    sg->lane_in  = cls == 0 ? 0.142f : cls == 1 ? 0.082f
                                                : 0.115f;
    if (s_hiway)
    {
        /*  A deck's lanes, from the 7.1 section: the half deck is a tile
         *  and holds three of them, their centres 2.75, 6.45 and 10.15 m
         *  out of 14.9.  Two lanes carry the traffic here, so it runs in
         *  the one against the median and the one against the shoulder. */
        sg->lane_in  = 0.185f;
        sg->lane_out = 0.681f;
    }
    if (rail)
        sg->lane_out = sg->lane_in = 0.133f; /* a rail: the track to the right of travel */
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

/*  Whether a station's tile is a level crossing or within a tile of one. */
static int near_crossing(const RCity *c, V2 pos)
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
            if (b >= 0x45u && b <= 0x48u)
                return 1;
        }
    return 0;
}

static float s_zorig[8192];

/*  The ground under a sunken road follows it (the user: "I think a lot
 *  of the carving issues could be resolved if we simply modified the
 *  height of the tile containing the road"): where a vertical curve
 *  sinks a road below the ground, the corners of the tiles it passes
 *  through are capped to a hair under the road and the mesh is built
 *  again on that field, so the terrain bends into the cut instead of
 *  being carved out of it.  One height per grid corner, shared by the
 *  four tiles around it, so the bend is continuous and the walls close
 *  what is left. */
float s_zcap[GRID * GRID];
int   s_pass; /* 0 not capping, 1 collecting the caps, 2 building on them */

static int put_prism_clip_m(RMesh *m, const RCity *c, uint8_t mask_bit, float order, float cx, float cy, float dx, float dy, float len, float wid, float zb, float zf, float z0, float z1, float paint, float mat);

int loft(RMesh *m, const RCity *c, uint8_t mask_bit, int comp, Family f, const Piece *pc, int np, float total, float zeb0, float zeb1, int pin0, int pin1)
{
    static Sample smp[8192];
    static float  zraw[8192];
    float         hw  = s_hiway ? 1.0f : f == F_ROAD ? ROAD_W * 0.5f
                                                     : RAIL_W * 0.5f;
    float         mat = f == F_ROAD ? MAT_ROAD : MAT_RAIL;
    int           k, ns = 0, i;
    float         s = 0.0f;
    /* the samples */
    for (k = 0; k < np; ++k)
    {
        const Piece *p  = &pc[k];
        int          nd = p->arc ? (int)ceilf(p->len / 0.08f) : (int)ceilf(p->len / 0.125f);
        if (nd < 1)
            nd = 1;
        for (i = (ns ? 1 : 0); i <= nd; ++i)
        {
            float t = p->len * (float)i / (float)nd;
            V2    pos, dir;
            if (ns >= 8190)
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
                float tc[9];
                int   ntc = 0, side, pass, q;
                for (side = -1; side <= 1; ++side)
                    for (pass = 0; pass < 2; ++pass)
                    {
                        int   ax  = pass == 0;
                        float off = (float)side * hw;
                        V2    p0, d0, q0, q1;
                        piece_at(p, t0, &p0, &d0);
                        q0 = (V2){p0.x + d0.y * off, p0.y - d0.x * off};
                        q1 = (V2){pos.x + dir.y * off, pos.y - dir.x * off};
                        {
                            float a0 = ax ? q0.x : q0.y, a1 = ax ? q1.x : q1.y;
                            if (floorf(a0) != floorf(a1) && ntc < 9)
                            {
                                float lo = t0, hi = t1, edge = floorf(a1 > a0 ? a1 : a0);
                                V2    pb, db, qb;
                                int   it;
                                for (it = 0; it < 12; ++it)
                                {
                                    float mid = 0.5f * (lo + hi), v;
                                    piece_at(p, mid, &pb, &db);
                                    qb = (V2){pb.x + db.y * off, pb.y - db.x * off};
                                    v  = ax ? qb.x : qb.y;
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
                for (q = 0; q < ntc && ns < 8190; ++q)
                {
                    V2    pb, db, pm, pp;
                    float zm, zp;
                    if (q > 0 && tc[q] - tc[q - 1] < 2e-3f)
                        continue;
                    piece_at(p, tc[q], &pb, &db);
                    /* both tiles meet here, maybe at a small wall: the higher */
                    pm          = (V2){pb.x - db.x * 0.004f, pb.y - db.y * 0.004f};
                    pp          = (V2){pb.x + db.x * 0.004f, pb.y + db.y * 0.004f};
                    zm          = section_height(c, mask_bit, pm, db, hw);
                    zp          = section_height(c, mask_bit, pp, db, hw);
                    smp[ns].pos = pb;
                    smp[ns].dir = db;
                    smp[ns].s   = s + tc[q];
                    zraw[ns]    = zm > zp ? zm : zp;
                    ++ns;
                }
            }
            smp[ns].pos = pos;
            smp[ns].dir = dir;
            smp[ns].s   = s + t;
            zraw[ns]    = section_height(c, mask_bit, pos, dir, hw);
            ++ns;
        }
        s += p->len;
    }
    /*  The profile: at each station the highest ground the cross-section
     *  spans, centre and both edges, and a hair of clearance, so the
     *  terrain never cuts through the road, on a straightened diagonal
     *  across the corner tiles beside the road tiles above all; a road
     *  along a slope lies on it, a road across one rides on its uphill
     *  edge with a skirt below the other (the user: "raise roadways if
     *  they're going to cut into the terrain"). */
    for (i = 0; i < ns; ++i)
        smp[i].z = zraw[i] + 0.03f;
    /*  The grade: no steeper than the original's slope pieces, one level
     *  per tile of road, so a band that climbs across a slope or steps
     *  onto a higher tile does so on a ramp with a skirt rather than a
     *  near-vertical face the light turns black.  Stations are only ever
     *  raised, so the band stays above the ground; an end that meets a
     *  junction box is pinned to it, a free end may rise with the band
     *  (pinned, a dead end on a slope fell a level in one station). */
    for (i = 1; i < ns - (pin1 ? 1 : 0); ++i)
    {
        float lim = smp[i - 1].z - ROAD_GRADE * (smp[i].s - smp[i - 1].s);
        if (smp[i].z < lim)
            smp[i].z = lim;
    }
    for (i = ns - 2; i >= (pin0 ? 1 : 0); --i)
    {
        float lim = smp[i + 1].z - ROAD_GRADE * (smp[i + 1].s - smp[i].s);
        if (smp[i].z < lim)
            smp[i].z = lim;
    }
    /*  Vertical curves (the user: "soften the transition so that the
     *  roads don't abruptly change angles... raising/lowering the road
     *  segments with engineered walls"; the specification's 3.10:
     *  fillet the profile polyline as the plan's, R_v 20 to 60 m).  The
     *  profile is averaged over half a tile each side, which rounds
     *  every grade break into a curve a tile long: at a sag the road
     *  rises off the ground onto its embankment, at a crest it sinks
     *  into the ground behind retaining walls, about a metre at the
     *  break for the art's one-level-per-tile grade.  The ends keep
     *  their height, a junction box, a carrier or a cap on the ground,
     *  the rounding fading in over the half tile from each. */
    for (i = 0; i < ns; ++i)
        s_zorig[i] = smp[i].z; /* the profile before the fillet: a station below it is in a cut */
    if (ns > 2 && total > 1.0f)
    {
        static float zs[8192];
        const float  T  = 0.5f;
        int          lo = 0;
        for (i = 0; i < ns; ++i)
        {
            float a = smp[i].s - T, b = smp[i].s + T, acc = 0.0f, dend, fade;
            int   j;
            if (a < 0.0f)
                a = 0.0f;
            if (b > total)
                b = total;
            while (lo + 1 < ns && smp[lo + 1].s <= a)
                ++lo;
            for (j = lo; j + 1 < ns && smp[j].s < b; ++j)
            {
                float s0 = smp[j].s, s1 = smp[j + 1].s, z0 = smp[j].z, z1 = smp[j + 1].z, c0, c1, zc0, zc1;
                if (s1 <= s0)
                    continue;
                c0 = s0 > a ? s0 : a;
                c1 = s1 < b ? s1 : b;
                if (c1 <= c0)
                    continue;
                zc0 = z0 + (z1 - z0) * (c0 - s0) / (s1 - s0);
                zc1 = z0 + (z1 - z0) * (c1 - s0) / (s1 - s0);
                acc += 0.5f * (zc0 + zc1) * (c1 - c0);
            }
            zs[i] = b > a ? acc / (b - a) : smp[i].z;
            dend  = smp[i].s < total - smp[i].s ? smp[i].s : total - smp[i].s;
            fade  = dend / T;
            if (fade > 1.0f)
                fade = 1.0f;
            {
                /*  Only a real correction survives: the average also drifts
                 *  a few centimetres off the ground along a curved road
                 *  across a slope, where the section's maximum wanders,
                 *  and that drift had grown a thin wall down a whole hill. */
                float d = (zs[i] - smp[i].z) * fade;
                if (d > 0.03f)
                    d -= 0.03f;
                else if (d < -0.03f)
                    d += 0.03f;
                else
                    d = 0.0f;
                zs[i] = smp[i].z + d;
            }
        }
        for (i = 0; i < ns; ++i)
            smp[i].z = zs[i];
    }
    /*  The deck stands clear (spec 7.2): 5 m under the soffit plus the
     *  girder is about 7.5 m to the road surface, and the vertical unit
     *  here is the altitude level, seven to eight metres.  So a little
     *  over one level, applied after the profile is settled so the deck
     *  follows the ground's shape while riding above it. */
    if (s_hiway)
        for (i = 0; i < ns; ++i)
            smp[i].z += HIWAY_LIFT;
    /*  The piers.  One per segment boundary, every two tiles, which is
     *  the spec's 30 m span (7.2).  The type comes from what is under
     *  the deck there: over nothing, a lot or a verge, a single
     *  hammerhead on the centreline carrying a cap the full width of
     *  the deck; over a surface road, a two-column bent with the
     *  columns outside the carriageway, never in a lane.
     *
     *  The cap spans both tiles of the band, so it is laid as two
     *  halves, each carrying the painter's order of the tile it is in:
     *  one order for a piece that straddles the seam would put half the
     *  cap in front of the deck over the other tile. */
    if (s_hiway)
    {
        float next = 1.0f;
        for (i = 1; i < ns; ++i)
        {
            const Sample *sm = &smp[i];
            float         px = -sm->dir.y, py = sm->dir.x;
            int           ew = fabsf(px) < 0.5f; /* the deck runs east-west */
            int32_t       tc, tr;
            float         g, top, cap, cw, cd;
            int           j, bent;
            if (sm->s < next)
                continue;
            next += HIWAY_BENT;
            tc = (int32_t)floorf(sm->pos.x);
            tr = (int32_t)floorf(sm->pos.y);
            if (tc < 0 || tr < 0 || tc >= R_MAP || tr >= R_MAP)
                continue;
            g   = surface_at_world(c, mask_bit, sm->pos.x, sm->pos.y);
            top = sm->z - HIWAY_GIRDER - g; /* the cap's top, over the ground */
            cap = top - HIWAY_CAP;
            if (top < 0.12f)
                continue; /* the deck has met the ground: no room to stand */
            /*  A road under the deck asks for a bent, its columns clear
             *  of the carriageway; anything else takes the hammerhead. */
            bent = road_under_deck(c, sm->pos.x, sm->pos.y, px, py);
            /*  The cap, the full width of the deck and 1.5 m along it,
             *  in two halves so each takes its own tile's order. */
            cw = ew ? HIWAY_CAP_D : 1.0f;
            cd = ew ? 1.0f : HIWAY_CAP_D;
            for (j = 0; j < 2; ++j)
            {
                float   o  = j ? 0.5f : -0.5f;
                float   hx = sm->pos.x + px * o, hy = sm->pos.y + py * o;
                int32_t hc = (int32_t)floorf(hx), hr = (int32_t)floorf(hy);
                if (hc < 0 || hr < 0 || hc >= R_MAP || hr >= R_MAP)
                    continue;
                if (put_box(m, c, mask_bit, tile_order(c, hc, hr, mask_bit), hx, hy, cw, cd, cap, top, MAT_PIER, 0.0f) != 0)
                    return -1;
            }
            /*  Then the column, or the bent's pair of them.  A column
             *  runs from the ground it stands on up to the cap. */
            for (j = 0; j < 2; ++j)
            {
                /*  Outside the carriageway when a road runs under the
                 *  deck (7.2: never in a lane), otherwise just inside
                 *  the deck's edges, where the art stands them. */
                float   o  = (bent ? 0.78f : 0.62f) * (j ? 1.0f : -1.0f);
                float   cx = sm->pos.x + px * o, cy = sm->pos.y + py * o;
                float   cg = surface_at_world(c, mask_bit, cx, cy);
                float   ch = sm->z - HIWAY_GIRDER - HIWAY_CAP - cg;
                int32_t bc = (int32_t)floorf(cx), br = (int32_t)floorf(cy);
                if (bc < 0 || br < 0 || bc >= R_MAP || br >= R_MAP || ch < 0.05f)
                    continue;
                if (put_box(m, c, mask_bit, tile_order(c, bc, br, mask_bit), cx, cy, HIWAY_COL, HIWAY_COL, 0.0f, ch, MAT_PIER, 0.0f) != 0)
                    return -1;
            }
        }
    }
    if (f == F_ROAD && ns >= 2 && net_record(&m->net, smp, ns, total, (int)(s_seg_class + 0.5f), 0) != 0)
        return -1;
    if (f == F_RAIL && ns >= 2 && net_record(&m->railnet, smp, ns, total, 0, 1) != 0)
        return -1;
    /*  A rail's signalling (spec 5.6), right-hand running: the track to
     *  the right of the walk carries traffic forward and its signals
     *  stand on its outer side facing back; the other track's face
     *  forward on the other side.  A block signal every ten tiles,
     *  green; an absolute one, red, a tile before a junction; a whistle
     *  post two tiles before a level crossing; none on or beside a
     *  crossing. */
    /*  Street lighting (spec 1.6, 6.4): on an avenue or a boulevard a
     *  cobra-head luminaire on a davit arm every two tiles, thirty
     *  metres, staggered side to side: a round pole on the sidewalk
     *  1.35 levels tall, ten metres, an arm three metres out over the
     *  road from its top, and the head at the arm's end; none within a
     *  tile of a junction, an end or a level crossing.  A local road
     *  goes unlit, as the spec's post-tops are a downtown's. */
    if (f == F_ROAD && s_seg_class >= 0.5f && total > 2.5f)
    {
        float at;
        int   side = 0;
        for (at = 1.25f; at < total - 1.0f; at += 2.0f, side ^= 1)
        {
            int   j = 1;
            float sgn, px, py, ax, ay, hx, hy, order;
            while (j < ns - 1 && smp[j].s < at)
                ++j;
            if (near_crossing(c, smp[j].pos))
                continue;
            sgn = side ? -1.0f : 1.0f;
            hx  = smp[j].dir.x;
            hy  = smp[j].dir.y;
            ax  = -hy * sgn; /* across, toward the pole's side */
            ay  = hx * sgn;
            px  = smp[j].pos.x + ax * hw * 0.90f;
            py  = smp[j].pos.y + ay * hw * 0.90f;
            {
                int32_t tc = (int32_t)floorf(px), tr = (int32_t)floorf(py);
                if (tc < 0 || tr < 0 || tc >= R_MAP || tr >= R_MAP)
                    continue;
                order = tile_order(c, tc, tr, mask_bit) + 0.3f;
            }
            /* the pole, on the sidewalk's height */
            if (put_prism_clip_m(m, c, mask_bit, order, px, py, hx, hy, 0.02f, 0.02f, smp[j].z, smp[j].z, 0.0f, 1.35f, 0.0f, MAT_PROP) != 0)
                return -1;
            /* the arm, from the pole's top in over the road, rising a little */
            if (put_prism_clip_m(m, c, mask_bit, order, px - ax * 0.10f, py - ay * 0.10f, -ax, -ay, 0.20f, 0.014f, smp[j].z, smp[j].z + 0.03f, 1.33f, 1.35f, 0.0f, MAT_PROP) != 0)
                return -1;
            /* the cobra head, 0.8 m long, hung at the arm's end */
            if (put_prism_clip_m(m, c, mask_bit, order, px - ax * 0.19f, py - ay * 0.19f, -ax, -ay, 0.055f, 0.025f, smp[j].z + 0.03f, smp[j].z + 0.03f, 1.30f, 1.345f, 0.0f, MAT_PROP) != 0)
                return -1;
        }
    }
    if (f == F_RAIL && ns > 2)
    {
        float sig;
        int   side;
        for (side = 0; side < 2; ++side)
        {
            float sgn = side ? -1.0f : 1.0f; /* the right track (forward) then the left (back) */
            for (sig = 5.0f; sig < total - 0.5f; sig += 10.0f)
            {
                int j = 1;
                while (j < ns - 1 && smp[j].s < sig)
                    ++j;
                if (near_crossing(c, smp[j].pos))
                    continue;
                {
                    float   rx = -smp[j].dir.y * sgn * 0.33f, ry = smp[j].dir.x * sgn * 0.33f;
                    int32_t tc = (int32_t)floorf(smp[j].pos.x + rx), tr = (int32_t)floorf(smp[j].pos.y + ry);
                    if (tc < 0 || tr < 0 || tc >= R_MAP || tr >= R_MAP)
                        continue;
                    if (put_rail_signal(m, c, mask_bit, tile_order(c, tc, tr, mask_bit) + 0.3f, smp[j].pos.x + rx, smp[j].pos.y + ry, -smp[j].dir.x * sgn, -smp[j].dir.y * sgn, 0, smp[j].s, (int)sgn) != 0)
                        return -1;
                }
            }
            /* the absolute signal before the junction this track runs toward */
            if ((side == 0 && pin1) || (side == 1 && pin0))
            {
                float at = side == 0 ? total - 1.0f : 1.0f;
                int   j  = 1;
                if (total > 2.5f)
                {
                    while (j < ns - 1 && smp[j].s < at)
                        ++j;
                    if (!near_crossing(c, smp[j].pos))
                    {
                        float   rx = -smp[j].dir.y * sgn * 0.33f, ry = smp[j].dir.x * sgn * 0.33f;
                        int32_t tc = (int32_t)floorf(smp[j].pos.x + rx), tr = (int32_t)floorf(smp[j].pos.y + ry);
                        if (tc >= 0 && tr >= 0 && tc < R_MAP && tr < R_MAP &&
                            put_rail_signal(m, c, mask_bit, tile_order(c, tc, tr, mask_bit) + 0.3f, smp[j].pos.x + rx, smp[j].pos.y + ry, -smp[j].dir.x * sgn, -smp[j].dir.y * sgn, 1, smp[j].s, (int)sgn) != 0)
                            return -1;
                    }
                }
            }
        }
        /* whistle posts: two tiles before each crossing tile, each direction, on the right */
        for (i = 1; i < ns; ++i)
        {
            int32_t col = (int32_t)floorf(smp[i].pos.x), row = (int32_t)floorf(smp[i].pos.y), pc2 = (int32_t)floorf(smp[i - 1].pos.x), pr2 = (int32_t)floorf(smp[i - 1].pos.y);
            uint8_t b;
            if (col < 0 || row < 0 || col >= R_MAP || row >= R_MAP || (col == pc2 && row == pr2))
                continue;
            b = c->xbld[row * R_MAP + col];
            if (!(b == 0x45u || b == 0x46u))
                continue;
            for (side = 0; side < 2; ++side)
            {
                float   sgn = side ? -1.0f : 1.0f;
                float   at  = smp[i].s - sgn * 2.0f;
                int     j   = 1;
                float   rx, ry;
                int32_t tc, tr;
                if (at < 0.3f || at > total - 0.3f)
                    continue;
                while (j < ns - 1 && smp[j].s < at)
                    ++j;
                rx = -smp[j].dir.y * sgn * 0.30f;
                ry = smp[j].dir.x * sgn * 0.30f;
                tc = (int32_t)floorf(smp[j].pos.x + rx);
                tr = (int32_t)floorf(smp[j].pos.y + ry);
                if (tc < 0 || tr < 0 || tc >= R_MAP || tr >= R_MAP)
                    continue;
                if (put_box(m, c, mask_bit, tile_order(c, tc, tr, mask_bit) + 0.3f, smp[j].pos.x + rx, smp[j].pos.y + ry, 0.012f, 0.012f, 0.0f, 0.19f, MAT_LAMP, 0.0f) != 0)
                    return -1;
            }
        }
    }
    /*  The road's approach to a level crossing (spec 3.15): from the RXR
     *  stencil to the stop line the lines are solid; the quads within a
     *  tile and a half of a crossing tile's centre carry the approach
     *  material with the distance to the crossing along. */
    {
        static float xs[64];
        int          nx = 0;
        if (f == F_ROAD)
            for (i = 0; i < ns && nx < 64; ++i)
            {
                int32_t col = (int32_t)floorf(smp[i].pos.x), row = (int32_t)floorf(smp[i].pos.y);
                uint8_t b;
                if (col < 0 || row < 0 || col >= R_MAP || row >= R_MAP)
                    continue;
                b = c->xbld[row * R_MAP + col];
                if ((b == 0x45u || b == 0x46u) && fabsf(smp[i].pos.x - (float)col - 0.5f) < 0.07f && fabsf(smp[i].pos.y - (float)row - 0.5f) < 0.07f)
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
    /*  The caps: every grid corner within reach of the band takes the
     *  lowest road height over it, a hair under, so the next pass's
     *  ground lies just below the road and the profile comes out the same. */
    if (s_pass == 1)
        for (i = 0; i < ns; ++i)
        {
            int32_t cc = (int32_t)floorf(smp[i].pos.x), cr = (int32_t)floorf(smp[i].pos.y);
            int     k;
            for (k = 0; k < 4; ++k)
            {
                int32_t gc = cc + ((k == NE || k == SE) ? 1 : 0), gr = cr + ((k == SW || k == SE) ? 1 : 0);
                float   dx, dy;
                if (gc < 0 || gr < 0 || gc >= GRID || gr >= GRID)
                    continue;
                dx = (float)gc - smp[i].pos.x;
                dy = (float)gr - smp[i].pos.y;
                if (dx * dx + dy * dy > (hw + 0.3f) * (hw + 0.3f)) /* a corner the band reaches */
                    continue;
                if (smp[i].z - 0.03f < s_zcap[gr * GRID + gc])
                    s_zcap[gr * GRID + gc] = smp[i].z - 0.03f;
            }
        }
    /* the quads and their skirts */
    for (i = 1; i < ns; ++i)
    {
        const Sample *pv = &smp[i - 1], *cu = &smp[i];
        float         ha    = hw * width_factor(pv->dir.x, pv->dir.y, comp);
        float         hb    = hw * width_factor(cu->dir.x, cu->dir.y, comp);
        float         a0[2] = {pv->pos.x + pv->dir.y * ha, pv->pos.y - pv->dir.x * ha};
        float         a1[2] = {pv->pos.x - pv->dir.y * ha, pv->pos.y + pv->dir.x * ha};
        float         b0[2] = {cu->pos.x + cu->dir.y * hb, cu->pos.y - cu->dir.x * hb};
        float         b1[2] = {cu->pos.x - cu->dir.y * hb, cu->pos.y + cu->dir.x * hb};
        float         mx = 0.5f * (pv->pos.x + cu->pos.x), my = 0.5f * (pv->pos.y + cu->pos.y);
        int32_t       tc = (int32_t)floorf(mx), tr = (int32_t)floorf(my);
        float         order, ma = mat, al_a = pv->s, al_b = cu->s;
        int           side;
        if (tc < 0)
            tc = 0;
        if (tr < 0)
            tr = 0;
        if (tc >= R_MAP)
            tc = R_MAP - 1;
        if (tr >= R_MAP)
            tr = R_MAP - 1;
        order        = tile_order(c, tc, tr, mask_bit);
        s_road_class = s_hiway ? 3.0f : f != F_ROAD ? 0.0f : s_seg_class >= 0.0f ? s_seg_class
                                                                : road_class(c, tc, tr);
        if (f == F_ROAD && pv->xd > 0.45f && 0.5f * (pv->xd + cu->xd) < 1.55f)
        {
            ma   = MAT_XAPPROACH; /* along: the distance to the crossing */
            al_a = pv->xd;
            al_b = cu->xd;
        }
        else if (f == F_ROAD && zeb0 > 0.0f && cu->s <= zeb0 + 1e-4f)
            ma = MAT_ZEBRA; /* the band's along runs from the box outward */
        else if (f == F_ROAD && zeb1 > 0.0f && pv->s >= total - zeb1 - 1e-4f)
        {
            ma   = MAT_ZEBRA;
            al_a = total - pv->s;
            al_b = total - cu->s;
        }
        {
            /*  A quad in a cut, the road below the ground at a corner, is
             *  flagged in its class (4 and up) so the clipping check knows
             *  the ground over it is meant, held back by the walls below. */
            float gc[4] = {surface_at_world(c, mask_bit, a0[0], a0[1]), surface_at_world(c, mask_bit, a1[0], a1[1]), surface_at_world(c, mask_bit, b0[0], b0[1]), surface_at_world(c, mask_bit, b1[0], b1[1])};
            int   cut   = pv->z < gc[0] - 0.015f || pv->z < gc[1] - 0.015f || cu->z < gc[2] - 0.015f || cu->z < gc[3] - 0.015f ||
                          pv->z < s_zorig[i - 1] - 0.005f || cu->z < s_zorig[i] - 0.005f;
            float cls   = s_road_class;
            if (cut)
                s_road_class += 4.0f;
            if (strip_quad_z(m, c, mask_bit, order, a0, a1, b0, b1, pv->z, cu->z, -1.0f, 1.0f, al_a, al_b, ma) != 0)
                return -1;
            s_road_class = cls;
        }
        /* the skirts: where the road stands above the ground at an edge, blocks down to it;
         * where it lies below, a retaining wall from the ground down to its edge, facing in */
        for (side = 0; side < 2; ++side)
        {
            static const float blocks[3] = {0.0f, 0.0f, MAT_SKIRT};

            const float *ea = side ? a1 : a0, *eb = side ? b1 : b0;
            float        ga    = surface_at_world(c, mask_bit, ea[0], ea[1]);
            float        gb    = surface_at_world(c, mask_bit, eb[0], eb[1]);
            float        t0[3] = {ea[0], ea[1], pv->z}, t1[3] = {eb[0], eb[1], cu->z};
            float        q0[3] = {ea[0], ea[1], ga}, q1[3] = {eb[0], eb[1], gb};
            float        nrm[3];
            nrm[0] = side ? -cu->dir.y : cu->dir.y;
            nrm[1] = side ? cu->dir.x : -cu->dir.x;
            nrm[2] = 0.0f;
            if (s_hiway)
            {
                /*  A deck stands clear of the ground the whole way, so
                 *  the embankment below would be one long wall of earth
                 *  under it.  What belongs there instead is the edge of
                 *  the structure: the girder's fascia down from the deck
                 *  and the parapet up from it, the bents carrying the
                 *  weight down (spec 7.2). */
                static const float conc[3] = {0.0f, 0.0f, MAT_SKIRT};
                float g0[3] = {ea[0], ea[1], pv->z - HIWAY_GIRDER};
                float g1[3] = {eb[0], eb[1], cu->z - HIWAY_GIRDER};
                float p0[3] = {ea[0], ea[1], pv->z + HIWAY_PARAPET};
                float p1[3] = {eb[0], eb[1], cu->z + HIWAY_PARAPET};
                if (put_wall(m, t0, t1, g0, g1, nrm, order, conc) != 0)
                    return -1;
                if (put_wall(m, p0, p1, t0, t1, nrm, order, conc) != 0)
                    return -1;
                continue;
            }
            if (pv->z > ga + 0.035f || cu->z > gb + 0.035f)
            {
                if (q0[2] > t0[2])
                    q0[2] = t0[2];
                if (q1[2] > t1[2])
                    q1[2] = t1[2];
                if (put_wall(m, t0, t1, q0, q1, nrm, order, blocks) != 0)
                    return -1;
            }
            if (pv->z < ga - 0.015f || cu->z < gb - 0.015f)
            {
                float u0[3] = {ea[0], ea[1], ga > pv->z ? ga : pv->z}, u1[3] = {eb[0], eb[1], gb > cu->z ? gb : cu->z};
                float in[3] = {-nrm[0], -nrm[1], 0.0f};
                /* the embankment's material, which the watertight check leaves out with the strip */
                if (put_wall(m, u0, u1, t0, t1, in, order, blocks) != 0)
                    return -1;
            }
        }
    }
    return 0;
}

static float v2cross(V2 a, V2 b)
{
    return a.x * b.y - a.y * b.x;
}

/*  The point where the line through a in direction da meets the line
 *  through b in direction db; 0 when they are parallel. */
int line_meet(V2 a, V2 da, V2 b, V2 db, V2 *out)
{
    float den = v2cross(da, db);
    float t;
    if (fabsf(den) < 1e-5f)
        return 0;
    t      = v2cross((V2){b.x - a.x, b.y - a.y}, db) / den;
    out->x = a.x + da.x * t;
    out->y = a.y + da.y * t;
    return 1;
}

/*  Straighten a polyline (spec 3.10, step 4): collinear points go; a
 *  staircase -- a run of two or more bends turning left, right, left
 *  with grid-aligned legs of at most three tiles between them -- becomes
 *  one line in the run's net direction through its corners' centroid,
 *  joined to the legs before and after where it crosses them.  The rule
 *  is explicit rather than a chord tolerance: a staircase's corners
 *  alternate between two parallel lines a tile-diagonal apart, so any
 *  chord leaves half of them standing off it.  Writes to `out`. */
static int straighten(const V2 *p, int n, V2 *out)
{
    static V2 w[MAX_PTS];
    int       m = 0, i, changed, passes = 0;
    for (i = 0; i < n; ++i)
    {
        if (i > 0 && i < n - 1)
        {
            V2 a = {p[i].x - w[m - 1].x, p[i].y - w[m - 1].y};
            V2 b = {p[i + 1].x - p[i].x, p[i + 1].y - p[i].y};
            if (fabsf(v2cross(a, b)) < 1e-4f && a.x * b.x + a.y * b.y > 0.0f)
                continue;
        }
        w[m++] = p[i];
    }
    do
    {
        changed = 0;
        for (i = 1; i < m - 1 && !changed; ++i)
        {
            int   j = i, k, grid = 1;
            float sign_prev = 0.0f;
            /*  A run that would end after one period because its first
             *  inner leg is unlike the third yields to the run from the
             *  next corner when that one is longer: Toronto's rail at
             *  column 108, row 25, a two-tile leg into a 1:1 staircase,
             *  had been taken as a 2:1 stair of one step joined to the
             *  1:1 stair by a half-tile jog, an abrupt turn where the
             *  staircase should have begun a tile later. */
            if (i + 4 < m)
            {
                V2 l1 = {w[i + 1].x - w[i].x, w[i + 1].y - w[i].y};
                V2 l3 = {w[i + 3].x - w[i + 2].x, w[i + 3].y - w[i + 2].y};
                V2 l2 = {w[i + 2].x - w[i + 1].x, w[i + 2].y - w[i + 1].y};
                V2 l4 = {w[i + 4].x - w[i + 3].x, w[i + 4].y - w[i + 3].y};
                if (fabsf(v2len(l1) - v2len(l3)) > 0.1f && fabsf(v2len(l2) - v2len(l4)) <= 0.1f && v2len(l1) > 1.05f)
                    continue;
            }
            for (k = i; k < m - 1; ++k)
            {
                V2    a    = {w[k].x - w[k - 1].x, w[k].y - w[k - 1].y};
                V2    b    = {w[k + 1].x - w[k].x, w[k + 1].y - w[k].y};
                float cr   = v2cross(a, b);
                float sign = cr > 1e-4f ? 1.0f : cr < -1e-4f ? -1.0f
                                                             : 0.0f;
                if (sign == 0.0f)
                    break;
                if (k > i && (sign == sign_prev || v2len(a) > 3.05f))
                    break;
                /*  The legs inside a staircase repeat every two: a leg
                 *  unlike the one two before it ends the run there, so a
                 *  stair that meets a straight through a crossing and
                 *  goes on as a stair is two stairs and a straight, not
                 *  one irregular run left to the fillet (Barcelona's rail
                 *  at column 105, rows 4 to 11). */
                if (k >= i + 3)
                {
                    V2 a2 = {w[k - 2].x - w[k - 3].x, w[k - 2].y - w[k - 3].y};
                    if (fabsf(v2len(a) - v2len(a2)) > 0.1f)
                        break;
                }
                if (k > i && fabsf(a.x) > 1e-3f && fabsf(a.y) > 1e-3f)
                    grid = 0; /* a diagonal leg inside the run: already straightened */
                sign_prev = sign;
                j         = k;
            }
            /*  Two full periods at least (spec 3.10, step 4): a single
             *  N, E pair is a corner and N, E, N a jog, both filleted. */
            if (j - i + 1 >= 3 && grid)
            {
                /*  The staircase's corners lie on two parallel lines, the
                 *  left-turning corners on one and the right-turning on
                 *  the other; the centreline is their midline, exactly.
                 *  The direction is that between corners two apart, which
                 *  share a line, not the chord from first to last, which
                 *  leans across by half a step when the run starts and
                 *  ends on different lines.  A run whose corners do not
                 *  sit on two lines -- legs of one, three, one, two -- is
                 *  no staircase and keeps its corners for the fillet. */
                V2    cen, dir = {w[i + 2].x - w[i].x, w[i + 2].y - w[i].y};
                float L = v2len(dir);
                V2    qin, qout, din = {w[i].x - w[i - 1].x, w[i].y - w[i - 1].y};
                V2    dout = {w[j + 1].x - w[j].x, w[j + 1].y - w[j].y};
                int   t, na = 0, nb = 0, regular = 1;
                float sa = 0.0f, sb = 0.0f, mid, ma, mb;
                if (L < 1e-4f)
                    continue;
                dir.x /= L;
                dir.y /= L;
                for (k = i; k <= j; ++k)
                {
                    V2    a   = {w[k].x - w[k - 1].x, w[k].y - w[k - 1].y};
                    V2    b   = {w[k + 1].x - w[k].x, w[k + 1].y - w[k].y};
                    float off = v2cross(dir, (V2){w[k].x - w[i].x, w[k].y - w[i].y});
                    if (v2cross(a, b) > 0.0f)
                    {
                        sa += off;
                        ++na;
                    }
                    else
                    {
                        sb += off;
                        ++nb;
                    }
                }
                if (!na || !nb)
                    continue;
                {
                    /*  The legs between the corners alternate: a riser of
                     *  one tile and a run of one to three, every riser and
                     *  every run alike (1:1, 2:1, 3:1).  Three-tile legs
                     *  both ways are a zig-zag of corners, not a staircase,
                     *  and its midline would leave the road tiles. */
                    float l0 = -1.0f, l1 = -1.0f;
                    int   ok = 1;
                    for (k = i; k < j; ++k)
                    {
                        float  len = v2len((V2){w[k + 1].x - w[k].x, w[k + 1].y - w[k].y});
                        float *ref = ((k - i) & 1) ? &l1 : &l0;
                        /* whole tiles: a leg left by an earlier join is no step */
                        if (fabsf(len - rintf(len)) > 0.1f || len < 0.9f)
                            ok = 0;
                        if (*ref < 0.0f)
                            *ref = len;
                        else if (fabsf(*ref - len) > 0.1f)
                            ok = 0;
                    }
                    if (!ok || (l0 > 1.05f && l1 > 1.05f))
                        continue;
                }
                ma = sa / (float)na;
                mb = sb / (float)nb;
                for (k = i; k <= j; ++k)
                {
                    V2    a   = {w[k].x - w[k - 1].x, w[k].y - w[k - 1].y};
                    V2    b   = {w[k + 1].x - w[k].x, w[k + 1].y - w[k].y};
                    float off = v2cross(dir, (V2){w[k].x - w[i].x, w[k].y - w[i].y});
                    if (fabsf(off - (v2cross(a, b) > 0.0f ? ma : mb)) > 0.2f)
                        regular = 0;
                }
                if (!regular)
                    continue;
                mid   = 0.5f * (ma + mb);
                cen.x = w[i].x - dir.y * mid;
                cen.y = w[i].y + dir.x * mid;
                if (!line_meet(w[i - 1], din, cen, dir, &qin) || !line_meet(w[j], dout, cen, dir, &qout))
                    continue;
                /* a leg nearly parallel to the midline meets it far away: no join there */
                if (v2len((V2){qin.x - w[i].x, qin.y - w[i].y}) > 3.0f || v2len((V2){qout.x - w[j].x, qout.y - w[j].y}) > 3.0f)
                    continue;
                /*  The join must cut the leg it joins between that leg's
                 *  ends: past an end the polyline would double back on
                 *  itself.  Toronto's rail at column 92, row 113: a stair
                 *  of one three-tile step and a one-tile jog, taken as a
                 *  3:1 staircase, had its midline meet the one-tile leg
                 *  after it half a tile past its corner, a hairpin drawn
                 *  in the middle of a diagonal.  Refused, the corners
                 *  stand, and the 1:1 staircase they begin is found from
                 *  the next corner instead. */
                {
                    float tin  = ((qin.x - w[i - 1].x) * din.x + (qin.y - w[i - 1].y) * din.y) / (din.x * din.x + din.y * din.y);
                    float tout = ((qout.x - w[j].x) * dout.x + (qout.y - w[j].y) * dout.y) / (dout.x * dout.x + dout.y * dout.y);
                    if (tin < -1e-3f || tin > 1.001f || tout < -1e-3f || tout > 1.001f)
                        continue;
                }
                w[i]     = qin;
                w[i + 1] = qout;
                for (t = j + 1; t < m; ++t)
                    w[i + 2 + (t - j - 1)] = w[t];
                m       = i + 2 + (m - j - 1);
                changed = 1;
            }
        }
    } while (changed && ++passes < 64);
    /* a join that lands on the corner it replaces leaves the point twice; once is enough */
    {
        int o = 0;
        for (i = 0; i < m; ++i)
            if (o == 0 || fabsf(w[i].x - out[o - 1].x) > 1e-3f || fabsf(w[i].y - out[o - 1].y) > 1e-3f)
                out[o++] = w[i];
        m = o;
    }
    return m;
}

/*  A rail's corners cut (the user, on Toronto's rail at column 108,
 *  row 25: "why can't it have a softer angle to reach its destination?"):
 *  a leg of two tiles or less between two longer legs whose lines meet
 *  on one of those legs, at a turn of sixty degrees or less, goes, the
 *  two corners it made replaced by that one point, which the fillet
 *  rounds.  A line down a column turning west a tile and a half, then
 *  north-west along a staircase, becomes a line down the column that
 *  bends once, forty-five degrees, onto the staircase's diagonal a tile
 *  and a half further on; the rail tile at the old corner is bypassed,
 *  which the check for undrawn pieces allows a rail beside its line. */
static int cut_corners(V2 *q, int n)
{
    int i, changed = 1;
    while (changed && n >= 4)
    {
        changed = 0;
        for (i = 1; i + 2 < n; ++i)
        {
            V2    a = q[i - 1], b = q[i], c = q[i + 1], d = q[i + 2], x;
            V2    ab = {b.x - a.x, b.y - a.y}, bc = {c.x - b.x, c.y - b.y}, cd = {d.x - c.x, d.y - c.y};
            float lab = v2len(ab), lbc = v2len(bc), lcd = v2len(cd), t, u, cosang;
            if (lbc > 2.05f || lab < 1e-4f || lcd < 1e-4f || lbc < 1e-4f)
                continue;
            cosang = (ab.x * cd.x + ab.y * cd.y) / (lab * lcd);
            if (cosang < 0.5f)
                continue; /* a turn over sixty degrees keeps its corners */
            if (!line_meet(a, ab, c, cd, &x))
                continue;
            t = ((x.x - a.x) * ab.x + (x.y - a.y) * ab.y) / (lab * lab); /* along a-b */
            u = ((x.x - c.x) * cd.x + (x.y - c.y) * cd.y) / (lcd * lcd); /* along c-d */
            if ((t > 0.2f && t < 1.0f - 1e-3f && lab - t * lab <= 2.05f) || (u > 1e-3f && u < 0.8f && u * lcd <= 2.05f))
            {
                int k;
                q[i] = x;
                for (k = i + 1; k + 1 < n; ++k)
                    q[k] = q[k + 1];
                --n;
                changed = 1;
                break;
            }
        }
    }
    return n;
}

/*  A lone piece no neighbour joins: a band across its own tile along the
 *  axis its art links, both ends capped, as the original's lone sprite. */
int build_island(RMesh *m, const RCity *c, const RAtlasLevel *l, uint8_t mask_bit, int comp, Family f, int32_t col, int32_t row)
{
    Piece pc;
    int   links = tile_links(c, l, col, row, f), ns;
    float cx = (float)col + 0.5f, cy = (float)row + 0.5f;
    if (!links)
        return 0;
    ns               = (links & (L_N | L_S)) ? 1 : 0;
    pc.arc           = 0;
    pc.a             = (V2){ns ? cx : cx - 0.49f, ns ? cy - 0.49f : cy}; /* a hair inside the tile, so the end stations read its surface */
    pc.b             = (V2){ns ? cx : cx + 0.49f, ns ? cy + 0.49f : cy};
    s_seg_node[0][0] = s_seg_node[1][0] = col;
    s_seg_node[0][1] = s_seg_node[1][1] = row;
    s_seg_kind[0] = s_seg_kind[1] = 0;
    s_seg_ctrl[0] = s_seg_ctrl[1] = 0;
    pc.c                          = pc.a;
    pc.len                        = 0.98f;
    pc.r                          = 0.0f;
    pc.t0 = pc.t1 = 0.0f;
    return loft(m, c, mask_bit, comp, f, &pc, 1, 0.98f, 0.0f, 0.0f, 0, 0);
}

/*  A node of a family's network: a junction (three or four links), an
 *  end (one), or nothing. */
int node_kind(const RCity *c, const RAtlasLevel *l, Family f, int32_t col, int32_t row)
{
    /*  A junction piece is a node whatever its neighbours return: its
     *  box is drawn and its dangling arms end at the box (Barcelona's
     *  crossing at column 101, row 0, two arms into buildings, was
     *  walked through as a bend to the map's edge).  Otherwise one
     *  returned link makes an end. */
    int n = link_count(eff_links(c, l, col, row, f));
    if (link_count(tile_links(c, l, col, row, f)) >= 3)
        return 2;
    return n == 1 ? 1 : 0;
}

/*  Where a segment ends on an end tile, and how.  The tile's art links
 *  that no neighbour returns point at the dead side; what stands there
 *  decides: against a building, a bridge, a tunnel end or a highway
 *  the road runs square to the tile's edge, as the original draws the
 *  straight piece whole and the carrier's sprite goes on from there;
 *  against open land the segment ends at the tile's centre and a road
 *  gets its turning head.  Returns 1 for a square end. */
static int end_point(const RCity *c, const RAtlasLevel *l, Family f, int32_t col, int32_t row, V2 *pt)
{
    int dead = tile_links(c, l, col, row, f) & ~eff_links(c, l, col, row, f), e;
    *pt      = (V2){(float)col + 0.5f, (float)row + 0.5f};
    for (e = 0; e < 4; ++e)
    {
        int32_t nc, nr;
        uint8_t b;
        if (!(dead & (1 << e)))
            continue;
        nc = col + (int32_t)ROAD_DU[e];
        nr = row + (int32_t)ROAD_DV[e];
        if (nc < 0 || nr < 0 || nc >= R_MAP || nr >= R_MAP)
            continue;
        b = c->xbld[nr * R_MAP + nc];
        if (b >= 0x3Bu) /* a tunnel end, a highway, a bridge, a building: a carrier */
        {
            pt->x += ROAD_DU[e] * 0.49f;
            pt->y += ROAD_DV[e] * 0.49f;
            return 1;
        }
    }
    return 0;
}

/*  Walk one segment of a family from a node tile out through link `e`,
 *  collect its centreline, straighten, fillet and loft it.  `visited`
 *  marks (tile, link) so each segment is walked once, from either end. */
int walk_segment(RMesh *m, const RCity *c, const RAtlasLevel *l, uint8_t mask_bit, int comp, Family f, int32_t col, int32_t row, int e, uint8_t *visited)
{
    static V2    pts[MAX_PTS];
    static V2    q[MAX_PTS];
    static Piece pieces[MAX_PIECES];
    float        hw = f == F_ROAD ? ROAD_W * 0.5f : RAIL_W * 0.5f;
    int          n = 0, k, nk, np, kind0 = node_kind(c, l, f, col, row), kind1 = 0;
    int          square0 = 0, square1 = 0; /* an end that runs square to a carrier */
    int32_t      cc = col, cr = row, back = (e + 2) & 3, guard = 0;
    int          ee    = e;
    float        total = 0.0f, rmax;
    if (visited[(row * R_MAP + col) * 4 + e])
        return 0;
    visited[(row * R_MAP + col) * 4 + e] = 1;
    /* the start: the side of the junction box, or the end tile's centre */
    if (kind0 == 2)
        pts[n++] = (V2){(float)col + 0.5f + ROAD_DU[e] * hw, (float)row + 0.5f + ROAD_DV[e] * hw};
    else
        square0 = end_point(c, l, f, col, row, &pts[n++]);
    for (;;)
    {
        int links, other;
        cc += (int32_t)ROAD_DU[ee];
        cr += (int32_t)ROAD_DV[ee];
        back = (ee + 2) & 3;
        if (cc < 0 || cr < 0 || cc >= R_MAP || cr >= R_MAP)
        {
            /* off the map: the segment runs to the edge, the last tile's side */
            pts[n++] = (V2){(float)(cc - (int32_t)ROAD_DU[ee]) + 0.5f + ROAD_DU[ee] * 0.5f,
                            (float)(cr - (int32_t)ROAD_DV[ee]) + 0.5f + ROAD_DV[ee] * 0.5f};
            kind1    = 0;
            break;
        }
        links = eff_links(c, l, cc, cr, f);
        if (!(links & (1 << back)))
            break; /* cannot happen on effective links; kept as a guard */
        visited[(cr * R_MAP + cc) * 4 + back] = 1;
        if (n + 2 >= MAX_PTS || ++guard > 4096)
            break;
        kind1 = node_kind(c, l, f, cc, cr);
        if (kind1 != 0 || link_count(links) != 2)
        {
            /* a node: the far end */
            if (kind1 == 0)
                kind1 = 1;
            if (kind1 == 2)
                pts[n++] = (V2){(float)cc + 0.5f + ROAD_DU[back] * hw, (float)cr + 0.5f + ROAD_DV[back] * hw};
            else
                square1 = end_point(c, l, f, cc, cr, &pts[n++]);
            break;
        }
        pts[n++]                            = (V2){(float)cc + 0.5f, (float)cr + 0.5f};
        other                               = links & ~(1 << back);
        ee                                  = other == L_N ? 0 : other == L_E ? 1
                                                             : other == L_S   ? 2
                                                                              : 3;
        visited[(cr * R_MAP + cc) * 4 + ee] = 1;
        if (cc == col && cr == row)
            break; /* a loop back to the start */
    }
    if (n < 2)
        return 0;
    /*  Straighten (spec 3.10, step 4).  First the collinear points go,
     *  so a straight run is one edge.  Then a staircase -- a run of two
     *  or more bends turning left, right, left with legs of at most
     *  three tiles between them -- becomes one straight line: its
     *  direction the run's net direction, through the centroid of its
     *  corners, joined to the legs before and after at the points where
     *  it crosses them.  A 45-degree staircase gives a 45-degree road, a
     *  2:1 staircase a 2:1 road; a lone corner or a U-turn is left for
     *  the fillet. */
    /*  One class for the whole segment, the median of its tiles', so an
     *  avenue's centre line does not start and stop mid-block with the
     *  traffic count of each tile (Bay View's shore road). */
    if (f == F_ROAD)
    {
        int cnt[3] = {0, 0, 0}, half, acc = 0, cls;
        for (k = 0; k < n; ++k)
        {
            int32_t tc = (int32_t)floorf(pts[k].x), tr = (int32_t)floorf(pts[k].y);
            if (tc < 0 || tr < 0 || tc >= R_MAP || tr >= R_MAP)
                continue;
            ++cnt[(int)road_class(c, tc, tr)];
        }
        half = (cnt[0] + cnt[1] + cnt[2] + 1) / 2;
        for (cls = 0; cls < 2; ++cls)
        {
            acc += cnt[cls];
            if (acc >= half)
                break;
        }
        s_seg_class = (float)cls;
    }
    else
        s_seg_class = -1.0f;
    nk = straighten(pts, n, q);
    if (f == F_RAIL)
        nk = cut_corners(q, nk);
    {
        /*  SC2K_ROAD_DUMP=1 prints every segment of six tiles or more;
         *  SC2K_ROAD_DUMP=col,row every segment through that tile. */
        const char *dump = getenv("SC2K_ROAD_DUMP");
        int         dc = -1, dr = -1, show = 0;
        if (dump && sscanf(dump, "%d,%d", &dc, &dr) == 2)
        {
            for (k = 0; k < n; ++k)
                if ((int32_t)floorf(pts[k].x) == dc && (int32_t)floorf(pts[k].y) == dr)
                    show = 1;
        }
        else if (dump && n >= 6)
            show = 1;
        if (show)
        {
            printf("segment f%d from c%d r%d e%d: %d points, %d kept:", (int)f, (int)col, (int)row, e, n, nk);
            for (k = 0; k < nk; ++k)
                printf(" (%.2f,%.2f)", (double)q[k].x, (double)q[k].y);
            printf("\n  raw:");
            for (k = 0; k < n; ++k)
                printf(" (%.2f,%.2f)", (double)pts[k].x, (double)pts[k].y);
            printf("\n");
        }
    }
    /* fillet: a lone corner's quarter circle, a diagonal merge's sweep of up to a tile (spec 3.9) */
    rmax = 1.0f;
    if (fillet(q, nk, rmax, pieces, &np) != 0 || np == 0)
        return 0;
    for (k = 0; k < np; ++k)
        total += pieces[k].len;
    s_seg_node[0][0] = col;
    s_seg_node[0][1] = row;
    s_seg_node[1][0] = cc;
    s_seg_node[1][1] = cr;
    s_seg_kind[0]    = kind0;
    s_seg_kind[1]    = kind1;
    s_seg_ctrl[0]    = (f == F_ROAD && kind0 == 2) ? (s_junc_ctrl[row * R_MAP + col] >> (2 * e)) & 3 : 0;
    s_seg_ctrl[1]    = (f == F_ROAD && kind1 == 2) ? (s_junc_ctrl[cr * R_MAP + cc] >> (2 * back)) & 3 : 0;
    /* the crosswalk and stop bar only on a controlled leg (spec 3.4) */
    if (loft(m, c, mask_bit, comp, f, pieces, np, total, s_seg_ctrl[0] ? 0.2f : 0.0f, s_seg_ctrl[1] ? 0.2f : 0.0f, kind0 == 2, kind1 == 2) != 0)
        return -1;
    /* an end on open land: a road's round cap, the turning head of a dead end; a rail ends square */
    if (f == F_ROAD)
    {
        int which;
        for (which = 0; which < 2; ++which)
        {
            int   at_end = which == 1;
            int   end    = at_end ? np - 1 : 0;
            V2    pos, dir;
            float h, ang;
            if (at_end ? (kind1 != 1 || square1) : (kind0 != 1 || square0))
                continue;
            /*  Every dead end gets its round cap (the user: "all dead end
             *  roads should have endcaps"); spec 3.10's step 11 keeps the
             *  turning head for a local road with open land around it, and
             *  ends an avenue square, but a square end in the middle of a
             *  tile is a raw edge, so the cap is unconditional. */
            piece_at(&pieces[end], at_end ? pieces[end].len : 0.0f, &pos, &dir);
            if (!at_end)
            {
                dir.x = -dir.x;
                dir.y = -dir.y;
            }
            h   = hw * width_factor(dir.x, dir.y, comp);
            ang = atan2f(dir.y, dir.x);
            {
                int32_t tc = (int32_t)floorf(pos.x), tr = (int32_t)floorf(pos.y);
                if (tc < 0)
                    tc = 0;
                if (tr < 0)
                    tr = 0;
                if (tc >= R_MAP)
                    tc = R_MAP - 1;
                if (tr >= R_MAP)
                    tr = R_MAP - 1;
                if (strip_fan_z(m, c, mask_bit, tile_order(c, tc, tr, mask_bit), pos.x, pos.y, ang - 1.5707963f, ang + 1.5707963f, h, h, 0.0f, MAT_ROAD, 8, 0.03f) != 0)
                    return -1;
            }
        }
    }
    return 0;
}

/*  The control of a road junction's arms (spec 3.4), from the classes
 *  of the roads meeting there, read on the first tile of each arm: all
 *  local, four legs, a two-way stop on the quieter axis; three legs
 *  with a local stem, a stop on the stem; a local against an avenue,
 *  stop on the local legs; avenue against avenue, an all-way stop, or a
 *  signal where the junction is busy; anything against a boulevard, a
 *  signal.  Two bits per arm. */
static int junction_control(const RCity *c, int32_t col, int32_t row, int links)
{
    int cls[4], ctrl[4] = {0, 0, 0, 0}, e, n = 0, maxc = 0, minc = 9, busy;
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
    if (maxc >= 2)
    {
        for (e = 0; e < 4; ++e)
            if (cls[e] >= 0)
                ctrl[e] = 2;
    }
    else if (maxc == 1 && minc == 1)
    {
        for (e = 0; e < 4; ++e)
            if (cls[e] >= 0)
                ctrl[e] = busy ? 2 : 1;
    }
    else if (maxc == 1)
    {
        for (e = 0; e < 4; ++e)
            if (cls[e] == 0)
                ctrl[e] = 1;
    }
    else if (n == 3)
    {
        /* the stem: the arm whose opposite is missing */
        for (e = 0; e < 4; ++e)
            if (cls[e] >= 0 && cls[(e + 2) & 3] < 0)
                ctrl[e] = 1;
    }
    else
    {
        /* four local legs: a two-way stop on the axis with the lighter traffic */
        int ns = traf[0] > traf[2] ? traf[0] : traf[2];
        int ew = traf[1] > traf[3] ? traf[1] : traf[3];
        for (e = 0; e < 4; ++e)
            if (cls[e] >= 0 && ((e == 0 || e == 2) ? ns <= ew : ew < ns))
                ctrl[e] = 1;
    }
    return ctrl[0] | (ctrl[1] << 2) | (ctrl[2] << 4) | (ctrl[3] << 6);
}

/*  A STOP sign at the driver's right of an approach, 1.5 m behind the
 *  crosswalk (spec 3.4, 6.3): a post with the bottom of the sign at
 *  2.1 m and a 750 mm red octagon facing the driver. */
static int put_stop_sign(RMesh *m, const RCity *c, int32_t col, int32_t row, uint8_t mask_bit, float order, int e, float h)
{
    static const int right[4] = {3, 0, 1, 2};
    int              r        = right[e];
    float            cx = (float)col + 0.5f, cy = (float)row + 0.5f;
    float            x = cx + ROAD_DU[e] * (h + 0.32f) + ROAD_DU[r] * (h + 0.03f);
    float            y = cy + ROAD_DV[e] * (h + 0.32f) + ROAD_DV[r] * (h + 0.03f);
    float            g = surface_at_world(c, mask_bit, x, y);
    if (put_box(m, c, mask_bit, order, x, y, 0.008f, 0.008f, 0.0f, 0.31f, MAT_PROP, 0.0f) != 0)
        return -1;
    return put_lamp_face(m, order, x, y, g, 0.29f, -ROAD_DU[e], -ROAD_DV[e], 0.025f, 0.0f, 13.0f);
}

/*  A junction of a family: the box, its sidewalk corners rounded as
 *  curb returns where two arms meet and square where a side is free,
 *  the outline along a free side, and for a road a signal on every arm. */
int build_junction(RMesh *m, const RCity *c, uint8_t mask_bit, Family f, int32_t col, int32_t row, int links, float order)
{
    float hw  = f == F_ROAD ? ROAD_W * 0.5f : RAIL_W * 0.5f;
    float mat = f == F_ROAD ? MAT_ROAD : MAT_RAIL;
    float cx = (float)col + 0.5f, cy = (float)row + 0.5f, h = hw;
    float sw    = h * 0.20f; /* the curb return's radius: the sidewalk's width      */
    float lw    = h * 0.20f; /* the sidewalk along a free side, across 0.8..1       */
    float a0[2] = {cx - h, cy - h}, a1[2] = {cx + h, cy - h}, b0[2] = {cx - h, cy + h}, b1[2] = {cx + h, cy + h};
    int   e;
    /*  The box stands the hair above the ground the strips do, 0.03 of a
     *  level: on the ground itself, under arms a hair higher, the oblique
     *  view showed a line of grass along every side of a rail box. */
    float zj     = surface_at_world(c, mask_bit, cx, cy) + 0.03f;
    s_road_class = 0.0f; /* the box is plain asphalt; a median ends at the junction */
    if (f == F_ROAD)
        s_junc_ctrl[row * R_MAP + col] = (uint8_t)junction_control(c, col, row, links);
    if (f == F_RAIL)
    {
        /*  A rail junction within its box (the user: "train T crossing
         *  are incorrect"; three half strips to the centre had crossed
         *  the branch's rails and ties through the through track's).
         *  The through line runs across the box whole.  A T is a
         *  junction of two double-track lines: the branch's inbound
         *  track, the right-hand one, curves right into the near through
         *  track, and its outbound track runs on across the near track,
         *  a diamond, and curves left into the far one, each curve a
         *  quarter circle of the largest radius the box allows, 0.177 of
         *  a tile; the spec's turnout (5.4) takes two to four tiles the
         *  art does not give.  A crossing is two whole strips, a diamond. */
        int n = link_count(links), eb = -1, ea = -1;
        for (e = 0; e < 4; ++e)
            if ((links & (1 << e)) && !(links & (1 << ((e + 2) & 3))))
                eb = e; /* the branch: the arm without an opposite */
            else if (links & (1 << e))
                ea = e; /* a through arm */
        if (n >= 3 && ea >= 0)
        {
            /* the through strip: across along the through arm's normal, along the through axis */
            float tx = ROAD_DU[ea], ty = ROAD_DV[ea], nx = -ty, ny = tx;
            float q0[2] = {cx - nx * h - tx * h, cy - ny * h - ty * h}, q1[2] = {cx + nx * h - tx * h, cy + ny * h - ty * h};
            float p0[2] = {cx - nx * h + tx * h, cy - ny * h + ty * h}, p1[2] = {cx + nx * h + tx * h, cy + ny * h + ty * h};
            if (strip_quad_z(m, c, mask_bit, order, q0, q1, p0, p1, zj, zj, -1.0f, 1.0f, -h, h, mat) != 0)
                return -1;
        }
        if (n == 4)
        {
            /* the other line of the diamond, over the first */
            float tx = ROAD_DV[ea], ty = -ROAD_DU[ea], nx = -ty, ny = tx;
            float q0[2] = {cx - nx * h - tx * h, cy - ny * h - ty * h}, q1[2] = {cx + nx * h - tx * h, cy + ny * h - ty * h};
            float p0[2] = {cx - nx * h + tx * h, cy - ny * h + ty * h}, p1[2] = {cx + nx * h + tx * h, cy + ny * h + ty * h};
            if (strip_quad_z(m, c, mask_bit, order + 0.03f, q0, q1, p0, p1, zj, zj, -1.0f, 1.0f, -h, h, mat) != 0)
                return -1;
        }
        else if (n == 3 && eb >= 0)
        {
            const float tk = 0.133f, r = h - 0.133f, tw = 0.087f; /* the track offset, the curve, half a tie */
            float       ax = -ROAD_DU[eb], ay = -ROAD_DV[eb];     /* inbound, from the branch's edge to the centre */
            float       rx = -ay, ry = ax;                        /* to the right of inbound */
            float       cX, cY, along, L0[2], L1[2], R0[2], R1[2];
            int         k;
            /*  The inbound track (right of inbound) curves right into the
             *  near through track: a quarter circle about the box's corner
             *  on that side, from the box's edge to its side. */
            cX = cx + rx * h - ax * h;
            cY = cy + ry * h - ay * h;
            for (k = 0; k < 6; ++k)
            {
                float t0 = 1.5707963f * (float)k / 6.0f, t1 = 1.5707963f * (float)(k + 1) / 6.0f;
                float ex0 = -rx * cosf(t0) + ax * sinf(t0), ey0 = -ry * cosf(t0) + ay * sinf(t0);
                float ex1 = -rx * cosf(t1) + ax * sinf(t1), ey1 = -ry * cosf(t1) + ay * sinf(t1);
                L0[0] = cX + ex0 * (r - tw), L0[1] = cY + ey0 * (r - tw);
                R0[0] = cX + ex0 * (r + tw), R0[1] = cY + ey0 * (r + tw);
                L1[0] = cX + ex1 * (r - tw), L1[1] = cY + ey1 * (r - tw);
                R1[0] = cX + ex1 * (r + tw), R1[1] = cY + ey1 * (r + tw);
                if (strip_quad_z(m, c, mask_bit, order + 0.03f, L0, R0, L1, R1, zj, zj, 0.15f, 0.71f, r * t0, r * t1, mat) != 0)
                    return -1;
            }
            /*  The outbound track (left of inbound) runs straight across
             *  the near track, then curves left into the far one. */
            {
                float p0x = cx - rx * tk - ax * h, p0y = cy - ry * tk - ay * h;
                float p1x = cx - rx * tk + ax * (tk - r), p1y = cy - ry * tk + ay * (tk - r);
                along = sqrtf((p1x - p0x) * (p1x - p0x) + (p1y - p0y) * (p1y - p0y));
                L0[0] = p0x - rx * tw, L0[1] = p0y - ry * tw;
                R0[0] = p0x + rx * tw, R0[1] = p0y + ry * tw;
                L1[0] = p1x - rx * tw, L1[1] = p1y - ry * tw;
                R1[0] = p1x + rx * tw, R1[1] = p1y + ry * tw;
                if (strip_quad_z(m, c, mask_bit, order + 0.03f, L0, R0, L1, R1, zj, zj, 0.15f, 0.71f, 0.0f, along, mat) != 0)
                    return -1;
                cX = p1x - rx * r;
                cY = p1y - ry * r;
            }
            for (k = 0; k < 6; ++k)
            {
                float t0 = 1.5707963f * (float)k / 6.0f, t1 = 1.5707963f * (float)(k + 1) / 6.0f;
                float ex0 = rx * cosf(t0) + ax * sinf(t0), ey0 = ry * cosf(t0) + ay * sinf(t0);
                float ex1 = rx * cosf(t1) + ax * sinf(t1), ey1 = ry * cosf(t1) + ay * sinf(t1);
                L0[0] = cX + ex0 * (r - tw), L0[1] = cY + ey0 * (r - tw);
                R0[0] = cX + ex0 * (r + tw), R0[1] = cY + ey0 * (r + tw);
                L1[0] = cX + ex1 * (r - tw), L1[1] = cY + ey1 * (r - tw);
                R1[0] = cX + ex1 * (r + tw), R1[1] = cY + ey1 * (r + tw);
                if (strip_quad_z(m, c, mask_bit, order + 0.03f, L0, R0, L1, R1, zj, zj, 0.15f, 0.71f, along + r * t0, along + r * t1, mat) != 0)
                    return -1;
            }
        }
        return 0;
    }
    if (strip_quad_z(m, c, mask_bit, order, a0, a1, b0, b1, zj, zj, 0.5f, 0.5f, -1.0f, -1.0f, mat) != 0)
        return -1;
    for (e = 0; e < 4; ++e)
    {
        /* the side: a free side carries the sidewalk and its curb along its length */
        float su = cx + ROAD_DU[e] * h, sv = cy + ROAD_DV[e] * h;
        float px = -ROAD_DV[e], py = ROAD_DU[e];
        if (!(links & (1 << e)))
        {
            float o0[2] = {su - px * h - ROAD_DU[e] * lw, sv - py * h - ROAD_DV[e] * lw};
            float o1[2] = {su + px * h - ROAD_DU[e] * lw, sv + py * h - ROAD_DV[e] * lw};
            float q0[2] = {su - px * h, sv - py * h}, q1[2] = {su + px * h, sv + py * h};
            if (strip_quad_z(m, c, mask_bit, order + 0.02f, o0, o1, q0, q1, zj, zj, 0.80f, 1.0f, -1.0f, -1.0f, mat) != 0)
                return -1;
        }
        else if (f == F_ROAD)
        {
            int ctrl = (s_junc_ctrl[row * R_MAP + col] >> (2 * e)) & 3;
            if (ctrl == 2 && put_signal(m, c, col, row, mask_bit, order, e, h) != 0)
                return -1;
            if (ctrl == 1 && put_stop_sign(m, c, col, row, mask_bit, order, e, h) != 0)
                return -1;
        }
    }
    if (f == F_ROAD)
        for (e = 0; e < 4; ++e)
        {
            /*  The corner between side e and side e+1.  Two arms meet
             *  there: a curb return, the sidewalk's corner rounded on a
             *  fillet of radius sw.  Otherwise the sidewalk's square. */
            int   ea = e, eb = (e + 1) & 3;
            float ku = cx + (ROAD_DU[ea] + ROAD_DU[eb]) * h, kv = cy + (ROAD_DV[ea] + ROAD_DV[eb]) * h;
            float iu = ku - (ROAD_DU[ea] + ROAD_DU[eb]) * sw, iv = kv - (ROAD_DV[ea] + ROAD_DV[eb]) * sw;
            if ((links & (1 << ea)) && (links & (1 << eb)))
            {
                /* the return: a fan from the corner point over the quarter circle about (iu, iv)...
                 * drawn as the region between the corner and the arc, as a fan from the corner */
                float ang0 = atan2f(ROAD_DV[ea] * h, ROAD_DU[ea] * h);
                float ang1 = atan2f(ROAD_DV[eb] * h, ROAD_DU[eb] * h);
                float t0   = atan2f(-(ROAD_DV[ea] + ROAD_DV[eb]), -(ROAD_DU[ea] + ROAD_DU[eb]));
                int   i;
                (void)ang0;
                (void)ang1;
                /* the arc about (iu, iv), radius sw, spanning the 90 degrees that face the corner */
                for (i = 0; i < 6; ++i)
                {
                    float fa = (float)i / 6.0f, fb = (float)(i + 1) / 6.0f;
                    float ta = t0 - 0.7853982f + 1.5707963f * fa, tb = t0 - 0.7853982f + 1.5707963f * fb;
                    float tri[3][3], ref[3] = {0.95f, 0.95f, 0.95f}, ref2[3] = {-1.0f, -1.0f, -1.0f};
                    float rc[3] = {0.0f, 0.0f, MAT_WALK};
                    /* the region between the arc and the corner: a fan from the corner point */
                    tri[0][0] = ku;
                    tri[0][1] = kv;
                    tri[1][0] = iu + sw * cosf(ta);
                    tri[1][1] = iv + sw * sinf(ta);
                    tri[2][0] = iu + sw * cosf(tb);
                    tri[2][1] = iv + sw * sinf(tb);
                    tri[0][2] = tri[1][2] = tri[2][2] = zj;
                    /* over the box, which a sidewalk face otherwise lies under */
                    if (put_tri_r2(m, (const float (*)[3])tri, NULL, order + 0.15f, rc, ref, ref2, 0) != 0)
                        return -1;
                }
                (void)ku;
                (void)kv;
            }
        }
    return 0;
}

/*  A power line on one tile: the pole at the centre with its crossbar,
 *  and a wire from its top to each joined edge's midpoint, where the
 *  neighbour's wire meets it.  On a crossing over a road or rail there
 *  is no pole: the wire spans from edge to edge. */
int build_power_tile(RMesh *m, const RCity *c, int32_t col, int32_t row, uint8_t mask_bit, int links, float order, int crossing)
{
    int   e;
    float top = 1.45f, cx = (float)col + 0.5f, cy = (float)row + 0.5f;
    if (!crossing)
    {
        int ns = (links & (L_N | L_S)) && !(links & (L_E | L_W));
        if (put_box(m, c, mask_bit, order, cx, cy, 0.04f, 0.04f, 0.0f, top, MAT_PROP, 0.0f) != 0)
            return -1;
        if (put_box(m, c, mask_bit, order, cx, cy, ns ? 0.24f : 0.04f, ns ? 0.04f : 0.24f, top - 0.12f, top - 0.08f, MAT_PROP, 0.0f) != 0)
            return -1;
        for (e = 0; e < 4; ++e)
            if (links & (1 << e))
                if (put_wire(m, c, mask_bit, order, cx, cy, top - 0.1f, (float)col + ROAD_MU[e], (float)row + ROAD_MV[e], top - 0.1f, 0.05f) != 0)
                    return -1;
        return 0;
    }
    if ((links & (L_N | L_S)) == (L_N | L_S))
        return put_wire(m, c, mask_bit, order, cx, (float)row, top - 0.1f, cx, (float)row + 1.0f, top - 0.1f, 0.1f);
    return put_wire(m, c, mask_bit, order, (float)col, cy, top - 0.1f, (float)col + 1.0f, cy, top - 0.1f, 0.1f);
}

/*  A prism clipped to the tile grid, each piece in the depth slot of
 *  the tile under it (the user: "train cars get masked off once in a
 *  while, like they flicker").  A car had carried the order of the tile
 *  under its centre, and the part of it over the next tile toward the
 *  viewer lay under that tile's ground until the centre crossed, when
 *  the whole car stood up at once.  `order` is the centre tile's slot
 *  with the fraction the pieces keep above each tile's ground. */
static int put_prism_clip_m(RMesh *m, const RCity *c, uint8_t mask_bit, float order, float cx, float cy, float dx, float dy, float len, float wid, float zb, float zf, float z0, float z1, float paint, float mat)
{
    static const float up[3] = {0.0f, 0.0f, 1.0f};
    float              ax = dx * len * 0.5f, ay = dy * len * 0.5f, bx = -dy * wid * 0.5f, by = dx * wid * 0.5f;
    float              p[4][3], top[4][3], col[3] = {paint, 0.0f, mat}, nrm[3], t3[3][3];
    float              ref[3] = {paint, paint, paint}, ref2[3] = {0.0f, 0.0f, 0.0f};
    int                k;
    p[0][0] = cx - ax - bx;
    p[0][1] = cy - ay - by;
    p[1][0] = cx + ax - bx;
    p[1][1] = cy + ay - by;
    p[2][0] = cx + ax + bx;
    p[2][1] = cy + ay + by;
    p[3][0] = cx - ax + bx;
    p[3][1] = cy - ay + by;
    for (k = 0; k < 4; ++k)
    {
        float zg  = (k == 1 || k == 2) ? zf : zb;
        p[k][2]   = zg + z0;
        top[k][0] = p[k][0];
        top[k][1] = p[k][1];
        top[k][2] = zg + z1;
    }
    for (k = 0; k < 4; ++k)
    {
        const float *a = p[k], *b = p[(k + 1) & 3], *ta = top[k], *tb = top[(k + 1) & 3];
        float        ex = b[0] - a[0], ey = b[1] - a[1], el = sqrtf(ex * ex + ey * ey);
        if (el < 1e-6f)
            continue;
        nrm[0] = ey / el;
        nrm[1] = -ex / el;
        nrm[2] = 0.0f;
        memcpy(t3[0], ta, sizeof t3[0]);
        memcpy(t3[1], tb, sizeof t3[1]);
        memcpy(t3[2], b, sizeof t3[2]);
        if (put_tri_road_n(m, c, mask_bit, order, (const float (*)[3])t3, nrm, col, ref, ref2) != 0)
            return -1;
        memcpy(t3[0], ta, sizeof t3[0]);
        memcpy(t3[1], b, sizeof t3[1]);
        memcpy(t3[2], a, sizeof t3[2]);
        if (put_tri_road_n(m, c, mask_bit, order, (const float (*)[3])t3, nrm, col, ref, ref2) != 0)
            return -1;
    }
    memcpy(t3[0], top[0], sizeof t3[0]);
    memcpy(t3[1], top[1], sizeof t3[1]);
    memcpy(t3[2], top[2], sizeof t3[2]);
    if (put_tri_road_n(m, c, mask_bit, order, (const float (*)[3])t3, up, col, ref, ref2) != 0)
        return -1;
    memcpy(t3[0], top[0], sizeof t3[0]);
    memcpy(t3[1], top[2], sizeof t3[1]);
    memcpy(t3[2], top[3], sizeof t3[2]);
    return put_tri_road_n(m, c, mask_bit, order, (const float (*)[3])t3, up, col, ref, ref2);
}

int put_prism_clip(RMesh *m, const RCity *c, uint8_t mask_bit, float order, float cx, float cy, float dx, float dy, float len, float wid, float zb, float zf, float z0, float z1, float paint)
{
    return put_prism_clip_m(m, c, mask_bit, order, cx, cy, dx, dy, len, wid, zb, zf, z0, z1, paint, MAT_VEHICLE);
}

/*  Every network: the power lines per tile; the roads and rails as
 *  junctions and segments. */
