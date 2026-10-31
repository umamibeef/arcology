/*  adapt.h: the simulation's city, as the read-only view of its cells.
 *  This is the one file that includes both sides.  The network's and the
 *  renderer's own headers never see sim.h, and the simulation never sees
 *  a frame.  The adapter copies what they read into an RCity, and they
 *  cannot reach the City it came from.  The view from another side is
 *  turn.h's. */
#ifndef R_ADAPT_H
#define R_ADAPT_H

#include "city.h"
#include "sim.h"
#include "world.h"

/*  Fill `v` from a whole world.  The view is a square `adapt_world_side`
 *  tiles a side, and its first tile is the world's first.  It holds
 *  every chunk at its place, the moving things of every city, and the
 *  records of city `active`. */
void adapt_world(RCity *v, const World *w, int active);
/*  The side of that square: the world's bounds, never under a city's
 *  window. */
int32_t adapt_world_side(const World *w);
#endif /* R_ADAPT_H */
