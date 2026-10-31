/*  world.c: building what gets drawn.  The sweep's op list, the terrain
 *  mesh, and the two one-frame paths that check or shoot instead of
 *  running.  All of it is cache invalidation in the end: the
 *  app marks itself dirty, and these are what pay the debt. */
#include "world.h"
#include "adapt.h"
#include "arco.h"
#include "internal.h"
#include "log.h"
#include "opt.h"
#include "shore.h"
#include "show.h"
#include "sim.h"
#include "turn.h"

#include <SDL3/SDL.h>

#include "build.h"
#include "draw/lamps.h"
#include "draw/roads.h"
#include "dump.h"
#include "geo.h"
#include "incr.h"
#include "net_svg.h"
#include "network/bank.h"
#include "network/network.h"
#include "network/shape.h"
#include "network/store.h"
#include "network/street.h"
#include "network/topology.h"
#include "trees.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int map_tiles(void);

/*  ==================================================================
 *  Building what gets drawn
 *
 *  The software sweep's op list, the terrain mesh, and the one-frame
 *  paths that check or shoot instead of running.
 *  ================================================================== */
/*  --map-tile K, or 1. */
static int map_tiles(void)
{
    int k = g_dev.map_tile ? atoi(g_dev.map_tile) : 1;
    return k > 1 ? k : 1;
}

/*  A footprint the bulldozer brought down, collapsing on screen ($71A4):
 *  begun at (row, col) with its edge n.  Its debris is drawn a frame at a
 *  time, one more each R_COLLAPSE_FRAME_NS, with the explosion's sound for
 *  each new frame.  The seed makes the same footprint fall the same way. */
#define R_COLLAPSE_FRAME_NS 200000000u

void collapse_begin(App *a, int32_t row, int32_t col, int32_t n)
{
    struct Collapse *c;
    if (n < 1 || a->n_collapse >= (int)(sizeof a->collapse / sizeof a->collapse[0]))
        return;
    c        = &a->collapse[a->n_collapse++];
    c->row   = row;
    c->col   = col;
    c->n     = n;
    c->t0    = SDL_GetTicksNS();
    c->shown = 0;
    c->seed  = (uint32_t)(row * 4099 + col * 131) + (uint32_t)(c->t0 >> 20);
    a->dirty = 1;
    collapse_step(a);
}

/*  Each collapse shows the frames its time has reached, and is gone once
 *  it has shown its last.  --show-at T holds every collapse at T seconds,
 *  for a shot. */
void collapse_step(App *a)
{
    uint64_t now = SDL_GetTicksNS();
    int      k, kept = 0;
    for (k = 0; k < a->n_collapse; ++k)
    {
        struct Collapse *c      = &a->collapse[k];
        int32_t          frames = c->n;
        uint64_t         t      = g_dev.show_at ? (uint64_t)(atof(g_dev.show_at) * 1e9) : now - c->t0;
        int32_t          at     = (int32_t)(t / R_COLLAPSE_FRAME_NS);
        if (!g_dev.show_at && frames <= at)
        {
            a->dirty = 1;
            continue;
        }
        if (at + 1 != c->shown)
        {
            if (frames > at)
                sound_play(a->snd, R_SND_EXPLODE);
            c->shown = at + 1 < frames ? at + 1 : frames;
            a->dirty = 1;
        }
        a->collapse[kept++] = *c;
    }
    a->n_collapse = kept;
}

/*  The debris of every collapse under way, added to the sweep's ops at
 *  the tiles of its footprint as the sweep's own orientation shows them. */
static void collapse_sweep(App *a, const RCity *sv)
{
    int k;
    for (k = 0; k < a->n_collapse; ++k)
    {
        const struct Collapse *c = &a->collapse[k];
        int32_t                tile[2 * 16];
        int                    nt = 0, dy, dx;
        for (dy = 0; dy < c->n; ++dy)
            for (dx = 0; dx < c->n; ++dx)
            {
                adapt_turned_dst(c->col - dx, c->row + dy, a->sweep_quarter, r_map, &tile[2 * nt], &tile[2 * nt + 1]);
                ++nt;
            }
        soft_debris(&a->atlas, sv, &a->opts, tile, nt, c->shown, c->seed, &a->ops);
    }
}

int app_world_load(App *a, const char *path)
{
    World *w = (World *)calloc(1, sizeof *w);
    if (!w)
        return 0;
    if (!path || !path[0])
    {
        /*  An empty map: one blank city at the origin. */
        City *c = (City *)calloc(1, sizeof *c);
        world_init(w);
        if (!c || city_extent(c, MAP_W, MAP_H) || city_own_map(c) || world_add_city(w, c, 0, 0) < 0)
        {
            free(c);
            free(w);
            return 0;
        }
    }
    else if (arco_is_arco(path))
    {
        if (arco_load_world(path, w) != 0)
        {
            free(w);
            return 0;
        }
    }
    else
    {
        City *c = (City *)calloc(1, sizeof *c);
        world_init(w);
        if (!c || !city_load(path, c) || world_add_city(w, c, 0, 0) < 0)
        {
            if (c)
                city_free(c);
            free(c);
            free(w);
            return 0;
        }
    }
    if (a->world)
    {
        world_free(a->world);
        free(a->world);
    }
    a->world      = w;
    a->city_i     = 0;
    a->city       = w->city[0].city;
    a->biome_city = trees_biome_of_city(path);
    /*  The trees grow the city's own biome from its first build, unless
     *  the View menu names another. */
    a->biome_shown = a->us.biome >= 0 ? a->us.biome : a->biome_city;
    trees_set_biome(a->biome_shown);
    return 1;
}

void app_city_offset(const App *a, int32_t *dc, int32_t *dr)
{
    const WorldCity *wc = &a->world->city[a->city_i];
    *dc                 = wc->city->ox - a->view->x0;
    *dr                 = wc->city->oy - a->view->y0;
}

int app_city_find(const App *a, int32_t col, int32_t row, int32_t *dc, int32_t *dr)
{
    int32_t wx = col + a->view->x0, wy = row + a->view->y0, i, pick = -1;
    for (i = 0; i < a->world->n_city; i++)
    {
        const WorldCity *wc = &a->world->city[i];
        if (wx >= wc->city->ox && wy >= wc->city->oy && wx < wc->city->ox + MAP_W && wy < wc->city->oy + MAP_H && (pick < 0 || i == a->city_i))
            pick = i;
    }
    if (pick >= 0)
    {
        *dc = a->world->city[pick].city->ox - a->view->x0;
        *dr = a->world->city[pick].city->oy - a->view->y0;
    }
    return pick;
}

int app_city_at(App *a, int32_t *col, int32_t *row)
{
    int32_t wx = *col + a->view->x0, wy = *row + a->view->y0, i, pick = -1;
    for (i = 0; i < a->world->n_city; i++)
    {
        const WorldCity *wc = &a->world->city[i];
        if (wx >= wc->city->ox && wy >= wc->city->oy && wx < wc->city->ox + MAP_W && wy < wc->city->oy + MAP_H && (pick < 0 || i == a->city_i))
            pick = i;
    }
    if (pick < 0)
        return 0;
    if (pick != a->city_i)
    {
        a->city_i = pick;
        a->city   = a->world->city[pick].city;
        R_NOTE("city", "now in city %d at %d,%d", (int)pick, (int)a->world->city[pick].city->ox, (int)a->world->city[pick].city->oy);
    }
    *col = wx - a->world->city[pick].city->ox;
    *row = wy - a->world->city[pick].city->oy;
    return 1;
}

/*  A world that holds no street layer gets one, once: each street's class
 *  as network/street.c works it out, two-way.  The layer is then the
 *  world's own, saved with it, and never worked out again. */
void app_streets_import(App *a)
{
    RCity       view;
    StreetEdge *e = NULL;
    int         n, i, set = 0;
    if (!a->world || a->world->map.has_streets)
        return;
    memset(&view, 0, sizeof view);
    adapt_world(&view, a->world, a->city_i);
    n = view.err[0] ? -1 : street_infer(&view, &e);
    if (n < 0)
    {
        R_ERR("city", "the street layer could not be worked out");
        rcity_free(&view);
        return;
    }
    for (i = 0; i < n; i++)
    {
        int32_t side = view.n;
        set += world_street_edge(a->world, e[i].a % side + view.x0, e[i].a / side + view.y0, e[i].b % side + view.x0, e[i].b / side + view.y0, e[i].edge) == 0;
    }
    a->world->map.has_streets = 1;
    R_NOTE("city", "the street layer worked out: %d edges", set);
    free(e);
    rcity_free(&view);
}

int app_net_check(App *a)
{
    RCity view;
    int   bad = -1;
    if (!a->world)
        return -1;
    memset(&view, 0, sizeof view);
    adapt_world(&view, a->world, a->city_i);
    net_store_begin();
    if (!view.err[0] && topology_build(&view) == 0 && network_build(&view) == 0)
    {
        /*  The lanes are what the check holds.  The shape and the ground
         *  beside a street are reported with it, and what they report does
         *  not fail the check. */
        bad = network_check(1);
        if (shape_build(&view) == 0 && bank_build(&view) == 0)
        {
            /*  Where lanes meet at a link their borders meet: a link apart
             *  fails the check as a lane fault does. */
            int apart = shape_lane_check(g_dev.net_dump ? 2 : 1);
            bad       = apart != 0 && bad >= 0 ? bad + (apart > 0 ? apart : 1) : bad;
            shape_check(1);
            {
                static int c_sink = -1, c_grid = -1;
                bank_check(1, 1.0 / geo_num(&c_grid, "road_ground_grid"), geo_num(&c_sink, "road_sink"));
            }
            /*  What a drawn junction is made of is held to the check. */
            if (roads_build(&view, 1) == 0)
            {
                int f = roads_check(g_dev.net_dump ? 2 : 1);
                bad   = f != 0 && bad >= 0 ? bad + (f > 0 ? f : 1) : bad;
            }
        }
        else
            R_WARN("net", "the road shape or the ground beside it could not be worked out");
        if (g_dev.net_dump)
        {
            topology_dump();
            network_dump();
            shape_dump();
        }
        if (g_dev.net_svg && network_svg(g_dev.net_svg, g_dev.net_svg_box) != 0)
            bad = bad > 0 ? bad : -1;
    }
    else
        R_ERR("net", "the road network could not be built");
    /*  The check's network is its own and is not kept. */
    net_store_discard();
    rcity_free(&view);
    return bad;
}

void app_world_free(App *a)
{
    if (a->world)
    {
        world_free(a->world);
        free(a->world);
    }
    a->world = NULL;
    a->city  = NULL;
}

/*  The renderer's views, with the layers each one holds, and the ground
 *  heights kept from them. */
void app_views_free(App *a)
{
    RCity **v[] = {&a->view, &a->view_rot, &a->build_view};
    size_t  i;
    for (i = 0; i < sizeof v / sizeof *v; i++)
        if (*v[i])
        {
            rcity_free(*v[i]);
            free(*v[i]);
            *v[i] = NULL;
        }
    rcity_free(&a->view_one);
    free(a->ground);
    free(a->ground_next);
    a->ground = a->ground_next = NULL;
    a->ground_n                = 0;
}

int32_t app_world_side(const App *a)
{
    return adapt_world_side(a->world) * map_tiles();
}

/*  The tiles the device draws from their own shape, to the device, turned
 *  to the sweep's quarter, so their art is left out. */
static void shaped_upload(App *a)
{
    static uint8_t *turned;
    static int32_t  had;
    int32_t         x, y, sx, sy;
    if (!a->shaped)
    {
        gpu_set_shaped(a->gpu, NULL, 0);
        return;
    }
    if (had != r_map)
    {
        free(turned);
        turned = (uint8_t *)malloc((size_t)r_map * (size_t)r_map);
        had    = turned ? r_map : 0;
        if (!turned)
            return;
    }
    for (y = 0; y < r_map; ++y)
        for (x = 0; x < r_map; ++x)
        {
            adapt_turned_src(x, y, a->sweep_quarter & 3, r_map, &sx, &sy);
            turned[y * r_map + x] = a->shaped[sy * r_map + sx];
        }
    gpu_set_shaped(a->gpu, turned, r_map);
}

int resweep(App *a)
{
    const RCity *sv = a->view;
    uint64_t     t0 = SDL_GetTicksNS();
    if (map_tiles() > 1)
    {
        adapt_world(&a->view_one, a->world, a->city_i);
        if (rcity_tile(a->view, &a->view_one, map_tiles()) != 0)
            return -1;
    }
    else
        adapt_world(a->view, a->world, a->city_i);
    a->opts.draw_traffic = !geometry_on(a); /* the original's traffic sprites, over the original's art alone */
    a->opts.n            = r_map;           /* the sweep covers the whole map, whatever size it is now */
    /*  At a quarter turn the sweep runs on the renderer's view of the
     *  city from that orientation.  The art, the anchors and the
     *  painter's order the original would show, read off the unturned
     *  grid.  And is projected unturned.  The mesh, built once from the
     *  same grid, is projected at the quarter about the map's center,
     *  and the two meet.  Nothing is rewritten. */
    if (sweep_wanted(a))
    {
        adapt_view_turned(a->view_rot, a->view, sweep_wanted(a));
        sv = a->view_rot;
    }
    a->sweep_quarter = sweep_wanted(a);
    if (soft_sweep(&a->atlas, sv, &a->opts, &a->ops, &a->sw) != 0)
        return -1;
    collapse_sweep(a, sv);
    if (gpu_set_ops(a->gpu, &a->ops, &a->sw) != 0)
        return -1;
    if (geometry_on(a) && gpu_set_smog(a->gpu, a->view->xplt, r_map / 2) != 0)
        return -1;
    shaped_upload(a);
    a->dirty = 0;
    if (g_dev.times)
        dumpf("time  resweep %.1f ms\n", (double)(SDL_GetTicksNS() - t0) / 1e6);
    return 0;
}

R_MAP_BUF(static, uint8_t, s_shore, 4)
R_MAP_BUF(static, uint8_t, s_laid, 4 * LAMP_TEXELS * LAMP_TEXELS)

/*  The water's field, kept from the build for the upload, which an edit
 *  may hold back a moment. */
int world_upload(App *a)
{
    a->upload_held = 0;
    if (gpu_set_shore(a->gpu, s_shore, r_map) != 0)
        return -1;
    {
        /*  The lamps' light and the trees' canopy, one texture: the
         *  canopy is a tile's, over each of its texels. */
        const uint8_t *l = lamps_light(), *t = trees_canopy();
        const int32_t  n = r_map * LAMP_TEXELS;
        int32_t        i;
        for (i = 0; i < n * n; ++i)
        {
            const int32_t at  = (i / n / LAMP_TEXELS) * r_map + (i % n) / LAMP_TEXELS;
            s_laid[4 * i]     = l[2 * i];
            s_laid[4 * i + 1] = l[2 * i + 1];
            s_laid[4 * i + 2] = t[2 * at];
            s_laid[4 * i + 3] = t[2 * at + 1];
        }
        if (gpu_set_laid(a->gpu, s_laid, n) != 0)
            return -1;
    }
    {
        float leaf[TREES_SPECIES_MAX][2][4], rag[TREES_SPECIES_MAX];
        trees_leaf_colors(leaf, rag);
        gpu_set_leaves(a->gpu, (const float (*)[2][4])leaf, rag);
    }
    if (gpu_set_mesh(a->gpu, &a->mesh, show_tri_key) != 0)
        return -1;
    show_arrive();
    land_settle_landed(a);
    /*  The streets the world just built draws, kept for the sweep: the
     *  next build works them out again on its own thread.  A build answers
     *  for the chunks it changed alone.  Every other chunk keeps what the
     *  build that laid it drew. */
    {
        const uint8_t *d   = roads_drawn();
        const RCity   *v   = a->view;
        int32_t        per = r_map / MESH_CHUNK, row, col;
        if (a->shaped_n != r_map)
        {
            free(a->shaped);
            a->shaped   = (uint8_t *)calloc((size_t)r_map * (size_t)r_map, 1);
            a->shaped_n = a->shaped ? r_map : 0;
            if (!a->shaped)
                return -1;
        }
        /*  The enhanced view draws every road, railway, highway and power
         *  line from the network, and every tree from the forest's stages.  So the art of a tile carrying one is
         *  never drawn, whether or not the drawing laid it.  A piece the
         *  drawing leaves out is then a gap to see, never the 1995 art
         *  standing in for it. */
        for (row = 0; row < r_map; ++row)
            for (col = 0; col < r_map; ++col)
                if (a->mesh.chunk_changed[row / MESH_CHUNK * per + col / MESH_CHUNK])
                {
                    int32_t k = row * r_map + col;
                    RPart   p[2];
                    int     n = v ? rcity_parts(v->xbld[k], v->xbit[k], p) : 0, j, net = 0;
                    for (j = 0; j < n; ++j)
                        net |= p[j].net == RNET_ROAD || p[j].net == RNET_RAIL || p[j].net == RNET_BAND || p[j].net == RNET_POWER;
                    net |= v && tree_byte(v->xbld[k]); /* the trees are the mesh's too (trees.c) */
                    a->shaped[k] = (uint8_t)((d && d[k]) || net);
                }
        {
            int32_t k, hid = 0, drew = 0;
            for (k = 0; k < r_map * r_map; ++k)
                hid += a->shaped[k] != 0, drew += d && d[k] != 0;
            R_DBG("mesh", "the art of %d tiles is not drawn: the world draws them from their shape, %d of them this build", (int)hid, (int)drew);
        }
        shaped_upload(a);
    }
    if (a->ground_n == r_map)
    {
        float *t        = a->ground;
        a->ground       = a->ground_next;
        a->ground_next  = t;
        a->ground_under = a->ground_next_under;
    }
    /*  A world built after the last drag ended shows what it took, and
     *  its marks go.  An older world does not, so they stay. */
    a->shown_gen = a->build_gen;
    if (!a->gesturing && a->shown_gen >= a->taken_gen)
        a->n_taken = 0;
    return 0;
}

/*  The corners of every tile of the world just built, made on the
 *  frame's thread once the build is done.  They wait in `ground_next`
 *  until the device takes the world. */
static void ground_keep(App *a, const RCity *v)
{
    const size_t n = (size_t)r_map * (size_t)r_map * 4u;
    int32_t      col, row;
    if (a->ground_n != r_map)
    {
        free(a->ground);
        free(a->ground_next);
        a->ground      = (float *)calloc(n, sizeof(float));
        a->ground_next = (float *)calloc(n, sizeof(float));
        a->ground_n    = a->ground && a->ground_next ? r_map : 0;
        if (!a->ground_n)
            return;
        a->ground_under = -1;
    }
    for (row = 0; row < r_map; ++row)
        for (col = 0; col < r_map; ++col)
            mesh_tile_corners(v, col, row, a->build_under, &a->ground_next[((size_t)row * (size_t)r_map + (size_t)col) * 4u]);
    a->ground_next_under = a->build_under;
}

/*  What a build leaves to the thread that owns the device.  That is the
 *  water's field, the world's upload or its hold, and the moving world on
 *  the new lanes.  `v` is the city the build read.
 *
 *  A build that FAILED, and this still answers that it did.  The rest of
 *  this runs on the half of a mesh the build did make.  A city whose
 *  FIRST build faults then has what was finished on the screen instead
 *  of an empty window.  Every check still fails and the exit code is
 *  unchanged: what is SHOWN is not what is reported. */
static int remesh_finish(App *a, int faulted, const RCity *v)
{
    const RAtlasLevel *l     = a->sw.level;
    uint8_t           *shore = s_shore;
    shore_field(v, shore);
    /*  --field-dump path writes the field as a binary PPM (r: water's
     *  distance to land, g: depth, b: land's distance to water; all x16). */
    if (g_dev.field_dump)
    {
        FILE *fp = fopen(g_dev.field_dump, "wb");
        if (fp)
        {
            int32_t k;
            fprintf(fp, "P6\n%d %d\n255\n", r_map, r_map);
            for (k = 0; k < r_map * r_map; ++k)
                fwrite(&shore[k * 4], 1, 3, fp);
            fclose(fp);
        }
    }
    /*  The last world's triangles the build did not make again are marked
     *  on the device, and they go.  The last world stays on the device
     *  until they have gone.  The frames go on meanwhile, and the next
     *  one that finds them gone puts this one up. */
    if (a->gpu)
    {
        static uint32_t at[SHOW_SLOTS], n[SHOW_SLOTS];
        const uint32_t *keys = NULL;
        if (show_rekey(&keys, at, n) && gpu_show_rekey(a->gpu, keys, at, n) != 0)
            return -1;
    }
    ground_keep(a, v);
    if (show_holds())
        a->upload_held = 1;
    else if (world_upload(a) != 0)
        return -1;
    if (faulted)
        return -1;
    R_DBG("mesh", "%u vertices, %u retaining walls; canvas %dx%d origin %d,%d "
                  "tile %dx%d step %d",
          (unsigned)a->mesh.n_land,
          (unsigned)a->mesh.n_walls,
          (int)a->sw.w,
          (int)a->sw.h,
          (int)a->sw.ox,
          (int)a->sw.oy,
          (int)l->tile_w,
          (int)l->tile_h,
          (int)l->alt_step);
    /*  Shown, and still a failure. */
    return faulted ? -1 : 0;
}

int remesh(App *a)
{
    int faulted, rc;
    if (!a->sw.level)
        return -1;
    /*  A build on its own thread owns the mesh and the network's stores
     *  until it is done.  So it is taken first, and this build starts
     *  from what it laid. */
    if (remesh_wait_done(a) != 0)
        return -1;
    /*  A world built and still held back goes up first: the build after
     *  it starts from what the device shows. */
    if (a->upload_held && world_upload(a) != 0)
        return -1;
    a->build_under = a->opts.underground;
    a->build_gen   = a->taken_gen;
    faulted        = build_world(&a->mesh, a->view, &a->atlas, a->sw.level, a->opts.underground, 1 /* the full mesh, every side and surface, once */, geometry_on(a) && !a->opts.underground) != 0;
    rc             = remesh_finish(a, faulted, a->view);
    a->mesh_dirty  = 0;
    return rc;
}

/*  THE BUILD ON A THREAD OF ITS OWN, for the live window.  A build
 *  holds the frame for a third of a second or more, and an edit's pieces
 *  are meant to move meanwhile.  So in the live window the build runs
 *  here and the frames go on.  It reads a copy of the city taken as it
 *  began, since the simulation goes on with the city itself.  Until it is
 *  done it owns the mesh, the stores the networks are built into, and the
 *  build's own state.  The frame leaves all three alone meanwhile (app.c,
 *  uiglue.c).  Everything that reaches the device is done after it, on
 *  the frame's own thread. */
static int build_thread(void *ud)
{
    App *a      = (App *)ud;
    a->build_rc = build_world(&a->mesh, a->build_view, &a->atlas, a->sw.level, a->build_under, 1, a->build_lines) != 0;
    SDL_SetAtomicInt(&a->build_done, 1);
    return 0;
}

int remesh_begin(App *a)
{
    SDL_PropertiesID props;
    if (!a->sw.level)
        return -1;
    if (a->upload_held && world_upload(a) != 0)
        return -1;
    if (!a->build_view && !(a->build_view = (RCity *)calloc(1, sizeof *a->build_view)))
        return -1;
    if (rcity_copy(a->build_view, a->view) != 0)
        return -1;
    a->build_under  = a->opts.underground;
    a->build_gen    = a->taken_gen;
    a->build_lines  = geometry_on(a) && !a->opts.underground;
    a->build_frames = 0;
    a->build_t0     = SDL_GetTicksNS();
    SDL_SetAtomicInt(&a->build_done, 0);
    /*  The build recurses through its stages: a stack the size of the
     *  main thread's. */
    props = SDL_CreateProperties();
    SDL_SetPointerProperty(props, SDL_PROP_THREAD_CREATE_ENTRY_FUNCTION_POINTER, (void *)build_thread);
    SDL_SetStringProperty(props, SDL_PROP_THREAD_CREATE_NAME_STRING, "build");
    SDL_SetPointerProperty(props, SDL_PROP_THREAD_CREATE_USERDATA_POINTER, a);
    SDL_SetNumberProperty(props, SDL_PROP_THREAD_CREATE_STACKSIZE_NUMBER, 64 * 1024 * 1024);
    a->builder = SDL_CreateThreadWithProperties(props);
    SDL_DestroyProperties(props);
    if (!a->builder)
    {
        R_WARN("mesh", "no thread for the build (%s), so it runs in the frame", SDL_GetError());
        return remesh(a);
    }
    a->building = 1;
    return 0;
}

int remesh_poll(App *a)
{
    if (!a->building)
        return 0;
    if (!SDL_GetAtomicInt(&a->build_done))
    {
        ++a->build_frames;
        return 1;
    }
    SDL_WaitThread(a->builder, NULL);
    a->builder  = NULL;
    a->building = 0;
    R_DBG("mesh", "built on its own thread in %.0f ms, %d frames drawn meanwhile", (double)(SDL_GetTicksNS() - a->build_t0) / 1e6, a->build_frames);
    {
        int32_t b[INCR_BOXES][4];
        int     k, laid = 0, rc = remesh_finish(a, a->build_rc, a->build_view) != 0 ? -1 : 0;
        if (incr_window(b))
        {
            for (k = 0; k < MESH_CHUNKS; ++k)
                laid += a->mesh.chunk_laid[k];
            R_DBG("mesh", "the block %d,%d to %d,%d is built, %.0f ms after it began: %d of %d chunks", (int)b[0][0], (int)b[0][1], (int)b[0][2] - 1, (int)b[0][3] - 1, (double)(SDL_GetTicksNS() - a->build_t0) / 1e6, laid, MESH_CHUNKS);
            incr_window_set(NULL);
        }
        return rc;
    }
}

int remesh_wait_done(App *a)
{
    while (a->building)
    {
        int r = remesh_poll(a);
        if (r < 0)
            return -1;
        if (r > 0)
            SDL_Delay(1);
    }
    return 0;
}

void remesh_wait(App *a)
{
    if (!a->building)
        return;
    SDL_WaitThread(a->builder, NULL);
    a->builder  = NULL;
    a->building = 0;
}

/*  THE NEXT BLOCK TO BUILD.  The world is cut into blocks of
 *  build_block (geo.c) chunks a side.  The block named is the one with
 *  a chunk not yet built whose middle lies nearest the camera's: the
 *  grid point under the window's middle. */
int resident_aim(App *a, SDL_Window *win)
{
    static int ix  = -1;
    int        per = r_map / MESH_CHUNK, bs, bx, by, best = -1;
    float      fc = (float)r_map * 0.5f, fr = (float)r_map * 0.5f, alt, far = 0.0f;
    int        pw, ph;
    static int c_whole = -1;
    if (!a->resident || r_map <= (int)geo_num(&c_whole, "build_whole_side"))
        return 0;
    bs = (int)geo_num(&ix, "build_block");
    bs = bs < 1 ? 1 : bs > per ? per
                               : bs;
    if (win && SDL_GetWindowSizeInPixels(win, &pw, &ph))
        screen_to_grid(a, (float)pw * 0.5f, (float)ph * 0.5f, win, &fc, &fr, &alt);
    for (by = 0; by * bs < per; ++by)
        for (bx = 0; bx * bs < per; ++bx)
        {
            int   x, y, open = 0;
            float dx, dy, d;
            for (y = by * bs; y < (by + 1) * bs && y < per; ++y)
                for (x = bx * bs; x < (bx + 1) * bs && x < per; ++x)
                    open |= !a->mesh.chunk_laid[y * per + x];
            if (!open)
                continue;
            dx = ((float)bx + 0.5f) * (float)(bs * MESH_CHUNK) - fc;
            dy = ((float)by + 0.5f) * (float)(bs * MESH_CHUNK) - fr;
            d  = dx * dx + dy * dy;
            if (best < 0 || d < far)
                best = by * per + bx, far = d;
        }
    if (best < 0)
    {
        incr_window_set(NULL);
        return 0;
    }
    {
        int32_t tiles[4];
        tiles[0] = best % per * bs * MESH_CHUNK;
        tiles[1] = best / per * bs * MESH_CHUNK;
        tiles[2] = tiles[0] + bs * MESH_CHUNK - 1;
        tiles[3] = tiles[1] + bs * MESH_CHUNK - 1;
        tiles[2] = tiles[2] < r_map ? tiles[2] : r_map - 1;
        tiles[3] = tiles[3] < r_map ? tiles[3] : r_map - 1;
        incr_window_set(tiles);
    }
    return 1;
}

void resident_stale(App *a)
{
    memset(a->mesh.chunk_laid, 0, sizeof a->mesh.chunk_laid);
    land_settle_want(a);
}

/*  The backdrop: the sky, except in the underground view, which the
 *  original fills white ($15452 _FillRect) before drawing. */
const uint8_t *backdrop(const App *a)
{
    static const uint8_t white[3] = {255, 255, 255};
    return a->opts.underground ? white : a->sky;
}

/*  THE SAVE.  The world is saved as .arco, the program's own format, and a
 *  path that does not end .arco is refused.  A 1995 .sc2 is loaded and
 *  never written.  A build still running is waited for first.  Answers 1 when it is written. */
static int is_arco_path(const char *path)
{
    size_t n = strlen(path);
    return n > 5 && strcmp(path + n - 5, ".arco") == 0;
}

int world_save(App *a, const char *path)
{
    int ok;
    if (!is_arco_path(path))
    {
        R_ERR("city", "%s: a world is saved as .arco", path);
        return 0;
    }
    remesh_wait(a);
    ok = arco_save_world(path, a->world, NULL, 0) == 0;
    R_DBG("city", "saved %s", path);
    return ok;
}

/*  The city's limits, where --limits-glow asks for them: the clock then
 *  writes only inside them (src/sim/sim.h).  With no glow the city is the
 *  original's own map. */
void city_limits_from_args(City *c)
{
    if (!c || !g_dev.limits_glow)
        return;
    c->limits_glow = atoi(g_dev.limits_glow);
    sim_limits(c);
    {
        int y, x, n = 0;
        for (y = 0; y < MAP_H; ++y)
            for (x = 0; x < MAP_W; ++x)
                n += T_LIMITS(c, y, x) != 0;
        R_NOTE("city", "the city's limits: %d of %d tiles, a glow of %d", n, MAP_H * MAP_W, c->limits_glow);
    }
}

/*  A turn, from a key or the menu.  With the geometry off it is the
 *  original's: a quarter at once, no camera move and no mesh in between.
 *  With it on, the camera turns by `deg` and a quarter settles when it
 *  comes to rest. */
void app_turn(App *a, float deg, SDL_Window *win)
{
    if (!geometry_on(a))
    {
        float y = fmodf(a->angle + (deg < 0.0f ? -90.0f : 90.0f) + 360.0f, 360.0f);
        cam_anchor(a, win);
        a->angle = a->gv.angle = y;
        a->cam_t               = 1.0f;
        settle_quarter_turn(a, win);
    }
    else
        rotate_by(a, deg, win);
}

/*  A quarter turn come to rest.
 *
 *      The camera pivots on the map's center from here on.
 *      So the mesh and the turned sweep meet.
 *      The anchor is held under the view's center.
 *
 *  The sweep is redone at once. */
void settle_quarter_turn(App *a, SDL_Window *win)
{
    float turn = fmodf(fabsf(a->angle), 90.0f);
    if (!(turn < 0.01f || turn > 89.99f))
        return; /* not on a quarter */
    /*  The snap is a quarter too: the fourth turn of four must hold the
     *  anchor and redo the sweep like the other three. */
    a->angle = a->gv.angle = (float)(view_quarter(a) * 90);
    a->yaw_to = a->yaw_from = a->angle;
    if (view_quarter(a))
        a->gv.pivot_c = a->gv.pivot_r = (float)(r_map / 2);
    else
    {
        a->gv.pivot_c = a->anch_c;
        a->gv.pivot_r = a->anch_r;
    }
    a->q_col = a->q_row = -1;
    a->dirty            = 1;
    /*  The sweep first, then the hold: the hold measures the anchor's
     *  canvas position against the sweep's origin, and a quarter's sweep
     *  has its own.  Held against the old one, the settle's frame showed
     *  another part of the city entirely before the next frame's hold put
     *  it right. */
    if (win && a->sw.level && resweep(a) != 0)
        return;
    cam_hold(a, win);
    ui_log(&a->us, "Turned to %d degrees", view_quarter(a) * 90);
}
