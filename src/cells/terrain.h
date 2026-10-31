/*  terrain.h: what a tile's terrain byte means to the land.  Is it water?
 *  Which slope does it carry, which corners does it lift and which way
 *  does its top fold?  Does the saddle lift apply to its ground?  The
 *  network, the mesh and the app each read the land through these.
 *  terrain.c holds the answers. */
#ifndef ARC_TERRAIN_H
#define ARC_TERRAIN_H

#include <stdint.h>

int     is_water(uint8_t xter);
int32_t slope_code(uint8_t xter);
int     corner_lifts(int32_t code);
int     cut_ne_sw(int32_t code);
int     saddle_terrain(uint8_t xter);

#endif /* ARC_TERRAIN_H */
