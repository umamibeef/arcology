/*  r_adapt.h -- the simulation's city, as the renderer's read-only view.
 *
 *  This is the one file that includes both sides.  The renderer's own
 *  headers never see sc2k.h, and the simulation never sees a frame: the
 *  adapter copies what the renderer reads into an RCity, and the renderer
 *  cannot reach the City it came from.
 */
#ifndef R_ADAPT_H
#define R_ADAPT_H

#include "r_city.h"
#include "sc2k.h"

/*  Fill `v` from `c`.  About 156 KB of copying, which is cheap next to a
 *  sweep; call it whenever the simulation has run. */
void r_adapt_city(RCity *v, const City *c);

#endif /* R_ADAPT_H */
