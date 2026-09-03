/* ==================================================================== *
 *  Arcology -- the one entry point.
 *
 *  There used to be four executables: the verification driver, the
 *  game, the atlas dumper and the software rasteriser.  They are one
 *  binary now, and which of them runs is decided here.
 *
 *  The rule is deliberately not a subcommand grammar.  The developer
 *  modes keep the exact spelling they have always had -- `--verify`,
 *  `--clock`, `--growth` and the rest -- because two dozen checkers in
 *  tools/ pass them, and a mode name in argv[1] is unambiguous: the
 *  game's own options and the developer modes share no spelling.  So
 *
 *      arcology                     the game, with the load menu
 *      arcology Bayview --run 2     the game, that city
 *      arcology --verify <dir>      the verification report
 *      arcology --atlas <dir>       the atlas dumper
 *      arcology --soft <city>       the software rasteriser
 *
 *  all mean what they look like.  Anything not in the table below goes
 *  to the game, which is the only mode a player ever wants.
 * ==================================================================== */
#include "arc_version.h"

#include <stdio.h>
#include <string.h>

int sc2k_dev_main(int argc, char **argv);
int r_atlas_main(int argc, char **argv);
int r_soft_main(int argc, char **argv);
int r_game_main(int argc, char **argv);

/*  Every mode sc2k/main.c answers to.  Keep this in step with the
 *  `strcmp(argv[1], "--...")` ladder there -- a mode missing from this
 *  list silently launches the game instead, which looks like the
 *  checker hanging. */
static const char *const ARC_DEV_MODES[] = {
    "--verify", "--bits", "--water", "--roundtrip", "--convert", "--micro", "--allocmicro", "--findmisc", "--riot", "--demolish1", "--averages", "--advisor", "--clock", "--graph", "--things", "--demolish", "--terrain", "--raise", "--settile", "--footprint", "--scenario", "--dump-growth-all", "--dump-growth", "--trace-growth", "--growth", "--economy", "--budget", "--dump", "--cycles", "--rng", NULL};

static void arc_usage(void)
{
    printf(
        "arcology -- a reconstruction of the SimCity 2000 simulation\n"
        "\n"
        "  arcology                    open the load menu\n"
        "  arcology <city>             open a city\n"
        "  arcology --help             the game's options\n"
        "  arcology --version          the version, one line\n"
        "\n"
        "developer modes:\n"
        "  arcology --verify <dir>...  the verification report\n"
        "  arcology --convert <in> <out.arco>\n"
        "                              a 1995 save becomes a world\n"
        "  arcology --clock <city> <ticks> <out>\n"
        "                              run the clock and dump it\n"
        "  arcology --atlas <dir>      dump the sprite atlas\n"
        "  arcology --soft <city>      the software rasteriser\n"
        "\n"
        "Run any developer mode with no arguments for its own usage.\n");
}

int main(int argc, char **argv)
{
    int i;

    if (argc >= 2)
    {
        if (!strcmp(argv[1], "--atlas"))
            return r_atlas_main(argc - 1, argv + 1);
        if (!strcmp(argv[1], "--soft"))
            return r_soft_main(argc - 1, argv + 1);
        if (!strcmp(argv[1], "--modes"))
        {
            arc_usage();
            return 0;
        }
        if (!strcmp(argv[1], "--version"))
        {
            printf("arcology %s\n", ARC_VERSION_FULL);
            return 0;
        }
        for (i = 0; ARC_DEV_MODES[i]; i++)
            if (!strcmp(argv[1], ARC_DEV_MODES[i]))
                return sc2k_dev_main(argc, argv);
    }
    return r_game_main(argc, argv);
}
