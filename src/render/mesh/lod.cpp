/*  lod.cpp: clusterlod.h compiled once, behind the C face in lod.h. */
#include "lod.h"

#include <meshoptimizer.h>

#define CLUSTERLOD_IMPLEMENTATION
#include <clusterlod.h>

#include <vector>

namespace
{
struct Relay
{
    void      *ctx;
    MeshLodOut out;
};

int relay(void *context, clodGroup group, const clodCluster *clusters, size_t count)
{
    const Relay                *r = static_cast<const Relay *>(context);
    std::vector<MeshLodCluster> mine(count);
    for (size_t i = 0; i < count; ++i)
    {
        const clodCluster &c = clusters[i];
        mine[i].depth        = group.depth;
        mine[i].refined      = c.refined;
        mine[i].centre[0]    = c.bounds.center[0];
        mine[i].centre[1]    = c.bounds.center[1];
        mine[i].centre[2]    = c.bounds.center[2];
        mine[i].radius       = c.bounds.radius;
        mine[i].error        = c.bounds.error;
        mine[i].idx          = c.indices;
        mine[i].n_idx        = c.index_count;
    }
    return r->out(r->ctx, group.depth, group.simplified.error, mine.data(), count);
}
} // namespace

extern "C" int mesh_lod_build(const MeshLodIn *in, size_t max_tris, void *ctx, MeshLodOut out)
{
    if (!in || !out || max_tris < 4 || max_tris > 256 || in->n_idx % 3 != 0)
        return -1;
    clodMesh mesh                 = {};
    mesh.indices                  = in->idx;
    mesh.index_count              = in->n_idx;
    mesh.vertex_count             = in->n_vert;
    mesh.vertex_positions         = in->pos;
    mesh.vertex_positions_stride  = in->stride;
    mesh.vertex_attributes        = in->attr;
    mesh.vertex_attributes_stride = in->attr ? in->attr_stride : 0;
    mesh.attribute_weights        = in->attr ? in->weight : nullptr;
    mesh.attribute_count          = in->attr ? in->n_attr : 0;
    Relay r                       = {ctx, out};
    clodBuild(clodDefaultConfig(max_tris), mesh, &r, relay);
    return 0;
}
