/*  mesh/poly.cpp: a polygon's triangles by earcut (src/vendor/earcut.hpp),
 *  behind the C face in mesh/poly.h. */
extern "C" {
#include "mesh/poly.h"
}

#include <array>
#include <vector>

#include "earcut.hpp"

extern "C" int poly_triangulate(const float *pt, int stride, int n, int *tri)
{
    std::vector<std::vector<std::array<double, 2>>> poly(1);
    if (n < 3)
        return 0;
    poly[0].reserve(static_cast<size_t>(n));
    for (int k = 0; k < n; ++k)
        poly[0].push_back({pt[stride * k], pt[stride * k + 1]});
    std::vector<int> idx = mapbox::earcut<int>(poly);
    for (size_t k = 0; k < idx.size(); ++k)
        tri[k] = idx[k];
    return static_cast<int>(idx.size() / 3);
}

#include "clipper2/clipper.h"

namespace
{
Clipper2Lib::PathsD to_paths(const PolySet *s)
{
    Clipper2Lib::PathsD out;
    for (int r = 0; s && r < s->nring; ++r)
    {
        Clipper2Lib::PathD p;
        for (int k = 0; k < s->count[r]; ++k)
            p.push_back(Clipper2Lib::PointD(s->xy[2 * (s->start[r] + k)], s->xy[2 * (s->start[r] + k) + 1]));
        out.push_back(p);
    }
    return out;
}

void put_ring(PolySet *o, const Clipper2Lib::PathD &p, int hole, int poly)
{
    if (o->nring >= POLY_RINGS || o->npt + static_cast<int>(p.size()) > POLY_POINTS)
        return;
    o->start[o->nring] = o->npt, o->count[o->nring] = static_cast<int>(p.size()), o->hole[o->nring] = hole;
    o->poly[o->nring++] = poly;
    for (const Clipper2Lib::PointD &q : p)
    {
        o->xy[2 * o->npt] = static_cast<float>(q.x), o->xy[2 * o->npt + 1] = static_cast<float>(q.y);
        ++o->npt;
    }
}

void put_tree(PolySet *o, const Clipper2Lib::PolyPathD &node, int *npoly)
{
    for (const auto &child : node)
    {
        /*  An outer ring and its holes are one polygon.  An island in a hole
         *  starts a polygon of its own. */
        int poly = (*npoly)++;
        put_ring(o, child->Polygon(), 0, poly);
        for (const auto &hole : *child)
        {
            put_ring(o, hole->Polygon(), 1, poly);
            put_tree(o, *hole, npoly);
        }
    }
}
} // namespace

extern "C" int poly_clip(const PolySet *a, const PolySet *b, PolySet *out)
{
    Clipper2Lib::ClipperD  c(6);
    Clipper2Lib::PolyTreeD tree;
    int                    npoly = 0;
    out->nring = out->npt = 0;
    c.AddSubject(to_paths(a));
    if (b && b->nring)
        c.AddClip(to_paths(b));
    if (!c.Execute(b && b->nring ? Clipper2Lib::ClipType::Difference : Clipper2Lib::ClipType::Union,
                   Clipper2Lib::FillRule::NonZero,
                   tree))
        return -1;
    put_tree(out, tree, &npoly);
    return npoly;
}

extern "C" int poly_triangulate_holes(const float *pt, int stride, const int *start, const int *count, int nring, int *tri)
{
    using Point = std::array<double, 2>;
    std::vector<std::vector<Point>> poly(static_cast<size_t>(nring));
    std::vector<int>                index;
    for (int r = 0; r < nring; ++r)
        for (int k = 0; k < count[r]; ++k)
        {
            poly[static_cast<size_t>(r)].push_back({pt[stride * (start[r] + k)], pt[stride * (start[r] + k) + 1]});
            index.push_back(start[r] + k);
        }
    std::vector<int> idx = mapbox::earcut<int>(poly);
    for (size_t k = 0; k < idx.size(); ++k)
        tri[k] = index[static_cast<size_t>(idx[k])];
    return static_cast<int>(idx.size() / 3);
}
