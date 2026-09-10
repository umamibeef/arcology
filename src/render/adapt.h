/*  adapt.h: the simulation's city, as the renderer's read-only view.
 *  This is the one file that includes both sides.  The renderer's own
 *  headers never see sim.h, and the simulation never sees a frame.  The
 *  adapter copies what the renderer reads into an RCity, and the
 *  renderer cannot reach the City it came from. */
#ifndef R_ADAPT_H
#define R_ADAPT_H

#include "city.h"
#include "sim.h"

/*  Fill `v` from `c`.  About 156 KB of copying, which is cheap next to a
 *  sweep.  Call it whenever the simulation has run. */
void adapt_city(RCity *v, const City *c);
void adapt_city_turned(RCity *v, const City *c, int q); /* the view from a quarter turn, read off the unturned city */

#endif /* R_ADAPT_H */
