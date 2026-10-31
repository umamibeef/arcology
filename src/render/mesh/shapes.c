/*  shapes.c: the shape primitives over the triangle emitter.
 *
 *  It holds line quads and fans, boxes, cylinders, bars, faces, wires
 *  and ground highlights.  None of them names a thing the world
 *  contains: what a box or a bar or a face IS, is the caller's. */
#include <math.h>
#include <string.h>

#include "geo.h"
#include "mesh/internal.h"

#include "mesh/emit.h"
#include "mesh/model.h"
#include "mesh/shapes.h"
#include "mesh/surface.h"

static int curves_hidden(float mat); /* the outline view: the materials it hides */

#define ARM_TRI(A, B, C, N)                                                                                  \
    do                                                                                                       \
    {                                                                                                        \
        memcpy(t3[0], (A), sizeof t3[0]);                                                                    \
        memcpy(t3[1], (B), sizeof t3[1]);                                                                    \
        memcpy(t3[2], (C), sizeof t3[2]);                                                                    \
        if (put_tri_line_n(m, c, mask_bit, order + 0.05f, (const float (*)[3])t3, (N), col, ref, ref2) != 0) \
            return -1;                                                                                       \
    } while (0)
static int gix_wire_w = -1;
/*  The decal's slot, which the geo store holds as decal_slot. */
static int gix_decal_slot = -1;

/*  A face laid over the ground, clipped to the tile grid.  Each piece
 *  takes the painter's order of the tile under its own centroid, with the
 *  caller's fraction of an order carried over.  So the ground of a later
 *  tile never paints over the part of a face inside it.  Sutherland-
 *  Hodgman against every integer x and y line the triangle's box spans,
 *  the across and along values interpolated with the points. */
typedef struct
{
    float x, y, z, ac, al;
} ClipVert;

/*  Where the edge from a to b meets the line at k, a's value being va
 *  and b's vb.  It is worked out from the edge's first end, by x, then
 *  y, then z.  So the edge gives the same point whichever way round it
 *  is met. */
static ClipVert edge_cut(const ClipVert *a, const ClipVert *b, float va, float vb, float k)
{
    ClipVert v;
    float    t;
    if (b->x < a->x || (b->x == a->x && (b->y < a->y || (b->y == a->y && b->z < a->z))))
    {
        const ClipVert *s = a;
        float           u = va;
        a = b, b = s, va = vb, vb = u;
    }
    t    = (k - va) / (vb - va);
    v.x  = a->x + (b->x - a->x) * t;
    v.y  = a->y + (b->y - a->y) * t;
    v.z  = a->z + (b->z - a->z) * t;
    v.ac = a->ac + (b->ac - a->ac) * t;
    v.al = a->al + (b->al - a->al) * t;
    return v;
}

/*  A polygon cut by the half plane ax*x + ay*y <= k, or >= k.  The same
 *  cut clip_axis makes, on any line rather than a grid line.  The tile's
 *  own diagonal is such a line.  A face laid on the ground is cut on it
 *  so that no triangle spans the fold in the tile's top. */
static int clip_half(const ClipVert *in, int n, float ax, float ay, float k, int below, ClipVert *out)
{
    int m = 0, i;
    for (i = 0; i < n; ++i)
    {
        const ClipVert *a = &in[i], *b = &in[(i + 1) % n];
        float           va = ax * a->x + ay * a->y, vb = ax * b->x + ay * b->y;
        int             ia = below ? va <= k + 1e-6f : va >= k - 1e-6f;
        int             ib = below ? vb <= k + 1e-6f : vb >= k - 1e-6f;
        if (ia)
            out[m++] = *a;
        if (ia != ib && fabsf(vb - va) > 1e-9f)
            out[m++] = edge_cut(a, b, va, vb, k);
        if (m >= 14)
            break;
    }
    return m;
}

/*  The line a tile's top is folded on, as tile.c cuts it.  It is x - y =
 *  tc - tr across the tile, or x + y = tc + tr + 1 the other way.  A
 *  tile whose top is flat is folded on either to the same effect. */
static void tile_fold(const RCity *c, int32_t tc, int32_t tr, float *ax, float *ay, float *k)
{
    if (cut_ne_sw(slope_code(c->map.xter[tr * r_map + tc])))
        *ax = 1.0f, *ay = 1.0f, *k = (float)tc + (float)tr + 1.0f;
    else
        *ax = 1.0f, *ay = -1.0f, *k = (float)tc - (float)tr;
}

/*  A polygon cut to one of the dual ground's eight triangles in tile
 *  (tc, tr).  It is the one of edge e, its first half where h is 0.
 *  Each of the triangle's three sides cuts away what lies outside it.
 *  Answers how many points are left. */
static int dual_piece(const ClipVert *in, int n, int32_t tc, int32_t tr, int e, int h, ClipVert *out)
{
    static const float CU[4] = {0.0f, 0.0f, 1.0f, 1.0f}, CV[4] = {0.0f, 1.0f, 1.0f, 0.0f};
    ClipVert           a[16], b[16];
    float              px[3], py[3];
    int                s, m = n, a_ = EDGE_A[e], b_ = EDGE_B[e];
    float              mu = 0.5f * (CU[a_] + CU[b_]), mv = 0.5f * (CV[a_] + CV[b_]);
    px[0] = (float)tc + 0.5f, py[0] = (float)tr + 0.5f;
    px[1] = (float)tc + (h ? mu : CU[a_]), py[1] = (float)tr + (h ? mv : CV[a_]);
    px[2] = (float)tc + (h ? CU[b_] : mu), py[2] = (float)tr + (h ? CV[b_] : mv);
    memcpy(a, in, (size_t)n * sizeof a[0]);
    for (s = 0; s < 3 && m >= 3; ++s)
    {
        int   i = s, j = (s + 1) % 3, w = (s + 2) % 3;
        float ax = py[j] - py[i], ay = -(px[j] - px[i]), k = ax * px[i] + ay * py[i];
        m = clip_half(a, m, ax, ay, k, ax * px[w] + ay * py[w] <= k, b);
        memcpy(a, b, (size_t)m * sizeof a[0]);
    }
    memcpy(out, a, (size_t)m * sizeof a[0]);
    return m;
}

static int clip_axis(const ClipVert *in, int n, int axis, float k, int below, ClipVert *out)
{
    int m = 0, i;
    for (i = 0; i < n; ++i)
    {
        const ClipVert *a = &in[i], *b = &in[(i + 1) % n];
        float           va = axis ? a->y : a->x, vb = axis ? b->y : b->x;
        int             ia = below ? va <= k + 1e-6f : va >= k - 1e-6f;
        int             ib = below ? vb <= k + 1e-6f : vb >= k - 1e-6f;
        if (ia)
            out[m++] = *a;
        if (ia != ib && fabsf(vb - va) > 1e-9f)
            out[m++] = edge_cut(a, b, va, vb, k);
        if (m >= 14)
            break;
    }
    return m;
}

/*  What the outline view hides: the props and their lit faces.  The
 *  ground tints and the materials named at run time stay. */
static int curves_hidden(float mat)
{
    if (!(TUNE(show_curves) > 0.5f))
        return 0;
    return MAT_IN(mat, MAT_PROP, MAT_LAMP);
}

/*  A convex polygon as a fan of triangles. */
static int emit_fan(const char *where, const char *who, RMesh *m, const ClipVert *v, int n, const float *nrm, float order, const float *col)
{
    int k;
    for (k = 1; k + 1 < n; ++k)
    {
        float           t3[3][3], r[3], r2[3];
        const ClipVert *p3[3] = {&v[0], &v[k], &v[k + 1]};
        int             j;
        for (j = 0; j < 3; ++j)
        {
            t3[j][0] = p3[j]->x;
            t3[j][1] = p3[j]->y;
            t3[j][2] = p3[j]->z;
            r[j]     = p3[j]->ac;
            r2[j]    = p3[j]->al;
        }
        if (put_tri_r2_at(where, who, m, (const float (*)[3])t3, nrm, order, col, r, r2, 0) != 0)
            return -1;
    }
    return 0;
}

/*  One triangle laid over the ground.  It is clipped to the tiles it
 *  crosses, so each piece is drawn in its own tile's slot.  `drape` lays
 *  it ON the ground, `lift` over it.  The piece is cut again on the
 *  tile's fold and every vertex takes the drawn surface at its own
 *  position.  So no piece stands clear of the terrain or dips under it. */
static int draped_tri(const char *where, const char *who, RMesh *m, const RCity *c, uint8_t mask_bit, float order, const float tri_in[3][3], const float *nrm, const float col[3], const float ref_in[3], const float ref2_in[3], int drape, float lift)
{
    /*  From its first corner, by x, then y, then z, its winding kept.
     *  The pieces it is cut into are then the same whatever order its
     *  corners came in. */
    float        tri[3][3], rref[3], rref2[3];
    const float *ref = ref_in ? rref : NULL, *ref2 = ref2_in ? rref2 : NULL;
    {
        int f = 0, i;
        for (i = 1; i < 3; ++i)
            if (tri_in[i][0] < tri_in[f][0] ||
                (tri_in[i][0] == tri_in[f][0] && (tri_in[i][1] < tri_in[f][1] || (tri_in[i][1] == tri_in[f][1] && tri_in[i][2] < tri_in[f][2]))))
                f = i;
        for (i = 0; i < 3; ++i)
        {
            memcpy(tri[i], tri_in[(f + i) % 3], sizeof tri[i]);
            if (ref_in)
                rref[i] = ref_in[(f + i) % 3];
            if (ref2_in)
                rref2[i] = ref2_in[(f + i) % 3];
        }
    }
    /*  The outline view lays no face of a material it hides. */
    if (curves_hidden(col[2]))
        return 0;
    static ClipVert poly[48][16];
    static int      cnt[48];
    ClipVert        tmp[16];
    const float    *pcol = col;
    float           minx = tri[0][0], maxx = tri[0][0], miny = tri[0][1], maxy = tri[0][1], frac;
    int             np = 1, k, axis, q;
    for (k = 1; k < 3; ++k)
    {
        if (tri[k][0] < minx)
            minx = tri[k][0];
        if (tri[k][0] > maxx)
            maxx = tri[k][0];
        if (tri[k][1] < miny)
            miny = tri[k][1];
        if (tri[k][1] > maxy)
            maxy = tri[k][1];
    }
    frac = order - floorf(order);
    /*  Within one tile it is drawn as it is, in that tile's slot, so a
     *  face lying wholly in one tile never takes another tile's. */
    if (!drape && floorf(minx) == floorf(maxx - 1e-6f) && floorf(miny) == floorf(maxy - 1e-6f))
    {
        int32_t tc = (int32_t)floorf(minx), tr = (int32_t)floorf(miny);
        if (tc >= 0 && tr >= 0 && tc < r_map && tr < r_map)
        {
            order = tile_order(c, tc, tr, mask_bit) + frac;
        }
        return put_tri_r2_at(where, who, m, tri, nrm, order, pcol, ref, ref2, 0);
    }
    for (k = 0; k < 3; ++k)
    {
        poly[0][k].x  = tri[k][0];
        poly[0][k].y  = tri[k][1];
        poly[0][k].z  = tri[k][2];
        poly[0][k].ac = ref[k];
        poly[0][k].al = ref2[k];
    }
    cnt[0] = 3;
    for (axis = 0; axis < 2; ++axis)
    {
        float lo = axis ? miny : minx, hi = axis ? maxy : maxx;
        float line;
        for (line = floorf(lo) + 1.0f; line < hi - 1e-6f; line += 1.0f)
        {
            int n0 = np;
            for (q = 0; q < n0; ++q)
            {
                int nb = clip_axis(poly[q], cnt[q], axis, line, 1, tmp);
                int na;
                if (np >= 48)
                    break;
                na = clip_axis(poly[q], cnt[q], axis, line, 0, poly[np]);
                memcpy(poly[q], tmp, (size_t)nb * sizeof tmp[0]);
                cnt[q] = nb;
                if (na >= 3)
                    cnt[np++] = na;
            }
        }
    }
    for (q = 0; q < np; ++q)
    {
        float   cx = 0.0f, cy = 0.0f, porder;
        int32_t tc, tr;
        if (cnt[q] < 3)
            continue;
        for (k = 0; k < cnt[q]; ++k)
        {
            cx += poly[q][k].x;
            cy += poly[q][k].y;
        }
        cx /= (float)cnt[q];
        cy /= (float)cnt[q];
        tc = (int32_t)floorf(cx);
        tr = (int32_t)floorf(cy);
        if (tc < 0)
            tc = 0;
        if (tr < 0)
            tr = 0;
        if (tc >= r_map)
            tc = r_map - 1;
        if (tr >= r_map)
            tr = r_map - 1;
        /*  A hair nearer than the surface it lies on.  A marking is a
         *  decal at the surface's own height.  Where that surface twists
         *  the two quads interpolate differently.  A spur's first tile
         *  twists, climbing and turning off the line, and the two quads
         *  interpolate different depths and the line tore into a
         *  sawtooth.  A slot's fraction outranks any height term. */
        porder = tile_order(c, tc, tr, mask_bit) + frac + geo_num(&gix_decal_slot, "decal_slot");
        pcol   = col;
        if (drape)
        {
            /*  Cut on the tile's fold, with every vertex on the drawn
             *  surface under it.  The piece then lies in the tile's own
             *  two planes rather than bridging them. */
            ClipVert half[2][16];
            float    ax, ay, kk, zt[4], zc, zm[4];
            int      side, hn;
            if (!tile_top_planar(c, tc, tr, mask_bit) && tile_top(c, tc, tr, mask_bit, zt) == T_LAND &&
                tile_dual(c, tc, tr, &zc, zm))
            {
                /*  The dual ground: cut to each of its eight triangles. */
                int e, h;
                for (e = 0; e < 4; ++e)
                    for (h = 0; h < 2; ++h)
                    {
                        hn = dual_piece(poly[q], cnt[q], tc, tr, e, h, half[0]);
                        for (k = 0; k < hn; ++k)
                            half[0][k].z = surface_at_tile(c, mask_bit, tc, tr, half[0][k].x, half[0][k].y) + lift;
                        if (hn >= 3 && emit_fan(where, who, m, half[0], hn, nrm, porder, pcol) != 0)
                            return -1;
                    }
                continue;
            }
            if (tile_top_planar(c, tc, tr, mask_bit))
            {
                /* one plane: the piece lies on it whole */
                for (k = 0; k < cnt[q]; ++k)
                    poly[q][k].z = surface_at_tile(c, mask_bit, tc, tr, poly[q][k].x, poly[q][k].y) + lift;
                if (emit_fan(where, who, m, poly[q], cnt[q], nrm, porder, pcol) != 0)
                    return -1;
                continue;
            }
            tile_fold(c, tc, tr, &ax, &ay, &kk);
            for (side = 0; side < 2; ++side)
            {
                hn = clip_half(poly[q], cnt[q], ax, ay, kk, !side, half[side]);
                for (k = 0; k < hn; ++k)
                    half[side][k].z = surface_at_tile(c, mask_bit, tc, tr, half[side][k].x, half[side][k].y) + lift;
                if (emit_fan(where, who, m, half[side], hn, nrm, porder, pcol) != 0)
                    return -1;
            }
            continue;
        }
        if (emit_fan(where, who, m, poly[q], cnt[q], nrm, porder, pcol) != 0)
            return -1;
    }
    return 0;
}

int put_tri_draped_n_at(const char *where, const char *who, RMesh *m, const RCity *c, uint8_t mask_bit, float order, const float tri[3][3], const float *nrm, const float col[3], const float ref[3], const float ref2[3])
{
    return draped_tri(where, who, m, c, mask_bit, order, tri, nrm, col, ref, ref2, 0, 0.0f);
}

/*  The same, `lift` over the ground.  Each piece follows the drawn
 *  surface of its tile, fold and all, so it never passes through it. */
int put_tri_over_ground_at(const char *where, const char *who, RMesh *m, const RCity *c, uint8_t mask_bit, float order, const float tri[3][3], const float col[3], float lift)
{
    const float ref[3] = {0.0f, 0.0f, 0.0f};
    return draped_tri(where, who, m, c, mask_bit, order, tri, NULL, col, ref, ref, 1, lift);
}

/*  A box standing on the surface at world (x, y).  It is `w` by `d` in
 *  tiles, from `z0` to `z1` levels above the ground there.  It has four
 *  sides and a top, in one material.  `phase` in col.r. */
int put_box(RMesh *m, const RCity *c, uint8_t mask_bit, float order, float cx, float cy, float w, float d, float z0, float z1, float mat, float phase)
{
    float g  = surface_at_world(c, mask_bit, cx, cy);
    float x0 = cx - w * 0.5f, x1 = cx + w * 0.5f, y0 = cy - d * 0.5f, y1 = cy + d * 0.5f;
    /*  The outline view lays no box of a material it hides. */
    if (curves_hidden(mat))
        return 0;
    float lo = g + z0, hi = g + z1;
    float col3[3] = {phase, 0.0f, mat};
    float t[4][3] = {
        {x0, y0, hi},
        {x1, y0, hi},
        {x1, y1, hi},
        {x0, y1, hi}
    };
    float b[4][3] = {
        {x0, y0, lo},
        {x1, y0, lo},
        {x1, y1, lo},
        {x0, y1, lo}
    };
    static const float nrm[4][3] = {
        {0.0f,  -1.0f, 0.0f},
        {1.0f,  0.0f,  0.0f},
        {0.0f,  1.0f,  0.0f},
        {-1.0f, 0.0f,  0.0f}
    };
    float tri[3][3], ref[3] = {phase, phase, phase};
    int   k;
    for (k = 0; k < 4; ++k)
        if (put_wall_r(m, t[k], t[(k + 1) & 3], b[k], b[(k + 1) & 3], nrm[k], order, col3, phase, phase) != 0)
            return -1;
    memcpy(tri[0], t[0], sizeof tri[0]);
    memcpy(tri[1], t[1], sizeof tri[1]);
    memcpy(tri[2], t[2], sizeof tri[2]);
    if (put_tri_r2(m, (const float (*)[3])tri, NULL, order, col3, ref, ref, 0) != 0)
        return -1;
    memcpy(tri[1], t[2], sizeof tri[1]);
    memcpy(tri[2], t[3], sizeof tri[2]);
    if (put_tri_r2(m, (const float (*)[3])tri, NULL, order, col3, ref, ref, 0) != 0)
        return -1;
    /*  The underside: a box that does not stand flat on the ground, a
     *  pier's column on a slope, showed its hollow inside. */
    memcpy(tri[0], b[0], sizeof tri[0]);
    memcpy(tri[1], b[2], sizeof tri[1]);
    memcpy(tri[2], b[1], sizeof tri[2]);
    if (put_tri_r2(m, (const float (*)[3])tri, NULL, order, col3, ref, ref, 0) != 0)
        return -1;
    memcpy(tri[1], b[3], sizeof tri[1]);
    memcpy(tri[2], b[2], sizeof tri[2]);
    return put_tri_r2(m, (const float (*)[3])tri, NULL, order, col3, ref, ref, 0);
}

int put_frustum(RMesh *m, float order, float x, float y, float g, float z0, float z1, float r0, float r1, int sides, float turn, float mat)
{
    const float col[3] = {0.0f, 0.0f, mat};
    const float dz = (z1 - z0) * MESH_LEVEL_H, slant = r0 - r1, nl = sqrtf(dz * dz + slant * slant);
    int         k;
    if (curves_hidden(mat))
        return 0;
    for (k = 0; k < sides; ++k)
    {
        float a0 = turn + 6.2831853f * (float)k / (float)sides, a1 = turn + 6.2831853f * (float)(k + 1) / (float)sides, am = 0.5f * (a0 + a1);
        float c0 = cosf(a0), s0 = sinf(a0), c1 = cosf(a1), s1 = sinf(a1);
        float nrm[3]  = {cosf(am) * dz / nl, sinf(am) * dz / nl, slant / nl};
        float t[3][3] = {
            {x + c0 * r0, y + s0 * r0, g + z0},
            {x + c1 * r0, y + s1 * r0, g + z0},
            {x + c1 * r1, y + s1 * r1, g + z1}
        };
        if (put_tri_r2(m, (const float (*)[3])t, nrm, order, col, NULL, NULL, 0) != 0)
            return -1;
        if (r1 > 0.0f)
        {
            float u[3][3] = {
                {x + c0 * r0, y + s0 * r0, g + z0},
                {x + c1 * r1, y + s1 * r1, g + z1},
                {x + c0 * r1, y + s0 * r1, g + z1}
            };
            if (put_tri_r2(m, (const float (*)[3])u, nrm, order, col, NULL, NULL, 0) != 0)
                return -1;
        }
    }
    if (r1 > 0.0f)
    {
        static const float up[3] = {0.0f, 0.0f, 1.0f};
        for (k = 1; k + 1 < sides; ++k)
        {
            float a0 = turn, a1 = turn + 6.2831853f * (float)k / (float)sides, a2 = turn + 6.2831853f * (float)(k + 1) / (float)sides;
            float t[3][3] = {
                {x + cosf(a0) * r1, y + sinf(a0) * r1, g + z1},
                {x + cosf(a1) * r1, y + sinf(a1) * r1, g + z1},
                {x + cosf(a2) * r1, y + sinf(a2) * r1, g + z1}
            };
            if (put_tri_r2(m, (const float (*)[3])t, up, order, col, NULL, NULL, 0) != 0)
                return -1;
        }
    }
    return 0;
}

/*  A wire.  It is a thin quad from world (x0, y0) at height z0 to (x1,
 *  y1) at z1 above the ground at each end.  It sags in the middle. */
int put_wire(RMesh *m, const RCity *c, uint8_t mask_bit, float order, float x0, float y0, float z0, float x1, float y1, float z1, float sag)
{
    const float col3[3] = {0.0f, 0.0f, MAT_PROP};
    const int   n       = 4;
    float       w       = geo_num(&gix_wire_w, "wire_w");
    float       dx = x1 - x0, dy = y1 - y0, len = sqrtf(dx * dx + dy * dy);
    float       nx = -dy / len * w, ny = dx / len * w;
    float       ga = surface_at_world(c, mask_bit, x0, y0);
    float       gb = surface_at_world(c, mask_bit, x1, y1);
    int         i;
    for (i = 0; i < n; ++i)
    {
        float ta = (float)i / (float)n, tb = (float)(i + 1) / (float)n;
        float za = ga + (gb - ga) * ta + z0 + (z1 - z0) * ta - sag * 4.0f * ta * (1.0f - ta);
        float zb = ga + (gb - ga) * tb + z0 + (z1 - z0) * tb - sag * 4.0f * tb * (1.0f - tb);
        float tri[3][3], ref[3] = {0.0f, 0.0f, 0.0f};
        float ax = x0 + dx * ta, ay = y0 + dy * ta, bx = x0 + dx * tb, by = y0 + dy * tb;
        tri[0][0] = ax - nx;
        tri[0][1] = ay - ny;
        tri[0][2] = za;
        tri[1][0] = ax + nx;
        tri[1][1] = ay + ny;
        tri[1][2] = za;
        tri[2][0] = bx + nx;
        tri[2][1] = by + ny;
        tri[2][2] = zb;
        if (put_tri_r2(m, (const float (*)[3])tri, NULL, order, col3, ref, ref, 0) != 0)
            return -1;
        tri[1][0] = bx + nx;
        tri[1][1] = by + ny;
        tri[1][2] = zb;
        tri[2][0] = bx - nx;
        tri[2][1] = by - ny;
        tri[2][2] = zb;
        if (put_tri_r2(m, (const float (*)[3])tri, NULL, order, col3, ref, ref, 0) != 0)
            return -1;
    }
    return 0;
}

int put_lamp_under(RMesh *m, float order, float x, float y, float g, float z, float fx, float fy, float len, float wid, float code)
{
    static const float down[3] = {0.0f, 0.0f, -1.0f};
    float              col[3] = {0.0f, code, MAT_LAMP}, ref[3] = {0.0f, 0.0f, 0.0f}, ref2[3] = {code, code, code};
    float              ax = fx * len * 0.5f, ay = fy * len * 0.5f, bx = -fy * wid * 0.5f, by = fx * wid * 0.5f;
    float              q[4][3], t[3][3];
    int                k;
    if (curves_hidden(MAT_LAMP))
        return 0;
    for (k = 0; k < 4; ++k)
    {
        float sa = (k == 1 || k == 2) ? 1.0f : -1.0f, sb = k >= 2 ? 1.0f : -1.0f;
        q[k][0] = x + ax * sa + bx * sb, q[k][1] = y + ay * sa + by * sb, q[k][2] = g + z;
    }
    memcpy(t[0], q[0], sizeof t[0]), memcpy(t[1], q[2], sizeof t[1]), memcpy(t[2], q[1], sizeof t[2]);
    if (put_tri_r2(m, (const float (*)[3])t, down, order, col, ref, ref2, 0) != 0)
        return -1;
    memcpy(t[1], q[3], sizeof t[1]), memcpy(t[2], q[2], sizeof t[2]);
    return put_tri_r2(m, (const float (*)[3])t, down, order, col, ref, ref2, 0);
}

/*  ONE FACE: a small square of MAT_LAMP carrying `code`, facing (fx, fy)
 *  at (x, y), `z` levels over the ground `g`.  `uv` says what its
 *  vertices carry: a phase.  So the material can blink it, or the face's
 *  own corners, so the material can paint a shape across it. */
int put_lamp_face(RMesh *m, float order, float x, float y, float g, float z, float fx, float fy, float sz, float phase, float code, int uv)
{
    if (curves_hidden(MAT_LAMP))
        return 0; /* the overlay hides the signals with everything else */
    float lamp[3] = {phase, code, MAT_LAMP};
    float q[4][3], tri[3][3], ref[3] = {phase, phase, phase}, ref2[3] = {code, code, code};
    float wu = -fy * sz, wv = fx * sz, nrm[3] = {fx, fy, 0.0f};
    float ox = x + fx * 0.02f, oy = y + fy * 0.02f;
    q[0][0] = ox - wu;
    q[0][1] = oy - wv;
    q[0][2] = g + z - sz;
    q[1][0] = ox + wu;
    q[1][1] = oy + wv;
    q[1][2] = g + z - sz;
    q[2][0] = ox + wu;
    q[2][1] = oy + wv;
    q[2][2] = g + z + sz;
    q[3][0] = ox - wu;
    q[3][1] = oy - wv;
    q[3][2] = g + z + sz;
    memcpy(tri[0], q[0], sizeof tri[0]);
    memcpy(tri[1], q[1], sizeof tri[1]);
    memcpy(tri[2], q[2], sizeof tri[2]);
    if (uv)
    {
        /*  A face that carries its own CORNERS rather than a phase: u in
         *  col.r, v in the fraction of col.g.  So the material can paint
         *  a shape across it.  The corner goes in `ref`, the code the
         *  material keys on in `ref2`.  Nothing here knows what shape. */
        ref[0]  = 0.0f;
        ref[1]  = 1.0f;
        ref[2]  = 1.0f;
        ref2[0] = code;
        ref2[1] = code;
        ref2[2] = code + 0.5f;
    }
    if (put_tri_r2(m, (const float (*)[3])tri, nrm, order, lamp, ref, ref2, 0) != 0)
        return -1;
    memcpy(tri[1], q[2], sizeof tri[1]);
    memcpy(tri[2], q[3], sizeof tri[2]);
    if (uv)
    {
        ref[0]  = 0.0f;
        ref[1]  = 1.0f;
        ref[2]  = 0.0f;
        ref2[0] = code;
        ref2[1] = code + 0.5f;
        ref2[2] = code + 0.5f;
    }
    return put_tri_r2(m, (const float (*)[3])tri, nrm, order, lamp, ref, ref2, 0);
}

/*  A PRISM clipped to the tile grid: a box with a skirt.  Each piece
 *  takes the depth slot of the tile under it.  So a prism that straddles
 *  two tiles is not pushed under the ground of the nearer one.  `order`
 *  is the center tile's slot, with the fraction each piece keeps above
 *  the ground it stands on. */
int put_prism_clip_m(RMesh *m, const RCity *c, uint8_t mask_bit, float order, float cx, float cy, float dx, float dy, float len, float wid, float zb, float zf, float z0, float z1, float paint, float mat)
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
        if (put_tri_line_n(m, c, mask_bit, order, (const float (*)[3])t3, nrm, col, ref, ref2) != 0)
            return -1;
        memcpy(t3[0], ta, sizeof t3[0]);
        memcpy(t3[1], b, sizeof t3[1]);
        memcpy(t3[2], a, sizeof t3[2]);
        if (put_tri_line_n(m, c, mask_bit, order, (const float (*)[3])t3, nrm, col, ref, ref2) != 0)
            return -1;
    }
    memcpy(t3[0], top[0], sizeof t3[0]);
    memcpy(t3[1], top[1], sizeof t3[1]);
    memcpy(t3[2], top[2], sizeof t3[2]);
    if (put_tri_line_n(m, c, mask_bit, order, (const float (*)[3])t3, up, col, ref, ref2) != 0)
        return -1;
    memcpy(t3[0], top[0], sizeof t3[0]);
    memcpy(t3[1], top[2], sizeof t3[1]);
    memcpy(t3[2], top[3], sizeof t3[2]);
    return put_tri_line_n(m, c, mask_bit, order, (const float (*)[3])t3, up, col, ref, ref2);
}
