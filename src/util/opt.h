/*  opt.h: the developer switches, as one struct the code reads.  Every
 *  knob the renderer exposes for looking at its own work is a
 *  command-line switch, parsed by CLI11 in app/options.cpp straight into
 *  g_dev.  So a typo is an error, --help lists them all, and the code
 *  reads a typed field rather than looking a string up.  A switch that
 *  takes no value is an int, 1 when given.  One that takes a value is a
 *  string, NULL when not given.  One whose value is optional is both.
 *  What stays in the environment is HOME, because it is the system's to say.  NO_COLOR, FORCE_COLOR, CLICOLOR and COLORTERM stay too.  These are conventions other tools set and this one honors. */
#ifndef ARC_OPT_H
#define ARC_OPT_H
#ifdef __cplusplus
extern "C" {
#endif
typedef struct
{
    /* looking at the build */
    int         input_log, inspect;
    int         net_dump; /* the road network's topology, end by end and road by road */
    const char *mesh_dump, *tile_dump, *field_dump, *show_at, *build_window;
    const char *work_window;   /* --work-outside N: work this far from an edit's tiles is its own, and listed */
    const char *open_menu;     /* --open-menu NAME: a menu open from the first frame, for a --shot */
    const char *probe;         /* --probe X,Y: every surface over one point of the map */
    int         net_check;     /* --net-check: the road network built from the cells and checked, nothing drawn */
    const char *net_svg;       /* --net-svg FILE: and written as a diagram */
    const char *net_svg_box;   /* --net-svg-box C0,R0,C1,R1: the diagram of those tiles alone */
    const char *shape_profile; /* --shape-profile FILE: street and deck profiles through the shape's solves */
    const char *dump_to;       /* the dumps' sink: a file, or NULL for stdout */
    /* the build, one stage at a time */
    int         no_sort, no_shadow, no_sky, no_incr, gpu_tight, no_lod, no_air, spike_check, check_open, show_edits, async_build, whole_build;
    int         tri_colors;  /* --tri-colors: each triangle of the mesh in a color of its own, from the start */
    const char *save_to;     /* --save-to FILE: the city saved as the Save menu saves it, once the run is done */
    const char *limits_glow; /* --limits-glow N: the clock bounded by the city's limits, N tiles past what is built */
    int         times;
    /* the window and the run */
    const char *tune, *win, *cities, *edit, *gesture, *seed, *area, *record;
    const char *map_tile; /* --map-tile K: the renderer draws the city K x K times over, a map K times as wide */
    int         mute;
    /* the device */
    int         gpu_counts;
    const char *gpu_dump_at;
} DevOpts;
extern DevOpts g_dev;

/*  THE SWITCHES TWO PARSERS TAKE.  The game reads its line with CLI11
 *  (app/options.cpp).  The software rasteriser reads its own
 *  (render/soft/main.c), because a mode's line is its own.  These four
 *  mean the same thing to both, so they are spelled once, here.  Two
 *  parsers with their own copies of a name is how a rename reaches one
 *  and misses the other. */
typedef struct
{
    const char *name;
    int         takes_value; /* 1 for a switch with a value after it */
} ArcSwitch;

enum
{
    ARC_SW_ZOOM,
    ARC_SW_SCALE,
    ARC_SW_UNDERGROUND,
    ARC_SW_NO_THINGS,
    ARC_SW_COUNT
};
extern const ArcSwitch ARC_SWITCHES[ARC_SW_COUNT];
#ifdef __cplusplus
}
#endif
#endif
