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
typedef struct
{
    const char *check_out, *shot_out, *theme_dir;
    /*  --song S OUT: one song rendered to a WAV, headless.  Set here,
     *  acted on once the assets are found.  These were a pair of file
     *  statics in app.c rather than traveling with the rest. */
    const char *song_arg, *song_out;
    int         check, ww, wh;
    int         run_frames, run_speed;
    int         rotate_turns; /* --rotate N: the city turned N quarters clockwise on load */
    float       zoomf;
    int         sound_test, want_mesh_check;
    int         have_scroll, scroll_x, scroll_y;
    int         have_centre, centre_col, centre_row;
    int         have_pick;
    float       pick_x, pick_y;
    int32_t     pixel_scale;
    int         sprites, want_geometry; /* --sprites, --geometry: bound by CLI11, weighed below */
    int         outline;                /* --outline: the roads stand aside for the curves and the margin network */
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
