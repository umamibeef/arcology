/*  meshvert.h -- one vertex of the terrain mesh.
 *
 *  Its own header because both sides need it and neither should own it:
 *  it lived in gpu.h, which made mesh.h include gpu.h -- the geometry
 *  subsystem depending on the device that happens to draw it.  The arrow
 *  now points from both at this.
 */
#ifndef R_MESHVERT_H
#define R_MESHVERT_H

/*  One vertex of the terrain mesh.  pos is column, row, altitude in levels
 *  and the tile's painter's index; nrm is the face normal in world units;
 *  nrm's fourth component is the height field's curvature at the vertex,
 *  col's first two its smoothed gradient, in world units, so the ground
 *  material can read the topology without the facets; col's alpha is the
 *  palette index. */
typedef struct
{
    float pos[4];
    float nrm[4];
    float col[4];
} RMeshVert;

#endif /* R_MESHVERT_H */
