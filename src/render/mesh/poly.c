/*  poly.c: the arithmetic over a polygon, a ring of points (mesh/poly.h).
 *  Its triangles and its union are poly.cpp's.  Nothing here knows what
 *  the polygon is for. */
#include "mesh/poly.h"

/*  ---- a polygon ---------------------------------------------------------- */

static float poly_cross(const float *pt, int stride, int a, int b, int c)
{
    const float *p = &pt[stride * a], *q = &pt[stride * b], *r = &pt[stride * c];
    return (q[0] - p[0]) * (r[1] - p[1]) - (q[1] - p[1]) * (r[0] - p[0]);
}

float poly_area(const float *pt, int stride, int n)
{
    /*  From its first point, so a ring far from the origin keeps its
     *  precision. */
    float a = 0.0f;
    int   k;
    if (n < 3)
        return 0.0f;
    for (k = 1; k + 1 < n; ++k)
        a += poly_cross(pt, stride, 0, k, k + 1);
    return 0.5f * a;
}

int poly_simple(const float *pt, int stride, int n)
{
    int i, j;
    for (i = 0; i < n; ++i)
        for (j = i + 1; j < n; ++j)
        {
            const float *a = &pt[stride * i], *b = &pt[stride * ((i + 1) % n)];
            const float *c = &pt[stride * j], *d = &pt[stride * ((j + 1) % n)];
            float        d1, d2, d3, d4;
            if (j == i + 1 || (i == 0 && j == n - 1))
                continue;
            d1 = (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0]);
            d2 = (b[0] - a[0]) * (d[1] - a[1]) - (b[1] - a[1]) * (d[0] - a[0]);
            d3 = (d[0] - c[0]) * (a[1] - c[1]) - (d[1] - c[1]) * (a[0] - c[0]);
            d4 = (d[0] - c[0]) * (b[1] - c[1]) - (d[1] - c[1]) * (b[0] - c[0]);
            if (((d1 > 1e-9f && d2 < -1e-9f) || (d1 < -1e-9f && d2 > 1e-9f)) &&
                ((d3 > 1e-9f && d4 < -1e-9f) || (d3 < -1e-9f && d4 > 1e-9f)))
                return 0;
        }
    return 1;
}
