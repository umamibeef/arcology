/*  mesh/emit.h: the two emitter primitives the rest of the renderer
 *  draws walls and clipped prisms with.  The triangle emitter itself is
 *  mesh/internal.h's. */
#ifndef ARC_MESH_EMIT_H
#define ARC_MESH_EMIT_H

#include "pipeline.h"
#include "mesh/mesh.h"

int   put_wall(RMesh *m, const float t0[3], const float t1[3], const float b0[3], const float b1[3], const float nrm[3], float order, const float col[3]);

int put_prism_clip_m(RMesh *m, const RCity *c, uint8_t mask_bit, float order, float cx, float cy, float dx, float dy, float len, float wid, float zb, float zf, float z0, float z1, float paint, float mat);

#endif /* ARC_MESH_EMIT_H */
