/*  mesh/ground.c: the ground itself, one tile at a time.
 *
 *  The heightfield, the slope codes and the pads a network cut into the
 *  hill are settled before this (mesh/tile.c).  This file draws four
 *  things over them:
 *
 *    the TOP     the dual ground of open land: eight triangles between
 *                the middle of the tile, the middles of its edges and its
 *                corners.  Or the face of a pad or a sloped piece, or the
 *                water surface where a body of water lies on the tile.
 *    the SEABED  under that water, and the land below the surface down to
 *                it on each side that is not water.  Both sit a quarter
 *                step behind the tile, so the surface wins inside the
 *                diamond and they show through the glass.
 *    the WALLS   down to the neighbor over each edge.  Only the east and
 *                south edges get them, so an edge between two tiles is
 *                drawn once.
 *    the RIM     at the cut edges of the map.  Through land it shows the
 *                layers of sediment from the drawn top down to the floor,
 *                and the foundation of a pad or a spur as blocks above the
 *                ground.  Through water it shows the glass from the
 *                surface to the seabed, and the sediment under it.
 *
 *  The arithmetic is done in double and each value becomes a float where
 *  it goes into a face. */
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "geo.h"
#include "mesh/emit.h"
#include "mesh/internal.h"

static float tile_pad(const TileFan *t, int k);

/*  How far behind the tile the seabed and the land under water sit.  And
 *  how far in front of it a wall between two tiles sits. */
static const double UNDER  = 0.25;
static const double FACING = 0.5;

/*  THE FACE OF A CLIFF, broken.  A grid of facets runs from its top to its
 *  foot.  Each row is a LEDGE, set in or out by its own amount, and each
 *  point inside the face is jagged a little more.  These are how far each
 *  goes, as a part of a tile, and how many facets cross each half of an
 *  edge.  The rim of the face lies on the edge of the tile, so it meets
 *  the ground.  The corners of the tile stay still, so it meets the faces
 *  beside it. */
static const double CLIFF_LEDGE = 0.07;
static const double CLIFF_JAG   = 0.05;
enum
{
    CLIFF_FACETS = 3
};

/*  A POINT of the top of a tile.  Points 0 to 3 are its corners, 4 to 7
 *  the middle of edge k - 4, and 8 its middle.  It is on the top, or on
 *  the seabed under it.  The seabed between two corners is on the straight line between
 *  them. */
void tile_point(const TileFan *t, int k, int bed, float out[3])
{
    if (k < 4)
        memcpy(out, bed ? t->bed[k] : t->p[k], sizeof(float) * 3);
    else if (k < 8)
    {
        memcpy(out, t->mid[k - 4], sizeof(float) * 3);
        if (bed)
            out[2] = 0.5f * (t->bed[EDGE_A[k - 4]][2] + t->bed[EDGE_B[k - 4]][2]);
    }
    else
    {
        memcpy(out, t->ctr, sizeof(float) * 3);
        if (bed)
            out[2] = 0.25f * (t->bed[0][2] + t->bed[1][2] + t->bed[2][2] + t->bed[3][2]);
    }
}

/*  The height of the pad at a point.  At a middle, it is the mean of the
 *  corners the point lies between. */
static float tile_pad(const TileFan *t, int k)
{
    if (k < 4)
        return t->pad[k];
    if (k < 8)
        return 0.5f * (t->pad[EDGE_A[k - 4]] + t->pad[EDGE_B[k - 4]]);
    return 0.25f * (t->pad[0] + t->pad[1] + t->pad[2] + t->pad[3]);
}

/*  How far the lean of a cliff moved point k, in levels. */
static double lean_at(const TileFan *t, int k)
{
    return t->dual ? (double)tile_lean(t->c, t->col, t->row, k) : 0.0;
}

static double top_z(const TileFan *t, int k)
{
    float p[3];
    tile_point(t, k, 0, p);
    return (double)p[2];
}

static double bed_z(const TileFan *t, int k)
{
    float p[3];
    tile_point(t, k, 1, p);
    return (double)p[2];
}

static void colour(float out[3], double r, double g, double b)
{
    out[0] = (float)r, out[1] = (float)g, out[2] = (float)b;
}

/*  A wall between two points of the edge of the tile.  It goes from the
 *  heights za and zb down to qa and qb, and turns to (nx, ny). */
static void wall(const TileFan *t, int ia, int ib, double za, double zb, double qza, double qzb, double nx, double ny, double order, const float col[3])
{
    float a[3], b[3], qa[3], qb[3], nrm[3];
    tile_point(t, ia, 0, a);
    tile_point(t, ib, 0, b);
    a[2] = (float)za, b[2] = (float)zb;
    qa[0] = a[0], qa[1] = a[1], qa[2] = (float)qza;
    qb[0] = b[0], qb[1] = b[1], qb[2] = (float)qzb;
    nrm[0] = (float)nx, nrm[1] = (float)ny, nrm[2] = 0.0f;
    put_wall(t->m, a, b, qa, qb, nrm, (float)order, col);
}

/*  The same wall cut into `n` columns along its edge, for a fine tile on
 *  either side of it. */
static void wall_cut(const TileFan *t, int ia, int ib, double za, double zb, double qza, double qzb, double nx, double ny, double order, const float col[3], int n)
{
    float a[3], b[3], nrm[3];
    int   k;
    tile_point(t, ia, 0, a);
    tile_point(t, ib, 0, b);
    nrm[0] = (float)nx, nrm[1] = (float)ny, nrm[2] = 0.0f;
    for (k = 0; k < n; ++k)
    {
        float u = (float)k / (float)n, v = (float)(k + 1) / (float)n;
        float p[3]  = {a[0] + (b[0] - a[0]) * u, a[1] + (b[1] - a[1]) * u, (float)(za + (zb - za) * u)};
        float q[3]  = {a[0] + (b[0] - a[0]) * v, a[1] + (b[1] - a[1]) * v, (float)(za + (zb - za) * v)};
        float pq[3] = {p[0], p[1], (float)(qza + (qzb - qza) * u)}, qq[3] = {q[0], q[1], (float)(qza + (qzb - qza) * v)};
        put_wall(t->m, p, q, pq, qq, nrm, (float)order, col);
    }
}

/*  The same wall, with the two heights the material reads along it (ra,
 *  rb).  The second reference at each end (sa, sb) is one where topsoil
 *  lies under the top and nought where none does. */
static void wall_r(const TileFan *t, int ia, int ib, double za, double zb, double qza, double qzb, double nx, double ny, double order, const float col[3], double ra, double rb, double sa, double sb)
{
    float a[3], b[3], qa[3], qb[3], nrm[3];
    tile_point(t, ia, 0, a);
    tile_point(t, ib, 0, b);
    a[2] = (float)za, b[2] = (float)zb;
    qa[0] = a[0], qa[1] = a[1], qa[2] = (float)qza;
    qb[0] = b[0], qb[1] = b[1], qb[2] = (float)qzb;
    nrm[0] = (float)nx, nrm[1] = (float)ny, nrm[2] = 0.0f;
    put_wall_r2(t->m, a, b, qa, qb, nrm, (float)order, col, (float)ra, (float)rb, (float)sa, (float)sb);
}

/*  wall_r cut into `n` columns along its edge, for a fine tile on it. */
static void wall_r_cut(const TileFan *t, int ia, int ib, double za, double zb, double qza, double qzb, double nx, double ny, double order, const float col[3], double ra, double rb, double sa, double sb, int n)
{
    float a[3], b[3], nrm[3];
    int   k;
    tile_point(t, ia, 0, a);
    tile_point(t, ib, 0, b);
    nrm[0] = (float)nx, nrm[1] = (float)ny, nrm[2] = 0.0f;
    for (k = 0; k < n; ++k)
    {
        double u = (double)k / n, v = (double)(k + 1) / n;
        float  p[3]  = {a[0] + (b[0] - a[0]) * (float)u, a[1] + (b[1] - a[1]) * (float)u, (float)(za + (zb - za) * u)};
        float  q[3]  = {a[0] + (b[0] - a[0]) * (float)v, a[1] + (b[1] - a[1]) * (float)v, (float)(za + (zb - za) * v)};
        float  pq[3] = {p[0], p[1], (float)(qza + (qzb - qza) * u)}, qq[3] = {q[0], q[1], (float)(qza + (qzb - qza) * v)};
        put_wall_r2(t->m, p, q, pq, qq, nrm, (float)order, col, (float)(ra + (rb - ra) * u), (float)(ra + (rb - ra) * v), (float)(sa + (sb - sa) * u), (float)(sa + (sb - sa) * v));
    }
}

/*  The glass at the cut edge of the map: a water face that carries four
 *  heights, the surface and the bed at each end. */
static void glass(const TileFan *t, int ia, int ib, double nx, double ny, double order, const float col[3])
{
    float a[3], b[3], qa[3], qb[3], nrm[3];
    tile_point(t, ia, 0, a);
    tile_point(t, ib, 0, b);
    tile_point(t, ia, 1, qa);
    tile_point(t, ib, 1, qb);
    nrm[0] = (float)nx, nrm[1] = (float)ny, nrm[2] = 0.0f;
    t->m->to_water = 1;
    put_wall_r2(t->m, a, b, qa, qb, nrm, (float)order, col, a[2], b[2], qa[2], qb[2]);
    t->m->to_water = 0;
}

/*  A number from -1 to 1 for a place and a key, the same every build. */
static double rock_noise(double x, double y, double k)
{
    double v = sin(x * 12.9898 + y * 78.233 + k * 37.719) * 43758.5453;
    return (v - floor(v)) * 2.0 - 1.0;
}

/*  One half of the face of a cliff, from tile point pa to pb.  It has the
 *  top and the foot at each end, the side it is seen from, and the rows
 *  of ledges the whole edge has.  (mx, my) is the middle of the edge,
 *  which keys the ledges, so the two halves of an edge agree. */
typedef struct
{
    const TileFan *t;
    int            pa, pb, rows;
    double         xa, ya, xb, yb, ta, fa, tb, fb, nx, ny, mx, my, order;
    float          col[3];
    int            parts; /* each rim cut into this many parts, for a fine tile on it, or 0 */
} Cliff;

/*  Point (i, j) of the grid of facets: x, y, z, and the top above it. */
static void cliff_point(const Cliff *f, int i, int j, double out[4])
{
    double u = (double)i / (double)CLIFF_FACETS, s = (double)j / (double)f->rows;
    double x = f->xa + (f->xb - f->xa) * u, y = f->ya + (f->yb - f->ya) * u;
    double top = f->ta + (f->tb - f->ta) * u, foot = f->fa + (f->fb - f->fa) * u;
    double z     = top + (foot - top) * s;
    int    still = (i == 0 && f->pa < 4) || (i == CLIFF_FACETS && f->pb < 4);
    if (j > 0 && j < f->rows && !still)
    {
        double d = CLIFF_LEDGE * rock_noise(f->mx, f->my, (double)j) + CLIFF_JAG * rock_noise(x, y, (double)j + 0.5);
        double h = top - foot;
        d        = d * (h < 1.0 ? h : 1.0);
        x = x + f->nx * d, y = y + f->ny * d;
    }
    out[0] = x, out[1] = y, out[2] = z, out[3] = top;
}

static void cliff_tri(const Cliff *f, const double p[4], const double q[4], const double w[4])
{
    float tri[3][3], side[2], ref[3];
    int   i;
    for (i = 0; i < 3; ++i)
    {
        tri[0][i] = (float)p[i];
        tri[1][i] = (float)q[i];
        tri[2][i] = (float)w[i];
    }
    side[0] = (float)f->nx, side[1] = (float)f->ny;
    ref[0] = (float)p[3], ref[1] = (float)q[3], ref[2] = (float)w[3];
    put_face(f->t->m, (const float (*)[3])tri, side, (float)f->order, f->col, ref);
}

/*  The top row or the foot row where a fine tile lies on the rim.  The rim is
 *  cut into the fine tile's parts and zipped to row j of the grid.  So every
 *  point of the rim is a corner of the face, as it is of the tile. */
static void cliff_zip(const Cliff *f, int rim, int j)
{
    double s = (double)rim / (double)f->rows, a0[4], a1[4], b0[4], b1[4];
    int    i = 0, k = 0, n = f->parts;
    while (i < n || k < CLIFF_FACETS)
    {
        double ua = (double)i / n, ub = (double)(i + 1) / n;
        a0[0] = f->xa + (f->xb - f->xa) * ua, a0[1] = f->ya + (f->yb - f->ya) * ua;
        a0[3] = f->ta + (f->tb - f->ta) * ua;
        a0[2] = a0[3] + (f->fa + (f->fb - f->fa) * ua - a0[3]) * s;
        cliff_point(f, k, j, b0);
        if (k == CLIFF_FACETS || (i < n && ub <= (double)(k + 1) / (double)CLIFF_FACETS))
        {
            a1[0] = f->xa + (f->xb - f->xa) * ub, a1[1] = f->ya + (f->yb - f->ya) * ub;
            a1[3] = f->ta + (f->tb - f->ta) * ub;
            a1[2] = a1[3] + (f->fa + (f->fb - f->fa) * ub - a1[3]) * s;
            cliff_tri(f, a0, a1, b0);
            ++i;
        }
        else
        {
            cliff_point(f, k + 1, j, b1);
            cliff_tri(f, a0, b0, b1);
            ++k;
        }
    }
}

/*  The top row and the foot row are fans from the two ends of the rim.
 *  So the rim stays one edge, as the rim of the ground does.  Row j of the
 *  grid is the row the fan meets. */
static void cliff_fan(const Cliff *f, int rim, int j)
{
    const int k = CLIFF_FACETS / 2;
    double    ra[4], rb[4], p[4], q[4];
    int       i;
    cliff_point(f, 0, rim, ra);
    cliff_point(f, CLIFF_FACETS, rim, rb);
    for (i = 0; i < k; ++i)
    {
        cliff_point(f, i, j, p), cliff_point(f, i + 1, j, q);
        cliff_tri(f, ra, p, q);
    }
    cliff_point(f, k, j, p);
    cliff_tri(f, ra, p, rb);
    for (i = k; i < CLIFF_FACETS; ++i)
    {
        cliff_point(f, i, j, p), cliff_point(f, i + 1, j, q);
        cliff_tri(f, rb, p, q);
    }
}

static void cliff_face(Cliff *f)
{
    float  a[3], b[3];
    double p[4], q[4], r[4], s[4];
    int    i, j;
    tile_point(f->t, f->pa, 0, a);
    tile_point(f->t, f->pb, 0, b);
    f->xa = (double)a[0], f->ya = (double)a[1];
    f->xb = (double)b[0], f->yb = (double)b[1];
    if (f->parts > 1)
        cliff_zip(f, 0, 1);
    else
        cliff_fan(f, 0, 1);
    for (j = 1; j <= f->rows - 2; ++j)
        for (i = 0; i < CLIFF_FACETS; ++i)
        {
            cliff_point(f, i, j, p), cliff_point(f, i + 1, j, q);
            cliff_point(f, i + 1, j + 1, r), cliff_point(f, i, j + 1, s);
            cliff_tri(f, p, q, r);
            cliff_tri(f, p, r, s);
        }
    if (f->parts > 1)
        cliff_zip(f, f->rows, f->rows - 1);
    else
        cliff_fan(f, f->rows, f->rows - 1);
}

static double max2(double a, double b)
{
    return b > a ? b : a;
}

static double min2(double a, double b)
{
    return b < a ? b : a;
}

/*  The two walls on edge e, in two halves through the middle of the edge.
 *  Each face is seen from its lower side. */
static void tile_walls(const TileFan *t, int e)
{
    const int ia = t->nbr[e].ia, ib = t->nbr[e].ib;
    double    za = top_z(t, ia), zb = top_z(t, ib), zm = top_z(t, 4 + e);
    double    na = (double)t->nbr[e].za, nb = (double)t->nbr[e].zb, nm = (double)t->nbr[e].zm;
    int       built, nbuilt, half, cliff = t->nbr[e].cliff, nx_bld = t->nbr[e].xbld;
    int       parts = (t->fine ? t->fine : t->split[e]) / 2; /* a fine tile on either side cuts each half */
    float     col[3];
    if (fabs(za - na) < 1e-4 && fabs(zb - nb) < 1e-4 && fabs(zm - nm) < 1e-4)
        return;
    /*  A wall of coursed blocks where the step is ENGINEERED, and earth
     *  where the step is the hill's own.  The foundation of a building is
     *  engineered.  So is the side of a network tile that stands ABOVE its
     *  neighbor, which is a line or a line built up on fill.  A network
     *  tile that sits BELOW its neighbor is a cut into the slope.  The face
     *  of the slope is earth, which is what the art shows. */
    built  = t->xbld >= 0x0E && t->xbld < 0x49;
    nbuilt = nx_bld >= 0x0E && nx_bld < 0x49;
    if (cliff)
        colour(col, 0.0, 0.0, MAT_SEDIMENT);
    else if (t->xbld >= 0x70 || nx_bld >= 0x70)
        colour(col, 0.0, 0.0, MAT_ENG_WALL);
    else if (za + zb > na + nb + 1e-4 && built)
        colour(col, 0.0, 0.0, MAT_ENG_WALL); /* this tile is built up over its neighbor */
    else if (na + nb > za + zb + 1e-4 && nbuilt)
        colour(col, 0.0, 0.0, MAT_ENG_WALL); /* the neighbor is */
    else if (t->wet && t->nbr[e].water)
        /*  A drop between two waters: a face of water, a cascade. */
        colour(col, max2(max2(max2(za, zb), na), nb), 1.0, MAT_SURFACE);
    else
        colour(col, 0.0, 0.0, MAT_EARTH);
    for (half = 0; half < 2; ++half)
    {
        int    pa = half ? 4 + e : ia, pb = half ? ib : 4 + e;
        double a1 = half ? zm : za, b1 = half ? zb : zm, a2 = half ? nm : na, b2 = half ? nb : nm;
        double nx = (double)t->nbr[e].nx, ny = (double)t->nbr[e].ny;
        if (fabs(a1 - a2) < 1e-4 && fabs(b1 - b2) < 1e-4)
            continue;
        if (a1 + b1 < a2 + b2)
            nx = -nx, ny = -ny;
        if (cliff)
        {
            /*  A CLIFF, where two tiles side by side are more than a level
             *  apart and the ground does not join (plans/terrain.md).  It
             *  is a broken cut through the strata of the land, which lie
             *  parallel to the top of the cut. */
            float  m[3];
            double step = max2(max2(fabs(za - na), fabs(zm - nm)), fabs(zb - nb));
            Cliff  f;
            memset(&f, 0, sizeof f);
            tile_point(t, 4 + e, 0, m);
            f.t = t, f.pa = pa, f.pb = pb;
            f.rows = (int)max2(2.0, ceil(step));
            f.ta = max2(a1, a2), f.fa = min2(a1, a2);
            f.tb = max2(b1, b2), f.fb = min2(b1, b2);
            f.nx = nx, f.ny = ny;
            f.mx = (double)m[0], f.my = (double)m[1];
            f.order = (double)t->order + FACING;
            f.parts = parts > 1 ? parts : 0;
            memcpy(f.col, col, sizeof f.col);
            cliff_face(&f);
        }
        else if (parts > 1)
            wall_cut(t, pa, pb, a1, b1, a2, b2, nx, ny, (double)t->order + FACING, col, parts);
        else
            wall(t, pa, pb, a1, b1, a2, b2, nx, ny, (double)t->order + FACING, col);
    }
    t->m->n_walls++;
}

/*  The cut edges of the map, all four of them always.  The mesh is built
 *  once and the camera can look at it from any side.  They go down to the
 *  floor of the map.  The cut is a SECTION through a larger world.  It
 *  shows the layers of the land under the surface before the lean of a
 *  cliff cut it.  Under every tile on dry land it shows topsoil, whatever
 *  stands on the tile.  Under water it shows the bed. */
static void tile_rim(const TileFan *t, int e)
{
    static int   gix_floor = -1;
    const double floor_z   = (double)geo_num(&gix_floor, "map_floor");
    const int    ia = t->nbr[e].ia, ib = t->nbr[e].ib;
    const double nx = (double)t->nbr[e].nx, ny = (double)t->nbr[e].ny, order = (double)t->order;
    float        sed[3];
    colour(sed, 0.0, 0.0, MAT_SEDIMENT);
    if (t->wet)
    {
        /*  Through the middle of the edge, as the surface and the bed
         *  run. */
        double bza = bed_z(t, ia), bzb = bed_z(t, ib), bzm = bed_z(t, 4 + e);
        float  gl[3];
        colour(gl, (double)t->z[0], 0.0, MAT_WATER);
        glass(t, ia, 4 + e, nx, ny, order, gl);
        glass(t, 4 + e, ib, nx, ny, order, gl);
        wall_r(t, ia, 4 + e, bza, bzm, floor_z, floor_z, nx, ny, order, sed, bza, bzm, 0.0, 0.0);
        wall_r(t, 4 + e, ib, bzm, bzb, floor_z, floor_z, nx, ny, order, sed, bzm, bzb, 0.0, 0.0);
    }
    else
    {
        /*  Through the middle of the edge, as the top runs. */
        double za = top_z(t, ia), zb = top_z(t, ib), zm = top_z(t, 4 + e);
        double ga = za, gm = zm, gb = zb, soil = 1.0;
        /*  A fine tile's top meets the rim in its own parts. */
        const int parts = t->fine / 2;
        if (t->kind == T_PAD || t->kind == T_PLANE)
        {
            float wl[3];
            colour(wl, 0.0, 0.0, MAT_ENG_WALL);
            ga = (double)tile_pad(t, ia), gm = (double)tile_pad(t, 4 + e), gb = (double)tile_pad(t, ib);
            if (parts > 1)
            {
                wall_cut(t, ia, 4 + e, za, zm, ga, gm, nx, ny, order, wl, parts);
                wall_cut(t, 4 + e, ib, zm, zb, gm, gb, nx, ny, order, wl, parts);
            }
            else
            {
                wall(t, ia, 4 + e, za, zm, ga, gm, nx, ny, order, wl);
                wall(t, 4 + e, ib, zm, zb, gm, gb, nx, ny, order, wl);
            }
        }
        if (parts > 1)
        {
            wall_r_cut(t, ia, 4 + e, ga, gm, floor_z, floor_z, nx, ny, order, sed, ga - lean_at(t, ia), gm - lean_at(t, 4 + e), soil, soil, parts);
            wall_r_cut(t, 4 + e, ib, gm, gb, floor_z, floor_z, nx, ny, order, sed, gm - lean_at(t, 4 + e), gb - lean_at(t, ib), soil, soil, parts);
        }
        else
        {
            wall_r(t, ia, 4 + e, ga, gm, floor_z, floor_z, nx, ny, order, sed, ga - lean_at(t, ia), gm - lean_at(t, 4 + e), soil, soil);
            wall_r(t, 4 + e, ib, gm, gb, floor_z, floor_z, nx, ny, order, sed, gm - lean_at(t, 4 + e), gb - lean_at(t, ib), soil, soil);
        }
    }
}

/*  ONE TILE OF THE GROUND, drawn (mesh_ground_fan gathers it). */
/*  A tile's eight-triangle top, with each edge a fine tile lies over cut
 *  into the fine tile's parts.  The parts lie on the same two triangles,
 *  so the surface is the same, and the two tiles share every point of
 *  the edge. */
static void top_split(const TileFan *t, float p[4][3], float mid[4][3], float ctr[3], const float col[3], const float *lean, float order)
{
    int e, k;
    for (e = 0; e < 4; ++e)
    {
        const int n = t->split[e] > 0 ? t->split[e] : 2;
        for (k = 0; k < n; ++k)
        {
            float a[3], b[3], w[3], tri[3][3];
            float ua = (float)k / (float)n, ub = (float)(k + 1) / (float)n;
            int   j;
            for (j = 0; j < 3; ++j)
            {
                a[j] = ua <= 0.5f ? p[EDGE_A[e]][j] + (mid[e][j] - p[EDGE_A[e]][j]) * ua * 2.0f
                                  : mid[e][j] + (p[EDGE_B[e]][j] - mid[e][j]) * (ua - 0.5f) * 2.0f;
                b[j] = ub <= 0.5f ? p[EDGE_A[e]][j] + (mid[e][j] - p[EDGE_A[e]][j]) * ub * 2.0f
                                  : mid[e][j] + (p[EDGE_B[e]][j] - mid[e][j]) * (ub - 0.5f) * 2.0f;
            }
            if (lean)
            {
                float la = ua <= 0.5f ? lean[EDGE_A[e]] + (lean[4 + e] - lean[EDGE_A[e]]) * ua * 2.0f
                                      : lean[4 + e] + (lean[EDGE_B[e]] - lean[4 + e]) * (ua - 0.5f) * 2.0f;
                float lb = ub <= 0.5f ? lean[EDGE_A[e]] + (lean[4 + e] - lean[EDGE_A[e]]) * ub * 2.0f
                                      : lean[4 + e] + (lean[EDGE_B[e]] - lean[4 + e]) * (ub - 0.5f) * 2.0f;
                w[0] = lean[8], w[1] = la, w[2] = lb;
            }
            /*  Turned the way put_top_dual turns them. */
            if ((a[0] - ctr[0]) * (b[1] - ctr[1]) - (a[1] - ctr[1]) * (b[0] - ctr[0]) < 0.0f)
            {
                float tmp[3], tl = w[1];
                memcpy(tmp, a, sizeof tmp), memcpy(a, b, sizeof a), memcpy(b, tmp, sizeof b);
                w[1] = w[2], w[2] = tl;
            }
            memcpy(tri[0], ctr, sizeof tri[0]), memcpy(tri[1], a, sizeof tri[1]), memcpy(tri[2], b, sizeof tri[2]);
            put_tri_lean(t->m, tri, NULL, order, col, NULL, NULL, lean ? w : NULL, t->kind == T_PAD && lean);
        }
    }
}

int mesh_ground_dry(const TileFan *t)
{
    int e;
    if (t->underground || t->wet || (t->kind != T_LAND && t->kind != T_PAD))
        return 0;
    for (e = 0; e < 4; ++e)
        if (!t->nbr[e].there && !t->nbr[e].rim)
            return 0;
    return 1;
}

/*  The height of a tile's eight-triangle top at a point inside it. */
static float top_at(const float p[4][3], const float mid[4][3], const float ctr[3], float x, float y)
{
    int e, h;
    for (e = 0; e < 4; ++e)
        for (h = 0; h < 2; ++h)
        {
            const float *a = h ? mid[e] : p[EDGE_A[e]], *b = h ? p[EDGE_B[e]] : mid[e];
            float        d = (a[1] - b[1]) * (ctr[0] - b[0]) + (b[0] - a[0]) * (ctr[1] - b[1]);
            float        wc, wa, wb;
            if (fabsf(d) < 1e-12f)
                continue;
            wc = ((a[1] - b[1]) * (x - b[0]) + (b[0] - a[0]) * (y - b[1])) / d;
            wa = ((b[1] - ctr[1]) * (x - b[0]) + (ctr[0] - b[0]) * (y - b[1])) / d;
            wb = 1.0f - wc - wa;
            if (wc >= -1e-5f && wa >= -1e-5f && wb >= -1e-5f)
                return wc * ctr[2] + wa * a[2] + wb * b[2];
        }
    return ctr[2];
}

void mesh_ground_tile_fine(const TileFan *t, int n, int fine, float (*height)(void *ud, float x, float y, float z), void *ud)
{
    float p[4][3], mid[4][3], ctr[3], *g;
    int   i, j, k;
    if (n < 2 || (n & 1))
        n = 8;
    for (k = 0; k < 4; ++k)
    {
        tile_point(t, k, 0, p[k]);
        tile_point(t, 4 + k, 0, mid[k]);
    }
    tile_point(t, 8, 0, ctr);
    g = (float *)malloc((size_t)(n + 1) * (size_t)(n + 1) * 3 * sizeof *g);
    if (!g)
        return;
    /*  Each point: on the top, then moved, unless it lies on an edge or a
     *  corner shared with a tile not drawn this way. */
    for (j = 0; j <= n; ++j)
        for (i = 0; i <= n; ++i)
        {
            float *o     = &g[3 * (j * (n + 1) + i)];
            int    dc    = i == 0 ? -1 : i == n ? 1
                                                : 0,
                   dr    = j == 0 ? -1 : j == n ? 1
                                                : 0,
                   moved = 1;
            o[0]         = (float)t->col + (float)i / (float)n;
            o[1]         = (float)t->row + (float)j / (float)n;
            o[2]         = top_at((const float (*)[3])p, (const float (*)[3])mid, ctr, o[0], o[1]);
            if (dc && dr)
                moved = (fine >> (4 + 3 * dr + dc)) & 1;
            else if (dc || dr)
                moved = (fine >> (4 + 3 * dr + dc)) & 1;
            if (moved && height)
                o[2] = height(ud, o[0], o[1], o[2]);
        }
    /*  Each cell in two triangles, its diagonal along the crease of the
     *  quarter it lies in: toward the corner of that quarter. */
    for (j = 0; j < n; ++j)
        for (i = 0; i < n; ++i)
        {
            const float *a = &g[3 * (j * (n + 1) + i)], *b = &g[3 * (j * (n + 1) + i + 1)];
            const float *c = &g[3 * ((j + 1) * (n + 1) + i + 1)], *d = &g[3 * ((j + 1) * (n + 1) + i)];
            int          along = (2 * i < n) == (2 * j < n);
            const float *q[2][3];
            int          h;
            if (along)
                q[0][0] = a, q[0][1] = b, q[0][2] = c, q[1][0] = a, q[1][1] = c, q[1][2] = d;
            else
                q[0][0] = a, q[0][1] = b, q[0][2] = d, q[1][0] = b, q[1][1] = c, q[1][2] = d;
            for (h = 0; h < 2; ++h)
            {
                float tri[3][3];
                int   flip = (q[h][1][0] - q[h][0][0]) * (q[h][2][1] - q[h][0][1]) -
                                 (q[h][1][1] - q[h][0][1]) * (q[h][2][0] - q[h][0][0]) <
                             0.0f;
                memcpy(tri[0], q[h][0], sizeof tri[0]);
                memcpy(tri[1], flip ? q[h][2] : q[h][1], sizeof tri[1]);
                memcpy(tri[2], flip ? q[h][1] : q[h][2], sizeof tri[2]);
                put_tri_lean(t->m, tri, NULL, t->order, t->land, NULL, NULL, NULL, 0);
            }
        }
    free(g);
    /*  The walls on its east and south edges, and the map's rim, cut to
     *  its parts. */
    if (t->nbr[E_E].there)
        tile_walls(t, E_E);
    if (t->nbr[E_S].there)
        tile_walls(t, E_S);
    for (k = 0; k < 4; ++k)
        if (t->nbr[k].rim)
            tile_rim(t, k);
}

void mesh_ground_tile(const TileFan *t)
{
    float p[4][3], mid[4][3], ctr[3], lean[9], col[3];
    int   k, e;
    /*  The underground view: the ground itself, the seabed under water,
     *  cut by its own slope code, and nothing else. */
    if (t->underground)
    {
        put_top(t->m, (const float (*)[3])t->bed, t->code, t->order, t->land, 0);
        return;
    }
    /*  The top.  A tile with water over it draws its surface and not its
     *  ground.  The surface is calm on a marina or a stream and alive
     *  elsewhere.  Every top is eight triangles through the middle of each
     *  edge, so the tiles on the two sides of an edge meet along it.  The
     *  lean of a cliff moved each point.  Where the lean cut the lip away,
     *  the ground shows the layers of the land.  Where it filled the foot,
     *  the ground shows scree. */
    if (t->wet)
        colour(col, (double)t->z[0], t->kind == T_PAD || t->xter >= 0x30 ? 1.0 : 0.0, MAT_SURFACE);
    else
        memcpy(col, t->land, sizeof col);
    for (k = 0; k < 9; ++k)
        lean[k] = (float)lean_at(t, k);
    for (k = 0; k < 4; ++k)
    {
        tile_point(t, k, 0, p[k]);
        tile_point(t, 4 + k, 0, mid[k]);
    }
    tile_point(t, 8, 0, ctr);
    if (t->split[0] || t->split[1] || t->split[2] || t->split[3])
        top_split(t, p, mid, ctr, col, lean, t->order);
    else
        put_top_dual(t->m, (const float (*)[3])p, (const float (*)[3])mid, ctr, t->order, col, t->kind == T_PAD, lean);

    /*  Under a water body: the seabed, and the land below the surface down
     *  to it on each side that is not water.  Water beside water shares
     *  the bed.  The land is in two halves, through the middle of the
     *  edge, and is seen from inside the water, so the face turns in. */
    if (t->wet)
    {
        const double under = (double)t->order - UNDER;
        float        earth[3];
        colour(col, (double)t->z[0], 0.0, MAT_SEABED);
        colour(earth, 0.0, 0.0, MAT_EARTH);
        for (k = 0; k < 4; ++k)
        {
            tile_point(t, k, 1, p[k]);
            tile_point(t, 4 + k, 1, mid[k]);
        }
        tile_point(t, 8, 1, ctr);
        if (t->split[0] || t->split[1] || t->split[2] || t->split[3])
            top_split(t, p, mid, ctr, col, NULL, (float)under);
        else
            put_top_dual(t->m, (const float (*)[3])p, (const float (*)[3])mid, ctr, (float)under, col, 0, NULL);
        for (e = E_N; e <= E_W; ++e)
        {
            int half;
            if (!t->nbr[e].there || t->nbr[e].sea)
                continue;
            for (half = 0; half < 2; ++half)
            {
                int    pa = half ? 4 + e : t->nbr[e].ia, pb = half ? t->nbr[e].ib : 4 + e;
                double za = top_z(t, pa), bza = bed_z(t, pa), zb = top_z(t, pb), bzb = bed_z(t, pb);
                if (bza < za - 1e-4 || bzb < zb - 1e-4)
                {
                    if (t->split[e] > 2)
                        wall_cut(t, pa, pb, za, zb, bza, bzb, -(double)t->nbr[e].nx, -(double)t->nbr[e].ny, under, earth, t->split[e] / 2);
                    else
                        wall(t, pa, pb, za, zb, bza, bzb, -(double)t->nbr[e].nx, -(double)t->nbr[e].ny, under, earth);
                }
            }
        }
    }

    /*  The walls, on the east and south edges. */
    if (t->nbr[E_E].there)
        tile_walls(t, E_E);
    if (t->nbr[E_S].there)
        tile_walls(t, E_S);

    for (e = E_N; e <= E_W; ++e)
        if (t->nbr[e].rim)
            tile_rim(t, e);
}
