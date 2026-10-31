/*  options.h: the command line, once it has been read.
 *
 *  parse_options fills a Startup in and game_main works from that, so
 *  the argument handling and the startup can be read separately. */
#ifndef ARC_OPTIONS_H
#define ARC_OPTIONS_H

#include "internal.h"

/*  Everything the command line and the environment decide, in one place.
 *  It was two hundred lines at the top of game_main, sharing a scope
 *  with the five hundred that follow.  So a name set here could be read,
 *  or quietly reused, anywhere below. */
typedef struct Startup
{
    const char *check_out, *shot_out, *theme_dir;
    /*  --song S OUT: one song rendered to a WAV, headless.  Set here,
     *  acted on once the assets are found.  These were a pair of file
     *  statics in app.c rather than traveling with the rest. */
    const char *song_arg, *song_out;
    int         check, ww, wh;
    int         run_frames, run_speed;
    float       zoomf;
    float       hour;  /* --hour H: the time of day for this run, or -1 */
    const char *trees; /* --trees BIOME: the trees' biome for this run, or NULL */
    int         sound_test, want_mesh_check;
    int         have_scroll, scroll_x, scroll_y;
    int         have_centre, centre_col, centre_row;
    int         have_pick;
    float       pick_x, pick_y;
    int32_t     pixel_scale;
    int         outline; /* --outline: the road network's lane graph over the bare ground */
    char        assets_dir[1024], city_path[1024];
} Startup;

/*  Returns 0 to carry on.  It returns -1 when the arguments asked for
 *  something that is over already and well (--help).  Any other value is
 *  an exit code. */
#ifdef __cplusplus
extern "C"
#endif
    int parse_options(Startup *o, App *a, int argc, char **argv);

#endif /* ARC_OPTIONS_H */
