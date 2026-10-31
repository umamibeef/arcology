/*  surface.c: THE DRAWN SURFACE, sampled: where the ground is under a
 *  point.  These read the terrain and the tile heights and nothing else,
 *  which is what makes them primitives anything laid on the ground can
 *  use. */
#include <math.h>
#include <string.h>

#include "mesh/internal.h"

#include "mesh/surface.h"

/*  The corners of a tile in (u, v), in the corner order. */
static const float CU[4] = {0.0f, 0.0f, 1.0f, 1.0f}, CV[4] = {0.0f, 1.0f, 1.0f, 0.0f};

/*  The height of the dual ground at (u, v) inside a tile: on the one of
 *  its eight triangles that holds the point.  Each edge e has two.  One
 *  runs from the tile's middle to the edge's first corner and middle.
 *  The other runs to the edge's middle and its second corner. */
static float dual_at(const float z[4], float zc, const float zm[4], float u, float v)
{
    int e, h;
    for (e = 0; e < 4; ++e)
        for (h = 0; h < 2; ++h)
        {
            int   a = EDGE_A[e], b = EDGE_B[e];
            float mu = 0.5f * (CU[a] + CU[b]), mv = 0.5f * (CV[a] + CV[b]);
            float pu = h ? mu : CU[a], pv = h ? mv : CV[a], pz = h ? zm[e] : z[a];
            float qu = h ? CU[b] : mu, qv = h ? CV[b] : mv, qz = h ? z[b] : zm[e];
            float d = (pu - 0.5f) * (qv - 0.5f) - (qu - 0.5f) * (pv - 0.5f);
            float l1, l2;
            if (fabsf(d) < 1e-9f)
                continue;
            l1 = ((u - 0.5f) * (qv - 0.5f) - (qu - 0.5f) * (v - 0.5f)) / d;
            l2 = ((pu - 0.5f) * (v - 0.5f) - (u - 0.5f) * (pv - 0.5f)) / d;
            if (l1 >= -1e-5f && l2 >= -1e-5f && l1 + l2 <= 1.0f + 1e-5f)
                return zc + (pz - zc) * l1 + (qz - zc) * l2;
        }
    return zc;
}

/*  The height of the drawn surface at (u, v) inside a tile.  U runs
 *  along the column and v along the row.  Open land is the dual ground.
 *  Any other tile is cut on the diagonal its top is cut on. */
static float surface_at(const RCity *c, int32_t col, int32_t row, uint8_t mask_bit, float u, float v)
{
    float z[4], zc, zm[4];
    Kind  k    = tile_top(c, col, row, mask_bit, z);
    int   code = k == T_LAND ? slope_code(c->map.xter[row * r_map + col]) : 0;
    if (u < 0.0f)
        u = 0.0f;
    if (u > 1.0f)
        u = 1.0f;
    if (v < 0.0f)
        v = 0.0f;
    if (v > 1.0f)
        v = 1.0f;
    if (k == T_LAND && tile_dual(c, col, row, &zc, zm))
        return dual_at(z, zc, zm, u, v);
    if (!cut_ne_sw(code))
        return u >= v ? z[NW] + (z[NE] - z[NW]) * u + (z[SE] - z[NE]) * v
                      : z[NW] + (z[SW] - z[NW]) * v + (z[SE] - z[SW]) * u;
    return u + v <= 1.0f ? z[NW] + (z[NE] - z[NW]) * u + (z[SW] - z[NW]) * v
                         : z[SE] + (z[NE] - z[SE]) * (1.0f - v) + (z[SW] - z[SE]) * (1.0f - u);
}

/*  The drawn surface of ONE tile at a world point, clamped to that tile.
 *  A piece clipped into a tile belongs to it.  So it takes that tile's
 *  top even at a shared edge, where the neighbor's may be a wall's
 *  height away. */
float surface_at_tile(const RCity *c, uint8_t mask_bit, int32_t col, int32_t row, float x, float y)
{
    if (col < 0)
        col = 0;
    if (row < 0)
        row = 0;
    if (col >= r_map)
        col = r_map - 1;
    if (row >= r_map)
        row = r_map - 1;
    return surface_at(c, col, row, mask_bit, x - (float)col, y - (float)row);
}

/*  Is the tile's top one plane?  Then its two triangles are coplanar and
 *  a face laid on it needs no cut along the fold. */
int tile_top_planar(const RCity *c, int32_t col, int32_t row, uint8_t mask_bit)
{
    float z[4], zc, zm[4];
    int   e;
    if (tile_top(c, col, row, mask_bit, z) == T_LAND && tile_dual(c, col, row, &zc, zm))
    {
        /*  The dual ground is one plane where it is flat. */
        for (e = 0; e < 4; ++e)
            if (fabsf(z[e] - zc) > 1e-4f || fabsf(zm[e] - zc) > 1e-4f)
                return 0;
        return 1;
    }
    return fabsf((z[NW] + z[SE]) - (z[NE] + z[SW])) < 1e-4f;
}

/*  The drawn surface at a world point, from whichever tile holds it, so
 *  a face that reaches past its tile's edge follows the ground there. */
float surface_at_world(const RCity *c, uint8_t mask_bit, float x, float y)
{
    int32_t col = (int32_t)floorf(x), row = (int32_t)floorf(y);
    if (col < 0)
        col = 0;
    if (row < 0)
        row = 0;
    if (col >= r_map)
        col = r_map - 1;
    if (row >= r_map)
        row = r_map - 1;
    return surface_at(c, col, row, mask_bit, x - (float)col, y - (float)row);
}
