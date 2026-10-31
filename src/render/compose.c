/*  compose.c: THE COMPOSITION, one build of the world.
 *
 *  A build hands its world here once.  The ground comes first: every
 *  tile's top settled, the field built from it, and every tile drawn.
 *  Then the zone tints over the ground, then the power lines over both.
 *  That is the painter's order the mesh is drawn in.  An edit's build
 *  then offers the pieces it changed to the edit show.
 *
 *  The arithmetic of the tints and the power lines is done in double and
 *  each value becomes a float where it goes into a face. */
#include "compose.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "draw/bands.h"
#include "draw/lamps.h"
#include "draw/rails.h"
#include "draw/roads.h"
#include "geo.h"
#include "incr.h"
#include "log.h"
#include "materials.h"
#include "mesh/emit.h"
#include "mesh/model.h"
#include "mesh/shapes.h"
#include "mesh/tile.h"
#include "prof.h"
#include "show.h"
#include "trees.h"

/*  The edges of a cell a part joins, a bit each. */
enum
{
    L_N = 1,
    L_E = 2,
    L_S = 4,
    L_W = 8
};

/*  Layer 0 names an edge by the cell across it.  L_N is the edge toward
 *  the row before, L_E the column after, L_S the row after and L_W the
 *  column before. */
static int edge_links(int edges)
{
    return (edges & 8 ? L_N : 0) | (edges & 4 ? L_E : 0) | (edges & 2 ? L_S : 0) | (edges & 1 ? L_W : 0);
}

/* ---- the numbers ---------------------------------------------------------- */

static float num(int *cache, const char *name)
{
    return geo_num(cache, name);
}

static double clamp01(double x)
{
    return x < 0.0 ? 0.0 : x > 1.0 ? 1.0
                                   : x;
}

/* ---- the zone tints --------------------------------------------------------- */

/*  How far over the ground a tint floats.  Where it sits in the stack, over
 *  the ground and under everything built on it, is the number zone_slot. */
static const double OVER = 0.02;

/*  How many straight pieces a quarter circle is drawn in. */
enum
{
    ARC_STEPS = 8
};

typedef struct
{
    double u, v;
} UV;

/*  A lot's edges, each naming the neighbor across it.  They are
 *  across v 0, u 1, v 1 and u 0, in that order. */
enum
{
    E_V0 = 0,
    E_U1,
    E_V1,
    E_U0
};
static const int EDGE[4][2] = {
    {0,  -1},
    {1,  0 },
    {0,  1 },
    {-1, 0 }
};

/*  Each corner of a lot: where it is, and the two edges that meet there. */
static const struct
{
    int u, v, a, b;
} CORNER[4] = {
    {0, 0, E_V0, E_U0},
    {1, 0, E_V0, E_U1},
    {1, 1, E_V1, E_U1},
    {0, 1, E_V1, E_U0}
};

static const RCity *s_c;

/*  Whether (col, row) is a lot of zone z.  A lot is zoned so, on
 *  plain ground, with nothing built on it past a small piece of a line. */
static int lot_of(int32_t col, int32_t row, int z)
{
    if (col < 0 || row < 0 || col >= r_map || row >= r_map)
        return 0;
    int32_t i = row * r_map + col;
    if (s_c->xzon[i] % 16 != z)
        return 0;
    return s_c->xbld[i] < 0x1D && s_c->map.xter[i] == 0;
}

/*  The points of a quarter circle about (cu, cv) of radius r.  It
 *  goes from the way (au, av) to the way (bu, bv), the short way round. */
static void quarter(double cu, double cv, double r, double au, double av, double bu, double bv, UV pts[ARC_STEPS + 1])
{
    double a0 = atan2(av, au), da = atan2(bv, bu) - a0;
    int    i;
    if (da > M_PI)
        da -= 2 * M_PI;
    if (da < -M_PI)
        da += 2 * M_PI;
    for (i = 0; i <= ARC_STEPS; ++i)
    {
        double a = a0 + da * i / (double)ARC_STEPS;
        pts[i].u = cu + r * cos(a);
        pts[i].v = cv + r * sin(a);
    }
}

/*  One cut of a convex polygon by one side of a box.  Side 0 keeps
 *  u >= at and 1 keeps u <= at.  Side 2 keeps v >= at and 3 keeps
 *  v <= at. */
static int cut(const UV *in, int n, int side, double at, UV *out)
{
    int k, m = 0;
    for (k = 0; k < n; ++k)
    {
        UV  a = in[k], b = in[(k + 1) % n];
        int ia = side == 0 ? a.u >= at : side == 1 ? a.u <= at
                                     : side == 2   ? a.v >= at
                                                   : a.v <= at;
        int ib = side == 0 ? b.u >= at : side == 1 ? b.u <= at
                                     : side == 2   ? b.v >= at
                                                   : b.v <= at;
        if (ia)
            out[m++] = a;
        if (ia != ib)
        {
            if (side < 2)
            {
                double f = (at - a.u) / (b.u - a.u);
                out[m].u = at, out[m].v = a.v + (b.v - a.v) * f;
            }
            else
            {
                double f = (at - a.v) / (b.v - a.v);
                out[m].u = a.u + (b.u - a.u) * f, out[m].v = at;
            }
            m++;
        }
    }
    return m;
}

/*  A convex polygon clipped to the box u0..u1, v0..v1. */
static int clip(const UV *poly, int n, double u0, double u1, double v0, double v1, UV *out)
{
    UV a[16], b[16];
    memcpy(a, poly, sizeof *poly * (size_t)n);
    n = cut(a, n, 0, u0, b);
    if (n > 0)
        n = cut(b, n, 1, u1, a);
    else
        return 0;
    if (n > 0)
        n = cut(a, n, 2, v0, b);
    else
        return 0;
    if (n > 0)
        n = cut(b, n, 3, v1, a);
    else
        return 0;
    memcpy(out, a, sizeof *a * (size_t)n);
    return n;
}

/*  What one lot's tint is drawn with. */
typedef struct
{
    TileFan *t;
    int32_t  col, row;
    int      zone;
    double   z, fill, edge, fill_zone;
} Lot;

/*  One flat triangle of a tint.  Over a lot it goes in the blended list,
 *  drawn after the opaque mesh, so what is under it shows through. */
static void tint_tri(TileFan *t, const double p[3][3], double zone, double chan, double order, int blended)
{
    float tri[3][3], col3[3], ref[3], ref2[3];
    int   i, was = t->m->to_water;
    for (i = 0; i < 3; ++i)
        tri[i][0] = (float)p[i][0], tri[i][1] = (float)p[i][1], tri[i][2] = (float)p[i][2];
    col3[0] = (float)zone, col3[1] = (float)chan, col3[2] = MAT_ZONE;
    ref[0] = ref[1] = ref[2] = col3[0];
    ref2[0] = ref2[1] = ref2[2] = col3[1];
    t->m->to_water              = blended;
    put_tri_r2(t->m, (const float (*)[3])tri, NULL, (float)order, col3, ref, ref2, 1);
    t->m->to_water = was;
}

static void lot_tri(const Lot *l, UV a, UV b, UV c, double chan, double zone, double order, double lift)
{
    double p[3][3] = {
        {l->col + a.u, l->row + a.v, l->z + lift},
        {l->col + b.u, l->row + b.v, l->z + lift},
        {l->col + c.u, l->row + c.v, l->z + lift}
    };
    tint_tri(l->t, p, zone, chan, order, 1);
}

static int lot_at(const Lot *l, int du, int dv)
{
    return lot_of(l->col + du, l->row + dv, l->zone);
}

/*  The tint of one tile.  A zoned tile that is not a lot gets one flat
 *  quad a hair over its ground.  A lot gets a fill with its outside
 *  corners rounded, and a border round the outside of its zoned area. */
static void zone_tint(TileFan *t)
{
    static int c_slot = -1, c_drawn = -1, c_fill = -1, c_lighten = -1, c_edge = -1, c_edge_w = -1, c_round = -1;
    double     p[4][3], order;
    int        k;
    if (t->zone == 0)
        return;
    for (k = 0; k < 4; ++k)
    {
        float q[3];
        tile_point(t, k, 0, q);
        p[k][0] = q[0], p[k][1] = q[1], p[k][2] = q[2];
    }
    order = (double)t->order + (double)num(&c_slot, "zone_slot");
    if (!(num(&c_drawn, "zones_drawn") > 0.5f && lot_of(t->col, t->row, t->zone)))
    {
        double a[3][3] = {
            {p[0][0], p[0][1], p[0][2] + OVER},
            {p[1][0], p[1][1], p[1][2] + OVER},
            {p[2][0], p[2][1], p[2][2] + OVER}
        };
        double b[3][3] = {
            {p[0][0], p[0][1], p[0][2] + OVER},
            {p[2][0], p[2][1], p[2][2] + OVER},
            {p[3][0], p[3][1], p[3][2] + OVER}
        };
        tint_tri(t, a, t->zone, 0.0, order, 0);
        tint_tri(t, b, t->zone, 0.0, order, 0);
        return;
    }
    Lot l;
    l.t = t, l.col = t->col, l.row = t->row, l.zone = t->zone;
    l.z = p[0][2];
    for (k = 1; k < 4; ++k)
        l.z = p[k][2] > l.z ? p[k][2] : l.z;
    l.fill      = 1.0 + clamp01(num(&c_fill, "zone_fill"));
    l.edge      = 3.0 + clamp01(num(&c_edge, "zone_edge"));
    l.fill_zone = l.zone + 0.4 * clamp01(num(&c_lighten, "zone_lighten"));
    double w    = num(&c_edge_w, "zone_edge_w");
    double r    = num(&c_round, "zone_round");
    r           = r < 0.5 ? r : 0.5;
    r           = w > r ? w : r;

    int out[4], kind[4] = {0, 0, 0, 0}, miss[4][2]; /* kind: 1 convex, 2 concave */
    for (k = 0; k < 4; ++k)
        out[k] = !lot_at(&l, EDGE[k][0], EDGE[k][1]);
    for (k = 0; k < 4; ++k)
    {
        int su = CORNER[k].u == 0 ? -1 : 1, sv = CORNER[k].v == 0 ? -1 : 1;
        if (out[CORNER[k].a] && out[CORNER[k].b])
            kind[k] = 1;
        else
        {
            int around[3][2] = {
                {EDGE[CORNER[k].a][0], EDGE[CORNER[k].a][1]},
                {EDGE[CORNER[k].b][0], EDGE[CORNER[k].b][1]},
                {su,                   sv                  }
            };
            int q, n = 0, gone = -1;
            for (q = 0; q < 3; ++q)
                if (!lot_at(&l, around[q][0], around[q][1]))
                    gone = q, n++;
            if (n == 1)
                kind[k] = 2, miss[k][0] = around[gone][0], miss[k][1] = around[gone][1];
        }
    }
    double o_fill = order, o_edge = order + 0.01, l_fill = OVER, l_edge = OVER + 0.002;

    /*  The fill: the tile, its convex corners rounded, as a fan about its
     *  middle. */
    UV  ring[4 * (ARC_STEPS + 1) + 1];
    int nr = 0, i;
    for (k = 0; k < 4; ++k)
    {
        if (kind[k] == 1)
        {
            double iu = CORNER[k].u == 0 ? 1 : -1, iv = CORNER[k].v == 0 ? 1 : -1;
            UV     pts[ARC_STEPS + 1];
            quarter(CORNER[k].u + iu * r, CORNER[k].v + iv * r, r, 0, -iv, -iu, 0, pts);
            for (i = 0; i <= ARC_STEPS; ++i)
                ring[nr++] = (k == 0 || k == 2) ? pts[ARC_STEPS - i] : pts[i];
        }
        else
            ring[nr].u = CORNER[k].u, ring[nr].v = CORNER[k].v, nr++;
    }
    ring[nr++] = ring[0];
    {
        UV hub = {0.5, 0.5};
        for (i = 0; i + 1 < nr; ++i)
            lot_tri(&l, hub, ring[i], ring[i + 1], l.fill, l.fill_zone, o_fill, l_fill);
    }

    /*  The corners' border, and the fillet a concave corner lays.  Each tile round a concave corner draws the stretch of its border
     *  that lies in its own square.  It draws it after its own fill, so no
     *  fill covers it.
     *  The tile across the corner from the missing one draws the fillet
     *  and the stretch in the missing tile too. */
    for (k = 0; k < 4; ++k)
    {
        double iu = CORNER[k].u == 0 ? 1 : -1, iv = CORNER[k].v == 0 ? 1 : -1;
        UV     inner[ARC_STEPS + 1], outer[ARC_STEPS + 1];
        if (kind[k] == 1)
        {
            double cu = CORNER[k].u + iu * r, cv = CORNER[k].v + iv * r;
            quarter(cu, cv, r - w, 0, -iv, -iu, 0, inner);
            quarter(cu, cv, r, 0, -iv, -iu, 0, outer);
            for (i = 0; i < ARC_STEPS; ++i)
            {
                lot_tri(&l, inner[i], outer[i], outer[i + 1], l.edge, l.zone, o_edge, l_edge);
                lot_tri(&l, inner[i], outer[i + 1], inner[i + 1], l.edge, l.zone, o_edge, l_edge);
            }
        }
        else if (kind[k] == 2)
        {
            int    mu = miss[k][0], mv = miss[k][1];
            double du = (mu + 0.5 - CORNER[k].u) > 0 ? 1 : -1, dv = (mv + 0.5 - CORNER[k].v) > 0 ? 1 : -1;
            double cu = CORNER[k].u + du * r, cv = CORNER[k].v + dv * r;
            double box[2][4] = {
                {0, 1, 0, 1},
                {0, 0, 0, 0}
            };
            int nbox = 1, b;
            if (mu != 0 && mv != 0)
            {
                /*  The fill stops under the middle of the border, so its
                 *  edge never shows against the ground. */
                UV pts[ARC_STEPS + 1], hub = {CORNER[k].u, CORNER[k].v};
                quarter(cu, cv, r + 0.5 * w, 0, -dv, -du, 0, pts);
                for (i = 0; i < ARC_STEPS; ++i)
                    lot_tri(&l, hub, pts[i], pts[i + 1], l.fill, l.fill_zone, o_fill, l_fill);
                box[1][0] = mu, box[1][1] = mu + 1, box[1][2] = mv, box[1][3] = mv + 1;
                nbox = 2;
            }
            quarter(cu, cv, r, 0, -dv, -du, 0, inner);
            quarter(cu, cv, r + w, 0, -dv, -du, 0, outer);
            for (i = 0; i < ARC_STEPS; ++i)
            {
                UV q[4] = {inner[i], outer[i], outer[i + 1], inner[i + 1]}, pc[16];
                for (b = 0; b < nbox; ++b)
                {
                    int n = clip(q, 4, box[b][0], box[b][1], box[b][2], box[b][3], pc), j;
                    for (j = 1; j + 1 < n; ++j)
                        lot_tri(&l, pc[0], pc[j], pc[j + 1], l.edge, l.zone, o_edge, l_edge);
                }
            }
        }
    }

    /*  The straight border along each edge that faces out.  An end gives
     *  way by the radius where a corner is rounded there, convex at this
     *  tile or concave at the next. */
#define GIVES(e, s) (out[s] ? r : lot_at(&l, EDGE[e][0] + EDGE[s][0], EDGE[e][1] + EDGE[s][1]) ? r \
                                                                                               : 0.0)
#define STRIP(u0, u1, v0, v1)                                      \
    do                                                             \
    {                                                              \
        UV a = {u0, v0}, b = {u1, v0}, c = {u1, v1}, d = {u0, v1}; \
        lot_tri(&l, a, b, c, l.edge, l.zone, o_edge, l_edge);      \
        lot_tri(&l, a, c, d, l.edge, l.zone, o_edge, l_edge);      \
    } while (0)
    if (out[E_V0])
        STRIP(GIVES(E_V0, E_U0), 1 - GIVES(E_V0, E_U1), 0, w);
    if (out[E_V1])
        STRIP(GIVES(E_V1, E_U0), 1 - GIVES(E_V1, E_U1), 1 - w, 1);
    if (out[E_U0])
        STRIP(0, w, GIVES(E_U0, E_V0), 1 - GIVES(E_U0, E_V1));
    if (out[E_U1])
        STRIP(1 - w, 1, GIVES(E_U1, E_V0), 1 - GIVES(E_U1, E_V1));
#undef GIVES
#undef STRIP
}

/* ---- the power lines -------------------------------------------------------- */

/*  The middle of each edge of a tile, in the order the links' bits name
 *  them. */
static const double MU[4] = {0.5, 1.0, 0.5, 0.0};
static const double MV[4] = {0.0, 0.5, 1.0, 0.5};

/*  A power line's tile.  It holds a pylon at the middle with a wire out to
 *  each edge it joins, where the neighbor's wire meets it.  Where it meets a road or a railway there is no pylon, since the
 *  way owns the ground.  The wire then spans the tile edge to edge. */
static void power(WorldFan *w, int32_t col, int32_t row)
{
    static int c_hang = -1, c_sag = -1, c_span = -1, pylon = -2;
    int32_t    idx = row * r_map + col;
    RPart      p[2];
    int        n, j, e, links = 0, meet = 0;
    n = rcity_parts(w->c->xbld[idx], w->c->xbit[idx], p);
    for (j = 0; j < n; ++j)
        if (p[j].net == RNET_POWER)
            links = edge_links(p[j].edges), meet = n > 1;
    if (!links)
        return;
    float  x = (float)col + 0.5f, y = (float)row + 0.5f;
    double hang = num(&c_hang, "wire_hang");
    shape_close(w->shape);
    w->shape = shape_open("power line at %d,%d%s", (int)col, (int)row, meet ? ", sharing the tile" : "");
    shape_tiles(&idx, 1, 1);
    shape_note("links\t%s%s%s%s", links & L_N ? "north " : "", links & L_E ? "east " : "", links & L_S ? "south " : "", links & L_W ? "west " : "");
    float order = tile_order(w->c, col, row, w->mask_bit);
    if (meet)
    {
        double sag = num(&c_span, "wire_span_sag");
        if ((links & 5) == 5)
            put_wire(w->m, w->c, w->mask_bit, order, x, (float)row, (float)hang, x, (float)(row + 1), (float)hang, (float)sag);
        else
            put_wire(w->m, w->c, w->mask_bit, order, (float)col, y, (float)hang, (float)(col + 1), y, (float)hang, (float)sag);
        return;
    }
    /*  A line running only along the rows turns its crossarm the other
     *  way. */
    int ns = (links & 5) == 5 && (links & 10) == 0;
    if (pylon == -2)
        pylon = net_model_find("pylon");
    net_model_put_on(pylon, w->m, w->c, w->mask_bit, order, x, y, ns ? 0.0f : 1.0f, ns ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0);
    for (e = 0; e < 4; ++e)
        if (links & (1 << e))
            put_wire(w->m, w->c, w->mask_bit, order, x, y, (float)hang, (float)(col + MU[e]), (float)(row + MV[e]), (float)hang, num(&c_sag, "wire_sag"));
}

/* ---- the build ---------------------------------------------------------------- */

/*  The steps a build names as it goes, for whatever is showing a bar, and
 *  the time each took, said once as the build ends. */
static struct
{
    const char *name;
    clock_t     at;
    int         i, n;
    char        said[160];
} s_step;

static void step(const char *name)
{
    clock_t now = clock();
    if (s_step.name)
    {
        size_t k = strlen(s_step.said);
        snprintf(s_step.said + k, sizeof s_step.said - k, "%s%s %.0f", k ? ", " : "", s_step.name, (double)(now - s_step.at) * 1000.0 / CLOCKS_PER_SEC);
    }
    s_step.name = name, s_step.at = now;
    if (name)
        prof_step(name, ++s_step.i, s_step.n);
}

/*  What an edit's build shows, offered in one phase.  A piece comes when the build made a triangle of it new, or it went
 *  and still stands.  It also comes when it changed within edit_show_come
 *  of the edit.  Going and leaving, nothing
 *  is marked. */
static void offer(int phase, RMesh *m)
{
    static int     c_come = -1;
    int            i;
    int32_t        box[4];
    const int32_t *edit = incr_window_edit() && incr_dirty_box(box) == 0 ? box : NULL;
    if (show_offer(phase, m, edit) < 0 || phase != SHOW_COMING)
        return;
    for (i = 0; i < show_count(); ++i)
    {
        float dist;
        int   same, back, kept;
        if (show_candidate(i, &dist, &same, &back, &kept) &&
            (back || kept || (!same && dist <= num(&c_come, "edit_show_come"))))
            show_mark(i);
    }
}

/* ---- the ground near a drawn street -------------------------------------- */

/*  The tiles whose ground is a fine grid, one byte a tile, and how many parts
 *  a side the grid has.  A tile is fine where it is near a drawn street and
 *  its ground is plain.  The ground of the four tiles round it must be plain
 *  too.  The decision reads every tile it needs whatever window is built, so
 *  every window decides alike. */
static uint8_t *s_fine;
static int32_t  s_fine_side;
static int      s_fine_n;

static int fine_at(int32_t col, int32_t row)
{
    return col >= 0 && row >= 0 && col < s_fine_side && row < s_fine_side && s_fine[row * s_fine_side + col];
}

/*  Whether two tiles meet with no wall between them along edge e of the
 *  first. */
static int edge_flush(const TileFan *t, int e)
{
    float a[3], b[3], c[3];
    tile_point(t, EDGE_A[e], 0, a);
    tile_point(t, EDGE_B[e], 0, b);
    tile_point(t, 4 + e, 0, c);
    return fabsf(t->nbr[e].za - a[2]) <= 1e-4f && fabsf(t->nbr[e].zb - b[2]) <= 1e-4f && fabsf(t->nbr[e].zm - c[2]) <= 1e-4f;
}

/*  Whether the edge between tile (col, row) and the tile over its edge e
 *  moves: both are fine and no wall stands between them. */
static int edge_moves(int32_t col, int32_t row, int e)
{
    TileFan t;
    if (!fine_at(col, row) || !fine_at(col + EDGE_DC[e], row + EDGE_DR[e]))
        return 0;
    return mesh_ground_fan_any(col, row, &t) && edge_flush(&t, e);
}

/*  Which of a tile's edges and corners move: bit (dr + 1) * 3 + (dc + 1). An
 *  edge moves where edge_moves says.  A corner moves only where the four
 *  tiles round it are fine and all four edges between them move.  So every
 *  tile that shares a point decides it alike. */
static int fine_round(int32_t col, int32_t row)
{
    int e, dr, dc, m = 0;
    for (e = 0; e < 4; ++e)
        if (edge_moves(col, row, e))
            m |= 1 << ((EDGE_DR[e] + 1) * 3 + (EDGE_DC[e] + 1));
    for (dr = -1; dr <= 1; dr += 2)
        for (dc = -1; dc <= 1; dc += 2)
        {
            /*  The four tiles round the corner: their top-left one, and
             *  the edges joining them. */
            int32_t c0 = dc < 0 ? col - 1 : col, r0 = dr < 0 ? row - 1 : row;
            int     ok = fine_at(c0, r0) && fine_at(c0 + 1, r0) && fine_at(c0, r0 + 1) && fine_at(c0 + 1, r0 + 1);
            int     k;
            for (k = 0; ok && k < 4; ++k)
            {
                /*  The edge between (c0, r0) and (c0 + 1, r0), (c0, r0) and
                 *  (c0, r0 + 1), and their two from (c0 + 1, r0 + 1). */
                static const int OFF[4][3] = {
                    {0, 0, 0},
                    {0, 0, 1},
                    {1, 1, 2},
                    {1, 1, 3}
                };
                int32_t c = c0 + OFF[k][0], r = r0 + OFF[k][1];
                int     e2, want              = OFF[k][2];
                /*  The tile's edge that runs toward the other: east, south,
                 *  west or north of it as the grid steps. */
                for (e2 = 0; e2 < 4; ++e2)
                    if ((want == 0 && EDGE_DC[e2] == 1 && EDGE_DR[e2] == 0) || (want == 1 && EDGE_DC[e2] == 0 && EDGE_DR[e2] == 1) ||
                        (want == 2 && EDGE_DC[e2] == -1 && EDGE_DR[e2] == 0) || (want == 3 && EDGE_DC[e2] == 0 && EDGE_DR[e2] == -1))
                        break;
                ok = e2 < 4 && edge_moves(c, r, e2);
            }
            if (ok)
                m |= 1 << ((dr + 1) * 3 + (dc + 1));
        }
    return m;
}

static float ground_height(void *ud, float x, float y, float z)
{
    (void)ud;
    return roads_ground(x, y, z);
}

static void fine_ground(WorldFan *w)
{
    static int c_grid = -1;
    TileFan    t;
    int32_t    col, row, k;
    if (s_fine_side != r_map)
    {
        free(s_fine);
        s_fine      = (uint8_t *)malloc((size_t)r_map * (size_t)r_map);
        s_fine_side = s_fine ? r_map : 0;
    }
    if (!s_fine_side)
        return;
    memset(s_fine, 0, (size_t)r_map * (size_t)r_map);
    if (w->underground || !roads_drawn())
        return;
    s_fine_n = (int)geo_num(&c_grid, "road_ground_grid");
    for (row = 0; row < r_map; ++row)
        for (col = 0; col < r_map; ++col)
        {
            /*  A tile any grade reaches is fine, whatever stands round
             *  it, so no grade stops at a tile's edge. */
            int ok                    = roads_near(col, row) && mesh_ground_fan_any(col, row, &t) && mesh_ground_dry(&t);
            s_fine[row * r_map + col] = (uint8_t)ok;
        }
    /*  A tile a drawn surface lies over whose ground could not be drawn
     *  under it: the surface and the ground there would fight. */
    {
        int32_t n = 0, first = -1;
        for (k = 0; k < r_map * r_map; ++k)
            if (roads_cover()[k] && !s_fine[k])
                first = first < 0 ? k : first, ++n;
        if (n)
            R_WARN("roads", "%d tiles under a drawn surface keep their ground at its height, the first at %d,%d", (int)n, (int)(first % r_map), (int)(first / r_map));
    }
}

int compose_world(WorldFan *w)
{
    static TileFan t;
    int32_t        col, row;
    int            tiles = 0;
    s_c                  = w->c;
    if (incr_window_edit())
        offer(SHOW_GOING, w->m);
    memset(&s_step, 0, sizeof s_step);
    s_step.n = 3 + !w->underground + (w->lines && !w->underground);

    /*  What the ground is, before anything reads a tile: where every
     *  tile's top comes from, and then the field built from it. */
    step("tops");
    if (roads_build(w->c, w->shaped) == 0)
    {
        /*  The highways' decks, on the tiles the streets are drawn on. */
        uint8_t       *drawn;
        const uint8_t *cover;
        int            side;
        if (roads_tiles(&drawn, &cover, &side) == 0)
            bands_build(w->c, drawn, cover, side);
    }
    mesh_tops_shaped(w->underground ? NULL : roads_drawn());
    /*  The field, once every tile's top is settled.  A water tile's bed is
     *  clamped under the surface drawn over it, and the tops say where
     *  that surface is. */
    mesh_tops_settle(w->c);
    build_field(w->c);

    /*  The ground, every tile of it, each in a shape of its own.  A tile an
     *  edit's build keeps is not gathered.  A tile near a drawn street is
     *  drawn as a fine grid graded to meet it (fine_ground). */
    step("ground");
    fine_ground(w);
    for (row = 0; row < r_map; ++row)
        for (col = 0; col < r_map; ++col)
            if (mesh_ground_fan(col, row, &t))
            {
                int e;
                shape_close(w->shape);
                w->shape = shape_open("ground at %d,%d", (int)col, (int)row);
                {
                    int32_t at = row * r_map + col;
                    shape_tiles(&at, 1, 1);
                }
                for (e = 0; e < 4; ++e)
                    t.split[e] = 0;
                t.fine = 0;
                if (s_fine && s_fine[row * r_map + col])
                {
                    t.fine      = s_fine_n;
                    w->m->range = MESH_RANGE_STANDS;
                    mesh_ground_tile_fine(&t, s_fine_n, fine_round(col, row), ground_height, NULL);
                    w->m->range = MESH_RANGE_GROUND;
                }
                else
                {
                    for (e = 0; e < 4; ++e)
                        if (s_fine && fine_at(col + EDGE_DC[e], row + EDGE_DR[e]))
                            t.split[e] = s_fine_n;
                    mesh_ground_tile(&t);
                }
                ++tiles;
            }
    shape_close(w->shape);
    w->shape = SHAPE_NONE;
    mesh_piece_is(0);

    /*  The zone tints, for the map view. */
    step("tints");
    for (row = 0; row < r_map; ++row)
        for (col = 0; col < r_map; ++col)
            if (mesh_tint_fan(col, row, &t))
            {
                shape_close(w->shape);
                w->shape = shape_open("zone tint at %d,%d", (int)col, (int)row);
                {
                    int32_t at = row * r_map + col;
                    shape_tiles(&at, 1, 1);
                }
                zone_tint(&t);
            }
    shape_close(w->shape);
    w->shape = SHAPE_NONE;
    mesh_piece_is(0);

    /*  The ground is done: what follows stands on it. */
    w->m->range = MESH_RANGE_STANDS;
    mesh_piece_is(0);

    /*  The lane graph view draws the network's lanes over the bare
     *  ground (build.c, net_overlay), so it lays no street surface. */
    if (!w->underground && !(TUNE(show_curves) > 0.5f))
    {
        step("streets");
        roads_draw(w->m, w->c, w->mask_bit);
        rails_draw(w->m, w->c, w->mask_bit);
        bands_draw(w->m, w->c, w->mask_bit);
        lamps_draw(w->m, w->c, w->mask_bit);
        mesh_piece_is(0);
    }
    if (w->lines && !w->underground)
    {
        step("power");
        for (row = 0; row < r_map; ++row)
            for (col = 0; col < r_map; ++col)
                if (mesh_wants_tile(col, row))
                    power(w, col, row);
        shape_close(w->shape);
        w->shape = SHAPE_NONE;
        mesh_piece_is(0);
    }
    /*  The trees of the forest tiles, over the surface. */
    if (!w->underground)
    {
        shape_close(w->shape);
        w->shape = SHAPE_NONE;
        if (trees_draw(w->m, w->c, w->mask_bit) != 0)
            return -1;
        mesh_piece_is(0);
    }
    shape_close(w->shape);
    w->shape = SHAPE_NONE;
    mesh_piece_is(0);
    step(NULL);
    prof_step("done", s_step.n, s_step.n);
    R_DBG("mesh", "build: %s ms | %d tiles", s_step.said, tiles);
    return 0;
}

void compose_shown(RMesh *m)
{
    offer(SHOW_LEAVING, m);
    offer(SHOW_COMING, m);
}
