/*  mesh/tile.h: what a tile answers about itself to the layers above:
 *  the height a node stands at. */
#ifndef ARC_MESH_TILE_H
#define ARC_MESH_TILE_H

#include "mesh/mesh.h"

float         node_altitude(const RCity *c, int32_t col, int32_t row);

#endif /* ARC_MESH_TILE_H */
