/* ==================================================================== *
 *  Arcology -- the one entry point.
 *
 *  The verification driver, the game, the atlas dumper and the
 *  software rasteriser are one binary.  app/modes.cpp reads the line with CLI11 and says which
 *  of them runs; each then reads its own arguments, so the line is
 *  handed on whole.  Anything that is not a developer mode is the
 *  game's, which is the only program a player ever wants.
 * ==================================================================== */
#include "modes.h"

int arc_dev_main(int argc, char **argv);
int atlas_main(int argc, char **argv);
int soft_main(int argc, char **argv);
int testcity_main(int argc, char **argv);
int lua_lint_main(int argc, char **argv);
int game_main(int argc, char **argv);

int main(int argc, char **argv)
{
    switch (arc_mode(argc, argv))
    {
        case ARC_ATLAS:
            return atlas_main(argc - 1, argv + 1);
        case ARC_SOFT:
            return soft_main(argc - 1, argv + 1);
        case ARC_TESTCITY:
            return testcity_main(argc - 1, argv + 1);
        case ARC_LUALINT:
            return lua_lint_main(argc - 1, argv + 1);
        case ARC_DEV:
            return arc_dev_main(argc, argv);
        case ARC_DONE:
            return 0;
        case ARC_BAD:
            return 2;
        case ARC_GAME:
            break;
    }
    return game_main(argc, argv);
}
