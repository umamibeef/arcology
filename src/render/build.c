/*  build.c: THE BUILD, from the outside.
 *
 *  A build composes the world once.  It derives the road network, sets
 *  the world up, and hands it to compose.c, which composes it in its own
 *  order.  It then takes back what the composition left.  This file is
 *  the one door a build comes through. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "build.h"
#include "compose.h"
#include "draw/net_overlay.h"
#include "geo.h"
#include "incr.h"
#include "log.h"
#include "mesh/emit.h"
#include "mesh/mesh.h"
#include "mesh/tile.h"
#include "network/bank.h"
#include "network/network.h"
#include "network/shape.h"
#include "network/store.h"
#include "network/topology.h"
#include "opt.h"
#include "prof.h"
#include "show.h"

static int build_run(RMesh *m, const RCity *c, const RAtlas *a, const RAtlasLevel *l, int underground, int rotated, int lines, int mode);
static int build_world_begin(RMesh *m, const RCity *c, const RAtlas *a, const RAtlasLevel *l, int underground, int rotated, int lines);

/*  What the build reads besides the tiles: a different key is a full
 *  build (incr.c diffs the tiles under the same key).  The sprite set
 *  being drawn is NOT in it.  The mesh is the world in world units.
 *  What it reads off the artwork.  A piece's links, a building's bulk,
 *  the ground's mean color.  Are properties of a tile id and are read
 *  from the finest level whatever level is on screen.  With the level in
 *  the key, every zoom across a set boundary would be a full rebuild.
 *
 *  The knobs are in it.  A mesh built under other knob values is not the
 *  mesh this one describes. */
typedef struct
{
    int   underground, rotated, lines;
    int   n_tune;
    float tune[ARC_TUNE_MAX];
} BuildKey;

/*  The mesh the last build wrote into: what a query that was handed no
 *  mesh of its own asks. */
static RMesh *s_built;

int build_world(RMesh *m, const RCity *c, const RAtlas *a, const RAtlasLevel *l, int underground, int rotated, int lines)
{
    s_built = m;
    BuildKey key;
    int      mode, rc, kept = 0;
    /*  Everything the build reads off the artwork comes from the finest
     *  level, so the mesh is the same whatever zoom is on screen. */
    if (a && a->n_levels > 0)
        l = &a->level[a->n_levels - 1];
    memset(&key, 0, sizeof key);
    key.underground = underground;
    key.rotated     = rotated;
    key.lines       = lines;
    key.n_tune      = tune_values(key.tune, ARC_TUNE_MAX);
    mode            = incr_begin(m, c, &key, sizeof key);
    if (mode == INCR_STANDS)
        return 0;
    if (mode != INCR_EDIT)
        show_reset();
    {
        int32_t box[4];
        prof_work_window(mode == INCR_EDIT && incr_dirty_box(box) == 0 ? box : NULL, g_dev.work_window ? atoi(g_dev.work_window) : 3);
    }
    rc = build_run(m, c, a, l, underground, rotated, lines, mode);
    prof_work_print();
    if (rc == 0)
        rc = incr_snapshot(m, c, &key, sizeof key);
    if (mode != INCR_EDIT || rc != 0)
        show_reset();
    /*  HOWEVER THE BUILD FAILED, WHAT IT MADE IS KEPT.  A stage that
     *  failed and a world that answered nothing both come here.  A
     *  person who cannot see how far the build got cannot tell which
     *  of them it was.  The build's own answer is untouched.  It still
     *  failed, and every check still fails with it. */
    if (rc != 0)
    {
        /*  The device draws BY CHUNK, so a mesh with no ranges is
         *  nothing to draw.  A faulted build never reaches the sort at
         *  the end of the cursor.  So the kept mesh is sorted and
         *  ranged here instead. */
        if ((incr_window_building() ? incr_window_end(m) : mesh_partition(m)) != 0)
            R_ERR("mesh", "the mesh could not be sorted, so there is nothing "
                          "to show after all");
        R_ERR("mesh", "the mesh is INCOMPLETE and is shown as it stands, %d "
                      "triangles.  What the build did not reach is missing "
                      "from it.",
              (int)(m->n_land + m->n_water));
        kept = 1;
    }
    if (rc != 0 && !kept)
        incr_abort(m);
    return rc;
}

/*  THE BUILD AS A CURSOR.  The world is set up here and handed out
 *  once.  Composing it is the drive's, so this file never enters
 *  compose.c.  `step` is how far the cursor has come: 0 not begun, 1 the
 *  world handed out, 2 finished. */
WorldFan s_world;
/*  Whether the network the build derived could be shaped. */
static int s_shaped;
static struct
{
    RMesh             *m;
    const RCity       *c;
    const RAtlas      *a;
    const RAtlasLevel *l;
    int                underground, rotated, lines, mode;
    int                step;
    int                rc;
} s_bld;

/*  The world, set up and handed out, or nothing once it has been. */
static void *build_world_next(void)
{
    if (s_bld.step == 0)
    {
        /*  The window this build lays, handed to the mesh before anything
         *  is laid, so the mesh asks the build nothing. */
        {
            int32_t box[INCR_BOXES][4];
            mesh_window_set(box, incr_window(box));
        }
        /*  Who drew what, for the inspector.  A window into the mesh
         *  standing keeps the shapes and records the chunks outside it
         *  were laid under. */
        if (s_bld.mode == INCR_WHOLE)
            mesh_origins_reset();
        else
            mesh_origins_clear_wanted();
        mesh_record(1);
        s_bld.rc   = build_world_begin(s_bld.m, s_bld.c, s_bld.a, s_bld.l, s_bld.underground, s_bld.rotated, s_bld.lines);
        s_bld.step = 1;
        return s_bld.rc != 0 ? NULL : &s_world;
    }
    return NULL;
}

/*  The world composed: what the build lays after compose.c, then the
 *  mesh cut into its chunks.  Once a build, whoever asks first. */
static void build_world_done(void)
{
    if (s_bld.step != 1)
        return;
    /*  What compose.c composed, and then what the build lays after it. */
    s_bld.rc = s_world.rc;
    /*  The lane graph over the bare ground, while the outline view is on.
     *  It is a picture of what the road network holds, laid from its
     *  records (draw/net_overlay.c). */
    if (s_bld.rc == 0 && TUNE(show_curves) > 0.5f && !s_bld.underground)
        s_bld.rc = net_overlay(s_bld.m, s_bld.c, s_world.mask_bit);
    if (s_bld.rc == 0) /* by chunk, for the frame's culling and per-chunk draws.  A window's build laid into the mesh standing */
        s_bld.rc = s_bld.mode == INCR_WHOLE ? mesh_partition(s_bld.m) : incr_window_end(s_bld.m);
    /*  The shapes kept to what the mesh holds, unless an edit is being
     *  shown.  The show reads the shapes by their ids until it ends, and
     *  the table is kept to the mesh then. */
    if (s_bld.rc == 0 && !show_active())
        s_bld.rc = shape_compact(s_bld.m);

    s_bld.step = 2;
    mesh_record(0);
}

int build_rc(void)
{
    return s_bld.rc;
}

/*  THE WORLD BEGUN: what the ground reads before compose.c composes it.
 *  The stores the build writes start empty.  Then the world is handed to
 *  the drive. */
static int build_world_begin(RMesh *m, const RCity *c, const RAtlas *a, const RAtlasLevel *l, int underground, int rotated, int lines)
{
    const uint8_t mask_bit = city_corner_mask(0);
    int           dump_r = -1, dump_c = -1;
    /*  THE ORDER the world is composed in is compose.c's: which tiles,
     *  in what order, and what is drawn over them.
     *
     *  The build STOPS HERE, before the field.  Then compose.c calls
     *  build_world_field.  It settles where every tile's top comes from
     *  and then builds the field from that answer. */
    if (g_dev.mesh_dump)
        sscanf(g_dev.mesh_dump, "%d,%d", &dump_c, &dump_r);
    mesh_build_begin(m, c, a, l, underground, dump_r, dump_c);
    memset(&s_world, 0, sizeof s_world);
    s_world.m = m, s_world.c = c, s_world.a = a, s_world.l = l;
    s_world.mask_bit    = mask_bit;
    s_world.lines       = lines;
    s_world.underground = underground;
    s_world.rotated     = rotated;
    s_world.shaped      = s_shaped;
    return 0;
}

static int build_network(const RCity *c)
{
    double t0 = prof_now(), t1, t2, t3;
    if (topology_build(c) != 0)
        return -1;
    t1 = prof_now();
    if (network_build(c) != 0)
        return -1;
    t2       = prof_now();
    s_shaped = shape_build(c) == 0;
    t3       = prof_now();
    s_shaped = s_shaped && bank_build(c) == 0;
    R_DBG("net", "derived: topology %.1f, records %.1f, shape %.1f, bank %.1f ms", t1 - t0, t2 - t1, t3 - t2, prof_now() - t3);
    if (!s_shaped)
        R_WARN("roads", "the road network could not be worked out, so no street is drawn");
    return 0;
}

static int build_run(RMesh *m, const RCity *c, const RAtlas *a, const RAtlasLevel *l, int underground, int rotated, int lines, int mode)
{
    memset(&s_bld, 0, sizeof s_bld);
    s_bld.m = m, s_bld.c = c, s_bld.a = a, s_bld.l = l;
    s_bld.underground = underground;
    s_bld.rotated     = rotated;
    s_bld.lines       = lines;
    s_bld.mode        = mode;
    int32_t  box[INCR_BOXES][4];
    int      n = incr_window(box), rc = 0, shaped;
    uint8_t  in[MESH_CHUNKS_CAP];
    uint64_t hash = rcity_hash(c);
    /*  The chunks this build lays: every one without a window. */
    {
        int per = r_map / MESH_CHUNK, ch, k;
        for (ch = 0; ch < MESH_CHUNKS; ++ch)
        {
            int32_t col = ch % per * MESH_CHUNK, row = ch / per * MESH_CHUNK;
            in[ch] = (uint8_t)(n == 0);
            for (k = 0; k < n; ++k)
                in[ch] |= (uint8_t)(col >= box[k][0] && row >= box[k][1] && col < box[k][2] && row < box[k][3]);
        }
    }
    /*  THE NETWORK IS DERIVED ONCE for each state of the city, for the
     *  whole map.  A build of cells the network was derived from before
     *  takes that network, and its chunks hold it from then on.  Any
     *  other derives it again, and keeps it with a copy of the city it
     *  read (network/store.h). */
    if (net_store_reuse(hash, in, &shaped))
        s_shaped = shaped;
    else
    {
        RCity *own = (RCity *)calloc(1, sizeof *own);
        if (!own || rcity_copy(own, c) != 0)
        {
            free(own);
            return -1;
        }
        net_store_begin();
        rc = build_network(own) != 0 ? -1 : 0;
        if (net_store_keep(own, in, hash, s_shaped) != 0 && rc == 0)
            rc = -1;
    }
    if (rc == 0)
    {
        /*  One turn of the world.  The build hands out its world once, and
         *  compose.c composes it and offers what an edit's build shows.
         *  Then the build is finished. */
        WorldFan *w = (WorldFan *)build_world_next();
        rc          = w ? compose_world(w) : 0;
        build_world_done();
        compose_shown(s_bld.m);
        rc = rc != 0 ? rc : build_rc();
    }
    return rc;
}

/*  WHAT THE BUILD HAS TO SAY, once it is over.
 *
 *  Every stage counts what it laid and what it could not, and each keeps
 *  its own tally.  This asks all of them, in one place, so no check has
 *  to reach across the tree for a report that is not its business.  It
 *  is said only where a run asked to be told. */
void build_reports(void)
{
    shape_unclaimed_report();
    if (g_dev.net_dump)
    {
        topology_dump();
        network_dump();
    }
}
