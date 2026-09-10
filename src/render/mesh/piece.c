/*  piece.c: the PIECE, and the arithmetic every path is made of.
 *
 *  A piece is a straight or an arc.  These are the general operations
 *  over one.
 *
 *      Where it is at a distance along it.  The same piece moved
 *      sideways.  A run of them reversed.  The box one covers.  The
 *      nearest point on one.  The chain that joins two poses.
 *
 *
 *
 *  Nothing here knows what the piece is FOR.  A line's lane and a spur's
 *  descent are made of pieces.  So are a turnout's thread and a margin,
 *  all made of these and none of them is named in this file.  That is
 *  what makes it a primitive rather than a stage of some pass.
 *
 *  Sides.  The map's own right-hand normal of a direction d is (-d.y,
 *  d.x).  The map is drawn reflected (net/traffic.c), so the viewer's
 *  right is (d.y, -d.x).  Everything that offsets to one side or the
 *  other goes through l_right so the two conventions cannot part
 *  company. */
#include <math.h>

#include "mesh/internal.h"
#include "pipeline.h"

V2 l_right(V2 d)
{
    return (V2){d.y, -d.x};
}


/*  A point on a piece at arc length t, and the direction of travel
 *  there (net/loft.c's piece_at). */
void l_piece_at(const Piece *p, float t, V2 *pos, V2 *dir)
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


/*  The same piece shifted sideways by `off`, to the viewer's right of
 *  travel when positive.  A straight moves over.  An arc keeps its
 *  center and changes radius.  The viewer's right of the travel
 *  direction on an arc is the radial direction times the arc's sign). */
void piece_offset(const Piece *p, float off, Piece *o)
{
    *o = *p;
    if (!p->arc)
    {
        float l = p->len > 1e-6f ? p->len : 1.0f;
        V2    u = {(p->b.x - p->a.x) / l, (p->b.y - p->a.y) / l}, r = l_right(u);
        o->a = (V2){p->a.x + r.x * off, p->a.y + r.y * off};
        o->b = (V2){p->b.x + r.x * off, p->b.y + r.y * off};
    }
    else
    {
        static int gix_lane_arc_min = -1;
        float      s   = p->t1 > p->t0 ? 1.0f : -1.0f;
        float      lo  = net_geo(&gix_lane_arc_min, "lane_arc_min");
        float      r2  = p->r + off * s;
        if (r2 < lo)
            r2 = lo;
        o->r   = r2;
        o->a   = (V2){p->c.x + r2 * cosf(p->t0), p->c.y + r2 * sinf(p->t0)};
        o->b   = (V2){p->c.x + r2 * cosf(p->t1), p->c.y + r2 * sinf(p->t1)};
        o->len = p->r > 1e-6f ? p->len * r2 / p->r : p->len;
    }
}


void pieces_reverse(Piece *p, int np)
{
    int i;
    for (i = 0; i < np / 2; ++i)
    {
        Piece t       = p[i];
        p[i]          = p[np - 1 - i];
        p[np - 1 - i] = t;
    }
    for (i = 0; i < np; ++i)
    {
        V2 t   = p[i].a;
        p[i].a = p[i].b;
        p[i].b = t;
        if (p[i].arc)
        {
            float u = p[i].t0;
            p[i].t0 = p[i].t1;
            p[i].t1 = u;
        }
    }
}


/*  The chain the router lays between two poses: two points where B lies
 *  dead ahead of A, else the equal-tangent biarc's four: A, A + d tA, B
 *  - d tB, B, with the same tangent length at both ends.  Answers how
 *  many points, or 0 where there is no such lane, which is B behind A.
 *  Cutting the chain into pieces is arc.rules.pieces's and happens
 *  elsewhere: nothing is decided here. */
int pose_chain(V2 A, V2 tA, V2 B, V2 tB, V2 *q, float *rad, float *tlim)
{
    V2    v  = {B.x - A.x, B.y - A.y};
    float vv = v.x * v.x + v.y * v.y, vl = sqrtf(vv);
    float cr = tA.x * v.y - tA.y * v.x, dt = tA.x * tB.x + tA.y * tB.y, dv = tA.x * v.x + tA.y * v.y;
    if (vv < 1e-10f)
        return 0;
    if (fabsf(cr) < 1e-4f * vl && dt > 0.9999f && dv > 0.0f)
    {
        q[0] = A, q[1] = B;
        rad[0] = rad[1] = 0.0f;
        tlim[0] = tlim[1] = 0.0f;
        return 2;
    }
    {
        V2         s2 = {tA.x + tB.x, tA.y + tB.y};
        float      c = dt, vs = v.x * s2.x + v.y * s2.y, kk = 2.0f * (1.0f - c), d;
        static int gix_chain_rmax = -1;
        float      cap = net_geo(&gix_chain_rmax, "lane_route_rmax");
        if (kk < 1e-5f)
        {
            if (vs <= 1e-6f)
                return 0;
            d = vv / (2.0f * vs);
        }
        else
            d = (-vs + sqrtf(vs * vs + kk * vv)) / kk;
        if (d <= 1e-4f)
            return 0;
        q[0]    = A;
        q[1]    = (V2){A.x + tA.x * d, A.y + tA.y * d};
        q[2]    = (V2){B.x - tB.x * d, B.y - tB.y * d};
        q[3]    = B;
        rad[0] = rad[3] = 0.0f;
        rad[1] = rad[2] = cap;
        tlim[0] = tlim[3] = 0.0f;
        tlim[1] = tlim[2] = d;
        return 4;
    }
}


void extent_add(float x, float y, float *x0, float *y0, float *x1, float *y1)
{
    if (x < *x0)
        *x0 = x;
    if (x > *x1)
        *x1 = x;
    if (y < *y0)
        *y0 = y;
    if (y > *y1)
        *y1 = y;
}


/*  The extent of a piece: a straight's two ends.  An arc's two ends and
 *  every quadrant point it sweeps through, in the search's own angular
 *  convention.  A hair wider, so a point the search finds on the piece
 *  is never a rounding outside it. */
void piece_extent(const Piece *pc, float *x0, float *y0, float *x1, float *y1)
{
    if (!pc->arc)
    {
        *x0 = *x1 = pc->a.x;
        *y0 = *y1 = pc->a.y;
        extent_add(pc->b.x, pc->b.y, x0, y0, x1, y1);
    }
    else
    {
        float sg = pc->t1 > pc->t0 ? 1.0f : -1.0f, sw = fabsf(pc->t1 - pc->t0);
        int   q;
        *x0 = *x1 = pc->c.x + pc->r * cosf(pc->t0);
        *y0 = *y1 = pc->c.y + pc->r * sinf(pc->t0);
        extent_add(pc->c.x + pc->r * cosf(pc->t1), pc->c.y + pc->r * sinf(pc->t1), x0, y0, x1, y1);
        for (q = 0; q < 4; ++q)
        {
            float th = (float)q * 1.5707963f, rel = (th - pc->t0) * sg;
            while (rel < 0.0f)
                rel += 6.2831853f;
            while (rel >= 6.2831853f)
                rel -= 6.2831853f;
            if (rel <= sw)
                extent_add(pc->c.x + pc->r * cosf(th), pc->c.y + pc->r * sinf(th), x0, y0, x1, y1);
        }
    }
    *x0 -= 0.01f, *y0 -= 0.01f, *x1 += 0.01f, *y1 += 0.01f;
}


/*  The nearest point of one piece to p, and which way the piece runs
 *  there.  A straight is projected onto and clamped to its ends.  An arc
 *  is taken at the angle of p about its center, clamped to its sweep. */
void piece_near(const Piece *pc, V2 p, V2 *q, V2 *dq)
{
    if (!pc->arc)
    {
        float len = pc->len > 1e-6f ? pc->len : 1.0f;
        V2    u   = {(pc->b.x - pc->a.x) / len, (pc->b.y - pc->a.y) / len};
        float t   = (p.x - pc->a.x) * u.x + (p.y - pc->a.y) * u.y;
        if (t < 0.0f)
            t = 0.0f;
        if (t > pc->len)
            t = pc->len;
        *q  = (V2){pc->a.x + u.x * t, pc->a.y + u.y * t};
        *dq = u;
        return;
    }
    {
        float sg = pc->t1 > pc->t0 ? 1.0f : -1.0f, sw = fabsf(pc->t1 - pc->t0);
        float an = atan2f(p.y - pc->c.y, p.x - pc->c.x), rel = (an - pc->t0) * sg, th;
        while (rel < 0.0f)
            rel += 6.2831853f;
        while (rel >= 6.2831853f)
            rel -= 6.2831853f;
        if (rel > sw)
            rel = (rel - sw) < (6.2831853f - rel) ? sw : 0.0f; /* past the arc: the nearer end */
        th  = pc->t0 + rel * sg;
        *q  = (V2){pc->c.x + pc->r * cosf(th), pc->c.y + pc->r * sinf(th)};
        *dq = (V2){-sinf(th) * sg, cosf(th) * sg};
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
