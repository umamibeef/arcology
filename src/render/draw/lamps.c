/*  draw/lamps.c: see lamps.h.  Each lamp of a row stands at its road's
 *  `s`, every `distance` along it, and at the row's `t` across it.  The
 *  shape (layer 3) gives the point, the way the road runs there and its
 *  height.  The lamp is a model, its height in levels at `lamp_level_m`
 *  meters a level.  The game's art draws a level shorter than a real
 *  story.  So a lamp is scaled to stand among the art as a real one
 *  stands among houses. */
#include "lamps.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "geo.h"
#include "laid.h"
#include "mesh/model.h"
#include "mesh/shape.h"
#include "mesh/tile.h"
#include "network/network.h"
#include "network/shape.h"

/*  How far a lamp's arm reaches over the road, in tiles: the model's
 *  (model.c, lamp_parts).  A cobra head throws its light forward and
 *  down, so its pool lies LAMP_THROW tiles out over the road. */
#define LAMP_REACH 0.15f
#define LAMP_THROW 0.28f

R_MAP_BUF(static, uint8_t, s_light, 2 * LAMP_TEXELS * LAMP_TEXELS)

const uint8_t *lamps_light(void)
{
    return s_light;
}

/*  Lamp k of object o: where it stands, the way its arm reaches and the
 *  height of its road there.  0 where the road has no shape. */
static int lamp_at(const NObject *o, int k, float *px, float *py, float *fx, float *fy, float *z)
{
    float su = o->s + (float)k * o->distance, sl = shape_s(o->road, su), x, y, hdg, nx, ny;
    if (!shape_at(o->road, sl, &x, &y, &hdg))
        return 0;
    nx = -sinf(hdg), ny = cosf(hdg); /* toward higher t */
    *px = x + nx * o->t, *py = y + ny * o->t;
    *fx = o->facing < 0 ? -nx : nx, *fy = o->facing < 0 ? -ny : ny;
    *z = shape_height(o->road, sl);
    return 1;
}

/*  One lamp's light, added to the texels of the tiles the build wants
 *  round (x, y).  It is a soft pool `radius` tiles across its middle, on
 *  a road at height `z` in levels.  Where light from another height
 *  already lies, the brighter of the two holds the texel. */
static void light_pool(float x, float y, float z, float radius)
{
    const int n = r_map * LAMP_TEXELS, reach = (int)ceilf(radius * 1.5f * LAMP_TEXELS);
    const int cx = (int)floorf(x * LAMP_TEXELS), cy = (int)floorf(y * LAMP_TEXELS);
    int       i, j;
    for (j = cy - reach; j <= cy + reach; ++j)
        for (i = cx - reach; i <= cx + reach; ++i)
        {
            float dx, dy, v;
            int   at;
            if (i < 0 || j < 0 || i >= n || j >= n || !mesh_wants_tile(i / LAMP_TEXELS, j / LAMP_TEXELS))
                continue;
            dx = ((float)i + 0.5f) / LAMP_TEXELS - x, dy = ((float)j + 0.5f) / LAMP_TEXELS - y;
            v = 255.0f * expf(-(dx * dx + dy * dy) / (2.0f * 0.25f * radius * radius));
            {
                float   hz = z * LAMP_LEVEL_STEP;
                uint8_t h  = (uint8_t)(hz < 0.0f ? 0.0f : hz > 255.0f ? 255.0f
                                                                      : hz + 0.5f);
                at         = 2 * (j * n + i);
                if (s_light[at] > 0 && abs((int)s_light[at + 1] - (int)h) > (int)LAMP_LEVEL_STEP)
                {
                    if (v <= (float)s_light[at])
                        continue;
                }
                else
                    v += (float)s_light[at];
                s_light[at]     = (uint8_t)(v > 255.0f ? 255.0f : v);
                s_light[at + 1] = h;
            }
        }
}

int lamps_draw(RMesh *m, const RCity *c, uint8_t mask_bit)
{
    static int  c_level = -1, c_pool = -1;
    const int   one = net_model_find("street lamp"), two = net_model_find("street lamp, twin");
    const float level_m = geo_num(&c_level, "lamp_level_m"), pool = geo_num(&c_pool, "lamp_pool_tiles");
    const int   n_side = r_map * LAMP_TEXELS;
    int         i, j;
    /*  The light of the tiles this build lays is laid again from nought. */
    for (j = 0; j < n_side; ++j)
        for (i = 0; i < n_side; ++i)
            if (mesh_wants_tile(i / LAMP_TEXELS, j / LAMP_TEXELS))
                s_light[2 * (j * n_side + i)] = 0, s_light[2 * (j * n_side + i) + 1] = 0;
    for (i = 0; i < network_objects(); ++i)
    {
        const NObject *o = network_object(i);
        int            k, n;
        if (o->distance <= 0.0f)
            continue;
        n = (int)floorf(o->length / o->distance + 1e-3f) + 1;
        for (k = 0; k < n; ++k)
        {
            float   px, py, fx, fy, z;
            int32_t col, row;
            if (!lamp_at(o, k, &px, &py, &fx, &fy, &z))
                continue;
            light_pool(px + fx * LAMP_THROW, py + fy * LAMP_THROW, z, pool);
            if (o->type == NOBJ_LAMP_TWIN)
                light_pool(px - fx * LAMP_THROW, py - fy * LAMP_THROW, z, pool);
            col = (int32_t)floorf(px), row = (int32_t)floorf(py);
            if (!mesh_wants_tile(col, row))
                continue;
            {
                ShapeId id = shape_open("street lamp %d of road %016llx", k, (unsigned long long)network_road(o->road)->id);
                int     rc;
                laid_ref_road(o->road);
                rc = net_model_put_on(o->type == NOBJ_LAMP_TWIN ? two : one, m, c, mask_bit, tile_order(c, col, row, mask_bit), px, py, fx, fy, o->height / level_m, 0.0f, 0.0f, z, z, 1);
                shape_close(id);
                if (rc != 0)
                    return -1;
            }
        }
    }
    return 0;
}
