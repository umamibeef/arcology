/*  draw/laid.c: see laid.h. */
#include "laid.h"

#include <math.h>
#include <stddef.h>

#include "geo.h"
#include "material.h"
#include "mesh/mesh.h"
#include "mesh/shape.h"
#include "network/network.h"
#include "network/shape.h"

static const uint8_t *s_road, *s_deck;
static const float   *s_cap;
static int            s_nroad, s_ndeck;

void laid_streets(const uint8_t *road, const float *cap, int n)
{
    s_road = road, s_cap = cap, s_nroad = n;
}

void laid_decks(const uint8_t *road, int n)
{
    s_deck = road, s_ndeck = n;
}

int laid_road(int road)
{
    return s_road && road >= 0 && road < s_nroad && s_road[road];
}

int laid_reach(int road)
{
    return laid_road(road) || (road >= 0 && road < s_nroad && !shape_road(road)->ngeom);
}

float laid_cap(int road, int contact)
{
    return s_cap && road >= 0 && road < s_nroad ? s_cap[2 * road + (contact ? 1 : 0)] : 0.0f;
}

int laid_deck(int road)
{
    return s_deck && road >= 0 && road < s_ndeck && s_deck[road];
}

void laid_materials(float *asphalt, float *sidewalk)
{
    static const float ASPHALT[3] = {0.239f, 0.259f, 0.278f}, CONCRETE[3] = {0.62f, 0.61f, 0.57f};
    *asphalt  = material_define("asphalt", ASPHALT, 0.9f);
    *sidewalk = material_define("sidewalk", CONCRETE, 0.95f);
}

/*  Whether a point lies within draw_margin tiles of the window and
 *  `reach` more: always where there is no window. */
int laid_near_xy(float x, float y, float reach)
{
    static int ix = -1;
    int32_t    box[MESH_WINDOW_BOXES][4];
    int        n = mesh_window(box), k;
    float      r = geo_num(&ix, "draw_margin") + reach;
    if (!n)
        return 1;
    for (k = 0; k < n; ++k)
        if (x >= (float)box[k][0] - r && y >= (float)box[k][1] - r && x <= (float)box[k][2] + r && y <= (float)box[k][3] + r)
            return 1;
    return 0;
}

int laid_near(int road)
{
    int32_t box[MESH_WINDOW_BOXES][4];
    float   s, L, x, y, h;
    if (!mesh_window(box))
        return 1;
    L = shape_road(road)->length;
    for (s = 0.0f;; s = fminf(s + 1.0f, L))
    {
        shape_at(road, s, &x, &y, &h);
        if (laid_near_xy(x, y, 0.0f))
            return 1;
        if (s >= L)
            return 0;
    }
}

void laid_ref_junction(int junction, int net)
{
    shape_ref(NLINK_JUNCTION, network_junction(junction)->id);
    shape_net(net);
}

void laid_ref_road(int road)
{
    shape_ref(NLINK_ROAD, network_road(road)->id);
    shape_net(network_road(road)->net);
}
