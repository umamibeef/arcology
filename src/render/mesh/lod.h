/*  lod.h: continuous level of detail for a piece of mesh, by clustered
 *  simplification (clusterlod.h, a companion project of meshoptimizer).
 *
 *  An indexed mesh goes in, with the attributes its look depends on.  Out come its clusters at every level of
 *  detail, each a small group of triangles.  They form a hierarchy: the
 *  clusters of a level are made by simplifying groups of the level
 *  below.  Each cluster comes with the sphere that bounds it and the
 *  error its simplification has reached, in the mesh's own units.  The
 *  caller draws, at a distance, the clusters whose error it can no
 *  longer see.  Nothing here decides what the mesh is. */
#ifndef R_MESH_LOD_H
#define R_MESH_LOD_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*  An indexed mesh: `pos` holds three floats for each of `n_vert`
 *  vertices, `stride` bytes apart, and `idx` three indices a triangle.
 *  A collapse that moves an attribute is charged its move times its
 *  weight, as position error is. */
typedef struct
{
    const float    *pos;
    size_t          n_vert, stride;
    const uint32_t *idx;
    size_t          n_idx;
    /*  Attributes a vertex carries that simplification must keep, `n_attr`
     *  floats `attr_stride` bytes apart, each weighed against position by
     *  its weight.  NULL for none. */
    const float *attr;
    size_t       attr_stride, n_attr;
    const float *weight;
} MeshLodIn;

/*  One cluster.  It has the level it was made at, 0 for the mesh as it
 *  came.  It has the group of the level below it was simplified from, -1
 *  for none.  It has its bounding sphere, the error it carries, and its
 *  triangles as indices into the mesh's vertices.  The indices last
 *  until the call returns. */
typedef struct
{
    int             depth, refined;
    float           centre[3], radius, error;
    const uint32_t *idx;
    size_t          n_idx;
} MeshLodCluster;

/*  Called once for each group of clusters as the hierarchy is built.  It
 *  is handed the group's own error (what its simplified clusters show),
 *  its clusters and their count.  Answers the group's number, which a
 *  cluster's `refined` names. */
typedef int (*MeshLodOut)(void *ctx, int depth, float group_error, const MeshLodCluster *clusters, size_t n);

/*  Build the hierarchy of `in`, with at most `max_tris` triangles to a
 *  cluster (4 to 256).  0 on success. */
int mesh_lod_build(const MeshLodIn *in, size_t max_tris, void *ctx, MeshLodOut out);

#ifdef __cplusplus
}
#endif

#endif
