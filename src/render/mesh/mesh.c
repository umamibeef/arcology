#include <stdio.h>
/*  mesh.c: building the mesh, and the query tool.
 *  See mesh/internal.h. */
#include "dump.h"
#include "geo.h"
#include "log.h"
#include "mesh/internal.h"
#include "mesh/shape.h"
#include "opt.h"
#include "prof.h"
#include <time.h>

/*  --times: where a build's time goes, for review.  Wall clock, ms. */
double tms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec * 1000.0 + (double)t.tv_nsec / 1e6;
}

/*  What a tile's composition wants beyond its own place: set once a
 *  build.  So one tile's ground can be composed from anywhere: the loop
 *  below, or compose.c, which drives the build. */
static struct
{
    RMesh       *m;
    const RCity *c;
    uint8_t      mask_bit;
    float        land[3];
    int          underground;
    int32_t      dump_r, dump_c;
} s_tp;

/*  What a build composes its tiles from, set once a build (build.c).  The mesh, the city, the ground's mean color and the tile
 *  --mesh-dump names.  The mesh's own counters start at nought. */
void mesh_build_begin(RMesh *m, const RCity *c, const RAtlas *a, const RAtlasLevel *l, int underground, int32_t dump_r, int32_t dump_c)
{
    /*  Which picture the ground's mean color is taken from is a number
     *  in the geo store.  So is what a run whose art set has no such
     *  picture falls back on.  The mean itself is arithmetic. */
    static int gix_art = -1, gix_r = -1, gix_g = -1, gix_b = -1;
    float      land_fb[3];
    float      land_col[3];
    if (mesh_lists_begin(m, MESH_RANGES * MESH_CHUNKS, MESH_CHUNKS) != 0)
        R_ERR("mesh", "no memory for the lists of %d chunks", MESH_CHUNKS);
    mesh_beyond_reset();
    m->to_water = 0;
    m->n_walls  = 0;
    land_fb[0]  = geo_num(&gix_r, "ground_red");
    land_fb[1]  = geo_num(&gix_g, "ground_green");
    land_fb[2]  = geo_num(&gix_b, "ground_blue");
    tile_colour(a, l, (int32_t)geo_num(&gix_art, "ground_art"), land_col, land_fb);
    land_col[2] = MAT_GROUND;
    s_tp.m = m, s_tp.c = c, s_tp.mask_bit = city_corner_mask(0);
    shape_city(c);
    memcpy(s_tp.land, land_col, sizeof s_tp.land);
    s_tp.underground = underground;
    s_tp.dump_r = dump_r, s_tp.dump_c = dump_c;
}

/*  ONE TILE'S GROUND, gathered.
 *
 *      Its corners and their heights.
 *      The water over it.
 *      The pad a network cut.
 *      What each of its four neighbors reaches it at.
 *
 *  Answers 0 where this build wants no such tile.
 *
 *  Nothing is drawn here and no shape is opened.  The world opens a
 *  shape for the tile and mesh_ground_tile draws it (mesh/ground.c).
 *  The heightfield and the slope codes are settled by the stages above. */
int mesh_ground_fan(int32_t col, int32_t row, TileFan *out)
{
    return mesh_wants_tile(col, row) && mesh_ground_fan_any(col, row, out);
}

int mesh_ground_fan_any(int32_t col, int32_t row, TileFan *out)
{
    RMesh        *m           = s_tp.m;
    const RCity  *c           = s_tp.c;
    const uint8_t mask_bit    = s_tp.mask_bit;
    const float  *land_col    = s_tp.land;
    const int     underground = s_tp.underground;
    const int32_t dump_r = s_tp.dump_r, dump_c = s_tp.dump_c;
    (void)m, (void)underground;
    int32_t idx  = row * r_map + col;
    uint8_t xter = c->map.xter[idx], xbld = c->xbld[idx];
    int32_t code  = slope_code(xter);
    float   order = tile_order(c, col, row, mask_bit);
    float   z[4], p[4][3], bed[4][3];
    Kind    kind = tile_top(c, col, row, mask_bit, z);
    int     wet  = water_top(c, idx, kind); /* a body of water under the top */
    int     k;

    for (k = 0; k < 4; ++k)
    {
        grid_point(col, row, k, z[k], p[k]);
        grid_point(col, row, k, s_b[corner_gi(col, row, k)], bed[k]);
    }
    if (row == dump_r && col == dump_c)
        dumpf("tile r%d c%d: xter %02x xbld %02x kind %d order %g  "
              "NW %g NE %g SE %g SW %g  bed %g %g %g %g\n",
              (int)row,
              (int)col,
              xter,
              xbld,
              (int)kind,
              order,
              z[NW],
              z[NE],
              z[SE],
              z[SW],
              bed[NW][2],
              bed[NE][2],
              bed[SE][2],
              bed[SW][2]);

    /*  mesh_ground_tile draws over all that.  It draws the top face, the
     *  seabed under a body of water and the wall down to each neighbor.
     *  It also draws the sediment at the cut edges of the map.  The heightfield,
     *  the slope codes and the pads a network cut are settled above. */
    {
        TileFan t;
        int     ee;
        memset(&t, 0, sizeof t);
        t.m = m, t.c = c, t.mask_bit = mask_bit;
        t.col = col, t.row = row;
        t.code = code, t.order = order;
        t.kind = (int)kind, t.wet = wet, t.underground = underground;
        t.xter = xter, t.xbld = xbld;
        for (k = 0; k < 4; ++k)
        {
            memcpy(t.p[k], p[k], sizeof t.p[0]);
            memcpy(t.bed[k], bed[k], sizeof t.bed[0]);
            t.z[k]   = z[k];
            t.pad[k] = s_h[corner_gi(col, row, k)];
        }
        memcpy(t.land, land_col, sizeof t.land);
        /*  The middles: the dual ground's own where it is drawn.  Any other
         *  top has its edge middles on the straight edge between two
         *  corners, and its middle on the diagonal it is folded on. */
        {
            float zc = cut_ne_sw(kind == T_LAND ? code : 0) ? 0.5f * (z[NE] + z[SW]) : 0.5f * (z[NW] + z[SE]), zm[4];
            for (ee = 0; ee < 4; ++ee)
                zm[ee] = 0.5f * (z[EDGE_A[ee]] + z[EDGE_B[ee]]);
            t.dual   = kind == T_LAND && tile_dual(c, col, row, &zc, zm);
            t.ctr[0] = (float)col + 0.5f, t.ctr[1] = (float)row + 0.5f, t.ctr[2] = zc;
            for (ee = 0; ee < 4; ++ee)
            {
                t.mid[ee][0] = 0.5f * (p[EDGE_A[ee]][0] + p[EDGE_B[ee]][0]);
                t.mid[ee][1] = 0.5f * (p[EDGE_A[ee]][1] + p[EDGE_B[ee]][1]);
                t.mid[ee][2] = zm[ee];
            }
        }
        for (ee = 0; ee < 4; ++ee)
        {
            int32_t nr = row + EDGE_DR[ee], nc = col + EDGE_DC[ee];
            float   zn[4];
            Kind    kn;
            t.nbr[ee].ia  = EDGE_A[ee];
            t.nbr[ee].ib  = EDGE_B[ee];
            t.nbr[ee].nx  = EDGE_N[ee][0];
            t.nbr[ee].ny  = EDGE_N[ee][1];
            t.nbr[ee].rim = ee == E_E   ? col == r_map - 1
                            : ee == E_S ? row == r_map - 1
                            : ee == E_W ? col == 0
                                        : row == 0;
            if (nr < 0 || nc < 0 || nr >= r_map || nc >= r_map)
                continue;
            kn              = tile_top(c, nc, nr, mask_bit, zn);
            t.nbr[ee].there = 1;
            t.nbr[ee].za    = zn[NBR_A[ee]];
            t.nbr[ee].zb    = zn[NBR_B[ee]];
            {
                float zc, zm[4];
                t.nbr[ee].zm = kn == T_LAND && tile_dual(c, nc, nr, &zc, zm) ? zm[(ee + 2) & 3]
                                                                             : 0.5f * (t.nbr[ee].za + t.nbr[ee].zb);
            }
            t.nbr[ee].water = water_top(c, nr * r_map + nc, kn);
            t.nbr[ee].sea   = is_water(c->map.xter[nr * r_map + nc]);
            t.nbr[ee].xbld  = c->xbld[nr * r_map + nc];
            t.nbr[ee].cliff = tile_cliff(c, idx, nr * r_map + nc);
        }
        /*  The tile is a PIECE, named by where it is. */
        mesh_piece_is(mesh_piece_key(2, 0, (uint32_t)idx));
        prof_work(WORK_GROUND, (float)col, (float)row, (float)col + 1.0f, (float)row + 1.0f);
        *out = t;
    }
    return 1;
}

/*  ONE TILE'S ZONE, gathered, for the map view's tint.  The zone it
 *  carries or the code a placed structure takes instead, and the quad a
 *  hair over its ground.  Answers 0 where there is no tint to draw. */
int mesh_tint_fan(int32_t col, int32_t row, TileFan *out)
{
    RMesh        *m        = s_tp.m;
    const RCity  *c        = s_tp.c;
    const uint8_t mask_bit = s_tp.mask_bit;
    (void)m;
    mesh_piece_is(0); /* the ground's pieces end where the tints begin */
    if (!mesh_wants_tile(col, row))
        return 0;
    int32_t idx  = row * r_map + col;
    int     zone = zone_of(c->xzon[idx]);
    float   z[4];
    Kind    kind;
    /*  A tile something was PLACED on carries its own code, not the zone
     *  under it (structure_tint).  0 leaves the tile to its zone. */
    if (structure_tint(c->xbld[idx]))
        zone = structure_tint(c->xbld[idx]);
    if (!zone)
        return 0;
    kind = tile_top(c, col, row, mask_bit, z);
    (void)kind;
    {
        TileFan t;
        int     k;
        memset(&t, 0, sizeof t);
        t.m = m, t.c = c, t.mask_bit = mask_bit;
        t.col = col, t.row = row, t.zone = zone;
        t.order = tile_order(c, col, row, mask_bit);
        for (k = 0; k < 4; ++k)
        {
            grid_point(col, row, k, z[k], t.p[k]);
            t.z[k] = z[k];
        }
        /*  The tint is a PIECE as the ground's tile is. */
        mesh_piece_is(mesh_piece_key(3, 0, (uint32_t)idx));
        *out = t;
    }
    return 1;
}

/* ---- the query tool ---------------------------------------------------- */

int mesh_tile_corners(const RCity *c, int32_t col, int32_t row, int underground, float z[4])
{
    float o[4], t[4];
    int   k;
    if (col < 0 || row < 0 || col >= r_map || row >= r_map)
        return -1;
    if (underground)
        for (k = 0; k < 4; ++k)
            t[k] = s_b[corner_gi(col, row, k)];
    else
        tile_top(c, col, row, city_corner_mask(0), t);
    /* out in NW, NE, SE, SW order, whatever the corner enum's is */
    o[0] = t[NW];
    o[1] = t[NE];
    o[2] = t[SE];
    o[3] = t[SW];
    memcpy(z, o, sizeof o);
    return 0;
}

int mesh_query(const RCity *c, int32_t col, int32_t row, char *buf, size_t n)
{
    static const char *const names[] = {"ground", "water", "levelled pad", "sloped plane"};
    int32_t                  idx     = row * r_map + col;
    float                    z[4], b[4];
    Kind                     kind;
    int                      k;
    if (col < 0 || row < 0 || col >= r_map || row >= r_map || !buf || n == 0)
        return -1;
    kind = tile_top(c, col, row, city_corner_mask(0), z);
    for (k = 0; k < 4; ++k)
        b[k] = s_b[corner_gi(col, row, k)];
    if (water_top(c, idx, kind))
        snprintf(buf, n, "Mesh: %s, surface %g, seabed NW %g NE %g SE %g SW %g", kind == T_PAD ? "pad on the water" : names[kind], z[NW], b[NW], b[NE], b[SE], b[SW]);
    else if (kind == T_PAD)
        snprintf(buf, n, "Mesh: %s at %g%s", names[kind], z[NW], saddle_lift(c, idx) ? " (saddle: lifted a level, $17528)" : "");
    else
        snprintf(buf, n, "Mesh: %s, NW %g NE %g SE %g SW %g", names[kind], z[NW], z[NE], z[SE], z[SW]);
    return 0;
}
