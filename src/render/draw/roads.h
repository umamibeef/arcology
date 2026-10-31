/*  draw/roads.h: the streets drawn from the road network's records.
 *
 *  The road network (src/network) is worked out for the city a build draws.
 *  A street that runs on every tile of its road is drawn from its
 *  records.  Its lanes and sidewalks run along its line at its own height.
 *  Each of its junctions is filled over its boundary.  The ground near it is
 *  graded to meet it (network/bank.h).  Any other way keeps the original's
 *  art. */
#ifndef ARC_ROADS_H
#define ARC_ROADS_H

#include <stdint.h>

#include "city.h"
#include "mesh/mesh.h"

/*  Which of a city's streets are drawn, from the road network the build
 *  derived (build_network).  Where the network could not be shaped,
 *  `shaped` is 0 and nothing is drawn.  0 on success. */
int roads_build(const RCity *c, int shaped);

/*  One byte a tile of the map, 1 where a drawn street stands, or a drawn
 *  highway's deck (bands.h).  The art of that tile is not drawn, and its
 *  ground is the land's.  NULL before
 *  roads_build has worked a city out. */
const uint8_t *roads_drawn(void);

/*  The tiles the streets are drawn on, for the highways' decks to mark
 *  theirs.  It gives one byte a tile, the cover beside it, and the map's
 *  side.  -1 before roads_build has worked a city out. */
int roads_tiles(uint8_t **drawn, const uint8_t **cover, int *side);

/*  One byte a tile of the map, 1 where a drawn surface lies over the tile,
 *  so its ground must lie under that surface.  NULL before roads_build. */
const uint8_t *roads_cover(void);

/*  Whether any point of a tile lies near enough to a drawn street that
 *  the ground there meets it. */
int roads_near(int32_t col, int32_t row);

/*  The ground at a point, given the height `ground` it has where no street
 *  is.  It is graded to meet the street nearest it, and it lies a hair under
 *  a street or a junction. */
float roads_ground(float x, float y, float ground);

/*  Hold every drawn junction to its geometry.  Its boundary and its
 *  sidewalk ring are simple, and the ring lies inside the boundary.
 *  Each sidewalk turns as the boundary does, and each ring's triangles
 *  cover it once.  Says why each junction of streets is not drawn.  No
 *  mesh is built.  Answers how many faults, or -1 before roads_build. */
int roads_check(int verbose);

/*  Lay every drawn street's surface into the mesh. */
void roads_draw(RMesh *m, const RCity *c, uint8_t mask_bit);

#endif /* ARC_ROADS_H */
