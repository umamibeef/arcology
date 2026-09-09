/*  Shape primitives over the triangle emitter: road quads and fans,
 *  boxes, cylinders, wires and ground highlights. */
#include <math.h>
#include <string.h>

#include "mesh/internal.h"
#include "net/internal.h"

static int gix_foot_sink = -1, gix_wire_w = -1;

/*  One quad of a strip between two cross-sections, each given by its
 *  two end points in world tile coordinates, with the across and along
 *  values of the material; `za`/`zb` are the sections' heights, or
 *  negative to take the ground under each corner. */
/*  A road face clipped to the tile grid, each piece with the painter's
 *  order of the tile under its own centroid (the caller's fraction of
 *  an order, a strip's small offset, carried over).  A quad that
 *  straddles two tiles had carried the order of the tile under its
 *  midpoint, and the later tile's ground was drawn over the part of the
 *  quad inside it: a wedge of grass cut into every arc and every
 *  diagonal at each tile boundary (Barcelona's rail at column 106, row
 *  10; Oakland's bends).  Sutherland-Hodgman against every integer x
 *  and y line the triangle's box spans, the across and along values
 *  interpolated with the points. */
typedef struct
{
    float x, y, z, ac, al;
} ClipVert;

/*  A polygon cut by the half plane ax*x + ay*y <= k, or >= k: the same
 *  cut clip_axis makes, on any line rather than a grid line.  The tile's
 *  own diagonal is such a line, and a strip laid on the ground is cut on
 *  it so that no triangle spans the fold in the tile's top. */
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
        {
            float    t = (k - va) / (vb - va);
            ClipVert v;
            v.x      = a->x + (b->x - a->x) * t;
            v.y      = a->y + (b->y - a->y) * t;
            v.z      = a->z + (b->z - a->z) * t;
            v.ac     = a->ac + (b->ac - a->ac) * t;
            v.al     = a->al + (b->al - a->al) * t;
            out[m++] = v;
        }
        if (m >= 14)
            break;
    }
    return m;
}

/*  The line a tile's top is folded on, as tile.c cuts it: x - y = tc - tr
 *  across the tile, or x + y = tc + tr + 1 the other way.  A tile whose
 *  top is flat is folded on either to the same effect. */
static void tile_fold(const RCity *c, int32_t tc, int32_t tr, float *ax, float *ay, float *k)
{
    if (cut_ne_sw(slope_code(c->xter[tr * R_MAP + tc])))
        *ax = 1.0f, *ay = 1.0f, *k = (float)tc + (float)tr + 1.0f;
    else
        *ax = 1.0f, *ay = -1.0f, *k = (float)tc - (float)tr;
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
        {
            float    t = (k - va) / (vb - va);
            ClipVert v;
            v.x      = a->x + (b->x - a->x) * t;
            v.y      = a->y + (b->y - a->y) * t;
            v.z      = a->z + (b->z - a->z) * t;
            v.ac     = a->ac + (b->ac - a->ac) * t;
            v.al     = a->al + (b->al - a->al) * t;
            out[m++] = v;
        }
        if (m >= 14)
            break;
    }
    return m;
}

/*  What "show curves" hides: EVERY network material -- the strips, the
 *  crosswalks, the walks, the rails and their crossings, the panel and
 *  approaches of a level crossing, the decks and their bents -- so the
 *  overlay stands on bare ground.  One rule for all of them.  The vehicles
 *  (15) carry the overlay's wires and stay, and so do the ground tints
 *  above 20. */
int curves_hidden(float mat)
{
    if (!(s_tune.show_curves > 0.5f))
        return 0;
    return mat > 6.5f && mat < 19.5f && !(mat > 14.5f && mat < 15.5f);
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

/*  One triangle of a road, a railway or their decals, clipped to the tiles
 *  it crosses so each piece is drawn in its own tile's slot. `drape` lays
 *  it ON the ground: the piece is cut again on the tile's fold and every
 *  vertex takes the drawn surface at its own position, so no triangle of a
 *  band ever stands above the terrain or dips under it. */
static int road_tri(const char *where, const char *who, RMesh *m, const RCity *c, uint8_t mask_bit, float order, const float tri[3][3], const float *nrm, const float col[3], const float ref[3], const float ref2[3], int drape)
{
    if (s_pass == 1)
        return 0; /* the grading pass draws nothing */
    /*  With the curves shown, the road itself gets out of the way: only the
     *  fitted centreline and the marks at its piece boundaries are drawn,
     *  over bare ground.  Vehicles carry the overlay, so they are what
     *  survives the filter.  The viaducts go the same way, deck and bents. */
    if (curves_hidden(col[2]))
        return 0;
    static ClipVert poly[48][16];
    static int      cnt[48];
    ClipVert        tmp[16];
    const float     xcol[3] = {col[0], col[1], MAT_RAIL_X};
    const float    *pcol    = col;
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
    /*  Within one tile: as it is, in that tile's slot (a face of a
     *  train car lying wholly in the tile ahead of the car's centre had
     *  kept the centre's slot, and lay under that tile's ground), a rail
     *  on a crossing tile as its rails alone. */
    if (!drape && floorf(minx) == floorf(maxx - 1e-6f) && floorf(miny) == floorf(maxy - 1e-6f))
    {
        int32_t tc = (int32_t)floorf(minx), tr = (int32_t)floorf(miny);
        if (tc >= 0 && tr >= 0 && tc < R_MAP && tr < R_MAP)
        {
            uint8_t b = c->xbld[tr * R_MAP + tc];
            order     = tile_order(c, tc, tr, mask_bit) + frac;
            if (col[2] > 10.5f && col[2] < 11.5f && net_road_over_rail(b) &&
                on_crossing_panel(c, tc, tr, (tri[0][0] + tri[1][0] + tri[2][0]) / 3.0f, (tri[0][1] + tri[1][1] + tri[2][1]) / 3.0f))
                pcol = xcol;
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
        if (tc >= R_MAP)
            tc = R_MAP - 1;
        if (tr >= R_MAP)
            tr = R_MAP - 1;
        /*  A hair nearer than the surface it lies on: a marking is a decal
         *  at the surface's own height, and where that surface twists -- a
         *  ramp's first tile, climbing and turning off the road -- the two
         *  quads interpolate different depths and the line tore into a
         *  sawtooth.  A slot's fraction outranks any height term. */
        porder = tile_order(c, tc, tr, mask_bit) + frac + 0.004f;
        /* a rail on a road-over-rail crossing tile: only its rails, in the crossing surface (spec 3.15) */
        pcol = col;
        if (col[2] > 10.5f && col[2] < 11.5f && on_crossing_panel(c, tc, tr, cx, cy))
            pcol = xcol;
        if (drape)
        {
            /*  Cut on the tile's fold, and every vertex on the drawn
             *  surface under it: the piece then lies in the tile's own
             *  two planes rather than bridging them. */
            ClipVert half[2][16];
            float    ax, ay, kk;
            int      side, hn;
            if (tile_top_planar(c, tc, tr, mask_bit))
            {
                /* one plane: the piece lies on it whole */
                for (k = 0; k < cnt[q]; ++k)
                    poly[q][k].z = surface_at_tile(c, mask_bit, tc, tr, poly[q][k].x, poly[q][k].y);
                if (emit_fan(where, who, m, poly[q], cnt[q], nrm, porder, pcol) != 0)
                    return -1;
                continue;
            }
            tile_fold(c, tc, tr, &ax, &ay, &kk);
            for (side = 0; side < 2; ++side)
            {
                hn = clip_half(poly[q], cnt[q], ax, ay, kk, !side, half[side]);
                for (k = 0; k < hn; ++k)
                    half[side][k].z = surface_at_tile(c, mask_bit, tc, tr, half[side][k].x, half[side][k].y);
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

int put_tri_road_n_at(const char *where, const char *who, RMesh *m, const RCity *c, uint8_t mask_bit, float order, const float tri[3][3], const float *nrm, const float col[3], const float ref[3], const float ref2[3])
{
    return road_tri(where, who, m, c, mask_bit, order, tri, nrm, col, ref, ref2, 0);
}

/*  The same, laid ON the ground: the triangle's own heights are not
 *  used at all -- each piece it is cut into takes the drawn surface of
 *  the tile it lands in.  A junction's asphalt is drawn this way, as a
 *  segment's band is. */
int put_tri_ground_at(const char *where, const char *who, RMesh *m, const RCity *c, uint8_t mask_bit, float order, const float tri[3][3], const float *nrm, const float col[3], const float ref[3], const float ref2[3])
{
    return road_tri(where, who, m, c, mask_bit, order, tri, nrm, col, ref, ref2, 1);
}

static int put_tri_road(RMesh *m, const RCity *c, uint8_t mask_bit, float order, const float tri[3][3], const float col[3], const float ref[3], const float ref2[3])
{
    return put_tri_road_n(m, c, mask_bit, order, tri, NULL, col, ref, ref2);
}

int strip_quad_z_at(const char *where, const char *who, RMesh *m, const RCity *c, uint8_t mask_bit, float order, const float a0[2], const float a1[2], const float b0[2], const float b1[2], float za, float zb, float across0, float across1, float along_a, float along_b, float mat)
{
    /*  A height below zero at both ends means the quad lies on the
     *  ground: its corners take the drawn surface, and the pieces it is
     *  cut into take it at their own corners too (road_tri's drape). */
    const int    drape       = za < 0.0f && zb < 0.0f;
    float        road_col[3] = {0.0f, 0.0f, mat};
    float        p[4][3], tri[3][3], ref[3], ref2[3];
    const float *pt[4] = {a0, a1, b1, b0};
    int          k;
    for (k = 0; k < 4; ++k)
    {
        float z = k < 2 ? za : zb;
        p[k][0] = pt[k][0];
        p[k][1] = pt[k][1];
        p[k][2] = z >= 0.0f ? z : surface_at_world(c, mask_bit, p[k][0], p[k][1]);
    }
    memcpy(tri[0], p[0], sizeof tri[0]);
    memcpy(tri[1], p[1], sizeof tri[1]);
    memcpy(tri[2], p[2], sizeof tri[2]);
    ref[0]  = across0;
    ref[1]  = across1;
    ref[2]  = across1;
    ref2[0] = along_a;
    ref2[1] = along_a;
    ref2[2] = along_b;
    if (road_tri(where, who, m, c, mask_bit, order, (const float (*)[3])tri, NULL, road_col, ref, ref2, drape) != 0)
        return -1;
    memcpy(tri[0], p[0], sizeof tri[0]);
    memcpy(tri[1], p[2], sizeof tri[1]);
    memcpy(tri[2], p[3], sizeof tri[2]);
    ref[0]  = across0;
    ref[1]  = across1;
    ref[2]  = across0;
    ref2[0] = along_a;
    ref2[1] = along_b;
    ref2[2] = along_b;
    return road_tri(where, who, m, c, mask_bit, order, (const float (*)[3])tri, NULL, road_col, ref, ref2, drape);
}

int strip_quad_at(const char *where, const char *who, RMesh *m, const RCity *c, uint8_t mask_bit, float order, const float a0[2], const float a1[2], const float b0[2], const float b1[2], float across0, float across1, float along_a, float along_b, float mat)
{
    return strip_quad_z_at(where, who, m, c, mask_bit, order, a0, a1, b0, b1, -1.0f, -1.0f, across0, across1, along_a, along_b, mat);
}

/*  A fan of triangles about a point, from angle t0 at radius r0 to t1
 *  at r1, one material; the rim carries across 1, the apex `across_c`. */
/*  `lift` is added to the ground under each vertex, so a cap on a slope
 *  lies on the slope (set flat at its centre's height, the uphill half
 *  of a dead end's cap on a slope piece lay under the ground). */
int strip_fan_z(RMesh *m, const RCity *c, uint8_t mask_bit, float order, float cx, float cy, float t0, float t1, float r0, float r1, float across_c, float mat, int n, float lift)
{
    float road_col[3] = {0.0f, 0.0f, mat};
    int   i;
    for (i = 0; i < n; ++i)
    {
        float fa = (float)i / (float)n, fb = (float)(i + 1) / (float)n;
        float ta = t0 + (t1 - t0) * fa, tb = t0 + (t1 - t0) * fb;
        float ra = r0 + (r1 - r0) * fa, rb = r0 + (r1 - r0) * fb;
        float tri[3][3], ref[3] = {across_c, 1.0f, 1.0f}, ref2[3] = {-1.0f, -1.0f, -1.0f};
        tri[0][0] = cx;
        tri[0][1] = cy;
        tri[1][0] = cx + ra * cosf(ta);
        tri[1][1] = cy + ra * sinf(ta);
        tri[2][0] = cx + rb * cosf(tb);
        tri[2][1] = cy + rb * sinf(tb);
        tri[0][2] = surface_at_world(c, mask_bit, tri[0][0], tri[0][1]) + lift;
        tri[1][2] = surface_at_world(c, mask_bit, tri[1][0], tri[1][1]) + lift;
        tri[2][2] = surface_at_world(c, mask_bit, tri[2][0], tri[2][1]) + lift;
        if (put_tri_road(m, c, mask_bit, order, (const float (*)[3])tri, road_col, ref, ref2) != 0)
            return -1;
    }
    return 0;
}

/*  A box standing on the surface at world (x, y), `w` by `d` in tiles,
 *  from `z0` to `z1` levels above the ground there: four sides and a
 *  top, one material, `phase` in col.r. */
/*  A round column: a twelve-sided prism of radius r about (cx, cy), its top
 *  flat at z1 over the ground under its centre and its foot draped on the
 *  terrain, each base vertex at the ground under itself, so the column
 *  meets a slope along its whole rim instead of hanging over the downhill
 *  side or burying a foot that then paints over the grass in front of it.
 *  Every face goes through the tile clipper: a column crossing a tile line
 *  with one painter's order was banded by the ground either side. */
int put_cyl(RMesh *m, const RCity *c, uint8_t mask_bit, float order, float cx, float cy, float r, float z0, float z1, float mat)
{
    enum
    {
        N = 12
    };
    float g       = surface_at_world(c, mask_bit, cx, cy);
    float hi      = g + z1;
    float col3[3] = {0.0f, 0.0f, mat}, ref[3] = {0.0f, 0.0f, 0.0f};
    float px[N], py[N], pz[N];
    int   k;
    (void)z0;
    for (k = 0; k < N; ++k)
    {
        float a = -6.2831853f * ((float)k + 0.5f) / (float)N;
        px[k]   = cx + r * cosf(a);
        py[k]   = cy + r * sinf(a);
        pz[k]   = surface_at_world(c, mask_bit, px[k], py[k]) - net_geo(&gix_foot_sink, "foot_sink");
        if (pz[k] > hi - 0.02f)
            pz[k] = hi - 0.02f;
    }
    for (k = 0; k < N; ++k)
    {
        int   j  = (k + 1) % N;
        float mx = 0.5f * (px[k] + px[j]) - cx, my = 0.5f * (py[k] + py[j]) - cy;
        float ml  = sqrtf(mx * mx + my * my), nrm[3], tri[3][3];
        nrm[0]    = ml > 1e-6f ? mx / ml : 1.0f;
        nrm[1]    = ml > 1e-6f ? my / ml : 0.0f;
        nrm[2]    = 0.0f;
        tri[0][0] = px[k];
        tri[0][1] = py[k];
        tri[0][2] = hi;
        tri[1][0] = px[j];
        tri[1][1] = py[j];
        tri[1][2] = hi;
        tri[2][0] = px[j];
        tri[2][1] = py[j];
        tri[2][2] = pz[j];
        if (put_tri_road_n(m, c, mask_bit, order, (const float (*)[3])tri, nrm, col3, ref, ref) != 0)
            return -1;
        tri[1][0] = px[j];
        tri[1][1] = py[j];
        tri[1][2] = pz[j];
        tri[2][0] = px[k];
        tri[2][1] = py[k];
        tri[2][2] = pz[k];
        if (put_tri_road_n(m, c, mask_bit, order, (const float (*)[3])tri, nrm, col3, ref, ref) != 0)
            return -1;
    }
    for (k = 1; k + 1 < N; ++k)
    {
        float up[3] = {0.0f, 0.0f, 1.0f}, tri[3][3];
        tri[0][0]   = px[0];
        tri[0][1]   = py[0];
        tri[0][2]   = hi;
        tri[1][0]   = px[k];
        tri[1][1]   = py[k];
        tri[1][2]   = hi;
        tri[2][0]   = px[k + 1];
        tri[2][1]   = py[k + 1];
        tri[2][2]   = hi;
        if (put_tri_road_n(m, c, mask_bit, order, (const float (*)[3])tri, up, col3, ref, ref) != 0)
            return -1;
    }
    return 0;
}

/*  A ground highlight under "show curves": the tile's own top, tinted
 *  and blended, a hair up, under the roads and everything else that
 *  stands on the tile.  col.r picks the tint: 3 the corridor's tan, 6
 *  the free air's blue. */
int tile_highlight(RMesh *m, const RCity *c, uint8_t mask_bit, int32_t col, int32_t row, float paint)
{
    if (s_pass == 1)
        return 0; /* the grading pass draws nothing */
    /*  A sliver above the ground's own slot and under the roads: a road
     *  strip is pulled a fifth of a slot nearer than its ground (the vertex
     *  shader), and the zone tint's 0.4 would otherwise outrank that and
     *  lie over the asphalt.  The ramp marker (paint 8) is a LID: at the
     *  top of the tile's own slot, over the ramp's concrete and its edge
     *  lines, translucent so they show through it orange.  The ramp is
     *  drawn in the ground's slot too, and nothing sits between them. */
    float lift  = paint > 7.5f ? 0.0f : 0.02f;
    float order = tile_order(c, col, row, mask_bit) + (paint > 7.5f ? 0.98f : 0.05f);
    float z[4], zc[3] = {paint, 0.0f, MAT_HILITE}, ref[3] = {paint, paint, paint}, tri[3][3];
    int   was = m->to_water, rc;
    tile_top(c, col, row, mask_bit, z);
    m->to_water = 1; /* the blended list, drawn after the opaque mesh */
    tri[0][0]   = (float)col;
    tri[0][1]   = (float)row;
    tri[0][2]   = z[NW] + lift;
    tri[1][0]   = (float)col + 1.0f;
    tri[1][1]   = (float)row;
    tri[1][2]   = z[NE] + lift;
    tri[2][0]   = (float)col + 1.0f;
    tri[2][1]   = (float)row + 1.0f;
    tri[2][2]   = z[SE] + lift;
    rc          = put_tri_r2(m, (const float (*)[3])tri, NULL, order, zc, ref, ref, 1);
    tri[1][0]   = (float)col + 1.0f;
    tri[1][1]   = (float)row + 1.0f;
    tri[1][2]   = z[SE] + lift;
    tri[2][0]   = (float)col;
    tri[2][1]   = (float)row + 1.0f;
    tri[2][2]   = z[SW] + lift;
    if (rc == 0)
        rc = put_tri_r2(m, (const float (*)[3])tri, NULL, order, zc, ref, ref, 1);
    m->to_water = was;
    return rc;
}

int put_box(RMesh *m, const RCity *c, uint8_t mask_bit, float order, float cx, float cy, float w, float d, float z0, float z1, float mat, float phase)
{
    if (s_pass == 1)
        return 0; /* the grading pass draws nothing */
    float g  = surface_at_world(c, mask_bit, cx, cy);
    float x0 = cx - w * 0.5f, x1 = cx + w * 0.5f, y0 = cy - d * 0.5f, y1 = cy + d * 0.5f;
    /*  A bent is the viaduct's, and hides with it under the curve overlay. */
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

/*  A wire: a thin quad from world (x0, y0) at height z0 to (x1, y1) at
 *  z1 above the ground at each end, sagging in the middle. */
int put_wire(RMesh *m, const RCity *c, uint8_t mask_bit, float order, float x0, float y0, float z0, float x1, float y1, float z1, float sag)
{
    return put_wire_paint(m, c, mask_bit, order, x0, y0, z0, x1, y1, z1, sag, -1.0f);
}

/*  The same in a vehicle paint (terrain.frag: 0 dark, 1 brown, 2 grey, 3
 *  tan, 4 white, 5 red, 6 blue, 7 light grey), or the pole's own material
 *  when the paint is negative.  The lane overlay draws in colours so two
 *  lane lines never read as a railway. */
int put_wire_paint(RMesh *m, const RCity *c, uint8_t mask_bit, float order, float x0, float y0, float z0, float x1, float y1, float z1, float sag, float paint)
{
    if (s_pass == 1)
        return 0; /* the grading pass draws nothing */
    const float col3[3] = {paint < 0.0f ? 0.0f : paint, 0.0f, paint < 0.0f ? MAT_PROP : MAT_VEHICLE};
    const int   n       = 4;
    float       w       = net_geo(&gix_wire_w, "wire_w");
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
