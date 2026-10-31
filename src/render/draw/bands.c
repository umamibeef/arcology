/*  draw/bands.c: the highways drawn from the road network's records (bands.h).
 *
 *  A drawn highway road is swept along its line, one lane section at a
 *  time, as a closed slab.  At each station its cross-section runs from its
 *  rightmost lane's outer border to its leftmost lane's.  It is cut where
 *  the material of its lanes changes: a shoulder or a median is concrete,
 *  and every lane traffic runs on is asphalt.  Every border is a lane's
 *  border at that `s` (layer 2), so the deck eases where its lanes do.  The
 *  deck's thickness and the spacing of its stations are knobs in
 *  geo_defaults. */
#include "bands.h"
#include "materials.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "geo.h"
#include "laid.h"
#include "log.h"
#include "material.h"
#include "mesh/emit.h"
#include "mesh/shape.h"
#include "mesh/slab.h"
#include "mesh/surface.h"
#include "mesh/tile.h"
#include "network/network.h"
#include "network/shape.h"
#include "network/topology.h"

/*  The most lanes a cross-section reads, and the most stations one sweep
 *  lays. */
#define LANES_MOST 32
#define SWEEP_MOST 4096

static uint8_t *s_road;
static float   *s_cap; /* per road, its cap's length at its start and at its end, in tiles */
static int      s_nroad;

/*  The cap of a dead end that only a road of no units reaches.  It lies
 *  from the shared edge across the dead end's unit, with that road's
 *  lanes, at its height. */
typedef struct
{
    int   road;
    float x, y, h, len, z;
} Lone;

static Lone *s_lone;
static int   s_nlone;

/*  A drawn highway road's point, heading and height `s` along its line.
 *  Past either end of its line it runs on straight along its heading
 *  there, level, over its cap (plans/drawing.md, "A dead end"). */
static void deck_point(int road, float s, float *x, float *y, float *h, float *z)
{
    float L = shape_road(road)->length, at = s < 0.0f ? 0.0f : s > L ? L
                                                                     : s;
    shape_at(road, at, x, y, h);
    *z = shape_height(road, at);
    *x += cosf(*h) * (s - at), *y += sinf(*h) * (s - at);
}

/*  Whether everything on a highway tile is drawn: the highway, a wire, and
 *  a street or a railway that `cover` says is drawn there.  A ramp's tile
 *  is not one of them. */
static int tile_drawn(const RCity *c, int32_t cell, const uint8_t *cover)
{
    RPart p[2];
    int   n = rcity_parts(c->xbld[cell], c->xbit[cell], p), k, band = 0;
    for (k = 0; k < n; ++k)
    {
        if (p[k].net == RNET_BAND && p[k].kind != RPART_RAMP)
            band = 1;
        else if (p[k].net == RNET_BAND || (p[k].net != RNET_POWER && !cover[cell]))
            return 0;
    }
    return band;
}

/*  Set each of the four tiles of the highway unit at `cell` whose
 *  everything is drawn. */
static void unit_drawn(const RCity *c, int32_t cell, uint8_t *drawn, const uint8_t *cover, int side)
{
    int32_t col = cell % side, row = cell / side, dc, dr;
    for (dr = 0; dr < 2; ++dr)
        for (dc = 0; dc < 2; ++dc)
            if (col + dc < side && row + dr < side && tile_drawn(c, (row + dr) * side + col + dc, cover))
                drawn[(row + dr) * side + col + dc] = 1;
}

/*  The first cell of the highway unit a ramp's tile joins, else -1.  The
 *  highway's piece of the ramp names the edge toward it.  An edge is a bit:
 *  1 toward col - 1, 2 toward row + 1, 4 toward col + 1, 8 toward row - 1. */
static int32_t ramp_unit(const RCity *c, int32_t cell, int side)
{
    RPart   p[2];
    int     n   = rcity_parts(c->xbld[cell], c->xbit[cell], p), k;
    int32_t col = cell % side, row = cell / side;
    for (k = 0; k < n; ++k)
        if (p[k].net == RNET_BAND && p[k].kind == RPART_RAMP)
        {
            int32_t cc = col - !!(p[k].edges & 1) + !!(p[k].edges & 4), rr = row + !!(p[k].edges & 2) - !!(p[k].edges & 8);
            if (cc < 0 || rr < 0 || cc >= side || rr >= side)
                return -1;
            return (rr & ~1) * side + (cc & ~1);
        }
    return -1;
}

/*  Whether a road's line stands `pier_least` or more over the ground
 *  anywhere along it. */
static int raised(const RCity *c, int road)
{
    static int  c_least = -1;
    const float least = geo_num(&c_least, "pier_least"), L = shape_road(road)->length;
    float       s;
    (void)c;
    for (s = 0.0f; s <= L; s += 0.1f)
    {
        float x, y, h;
        shape_at(road, s, &x, &y, &h);
        if (shape_height(road, s) - (float)shape_land_at(x, y) >= least)
            return 1;
    }
    return 0;
}

int bands_build(const RCity *c, uint8_t *drawn, const uint8_t *cover, int side)
{
    int i, e, k;
    free(s_road);
    laid_decks(NULL, 0);
    s_nroad = network_roads();
    if ((s_road = (uint8_t *)calloc((size_t)s_nroad + 1, 1)) == NULL)
        return -1;
    laid_decks(s_road, s_nroad);
    /*  Every highway road with a line is a deck.  So is every other
     *  drawn road that stands raised (plans/drawing.md, "A lane is drawn
     *  the same wherever it is"). */
    for (i = 0; i < s_nroad; ++i)
        s_road[i] = shape_road(i)->ngeom > 0 && (network_road(i)->net == RNET_BAND || (laid_road(i) && raised(c, i)));
    /*  Each drawn highway road's cap, where an end of it meets a highway's
     *  dead end.  It reaches the far side of the dead end's unit, and the
     *  unit's tiles are the highway's. */
    free(s_cap);
    if ((s_cap = (float *)calloc((size_t)s_nroad * 2 + 1, sizeof *s_cap)) == NULL)
        return -1;
    for (i = 0; i < s_nroad; ++i)
        for (e = 0; e < 2 && s_road[i]; ++e)
        {
            const NLink   *l = e ? &network_road(i)->succ : &network_road(i)->pred;
            const TopoEnd *te;
            float          x, y, h, z, dx, dy, far = 0.0f;
            int32_t        cell;
            if (l->kind != NLINK_JUNCTION || l->index < 0)
                continue;
            te = topology_end(network_junction(l->index)->end);
            if (te->net != RNET_BAND || te->kind != END_DEAD || te->ncell < 1)
                continue;
            deck_point(i, e ? shape_road(i)->length : 0.0f, &x, &y, &h, &z);
            dx = e ? cosf(h) : -cosf(h), dy = e ? sinf(h) : -sinf(h);
            cell = topology_cell(te->cell0);
            for (k = 0; k < 4; ++k)
            {
                float d = ((float)(cell % side + 2 * (k & 1)) - x) * dx + ((float)(cell / side + (k & 2)) - y) * dy;
                far     = d > far ? d : far;
            }
            s_cap[2 * i + e] = far;
            unit_drawn(c, cell, drawn, cover, side);
        }
    /*  Each dead end that only a road of no units reaches.  Its cap runs from
     *  the middle of the edge it shares with the unit across that road to
     *  the middle of its far side. */
    free(s_lone), s_lone = NULL, s_nlone = 0;
    if ((s_lone = (Lone *)malloc(sizeof *s_lone * (size_t)(2 * s_nroad + 1))) == NULL)
        return -1;
    for (i = 0; i < s_nroad; ++i)
    {
        const NRoad *r = network_road(i);
        if (r->net != RNET_BAND || r->topo < 0 || topology_road(r->topo)->nunit != 0 || r->nsec < 1 || r->pred.kind != NLINK_JUNCTION ||
            r->succ.kind != NLINK_JUNCTION || r->pred.index < 0 || r->succ.index < 0)
            continue;
        for (e = 0; e < 2; ++e)
        {
            const TopoEnd *te = topology_end(network_junction(e ? r->succ.index : r->pred.index)->end);
            const TopoEnd *to = topology_end(network_junction(e ? r->pred.index : r->succ.index)->end);
            int32_t        cell, best = -1;
            float          cx, cy, ax = 0.0f, ay = 0.0f, z = shape_junction_height(e ? r->succ.index : r->pred.index);
            if (te->net != RNET_BAND || te->kind != END_DEAD || te->ncell < 1 || isnan(z))
                continue;
            cell = topology_cell(te->cell0);
            cx = (float)(cell % side) + 1.0f, cy = (float)(cell / side) + 1.0f;
            /*  The unit across the shared edge is the one beside it. */
            for (k = 0; k < to->ncell && best < 0; ++k)
            {
                int32_t o = topology_cell(to->cell0 + k), dc = o % side - cell % side, dr = o / side - cell / side;
                if ((dc == 0 && (dr == 2 || dr == -2)) || (dr == 0 && (dc == 2 || dc == -2)))
                    best = o, ax = -(float)dc * 0.5f, ay = -(float)dr * 0.5f;
            }
            if (best < 0)
                continue;
            s_lone[s_nlone++] = (Lone){i, cx - ax, cy - ay, atan2f(ay, ax), 2.0f, z};
            unit_drawn(c, cell, drawn, cover, side);
        }
    }
    /*  A highway road's units are replaced where it is drawn. */
    for (i = 0; i < s_nroad; ++i)
    {
        const NRoad    *r = network_road(i);
        const TopoRoad *t;
        if (!s_road[i] || r->topo < 0)
            continue;
        t = topology_road(r->topo);
        for (k = 0; k < t->nunit; ++k)
            unit_drawn(c, topology_cell(t->unit0 + k), drawn, cover, side);
    }
    /*  An end with a highway's units is replaced where every road of its
     *  junctions has a line.  A placeholder keeps its art.  A dead end is
     *  replaced by its road's cap, above.  An end that holds a ramp
     *  holds units of two networks, and only the highway's tiles are
     *  replaced. */
    for (e = 0; e < topology_ends(); ++e)
    {
        const TopoEnd *te  = topology_end(e);
        int            all = 1;
        if ((te->net != RNET_BAND && te->net != -1) || te->kind == END_DEAD || shape_placeholder(e))
            continue;
        for (i = 0; i < s_nroad && all; ++i)
        {
            const NRoad *r = network_road(i);
            if (r->junction < 0 || network_junction(r->junction)->end != e)
                continue;
            all = shape_road(i)->ngeom > 0;
        }
        for (k = 0; all && k < te->ncell; ++k)
            unit_drawn(c, topology_cell(te->cell0 + k), drawn, cover, side);
    }
    /*  A ramp's tile is the highway's.  Its art goes where the highway's
     *  unit it joins is drawn, as the highway's strand or its gore's lane
     *  runs over it. */
    for (k = 0; k < side * side; ++k)
    {
        int32_t u = ramp_unit(c, k, side);
        if (u >= 0 && drawn[u])
            drawn[k] = 1;
    }
    {
        int n = 0, t = 0;
        for (i = 0; i < s_nroad; ++i)
            n += s_road[i];
        for (k = 0; k < side * side; ++k)
            t += drawn[k] && (tile_drawn(c, k, cover) || ramp_unit(c, k, side) >= 0);
        R_DBG("bands", "highway roads drawn %d, tiles whose art the decks replace %d", n, t);
    }
    return 0;
}

/*  The materials of a deck: its asphalt where traffic runs, its shoulder,
 *  its median, its sidewalk, and the concrete of its sides and underside.
 *  The asphalt and the sidewalk are every road's (roads.h). */
typedef struct
{
    float asphalt, shoulder, median, sidewalk, side;
} DeckMats;

/*  The material of a lane, by its type alone. */
static float lane_material(int type, const DeckMats *dm)
{
    return type == NLANE_SHOULDER ? dm->shoulder : type == NLANE_MEDIAN ? dm->median
                                               : type == NLANE_SIDEWALK ? dm->sidewalk
                                                                        : dm->asphalt;
}

/*  The borders across lane section `sec` of a road, `ds` units into it.
 *  They run from the right edge to the left, cut only where the material
 *  changes.  It
 *  puts the borders' t in `t` and each strip's material in `mat`.  Answers
 *  how many borders, nought where the section has no lane. */
static int borders(int sec, float ds, const DeckMats *dm, float *t, float *mat)
{
    const NSection *x = network_section(sec);
    int             k, n = 0;
    /*  Its lanes run from the leftmost down to the rightmost, so they are
     *  read from the last.  A lane of the material before it widens the
     *  strip before it. */
    for (k = x->nlane - 1; k >= 0 && n < LANES_MOST; --k)
    {
        const NLane *l = network_lane(x->lane0 + k);
        float        t_in, t_out, m = lane_material(l->type, dm);
        network_lane_t(x->lane0 + k, ds, &t_in, &t_out);
        if (n == 0)
            t[n++] = t_in < t_out ? t_in : t_out;
        if (n >= 2 && mat[n - 2] == m)
            t[n - 1] = t_in < t_out ? t_out : t_in;
        else
            mat[n - 1] = m, t[n++] = t_in < t_out ? t_out : t_in;
    }
    return n;
}

/*  The borders across lane section `sec` `ds` units into it, one strip a
 *  lane, from the right edge to the left.  A lane with no width anywhere
 *  in the section lays no strip.  It puts each strip's material in `mat`
 *  and its lane in `lane`.  Answers how many borders. */
static int lane_borders(int sec, float ds, const DeckMats *dm, float *t, float *mat, int *lane)
{
    const NSection *x = network_section(sec);
    int             k, n = 0;
    for (k = x->nlane - 1; k >= 0 && n < LANES_MOST; --k)
    {
        const NLane *l = network_lane(x->lane0 + k);
        float        t_in, t_out;
        if (!network_lane_wide(x->lane0 + k))
            continue;
        network_lane_t(x->lane0 + k, ds, &t_in, &t_out);
        if (n == 0)
            t[n++] = t_in < t_out ? t_in : t_out;
        mat[n - 1] = lane_material(l->type, dm), lane[n - 1] = x->lane0 + k;
        t[n++] = t_in < t_out ? t_out : t_in;
    }
    return n;
}

/*  One lane section of a road as a closed slab, from its `s` s0 to s1
 *  along the line.  Each lane's strip of its top is a shape of its own,
 *  under the road's, that names the lane (NREF_LANE). */
static void deck_section(RMesh *m, const RCity *c, uint8_t mask_bit, int road, int sec, float s0, float s1, float step, float thick, const DeckMats *dm)
{
    const NSection *x = network_section(sec);
    float           t[LANES_MOST + 1], mat[LANES_MOST], *pts;
    int             lane[LANES_MOST], nb, ns, k, e;
    uint32_t        part[LANES_MOST];
    if (s1 - s0 < 1e-4f || (nb = lane_borders(sec, 0.0f, dm, t, mat, lane)) < 2)
        return;
    ns = (int)ceilf((s1 - s0) / step) + 1;
    ns = ns < 2 ? 2 : ns > SWEEP_MOST ? SWEEP_MOST
                                      : ns;
    if ((pts = (float *)malloc(sizeof *pts * 3 * (size_t)nb * (size_t)ns)) == NULL)
        return;
    for (e = 0; e + 1 < nb; ++e)
    {
        const NLane *l = network_lane(lane[e]);
        part[e]        = shape_open("lane %d of section %d", (int)l->id, sec - network_road(road)->sec0);
        shape_ref(NREF_LANE, NREF_LANE_KEY(sec - network_road(road)->sec0, l->id));
        shape_close(part[e]);
    }
    for (k = 0; k < ns; ++k)
    {
        float s = s0 + (s1 - s0) * (float)k / (float)(ns - 1), px, py, h, z, L = shape_road(road)->length;
        deck_point(road, s, &px, &py, &h, &z);
        lane_borders(sec, shape_units(road, s < 0.0f ? 0.0f : s > L ? L
                                                                    : s) -
                              x->s,
                     dm,
                     t,
                     mat,
                     lane);
        for (e = 0; e < nb; ++e)
        {
            float *q = pts + 3 * ((size_t)k * (size_t)nb + (size_t)e);
            q[0] = px - sinf(h) * t[e], q[1] = py + cosf(h) * t[e], q[2] = z;
        }
    }
    slab_sweep_parts(m, c, mask_bit, pts, ns, nb, thick, mat, dm->side, part);
    free(pts);
}

/*  A column turned to heading `h`, `half` its half width, its top at
 *  `top`.  It stands on the ground under its middle, open there, in the
 *  painter's slot of the tile under it.  -1 on a mesh error. */
static int column_box(RMesh *m, const RCity *c, uint8_t mask_bit, float x, float y, float h, float half, float top)
{
    float              ux = cosf(h), uy = sinf(h), vx = -uy, vy = ux, col[3] = {0.0f, 0.0f, MAT_PIER}, ref[3] = {0.0f, 0.0f, 0.0f};
    float              lo = surface_at_world(c, mask_bit, x, y), order = tile_order(c, (int32_t)floorf(x), (int32_t)floorf(y), mask_bit);
    float              t[4][3], b[4][3], tri[3][3];
    int                k;
    static const float SA[4] = {-1.0f, 1.0f, 1.0f, -1.0f}, SC[4] = {-1.0f, -1.0f, 1.0f, 1.0f};
    for (k = 0; k < 4; ++k)
    {
        t[k][0] = b[k][0] = x + (ux * SA[k] + vx * SC[k]) * half;
        t[k][1] = b[k][1] = y + (uy * SA[k] + vy * SC[k]) * half;
        t[k][2] = top, b[k][2] = lo;
    }
    /*  Each side faces out from the middle, and the top faces up. */
    for (k = 0; k < 4; ++k)
    {
        int   j  = (k + 1) & 3;
        float mx = 0.5f * (t[k][0] + t[j][0]) - x, my = 0.5f * (t[k][1] + t[j][1]) - y, l = hypotf(mx, my);
        float nrm[3] = {l > 0.0f ? mx / l : 0.0f, l > 0.0f ? my / l : 0.0f, 0.0f};
        if (put_wall_r(m, t[k], t[j], b[k], b[j], nrm, order, col, 0.0f, 0.0f) != 0)
            return -1;
    }
    memcpy(tri[0], t[0], sizeof tri[0]), memcpy(tri[1], t[1], sizeof tri[1]), memcpy(tri[2], t[2], sizeof tri[2]);
    if (put_tri_r2(m, (const float (*)[3])tri, NULL, order, col, ref, ref, 0) != 0)
        return -1;
    memcpy(tri[1], t[2], sizeof tri[1]), memcpy(tri[2], t[3], sizeof tri[2]);
    return put_tri_r2(m, (const float (*)[3])tri, NULL, order, col, ref, ref, 0) != 0 ? -1 : 0;
}

/*  How far under the deck's underside a column's top stops, in levels.
 *  So the column and the deck do not meet face to face. */
#define COLUMN_GAP 0.002f

/*  A sample of a drawn road's line, which a column must stand clear of.
 *  It holds its point, its heading's normal, its height and its half
 *  width. */
typedef struct
{
    float x, y, nx, ny, z, half, s;
    int   road;
} Foot;

static Foot *s_foot;
static int  *s_foot0, s_nfoot; /* per tile, where its samples start in s_foot, a tile a side */

/*  Every drawn road's samples, by the tile they lie on: the highways, and
 *  the streets and railways roads.c draws. */
static int feet_build(int side, float step)
{
    int   i, n = 0, k, cap = 0;
    float s;
    free(s_foot), free(s_foot0), s_foot = NULL, s_foot0 = NULL, s_nfoot = 0;
    for (i = 0; i < network_roads(); ++i)
        if (shape_road(i)->ngeom && ((s_road && i < s_nroad && s_road[i]) || laid_road(i)))
            cap += (int)(shape_road(i)->length / step) + 2;
    if ((s_foot = (Foot *)malloc(sizeof *s_foot * (size_t)(cap + 1))) == NULL ||
        (s_foot0 = (int *)calloc((size_t)side * (size_t)side + 1, sizeof *s_foot0)) == NULL)
        return -1;
    for (i = 0; i < network_roads(); ++i)
    {
        float L = shape_road(i)->length;
        if (!shape_road(i)->ngeom || !((s_road && i < s_nroad && s_road[i]) || laid_road(i)))
            continue;
        for (s = 0.0f; s <= L + 1e-4f && n < cap; s += step)
        {
            float x, y, h;
            shape_at(i, s < L ? s : L, &x, &y, &h);
            s_foot[n++] = (Foot){x, y, -sinf(h), cosf(h), shape_height(i, s < L ? s : L), 0.5f * shape_width(i, s < L ? s : L), s, i};
        }
    }
    /*  Sorted by tile: a count a tile, then the starts, then the samples. */
    {
        Foot *sorted = (Foot *)malloc(sizeof *sorted * (size_t)(n + 1));
        int  *at     = (int *)calloc((size_t)side * (size_t)side + 1, sizeof *at);
        if (!sorted || !at)
        {
            free(sorted), free(at);
            return -1;
        }
        for (k = 0; k < n; ++k)
        {
            int32_t col = (int32_t)floorf(s_foot[k].x), row = (int32_t)floorf(s_foot[k].y);
            if (col >= 0 && row >= 0 && col < side && row < side)
                s_foot0[row * side + col + 1]++;
        }
        for (k = 0; k < side * side; ++k)
            s_foot0[k + 1] += s_foot0[k];
        for (k = 0; k < n; ++k)
        {
            int32_t col = (int32_t)floorf(s_foot[k].x), row = (int32_t)floorf(s_foot[k].y);
            if (col >= 0 && row >= 0 && col < side && row < side)
                sorted[s_foot0[row * side + col] + at[row * side + col]++] = s_foot[k];
        }
        free(s_foot), free(at);
        s_foot = sorted, s_nfoot = s_foot0[side * side];
    }
    return 0;
}

/*  Whether a column at (x, y) holding road `own` up at `top`, `at` along
 *  it, meets a road.  That is a road not clearly over it, within `clear`
 *  of its width, beside one of its samples.  Its own road meets it only
 *  away from where it holds it. */
static int column_blocked(int side, float x, float y, float top, int own, float at, float half, float clear)
{
    int32_t col = (int32_t)floorf(x), row = (int32_t)floorf(y), dc, dr;
    int     k;
    for (dr = -1; dr <= 1; ++dr)
        for (dc = -1; dc <= 1; ++dc)
        {
            int32_t cc = col + dc, rr = row + dr;
            if (cc < 0 || rr < 0 || cc >= side || rr >= side)
                continue;
            for (k = s_foot0[rr * side + cc]; k < s_foot0[rr * side + cc + 1]; ++k)
            {
                const Foot *f  = &s_foot[k];
                float       dx = x - f->x, dy = y - f->y, t = dx * f->nx + dy * f->ny, along = fabsf(dx * f->ny - dy * f->nx);
                if ((f->road == own && fabsf(f->s - at) < 0.5f) || f->z > top + 0.3f)
                    continue;
                if (along < 0.06f && fabsf(t) < f->half + half + clear)
                    return 1;
            }
        }
    return 0;
}

/*  Where a deck's columns stand across it, as t: under the middle of each
 *  way's traffic where it has a median, else under its middle.  Answers
 *  how many. */
static int column_offsets(const float *t, const float *mat, int nb, const DeckMats *dm, float tc[2])
{
    int w, e, nc = 0;
    for (w = 0; w < 2; ++w)
    {
        float sum = 0.0f;
        int   cnt = 0, med = 0;
        for (e = 0; e + 1 < nb; ++e)
        {
            float mid = 0.5f * (t[e] + t[e + 1]);
            med |= mat[e] == dm->median;
            if (mat[e] == dm->asphalt && (w ? mid < 0.0f : mid > 0.0f))
                sum += mid, cnt++;
        }
        if (med && cnt)
            tc[nc++] = sum / (float)cnt;
    }
    if (!nc)
        tc[nc++] = 0.5f * (t[0] + t[nb - 1]);
    return nc;
}

/*  A HIGHWAY'S COLUMNS (plans/drawing.md, "A highway stands on
 *  columns").  One every `pier_step` along every drawn highway road,
 *  under the middle of the traffic of each way, where the deck stands
 *  `pier_least` over the ground.  A column that would stand on a lower
 *  road moves along its own road to clear it, a tenth of a tile at a
 *  time.  It moves half a tile at most, or is left out. */
static void columns(RMesh *m, const RCity *c, uint8_t mask_bit, float thick, const DeckMats *dm)
{
    static int         c_col = -1, c_step = -1, c_least = -1, c_clear = -1;
    static const float SHIFT[11] = {0.0f, 0.1f, -0.1f, 0.2f, -0.2f, 0.3f, -0.3f, 0.4f, -0.4f, 0.5f, -0.5f};
    const float        half = geo_num(&c_col, "pier_column"), step = geo_num(&c_step, "pier_step");
    const float        least = geo_num(&c_least, "pier_least"), clear = geo_num(&c_clear, "pier_clear");
    int                side = c->n, i;
    if (feet_build(side, 0.08f) != 0)
        return;
    for (i = 0; i < s_nroad && s_road; ++i)
    {
        float   L = shape_road(i)->length, s0, x0, y0, h0;
        ShapeId id;
        if (!s_road[i] || !shape_road(i)->ngeom)
            continue;
        shape_at(i, 0.0f, &x0, &y0, &h0);
        id = shape_open("highway columns, road %016llx from %d,%d", (unsigned long long)network_road(i)->id, (int)floorf(x0), (int)floorf(y0));
        laid_ref_road(i);
        /*  Along its line and its caps. */
        const float a0 = -s_cap[2 * i], a1 = L + s_cap[2 * i + 1];
        for (s0 = a0 + 0.5f * step; s0 < a1; s0 += step)
        {
            float t[LANES_MOST + 1], mat[LANES_MOST], tc[2], sl = s0 < 0.0f ? 0.0f : s0 > L ? L
                                                                                            : s0;
            int   nb, sec = network_section_at(i, shape_units(i, sl)), nc, w;
            if (sec < 0 || (nb = borders(sec, shape_units(i, sl) - network_section(sec)->s, dm, t, mat)) < 2)
                continue;
            nc = column_offsets(t, mat, nb, dm, tc);
            for (w = 0; w < nc; ++w)
            {
                int k;
                for (k = 0; k < 11; ++k)
                {
                    float s = s0 + SHIFT[k], x, y, h, z, px, py;
                    if (s < a0 + 0.05f || s > a1 - 0.05f)
                        continue;
                    deck_point(i, s, &x, &y, &h, &z);
                    px = x - sinf(h) * tc[w], py = y + cosf(h) * tc[w];
                    if (z - surface_at_world(c, mask_bit, px, py) < least || column_blocked(side, px, py, z, i, s, half, clear))
                        continue;
                    /*  Its top meets the underside where that is lowest over
                     *  it, as the deck slopes along its road. */
                    {
                        float za, zb, qx, qy, qh;
                        deck_point(i, s - half, &qx, &qy, &qh, &za);
                        deck_point(i, s + half, &qx, &qy, &qh, &zb);
                        column_box(m, c, mask_bit, px, py, h, half, (za < zb ? za : zb) - thick - COLUMN_GAP);
                    }
                    break;
                }
            }
        }
        shape_close(id);
    }
    /*  And under each lone cap's middle. */
    for (i = 0; i < s_nlone; ++i)
    {
        const Lone *lc = &s_lone[i];
        float       t[LANES_MOST + 1], mat[LANES_MOST], tc[2], mx = lc->x + cosf(lc->h) * lc->len * 0.5f, my = lc->y + sinf(lc->h) * lc->len * 0.5f;
        int         nb = borders(network_road(lc->road)->sec0, 0.0f, dm, t, mat), nc, w;
        ShapeId     id;
        if (nb < 2)
            continue;
        id = shape_open("highway columns, the cap at %d,%d", (int)floorf(mx), (int)floorf(my));
        laid_ref_road(lc->road);
        nc = column_offsets(t, mat, nb, dm, tc);
        for (w = 0; w < nc; ++w)
        {
            float px = mx - sinf(lc->h) * tc[w], py = my + cosf(lc->h) * tc[w];
            if (lc->z - surface_at_world(c, mask_bit, px, py) >= least && !column_blocked(side, px, py, lc->z, -1, 0.0f, half, clear))
                column_box(m, c, mask_bit, px, py, lc->h, half, lc->z - thick - COLUMN_GAP);
        }
        shape_close(id);
    }
    free(s_foot), free(s_foot0), s_foot = NULL, s_foot0 = NULL;
}

void bands_draw(RMesh *m, const RCity *c, uint8_t mask_bit)
{
    static const float SHOULDER[3] = {0.604f, 0.627f, 0.639f};
    static const float MEDIAN[3] = {0.788f, 0.765f, 0.702f}, SIDE[3] = {0.718f, 0.737f, 0.749f};
    static int         c_step = -1, c_thick = -1;
    const float        step = geo_num(&c_step, "road_step"), thick = geo_num(&c_thick, "deck_thick");
    DeckMats           dm;
    int                i, k;
    laid_materials(&dm.asphalt, &dm.sidewalk);
    dm.shoulder = material_define("deck shoulder", SHOULDER, 0.95f);
    dm.median = material_define("deck median", MEDIAN, 0.95f), dm.side = material_define("deck side", SIDE, 0.95f);
    for (i = 0; i < s_nroad && s_road; ++i)
    {
        const NRoad *r = network_road(i);
        float        x, y, h;
        ShapeId      id;
        if (!s_road[i] || !laid_near(i))
            continue;
        shape_at(i, 0.0f, &x, &y, &h);
        id = shape_open("highway, road %016llx from %d,%d", (unsigned long long)network_road(i)->id, (int)floorf(x), (int)floorf(y));
        laid_ref_road(i);
        /*  Each lane section from where it starts to where the next does,
         *  along the line. */
        for (k = 0; k < r->nsec; ++k)
        {
            float u0 = network_section(r->sec0 + k)->s, u1 = k + 1 < r->nsec ? network_section(r->sec0 + k + 1)->s : r->length;
            deck_section(m, c, mask_bit, i, r->sec0 + k, shape_s(i, u0) - (k == 0 ? s_cap[2 * i] : 0.0f), shape_s(i, u1) + (k + 1 == r->nsec ? s_cap[2 * i + 1] : 0.0f), step, thick, &dm);
        }
        shape_close(id);
    }
    for (i = 0; i < s_nlone; ++i)
    {
        const Lone *lc = &s_lone[i];
        float       t[LANES_MOST + 1], mat[LANES_MOST], pts[2 * 3 * (LANES_MOST + 1)];
        int         nb = borders(network_road(lc->road)->sec0, 0.0f, &dm, t, mat), e, j;
        ShapeId     id;
        if (nb < 2 || !laid_near_xy(lc->x, lc->y, lc->len + 4.0f))
            continue;
        id = shape_open("highway, the cap at %d,%d", (int)floorf(lc->x), (int)floorf(lc->y));
        laid_ref_road(lc->road);
        for (j = 0; j < 2; ++j)
            for (e = 0; e < nb; ++e)
            {
                float *q = pts + 3 * (j * nb + e), px = lc->x + cosf(lc->h) * lc->len * (float)j, py = lc->y + sinf(lc->h) * lc->len * (float)j;
                q[0] = px - sinf(lc->h) * t[e], q[1] = py + cosf(lc->h) * t[e], q[2] = lc->z;
            }
        slab_sweep_sided(m, c, mask_bit, pts, 2, nb, thick, mat, dm.side);
        shape_close(id);
    }
    columns(m, c, mask_bit, thick, &dm);
}
