/*  draw/roads.c: the streets drawn from the road network's records.
 *
 *  It reads the road network's layers (plans/network.md).  It reads each
 *  road's lanes (layer 2), and its line, its height and each junction's
 *  boundary (layer 3).  It also reads the ground beside each street
 *  (network/bank.c).  It files nothing back.
 *
 *  A STREET is drawn where a street runs on every tile of its road
 *  (street_piece).  It can run under a power line or a highway, or on its
 *  own.  Its surface is swept along its line at its own height: the lanes
 *  between the two sidewalks, and a sidewalk on each side.  A junction of
 *  streets that are all drawn is filled over its boundary.  Its height at
 *  each point of the boundary is the height of the street that reaches it
 *  nearest.  Any other way keeps the original's art. */
#include "roads.h"
#include "rail_shape.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dump.h"
#include "geo.h"
#include "laid.h"
#include "log.h"
#include "material.h"
#include "mesh/poly.h"
#include "mesh/slab.h"
#include "network/bank.h"
#include "network/network.h"
#include "network/shape.h"
#include "network/topology.h"

/*  A drawn junction's fill: its boundary, each point with its height, and
 *  what is drawn of it.  The SIDEWALK RING lies a sidewalk's width inside
 *  each corner edge, and on a mouth's edge where the arm's sidewalks end.
 *  The asphalt is that ring's triangles.  The bottom is the triangles of the
 *  boundary with the ring's points on each mouth put in.  Each is filed when
 *  the fill is, so what draws it and what checks it read the same rings. */
typedef struct
{
    float *pt; /* its boundary, x, y and z a point, in turn round it */
    int    np;
    float  cx, cy, cz;
    int    junction; /* the junction it fills */
    int   *tri;      /* the boundary's triangles, three indices each */
    int    ntri;
    float *in; /* the sidewalk ring */
    int    nin;
    int   *ine, *ins; /* each boundary point's point of the ring for the edge that ends there, and for the one that starts */
    int   *tin;       /* the ring's triangles */
    int    ntin;
    float *loop; /* the bottom's ring */
    int    nloop;
    int   *tloop; /* its triangles */
    int    ntloop;
    float  turn; /* 1 where the boundary runs counterclockwise, else -1 */
    int    rail; /* 1 for a railway's junction: its gravel's foot alone, drawn by rails.c */
} Fill;

static uint8_t *s_drawn, *s_near, *s_road, *s_cover;
static int32_t  s_side;
static int      s_nroad, s_ready;
static Fill    *s_fills;
static int      s_nfill;
static int     *s_tile_fill; /* a tile's fill, plus one, or 0 */

/*  A STUB is a straight piece of a road with no line of its own, level.
 *  A dead end's cap is one, of a street or a railway.  A dead end of one
 *  tile beside a drawn junction is another.  The network joins it to the
 *  junction by a road of no tiles.  It is drawn from the edge it shares
 *  with the junction to past its tile's middle. */
typedef struct
{
    float x0, y0, x1, y1; /* its line, from the shared edge to its far end */
    float z, half;        /* its height, and its half width */
    float walk;           /* the width of its sidewalk each side: its road's, nought for a ramp road */
    int   net;            /* its family: a railway's stub is drawn by rails.c */
} Stub;

/*  A stub's half width: a street's. */
#define STUB_HALF 0.25f

static Stub  *s_stubs;
static int    s_nstub;
static float *s_cap; /* each road's cap past each end of its line, 0 for none */

/*  An ARM is the part of an arm that a junction stops short of.  It is a
 *  straight strip of street from the junction's mouth.  It ends at the arm's
 *  road's end, or at the tile's edge where the arm has no drawn road.  It is
 *  filed with the fills.  So the code that draws it, marks the tiles it
 *  covers and grades the ground under it reads one strip.  Its outline is
 *  held as a fill of four points. */
typedef struct
{
    float from[6][3], to[6][3]; /* its cross-sections at the mouth and at its end */
    float pt[4 * 3];            /* its outline: from's edges, then to's */
    int   tri[6];               /* the outline's two triangles */
    Fill  out;                  /* that outline, as a fill */
    int   fill;                 /* the junction's fill it runs from */
} Arm;

static Arm *s_arms;
static int  s_narm;

/*  The junctions no fill is drawn for, by why, and a tile of the first
 *  of each.  Either a tile of it carries something no street runs on, or
 *  an arm's street is not drawn.  Or it has more arms than a fill holds,
 *  or its boundary has fewer than three points. */
enum
{
    NOFILL_PIECE,
    NOFILL_ARM,
    NOFILL_ARMS,
    NOFILL_BOUNDARY,
    NOFILL_N
};
static int     s_nofill[NOFILL_N];
static int32_t s_nofill_at[NOFILL_N];

static int c_sink = -1, c_step = -1;

/*  What a tile is to a drawn street.  PIECE_NO: no street runs on it.
 *  PIECE_DRAWN: a street runs on it and nothing else lies on the ground, so
 *  the street is drawn and the tile's art is not.  A power line over it is
 *  drawn on its own.  PIECE_UNDER: a street runs on it under a highway, or
 *  up a ramp to one.  The highway's art and the street's are one picture, so
 *  the street is drawn and the art kept.  The highway's drawing takes the art
 *  away where it draws the highway there (bands.h). */
enum
{
    PIECE_NO = 0,
    PIECE_DRAWN,
    PIECE_UNDER
};

/*  What a tile is to a drawn railway, as street_piece is to a street.  A tile is PIECE_DRAWN where a track runs on its own piece, a slope's foot or a crossing with a wire.  Under a highway it is PIECE_UNDER.  A bridge is not drawn.  A rail end over a subway is not drawn. */
static int rail_piece(const RCity *c, int32_t cell)
{
    RPart p[2];
    int   n = rcity_parts(c->xbld[cell], c->xbit[cell], p), k, rail = 0, under = 0;
    for (k = 0; k < n; ++k)
    {
        if (p[k].net == RNET_RAIL &&
            (p[k].kind == RPART_FITTED || p[k].kind == RPART_CROSSING || p[k].kind == RPART_SLOPE_FOOT))
            rail = 1;
        else if (p[k].net == RNET_BAND)
            under = 1;
        else if (p[k].net != RNET_POWER)
            return PIECE_NO;
    }
    return !rail ? PIECE_NO : under ? PIECE_UNDER
                                    : PIECE_DRAWN;
}

static int street_piece(const RCity *c, int32_t cell)
{
    RPart p[2];
    int   n = rcity_parts(c->xbld[cell], c->xbit[cell], p), k, road = 0, under = 0;
    for (k = 0; k < n; ++k)
    {
        if (p[k].net == RNET_ROAD && (p[k].kind == RPART_FITTED || p[k].kind == RPART_CROSSING || p[k].kind == RPART_RAMP))
            road = 1;
        else if (p[k].net == RNET_BAND)
            under = 1;
        else if (p[k].net != RNET_POWER)
            return PIECE_NO;
    }
    return !road ? PIECE_NO : under ? PIECE_UNDER
                                    : PIECE_DRAWN;
}

/*  What a tile is to a drawn road of a family: a street or a railway. */
static int piece_of(const RCity *c, int net, int32_t cell)
{
    return net == RNET_ROAD ? street_piece(c, cell) : net == RNET_RAIL ? rail_piece(c, cell)
                                                                       : PIECE_NO;
}

/*  Whether unit `unit` of road `road` lies on one of the road's bridges
 *  (NOBJ_BRIDGE). */
static int on_bridge(int road, int unit)
{
    int k;
    for (k = 0; k < network_objects(); ++k)
    {
        const NObject *o = network_object(k);
        if (o->type == NOBJ_BRIDGE && o->road == road && (float)unit >= o->s && (float)unit < o->s + o->length)
            return 1;
    }
    return 0;
}

/*  The stretches of road `road`'s line its bridges span, as `s` along the
 *  line, at most `cap` of them in order along it.  How many. */
static int bridge_gaps(int road, float (*gap)[2], int cap)
{
    int k, n = 0;
    for (k = 0; k < network_objects() && n < cap; ++k)
    {
        const NObject *o = network_object(k);
        if (o->type == NOBJ_BRIDGE && o->road == road)
            gap[n][0] = shape_s(road, o->s), gap[n][1] = shape_s(road, o->s + o->length), ++n;
    }
    return n;
}

/*  Whether `s` along road `road`'s line lies over one of its bridges. */
static int over_bridge(int road, float s)
{
    float gap[8][2];
    int   k, n = bridge_gaps(road, gap, 8);
    for (k = 0; k < n; ++k)
        if (s > gap[k][0] && s < gap[k][1])
            return 1;
    return 0;
}

/*  Whether a road of the road network carries a lane of the type `type`. */
static int has_lane(const NRoad *r, int type)
{
    int k, l;
    for (k = 0; k < r->nsec; ++k)
    {
        const NSection *sec = network_section(r->sec0 + k);
        for (l = 0; l < sec->nlane; ++l)
            if (network_lane(sec->lane0 + l)->type == type)
                return 1;
    }
    return 0;
}

/*  Whether a road of the road network carries a sidewalk lane. */
static int has_sidewalk(const NRoad *r)
{
    return has_lane(r, NLANE_SIDEWALK);
}

/*  Mark every tile within `r` tiles of a point as near. */
static void mark_near(float x, float y, float r)
{
    int32_t c0 = (int32_t)floorf(x - r), c1 = (int32_t)floorf(x + r);
    int32_t r0 = (int32_t)floorf(y - r), r1 = (int32_t)floorf(y + r), c, w;
    for (w = r0; w <= r1; ++w)
        for (c = c0; c <= c1; ++c)
            if (c >= 0 && w >= 0 && c < s_side && w < s_side)
                s_near[w * s_side + c] = 1;
}

static void road_at(int i, float s, float *x, float *y, float *h, float *z);

/*  How many roads of its family a level meet's connecting road `i` joins
 *  there.  A meet carries a family straight on across it where that is two.
 *  Where it is more, the family turns inside the meet. */
static int meet_arms(int i)
{
    const NRoad *r = network_road(i);
    int          arm[64], n = 0, k, e, q;
    for (k = 0; k < s_nroad; ++k)
    {
        const NRoad *o = network_road(k);
        if (o->junction != r->junction || o->net != r->net)
            continue;
        for (e = 0; e < 2; ++e)
        {
            const NLink *l = e ? &o->succ : &o->pred;
            if (l->kind != NLINK_ROAD)
                continue;
            for (q = 0; q < n && arm[q] != l->index; ++q)
                ;
            if (q == n && n < 64)
                arm[n++] = l->index;
        }
    }
    return n;
}

/*  Mark every tile within `reach` of a drawn road's line as near. */
static void mark_line(int i, float reach)
{
    float L = shape_road(i)->length, s, x, y, h, z;
    for (s = 0.0f;; s += 0.25f)
    {
        road_at(i, s < L ? s : L, &x, &y, &h, &z);
        mark_near(x, y, reach);
        if (s >= L)
            break;
    }
}

/*  A drawn road's point and height at `s`. */
static void road_at(int i, float s, float *x, float *y, float *h, float *z)
{
    shape_at(i, s, x, y, h);
    *z = shape_height(i, s);
}

static void roads_free(void)
{
    int i;
    for (i = 0; i < s_nfill; ++i)
        free(s_fills[i].pt), free(s_fills[i].tri), free(s_fills[i].in), free(s_fills[i].tin), free(s_fills[i].loop),
            free(s_fills[i].tloop), free(s_fills[i].ine), free(s_fills[i].ins);
    free(s_drawn), free(s_near), free(s_road), free(s_fills), free(s_tile_fill), free(s_stubs), free(s_cover), free(s_arms), free(s_cap);
    s_drawn = s_near = s_road = s_cover = NULL, s_fills = NULL, s_tile_fill = NULL, s_stubs = NULL, s_arms = NULL, s_cap = NULL;
    s_nfill = s_nroad = s_ready = s_nstub = s_narm = 0;
    laid_streets(NULL, NULL, 0);
    memset(s_nofill, 0, sizeof s_nofill);
}

static void meet_lines(const float p[2], const float d[2], const float q[2], const float e[2], float out[2]);

/*  Whether two points lie at one place on the map. */
static int same_point(const float *a, const float *b)
{
    return fabsf(a[0] - b[0]) < 1e-5f && fabsf(a[1] - b[1]) < 1e-5f;
}

/*  Add a point to a loop, unless it is the loop's last point. */
static void loop_add(float *loop, int *nl, const float *p)
{
    if (*nl > 0 && same_point(&loop[3 * (*nl - 1)], p))
        return;
    memcpy(&loop[3 * (*nl)++], p, 3 * sizeof *loop);
}

/*  A fill's sidewalk ring, its bottom's ring and the triangles of each.
 *  A point between two corner edges lies a sidewalk's width inside both,
 *  where the two lines that far in meet.  Inside is the side the
 *  boundary turns toward.  A point at a mouth has the arm's own sidewalk
 *  end a sidewalk's width along the mouth from it.  Where the boundary
 *  turns in at the mouth, the corner's sidewalk ends on the mouth's
 *  line.  It ends where the line a sidewalk's width inside the corner
 *  edge meets the mouth's.  So the ring has three points there: that
 *  end, the boundary's point and the arm's sidewalk end, all on the
 *  mouth's line.  A street's corner has its sidewalk only between two
 *  arms that both carry sidewalks.  A mouth has one only where its arm
 *  carries one (plans/drawing.md, "A junction").  Elsewhere the ring
 *  follows the boundary.  Nonzero where something could not be held. */
static int fill_rings(Fill *f)
{
    static int  c_bed = -1, c_rise = -1;
    const float side = f->rail ? network_width(RNET_RAIL, 0) * 0.5f - geo_num(&c_bed, "rail_bed_half")
                               : network_type_width(RNET_ROAD, NLANE_SIDEWALK);
    const float rise = f->rail ? geo_num(&c_rise, "rail_bed_height") : 0.0f;
    const int   n    = f->np;
    int         k, nl = 0, ni = 0;
    f->tri   = (int *)malloc((size_t)n * 3 * sizeof *f->tri);
    f->in    = (float *)malloc((size_t)n * 9 * sizeof *f->in);
    f->ine   = (int *)malloc((size_t)n * sizeof *f->ine);
    f->ins   = (int *)malloc((size_t)n * sizeof *f->ins);
    f->tin   = (int *)malloc((size_t)n * 9 * sizeof *f->tin);
    f->loop  = (float *)malloc((size_t)n * 9 * sizeof *f->loop);
    f->tloop = (int *)malloc((size_t)n * 9 * sizeof *f->tloop);
    if (!f->tri || !f->in || !f->ine || !f->ins || !f->tin || !f->loop || !f->tloop)
        return -1;
    f->turn = poly_area(f->pt, 3, n) < 0.0f ? -1.0f : 1.0f;
    /*  How far the ring lies inside each edge, and each mouth's arm's
     *  sidewalk. */
    float *wide = (float *)malloc((size_t)n * sizeof *wide);
    if (!wide)
        return -1;
    for (k = 0; k < n; ++k)
    {
        int road = -1, contact = 0;
        wide[k] = side;
        if (!f->rail && shape_boundary_mouth(f->junction, k) && shape_boundary_arm(f->junction, k, &road, &contact) && road >= 0)
            wide[k] = has_sidewalk(network_road(road)) ? side : 0.0f;
    }
    for (k = 0; k < n && !f->rail; ++k)
        if (!shape_boundary_mouth(f->junction, k))
        {
            int a = k, b = k, q;
            for (q = 0; q < n && !shape_boundary_mouth(f->junction, a); ++q)
                a = (a + n - 1) % n;
            for (q = 0; q < n && !shape_boundary_mouth(f->junction, b); ++q)
                b = (b + 1) % n;
            if (shape_boundary_mouth(f->junction, a) && shape_boundary_mouth(f->junction, b) && (wide[a] <= 0.0f || wide[b] <= 0.0f))
                wide[k] = -1.0f;
        }
    for (k = 0; k < n; ++k)
        wide[k] = wide[k] < 0.0f ? 0.0f : wide[k];
    for (k = 0; k < n; ++k)
    {
        int          kp = (k + n - 1) % n, mp = shape_boundary_mouth(f->junction, kp), mk = shape_boundary_mouth(f->junction, k);
        const float *a = &f->pt[3 * kp], *b = &f->pt[3 * k], *cc = &f->pt[3 * ((k + 1) % n)];
        float        d0[2] = {b[0] - a[0], b[1] - a[1]}, d1[2] = {cc[0] - b[0], cc[1] - b[1]}, l0, l1, p0[2], p1[2];
        float       *o = &f->in[3 * ni];
        int          reflex;
        l0 = hypotf(d0[0], d0[1]), l1 = hypotf(d1[0], d1[1]);
        memcpy(o, b, 3 * sizeof *o);
        f->ine[k] = f->ins[k] = ni++;
        if (l0 < 1e-6f || l1 < 1e-6f || (mp && mk))
            continue;
        /*  The lines a sidewalk's width inside each edge.  Inside an edge is
         *  to its left where the boundary runs counterclockwise. */
        p0[0] = a[0] - d0[1] / l0 * wide[kp] * f->turn, p0[1] = a[1] + d0[0] / l0 * wide[kp] * f->turn;
        p1[0] = b[0] - d1[1] / l1 * wide[k] * f->turn, p1[1] = b[1] + d1[0] / l1 * wide[k] * f->turn;
        reflex = (d0[0] * d1[1] - d0[1] * d1[0]) * f->turn < 0.0f;
        if (mp)
        {
            /*  The mouth ends here: the arm's sidewalk end, then the
             *  corner's. */
            o[0] = b[0] - d0[0] / l0 * wide[kp], o[1] = b[1] - d0[1] / l0 * wide[kp];
            if (reflex)
            {
                float *q = &f->in[3 * ni];
                memcpy(q, b, 3 * sizeof *q);
                ++ni;
                q = &f->in[3 * ni];
                memcpy(q, b, 3 * sizeof *q);
                meet_lines(b, d0, p1, d1, q);
                f->ins[k] = ni++;
            }
            continue;
        }
        if (mk)
        {
            /*  A mouth starts here: the corner's sidewalk end, then the
             *  arm's. */
            float *q;
            if (reflex)
            {
                meet_lines(p0, d0, b, d1, o);
                q = &f->in[3 * ni];
                memcpy(q, b, 3 * sizeof *q);
                ++ni;
                q = &f->in[3 * ni];
                memcpy(q, b, 3 * sizeof *q);
                f->ins[k] = ni++;
            }
            else
                q = o;
            q[0] = b[0] + d1[0] / l1 * wide[k], q[1] = b[1] + d1[1] / l1 * wide[k];
            continue;
        }
        /*  Where the two edges run on in line, the point is this one's own,
         *  a sidewalk's width in. */
        meet_lines(p1, d1, p0, d0, o);
    }
    free(wide);
    /*  Where the ring lies on the boundary, two of its points can be one.
     *  The triangulation needs each point once. */
    {
        int *at = (int *)malloc((size_t)(ni > 0 ? ni : 1) * sizeof *at), m = 0;
        if (!at)
            return -1;
        for (k = 0; k < ni; ++k)
        {
            if (m > 0 && same_point(&f->in[3 * (m - 1)], &f->in[3 * k]))
            {
                at[k] = m - 1;
                continue;
            }
            memmove(&f->in[3 * m], &f->in[3 * k], 3 * sizeof *f->in);
            at[k] = m++;
        }
        if (m > 1 && same_point(&f->in[3 * (m - 1)], &f->in[0]))
        {
            for (k = 0; k < ni; ++k)
                at[k] = at[k] == m - 1 ? 0 : at[k];
            --m;
        }
        for (k = 0; k < n; ++k)
            f->ine[k] = at[f->ine[k]], f->ins[k] = at[f->ins[k]];
        free(at);
        ni = m;
    }
    f->nin = ni;
    /*  A gravel pile's ring is the top of its slope. */
    for (k = 0; k < ni; k++)
        f->in[3 * k + 2] += rise;
    for (k = 0; k < n; ++k)
    {
        loop_add(f->loop, &nl, &f->pt[3 * k]);
        if (shape_boundary_mouth(f->junction, k))
        {
            loop_add(f->loop, &nl, &f->in[3 * f->ins[k]]);
            loop_add(f->loop, &nl, &f->in[3 * f->ine[(k + 1) % n]]);
        }
    }
    if (nl > 1 && same_point(&f->loop[3 * (nl - 1)], &f->loop[0]))
        --nl;
    f->nloop  = nl;
    f->ntri   = poly_triangulate(f->pt, 3, n, f->tri);
    f->ntin   = poly_triangulate(f->in, 3, ni, f->tin);
    f->ntloop = poly_triangulate(f->loop, 3, nl, f->tloop);
    return f->ntri < 1 || f->ntin < 1 || f->ntloop < 1;
}

/*  The drawn junctions' fills.  A junction is drawn where every street
 *  that reaches it is drawn and a street alone runs on each of its tiles.
 *  A dead end is not a node, and its street's line covers it. */
static int fills_build(const RCity *c)
{
    int j, i, k;
    s_fills     = (Fill *)calloc((size_t)network_junctions() + 1, sizeof *s_fills);
    s_tile_fill = (int *)calloc((size_t)s_side * (size_t)s_side, sizeof *s_tile_fill);
    if (!s_fills || !s_tile_fill)
        return -1;
    for (j = 0; j < network_junctions(); ++j)
    {
        const NJunction *nj = network_junction(j);
        const TopoEnd   *te = topology_end(nj->end);
        const float     *xy;
        float            ex[16], ey[16], ez[16];
        int              np = shape_boundary(j, &xy), arms = 0, why = -1;
        if (te->kind != END_NODE || (te->net != RNET_ROAD && te->net != RNET_RAIL))
            continue;
        for (k = 0; why < 0 && k < te->ncell; ++k)
            if (te->net == RNET_ROAD ? street_piece(c, topology_cell(te->cell0 + k)) != PIECE_DRAWN
                                     : rail_piece(c, topology_cell(te->cell0 + k)) != PIECE_DRAWN)
                why = NOFILL_PIECE;
        /*  The ends of the streets that reach it. */
        for (i = 0; why < 0 && i < network_roads(); ++i)
        {
            const NRoad *r = network_road(i);
            int          e;
            for (e = 0; e < 2; ++e)
            {
                const NLink *l = e ? &r->succ : &r->pred;
                float        h;
                if (l->kind != NLINK_JUNCTION || l->index != j || r->junction >= 0)
                    continue;
                /*  A road of no tiles is the edge two junctions share: the
                 *  two fills meet across it. */
                if (r->topo >= 0 && topology_road(r->topo)->nunit == 0)
                    continue;
                if (!s_road[i] || arms == 16)
                {
                    why = s_road[i] ? NOFILL_ARMS : NOFILL_ARM;
                    break;
                }
                road_at(i, e ? shape_road(i)->length : 0.0f, &ex[arms], &ey[arms], &h, &ez[arms]);
                ++arms;
            }
        }
        if (why < 0 && (arms < 1 || np < 3))
            why = NOFILL_BOUNDARY;
        if (why >= 0)
        {
            if (!s_nofill[why]++)
                s_nofill_at[why] = te->ncell > 0 ? topology_cell(te->cell0) : -1;
            continue;
        }
        if (te->net == RNET_RAIL)
        {
            /*  A railway's junction: the foot of its gravel (rails.c), for
             *  the ground under it and the tiles it covers. */
            enum
            {
                MOST = 1024
            };
            Fill *f = &s_fills[s_nfill];
            f->pt   = (float *)malloc((size_t)MOST * 3 * sizeof *f->pt);
            f->tri  = (int *)malloc((size_t)MOST * 3 * sizeof *f->tri);
            if (!f->pt || !f->tri)
                return -1;
            f->np       = rail_gravel_outline(j, f->pt, MOST);
            f->junction = j;
            f->rail     = 1;
            if (f->np < 3)
            {
                free(f->pt), free(f->tri);
                f->pt = NULL, f->tri = NULL;
                if (!s_nofill[NOFILL_BOUNDARY]++)
                    s_nofill_at[NOFILL_BOUNDARY] = te->ncell > 0 ? topology_cell(te->cell0) : -1;
                continue;
            }
            f->ntri = poly_triangulate(f->pt, 3, f->np, f->tri);
            f->cx = f->cy = f->cz = 0.0f;
            for (k = 0; k < f->np; ++k)
            {
                f->cx += f->pt[3 * k] / (float)f->np, f->cy += f->pt[3 * k + 1] / (float)f->np;
                f->cz += f->pt[3 * k + 2] / (float)f->np;
                mark_near(f->pt[3 * k], f->pt[3 * k + 1], 0.6f);
            }
            for (k = 0; k < te->ncell; ++k)
            {
                int32_t cell      = topology_cell(te->cell0 + k);
                s_drawn[cell]     = 1;
                s_tile_fill[cell] = s_nfill + 1;
                mark_near((float)(cell % s_side) + 0.5f, (float)(cell / s_side) + 0.5f, 1.0f);
            }
            ++s_nfill;
            continue;
        }
        {
            Fill *f = &s_fills[s_nfill];
            f->pt   = (float *)malloc((size_t)(np + 8) * 3 * sizeof *f->pt);
            if (!f->pt)
                return -1;
            f->np       = np;
            f->junction = j;
            f->rail     = 0;
            f->cx = f->cy = f->cz = 0.0f;
            for (k = 0; k < arms; ++k)
                f->cz += ez[k] / (float)arms;
            /*  Each point on an arm stands at the road's height at its cut.
             *  Each point between two arms takes its height from them, by how
             *  far round it is from each. */
            {
                int   known[256], a, rd, ct;
                float zk[256];
                for (k = 0; k < np && k < 256; ++k)
                {
                    float x, y, h, z, cut, sk;
                    f->pt[3 * k] = xy[2 * k], f->pt[3 * k + 1] = xy[2 * k + 1], f->pt[3 * k + 2] = f->cz;
                    known[k] = shape_boundary_arm(j, k, &rd, &ct);
                    if (known[k])
                    {
                        cut = shape_cut(rd, ct) > 0.0f ? shape_cut(rd, ct) : 0.0f;
                        sk  = ct ? shape_road(rd)->length - cut : cut;
                        road_at(rd, sk, &x, &y, &h, &z);
                        zk[k] = z;
                    }
                    else if (shape_boundary_mouth(j, k) || shape_boundary_mouth(j, (k + np - 1) % np))
                    {
                        /*  A mouth with no arm filed at it stands at the
                         *  height of the arm whose end is at its middle. */
                        int   m0 = shape_boundary_mouth(j, k) ? k : (k + np - 1) % np, a2, best = -1;
                        float mx = 0.5f * (xy[2 * m0] + xy[2 * ((m0 + 1) % np)]);
                        float my = 0.5f * (xy[2 * m0 + 1] + xy[2 * ((m0 + 1) % np) + 1]), bd = 0.6f;
                        for (a2 = 0; a2 < arms; ++a2)
                            if (hypotf(ex[a2] - mx, ey[a2] - my) < bd)
                                bd = hypotf(ex[a2] - mx, ey[a2] - my), best = a2;
                        known[k] = 1, zk[k] = best >= 0 ? ez[best] : f->cz;
                    }
                }
                for (k = 0; k < np && k < 256; ++k)
                {
                    int b0 = -1, b1 = -1, d0 = 0, d1 = 0;
                    if (known[k])
                    {
                        f->pt[3 * k + 2] = zk[k];
                        continue;
                    }
                    for (a = 1; a < np && b0 < 0; ++a)
                        if (known[(k + np - a) % np])
                            b0 = (k + np - a) % np, d0 = a;
                    for (a = 1; a < np && b1 < 0; ++a)
                        if (known[(k + a) % np])
                            b1 = (k + a) % np, d1 = a;
                    if (b0 >= 0 && b1 >= 0)
                        f->pt[3 * k + 2] = (zk[b0] * (float)d1 + zk[b1] * (float)d0) / (float)(d0 + d1);
                }
            }
            for (k = 0; k < np; ++k)
            {
                f->cx += f->pt[3 * k] / (float)np;
                f->cy += f->pt[3 * k + 1] / (float)np;
                mark_near(f->pt[3 * k], f->pt[3 * k + 1], 0.6f);
            }
            fill_rings(f);
            for (k = 0; k < te->ncell; ++k)
            {
                int32_t cell      = topology_cell(te->cell0 + k);
                s_drawn[cell]     = 1;
                s_tile_fill[cell] = s_nfill + 1;
                mark_near((float)(cell % s_side) + 0.5f, (float)(cell / s_side) + 0.5f, 1.0f);
            }
            ++s_nfill;
        }
    }
    return 0;
}

/*  The height of a fill's boundary point nearest a point. */
static float nearest_z(const Fill *f, float x, float y)
{
    float best = 1e18f, z = f->cz;
    int   k;
    for (k = 0; k < f->np; ++k)
    {
        float d = hypotf(f->pt[3 * k] - x, f->pt[3 * k + 1] - y);
        if (d < best)
            best = d, z = f->pt[3 * k + 2];
    }
    return z;
}

/*  The stubs.  Each dead end's cap past a drawn street is one.  Each dead
 *  end of one tile beside a drawn junction is another. */
static int stubs_build(const RCity *c)
{
    static const int DC[4] = {1, -1, 0, 0}, DR[4] = {0, 0, 1, -1};
    int              i, k;
    s_stubs = (Stub *)calloc((size_t)topology_ends() + (size_t)network_junctions() + 1, sizeof *s_stubs);
    s_cap   = (float *)calloc(2 * (size_t)s_nroad + 1, sizeof *s_cap);
    laid_streets(s_road, s_cap, s_nroad);
    if (!s_stubs || !s_cap)
        return -1;
    /*  A dead end's CAP: its junction's boundary, from the end of the line of
     *  the road that reaches it across the rest of its tile.  The cap runs
     *  straight from that end, along the road's heading, to the far side of
     *  the boundary.  A street's cap is a stub.  A railway's pile and tracks
     *  run on over its cap (rails.c). */
    for (i = 0; i < s_nroad; ++i)
    {
        const NRoad *r = network_road(i);
        int          e;
        if (!s_road[i] || (r->net != RNET_ROAD && r->net != RNET_RAIL))
            continue;
        for (e = 0; e < 2; ++e)
        {
            const NLink   *l = e ? &r->succ : &r->pred;
            const TopoEnd *te;
            const float   *xy;
            float          x, y, h, z, dx, dy, far = 0.0f;
            int            np;
            /*  A road of no units is the edge its two ends share.  So the
             *  cap looks across it to the end beyond, the one that is not
             *  this road's own junction. */
            if (l->kind == NLINK_ROAD && !shape_road(l->index)->ngeom)
            {
                const NRoad *edge = network_road(l->index);
                l                 = edge->pred.kind == NLINK_JUNCTION && edge->pred.index != r->junction ? &edge->pred : &edge->succ;
            }
            if (l->kind != NLINK_JUNCTION)
                continue;
            te = topology_end(network_junction(l->index)->end);
            if (te->kind != END_DEAD || te->ncell != 1 || piece_of(c, r->net, topology_cell(te->cell0)) == PIECE_NO)
                continue;
            /*  A dead end with no boundary of its own, reached only across an
             *  edge, is its one cell. */
            if ((np = shape_boundary(l->index, &xy)) < 3)
            {
                static float  square[8];
                const int32_t cell = topology_cell(te->cell0);
                const float   cx = (float)(cell % s_side), cy = (float)(cell / s_side);
                square[0] = cx, square[1] = cy, square[2] = cx + 1.0f, square[3] = cy;
                square[4] = cx + 1.0f, square[5] = cy + 1.0f, square[6] = cx, square[7] = cy + 1.0f;
                xy = square, np = 4;
            }
            road_at(i, e ? shape_road(i)->length : 0.0f, &x, &y, &h, &z);
            dx = cosf(h) * (e ? 1.0f : -1.0f), dy = sinf(h) * (e ? 1.0f : -1.0f);
            for (k = 0; k < np; ++k)
            {
                float u = (xy[2 * k] - x) * dx + (xy[2 * k + 1] - y) * dy;
                far     = u > far ? u : far;
            }
            if (far < 1e-3f)
                continue;
            /*  Under a highway the art stays until the highway's drawing
             *  takes it (bands.h). */
            s_drawn[topology_cell(te->cell0)] = piece_of(c, r->net, topology_cell(te->cell0)) == PIECE_DRAWN;
            mark_near(x + dx * far * 0.5f, y + dy * far * 0.5f, 1.0f);
            if (r->net == RNET_RAIL)
                s_cap[2 * i + e] = far;
            Stub *st = &s_stubs[s_nstub++];
            st->net  = r->net;
            st->x0 = x, st->y0 = y, st->z = z;
            st->x1 = x + dx * far, st->y1 = y + dy * far;
            st->half = r->net == RNET_RAIL ? rail_foot() : shape_width(i, e ? shape_road(i)->length : 0.0f) * 0.5f;
            st->walk = has_sidewalk(r) ? network_type_width(RNET_ROAD, NLANE_SIDEWALK) : 0.0f;
        }
    }
    for (i = 0; i < topology_ends(); ++i)
    {
        const TopoEnd *e = topology_end(i);
        int32_t        cell, col, row;
        if (e->kind != END_DEAD || e->net != RNET_ROAD || e->ncell != 1)
            continue;
        cell = topology_cell(e->cell0), col = cell % s_side, row = cell / s_side;
        if (s_drawn[cell] || street_piece(c, cell) != PIECE_DRAWN)
            continue;
        for (k = 0; k < 4; ++k)
        {
            int32_t jc = col - DC[k], jr = row - DR[k], at;
            float   mx, my, half = STUB_HALF, z0, z1;
            Fill   *f;
            if (jc < 0 || jr < 0 || jc >= s_side || jr >= s_side || !s_tile_fill[at = jr * s_side + jc])
                continue;
            /*  The stub starts at the shared edge, at the height of the
             *  junction's nearest point there. */
            f  = &s_fills[s_tile_fill[at] - 1];
            mx = (float)col + 0.5f - 0.5f * (float)DC[k], my = (float)row + 0.5f - 0.5f * (float)DR[k];
            z0 = z1  = nearest_z(f, mx, my);
            Stub *st = &s_stubs[s_nstub++];
            st->net  = RNET_ROAD;
            st->x0 = mx, st->y0 = my, st->z = 0.5f * (z0 + z1), st->half = half;
            st->walk      = network_type_width(RNET_ROAD, NLANE_SIDEWALK);
            st->x1        = (float)col + 0.5f + (float)DC[k] * half;
            st->y1        = (float)row + 0.5f + (float)DR[k] * half;
            s_drawn[cell] = 1;
            mark_near((float)col + 0.5f, (float)row + 0.5f, 1.0f);
            break;
        }
        /*  A street of one tile joined to nothing: a stub across its tile,
         *  along the edges its piece claims, on the land. */
        if (!s_drawn[cell])
        {
            RPart p[2];
            int   n = rcity_parts(c->xbld[cell], c->xbit[cell], p), q, edges = 0, across;
            for (q = 0; q < n; ++q)
                if (p[q].net == RNET_ROAD)
                    edges |= p[q].edges;
            /*  An edge is a bit: 1 toward col - 1, 2 toward row + 1, 4
             *  toward col + 1, 8 toward row - 1. */
            across   = (edges & 10) && !(edges & 5);
            Stub *st = &s_stubs[s_nstub++];
            st->net  = RNET_ROAD;
            st->x0 = (float)col + (across ? 0.5f : 0.0f), st->y0 = (float)row + (across ? 0.0f : 0.5f);
            st->x1 = (float)col + (across ? 0.5f : 1.0f), st->y1 = (float)row + (across ? 1.0f : 0.5f);
            st->z         = (float)shape_land_at((double)col + 0.5, (double)row + 0.5);
            st->half      = STUB_HALF;
            st->walk      = network_type_width(RNET_ROAD, NLANE_SIDEWALK);
            s_drawn[cell] = 1;
            mark_near((float)col + 0.5f, (float)row + 0.5f, 1.0f);
        }
    }
    return 0;
}

static int fill_z(const Fill *f, float x, float y, float *z);

/*  Mark the tile under a point as covered. */
static void cover_at(float x, float y)
{
    int32_t c = (int32_t)floorf(x), r = (int32_t)floorf(y);
    if (c >= 0 && r >= 0 && c < s_side && r < s_side)
        s_cover[r * s_side + c] = 1;
}

/*  Mark every tile a strip of surface lies over.  The strip is a quad from
 *  (a, b) to (c, d), walked in steps a twentieth of a tile apart. */
static void cover_quad(const float a[2], const float b[2], const float c[2], const float d[2])
{
    float len = fmaxf(hypotf(b[0] - a[0], b[1] - a[1]), hypotf(c[0] - d[0], c[1] - d[1]));
    float wid = fmaxf(hypotf(d[0] - a[0], d[1] - a[1]), hypotf(c[0] - b[0], c[1] - b[1]));
    int   nu = (int)ceilf(len / 0.05f) + 1, nv = (int)ceilf(wid / 0.05f) + 1, i, j;
    for (i = 0; i <= nu; ++i)
        for (j = 0; j <= nv; ++j)
        {
            /*  Just inside the quad, so a surface that ends on a tile's
             *  edge does not cover the tile past it. */
            float u = 1e-3f + 0.998f * (float)i / (float)nu, v = 1e-3f + 0.998f * (float)j / (float)nv;
            float x0 = a[0] + (b[0] - a[0]) * u, y0 = a[1] + (b[1] - a[1]) * u;
            float x1 = d[0] + (c[0] - d[0]) * u, y1 = d[1] + (c[1] - d[1]) * u;
            cover_at(x0 + (x1 - x0) * v, y0 + (y1 - y0) * v);
        }
}

static void across(float half, float side, float mid, float t[6]);
static void street_cross(int road, float s, float t[6]);
static void section(const float r[2], const float l[2], float z, float side, float mid, float out[6][3]);

/*  The arms the drawn junctions stop short of (Arm). */
static int arms_build(float reach)
{
    int i, k, most = 0;
    for (i = 0; i < s_nfill; ++i)
        most += s_fills[i].np;
    s_arms = (Arm *)calloc((size_t)most + 1, sizeof *s_arms);
    if (!s_arms)
        return -1;
    for (i = 0; i < s_nfill; ++i)
    {
        const Fill *f = &s_fills[i];
        /*  A railway's gravel reaches each of its arms already. */
        if (f->rail)
            continue;
        for (k = 0; k < f->np; ++k)
        {
            const float *ra = &f->pt[3 * k], *la = &f->pt[3 * ((k + 1) % f->np)];
            float        from[6][3], to[6][3], r[2] = {ra[0], ra[1]}, l[2] = {la[0], la[1]};
            int          rd, ct, p;
            float        cut, x, y, h, z;
            Arm         *a;
            if (!shape_boundary_mouth(f->junction, k))
                continue;
            if (shape_boundary_arm(f->junction, k, &rd, &ct))
            {
                float t[6];
                cut = shape_cut(rd, ct);
                if (cut >= -1e-4f)
                    continue;
                road_at(rd, ct ? shape_road(rd)->length : 0.0f, &x, &y, &h, &z);
                street_cross(rd, ct ? shape_road(rd)->length : 0.0f, t);
                /*  The mouth carries the road's sidewalk and median. */
                section(r, l, ra[2], t[5] - t[4], 0.5f * (t[3] - t[2]), from);
                {
                    /*  The road's end, its points in the same order as the
                     *  mouth's. */
                    float nx = -sinf(h), ny = cosf(h), fl;
                    fl = nx * (l[0] - r[0]) + ny * (l[1] - r[1]) < 0.0f ? -1.0f : 1.0f;
                    for (p = 0; p < 6; ++p)
                    {
                        float tp = fl > 0.0f ? t[p] : -t[5 - p];
                        to[p][0] = x + nx * tp * fl, to[p][1] = y + ny * tp * fl, to[p][2] = z;
                    }
                }
            }
            else
            {
                /*  An arm with no drawn road: on to the tile's edge. */
                float dx = 0.5f * (r[0] + l[0]) - f->cx, dy = 0.5f * (r[1] + l[1]) - f->cy, ex = l[1] - r[1], ey = -(l[0] - r[0]);
                float el = hypotf(ex, ey), mx, my, e2[2], e1[2];
                if (el < 1e-6f)
                    continue;
                ex /= el, ey /= el;
                if (ex * dx + ey * dy < 0.0f)
                    ex = -ex, ey = -ey;
                mx = 0.5f * (r[0] + l[0]), my = 0.5f * (r[1] + l[1]);
                /*  How far on to the tile's edge it faces. */
                {
                    float dist = fabsf(ex) > 0.5f ? fabsf((ex > 0.0f ? ceilf(mx - 1e-3f) : floorf(mx + 1e-3f)) - mx)
                                                  : fabsf((ey > 0.0f ? ceilf(my - 1e-3f) : floorf(my + 1e-3f)) - my);
                    if (dist < 1e-3f)
                        continue;
                    e1[0] = r[0] + ex * dist, e1[1] = r[1] + ey * dist;
                    e2[0] = l[0] + ex * dist, e2[1] = l[1] + ey * dist;
                }
                section(r, l, ra[2], network_type_width(RNET_ROAD, NLANE_SIDEWALK), 0.0f, from);
                section(e1, e2, ra[2], network_type_width(RNET_ROAD, NLANE_SIDEWALK), 0.0f, to);
            }
            a = &s_arms[s_narm++];
            memcpy(a->from, from, sizeof a->from);
            memcpy(a->to, to, sizeof a->to);
            a->fill = i;
            memcpy(&a->pt[0], from[0], sizeof from[0]);
            memcpy(&a->pt[3], from[5], sizeof from[5]);
            memcpy(&a->pt[6], to[5], sizeof to[5]);
            memcpy(&a->pt[9], to[0], sizeof to[0]);
            a->out.pt       = a->pt;
            a->out.np       = 4;
            a->out.cx       = 0.25f * (a->pt[0] + a->pt[3] + a->pt[6] + a->pt[9]);
            a->out.cy       = 0.25f * (a->pt[1] + a->pt[4] + a->pt[7] + a->pt[10]);
            a->out.cz       = 0.25f * (a->pt[2] + a->pt[5] + a->pt[8] + a->pt[11]);
            a->out.junction = f->junction;
            a->tri[0] = 0, a->tri[1] = 1, a->tri[2] = 2, a->tri[3] = 0, a->tri[4] = 2, a->tri[5] = 3;
            a->out.tri  = a->tri;
            a->out.ntri = 2;
            for (p = 0; p < 4; ++p)
                mark_near(a->pt[3 * p], a->pt[3 * p + 1], reach);
        }
    }
    return 0;
}

/*  The tiles every drawn surface lies over: each street, stub, fill and arm. */
static int cover_build(void)
{
    int i, k;
    s_cover = (uint8_t *)calloc((size_t)s_side * (size_t)s_side, 1);
    if (!s_cover)
        return -1;
    for (i = 0; i < s_nroad; ++i)
    {
        float L, p[2][2], q[2][2];
        int   n, was_bridged = 0;
        if (!s_road[i])
            continue;
        L = shape_road(i)->length;
        n = (int)ceilf(L / 0.05f);
        for (k = 0; k <= n; ++k)
        {
            float x, y, h, z, s = L * (float)k / (float)(n > 0 ? n : 1), t[6];
            /*  A street's bridge covers no tile: the street is not laid
             *  there. */
            int bridged = network_road(i)->net == RNET_ROAD && over_bridge(i, s);
            road_at(i, s, &x, &y, &h, &z);
            /*  A street's edges where its lanes put them, a railway's at
             *  its gravel's foot. */
            if (network_road(i)->net == RNET_ROAD)
                street_cross(i, s, t);
            else if (network_road(i)->net == RNET_RAIL)
                t[0] = -rail_foot(), t[5] = rail_foot();
            else
                t[0] = -shape_width(i, s) * 0.5f, t[5] = -t[0];
            q[0][0] = x - sinf(h) * t[5], q[0][1] = y + cosf(h) * t[5];
            q[1][0] = x - sinf(h) * t[0], q[1][1] = y + cosf(h) * t[0];
            if (k > 0 && !bridged && !was_bridged)
                cover_quad(p[0], q[0], q[1], p[1]);
            memcpy(p, q, sizeof p);
            was_bridged = bridged;
        }
    }
    for (i = 0; i < s_nstub; ++i)
    {
        const Stub *st = &s_stubs[i];
        float       dx = st->x1 - st->x0, dy = st->y1 - st->y0, len = hypotf(dx, dy), nx, ny, a[2], b[2], c[2], d[2];
        if (len < 1e-6f)
            continue;
        nx = -dy / len * st->half, ny = dx / len * st->half;
        a[0] = st->x0 + nx, a[1] = st->y0 + ny, b[0] = st->x1 + nx, b[1] = st->y1 + ny;
        c[0] = st->x1 - nx, c[1] = st->y1 - ny, d[0] = st->x0 - nx, d[1] = st->y0 - ny;
        cover_quad(a, b, c, d);
    }
    for (i = 0; i < s_nfill; ++i)
        for (k = 0; k < s_fills[i].ntri; ++k)
        {
            const Fill  *f = &s_fills[i];
            const float *a = &f->pt[3 * f->tri[3 * k]], *b = &f->pt[3 * f->tri[3 * k + 1]], *cc = &f->pt[3 * f->tri[3 * k + 2]];
            float        pa[2] = {a[0], a[1]}, pb[2] = {b[0], b[1]}, pc[2] = {cc[0], cc[1]};
            cover_quad(pa, pb, pc, pa);
        }
    for (i = 0; i < s_narm; ++i)
    {
        const Arm *a     = &s_arms[i];
        float      p0[2] = {a->pt[0], a->pt[1]}, p1[2] = {a->pt[9], a->pt[10]}, p2[2] = {a->pt[6], a->pt[7]},
                   p3[2] = {a->pt[3], a->pt[4]};
        cover_quad(p0, p1, p2, p3);
    }
    return 0;
}

int roads_build(const RCity *c, int shaped)
{
    int   i, k;
    float reach;
    roads_free();
    if (!shaped)
        return -1;
    s_side  = topology_side();
    s_nroad = network_roads();
    s_drawn = (uint8_t *)calloc((size_t)s_side * (size_t)s_side, 1);
    s_near  = (uint8_t *)calloc((size_t)s_side * (size_t)s_side, 1);
    s_road  = (uint8_t *)calloc((size_t)s_nroad + 1, 1);
    if (!s_drawn || !s_near || !s_road)
        return -1;
    laid_streets(s_road, NULL, s_nroad);
    {
        static int c_grid = -1;
        /*  As far as any grade reaches, and half the spacing of the points
         *  the street is walked through. */
        reach = (float)(bank_width() / 2.0 + bank_reach()) + 1.0f / geo_num(&c_grid, "road_ground_grid") + 0.15f;
    }
    /*  The roads drawn, streets and railways alike.  A road is drawn
     *  where it has a line and each of its tiles carries a piece its
     *  family runs on (piece_of).  A street is drawn from the lanes it
     *  has, with a sidewalk only where it carries one.
     *  Its art is not drawn, but under a highway, where the highway's
     *  art and the road's are one picture.  Why a street is not drawn is
     *  counted for the log, with the piece that stopped each. */
    int why[4] = {0, 0, 0, 0}, stop[256];
    memset(stop, 0, sizeof stop);
    for (i = 0; i < s_nroad; ++i)
    {
        const NRoad    *r = network_road(i);
        const TopoRoad *t;
        int             ok = 1, street = r->net == RNET_ROAD;
        if (r->topo < 0 || (!street && r->net != RNET_RAIL))
            continue;
        if (shape_road(i)->ngeom == 0)
        {
            why[1] += street;
            continue;
        }
        /*  A street's bridges are left out of it (on_bridge), and the rest
         *  of it is drawn.  A street that is all bridge is not drawn. */
        t = topology_road(r->topo);
        {
            int land = 0;
            for (k = 0; ok && k < t->nunit; ++k)
            {
                if (street && on_bridge(i, k))
                    continue;
                ++land;
                if (!(ok = piece_of(c, r->net, topology_cell(t->unit0 + k)) != PIECE_NO) && street)
                    ++stop[c->xbld[topology_cell(t->unit0 + k)]];
            }
            ok = ok && land > 0;
        }
        if (!ok)
        {
            why[2] += street;
            continue;
        }
        why[3] += street;
        s_road[i] = 1;
        for (k = 0; k < t->nunit; ++k)
            s_drawn[topology_cell(t->unit0 + k)] = piece_of(c, r->net, topology_cell(t->unit0 + k)) == PIECE_DRAWN;
        mark_line(i, reach);
    }
    {
        char said[256];
        int  n  = 0, b;
        said[0] = 0;
        for (b = 0; b < 256; ++b)
            if (stop[b] && n < (int)sizeof said - 16)
                n += snprintf(said + n, sizeof said - (size_t)n, " %02x:%d", b, stop[b]);
        R_DBG("roads", "streets drawn %d; not drawn: %d without a line, %d with a piece no street runs on (the pieces:%s)", why[3], why[1], why[2], said);
    }
    /*  A level meet carries each family straight on across it.  A
     *  family's connecting road between two of its drawn roads is drawn
     *  as a road of that family.  It is drawn once each meet and family.
     *  The meet's art is not drawn. */
    for (i = 0; i < s_nroad; i++)
    {
        const NRoad *r = network_road(i);
        int          k2, back = 0;
        if (r->junction < 0 || (r->net != RNET_ROAD && r->net != RNET_RAIL) || !shape_road(i)->ngeom ||
            r->pred.kind != NLINK_ROAD || r->succ.kind != NLINK_ROAD || !laid_reach(r->pred.index) || !laid_reach(r->succ.index) ||
            topology_end(network_junction(r->junction)->end)->kind != END_MEET)
            continue;
        /*  Of the two that run each way across, the one from the lower road. */
        for (k2 = 0; k2 < s_nroad && r->pred.index > r->succ.index; k2++)
        {
            const NRoad *o = network_road(k2);
            back |= k2 != i && o->junction == r->junction && o->net == r->net && shape_road(k2)->ngeom &&
                    o->pred.kind == NLINK_ROAD && o->succ.kind == NLINK_ROAD && o->pred.index == r->succ.index &&
                    o->succ.index == r->pred.index;
        }
        if (back || meet_arms(i) != 2)
            continue;
        s_road[i] = 1;
        {
            const TopoEnd *te = topology_end(network_junction(r->junction)->end);
            for (k = 0; k < te->ncell; k++)
                s_drawn[topology_cell(te->cell0 + k)] = 1;
        }
        mark_line(i, reach);
    }
    if (fills_build(c) != 0 || stubs_build(c) != 0 || arms_build(reach) != 0)
        return -1;
    if (cover_build() != 0)
        return -1;
    s_ready = 1;
    return 0;
}

int roads_tiles(uint8_t **drawn, const uint8_t **cover, int *side)
{
    if (!s_drawn || !s_cover)
        return -1;
    *drawn = s_drawn, *cover = s_cover, *side = s_side;
    return 0;
}

const uint8_t *roads_cover(void)
{
    return s_ready ? s_cover : NULL;
}

const uint8_t *roads_drawn(void)
{
    return s_ready ? s_drawn : NULL;
}

int roads_near(int32_t col, int32_t row)
{
    return s_ready && col >= 0 && row >= 0 && col < s_side && row < s_side && s_near[row * s_side + col];
}

/*  Whether a point lies inside a fill, and its height there, from the
 *  boundary's triangles. */
static int fill_z(const Fill *f, float x, float y, float *z)
{
    int k;
    for (k = 0; k < f->ntri; ++k)
    {
        const float *a = &f->pt[3 * f->tri[3 * k]], *b = &f->pt[3 * f->tri[3 * k + 1]], *c = &f->pt[3 * f->tri[3 * k + 2]];
        float        d = (b[1] - c[1]) * (a[0] - c[0]) + (c[0] - b[0]) * (a[1] - c[1]), wa, wb, wc;
        if (fabsf(d) < 1e-12f)
            continue;
        wa = ((b[1] - c[1]) * (x - c[0]) + (c[0] - b[0]) * (y - c[1])) / d;
        wb = ((c[1] - a[1]) * (x - c[0]) + (a[0] - c[0]) * (y - c[1])) / d;
        wc = 1.0f - wa - wb;
        if (wa >= -1e-5f && wb >= -1e-5f && wc >= -1e-5f)
        {
            *z = wa * a[2] + wb * b[2] + wc * c[2];
            return 1;
        }
    }
    return 0;
}

/*  How far a point lies outside a fill's boundary, nought or less inside:
 *  its distance to the outline, signed by whether the fill holds it. */
static float fill_out(const Fill *f, float x, float y)
{
    float best = 1e9f, z;
    int   k;
    for (k = 0; k < f->np; ++k)
    {
        const float *a = &f->pt[3 * k], *b = &f->pt[3 * ((k + 1) % f->np)];
        float        ex = b[0] - a[0], ey = b[1] - a[1], l2 = ex * ex + ey * ey, t, d;
        t    = l2 > 1e-12f ? ((x - a[0]) * ex + (y - a[1]) * ey) / l2 : 0.0f;
        t    = t < 0.0f ? 0.0f : t > 1.0f ? 1.0f
                                          : t;
        d    = hypotf(x - a[0] - ex * t, y - a[1] - ey * t);
        best = d < best ? d : best;
    }
    return fill_z(f, x, y, &z) ? -best : best;
}

/*  The ground as one drawn surface meets it, kept if lower than the
 *  ground any other surface leaves there. */
typedef struct
{
    float ground, out, sink, margin, reach;
} Meet;

/*  A surface of height z, with the point e tiles past its edge
 *  (bank_grade). */
static void meet(Meet *m, float z, float e)
{
    float g = (float)bank_grade(z, e, m->ground, m->margin, m->sink);
    m->out  = g < m->out ? g : m->out;
}

/*  The ground under and beside the drawn surfaces.  Each street, stub and
 *  fill that reaches a point grades it, and the lowest grade wins.  So the
 *  ground is continuous wherever the surfaces meet. */
float roads_ground(float x, float y, float ground)
{
    static int c_grid = -1;
    Meet       m;
    int32_t    col = (int32_t)floorf(x), row = (int32_t)floorf(y), dc, dr;
    int        k;
    if (!s_ready)
        return ground;
    m.ground = m.out = ground;
    m.sink           = geo_num(&c_sink, "road_sink");
    m.margin         = 1.0f / geo_num(&c_grid, "road_ground_grid");
    m.reach          = (float)bank_reach();
    /*  The fills of the junctions round the point. */
    for (dr = -1; dr <= 1; ++dr)
        for (dc = -1; dc <= 1; ++dc)
        {
            int32_t c = col + dc, r = row + dr;
            if (c < 0 || r < 0 || c >= s_side || r >= s_side || !s_tile_fill[r * s_side + c])
                continue;
            {
                const Fill *f = &s_fills[s_tile_fill[r * s_side + c] - 1];
                float       e = fill_out(f, x, y), z;
                if (e < m.margin + m.reach)
                {
                    if (!fill_z(f, x, y, &z))
                        z = nearest_z(f, x, y);
                    meet(&m, z, e);
                }
            }
        }
    /*  The arms the junctions stop short of. */
    for (k = 0; k < s_narm; ++k)
    {
        const Fill *f = &s_arms[k].out;
        float       e, z;
        if (fabsf(x - f->cx) > 1.5f + m.margin + m.reach || fabsf(y - f->cy) > 1.5f + m.margin + m.reach)
            continue;
        e = fill_out(f, x, y);
        if (e < m.margin + m.reach)
        {
            if (!fill_z(f, x, y, &z))
                z = nearest_z(f, x, y);
            meet(&m, z, e);
        }
    }
    /*  The streets, up to their ends. */
    {
        float g = (float)bank_ground(x, y, m.ground, m.margin, m.sink);
        m.out   = g < m.out ? g : m.out;
    }
    /*  The stubs and the dead ends' caps. */
    for (k = 0; k < s_nstub; ++k)
    {
        const Stub *st = &s_stubs[k];
        float       dx = st->x1 - st->x0, dy = st->y1 - st->y0, len = hypotf(dx, dy), u, v;
        if (len < 1e-6f)
            continue;
        u = ((x - st->x0) * dx + (y - st->y0) * dy) / len;
        v = fabsf((x - st->x0) * dy - (y - st->y0) * dx) / len;
        if (u < -(m.margin + m.reach) || u > len + m.margin + m.reach)
            continue;
        meet(&m, st->z, fmaxf(v - st->half, fmaxf(-u, u - len)));
    }
    return m.out;
}

/*  One side of a road's lanes `u` units along it (layer 2), as t on that
 *  side.  It gives the outer border of its sidewalk and the asphalt's
 *  outer border.  It also gives the asphalt's inner border, which is a
 *  median's outer border or lane 0.  A side with no sidewalk ends at its
 *  asphalt, and a side with no traffic lane has no asphalt. */
static void side_of(int road, float u, int side, float lane0, float *walk, float *outer, float *inner)
{
    int             sec = network_section_at(road, u), i;
    const NSection *x;
    float           ds, t_in, t_out;
    *walk = *outer = *inner = lane0;
    if (sec < 0)
        return;
    x  = network_section(sec);
    ds = u - x->s;
    for (i = 0; i < x->nlane; i++)
    {
        const NLane *l = network_lane(x->lane0 + i);
        if ((l->id > 0 ? 1 : -1) != side)
            continue;
        network_lane_t(x->lane0 + i, ds, &t_in, &t_out);
        if (l->type == NLANE_MEDIAN)
            *inner = t_out;
        else if (l->type == NLANE_SIDEWALK)
            *walk = t_out;
        else if ((t_out - *outer) * (float)side > 0.0f)
            *outer = t_out;
    }
    if ((*inner - *outer) * (float)side > 0.0f)
        *outer = *inner;
    if ((*outer - *walk) * (float)side > 0.0f)
        *walk = *outer;
}

/*  Where lane 0 of a road lies `u` units along it: its section's offset. */
static float lane0_of(int road, float u)
{
    int             sec = network_section_at(road, u), i;
    const NSection *x;
    float           t_in, t_out;
    if (sec < 0)
        return 0.0f;
    x = network_section(sec);
    for (i = 0; i < x->nlane; i++)
        if (abs(network_lane(x->lane0 + i)->id) == 1)
        {
            network_lane_t(x->lane0 + i, u - x->s, &t_in, &t_out);
            return t_in;
        }
    return 0.0f;
}

/*  A road of the meet's junction of the same family that runs from `from`
 *  to `to`, of sidewalk lanes alone or of none, else -1. */
static int meet_road(const NRoad *r, int from, int to, int walk)
{
    int k;
    for (k = 0; k < s_nroad; ++k)
    {
        const NRoad    *o = network_road(k);
        const NSection *x;
        if (o->junction != r->junction || o->net != r->net || o->pred.kind != NLINK_ROAD || o->succ.kind != NLINK_ROAD ||
            o->pred.index != from || o->succ.index != to || o->nsec < 1)
            continue;
        x = network_section(o->sec0);
        if ((network_lane(x->lane0)->type == NLANE_SIDEWALK) == (walk != 0))
            return k;
    }
    return -1;
}

/*  A street's cross-section at `s` along its line, from the lanes the
 *  network files.  From the right it is its edge, its sidewalk's inner
 *  edge and the asphalt's inner edge, then the same mirrored on the
 *  left.  A street across a level meet is the meet's connecting roads
 *  that run it.  Its own lanes lie on one side, the lanes of the one
 *  that runs back on the other, and the meet's sidewalk lies past each. */
static void street_cross(int road, float s, float t[6])
{
    const NRoad *r = network_road(road);
    float        u = shape_units(road, s), lane0 = lane0_of(road, u), walk, outer, inner;
    if (r->junction < 0 || r->pred.kind != NLINK_ROAD || r->succ.kind != NLINK_ROAD ||
        topology_end(network_junction(r->junction)->end)->kind != END_MEET)
    {
        side_of(road, u, -1, lane0, &walk, &outer, &inner);
        t[0] = walk, t[1] = outer, t[2] = inner;
        side_of(road, u, 1, lane0, &walk, &outer, &inner);
        t[3] = inner, t[4] = outer, t[5] = walk;
        return;
    }
    {
        int   back = meet_road(r, r->succ.index, r->pred.index, 0), k, own;
        float mine[3], theirs[3], w;
        /*  Its lanes lie on one side of its line, and the way back's on
         *  the other.  The way back runs the other way along the same
         *  line. */
        own = network_lane(network_section(r->sec0)->lane0)->id > 0 ? 1 : -1;
        side_of(road, u, own, lane0, &mine[0], &mine[1], &mine[2]);
        theirs[0] = theirs[1] = theirs[2] = -mine[2];
        if (back >= 0)
        {
            float sb = shape_road(back)->length - s, ub = shape_units(back, sb > 0.0f ? sb : 0.0f), l0 = lane0_of(back, ub);
            side_of(back, ub, own, l0, &theirs[0], &theirs[1], &theirs[2]);
            theirs[0] = -theirs[0], theirs[1] = -theirs[1], theirs[2] = -theirs[2];
        }
        /*  The meet's sidewalk either side, past each way's asphalt. */
        k         = meet_road(r, r->pred.index, r->succ.index, 1);
        w         = k >= 0 ? network_lane_width(network_section(network_road(k)->sec0)->lane0, 0.0f) : 0.0f;
        mine[0]   = mine[1] + (float)own * w;
        k         = meet_road(r, r->succ.index, r->pred.index, 1);
        w         = k >= 0 ? network_lane_width(network_section(network_road(k)->sec0)->lane0, 0.0f) : 0.0f;
        theirs[0] = theirs[1] - (float)own * w;
        if (own < 0)
            t[0] = mine[0], t[1] = mine[1], t[2] = mine[2], t[3] = theirs[2], t[4] = theirs[1], t[5] = theirs[0];
        else
            t[0] = theirs[0], t[1] = theirs[1], t[2] = theirs[2], t[3] = mine[2], t[4] = mine[1], t[5] = mine[0];
    }
}

/*  The cross-section across a strip of street, six points from one edge to
 *  the other.  They are the edge, the sidewalk's inner edge, the median's two
 *  edges, the other sidewalk's inner edge and the other edge.  The median has
 *  no width but on a boulevard. */
static void across(float half, float side, float mid, float t[6])
{
    t[0] = -half, t[1] = -half + side, t[2] = -mid, t[3] = mid, t[4] = half - side, t[5] = half;
}

/*  Where the line through p along d meets the line through q along e. */
static void meet_lines(const float p[2], const float d[2], const float q[2], const float e[2], float out[2])
{
    float den = d[0] * e[1] - d[1] * e[0], t;
    if (fabsf(den) < 1e-9f)
    {
        out[0] = p[0], out[1] = p[1];
        return;
    }
    t      = ((q[0] - p[0]) * e[1] - (q[1] - p[1]) * e[0]) / den;
    out[0] = p[0] + d[0] * t, out[1] = p[1] + d[1] * t;
}

/*  A cross-section from a's point r (one edge) to l (the other), with the
 *  given median, at height z. */
static void section(const float r[2], const float l[2], float z, float side, float mid, float out[6][3])
{
    float cx = 0.5f * (r[0] + l[0]), cy = 0.5f * (r[1] + l[1]), dx = l[0] - r[0], dy = l[1] - r[1];
    float len = hypotf(dx, dy), t[6];
    int   k;
    if (len < 1e-9f)
        len = 1.0f;
    across(0.5f * len, side, mid, t);
    for (k = 0; k < 6; ++k)
        out[k][0] = cx + dx / len * t[k], out[k][1] = cy + dy / len * t[k], out[k][2] = z;
}

/*  The foot of a railway's gravel on one side: the railway's connecting
 *  road across the meet, and the side. */
typedef struct
{
    int   road;
    float side;
} Foot;

/*  How far along d from p the line through p meets the foot of one
 *  railway's gravel, as the pile lays it.  It looks FOOT_REACH either
 *  way. */
#define FOOT_MOST  4096
#define FOOT_REACH 1.5f
static void foot_hit_road(int road, float side, const float p[2], const float d[2], float *best, int *hit)
{
    static float xy[2 * FOOT_MOST];
    int          n = rail_foot_line(road, side, xy, FOOT_MOST), k;
    for (k = 1; k < n; ++k)
    {
        const float *a = &xy[2 * (k - 1)], *b = &xy[2 * k];
        float        ex = b[0] - a[0], ey = b[1] - a[1], den = d[0] * ey - d[1] * ex, at, v;
        if (fabsf(den) < 1e-9f)
            continue;
        at = ((a[0] - p[0]) * ey - (a[1] - p[1]) * ex) / den;
        v  = ((a[0] - p[0]) * d[1] - (a[1] - p[1]) * d[0]) / den;
        if (v >= 0.0f && v <= 1.0f && fabsf(at) < FOOT_REACH && fabsf(at) < fabsf(*best))
            *best = at, *hit = 1;
    }
}

/*  How far along d from p the line through p meets a railway's foot.
 *  The foot is that of its connecting road across the meet, and of each
 *  drawn railway that road joins.  A railway that runs the other way has
 *  its sides turned.  The nearest meet counts. 0 where it meets none. */
static int foot_hit(const Foot *ft, const float p[2], const float d[2], float *u)
{
    const NRoad *r    = network_road(ft->road);
    float        best = 1e9f;
    int          hit  = 0, e;
    foot_hit_road(ft->road, ft->side, p, d, &best, &hit);
    for (e = 0; e < 2; ++e)
    {
        const NLink *l = e ? &r->succ : &r->pred;
        /*  The road before runs the same way where it ends here, and the
         *  road after where it starts here. */
        int same = e ? l->contact == 0 : l->contact == 1;
        if (l->kind == NLINK_ROAD && laid_road(l->index) && network_road(l->index)->net == RNET_RAIL)
            foot_hit_road(l->index, same ? ft->side : -ft->side, p, d, &best, &hit);
    }
    *u = best;
    return hit;
}

/*  Where a street across a level meet gives way to a railway across it.  The railway's gravel covers the street's line from g0 to g1.  The gravel's foot before is foot[0], and the foot after is foot[1].  The first of the street's edges meets the foot before at g0.  The last meets the foot after at g1.  0 where it gives way to nothing. */
static int gives_way(int i, float *g0, float *g1, Foot foot[2])
{
    const NRoad *r = network_road(i);
    int          j;
    if (r->junction < 0 || topology_end(network_junction(r->junction)->end)->kind != END_MEET)
        return 0;
    for (j = 0; j < s_nroad; ++j)
    {
        const NRoad *o = network_road(j);
        float        L = shape_road(i)->length, M, best = 1e9f, sb = 0.0f, tb = 0.0f, s, t;
        if (j == i || o->junction != r->junction || o->net != RNET_RAIL || !s_road[j])
            continue;
        M = shape_road(j)->length;
        /*  Where the street's line passes nearest the railway's. */
        for (s = 0.0f; s <= L; s += 0.01f)
            for (t = 0.0f; t <= M; t += 0.01f)
            {
                float xa, ya, ha, xb, yb, hb, d;
                shape_at(i, s, &xa, &ya, &ha);
                shape_at(j, t, &xb, &yb, &hb);
                d = hypotf(xa - xb, ya - yb);
                if (d < best)
                    best = d, sb = s, tb = t;
            }
        {
            float xb, yb, hb, xs, ys, hs, side, cross[6], lo = 1e9f, hi = -1e9f;
            int   q, e;
            street_cross(i, sb, cross);
            /*  The foot toward the street's start is foot 0: the side the
             *  street comes from. */
            shape_at(j, tb, &xb, &yb, &hb);
            shape_at(i, sb, &xs, &ys, &hs);
            side = cosf(hs) * -sinf(hb) + sinf(hs) * cosf(hb) <= 0.0f ? 1.0f : -1.0f;
            for (q = 0; q < 2; ++q)
                foot[q].road = j, foot[q].side = q ? -side : side;
            /*  Each of the street's two edges and its line, run along the
             *  street onto each foot. */
            for (q = 0; q < 2; ++q)
                for (e = 0; e < 3; ++e)
                {
                    float te   = e == 0 ? cross[0] : e == 1 ? 0.0f
                                                            : cross[5];
                    float p[2] = {xs - sinf(hs) * te, ys + cosf(hs) * te}, d[2] = {cosf(hs), sinf(hs)}, u;
                    if (!foot_hit(&foot[q], p, d, &u))
                        return 0;
                    if (q == 0 && sb + u < lo)
                        lo = sb + u;
                    if (q == 1 && sb + u > hi)
                        hi = sb + u;
                }
            *g0 = lo, *g1 = hi;
        }
        return 1;
    }
    return 0;
}

/*  One cross-section of a street stretch.  It holds its points from one
 *  edge to the other, on top and under.  at[e] is the point that starts
 *  strip e.  At an end on a railway's foot each strip is cut into
 *  pieces, so the end follows the foot where it curves. */
enum
{
    ROW_MOST = 160
};
#define ROW_PIECE 0.02f /* tiles: the most a piece of an end on a foot spans */

typedef struct
{
    float top[ROW_MOST][3], bot[ROW_MOST][3];
    int   n, at[6];
} Row;

/*  One strip of the band between two sections, its top and then its
 *  bottom.  The strip is zipped from the one section to the other, so a
 *  strip in pieces meets one that is whole with no gap. */
static void row_strip(RMesh *m, const RCity *c, uint8_t mask_bit, const Row *a, const Row *b, int e, float mat)
{
    int na = a->at[e + 1] - a->at[e], nb = b->at[e + 1] - b->at[e], under;
    for (under = 0; under < 2; ++under)
    {
        const float (*ta)[3] = under ? a->bot : a->top, (*tb)[3] = under ? b->bot : b->top;
        int i = 0, j = 0;
        while (i < na || j < nb)
        {
            const int    ia = a->at[e] + i, jb = b->at[e] + j;
            const int    adv_b = j < nb && (i >= na || (float)(j + 1) * (float)na <= (float)(i + 1) * (float)nb);
            const float *t3    = adv_b ? tb[jb + 1] : ta[ia + 1];
            slab_tri_faced(m, c, mask_bit, ta[ia], tb[jb], t3, mat, under);
            if (adv_b)
                ++j;
            else
                ++i;
        }
    }
}

/*  A stretch of a street from s0 to s1 along its line.  It is a thin
 *  closed slab with five strips across its top, a bottom, two sides and
 *  two ends. */
static void street_stretch(RMesh *m, const RCity *c, uint8_t mask_bit, int i, float s0, float s1, float step, float thick, const float mat[5], float concrete, const Foot *end0, const Foot *end1)
{
    static Row rows[2];
    int        n, k;
    n = (int)ceilf((s1 - s0) / step);
    for (k = 0; k <= n; ++k)
    {
        Row        *cur = &rows[k & 1], *prev = &rows[(k + 1) & 1];
        const Foot *ft = k == 0 ? end0 : k == n ? end1
                                                : NULL;
        float       x, y, h, z, nx, ny, s = s0 + (s1 - s0) * (float)k / (float)(n > 0 ? n : 1), t[6];
        int         e, q;
        road_at(i, s, &x, &y, &h, &z);
        nx = -sinf(h), ny = cosf(h);
        street_cross(i, s, t);
        cur->n = 0;
        for (e = 0; e < 6; ++e)
        {
            int pieces = 1;
            cur->at[e] = cur->n;
            if (e == 5)
                pieces = 0;
            else if (ft)
                pieces = (int)ceilf((t[e + 1] - t[e]) / ROW_PIECE);
            if (e < 5 && pieces < 1)
                pieces = 1;
            for (q = 0; q < pieces || (e == 5 && q == 0); ++q)
            {
                float v = e == 5 ? t[5] : t[e] + (t[e + 1] - t[e]) * (float)q / (float)pieces, *pt = cur->top[cur->n];
                if (cur->n >= ROW_MOST)
                    break;
                pt[0] = x + nx * v, pt[1] = y + ny * v, pt[2] = z;
                /*  An end on a railway's foot: each point slid along the
                 *  street onto it. */
                if (ft)
                {
                    float d[2] = {cosf(h), sinf(h)}, u;
                    if (foot_hit(ft, pt, d, &u))
                        pt[0] += d[0] * u, pt[1] += d[1] * u;
                }
                memcpy(cur->bot[cur->n], pt, sizeof cur->bot[0]);
                cur->bot[cur->n][2] -= thick;
                ++cur->n;
            }
        }
        /*  Strip by strip: its end, then the band since the last section. */
        for (e = 0; e < 5; ++e)
        {
            if (k == 0 || k == n)
                for (q = cur->at[e]; q < cur->at[e + 1]; ++q)
                    slab_quad(m, c, mask_bit, cur->top[q], cur->top[q + 1], cur->bot[q + 1], cur->bot[q], mat[e]);
            if (k > 0)
                row_strip(m, c, mask_bit, prev, cur, e, mat[e]);
        }
        if (k > 0)
        {
            slab_quad(m, c, mask_bit, prev->top[0], cur->top[0], cur->bot[0], prev->bot[0], concrete);
            slab_quad(m, c, mask_bit, prev->top[prev->n - 1], cur->top[cur->n - 1], cur->bot[cur->n - 1], prev->bot[prev->n - 1], concrete);
        }
    }
}

/*  A drawn junction, from its rings (fill_rings).  A sidewalk lies along
 *  each corner edge, between the boundary and the sidewalk ring.  A mouth's
 *  edge has none: an arm comes in there.  The asphalt is the ring's
 *  triangles.  The slab is closed under them by the bottom, and its sides
 *  are cut where its top's edges are. */
static void fill_draw(RMesh *m, const RCity *c, uint8_t mask_bit, const Fill *f, float thick, float asphalt, float concrete)
{
    int k;
    if (f->ntin < 1 || f->ntloop < 1)
        return;
    /*  A railway's junction is drawn by rails.c. */
    if (f->rail)
        return;
    /*  A corner's sidewalk, where it has one: an edge whose ring lies on the
     *  boundary has none. */
    for (k = 0; k < f->np; ++k)
    {
        const float *p0 = &f->pt[3 * k], *p1 = &f->pt[3 * ((k + 1) % f->np)];
        const float *q0 = &f->in[3 * f->ins[k]], *q1 = &f->in[3 * f->ine[(k + 1) % f->np]];
        if (shape_boundary_mouth(f->junction, k) ||
            (hypotf(p0[0] - q0[0], p0[1] - q0[1]) < 1e-5f && hypotf(p1[0] - q1[0], p1[1] - q1[1]) < 1e-5f))
            continue;
        slab_quad(m, c, mask_bit, p0, p1, q1, q0, concrete);
    }
    for (k = 0; k < f->ntin; ++k)
        slab_tri(m, c, mask_bit, &f->in[3 * f->tin[3 * k]], &f->in[3 * f->tin[3 * k + 1]], &f->in[3 * f->tin[3 * k + 2]], asphalt);
    for (k = 0; k < f->ntloop; ++k)
    {
        float lo[3][3];
        int   e;
        for (e = 0; e < 3; ++e)
        {
            memcpy(lo[e], &f->loop[3 * f->tloop[3 * k + e]], sizeof lo[e]);
            lo[e][2] -= thick;
        }
        slab_tri_faced(m, c, mask_bit, lo[0], lo[1], lo[2], asphalt, 1);
    }
    for (k = 0; k < f->nloop; ++k)
    {
        const float *a = &f->loop[3 * k], *b = &f->loop[3 * ((k + 1) % f->nloop)];
        float        al[3] = {a[0], a[1], a[2] - thick}, bl[3] = {b[0], b[1], b[2] - thick};
        slab_quad(m, c, mask_bit, a, b, bl, al, concrete);
    }
}

void roads_draw(RMesh *m, const RCity *c, uint8_t mask_bit)
{
    const float step = geo_num(&c_step, "road_step"), thick = geo_num(&c_sink, "road_sink") * 0.5f;
    float       asphalt, concrete;
    int         i, k;
    if (!s_ready)
        return;
    laid_materials(&asphalt, &concrete);
    /*  A street is a thin closed slab.  Its surface is on top, and its bottom
     *  is half its sink under it, clear of the ground.  Its sides and ends
     *  close it.  Across it run five strips: a sidewalk, the lanes one way,
     *  the median, the lanes the other way and a sidewalk.  The median has
     *  no width but on a boulevard, and a strip of no width lays nothing. */
    for (i = 0; i < s_nroad; ++i)
    {
        float   L, mat[5], s0, s1, x0, y0, h0, z0;
        ShapeId id;
        if (!s_road[i] || network_road(i)->net != RNET_ROAD || laid_deck(i) || !laid_near(i))
            continue;
        road_at(i, 0.0f, &x0, &y0, &h0, &z0);
        id = shape_open("street, road %016llx from %d,%d", (unsigned long long)network_road(i)->id, (int)floorf(x0), (int)floorf(y0));
        laid_ref_road(i);
        mat[0] = concrete, mat[1] = asphalt, mat[2] = concrete, mat[3] = asphalt, mat[4] = concrete;
        L = shape_road(i)->length;
        /*  From where its junction at each end leaves off. */
        {
            float c0 = shape_cut(i, 0) > 0.0f ? shape_cut(i, 0) : 0.0f, c1 = shape_cut(i, 1) > 0.0f ? shape_cut(i, 1) : 0.0f;
            s0 = c0 < L * 0.5f ? c0 : L * 0.5f;
            s1 = L - (c1 < L * 0.5f ? c1 : L * 0.5f);
        }
        /*  The street is laid between its gaps, each a stretch it is not
         *  laid on.  Across a level meet a railway has priority over a
         *  street.  The railway is drawn whole, and the street gives way
         *  where the railway's gravel crosses it.  Its ends are cut at the
         *  gravel's foot.  And the street is not laid over its bridges. */
        {
            float       g[9][2], at       = s0;
            const Foot *foot[9][2], *from = NULL;
            Foot        edge[2];
            int         ng = 0, a, b;
            if (gives_way(i, &g[0][0], &g[0][1], edge))
                foot[0][0] = &edge[0], foot[0][1] = &edge[1], ng = 1;
            {
                float br[8][2];
                int   nb = bridge_gaps(i, br, 8);
                for (a = 0; a < nb; ++a, ++ng)
                    g[ng][0] = br[a][0], g[ng][1] = br[a][1], foot[ng][0] = foot[ng][1] = NULL;
            }
            /*  In order along the line. */
            for (a = 1; a < ng; ++a)
                for (b = a; b > 0 && g[b][0] < g[b - 1][0]; --b)
                {
                    float       tg[2] = {g[b][0], g[b][1]};
                    const Foot *tf[2] = {foot[b][0], foot[b][1]};
                    g[b][0] = g[b - 1][0], g[b][1] = g[b - 1][1], foot[b][0] = foot[b - 1][0], foot[b][1] = foot[b - 1][1];
                    g[b - 1][0] = tg[0], g[b - 1][1] = tg[1], foot[b - 1][0] = tf[0], foot[b - 1][1] = tf[1];
                }
            /*  A stretch a gap covers to its end is not laid. */
            for (a = 0; a < ng; ++a)
            {
                if (g[a][0] > at && at < s1)
                    street_stretch(m, c, mask_bit, i, at, g[a][0] < s1 ? g[a][0] : s1, step, thick, mat, concrete, from, foot[a][0]);
                if (g[a][1] > at)
                    at = g[a][1], from = foot[a][1];
            }
            if (at < s1)
                street_stretch(m, c, mask_bit, i, at, s1, step, thick, mat, concrete, from, NULL);
        }
        shape_close(id);
    }
    /*  Each stub, a straight piece of street, closed the same way. */
    for (i = 0; i < s_nstub; ++i)
    {
        const Stub *st = &s_stubs[i];
        float       dx = st->x1 - st->x0, dy = st->y1 - st->y0, len = hypotf(dx, dy), nx, ny, a[2][4][3], b[2][4][3];
        float       mat[3] = {concrete, asphalt, concrete};
        int         end, e;
        ShapeId     id;
        if (len < 1e-6f || st->net != RNET_ROAD || !laid_near_xy(st->x0, st->y0, len + 4.0f))
            continue;
        id = shape_open("street end at %d,%d", (int)floorf(st->x0), (int)floorf(st->y0));
        shape_net(RNET_ROAD);
        nx = -dy / len, ny = dx / len;
        for (end = 0; end < 2; ++end)
            for (e = 0; e < 4; ++e)
            {
                static const float T[4] = {-1.0f, -1.0f, 1.0f, 1.0f};
                float              t    = T[e] * (e == 1 || e == 2 ? st->half - st->walk : st->half);
                a[end][e][0] = (end ? st->x1 : st->x0) + nx * t, a[end][e][1] = (end ? st->y1 : st->y0) + ny * t;
                a[end][e][2] = st->z;
                memcpy(b[end][e], a[end][e], sizeof b[end][e]);
                b[end][e][2] -= thick;
            }
        for (e = 0; e < 3; ++e)
        {
            slab_quad(m, c, mask_bit, a[0][e], a[1][e], a[1][e + 1], a[0][e + 1], mat[e]);
            slab_quad_under(m, c, mask_bit, b[0][e], b[1][e], b[1][e + 1], b[0][e + 1], mat[e]);
            slab_quad(m, c, mask_bit, a[0][e], a[0][e + 1], b[0][e + 1], b[0][e], mat[e]);
            slab_quad(m, c, mask_bit, a[1][e], a[1][e + 1], b[1][e + 1], b[1][e], mat[e]);
        }
        slab_quad(m, c, mask_bit, a[0][0], a[1][0], b[1][0], b[0][0], concrete);
        slab_quad(m, c, mask_bit, a[0][3], a[1][3], b[1][3], b[0][3], concrete);
        shape_close(id);
    }
    /*  Each junction's fill.  Its outline runs round the arms' mouths and
     *  the corners between them.  A sidewalk lies along each corner edge,
     *  `side` wide, and asphalt fills what is inside them.  A mouth edge has
     *  no sidewalk: an arm comes in there.  The slab is closed the same way
     *  as a street's, its sides cut where its top's edges are. */
    for (i = 0; i < s_nfill; ++i)
    {
        const Fill *f = &s_fills[i];
        if (!laid_near_xy(f->cx, f->cy, 8.0f))
            continue;
        ShapeId id = shape_open("junction %016llx at %d,%d", (unsigned long long)network_junction(f->junction)->id, (int)floorf(f->cx), (int)floorf(f->cy));
        laid_ref_junction(f->junction, RNET_ROAD);
        fill_draw(m, c, mask_bit, f, thick, asphalt, concrete);
        for (k = 0; k < s_narm; ++k)
            if (s_arms[k].fill == i)
            {
                const float mat[5] = {concrete, asphalt, concrete, asphalt, concrete};
                slab_strips(m, c, mask_bit, (const float (*)[3])s_arms[k].from, (const float (*)[3])s_arms[k].to, 6, thick, mat);
            }
        shape_close(id);
    }
}

/*  The area a fill's triangles of one ring cover, each counted once
 *  whichever way it turns.  Where it is the ring's own area, they cover the
 *  ring once. */
static float tris_area(const float *pt, const int *tri, int nt)
{
    float a = 0.0f;
    int   k;
    for (k = 0; k < nt; ++k)
    {
        const float *p = &pt[3 * tri[3 * k]], *q = &pt[3 * tri[3 * k + 1]], *r = &pt[3 * tri[3 * k + 2]];
        a += 0.5f * fabsf((q[0] - p[0]) * (r[1] - p[1]) - (q[1] - p[1]) * (r[0] - p[0]));
    }
    return a;
}

/*  One fault of a fill, said with where it is.  With more said, the fill's
 *  boundary and its sidewalk ring are listed after its first fault. */
static void fill_fault(const Fill *f, int verbose, const char *what, int *faults)
{
    static const Fill *said;
    int                k;
    ++*faults;
    if (verbose)
        dumpf("roads check  junction %d at %d,%d: %s\n", f->junction, (int)floorf(f->cx), (int)floorf(f->cy), what);
    if (verbose > 1 && said != f)
    {
        said = f;
        for (k = 0; k < f->np; ++k)
            dumpf("roads check    boundary %d %.4f %.4f%s ring %.4f %.4f %.4f %.4f\n", k, (double)f->pt[3 * k], (double)f->pt[3 * k + 1], shape_boundary_mouth(f->junction, k) ? " mouth" : "", (double)f->in[3 * f->ine[k]], (double)f->in[3 * f->ine[k] + 1], (double)f->in[3 * f->ins[k]], (double)f->in[3 * f->ins[k] + 1]);
    }
}

int roads_check(int verbose)
{
    static const char *WHY[NOFILL_N] = {"a tile of it carries something no street runs on", "a street that reaches it is not drawn", "it has more arms than a fill holds", "its boundary has fewer than three points"};
    int                i, k, faults = 0, nofill = 0;
    if (!s_ready)
        return -1;
    for (i = 0; i < s_nfill; ++i)
    {
        const Fill *f    = &s_fills[i];
        float       area = poly_area(f->pt, 3, f->np);
        if (!poly_simple(f->pt, 3, f->np))
        {
            fill_fault(f, verbose, "its boundary crosses itself", &faults);
        }
        /*  A railway's junction is its gravel's foot alone: its outline
         *  and its triangles are all there is to hold. */
        if (f->rail)
        {
            if (f->ntri < 1 || fabsf(tris_area(f->pt, f->tri, f->ntri) - fabsf(area)) > 1e-4f)
                fill_fault(f, verbose, "its gravel's triangles do not cover it once", &faults);
            continue;
        }
        if (!poly_simple(f->in, 3, f->nin))
            fill_fault(f, verbose, "its sidewalk ring crosses itself", &faults);
        for (k = 0; k < f->nin; ++k)
            if (fill_out(f, f->in[3 * k], f->in[3 * k + 1]) > 1e-4f)
            {
                fill_fault(f, verbose, "its sidewalk ring runs outside its boundary", &faults);
                break;
            }
        if (f->ntri < 1 || fabsf(tris_area(f->pt, f->tri, f->ntri) - fabsf(area)) > 1e-4f)
        {
            char say[96];
            snprintf(say, sizeof say, "its boundary's triangles do not cover it once (%d of %d, area %.4f of %.4f)", f->ntri, f->np - 2, (double)(f->ntri > 0 ? tris_area(f->pt, f->tri, f->ntri) : 0.0f), (double)area);
            fill_fault(f, verbose, say, &faults);
        }
        if (f->ntin < 1 || fabsf(tris_area(f->in, f->tin, f->ntin) - fabsf(poly_area(f->in, 3, f->nin))) > 1e-4f)
            fill_fault(f, verbose, "its asphalt's triangles do not cover the sidewalk ring once", &faults);
        if (f->ntloop < 1 || fabsf(tris_area(f->loop, f->tloop, f->ntloop) - fabsf(poly_area(f->loop, 3, f->nloop))) > 1e-4f)
            fill_fault(f, verbose, "its bottom's triangles do not cover it once", &faults);
        /*  Each sidewalk turns as the boundary does, so it lies inside. */
        for (k = 0; k < f->np; ++k)
        {
            float q[4][3];
            if (shape_boundary_mouth(f->junction, k))
                continue;
            memcpy(q[0], &f->pt[3 * k], sizeof q[0]);
            memcpy(q[1], &f->pt[3 * ((k + 1) % f->np)], sizeof q[1]);
            memcpy(q[2], &f->in[3 * f->ine[(k + 1) % f->np]], sizeof q[2]);
            memcpy(q[3], &f->in[3 * f->ins[k]], sizeof q[3]);
            if (!poly_simple(&q[0][0], 3, 4) || poly_area(&q[0][0], 3, 4) * f->turn < -1e-6f)
            {
                char say[160];
                snprintf(say, sizeof say, "a sidewalk of it folds over: %.3f,%.3f %.3f,%.3f in %.3f,%.3f %.3f,%.3f", (double)q[0][0], (double)q[0][1], (double)q[1][0], (double)q[1][1], (double)q[2][0], (double)q[2][1], (double)q[3][0], (double)q[3][1]);
                fill_fault(f, verbose, say, &faults);
                break;
            }
        }
    }
    for (i = 0; i < s_narm; ++i)
        if (!poly_simple(s_arms[i].pt, 3, 4))
            fill_fault(&s_fills[s_arms[i].fill], verbose, "an arm it stops short of crosses itself", &faults);
    for (k = 0; k < NOFILL_N; ++k)
        if (s_nofill[k])
        {
            nofill += s_nofill[k];
            if (verbose)
                dumpf("roads check  %d junctions not drawn: %s, the first at %d,%d\n", s_nofill[k], WHY[k], (int)(s_nofill_at[k] % s_side), (int)(s_nofill_at[k] / s_side));
        }
    if (verbose)
        dumpf("roads check  %d junctions drawn, %d arms, %d not drawn, %d faults\n", s_nfill, s_narm, nofill, faults);
    return faults;
}
