/*  walk/internal.h: what one file in this directory lends another.
 *
 *  A name here is the DIRECTORY'S, not the pipeline's.  What the rest of
 *  the renderer may call is walk/walk.h and walk/walkway.h. */
#ifndef ARC_WALK_INTERNAL_H
#define ARC_WALK_INTERNAL_H

#include "net/net.h"

/*  walk.c reads a run's cells into a segment.  cursor.c starts a fit
 *  from the same place the drawing walk does, so it reads them too. */
int seg_from_cells(Seg *x, const NetRun *w);

/*  And one segment walked, which cursor.c asks for once a run. */
int walk_segment(RMesh *m, const RCity *c, const RAtlasLevel *l, uint8_t mask_bit, int comp, Family f, const NetRun *run, uint8_t *visited);

#endif /* ARC_WALK_INTERNAL_H */
