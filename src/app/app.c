/*  app.c: the game's startup and its frame loop.
 *
 *      arcology [city] [--assets DIR] [--zoom F] [--scale N]
 *              [--compare-renderers out.png]  (arcology --help lists every option)
 *
 *  The simulation is the verified reconstruction, run as the original's
 *  main loop runs it.  It runs a phase of the 25-phase clock when the
 *  speed's deadline passes.  It runs the moving things every fifteen
 *  ticks, the palette runs every twelve and ninety.  --compare-renderers draws one
 *  frame both through the software sweep and on the GPU, and says
 *  whether they agree.  The keys are input.c's. */
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
#include "trees.h"
#include "ui.h"
#include "world.h"

/*  ==================================================================
 *  Startup and the frame loop
 *
 *  Options are read once, into Startup, and the loop that follows shares
 *  nothing with them.
 *  ================================================================== */

/*  The frame loop: events, the clock, the camera, and the frame itself.
 *  It shares nothing with the startup options, checked, not assumed.  So
 *  it takes only the app and its window. */
static void frame_loop(App *a, SDL_Window *win)
{
    while (!a->quit)
    {
        SDL_Event e;
        while (SDL_PollEvent(&e))
            input_event(a, win, &e);
        if (!ui_wants_keyboard(a->ui))
            cam_keys(a, win);

        {
            float dt = app_frame_dt(a);
            if (app_frame(a, win, dt, (float)((double)(a->last_ns - a->t0_ns) / 1e9), NULL) != 0)
                break;
        }
        /*  A screenshot the interface asked for, once the frame is drawn. */
        if (a->want_shot)
        {
            a->want_shot = 0;
            check_frame(a, win, "arcology-check.png");
            ui_log(&a->us, "Wrote arcology-check.png");
        }
    }
}

/*  The music draws from the city's own stream, as the original's does.
 *  src/audio knows nothing of a City.  So the city rides across as the
 *  context the player hands back. */
static uint16_t music_rand(void *at, uint16_t n)
{
    return lib_rand((City *)at, n);
}

/*  --tune name=value,name=value: the knobs, set for a headless render of
 *  one particular setting.  A knob is NAMED here, because the list of
 *  them is geo.c's and so is its order.  Give as many as the run
 *  means to move.  The rest stay at the value the declaration gave them.
 *  A name nothing declared is reported and passed over. */
static void tune_from_args(const char *arg)
{
    const char *p = arg;
    while (p && *p)
    {
        char        name[32];
        const char *eq  = strchr(p, '=');
        const char *end = strchr(p, ',');
        size_t      len;
        if (!eq || (end && eq > end))
        {
            R_WARN("tune", "--tune wants name=value: %s", p);
            return;
        }
        len = (size_t)(eq - p);
        if (len >= sizeof name)
            len = sizeof name - 1;
        memcpy(name, p, len);
        name[len] = 0;
        if (!tune_set(name, (float)atof(eq + 1)))
            R_WARN("tune", "--tune: there is no knob named %s", name);
        if (!end)
            return;
        p = end + 1;
    }
}

/*  --hour and --trees: the View menu's time of day and biome, for one run.
 *  They replace the saved choice and are not saved.  A run with no
 *  interface applies them here, since no menu applies them. */
static void view_from_args(App *a, const Startup *o)
{
    if (o->hour >= 0.0f)
    {
        a->us.hour = o->hour;
        geo_put("sun_hour", o->hour);
    }
    if (o->trees)
    {
        int k, n = trees_biomes();
        for (k = 0; k < n && strcmp(trees_biome_name(k), o->trees) != 0; ++k)
            ;
        if (k == n)
        {
            R_WARN("trees", "--trees: there is no biome named %s", o->trees);
            return;
        }
        a->us.biome = k;
        if (k != a->biome_shown)
        {
            trees_set_biome(k);
            a->biome_shown = k;
            a->mesh_dirty  = 1;
        }
    }
}

int game_main(int argc, char **argv)
{
    /*  Zeroed: the struct is a few hundred fields and only some are set
     *  below, so anything else began as whatever the stack held.  The
     *  selection's flash timer was one of them.  A large enough garbage
     *  value held `flash` true for minutes at a time, which suppressed
     *  the hover outline the inspector draws. */
    App         a  = {0};
    int         rc = 0; /* how the run went, whichever mode it was */
    SDL_Window *win;
    Startup     o;
    char        err[256];
    /*  Phase clocks for the init log.  SDL3's ticks need no SDL_Init. */
    const uint64_t t_start = SDL_GetTicksNS();
    uint64_t       t_phase = t_start, t_atlas = 0, t_city = 0, t_gpu = 0, t_sweep = 0;
#define PHASE_MS(var)                    \
    do                                   \
    {                                    \
        uint64_t now = SDL_GetTicksNS(); \
        (var)        = now - t_phase;    \
        t_phase      = now;              \
    } while (0)
    int     ww, wh, headless;
    int32_t pixel_scale;
    char    assets_dir[1024], city_path[1024];
    int     i;
    {
        int opt_rc = parse_options(&o, &a, argc, argv);
        if (opt_rc != 0)
            return opt_rc < 0 ? 0 : opt_rc;
    }
    dump_open(g_dev.dump_to); /* the developer dumps' sink, before anything dumps */
    ww          = o.ww;
    wh          = o.wh;
    pixel_scale = o.pixel_scale;
    memcpy(assets_dir, o.assets_dir, sizeof assets_dir);
    memcpy(city_path, o.city_path, sizeof city_path);

    /*  where the cities are, for resolving a name and for the menu */
    find_cities(a.us.city_dir, sizeof a.us.city_dir);
    scan_cities(&a.us);
    if (a.us.city_dir[0])
        R_DBG("cities", "%s (%d)", a.us.city_dir, a.us.n_cities);
    else
        R_DBG("cities", "none found");

    if (city_path[0])
    {
        char resolved[1024];
        if (!resolve_city(resolved, sizeof resolved, city_path, a.us.city_dir))
        {
            R_ERR("city", "no city called %s", city_path);
            return 1;
        }
        snprintf(city_path, sizeof city_path, "%s", resolved);
    }

    if (!assets_dir[0] && !find_assets(assets_dir, sizeof assets_dir, argv[0]))
    {
        R_ERR("assets", "not found; pass --assets DIR");
        return 1;
    }
    R_DBG("assets", "%s", assets_dir);
    if (o.song_out)
    {
        /*  A song to a WAV and out: no window, no device.  The check in
         *  tests/music_check.py runs this.  So can anyone who wants to
         *  hear a song without the game. */
        RMusic *mus    = music_create(assets_dir, 0);
        int     wav_rc = mus ? music_render_wav(mus, o.song_arg, o.song_out, 44100) : -1;
        if (wav_rc == 0)
            R_NOTE("music", "%s -> %s", o.song_arg, o.song_out);
        else
            R_ERR("music", "could not render %s%s", o.song_arg, mus ? "" : " (no assets/music: run tools/import_assets.py)");
        music_destroy(mus);
        return wav_rc ? 1 : 0;
    }
    if (atlas_load(&a.atlas, assets_dir) != 0)
    {
        R_ERR("atlas", "%s", a.atlas.err);
        return 1;
    }
    PHASE_MS(t_atlas);
    {
        int32_t tiles = 0;
        char    zooms[32];
        int     zn = 0;
        zooms[0]   = 0;
        for (i = 0; i < a.atlas.n_levels; i++)
        {
            tiles += a.atlas.level[i].n_tiles;
            zn += snprintf(zooms + zn, sizeof zooms - (size_t)zn, "%s%d", i ? "/" : "", (int)a.atlas.level[i].zoom);
        }
        R_NOTE("atlas", "%d levels (%s px), %d tiles, %d animated runs, %.0f ms", (int)a.atlas.n_levels, zooms, (int)tiles, (int)a.atlas.n_anim, NS_MS(t_atlas));
        for (i = 0; i < a.atlas.n_levels; i++)
            R_DBG("atlas", "%d px: %d tiles in %dx%d, tile %dx%d, level step %d", (int)a.atlas.level[i].zoom, (int)a.atlas.level[i].n_tiles, (int)a.atlas.level[i].w, (int)a.atlas.level[i].h, (int)a.atlas.level[i].tile_w, (int)a.atlas.level[i].tile_h, (int)a.atlas.level[i].alt_step);
    }
    a.view     = (RCity *)calloc(1, sizeof *a.view);
    a.view_rot = (RCity *)calloc(1, sizeof *a.view_rot);
    /*  Three views of one city.
     *
     *      The city as the simulation holds it.
     *      The renderer's reading of it.
     *      That reading turned a quarter for the sprite sweep.
     *
     *  Every way out from here gives all three back, which is what
     *  `fail` is for. */
    if (!a.view || !a.view_rot)
        goto fail;
    if (city_path[0])
    {
        R_NOTE("city", "loading %s", city_path);
        if (!app_world_load(&a, city_path))
        {
            R_ERR("city", "%s is not a city", city_path);
            goto fail;
        }
        PHASE_MS(t_city);
        set_city_name(&a, city_path);
        log_city_loaded(a.city, city_path, NS_MS(t_city));
    }
    else
    {
        /*  Nothing asked for, so start on an empty map with the load
         *  menu up.  Calloc has already made a blank city, which the
         *  view is happy to draw. */
        if (!app_world_load(&a, ""))
            goto fail;
        snprintf(a.city_base, sizeof a.city_base, "%s", "Untitled");
        snprintf(a.us.save_path, sizeof a.us.save_path, "Untitled.arco");
        a.us.open_load = 1;
    }
    /*  The generator's seed is not saved ($11DC never reaches MISC).  So a run starts from the clock, like the original.  Or it starts from --seed N.  Two builds can then be compared on the traffic they draw. */
    {
        int32_t seed = g_dev.seed ? (int32_t)atoi(g_dev.seed) : (int32_t)time(NULL);
        rng_seed(a.city, seed, (uint16_t)(seed & 0xFFFF));
    }

    t_phase = SDL_GetTicksNS();
    /*  A headless run, a check, a shot, a dump, a report, shows no
     *  window and must not show on the Dock either.  Every test launch
     *  was an icon bouncing up and gone.  The hint has to precede
     *  SDL_Init.  The system then lists the process as BackgroundOnly. */
    headless   = o.check || o.run_frames || o.shot_out || o.want_mesh_check || o.have_pick || g_dev.area != NULL ||
                 g_dev.shape_profile != NULL || g_dev.net_check || g_dev.net_svg != NULL || g_dev.probe != NULL || g_dev.tile_dump != NULL;
    a.live     = !headless;
    a.resident = (a.live || g_dev.async_build) && !g_dev.whole_build;
    a.gv.tris  = g_dev.tri_colors;
    land_settle_want(&a);
    a.main_thread = SDL_GetCurrentThreadID(); /* the frame's thread: a build on another only says how far it got */
    if (headless)
        SDL_SetHint(SDL_HINT_MAC_BACKGROUND_APP, "1");
    if (!SDL_Init(SDL_INIT_VIDEO))
    {
        R_ERR("sdl", "%s", SDL_GetError());
        return 1;
    }
    R_DBG("sdl", "video %s, driver %s", SDL_GetRevision(), SDL_GetCurrentVideoDriver() ? SDL_GetCurrentVideoDriver() : "none");
    /*  A live run with no --win opens at nine tenths of the usable area of
     *  the display, each way.  A headless run keeps the fixed size, so its
     *  shots stay the same. */
    if (!headless && !g_dev.win)
    {
        SDL_Rect r;
        if (SDL_GetDisplayUsableBounds(SDL_GetPrimaryDisplay(), &r) && r.w > 63 && r.h > 63)
        {
            ww = r.w * 9 / 10, wh = r.h * 9 / 10;
            R_NOTE("init", "a %dx%d window, nine tenths of the display's %dx%d", ww, wh, r.w, r.h);
        }
    }
    win = SDL_CreateWindow(APP_TITLE, ww, wh, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY | (headless ? SDL_WINDOW_HIDDEN : 0));
    if (!win)
    {
        R_ERR("sdl", "window: %s", SDL_GetError());
        return 1;
    }
    /*  The camera places the view in the window from the start. */
    SDL_GetWindowSizeInPixels(win, &a.win_pw, &a.win_ph);
    {
        int pw, ph;
        SDL_GetWindowSizeInPixels(win, &pw, &ph);
        R_DBG("sdl", "window %dx%d, %dx%d px%s", ww, wh, pw, ph, (o.check || o.run_frames || o.shot_out) ? ", hidden" : "");
    }
    a.gpu = gpu_create(win, &a.atlas, err, sizeof err);
    if (!a.gpu)
    {
        R_ERR("gpu", "%s", err);
        return 1;
    }
    PHASE_MS(t_gpu);
    R_NOTE("gpu", "%s, %.0f ms", gpu_driver(a.gpu), NS_MS(t_gpu));
    /*  A headless run is the live run's frame with no interface on it.
     *  A headless run lays the interface out only for --open-menu (a menu
     *  or a window), --pick (the query window) or --inspect. */
    if (a.live || ((g_dev.open_menu || o.have_pick || g_dev.inspect) && !o.check))
    {
        a.offscreen = o.shot_out != NULL; /* the interface's pipeline is made for the readback's format, so every frame is read back */
        a.ui        = ui_create(win, gpu_device(a.gpu), a.offscreen ? gpu_offscreen_format() : gpu_swapchain_format(a.gpu), 1.0f, assets_dir);
        prof_watch(build_shown, &a);                                       /* an edit's build shows from inside it */
        show_enable((a.ui && a.live && !a.offscreen) || g_dev.show_edits); /* an edit's pieces go and come where a person watches */
        R_DBG("ui", "%s", a.ui ? "imgui" : "none");
        if (a.ui)
        {
            /*  --theme for this run beats the saved preference, which
             *  beats the default.  A saved pack that has gone falls back
             *  to the default rather than to nothing. */
            char saved[64], ppath[1024];
            scan_themes(&a, assets_dir);
            R_DBG("theme", "%d packs in %s", a.us.n_themes, a.themes_dir);
            /*  The display preferences are the interactive run's alone.
             *  A check, a shot, a probe, a dump or a report must see the
             *  city as the code draws it.  It must not see it as the
             *  last session left it.  The outline view is the one that
             *  matters.  Saved on, it lays the lane graph over the ground.
             *  A measurement then answers about an overlay rather than
             *  about the world, which is wrong without looking wrong. */
            a.prefs_ok = a.live; /* a headless run reads no preference and writes none */
            if (prefs_path(ppath, sizeof ppath))
                R_DBG("prefs", "%s", ppath);
            if (o.theme_dir)
                apply_theme_choice(&a, o.theme_dir, "--theme");
            else if (prefs_get("theme", saved, sizeof saved) && saved[0] && apply_theme_choice(&a, saved, "saved preference"))
                ;
            else
                apply_theme_choice(&a, DEFAULT_THEME, "default");
        }
    }
    if (city_path[0])
        ui_log(&a.us, "Loaded %s", a.city_base);
    /*  Every number the world is built from, before anything is built, and
     *  the street layer a city without one works out. */
    geo_defaults();
    app_streets_import(&a);
    /*  --mute: no audio device at all, for a run under test.  A mesh check
     *  or a headless frame opens the app for a second and the music started
     *  every time. */
    if (((!o.check && !o.run_frames && !o.shot_out) || o.sound_test) && !g_dev.mute)
    {
        a.snd = sound_create(assets_dir);
        if (a.snd)
        {
            char on[16];
            a.mus = music_create(assets_dir, sound_device(a.snd));
            if (a.mus)
            {
                music_set_rand(a.mus, music_rand, a.city); /* the game's own stream, as the original shares it */
                /*  on unless the preference says off: the original's
                 *  Options menu starts with Music checked */
                int want = !(prefs_get("music", on, sizeof on) && strcmp(on, "off") == 0);
                R_DBG("music", "%d songs, %d instruments", music_n_songs(a.mus), 17);
                music_set_enabled(a.mus, want && !o.sound_test);
                a.us.music_on = music_enabled(a.mus);
            }
            else
                R_DBG("music", "none: run tools/import_assets.py for assets/music");
        }
        if (a.snd)
            R_DBG("sound", "%d effects", sound_loaded(a.snd));
        else
            R_WARN("sound", "no audio device");
        if (o.sound_test)
            sound_play(a.snd, o.sound_test);
    }

    if (pixel_scale < 1)
    {
        int pw, ph;
        SDL_GetWindowSizeInPixels(win, &pw, &ph);
        pixel_scale = pw >= 2 * ww ? 2 : 1;
    }
    a.gv.scale = pixel_scale;
    a.t0_ns    = SDL_GetTicksNS();
    if (r_map_set(app_world_side(&a)) != 0)
        return 1;
    /*  THE KNOBS.  --outline asks for the view outright.  Failing that --tune may
     *  name it.  Failing both, the preference the last session left. */
    if (g_dev.tune)
        tune_from_args(g_dev.tune);
    if (o.outline)
        tune_set("show_curves", 1.0f);
    /*  The switches the last session left on, put on as the menu puts them
     *  on: set in the interface's state and applied from it. */
    if (a.prefs_ok)
    {
        int pw, ph;
        SDL_GetWindowSizeInPixels(win, &pw, &ph);
        ui_fill(&a);
        prefs_switches_load(&a.us, !o.outline && !g_dev.tune);
        view_from_args(&a, &o);
        prefs_switches_seen(&a);
        ui_apply(&a, win, pw, ph);
    }
    else
        view_from_args(&a, &o);
    city_limits_from_args(a.city);
    if (view_quarter(&a))
        settle_quarter_turn(&a, NULL); /* --angle 90/180/270: the camera at that quarter, pivoting on the map's center */
    open_paused(&a);

    t_phase = SDL_GetTicksNS();
    if (resweep(&a) != 0)
    {
        R_ERR("sweep", "no %d px art set", (int)a.opts.zoom);
        return 1;
    }
    PHASE_MS(t_sweep);
    R_DBG("sweep", "canvas %dx%d at %d px, %.0f ms", (int)a.sw.w, (int)a.sw.h, (int)a.opts.zoom, NS_MS(t_sweep));
    {
        int pw, ph;
        SDL_GetWindowSizeInPixels(win, &pw, &ph);
        a.gv.scroll_x = a.sw.w / 2 - (pw / a.gv.scale) / 2;
        a.gv.scroll_y = a.sw.h / 2 - (ph / a.gv.scale) / 2;
        /*  --center puts a MAP TILE in the middle of the window.  This
         *  is how a person describes a view.  --scroll takes canvas
         *  pixels, which is how the renderer stores one.  The sweep has
         *  already run by here.  So it knows where the tile landed and
         *  no caller has to work the isometric projection out for
         *  itself. */
        if (o.have_centre)
        {
            int32_t fx, fy;
            a.opts.focus_col = o.centre_col;
            a.opts.focus_row = o.centre_row;
            a.dirty          = 1;
            if (resweep(&a) == 0 && soft_focus_result(&fx, &fy))
            {
                a.gv.scroll_x = fx - (pw / a.gv.scale) / 2;
                a.gv.scroll_y = fy - (ph / a.gv.scale) / 2;
            }
            else
                R_WARN("view", "tile %d,%d is off the canvas; "
                               "centring on the map instead",
                       o.centre_col,
                       o.centre_row);
            /*  The map view is a different camera, and the sweep's
             *  canvas is not its canvas: put the tile under the center
             *  through the camera itself. */
            if (a.plan && o.centre_col >= 0 && o.centre_col < r_map &&
                o.centre_row >= 0 && o.centre_row < r_map)
            {
                int32_t idx = o.centre_row * r_map + o.centre_col;
                a.anch_c    = (float)o.centre_col + 0.5f;
                a.anch_r    = (float)o.centre_row + 0.5f;
                a.anch_alt  = (float)rcity_alt_surface(a.view->map.altm[idx], a.view->map.xter[idx]);
                /*  The hold turns the anchor about the view's pivot, as
                 *  the frame turns the world.  In the app the anchor IS
                 *  the pivot (cam_anchor), and here it has to be too.  With
                 *  another pivot the tile lands where the map's center
                 *  turns it. */
                a.gv.pivot_c = a.anch_c;
                a.gv.pivot_r = a.anch_r;
                cam_hold(&a, win);
            }
        }
        if (o.have_scroll)
        {
            a.gv.scroll_x = o.scroll_x;
            a.gv.scroll_y = o.scroll_y;
        }
    }
    a.mesh_dirty = 1;
    a.zoom_world = (float)a.opts.zoom / 32.0f;
    a.gv.zoom    = 1.0f;
    if (a.gv.pitch <= 0.0f)
        a.gv.pitch = ARC_PITCH_DEG; /* the game's own camera */
    a.cam_t = 1.0f;
    if (o.zoomf > 0.0f)
    {
        int pw, ph;
        SDL_GetWindowSizeInPixels(win, &pw, &ph);
        zoom_to(&a, o.zoomf, (float)pw * 0.5f, (float)ph * 0.5f);
        if (a.dirty && resweep(&a) != 0)
            return 1;
    }
    if (a.angle != 0.0f)
    {
        /* --angle: the view --scroll and the zoom give, turned about its center */
        float ang = a.angle;
        a.angle   = 0.0f;
        cam_anchor(&a, win);
        a.angle = ang;
    }
    /*  --plan with nowhere named looks at the middle of the map: the
     *  sweep's canvas is the city view's, not this camera's. */
    if (a.plan && !o.have_centre && !o.have_scroll)
    {
        int32_t idx = (r_map / 2) * r_map + r_map / 2;
        a.anch_c = a.anch_r = (float)(r_map / 2);
        a.anch_alt          = (float)rcity_alt_surface(a.view->map.altm[idx], a.view->map.xter[idx]);
        a.gv.pivot_c        = a.anch_c;
        a.gv.pivot_r        = a.anch_r;
        cam_hold(&a, win);
    }

    R_NOTE("init", "ready in %.0f ms (atlas %.0f, city %.0f, gpu %.0f, sweep %.0f)", NS_MS(SDL_GetTicksNS() - t_start), NS_MS(t_atlas), NS_MS(t_city), NS_MS(t_gpu), NS_MS(t_sweep));
#undef PHASE_MS

    if (run_mode(&a, win, &o, &rc))
        goto done;

    if (o.run_frames > 0)
        a.quit = 1;

    frame_loop(&a, win);

done:
    /*  The one way out, whichever mode the run was.  Every headless mode
     *  builds the same world as the game does.  So it gives back the
     *  same world: a mode that let itself out early left the whole mesh
     *  behind. */
    remesh_wait(&a); /* a build still running finishes before what it builds into goes */
    if (g_dev.save_to && !world_save(&a, g_dev.save_to))
        R_ERR("city", "could not save %s", g_dev.save_to);
    incr_free(&a.mesh);
    mesh_free(&a.mesh);
    mesh_emit_free(); /* and the emitter's own (mesh/emit.c) */
    ops_free(&a.ops);
    ui_destroy(a.ui);
    music_destroy(a.mus);
    sound_destroy(a.snd);
    gpu_destroy(a.gpu);
    SDL_DestroyWindow(win);
    SDL_Quit();
    app_world_free(&a);
    app_views_free(&a);
    atlas_free(&a.atlas);
    return rc;

fail:
    /*  Anything the run had opened by the point it gave up is its own to
     *  close.  What is always ours by here is the world and the views. */
    app_world_free(&a);
    app_views_free(&a);
    atlas_free(&a.atlas);
    return 1;
}
