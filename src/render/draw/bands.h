/*  draw/bands.h: the highways drawn from the road network's records.
 *
 *  Each highway road with a line is a deck: a closed slab along its line at
 *  its height.  Its top is cut into strips where the material of its lanes
 *  changes (plans/drawing.md, "A highway").  A highway unit whose roads all
 *  have their lines is drawn this way, and its art is not drawn.  An
 *  interchange that is a placeholder keeps its art. */
#ifndef ARC_BANDS_H
#define ARC_BANDS_H

#include <stdint.h>

#include "city.h"
#include "mesh/mesh.h"

/*  Work out which highway roads are drawn, from the network roads_build
 *  worked out.  Each tile whose art the decks replace is set to 1 in
 *  `drawn`, one byte a tile of a map `side` tiles across.  A tile is
 *  replaced where everything on it is drawn: `cover` is 1 where a drawn
 *  street or railway lies.  0 on success. */
int bands_build(const RCity *c, uint8_t *drawn, const uint8_t *cover, int side);

/*  Lay every deck into the mesh. */
void bands_draw(RMesh *m, const RCity *c, uint8_t mask_bit);

#endif /* ARC_BANDS_H */
