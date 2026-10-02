/*  devopts.c: the developer switches' one definition.  See opt.h. */
#include "opt.h"

DevOpts g_dev;

/*  The four a mode and the game both take.  opt.h says why they are
 *  here rather than typed into each parser. */
const ArcSwitch ARC_SWITCHES[ARC_SW_COUNT] = {
    {"--zoom", 1},
    {"--scale", 1},
    {"--underground", 0},
    {"--no-things", 0},
};
