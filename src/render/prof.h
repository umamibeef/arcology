/*  prof.h: where a build's time goes, stage by stage (--times).
 *
 *  Every stage of a pass adds the wall time it took under its own name.
 *  The build prints the table once its networks are all built.  Nothing
 *  here decides anything. */
#ifndef ARC_PROF_H
#define ARC_PROF_H

/*  The stages, one slot each: every stage adds to its own, and
 *  prof_print reads them all. */
enum
{
    PROF_WALK_FIT,
    PROF_TRIMS,
    PROF_LANES,
    PROF_OVERLAY,
    PROF_CAPS,
    PROF_SAMPLE,
    PROF_PROFILE,
    PROF_SLAB_WORKS,
    PROF_RECORD,
    PROF_SLAB,
    PROF_SLAB_SLAB,
    PROF_JUNC_LANES,
    PROF_JUNC_BOX,
    PROF_STATIONS,
    PROF_CACHED,
    PROF_SAMPLED,
    PROF_GROUND,
    PROF_BAND_AIR, /* build_bands (band.c): the free-air scan, the bands, the tints, the spurs, the transitions, the lane check */
    PROF_BANDS,
    PROF_BAND_TINT,
    PROF_BAND_SPURS,
    PROF_BAND_TRANS,
    PROF_BAND_CHECK,
    PROF_SPUR_LOFT, /* of the spurs: their lofts (the rest is finding, posing and routing them) */
    PROF_N
};

/*  The clock, in milliseconds. */
double prof_now(void);
void   prof_add(int stage, double amount);
void   prof_reset(void);
void   prof_print(void);

#endif /* ARC_PROF_H */
