/*  mesh/slab.h: a closed slab, laid face by face.  A slab has a top, a
 *  bottom that faces down and sides that close it.  Nothing here knows what
 *  the slab is for. */
#ifndef ARC_MESH_SLAB_H
#define ARC_MESH_SLAB_H

#include <stdint.h>

#include "city.h"
#include "mesh/mesh.h"

/*  One triangle, turned to face up, or down where `down` is set.  It takes
 *  the painter's slot of the tile its first corner lies on. */
void slab_tri_faced(RMesh *m, const RCity *c, uint8_t mask_bit, const float a[3], const float b[3], const float cc[3], float mat, int down);
/*  One triangle facing up. */
void slab_tri(RMesh *m, const RCity *c, uint8_t mask_bit, const float a[3], const float b[3], const float cc[3], float mat);
/*  One quad facing up, as two triangles. */
void slab_quad(RMesh *m, const RCity *c, uint8_t mask_bit, const float a[3], const float b[3], const float cc[3], const float d[3], float mat);
/*  One quad facing down, for a bottom. */
void slab_quad_under(RMesh *m, const RCity *c, uint8_t mask_bit, const float a[3], const float b[3], const float cc[3], const float d[3], float mat);
/*  A closed slab between two cross-sections a and b of n points each, their
 *  points in the same order.  Its top is n - 1 strips, strip e of material
 *  mat[e].  Its bottom lies `thick` under the top.  Its outer sides and its
 *  two ends close it. */
void slab_strips(RMesh *m, const RCity *c, uint8_t mask_bit, const float (*a)[3], const float (*b)[3], int n, float thick, const float *mat);

/*  A closed slab swept along a run of ns cross-sections of n points
 *  each.  They are laid out section after section in `sec`, x, y and z a
 *  point.  Its top is n - 1 strips along the run, and its bottom lies
 *  `thick` under it.  Its outer sides and its two ends close it. */
void slab_sweep(RMesh *m, const RCity *c, uint8_t mask_bit, const float *sec, int ns, int n, float thick, const float *mat);
/*  As slab_sweep, its bottom, sides and ends of material `side`.  A `side`
 *  below nought keeps each strip's own material. */
void slab_sweep_sided(RMesh *m, const RCity *c, uint8_t mask_bit, const float *sec, int ns, int n, float thick, const float *mat, float side);
/*  As slab_sweep_sided, the top of strip e filed under shape part[e]
 *  where that is a shape.  The bottom, the sides and the ends are the
 *  shape open around the call. */
void slab_sweep_parts(RMesh *m, const RCity *c, uint8_t mask_bit, const float *sec, int ns, int n, float thick, const float *mat, float side, const uint32_t *part);

#endif /* ARC_MESH_SLAB_H */
