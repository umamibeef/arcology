/*  mesh/slab.c: a closed slab, laid face by face (mesh/slab.h).  Nothing here
 *  knows what the slab is for. */
#include "mesh/slab.h"

#include <math.h>
#include <string.h>

#include "mesh/internal.h"
#include "mesh/shape.h"

/*  The most points a cross-section of a slab has. */
#define SLAB_MOST 16

/*  One triangle of a slab, turned to face up, or down for its bottom.  The
 *  bottom of a closed slab faces out of it, so the check of faces at one
 *  height can tell it from a top. */
void slab_tri_faced(RMesh *m, const RCity *c, uint8_t mask_bit, const float a[3], const float b[3], const float cc[3], float mat, int down)
{
    float col[3] = {0.0f, 0.0f, mat}, t[3][3];
    float order  = tile_order(c, (int32_t)floorf(a[0]), (int32_t)floorf(a[1]), mask_bit);
    int   flip   = ((b[0] - a[0]) * (cc[1] - a[1]) - (b[1] - a[1]) * (cc[0] - a[0]) < 0.0f) != (down != 0);
    memcpy(t[0], a, sizeof t[0]);
    memcpy(t[1], flip ? cc : b, sizeof t[1]);
    memcpy(t[2], flip ? b : cc, sizeof t[2]);
    put_tri_r2(m, (const float (*)[3])t, NULL, order, col, NULL, NULL, 1);
}

void slab_tri(RMesh *m, const RCity *c, uint8_t mask_bit, const float a[3], const float b[3], const float cc[3], float mat)
{
    slab_tri_faced(m, c, mask_bit, a, b, cc, mat, 0);
}

/*  One quad of a slab, as two triangles. */
void slab_quad(RMesh *m, const RCity *c, uint8_t mask_bit, const float a[3], const float b[3], const float cc[3], const float d[3], float mat)
{
    slab_tri(m, c, mask_bit, a, b, cc, mat);
    slab_tri(m, c, mask_bit, a, cc, d, mat);
}

/*  One quad of a slab's bottom, facing down. */
void slab_quad_under(RMesh *m, const RCity *c, uint8_t mask_bit, const float a[3], const float b[3], const float cc[3], const float d[3], float mat)
{
    slab_tri_faced(m, c, mask_bit, a, b, cc, mat, 1);
    slab_tri_faced(m, c, mask_bit, a, cc, d, mat, 1);
}

/*  A closed slab between two cross-sections: its top in strips, its bottom
 *  `thick` under it, its outer sides and its two ends. */
void slab_strips(RMesh *m, const RCity *c, uint8_t mask_bit, const float (*a)[3], const float (*b)[3], int n, float thick, const float *mat)
{
    float al[SLAB_MOST][3], bl[SLAB_MOST][3];
    int   e;
    if (n < 2 || n > SLAB_MOST)
        return;
    for (e = 0; e < n; ++e)
    {
        memcpy(al[e], a[e], sizeof al[e]), al[e][2] -= thick;
        memcpy(bl[e], b[e], sizeof bl[e]), bl[e][2] -= thick;
    }
    for (e = 0; e < n - 1; ++e)
    {
        slab_quad(m, c, mask_bit, a[e], b[e], b[e + 1], a[e + 1], mat[e]);
        slab_quad_under(m, c, mask_bit, al[e], bl[e], bl[e + 1], al[e + 1], mat[e]);
        slab_quad(m, c, mask_bit, a[e], a[e + 1], al[e + 1], al[e], mat[e]);
        slab_quad(m, c, mask_bit, b[e], b[e + 1], bl[e + 1], bl[e], mat[e]);
    }
    slab_quad(m, c, mask_bit, a[0], b[0], bl[0], al[0], mat[0]);
    slab_quad(m, c, mask_bit, a[n - 1], b[n - 1], bl[n - 1], al[n - 1], mat[n - 2]);
}

void slab_sweep(RMesh *m, const RCity *c, uint8_t mask_bit, const float *sec, int ns, int n, float thick, const float *mat)
{
    slab_sweep_sided(m, c, mask_bit, sec, ns, n, thick, mat, -1.0f);
}

void slab_sweep_sided(RMesh *m, const RCity *c, uint8_t mask_bit, const float *sec, int ns, int n, float thick, const float *mat, float side)
{
    slab_sweep_parts(m, c, mask_bit, sec, ns, n, thick, mat, side, NULL);
}

void slab_sweep_parts(RMesh *m, const RCity *c, uint8_t mask_bit, const float *sec, int ns, int n, float thick, const float *mat, float side, const uint32_t *part)
{
    int k, e;
    if (ns < 2 || n < 2 || n > SLAB_MOST)
        return;
    for (k = 0; k < ns; ++k)
    {
        const float (*a)[3] = (const float (*)[3])(sec + (size_t)(3 * n) * (size_t)(k > 0 ? k - 1 : 0));
        const float (*b)[3] = (const float (*)[3])(sec + (size_t)(3 * n) * (size_t)k);
        float al[SLAB_MOST][3], bl[SLAB_MOST][3];
        for (e = 0; e < n; ++e)
        {
            memcpy(al[e], a[e], sizeof al[e]), al[e][2] -= thick;
            memcpy(bl[e], b[e], sizeof bl[e]), bl[e][2] -= thick;
        }
        /*  The two ends close it. */
        if (k == 0 || k == ns - 1)
            for (e = 0; e < n - 1; ++e)
                slab_quad(m, c, mask_bit, b[e], b[e + 1], bl[e + 1], bl[e], side >= 0.0f ? side : mat[e]);
        if (k == 0)
            continue;
        for (e = 0; e < n - 1; ++e)
        {
            if (part && part[e] != SHAPE_NONE)
                shape_enter(part[e]);
            slab_quad(m, c, mask_bit, a[e], b[e], b[e + 1], a[e + 1], mat[e]);
            if (part && part[e] != SHAPE_NONE)
                shape_close(part[e]);
            slab_quad_under(m, c, mask_bit, al[e], bl[e], bl[e + 1], al[e + 1], side >= 0.0f ? side : mat[e]);
        }
        slab_quad(m, c, mask_bit, a[0], b[0], bl[0], al[0], side >= 0.0f ? side : mat[0]);
        slab_quad(m, c, mask_bit, a[n - 1], b[n - 1], bl[n - 1], al[n - 1], side >= 0.0f ? side : mat[n - 2]);
    }
}
