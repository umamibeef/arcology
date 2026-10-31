/*  draw/rail_shape.c: the railway's footprint (rail_shape.h).  Its
 *  numbers, its way and its line, the stations along it, its foot and
 *  the gravel of a junction.  The track drawing (rails.c) and the street
 *  drawing (roads.c) both read it, so a level crossing has one owner. */
#include "rail_shape.h"

#include <math.h>
#include <string.h>

#include "geo.h"
#include "laid.h"
#include "network/network.h"
#include "network/shape.h"
#include "network/topology.h"

#define GRAVEL_STEP 0.02f
static PolySet    s_strips;
static RailGravel s_gr;
static float      s_st[SWEEP_MOST];

/*  A knob of the tuning window, or `def` where it has none. */
static float tuned(const char *name, float def)
{
    float v;
    return tune_find(name, &v) ? v : def;
}
void rail_knobs(RailKnobs *k)
{
    static int c[13] = {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1};
    k->bed           = tuned("rail_bed_half", 0.06f);
    /*  A railway's lanes never ease, so each lies in the same place on
     *  every railway.  Each track lies halfway across its rail lane, and
     *  the gravel's foot at the outer border of its border lanes. */
    k->foot         = network_width(RNET_RAIL, 0) * 0.5f;
    k->height       = geo_num(&c[2], "rail_bed_height");
    k->offset       = network_type_width(RNET_RAIL, NLANE_RAIL) * 0.5f;
    k->gauge        = geo_num(&c[4], "rail_gauge");
    k->rail_h       = geo_num(&c[5], "rail_height");
    k->rail_w       = geo_num(&c[6], "rail_width");
    k->gap          = geo_num(&c[7], "rail_sleeper_gap");
    k->sleeper_half = geo_num(&c[8], "rail_sleeper_half");
    k->sleeper_w    = geo_num(&c[9], "rail_sleeper_width");
    k->sleeper_h    = geo_num(&c[10], "rail_sleeper_height");
    k->step         = geo_num(&c[11], "road_step");
    k->sink         = geo_num(&c[12], "road_sink");
    k->stain        = tuned("rail_stain_half", 0.018f);
    k->dark         = tuned("rail_stain_dark", 0.5f);
    k->buf_half     = tuned("rail_buffer_half", 0.022f);
    k->buf_depth    = tuned("rail_buffer_depth", 0.03f);
    k->buf_h        = tuned("rail_buffer_height", 0.024f);
    /*  The bed holds both tracks and their dark gravel, inside the foot. */
    if (k->bed < k->offset + k->stain)
        k->bed = k->offset + k->stain;
    if (k->bed > k->foot)
        k->bed = k->foot;
}
float rail_foot(void)
{
    RailKnobs k;
    rail_knobs(&k);
    return k.foot;
}
/*  A point of a road's line at s, its heading and its height.  Past either
 *  end of the line, over a cap at a dead end, the line runs straight on
 *  along its heading there, level. */
void rail_way_at(int road, float s, float *x, float *y, float *h, float *z)
{
    float L = shape_road(road)->length, e = s < 0.0f ? 0.0f : s > L ? L
                                                                    : s;
    shape_at(road, e, x, y, h);
    *z = shape_height(road, e);
    *x += cosf(*h) * (s - e), *y += sinf(*h) * (s - e);
}
/*  A point of a road's way at s, moved t across it, with its height. */
void rail_line_at(int road, float s, float t, float lift, float out[3])
{
    float x, y, h, z;
    rail_way_at(road, s, &x, &y, &h, &z);
    out[0] = x - sinf(h) * t, out[1] = y + cosf(h) * t, out[2] = z + lift;
}
/*  Where a railway's way runs from and to: its line, and its caps past its
 *  ends (laid_cap). */
float rail_way_from(int road)
{
    return -laid_cap(road, 0);
}

float rail_way_to(int road)
{
    return shape_road(road)->length + laid_cap(road, 1);
}
/*  The stations of a sweep along a road from s0 to s1, no further apart
 *  than step.  Answers how many. */
int rail_stations(float s0, float s1, float step, float *st)
{
    int n = (int)ceilf((s1 - s0) / step), k;
    if (n < 1)
        n = 1;
    if (n + 1 > SWEEP_MOST)
        n = SWEEP_MOST - 1;
    for (k = 0; k <= n; ++k)
        st[k] = s0 + (s1 - s0) * (float)k / (float)n;
    return n + 1;
}
/*  The side of its road's line a rail lane lies on: its id's sign. */
float rail_lane_side(const NRoad *r, int which)
{
    int s, l, seen = 0;
    for (s = 0; s < r->nsec; ++s)
    {
        const NSection *sec = network_section(r->sec0 + s);
        for (l = 0; l < sec->nlane; ++l)
        {
            const NLane *ln = network_lane(sec->lane0 + l);
            if (ln->type != NLANE_RAIL)
                continue;
            if (seen++ == which)
                return ln->id > 0 ? 1.0f : -1.0f;
        }
    }
    return 0.0f;
}
/*  Whether a connecting road of a railway's junction is drawn: it joins two
 *  drawn railways (laid_reach).  A level meet's is
 *  drawn as a railway itself (roads.c), so it is no junction's here. */
int rail_connection_drawn(int i)
{
    const NRoad *r = network_road(i);
    return r->junction >= 0 && r->net == RNET_RAIL && shape_road(i)->ngeom && r->pred.kind == NLINK_ROAD &&
           r->succ.kind == NLINK_ROAD && rail_lane_side(r, 0) != 0.0f &&
           topology_end(network_junction(r->junction)->end)->kind != END_MEET &&
           laid_reach(r->pred.index) && laid_reach(r->succ.index);
}
static void strip_at(PolySet *set, int road, float t, float half);
/*  The strip either side of a road's line, `half` wide each way. */
static void strip(PolySet *set, int road, float half)
{
    strip_at(set, road, 0.0f, half);
}
/*  The strip either side of the line t across a road's line, `half` wide
 *  each way. */
static void strip_at(PolySet *set, int road, float t, float half)
{
    float L = shape_road(road)->length, st[512], p[3];
    int   n = (int)ceilf(L / GRAVEL_STEP), q;
    if (n < 1)
        n = 1;
    if (n > 255 || set->nring >= POLY_RINGS || set->npt + 2 * (n + 1) > POLY_POINTS)
        return;
    for (q = 0; q <= n; ++q)
        st[q] = L * (float)q / (float)n;
    set->start[set->nring] = set->npt, set->count[set->nring] = 2 * (n + 1), set->hole[set->nring] = 0;
    set->poly[set->nring] = set->nring;
    for (q = 0; q <= n; ++q)
    {
        rail_line_at(road, st[q], t + half, 0.0f, p);
        set->xy[2 * set->npt] = p[0], set->xy[2 * set->npt + 1] = p[1], set->npt++;
    }
    for (q = n; q >= 0; --q)
    {
        rail_line_at(road, st[q], t - half, 0.0f, p);
        set->xy[2 * set->npt] = p[0], set->xy[2 * set->npt + 1] = p[1], set->npt++;
    }
    set->nring++;
}
/*  The height of the gravel's foot at a point of a junction, as `g` holds it. */
float rail_gravel_z(const RailGravel *g, float x, float y)
{
    float best = 1e18f, z = 0.0f;
    int   k;
    for (k = 0; k < g->ngz; ++k)
    {
        float d = (g->gz[k][0] - x) * (g->gz[k][0] - x) + (g->gz[k][1] - y) * (g->gz[k][1] - y);
        if (d < best)
            best = d, z = g->gz[k][2];
    }
    return z;
}

/*  A junction's gravel: the union of the piles of its drawn connecting
 *  roads (Clipper2, mesh/poly.cpp).  Its foot is the union of each one's
 *  foot strip, and its bed the union of each one's bed strip.  So where
 *  two branches part, grass shows between them.  Its slope is the foot
 *  less the bed.  A point's height is that of the nearest point of a
 *  connecting road's line under it. */
RailGravel *rail_gravel(int j, const RailKnobs *k, int *kind)
{
    int i, n = 0;
    s_strips.nring = s_strips.npt = 0, s_gr.ngz = 0;
    for (i = 0; i < network_roads(); ++i)
        if (network_road(i)->junction == j && rail_connection_drawn(i))
        {
            float L = shape_road(i)->length, sx, p[3];
            strip(&s_strips, i, k->foot);
            for (sx = 0.0f; s_gr.ngz < GRAVEL_PTS; sx += GRAVEL_STEP)
            {
                rail_line_at(i, sx < L ? sx : L, 0.0f, 0.0f, p);
                memcpy(s_gr.gz[s_gr.ngz++], p, sizeof p);
                if (sx >= L)
                    break;
            }
            ++n;
        }
    if (!n || poly_clip(&s_strips, NULL, &s_gr.foot) <= 0)
        return *kind = 0, &s_gr;
    s_strips.nring = s_strips.npt = 0;
    for (i = 0; i < network_roads(); ++i)
        if (network_road(i)->junction == j && rail_connection_drawn(i))
            strip(&s_strips, i, k->bed);
    if (poly_clip(&s_strips, NULL, &s_gr.bed) <= 0 || poly_clip(&s_gr.foot, &s_gr.bed, &s_gr.band) < 0)
        return *kind = 0, &s_gr;
    /*  The darker gravel under each track, inside the bed, and the rest of
     *  the bed's top. */
    s_strips.nring = s_strips.npt = 0;
    for (i = 0; i < network_roads(); ++i)
        if (network_road(i)->junction == j && rail_connection_drawn(i))
            strip_at(&s_strips, i, rail_lane_side(network_road(i), 0) * k->offset, k->stain);
    s_gr.dark.nring = s_gr.light.nring = 0;
    if (s_strips.nring && poly_clip(&s_strips, &s_gr.bed, &s_gr.off) >= 0 && poly_clip(&s_strips, &s_gr.off, &s_gr.dark) >= 0 &&
        poly_clip(&s_gr.bed, &s_gr.dark, &s_gr.light) >= 0)
        return *kind = 2, &s_gr;
    return *kind = 1, &s_gr;
}
int rail_gravel_outline(int junction, float *xyz, int most)
{
    RailKnobs k;
    int       q, n;
    rail_knobs(&k);
    RailGravel *g = rail_gravel(junction, &k, &q);
    if (!q || g->foot.nring < 1)
        return 0;
    n = g->foot.count[0] < most ? g->foot.count[0] : most;
    for (q = 0; q < n; ++q)
    {
        float x = g->foot.xy[2 * (g->foot.start[0] + q)], y = g->foot.xy[2 * (g->foot.start[0] + q) + 1];
        xyz[3 * q] = x, xyz[3 * q + 1] = y, xyz[3 * q + 2] = rail_gravel_z(g, x, y);
    }
    return n;
}
int rail_foot_line(int road, float side, float *xy, int most)
{
    RailKnobs k;
    float     pt[3];
    int       n, q;
    rail_knobs(&k);
    n = rail_stations(rail_way_from(road), rail_way_to(road), k.step, s_st);
    if (n > most)
        n = most;
    for (q = 0; q < n; ++q)
    {
        rail_line_at(road, s_st[q], side * k.foot, 0.0f, pt);
        xy[2 * q] = pt[0], xy[2 * q + 1] = pt[1];
    }
    return n;
}
