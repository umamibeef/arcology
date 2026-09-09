/*  mesh/check.c -- the checks: watertightness, and roads against the
 *  ground.  Split out of mesh.c; see mesh/internal.h. */
#include "dump.h"
#include "mesh/internal.h"
#include "net/internal.h"
#include "opt.h"
#include <string.h>

/* ---- the check --------------------------------------------------------- */

/*  Every edge of every triangle must belong to another triangle too: a
 *  surface with no free edge has no crack.  Edges are keyed on their
 *  quantised end points (heights are means of at most four levels, so
 *  forty-eighths are exact).  A vertical edge is a span on its corner's
 *  vertical, and the spans there may be split differently by the walls
 *  meeting at the corner, so those are checked as coverage: every part of
 *  the vertical that any edge covers must be covered at least twice.  The
 *  base of the map's cut, z = 0, is the floor and is closed by definition. */
typedef struct
{
    int32_t a[3], b[3];
    int32_t count;
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

static const char *mat_name(float m)
{
    switch ((int)(m + 0.5f))
    {
        case 0:
            return "ground";
        case 1:
            return "blocks";
        case 2:
            return "sediment";
        case 3:
            return "glass";
        case 4:
            return "seabed";
        case 5:
            return "earth";
        case 6:
            return "water";
        default:
            return "?";
    }
}

/*  The clipping check: no terrain face may rise above a road or sidewalk
 *  face over its footprint.  Every road triangle is sampled on a
 *  barycentric grid; at each sample the top faces of the tile under it
 *  are evaluated, and a terrain height above the road's by more than a
 *  hair is a penetration.  Returns the number of penetrating samples;
 *  with `verbose` prints the tiles, the deepest first. */
typedef struct
{
    uint32_t n;
    float    worst;
} ClipTile;


/*  Coplanar overlap: two faces covering the same ground at the same
 *  height.  Nothing in the renderer can separate them -- both are at one
 *  depth, and which of the two a pixel shows is settled by the painter's
 *  slot, or by nothing at all when the slots agree -- so the pair reads as
 *  a flicker, a blotch, or one surface simply missing.  Every such pair is
 *  a defect, and the count is meant to be zero.
 *
 *  Two triangles of the SAME shape are passed over: a shape's own faces
 *  tile a surface and meet along their edges, which the area test already
 *  ignores, and a shape that overlaps itself is its producer's business.
 *  What matters here is two different things laid on the same ground. */
typedef struct
{
    float x, y;
} OvPt;

#define OV_MAX 8 /* a triangle clipped by three half planes keeps six */

static float ov_clip(const OvPt *in, int n, OvPt a, OvPt b, OvPt *out)
{
    /*  Keep what lies to the left of a->b, the half plane the third corner
     *  is on; the caller orders the triangle so that is the inside. */
    float ex = b.x - a.x, ey = b.y - a.y;
    int   m = 0, i;
    for (i = 0; i < n; ++i)
    {
        const OvPt *p = &in[i], *q = &in[(i + 1) % n];
        float       dp = (p->x - a.x) * ey - (p->y - a.y) * ex;
        float       dq = (q->x - a.x) * ey - (q->y - a.y) * ex;
        /*  Each edge may keep its own start and the point where it
         *  crosses the line, so a pass can add two: the room is checked
         *  before each rather than after the pair. */
        if (dp <= 0.0f && m < OV_MAX)
            out[m++] = *p;
        if ((dp < 0.0f) != (dq < 0.0f) && fabsf(dq - dp) > 1e-12f && m < OV_MAX)
        {
            float t = dp / (dp - dq);
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
     *  points as it keeps, so a slot past that count stands for the
     *  origin rather than for whatever the stack held. */
    OvPt poly[2][OV_MAX] = {{{0.0f, 0.0f}}}, tri[3];
    int  n = 3, k, src = 0;
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

int mesh_check_overlap(const RMesh *m, int verbose)
{
    enum
    {
        MAXT = 4096
    };
    static uint32_t tri[MAXT];
    int32_t         col, row;
    int             pairs = 0, fights = 0, buried = 0, tiles = 0;
    int             kind_a[32], kind_b[32], kind_n[32], nkinds = 0;
    if (!m || !m->tri_comp)
        return 0;
    for (row = 0; row < R_MAP; ++row)
        for (col = 0; col < R_MAP; ++col)
        {
            int n = mesh_tris_at(m, col, row, tri, MAXT), i, j, here = 0;
            for (i = 0; i < n; ++i)
                for (j = i + 1; j < n; ++j)
                {
                    float    pa[3][3], pb[3][3];
                    uint32_t sa, sb;
                    OvPt     at = {0.0f, 0.0f}; /* nowhere, until an overlap has somewhere */
                    float    ar, za, zb;
                    mesh_tri_get(m, tri[i], pa, &sa);
                    mesh_tri_get(m, tri[j], pb, &sb);
                    if (sa == sb)
                        continue; /* one shape's own faces are its own business */
                    ar = ov_area(pa, pb, &at);
                    if (ar < 1e-4f)
                        continue;
                    za = ov_z(pa, at);
                    zb = ov_z(pb, at);
                    if (fabsf(za - zb) > 0.004f)
                        continue; /* one stands clear of the other */
                    /*  Two faces at one height are ordinary: the road
                     *  network lies ON the ground, and the painter's slot
                     *  each vertex carries settles which is in front.  A
                     *  pair is a FIGHT only when the slots agree too, for
                     *  then nothing decides and the pixel goes to whichever
                     *  was drawn last. */
                    {
                        float oa = m->land[3u * tri[i]].pos[3], ob = m->land[3u * tri[j]].pos[3];
                        float wa = m->land[3u * tri[i]].col[2], wb = m->land[3u * tri[j]].col[2];
                        ++pairs;
                        /*  A footway with THE THING IT BELONGS TO under
                         *  it: a junction's asphalt beneath the junction's
                         *  own footway, or a strip's carriageway beneath
                         *  its own.  That means the piece was not drawn
                         *  back to its own kerb, and it is the defect this
                         *  check exists for.  A railway crossing a
                         *  footway is another matter and is counted with
                         *  the rest. */
                        {
                            int     wa_is = wa > 12.5f && wa < 13.5f, wb_is = wb > 12.5f && wb < 13.5f;
                            ShapeId walk  = wa_is ? sa : sb, under = wa_is ? sb : sa;
                            ShapeId owner = shape_parent(walk);
                            /*  The footway's OWNER is the piece it belongs
                             *  to, and what is buried is that piece or
                             *  anything drawn inside it -- a crossing
                             *  painted on the carriageway is the road's
                             *  own surface under its own kerb as much as
                             *  the plain asphalt beside it. */
                            if (wa_is != wb_is && (owner == under || shape_is_ancestor(owner, under)))
                            {
                                ++buried;
                                if (verbose && (g_dev.buried_all || buried <= 40))
                                    dumpf("overlap   BURIED %d,%d: %s under its own %s, %.4f tiles at %.3f\n", (int)col, (int)row,
                                          shape_name(under) ? shape_name(under) : "?", shape_name(walk) ? shape_name(walk) : "?", (double)ar, (double)za);
                            }
                        }
                        if (fabsf(oa - ob) > 1e-4f)
                            continue;
                        /*  A shape and a shape drawn INSIDE it are one
                         *  surface, as two faces of one shape are: a
                         *  crossing is the carriageway it is painted on,
                         *  and the hairline its quads share with the
                         *  asphalt either side of it is a seam, not a
                         *  fight over the same pixel. */
                        if (shape_is_ancestor(sa, sb) || shape_is_ancestor(sb, sa))
                            continue;
                        ++fights;
                        {
                            /*  Which KINDS collide, so the worst can be
                             *  given an order rather than chased one tile
                             *  at a time. */
                            float ma = m->land[3u * tri[i]].col[2], mb = m->land[3u * tri[j]].col[2];
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
                            dumpf("overlap   %d,%d: %s and %s share %.4f tiles at height %.3f and slot %.3f\n", (int)col, (int)row,
                                  shape_name(sa) ? shape_name(sa) : "?", shape_name(sb) ? shape_name(sb) : "?", (double)ar, (double)za, (double)oa);
                    }
                }
            if (here)
                ++tiles;
        }
    dumpf("overlap  %d coplanar face pairs, %d with the same painter's slot on %d tiles, %d of road works buried under a footway\n", pairs, fights, tiles, buried);
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
    /*  The buried footways are the ones a build must never have; the rest
     *  is reported and left to the eye for now. */
    return buried;
}


/*  The Z probe: every surface over one point of the map, in the order the
 *  renderer sees them.  For each face covering the point it gives the
 *  height there, the material, the painter's slot the vertex carries, and
 *  the shape it belongs to.  Two faces at one height are what a flicker
 *  or a wrongly hidden surface is made of, and this is the only way to
 *  see which of them is in front and by how much. */
void mesh_probe(const RMesh *m, float px, float py)
{
    enum
    {
        MAXT = 4096,
        MAXH = 64
    };
    static uint32_t tri[MAXT];
    typedef struct
    {
        float    z, mat, order;
        uint32_t shape, t;
    } Hit;
    Hit hit[MAXH];
    int     nh = 0, i, j;
    int32_t bc = (int32_t)floorf(px), br = (int32_t)floorf(py), dc, dr;
    if (!m || !m->tri_comp)
        return;
    for (dr = -1; dr <= 1; ++dr)
        for (dc = -1; dc <= 1; ++dc)
        {
            int n = mesh_tris_at(m, bc + dc, br + dr, tri, MAXT), k;
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
                l0 = ((p[1][1] - p[2][1]) * (px - p[2][0]) + (p[2][0] - p[1][0]) * (py - p[2][1])) /
                     ((p[1][1] - p[2][1]) * (p[0][0] - p[2][0]) + (p[2][0] - p[1][0]) * (p[0][1] - p[2][1]));
                l1 = ((p[2][1] - p[0][1]) * (px - p[2][0]) + (p[0][0] - p[2][0]) * (py - p[2][1])) /
                     ((p[1][1] - p[2][1]) * (p[0][0] - p[2][0]) + (p[2][0] - p[1][0]) * (p[0][1] - p[2][1]));
                l2          = 1.0f - l0 - l1;
                hit[nh].z   = l0 * p[0][2] + l1 * p[1][2] + l2 * p[2][2];
                hit[nh].mat = m->land[3u * tri[k]].col[2];
                hit[nh].order = m->land[3u * tri[k]].pos[3];
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
        dumpf("probe     z %.5f  slot %9.3f  material %5g  %s%s%s\n", (double)hit[i].z, (double)hit[i].order, (double)hit[i].mat,
              nm ? nm : "?", pn ? ", in " : "", pn ? pn : "");
        if (i && fabsf(hit[i].z - hit[i - 1].z) < 0.0005f)
            dumpf("probe       ^ the same height as the face above it, to %.5f\n", (double)fabsf(hit[i].z - hit[i - 1].z));
    }
}

int mesh_check_roads(const RMesh *m, int verbose)
{
    uint32_t  n_tri = m->n_land / 3u, i, *count, *start, *list, hits = 0;
    ClipTile *tiles;
    uint32_t *order;
    const int N = 8;
    count       = (uint32_t *)calloc((size_t)R_MAP * R_MAP + 1u, sizeof *count);
    start       = (uint32_t *)calloc((size_t)R_MAP * R_MAP + 1u, sizeof *start);
    list        = (uint32_t *)malloc((size_t)n_tri * sizeof *list);
    tiles       = (ClipTile *)calloc((size_t)R_MAP * R_MAP, sizeof *tiles);
    order       = (uint32_t *)malloc((size_t)R_MAP * R_MAP * sizeof *order);
    if (!count || !start || !list || !tiles || !order)
    {
        free(count);
        free(start);
        free(list);
        free(tiles);
        free(order);
        return -1;
    }
    /*  The terrain's top faces by tile: the ones with an area in plan. */
    for (i = 0; i < n_tri; ++i)
    {
        const RMeshVert *v  = &m->land[i * 3u];
        float            ax = v[1].pos[0] - v[0].pos[0], ay = v[1].pos[1] - v[0].pos[1];
        float            bx = v[2].pos[0] - v[0].pos[0], by = v[2].pos[1] - v[0].pos[1];
        float            cx, cy;
        int32_t          col, row;
        if (v[0].col[2] > 6.5f || fabsf(ax * by - ay * bx) < 1e-6f)
            continue;
        cx  = (v[0].pos[0] + v[1].pos[0] + v[2].pos[0]) / 3.0f;
        cy  = (v[0].pos[1] + v[1].pos[1] + v[2].pos[1]) / 3.0f;
        col = (int32_t)floorf(cx);
        row = (int32_t)floorf(cy);
        if (col < 0 || row < 0 || col >= R_MAP || row >= R_MAP)
            continue;
        ++count[row * R_MAP + col];
    }
    for (i = 0; i < (uint32_t)R_MAP * R_MAP; ++i)
        start[i + 1] = start[i] + count[i];
    memset(count, 0, ((size_t)R_MAP * R_MAP + 1u) * sizeof *count);
    for (i = 0; i < n_tri; ++i)
    {
        const RMeshVert *v  = &m->land[i * 3u];
        float            ax = v[1].pos[0] - v[0].pos[0], ay = v[1].pos[1] - v[0].pos[1];
        float            bx = v[2].pos[0] - v[0].pos[0], by = v[2].pos[1] - v[0].pos[1];
        float            cx, cy;
        int32_t          col, row, t;
        if (v[0].col[2] > 6.5f || fabsf(ax * by - ay * bx) < 1e-6f)
            continue;
        cx  = (v[0].pos[0] + v[1].pos[0] + v[2].pos[0]) / 3.0f;
        cy  = (v[0].pos[1] + v[1].pos[1] + v[2].pos[1]) / 3.0f;
        col = (int32_t)floorf(cx);
        row = (int32_t)floorf(cy);
        if (col < 0 || row < 0 || col >= R_MAP || row >= R_MAP)
            continue;
        t                           = row * R_MAP + col;
        list[start[t] + count[t]++] = i;
    }
    /*  --tile-dump col,row prints every face whose centroid lies on
     *  that tile, any material, for inspection. */
    {
        const char *dump = g_dev.tile_dump;
        int         dc, dr;
        if (dump && strcmp(dump, "all") == 0)
            for (i = 0; i < n_tri; ++i)
            {
                /* every triangle's material and the tile its centroid lies on, for a whole-mesh diff */
                const RMeshVert *v  = &m->land[i * 3u];
                float            cx = (v[0].pos[0] + v[1].pos[0] + v[2].pos[0]) / 3.0f;
                float            cy = (v[0].pos[1] + v[1].pos[1] + v[2].pos[1]) / 3.0f;
                dumpf("tri mat %g tile %d,%d\n", (double)v[0].col[2], (int)floorf(cx), (int)floorf(cy));
            }
        else if (dump && sscanf(dump, "%d,%d", &dc, &dr) == 2)
            for (i = 0; i < n_tri; ++i)
            {
                const RMeshVert *v  = &m->land[i * 3u];
                float            cx = (v[0].pos[0] + v[1].pos[0] + v[2].pos[0]) / 3.0f;
                float            cy = (v[0].pos[1] + v[1].pos[1] + v[2].pos[1]) / 3.0f;
                if ((int32_t)floorf(cx) == dc && (int32_t)floorf(cy) == dr)
                    dumpf("tri mat %g order %g (%.3f,%.3f,%.3f) (%.3f,%.3f,%.3f) (%.3f,%.3f,%.3f)\n", (double)v[0].col[2], (double)v[0].pos[3], (double)v[0].pos[0], (double)v[0].pos[1], (double)v[0].pos[2], (double)v[1].pos[0], (double)v[1].pos[1], (double)v[1].pos[2], (double)v[2].pos[0], (double)v[2].pos[1], (double)v[2].pos[2]);
            }
    }
    /*  Every road face, sampled. */
    for (i = 0; i < n_tri; ++i)
    {
        const RMeshVert *v   = &m->land[i * 3u];
        float            mat = v[0].col[2];
        int              a, b;
        if (!(mat > 6.5f && mat < 7.5f) && !(mat > 9.5f && mat < 11.5f) && !(mat > 12.5f && mat < 14.5f) && !(mat > 15.5f && mat < 17.5f) && !(mat > 18.5f && mat < 19.5f))
            continue; /* the bands, the sidewalk corners, the crossing surface and approach; not props, not vehicles, not a pier, whose foot is buried; a deck is checked like any other carriageway */
        if (v[0].nrm[3] >= 3.5f)
            continue; /* a quad in a cut: the ground over it is meant, held back by retaining walls */
        {
            /*  A sliver of no area draws nothing, so nothing can show
             *  through it: the joints of a path fitted through a corridor
             *  leave a few where the band's edge crosses a tile line at a
             *  shallow angle. */
            float ax = v[1].pos[0] - v[0].pos[0], ay = v[1].pos[1] - v[0].pos[1];
            float bx = v[2].pos[0] - v[0].pos[0], by = v[2].pos[1] - v[0].pos[1];
            if (fabsf(ax * by - ay * bx) < 2e-3f) /* under half a square metre: no pixel of its own */
                continue;
        }
        /* strictly inside the face: a sample on an edge would read the
         * tile across it, which may stand higher behind a wall */
        for (a = 1; a < N; ++a)
            for (b = 1; a + b < N; ++b)
            {
                float    u = (float)a / (float)N, w = (float)b / (float)N, s = 1.0f - u - w;
                float    x   = u * v[0].pos[0] + w * v[1].pos[0] + s * v[2].pos[0];
                float    y   = u * v[0].pos[1] + w * v[1].pos[1] + s * v[2].pos[1];
                float    z   = u * v[0].pos[2] + w * v[1].pos[2] + s * v[2].pos[2];
                int32_t  col = (int32_t)floorf(x), row = (int32_t)floorf(y), t;
                uint32_t j;
                float    worst = 0.0f;
                if (col < 0 || row < 0 || col >= R_MAP || row >= R_MAP)
                    continue;
                t = row * R_MAP + col;
                for (j = start[t]; j < start[t + 1]; ++j)
                {
                    const RMeshVert *q  = &m->land[list[j] * 3u];
                    float            d  = (q[1].pos[0] - q[0].pos[0]) * (q[2].pos[1] - q[0].pos[1]) -
                                          (q[2].pos[0] - q[0].pos[0]) * (q[1].pos[1] - q[0].pos[1]);
                    float            l1 = ((q[1].pos[0] - x) * (q[2].pos[1] - y) - (q[2].pos[0] - x) * (q[1].pos[1] - y)) / d;
                    float            l2 = ((q[2].pos[0] - x) * (q[0].pos[1] - y) - (q[0].pos[0] - x) * (q[2].pos[1] - y)) / d;
                    float            l3 = 1.0f - l1 - l2, zt;
                    if (l1 < -1e-4f || l2 < -1e-4f || l3 < -1e-4f)
                        continue;
                    zt = l1 * q[0].pos[2] + l2 * q[1].pos[2] + l3 * q[2].pos[2];
                    if (zt - z > worst)
                        worst = zt - z;
                }
                if (worst > 0.02f) /* a hair: a fifth of a pixel at the base zoom */
                {
                    ++hits;
                    ++tiles[t].n;
                    if (worst > tiles[t].worst)
                        tiles[t].worst = worst;
                    if (g_dev.clip_dump && tiles[t].n <= 2)
                    {
                        dumpf("  clip at %.2f,%.2f road z %.2f mat %g tri (%.2f,%.2f,%.2f) (%.2f,%.2f,%.2f) (%.2f,%.2f,%.2f) order %g; ground +%.2f\n",
                              (double)x,
                              (double)y,
                              (double)z,
                              (double)mat,
                              (double)v[0].pos[0],
                              (double)v[0].pos[1],
                              (double)v[0].pos[2],
                              (double)v[1].pos[0],
                              (double)v[1].pos[1],
                              (double)v[1].pos[2],
                              (double)v[2].pos[0],
                              (double)v[2].pos[1],
                              (double)v[2].pos[2],
                              (double)v[0].pos[3],
                              (double)worst);
                        for (j = start[t]; j < start[t + 1]; ++j)
                        {
                            const RMeshVert *q = &m->land[list[j] * 3u];
                            dumpf("     ground mat %g (%.2f,%.2f,%.2f) (%.2f,%.2f,%.2f) (%.2f,%.2f,%.2f)\n", (double)q[0].col[2], (double)q[0].pos[0], (double)q[0].pos[1], (double)q[0].pos[2], (double)q[1].pos[0], (double)q[1].pos[1], (double)q[1].pos[2], (double)q[2].pos[0], (double)q[2].pos[1], (double)q[2].pos[2]);
                        }
                    }
                }
            }
    }
    /*  Every tile carrying a network piece must have network geometry
     *  over it: a face of a road material whose centroid lies on the
     *  tile.  A piece the walk could not draw would otherwise vanish,
     *  its sprite skipped and nothing in its place. */
    {
        uint32_t missing = 0;
        memset(count, 0, ((size_t)R_MAP * R_MAP + 1u) * sizeof *count);
        for (i = 0; i < n_tri; ++i)
        {
            const RMeshVert *v = &m->land[i * 3u];
            int              k;
            if (v[0].col[2] < 6.5f || (v[0].col[2] > 14.5f && v[0].col[2] < 15.5f))
                continue; /* a vehicle is no network geometry */
            /* the face touches every tile a vertex, an edge's midpoint or the centroid lies on */
            for (k = 0; k < 7; ++k)
            {
                float   x   = k < 3 ? v[k].pos[0] : k < 6 ? 0.5f * (v[k - 3].pos[0] + v[(k - 2) % 3].pos[0])
                                                          : (v[0].pos[0] + v[1].pos[0] + v[2].pos[0]) / 3.0f;
                float   y   = k < 3 ? v[k].pos[1] : k < 6 ? 0.5f * (v[k - 3].pos[1] + v[(k - 2) % 3].pos[1])
                                                          : (v[0].pos[1] + v[1].pos[1] + v[2].pos[1]) / 3.0f;
                int32_t col = (int32_t)floorf(x), row = (int32_t)floorf(y);
                if (col >= 0 && row >= 0 && col < R_MAP && row < R_MAP)
                    ++count[row * R_MAP + col];
            }
        }
        for (i = 0; i < (uint32_t)R_MAP * R_MAP; ++i)
        {
            uint8_t b = s_check_xbld ? s_check_xbld[i] : 0;
            /* the networks, the crossings, and the highway family the deck walk builds: decks, on-ramps, end cells, blocks -- not the bridges, 0x51-0x5C, which nothing builds yet */
            if (!((b >= 0x0Eu && b <= 0x3Au) || (b >= 0x43u && b <= 0x48u) || (b >= 0x49u && b <= 0x50u) || (b >= 0x5Du && b <= 0x68u)))
                continue;
            if (count[i] == 0 && net_tile_served((int32_t)i))
                continue; /* a rail piece bypassed by its line, fitted across the free ground beside it: its segment was drawn (net/walk.c) */
            if (count[i] == 0 && b >= 0x49u && b <= 0x68u)
            {
                /*  A highway is a viaduct on its own fit, and the fit
                 *  keeps only within the free air beside the band: an arc
                 *  cuts inside a corner, a staircase becomes one sweep
                 *  (Toronto 72,97 -> 67,111), and the tiles it left are
                 *  ground under open air, by design.  Bare is a deck tile
                 *  with no network geometry within two tiles of it -- a
                 *  band the walk never reached (Toronto 72,96, "nothing
                 *  rendering") still shows, a corner cut does not. */
                int32_t col = (int32_t)(i % R_MAP), row = (int32_t)(i / R_MAP), dc, dr, any = 0;
                for (dr = -2; dr <= 2 && !any; ++dr)
                    for (dc = -2; dc <= 2 && !any; ++dc)
                        if (col + dc >= 0 && row + dr >= 0 && col + dc < R_MAP && row + dr < R_MAP && count[(row + dr) * R_MAP + col + dc])
                            any = 1;
                if (any)
                    continue;
            }
            if (count[i] == 0)
            {
                ++missing;
                if (verbose && missing <= 40)
                    dumpf("  no network geometry on column %3d row %3d, XBLD %02x\n", (int)(i % R_MAP), (int)(i / R_MAP), (unsigned)b);
            }
        }
        if (verbose)
            dumpf("road pieces without geometry: %u\n", missing);
        hits += missing;
    }
    if (verbose)
    {
        uint32_t shown = 0, n_bad = 0;
        for (i = 0; i < (uint32_t)R_MAP * R_MAP; ++i)
            if (tiles[i].n)
                order[n_bad++] = i;
        /* the deepest first: sort the indices by the tile's worst */
        {
            uint32_t p, q;
            for (p = 1; p < n_bad; ++p)
                for (q = p; q > 0 && tiles[order[q - 1]].worst < tiles[order[q]].worst; --q)
                {
                    uint32_t tmp = order[q];
                    order[q]     = order[q - 1];
                    order[q - 1] = tmp;
                }
        }
        dumpf("road clip  %u samples under the terrain on %u tiles\n", hits, n_bad);
        fit_stats();
        lane_stats_print();
        sidewalk_stats_print();
        junction_outline_print();
        path_fit_probes();
        walk_net_check();
        shape_unclaimed_report();
        ramp_stats();
        for (i = 0; i < n_bad && shown < 40; ++i, ++shown)
            dumpf("  column %3d row %3d  %4u samples, terrain above by %.2f\n",
                   (int)(order[i] % R_MAP),
                   (int)(order[i] / R_MAP),
                   tiles[order[i]].n,
                   (double)tiles[order[i]].worst);
    }
    free(count);
    free(start);
    free(list);
    free(tiles);
    free(order);
    return (int)hits;
}

int mesh_check(const RMesh *m, int verbose)
{
    if (g_dev.spike_check)
    {
        /* ground top faces spanning over a level, or wider than a tile: a deformed or mis-welded face */
        uint32_t i, n = m->n_land / 3u, bad = 0;
        for (i = 0; i < n; ++i)
        {
            const RMeshVert *v  = &m->land[i * 3u];
            float            z0 = v[0].pos[2], z1 = z0, x0 = v[0].pos[0], x1 = x0, y0 = v[0].pos[1], y1 = y0;
            int              k;
            if (v[0].col[2] > 0.5f)
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
    uint32_t n_tri = (m->n_land + m->n_water) / 3u, cap = 1u, i, free_edges = 0, free_spans = 0;
    Edge    *tab;
    Span    *spans;
    uint32_t n_spans = 0;
    uint32_t shown   = 0;
    uint32_t by_mat[8];
    memset(by_mat, 0, sizeof by_mat);
    while (cap < n_tri * 6u)
        cap *= 2u;
    tab   = (Edge *)calloc(cap, sizeof *tab);
    spans = (Span *)malloc((size_t)n_tri * 3u * sizeof *spans);
    if (!tab || !spans)
    {
        free(tab);
        free(spans);
        return -1;
    }
    for (i = 0; i < n_tri; ++i)
    {
        const RMeshVert *v = i * 3u < m->n_land ? &m->land[i * 3u]
                                                : &m->water[i * 3u - m->n_land];
        int32_t          q[3][3];
        int              k;
        if (v[0].col[2] > 6.5f)
            continue; /* a road strip lies on the surface; it is not the surface */
        for (k = 0; k < 3; ++k)
            quantise(v[k].pos, q[k]);
        for (k = 0; k < 3; ++k)
        {
            const int32_t *a = q[k], *b = q[(k + 1) % 3];
            uint32_t       h;
            if (a[0] == b[0] && a[1] == b[1])
            {
                Span *s;
                if (a[2] == b[2])
                    continue; /* a point, not an edge */
                s      = &spans[n_spans++];
                s->x   = a[0];
                s->y   = a[1];
                s->z0  = a[2] < b[2] ? a[2] : b[2];
                s->z1  = a[2] < b[2] ? b[2] : a[2];
                s->mat = v[k].col[2];
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
                    e->mat   = v[k].col[2];
                    break;
                }
                if (memcmp(e->a, a, sizeof e->a) == 0 && memcmp(e->b, b, sizeof e->b) == 0)
                {
                    e->count++;
                    break;
                }
                h = (h + 1u) & (cap - 1u);
            }
        }
    }
    for (i = 0; i < cap; ++i)
    {
        const Edge *e = &tab[i];
        if (e->count != 1)
            continue;
        if (e->a[2] == 0 && e->b[2] == 0)
            continue; /* the floor */
        free_edges++;
        by_mat[(int)(e->mat + 0.5f) & 7]++;
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
                    by_mat[(int)(mat + 0.5f) & 7]++;
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
        for (k = 0; k < 8; ++k)
            if (by_mat[k])
                dumpf("  %-9s %u\n", mat_name((float)k), (unsigned)by_mat[k]);
    }
    free(tab);
    free(spans);
    return (int)(free_edges + free_spans) + mesh_ranges_check(m, verbose);
}

void mesh_free(RMesh *m)
{
    free(m->tri_comp);
    free(m->tri_comp_old);
    free(m->land);
    free(m->bucket);
    free(m->wbucket);
    free(m->snap);
    free(m->water);
    free(m->net.pts);
    free(m->net.segs);
    free(m->railnet.pts);
    free(m->railnet.segs);
    free(m->xings);
    free(m->rsigs);
    memset(m, 0, sizeof *m);
}
