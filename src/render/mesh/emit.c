/*  mesh/emit.c: the emit primitives: triangles, walls, tops and boxes.
 *  See mesh/internal.h. */
#include "mesh/internal.h"
#include "pipeline.h"

#include <stdarg.h>
#include <stdio.h>

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
 *  ground material reads of the topology at its corner.  That is the
 *  curvature in the normal's fourth component.  It is also the height
 *  field's gradient, in world units, in the color's first two.  The
 *  gradient is zero when `flat` is set, for a leveled pad.  A wall
 *  vertex carries the material's reference heights instead, `ref` and
 *  `ref2` per vertex.  They are the ground the sediment's layers follow,
 *  the table and the bed the water column deepens between.  A degenerate
 *  triangle is dropped. */
/*  Who drew what, per tile: up to eight (function, material) pairs a
 *  tile, in the order they first drew there.  The names are string
 *  literals from __func__, so the table holds pointers and costs nothing
 *  to fill.  It is written as the mesh is built and read by the
 *  inspector.  A rebuild starts it again. */
static void box_grow(float b[6], const float p[3][3])
{
    int k, a;
    for (k = 0; k < 3; ++k)
        for (a = 0; a < 3; ++a)
        {
            if (p[k][a] < b[a])
                b[a] = p[k][a];
            if (p[k][a] > b[3 + a])
                b[3 + a] = p[k][a];
        }
}

/*  Who is drawing.  The shape layer (mesh/shape.h) holds the answer: a
 *  producer opens a shape, draws into it and closes it.  Every triangle
 *  emitted meanwhile is filed under it.  A boundary worked out from the
 *  call site, the material and how near the last triangle fell merges
 *  unrelated things.  It also splits single ones, which is why nothing
 *  here guesses. */
static int s_record; /* a build is under way: the emitter files what it draws */

void mesh_record(int on)
{
    s_record = on;
    shape_record(on);
}

int mesh_comp_edges_key(uint32_t a, uint32_t b)
{
    return a == b || shape_is_ancestor(b, a);
}

#define ORIGINS_PER_TILE 24
static struct
{
    const char *who[ORIGINS_PER_TILE];
    const char *where[ORIGINS_PER_TILE]; /* the file and line the call sits on */
    float       mat[ORIGINS_PER_TILE];
    uint32_t    count[ORIGINS_PER_TILE];
    float       box[ORIGINS_PER_TILE][6]; /* what it drew here: min x,y,z then max x,y,z */
    int         n;
} s_origin[R_MAP * R_MAP];

void mesh_origins_reset(void)
{
    memset(s_origin, 0, sizeof s_origin);
    shape_reset();
}

void mesh_origins_clear_wanted(void)
{
    int32_t col, row;
    for (row = 0; row < R_MAP; ++row)
        for (col = 0; col < R_MAP; ++col)
            if (mesh_want_tile(col, row))
                s_origin[row * R_MAP + col].n = 0;
}

static void origin_note(const char *where, const char *who, const float p[3][3], float mat)
{
    float   cx = (p[0][0] + p[1][0] + p[2][0]) / 3.0f, cy = (p[0][1] + p[1][1] + p[2][1]) / 3.0f;
    int32_t col = (int32_t)floorf(cx), row = (int32_t)floorf(cy);
    int     k;
    if (!who || col < 0 || row < 0 || col >= R_MAP || row >= R_MAP)
        return;
    {
        int32_t i = row * R_MAP + col;
        for (k = 0; k < s_origin[i].n; ++k)
            if (s_origin[i].who[k] == who && s_origin[i].mat[k] == mat)
            {
                ++s_origin[i].count[k];
                box_grow(s_origin[i].box[k], p);
                return;
            }
        if (s_origin[i].n >= ORIGINS_PER_TILE)
            return;
        k                     = s_origin[i].n++;
        s_origin[i].who[k]    = who;
        s_origin[i].where[k]  = where;
        s_origin[i].mat[k]    = mat;
        s_origin[i].count[k]  = 1;
        s_origin[i].box[k][0] = s_origin[i].box[k][1] = s_origin[i].box[k][2] = 1e9f;
        s_origin[i].box[k][3] = s_origin[i].box[k][4] = s_origin[i].box[k][5] = -1e9f;
        box_grow(s_origin[i].box[k], p);
    }
}

/*  The topmost thing drawn on a tile whose footprint holds the point: a
 *  lamp, a signal, a wall, a strip.  Whatever stands highest there.
 *  Everything the mesh draws is in this table.  This is the only handle
 *  on the things no table of the network's knows about. */
int mesh_origin_pick(int32_t col, int32_t row, float wx, float wy, const char **who, const char **where, float *mat, uint32_t *tris, float box[6])
{
    int32_t i = row * R_MAP + col;
    int     k, best = -1;
    if (col < 0 || row < 0 || col >= R_MAP || row >= R_MAP)
        return -1;
    for (k = 0; k < s_origin[i].n; ++k)
    {
        const float *b  = s_origin[i].box[k];
        float        mk = s_origin[i].mat[k];
        if (wx < b[0] - 0.02f || wx > b[3] + 0.02f || wy < b[1] - 0.02f || wy > b[4] + 0.02f)
            continue;
        if (mk > 19.4f || (mk > 14.5f && mk < 15.5f))
            continue; /* the map's zone tint, the show-curves highlight and the vehicles are not things to point at */
        if (best < 0)
        {
            best = k;
            continue;
        }
        {
            /*  The topmost wins.  But the ground and what lies ON it are
             *  a hair apart, and the thing drawn on the ground is what
             *  the eye is on.  A junction's fill over the pad it sits
             *  on, a marking over the fill. */
            const float *bb   = s_origin[i].box[best];
            int          hair = fabsf(b[5] - bb[5]) < 0.08f;
            /*  The topmost.  And where two lie a hair apart.  A
             *  junction's fill and the markings painted on it.  The one
             *  that made the most of the geometry, which is the thing
             *  itself. */
            if (hair ? s_origin[i].count[k] > s_origin[i].count[best] : b[5] > bb[5])
                best = k;
        }
    }
    if (best < 0)
        return -1;
    if (who)
        *who = s_origin[i].who[best];
    if (where)
        *where = s_origin[i].where[best];
    if (mat)
        *mat = s_origin[i].mat[best];
    if (tris)
        *tris = s_origin[i].count[best];
    if (box)
        memcpy(box, s_origin[i].box[best], sizeof s_origin[i].box[best]);
    return 0;
}

int mesh_origins(int32_t col, int32_t row, char *out, size_t n)
{
    int32_t i    = row * R_MAP + col;
    size_t  used = 0;
    int     k;
    if (!out || !n || col < 0 || row < 0 || col >= R_MAP || row >= R_MAP)
        return -1;
    out[0] = 0;
    for (k = 0; k < s_origin[i].n && used + 1 < n; ++k)
        used += (size_t)snprintf(out + used, n - used, "   %-22s material %-5g %u triangles\n", s_origin[i].who[k], (double)s_origin[i].mat[k], s_origin[i].count[k]);
    if (s_origin[i].n >= ORIGINS_PER_TILE && used + 1 < n)
        snprintf(out + used, n - used, "   ... the record is full: this tile carries more than it can hold\n");
    return s_origin[i].n;
}

/*  Every face has a color: `col` is r, g and the material, and the
 *  material is what the whole pipeline reads a face by.  `nrm`, `ref`
 *  and `ref2` are optional.  `col` is not. */
int put_tri_r2_at(const char *where, const char *who, RMesh *m, const float p[3][3], const float *nrm, float order, const float col[3], const float *ref, const float *ref2, int flat)
{
    /*  Only while the mesh is being built, and never in the grading
     *  pass: the traffic is drawn again on every frame through the same
     *  emitter.  Its boxes would pile up in the table for ever. */
    if (s_record && s_pass != 1)
        origin_note(where, who, p, col[2]);
    /*  The first pass grades the ground and draws nothing: every emitter
     *  ends here, and what it would have built the second pass rebuilds
     *  from scratch.  A third of a build's time went into geometry that
     *  was thrown away. */
    if (s_pass == 1)
        return 0;
    if (s_incr_on && !mesh_want_xy(p[0][0], p[0][1]))
        return 0; /* an edit's build: the triangle lands in a chunk that stands (keyed as the partition keys it) */
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
    if (!m->to_water)
    {
        /*  The triangle about to be written is land triangle n_land/3:
         *  record which component made it.  So the inspector can point
         *  at a triangle and be told.  Outside a build, a frame's
         *  traffic, there is nothing to record. */
        uint32_t t = m->n_land / 3u;
        if (t >= m->cap_tri_comp)
        {
            uint32_t  cap = m->cap_tri_comp ? m->cap_tri_comp * 2u : 65536u;
            uint32_t *nc;
            while (cap <= t)
                cap *= 2u;
            nc = (uint32_t *)realloc(m->tri_comp, cap * sizeof *nc);
            if (!nc)
                return -1;
            m->tri_comp     = nc;
            m->cap_tri_comp = cap;
        }
        m->tri_comp[t] = shape_claim(where, who, p, col ? col[2] : -1.0f);
    }
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
        o->nrm[3] = col[2] > 6.5f ? m->strip_class : curv; /* a strip: its class */
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
 *  the two ends.  The quad may twist, t0 above b0 and t1 below b1.  Is
 *  still two triangles on the same four points, so every edge it makes
 *  is shared. */
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

/*  The diagonal a tile's top is cut on, which the script settles for
 *  every slope code (`fold_ne_sw`). */
int cut_ne_sw(int32_t code)
{
    return script_bytes("fold_ne_sw")[code];
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

/*  The mean color of a sprite's opaque pixels through the phase-0
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

/*  Set while a band slab is being lofted.  The slab is a two-tile band,
 *  not a one-tile strip, and nothing else about the loft changes for it
 *  yet. */

/*  How far the slab rides above the ground, in altitude levels.  Spec
 *  7.2 puts the line surface 7.5 to 8 m up. 7.4.4 makes the step between
 *  one slab level and the next 7.4 m.  L1 is 7.5 m over ground, L2 is L1
 *  + 7.4, and L3 is L2 + 7.4.  A level IS that step, so the lower slab
 *  stands at exactly one and an upper slab will stand at two. */
/*  The box girder's depth under the slab, the parapet's height above it
 *  and the bent's span along: two tiles, the spec's 30 m. */
/*  The cap is 1.5 m deep along the slab (7.2) and the columns 1.8 m
 *  across, against the spec's 15 m tile.  Two departures from 7.2, both
 *  taken from the original's own art, which is the reference for how a
 *  raised band reads.  It stands a column under each edge of the slab
 *  every TILE, not a hammerhead on the centerline every two.  Measured
 *  off the sprites, the columns are 16 px apart in x.  This is one tile
 *  step along a slab, about 4 px wide, and they drop about 5 px below
 *  the slab's near edge.  A hammerhead on the centerline is hidden by
 *  the slab it carries at this camera.  The near edge projects eight
 *  pixels further down the screen than the centerline does.  Which is
 *  why the first build read as a slab lying on the ground. */

/*  A box with a clipped skirt, and the vehicle default.  These came out
 *  of line.c, where they had accreted at the bottom under the line
 *  algorithm.  They are emit primitives, a prism, its four sides and its
 *  top.  This is the file that owns those.  The line code and the
 *  traffic both draw with them. */
/*  A prism clipped to the tile grid, each piece in the depth slot of the
 *  tile under it.  A car had carried the order of the tile under its
 *  center.  The part of it over the next tile toward the viewer then lay
 *  under that tile's ground.  It stayed there until the center crossed,
 *  when the whole car stood up at once.  `order` is the center tile's
 *  slot with the fraction the pieces keep above each tile's ground. */
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

/*  ------------------------------------------------------------------
 *  Pointing at a triangle.  The inspector asks what is under the pointer.
 *  the answer is a land triangle, and through it the component that drew
 *  it.  A tile index makes the question cheap: the triangles whose middle
 *  lies in a tile, in a linked list a build long.
 *  ------------------------------------------------------------------ */
static int32_t     *s_tri_head;    /* per tile: the first triangle, or -1 */
static int32_t     *s_tri_next;    /* per triangle: the next in its tile */
static uint32_t     s_tri_index_n; /* the triangles it was built for */
static const RMesh *s_tri_index_m;

static void tri_index(const RMesh *m)
{
    uint32_t ntri = m->n_land / 3u, t;
    int32_t  i;
    if (s_tri_index_m == m && s_tri_index_n == ntri)
        return;
    if (!s_tri_head)
    {
        s_tri_head = (int32_t *)malloc((size_t)R_MAP * R_MAP * sizeof *s_tri_head);
        if (!s_tri_head)
            return;
    }
    {
        int32_t *nn = (int32_t *)realloc(s_tri_next, (ntri ? ntri : 1u) * sizeof *nn);
        if (!nn)
            return;
        s_tri_next = nn;
    }
    for (i = 0; i < R_MAP * R_MAP; ++i)
        s_tri_head[i] = -1;
    for (t = 0; t < ntri; ++t)
    {
        const RMeshVert *v  = &m->land[3u * t];
        float            cx = (v[0].pos[0] + v[1].pos[0] + v[2].pos[0]) / 3.0f;
        float            cy = (v[0].pos[1] + v[1].pos[1] + v[2].pos[1]) / 3.0f;
        int32_t          c = (int32_t)floorf(cx), r = (int32_t)floorf(cy), k;
        s_tri_next[t] = -1;
        if (c < 0 || r < 0 || c >= R_MAP || r >= R_MAP)
            continue;
        k             = r * R_MAP + c;
        s_tri_next[t] = s_tri_head[k];
        s_tri_head[k] = (int32_t)t;
    }
    s_tri_index_m = m;
    s_tri_index_n = ntri;
}

int mesh_tris_at(const RMesh *m, int32_t col, int32_t row, uint32_t *out, int max)
{
    int32_t t;
    int     n = 0;
    if (!m || !out || col < 0 || row < 0 || col >= R_MAP || row >= R_MAP)
        return 0;
    tri_index(m);
    if (!s_tri_head)
        return 0;
    for (t = s_tri_head[row * R_MAP + col]; t >= 0 && n < max; t = s_tri_next[t])
        out[n++] = (uint32_t)t;
    return n;
}

void mesh_tri_get(const RMesh *m, uint32_t t, float p[3][3], uint32_t *comp)
{
    int k, a;
    for (k = 0; k < 3; ++k)
        for (a = 0; a < 3; ++a)
            p[k][a] = m->land[3u * t + (uint32_t)k].pos[a];
    if (comp)
        *comp = (m->tri_comp && 3u * t < m->n_land) ? m->tri_comp[t] : SHAPE_NONE;
}

/*  The component's outline: the edges of its triangles that one triangle
 *  alone holds.
 *
 *  Welded in THREE dimensions.  A component stands over itself, a slab
 *  and the spur coming down under it, a strip and the skirt hanging off
 *  its edge.  So two edges at one place on the map at different heights
 *  are not the same edge.  A face standing vertically has no area on the
 *  map at all.  Judged in plan, the first pair cancel each other and the
 *  second cancels nothing.  This is how an outline comes to show every
 *  triangle it is made of.
 *
 *  Each edge is first cut at any vertex of the component lying on it.
 *  So the T left where a clip split one side and not the other closes.
 *  The long edge becomes the two halves its neighbor already has, and
 *  both cancel.  Written as pairs of points, SIX floats to a segment. */

typedef struct
{
    float p[3];
} CoVert;

typedef struct
{
    uint32_t a, b;
    int      n;
} CoEdge;

static CoVert  *s_cov;
static int32_t *s_cov_hash;
static int      s_cov_n, s_cov_cap, s_cov_hn;
static CoEdge  *s_coe;
static int32_t *s_coe_hash;

/*  The emitter's own scratch.  It is kept between builds, because every
 *  build wants the same tables at about the same size.  The triangle
 *  index by tile, and the two hashes the coplanar check walks with.
 *  Given back when the program is done with meshes altogether. */
void mesh_emit_free(void)
{
    free(s_tri_head), s_tri_head = NULL;
    free(s_tri_next), s_tri_next = NULL;
    free(s_cov_hash), s_cov_hash = NULL;
    free(s_coe_hash), s_coe_hash = NULL;
}
static int      s_coe_n, s_coe_cap, s_coe_hn;

static int gix_weld_grid = -1, gix_thin_grid = -1, gix_thin_tol = -1, gix_weld_split_end = -1;
#define CO_GRID net_geo(&gix_weld_grid, "weld_grid") /* the weld's grid, in parts of a tile */

static uint32_t co_mix(uint32_t h, uint32_t v)
{
    return (h ^ v) * 16777619u;
}

static int co_grow(void **buf, int *cap, size_t elem, int want)
{
    int   nc = *cap ? *cap : 256;
    void *ne;
    while (nc < want)
        nc *= 2;
    if (nc == *cap)
        return 0;
    ne = realloc(*buf, (size_t)nc * elem);
    if (!ne)
        return -1;
    *buf = ne;
    *cap = nc;
    return 0;
}

/*  The vertex at p, added if it is new.  The lookup walks the
 *  twenty-seven cells around its own.  So two vertices a float's breadth
 *  apart, but either side of a cell line, still come back as one. */
static int32_t co_vert_g(const float p[3], float grid)
{
    int32_t q[3], d0, d1, d2;
    int     k;
    for (k = 0; k < 3; ++k)
        q[k] = (int32_t)floorf(p[k] * grid + 0.5f);
    for (d0 = -1; d0 <= 1; ++d0)
        for (d1 = -1; d1 <= 1; ++d1)
            for (d2 = -1; d2 <= 1; ++d2)
            {
                uint32_t h = co_mix(co_mix(co_mix(2166136261u, (uint32_t)(q[0] + d0)),
                                           (uint32_t)(q[1] + d1)),
                                    (uint32_t)(q[2] + d2)) &
                             (uint32_t)(s_cov_hn - 1);
                while (s_cov_hash[h] >= 0)
                {
                    const CoVert *v = &s_cov[s_cov_hash[h]];
                    if (fabsf(v->p[0] - p[0]) < 1.0f / grid && fabsf(v->p[1] - p[1]) < 1.0f / grid &&
                        fabsf(v->p[2] - p[2]) < 1.0f / grid)
                        return s_cov_hash[h];
                    h = (h + 1u) & (uint32_t)(s_cov_hn - 1);
                }
            }
    {
        uint32_t h = co_mix(co_mix(co_mix(2166136261u, (uint32_t)q[0]), (uint32_t)q[1]), (uint32_t)q[2]) &
                     (uint32_t)(s_cov_hn - 1);
        while (s_cov_hash[h] >= 0)
            h = (h + 1u) & (uint32_t)(s_cov_hn - 1);
        if (co_grow((void **)&s_cov, &s_cov_cap, sizeof *s_cov, s_cov_n + 1) != 0)
            return -1;
        memcpy(s_cov[s_cov_n].p, p, 3 * sizeof(float));
        s_cov_hash[h] = s_cov_n;
        return s_cov_n++;
    }
}

static int32_t co_vert(const float p[3])
{
    return co_vert_g(p, CO_GRID);
}

/*  The edge between two vertices, counted.  `add` files it.  Otherwise
 *  it is only asked after, which is how a piece of a cut edge learns
 *  that the triangle next door already holds it. */
static CoEdge *co_edge(int32_t a, int32_t b, int add)
{
    uint32_t h;
    int32_t  t;
    if (a > b)
        t = a, a = b, b = t;
    h = co_mix(co_mix(2166136261u, (uint32_t)a), (uint32_t)b) & (uint32_t)(s_coe_hn - 1);
    while (s_coe_hash[h] >= 0)
    {
        CoEdge *e = &s_coe[s_coe_hash[h]];
        if (e->a == (uint32_t)a && e->b == (uint32_t)b)
        {
            if (add)
                ++e->n;
            return e;
        }
        h = (h + 1u) & (uint32_t)(s_coe_hn - 1);
    }
    if (!add)
        return NULL;
    if (co_grow((void **)&s_coe, &s_coe_cap, sizeof *s_coe, s_coe_n + 1) != 0)
        return NULL;
    s_coe[s_coe_n].a = (uint32_t)a;
    s_coe[s_coe_n].b = (uint32_t)b;
    s_coe[s_coe_n].n = 1;
    s_coe_hash[h]    = s_coe_n;
    return &s_coe[s_coe_n++];
}

static int co_tables(int want)
{
    int hn = 1024;
    while (hn < want * 4)
        hn *= 2;
    if (hn != s_cov_hn)
    {
        int32_t *nh = (int32_t *)realloc(s_cov_hash, (size_t)hn * sizeof *nh);
        if (!nh)
            return -1;
        s_cov_hash = nh;
        s_cov_hn   = hn;
    }
    if (hn != s_coe_hn)
    {
        int32_t *nh = (int32_t *)realloc(s_coe_hash, (size_t)hn * sizeof *nh);
        if (!nh)
            return -1;
        s_coe_hash = nh;
        s_coe_hn   = hn;
    }
    memset(s_cov_hash, 0xFF, (size_t)s_cov_hn * sizeof *s_cov_hash);
    memset(s_coe_hash, 0xFF, (size_t)s_coe_hn * sizeof *s_coe_hash);
    s_cov_n = s_coe_n = 0;
    return 0;
}

/*  Where a vertex sits along an edge, or -1 if it is not on it.  It is
 *  the parameter of its projection, taken only when it lies within a
 *  weld of the line and clear of both ends. */
static float co_on_edge(const float a[3], const float b[3], const float p[3])
{
    float d[3], w[3], len2 = 0.0f, t = 0.0f, off = 0.0f, end;
    int   k;
    for (k = 0; k < 3; ++k)
    {
        d[k] = b[k] - a[k];
        w[k] = p[k] - a[k];
        len2 += d[k] * d[k];
    }
    if (len2 < 1e-12f)
        return -1.0f;
    for (k = 0; k < 3; ++k)
        t += w[k] * d[k];
    t /= len2;
    end = net_geo(&gix_weld_split_end, "weld_split_end");
    if (t <= end || t >= 1.0f - end)
        return -1.0f;
    for (k = 0; k < 3; ++k)
    {
        float e = w[k] - t * d[k];
        off += e * e;
    }
    if (off > (2.0f / CO_GRID) * (2.0f / CO_GRID))
        return -1.0f;
    return t;
}

/*  The outline's pieces, chained and thinned.  Where exactly two pieces
 *  meet at a point the chain runs on through it.  Each chain is reduced
 *  to the points that keep it within a fiftieth of a tile of itself
 *  (Douglas-Peucker).  So the thousand stations along a band's edge come
 *  out as the few edges the eye can tell apart.  The ends are welded on
 *  a coarser grid than the triangles were.  This is because a cut
 *  piece's end is an interpolation and not the vertex it stands for. */
#define THIN_GRID net_geo(&gix_thin_grid, "thin_grid")
#define THIN_TOL  net_geo(&gix_thin_tol, "thin_tol")
typedef struct
{
    int32_t a, b;
    int     done;
} ThinSeg;
typedef struct
{
    int32_t deg, s[2];
} ThinVert;
static float    *s_raw; /* the pieces as found: six floats each */
static int       s_raw_n, s_raw_cap;
static ThinSeg  *s_ts;
static ThinVert *s_tv;
static int32_t  *s_chain;
static uint8_t  *s_keep;
static int32_t  *s_stk;
static int       s_ts_cap, s_tv_cap, s_chain_cap, s_keep_cap, s_stk_cap;

static int raw_add(const float p0[3], const float p1[3])
{
    if (co_grow((void **)&s_raw, &s_raw_cap, 6 * sizeof(float), s_raw_n + 1) != 0)
        return -1;
    memcpy(s_raw + 6 * s_raw_n, p0, 3 * sizeof(float));
    memcpy(s_raw + 6 * s_raw_n + 3, p1, 3 * sizeof(float));
    ++s_raw_n;
    return 0;
}

/*  The distance of p from the segment a-b in tiles, a level counted at
 *  its world height. */
static float thin_dist(const float *a, const float *b, const float *p)
{
    float d[3], w[3], len2 = 0.0f, t = 0.0f, off = 0.0f;
    int   k;
    for (k = 0; k < 3; ++k)
    {
        float s = k == 2 ? LEVEL_H : 1.0f;
        d[k]    = (b[k] - a[k]) * s;
        w[k]    = (p[k] - a[k]) * s;
        len2 += d[k] * d[k];
        t += w[k] * d[k];
    }
    t = len2 > 1e-12f ? t / len2 : 0.0f;
    if (t < 0.0f)
        t = 0.0f;
    if (t > 1.0f)
        t = 1.0f;
    for (k = 0; k < 3; ++k)
    {
        float e = w[k] - t * d[k];
        off += e * e;
    }
    return sqrtf(off);
}

/*  One chain of vertices, thinned and written out from segment `out`. */
static int chain_out(const int32_t *pt, int np, float *segs, int max_seg, int out)
{
    int sp = 0, i, last;
    if (np < 2)
        return out;
    if (co_grow((void **)&s_keep, &s_keep_cap, sizeof *s_keep, np) != 0 || co_grow((void **)&s_stk, &s_stk_cap, sizeof *s_stk, 2 * np + 2) != 0)
        return out;
    memset(s_keep, 0, (size_t)np);
    s_keep[0] = s_keep[np - 1] = 1;
    s_stk[sp++] = 0;
    s_stk[sp++] = np - 1;
    while (sp)
    {
        int   j = s_stk[--sp], i0 = s_stk[--sp], far = -1;
        float fd = THIN_TOL;
        for (i = i0 + 1; i < j; ++i)
        {
            float d = thin_dist(s_cov[pt[i0]].p, s_cov[pt[j]].p, s_cov[pt[i]].p);
            if (d > fd)
                fd = d, far = i;
        }
        if (far < 0)
            continue;
        s_keep[far] = 1;
        s_stk[sp++] = i0;
        s_stk[sp++] = far;
        s_stk[sp++] = far;
        s_stk[sp++] = j;
    }
    for (last = 0, i = 1; i < np && out < max_seg; ++i)
    {
        if (!s_keep[i])
            continue;
        memcpy(segs + 6 * out, s_cov[pt[last]].p, 3 * sizeof(float));
        memcpy(segs + 6 * out + 3, s_cov[pt[i]].p, 3 * sizeof(float));
        ++out;
        last = i;
    }
    return out;
}

static int outline_thin(float *segs, int max_seg)
{
    int n = s_raw_n, i, out = 0, pass;
    if (n < 1 || co_tables(2 * n) != 0 || co_grow((void **)&s_ts, &s_ts_cap, sizeof *s_ts, n) != 0)
        return 0;
    for (i = 0; i < n; ++i)
    {
        s_ts[i].a    = co_vert_g(s_raw + 6 * i, THIN_GRID);
        s_ts[i].b    = co_vert_g(s_raw + 6 * i + 3, THIN_GRID);
        s_ts[i].done = s_ts[i].a < 0 || s_ts[i].b < 0 || s_ts[i].a == s_ts[i].b;
    }
    if (co_grow((void **)&s_tv, &s_tv_cap, sizeof *s_tv, s_cov_n) != 0 || co_grow((void **)&s_chain, &s_chain_cap, sizeof *s_chain, n + 1) != 0)
        return 0;
    memset(s_tv, 0, (size_t)s_cov_n * sizeof *s_tv);
    for (i = 0; i < n; ++i)
    {
        int e;
        if (s_ts[i].done)
            continue;
        for (e = 0; e < 2; ++e)
        {
            ThinVert *v = &s_tv[e ? s_ts[i].b : s_ts[i].a];
            if (v->deg < 2)
                v->s[v->deg] = i;
            ++v->deg;
        }
    }
    /*  Chains from every open end or fork first, then whatever is left,
     *  which is the closed loops. */
    for (pass = 0; pass < 2 && out < max_seg; ++pass)
        for (i = 0; i < n && out < max_seg; ++i)
        {
            int32_t v, s = i, np = 0;
            if (s_ts[i].done)
                continue;
            if (pass == 0 && s_tv[s_ts[i].a].deg == 2 && s_tv[s_ts[i].b].deg == 2)
                continue;
            v            = (pass == 0 && s_tv[s_ts[i].a].deg == 2) ? s_ts[i].b : s_ts[i].a;
            s_chain[np++] = v;
            for (;;)
            {
                int32_t w    = s_ts[s].a == v ? s_ts[s].b : s_ts[s].a;
                s_ts[s].done = 1;
                s_chain[np++] = w;
                if (s_tv[w].deg != 2)
                    break;
                s = s_tv[w].s[0] == s ? s_tv[w].s[1] : s_tv[w].s[0];
                if (s_ts[s].done)
                    break; /* a loop closed */
                v = w;
            }
            out = chain_out(s_chain, np, segs, max_seg, out);
        }
    return out;
}

int mesh_comp_edges(const RMesh *m, uint32_t comp, float *segs, int max_seg)
{
    uint32_t ntri = m ? m->n_land / 3u : 0u, t;
    int      nt = 0, i;
    if (!m || !m->tri_comp || !segs || max_seg < 1 || comp == SHAPE_NONE)
        return 0;
    for (t = 0; t < ntri; ++t)
        if (mesh_comp_edges_key(m->tri_comp[t], comp))
            ++nt;
    if (!nt)
        return 0;
    if (co_tables(nt * 3) != 0)
        return 0;
    for (t = 0; t < ntri; ++t)
    {
        int32_t id[3];
        int     k;
        if (!mesh_comp_edges_key(m->tri_comp[t], comp))
            continue;
        for (k = 0; k < 3; ++k)
        {
            id[k] = co_vert(m->land[3u * t + (uint32_t)k].pos);
            if (id[k] < 0)
                return 0;
        }
        for (k = 0; k < 3; ++k)
            if (id[k] != id[(k + 1) % 3] && !co_edge(id[k], id[(k + 1) % 3], 1))
                return 0;
    }
    /*  An edge two triangles share is inside the component.  One that
     *  stands alone is its outline: unless a vertex lies along it, when
     *  the pieces it cuts into are asked for themselves. */
    s_raw_n = 0;
    for (i = 0; i < s_coe_n; ++i)
    {
        float pa[3], pb[3], cut[16];
        int   ncut = 0, j, v, nv = s_cov_n;
        if (s_coe[i].n != 1)
            continue;
        /*  Copied, not pointed at: cutting an edge can file a vertex and
         *  move the table under us. */
        memcpy(pa, s_cov[s_coe[i].a].p, sizeof pa);
        memcpy(pb, s_cov[s_coe[i].b].p, sizeof pb);
        /*  The cut is a walk of the component's vertices, so it is left
         *  off a component too big to pay for it.  A T there shows as a
         *  line across the outline, which is a good deal better than the
         *  wait. */
        if (nv > 4000)
            nv = 0;
        for (v = 0; v < nv && ncut < (int)(sizeof cut / sizeof *cut); ++v)
        {
            float at;
            if ((uint32_t)v == s_coe[i].a || (uint32_t)v == s_coe[i].b)
                continue;
            at = co_on_edge(pa, pb, s_cov[v].p);
            if (at >= 0.0f)
                cut[ncut++] = at;
        }
        for (j = 1; j < ncut; ++j)
        {
            float key = cut[j];
            int   k   = j - 1;
            while (k >= 0 && cut[k] > key)
                cut[k + 1] = cut[k], --k;
            cut[k + 1] = key;
        }
        for (j = 0; j <= ncut; ++j)
        {
            float t0 = j == 0 ? 0.0f : cut[j - 1];
            float t1 = j == ncut ? 1.0f : cut[j];
            float p0[3], p1[3];
            int   k;
            if (t1 - t0 < 1e-4f)
                continue;
            for (k = 0; k < 3; ++k)
            {
                p0[k] = pa[k] + (pb[k] - pa[k]) * t0;
                p1[k] = pa[k] + (pb[k] - pa[k]) * t1;
            }
            if (ncut)
            {
                /*  A piece the neighbor already holds is an inside edge:
                 *  this is the T closing. */
                int32_t i0 = co_vert(p0), i1 = co_vert(p1);
                if (i0 >= 0 && i1 >= 0 && i0 != i1 && co_edge(i0, i1, 0))
                    continue;
            }
            if (raw_add(p0, p1) != 0)
                return 0;
        }
    }
    return outline_thin(segs, max_seg);
}
