/*  modes.h: which of the binary's programs a command line asks for. */
#ifndef ARC_MODES_H
#define ARC_MODES_H
#ifdef __cplusplus
extern "C" {
#endif
typedef enum
{
    ARC_GAME, /* no mode on the line: the game reads it (app/options.cpp) */
    ARC_RUN,  /* a mode was named, and `run` is what it runs */
    ARC_DONE, /* --modes or --version: printed, exit 0 */
    ARC_BAD   /* a line that made no sense: said, exit 2 */
} ArcMode;

/*  What a mode runs: the WHOLE line, with argv[1] its own name.  Every
 *  mode reads the rest of it itself. */
typedef int (*ArcRun)(int argc, char **argv);

/*  Which program the line asks for.  ARC_RUN puts what runs it in
 *  `run`.  There is then no second place that maps a mode to its
 *  program: the table that knows the spelling knows the function. */
ArcMode arc_mode(int argc, char **argv, ArcRun *run);
#ifdef __cplusplus
}
#endif
#endif
