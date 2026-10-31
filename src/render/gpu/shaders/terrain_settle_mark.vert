#version 450
/*  terrain_settle_mark.vert -- terrain_settle.vert, reading the mesh from
 *  its buffers for each triangle's tile, which a mark lights.  So the
 *  marks light the land while it settles, as they light the art. */
#define LAND_SETTLE 1
#define MESH_PULLED 1
#define TILE_MARKS 1
//#include "terrain_body.vert"
