/*  geo.h: the numbers a script tunes the world with.
 *
 *  Two stores, and neither is any one directory's.  The KNOBS are a
 *  fixed struct the tuning window writes and the macros read.  The
 *  NUMBERS are free: a name exists from the moment a script sets it, so
 *  there is nothing to declare before one can be invented.
 *
 *  Both are read by all three halves of the renderer, which is why they
 *  sit here rather than with any of them.  A primitive wants the step it
 *  samples at.  A store wants a width.  A stage wants a reserve.  All
 *  three ask this store, and they ask it the same way. */
#ifndef ARC_GEO_H
#define ARC_GEO_H

/*  The knobs the look is tuned with, live.  They were constants.  The
 *  judgment they encode is aesthetic and belongs to the person looking
 *  at the city, not to a number I picked.  The macros still stand so
 *  every use site reads the current value, and the UI's tuning window
 *  writes them and rebuilds the mesh. */
typedef struct
{
    float line_w;    /* the way, across, in tiles                 */
    float thread_w;    /* a double thread's right of way                     */
    float line_rmin; /* the tightest curve each may be drawn with         */
    float thread_rmin;
    float line_rmax; /* and the widest sweep to look for                  */
    float thread_rmax;
    float approach;    /* straight run reserved at every node               */
    float margin;      /* how far inside its corridor the band is held      */
    float trim_cap;    /* how far out a junction may cut its arms back      */
    float show_curves; /* draw the fitted centerline over the world       */
    /*  The band's, live too.  The slab's half width across, in tiles,
     *  and what sits on it follows.  The radii its corners may take.
     *  How far an on-spur reaches for the slab.  How many straight cells
     *  may lie between two curve blocks for them to be one staircase.
     *  And the share of an edge a corner may take for its fillet.  The
     *  half rule holds it at 0.5.  It is the same for every family,
     *  because the fit is one. */
    float band_w;
    float band_rmin, band_rmax;
    float band_reach;
    float band_stair;
    float corner_share;
    float spur_merge; /* how far along the line an on-spur's join may slide from the point it aimed at, tiles */
    float band_grade; /* the slab's steepest rise or fall, levels per tile */
    float band_stiff; /* the slab's stiffness: the window, tiles, over which it holds level across dips and rounds its crests */
} RTune;

extern RTune s_tune;

/*  A number by name, remembering where it was found.  The cache is the
 *  reader's own and starts at -1.  A name nothing has set reads as
 *  nought. */
float        geo_num(int *cache, const char *name);
int          geo_set(const char *name, float v);
const char  *geo_name(int i, float *v);

/*  The knobs, the same way.  A family holds pointers at its width and
 *  its radii, so every use reads the live value. */
int          tune_set(const char *name, float v);
const float *tune_at(const char *name);
const char  *tune_name(int i, float *v);
/*  And the knobs as the tuning window holds them: one array of floats. */
float       *tune_array(void);

#endif /* ARC_GEO_H */
