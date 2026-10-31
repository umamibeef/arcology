/*  mesh/shapes.h: the quads and fans over the emitter, and the props.
 *
 *  Every shape drawn over the ground is one of these.  A draped
 *  triangle.  A quad or a fan between two cross-sections.  A box, a
 *  cylinder, a wire, a bar or a lamp face.  The macros hand the emitter
 *  the caller's own name, which the inspector reports. */
#ifndef ARC_MESH_SHAPES_H
#define ARC_MESH_SHAPES_H

#include "mesh/mesh.h"
#include "mesh/shape.h"

/*  The draped emitters take, invisibly, the name of the function that
 *  called them: the inspector reports who drew each material on a tile.  The innermost emitter's own name says nothing
 *  useful. */
int put_tri_draped_n_at(const char *where, const char *who, RMesh *m, const RCity *c, uint8_t mask_bit, float order, const float tri[3][3], const float *nrm, const float col[3], const float ref[3], const float ref2[3]);

#define put_tri_line_n(...) put_tri_draped_n_at(__FILE__ ":" R_STR(__LINE__), __func__, __VA_ARGS__)

int put_tri_over_ground_at(const char *where, const char *who, RMesh *m, const RCity *c, uint8_t mask_bit, float order, const float tri[3][3], const float col[3], float lift); /* ... lift over the drawn surface */

int put_box(RMesh *m, const RCity *c, uint8_t mask_bit, float order, float cx, float cy, float w, float d, float z0, float z1, float mat, float phase);

/*  A FRUSTUM: `sides` faces round (x, y), from radius `r0` at `z0` to
 *  `r1` at `z1`, in levels over the ground `g`.  It has a cap on top
 *  where `r1` is not nought, and it is turned `turn` radians.  Answers 0,
 *  or -1 on a mesh error. */
int put_frustum(RMesh *m, float order, float x, float y, float g, float z0, float z1, float r0, float r1, int sides, float turn, float mat);
int put_wire(RMesh *m, const RCity *c, uint8_t mask_bit, float order, float x0, float y0, float z0, float x1, float y1, float z1, float sag);

int put_lamp_face(RMesh *m, float order, float x, float y, float g, float z, float fx, float fy, float sz, float phase, float code, int uv);
/*  A lamp's lens on the underside of its head: a flat face of MAT_LAMP
 *  carrying `code`, facing the ground.  It is `len` along (fx, fy) and
 *  `wid` across it, centered at (x, y), `z` over the ground `g`. */
int put_lamp_under(RMesh *m, float order, float x, float y, float g, float z, float fx, float fy, float len, float wid, float code);

/*  A prism clipped to the tile grid (shapes.c). */
int put_prism_clip_m(RMesh *m, const RCity *c, uint8_t mask_bit, float order, float cx, float cy, float dx, float dy, float len, float wid, float zb, float zf, float z0, float z1, float paint, float mat);

#endif /* ARC_MESH_SHAPES_H */
