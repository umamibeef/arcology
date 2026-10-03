/*  world.c: building what gets drawn.  The sweep's op list, the terrain
 *  mesh, the traffic, and the two one-frame paths that check or shoot
 *  instead of running.  All of it is cache invalidation in the end: the
 *  app marks itself dirty, and these are what pay the debt. */
#include "adapt.h"
#include "internal.h"
#include "log.h"
#include "opt.h"
#include "project.h"
#include "sim.h"
#include "show.h"

#include <SDL3/SDL.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "dump.h"
#include "build.h"

/*  ==================================================================
 *  Building what gets drawn
 *
 *  The software sweep's op list, the terrain mesh, the traffic, and the
 *  one-frame paths that check or shoot instead of running.
 *  ================================================================== */
int resweep(App *a)
{
    const RCity *sv = a->view;
    uint64_t     t0 = SDL_GetTicksNS();
    adapt_city(a->view, a->city);
    a->opts.draw_traffic = !geometry_on(a); /* the 3D networks carry their own vehicles */
    /*  At a quarter turn the sweep runs on the renderer's view of the
     *  city from that orientation.  The art, the anchors and the
     *  painter's order the original would show, read off the unturned
     *  grid.  And is projected unturned.  The mesh, built once from the
     *  same grid, is projected at the quarter about the map's center,
     *  and the two meet.  Nothing is rewritten. */
    if (sweep_wanted(a))
    {
        adapt_city_turned(a->view_rot, a->city, sweep_wanted(a));
        sv = a->view_rot;
    }
    a->sweep_quarter = sweep_wanted(a);
    if (soft_sweep(&a->atlas, sv, &a->opts, &a->ops, &a->sw) != 0)
        return -1;
    if (gpu_set_ops(a->gpu, &a->ops, &a->sw) != 0)
        return -1;
    a->dirty = 0;
    if (g_dev.times)
        dumpf("time  resweep %.1f ms\n", (double)(SDL_GetTicksNS() - t0) / 1e6);
    return 0;
}

/*  The terrain field.  Per tile it holds a water tile's distance to the
 *  nearest land, and a land tile's distance to the nearest water.  Both
 *  are in tiles by a two-pass chamfer transform, with 3-4 weights, so
 *  diagonals count about 1.4.  The water's depth comes in levels from
 *  ALTM.  The water shader lays foam by the first and grades color by
 *  the depth.  The ground shader lays sand and damp ground by the third. */
static void chamfer(int32_t *d)
{
    int32_t r, cc;
    for (r = 0; r < R_MAP; ++r)
        for (cc = 0; cc < R_MAP; ++cc)
        {
            int32_t i = r * R_MAP + cc, v = d[i];
            if (cc > 0 && d[i - 1] + 3 < v)
                v = d[i - 1] + 3;
            if (r > 0 && d[i - R_MAP] + 3 < v)
                v = d[i - R_MAP] + 3;
            if (r > 0 && cc > 0 && d[i - R_MAP - 1] + 4 < v)
                v = d[i - R_MAP - 1] + 4;
            if (r > 0 && cc < R_MAP - 1 && d[i - R_MAP + 1] + 4 < v)
                v = d[i - R_MAP + 1] + 4;
            d[i] = v;
        }
    for (r = R_MAP - 1; r >= 0; --r)
        for (cc = R_MAP - 1; cc >= 0; --cc)
        {
            int32_t i = r * R_MAP + cc, v = d[i];
            if (cc < R_MAP - 1 && d[i + 1] + 3 < v)
                v = d[i + 1] + 3;
            if (r < R_MAP - 1 && d[i + R_MAP] + 3 < v)
                v = d[i + R_MAP] + 3;
            if (r < R_MAP - 1 && cc < R_MAP - 1 && d[i + R_MAP + 1] + 4 < v)
                v = d[i + R_MAP + 1] + 4;
            if (r < R_MAP - 1 && cc > 0 && d[i + R_MAP - 1] + 4 < v)
                v = d[i + R_MAP - 1] + 4;
            d[i] = v;
        }
}

void shore_field(const RCity *c, uint8_t *out)
{
    static int32_t d[R_MAP * R_MAP], w[R_MAP * R_MAP];
    const int32_t  big = 1 << 20;
    int32_t        k;
    for (k = 0; k < R_MAP * R_MAP; ++k)
    {
        d[k] = c->xter[k] >= 0x10u ? big : 0; /* water: distance to land */
        w[k] = c->xter[k] >= 0x10u ? 0 : big; /* land: distance to water */
    }
    chamfer(d);
    chamfer(w);
    for (k = 0; k < R_MAP * R_MAP; ++k)
    {
        int32_t v     = d[k] * R_SHORE_SCALE / 3; /* tiles * scale */
        int32_t u     = w[k] * R_SHORE_SCALE / 3;
        int32_t depth = 0;
        if (c->xter[k] >= 0x10u)
        {
            /*  ALTM's two heights: the table in bits 5..9 over the bed in
             *  bits 0..4.  The map edge draws this column with 284. */
            depth = rcity_alt_table(c->altm[k]) - rcity_alt_ground(c->altm[k]);
            if (depth < 0)
                depth = 0;
            /*  Surface water on the ground, XTER 0x30 on, has no bed of
             *  its own.  Its table is its level, so it would read as the
             *  shallows' turquoise.  The original paints a stream the
             *  sea's blue, so it reads as a level deep.  Color only. */
            if (c->xter[k] >= 0x30u && depth < 1)
                depth = 1;
            depth *= R_SHORE_SCALE;
        }
        out[k * 4]     = (uint8_t)(v > 255 ? 255 : v);
        out[k * 4 + 1] = (uint8_t)(depth > 255 ? 255 : depth);
        out[k * 4 + 2] = (uint8_t)(u > 255 ? 255 : u);
        out[k * 4 + 3] = 0;
    }
}

/*  The traffic, advanced to `time` and rebuilt into the movers' buffer.
 *  With the road mesh off, or underground, the buffer is empty. */
int traffic_frame(App *a, float time)
{
    int on = geometry_on(a) && !a->opts.underground;
    /*  A world the build faulted on has a half-made lane model, and the
     *  movers read that model.  So nothing moves, and that is not a
     *  failure to report every frame. */
    if (!on || a->world_faulted)
        return gpu_set_movers(a->gpu, NULL, 0);
    {
        float dt = a->traffic_time > 0.0f && time > a->traffic_time ? time - a->traffic_time : 0.0f;
        a->traffic_time = time;
        if (traffic_moving(&a->traffic, &a->mesh, a->view, dt, time, 1) != 0)
            return -1;
    }
    return gpu_set_movers(a->gpu, a->traffic.scratch.land, a->traffic.scratch.n_land);
}

/*  The water's field, kept from the build for the upload, which an edit
 *  may hold back a moment. */
static uint8_t s_shore[R_MAP * R_MAP * 4];

int world_upload(App *a)
{
    a->upload_held = 0;
    if (gpu_set_shore(a->gpu, s_shore, R_MAP) != 0)
        return -1;
    if (gpu_set_mesh(a->gpu, &a->mesh) != 0)
        return -1;
    show_arrive();
    return 0;
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
    /*  THE MOVING WORLD IS DOWN FROM HERE, whatever is shown.  It reads
     *  the lane model the build lays, and a build that faulted left that
     *  model half made.  So the movers are not driven over it, and the
     *  drive is not asked for a turn it cannot take. */
    if (faulted)
        a->world_faulted = 1;
    shore_field(v, shore);
    /*  --field-dump path writes the field as a binary PPM (r: water's
     *  distance to land, g: depth, b: land's distance to water; all x16). */
    if (g_dev.field_dump)
    {
        FILE *fp = fopen(g_dev.field_dump, "wb");
        if (fp)
        {
            int32_t k;
            fprintf(fp, "P6\n%d %d\n255\n", R_MAP, R_MAP);
            for (k = 0; k < R_MAP * R_MAP; ++k)
                fwrite(&shore[k * 4], 1, 3, fp);
            fclose(fp);
        }
    }
    /*  An edit's pieces still going keep the last world on the device
     *  until they have gone.  The frames go on meanwhile, and the next
     *  one that finds them gone puts this one up. */
    if (show_holds())
        a->upload_held = 1;
    else if (world_upload(a) != 0)
        return -1;
    /*  THE MOVING WORLD NEEDS A WHOLE ONE.  It is driven off the lane
     *  model.  A build that faulted part way through left that model half
     *  made.  Every lane after the fault is missing, and the ones
     *  before it may name lanes that were never laid.  So a kept mesh is
     *  shown with nothing moving on it. */
    if (faulted)
    {
        R_WARN("mesh", "the mesh is up and nothing moves on it, because the "
                       "lane model the movers read is half made");
        return -1;
    }
    if (traffic_init(&a->traffic, &a->mesh, v) != 0)
        return -1;
    a->traffic_time = 0.0f;
    /*  --traffic-t seconds advances the traffic that far on the
     *  build, so a headless frame shows it under way. */
    if (g_dev.traffic_t)
    {
        float t = (float)atof(g_dev.traffic_t), at = 0.0f;
        while (at < t)
        {
            traffic_moving(&a->traffic, &a->mesh, v, 0.05f, at, 0);
            at += 0.05f;
        }
    }
    if (traffic_frame(a, a->gv.time) != 0)
        return -1;
    if (g_dev.traffic_t)
        traffic_digest(&a->traffic, &a->mesh);
    a->world_faulted = 0;
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
    /*  A world built and still held back goes up first: the build after
     *  it starts from what the device shows. */
    if (a->upload_held && world_upload(a) != 0)
        return -1;
    faulted       = build_world(&a->mesh, a->view, &a->atlas, a->sw.level, a->opts.underground, 1 /* the full mesh, every side and surface, once */, geometry_on(a) && !a->opts.underground) != 0;
    rc            = remesh_finish(a, faulted, a->view);
    a->mesh_dirty = 0;
    return rc;
}

/*  THE BUILD ON A THREAD OF ITS OWN, for the live window.  A build
 *  holds the frame for a third of a second or more, and an edit's pieces
 *  are meant to move meanwhile.  So in the live window the build runs
 *  here and the frames go on.  It reads a copy of the city taken as it
 *  began, since the simulation goes on with the city itself.  Until it is
 *  done it owns the mesh, the stores the networks are built into, and the
 *  scripts' state.  The frame leaves all three alone meanwhile (app.c,
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
    if (!a->build_view && !(a->build_view = (RCity *)malloc(sizeof *a->build_view)))
        return -1;
    memcpy(a->build_view, a->view, sizeof *a->build_view);
    a->build_under  = a->opts.underground;
    a->build_lines  = geometry_on(a) && !a->opts.underground;
    a->build_step_i = a->build_step_n = 0;
    a->build_frames                   = 0;
    a->build_t0                       = SDL_GetTicksNS();
    SDL_SetAtomicInt(&a->build_done, 0);
    /*  The build recurses through the scripts and the stages: a stack
     *  the size of the main thread's. */
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
    R_DBG("mesh", "built on its own thread in %.0f ms, %d frames drawn meanwhile",
          (double)(SDL_GetTicksNS() - a->build_t0) / 1e6, a->build_frames);
    return remesh_finish(a, a->build_rc, a->build_view) != 0 ? -1 : 0;
}

void remesh_wait(App *a)
{
    if (!a->building)
        return;
    SDL_WaitThread(a->builder, NULL);
    a->builder  = NULL;
    a->building = 0;
}

/*  The backdrop: the sky, except in the underground view, which the
 *  original fills white ($15452 _FillRect) before drawing. */
const uint8_t *backdrop(const App *a)
{
    static const uint8_t white[3] = {255, 255, 255};
    return a->opts.underground ? white : a->sky;
}

/*  Crop the software frame to the target and compare with the GPU frame. */
int check_frame(App *a, SDL_Window *win, const char *out_path)
{
    RImage  soft, gpu, crop;
    int32_t w = 0, h = 0, x, y;
    size_t  same = 0, npx;
    int     rc;

    {
        int ww, wh;
        if (!SDL_GetWindowSizeInPixels(win, &ww, &wh))
        {
            R_ERR("check", "check: window size: %s", SDL_GetError());
            return -1;
        }
        w = ww / a->gv.scale;
        h = wh / a->gv.scale;
    }
    RGpuView fv = frame_view(a);
    if (gpu_readback(a->gpu, &fv, backdrop(a), w, h, 1, &gpu, NULL, NULL) != 0)
    {
        R_ERR("check", "check: readback failed: %s", SDL_GetError());
        return -1;
    }
    {
        RSoftOpts so = a->opts;
        so.mesh      = 1;
        if (soft_render(&soft, &a->atlas, view_quarter(a) ? a->view_rot : a->view, &so) != 0)
        {
            R_ERR("check", "check: the software render failed");
            image_free(&gpu);
            return -1;
        }
    }
    memset(&crop, 0, sizeof crop);
    crop.w    = w;
    crop.h    = h;
    npx       = (size_t)w * (size_t)h;
    crop.rgb  = (uint8_t *)malloc(npx * 3u);
    crop.idx  = (uint8_t *)calloc(npx, 1);
    crop.prov = (uint16_t *)calloc(npx, sizeof(uint16_t));
    if (!crop.rgb || !crop.idx || !crop.prov)
    {
        image_free(&gpu);
        image_free(&soft);
        image_free(&crop);
        return -1;
    }
    for (y = 0; y < h; ++y)
        for (x = 0; x < w; ++x)
        {
            int32_t  sx = x + a->gv.scroll_x, sy = y + a->gv.scroll_y;
            uint8_t *d = crop.rgb + ((size_t)y * (size_t)w + (size_t)x) * 3u;
            if (sx >= 0 && sy >= 0 && sx < soft.w && sy < soft.h)
                memcpy(d, soft.rgb + ((size_t)sy * (size_t)soft.w + (size_t)sx) * 3u, 3u);
            else
                memcpy(d, soft.rgb, 3u); /* the snapped background */
        }
    for (y = 0; y < h; ++y)
        for (x = 0; x < w; ++x)
        {
            size_t k = ((size_t)y * (size_t)w + (size_t)x) * 3u;
            if (memcmp(crop.rgb + k, gpu.rgb + k, 3u) == 0)
                ++same;
        }
    dumpf("check     %dx%d at canvas %d,%d zoom %d: %zu of %zu pixels "
           "identical (%.4f%%), software crc %#010x, gpu crc %#010x\n",
           (int)w,
           (int)h,
           (int)a->gv.scroll_x,
           (int)a->gv.scroll_y,
           (int)a->opts.zoom,
           same,
           npx,
           100.0 * (double)same / (double)npx,
           image_crc(&crop),
           image_crc(&gpu));
    rc = 0;
    if (out_path)
    {
        char path[1024];
        if (image_write_png(&gpu, out_path) != 0)
            rc = -1;
        snprintf(path, sizeof path, "%s.soft.png", out_path);
        if (image_write_png(&crop, path) != 0)
            rc = -1;
        R_NOTE("write", "%s and %s", out_path, path);
    }
    image_free(&gpu);
    image_free(&soft);
    image_free(&crop);
    return rc;
}

/*  The game's frame taken headless: three of the live loop's own frames.
 *  ImGui keeps a window hidden on the frame that first sizes it, and a
 *  table's columns settle on their own first frame.  The third read back
 *  to a PNG at `path`.  With no path the frames are run for what they
 *  lay out and fill in, and nothing is written. */
int shot_frame(App *a, SDL_Window *win, const char *path)
{
    RImage img;
    int    k, rc;
    /*  The frame's time is the clock's, or --traffic-t's.  So a shot
     *  taken with the traffic advanced is the same frame every run, and
     *  two builds can be compared on it. */
    float time = g_dev.traffic_t ? (float)atof(g_dev.traffic_t) : (float)((double)(SDL_GetTicksNS() - a->t0_ns) / 1e9);
    for (k = 0; k < 3; ++k)
        if (app_frame(a, win, -1.0f, time, k == 2 && path ? &img : NULL) != 0)
            return -1;
    if (!path)
        return 0;
    rc = image_write_png(&img, path);
    R_NOTE("write", "%s (%dx%d)", path, (int)img.w, (int)img.h);
    image_free(&img);
    return rc;
}
