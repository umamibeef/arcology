/*  mesh/emit.h: the emitter primitives the rest of the renderer draws
 *  with.  They are the land triangle, the walls,
 *  the record of who drew what, and the piece every triangle is filed
 *  under. */
#ifndef ARC_MESH_EMIT_H
#define ARC_MESH_EMIT_H

#include "mesh/mesh.h"

int put_wall(RMesh *m, const float t0[3], const float t1[3], const float b0[3], const float b1[3], const float nrm[3], float order, const float col[3]);

/*  A wall from top edge t0-t1 down to b0-b1, with its texture run from
 *  r0 to r1 along it. */
int put_wall_r(RMesh *m, const float t0[3], const float t1[3], const float b0[3], const float b1[3], const float nrm[3], float order, const float col[3], float r0, float r1);

#define R_STR2(x) #x
#define R_STR(x)  R_STR2(x)
int put_tri_r2_at(const char *where, const char *who, RMesh *m, const float p[3][3], const float *nrm, float order, const float col[3], const float *ref, const float *ref2, int flat);
/*  Every land triangle goes through here.  Each carries the name of the
 *  function that made it.  The macro hands the caller's own __func__ to
 *  the emitter, so no call site says anything.  The names are kept per
 *  tile, by material, and the inspector reads them back (mesh_origins). */
#define put_tri_r2(...) put_tri_r2_at(__FILE__ ":" R_STR(__LINE__) " ", __func__, __VA_ARGS__)
/*  A triangle with a normal at each corner, so a curved surface laid of
 *  few faces shades smoothly across them. */
int put_tri_n3_at(const char *where, const char *who, RMesh *m, const float p[3][3], const float nrm3[3][3], float order, const float col[3], const float *ref, const float *ref2);
#define put_tri_n3(...) put_tri_n3_at(__FILE__ ":" R_STR(__LINE__) " ", __func__, __VA_ARGS__)

/*  The record of who drew what: the per-tile origins and the component
 *  table.  Written only while a build records (mesh_record), so nothing
 *  drawn between builds adds to it.  A full build starts it again.  An edit's build keeps
 *  the components of the chunks that stand, whose triangles still index
 *  them.  Clears the origins of the tiles it builds once the closure has
 *  named them. */
void mesh_record(int on);

/*  Who drew what, by tile, for the inspector.  A whole build starts it
 *  again, and a window's build the tiles it visits. */
void mesh_origins_reset(void);
void mesh_origins_clear_wanted(void);

/*  THE PIECE being drawn: what every land triangle laid until the next
 *  call is filed under, named by where it is.  A key of 0 is no piece. */
uint32_t mesh_piece_key(int kind, int family, uint32_t at);
void     mesh_piece_is(uint32_t key);

#endif /* ARC_MESH_EMIT_H */
