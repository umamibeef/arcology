/*  modes.h: which of the binary's programs a command line asks for. */
#ifndef ARC_MODES_H
#define ARC_MODES_H
#ifdef __cplusplus
extern "C" {
#endif
typedef enum
{
    ARC_GAME,     /* no mode on the line: the game reads it (app/options.cpp) */
    ARC_DEV,      /* a simulation mode: sim/dev.c reads the rest of the line */
    ARC_ATLAS,    /* the atlas dumper */
    ARC_SOFT,     /* the software rasteriser */
    ARC_TESTCITY, /* the test city writer */
    ARC_LUALINT,  /* the script linter */
    ARC_DONE,     /* --modes or --version: printed, exit 0 */
    ARC_BAD       /* a line that made no sense: said, exit 2 */
} ArcMode;

ArcMode arc_mode(int argc, char **argv);
#ifdef __cplusplus
}
#endif
#endif
