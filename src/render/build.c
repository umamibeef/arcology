/*  build.c: THE BUILD, from the outside.
 *
 *  A build is two passes over one body.  The first GRADES.  It walks
 *  every network and records the surface each corridor wants.  It draws
 *  nothing, because every emitter is a no-op while it runs.  The second
 *  BUILDS the world on those shelves.  Where there are no lines, or the
 *  view is underground, there is one pass.
 *
 *  This file sits above mesh/, net/ and walk/ because it runs all three.
 *  It sets a pass up, hands it to the drive, and takes back what the
 *  drive left.  Composing a pass is the SCRIPT'S, so nothing here
 *  enters one: the door is net/drive.c's and the order is
 *  scripts/compose/world.lua's. */
#include <stdio.h>
#include <string.h>

#include "geo.h"
#include "pipeline.h"
#include "build.h"
#include "mesh/internal.h"
#include "mesh/mesh.h"
#include "net/net.h"
#include "walk/walk.h"
#include "dump.h"
#include "log.h"
#include "opt.h"
#include "incr.h"
#include "mesh/fit.h"
#include "net/cell.h"
#include "walk/cursor.h"
#include "net/walkway.h"
#include "script.h"
#include "prof.h"
#include "show.h"

static int build_passes(RMesh *m, const RCity *c, const RAtlas *a, const RAtlasLevel *l, int underground, int rotated, int lines, int mode);

/*  What the build reads besides the tiles: a different key is a full
 *  build (incr.c diffs the tiles under the same key).  The sprite set
 *  being drawn is NOT in it.  The mesh is the world in world units.
 *  What it reads off the artwork.  A piece's links, a building's bulk,
 *  the ground's mean color.  Are properties of a tile id and are read
 *  from the finest level whatever level is on screen.  With the level in
 *  the key, every zoom across a set boundary was a full rebuild.
 *
 *  The SCRIPTS are in it, as the stamp that changes each time they are
 *  read.  They decide every shape in the world, so a mesh built under
 *  another reading is not the mesh this one describes.  Without the
 *  stamp a city that has not changed answers "the mesh stands" and the
 *  saved rule is read and then drawn by nothing. */
typedef struct
{
    int   underground, rotated, lines, furniture, script;
    int   n_tune;
    float tune[ARC_TUNE_MAX];
} BuildKey;

/*  The mesh the last build wrote into: what a query that was handed no
 *  mesh of its own asks. */
static RMesh *s_built;

const RMesh *build_mesh(void)
{
    return s_built;
}

int build_world(RMesh *m, const RCity *c, const RAtlas *a, const RAtlasLevel *l, int underground, int rotated, int lines)
{
    s_built = m;
    BuildKey key;
    /*  The map the pass is about to read, offered to the scripts: a
     *  script walks it through arc.city.  It stands after the build so
     *  the console and --lua-eval read the same city. */
    script_city_is(c);
    int      mode, rc, kept = 0;
    /*  Everything the build reads off the artwork comes from the finest
     *  level, so the mesh is the same whatever zoom is on screen. */
    if (a && a->n_levels > 0)
        l = &a->level[a->n_levels - 1];
    memset(&key, 0, sizeof key);
    key.underground = underground;
    key.rotated     = rotated;
    key.lines       = lines;
    key.furniture   = furniture_on();
    key.script      = script_stamp();
    key.n_tune      = tune_values(key.tune, ARC_TUNE_MAX);
    mode = incr_begin(m, c, &key, sizeof key);
    if (mode < 0)
        return 0; /* the same city under the same key: the mesh stands */
    if (mode != 1)
        show_reset();
    rc = build_passes(m, c, a, l, underground, rotated, lines, mode);
    /*  A rule that raised answered nothing, so whatever it was to draw
     *  is missing from what the passes just built.  The mesh is
     *  abandoned rather than shown: the one on the screen is the last
     *  that was whole.  The fault says which rule to go and look at.  It
     *  stands until the scripts are read again.  So saving the file is
     *  what starts the next build.
     *
     *  What the passes DID make is kept, so a city whose FIRST build
     *  faults has something on the screen to look at.  The build still
     *  FAILS. */
    if (rc == 0 && script_fault())
    {
        int n = script_fault_count();
        R_ERR("mesh", "build abandoned, %d script fault%s; the first: %s",
              n, n == 1 ? "" : "s", script_fault());
        rc = -1;
    }
    if (rc == 0)
        rc = incr_snapshot(m, c, &key, sizeof key);
    if (mode != 1 || rc != 0)
        show_reset();
    /*  HOWEVER THE BUILD FAILED, WHAT IT MADE IS KEPT.  A rule that
     *  raised and a pass that answered nothing both come here.  A
     *  person who cannot see how far the build got cannot tell which
     *  of them it was.  The build's own answer is untouched.  It still
     *  failed, and every check still fails with it. */
    if (rc != 0)
    {
        /*  The device draws BY CHUNK, so a mesh with no ranges is
         *  nothing to draw.  A faulted build never reaches the sort at
         *  the end of the cursor.  So the kept mesh is sorted and
         *  ranged here instead. */
        if (mesh_partition(m) != 0)
            R_ERR("mesh", "the mesh could not be sorted, so there is nothing "
                          "to show after all");
        R_ERR("mesh", "the mesh is INCOMPLETE and is shown as it stands, %d "
                      "triangles.  What the build did not reach is missing "
                      "from it.", (int)(m->n_land + m->n_water));
        kept = 1;
    }
    if (rc != 0 && !kept)
        incr_abort(m);
    return rc;
}

/*  THE BUILD AS A CURSOR.  The passes are set up here and handed out one
 *  at a time.  Composing each is the drive's, so this file never enters
 *  a script.  `step` is how far the cursor has come: 0 not begun, 1 and
 *  2 the pass of that number open, 3 nothing left. */
WorldFan s_world;
static struct
{
    RMesh             *m;
    const RCity       *c;
    const RAtlas      *a;
    const RAtlasLevel *l;
    int                underground, rotated, lines, mode;
    int                two;  /* the lines want a grading pass and a building pass */
    int                step;
    int                rc;
} s_bld;

/*  What has to happen before the first pass: who drew what, for the
 *  inspector, and the corridor's own tables where there are two passes. */
static void mesh_passes_begin(void)
{
    if (s_bld.mode != 1)
        mesh_origins_reset();
    mesh_record(1);
    if (!s_bld.two)
        return;
    {
        int32_t g;
        for (g = 0; g < GRID * GRID; ++g)
        {
            s_zcap[g]  = 1e9f;
            s_zlow[g]  = 1e9f;
            s_zdist[g] = 1e9f;
            s_corr[g]  = 0;
        }
    }
    {
        /*  The shelves are one entry per tile CORNER, four times the map
         *  and not the grid's own count.  Cleared on the grid's loop,
         *  three quarters of the map holds a shelf of zero.  This drops
         *  every tile it touches to the sea and stands a wall round it. */
        int32_t t;
        for (t = 0; t < R_MAP * R_MAP * 4; ++t)
        {
            s_tilez[t] = 1e9f;
            s_tile_reset(t);
        }
    }
}

/*  The next pass, set up and handed out, or nothing when the build has
 *  none left. */
void *build_pass_next(void)
{
    double t0;
    if (s_bld.step == 0)
    {
        mesh_passes_begin();
        if (!s_bld.two && s_bld.mode == 1)
        {
            incr_closure(0);
            mesh_origins_clear_wanted();
        }
        s_pass    = s_bld.two ? 1 : 0;
        s_bld.rc  = build_pass_begin(s_bld.m, s_bld.c, s_bld.a, s_bld.l, s_bld.underground, s_bld.rotated, s_bld.lines);
        s_bld.step = 1;
        if (s_bld.rc != 0)
            return NULL;
        return &s_world;
    }
    /*  What the pass just composed left behind, and then the next. */
    s_bld.rc = s_world.rc;
    if (s_bld.step == 1 && s_bld.two && s_bld.rc == 0)
    {
        if (s_bld.mode == 1)
        {
            incr_closure(1); /* an edit: which chunks the building pass emits into */
            mesh_origins_clear_wanted();
        }
        t0        = tms();
        s_pass    = 2;
        s_bld.rc  = build_pass_begin(s_bld.m, s_bld.c, s_bld.a, s_bld.l, s_bld.underground, s_bld.rotated, s_bld.lines);
        tnote("whole pass", t0);
        s_bld.step = 2;
        if (s_bld.rc != 0)
            return NULL;
        return &s_world;
    }
    if (s_bld.rc == 0) /* by chunk, for the frame's culling and per-chunk draws.  An edit's build spliced in */
        s_bld.rc = s_bld.mode == 1 ? incr_end(s_bld.m) : mesh_partition(s_bld.m);

    s_pass     = 0;
    s_bld.step = 3;
    mesh_record(0);
    return NULL;
}

int build_rc(void)
{
    return s_bld.rc;
}

/*  ONE PASS BEGUN: what the networks and the ground read before the script
 *  composes it.  The stores the pass writes start empty.  The corridor
 *  corners a script named are filled in from their tile's others.  Then the
 *  pass in hand is handed to the script as a `world`. */
int build_pass_begin(RMesh *m, const RCity *c, const RAtlas *a, const RAtlasLevel *l, int underground, int rotated, int lines)
{
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

    mesh_pass_begin(m, c, a, l, underground, dump_r, dump_c);
    memset(&s_world, 0, sizeof s_world);
    s_world.m = m, s_world.c = c, s_world.a = a, s_world.l = l;
    s_world.mask_bit    = mask_bit;
    s_world.pass        = s_pass;
    s_world.lines       = lines;
    s_world.underground = underground;
    s_world.rotated     = rotated;
    return 0;
}

/*  THE FIELD, built when the DRIVE asks for it: after the rule has said
 *  where every tile's top comes from.  This is because a water tile's
 *  bed is clamped under the surface drawn over it and that surface is
 *  the rule's answer. */
void build_pass_field(void)
{
    const RCity *c = s_world.c;
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
    }
}

/*  The three network passes, as primitives the composing script asks for
 *  by name.  The lane model first, since a junction beside a spur needs
 *  which slab lane that spur takes.  Then the networks' measure, the
 *  script's own pass over the meets and the lines, the networks'
 *  drawing, and the bands over them.  Answers 0, or -1 with the reason
 *  already reported. */
int build_world_nets(WorldFan *w, int what)
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
        return build_networks(w->m, w->c, w->a, w->mask_bit, !w->rotated);
    }
    if (what == 3)
    {
        return build_networks_draw(w->m, w->c, w->mask_bit, !w->rotated);
    }
    {
        double t0 = tms();
        int    rc = build_bands(w->m, w->c, w->mask_bit, !w->rotated);
        tnote("bands, spurs, lanes", t0);
        prof_print(); /* the pass's network profile, stage by stage, once its networks are all built */
        seg_table_misses_print();
        return rc;
    }
}

static int build_passes(RMesh *m, const RCity *c, const RAtlas *a, const RAtlasLevel *l, int underground, int rotated, int lines, int mode)
{
    memset(&s_bld, 0, sizeof s_bld);
    s_bld.m = m, s_bld.c = c, s_bld.a = a, s_bld.l = l;
    s_bld.underground = underground;
    s_bld.rotated     = rotated;
    s_bld.lines       = lines;
    s_bld.mode        = mode;
    s_bld.two         = lines && !underground && !g_dev.no_cap;
    return net_drive_build();
}

/*  WHAT THE BUILD HAS TO SAY, once it is over.
 *
 *  Every pass counts what it laid and what it could not, and each keeps
 *  its own tally.  This asks all of them, in one place, so no check has
 *  to reach across the tree for a report that is not its business.  It
 *  is said only where a run asked to be told. */
void build_reports(void)
{
    fit_stats();
    lane_stats_print();
    margin_stats_print();
    junction_outline_print();
    path_fit_probes();
    walk_net_check();
    shape_unclaimed_report();
    spur_stats();
}

/*  ---- how far the build has got ------------------------------------- */

static BuildWatcher s_watch;
static void        *s_watch_ud;

void build_watch(BuildWatcher fn, void *ud)
{
    s_watch    = fn;
    s_watch_ud = ud;
}

/*  The step under way, kept so a tick inside it can name it again. */
static char s_step_name[64];
static int  s_step_i, s_step_n;

void build_step(const char *step, int i, int n)
{
    if (!step)
        return;
    snprintf(s_step_name, sizeof s_step_name, "%s", step);
    s_step_i = i, s_step_n = n;
    if (s_watch)
        s_watch(s_watch_ud, step, i, n);
}

void build_tick(void)
{
    if (s_watch && s_step_name[0] && show_active())
        s_watch(s_watch_ud, s_step_name, s_step_i, s_step_n);
}

int grade_only(int allowed)
{
    return s_pass == 1 && !g_dev.grade_all && !allowed;
}

/*  The width compensation this build lofts with: on when the view is
 *  not turned.  Read where a box lofts as a segment does. */
int build_compensate(void)
{
    return !s_world.rotated;
}
