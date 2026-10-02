/*  modes.cpp: the developer modes, and which program a line asks for.
 *  The verification driver, the game, the atlas dumper and the software
 *  rasteriser are one binary, and CLI11 decides which of them runs.
 *  Every developer mode is a flag here, with the spelling it has always
 *  had.  Two dozen checkers in tools/ pass them, so the spellings cannot
 *  change.  The mode must lead the line and the rest of the line is its
 *  own, read by its own main.  So this parser stops at the first thing
 *  it does not know and hands the whole line on.  A line with no mode is
 *  the game's, and app/options.cpp reads that.  The descriptions are one
 *  line each.  The mode's own code in sim/dev.c is the reference for
 *  what it prints. */
#include "modes.h"

#include "dev.h"

#include <cstdio>
#include <cstring>
#include <memory>
#include <string>

#include "CLI11.hpp"
#include "arc_version.h"
#include "log.h"

namespace
{
/*  ONE ROW A MODE: its spelling, the sketch of its line, one line of
 *  help, and what runs it.  The dev modes' rows come from src/dev's own
 *  list, which is where their functions are.  These are the other
 *  programs in the binary. */
struct Mode
{
    const char *name, *args, *help;
    ArcRun      run;
};
/*  The four that are programs of their own.  Each reads the line from
 *  its own first argument, so the mode's word is stepped over.  They are
 *  C, and this file is the one C++ in the dispatch. */
extern "C"
{
    int atlas_main(int argc, char **argv);
    int soft_main(int argc, char **argv);
    int testcity_main(int argc, char **argv);
    int lua_lint_main(int argc, char **argv);
}
int run_atlas(int argc, char **argv) { return atlas_main(argc - 1, argv + 1); }
int run_soft(int argc, char **argv) { return soft_main(argc - 1, argv + 1); }
int run_testcity(int argc, char **argv) { return testcity_main(argc - 1, argv + 1); }
int run_lua_lint(int argc, char **argv) { return lua_lint_main(argc - 1, argv + 1); }

const Mode PROGRAMS[] = {
    {"--atlas", "<assets> [shape [out.png]]",
     "dump the sprite atlas", run_atlas},
    {"--soft", "<assets> <city> [out.png]",
     "the software rasteriser", run_soft},
    {"--testcity", "<template> <out.sc2>",
     "a city of every network case", run_testcity},
    {"--lua-lint", "<script.lua>...",
     "read the scripts and say what is wrong with them", run_lua_lint},
};
/*  What the parser settled on: the spelling it matched, how short a line
 *  it refuses, and what runs it.  Thrown, because CLI11 tells a callback
 *  nothing else.
 *
 *  A mode reads the rest of the line by position, so a line too short
 *  for it would read past the end.  The programs of their own read
 *  their own usage and set no minimum here. */
struct Chosen
{
    const char *name;
    int         min_argc;
    ArcRun      run;
};
} // namespace

extern "C" ArcMode arc_mode(int argc, char **argv, ArcRun *run)
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
    /*  Every mode, from the two lists that hold them: the programs above
     *  and src/dev's own.  Neither is repeated here, so a mode cannot be
     *  spelled one way in the parser and another where it runs. */
    for (const Mode &m : PROGRAMS)
        app.add_flag_callback(
               m.name, [&m] { throw Chosen{m.name, 0, m.run}; }, m.help)
            ->option_text(m.args)
            ->trigger_on_parse()
            ->group("Developer modes");
    for (int i = 0; i < ARC_DEV_MODE_COUNT; ++i)
    {
        const ArcDevMode &d = ARC_DEV_MODES[i];
        app.add_flag_callback(
               d.name, [&d] { throw Chosen{d.name, d.min_argc, d.run}; }, d.help)
            ->option_text(d.args)
            ->trigger_on_parse()
            ->group("Developer modes");
    }
    try
    {
        app.parse(argc, argv);
    }
    catch (const Chosen &c)
    {
        if (argc < 2 || strcmp(argv[1], c.name) != 0)
        {
            R_ERR("app", "arcology: %s is a developer mode and must come first", c.name);
            return ARC_BAD;
        }
        if (argc < c.min_argc)
        {
            R_ERR("app", "arcology: %s wants %d words on the line, and this has %d",
                  c.name, c.min_argc, argc);
            return ARC_BAD;
        }
        *run = c.run;
        return ARC_RUN;
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
