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
#include <string.h>

#include "geo.h"
#include "pipeline.h"
#include "build.h"
#include "mesh/internal.h"
#include "mesh/mesh.h"
#include "net/net.h"
#include "walk/walk.h"
#include "log.h"
#include "opt.h"
#include "incr.h"

static int mesh_build_passes(RMesh *m, const RCity *c, const RAtlas *a, const RAtlasLevel *l, int underground, int rotated, int lines, int mode);

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
    float tune[19];
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
    int      mode, rc;
    /*  Everything the build reads off the artwork comes from the finest
     *  level, so the mesh is the same whatever zoom is on screen. */
    if (a && a->n_levels > 0)
        l = &a->level[a->n_levels - 1];
    net_piece_art(a);
    memset(&key, 0, sizeof key);
    key.underground = underground;
    key.rotated     = rotated;
    key.lines       = lines;
    key.furniture   = furniture_on();
    key.script      = script_stamp();
    memcpy(key.tune, tune_array(), sizeof key.tune);
    mode = incr_begin(m, c, &key, sizeof key);
    if (mode < 0)
        return 0; /* the same city under the same key: the mesh stands */
    rc = mesh_build_passes(m, c, a, l, underground, rotated, lines, mode);
    /*  A rule that raised answered nothing, so whatever it was to draw
     *  is missing from what the passes just built.  The mesh is
     *  abandoned rather than shown: the one on the screen is the last
     *  that was whole.  The fault says which rule to go and look at.  It
     *  stands until the scripts are read again.  So saving the file is
     *  what starts the next build. */
    if (rc == 0 && script_fault())
    {
        int n = script_fault_count();
        R_ERR("mesh", "build abandoned, %d script fault%s; the first: %s",
              n, n == 1 ? "" : "s", script_fault());
        rc = -1;
    }
    if (rc == 0)
        rc = incr_snapshot(m, c, &key, sizeof key);
    if (rc != 0)
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
        s_bld.rc  = mesh_build_pass(s_bld.m, s_bld.c, s_bld.a, s_bld.l, s_bld.underground, s_bld.rotated, s_bld.lines);
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
        s_bld.rc  = mesh_build_pass(s_bld.m, s_bld.c, s_bld.a, s_bld.l, s_bld.underground, s_bld.rotated, s_bld.lines);
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

static int mesh_build_passes(RMesh *m, const RCity *c, const RAtlas *a, const RAtlasLevel *l, int underground, int rotated, int lines, int mode)
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

void build_step(const char *step, int i, int n)
{
    if (s_watch && step)
        s_watch(s_watch_ud, step, i, n);
}
