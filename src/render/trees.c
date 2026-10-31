/*  trees.c: see trees.h.  Every number that decides how the forest looks
 *  is a table in this file: the species, the stages and, later, the
 *  biomes.  A SPECIES is a row of numbers, and one builder turns it into
 *  a tree (Weber and Penn, 1995).  The placement,
 *  place_trees, reads the tables and a tile, and answers the tile's trees.  It is pure.  A seed from the
 *  tile's place gives the same forest every run, and nothing here reads
 *  the clock.
 *
 *  Heights are in levels over the ground, and widths in tiles. */
#include "trees.h"
#include "terrain.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "material.h"
#include "materials.h"
#include "mesh/emit.h"
#include "mesh/shape.h"
#include "mesh/shapes.h"
#include "mesh/surface.h"
#include "mesh/tile.h"

/*  How a species' crown is made.  CONES is a conifer: a stack of tiers.
 *  Each tier is a skirt of branches whose tips droop.  The tiers follow
 *  Weber and Penn's conical shape, 0.2 + 0.8 of the way down the crown. */
enum
{
    CROWN_CONES = 0,
    CROWN_CARDS, /* a broadleaf: crossed cards and caps, cut to a ragged round crown */
    CROWN_FRONDS /* a palm: fronds from the trunk's top, rising and then drooping */
};

typedef struct
{
    const char *name;
    int         crown;                      /* CROWN_* */
    float       trunk_h, trunk_r;           /* the bare trunk under the crown, at mature size */
    float       crown_h, crown_r;           /* the crown over it, and its widest */
    int         elements;                   /* the tiers of its crown, its cards, or its fronds */
    int         branches;                   /* the branch tips round a tier */
    float       droop;                      /* how far a tier's tips hang below its top, against the tier's height */
    float       rag;                        /* how ragged the edge of a tier is, 0 to 1 */
    float       leaf[3], leaf2[3], bark[3]; /* its two leaf colors and its bark */
    float       alpine, wet;                /* how much more it grows on high ground and by water, 0 to 1 */
} Species;

static const Species SPECIES[] = {
    {"fir",     CROWN_CONES,  0.16f, 0.018f, 1.15f, 0.21f, 6, 9, 0.85f, 0.45f, {0.24f, 0.40f, 0.18f}, {0.10f, 0.22f, 0.11f}, {0.30f, 0.22f, 0.15f}, 1.0f, 0.0f},
    {"pine",    CROWN_CONES,  0.70f, 0.022f, 0.55f, 0.22f, 3, 8, 0.35f, 0.55f, {0.26f, 0.38f, 0.19f}, {0.12f, 0.22f, 0.12f}, {0.42f, 0.27f, 0.17f}, 0.7f, 0.0f},
    {"oak",     CROWN_CARDS,  0.32f, 0.030f, 0.80f, 0.32f, 3, 0, 0.0f,  0.45f, {0.34f, 0.46f, 0.17f}, {0.15f, 0.26f, 0.09f}, {0.33f, 0.26f, 0.19f}, 0.0f, 0.0f},
    {"maple",   CROWN_CARDS,  0.36f, 0.024f, 0.98f, 0.24f, 3, 0, 0.0f,  0.45f, {0.38f, 0.50f, 0.17f}, {0.18f, 0.30f, 0.10f}, {0.35f, 0.28f, 0.21f}, 0.1f, 0.2f},
    {"birch",   CROWN_CARDS,  0.42f, 0.016f, 0.82f, 0.16f, 3, 0, 0.0f,  0.55f, {0.46f, 0.56f, 0.21f}, {0.24f, 0.36f, 0.12f}, {0.86f, 0.84f, 0.78f}, 0.5f, 1.0f},
    {"cypress", CROWN_CONES,  0.08f, 0.016f, 1.35f, 0.09f, 6, 8, 0.25f, 0.30f, {0.17f, 0.30f, 0.15f}, {0.07f, 0.17f, 0.09f}, {0.35f, 0.25f, 0.18f}, 0.0f, 1.0f},
    {"palm",    CROWN_FRONDS, 1.05f, 0.020f, 0.22f, 0.30f, 8, 0, 0.0f,  0.80f, {0.40f, 0.52f, 0.18f}, {0.20f, 0.32f, 0.10f}, {0.50f, 0.40f, 0.28f}, 0.0f, 1.0f},
    {"acacia",  CROWN_CARDS,  0.55f, 0.022f, 0.24f, 0.34f, 2, 0, 0.0f,  0.50f, {0.40f, 0.46f, 0.20f}, {0.22f, 0.30f, 0.12f}, {0.36f, 0.28f, 0.20f}, 0.0f, 0.0f},
};
enum
{
    SP_FIR,
    SP_PINE,
    SP_OAK,
    SP_MAPLE,
    SP_BIRCH,
    SP_CYPRESS,
    SP_PALM,
    SP_ACACIA
};
#define N_SPECIES ((int)(sizeof SPECIES / sizeof SPECIES[0]))

/*  THE BIOMES.  Each row gives the mix of species a part of the world
 *  grows, by weight.  It also gives how many trees a stage holds against
 *  the stage table, and a tint of its leaves (1 for none). */
typedef struct
{
    const char *name;
    float       mix[N_SPECIES]; /* by SP_*, summing to 1 */
    float       count;
    float       tint[3];
} Biome;

static const Biome BIOMES[] = {
    {"temperate mixed",    {0.00f, 0.20f, 0.35f, 0.30f, 0.15f, 0.00f, 0.00f, 0.00f}, 1.0f, {1.0f, 1.0f, 1.0f}  },
    {"northeast hardwood", {0.00f, 0.10f, 0.30f, 0.40f, 0.20f, 0.00f, 0.00f, 0.00f}, 1.0f, {1.0f, 1.0f, 1.0f}  },
    {"southeast pine",     {0.00f, 0.55f, 0.30f, 0.00f, 0.00f, 0.15f, 0.00f, 0.00f}, 1.0f, {1.0f, 1.0f, 1.0f}  },
    {"boreal",             {0.60f, 0.25f, 0.00f, 0.00f, 0.15f, 0.00f, 0.00f, 0.00f}, 1.0f, {1.0f, 1.0f, 1.0f}  },
    {"pacific northwest",  {0.70f, 0.15f, 0.00f, 0.15f, 0.00f, 0.00f, 0.00f, 0.00f}, 1.0f, {1.0f, 1.0f, 1.0f}  },
    {"mediterranean",      {0.00f, 0.35f, 0.30f, 0.00f, 0.00f, 0.35f, 0.00f, 0.00f}, 0.8f, {1.02f, 1.0f, 0.88f}},
    {"tropical",           {0.00f, 0.00f, 0.25f, 0.00f, 0.00f, 0.00f, 0.60f, 0.15f}, 1.0f, {1.0f, 1.05f, 0.95f}},
    {"arid",               {0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.15f, 0.25f, 0.60f}, 0.5f, {1.08f, 1.0f, 0.78f}},
};
#define N_BIOMES ((int)(sizeof BIOMES / sizeof BIOMES[0]))

/*  The shipped cities' biomes, by the name of their file, from where in
 *  the world each is.  A city not here is temperate mixed. */
static const struct
{
    const char *file;
    int         biome;
} CITY_BIOME[] = {
    {"atlanta",        2},
    {"charleston",     2},
    {"neworlea",       2},
    {"manhattan",      1},
    {"nyc",            1},
    {"washington-d-c", 1},
    {"chicago",        1},
    {"toronto",        1},
    {"toronto-2",      1},
    {"lincoln",        0},
    {"portland",       4},
    {"greenlan",       3},
    {"san-francisco",  5},
    {"oakland",        5},
    {"silicon-valley", 5},
    {"hollywood",      5},
    {"malibu",         5},
    {"roma",           5},
    {"venice",         5},
    {"barcelona",      5},
    {"sydney",         5},
    {"sydney-2",       5},
    {"hawaii",         6},
    {"the-bahamas",    6},
    {"rio-de-janeiro", 6},
    {"puntjak-putih",  6},
    {"la-isla",        6},
    {"floating",       6},
    {"las-vegas",      7},
    {"egypt-falls",    7},
};

static int s_biome = 0;

int trees_biomes(void)
{
    return N_BIOMES;
}

const char *trees_biome_name(int i)
{
    return i >= 0 && i < N_BIOMES ? BIOMES[i].name : "";
}

int trees_biome_of_city(const char *path)
{
    const char *b = path ? strrchr(path, '/') : NULL;
    char        stem[64];
    size_t      k, n;
    b = b ? b + 1 : path ? path
                         : "";
    for (n = 0; b[n] && b[n] != '.' && n + 1 < sizeof stem; ++n)
        stem[n] = b[n];
    stem[n] = 0;
    for (k = 0; k < sizeof CITY_BIOME / sizeof CITY_BIOME[0]; ++k)
        if (strcmp(CITY_BIOME[k].file, stem) == 0)
            return CITY_BIOME[k].biome;
    return 0;
}

void trees_set_biome(int i)
{
    s_biome = i >= 0 && i < N_BIOMES ? i : 0;
}

/*  The species' leaf colors, as the leaf material reads them (trees.h). */
void trees_leaf_colors(float out[TREES_SPECIES_MAX][2][4], float rag[TREES_SPECIES_MAX])
{
    int s, k;
    for (s = 0; s < TREES_SPECIES_MAX; ++s)
    {
        const Species *sp = &SPECIES[s < N_SPECIES ? s : 0];
        for (k = 0; k < 3; ++k)
            out[s][0][k] = sp->leaf[k] * BIOMES[s_biome].tint[k], out[s][1][k] = sp->leaf2[k] * BIOMES[s_biome].tint[k];
        out[s][0][3] = out[s][1][3] = 0.0f;
        rag[s]                      = sp->rag;
    }
}

/*  A forest stage, from the first (the sapling, 0x06) on: how many trees
 *  stand on the tile and their size against a mature one.  A tree
 *  advances one stage a month to 0x0C.  The map generator alone places
 *  0x0D, old growth. */
static const struct
{
    int   n;
    float size;
} STAGE[8] = {
    {1, 0.25f},
    {1, 0.45f},
    {2, 0.60f},
    {2, 0.75f},
    {3, 0.85f},
    {3, 1.00f},
    {4, 1.00f},
    {5, 1.10f},
};

/*  Where the trees of a tile stand.  They are kept this far in from its
 *  edges, and each lies off its grid cell by up to this share of it. */
#define TREE_EDGE_INSET  0.1f
#define TREE_JITTER      0.35f
#define TREE_CLUMP_TILES 4.0f /* the tiles across a clump of trees */

typedef struct
{
    int      species;
    float    x, y, size;
    uint32_t seed;
} Tree;
#define TREES_MAX 5

/*  A number from 0 to 1 for a tile and a salt. */
static float tile_rand(int32_t col, int32_t row, uint32_t salt)
{
    uint32_t x = (uint32_t)col * 747796405u + (uint32_t)row * 2654435761u + salt * 2891336453u + 1u;
    x ^= x >> 16, x *= 0x7feb352du, x ^= x >> 15, x *= 0x846ca68bu, x ^= x >> 16;
    return (float)(x & 0xFFFFFFu) / 16777216.0f;
}

/*  The trees of the forest tile (col, row) at stage `st`, from nought.
 *  A forest grows in clumps with gaps between them.  A slow noise lies
 *  over the map, a clump every few tiles.  It pulls each tile's trees
 *  toward the nearer clump, and thins them where it is low.  Each tree takes a
 *  jittered cell of the tile and its own size.  Answers how many. */
static float clump_at(float x, float y)
{
    int32_t ix = (int32_t)floorf(x), iy = (int32_t)floorf(y);
    float   fx = x - (float)ix, fy = y - (float)iy, a, b, c2, d;
    fx = fx * fx * (3.0f - 2.0f * fx), fy = fy * fy * (3.0f - 2.0f * fy);
    a = tile_rand(ix, iy, 501u), b = tile_rand(ix + 1, iy, 501u), c2 = tile_rand(ix, iy + 1, 501u), d = tile_rand(ix + 1, iy + 1, 501u);
    return a + (b - a) * fx + (c2 - a) * fy + (a - b - c2 + d) * fx * fy;
}

/*  THE LAND A TILE'S TREES STAND ON, as the ground's shading reads it
 *  (terrain_body.frag).  `high` is 0 below 8 levels and 1 above 16.
 *  `slope` is the ground's slope term over the tile's corners.  `wet` is
 *  1 by water and 0 three tiles from it. */
typedef struct
{
    float high, slope, wet;
} Land;

static Land land_at(const RCity *c, uint8_t mask_bit, int32_t col, int32_t row)
{
    const float x = (float)col, y = (float)row;
    const float h00 = surface_at_world(c, mask_bit, x, y), h10 = surface_at_world(c, mask_bit, x + 1.0f, y);
    const float h01 = surface_at_world(c, mask_bit, x, y + 1.0f), h11 = surface_at_world(c, mask_bit, x + 1.0f, y + 1.0f);
    const float gx = 0.5f * (h10 + h11 - h00 - h01) * MESH_LEVEL_H, gy = 0.5f * (h01 + h11 - h00 - h10) * MESH_LEVEL_H;
    const float mid = 0.25f * (h00 + h10 + h01 + h11), t = fminf(fmaxf((mid - 8.0f) / 8.0f, 0.0f), 1.0f);
    float       near = 9.0f;
    int32_t     dc, dr;
    Land        l;
    for (dr = -2; dr <= 2; ++dr)
        for (dc = -2; dc <= 2; ++dc)
        {
            int32_t cc = col + dc, rr = row + dr;
            float   d = sqrtf((float)(dc * dc + dr * dr));
            if (cc >= 0 && rr >= 0 && cc < r_map && rr < r_map && d < near && is_water(c->map.xter[rr * r_map + cc]))
                near = d;
        }
    l.high  = t * t * (3.0f - 2.0f * t);
    l.slope = fminf((1.0f - 1.0f / sqrtf(1.0f + gx * gx + gy * gy)) * 3.0f, 1.0f);
    l.wet   = fmaxf(1.0f - near / 3.0f, 0.0f);
    return l;
}

/*  Where the ground is too steep for trees, by the slope term.  Past
 *  the first a tile holds half its trees.  Past the second it holds none. */
#define TREE_SLOPE_HALF 0.5f
#define TREE_SLOPE_NONE 0.8f

static int place_trees(const RCity *c, uint8_t mask_bit, int32_t col, int32_t row, int st, Tree out[TREES_MAX])
{
    const int   n = STAGE[st].n, side = n <= 1 ? 1 : n <= 4 ? 2
                                                            : 3;
    const float cs   = TREE_CLUMP_TILES;
    float       here = clump_at((float)col / cs, (float)row / cs);
    float       gx   = (clump_at(((float)col + 1.0f) / cs, (float)row / cs) - clump_at(((float)col - 1.0f) / cs, (float)row / cs)) * 0.5f;
    float       gy   = (clump_at((float)col / cs, ((float)row + 1.0f) / cs) - clump_at((float)col / cs, ((float)row - 1.0f) / cs)) * 0.5f;
    const Land  land = land_at(c, mask_bit, col, row);
    int         k, m = 0;
    if (land.slope > TREE_SLOPE_NONE)
        return 0;
    for (k = 0; k < n; ++k)
    {
        float cx = ((float)(k % side) + 0.5f) / (float)side, cy = ((float)(k / side) + 0.5f) / (float)side, px, py;
        float jx = (tile_rand(col, row, 3u * (uint32_t)k) - 0.5f) * 2.0f * TREE_JITTER / (float)side;
        float jy = (tile_rand(col, row, 3u * (uint32_t)k + 1u) - 0.5f) * 2.0f * TREE_JITTER / (float)side;
        /*  A gap where the clumps are thin: a tree there stands only now
         *  and then. */
        if (k > 0 && tile_rand(col, row, 3u * (uint32_t)k + 7u) > here * 1.6f * BIOMES[s_biome].count)
            continue;
        if (land.slope > TREE_SLOPE_HALF && (k & 1))
            continue;
        px = cx + jx + gx * cs * 2.5f, py = cy + jy + gy * cs * 2.5f;
        {
            /*  The biome's mix, by a draw of the tree's own.  High ground
             *  favors the alpine species and water the wet ones, so a slope
             *  grades from one to the other with no line between. */
            float w[N_SPECIES], sum = 0.0f, r, acc = 0.0f;
            int   q;
            for (q = 0; q < N_SPECIES; ++q)
            {
                w[q] = BIOMES[s_biome].mix[q] * (1.0f + 3.0f * SPECIES[q].alpine * land.high) * (1.0f + 3.0f * SPECIES[q].wet * land.wet);
                sum += w[q];
            }
            r              = tile_rand(col, row, 3u * (uint32_t)k + 11u) * sum;
            out[m].species = 0;
            for (q = 0; q < N_SPECIES; ++q)
            {
                acc += w[q];
                if (r < acc)
                {
                    out[m].species = q;
                    break;
                }
            }
        }
        out[m].x    = (float)col + TREE_EDGE_INSET + (1.0f - 2.0f * TREE_EDGE_INSET) * fminf(fmaxf(px, 0.0f), 1.0f);
        out[m].y    = (float)row + TREE_EDGE_INSET + (1.0f - 2.0f * TREE_EDGE_INSET) * fminf(fmaxf(py, 0.0f), 1.0f);
        out[m].size = STAGE[st].size * (0.75f + 0.5f * tile_rand(col, row, 3u * (uint32_t)k + 2u));
        out[m].seed = (uint32_t)col * 73856093u ^ (uint32_t)row * 19349663u ^ (uint32_t)k * 83492791u;
        ++m;
    }
    return m;
}

/*  One triangle of foliage.  Each corner carries two numbers.  The first
 *  is how far out from the trunk it lies, 0 to 1.  The second is how far
 *  down its tier's skirt: 0 at the top, 1 at a drooped tip, past 1 under
 *  it.  The normal is the crown's envelope at the face, not the face's
 *  own.  So a crown is lit as one rounded mass, not as its facets. */
static int leaf_tri(RMesh *m, float order, float mat, const float p[3][3], const float u[3], const float v[3], float cx, float cy, float up)
{
    float col[3] = {0.0f, 0.0f, mat}, n[3][3];
    float mx = (p[0][0] + p[1][0] + p[2][0]) / 3.0f - cx, my = (p[0][1] + p[1][1] + p[2][1]) / 3.0f - cy;
    int   k;
    for (k = 0; k < 3; ++k)
    {
        /*  Out from the trunk at the corner, and up or down by the crown's
         *  slope there.  A corner on the trunk itself takes the way out at
         *  the face's middle. */
        float dx = p[k][0] - cx, dy = p[k][1] - cy, l = sqrtf(dx * dx + dy * dy);
        if (l < 1e-4f)
            dx = mx, dy = my, l = sqrtf(dx * dx + dy * dy);
        n[k][0] = l > 1e-6f ? dx / l : 0.0f, n[k][1] = l > 1e-6f ? dy / l : 0.0f, n[k][2] = up;
        l = sqrtf(n[k][0] * n[k][0] + n[k][1] * n[k][1] + n[k][2] * n[k][2]);
        n[k][0] /= l, n[k][1] /= l, n[k][2] /= l;
    }
    return put_tri_n3(m, p, (const float (*)[3])n, order, col, u, v);
}

/*  The next number from 0 to 1 of a tree's own seed. */
static float seed_rand(uint32_t *s)
{
    uint32_t x = (*s += 0x9E3779B9u);
    x ^= x >> 16, x *= 0x7feb352du, x ^= x >> 15, x *= 0x846ca68bu, x ^= x >> 16;
    return (float)(x & 0xFFFFFFu) / 16777216.0f;
}

/*  A card or a cap of foliage: a quad from four corners.  Each corner
 *  carries where on the quad it lies (u across, v up or along).  The leaf
 *  material reads it for its cutout.  `kind` is 3 for a standing card, 5 for a cap and 7
 *  for a frond, laid on v so the material tells them apart.  The normals
 *  point out from the crown's middle (cx, cy, cz), so the crown is lit as
 *  one mass. */
static int leaf_quad(RMesh *m, float order, float mat, const float q[4][3], const float uv[4][2], float kind, float shade, float cx, float cy, float cz)
{
    static const int T[2][3] = {
        {0, 1, 2},
        {0, 2, 3}
    };
    float col[3] = {0.0f, 0.0f, mat};
    int   t, k;
    for (t = 0; t < 2; ++t)
    {
        float p[3][3], n[3][3], u[3], v[3];
        for (k = 0; k < 3; ++k)
        {
            const float *c  = q[T[t][k]];
            float        dx = c[0] - cx, dy = c[1] - cy, dz = (c[2] - cz) * 0.5f, l = sqrtf(dx * dx + dy * dy + dz * dz);
            memcpy(p[k], c, sizeof p[k]);
            n[k][0] = l > 1e-6f ? dx / l : 0.0f, n[k][1] = l > 1e-6f ? dy / l : 0.0f, n[k][2] = l > 1e-6f ? dz / l + 0.35f : 1.0f;
            l = sqrtf(n[k][0] * n[k][0] + n[k][1] * n[k][1] + n[k][2] * n[k][2]);
            n[k][0] /= l, n[k][1] /= l, n[k][2] /= l;
            u[k] = 2.0f * shade + uv[T[t][k]][0], v[k] = kind + uv[T[t][k]][1];
        }
        if (put_tri_n3(m, p, (const float (*)[3])n, order, col, u, v) != 0)
            return -1;
    }
    return 0;
}

/*  A broadleaf's crown: `elements` standing cards cross at the trunk.
 *  Each card has the crown's width and height.  Two caps lie across them,
 *  and the lower cap is the wider.  The camera from above sees the caps,
 *  and from the side it sees the cards.  The crossed card is a degenerate
 *  billboard cloud (Decoret et al.).  The material cuts each to a ragged
 *  round crown. */
static int cards_crown(RMesh *m, const Species *sp, float order, float mat, float x, float y, float g, float th, float ch, float cr, float turn, float shade, int n)
{
    static const float UV[4][2] = {
        {0.0f, 0.0f},
        {1.0f, 0.0f},
        {1.0f, 1.0f},
        {0.0f, 1.0f}
    };
    const float cz = g + th + 0.5f * ch;
    int         k;
    for (k = 0; k < n; ++k)
    {
        float a = turn + 3.1415927f * (float)k / (float)n, dx = cosf(a) * cr, dy = sinf(a) * cr;
        float q[4][3] = {
            {x - dx, y - dy, g + th     },
            {x + dx, y + dy, g + th     },
            {x + dx, y + dy, g + th + ch},
            {x - dx, y - dy, g + th + ch}
        };
        if (leaf_quad(m, order, mat, q, UV, 3.0f, shade, x, y, cz) != 0)
            return -1;
    }
    for (k = 0; k < 2; ++k)
    {
        float z = g + th + ch * (k ? 0.88f : 0.5f), r = cr * (k ? 0.72f : 1.04f), a = turn + 0.785f * (float)k;
        float c0 = cosf(a) * r, s0 = sinf(a) * r;
        float q[4][3] = {
            {x - c0 + s0, y - s0 - c0, z},
            {x + c0 + s0, y + s0 - c0, z},
            {x + c0 - s0, y + s0 + c0, z},
            {x - c0 - s0, y - s0 + c0, z}
        };
        if (leaf_quad(m, order, mat, q, UV, 5.0f, shade, x, y, cz - 0.3f * ch) != 0)
            return -1;
    }
    (void)sp;
    return 0;
}

/*  A palm's crown: `n` fronds from the trunk's top, each two strips that
 *  rise and then droop to the tip.  The material cuts each into a fringe
 *  of leaflets. */
static int fronds_crown(RMesh *m, float order, float mat, float x, float y, float top, float cr, float turn, float shade, int n, uint32_t *rs)
{
    int k;
    for (k = 0; k < n; ++k)
    {
        float a = turn + 6.2831853f * ((float)k + 0.3f * seed_rand(rs)) / (float)n, ca = cosf(a), sa = sinf(a);
        float len = cr * (0.8f + 0.4f * seed_rand(rs)), w = 0.07f * len / 0.3f;
        float rise = 0.10f, droop = 0.28f * (0.7f + 0.6f * seed_rand(rs));
        float p0[3] = {x, y, top}, p1[3] = {x + ca * len * 0.5f, y + sa * len * 0.5f, top + rise}, p2[3] = {x + ca * len, y + sa * len, top + rise - droop};
        float wx = -sa * w * 0.5f, wy = ca * w * 0.5f;
        float q1[4][3] = {
            {p0[0] - wx * 0.3f, p0[1] - wy * 0.3f, p0[2]},
            {p0[0] + wx * 0.3f, p0[1] + wy * 0.3f, p0[2]},
            {p1[0] + wx,        p1[1] + wy,        p1[2]},
            {p1[0] - wx,        p1[1] - wy,        p1[2]}
        };
        float q2[4][3] = {
            {p1[0] - wx,        p1[1] - wy,        p1[2]},
            {p1[0] + wx,        p1[1] + wy,        p1[2]},
            {p2[0] + wx * 0.2f, p2[1] + wy * 0.2f, p2[2]},
            {p2[0] - wx * 0.2f, p2[1] - wy * 0.2f, p2[2]}
        };
        const float uv1[4][2] = {
            {0.0f, 0.0f},
            {1.0f, 0.0f},
            {1.0f, 0.5f},
            {0.0f, 0.5f}
        },
                    uv2[4][2] = {{0.0f, 0.5f}, {1.0f, 0.5f}, {1.0f, 0.99f}, {0.0f, 0.99f}};
        if (leaf_quad(m, order, mat, q1, uv1, 7.0f, shade, x, y, top - 0.2f) != 0 || leaf_quad(m, order, mat, q2, uv2, 7.0f, shade, x, y, top - 0.2f) != 0)
            return -1;
    }
    return 0;
}

/*  THE CANOPY: one tree of a species row at (x, y) over the ground `g`,
 *  `size` against a mature one.  `seed` makes this tree its own.  It
 *  gives its height, its width, its lean and its shade of the species'
 *  leaves.  It also gives the number, reach and droop of each tier's
 *  branches.  `detail`, from 0 to 1, sets how many tiers, cards or fronds
 *  the crown has, from the fewest that still read as its kind.  Below a
 *  half the trunk has three sides.  The builder names no species. */
static int canopy(RMesh *m, const Species *sp, int si, float order, float x, float y, float g, float size, uint32_t seed, float detail, float bark)
{
    uint32_t    rs   = seed;
    const float tall = 0.82f + 0.36f * seed_rand(&rs), wide = 0.8f + 0.4f * seed_rand(&rs);
    const float lean = 0.03f * seed_rand(&rs), lean_a = 6.2831853f * seed_rand(&rs), turn = 6.2831853f * seed_rand(&rs);
    const float shade = (float)(int)(seed_rand(&rs) * 9.99f);
    const float th = sp->trunk_h * size * tall, ch = sp->crown_h * size * tall, cr = sp->crown_r * size * wide;
    const float mat   = MAT_LEAF + 0.0625f * (float)si; /* a sixteenth a species: exact in the half float a vertex keeps it in */
    const int   least = sp->crown == CROWN_CONES ? 1 : sp->crown == CROWN_CARDS ? 2
                                                                                : 4; /* a crossed pair, a palm's four fronds */
    const int   most  = sp->elements > least ? sp->elements : least;
    const int   n     = least + (int)lrintf((float)(most - least) * detail);
    int         k, i;
    if (put_frustum(m, order, x, y, g, 0.0f, th + (sp->crown == CROWN_FRONDS ? 0.0f : 0.5f * ch), sp->trunk_r * size, sp->trunk_r * size * 0.55f, detail < 0.5f ? 3 : 5, turn, bark) != 0)
        return -1;
    if (sp->crown == CROWN_CARDS)
        return cards_crown(m, sp, order, mat, x, y, g, th, ch, cr, turn, shade, n);
    if (sp->crown == CROWN_FRONDS)
        return fronds_crown(m, order, mat, x + cosf(lean_a) * lean * 2.0f * size, y + sinf(lean_a) * lean * 2.0f * size, g + th, cr, turn, shade, n, &rs);
    for (k = 0; k < n; ++k)
    {
        /*  The tiers from the bottom up, each a share of the crown's height
         *  and overlapping the next.  Its reach is the conical envelope at
         *  its foot.  The crown leans a little, more toward the top. */
        float f = n > 1 ? (float)k / (float)(n - 1) : 0.0f, tier = ch / ((float)n * 0.55f + 0.45f);
        float top = th + f * (ch - tier) + tier, reach = cr * (0.2f + 0.8f * (1.0f - f)), drop = tier * sp->droop;
        float lx = x + cosf(lean_a) * lean * f * size, ly = y + sinf(lean_a) * lean * f * size;
        float ap[3] = {lx, ly, g + top}, un[3] = {lx, ly, g + top - drop * 1.15f};
        int   nb = sp->branches - 2 + (int)(seed_rand(&rs) * 4.99f);
        float tw = turn + (float)k * 2.39996f; /* the golden angle, so no two tiers' tips line up */
        float r0 = reach * (0.75f + 0.4f * seed_rand(&rs)), d0 = drop * (0.75f + 0.5f * seed_rand(&rs)), rf = r0, df = d0;
        for (i = 0; i < nb; ++i)
        {
            /*  Each branch tip its own reach and droop, the last one meeting
             *  the first so the tier closes. */
            float       r1 = i + 1 < nb ? reach * (0.75f + 0.4f * seed_rand(&rs)) : rf, d1 = i + 1 < nb ? drop * (0.75f + 0.5f * seed_rand(&rs)) : df;
            float       a0 = tw + 6.2831853f * (float)i / (float)nb, a1 = tw + 6.2831853f * (float)(i + 1) / (float)nb;
            float       am = 0.5f * (a0 + a1) + (seed_rand(&rs) - 0.5f) * 0.3f / (float)nb * 6.2831853f, vr = 0.45f + 0.25f * seed_rand(&rs);
            float       tip0[3] = {lx + cosf(a0) * r0, ly + sinf(a0) * r0, g + top - d0};
            float       val[3]  = {lx + cosf(am) * 0.5f * (r0 + r1) * vr, ly + sinf(am) * 0.5f * (r0 + r1) * vr, g + top - 0.5f * (d0 + d1) * vr};
            float       tip1[3] = {lx + cosf(a1) * r1, ly + sinf(a1) * r1, g + top - d1};
            float       t1[3][3], t2[3][3], t3[3][3];
            const float sh    = 2.0f * shade; /* the tree's shade rides on the reach, two a step (terrain_body.frag) */
            const float u1[3] = {sh, sh + 1.0f, sh + vr}, v1[3] = {0.0f, 1.0f, vr};
            const float u2[3] = {sh, sh + vr, sh + 1.0f}, v2[3] = {0.0f, vr, 1.0f};
            const float u3[3] = {sh + 1.0f, sh + 1.0f, sh}, v3[3] = {1.0f, 1.0f, 1.6f};
            memcpy(t1[0], ap, sizeof ap), memcpy(t1[1], tip0, sizeof tip0), memcpy(t1[2], val, sizeof val);
            memcpy(t2[0], ap, sizeof ap), memcpy(t2[1], val, sizeof val), memcpy(t2[2], tip1, sizeof tip1);
            memcpy(t3[0], tip0, sizeof tip0), memcpy(t3[1], tip1, sizeof tip1), memcpy(t3[2], un, sizeof un);
            if (leaf_tri(m, order, mat, (const float (*)[3])t1, u1, v1, lx, ly, 0.75f) != 0 || leaf_tri(m, order, mat, (const float (*)[3])t2, u2, v2, lx, ly, 0.75f) != 0 ||
                leaf_tri(m, order, mat, (const float (*)[3])t3, u3, v3, lx, ly, -0.6f) != 0)
                return -1;
            r0 = r1, d0 = d1;
        }
    }
    return 0;
}

/*  THE CANOPY over each tile, two bytes a tile: how much of it the
 *  crowns cover, and the species of its largest tree. */
R_MAP_BUF(static, uint8_t, s_canopy, 2)

const uint8_t *trees_canopy(void)
{
    return s_canopy;
}

/*  A tile's trees as simple crowns, for a distance where each tree is a
 *  few pixels.  Each tree stands where its whole one does, at the least
 *  detail the builder gives.  So the two levels cover the same pixels and
 *  cross over with no halo. */
static int tile_crowns(RMesh *m, const RCity *c, uint8_t mask_bit, float order, const Tree *t, int n, const float *bark)
{
    int k;
    for (k = 0; k < n; ++k)
        if (canopy(m, &SPECIES[t[k].species], t[k].species, order, t[k].x, t[k].y, surface_at_world(c, mask_bit, t[k].x, t[k].y), t[k].size, t[k].seed, 0.0f, bark[t[k].species]) != 0)
            return -1;
    return 0;
}

int trees_draw(RMesh *m, const RCity *c, uint8_t mask_bit)
{
    float   bark[N_SPECIES];
    int32_t col, row;
    int     s;
    for (s = 0; s < N_SPECIES; ++s)
    {
        char name[48];
        snprintf(name, sizeof name, "%s bark", SPECIES[s].name);
        bark[s] = material_define(name, SPECIES[s].bark, 0.95f);
    }
    for (row = 0; row < r_map; ++row)
        for (col = 0; col < r_map; ++col)
        {
            uint8_t b = c->xbld[row * r_map + col], *cv = &s_canopy[2 * (row * r_map + col)];
            Tree    t[TREES_MAX];
            int     n, k, big = 0, rc = 0;
            float   cover = 0.0f, order;
            ShapeId id;
            if (!mesh_wants_tile(col, row))
                continue;
            cv[0] = cv[1] = 0;
            if (!tree_byte(b))
                continue;
            n     = place_trees(c, mask_bit, col, row, b - 0x06, t);
            order = tile_order(c, col, row, mask_bit);
            if (n == 0)
                continue;
            /*  Each tree whole, in the chunk's range of trees. */
            m->range = MESH_RANGE_TREES;
            id       = shape_open("trees at %d,%d", (int)col, (int)row);
            {
                int32_t at = row * r_map + col;
                shape_tiles(&at, 1, 1);
            }
            for (k = 0; k < n && rc == 0; ++k)
            {
                const Species *sp = &SPECIES[t[k].species];
                rc                = canopy(m, sp, t[k].species, order, t[k].x, t[k].y, surface_at_world(c, mask_bit, t[k].x, t[k].y), t[k].size, t[k].seed, 1.0f, bark[t[k].species]);
                cover += 3.14159f * sp->crown_r * sp->crown_r * t[k].size * t[k].size;
                big = t[k].size > t[big].size ? k : big;
            }
            shape_close(id);
            /*  The tile's simple crowns, in the chunk's range of crowns. */
            m->range = MESH_RANGE_CROWNS;
            id       = shape_open("crowns of the trees at %d,%d", (int)col, (int)row);
            {
                int32_t at = row * r_map + col;
                shape_tiles(&at, 1, 1);
            }
            if (rc == 0)
                rc = tile_crowns(m, c, mask_bit, order, t, n, bark);
            shape_close(id);
            m->range = MESH_RANGE_STANDS;
            if (rc != 0)
                return -1;
            cv[0] = (uint8_t)lrintf(255.0f * (cover < 1.0f ? cover : 1.0f));
            cv[1] = (uint8_t)t[big].species;
        }
    return 0;
}
