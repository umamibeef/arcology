/*  mesh/shapes.h: the line quads and fans over the emitter, and the props.
 *
 *  Every shape a family draws over the mesh is one of these.  A draped
 *  triangle.  A quad or a fan along a strip.  A box, a cylinder, a wire,
 *  a bar or a lamp face.  The two macros hand the emitter the caller's own
 *  name, which the inspector reports. */
#ifndef ARC_MESH_SHAPES_H
#define ARC_MESH_SHAPES_H

#include "pipeline.h"
#include "mesh/mesh.h"
#include "mesh/shape.h"

float ease_smooth(float f); /* a smoothstep over 0..1, level at both ends */

#define BAND_PARAPET 0.045f    /* a barrier, not a wall: 0.35 m over the slab */

/*  The line and thread emitters take, invisibly, the name of the
 *  function that called them: the inspector reports who drew each
 *  material on a tile.  The innermost emitter's own name says nothing
 *  useful. */
int   put_tri_draped_n_at(const char *where, const char *who, RMesh *m, const RCity *c, uint8_t mask_bit, float order, const float tri[3][3], const float *nrm, const float col[3], const float ref[3], const float ref2[3]);

#define put_tri_line_n(...) put_tri_draped_n_at(__FILE__ ":" R_STR(__LINE__), __func__, __VA_ARGS__)

int   put_tri_ground_at(const char *where, const char *who, RMesh *m, const RCity *c, uint8_t mask_bit, float order, const float tri[3][3], const float col[3], const float ref[3], const float ref2[3]); /* ... laid on the drawn surface */

int   strip_quad_z_at(const char *where, const char *who, RMesh *m, const RCity *c, uint8_t mask_bit, float order, const float a0[2], const float a1[2], const float b0[2], const float b1[2], float za, float zb, float across0, float across1, float across0b, float across1b, float along_a, float along_b, float mat);

int   strip_quad_at(const char *where, const char *who, RMesh *m, const RCity *c, uint8_t mask_bit, float order, const float a0[2], const float a1[2], const float b0[2], const float b1[2], float across0, float across1, float along_a, float along_b, float mat);

#define strip_quad(...) strip_quad_at(__FILE__ ":" R_STR(__LINE__), __func__, __VA_ARGS__)

int   strip_fan_z(RMesh *m, const RCity *c, uint8_t mask_bit, float order, float cx, float cy, float t0, float t1, float r0, float r1, float across_c, float mat, int n, float lift);

/*  A tile's outline on the ground, for the curve overlay.  It is four
 *  thin bars in a vehicle paint.  The tile's own ground and sprites stay
 *  visible inside them. */
int tile_highlight(RMesh *m, const RCity *c, uint8_t mask_bit, int32_t col, int32_t row, float paint);

int put_box(RMesh *m, const RCity *c, uint8_t mask_bit, float order, float cx, float cy, float w, float d, float z0, float z1, float mat, float phase);

int put_cyl(RMesh *m, const RCity *c, uint8_t mask_bit, float order, float cx, float cy, float r, float z0, float z1, float mat);

int curves_hidden(float mat); /* show curves: the network materials it hides */

int put_wire(RMesh *m, const RCity *c, uint8_t mask_bit, float order, float x0, float y0, float z0, float x1, float y1, float z1, float sag);

int put_wire_paint(RMesh *m, const RCity *c, uint8_t mask_bit, float order, float x0, float y0, float z0, float x1, float y1, float z1, float sag, float paint);

int put_bar(RMesh *m, const RCity *c, uint8_t mask_bit, float order, float x0, float y0, float z0, float x1, float y1, float z1, float fx, float fy, float w, float d, float mat, float code, float phase);

int put_lamp_face(RMesh *m, float order, float x, float y, float g, float z, float fx, float fy, float sz, float phase, float code, int uv);

/* ---- band.c: the band family */
int put_fascia(RMesh *m, const RCity *c, uint8_t mask_bit, float order, const float ea[2], const float eb[2], float za, float zb, const float nrm[3], float girder, int parapet);

/*  Show curves: a fitted path's vertices marked with the node models,
 *  `lift` over the ground.  Answers 0, or -1 where a mark could not be
 *  put. */
int curve_marks(RMesh *m, const RCity *c, uint8_t mask_bit, const V2 *q, const float *rad, int nk, float lift);

#endif /* ARC_MESH_SHAPES_H */
