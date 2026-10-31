/*  options.cpp: what the command line says, read by CLI11.  CLI11 binds
 *  every option straight into the Startup, the App and the developer
 *  switches (g_dev, opt.h).  There is no argv loop and no string table,
 *  and --help is generated from the same registrations.  A flag nothing
 *  registered is an error.  Split out of app.c: reading the arguments
 *  and running the program are two jobs. */
#include <dirent.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>

#include <SDL3/SDL.h>

#include "CLI11.hpp"
#include <string>
#include <vector>

extern "C" {
#include "arc_version.h"
#include "internal.h"
#include "log.h"
#include "opt.h"
#include "options.h"
#include "soft/soft.h"
}
#ifndef _WIN32
    #include <sys/utsname.h>
#endif

/*  The platform, for the banner: what SDL calls it, the kernel and
 *  the machine where uname can say, and the SDL this is running on. */
static void platform_line(char *out, size_t n)
{
    const int v = SDL_GetVersion();
#ifdef _WIN32
    snprintf(out, n, "%s %s, SDL %d.%d.%d", SDL_GetPlatform(), sizeof(void *) == 8 ? "x64" : "x86", SDL_VERSIONNUM_MAJOR(v), SDL_VERSIONNUM_MINOR(v), SDL_VERSIONNUM_MICRO(v));
#else
    struct utsname u;
    if (uname(&u) == 0)
        snprintf(out, n, "%s, %s %s %s, SDL %d.%d.%d", SDL_GetPlatform(), u.sysname, u.release, u.machine, SDL_VERSIONNUM_MAJOR(v), SDL_VERSIONNUM_MINOR(v), SDL_VERSIONNUM_MICRO(v));
    else
        snprintf(out, n, "%s, SDL %d.%d.%d", SDL_GetPlatform(), SDL_VERSIONNUM_MAJOR(v), SDL_VERSIONNUM_MINOR(v), SDL_VERSIONNUM_MICRO(v));
#endif
}

namespace
{
/*  A developer switch: a flag, a value, or a flag with an optional
 *  value, each bound to its field of g_dev. */
struct DevFlag
{
    const char *name;
    int        *set;
    const char *help;
};
struct DevValue
{
    const char  *name;
    const char **val;
    const char  *help;
    const char  *text;
};
std::string s_hold[24]; /* the values' storage, as long as the program */

/*  A flag is a flag: no "[0]" after it in the help. */
struct Plain : CLI::Formatter
{
    std::string make_option_opts(const CLI::Option *opt) const override
    {
        return opt->get_expected_max() == 0 ? std::string() : CLI::Formatter::make_option_opts(opt);
    }
};

const DevFlag FLAGS[] = {
    {"net-dump",    &g_dev.net_dump,    "the road network's topology: every end and every road, by place"                                                },
    {"net-check",   &g_dev.net_check,   "build the road network from the cells and check that every lane goes on and is fed, then stop: nothing is drawn"},
    {"input-log",   &g_dev.input_log,   "every mouse and touch event as it arrives, to see what the threadpad sends"                                     },
    {"inspect",     &g_dev.inspect,     "the inspector on from the start: the mesh under the pointer outlined, named and reported"                       },
    {"gpu-tight",   &g_dev.gpu_tight,   "the mesh's GPU slots with no room to grow, laid out afresh at every upload"                                     },
    {"show-edits",  &g_dev.show_edits,  "an edit's pieces go and come even with no window, so a --shot can catch them"                                   },
    {"async-build", &g_dev.async_build, "a build the frame asks for runs on its own thread even with no window, as the live one's does"                  },
    {"tri-colors",  &g_dev.tri_colors,  "each triangle of the mesh in a color of its own, to see how it is cut (Cmd-Shift-W)"                            },
    {"whole-build", &g_dev.whole_build, "the live window builds the whole world at once, not a block at a time nearest the camera first"                 },
    {"spike-check", &g_dev.spike_check, "with --mesh-check: also fail on a ground face that spans over a level or is wider than a tile"                  },
    {"check-open",  &g_dev.check_open,  "with --mesh-check: check the plain build, whose two uncut map edges are open, to test the checker itself"       },
    {"times",       &g_dev.times,       "where a build's time goes, phase by phase"                                                                      },
    {"mute",        &g_dev.mute,        "no sound and no music"                                                                                          },
    {"gpu-counts",  &g_dev.gpu_counts,  "each frame's count of sprites and mesh triangles drawn, in the debug log (with -v)"                             },
};
const DevValue VALUES[] = {
    {"limits-glow",   &g_dev.limits_glow,   "the city's limits: the clock writes only within N tiles of what is zoned or built",                                                                                                                                                                     "N"                                   },
    {"save-to",       &g_dev.save_to,       "once the run is done, the world saved as the Save menu saves it, to a path that ends .arco",                                                                                                                                                            "FILE"                                },
    {"tune",          &g_dev.tune,          "the knobs the tuning window shows, by name",                                                                                                                                                                                                            "NAME=VALUE,NAME=VALUE"               },
    {"win",           &g_dev.win,           "the window's size in pixels",                                                                                                                                                                                                                           "WxH"                                 },
    {"cities",        &g_dev.cities,        "the directory the city files are loaded from",                                                                                                                                                                                                          "DIR"                                 },
    {"edit",          &g_dev.edit,          "build, demolish these tiles, and build again: the incremental rebuild's test",                                                                                                                                                                          "C,R[;C,R...]"                        },
    {"gesture",       &g_dev.gesture,       "build, work the player's tools as the mouse would, and build again. FLAGS 1 Command, 2 underground. A bar builds between two",                                                                                                                          "TOOL,SUB[,FLAGS]:C,R/C,R[;...][|...]"},
    {"record",        &g_dev.record,        "with --shot: N frames at 60 Hz, each to the shot's name numbered.  turn turns the view a quarter, zoom eases it out to half and back, still holds the camera while the land settles and a build comes in",                                              "turn:N|zoom:N|still:N"               },
    {"work-outside",  &g_dev.work_window,   "with --times and --edit: work further than N tiles from the edited tiles is counted apart, and each unit of it listed.  Without it the count is of work further than 3",                                                                                "N"                                   },
    {"area",          &g_dev.area,          "the debug report of an area, to the dump sink; the map view's Shift-drag makes the same",                                                                                                                                                               "C0,R0-C1,R1"                         },
    {"map-tile",      &g_dev.map_tile,      "the renderer draws the city K x K times over, side by side: a map K times as wide, for measuring",                                                                                                                                                      "K"                                   },
    {"seed",          &g_dev.seed,          "seed the generator with N instead of the clock, so two runs play the same",                                                                                                                                                                             "N"                                   },
    {"mesh-dump",     &g_dev.mesh_dump,     "every face of one tile, as the build lays it",                                                                                                                                                                                                          "C,R"                                 },
    {"tile-dump",     &g_dev.tile_dump,     "every face whose centroid lies on a tile, or 'all' for the whole mesh by tile",                                                                                                                                                                         "C,R"                                 },
    {"dump-to",       &g_dev.dump_to,       "write every --x-dump to FILE instead of stdout ('-' is stdout)",                                                                                                                                                                                        "FILE"                                },
    {"field-dump",    &g_dev.field_dump,    "the terrain field, to a file",                                                                                                                                                                                                                          "FILE"                                },
    {"open-menu",     &g_dev.open_menu,     "with --shot: a menu open from the first frame, by its title, tool:N for the palette's sub-tool menu of tool N, tip:N for its tooltip, or window:NAME for a window or dialog (load, save, city, budget, graphs, query, tuning, shading, renderer, log)", "NAME"                                },
    {"show-at",       &g_dev.show_at,       "with --show-edits: an edit's pieces as they stand this long after it, for a --shot",                                                                                                                                                                    "SECONDS"                             },
    {"build-window",  &g_dev.build_window,  "a build lays only the chunks the tiles C0,R0 to C1,R1 lie in and reads the map only so far round them.  Each further window is built after it into the mesh standing",                                                                                  "C0,R0,C1,R1[;...]"                   },
    {"net-svg",       &g_dev.net_svg,       "the road network as a diagram, written to FILE.  It runs the network check (--net-check)",                                                                                                                                                              "FILE"                                },
    {"net-svg-box",   &g_dev.net_svg_box,   "with --net-svg: the diagram of the tiles from C0,R0 to C1,R1 alone, drawn larger",                                                                                                                                                                      "C0,R0,C1,R1"                         },
    {"shape-profile", &g_dev.shape_profile, "street and deck profiles read from FILE, each line 'street' or 'deck' and its samples s,h, through the road shape's own solves",                                                                                                                        "FILE"                                },
    {"probe",         &g_dev.probe,         "every surface over one point of the map, highest first",                                                                                                                                                                                                "X,Y"                                 },
    {"gpu-dump",      &g_dev.gpu_dump_at,   "a tile's instances on the device",                                                                                                                                                                                                                      "C,R"                                 },
};
} // namespace

extern "C" int parse_options(Startup *o, App *a, int argc, char **argv)
{
    std::string              pos1, pos2, assets, look, check_s, shot_s, theme, scroll, centre, pick, mark, angle_s, pitch_s, trees_s;
    std::vector<std::string> song, disable;
    std::vector<std::string> dev_vals(sizeof VALUES / sizeof VALUES[0]);
    int                      ww = 1280, wh = 800;
    static std::string       hold_check, hold_shot, hold_theme, hold_trees, hold_song[2];

    memset(a, 0, sizeof *a);
    memset(o, 0, sizeof *o);
    o->hour = -1.0f; /* the store's own hour, or the saved one */
    memset(&g_dev, 0, sizeof g_dev);
    soft_defaults(&a->opts);
    a->gv.pivot_c = a->gv.pivot_r = 64.0f; /* until the view turns: the map's center */
    a->sky[0]                     = 16;
    a->sky[1]                     = 20;
    a->sky[2]                     = 22;

    CLI::App app{"Arcology -- the SimCity 2000 simulation, reconstructed.\n"
                 "With no city, the load menu opens.  arcology --modes lists the developer modes."};
    app.name(argc > 0 ? argv[0] : "arcology");
    app.set_help_flag("-h,--help", "the game's options");
    app.formatter(std::make_shared<Plain>());
    app.get_formatter()->column_width(28);
    /*  Both positional arguments are optional, and either can be given:
     *
     *      arcology                     the load menu
     *      arcology Bayview             by name, from the cities directory
     *      arcology path/to/City        by path
     *      arcology assets path/City    assets first, also understood
     *
     *  --assets DIR overrides the search, as does --cities. */
    app.add_option("city", pos1, "a city, by name from the cities directory or by path (or the assets directory, with the city after it)");
    app.add_option("second", pos2, "the city, when the first argument was the assets directory")->group("");
    app.add_option("--assets", assets, "the directory the art is imported into (assets/)")->option_text("DIR");
    app.add_option(ARC_SWITCHES[ARC_SW_SCALE].name, o->pixel_scale, "the pixel scale, 1 or 2; auto when not given")->option_text("N");
    app.add_option("--look", look, "how the world is drawn: sprites, the original's terrain and water art; geometry, the mesh and the art standing on it; or mesh, the mesh alone.  Every run draws geometry but --compare-renderers, which draws sprites")->check(CLI::IsMember({"sprites", "geometry", "mesh"}))->option_text("sprites|geometry|mesh");
    app.add_flag("--mesh-check", o->want_mesh_check, "build the world and prove it has no free edge; then the overlap and collision checks");
    app.add_flag("--grid", a->gv.grid, "the sprites' grid outline on the mesh");
    app.add_flag("--cells", a->us.show_cells, "every cell's col,row at its bottom-right corner, in the map view");
    app.add_flag("--outline", o->outline, "the road network's lane graph drawn over the bare ground");
    app.add_flag(std::string("!") + ARC_SWITCHES[ARC_SW_NO_THINGS].name, a->opts.draw_things, "no things: the sprites of what stands on the tiles");
    app.add_flag(ARC_SWITCHES[ARC_SW_UNDERGROUND].name, a->opts.underground, "the underground view");
    app.add_option("--center,--centre", centre, "put map tile (col,row) in the middle")->option_text("C,R");
    app.add_option("--scroll", scroll, "the canvas pixel at the top-left")->option_text("X,Y");
    app.add_option("--pick", pick, "what the query tool reads at a window point")->option_text("X,Y");
    app.add_option("--mark", mark, "map tiles marked as the pointer marks them, for a --shot")->option_text("C,R[,C,R]");
    app.add_flag("--plan", a->plan, "the map view: the camera straight down, north up");
    app.add_option("--pitch", pitch_s, "the camera anywhere between 30 and 90")->option_text("DEG");
    app.add_option("--angle", angle_s, "turn the camera about the view's center")->option_text("DEG");
    app.add_option("--hour", o->hour, "the time of day for this run, 0 to 24, as View > Time of day sets it")->check(CLI::Range(0.0f, 24.0f))->option_text("H");
    app.add_option("--trees", trees_s, "the trees' biome for this run, by its name in View > Trees")->option_text("BIOME");
    app.add_option("--zoom", o->zoomf, "how near the camera is: 1 draws the art at its own size, 2 twice as near, 0.5 half")->option_text("F");
    app.add_option("--shot", shot_s, "render one frame to a PNG and exit")->option_text("FILE");
    app.add_option("--compare-renderers", check_s, "draw one frame with the software and the GPU renderers, count the pixels that agree, and write the two side by side to FILE")->option_text("FILE");
    app.add_option("--run", o->run_frames, "advance N frames headless")->option_text("N");
    app.add_option("--speed", o->run_speed, "the speed for --run, 1 paused to 5")->option_text("N");
    app.add_option("--theme", theme, "a Kaleidoscope scheme: a pack under assets/themes, a path, or none; the default is classic7")->option_text("NAME");
    app.add_option("--song", song, "render song S (a SONG id, 10000..10018, or a .mid) to OUT.wav and exit")->expected(2)->option_text("S OUT");
    app.add_option("--sound-test", o->sound_test, "play one effect in a headless run")->option_text("N");
    {
        static int verbose, no_colour;
        verbose = no_colour = 0;
        app.add_flag("-v,--verbose", verbose, "debug logging");
        app.add_flag("--no-color,--no-colour", no_colour, "no color in the log");
        for (const DevFlag &f : FLAGS)
            app.add_flag(std::string("--") + f.name, *f.set, f.help)->group("Developer switches");
        /*  Parts of the drawing or the build turned off for one run, to see
         *  what each costs or to rule it out. */
        static const struct
        {
            const char *name;
            int        *off;
        } PARTS[] = {
            {"sort",        &g_dev.no_sort  }, /* the sprites in the sweep's order */
            {"shadows",     &g_dev.no_shadow}, /* the silhouettes under flying things */
            {"clouds",      &g_dev.no_sky   }, /* the clouds over the mesh */
            {"incremental", &g_dev.no_incr  }, /* every build a whole one */
            {"lod",         &g_dev.no_lod   }, /* the mesh whole at every zoom */
            {"air",         &g_dev.no_air   }, /* the air's haze, smog and scattered light */
        };
        std::vector<std::string> part_names;
        for (const auto &q : PARTS)
            part_names.push_back(q.name);
        app.add_option("--disable", disable, "turn parts off for one run: sort (the sprites drawn in the sweep's order), shadows (the silhouettes under flying things), clouds, incremental (every build a whole one), lod (the mesh whole at every zoom), air")
            ->delimiter(',')
            ->check(CLI::IsMember(part_names))
            ->option_text("PART,PART")
            ->group("Developer switches");
        for (size_t k = 0; k < sizeof VALUES / sizeof VALUES[0]; ++k)
        {
            app.add_option(std::string("--") + VALUES[k].name, dev_vals[k], VALUES[k].help)->option_text(VALUES[k].text)->group("Developer switches");
        }
        /*  The window's size, where the cities are, and the sound are the
         *  player's, so they are listed with the game's own options. */
        for (const char *name : {"--win", "--cities", "--mute"})
            app.get_option(name)->group(app.get_option("--assets")->get_group());
        try
        {
            app.parse(argc, argv);
        }
        catch (const CLI::CallForHelp &e)
        {
            app.exit(e);
            return -1; /* over, and well */
        }
        catch (const CLI::ParseError &e)
        {
            return app.exit(e) ? 2 : 2; /* said already */
        }
        for (const std::string &d : disable)
            for (const auto &q : PARTS)
                if (d == q.name)
                    *q.off = 1;
        if (verbose)
            log_set_level(R_LOG_DEBUG);
        if (no_colour)
            log_set_colour(0);
        for (size_t k = 0; k < sizeof VALUES / sizeof VALUES[0]; ++k)
        {
            if (app.count(std::string("--") + VALUES[k].name) > 0 && !dev_vals[k].empty())
            {
                s_hold[k]      = dev_vals[k];
                *VALUES[k].val = s_hold[k].c_str();
            }
        }
    }
    hold_check = check_s, hold_shot = shot_s, hold_theme = theme;
    o->check     = !check_s.empty();
    o->check_out = check_s.empty() ? NULL : hold_check.c_str();
    o->shot_out  = shot_s.empty() ? NULL : hold_shot.c_str();
    o->theme_dir = theme.empty() ? NULL : hold_theme.c_str();
    hold_trees   = trees_s;
    o->trees     = trees_s.empty() ? NULL : hold_trees.c_str();
    if (song.size() == 2)
    {
        hold_song[0] = song[0], hold_song[1] = song[1];
        o->song_arg = hold_song[0].c_str();
        o->song_out = hold_song[1].c_str();
    }
    if (!scroll.empty() && sscanf(scroll.c_str(), "%d,%d", &o->scroll_x, &o->scroll_y) == 2)
        o->have_scroll = 1;
    if (!centre.empty() && sscanf(centre.c_str(), "%d,%d", &o->centre_col, &o->centre_row) == 2)
        o->have_centre = 1;
    if (!pick.empty() && sscanf(pick.c_str(), "%f,%f", &o->pick_x, &o->pick_y) == 2)
        o->have_pick = 1;
    if (!mark.empty())
    {
        int32_t *m = a->mark_fixed;
        int      n = sscanf(mark.c_str(), "%d,%d,%d,%d", &m[0], &m[1], &m[2], &m[3]);
        if (n == 2)
            m[2] = m[0], m[3] = m[1];
        a->have_mark_fixed = n == 2 || n == 4;
    }
    if (!angle_s.empty())
        a->angle = a->gv.angle = (float)atof(angle_s.c_str());
    if (!pitch_s.empty())
        a->gv.pitch = (float)atof(pitch_s.c_str()); /* the camera, 30..90 */
    if (pos1.size())
    {
        if (looks_like_assets(pos1.c_str()))
        {
            snprintf(o->assets_dir, sizeof o->assets_dir, "%s", pos1.c_str());
            if (pos2.size())
                snprintf(o->city_path, sizeof o->city_path, "%s", pos2.c_str());
        }
        else
            snprintf(o->city_path, sizeof o->city_path, "%s", pos1.c_str());
    }
    if (assets.size())
        snprintf(o->assets_dir, sizeof o->assets_dir, "%s", assets.c_str());
    /*  --win WxH renders at a larger framebuffer, so a shot of the same
     *  ground carries more pixels: the window is otherwise fixed. */
    if (g_dev.win)
    {
        int w2 = 0, h2 = 0;
        if (sscanf(g_dev.win, "%dx%d", &w2, &h2) == 2 && w2 > 63 && h2 > 63)
        {
            ww = w2;
            wh = h2;
        }
    }
    o->ww = ww;
    o->wh = wh;
    /*  The geometry is the game's look, with a window and without one.
     *  The sprites are a choice (--look sprites, or Shift-Cmd-T).
     *  --compare-renderers draws the sprites unless --look says otherwise,
     *  because the software renderer it compares against draws only the
     *  sprites. */
    if (look == "geometry" || look == "mesh" || (look.empty() && !o->check))
        a->gv.geometry = 1;
    a->gv.mesh_only = look == "mesh";
    /*  --plan starts in the map view: the camera at 90, turned to the
     *  nearest square, which is where set_plan would have taken it. */
    if (a->plan)
    {
        a->gv.pitch = 90.0f;
        a->angle = a->gv.angle = PLAN_YAW; /* the map view: north up, east right */
    }
    a->q_col = a->q_row = -1;
    a->us.show_palette  = 1;
    a->us.open_menu     = g_dev.open_menu;
    a->us.show_tuning   = 0; /* --open-menu window:tuning opens it */
    a->us.show_log      = 0; /* under Windows > Messages */
    a->us.tool          = -1;
    a->us.biome         = -1; /* the city's own, until the View menu names one */
    {
        static const char *const BANNER[] = {
            "   ___                __",
            "  / _ | ___________  / /__  ___ ___ __",
            " / __ |/ __/ __/ _ \\/ / _ \\/ _ `/ // /",
            "/_/ |_/_/  \\__/\\___/_/\\___/\\_, /\\_, /",
            "                          /___//___/",
        };
        char plat[160], line[256];
        log_banner(BANNER, 5);
        platform_line(plat, sizeof plat);
        snprintf(line, sizeof line, "\nArcology %s -- the SimCity 2000 simulation, reconstructed\n%s\n\n", ARC_VERSION_FULL, plat);
        log_raw(line);
        log_raw("SimCity 2000 is copyright (c) 1993-1995 Maxis, now part of Electronic Arts Inc.\n"
                "Not affiliated with, endorsed by, or connected to Electronic Arts or Maxis.\n"
                "Arcology is copyright (c) 2026 the Arcology authors, MIT licence.\n"
                "https://github.com/umamibeef/arcology\n");
    }
    if (o->check)
        R_NOTE("init", "arcology, check against %s", o->check_out ? o->check_out : "the original");
    else if (o->run_frames)
    {
        char speed[32];
        if (o->run_speed)
            snprintf(speed, sizeof speed, "speed %d", o->run_speed);
        else
            snprintf(speed, sizeof speed, "the city's speed");
        R_NOTE("init", "arcology, headless: %d frames at %s%s%s", o->run_frames, speed, o->shot_out ? ", shot " : "", o->shot_out ? o->shot_out : "");
    }
    else if (o->shot_out)
        R_NOTE("init", "arcology, one frame to %s", o->shot_out);
    else
        R_NOTE("init", "arcology, %dx%d window", ww, wh);
    R_DBG("init", "zoom %d, scale %s, %s", (int)a->opts.zoom, o->pixel_scale < 1 ? "auto" : o->pixel_scale == 1 ? "1"
                                                                                                                : "2",
          a->gv.geometry ? "geometry" : "sprites");

    return 0;
}
