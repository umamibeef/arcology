/*  mesh/check.c: the checks: watertightness, and lines against the
 *  ground.  See mesh/internal.h. */
#include "dump.h"
#include "geo.h"
#include "mesh/internal.h"
#include "mesh/shape.h"
#include "opt.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int mesh_ranges_check(const RMesh *m, int verbose);

/* ---- the check --------------------------------------------------------- */

/*  How close in height two faces lie before the check calls them one
 *  surface.  The geo store holds it as coplanar_z. */
static int gix_coplanar_z = -1, gix_map_floor = -1;

/*  Every edge of every triangle must belong to another triangle too: a
 *  surface with no free edge has no crack.  Edges are keyed on their
 *  quantised end points (heights are means of at most four levels, so
 *  forty-eighths are exact).  A vertical edge is a span on its corner's
 *  vertical.  The spans there may be split differently by the walls
 *  meeting at the corner.  So those are checked as coverage: every part
 *  of the vertical that any edge covers must be covered at least twice.
 *  The base of the map's cut is the floor, at the height the geo store
 *  names (map_floor), and is closed by definition. */
typedef struct
{
    int32_t a[3], b[3];
    int32_t count; /* the triangles that meet it */
    int32_t own;   /* of them, those the build laid: not the world beyond a window */
    float   mat;
} Edge;

static uint32_t edge_hash(const int32_t *a, const int32_t *b)
{
    uint32_t h = 2166136261u;
    int      k;
    for (k = 0; k < 3; ++k)
    {
        h = (h ^ (uint32_t)a[k]) * 16777619u;
        h = (h ^ (uint32_t)b[k]) * 16777619u;
    }
    return h;
}

static void quantise(const float p[4], int32_t q[3])
{
    q[0] = (int32_t)lrintf(p[0] * 16.0f);
    q[1] = (int32_t)lrintf(p[1] * 16.0f);
    q[2] = (int32_t)lrintf(p[2] * 48.0f);
}

/*  Whether a corner, at 16 units a tile, touches a laid chunk.  A corner
 *  on a laid chunk's far edge touches it too. */
static int corner_laid(const RMesh *m, int32_t x, int32_t y)
{
    int32_t xs[2], ys[2];
    int     i, j;
    xs[0] = (int32_t)floorf((float)(x - 1) / 16.0f), xs[1] = (int32_t)floorf((float)x / 16.0f);
    ys[0] = (int32_t)floorf((float)(y - 1) / 16.0f), ys[1] = (int32_t)floorf((float)y / 16.0f);
    for (i = 0; i < 2; ++i)
        for (j = 0; j < 2; ++j)
            if (xs[i] >= 0 && ys[j] >= 0 && xs[i] < r_map && ys[j] < r_map && m->chunk_laid[mesh_chunk_of(xs[i], ys[j])])
                return 1;
    return 0;
}

static int key_less(const int32_t *a, const int32_t *b)
{
    int k;
    for (k = 0; k < 3; ++k)
        if (a[k] != b[k])
            return a[k] < b[k];
    return 0;
}

typedef struct
{
    int32_t x, y, z0, z1;
    float   mat;
} Span;

static int span_cmp(const void *pa, const void *pb)
{
    const Span *a = (const Span *)pa, *b = (const Span *)pb;
    if (a->x != b->x)
        return a->x < b->x ? -1 : 1;
    if (a->y != b->y)
        return a->y < b->y ? -1 : 1;
    if (a->z0 != b->z0)
        return a->z0 < b->z0 ? -1 : 1;
    return 0;
}

/*  A built-in material past the leveled pad is laid over the surface: a
 *  line strip, a prop, a lamp or a zone tint.  It is not the surface, so
 *  the check does not read it.  A material named at run time is a face
 *  of its own, and the check reads it. */
static int over_surface(float mat)
{
    return MAT_IN(mat, MAT_SURFACE + 1.0f, MAT_RUNTIME_BASE - 1.0f);
}

/*  One triangle into the edge table and the vertical spans.  `own` is 1
 *  for a triangle the build laid, and 0 for one of the world beyond its
 *  window.  That one meets an edge and covers a span, but is not judged. */
static void tally(Edge *tab, uint32_t cap, Span *spans, uint32_t *n_spans, const float p0[4], const float p1[4], const float p2[4], float mat, int own)
{
    const float *p[3] = {p0, p1, p2};
    int32_t      q[3][3];
    int          k;
    for (k = 0; k < 3; ++k)
        quantise(p[k], q[k]);
    for (k = 0; k < 3; ++k)
    {
        const int32_t *a = q[k], *b = q[(k + 1) % 3];
        uint32_t       h;
        if (a[0] == b[0] && a[1] == b[1])
        {
            Span *s;
            if (a[2] == b[2])
                continue; /* a point, not an edge */
            s      = &spans[(*n_spans)++];
            s->x   = a[0];
            s->y   = a[1];
            s->z0  = a[2] < b[2] ? a[2] : b[2];
            s->z1  = a[2] < b[2] ? b[2] : a[2];
            s->mat = mat;
            continue;
        }
        if (key_less(b, a))
        {
            const int32_t *t = a;
            a                = b;
            b                = t;
        }
        h = edge_hash(a, b) & (cap - 1u);
        for (;;)
        {
            Edge *e = &tab[h];
            if (e->count == 0)
            {
                memcpy(e->a, a, sizeof e->a);
                memcpy(e->b, b, sizeof e->b);
                e->count = 1;
                e->own   = own;
                e->mat   = mat;
                break;
            }
            if (memcmp(e->a, a, sizeof e->a) == 0 && memcmp(e->b, b, sizeof e->b) == 0)
            {
                e->count++;
                e->own += own;
                break;
            }
            h = (h + 1u) & (cap - 1u);
        }
    }
}

/*  WHICH MATERIAL a number is: the nearest of the built-in ones within
 *  half a step, or MAT_COUNT for one named at run time.  A variant lies
 *  beside its whole neighbor, so the nearest is the one that names it. */
static int mat_slot(float m)
{
    int   i, best = MAT_COUNT;
    float near = 0.5f;
    for (i = 0; i < r_materials_n; ++i)
    {
        float d = m - r_materials[i].value;
        if (d < 0.0f)
            d = -d;
        if (d < near)
            near = d, best = i;
    }
    return best;
}

/*  A material's name, as materials.def gives it.  The report reads the
 *  generated table.  A second copy of the list here would say one thing
 *  where the mesh and the shaders say another. */
static const char *mat_name(float m)
{
    int k = mat_slot(m);
    return k < MAT_COUNT ? r_materials[k].name : "a run-time one";
}

/*  Coplanar overlap: two faces covering the same ground at the same
 *  height.  Nothing in the renderer can separate them.  Both are at one
 *  depth.  Which of the two a pixel shows is settled by the painter's
 *  slot, or by nothing at all when the slots agree.  So the pair reads
 *  as a flicker, a blotch, or one surface simply missing.  Every such
 *  pair is a defect, and the count is meant to be zero.
 *
 *  Two triangles of one shape are held to it too.  A shape's own faces
 *  tile a surface and meet along their edges, which the area test
 *  ignores.  So a pair of them that shares area is a fold in the shape. */
typedef struct
{
    float x, y;
} OvPt;

#define OV_MAX 8 /* a triangle clipped by three half planes keeps six */

static float ov_clip(const OvPt *in, int n, OvPt a, OvPt b, OvPt *out)
{
    /*  Keep what lies to the left of a->b, the half plane the third
     *  corner is on.  The caller orders the triangle so that is the
     *  inside. */
    float ex = b.x - a.x, ey = b.y - a.y;
    int   m = 0, i;
    for (i = 0; i < n; ++i)
    {
        const OvPt *p = &in[i], *q = &in[(i + 1) % n];
        float       dp = (p->x - a.x) * ey - (p->y - a.y) * ex;
        float       dq = (q->x - a.x) * ey - (q->y - a.y) * ex;
        /*  Each edge may keep its own start and the point where it
         *  crosses the line.  So a pass can add two: the room is checked
         *  before each rather than after the pair. */
        if (dp <= 0.0f && m < OV_MAX)
            out[m++] = *p;
        if ((dp < 0.0f) != (dq < 0.0f) && fabsf(dq - dp) > 1e-12f && m < OV_MAX)
        {
            float t  = dp / (dp - dq);
            out[m].x = p->x + (q->x - p->x) * t;
            out[m].y = p->y + (q->y - p->y) * t;
            ++m;
        }
        if (m >= OV_MAX)
            break;
    }
    return (float)m;
}

/*  The area the two triangles share, in tiles squared, and where. */
static float ov_area(const float a[3][3], const float b[3][3], OvPt *at)
{
    /*  Zeroed: the clip walks the ring it was handed and writes as many
     *  points as it keeps.  So a slot past that count stands for the
     *  origin rather than for whatever the stack held. */
    OvPt  poly[2][OV_MAX] = {{{0.0f, 0.0f}}}, tri[3];
    int   n = 3, k, src = 0;
    float ar = 0.0f;
    for (k = 0; k < 3; ++k)
    {
        poly[0][k] = (OvPt){a[k][0], a[k][1]};
        tri[k]     = (OvPt){b[k][0], b[k][1]};
    }
    /*  The clipper must be wound so its inside is to the LEFT of every
     *  edge, which is the side ov_clip keeps.  Wound the other way it
     *  keeps the OUTSIDE, every area comes back near zero, and the check
     *  reports no overlap however much there is. */
    if ((tri[1].x - tri[0].x) * (tri[2].y - tri[0].y) - (tri[1].y - tri[0].y) * (tri[2].x - tri[0].x) < 0.0f)
    {
        OvPt t = tri[1];
        tri[1] = tri[2];
        tri[2] = t;
    }
    for (k = 0; k < 3 && n > 2; ++k)
    {
        n   = (int)ov_clip(poly[src], n, tri[k], tri[(k + 1) % 3], poly[!src]);
        src = !src;
    }
    if (n < 3)
        return 0.0f;
    at->x = at->y = 0.0f;
    for (k = 0; k < n; ++k)
    {
        const OvPt *p = &poly[src][k], *q = &poly[src][(k + 1) % n];
        ar += p->x * q->y - q->x * p->y;
        at->x += p->x;
        at->y += p->y;
    }
    at->x /= (float)n;
    at->y /= (float)n;
    return fabsf(ar) * 0.5f;
}

/*  Whether a triangle has no area seen from above. */
static int ov_flat(const float t[3][3])
{
    return fabsf((t[1][0] - t[0][0]) * (t[2][1] - t[0][1]) - (t[1][1] - t[0][1]) * (t[2][0] - t[0][0])) < 1e-7f;
}

/*  Whether a triangle faces down: wound clockwise seen from above. */
static int ov_down(const float t[3][3])
{
    return (t[1][0] - t[0][0]) * (t[2][1] - t[0][1]) - (t[1][1] - t[0][1]) * (t[2][0] - t[0][0]) < -1e-9f;
}

/*  The height of a triangle's plane at a point. */
static float ov_z(const float t[3][3], OvPt p)
{
    float d = (t[1][1] - t[2][1]) * (t[0][0] - t[2][0]) + (t[2][0] - t[1][0]) * (t[0][1] - t[2][1]);
    float l0, l1, l2;
    if (fabsf(d) < 1e-9f)
        return t[0][2];
    l0 = ((t[1][1] - t[2][1]) * (p.x - t[2][0]) + (t[2][0] - t[1][0]) * (p.y - t[2][1])) / d;
    l1 = ((t[2][1] - t[0][1]) * (p.x - t[2][0]) + (t[0][0] - t[2][0]) * (p.y - t[2][1])) / d;
    l2 = 1.0f - l0 - l1;
    return l0 * t[0][2] + l1 * t[1][2] + l2 * t[2][2];
}

/*  COLLIDING GEOMETRY: two faces of different shapes that pass THROUGH
 *  one another.  The coplanar test above is two surfaces laid on the
 *  same ground.  This is one surface driven through another: a wire
 *  through the ground, a box through a wall.
 *
 *  Nothing in the renderer can resolve it: the two are interleaved in
 *  space.  So every camera shows part of each and no painter's order can
 *  separate them.
 *
 *  A pair collides when an EDGE of one crosses the other's plane
 *  strictly between its own ends.  The meet point must also fall
 *  strictly inside the other triangle.  Touching along an edge, meeting
 *  at a corner and lying in one plane all fail that test.  This is what
 *  keeps two faces laid end to end, or one resting on another, from
 *  reading as a fault. */
#define COLL_EPS 0.002f /* tiles: a hair of contact is contact, not a meet */
#define COLL_IN  0.001f /* inside the triangle, not on its rim */

typedef struct
{
    float x0, y0, z0, x1, y1, z1;
} CollBox;

/*  Where an edge crosses a triangle's plane strictly inside it.  It
 *  answers 1 with the point.  It answers 0 for a miss, an edge that only
 *  touches, or a plane the edge lies in. */
static int coll_through(const float t[3][3], const float a[3], const float b[3])
{
    float ux = t[1][0] - t[0][0], uy = t[1][1] - t[0][1], uz = t[1][2] - t[0][2];
    float vx = t[2][0] - t[0][0], vy = t[2][1] - t[0][1], vz = t[2][2] - t[0][2];
    float nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
    float nl = sqrtf(nx * nx + ny * ny + nz * nz);
    float da, db, s, px, py, pz, w0, w1, w2, uu, uv, vv, wu, wv, den;
    if (nl < 1e-9f)
        return 0; /* a degenerate face has no inside to cross */
    nx /= nl, ny /= nl, nz /= nl;
    da = (a[0] - t[0][0]) * nx + (a[1] - t[0][1]) * ny + (a[2] - t[0][2]) * nz;
    db = (b[0] - t[0][0]) * nx + (b[1] - t[0][1]) * ny + (b[2] - t[0][2]) * nz;
    if (!(da > COLL_EPS && db < -COLL_EPS) && !(da < -COLL_EPS && db > COLL_EPS))
        return 0;
    s  = da / (da - db);
    px = a[0] + (b[0] - a[0]) * s;
    py = a[1] + (b[1] - a[1]) * s;
    pz = a[2] + (b[2] - a[2]) * s;
    /*  Inside, in the triangle's own plane: the barycentric weights of
     *  the meet point, each clear of the rim. */
    w0 = px - t[0][0], w1 = py - t[0][1], w2 = pz - t[0][2];
    uu  = ux * ux + uy * uy + uz * uz;
    uv  = ux * vx + uy * vy + uz * vz;
    vv  = vx * vx + vy * vy + vz * vz;
    wu  = w0 * ux + w1 * uy + w2 * uz;
    wv  = w0 * vx + w1 * vy + w2 * vz;
    den = uv * uv - uu * vv;
    if (fabsf(den) < 1e-12f)
        return 0;
    {
        float sc = (uv * wv - vv * wu) / den;
        float tc = (uv * wu - uu * wv) / den;
        return sc > COLL_IN && tc > COLL_IN && sc + tc < 1.0f - COLL_IN;
    }
}

static int coll_pair(const float a[3][3], const float b[3][3])
{
    int k;
    for (k = 0; k < 3; ++k)
        if (coll_through(a, b[k], b[(k + 1) % 3]) || coll_through(b, a[k], a[(k + 1) % 3]))
            return 1;
    return 0;
}

/*  The triangles under one tile, for the two checks and the probe that
 *  walk the tiles in turn.  One buffer and one size: none of the three
 *  calls another. */
enum
{
    CHECK_MAXT = 4096
};
static uint32_t s_tri[CHECK_MAXT];

/*  The whole mesh swept for them, tile by tile, since two faces that
 *  cross must share a tile.  Answers how many pairs collide.  With
 *  `verbose` it prints where. */
int mesh_check_collide(const RMesh *m, int verbose)
{
    uint32_t       *tri = s_tri;
    static CollBox  box[CHECK_MAXT];
    static float    pos[CHECK_MAXT][3][3];
    static uint32_t comp[CHECK_MAXT];
    int32_t         col, row;
    int             hits = 0, tiles = 0, shown = 0;
    if (!m || !m->ranged)
        return 0;
    for (row = 0; row < r_map; ++row)
        for (col = 0; col < r_map; ++col)
        {
            int n = mesh_tris_at(m, col, row, tri, CHECK_MAXT), i, j, here = 0;
            for (i = 0; i < n; ++i)
            {
                int k;
                mesh_tri_get(m, tri[i], pos[i], &comp[i]);
                box[i].x0 = box[i].x1 = pos[i][0][0];
                box[i].y0 = box[i].y1 = pos[i][0][1];
                box[i].z0 = box[i].z1 = pos[i][0][2];
                for (k = 1; k < 3; ++k)
                {
                    if (pos[i][k][0] < box[i].x0)
                        box[i].x0 = pos[i][k][0];
                    if (pos[i][k][0] > box[i].x1)
                        box[i].x1 = pos[i][k][0];
                    if (pos[i][k][1] < box[i].y0)
                        box[i].y0 = pos[i][k][1];
                    if (pos[i][k][1] > box[i].y1)
                        box[i].y1 = pos[i][k][1];
                    if (pos[i][k][2] < box[i].z0)
                        box[i].z0 = pos[i][k][2];
                    if (pos[i][k][2] > box[i].z1)
                        box[i].z1 = pos[i][k][2];
                }
            }
            for (i = 0; i < n; ++i)
                for (j = i + 1; j < n; ++j)
                {
                    if (comp[i] == comp[j])
                        continue; /* a shape's own faces meet: its producer's business */
                    if (box[i].x1 < box[j].x0 - COLL_EPS || box[j].x1 < box[i].x0 - COLL_EPS ||
                        box[i].y1 < box[j].y0 - COLL_EPS || box[j].y1 < box[i].y0 - COLL_EPS ||
                        box[i].z1 < box[j].z0 - COLL_EPS || box[j].z1 < box[i].z0 - COLL_EPS)
                        continue;
                    if (!coll_pair(pos[i], pos[j]))
                        continue;
                    ++hits;
                    here = 1;
                    if (verbose && shown < 4000)
                    {
                        ++shown;
                        dumpf("collide   %d,%d: %s through %s, at %.3f,%.3f,%.3f\n", (int)col, (int)row, shape_name(comp[i]) ? shape_name(comp[i]) : "?", shape_name(comp[j]) ? shape_name(comp[j]) : "?", (double)pos[i][0][0], (double)pos[i][0][1], (double)pos[i][0][2]);
                    }
                }
            tiles += here;
        }
    dumpf("collide  %d face pairs pass through one another on %d tiles\n", hits, tiles);
    return hits;
}

int mesh_check_overlap(const RMesh *m, int verbose)
{
    uint32_t *tri = s_tri;
    int32_t   col, row;
    int       pairs = 0, fights = 0, tiles = 0;
    int       kind_a[32], kind_b[32], kind_n[32], nkinds = 0;
    if (!m || !m->ranged)
        return 0;
    for (row = 0; row < r_map; ++row)
        for (col = 0; col < r_map; ++col)
        {
            int n = mesh_tris_at(m, col, row, tri, CHECK_MAXT), i, j, here = 0;
            for (i = 0; i < n; ++i)
                for (j = i + 1; j < n; ++j)
                {
                    float    pa[3][3], pb[3][3];
                    uint32_t sa, sb;
                    OvPt     at = {0.0f, 0.0f}; /* nowhere, until an overlap has somewhere */
                    float    ar, za, zb;
                    mesh_tri_get(m, tri[i], pa, &sa);
                    mesh_tri_get(m, tri[j], pb, &sb);
                    /*  A face turned down is the bottom of something, seen
                     *  only from under the ground.  The camera is always
                     *  over it, so no pixel shows the pair. */
                    if (ov_down(pa) || ov_down(pb))
                        continue;
                    /*  A face with no area seen from above stands on its
                     *  edge, a wall.  It covers no ground, and it has no
                     *  turn to clip the other by. */
                    if (ov_flat(pa) || ov_flat(pb))
                        continue;
                    ar = ov_area(pa, pb, &at);
                    if (ar < 1e-4f)
                        continue;
                    za = ov_z(pa, at);
                    zb = ov_z(pb, at);
                    if (fabsf(za - zb) > geo_num(&gix_coplanar_z, "coplanar_z"))
                        continue; /* one stands clear of the other */
                    /*  Two faces at one height are ordinary.  A mark
                     *  lies ON the ground, and the painter's slot each
                     *  vertex carries settles which is in front.  A pair is a FIGHT only when the slots
                     *  agree too, for then nothing decides and the pixel
                     *  goes to whichever was drawn last. */
                    {
                        float oa = mesh_tri_order(m, tri[i]), ob = mesh_tri_order(m, tri[j]);
                        ++pairs;
                        if (fabsf(oa - ob) > 1e-4f)
                            continue;
                        /*  A shape and a shape drawn INSIDE it are one
                         *  surface, as two faces of one shape are.  A
                         *  meet is the way it is painted on.  The
                         *  hairline its quads share with the fill either
                         *  side of it is a seam, not a fight over the
                         *  same pixel. */
                        if (sa != sb && (shape_is_ancestor(sa, sb) || shape_is_ancestor(sb, sa)))
                            continue;
                        ++fights;
                        {
                            /*  Which KINDS collide, so the worst can be
                             *  given an order rather than chased one tile
                             *  at a time. */
                            float ma = mesh_tri_mat(m, tri[i]), mb = mesh_tri_mat(m, tri[j]);
                            int   q, lo = (int)(ma < mb ? ma : mb), hi = (int)(ma < mb ? mb : ma);
                            for (q = 0; q < nkinds; ++q)
                                if (kind_a[q] == lo && kind_b[q] == hi)
                                    break;
                            if (q == nkinds && nkinds < 32)
                                kind_a[nkinds] = lo, kind_b[nkinds] = hi, kind_n[nkinds] = 0, ++nkinds;
                            if (q < 32)
                                ++kind_n[q];
                        }
                        if (!here++ && verbose && tiles < 12)
                            dumpf("overlap   %d,%d: %s and %s share %.4f tiles at height %.3f and slot %.3f\n", (int)col, (int)row, shape_name(sa) ? shape_name(sa) : "?", shape_name(sb) ? shape_name(sb) : "?", (double)ar, (double)za, (double)oa);
                    }
                }
            if (here)
                ++tiles;
        }
    dumpf("overlap  %d coplanar face pairs, %d with the same painter's slot on %d tiles\n", pairs, fights, tiles);
    {
        int q, r;
        for (r = 0; r < nkinds && r < 10; ++r)
        {
            int best = -1, bn = 0;
            for (q = 0; q < nkinds; ++q)
                if (kind_n[q] > bn)
                    bn = kind_n[q], best = q;
            if (best < 0)
                break;
            dumpf("overlap    material %2d with material %2d: %6d pairs\n", kind_a[best], kind_b[best], bn);
            kind_n[best] = 0;
        }
    }
    return fights;
}

/*  The Z probe: every surface over one point of the map, in the order
 *  the renderer sees them.  For each face covering the point it gives
 *  the height there, the material, the painter's slot the vertex
 *  carries, and the shape it belongs to.  Two faces at one height are
 *  what a flicker or a wrongly hidden surface is made of.  This is the
 *  only way to see which of them is in front and by how much. */
void mesh_probe(const RMesh *m, float px, float py)
{
    enum
    {
        MAXH = 64
    };
    uint32_t *tri = s_tri;
    typedef struct
    {
        float    z, mat, order;
        uint32_t shape, t;
    } Hit;
    Hit     hit[MAXH];
    int     nh = 0, i, j;
    int32_t bc = (int32_t)floorf(px), br = (int32_t)floorf(py), dc, dr;
    if (!m || !m->ranged)
        return;
    for (dr = -1; dr <= 1; ++dr)
        for (dc = -1; dc <= 1; ++dc)
        {
            int n = mesh_tris_at(m, bc + dc, br + dr, tri, CHECK_MAXT), k;
            for (k = 0; k < n && nh < MAXH; ++k)
            {
                float    p[3][3], d1, d2, d3, area, l0, l1, l2;
                uint32_t sh;
                mesh_tri_get(m, tri[k], p, &sh);
                d1 = (px - p[1][0]) * (p[0][1] - p[1][1]) - (p[0][0] - p[1][0]) * (py - p[1][1]);
                d2 = (px - p[2][0]) * (p[1][1] - p[2][1]) - (p[1][0] - p[2][0]) * (py - p[2][1]);
                d3 = (px - p[0][0]) * (p[2][1] - p[0][1]) - (p[2][0] - p[0][0]) * (py - p[0][1]);
                if (((d1 < 0.0f) || (d2 < 0.0f) || (d3 < 0.0f)) && ((d1 > 0.0f) || (d2 > 0.0f) || (d3 > 0.0f)))
                    continue;
                area = (p[1][0] - p[0][0]) * (p[2][1] - p[0][1]) - (p[2][0] - p[0][0]) * (p[1][1] - p[0][1]);
                if (fabsf(area) < 1e-9f)
                    continue; /* no area here: it covers no point */
                l0            = ((p[1][1] - p[2][1]) * (px - p[2][0]) + (p[2][0] - p[1][0]) * (py - p[2][1])) /
                                ((p[1][1] - p[2][1]) * (p[0][0] - p[2][0]) + (p[2][0] - p[1][0]) * (p[0][1] - p[2][1]));
                l1            = ((p[2][1] - p[0][1]) * (px - p[2][0]) + (p[0][0] - p[2][0]) * (py - p[2][1])) /
                                ((p[1][1] - p[2][1]) * (p[0][0] - p[2][0]) + (p[2][0] - p[1][0]) * (p[0][1] - p[2][1]));
                l2            = 1.0f - l0 - l1;
                hit[nh].z     = l0 * p[0][2] + l1 * p[1][2] + l2 * p[2][2];
                hit[nh].mat   = mesh_tri_mat(m, tri[k]);
                hit[nh].order = mesh_tri_order(m, tri[k]);
                hit[nh].shape = sh;
                hit[nh].t     = tri[k];
                ++nh;
            }
        }
    /* the highest first, and among equals the nearer painter's slot */
    for (i = 1; i < nh; ++i)
        for (j = i; j > 0; --j)
        {
            int swap = hit[j].z > hit[j - 1].z + 1e-5f || (fabsf(hit[j].z - hit[j - 1].z) <= 1e-5f && hit[j].order > hit[j - 1].order);
            if (!swap)
                break;
            {
                Hit t      = hit[j];
                hit[j]     = hit[j - 1];
                hit[j - 1] = t;
            }
        }
    dumpf("probe   %.4f,%.4f: %d faces\n", (double)px, (double)py, nh);
    for (i = 0; i < nh; ++i)
    {
        const char *nm = shape_name(hit[i].shape);
        const char *pn = shape_name(shape_parent(hit[i].shape));
        dumpf("probe     z %.5f  slot %9.3f  material %5g  %s%s%s\n", (double)hit[i].z, (double)hit[i].order, (double)hit[i].mat, nm ? nm : "?", pn ? ", in " : "", pn ? pn : "");
        if (i && fabsf(hit[i].z - hit[i - 1].z) < 0.0005f)
            dumpf("probe       ^ the same height as the face above it, to %.5f\n", (double)fabsf(hit[i].z - hit[i - 1].z));
    }
}

/*  THE MESH AS TEXT: every face, in draw order.  Each line holds its
 *  tile, the shape it is drawn as, its painter's slot and its three
 *  corners byte for byte.  `which` is "all", or "C,R" for the faces whose middle lies on
 *  one tile.  Two builds that lay the same faces write the same text. */
void mesh_tile_dump(const RMesh *m, const char *which)
{
    uint32_t t, n = (m->n_land + m->n_water) / 3u;
    int      all = strcmp(which, "all") == 0, wc = -1, wr = -1;
    if (!all && sscanf(which, "%d,%d", &wc, &wr) != 2)
        return;
    for (t = 0; t < n; ++t)
    {
        RMeshVert   v[3];
        const char *name;
        float       cx, cy;
        int         k, b;
        if (mesh_tri_copy(m, t, v) != 0)
            continue;
        cx = (v[0].pos[0] + v[1].pos[0] + v[2].pos[0]) / 3.0f;
        cy = (v[0].pos[1] + v[1].pos[1] + v[2].pos[1]) / 3.0f;
        if (!all && ((int)floorf(cx) != wc || (int)floorf(cy) != wr))
            continue;
        name = shape_name(mesh_tri_comp(m, t));
        dumpf("face %d,%d %s %s slot %a ", (int)floorf(cx), (int)floorf(cy), 3u * t < m->n_land ? "land" : "water", name ? name : "-", (double)mesh_tri_order(m, t));
        for (k = 0; k < 3; ++k)
        {
            const unsigned char *bytes = (const unsigned char *)&v[k];
            for (b = 0; b < (int)sizeof v[k]; ++b)
                dumpf("%02x", bytes[b]);
            dumpf(k < 2 ? " " : "\n");
        }
    }
}

int mesh_check(const RMesh *m, int verbose)
{
    if (g_dev.spike_check)
    {
        /* ground top faces spanning over a level, or wider than a tile: a deformed or mis-welded face */
        uint32_t i, n = m->n_land / 3u, bad = 0;
        for (i = 0; i < n; ++i)
        {
            RMeshVert v[3];
            mesh_tri_copy(m, i, v);
            float z0 = v[0].pos[2], z1 = z0, x0 = v[0].pos[0], x1 = x0, y0 = v[0].pos[1], y1 = y0;
            int   k;
            if (MAT_ABOVE(mesh_vert_mat(&v[0]), MAT_GROUND))
                continue;
            for (k = 1; k < 3; ++k)
            {
                z0 = fminf(z0, v[k].pos[2]);
                z1 = fmaxf(z1, v[k].pos[2]);
                x0 = fminf(x0, v[k].pos[0]);
                x1 = fmaxf(x1, v[k].pos[0]);
                y0 = fminf(y0, v[k].pos[1]);
                y1 = fmaxf(y1, v[k].pos[1]);
            }
            if (z1 - z0 > 1.05f || x1 - x0 > 1.01f || y1 - y0 > 1.01f)
            {
                ++bad;
                if (bad <= 5)
                    dumpf("spike: (%.3f,%.3f,%.3f) (%.3f,%.3f,%.3f) (%.3f,%.3f,%.3f)\n", (double)v[0].pos[0], (double)v[0].pos[1], (double)v[0].pos[2], (double)v[1].pos[0], (double)v[1].pos[1], (double)v[1].pos[2], (double)v[2].pos[0], (double)v[2].pos[1], (double)v[2].pos[2]);
            }
        }
        dumpf("spikes: %u\n", bad);
    }
    uint32_t n_tri = (m->n_land + m->n_water) / 3u, cap = 1u, i, free_edges = 0, free_spans = 0, n_beyond = 0;
    int      ch, windowed = 0;
    Edge    *tab;
    Span    *spans;
    uint32_t n_spans = 0;
    uint32_t shown   = 0;
    uint32_t by_mat[MAT_COUNT + 1];
    memset(by_mat, 0, sizeof by_mat);
    /*  A mesh with a chunk not laid is a window's, and the world beyond
     *  it is the triangles its laid chunks dropped. */
    for (ch = 0; ch < MESH_CHUNKS; ++ch)
    {
        uint32_t n;
        if (!m->chunk_laid[ch])
            windowed = 1;
        else if (mesh_beyond(ch, &n))
            n_beyond += n;
    }
    while (cap < (n_tri + n_beyond) * 6u)
        cap *= 2u;
    tab   = (Edge *)calloc(cap, sizeof *tab);
    spans = (Span *)malloc((size_t)(n_tri + n_beyond) * 3u * sizeof *spans + sizeof *spans);
    if (!tab || !spans)
    {
        free(tab);
        free(spans);
        return -1;
    }
    for (i = 0; i < n_tri; ++i)
    {
        RMeshVert v[3];
        mesh_tri_copy(m, i, v);
        if (over_surface(mesh_vert_mat(&v[0])))
            continue;
        tally(tab, cap, spans, &n_spans, v[0].pos, v[1].pos, v[2].pos, mesh_vert_mat(&v[0]), 1);
    }
    /*  And the world beyond a window's border, which meets what the window
     *  laid there and is judged nowhere else.  A triangle whose own chunk
     *  is laid is not beyond: that chunk's own triangles stand there. */
    if (windowed)
        for (ch = 0; ch < MESH_CHUNKS; ++ch)
        {
            uint32_t     n;
            const float *beyond = m->chunk_laid[ch] ? mesh_beyond(ch, &n) : NULL;
            for (i = 0; beyond && i < n; ++i)
            {
                const float *b = &beyond[(size_t)i * 13u];
                if (!over_surface(b[12]) && !m->chunk_laid[mesh_chunk_of((int32_t)floorf(b[0]), (int32_t)floorf(b[1]))])
                    tally(tab, cap, spans, &n_spans, &b[0], &b[4], &b[8], b[12], 0);
            }
        }
    const int32_t floor_q = (int32_t)lrintf(geo_num(&gix_map_floor, "map_floor") * 48.0f);
    for (i = 0; i < cap; ++i)
    {
        const Edge *e = &tab[i];
        if (e->count != 1 || e->own != 1)
            continue; /* met, or the world beyond a window's own edge */
        if (e->a[2] == floor_q && e->b[2] == floor_q)
            continue; /* the floor */
        free_edges++;
        by_mat[mat_slot(e->mat)]++;
        if (verbose && shown++ < 40)
            dumpf("free edge  (%g,%g,%g)-(%g,%g,%g)  tile c%d r%d  %s\n",
                  e->a[0] / 16.0,
                  e->a[1] / 16.0,
                  e->a[2] / 48.0,
                  e->b[0] / 16.0,
                  e->b[1] / 16.0,
                  e->b[2] / 48.0,
                  (int)((e->a[0] < e->b[0] ? e->a[0] : e->b[0]) / 16),
                  (int)((e->a[1] < e->b[1] ? e->a[1] : e->b[1]) / 16),
                  mat_name(e->mat));
    }
    /*  The verticals: per corner, the coverage of every elementary span. */
    if (n_spans)
    {
        uint32_t s0 = 0;
        qsort(spans, n_spans, sizeof *spans, span_cmp);
        while (s0 < n_spans)
        {
            uint32_t s1 = s0, j;
            int32_t  zs[512];
            uint32_t nz = 0, zi;
            while (s1 < n_spans && spans[s1].x == spans[s0].x && spans[s1].y == spans[s0].y)
                ++s1;
            /*  A corner of the world beyond a window is not judged: only
             *  the window's own, its border's included. */
            if (windowed && !corner_laid(m, spans[s0].x, spans[s0].y))
            {
                s0 = s1;
                continue;
            }
            for (j = s0; j < s1 && nz < 510; ++j)
            {
                zs[nz++] = spans[j].z0;
                zs[nz++] = spans[j].z1;
            }
            /* sort the breakpoints, small n */
            for (zi = 1; zi < nz; ++zi)
            {
                int32_t  t = zs[zi];
                uint32_t p = zi;
                while (p > 0 && zs[p - 1] > t)
                {
                    zs[p] = zs[p - 1];
                    --p;
                }
                zs[p] = t;
            }
            for (zi = 0; zi + 1 < nz; ++zi)
            {
                int32_t lo = zs[zi], hi = zs[zi + 1], cover = 0;
                float   mat = 0.0f;
                if (lo == hi)
                    continue;
                for (j = s0; j < s1; ++j)
                    if (spans[j].z0 <= lo && spans[j].z1 >= hi)
                    {
                        cover++;
                        mat = spans[j].mat;
                    }
                if (cover == 1)
                {
                    free_spans++;
                    by_mat[mat_slot(mat)]++;
                    if (verbose && shown++ < 40)
                        dumpf("free span  corner (%g,%g) z %g..%g  %s\n",
                              spans[s0].x / 16.0,
                              spans[s0].y / 16.0,
                              lo / 48.0,
                              hi / 48.0,
                              mat_name(mat));
                }
            }
            s0 = s1;
        }
    }
    if (verbose)
    {
        int k;
        dumpf("mesh check: %u triangles, %u free edges, %u free vertical spans\n",
              (unsigned)n_tri,
              (unsigned)free_edges,
              (unsigned)free_spans);
        for (k = 0; k <= MAT_COUNT; ++k)
            if (by_mat[k])
                dumpf("  %-9s %u\n", k < MAT_COUNT ? r_materials[k].name : "a run-time one", (unsigned)by_mat[k]);
    }
    free(tab);
    free(spans);
    return (int)(free_edges + free_spans) + mesh_ranges_check(m, verbose);
}

void mesh_free(RMesh *m)
{
    mesh_lists_free(m->rl, m->n_rl);
    mesh_lists_free(m->wl, m->n_wl);
    mesh_lists_free(m->rl_old, m->n_rl ? m->n_rl : MESH_RANGES * MESH_CHUNKS);
    mesh_lists_free(m->wl_old, m->n_wl ? m->n_wl : MESH_CHUNKS);
    memset(m, 0, sizeof *m);
}

/*  What the mesh holds and what it costs.  That is its triangles by
 *  kind, the bytes of every list it keeps, and how many of its vertices
 *  differ.  An index buffer cannot share a vertex that differs from
 *  every other.  It could share one whose place alone differs, were the
 *  rest computed rather than stored. */
static uint64_t vert_hash(const float *f, int n)
{
    uint64_t h = 1469598103934665603ull;
    int      i;
    for (i = 0; i < n; ++i)
    {
        uint32_t u;
        memcpy(&u, &f[i], 4);
        h = (h ^ u) * 1099511628211ull;
    }
    return h ? h : 1;
}

static uint32_t distinct(const RMeshVert *v, uint32_t n, int floats)
{
    uint32_t  cap = 1, i, k = 0;
    uint64_t *set;
    while (cap < 2 * n + 2)
        cap <<= 1;
    set = (uint64_t *)calloc(cap, sizeof *set);
    if (!set)
        return 0;
    for (i = 0; i < n; ++i)
    {
        uint64_t h = vert_hash(&v[i].pos[0], floats);
        uint32_t s = (uint32_t)h & (cap - 1);
        while (set[s] && set[s] != h)
            s = (s + 1) & (cap - 1);
        if (!set[s])
        {
            set[s] = h;
            ++k;
        }
    }
    free(set);
    return k;
}

static void mesh_size_report(const RMesh *m)
{
    uint32_t ground = 0, lines = 0, trees = 0, room = 0, wroom = 0, tags = 0, all = 0, whole = 0, place = 0, corners = 0;
    double   mb = 1024.0 * 1024.0;
    int      k;
    for (k = 0; k < m->n_rl; ++k)
    {
        if (k % MESH_RANGES == MESH_RANGE_GROUND)
            ground += m->rl[k].n / 3u;
        else if (k % MESH_RANGES == MESH_RANGE_STANDS)
            lines += m->rl[k].n / 3u;
        else
            trees += m->rl[k].n / 3u;
        room += m->rl[k].cap;
        corners += m->rl[k].idx ? m->rl[k].n : 0u;
        tags += 3u * m->rl[k].tcap;
        all += m->rl[k].n;
        whole += m->rl[k].idx ? m->rl[k].nv : m->rl[k].n;
        place += distinct(m->rl[k].v, m->rl[k].idx ? m->rl[k].nv : m->rl[k].n, 3);
    }
    for (k = 0; k < m->n_wl; ++k)
    {
        wroom += m->wl[k].cap;
        corners += m->wl[k].idx ? m->wl[k].n : 0u;
        tags += 3u * m->wl[k].tcap;
    }
    dumpf("mesh    %u ground, %u line, %u tree, %u water triangles\n", ground, lines, trees, m->n_water / 3u);
    {
        int big = 0, i;
        for (i = 1; i < m->n_rl; ++i)
            if (m->rl[i].n > m->rl[big].n)
                big = i;
        if (m->n_rl)
        {
            static const char *const what[MESH_RANGES] = {"terrain", "networks", "trees", "crowns"};
            dumpf("mesh    the largest range is %d, chunk %d's %s, %u triangles\n", big, big / MESH_RANGES, what[big % MESH_RANGES], m->rl[big].n / 3u);
        }
    }
    dumpf("mesh    land %.1f MB, water %.1f MB, corners %.1f MB, tags %.1f MB, at %u bytes a vertex\n", (double)room * sizeof(RMeshVert) / mb, (double)wroom * sizeof(RMeshVert) / mb, 4.0 * (double)corners / mb, 4.0 * (double)tags / mb, (unsigned)sizeof(RMeshVert));
    dumpf("mesh    %u land corners: %u distinct vertices, %u distinct places, within their chunk ranges\n", all, whole, place);
    {
        uint32_t lv[MESH_LOD_LEVELS];
        int      L;
        memset(lv, 0, sizeof lv);
        for (k = 0; k < m->n_rl; ++k)
            for (L = 0; L < MESH_LOD_LEVELS; ++L)
                lv[L] += (L == 0 || !m->rl[k].lod[L] ? m->rl[k].n : m->rl[k].lod_n[L]) / 3u;
        for (L = 0; L < MESH_LOD_LEVELS; ++L)
            dumpf("mesh    level of detail %d, a pixel hiding %.4f tiles: %u land triangles\n", L, L ? (double)(MESH_LOD_ERR0 * (float)(1u << (L - 1))) : 0.0, lv[L]);
    }
    /*  The line triangles by the shape that laid them, the most first. */
    {
        enum
        {
            NAMES = 48
        };
        static char name[NAMES][48];
        uint32_t    cnt[NAMES], t, other = 0;
        int         nn = 0, i, j;
        memset(cnt, 0, sizeof cnt);
        for (k = MESH_RANGE_STANDS; k < m->n_rl; k += MESH_RANGES)
            for (t = 0; t < m->rl[k].n / 3u; ++t)
            {
                const char *s = shape_name(m->rl[k].comp[t]);
                char        nm[48];
                size_t      n = 0;
                if (!s)
                    s = "(none)";
                while (s[n] && n < sizeof nm - 1 && !(s[n] >= '0' && s[n] <= '9') && s[n] != ' ')
                {
                    nm[n] = s[n];
                    ++n;
                }
                nm[n] = 0;
                for (i = 0; i < nn && strcmp(name[i], nm); ++i)
                    ;
                if (i == nn)
                {
                    if (nn == NAMES)
                    {
                        ++other;
                        continue;
                    }
                    memcpy(name[nn++], nm, n + 1);
                }
                ++cnt[i];
            }
        for (i = 0; i < nn; ++i)
            for (j = i + 1; j < nn; ++j)
                if (cnt[j] > cnt[i])
                {
                    uint32_t c = cnt[i];
                    char     s[48];
                    cnt[i] = cnt[j], cnt[j] = c;
                    memcpy(s, name[i], 48), memcpy(name[i], name[j], 48), memcpy(name[j], s, 48);
                }
        for (i = 0; i < nn && i < 12; ++i)
            dumpf("mesh      %-28s %u line triangles\n", name[i], cnt[i]);
        if (other)
            dumpf("mesh      %-28s %u line triangles\n", "(other shapes)", other);
    }
}

/*  Every triangle in a range keys to that range's chunk, the ranges tile
 *  the list: what a meet must keep true. */
static int mesh_ranges_check(const RMesh *m, int verbose)
{
    uint32_t bad = 0, pos = 0, i;
    int      k;
    if (!m->ranged)
    {
        if (verbose)
            dumpf("chunks  the mesh is not partitioned\n");
        return m->n_land ? 1 : 0;
    }
    for (k = 0; k < MESH_RANGES * MESH_CHUNKS; ++k)
    {
        if (m->range_start[k] != pos)
            ++bad;
        pos += m->range_count[k];
        if (k >= m->n_rl || m->range_count[k] != m->rl[k].n)
        {
            ++bad;
            continue;
        }
        for (i = 0; i < m->range_count[k]; i += 3)
        {
            const RMeshVert *v = mesh_list_corner(&m->rl[k], i);
            if (mesh_chunk_of((int32_t)floorf(v->pos[0]), (int32_t)floorf(v->pos[1])) != k / MESH_RANGES)
                ++bad;
        }
    }
    if (pos != m->n_land)
        ++bad;
    pos = 0;
    for (k = 0; k < MESH_CHUNKS; ++k)
    {
        if (m->wrange_start[k] != pos)
            ++bad;
        pos += m->wrange_count[k];
        if (k >= m->n_wl || m->wrange_count[k] != m->wl[k].n)
        {
            ++bad;
            continue;
        }
        for (i = 0; i < m->wrange_count[k]; i += 3)
        {
            const RMeshVert *v = mesh_list_corner(&m->wl[k], i);
            if (mesh_chunk_of((int32_t)floorf(v->pos[0]), (int32_t)floorf(v->pos[1])) != k)
                ++bad;
        }
    }
    if (pos != m->n_water)
        ++bad;
    if (verbose)
    {
        dumpf("chunks  %d land and %d water ranges, %u triangles out of place\n", MESH_RANGES * MESH_CHUNKS, MESH_CHUNKS, (unsigned)bad);
        mesh_size_report(m);
    }
    return bad != 0;
}
