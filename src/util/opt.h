/*  opt.h -- the developer switches, as one struct the code reads.  Every
 *  knob the renderer exposes for looking at its own work is a command-line
 *  switch, parsed by CLI11 in app/options.cpp straight into g_dev, so a
 *  typo is an error, --help lists them all, and the code reads a typed
 *  field rather than looking a string up.  A switch that takes no value is
 *  an int, 1 when given; one that takes a value is a string, NULL when not
 *  given; one whose value is optional is both.  What stays in the
 *  environment: HOME, because it is the system's to say, and NO_COLOR /
 *  FORCE_COLOR / CLICOLOR / COLORTERM, which are conventions other tools
 *  set and this one honours. */
#ifndef ARC_OPT_H
#define ARC_OPT_H
#ifdef __cplusplus
extern "C" {
#endif
typedef struct
{
    /* looking at the road work */
    int         curve_dump, junc_dump, path_dump, plan_dump, prof_dump, loft_dump, hiway_dump, clip_dump, input_log, inspect;
    int         road_dump, lane_dump;        /* given at all */
    int         sidewalk_dump;                   /* the sidewalk ends that meet nothing */
    int         buried_all;                      /* every footway with its own piece under it, not the first few */
    const char *road_dump_at, *lane_dump_at; /* ... and at a tile, C,R, or NULL */
    const char *mesh_dump, *tile_dump, *field_dump, *sweep_probe;
    const char *probe; /* --probe X,Y: every surface over one point of the map */
    const char *dump_to; /* the dumps' sink: a file, or NULL for stdout */
    const char *lua;      /* --lua FILE: the script, watched and read again when it changes */
    const char *lua_eval; /* --lua-eval SRC: one chunk, run once the world is up */
    /* the build, one stage at a time */
    int noscale, no_sort, no_shadow, no_cap, no_replay, no_incr, no_smp_cache, grade_loft, grade_all, grade_junc, grade_lanes, gpu_tight, spike_check, check_open;
    int times;
    /* the window and the run */
    const char *tune, *win, *cities, *traffic_t, *edit, *seed, *area, *turn;
    int         tunewin, scriptwin, mute;
    /* the device */
    int         gpu_debug, gpu_dump;
    const char *gpu_dump_at;
    /* verbose corners */
    int xing_debug, box_debug;
} DevOpts;
extern DevOpts g_dev;
#ifdef __cplusplus
}
#endif
#endif
