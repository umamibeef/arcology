/*  incr.h: WHAT A BUILD HAS TO REDO.
 *
 *  An edit moves a few tiles.  Rebuilding the whole world for them is
 *  what this exists to avoid.  It diffs the tiles and works out which
 *  chunks the change can reach.  The build then draws those alone.
 *
 *  It sits above mesh/, net/ and walk/ because all three ask it the same
 *  two questions.  Is this tile in a chunk this build draws.  Did an
 *  edit land near here.  The rest of it is the build's own, and only
 *  build.c calls that. */
#ifndef ARC_INCR_H
#define ARC_INCR_H

#include <stddef.h>

#include "pipeline.h"
#include "mesh/mesh.h"

/*  The two every layer asks. */
int  incr_want_tile(int32_t col, int32_t row);
/*  Whether the build under way is an edit's, which keeps the chunks its
 *  closure did not name.  A script keeps what it may answer again from
 *  the last build only in one of these. */
int incr_editing(void);
/*  Whether any chunk under the tiles from (c0,r0) to (c1,r1) builds.  A
 *  piece of work that draws only there is passed over where none does. */
int incr_want_rect(int32_t c0, int32_t r0, int32_t c1, int32_t r1);
/*  The box round the tiles an edit changed: the least column and row,
 *  then the greatest.  Nonzero when no tile changed. */
int incr_dirty_box(int32_t box[4]);
/*  Whether the edit changed this tile. */
int incr_dirty_tile(int32_t col, int32_t row);
int  incr_near(int32_t col, int32_t row);

/*  And the build's own: begin one, close it, and say which chunks the
 *  edit can reach. */
int  incr_begin(RMesh *m, const RCity *c, const void *key, size_t keylen);
int  incr_snapshot(RMesh *m, const RCity *c, const void *key, size_t keylen);
int  incr_end(RMesh *m);
void incr_abort(RMesh *m);
void incr_closure(int lines);
int  incr_dirty(void);
void incr_free(void);

/*  The finished triangles cut into chunks, for the frame's culling. */
int  mesh_partition(RMesh *m);
#endif /* ARC_INCR_H */
