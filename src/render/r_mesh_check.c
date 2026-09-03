/*  r_mesh_check.c -- the checks: watertightness, and roads against the ground.
 *
 *  Split out of r_mesh.c; see r_mesh_int.h.
 */
#include "r_mesh_int.h"

/* ---- the check --------------------------------------------------------- */

/*  Every edge of every triangle must belong to another triangle too: a
 *  surface with no free edge has no crack.  Edges are keyed on their
 *  quantised end points (heights are means of at most four levels, so
 *  forty-eighths are exact).  A vertical edge is a span on its corner's
 *  vertical, and the spans there may be split differently by the walls
 *  meeting at the corner, so those are checked as coverage: every part
 *  of the vertical that any edge covers must be covered at least twice.
 *  The base of the map's cut, z = 0, is the floor and is closed by
 *  definition. */
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

int r_mesh_check_roads(const RMesh *m, int verbose)
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
    /*  SC2K_TILE_DUMP=col,row prints every face whose centroid lies on
     *  that tile, any material, for inspection. */
    {
        const char *dump = getenv("SC2K_TILE_DUMP");
        int         dc, dr;
        if (dump && sscanf(dump, "%d,%d", &dc, &dr) == 2)
            for (i = 0; i < n_tri; ++i)
            {
                const RMeshVert *v  = &m->land[i * 3u];
                float            cx = (v[0].pos[0] + v[1].pos[0] + v[2].pos[0]) / 3.0f;
                float            cy = (v[0].pos[1] + v[1].pos[1] + v[2].pos[1]) / 3.0f;
                if ((int32_t)floorf(cx) == dc && (int32_t)floorf(cy) == dr)
                    printf("tri mat %g order %g (%.3f,%.3f,%.3f) (%.3f,%.3f,%.3f) (%.3f,%.3f,%.3f)\n", (double)v[0].col[2], (double)v[0].pos[3], (double)v[0].pos[0], (double)v[0].pos[1], (double)v[0].pos[2], (double)v[1].pos[0], (double)v[1].pos[1], (double)v[1].pos[2], (double)v[2].pos[0], (double)v[2].pos[1], (double)v[2].pos[2]);
            }
    }
    /*  Every road face, sampled. */
    for (i = 0; i < n_tri; ++i)
    {
        const RMeshVert *v   = &m->land[i * 3u];
        float            mat = v[0].col[2];
        int              a, b;
        if (!(mat > 6.5f && mat < 7.5f) && !(mat > 9.5f && mat < 11.5f) && !(mat > 12.5f && mat < 14.5f) && !(mat > 15.5f))
            continue; /* the bands, the sidewalk corners, the crossing surface and approach; not props, not vehicles */
        if (v[0].nrm[3] >= 3.5f)
            continue; /* a quad in a cut: the ground over it is meant, held back by retaining walls */
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
                    if (getenv("SC2K_CLIP_DUMP") && tiles[t].n <= 2)
                    {
                        printf("  clip at %.2f,%.2f road z %.2f mat %g tri (%.2f,%.2f,%.2f) (%.2f,%.2f,%.2f) (%.2f,%.2f,%.2f) order %g; ground +%.2f\n",
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
                            printf("     ground mat %g (%.2f,%.2f,%.2f) (%.2f,%.2f,%.2f) (%.2f,%.2f,%.2f)\n", (double)q[0].col[2], (double)q[0].pos[0], (double)q[0].pos[1], (double)q[0].pos[2], (double)q[1].pos[0], (double)q[1].pos[1], (double)q[1].pos[2], (double)q[2].pos[0], (double)q[2].pos[1], (double)q[2].pos[2]);
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
            if (!((b >= 0x0Eu && b <= 0x3Au) || (b >= 0x43u && b <= 0x48u)))
                continue;
            if (count[i] == 0 && b >= 0x2Cu && b <= 0x3Au)
            {
                /* a rail piece bypassed by its line's cut corner: geometry on a neighbour will do */
                int32_t col = (int32_t)(i % R_MAP), row = (int32_t)(i / R_MAP), dc, dr, near = 0;
                for (dr = -1; dr <= 1 && !near; ++dr)
                    for (dc = -1; dc <= 1 && !near; ++dc)
                        if (col + dc >= 0 && row + dr >= 0 && col + dc < R_MAP && row + dr < R_MAP && count[(row + dr) * R_MAP + col + dc])
                            near = 1;
                if (near)
                    continue;
            }
            if (count[i] == 0)
            {
                ++missing;
                if (verbose && missing <= 40)
                    printf("  no network geometry on column %3d row %3d, XBLD %02x\n", (int)(i % R_MAP), (int)(i / R_MAP), (unsigned)b);
            }
        }
        if (verbose)
            printf("road pieces without geometry: %u\n", missing);
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
        printf("road clip  %u samples under the terrain on %u tiles\n", hits, n_bad);
        for (i = 0; i < n_bad && shown < 40; ++i, ++shown)
            printf("  column %3d row %3d  %4u samples, terrain above by %.2f\n",
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

int r_mesh_check(const RMesh *m, int verbose)
{
    if (getenv("SC2K_SPIKE_CHECK"))
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
                    printf("spike: (%.3f,%.3f,%.3f) (%.3f,%.3f,%.3f) (%.3f,%.3f,%.3f)\n", (double)v[0].pos[0], (double)v[0].pos[1], (double)v[0].pos[2], (double)v[1].pos[0], (double)v[1].pos[1], (double)v[1].pos[2], (double)v[2].pos[0], (double)v[2].pos[1], (double)v[2].pos[2]);
            }
        }
        printf("spikes: %u\n", bad);
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
            printf("free edge  (%g,%g,%g)-(%g,%g,%g)  tile c%d r%d  %s\n",
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
                        printf("free span  corner (%g,%g) z %g..%g  %s\n",
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
        printf("mesh check: %u triangles, %u free edges, %u free vertical spans\n",
               (unsigned)n_tri,
               (unsigned)free_edges,
               (unsigned)free_spans);
        for (k = 0; k < 8; ++k)
            if (by_mat[k])
                printf("  %-9s %u\n", mat_name((float)k), (unsigned)by_mat[k]);
    }
    free(tab);
    free(spans);
    return (int)(free_edges + free_spans);
}

void r_mesh_free(RMesh *m)
{
    free(m->land);
    free(m->water);
    free(m->net.pts);
    free(m->net.segs);
    free(m->railnet.pts);
    free(m->railnet.segs);
    free(m->xings);
    free(m->rsigs);
    memset(m, 0, sizeof *m);
}
