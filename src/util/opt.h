/*  opt.h -- the developer switches, as arguments rather than environment.
 *
 *  Every knob the renderer exposes for looking at its own work used to be
 *  an environment variable, which meant they were invisible to --help,
 *  impossible to typo-check, and leaked between runs in a shell (the
 *  user: "can you please get rid of environment variable usage in the
 *  code?  I just want arguments").
 *
 *      opt_init(argc, argv)   once, before anything reads a switch
 *      opt_set("curve-dump")  is --curve-dump present?
 *      opt_get("tune")        the value of --tune V or --tune=V, or NULL
 *
 *  A switch is named for the variable it replaced, lowercased with its
 *  prefix dropped and underscores turned to dashes, so the old
 *  CURVE_DUMP is now --curve-dump.  Unknown arguments are left
 *  alone here and ignored by the game's own parser, so a switch nothing
 *  reads is silently inert rather than fatal.
 *
 *  What stays in the environment: HOME, because it is the system's to
 *  say, and NO_COLOR / FORCE_COLOR / CLICOLOR / COLORTERM, which are
 *  conventions other tools set and this one honours.
 */
#ifndef ARC_OPT_H
#define ARC_OPT_H

#ifdef __cplusplus
extern "C" {
#endif

void        opt_init(int argc, char **argv);
int         opt_set(const char *name);
const char *opt_get(const char *name);

#ifdef __cplusplus
}
#endif

#endif
