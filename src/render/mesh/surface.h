/*  mesh/surface.h: the drawn ground sampled, at a point and at a
 *  tile, and a shelf's corner over it. */
#ifndef ARC_MESH_SURFACE_H
#define ARC_MESH_SURFACE_H

#include "pipeline.h"
#include "mesh/mesh.h"

float surface_at_world(const RCity *c, uint8_t mask_bit, float x, float y);

float surface_at_tile(const RCity *c, uint8_t mask_bit, int32_t col, int32_t row, float x, float y); /* that tile's top, clamped to it */

int   tile_top_planar(const RCity *c, int32_t col, int32_t row, uint8_t mask_bit);                   /* its two triangles are coplanar */

/*  A shelf's corner over the drawn surface (mesh/surface.c). */
float surface_corner_height(const Sample *smp, int ns, int i, float dx, float dy, float shelf_grade, Family f);

#endif /* ARC_MESH_SURFACE_H */
