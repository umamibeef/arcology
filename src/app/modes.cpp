/*  modes.cpp -- the developer modes, and which program a line asks for.
 *  The verification driver, the game, the atlas dumper and the software
 *  rasteriser are one binary, and CLI11 decides which of them runs.  Every developer mode is a flag
 *  here, with the spelling it has always had (--verify, --clock and the
 *  rest) because two dozen checkers in tools/ pass them.  The mode must
 *  lead the line and the rest of the line is its own, read by its own main
 *  -- so this parser stops at the first thing it does not know and hands
 *  the whole line on.  A line with no mode is the game's, and
 *  app/options.cpp reads that.  The descriptions are one line each; the
 *  mode's own code in sim/dev.c is the reference for what it prints. */
#include "modes.h"

#include <cstdio>
#include <cstring>
#include <memory>
#include <string>

#include "CLI11.hpp"
#include "arc_version.h"

namespace
{
struct Mode
{
    const char *name, *args, *help;
    ArcMode     kind;
};
const Mode MODES[] = {
    {"--verify",          "<dir>...",                          "the verification report over every save in the directories",   ARC_DEV     },
    {"--convert",         "<in> <out>",                        "a 1995 save becomes a world (.arco), or back again",           ARC_DEV     },
    {"--clock",           "<city> <ticks> <out>",              "run the clock and dump it",                                    ARC_DEV     },
    {"--atlas",           "<assets> [shape [out.png]]",        "dump the sprite atlas",                                        ARC_ATLAS   },
    {"--soft",            "<assets> <city> [out.png]",         "the software rasteriser",                                      ARC_SOFT    },
    {"--testcity",        "<template> <out.sc2>",              "a city of every network case",                                 ARC_TESTCITY},
    {"--lua-lint",        "<script.lua>...",                   "read the scripts and say what is wrong with them",             ARC_LUALINT },
    {"--micro",           "<city> <out>",                      "the year-end microsim pass, and what it wrote",                ARC_DEV     },
    {"--allocmicro",      "<city>",                            "place one special building of every id and report its record", ARC_DEV     },
    {"--riot",            "<city> <h> <v> [kind] [rng]",       "a disaster at (h,v), a riot unless told otherwise",            ARC_DEV     },
    {"--demolish1",       "<city> <y> <x>",                    "demolish one tile",                                            ARC_DEV     },
    {"--demolish",        "<city>",                            "demolition, city-wide",                                        ARC_DEV     },
    {"--averages",        "<city>",                            "the city scan's averages",                                     ARC_DEV     },
    {"--advisor",         "<city> [ticks] [plain]",            "the advisors' text after a run",                               ARC_DEV     },
    {"--graph",           "<city> <month> <years>",            "the graphs' history",                                          ARC_DEV     },
    {"--things",          "<city> [passes]",                   "the things pass: what stands on the tiles",                    ARC_DEV     },
    {"--terrain",         "<city> [nb]",                       "the terrain pass",                                             ARC_DEV     },
    {"--raise",           "<city>",                            "raise terrain",                                                ARC_DEV     },
    {"--settile",         "<city>",                            "set a tile",                                                   ARC_DEV     },
    {"--footprint",       "<city>",                            "the buildings' footprints",                                    ARC_DEV     },
    {"--scenario",        "<city> [zero|hard]",                "the scenario's conditions",                                    ARC_DEV     },
    {"--growth",          "<city>",                            "one growth pass",                                              ARC_DEV     },
    {"--dump-growth",     "<city> <phase> <n> <out>",          "one growth scan, dumped",                                      ARC_DEV     },
    {"--trace-growth",    "<city> <phase> <n>",                "one growth scan, traced",                                      ARC_DEV     },
    {"--dump-growth-all", "<city> <out> [--rng | --cycles N]", "every growth phase, dumped",                                   ARC_DEV     },
    {"--economy",         "<city>",                            "the economy model",                                            ARC_DEV     },
    {"--budget",          "<city>",                            "the budget",                                                   ARC_DEV     },
    {"--dump",            "<city> <out> [--pre=PHASES]",       "the city scan, dumped",                                        ARC_DEV     },
};
struct Chosen
{
    const Mode *mode;
};
} // namespace

extern "C" ArcMode arc_mode(int argc, char **argv)
{
    CLI::App app{"arcology -- a reconstruction of the SimCity 2000 simulation.\n"
                 "Bare, it opens the load menu; with a city, that city; --help lists the "
                 "game's options and the developer switches, --version the version.  A "
                 "developer mode leads the line and the rest of the line is its own; run "
                 "one with no arguments for its usage."};
    app.name("arcology");
    app.set_help_flag("--modes", "the developer modes");
    app.set_version_flag("--version", std::string("arcology ") + ARC_VERSION_FULL);
    app.get_formatter()->column_width(44);
    app.allow_extras();
    app.prefix_command();
    for (const Mode &m : MODES)
        app.add_flag_callback(
               m.name, [&m] { throw Chosen{&m}; }, m.help)
            ->option_text(m.args)
            ->trigger_on_parse()
            ->group("Developer modes");
    try
    {
        app.parse(argc, argv);
    }
    catch (const Chosen &c)
    {
        if (argc < 2 || strcmp(argv[1], c.mode->name) != 0)
        {
            fprintf(stderr, "arcology: %s is a developer mode and must come first\n", c.mode->name);
            return ARC_BAD;
        }
        return c.mode->kind;
    }
    catch (const CLI::CallForHelp &e)
    {
        app.exit(e);
        return ARC_DONE;
    }
    catch (const CLI::CallForVersion &e)
    {
        app.exit(e);
        return ARC_DONE;
    }
    catch (const CLI::ParseError &e)
    {
        app.exit(e);
        return ARC_BAD;
    }
    return ARC_GAME;
}
