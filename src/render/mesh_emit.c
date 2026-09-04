/*  mesh_emit.c -- the emit primitives: triangles, walls, tops and boxes.
 *
 *  Split out of mesh.c; see mesh_int.h.
 */
#include "mesh_int.h"

/* ---- emitting ---------------------------------------------------------- */

static int grow(RMeshVert **v, uint32_t *n, uint32_t *cap, uint32_t need)
{
    uint32_t   c;
    RMeshVert *nv;
    if (*n + need <= *cap)
        return 0;
    c = *cap ? *cap : 4096u;
    while (c < *n + need)
        c *= 2u;
    nv = (RMeshVert *)realloc(*v, (size_t)c * sizeof *nv);
    if (!nv)
        return -1;
    *v   = nv;
    *cap = c;
    return 0;
}

/*  The normal of a face given as grid points, computed in the metric
 *  space where a level is LEVEL_H units, and pointed upward. */
static void face_normal(const float a[3], const float b[3], const float c[3], float n[3])
{
    float u[3], v[3], len;
    u[0] = b[0] - a[0];
    u[1] = b[1] - a[1];
    u[2] = (b[2] - a[2]) * LEVEL_H;
    v[0] = c[0] - a[0];
    v[1] = c[1] - a[1];
    v[2] = (c[2] - a[2]) * LEVEL_H;
    n[0] = u[1] * v[2] - u[2] * v[1];
    n[1] = u[2] * v[0] - u[0] * v[2];
    n[2] = u[0] * v[1] - u[1] * v[0];
    if (n[2] < 0.0f)
    {
        n[0] = -n[0];
        n[1] = -n[1];
        n[2] = -n[2];
    }
    len = sqrtf(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
    if (len > 0.0f)
    {
        n[0] /= len;
        n[1] /= len;
        n[2] /= len;
    }
    else
        n[2] = 1.0f;
}

static int same_point(const float a[3], const float b[3])
{
    return a[0] == b[0] && a[1] == b[1] && a[2] == b[2];
}

/*  One flat-shaded triangle.  A ground vertex also carries what the
 *  ground material reads of the topology at its corner: the curvature in
 *  the normal's fourth component and the height field's gradient, in
 *  world units, in the colour's first two -- or zero gradient when `flat`
 *  is set, for a levelled pad.  A wall vertex carries the material's
 *  reference heights instead, `ref` and `ref2` per vertex: the ground the
 *  sediment's layers follow, the table and the bed the water column
 *  deepens between.  A degenerate triangle is dropped. */
int put_tri_r2(RMesh *m, const float p[3][3], const float *nrm, float order, const float col[3], const float *ref, const float *ref2, int flat)
{
    float nn[3];
    int   k;
    if (same_point(p[0], p[1]) || same_point(p[1], p[2]) || same_point(p[0], p[2]))
        return 0;
    if (m->to_water)
    {
        if (grow(&m->water, &m->n_water, &m->cap_water, 3) != 0)
            return -1;
    }
    else if (grow(&m->land, &m->n_land, &m->cap_land, 3) != 0)
        return -1;
    if (nrm)
        memcpy(nn, nrm, sizeof nn);
    else
        face_normal(p[0], p[1], p[2], nn);
    for (k = 0; k < 3; ++k)
    {
        RMeshVert *o  = m->to_water ? &m->water[m->n_water++]
                                    : &m->land[m->n_land++];
        int32_t    gc = (int32_t)(p[k][0] + 0.5f), gr = (int32_t)(p[k][1] + 0.5f);
        float      curv = 0.0f, gx = 0.0f, gy = 0.0f;
        if (!flat && gc >= 0 && gc < GRID && gr >= 0 && gr < GRID)
        {
            int32_t c0 = gc > 0 ? gc - 1 : gc, c1 = gc < GRID - 1 ? gc + 1 : gc;
            int32_t r0 = gr > 0 ? gr - 1 : gr, r1 = gr < GRID - 1 ? gr + 1 : gr;
            curv = s_k[gr * GRID + gc];
            gx   = (s_h[gr * GRID + c1] - s_h[gr * GRID + c0]) * LEVEL_H / (float)(c1 - c0);
            gy   = (s_h[r1 * GRID + gc] - s_h[r0 * GRID + gc]) * LEVEL_H / (float)(r1 - r0);
        }
        o->pos[0] = p[k][0];
        o->pos[1] = p[k][1];
        o->pos[2] = p[k][2];
        o->pos[3] = order;
        o->nrm[0] = nn[0];
        o->nrm[1] = nn[1];
        o->nrm[2] = nn[2];
        o->nrm[3] = col[2] > 6.5f ? s_road_class : curv; /* a strip: its class */
        if (col[2] >= 0.5f)
        {
            o->col[0] = ref ? ref[k] : col[0];
            o->col[1] = ref2 ? ref2[k] : col[1];
        }
        else
        {
            o->col[0] = gx;
            o->col[1] = gy;
        }
        o->col[2] = col[2];
        o->col[3] = LAND_INDEX;
    }
    return 0;
}

static int put_tri(RMesh *m, const float p[3][3], float order, const float col[3], int flat)
{
    return put_tri_r2(m, p, NULL, order, col, NULL, NULL, flat);
}

/*  A quad standing on an edge: the top points t0, t1 and the points b0,
 *  b1 under them.  `r` and `s` are the material's reference heights at
 *  the two ends.  The quad may twist -- t0 above b0 and t1 below b1 --
 *  and is still two triangles on the same four points, so every edge it
 *  makes is shared. */
int put_wall_r2(RMesh *m, const float t0[3], const float t1[3], const float b0[3], const float b1[3], const float nrm[3], float order, const float col[3], float r0, float r1, float s0, float s1)
{
    float p[3][3], ref[3], ref2[3];
    memcpy(p[0], t0, sizeof p[0]);
    memcpy(p[1], t1, sizeof p[1]);
    memcpy(p[2], b1, sizeof p[2]);
    ref[0]  = r0;
    ref[1]  = r1;
    ref[2]  = r1;
    ref2[0] = s0;
    ref2[1] = s1;
    ref2[2] = s1;
    if (put_tri_r2(m, p, nrm, order, col, ref, ref2, 0) != 0)
        return -1;
    memcpy(p[0], t0, sizeof p[0]);
    memcpy(p[1], b1, sizeof p[1]);
    memcpy(p[2], b0, sizeof p[2]);
    ref[0]  = r0;
    ref[1]  = r1;
    ref[2]  = r0;
    ref2[0] = s0;
    ref2[1] = s1;
    ref2[2] = s0;
    return put_tri_r2(m, p, nrm, order, col, ref, ref2, 0);
}

int put_wall_r(RMesh *m, const float t0[3], const float t1[3], const float b0[3], const float b1[3], const float nrm[3], float order, const float col[3], float r0, float r1)
{
    return put_wall_r2(m, t0, t1, b0, b1, nrm, order, col, r0, r1, col[1], col[1]);
}

int put_wall(RMesh *m, const float t0[3], const float t1[3], const float b0[3], const float b1[3], const float nrm[3], float order, const float col[3])
{
    return put_wall_r(m, t0, t1, b0, b1, nrm, order, col, t0[2], t1[2]);
}

/*  The diagonal a tile's top is cut on: a tile with one odd corner keeps
 *  a flat triangle on the other three (tools/terrain_shapes.py), so the
 *  cut avoids the odd corner; the saddle is cut NE-SW; a plane is planar
 *  either way. */
int cut_ne_sw(int32_t code)
{
    uint8_t mask   = CODE_MASK[code];
    int     raised = 0, odd = -1, k;
    for (k = 0; k < 4; ++k)
        if (mask & (1u << k))
            ++raised;
    if (raised == 1 || raised == 3)
        for (k = 0; k < 4; ++k)
            if (((mask >> k) & 1u) == (raised == 1 ? 1u : 0u))
                odd = k;
    return (code == 13) || odd == NW || odd == SE;
}

/*  The two triangles of a tile's top, cut on the diagonal the sprite is
 *  cut on. */
int put_top(RMesh *m, const float p[4][3], int32_t code, float order, const float col[3], int flat)
{
    int   ne_sw = cut_ne_sw(code);
    float tri[3][3];
    if (ne_sw)
    {
        memcpy(tri[0], p[NE], sizeof tri[0]);
        memcpy(tri[1], p[SE], sizeof tri[1]);
        memcpy(tri[2], p[SW], sizeof tri[2]);
        if (put_tri(m, tri, order, col, flat) != 0)
            return -1;
        memcpy(tri[0], p[NW], sizeof tri[0]);
        memcpy(tri[1], p[NE], sizeof tri[1]);
        memcpy(tri[2], p[SW], sizeof tri[2]);
        return put_tri(m, tri, order, col, flat);
    }
    memcpy(tri[0], p[NW], sizeof tri[0]);
    memcpy(tri[1], p[NE], sizeof tri[1]);
    memcpy(tri[2], p[SE], sizeof tri[2]);
    if (put_tri(m, tri, order, col, flat) != 0)
        return -1;
    memcpy(tri[0], p[NW], sizeof tri[0]);
    memcpy(tri[1], p[SE], sizeof tri[1]);
    memcpy(tri[2], p[SW], sizeof tri[2]);
    return put_tri(m, tri, order, col, flat);
}

/*  The mean colour of a sprite's opaque pixels through the phase-0
 *  palette, or `fallback` when the level has no such tile. */
void tile_colour(const RAtlas *a, const RAtlasLevel *l, int32_t tile, float out[3], const float fallback[3])
{
    const RTile *t = atlas_tile(l, l->id_base + tile);
    long         r = 0, g = 0, b = 0, n = 0;
    int32_t      x, y;
    if (t)
        for (y = 0; y < (int32_t)t->h; ++y)
            for (x = 0; x < (int32_t)t->w; ++x)
            {
                uint8_t v = l->indices[((size_t)t->y + (size_t)y) *
                                           (size_t)l->w +
                                       (size_t)t->x + (size_t)x];
                if ((int32_t)v == l->transparent)
                    continue;
                r += a->palette0[v][0];
                g += a->palette0[v][1];
                b += a->palette0[v][2];
                ++n;
            }
    if (!n)
    {
        out[0] = fallback[0];
        out[1] = fallback[1];
        out[2] = fallback[2];
        return;
    }
    out[0] = (float)r / (float)n / 255.0f;
    out[1] = (float)g / (float)n / 255.0f;
    out[2] = (float)b / (float)n / 255.0f;
}

void grid_point(int32_t col, int32_t row, int k, float z, float out[3])
{
    out[0] = (float)(col + ((k == NE || k == SE) ? 1 : 0));
    out[1] = (float)(row + ((k == SW || k == SE) ? 1 : 0));
    out[2] = z;
}

/*  Set while a highway deck is being lofted: the deck is a two-tile
 *  band, not a one-tile strip, and nothing else about the loft changes
 *  for it yet. */
int   s_hiway;
float s_hiway_ramp0, s_hiway_ramp1; /* the tile lengths the deck ramps down over at each end */

/*  How far the deck rides above the ground, in altitude levels.  Spec
 *  7.2 puts the road surface 7.5 to 8 m up, and 7.4.4 makes the step
 *  between one deck level and the next 7.4 m: L1 is 7.5 m over ground,
 *  L2 = L1 + 7.4, L3 = L2 + 7.4.  A level IS that step, so the lower
 *  deck stands at exactly one and an upper deck will stand at two. */
/*  The box girder's depth under the deck, the parapet's height above it
 *  and the bent's span along -- two tiles, the spec's 30 m. */
/*  The cap is 1.5 m deep along the deck (7.2) and the columns 1.8 m
 *  across, against the spec's 15 m tile.
 *
 *  Two departures from 7.2, both taken from the original's own art,
 *  which is the reference for how a raised highway reads.  It stands a
 *  column under each edge of the deck every TILE, not a hammerhead on
 *  the centreline every two: measured off the sprites, the columns are
 *  16 px apart in x, which is one tile step along a deck, about 4 px
 *  wide, and they drop about 5 px below the deck's near edge.  A
 *  hammerhead on the centreline is hidden by the deck it carries at
 *  this camera -- the near edge projects eight pixels further down the
 *  screen than the centreline does -- which is why the first build read
 *  as a deck lying on the ground (the user: "why is it so low???",
 *  "the original sprites had columns"). */
