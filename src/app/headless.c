/*  headless.c: the runs that show no window.  A mode builds the world
 *  and does one thing with it, and then the run is over.  That thing is a
 *  check of the GPU against the software sweep, a shot, a dump or a
 *  report. */
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
static int shot_frame(App *a, SDL_Window *win, const char *path)
{
    RImage img;
    int    k, rc;
    float  time = (float)((double)(SDL_GetTicksNS() - a->t0_ns) / 1e9);
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

/*  The build a headless mode reads: the first window, then each further
 *  window --build-window lists, built into the mesh standing one after
 *  another.  The whole map where none is named.  With `always` clear, a
 *  build the frames already made stands, and only a mesh still dirty is
 *  built again.  0 on success. */
static int remesh_windows(App *a, int always)
{
    if (remesh_wait_done(a) != 0)
        return -1;
    if ((always || a->mesh_dirty) && remesh(a) != 0)
        return -1;
    while (incr_window_next())
        if (remesh(a) != 0)
            return -1;
    return 0;
}

/*  The headless modes: a build and one thing done with it, then the run
 *  is over.  1 where a mode ran, with how it went in `out`.  0 where none
 *  was asked for, and the live loop is next. */
int run_mode(App *a, SDL_Window *win, const Startup *o, int *out)
{
    int rc = 0;
    /*  --gesture SPEC: build, work the player's tools as the mouse would,
     *  and let the paths below run and build again.  The gestures come
     *  first, so a run after them simulates the city they left.  A bar
     *  between two specs builds between them, as the edits of a player
     *  one after another do. */
    if (g_dev.gesture)
    {
        const char *s = g_dev.gesture;
        if (remesh(a) != 0)
            build_failed();
        for (;;)
        {
            const char *bar  = strchr(s, '|');
            size_t      n    = bar ? (size_t)(bar - s) : strlen(s);
            char       *spec = (char *)malloc(n + 1);
            int         ok;
            if (!spec)
                return 1;
            memcpy(spec, s, n);
            spec[n] = 0;
            ok      = gesture_script(a, spec);
            free(spec);
            if (!ok)
            {
                R_ERR("app", "--gesture %s: a tool that does not read, or is not reconstructed", g_dev.gesture);
                return 1;
            }
            if (resweep(a) != 0)
                R_ERR("app", "sweep failed");
            if (!bar)
                break;
            if (remesh(a) != 0)
                build_failed();
            s = bar + 1;
        }
        a->mesh_dirty = 1;
    }
    if (o->run_frames > 0)
    {
        /*  Headless: the game's own frame a number of times at a speed,
         *  and how far the clock got.  So the schedule can be checked
         *  without watching the window. */
        int32_t  date0 = a->city->date;
        uint64_t t0    = SDL_GetTicksNS();
        int      f;
        if (o->run_speed)
            set_speed(a, o->run_speed);
        for (f = 0; f < o->run_frames; ++f)
        {
            float dt = app_frame_dt(a);
            if (app_frame(a, win, dt, (float)((double)(a->last_ns - a->t0_ns) / 1e9), NULL) != 0)
                break;
        }
        dumpf("run       %d frames at speed %d in %.2f s: date %d -> %d "
              "(%d phases), funds %d, population %d, palette steps %d/%d\n",
              o->run_frames,
              (int)a->speed,
              (double)(SDL_GetTicksNS() - t0) / 1e9,
              (int)date0,
              (int)a->city->date,
              (int)(a->city->date - date0),
              (int)a->city->funds,
              (int)a->city->population,
              (int)a->anim_a,
              (int)a->anim_b);
        /*  And each city of the world, when there are more than one. */
        if (a->world->n_city > 1)
        {
            int ci;
            for (ci = 0; ci < a->world->n_city; ++ci)
                dumpf("city      %d at %d,%d: date %d, funds %d, population %d\n", ci, (int)a->world->city[ci].city->ox, (int)a->world->city[ci].city->oy, (int)a->world->city[ci].city->date, (int)a->world->city[ci].city->funds, (int)a->world->city[ci].city->population);
        }
        a->dirty = 1;
        if (resweep(a) != 0)
            R_ERR("app", "sweep failed");
    }
    /*  The mesh check's camera.  The turned build cuts all four edges: a
     *  closed surface.  With --check-open set the plain build is checked
     *  instead, whose two uncut edges are open by design: the checker's
     *  own test.  It is set before an edit.  So the build before the
     *  edit and the edit's own build are under the same key.  The second
     *  is an edit's build (incr.c), not a whole one. */
    if (o->want_mesh_check)
        a->angle = g_dev.check_open ? 0.0f : 1.0f;
    /*  --edit C,R[;C,R...]: build, demolish those tiles, and let the
     *  path below build again: the incremental rebuild's test
     *  (incr.c). */
    if (g_dev.edit)
    {
        const char *s = g_dev.edit;
        int         col, row, n;
        if (remesh(a) != 0)
            build_failed();
        while (sscanf(s, "%d,%d%n", &col, &row, &n) == 2)
        {
            {
                int32_t cc = col, cr = row;
                if (app_city_at(a, &cc, &cr))
                    sim_demolish_tile(a->city, cr, cc, 0, 0);
            }
            s += n;
            if (*s != ';')
                break;
            ++s;
        }
        a->dirty = 1;
        if (resweep(a) != 0)
            R_ERR("app", "sweep failed");
        a->mesh_dirty = 1;
    }
    if (o->have_pick)
    {
        char what[160] = "";
        if (remesh_windows(a, 0) != 0)
            build_failed();
        pick_tile(a, o->pick_x, o->pick_y, win);
        /*  The game's own frames with the pointer there.  The interface
         *  is laid out and filled.  With --shot the frame is written,
         *  with the query window on it, and then what they filled in,
         *  dumped. */
        if (o->shot_out && a->ui)
            a->us.show_query = 1;
        rc = shot_frame(a, win, o->shot_out);
        if (a->q_col >= 0)
            mesh_query(a->view, a->q_col, a->q_row, what, sizeof what);
        dumpf("pick      window %g,%g: column %d row %d  %s\n", (double)o->pick_x, (double)o->pick_y, (int)a->q_col, (int)a->q_row, what);
        if (a->us.q_ok)
            dumpf("pick      footprint %d x %d, north-east tile column %d row %d; "
                  "outline %.0f,%.0f %.0f,%.0f %.0f,%.0f %.0f,%.0f\n",
                  (int)a->us.q_size,
                  (int)a->us.q_size,
                  (int)a->us.q_ocol,
                  (int)a->us.q_orow,
                  (double)a->us.q_poly[0][0],
                  (double)a->us.q_poly[0][1],
                  (double)a->us.q_poly[1][0],
                  (double)a->us.q_poly[1][1],
                  (double)a->us.q_poly[2][0],
                  (double)a->us.q_poly[2][1],
                  (double)a->us.q_poly[3][0],
                  (double)a->us.q_poly[3][1]);
        if (inspect_dump(a))
            rc = 0;
        rc          = rc ? 1 : 0;
        return *out = rc, 1;
    }
    if (g_dev.shape_profile)
    {
        rc          = shape_profiles(g_dev.shape_profile) != 0;
        return *out = rc, 1;
    }
    if (g_dev.net_check || g_dev.net_svg)
    {
        rc          = app_net_check(a) != 0;
        return *out = rc, 1;
    }
    if (g_dev.probe)
    {
        float px, py;
        if (remesh_windows(a, 1) != 0)
            build_failed();
        if (sscanf(g_dev.probe, "%f,%f", &px, &py) == 2)
            mesh_probe(&a->mesh, px, py);
        else
            R_ERR("app", "--probe wants X,Y in tiles");
        return *out = rc, 1;
    }
    if (g_dev.area)
    {

        if (remesh_windows(a, 1) != 0)
            build_failed();
        rc          = area_report_cli(a, g_dev.area);
        rc          = rc ? 1 : 0;
        return *out = rc, 1;
    }
    /*  --tile-dump alone: the build, every window --build-window lists,
     *  and what it laid, tile by tile.  The mesh check is a mode of its
     *  own, and its verdict is not this mode's. */
    if (g_dev.tile_dump && !o->want_mesh_check)
    {
        if (remesh_windows(a, 1) != 0)
        {
            build_failed();
            rc          = 1;
            return *out = rc, 1;
        }
        mesh_tile_dump(&a->mesh, g_dev.tile_dump);
        return *out = rc, 1;
    }
    if (o->want_mesh_check)
    {
        int bad;
        /*  A mesh the build abandoned is not a mesh to check: it is
         *  missing whatever the stage that failed was to draw.  The
         *  checks would report on the hole rather than on the city. */
        if (remesh_windows(a, 1) != 0)
        {
            build_failed();
            rc          = 1;
            return *out = rc, 1;
        }
        if (g_dev.tile_dump)
            mesh_tile_dump(&a->mesh, g_dev.tile_dump);
        bad = mesh_check(&a->mesh, 1);
        if (geometry_on(a))
        {
            build_reports();
            /*  The faces that share a painter's slot are counted and
             *  reported, and no check holds their count. */
            mesh_check_overlap(&a->mesh, 1);
            /*  And geometry that COLLIDES: one surface driven through
             *  another.  It is reported the same way, and no check holds
             *  its count. */
            mesh_check_collide(&a->mesh, 1);
        }
        rc          = bad != 0;
        return *out = rc, 1;
    }
    if (o->shot_out)
    {
        if (remesh_windows(a, 0) != 0)
            build_failed();
        if (g_dev.record)
        {
            /*  --record KIND:N: N frames through the live loop's own frame
             *  at 60 Hz, each to the shot's name numbered.  So a frame drawn
             *  from a stale build, or a jump, is found by diffing neighbors.
             *  A turn turns the view a quarter.  A zoom eases it out to half
             *  and back in, as the wheel eases it.  A still holds the camera
             *  while what moves by itself moves: the land settling, a build
             *  coming in. */
            char   kind[16] = "";
            int    n = 0, f, w = 0, h = 0;
            size_t bl = strlen(o->shot_out);
            char   base[1024], path[1100];
            float  z0;
            if (sscanf(g_dev.record, "%15[a-z]:%d", kind, &n) != 2 || n < 1 ||
                (strcmp(kind, "turn") != 0 && strcmp(kind, "zoom") != 0 && strcmp(kind, "still") != 0))
            {
                R_ERR("app", "--record wants turn:N, zoom:N or still:N, not %s", g_dev.record);
                return *out = 2, 1;
            }
            if (bl > 4 && strcmp(o->shot_out + bl - 4, ".png") == 0)
                bl -= 4;
            snprintf(base, sizeof base, "%.*s", (int)bl, o->shot_out);
            SDL_GetWindowSizeInPixels(win, &w, &h);
            rc = app_frame(a, win, -1.0f, 0.0f, NULL);
            z0 = a->zoom_world > 0.0f ? a->zoom_world : 1.0f;
            if (kind[0] == 't')
                app_turn(a, 90.0f, win);
            else if (kind[0] == 'z')
                zoom_toward(a, z0 * 0.5f, (float)w * 0.5f, (float)h * 0.5f);
            for (f = 0; f < n && rc == 0; ++f)
            {
                RImage img;
                if (kind[0] == 'z' && f == n / 2)
                    zoom_toward(a, z0, (float)w * 0.5f, (float)h * 0.5f);
                if (app_frame(a, win, 1.0f / 60.0f, (float)(f + 1) / 60.0f, &img) != 0)
                {
                    rc = 1;
                    break;
                }
                snprintf(path, sizeof path, "%s_%03d.png", base, f);
                dumpf("frame %3d: angle %.3f zoom %.4f level %d building %d quarter %d swept %d scroll %d,%d origin %d,%d\n", f, (double)a->angle, (double)a->zoom_world, (int)a->opts.zoom, a->building, view_quarter(a), a->sweep_quarter, (int)a->gv.scroll_x, (int)a->gv.scroll_y, (int)a->sw.ox, (int)a->sw.oy);
                rc = image_write_png(&img, path);
                image_free(&img);
            }
        }
        else
            rc = shot_frame(a, win, o->shot_out);
        rc          = rc ? 1 : 0;
        return *out = rc, 1;
    }
    if (o->check)
    {

        if (remesh_windows(a, 1) != 0)
            build_failed();
        rc          = check_frame(a, win, o->check_out);
        rc          = rc ? 1 : 0;
        return *out = rc, 1;
    }
    return 0;
}
