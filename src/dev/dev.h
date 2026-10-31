/*  dev.h: the developer modes, as one list.
 *
 *  `arcology --verify`, `--clock`, `--micro` and the rest are tooling:
 *  each drives the simulation and prints what it did.  None of them is a
 *  routine the original has.
 *
 *  THE LIST IS ONE LIST.  A mode is a row here and a function in dev.c.
 *  The parser (app/modes.cpp) and the dispatcher read the same rows.
 *  A row and a branch of a chain somewhere else is how a mode gets a
 *  spelling in one place and a meaning in another. */
#ifndef ARC_DEV_H
#define ARC_DEV_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    const char *name;                  /* the spelling, which tools/ pass and cannot change */
    const char *args;                  /* the sketch of its line, for --modes              */
    const char *help;                  /* one line, and the mode's own code is the reference */
    int         min_argc;              /* how many words its line needs, this one included */
    int (*run)(int argc, char **argv); /* the whole line: argv[1] is the name */
} ArcDevMode;

extern const ArcDevMode ARC_DEV_MODES[];
extern const int        ARC_DEV_MODE_COUNT;

#ifdef __cplusplus
}
#endif
#endif /* ARC_DEV_H */
