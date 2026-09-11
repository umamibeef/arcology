#include <stdio.h>
/*  mesh.c: building the mesh, and the query tool.
 *  See mesh/internal.h. */
#include "dump.h"
#include "log.h"
#include "mesh/internal.h"
#include "pipeline.h"
#include "build.h"
#include "net/net.h"
#include "opt.h"
#include "script.h"
#include "incr.h"
#include <time.h>

/*  --times: where a build's time goes, for review.  Wall clock, ms. */
int s_pass; /* 0 not capping, 1 collecting the caps, 2 building on them */

/*  --times: where a build's time goes, for review.  Wall clock, ms. */
double tms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec * 1000.0 + (double)t.tv_nsec / 1e6;
}
void tnote(const char *what, double t0)
{
    if (g_dev.times)
        dumpf("time  pass %d  %-28s %7.1f ms\n", s_pass, what, tms() - t0);
}


/*  A mesh is built in two passes when there are lines.  The first
 *  GRADES: it walks every network and records the surface each corridor
 *  wants (the shelves, s_tilez), and draws nothing.  Every emitter is a
 *  no-op under s_pass 1.  The second BUILDS the world on those shelves.
 *  They are the same pass body, mesh_build_pass, told which it is. */


/*  What a tile's composition wants beyond its own place: set once a
 *  pass.  So one tile's ground can be composed from anywhere: the loop
 *  below, or the script that drives the build. */
static struct
{
    RMesh       *m;
    const RCity *c;
    uint8_t      mask_bit;
    float        land[3];
    int          underground;
    int32_t      dump_r, dump_c;
} s_tp;

/*  THE FIELD, built when the DRIVE asks for it: after the rule has said
 *  where every tile's top comes from.  This is because a water tile's
 *  bed is clamped under the surface drawn over it and that surface is
 *  the rule's answer. */
void mesh_field(void)
{
    const RCity *c = s_tp.c;
    if (!c)
        return;
    build_field(c);
    /*  --plan-dump 1 writes the world as numbers for tools/plan.py.
     *  This draws a city from above: the field's heights, what each tile
     *  carries, and (with the paths) the corridors and their
     *  centerlines.  A plan view is how this is inspected. */
    if (s_pass != 1 && g_dev.plan_dump)
    {
        int32_t gx, gy;
        dumpf("FIELD %d\n", GRID);
        for (gy = 0; gy < GRID; ++gy)
        {
            for (gx = 0; gx < GRID; ++gx)
                dumpf("%s%.3f", gx ? " " : "", (double)s_h[gy * GRID + gx]);
            dumpf("\n");
        }
        /*  The corridors' shelves, four corners a tile in the order NW
         *  NE SE SW, 1e9 where a tile has none.  What a network tile is
         *  drawn from (tile.c tile_top).  So continuity along a corridor
         *  can be measured. */
        dumpf("SHELF %d\n", R_MAP);
        for (gy = 0; gy < R_MAP; ++gy)
        {
            for (gx = 0; gx < R_MAP; ++gx)
                dumpf("%s%.3f %.3f %.3f %.3f", gx ? " " : "", (double)s_tilez[(gy * R_MAP + gx) * 4 + NW], (double)s_tilez[(gy * R_MAP + gx) * 4 + NE], (double)s_tilez[(gy * R_MAP + gx) * 4 + SE], (double)s_tilez[(gy * R_MAP + gx) * 4 + SW]);
            dumpf("\n");
        }
        dumpf("XBLD %d\n", R_MAP);
        for (gy = 0; gy < R_MAP; ++gy)
        {
            for (gx = 0; gx < R_MAP; ++gx)
                dumpf("%s%02x", gx ? " " : "", (unsigned)c->xbld[gy * R_MAP + gx]);
            dumpf("\n");
        }
        dumpf("XTER %d\n", R_MAP);
        for (gy = 0; gy < R_MAP; ++gy)
        {
            for (gx = 0; gx < R_MAP; ++gx)
                dumpf("%s%02x", gx ? " " : "", (unsigned)c->xter[gy * R_MAP + gx]);
            dumpf("\n");
        }
    }}

/*  ONE TILE'S GROUND, gathered.
 *
 *      Its corners and their heights.
 *      The water over it.
 *      The pad a network cut.
 *      What each of its four neighbors reaches it at.
 *
 *  Answers 0 where this build wants no such tile.
 *
 *  Nothing is drawn here and no shape is opened.  Both are the script's:
 *  it asks for the tile, opens a shape for it and composes it.  The
 *  heightfield and the slope codes are settled by the stages above. */
int mesh_ground_fan(int32_t col, int32_t row, TileFan *out)
{
    RMesh        *m           = s_tp.m;
    const RCity  *c           = s_tp.c;
    const uint8_t mask_bit    = s_tp.mask_bit;
    const float  *land_col    = s_tp.land;
    const int     underground = s_tp.underground;
    const int32_t dump_r = s_tp.dump_r, dump_c = s_tp.dump_c;
    (void)m, (void)underground;
    if (s_pass != 1 && !incr_want_tile(col, row))
        return 0; /* an edit's build: this chunk stands */
    int32_t idx  = row * R_MAP + col;
    uint8_t xter = c->xter[idx], xbld = c->xbld[idx];
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

    /*  What is DRAWN over all that is the SCRIPT'S (arc.rules.tile).  It
     *  draws the top face, and the seabed under a body of water.  It
     *  draws the wall down to each neighbor, and the sediment at the
     *  map's cut edges.  The heightfield, the slope codes and the pads a
     *  network cut are settled above.  With no rule the ground is not
     *  drawn at all. */
    {
        TileFan t;
        int     ee;
        memset(&t, 0, sizeof t);
        t.m = m, t.c = c, t.mask_bit = mask_bit;
        t.col = col, t.row = row;
        t.code = code, t.order = order;
        t.kind = (int)kind, t.wet = wet, t.underground = underground;
        t.corridor = corridor_tile(col, row);
        t.xter = xter, t.xbld = xbld;
        for (k = 0; k < 4; ++k)
        {
            memcpy(t.p[k], p[k], sizeof t.p[0]);
            memcpy(t.bed[k], bed[k], sizeof t.bed[0]);
            t.z[k]   = z[k];
            t.pad[k] = s_h[corner_gi(col, row, k)];
        }
        memcpy(t.land, land_col, sizeof t.land);
        for (ee = 0; ee < 4; ++ee)
        {
            int32_t nr = row + EDGE_DR[ee], nc = col + EDGE_DC[ee];
            float   zn[4];
            Kind    kn;
            t.nbr[ee].ia = EDGE_A[ee];
            t.nbr[ee].ib = EDGE_B[ee];
            t.nbr[ee].nx = EDGE_N[ee][0];
            t.nbr[ee].ny = EDGE_N[ee][1];
            t.nbr[ee].rim = ee == E_E   ? col == R_MAP - 1
                            : ee == E_S ? row == R_MAP - 1
                            : ee == E_W ? col == 0
                                        : row == 0;
            if (nr < 0 || nc < 0 || nr >= R_MAP || nc >= R_MAP)
                continue;
            kn                  = tile_top(c, nc, nr, mask_bit, zn);
            t.nbr[ee].there     = 1;
            t.nbr[ee].za        = zn[NBR_A[ee]];
            t.nbr[ee].zb        = zn[NBR_B[ee]];
            t.nbr[ee].water     = water_top(c, nr * R_MAP + nc, kn);
            t.nbr[ee].sea       = is_water(c->xter[nr * R_MAP + nc]);
            t.nbr[ee].xbld      = c->xbld[nr * R_MAP + nc];
            t.nbr[ee].corridor  = corridor_tile(nc, nr);
        }
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
    if (s_pass != 1 && !incr_want_tile(col, row))
        return 0;
    int32_t idx  = row * R_MAP + col;
    int     zone = c->xzon[idx] & 0x0Fu;
    float   z[4];
    Kind    kind;
    /*  A tile something was PLACED on carries its own code rather than
     *  the zone under it: which structures.  Which code each takes, is
     *  the script's (scripts/ground_tiles.lua), 0 leaving the tile to
     *  its zone. */
    if (structure_tint(c->xbld[idx]))
        zone = structure_tint(c->xbld[idx]);
    if (!zone)
        return 0;
    kind    = tile_top(c, col, row, mask_bit, z);
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
        *out = t;
    }
    return 1;
}

int mesh_build_pass(RMesh *m, const RCity *c, const RAtlas *a, const RAtlasLevel *l, int underground, int rotated, int lines)
{
    static const float land_fb[3] = {0.45f, 0.62f, 0.30f};
    float              land_col[3];
    const uint8_t      mask_bit = city_corner_mask(c->rotation);
    int                dump_r = -1, dump_c = -1;
    s_check_xbld                = lines ? c->xbld : NULL;
    m->net.n_pts                  = 0;
    m->net.n_segs                 = 0;
    m->threadnet.n_pts              = 0;
    m->threadnet.n_segs             = 0;
    m->n_laps                    = 0;
    m->n_rsigs                    = 0;
    if (s_pass != 2)
    {
        /*  The junction controls are stage three's, worked out with the
         *  trims and the meets that turn on them (walk/walk.c).  So they
         *  are cleared where those are and the building pass reads what
         *  the grading pass left. */
        memset(s_junc_ctrl, 0, sizeof s_junc_ctrl);
        fit_stats_reset(); /* the building pass fits nothing: keep the grading pass's count */
    }
    m->n_land   = 0;
    m->n_water  = 0;
    m->to_water = 0;
    m->n_walls  = 0;
    if (s_pass == 2)
    {
        /*  A corridor tile must be graded on all four corners or its
         *  surface tilts through the line.  Where several segments meet,
         *  one corner could keep the hillside's height while its
         *  neighbors took the line's.  So a tile the script names as a
         *  corridor's (`corridor_fill`), with any graded corner, has the
         *  rest filled in.  They take the mean of those it has, twice
         *  over so it carries across a junction. */
        int32_t pass2, col2, row2;
        for (pass2 = 0; pass2 < 2; ++pass2)
            for (row2 = 0; row2 < R_MAP; ++row2)
                for (col2 = 0; col2 < R_MAP; ++col2)
                {
                    int32_t gi[4] = {row2 * GRID + col2, row2 * GRID + col2 + 1, (row2 + 1) * GRID + col2, (row2 + 1) * GRID + col2 + 1};
                    float   sum = 0.0f, low = 1e9f;
                    int     k2, have        = 0;
                    if (!script_bytes("corridor_fill")[c->xbld[row2 * R_MAP + col2]])
                        continue; /* the script names no corridor here */
                    for (k2 = 0; k2 < 4; ++k2)
                        if (s_corr[gi[k2]] == 1 && s_zcap[gi[k2]] < 1e8f)
                        {
                            sum += s_zcap[gi[k2]];
                            if (s_zlow[gi[k2]] < low)
                                low = s_zlow[gi[k2]];
                            ++have;
                        }
                    if (!have || have == 4)
                        continue;
                    for (k2 = 0; k2 < 4; ++k2)
                        if (s_corr[gi[k2]] != 1)
                        {
                            /*  A corner filled in from its tile's others
                             *  inherits their ceiling too, or the
                             *  smoothing will lift it over the line. */
                            s_zcap[gi[k2]] = sum / (float)have;
                            if (low < s_zlow[gi[k2]])
                                s_zlow[gi[k2]] = low;
                            s_corr[gi[k2]] = 1;
                        }
                }
    }

    /*  THE ORDER the world is composed in is the SCRIPT'S
     *  (scripts/compose/world.lua): which tiles, in what order, and
     *  which of the network passes run at all.  There is no C loop
     *  behind it: a build with no rule composes nothing, and says so
     *  rather than drawing an empty city.
     *
     *  The pass STOPS HERE, before the field.  The drive asks
     *  arc.rules.terrain which of six places every tile's top comes
     *  from, and then w:field builds the field from that answer.  A
     *  water tile's bed is clamped under the surface drawn over it.  So
     *  the terrain has to be settled first, and it is the DRIVE that
     *  says so rather than this file. */
    if (g_dev.mesh_dump)
        sscanf(g_dev.mesh_dump, "%d,%d", &dump_r, &dump_c);
    if (s_pass == 2)
    {
        /*  The corridors are NOTCHED into the terrain, and a notch only
         *  ever cuts.  So a corner the band reaches is lowered to the
         *  corridor's height and never lifted above the ground it was
         *  cut from.  Only corners the band itself reaches are touched
         *  at all: the batter ring beyond them is terrain.  Terrain is
         *  left as it is.  The wall rule closes the sides of the cut,
         *  and the watertight check covers the corridor like any other
         *  ground. */
        {

            /*  No global solve, and nothing written to the terrain.  The
             *  corridor's height is its own spur between node altitudes,
             *  which every segment computes for itself and writes into
             *  its corners.  So two segments that meet spur to the same
             *  node and nothing here has to reconcile them.  The
             *  corridor carries its own surface tile by tile, the ground
             *  stays exactly where it was, and the wall rule closes the
             *  step between them. */
        }
    }
    tile_colour(a, l, 256, land_col, land_fb);
    land_col[2] = MAT_GROUND;
    s_tp.m = m, s_tp.c = c, s_tp.mask_bit = mask_bit;
    memcpy(s_tp.land, land_col, sizeof s_tp.land);
    s_tp.underground = underground;
    s_tp.dump_r = dump_r, s_tp.dump_c = dump_c;

    memset(&s_world, 0, sizeof s_world);
    s_world.m = m, s_world.c = c, s_world.a = a, s_world.l = l;
    s_world.mask_bit    = mask_bit;
    s_world.pass        = s_pass;
    s_world.lines       = lines;
    s_world.underground = underground;
    s_world.rotated     = rotated;
    return 0;
}

/*  The three network passes, as primitives the composing script asks for
 *  by name.  The lane model first, since a junction beside a spur needs
 *  which slab lane that spur takes.  Then the networks' measure, the
 *  script's own pass over the meets and the lines, the networks'
 *  drawing, and the bands over them.  Answers 0, or -1 with the reason
 *  already reported. */
int mesh_world_nets(WorldFan *w, int what)
{
    if (what == 0)
    {
        band_lanes(w->c);
        return 0;
    }
    if (what == 1)
    {
        /*  What follows is the networks': the frame draws the terrain's
         *  range once a pass, and this is where it ends. */
        w->m->n_terrain = w->m->n_land;
        return build_networks(w->m, w->c, w->a, w->l, w->mask_bit, !w->rotated);
    }
    if (what == 3)
    {
        return build_networks_draw(w->m, w->c, w->l, w->mask_bit, !w->rotated);
    }
    {
        double t0 = tms();
        int    rc = build_bands(w->m, w->c, w->l, w->mask_bit, !w->rotated);
        tnote("bands, spurs, lanes", t0);
        net_prof_print(); /* the pass's network profile, stage by stage, once its networks are all built */
        seg_table_misses_print();
        return rc;
    }
}

/* ---- the query tool ---------------------------------------------------- */

int mesh_tile_corners(const RCity *c, int32_t col, int32_t row, int underground, float z[4])
{
    float o[4], t[4];
    int   k;
    if (col < 0 || row < 0 || col >= R_MAP || row >= R_MAP)
        return -1;
    if (underground)
        for (k = 0; k < 4; ++k)
            t[k] = s_b[corner_gi(col, row, k)];
    else
        tile_top(c, col, row, city_corner_mask(c->rotation), t);
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
    int32_t                  idx     = row * R_MAP + col;
    float                    z[4], b[4];
    Kind                     kind;
    int                      k;
    if (col < 0 || row < 0 || col >= R_MAP || row >= R_MAP || !buf || n == 0)
        return -1;
    kind = tile_top(c, col, row, city_corner_mask(c->rotation), z);
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

/*  THE STREET-FURNITURE PASS, switched as one from the View menu or
 *  --no-furniture.  Everything that stands beside a line rather than
 *  being one is drawn behind it, and a change rebuilds the mesh. */
static int s_furniture = 1; /* on unless the app says otherwise (View > Street furniture, --no-furniture) */

void furniture_enable(int on)
{
    s_furniture = on ? 1 : 0;
}

int furniture_on(void)
{
    return s_furniture;
}
