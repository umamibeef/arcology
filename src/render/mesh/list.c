/*  mesh/list.c: the mesh's triangle lists, one for each chunk's range,
 *  and the numbering that lays them end to end (mesh.h). */
#include "mesh/mesh.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>
#include <meshoptimizer.h>

#include "mesh/lod.h"
#include "mesh/shape.h"

int mesh_chunk_of(int32_t col, int32_t row)
{
    if (col < 0)
        col = 0;
    if (row < 0)
        row = 0;
    if (col >= r_map)
        col = r_map - 1;
    if (row >= r_map)
        row = r_map - 1;
    return (row / MESH_CHUNK) * (r_map / MESH_CHUNK) + col / MESH_CHUNK;
}

/*  Cut a packed list into its levels of detail, by clustered
 *  simplification (mesh/lod.h).  0 on success, and a list it cannot cut
 *  keeps level 0 alone. */
static int mesh_list_lod(RMeshList *l);

/*  Pack a list: from here it holds each distinct vertex once.  0 on
 *  success, and a list that cannot be packed stays as it was laid. */
static int mesh_list_pack(RMeshList *l);

static int mesh_list_reserve(RMeshList *l, uint32_t verts);

static void lod_free(RMeshList *l)
{
    int k;
    for (k = 1; k < MESH_LOD_LEVELS; ++k)
    {
        free(l->lod[k]);
        l->lod[k]   = NULL;
        l->lod_n[k] = 0;
    }
}

static void list_clear(RMeshList *l)
{
    lod_free(l);
    free(l->idx);
    l->idx = NULL;
    l->n = l->nv = 0;
}

static int lists_size(RMeshList **l, int *n, int want)
{
    int i;
    if (*n == want)
    {
        for (i = 0; i < want; ++i)
            list_clear(&(*l)[i]);
        return 0;
    }
    if (*l)
        mesh_lists_free(*l, *n);
    *l = NULL;
    *n = 0;
    if (want == 0)
        return 0;
    *l = (RMeshList *)calloc((size_t)want, sizeof **l);
    if (!*l)
        return -1;
    *n = want;
    return 0;
}

int mesh_lists_begin(RMesh *m, int n_rl, int n_wl)
{
    m->n_land  = 0;
    m->n_water = 0;
    m->range   = MESH_RANGE_GROUND;
    return lists_size(&m->rl, &m->n_rl, n_rl) || lists_size(&m->wl, &m->n_wl, n_wl) ? -1 : 0;
}

static void list_free(RMeshList *l)
{
    lod_free(l);
    free(l->v);
    free(l->idx);
    free(l->comp);
    free(l->piece);
    free(l->tile);
    free(l->order);
    memset(l, 0, sizeof *l);
}

void mesh_lists_free(RMeshList *l, int n)
{
    int i;
    if (!l)
        return;
    for (i = 0; i < n; ++i)
        list_free(&l[i]);
    free(l);
}

static int mesh_list_reserve(RMeshList *l, uint32_t verts)
{
    uint32_t c = l->cap;
    if (verts > c)
    {
        RMeshVert *nv;
        c = c ? c : 3u * 256u;
        while (c < verts)
            c *= 2u;
        nv = (RMeshVert *)realloc(l->v, (size_t)c * sizeof *nv);
        if (!nv)
            return -1;
        l->v   = nv;
        l->cap = c;
    }
    if (l->cap / 3u > l->tcap)
    {
        uint32_t  tc = l->cap / 3u;
        uint32_t *nc, *np;
        float    *no;
        int32_t  *nt;
        nc = (uint32_t *)realloc(l->comp, (size_t)tc * sizeof *nc);
        if (!nc)
            return -1;
        l->comp = nc;
        np      = (uint32_t *)realloc(l->piece, (size_t)tc * sizeof *np);
        if (!np)
            return -1;
        l->piece = np;
        no       = (float *)realloc(l->order, (size_t)tc * sizeof *no);
        if (!no)
            return -1;
        l->order = no;
        nt       = (int32_t *)realloc(l->tile, (size_t)tc * sizeof *nt);
        if (!nt)
            return -1;
        l->tile = nt;
        l->tcap = tc;
    }
    return 0;
}

int mesh_list_room(RMeshList *l)
{
    return l->n + 3u <= l->cap && l->n / 3u < l->tcap ? 0 : mesh_list_reserve(l, l->n + 3u);
}

static int mesh_list_pack(RMeshList *l)
{
    uint32_t  *remap;
    RMeshVert *nv;
    size_t     u;
    if (l->idx)
        return 0;
    if (l->n == 0)
    {
        list_free(l);
        return 0;
    }
    remap = (uint32_t *)malloc((size_t)l->n * sizeof *remap);
    if (!remap)
        return -1;
    u  = meshopt_generateVertexRemap(remap, NULL, l->n, l->v, l->n, sizeof *l->v);
    nv = (RMeshVert *)malloc(u * sizeof *nv);
    if (!nv)
    {
        free(remap);
        return -1;
    }
    meshopt_remapVertexBuffer(nv, l->v, l->n, sizeof *l->v, remap);
    free(l->v);
    l->v   = nv;
    l->nv  = (uint32_t)u;
    l->cap = (uint32_t)u;
    l->idx = remap;
    /*  The triangles' own tables cut to what they hold. */
    {
        uint32_t  t  = l->n / 3u;
        uint32_t *nc = (uint32_t *)realloc(l->comp, (size_t)t * sizeof *nc);
        uint32_t *np;
        float    *no;
        int32_t  *nt;
        if (nc)
            l->comp = nc;
        np = (uint32_t *)realloc(l->piece, (size_t)t * sizeof *np);
        if (np)
            l->piece = np;
        no = (float *)realloc(l->order, (size_t)t * sizeof *no);
        if (no)
            l->order = no;
        nt = (int32_t *)realloc(l->tile, (size_t)t * sizeof *nt);
        if (nt)
            l->tile = nt;
        if (nc && np && no && nt)
            l->tcap = t;
    }
    return 0;
}

/*  The hierarchy as clusterlod hands it over: each group's error, and
 *  each cluster's group, the group it was refined from and its corners. */
typedef struct
{
    float    *gerr;
    int       ng, gcap;
    int      *cgroup, *crefined;
    uint32_t *cfirst, *ccount;
    int       nc, ccap;
    uint32_t *corners;
    size_t    nk, kcap;
    int       failed;
} LodHeap;

static int lod_group(void *ctx, int depth, float group_error, const MeshLodCluster *cl, size_t n)
{
    LodHeap *h  = (LodHeap *)ctx;
    int      id = h->ng;
    size_t   i;
    (void)depth;
    if (h->failed)
        return id;
    if (h->ng == h->gcap)
    {
        int    cap = h->gcap ? 2 * h->gcap : 64;
        float *g   = (float *)realloc(h->gerr, (size_t)cap * sizeof *g);
        if (!g)
            return h->failed = 1, id;
        h->gerr = g, h->gcap = cap;
    }
    h->gerr[h->ng++] = group_error;
    for (i = 0; i < n; ++i)
    {
        if (h->nc == h->ccap)
        {
            int       cap = h->ccap ? 2 * h->ccap : 256;
            int      *a   = (int *)realloc(h->cgroup, (size_t)cap * sizeof *a);
            int      *b   = a ? (int *)realloc(h->crefined, (size_t)cap * sizeof *b) : NULL;
            uint32_t *c   = b ? (uint32_t *)realloc(h->cfirst, (size_t)cap * sizeof *c) : NULL;
            uint32_t *d   = c ? (uint32_t *)realloc(h->ccount, (size_t)cap * sizeof *d) : NULL;
            if (a)
                h->cgroup = a;
            if (b)
                h->crefined = b;
            if (c)
                h->cfirst = c;
            if (!d)
                return h->failed = 1, id;
            h->ccount = d, h->ccap = cap;
        }
        while (h->nk + cl[i].n_idx > h->kcap)
        {
            size_t    cap = h->kcap ? 2 * h->kcap : 4096;
            uint32_t *k   = (uint32_t *)realloc(h->corners, cap * sizeof *k);
            if (!k)
                return h->failed = 1, id;
            h->corners = k, h->kcap = cap;
        }
        memcpy(h->corners + h->nk, cl[i].idx, cl[i].n_idx * sizeof *h->corners);
        h->cgroup[h->nc]   = id;
        h->crefined[h->nc] = cl[i].refined;
        h->cfirst[h->nc]   = (uint32_t)h->nk;
        h->ccount[h->nc]   = (uint32_t)cl[i].n_idx;
        h->nk += cl[i].n_idx;
        ++h->nc;
    }
    return id;
}

static const float PAINT_WEIGHT[2] = {1.0f, 1.0f};

static int mesh_list_lod(RMeshList *l)
{
    LodHeap   h;
    MeshLodIn in;
    float    *xyz;
    uint32_t  i;
    int       k, c, rc = 0;
    lod_free(l);
    if (!l->idx || l->n < 3)
        return 0;
    /*  The positions in tiles, a level standing half a tile. */
    xyz = (float *)malloc((size_t)l->nv * 3 * sizeof *xyz);
    if (!xyz)
        return -1;
    for (i = 0; i < l->nv; ++i)
    {
        xyz[3 * i]     = l->v[i].pos[0];
        xyz[3 * i + 1] = l->v[i].pos[1];
        xyz[3 * i + 2] = l->v[i].pos[2] * 0.5f;
    }
    memset(&h, 0, sizeof h);
    memset(&in, 0, sizeof in);
    in.pos = xyz, in.n_vert = l->nv, in.stride = 3 * sizeof *xyz, in.idx = l->idx, in.n_idx = l->n;
    /*  The paint across and along a face, which pick what is painted
     *  where, kept as a pixel keeps position. */
    in.attr = l->v[0].paint, in.attr_stride = sizeof *l->v, in.n_attr = 2, in.weight = PAINT_WEIGHT;
    if (mesh_lod_build(&in, 128, &h, lod_group) != 0 || h.failed)
        rc = -1;
    /*  At each level, the clusters whose group's error a pixel there
     *  does not hide, and whose finer group's it does. */
    for (k = 1; rc == 0 && k < MESH_LOD_LEVELS; ++k)
    {
        float     err = MESH_LOD_ERR0 * (float)(1u << (k - 1));
        size_t    n   = 0;
        uint32_t *out;
        for (c = 0; c < h.nc; ++c)
            if (h.gerr[h.cgroup[c]] > err && (h.crefined[c] < 0 || h.gerr[h.crefined[c]] <= err))
                n += h.ccount[c];
        out = (uint32_t *)malloc((n ? n : 1) * sizeof *out);
        if (!out)
        {
            rc = -1;
            break;
        }
        n = 0;
        for (c = 0; c < h.nc; ++c)
            if (h.gerr[h.cgroup[c]] > err && (h.crefined[c] < 0 || h.gerr[h.crefined[c]] <= err))
            {
                memcpy(out + n, h.corners + h.cfirst[c], h.ccount[c] * sizeof *out);
                n += h.ccount[c];
            }
        l->lod[k]   = out;
        l->lod_n[k] = (uint32_t)n;
    }
    if (rc != 0)
        lod_free(l);
    free(h.gerr), free(h.cgroup), free(h.crefined), free(h.cfirst), free(h.ccount), free(h.corners);
    free(xyz);
    return rc;
}

void mesh_list_tri(const RMeshList *l, uint32_t t, RMeshVert out[3])
{
    int k;
    for (k = 0; k < 3; ++k)
        out[k] = *mesh_list_corner(l, 3u * t + (uint32_t)k);
}

/*  The lists are packed and cut on every core: each worker takes the
 *  next list nobody has taken.  A list is its own work and shares
 *  nothing with another. */
typedef struct
{
    RMesh        *m;
    SDL_AtomicInt next;
} PackJob;

static int pack_worker(void *data)
{
    PackJob *j = (PackJob *)data;
    for (;;)
    {
        int k = SDL_AddAtomicInt(&j->next, 1);
        if (k >= j->m->n_rl + j->m->n_wl)
            break;
        if (k < j->m->n_rl)
        {
            /*  Only a chunk's ground is simplified.  What stands on it is
             *  thin pieces of several materials side by side.  A collapse
             *  across two of them would carry one's material over the
             *  other.  The ground near a street is laid with
             *  them, so its small dip under the street is never smoothed
             *  away.  The trees have levels of their own. */
            if (mesh_list_pack(&j->m->rl[k]) == 0 && !(j->m->n_rl > 1 && k % MESH_RANGES != MESH_RANGE_GROUND))
                mesh_list_lod(&j->m->rl[k]);
        }
        else
            mesh_list_pack(&j->m->wl[k - j->m->n_rl]);
    }
    return 0;
}

void mesh_lists_pack(RMesh *m)
{
    enum
    {
        MOST = 32
    };
    SDL_Thread *th[MOST];
    PackJob     job;
    int         n = SDL_GetNumLogicalCPUCores(), i;
    n             = n < 1 ? 1 : n > MOST ? MOST
                                         : n;
    job.m         = m;
    SDL_SetAtomicInt(&job.next, 0);
    for (i = 1; i < n; ++i)
        th[i] = SDL_CreateThread(pack_worker, "pack", &job);
    pack_worker(&job);
    for (i = 1; i < n; ++i)
        if (th[i])
            SDL_WaitThread(th[i], NULL);
}

void mesh_ranges_set(RMesh *m)
{
    uint32_t pos = 0;
    int      k;
    for (k = 0; k < m->n_rl; ++k)
    {
        m->range_start[k] = pos;
        m->range_count[k] = m->rl[k].n;
        pos += m->rl[k].n;
    }
    m->n_land = pos;
    pos       = 0;
    for (k = 0; k < m->n_wl; ++k)
    {
        m->wrange_start[k] = pos;
        m->wrange_count[k] = m->wl[k].n;
        pos += m->wl[k].n;
    }
    m->n_water = pos;
}

/*  The list triangle `t` lies in, and its place there.  A search over
 *  the ranges' starts: the last range starting at or before it that
 *  holds anything. */
static const RMeshList *tri_find(const RMesh *m, uint32_t t, uint32_t *at)
{
    const uint32_t  *start = m->range_start, *count = m->range_count;
    const RMeshList *l  = m->rl;
    uint32_t         v  = 3u * t;
    int              lo = 0, hi = m->n_rl - 1, n = m->n_rl;
    if (v >= m->n_land)
    {
        v -= m->n_land;
        if (v >= m->n_water)
            return NULL;
        start = m->wrange_start, count = m->wrange_count, l = m->wl;
        hi = n = m->n_wl, --hi;
    }
    if (hi < 0)
        return NULL;
    while (lo < hi)
    {
        int mid = (lo + hi + 1) / 2;
        if (start[mid] <= v)
            lo = mid;
        else
            hi = mid - 1;
    }
    /*  Ranges that hold nothing share their start with the one after. */
    while (lo < n - 1 && v >= start[lo] + count[lo])
        ++lo;
    *at = (v - start[lo]) / 3u;
    return &l[lo];
}

int mesh_tri_copy(const RMesh *m, uint32_t t, RMeshVert out[3])
{
    uint32_t         at;
    const RMeshList *l = tri_find(m, t, &at);
    if (!l)
        return -1;
    mesh_list_tri(l, at, out);
    return 0;
}

uint32_t mesh_tri_comp(const RMesh *m, uint32_t t)
{
    uint32_t         at;
    const RMeshList *l = tri_find(m, t, &at);
    return l && l->comp && at < l->tcap ? l->comp[at] : SHAPE_NONE;
}

float mesh_tri_order(const RMesh *m, uint32_t t)
{
    uint32_t         at;
    const RMeshList *l = tri_find(m, t, &at);
    return l && l->order && at < l->tcap ? l->order[at] : 0.0f;
}

float mesh_tri_mat(const RMesh *m, uint32_t t)
{
    uint32_t         at;
    const RMeshList *l = tri_find(m, t, &at);
    return l ? mesh_vert_mat(mesh_list_corner(l, 3u * at)) : 0.0f;
}

/* ---- one vertex ------------------------------------------------------- */

static int16_t snorm16(float f)
{
    f = f < -1.0f ? -1.0f : f > 1.0f ? 1.0f
                                     : f;
    return (int16_t)lrintf(f * 32767.0f);
}

void mesh_vert_pack(RMeshVert *v, const float nrm[3], float aux, float mat)
{
    float l = fabsf(nrm[0]) + fabsf(nrm[1]) + fabsf(nrm[2]);
    float x = l > 0.0f ? nrm[0] / l : 0.0f, y = l > 0.0f ? nrm[1] / l : 0.0f;
    /*  The octahedron's lower half folds out over the corners. */
    if (nrm[2] < 0.0f)
    {
        float fx = (1.0f - fabsf(y)) * (x >= 0.0f ? 1.0f : -1.0f);
        float fy = (1.0f - fabsf(x)) * (y >= 0.0f ? 1.0f : -1.0f);
        x = fx, y = fy;
    }
    v->nrm[0] = snorm16(x);
    v->nrm[1] = snorm16(y);
    v->aux[0] = meshopt_quantizeHalf(aux);
    v->aux[1] = meshopt_quantizeHalf(mat);
}

float mesh_vert_mat(const RMeshVert *v)
{
    return meshopt_dequantizeHalf(v->aux[1]);
}
