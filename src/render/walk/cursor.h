/*  walk/cursor.h: the order the drive steps a build's segments in, the
 *  fit arena a segment's choices are held in, and the drawing pass. */
#ifndef ARC_WALK_CURSOR_H
#define ARC_WALK_CURSOR_H

#include "pipeline.h"
#include "mesh/mesh.h"


/*  The band one arm gave up line for, 0 for none (walk.c s_xwalk). */

int            build_networks_draw(RMesh *m, const RCity *c, uint8_t mask_bit, int comp);

int            build_draw_families(void);

void           build_draw_boxes_begin(RMesh *m, const RCity *c, uint8_t mask_bit, int fk);

int            build_draw_box_next(void);

void           build_draw_begin(RMesh *m, const RCity *c, uint8_t mask_bit, int comp, int fk);

int            build_draw_next(void);

int            build_networks_drawn(void);

/*  Every junction the walk visits, so the script can be asked for each
 *  one's ring between the fit and the trims. */
int            build_junction_count(const RCity *c);

int            build_junction_nth(const RCity *c, int i, Family *f, int32_t *col, int32_t *row, int *links);


int    build_networks_classes(RMesh *m, const RCity *c, uint8_t mask_bit, int comp);








/*  One segment's reading, for the rule that says what it carries: how
 *  many of its tiles wear each class, and the tiles themselves.  So a
 *  rule may go and read the density and the neighborhood at them. */


#endif /* ARC_WALK_CURSOR_H */
