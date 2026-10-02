/*  walk/walk.h: what the walk over the map answers for other directories.
 *
 *  The walk itself is walk.c's own business.  This is the one thing it
 *  measures that a store outside needs: a segment's arms, measured
 *  before any trim. */
#ifndef ARC_WALK_H
#define ARC_WALK_H

#include "net/net.h"

int            seg_measure_arms(Seg *x);

/*  One tile's own piece, drawn on its own where no segment reaches it. */
int build_island(RMesh *m, const RCity *c, uint8_t mask_bit, int comp, Family f, int32_t col, int32_t row);

/*  And the drive's own walk over the junctions stage three measures. */
int   walk_trim_junctions(const RCity *c);

void *walk_trim_band(int i);

int   walk_trim_mouths(void);

void  walk_trim_done(int i);

/* ---- walk/walk.c: the walk over the map, a segment's stages, the per-segment context the stages share, the profile */


extern V2      s_wk_pts[], s_wk_q[MAX_PTS];

extern float   s_wk_rad[], s_wk_tlim[MAX_PTS];

extern int32_t s_wk_tcol[], s_wk_trow[MAX_PTS], s_wk_marks[2 * MAX_PTS];

extern Piece   s_wk_pieces[];

/*  The networks in two passes, with the script's own between them: the
 *  measure fits every segment and settles the trims and the meets'
 *  paths.  The draw lays the junctions and the strips.  The level meets
 *  and the power lines fall in the gap, because a meet is built from the
 *  two paths the measure fitted. */
int            build_networks(RMesh *m, const RCity *c, const RAtlas *a, uint8_t mask_bit, int comp);

int            build_draw_done(void);

int            build_networks_controls(const RCity *c);

int            build_networks_trims(const RCity *c);

#endif /* ARC_WALK_H */
