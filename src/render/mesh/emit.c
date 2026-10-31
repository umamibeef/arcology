/*  mesh/emit.c: the emit primitives: triangles, walls, tops and boxes.
 *  See mesh/internal.h. */
#include "mesh/emit.h"
#include "mesh/internal.h"
#include "mesh/shapes.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*  The piece being drawn. */
static uint32_t s_piece;

uint32_t mesh_piece_key(int kind, int family, uint32_t at)
{
    return (uint32_t)kind << 28 | (uint32_t)(family & 3) << 26 | (at & 0x3FFFFFFu);
}

void mesh_piece_is(uint32_t key)
{
    s_piece = key;
}

/* ---- beyond the window --------------------------------------------------- */

/*  THE TRIANGLES A WINDOW DROPS at its border, where no chunk stands to
 *  hold their like.  A triangle laid in the window meets them across its
 *  border.  So the mesh check reads them as the world beyond, and judges
 *  the window as part of it.  Kept within BEYOND_REACH tiles of the
 *  window, each as its three corners, four floats each as a vertex holds
 *  them, and its material.
 *
 *  Each is filed under the chunk of the window nearest its first corner.
 *  So the store is keyed by place: a build replaces its own chunks'
 *  stores and leaves the others standing. */
#define BEYOND_REACH  4.0f
#define BEYOND_FLOATS 13
static float   *s_beyond[MESH_CHUNKS_CAP];
static uint32_t s_n_beyond[MESH_CHUNKS_CAP], s_cap_beyond[MESH_CHUNKS_CAP];
static int32_t  s_beyond_box[MESH_WINDOW_BOXES][4];
static int      s_beyond_on; /* how many boxes the window is, 0 for none */

void mesh_beyond_reset(void)
{
    int ch, per = r_map / MESH_CHUNK;
    s_beyond_on = mesh_window(s_beyond_box);
    for (ch = 0; ch < MESH_CHUNKS; ++ch)
        if (mesh_window_has(ch % per * MESH_CHUNK, ch / per * MESH_CHUNK))
            s_n_beyond[ch] = 0;
}

static void beyond_keep(const RMesh *m, const float p[3][3], float mat)
{
    int     k, b, near = -1, ch;
    int32_t col, row;
    float  *o;
    if (!s_beyond_on)
        return;
    for (b = 0; b < s_beyond_on && near < 0; ++b)
        for (k = 0; k < 3; ++k)
            if (p[k][0] >= (float)s_beyond_box[b][0] - BEYOND_REACH && p[k][0] <= (float)s_beyond_box[b][2] + BEYOND_REACH &&
                p[k][1] >= (float)s_beyond_box[b][1] - BEYOND_REACH && p[k][1] <= (float)s_beyond_box[b][3] + BEYOND_REACH)
                near = b;
    if (near < 0 || m->chunk_laid[mesh_chunk_of((int32_t)floorf(p[0][0]), (int32_t)floorf(p[0][1]))])
        return;
    /*  The window's chunk nearest the first corner. */
    col = (int32_t)floorf(p[0][0]), row = (int32_t)floorf(p[0][1]);
    col = col < s_beyond_box[near][0] ? s_beyond_box[near][0] : col >= s_beyond_box[near][2] ? s_beyond_box[near][2] - 1
                                                                                             : col;
    row = row < s_beyond_box[near][1] ? s_beyond_box[near][1] : row >= s_beyond_box[near][3] ? s_beyond_box[near][3] - 1
                                                                                             : row;
    ch  = mesh_chunk_of(col, row);
    if (s_n_beyond[ch] >= s_cap_beyond[ch])
    {
        uint32_t cap = s_cap_beyond[ch] ? 2u * s_cap_beyond[ch] : 256u;
        float   *nb  = (float *)realloc(s_beyond[ch], (size_t)cap * BEYOND_FLOATS * sizeof *nb);
        if (!nb)
            return;
        s_beyond[ch] = nb, s_cap_beyond[ch] = cap;
    }
    o = &s_beyond[ch][(size_t)s_n_beyond[ch]++ * BEYOND_FLOATS];
    for (k = 0; k < 3; ++k)
        o[4 * k] = p[k][0], o[4 * k + 1] = p[k][1], o[4 * k + 2] = p[k][2], o[4 * k + 3] = 0.0f;
    {
        /*  The material as a vertex holds it, so the check reads both
         *  sides of the border alike. */
        RMeshVert   v;
        const float up[3] = {0.0f, 0.0f, 1.0f};
        memset(&v, 0, sizeof v);
        mesh_vert_pack(&v, up, 0.0f, mat);
        o[12] = mesh_vert_mat(&v);
    }
}

const float *mesh_beyond(int ch, uint32_t *n)
{
    *n = s_n_beyond[ch];
    return *n ? s_beyond[ch] : NULL;
}

/* ---- emitting ---------------------------------------------------------- */

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

/*  Who drew what, per tile: up to ORIGINS_PER_TILE (function, material)
 *  pairs a tile, in the order they first drew there.  The names are
 *  string literals from __func__, so the table holds pointers and costs
 *  nothing to fill.  It is written as the mesh is built and read by the
 *  inspector.  A rebuild starts it again.
 *
 *  The records are one pool, and each tile holds a list into it.  Most
 *  tiles hold a few records, so a pool costs a fraction of a fixed
 *  table of the most a tile can hold. */
#define ORIGINS_PER_TILE 24
typedef struct
{
    const char *who;
    const char *where; /* the file and line the call sits on */
    float       mat;
    uint32_t    count;
    float       box[6]; /* what it drew here: min x,y,z then max x,y,z */
    int32_t     next;   /* the tile's next record, or -1 */
} Origin;
static Origin *s_orig;
static int32_t s_norig, s_caporig;
R_MAP_BUF(static, int32_t, s_ohead, 1) /* a tile's first record, plus 1: 0 for none */
R_MAP_BUF(static, int32_t, s_otail, 1) /* and its last */
R_MAP_BUF(static, uint8_t, s_on, 1)    /* how many it holds */

void mesh_origins_reset(void)
{
    memset(s_ohead, 0, r_map_bytes(s_ohead));
    memset(s_on, 0, r_map_bytes(s_on));
    s_norig = 0;
    shape_reset();
}

/*  The wanted tiles start again.  The pool is then packed, so a run of
 *  edits does not grow it: every other tile keeps its records, in order. */
void mesh_origins_clear_wanted(void)
{
    int32_t col, row, i, k, n = 0;
    Origin *packed;
    for (row = 0; row < r_map; ++row)
        for (col = 0; col < r_map; ++col)
            if (mesh_wants_tile(col, row))
            {
                s_ohead[row * r_map + col] = 0;
                s_on[row * r_map + col]    = 0;
            }
    if (!s_norig)
        return;
    packed = (Origin *)malloc((size_t)s_norig * sizeof *packed);
    if (!packed)
        return;
    for (i = 0; i < r_map * r_map; ++i)
    {
        int32_t first = n;
        for (k = s_ohead[i] - 1; k >= 0; k = s_orig[k].next)
        {
            packed[n]      = s_orig[k];
            packed[n].next = -1;
            if (n > first)
                packed[n - 1].next = n;
            ++n;
        }
        s_ohead[i] = n > first ? first + 1 : 0;
        s_otail[i] = n - 1;
    }
    free(s_orig);
    s_orig    = packed;
    s_norig   = n;
    s_caporig = n;
}

static void origin_note(const char *where, const char *who, const float p[3][3], float mat)
{
    float   cx = (p[0][0] + p[1][0] + p[2][0]) / 3.0f, cy = (p[0][1] + p[1][1] + p[2][1]) / 3.0f;
    int32_t col = (int32_t)floorf(cx), row = (int32_t)floorf(cy);
    int32_t i, k;
    Origin *o;
    if (!who || col < 0 || row < 0 || col >= r_map || row >= r_map)
        return;
    i = row * r_map + col;
    for (k = s_ohead[i] - 1; k >= 0; k = s_orig[k].next)
        if (s_orig[k].who == who && s_orig[k].mat == mat)
        {
            ++s_orig[k].count;
            mesh_box_grow(s_orig[k].box, p);
            return;
        }
    if (s_on[i] >= ORIGINS_PER_TILE)
        return;
    if (s_norig == s_caporig)
    {
        int32_t cap = s_caporig ? s_caporig * 2 : 65536;
        Origin *g   = (Origin *)realloc(s_orig, (size_t)cap * sizeof *g);
        if (!g)
            return;
        s_orig    = g;
        s_caporig = cap;
    }
    k         = s_norig++;
    o         = &s_orig[k];
    o->who    = who;
    o->where  = where;
    o->mat    = mat;
    o->count  = 1;
    o->next   = -1;
    o->box[0] = o->box[1] = o->box[2] = 1e9f;
    o->box[3] = o->box[4] = o->box[5] = -1e9f;
    mesh_box_grow(o->box, p);
    if (s_ohead[i])
        s_orig[s_otail[i]].next = k;
    else
        s_ohead[i] = k + 1;
    s_otail[i] = k;
    ++s_on[i];
}

int mesh_origins(int32_t col, int32_t row, char *out, size_t n)
{
    int32_t i, k;
    size_t  used = 0;
    if (!out || !n || col < 0 || row < 0 || col >= r_map || row >= r_map)
        return -1;
    i      = row * r_map + col;
    out[0] = 0;
    for (k = s_ohead[i] - 1; k >= 0 && used + 1 < n; k = s_orig[k].next)
        used += (size_t)snprintf(out + used, n - used, "   %-22s material %-5g %u triangles\n", s_orig[k].who, (double)s_orig[k].mat, s_orig[k].count);
    if (s_on[i] >= ORIGINS_PER_TILE && used + 1 < n)
        snprintf(out + used, n - used, "   ... the record is full: this tile carries more than it can hold\n");
    return s_on[i];
}

/*  One flat-shaded triangle.  Every face has a color: `col` is r, g and
 *  the material, and the material is what the whole pipeline reads a
 *  face by.  `nrm`, `ref` and `ref2` are optional.  `col` is not.
 *
 *  A ground vertex also carries what the ground material reads of the
 *  topology at its corner.  That is the curvature in the normal's fourth
 *  component.  It is also the height field's gradient, in world units,
 *  in the color's first two.  The gradient is zero when `flat` is set,
 *  for a leveled pad.  And it carries how far a cliff's lean moved the
 *  ground there (put_tri_lean_at), in the position's fourth.  A wall vertex carries the material's reference
 *  heights instead, `ref` and `ref2` per vertex.  They are the ground
 *  the sediment's layers follow, the table and the bed the water column
 *  deepens between.  A degenerate triangle is dropped. */
/*  Whether corner a comes before corner b: by x, then y, then z. */
static int corner_before(const float a[3], const float b[3])
{
    if (a[0] != b[0])
        return a[0] < b[0];
    if (a[1] != b[1])
        return a[1] < b[1];
    return a[2] < b[2];
}

int put_tri_r2_at(const char *where, const char *who, RMesh *m, const float p_in[3][3], const float *nrm, float order, const float col[3], const float *ref_in, const float *ref2_in, int flat)
{
    return put_tri_lean_at(where, who, m, p_in, nrm, order, col, ref_in, ref2_in, NULL, flat);
}

static int tri_put(const char *where, const char *who, RMesh *m, const float p_in[3][3], const float *nrm, const float (*nrm3)[3], float order, const float col[3], const float *ref_in, const float *ref2_in, const float *lean_in, int flat);

int put_tri_lean_at(const char *where, const char *who, RMesh *m, const float p_in[3][3], const float *nrm, float order, const float col[3], const float *ref_in, const float *ref2_in, const float *lean_in, int flat)
{
    return tri_put(where, who, m, p_in, nrm, NULL, order, col, ref_in, ref2_in, lean_in, flat);
}

int put_tri_n3_at(const char *where, const char *who, RMesh *m, const float p_in[3][3], const float nrm3[3][3], float order, const float col[3], const float *ref_in, const float *ref2_in)
{
    return tri_put(where, who, m, p_in, NULL, nrm3, order, col, ref_in, ref2_in, NULL, 1);
}

/*  One triangle: one normal for the face, or one for each corner. */
static int tri_put(const char *where, const char *who, RMesh *m, const float p_in[3][3], const float *nrm, const float (*nrm3)[3], float order, const float col[3], const float *ref_in, const float *ref2_in, const float *lean_in, int flat)
{
    /*  EVERY TRIANGLE FROM ITS FIRST CORNER, its winding kept.  So a
     *  surface laid from either end, or in any order, is the same
     *  triangles, and each is filed by the same corner. */
    float        p[3][3], rref[3], rref2[3], lean[3] = {0.0f, 0.0f, 0.0f}, cn[3][3];
    const float *ref = ref_in ? rref : NULL, *ref2 = ref2_in ? rref2 : NULL;
    {
        int f = corner_before(p_in[1], p_in[0]) ? 1 : 0, i;
        if (corner_before(p_in[2], p_in[f]))
            f = 2;
        for (i = 0; i < 3; ++i)
        {
            memcpy(p[i], p_in[(f + i) % 3], sizeof p[i]);
            if (ref_in)
                rref[i] = ref_in[(f + i) % 3];
            if (ref2_in)
                rref2[i] = ref2_in[(f + i) % 3];
            if (lean_in)
                lean[i] = lean_in[(f + i) % 3];
            if (nrm3)
                memcpy(cn[i], nrm3[(f + i) % 3], sizeof cn[i]);
        }
    }
    /*  Who drew it, only while the mesh is being built. */
    if (s_record)
        origin_note(where, who, p, col[2]);
    if (!mesh_window_has((int32_t)floorf(p[0][0]), (int32_t)floorf(p[0][1])))
    {
        /*  A build of a window: the triangle's chunk lies outside it. */
        if (!same_point(p[0], p[1]) && !same_point(p[1], p[2]) && !same_point(p[0], p[2]))
            beyond_keep(m, p, col ? col[2] : 0.0f);
        return 0;
    }
    float      nn[3];
    int        k;
    RMeshList *lst;
    if (same_point(p[0], p[1]) || same_point(p[1], p[2]) || same_point(p[0], p[2]))
        return 0;
    /*  The list the triangle goes to: its chunk's, by its first corner,
     *  and the chunk's range the build is laying.  A mesh of one range
     *  holds all of it there. */
    if ((m->n_rl == 0 || m->n_wl == 0) && mesh_lists_begin(m, 1, 1) != 0)
        return -1;
    {
        int ch = (m->to_water ? m->n_wl : m->n_rl) > 1 ? mesh_chunk_of((int32_t)floorf(p[0][0]), (int32_t)floorf(p[0][1])) : 0;
        lst    = m->to_water ? &m->wl[ch] : &m->rl[m->n_rl > 1 ? MESH_RANGES * ch + m->range : 0];
    }
    if (mesh_list_room(lst) != 0)
        return -1;
    /*  Which shape made the triangle, so the inspector can point at it
     *  and be told, and which piece was being drawn. */
    lst->comp[lst->n / 3u]  = m->to_water ? SHAPE_NONE : shape_claim(where, who, p, col ? col[2] : -1.0f);
    lst->piece[lst->n / 3u] = s_piece;
    /*  The tile it belongs to.  Water lies on the tile under it, and so
     *  does a triangle whose shapes name no tile. */
    {
        int32_t tile = m->to_water ? -1 : shape_tile_of(lst->comp[lst->n / 3u], p);
        if (tile == -1)
        {
            int32_t tc = (int32_t)floorf((p[0][0] + p[1][0] + p[2][0]) / 3.0f), tr = (int32_t)floorf((p[0][1] + p[1][1] + p[2][1]) / 3.0f);
            tile = tc >= 0 && tr >= 0 && tc < r_map && tr < r_map ? tr * r_map + tc : -1;
        }
        lst->tile[lst->n / 3u] = tile < 0 ? -1 : tile;
    }
    lst->order[lst->n / 3u] = order;
    if (m->to_water)
        m->n_water += 3u;
    else
        m->n_land += 3u;
    if (nrm)
        memcpy(nn, nrm, sizeof nn);
    else
        face_normal(p[0], p[1], p[2], nn);
    for (k = 0; k < 3; ++k)
    {
        RMeshVert *o  = &lst->v[lst->n++];
        int32_t    gc = (int32_t)(p[k][0] + 0.5f), gr = (int32_t)(p[k][1] + 0.5f);
        float      curv = 0.0f, gx = 0.0f, gy = 0.0f;
        if (!flat && gc >= 0 && gc < GRID && gr >= 0 && gr < GRID)
        {
            int32_t c0 = gc > 0 ? gc - 1 : gc, c1 = gc < GRID - 1 ? gc + 1 : gc;
            int32_t r0 = gr > 0 ? gr - 1 : gr, r1 = gr < GRID - 1 ? gr + 1 : gr;
            curv = s_k[gr * GRID + gc];
            gx   = (s_h[gr * GRID + c1] - s_h[gr * GRID + c0]) * LEVEL_H / (float)(c1 - c0);
            gy   = (s_h[r1 * GRID + gc] - s_h[r0 * GRID + gc]) * LEVEL_H / (float)(r1 - r0);
            /*  The shared heights do not see a cliff.  Near one, the
             *  smoothed slope reads the whole drop and browns flat grass
             *  a tile back.  There the face's own slope stands in. */
            if (s_split[gr * GRID + gc] || s_split[gr * GRID + c0] || s_split[gr * GRID + c1] ||
                s_split[r0 * GRID + gc] || s_split[r1 * GRID + gc])
            {
                float nz = nn[2] > 0.05f ? nn[2] : 0.05f;
                gx       = -nn[0] / nz;
                gy       = -nn[1] / nz;
            }
        }
        o->pos[0] = p[k][0];
        o->pos[1] = p[k][1];
        o->pos[2] = p[k][2];
        o->pos[3] = lean[k];
        if (!MAT_UPTO(col[2], MAT_GROUND))
        {
            o->paint[0] = ref ? ref[k] : col[0];
            o->paint[1] = ref2 ? ref2[k] : col[1];
        }
        else
        {
            o->paint[0] = gx;
            o->paint[1] = gy;
        }
        mesh_vert_pack(o, nrm3 ? cn[k] : nn, MAT_ABOVE(col[2], MAT_SURFACE) ? 0.0f : curv, col[2]); /* the ground's curvature, nought over it */
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

/*  One triangle at any three points, seen from the side (sx, sy): its
 *  normal is turned toward that side, whichever way the face leans.
 *  `ref` is the material's reference height at each point. */
int put_face(RMesh *m, const float p[3][3], const float side[2], float order, const float col[3], const float ref[3])
{
    float u[3], v[3], n[3], len;
    u[0] = p[1][0] - p[0][0];
    u[1] = p[1][1] - p[0][1];
    u[2] = (p[1][2] - p[0][2]) * LEVEL_H;
    v[0] = p[2][0] - p[0][0];
    v[1] = p[2][1] - p[0][1];
    v[2] = (p[2][2] - p[0][2]) * LEVEL_H;
    n[0] = u[1] * v[2] - u[2] * v[1];
    n[1] = u[2] * v[0] - u[0] * v[2];
    n[2] = u[0] * v[1] - u[1] * v[0];
    if (n[0] * side[0] + n[1] * side[1] < 0.0f)
        n[0] = -n[0], n[1] = -n[1], n[2] = -n[2];
    len = sqrtf(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
    if (len <= 0.0f)
        return 0;
    n[0] /= len, n[1] /= len, n[2] /= len;
    return put_tri_r2(m, p, n, order, col, ref, NULL, 1);
}

int put_wall_r(RMesh *m, const float t0[3], const float t1[3], const float b0[3], const float b1[3], const float nrm[3], float order, const float col[3], float r0, float r1)
{
    return put_wall_r2(m, t0, t1, b0, b1, nrm, order, col, r0, r1, col[1], col[1]);
}

int put_wall(RMesh *m, const float t0[3], const float t1[3], const float b0[3], const float b1[3], const float nrm[3], float order, const float col[3])
{
    return put_wall_r(m, t0, t1, b0, b1, nrm, order, col, t0[2], t1[2]);
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

/*  A tile's top as eight triangles about its middle, two for each edge.
 *  One runs from the edge's first corner to its middle, and one from its
 *  middle to its second corner. */
int put_top_dual(RMesh *m, const float p[4][3], const float mid[4][3], const float ctr[3], float order, const float col[3], int flat, const float *lean)
{
    float tri[3][3], w[3];
    int   e, h;
    for (e = 0; e < 4; ++e)
        for (h = 0; h < 2; ++h)
        {
            const float *a = h ? mid[e] : p[EDGE_A[e]], *b = h ? p[EDGE_B[e]] : mid[e];
            int          ka = h ? 4 + e : EDGE_A[e], kb = h ? EDGE_B[e] : 4 + e;
            /*  Every triangle turns the way put_top's do, whichever way
             *  round its edge runs. */
            if ((a[0] - ctr[0]) * (b[1] - ctr[1]) - (a[1] - ctr[1]) * (b[0] - ctr[0]) < 0.0f)
            {
                const float *t = a;
                int          k = ka;
                a = b, b = t, ka = kb, kb = k;
            }
            memcpy(tri[0], ctr, sizeof tri[0]);
            memcpy(tri[1], a, sizeof tri[1]);
            memcpy(tri[2], b, sizeof tri[2]);
            if (lean)
                w[0] = lean[8], w[1] = lean[ka], w[2] = lean[kb];
            if (put_tri_lean(m, tri, NULL, order, col, NULL, NULL, lean ? w : NULL, flat) != 0)
                return -1;
        }
    return 0;
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
        s_tri_head = (int32_t *)malloc((size_t)r_map * (size_t)r_map * sizeof *s_tri_head);
        if (!s_tri_head)
            return;
    }
    {
        int32_t *nn = (int32_t *)realloc(s_tri_next, (ntri ? ntri : 1u) * sizeof *nn);
        if (!nn)
            return;
        s_tri_next = nn;
    }
    for (i = 0; i < r_map * r_map; ++i)
        s_tri_head[i] = -1;
    for (t = 0; t < ntri; ++t)
    {
        RMeshVert v[3];
        float     cx, cy;
        int32_t   c, r, k;
        mesh_tri_copy(m, t, v);
        cx = (v[0].pos[0] + v[1].pos[0] + v[2].pos[0]) / 3.0f;
        cy = (v[0].pos[1] + v[1].pos[1] + v[2].pos[1]) / 3.0f;
        c = (int32_t)floorf(cx), r = (int32_t)floorf(cy);
        s_tri_next[t] = -1;
        if (c < 0 || r < 0 || c >= r_map || r >= r_map)
            continue;
        k             = r * r_map + c;
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
    if (!m || !out || col < 0 || row < 0 || col >= r_map || row >= r_map)
        return 0;
    tri_index(m);
    if (!s_tri_head)
        return 0;
    for (t = s_tri_head[row * r_map + col]; t >= 0 && n < max; t = s_tri_next[t])
        out[n++] = (uint32_t)t;
    return n;
}

void mesh_tri_get(const RMesh *m, uint32_t t, float p[3][3], uint32_t *comp)
{
    RMeshVert v[3];
    int       have = 3u * t < m->n_land && mesh_tri_copy(m, t, v) == 0, k, a;
    for (k = 0; k < 3; ++k)
        for (a = 0; a < 3; ++a)
            p[k][a] = have ? v[k].pos[a] : 0.0f;
    if (comp)
        *comp = have ? mesh_tri_comp(m, t) : SHAPE_NONE;
}

/*  The emitter's own scratch: the triangle index by tile, kept between
 *  builds.  Given back when the program is done with meshes altogether. */
void mesh_emit_free(void)
{
    free(s_tri_head), s_tri_head = NULL;
    free(s_tri_next), s_tri_next = NULL;
}

int mesh_tris_by(const RMesh *m, const uint16_t *grp, uint32_t ngrp, float (*out)[3][3], uint16_t *gout, int max)
{
    int      r, n = 0, k;
    uint32_t t;
    if (!m || !m->rl || !grp || !out || !gout)
        return 0;
    for (r = 0; r < m->n_rl; ++r)
        for (t = 0; t < m->rl[r].n / 3u && n < max; ++t)
        {
            uint32_t c = m->rl[r].comp[t];
            if (c >= ngrp || !grp[c])
                continue;
            for (k = 0; k < 3; ++k)
                memcpy(out[n][k], mesh_list_corner(&m->rl[r], 3u * t + (uint32_t)k)->pos, sizeof out[n][k]);
            gout[n++] = grp[c];
        }
    return n;
}
