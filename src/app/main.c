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
#include "opt.h"

#include <stdio.h>
#include <string.h>

int sc2k_dev_main(int argc, char **argv);
int atlas_main(int argc, char **argv);
int soft_main(int argc, char **argv);
int testcity_main(int argc, char **argv);
int game_main(int argc, char **argv);

/*  Every mode sc2k/main.c answers to.  Keep this in step with the
 *  `strcmp(argv[1], "--...")` ladder there -- a mode missing from this
 *  list silently launches the game instead, which looks like the
 *  checker hanging.  The reverse is just as bad and was true here: four
 *  names were listed that main.c has no handler for, so each fell through
 *  to "not a city" instead of saying it does not exist. */
static const char *const ARC_DEV_MODES[] = {
    "--verify", "--convert", "--micro", "--allocmicro", "--riot", "--demolish1", "--averages", "--advisor", "--clock", "--graph", "--things", "--demolish", "--terrain", "--raise", "--settile", "--footprint", "--scenario", "--dump-growth-all", "--dump-growth", "--trace-growth", "--growth", "--economy", "--budget", "--dump", "--cycles", "--rng", NULL};

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
        "  arcology --testcity <template> <out.sc2>\n"
        "                              a city of every network case\n"
        "\n"
        "Run any developer mode with no arguments for its own usage.\n"
        "\n"
        "developer switches (were environment variables; now arguments):\n"
        "  --tune W_ROAD,W_RAIL,RMIN_ROAD,RMIN_RAIL,RMAX_ROAD,RMAX_RAIL,\n"
        "         APPROACH,MARGIN,TRIM,CURVES,WIDEFIT,SPLINE,TENSION\n"
        "                              the road knobs, as the tuning window sets them\n"
        "  --win WxH                   render at a larger framebuffer\n"
        "  --coords                    the tile ruler down the map's edges\n"
        "  --no-tunewin                leave the tuning window closed\n"
        "  --curve-dump                every corner: where, how far it turns, its radius\n"
        "  --junc-dump                 every junction: its arms, their trims\n"
        "  --road-dump [C,R]           the fitted points of every segment, or of one tile's\n"
        "  --path-dump  --plan-dump    the fit stage by stage, for tools/plan.py\n"
        "  --space-dump --spline-dump  why a node stayed, why a spline fell back\n"
        "  --prof-dump  --loft-dump    the finished profile, the loft's stations\n"
        "  --mesh-dump R,C  --tile-dump  --clip-dump  --field-dump FILE\n"
        "  --no-spacing --old-clear --noscale  the fit, one stage at a time\n"
        "  --no-sort --no-shadow --no-cap      the draw, one stage at a time\n"
        "  --assets DIR --cities DIR   where the art and the saves are\n"
        "  --traffic-t SECONDS         advance the traffic before a headless frame\n"
        "  --gpu-dump [N] --gpu-debug  the device's own log, and its validation layer\n"
        "  --hiway-dump --xing-debug --box-debug --spike-check --check-open\n");
}

int main(int argc, char **argv)
{
    int i;

    /*  Before anything reads a switch: every developer option is an
     *  argument now, and they are read straight from argv wherever they
     *  are needed rather than threaded through. */
    opt_init(argc, argv);

    if (argc >= 2)
    {
        if (!strcmp(argv[1], "--atlas"))
            return atlas_main(argc - 1, argv + 1);
        if (!strcmp(argv[1], "--soft"))
            return soft_main(argc - 1, argv + 1);
        if (!strcmp(argv[1], "--testcity"))
            return testcity_main(argc - 1, argv + 1);
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
    return game_main(argc, argv);
}
