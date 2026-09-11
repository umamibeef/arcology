/*  walk/walk.h: what the walk over the map answers for other directories.
 *
 *  The walk itself is walk.c's own business.  This is the one thing it
 *  measures that a store outside needs: a segment's arms, measured
 *  before any trim. */
#ifndef ARC_WALK_H
#define ARC_WALK_H

#include "net/net.h"

int            seg_measure_arms(Seg *x);

#endif /* ARC_WALK_H */
