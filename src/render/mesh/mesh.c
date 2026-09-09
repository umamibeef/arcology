#include <stdio.h>
/*  mesh.c -- building the mesh, and the query tool.  Split out of mesh.c;
 *  see mesh/internal.h. */
#include "dump.h"
#include "log.h"
#include "mesh/internal.h"
#include "net/internal.h"
#include "opt.h"
#include "script.h"
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

static int mesh_build_pass(RMesh *m, const RCity *c, const RAtlas *a, const RAtlasLevel *l, int underground, int rotated, int roads);

/*  A mesh is built in two passes when there are roads.  The first GRADES:
 *  it walks every network and records the surface each corridor wants
 *  (the shelves, s_tilez), and draws nothing -- every emitter is a no-op
 *  under s_pass 1.  The second BUILDS the world on those shelves.  They
 *  are the same pass body, mesh_build_pass, told which it is. */
static int mesh_build_passes(RMesh *m, const RCity *c, const RAtlas *a, const RAtlasLevel *l, int underground, int rotated, int roads, int mode);

/*  What the build reads besides the tiles: a different key is a full
 *  build (mesh/incr.c diffs the tiles under the same key).  The sprite
 *  set being drawn is NOT in it.  The mesh is the world in world units;
 *  what it reads off the artwork -- a piece's links, a building's bulk,
 *  the ground's mean colour -- are properties of a tile id and are read
 *  from the finest level whatever level is on screen.  With the level in
 *  the key, every zoom across a set boundary was a full rebuild. */
typedef struct
{
    int   underground, rotated, roads, furniture;
    float tune[19];
} BuildKey;

/*  The mesh the last build wrote into: what a query that was handed no
 *  mesh of its own asks. */
static RMesh *s_built;

const RMesh *mesh_built(void)
{
    return s_built;
}

int mesh_build(RMesh *m, const RCity *c, const RAtlas *a, const RAtlasLevel *l, int underground, int rotated, int roads)
{
    s_built = m;
    BuildKey key;
    int      mode, rc;
    /*  Everything the build reads off the artwork comes from the finest
     *  level, so the mesh is the same whatever zoom is on screen. */
    if (a && a->n_levels > 0)
        l = &a->level[a->n_levels - 1];
    net_piece_art(a);
    memset(&key, 0, sizeof key);
    key.underground = underground;
    key.rotated     = rotated;
    key.roads       = roads;
    key.furniture   = furniture_on();
    memcpy(key.tune, mesh_tune(), sizeof key.tune);
    mode = mesh_incr_begin(m, c, &key, sizeof key);
    if (mode < 0)
        return 0; /* the same city under the same key: the mesh stands */
    rc = mesh_build_passes(m, c, a, l, underground, rotated, roads, mode);
    /*  A rule that raised answered nothing, so whatever it was to draw
     *  is missing from what the passes just built.  The mesh is
     *  abandoned rather than shown: the one on the screen is the last
     *  that was whole, and the fault says which rule to go and look at.
     *  It stands until the scripts are read again, so saving the file
     *  is what starts the next build. */
    if (rc == 0 && script_fault())
    {
        int n = script_fault_count();
        R_ERR("mesh", "build abandoned, %d script fault%s; the first: %s",
              n, n == 1 ? "" : "s", script_fault());
        rc = -1;
    }
    if (rc == 0)
        rc = mesh_incr_snapshot(m, c, &key, sizeof key);
    if (rc != 0)
        mesh_incr_abort(m);
    return rc;
}

static int mesh_build_passes(RMesh *m, const RCity *c, const RAtlas *a, const RAtlasLevel *l, int underground, int rotated, int roads, int mode)
{
    /*  Who drew what, for the inspector.  A full build says it all again;
     *  an edit's keeps the record of the chunks that stand, whose
     *  triangles are spliced in with their component ids, and clears
     *  only the tiles it builds, once the closure has named them. */
    if (mode != 1)
        mesh_origins_reset();
    mesh_record(1);
    /*  Two passes when there are roads: the first lays the networks out
     *  and records the surface their corridors want, the second builds
     *  the world with those corridors notched into it. */
    if (roads && !underground && !g_dev.no_cap)
    {
        int32_t g;
        int     rc;
        for (g = 0; g < GRID * GRID; ++g)
        {
            s_zcap[g]  = 1e9f;
            s_zlow[g]  = 1e9f;
            s_zdist[g] = 1e9f;
            s_corr[g]  = 0;
        }
        {
            /*  The shelves are one entry per tile CORNER, four times the
             *  map and not the grid's own count.  Clearing them on the
             *  grid's loop left three quarters of the map holding a shelf
             *  of zero, which dropped every tile it touched to the sea
             *  and stood a wall round it. */
            int32_t t;
            for (t = 0; t < R_MAP * R_MAP * 4; ++t)
            {
                s_tilez[t] = 1e9f;
                s_tile_reset(t);
            }
        }
        {
            double t0 = tms();
            s_pass    = 1;
            rc        = mesh_build_pass(m, c, a, l, underground, rotated, roads);
            tnote("whole pass", t0);
        }
        if (rc == 0 && mode == 1)
        {
            mesh_incr_closure(1); /* an edit: which chunks the building pass emits into */
            mesh_origins_clear_wanted();
        }
        if (rc == 0)
        {
            double t0 = tms();
            s_pass    = 2;
            rc        = mesh_build_pass(m, c, a, l, underground, rotated, roads);
            if (rc == 0) /* by chunk, for the frame's culling and per-chunk draws; an edit's build spliced in */
                rc = mode == 1 ? mesh_incr_end(m) : mesh_partition(m);
            tnote("whole pass", t0);
        }
        s_pass = 0;
        mesh_record(0);
        return rc;
    }
    s_pass = 0;
    {
        int rc;
        if (mode == 1)
        {
            mesh_incr_closure(0);
            mesh_origins_clear_wanted();
        }
        rc = mesh_build_pass(m, c, a, l, underground, rotated, roads);
        if (rc == 0)
            rc = mode == 1 ? mesh_incr_end(m) : mesh_partition(m);
        mesh_record(0);
        return rc;
    }
}

/*  What a tile's composition wants beyond its own place: set once a
 *  pass, so one tile's ground can be composed from anywhere -- the loop
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

/*  ONE TILE'S GROUND, gathered: its corners and their heights, the water
 *  over it, the pad a network cut, and what each of its four neighbours
 *  reaches it at.  Answers 0 where this build wants no such tile.
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
    if (s_pass != 1 && !mesh_want_tile(col, row))
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

    /*  What is DRAWN over all that is the SCRIPT'S
     *  (arc.rules.tile): the top face, the seabed under a body
     *  of water, the wall down to each neighbour and the
     *  sediment at the map's cut edges.  The heightfield, the
     *  slope codes and the pads a network cut are settled above;
     *  with no rule the ground is not drawn at all. */
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

/*  ONE TILE'S ZONE, gathered, for the map view's tint: the zone it
 *  carries or the code a placed structure takes instead, and the quad a
 *  hair over its ground.  Answers 0 where there is no tint to draw. */
int mesh_tint_fan(int32_t col, int32_t row, TileFan *out)
{
    RMesh        *m        = s_tp.m;
    const RCity  *c        = s_tp.c;
    const uint8_t mask_bit = s_tp.mask_bit;
    (void)m;
    if (s_pass != 1 && !mesh_want_tile(col, row))
        return 0;
    int32_t idx  = row * R_MAP + col;
    int     zone = c->xzon[idx] & 0x0Fu;
    float   z[4];
    Kind    kind;
    /*  A tile something was PLACED on carries its own code
     *  rather than the zone under it: which structures, and
     *  which code each takes, is the script's
     *  (scripts/ground_tiles.lua), 0 leaving the tile to its
     *  zone. */
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

static int mesh_build_pass(RMesh *m, const RCity *c, const RAtlas *a, const RAtlasLevel *l, int underground, int rotated, int roads)
{
    static const float land_fb[3] = {0.45f, 0.62f, 0.30f};
    s_check_xbld                  = roads ? c->xbld : NULL;
    m->net.n_pts                  = 0;
    m->net.n_segs                 = 0;
    m->railnet.n_pts              = 0;
    m->railnet.n_segs             = 0;
    m->n_xings                    = 0;
    m->n_rsigs                    = 0;
    if (s_pass != 2)
    {
        /*  The junction controls are stage three's, worked out with the
         *  trims and the crossings that turn on them (net/walk.c), so
         *  they are cleared where those are and the building pass reads
         *  what the grading pass left. */
        memset(s_junc_ctrl, 0, sizeof s_junc_ctrl);
        fit_stats_reset(); /* the building pass fits nothing: keep the grading pass's count */
    }
    float              land_col[3];
    const uint8_t      mask_bit = city_corner_mask(c->rotation);
    int                dump_r = -1, dump_c = -1;

    if (g_dev.mesh_dump)
        sscanf(g_dev.mesh_dump, "%d,%d", &dump_r, &dump_c);
    m->n_land   = 0;
    m->n_water  = 0;
    m->to_water = 0;
    m->n_walls  = 0;
    build_field(c);
    if (s_pass == 2)
    {
        /*  A corridor tile must be graded on all four corners or its
         *  surface tilts through the road: where several segments meet,
         *  one corner could keep the hillside's height while its
         *  neighbours took the road's.  So every tile carrying a network
         *  piece with any graded corner has the rest filled in from the
         *  mean of those it has, twice over so it carries across a
         *  junction. */
        int32_t pass2, col2, row2;
        for (pass2 = 0; pass2 < 2; ++pass2)
            for (row2 = 0; row2 < R_MAP; ++row2)
                for (col2 = 0; col2 < R_MAP; ++col2)
                {
                    int32_t gi[4] = {row2 * GRID + col2, row2 * GRID + col2 + 1, (row2 + 1) * GRID + col2, (row2 + 1) * GRID + col2 + 1};
                    float   sum = 0.0f, low = 1e9f;
                    int     k2, have        = 0;
                    uint8_t b2 = c->xbld[row2 * R_MAP + col2];
                    if (b2 < 0x0Eu || b2 >= 0x49u)
                        continue; /* not a network piece */
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
                             *  smoothing will lift it over the road. */
                            s_zcap[gi[k2]] = sum / (float)have;
                            if (low < s_zlow[gi[k2]])
                                s_zlow[gi[k2]] = low;
                            s_corr[gi[k2]] = 1;
                        }
                }
    }
    if (s_pass == 2)
    {
        /*  The corridors are NOTCHED into the terrain, and a notch only
         *  ever cuts.  So a corner the band reaches is lowered to the
         *  corridor's height and never lifted above the ground it was cut
         *  from, and only corners the band itself reaches are touched at
         *  all -- the batter ring beyond them is terrain, and terrain is
         *  left as it is.  The wall rule closes the sides of the cut, and
         *  the watertight check covers the corridor like any other ground. */
        {

            /*  No global solve, and nothing written to the terrain.  The
             *  corridor's height is its own ramp between node altitudes,
             *  which every segment computes for itself and writes into its
             *  corners, so two segments that meet ramp to the same node and
             *  nothing here has to reconcile them.  The corridor carries
             *  its own surface tile by tile, the ground stays exactly where
             *  it was, and the wall rule closes the step between them. */
        }
    }
    /*  --plan-dump 1 writes the world as numbers for tools/plan.py, which
     *  draws a city from above: the field's heights, what each tile
     *  carries, and (with the paths) the corridors and their centrelines.
     *  A plan view is how this is inspected. */
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
         *  NE SE SW, 1e9 where a tile has none: what a network tile is
         *  drawn from (tile.c tile_top), so continuity along a corridor
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
    }
    tile_colour(a, l, 256, land_col, land_fb);
    land_col[2] = MAT_GROUND;
    s_tp.m = m, s_tp.c = c, s_tp.mask_bit = mask_bit;
    memcpy(s_tp.land, land_col, sizeof s_tp.land);
    s_tp.underground = underground;
    s_tp.dump_r = dump_r, s_tp.dump_c = dump_c;

    /*  THE ORDER the world is composed in is the SCRIPT'S
     *  (scripts/compose/world.lua): which tiles, in what order, and
     *  which of the network passes run at all.  There is no C loop
     *  behind it -- a build with no rule composes nothing, and says so
     *  rather than drawing an empty city. */
    {
        WorldFan w;
        memset(&w, 0, sizeof w);
        w.m = m, w.c = c, w.a = a, w.l = l;
        w.mask_bit    = mask_bit;
        w.pass        = s_pass;
        w.roads       = roads;
        w.underground = underground;
        w.rotated     = rotated;
        if (!script_rule_object("world", "world", &w))
        {
            R_ERR("mesh", "arc.rules.world composed nothing: no such rule, or it answered false");
            return -1;
        }
        return w.rc;
    }
    return 0;
}

/*  The three network passes, as primitives the composing script asks for
 *  by name: the lane model first, since a junction beside a ramp needs
 *  which deck lane that ramp takes; then the networks' measure, the
 *  script's own pass over the crossings and the lines, the networks'
 *  drawing, and the highways over them.  Answers 0, or -1 with the reason already
 *  reported. */
int mesh_world_nets(WorldFan *w, int what)
{
    if (what == 0)
    {
        hiway_lanes(w->c);
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
        int    rc = build_highways(w->m, w->c, w->l, w->mask_bit, !w->rotated);
        tnote("highways, ramps, lanes", t0);
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
