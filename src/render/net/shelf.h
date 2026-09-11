/*  net/shelf.h: what shelf.c answers for.
 *
 *  the corridor's shelf: what a strip asks of the ground, and the graded surface.
 *
 *  Nothing declared here decides anything. */
#ifndef ARC_NET_SHELF_H
#define ARC_NET_SHELF_H

#include "net/types.h"

/*  The corridor field (net/shelf.c): per grid corner, what the strips
 *  passing over it ask of the ground. */
extern float   s_zcap[GRID * GRID];
extern uint8_t s_corr[GRID * GRID];
extern float   s_zdist[GRID * GRID];
/*  The lowest line that passes over each corridor corner.  The shelf is
 *  shaped by the nearest one and smoothed across the field.  This is the
 *  ceiling that smoothing may not break: ground that rises above the
 *  band it carries is ground the line is buried in. */
extern float s_zlow[GRID * GRID];
/*  A corridor tile's OWN four corner heights, flat across the band and
 *  graded along it.  The corridor is not bound by the height field's
 *  rule that neighboring tiles share a corner.  Two families that run
 *  side by side at different heights are two shelves with a wall between
 *  them.  They are not one warped quad. 1e9 where a tile has none. */
extern float s_tilez[R_MAP * R_MAP * 4];
void         s_tile_reset(int32_t i);
void         shelf_node(int32_t col, int32_t row); /* a tile where edges meet: they share one level there (grade.c) */
void         shelf_steps(int *n, float *worst); /* corridor tiles disagreeing at a shared corner (grade.c) */
/*  Reconciling the shelf: the copies of one corner, the copies round a
 *  node's tile, and the level they are all given. */
int   shelf_ask(ShelfFan *s); /* the corridor corners a shelf rule reconciles (grade.c) */
int   shelf_copies(const ShelfFan *s, int gx, int gy, int *owner, float *dist, float *z, int max);
void  shelf_set(ShelfFan *s, int gx, int gy, int owner, float z);
int   shelf_node_at(const ShelfFan *s, int i, int32_t *col, int32_t *row);
int   shelf_node_heights(const ShelfFan *s, int32_t col, int32_t row, float *z, int max);
void  shelf_node_set(ShelfFan *s, int32_t col, int32_t row, float z);
/* ---- net/shelf.c: the corridor grading and the graded surface */
int loft_surface(const RCity *c, uint8_t mask_bit, Sample *smp, int ns, float hw, const RLoft *d);
int corridor_tile(int32_t col, int32_t row);

#endif /* ARC_NET_SHELF_H */
