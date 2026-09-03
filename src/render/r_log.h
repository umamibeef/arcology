/*  r_log.h -- console messages that say where they came from.
 *
 *  Every line is `source  message`, the source coloured so a glance
 *  tells you which part is talking.  spdlog does the work; this is a
 *  small C face over it so the C sources stay plain C.
 *
 *  Streams.  Diagnostics go to stderr.  This program's stdout carries
 *  measured output -- the --run summary, --check counts, --pick
 *  coordinates -- which people pipe elsewhere, so a log line on stdout
 *  would corrupt data rather than merely annoy.
 *
 *  Colour is decided once, in this order:
 *
 *      NO_COLOR present and non-empty  -> no     (no-color.org)
 *      CLICOLOR=0                      -> no
 *      FORCE_COLOR / CLICOLOR_FORCE    -> yes, unless set to 0
 *      otherwise                       -> spdlog decides, which covers
 *                                         isatty, TERM and the Windows
 *                                         console
 *
 *  A refusal beats a request, a request beats a guess.
 *
 *  Levels come from SPDLOG_LEVEL, which spdlog reads itself:
 *      SPDLOG_LEVEL=debug              everything
 *      SPDLOG_LEVEL=warn               warnings and errors only
 *      SPDLOG_LEVEL=gpu=debug,atlas=off   per source
 */
#ifndef R_LOG_H
#define R_LOG_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    R_LOG_ERROR = 0,
    R_LOG_WARN,
    R_LOG_NOTE, /* the default ceiling: worth saying once      */
    R_LOG_DEBUG /* --verbose only: driver names, paths, counts */
} RLogLevel;

/*  Safe to call more than once, and called for you on the first
 *  message if you forget. */
void r_log_init(void);

/*  Nothing quieter than this is printed.  R_LOG_NOTE by default. */
void r_log_set_level(RLogLevel max);

/*  Force colour on or off, whatever the environment said. */
void r_log_set_colour(int on);

/*  Whether colour ended up on, for code that draws its own output. */
int r_log_colour(void);

void r_log(RLogLevel lvl, const char *source, const char *fmt, ...);

#define R_ERR(src, ...)  r_log(R_LOG_ERROR, (src), __VA_ARGS__)
#define R_WARN(src, ...) r_log(R_LOG_WARN, (src), __VA_ARGS__)
#define R_NOTE(src, ...) r_log(R_LOG_NOTE, (src), __VA_ARGS__)
#define R_DBG(src, ...)  r_log(R_LOG_DEBUG, (src), __VA_ARGS__)

/*  The banner: `n` lines of ASCII art to the log's stream, each
 *  character coloured by a gradient across the banner's width, under
 *  the same colour rules as the log, so NO_COLOR gets the plain text. */
void r_log_banner(const char *const *lines, int n);

/*  Text verbatim to the log's stream -- no stamp, no level, no colour.
 *  The lines under the banner.  Kept here rather than as an fputs in
 *  the caller so the choice of stream stays in one place. */
void r_log_raw(const char *text);

#ifdef __cplusplus
}
#endif

#endif /* R_LOG_H */
