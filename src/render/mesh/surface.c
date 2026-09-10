/*  surface.c: THE DRAWN SURFACE, sampled.
 *
 *  Where the ground is under a point, and how a corner of a corridor's
 *  shelf stands over it.  These read the terrain and the tile heights
 *  and nothing else, no corridor field, no station, no strip.  This is
 *  what makes them primitives every pass can use rather than a stage of
 *  the grading.
 *
 *  The graded FIELD itself, which the corridors write and later passes
 *  read, is a store and lives in mesh/grade.c beside the pass that fills
 *  it. */
#include <math.h>
#include <string.h>

#include "mesh/internal.h"
#include "pipeline.h"

/*  A grid corner's height off the station that owns it.  It is the
 *  line's height AT THE CORNER'S OWN PROJECTION on the centerline, not
 *  the nearest station's.  So the two corners across the band come out
 *  the same and the shelf is flat across by construction.  Along it the
 *  profile's own grade carries them.  It is held to the grade the
 *  profile itself may take.  It is held to the distance a corner can
 *  honestly be from its own station.  A local estimate over two close
 *  stations can read far steeper than the line ever goes.  Extrapolating
 *  on it throws the corner a level out. */
float surface_corner_height(const Sample *smp, int ns, int i, float dx, float dy, float shelf_grade, Family f)
{
    float along = dx * smp[i].dir.x + dy * smp[i].dir.y;
    float grade = 0.0f;
    if (i > 0 && i + 1 < ns && smp[i + 1].s - smp[i - 1].s > 1e-3f)
        grade = (smp[i + 1].z - smp[i - 1].z) / (smp[i + 1].s - smp[i - 1].s);
    if (grade > shelf_grade)
        grade = shelf_grade;
    if (grade < -shelf_grade)
        grade = -shelf_grade;
    {
        const float cap = net_family_rules(f)->shelf_along;
        if (along > cap)
            along = cap;
        if (along < -cap)
            along = -cap;
    }
    return smp[i].z + grade * along;
}


/*  The height of the drawn surface at (u, v) inside a tile.  U runs
 *  along the column and v along the row.  It comes from the corners
 *  tile_top gives and the diagonal the top is cut on. */
static float surface_at(const RCity *c, int32_t col, int32_t row, uint8_t mask_bit, float u, float v)
{
    float z[4];
    Kind  k    = tile_top(c, col, row, mask_bit, z);
    int   code = k == T_LAND ? slope_code(c->xter[row * R_MAP + col]) : 0;
    if (u < 0.0f)
        u = 0.0f;
    if (u > 1.0f)
        u = 1.0f;
    if (v < 0.0f)
        v = 0.0f;
    if (v > 1.0f)
        v = 1.0f;
    if (!cut_ne_sw(code))
        return u >= v ? z[NW] + (z[NE] - z[NW]) * u + (z[SE] - z[NE]) * v
                      : z[NW] + (z[SW] - z[NW]) * v + (z[SE] - z[SW]) * u;
    return u + v <= 1.0f ? z[NW] + (z[NE] - z[NW]) * u + (z[SW] - z[NW]) * v
                         : z[SE] + (z[NE] - z[SE]) * (1.0f - v) + (z[SW] - z[SE]) * (1.0f - u);
}


/*  The drawn surface of ONE tile at a world point, clamped to that tile.
 *  A piece of a band belongs to the tile it was clipped into.  Must take
 *  that tile's top even at a shared edge, where the neighbor's may be a
 *  wall's height away. */
float surface_at_tile(const RCity *c, uint8_t mask_bit, int32_t col, int32_t row, float x, float y)
{
    if (col < 0)
        col = 0;
    if (row < 0)
        row = 0;
    if (col >= R_MAP)
        col = R_MAP - 1;
    if (row >= R_MAP)
        row = R_MAP - 1;
    return surface_at(c, col, row, mask_bit, x - (float)col, y - (float)row);
}


/*  Is the tile's top one plane?  Then its two triangles are coplanar and
 *  a band on it needs no cut along the fold. */
int tile_top_planar(const RCity *c, int32_t col, int32_t row, uint8_t mask_bit)
{
    float z[4];
    tile_top(c, col, row, mask_bit, z);
    return fabsf((z[NW] + z[SE]) - (z[NE] + z[SW])) < 1e-4f;
}


/*  The drawn surface at a world point, from whichever tile holds it, so
 *  a strip that reaches past its tile's edge follows the ground there. */
float surface_at_world(const RCity *c, uint8_t mask_bit, float x, float y)
{
    int32_t col = (int32_t)floorf(x), row = (int32_t)floorf(y);
    if (col < 0)
        col = 0;
    if (row < 0)
        row = 0;
    if (col >= R_MAP)
        col = R_MAP - 1;
    if (row >= R_MAP)
        row = R_MAP - 1;
    return surface_at(c, col, row, mask_bit, x - (float)col, y - (float)row);
}


