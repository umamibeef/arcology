/* ==================================================================== *
 *  main.c: the one entry point of Arcology.
 *
 *  The verification driver, the game, the atlas dumper and the
 *  software rasteriser are one binary.  app/modes.cpp reads the line
 *  with CLI11 and hands back WHAT RUNS IT.  Each program then reads its
 *  own arguments, so the line is handed on whole.  Anything that is not
 *  a developer mode is the game's, which is the only program a player
 *  ever wants.
 * ==================================================================== */
#include "modes.h"

int game_main(int argc, char **argv);

int main(int argc, char **argv)
{
    ArcRun run = 0;
    switch (arc_mode(argc, argv, &run))
    {
        case ARC_RUN:
            return run(argc, argv);
        case ARC_DONE:
            return 0;
        case ARC_BAD:
            return 2;
        case ARC_GAME:
            break;
    }
    return game_main(argc, argv);
}
