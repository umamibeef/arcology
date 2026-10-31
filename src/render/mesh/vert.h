/*  mesh/vert.h: one vertex of the terrain mesh.  Its own header because
 *  both the mesh and the device read it and neither owns it. */
#ifndef R_MESHVERT_H
#define R_MESHVERT_H

#include <stdint.h>

/*  One vertex of the terrain mesh, 32 bytes, the same in memory and on
 *  the device.
 *
 *      Pos: column, row, altitude in levels, and how far a cliff's lean
 *      moved the ground there, in levels, nought on any other face.
 *      The painter's order is the triangle's, kept beside it (mesh.h).
 *      Paint: across and along a material's paint, a wall's two
 *      reference heights, or the height field's smoothed gradient in
 *      world units.
 *      Nrm: the normal in world units, octahedral, two signed 16-bit
 *      fractions.
 *      Aux: two half floats.  The first is the height field's curvature
 *      on the ground, nought over it.  The second is the material.
 *
 *  So the ground material can read the topology without the facets.
 *  Every vertex has the same palette index, which the shader holds. */
typedef struct
{
    float    pos[4];
    float    paint[2];
    int16_t  nrm[2];
    uint16_t aux[2];
} RMeshVert;

/*  Write the normal, the curvature or class and the material.  Read the
 *  curvature or class and the material back (mesh/list.c). */
void  mesh_vert_pack(RMeshVert *v, const float nrm[3], float aux, float mat);
float mesh_vert_mat(const RMeshVert *v);

#endif /* R_MESHVERT_H */
