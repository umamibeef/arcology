/*  shore.c: see shore.h. */
#include "shore.h"

#include "gpu/gpu.h"
#include "mesh/mesh.h"
#include "terrain.h"

#include <math.h>

/*  The terrain field.  Per tile it holds a water tile's distance to the
 *  nearest land, and a land tile's distance to the nearest water.  Both
 *  are in tiles by a two-pass chamfer transform, with 3-4 weights, so
 *  diagonals count about 1.4.  The water's depth comes in levels from
 *  ALTM.  The water shader lays foam by the first and grades color by
 *  the depth.  The ground shader lays sand and damp ground by the third. */
static void chamfer(int32_t *d)
{
    int32_t r, cc;
    for (r = 0; r < r_map; ++r)
        for (cc = 0; cc < r_map; ++cc)
        {
            int32_t i = r * r_map + cc, v = d[i];
            if (cc > 0 && d[i - 1] + 3 < v)
                v = d[i - 1] + 3;
            if (r > 0 && d[i - r_map] + 3 < v)
                v = d[i - r_map] + 3;
            if (r > 0 && cc > 0 && d[i - r_map - 1] + 4 < v)
                v = d[i - r_map - 1] + 4;
            if (r > 0 && cc < r_map - 1 && d[i - r_map + 1] + 4 < v)
                v = d[i - r_map + 1] + 4;
            d[i] = v;
        }
    for (r = r_map - 1; r >= 0; --r)
        for (cc = r_map - 1; cc >= 0; --cc)
        {
            int32_t i = r * r_map + cc, v = d[i];
            if (cc < r_map - 1 && d[i + 1] + 3 < v)
                v = d[i + 1] + 3;
            if (r < r_map - 1 && d[i + r_map] + 3 < v)
                v = d[i + r_map] + 3;
            if (r < r_map - 1 && cc < r_map - 1 && d[i + r_map + 1] + 4 < v)
                v = d[i + r_map + 1] + 4;
            if (r < r_map - 1 && cc > 0 && d[i + r_map - 1] + 4 < v)
                v = d[i + r_map - 1] + 4;
            d[i] = v;
        }
}

/*  THE SKY A TILE SEES.  The land round a tile hides part of the sky
 *  from it.  In each of eight ways the highest angle the land rises to
 *  within SKY_REACH tiles is the horizon that way.  Over a horizon at
 *  angle a, a cosine-weighted sky keeps cos^2 a of its light, which is
 *  1 / (1 + tan^2 a).  The
 *  mean over the eight ways is the share of the sky the tile sees, 1 on
 *  open level ground.  So a valley, a hollow or the foot of a slope is
 *  darker only by the land that stands over it.  Past the map's edge
 *  the land answers as the edge tile does. */
#define SKY_REACH 16

static const int8_t SKY_WAY[8][2] = {
    {1,  0 },
    {1,  1 },
    {0,  1 },
    {-1, 1 },
    {-1, 0 },
    {-1, -1},
    {0,  -1},
    {1,  -1}
};
static const uint8_t SKY_STEP[] = {1, 2, 3, 4, 6, 8, 11, 16};

R_MAP_BUF(static, float, s_sky_h, 1) /* each tile's height, as the last field measured it */
R_MAP_BUF(static, uint8_t, s_sky, 1) /* the share of the sky each tile sees, 255 for all */
static int32_t s_sky_n;              /* the map's side the two were measured on, 0 for none */

/*  A tile's height in tiles: its ground, with the corners its slope lifts
 *  a level higher taken as a mean.  Water lies at its surface. */
static float sky_height(const RCity *c, int32_t k)
{
    unsigned alt = c->map.altm[k];
    uint8_t  x   = c->map.xter[k];
    int      lift;
    if (is_water(x))
        return (float)rcity_alt_surface(alt, x) * MESH_LEVEL_H;
    lift = corner_lifts(slope_code(x));
    return ((float)rcity_alt_ground(alt) + 0.25f * (float)((lift & 1) + ((lift >> 1) & 1) + ((lift >> 2) & 1) + ((lift >> 3) & 1))) *
           MESH_LEVEL_H;
}

static uint8_t sky_seen(const float *h, int32_t cc, int32_t r)
{
    const float here = h[r * r_map + cc];
    float       sum  = 0.0f;
    int         w, s;
    for (w = 0; w < 8; ++w)
    {
        const float len = (SKY_WAY[w][0] && SKY_WAY[w][1]) ? 1.41421356f : 1.0f;
        float       top = 0.0f; /* the horizon's tangent */
        for (s = 0; s < (int)sizeof SKY_STEP; ++s)
        {
            int32_t x = cc + SKY_WAY[w][0] * SKY_STEP[s], y = r + SKY_WAY[w][1] * SKY_STEP[s];
            float   t;
            x   = x < 0 ? 0 : x >= r_map ? r_map - 1
                                         : x;
            y   = y < 0 ? 0 : y >= r_map ? r_map - 1
                                         : y;
            t   = (h[y * r_map + x] - here) / ((float)SKY_STEP[s] * len);
            top = t > top ? t : top;
        }
        sum += 1.0f / (1.0f + top * top);
    }
    return (uint8_t)lrintf(255.0f * sum / 8.0f);
}

/*  Measure the sky again where the heights changed, out to SKY_REACH
 *  round them, and everywhere when the map is new. */
static void sky_field(const RCity *c)
{
    int32_t lo_c = r_map, lo_r = r_map, hi_c = -1, hi_r = -1, cc, r;
    float  *h = s_sky_h;
    for (r = 0; r < r_map; ++r)
        for (cc = 0; cc < r_map; ++cc)
        {
            int32_t k = r * r_map + cc;
            float   v = sky_height(c, k);
            if (s_sky_n == r_map && v == h[k])
                continue;
            h[k] = v;
            lo_c = cc < lo_c ? cc : lo_c, hi_c = cc > hi_c ? cc : hi_c;
            lo_r = r < lo_r ? r : lo_r, hi_r = r > hi_r ? r : hi_r;
        }
    if (s_sky_n != r_map)
        lo_c = lo_r = 0, hi_c = hi_r = r_map - 1;
    s_sky_n = r_map;
    if (hi_c < 0)
        return;
    lo_c = lo_c - SKY_REACH < 0 ? 0 : lo_c - SKY_REACH;
    lo_r = lo_r - SKY_REACH < 0 ? 0 : lo_r - SKY_REACH;
    hi_c = hi_c + SKY_REACH >= r_map ? r_map - 1 : hi_c + SKY_REACH;
    hi_r = hi_r + SKY_REACH >= r_map ? r_map - 1 : hi_r + SKY_REACH;
    for (r = lo_r; r <= hi_r; ++r)
        for (cc = lo_c; cc <= hi_c; ++cc)
            s_sky[r * r_map + cc] = sky_seen(h, cc, r);
}

R_MAP_BUF(static, int32_t, s_shore_d, 1) /* the shore field's distance to the water */
R_MAP_BUF(static, int32_t, s_shore_w, 1) /* and the water it is measured to */

void shore_field(const RCity *c, uint8_t *out)
{
    int32_t      *d = s_shore_d, *w = s_shore_w;
    const int32_t big = 1 << 20;
    int32_t       k;
    for (k = 0; k < r_map * r_map; ++k)
    {
        d[k] = c->map.xter[k] >= 0x10u ? big : 0; /* water: distance to land */
        w[k] = c->map.xter[k] >= 0x10u ? 0 : big; /* land: distance to water */
    }
    chamfer(d);
    chamfer(w);
    sky_field(c);
    for (k = 0; k < r_map * r_map; ++k)
    {
        int32_t v     = d[k] * R_SHORE_SCALE / 3; /* tiles * scale */
        int32_t u     = w[k] * R_SHORE_SCALE / 3;
        int32_t depth = 0;
        if (c->map.xter[k] >= 0x10u)
        {
            /*  ALTM's two heights: the table in bits 5..9 over the bed in
             *  bits 0..4.  The map edge draws this column with 284. */
            depth = rcity_alt_table(c->map.altm[k]) - rcity_alt_ground(c->map.altm[k]);
            if (depth < 0)
                depth = 0;
            /*  Surface water on the ground, XTER 0x30 on, has no bed of
             *  its own.  Its table is its level, so it would read as the
             *  shallows' turquoise.  The original paints a stream the
             *  sea's blue, so it reads as a level deep.  Color only. */
            if (c->map.xter[k] >= 0x30u && depth < 1)
                depth = 1;
            depth *= R_SHORE_SCALE;
        }
        out[k * 4]     = (uint8_t)(v > 255 ? 255 : v);
        out[k * 4 + 1] = (uint8_t)(depth > 255 ? 255 : depth);
        out[k * 4 + 2] = (uint8_t)(u > 255 ? 255 : u);
        out[k * 4 + 3] = s_sky[k];
    }
}
