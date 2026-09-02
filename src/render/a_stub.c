/*  Stands in for the game when the build found no SDL3.  The developer
 *  modes are what ctest and the checkers in tools/ drive, and none of
 *  them needs a window, so the binary is still worth building. */
#include <stdio.h>

int r_game_main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    fprintf(stderr,
            "arcology was built without SDL3, so the game is not in this "
            "binary.\n"
            "Install SDL3 and reconfigure, or use -DSC2K_FETCH_SDL3=ON.\n"
            "The developer modes still work: try `arcology --modes`.\n");
    return 2;
}
