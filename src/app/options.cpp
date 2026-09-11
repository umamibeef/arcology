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

#define JSMN_STATIC
extern "C" {
#include "internal.h"
#include "arc_version.h"
#include "log.h"
#include "mesh/mesh.h"
#include "opt.h"
#include "options.h"
#include "soft/soft.h"
#include "net/net.h"
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
    int         *set; /* when the value is optional: given at all */
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
    {"curve-dump",  &g_dev.curve_dump,  "every corner: where, how far it turns, its radius"                            },
    {"junc-dump",   &g_dev.junc_dump,   "every junction: its arms, their trims, its outline"                           },
    {"buried-all",  &g_dev.buried_all,  "the mesh check lists every margin with its own piece under it"               },
    {"margin-dump", &g_dev.margin_dump, "every margin end that meets nothing"},
    {"path-dump",   &g_dev.path_dump,   "the fit stage by stage, for tools/plan.py"                                    },
    {"plan-dump",   &g_dev.plan_dump,   "the fit stage by stage, for tools/plan.py"                                    },
    {"prof-dump",   &g_dev.prof_dump,   "the finished profile of every segment"                                        },
    {"loft-dump",   &g_dev.loft_dump,   "the loft's stations"                                                          },
    {"band-dump",  &g_dev.band_dump,  "the bands and their spurs"                                            },
    {"clip-dump",   &g_dev.clip_dump,   "the road clip check's samples, tile by tile"                                  },
    {"input-log",   &g_dev.input_log,   "every mouse and touch event as it arrives, to see what the threadpad sends"     },
    {"inspect",     &g_dev.inspect,     "the inspector on from the start: the mesh under the pointer outlined, named and reported" },
    {"noscale",     &g_dev.noscale,     "the node approach unscaled by the road's width"                               },
    {"no-sort",     &g_dev.no_sort,     "draw in the sweep's order, to show what it costs"                             },
    {"no-shadow",   &g_dev.no_shadow,   "no shadow pass"                                                               },
    {"no-cap",      &g_dev.no_cap,      "no corridor grading: one pass, the roads over the raw ground"                 },
    {"no-replay",   &g_dev.no_replay,   "the building pass walks and fits again instead of replaying the segment table"},
    {"no-incr",     &g_dev.no_incr,     "every build a full one, never the edit's chunks alone"                          },
    {"no-smp-cache", &g_dev.no_smp_cache, "every loft samples its stations afresh, none from the segment table"             },
    {"grade-loft",  &g_dev.grade_loft,  "the grading pass runs the loft's record and slab too, as it once did"            },
    {"gpu-tight",   &g_dev.gpu_tight,   "the mesh's GPU slots with no room to grow, laid out afresh at every upload"      },
    {"grade-all",   &g_dev.grade_all,   "the grading pass builds everything, boxes, lanes and all"                     },
    {"grade-junc",  &g_dev.grade_junc,  "the grading pass builds the junction boxes"                                   },
    {"grade-lanes", &g_dev.grade_lanes, "the grading pass builds the lanes"                                            },
    {"spike-check", &g_dev.spike_check, "the terrain spike check"                                                      },
    {"check-open",  &g_dev.check_open,  "--check with the camera off the snap"                                         },
    {"times",       &g_dev.times,       "where a build's time goes, phase by phase"                                    },
    {"tunewin",     &g_dev.tunewin,     "open the road tuning window"                                                  },
    {"scriptwin",   &g_dev.scriptwin,   "open the script console"                                                      },
    {"mute",        &g_dev.mute,        "no sound and no music: for runs under test"                                   },
    {"gpu-debug",   &g_dev.gpu_debug,   "the device's validation layer"                                                },
    {"meet-debug",  &g_dev.lap_debug,  "the level meets, verbosely"                                               },
    {"box-debug",   &g_dev.box_debug,   "the traffic's boxes, verbosely"                                               },
};
const DevValue VALUES[] = {
    {"tune",        &g_dev.tune,         nullptr,          "the road and band knobs, as the tuning window sets them: nineteen, in its order",                                       "W_ROAD,W_RAIL,RMIN_ROAD,RMIN_RAIL,RMAX_ROAD,RMAX_RAIL,APPROACH,MARGIN,TRIM,CURVES"},
    {"lua",         &g_dev.lua,          nullptr,          "the script the rules and the knobs are read from, watched and read again when it changes",      "FILE"                                                                     },
    {"lua-eval",    &g_dev.lua_eval,     nullptr,          "run one chunk of script once the world is built, and print what it answers",                    "SRC"                                                                      },
    {"win",         &g_dev.win,          nullptr,          "render at a larger framebuffer",                                                       "WxH"                                                                              },
    {"cities",      &g_dev.cities,       nullptr,          "where the saves are",                                                                  "DIR"                                                                              },
    {"traffic-t",   &g_dev.traffic_t,    nullptr,          "advance the traffic before a headless frame",                                          "SECONDS"                                                                          },
    {"edit",        &g_dev.edit,         nullptr,          "build, demolish these tiles, and build again: the incremental rebuild's test",       "C,R[;C,R...]"                                                                     },
    {"turn",        &g_dev.turn,         nullptr,          "with --shot: a quarter turn frame by frame at 60 Hz, N frames, each to the shot's name numbered", "N"                                                                    },
    {"area",        &g_dev.area,         nullptr,          "the debug report of an area, to the dump sink; the map view's Shift-drag makes the same", "C0,R0-C1,R1"},
    {"seed",        &g_dev.seed,         nullptr,          "seed the generator with N instead of the clock, so two runs draw the same traffic",  "N"                                                                                },
    {"sweep-probe", &g_dev.sweep_probe,  nullptr,          "the fit's probe at one node",                                                          "X,Y"                                                                              },
    {"mesh-dump",   &g_dev.mesh_dump,    nullptr,          "every face of one tile",                                                               "R,C"                                                                              },
    {"tile-dump",   &g_dev.tile_dump,    nullptr,          "every face whose centroid lies on a tile, or 'all' for the whole mesh by tile",        "C,R"                                                                              },
    {"dump-to", &g_dev.dump_to, nullptr, "write every --x-dump to FILE instead of stdout ('-' is stdout)", "FILE"},
    {"field-dump",  &g_dev.field_dump,   nullptr,          "the terrain field, to a file",                                                         "FILE"                                                                             },
    {"line-dump",   &g_dev.line_dump_at, &g_dev.line_dump, "the fitted points of every segment, or of one tile's",                                 "[C,R]"                                                                            },
    {"lane-dump",   &g_dev.lane_dump_at, &g_dev.lane_dump, "a junction's ports, connectors and lanes; bare, the misses and tight turns city-wide", "[C,R]"                                                                            },
    {"probe",       &g_dev.probe,        nullptr,          "every surface over one point of the map, highest first",                               "X,Y"                                                                              },
    {"gpu-dump",    &g_dev.gpu_dump_at,  &g_dev.gpu_dump,  "a tile's instances on the device",                                                     "[R,C]"                                                                            },
};
} // namespace

extern "C" int parse_options(Startup *o, App *a, int argc, char **argv)
{
    std::string              pos1, pos2, assets, check_s, shot_s, theme, scroll, centre, pick, angle_s, pitch_s;
    std::vector<std::string> song;
    bool                     no_markings = false, no_furniture = false, no_margins = false; /* CLI11 zeroes an int it binds a flag to, and a negated flag on an int counts backwards: bools, then the fields */
    std::vector<std::string> dev_vals(sizeof VALUES / sizeof VALUES[0]);
    int                      ww = 1280, wh = 800;
    static std::string       hold_check, hold_shot, hold_theme, hold_song[2];

    memset(a, 0, sizeof *a);
    memset(o, 0, sizeof *o);
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
    app.add_option("--assets", assets, "where the art is")->option_text("DIR");
    app.add_option("--zoom", a->opts.zoom, "8, 16 or 32")->option_text("N");
    app.add_option("--scale", o->pixel_scale, "the pixel scale, 1 or 2; auto when not given")->option_text("N");
    app.add_flag("--sprites", o->sprites, "the original's terrain and water art");
    app.add_flag("--geometry", o->want_geometry, "the terrain mesh and water shader, even in a headless --run");
    app.add_flag("--terrain3d,--water3d,--roads3d", a->gv.geometry, "the same switch, by its older names");
    app.add_flag("--mesh-only", a->gv.mesh_only, "the mesh alone, no sprites");
    app.add_flag("--mesh-check", o->want_mesh_check, "build the terrain and prove it has no free edge; then the road and lane checks");
    app.add_flag("--grid", a->gv.grid, "the sprites' grid outline on the mesh");
    app.add_flag("--cells", a->us.show_cells, "every cell's col,row at its bottom-right corner, in the map view");
    app.add_flag("--no-markings", no_markings, "no road markings: blank asphalt, the marking pass off");
    app.add_flag("--no-furniture", no_furniture, "no street furniture: lamps, signs, signals, gates, the furniture pass off");
    app.add_flag("--no-margins", no_margins, "no margins: the carriageway alone, the margin pass off");
    app.add_flag("--outline", o->outline, "outline: the roads stand aside and the fitted curves and the margin network are drawn on bare ground");
    app.add_flag("!--no-things", a->opts.draw_things, "no things: the sprites of what stands on the tiles");
    app.add_flag("--underground", a->opts.underground, "the underground view");
    app.add_option("--centre,--center", centre, "put map tile (col,row) in the middle")->option_text("C,R");
    app.add_option("--scroll", scroll, "the canvas pixel at the top-left")->option_text("X,Y");
    app.add_option("--pick", pick, "what the query tool reads at a window point")->option_text("X,Y");
    app.add_flag("--plan", a->plan, "the map view: the camera straight down, north up");
    app.add_option("--pitch", pitch_s, "the camera anywhere between 30 and 90")->option_text("DEG");
    app.add_option("--angle", angle_s, "turn the camera about the view's centre")->option_text("DEG");
    app.add_option("--zoomf", o->zoomf, "continuous zoom, 1 = the 32 px set at 1:1")->option_text("F");
    app.add_option("--rotate", o->rotate_turns, "the city turned N quarters clockwise on load (the original's map rewrite; for tests)")->option_text("N");
    app.add_option("--shot", shot_s, "render one frame to a PNG and exit")->option_text("FILE");
    app.add_option("--check", check_s, "compare the software and GPU renderers, writing FILE")->option_text("FILE");
    app.add_option("--run", o->run_frames, "advance N frames headless")->option_text("N");
    app.add_option("--speed", o->run_speed, "the speed for --run, 1 paused to 5")->option_text("N");
    app.add_option("--theme", theme, "a Kaleidoscope scheme: a pack under assets/themes, a path, or none; the default is classic7")->option_text("NAME");
    app.add_option("--song", song, "render song S (a SONG id, 10000..10018, or a .mid) to OUT.wav and exit")->expected(2)->option_text("S OUT");
    app.add_option("--sound-test", o->sound_test, "play one effect in a headless run")->option_text("N");
    {
        static int verbose, no_colour;
        verbose = no_colour = 0;
        app.add_flag("-v,--verbose", verbose, "debug logging");
        app.add_flag("--no-colour,--no-color", no_colour, "no colour in the log");
        for (const DevFlag &f : FLAGS)
            app.add_flag(std::string("--") + f.name, *f.set, f.help)->group("Developer switches");
        for (size_t k = 0; k < sizeof VALUES / sizeof VALUES[0]; ++k)
        {
            auto *opt = app.add_option(std::string("--") + VALUES[k].name, dev_vals[k], VALUES[k].help)->option_text(VALUES[k].text)->group("Developer switches");
            if (VALUES[k].set)
                opt->expected(0, 1);
        }
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
        if (verbose)
            log_set_level(R_LOG_DEBUG);
        if (no_colour)
            log_set_colour(0);
        for (size_t k = 0; k < sizeof VALUES / sizeof VALUES[0]; ++k)
        {
            const bool given = app.count(std::string("--") + VALUES[k].name) > 0;
            if (VALUES[k].set)
                *VALUES[k].set = given;
            if (given && !dev_vals[k].empty())
            {
                s_hold[k]      = dev_vals[k];
                *VALUES[k].val = s_hold[k].c_str();
            }
        }
    }
    a->gv.markings  = no_markings ? 0 : 1;
    a->gv.furniture = no_furniture ? 0 : 1;
    a->gv.margins = no_margins ? 0 : 1;
    hold_check = check_s, hold_shot = shot_s, hold_theme = theme;
    o->check     = !check_s.empty();
    o->check_out = check_s.empty() ? NULL : hold_check.c_str();
    o->shot_out  = shot_s.empty() ? NULL : hold_shot.c_str();
    o->theme_dir = theme.empty() ? NULL : hold_theme.c_str();
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
    /*  The geometry and the water shader are the game's look.  The
     *  o->sprites are the o->check's baseline and an option
     *  (--o->sprites, or the t and y keys).  A headless run defaults to
     *  o->sprites because --o->check and --run are comparison harnesses
     *  and the o->sprites are what they compare against.  --geometry
     *  overrides that.  It is how a headless run produces a picture of
     *  the game as it actually looks, which is what
     *  tools/gen_showcase.py wants. */
    if (o->want_geometry || (!o->check && !o->run_frames && !o->sprites))
        a->gv.geometry = 1;
    /*  --plan starts in the map view: the camera at 90, turned to the
     *  nearest square, which is where set_plan would have taken it. */
    if (a->plan)
    {
        a->gv.pitch = 90.0f;
        a->angle = a->gv.angle = PLAN_YAW; /* the map view: north up, east right */
    }
    /*  --tune w_road,w_rail,rmin_road,rmin_rail,rmax_road,rmax_rail,
     *  approach,margin,trim,curves: the knobs the tuning window shows,
     *  in the struct's order, for a headless render of a particular
     *  setting.  Give as many as you mean to change.  The rest keep
     *  their defaults. */
    if (g_dev.tune)
    {
        float *t = tune_array();
        sscanf(g_dev.tune, "%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f", &t[0], &t[1], &t[2], &t[3], &t[4], &t[5], &t[6], &t[7], &t[8], &t[9], &t[10], &t[11], &t[12], &t[13], &t[14], &t[15], &t[16], &t[17], &t[18]);
    }
    a->q_col = a->q_row = -1;
    a->us.show_palette  = 1;
    a->us.show_tuning   = g_dev.tunewin; /* closed unless asked */
    a->us.show_script   = g_dev.scriptwin;
    a->us.show_log      = 0; /* under Windows > Messages */
    a->us.tool          = -1;
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
        R_NOTE("init", "arcology, o->check against %s", o->check_out ? o->check_out : "the original");
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
          a->gv.geometry ? "geometry" : "o->sprites");

    return 0;
}
