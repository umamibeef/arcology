/*  mesh/poly.h: a polygon, a ring of points, and a set of them.  Their
 *  area and whether a ring crosses itself are poly.c's.  Their triangles
 *  and their union are poly.cpp's.  Nothing here knows what a polygon is
 *  for. */
#ifndef ARC_MESH_POLY_H
#define ARC_MESH_POLY_H

/*  A ring of points, `stride` floats a point, x and y first. */

/*  Its area, positive where it runs counterclockwise. */
float poly_area(const float *pt, int stride, int n);
/*  Whether no two of its edges cross. */
int poly_simple(const float *pt, int stride, int n);
/*  Its triangles by earcut (mesh/poly.cpp), three indices each into `tri`,
 *  which holds n - 2 of them.  A point in line with its two neighbors, or
 *  one at the same place as the one before it, may be left out.  Answers
 *  how many. */
int poly_triangulate(const float *pt, int stride, int n, int *tri);

/*  A set of rings.  Each is `count` points from `start` in xy, x and y a
 *  point.  Each is a hole or not, and has the polygon it belongs to.  A
 *  polygon is an outer ring and the holes in it. */
#define POLY_RINGS  256
#define POLY_POINTS 8192
typedef struct
{
    float xy[2 * POLY_POINTS];
    int   start[POLY_RINGS], count[POLY_RINGS], hole[POLY_RINGS], poly[POLY_RINGS];
    int   nring, npt;
} PolySet;

/*  The union of a's rings, less the union of b's where b has any (Clipper2,
 *  mesh/poly.cpp).  A ring is filled where it winds round a point.  Answers
 *  how many polygons, or -1 where it cannot. */
int poly_clip(const PolySet *a, const PolySet *b, PolySet *out);
/*  The triangles of one polygon, its outer ring first and then its holes,
 *  each ring `count` points from `start`, `stride` floats a point.  Each
 *  index is the point's own.  Answers how many triangles. */
int poly_triangulate_holes(const float *pt, int stride, const int *start, const int *count, int nring, int *tri);

#endif /* ARC_MESH_POLY_H */
