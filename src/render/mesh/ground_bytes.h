/*  mesh/ground_bytes.h: what the building byte of a tile means to the
 *  ground.  The terrain byte's meaning is terrain.h's.
 *
 *  Each answer comes from the byte alone, and the ground asks each one at
 *  every tile of the map.  Does anything stand on the tile?  Does the piece on it follow the
 *  slope?  Is it a building with a footprint and an anchor?  Is it a raised
 *  piece that its neighbor orders?  Does the saddle lift apply?  What does
 *  the map view tint a placed structure, and which zone is a tile zoned
 *  for?  mesh/ground_bytes.c holds the answers. */
#ifndef R_MESH_GROUND_BYTES_H
#define R_MESH_GROUND_BYTES_H

#include <stdint.h>

#include "terrain.h"

int saddle_tile(uint8_t xbld);
int tile_built(uint8_t xbld);
int tile_sloped(uint8_t xbld);
int tile_building(uint8_t xbld);
int tile_elevated(uint8_t xbld);
int tile_placed(uint8_t xbld);
int tile_meshed(uint8_t xbld);
int structure_tint(uint8_t xbld);
int zone_of(uint8_t xzon);

#endif
