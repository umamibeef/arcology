/*  frame.c: THE frame, the live loop's and every headless mode's alike.
 *  The world and the camera advanced, the interface filled and laid out,
 *  the frame drawn, and what the interface asked for applied.  The build's
 *  reports to the frame are here too: a failed build, an edit's build
 *  shown from inside it. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <SDL3/SDL.h>

#include "arc_version.h"
#include "atlas/atlas.h"
#include "build.h"
#include "city.h"
#include "dump.h"
#include "geo.h"
#include "gpu/gpu.h"
#include "incr.h"
#include "internal.h"
#include "log.h"
#include "mesh/mesh.h"
#include "music.h"
#include "network/shape.h"
#include "opt.h"
#include "options.h"
#include "prof.h"
#include "project.h"
#include "show.h"
#include "sim.h"
#include "soft/soft.h"
#include "sound.h"
#include "ui.h"
#include "world.h"

static int app_advance(App *a, SDL_Window *win, float dt, float time); /* one frame's world and camera.  -1 when the sweep fails */

/*  The view a frame is drawn with: the switches, the map view's
 *  overrides, and the underground, which is the ground alone. */
RGpuView frame_view(const App *a)
{
    RGpuView fv      = a->gv;
    fv.geometry      = geometry_on(a);
    fv.sweep_quarter = a->sweep_quarter;
    /*  The map view is read tile by tile.  So it carries the grid
     *  whether or not the city view does, from the moment the camera
     *  starts to rise. */
    fv.grid = a->gv.grid || a->plan || a->gv.pitch > 30.01f;
    fv.plan = a->plan; /* the network tints are the map view's alone */
    if (a->opts.underground && fv.geometry)
        fv.underground = 1;
    show_uniform(fv.show);
    memcpy(fv.settle, a->settle_u, sizeof fv.settle);
    /*  The scroll's fraction, where it still belongs to the scroll. */
    fv.scroll_fx = fv.scroll_fy = 0.0f;
    if (a->gv.scroll_x == a->scroll_kept[0] && a->gv.scroll_y == a->scroll_kept[1])
        fv.scroll_fx = a->pan_x, fv.scroll_fy = a->pan_y;
    return fv;
}

/*  The mesh build was abandoned.  The stage that failed has already
 *  named itself.  This says what became of the frame.  In the per-frame
 *  path the state holds for as long as the build keeps failing.  So the
 *  line is said when it CHANGES rather than once a frame, and the build
 *  coming back says so too. */
static int s_build_bad;

void build_failed(void)
{
    if (!s_build_bad)
    {
        R_ERR("mesh", "the build FAILED, so what it made is on the screen and "
                      "is incomplete");
    }
    s_build_bad = 1;
}

/*  The pieces an edit shows going and coming, to the device whenever
 *  their states changed. */
static void show_upload(App *a)
{
    static uint8_t st[SHOW_PIECES];
    if (show_states(st) && a->gpu && gpu_set_show(a->gpu, st) != 0)
        R_WARN("app", "the pieces an edit shows could not be uploaded");
}

static void build_ok(void)
{
    if (s_build_bad)
        R_NOTE("mesh", "the build is whole again");
    s_build_bad = 0;
}

/*  The share of the sky under cloud: the cities' own cloud cover
 *  (weather2, 0 to 100) on the average, and never under sky_cover_least.
 *  Nought with the clouds off, or with no mesh to shade. */
static float sky_cover(const App *a)
{
    static int c_least = -1;
    float      sum     = 0.0f;
    int32_t    k, n = 0;
    if (g_dev.no_sky || a->us.no_clouds || !a->gv.geometry || !a->world)
        return 0.0f;
    for (k = 0; k < a->world->n_city; k++)
        if (a->world->city[k].city)
            sum += (float)a->world->city[k].city->weather2 / 100.0f, n++;
    sum = n ? sum / (float)n : 0.0f;
    return fminf(1.0f, fmaxf(geo_num(&c_least, "sky_cover_least"), sum));
}

static int app_advance(App *a, SDL_Window *win, float dt, float time)
{
    step_clock(a);
    if (win)
        SDL_GetWindowSizeInPixels(win, &a->win_pw, &a->win_ph);
    /*  The sweep is made for one orientation.  Whenever the camera's
     *  quarter is not the one it was made for.  A turn come to rest on
     *  another, or leaving one for a free turn or the snap.  It is
     *  redone before the frame: drawn with any other camera it shows the
     *  city turned twice, or turned at the snap. */
    if (sweep_wanted(a) != a->sweep_quarter)
        a->dirty = 1;
    if (a->n_collapse)
        collapse_step(a);
    if (a->dirty && resweep(a) != 0)
        return -1;
    if (a->building)
    {
        /*  A build on its own thread: done or not, once a frame.  An
         *  edit's build shows its pieces going while it runs.  The next
         *  block of the world starts in the frame the last one is taken.
         *  So the blocks do not wait on the frames between them. */
        int r = remesh_poll(a);
        if (r < 0)
            build_failed();
        else if (r == 0)
        {
            build_ok();
            if (!a->mesh_dirty && !s_build_bad && resident_aim(a, win) && remesh_begin(a) != 0)
                build_failed();
        }
    }
    else if (a->mesh_dirty)
    {
        if ((a->live && a->ui && !a->offscreen) || g_dev.async_build)
        {
            /*  The live window builds on a thread of its own and draws
             *  meanwhile.  Whatever asks for a build while it runs asks
             *  for the next one.  While the world is built a block at a
             *  time, the build is aimed at the next block.  An edit
             *  builds the chunks it can reach first (incr.c). */
            a->mesh_dirty = 0;
            resident_aim(a, win);
            if (remesh_begin(a) != 0)
            {
                build_failed();
            }
        }
        else if (remesh(a) != 0)
        {
            build_failed();
            a->mesh_dirty = 0;
        }
        else
            build_ok();
    }
    /*  And with nothing else to build, the next block of the world. */
    else if (!s_build_bad && resident_aim(a, win) && remesh_begin(a) != 0)
    {
        build_failed();
    }
    /*  What the edit shows.  The world built for it goes up once its last
     *  piece has gone.  Once its last piece has come, every piece is
     *  still again.  The device hears of each. */
    if (a->upload_held && !show_holds() && world_upload(a) != 0)
        R_ERR("app", "the world built for the edit could not be uploaded");
    /*  A show that has ended reads the shapes by their ids no more, so the
     *  table is kept to the mesh then. */
    if (show_tick() && !a->building && shape_compact(&a->mesh) != 0)
        R_ERR("app", "the shape table could not be kept to the mesh");
    show_upload(a);
    land_settle_tick(a, win);
    a->gv.time      = time;
    a->gv.sky_cover = sky_cover(a);
    a->gv.wind      = !g_dev.no_sky && !a->us.no_clouds && a->gv.geometry;
    a->gv.air       = !g_dev.no_air && !a->us.no_air && a->gv.geometry;
    /*  The pointer in the frame's pixels, which the clouds keep clear
     *  round, over the map or off it.  Before the pointer has moved there
     *  is none to keep clear. */
    {
        const float dens    = a->win_density > 0.0f ? a->win_density : 1.0f;
        const int   seen    = a->q_mx != 0.0f || a->q_my != 0.0f;
        a->gv.sky_cursor[0] = seen ? a->q_mx * dens : -1e6f;
        a->gv.sky_cursor[1] = seen ? a->q_my * dens : -1e6f;
    }
    music_update(a->mus); /* the original's scheduler, once a pass */
    if (dt >= 0.0f)
    {
        /*  The camera's travel, a frame's worth. */
        cam_step(a, dt, win);
        zoom_ease(a, dt);
        /*  A quarter turn come to rest moves the pivot to the map's
         *  center and does the sweep again.  The city's data is not
         *  turned, because a turn is a change of perspective.  Off the
         *  quarters the camera turn stays a camera turn. */
        if (a->cam_t >= 1.0f && view_quarter(a) && (a->gv.pivot_c != (float)(r_map / 2) || a->gv.pivot_r != (float)(r_map / 2)))
            settle_quarter_turn(a, win);
        else if (a->cam_t >= 1.0f && !view_quarter(a) && fmodf(fabsf(a->angle), 90.0f) < 0.01f && view_quarter(a) != a->sweep_quarter)
            settle_quarter_turn(a, win); /* come to rest at the snap from a quarter: hold and re-sweep */
    }
    return 0;
}

float app_frame_dt(App *a)
{
    uint64_t now = SDL_GetTicksNS();
    float    dt  = -1.0f; /* the first frame moves no camera */
    if (a->last_ns)
    {
        float ms    = (float)((double)(now - a->last_ns) / 1e6);
        a->frame_ms = a->frame_ms > 0.0f ? a->frame_ms * 0.9f + ms * 0.1f : ms;
        a->fps      = a->frame_ms > 0.0f ? 1000.0f / a->frame_ms : 0.0f;
        dt          = ms > 100.0f ? 0.1f : ms / 1000.0f; /* capped so a stall does not jump the camera */
    }
    a->last_ns = now;
    return dt;
}

/*  An edit's build shows its pieces going while it runs.  A build holds
 *  the frame for a second or more, so the show moves only if something
 *  paints from inside the build.  A frame every thirtieth of a second is
 *  enough, and each one is time the build does not spend building.  Any
 *  other build shows nothing while it runs.
 *
 *  Never on a headless run.  There the frame is read back into an image,
 *  and one readback a frame would cost more than the build. */
void build_shown(void *ud, const char *step, int i, int n)
{
    App     *a = (App *)ud;
    RGpuView fv;
    uint64_t now;
    (void)step, (void)i, (void)n;
    if (!a || !a->ui || a->offscreen || SDL_GetCurrentThreadID() != a->main_thread || !show_active())
        return;
    now = SDL_GetTicksNS();
    if (now - a->show_painted < 33000000u)
        return;
    a->show_painted = now;
    show_upload(a);
    /*  The state the rest of the window reads was filled on the last
     *  frame the app drew, and the build moves none of it. */
    ui_frame(a->ui, &a->us);
    fv = frame_view(a);
    gpu_frame(a->gpu, &fv, backdrop(a), ui_render, a->ui);
}

/*  One frame's advance of the world and the camera, the live loop's and
 *  the headless turn's alike.  It runs the clock, and redoes the sweep
 *  when the camera's quarter is not the one it was made for, and the
 *  mesh rebuilt when dirty.  Then the camera's move by `dt`
 *  seconds (none when negative) and a quarter turn come to rest settled.
 *  -1 when the sweep fails. */
int app_frame(App *a, SDL_Window *win, float dt, float time, RImage *out)
{
    RGpuView fv;
    int      rc = 0;
    if (app_advance(a, win, dt, time) != 0)
        return -1;
    if (a->ui)
    {
        a->win_density = SDL_GetWindowPixelDensity(win);
        ui_fill(a);
        ui_frame(a->ui, &a->us);
    }
    /*  The underground view is the original's.
     *
     *      The ground at its own altitude.
     *      The seabed under water.
     *      In the underground art.
     *      With the pipes and subways on it.
     *
     *  The surface mesh and the water are not part of it. */
    fv = frame_view(a);
    if (out || a->offscreen)
    {
        /*  Read back: into the caller's image, or into one thrown away
         *  when the frame is only for what it settles and fills in. */
        RImage scratch, *img = out ? out : &scratch;
        int    pw, ph;
        if (!SDL_GetWindowSizeInPixels(win, &pw, &ph))
            return -1;
        rc = gpu_readback(a->gpu, &fv, backdrop(a), pw, ph, a->gv.scale, img, a->ui ? ui_render : NULL, a->ui);
        if (rc != 0)
            R_ERR("frame", "readback failed: %s", SDL_GetError());
        else if (!out)
            image_free(&scratch);
    }
    else if (gpu_frame(a->gpu, &fv, backdrop(a), a->ui ? ui_render : NULL, a->ui) != 0)
        R_ERR("frame", "%s", SDL_GetError());
    if (a->ui)
    {
        int pw, ph;
        SDL_GetWindowSizeInPixels(win, &pw, &ph);
        ui_apply(a, win, pw, ph);
    }
    return rc;
}
