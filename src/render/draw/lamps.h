/*  draw/lamps.h: the street lamps, laid from the road network's objects
 *  (plans/network.md, "the street lamps"), and the light they throw on
 *  the ground at night. */
#ifndef R_DRAW_LAMPS_H
#define R_DRAW_LAMPS_H

#include <stdint.h>

#include "city.h"
#include "mesh/mesh.h"

/*  Every lamp of every road object that stands on a tile the build
 *  wants.  It also lights again those tiles of the lamps' light field.
 *  Answers 0, or -1 on a mesh error. */
int lamps_draw(RMesh *m, const RCity *c, uint8_t mask_bit);

/*  The lamps' light on the ground: LAMP_TEXELS texels a tile a side,
 *  r_map times that a side in all, two bytes each.  The first is the
 *  light, 255 the most.  The second is the height of the road the light
 *  falls on: its levels, times LAMP_LEVEL_STEP (four).  A deck's light
 *  therefore does not fall on the ground under it. */
#define LAMP_LEVEL_STEP 4.0f
#define LAMP_TEXELS     2
const uint8_t *lamps_light(void);

#endif
