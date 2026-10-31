/*  app/net_pick.c: the road network's record a drawn shape names, and
 *  what the records hold of it.  See app/net_pick.h. */
#include "net_pick.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mesh/shape.h"
#include "network/network.h"
#include "network/shape.h"
#include "network/store.h"
#include "network/topology.h"

/* ---- ids to records ----------------------------------------------------- */

/*  The roads' and the junctions' ids, each to its index, in one table
 *  built once a network.  A slot holds an id, whether it is a junction's,
 *  and the index, -1 in an empty slot. */
typedef struct
{
    uint64_t id;
    int      junction, at;
} IdSlot;
static IdSlot         *s_ids;
static uint32_t        s_slots;
static int             s_built;
static const NetStore *s_built_for; /* the network the table was built from */

void net_pick_forget(void)
{
    s_built = 0;
}

static uint32_t slot_of(uint64_t id, int junction)
{
    return (uint32_t)(((id ^ (uint64_t)junction) * 0xBF58476D1CE4E5B9ull) >> 32) & (s_slots - 1u);
}

static int ids_build(void)
{
    int      i, j, n = network_roads() + network_junctions();
    uint32_t want = 1024, k;
    if (s_built && s_built_for == net_store_loaded())
        return 0;
    while (want < 2u * (uint32_t)n)
        want *= 2u;
    if (want != s_slots)
    {
        IdSlot *ns = (IdSlot *)realloc(s_ids, want * sizeof *ns);
        if (!ns)
            return -1;
        s_ids = ns, s_slots = want;
    }
    for (k = 0; k < s_slots; ++k)
        s_ids[k].at = -1;
    for (j = 0; j < 2; ++j)
        for (i = 0; i < (j ? network_junctions() : network_roads()); ++i)
        {
            uint64_t id = j ? network_junction(i)->id : network_road(i)->id;
            for (k = slot_of(id, j); s_ids[k].at >= 0; k = (k + 1u) & (s_slots - 1u))
                ;
            s_ids[k].id = id, s_ids[k].junction = j, s_ids[k].at = i;
        }
    s_built     = 1;
    s_built_for = net_store_loaded();
    return 0;
}

void net_pick_load(const NetPick *p)
{
    net_store_use_at((int32_t)floorf(p->hit[0]), (int32_t)floorf(p->hit[1]));
}

/*  The index of the road, or with `junction` the junction, of an id, or
 *  -1. */
static int ids_get(uint64_t id, int junction)
{
    uint32_t k;
    if (ids_build() != 0)
        return -1;
    for (k = slot_of(id, junction); s_ids[k].at >= 0; k = (k + 1u) & (s_slots - 1u))
        if (s_ids[k].id == id && s_ids[k].junction == junction)
            return s_ids[k].at;
    return -1;
}

static int shaped(int road)
{
    const SRoad *sr = road >= 0 ? shape_road(road) : NULL;
    return sr && sr->ngeom > 0 && sr->length > 1e-4f;
}

int net_pick_record(uint32_t shape, int *road, int *lane, int *junction)
{
    int      kind;
    uint64_t key, lane_key = 0;
    ShapeId  at       = shape_ref_of(shape, &kind, &key);
    int      has_lane = 0;
    *road = *lane = *junction = -1;
    if (at == SHAPE_NONE)
        return 0;
    if (kind == NREF_LANE)
    {
        has_lane = 1, lane_key = key;
        if ((at = shape_ref_of(shape_parent(at), &kind, &key)) == SHAPE_NONE)
            return 0;
    }
    if (kind == NLINK_JUNCTION)
        return (*junction = ids_get(key, 1)) >= 0;
    if (kind != NLINK_ROAD || (*road = ids_get(key, 0)) < 0)
        return 0;
    if (has_lane)
    {
        const NRoad *r   = network_road(*road);
        int          sec = r->sec0 + NREF_LANE_SECTION(lane_key), k;
        if (sec < r->sec0 + r->nsec)
            for (k = 0; k < network_section(sec)->nlane; ++k)
                if (network_lane(network_section(sec)->lane0 + k)->id == NREF_LANE_ID(lane_key))
                    *lane = network_section(sec)->lane0 + k;
    }
    return 1;
}

/*  The point of a road's line nearest (x, y), its `s` and its `t`.  0 for
 *  a road with no line. */
static int project(int road, float x, float y, float *s_out, float *t_out)
{
    const float step = 0.1f;
    float       L, best = 1e30f, bs = 0.0f, lo, hi, px, py, h;
    int         n, k, it;
    if (!shaped(road))
        return 0;
    L = shape_road(road)->length;
    n = (int)ceilf(L / step) + 1;
    n = n < 2 ? 2 : n > 8192 ? 8192
                             : n;
    for (k = 0; k < n; ++k)
    {
        float s2 = L * (float)k / (float)(n - 1), d;
        shape_at(road, s2, &px, &py, &h);
        d = (x - px) * (x - px) + (y - py) * (y - py);
        if (d < best)
            best = d, bs = s2;
    }
    lo = fmaxf(0.0f, bs - L / (float)(n - 1)), hi = fminf(L, bs + L / (float)(n - 1));
    for (it = 0; it < 24; ++it)
    {
        float m1 = lo + (hi - lo) / 3.0f, m2 = hi - (hi - lo) / 3.0f, d1, d2;
        shape_at(road, m1, &px, &py, &h);
        d1 = (x - px) * (x - px) + (y - py) * (y - py);
        shape_at(road, m2, &px, &py, &h);
        d2 = (x - px) * (x - px) + (y - py) * (y - py);
        if (d1 < d2)
            hi = m2;
        else
            lo = m1;
    }
    *s_out = 0.5f * (lo + hi);
    shape_at(road, *s_out, &px, &py, &h);
    *t_out = -(x - px) * sinf(h) + (y - py) * cosf(h);
    return 1;
}

int net_pick_junction_roads(int junction, int *out, int max)
{
    int i, n = 0;
    for (i = 0; i < network_roads() && n < max; ++i)
        if (network_road(i)->junction == junction)
            out[n++] = i;
    return n;
}

int net_pick_at(uint32_t shape, const float hit[3], NetPick *p)
{
    memset(p, 0, sizeof *p);
    p->road = p->section = p->lane = p->junction = -1;
    memcpy(p->hit, hit, sizeof p->hit);
    net_pick_load(p);
    if (!net_pick_record(shape, &p->road, &p->lane, &p->junction))
        return 0;
    if (p->road >= 0)
    {
        const NRoad *r  = network_road(p->road);
        int          on = project(p->road, hit[0], hit[1], &p->s, &p->t);
        /*  The lane's section, or the section at the point's `s`. */
        if (p->lane >= 0)
            p->section = network_lane(p->lane)->section;
        else if (on)
            p->section = network_section_at(p->road, shape_units(p->road, p->s));
        /*  A connecting road is in its junction.  Any other road reaches a
         *  junction by its end nearer the point, where it has one there. */
        if (r->junction >= 0)
            p->junction = r->junction;
        else
        {
            const int    far_end = on && p->s > 0.5f * shape_road(p->road)->length;
            const NLink *near = far_end ? &r->succ : &r->pred, *other = far_end ? &r->pred : &r->succ;
            p->junction = near->kind == NLINK_JUNCTION ? near->index : other->kind == NLINK_JUNCTION ? other->index
                                                                                                     : -1;
        }
    }
    p->level = p->lane >= 0 ? NPICK_LANE : p->road >= 0 ? NPICK_ROAD
                                                        : NPICK_JUNCTION;
    return 1;
}

int net_pick_has(const NetPick *p, int level)
{
    net_pick_load(p);
    switch (level)
    {
        case NPICK_LANE:
            return p->lane >= 0;
        case NPICK_SECTION:
            return p->section >= 0;
        case NPICK_ROAD:
            return p->road >= 0;
        case NPICK_JUNCTION:
            return p->junction >= 0;
        default:
            return 0;
    }
}

/* ---- the words ---------------------------------------------------------- */

typedef struct
{
    char  *buf;
    size_t n, at;
} Out;

static void put(Out *o, const char *fmt, ...)
{
    va_list ap;
    int     w;
    if (o->at + 1 >= o->n)
        return;
    va_start(ap, fmt);
    w = vsnprintf(o->buf + o->at, o->n - o->at, fmt, ap);
    va_end(ap);
    if (w > 0)
        o->at += (size_t)w < o->n - o->at ? (size_t)w : o->n - o->at - 1;
}

static int section_index(int sec)
{
    int road = network_section(sec)->nlane ? network_lane(network_section(sec)->lane0)->road : -1;
    return road >= 0 ? sec - network_road(road)->sec0 : -1;
}

/*  A lane by what the dump names it by: its road's id and its own. */
static void lane_name(Out *o, int lane)
{
    const NLane *l = network_lane(lane);
    put(o, "road %016llx section %d lane %d (%s)", (unsigned long long)network_road(l->road)->id, section_index(l->section), (int)l->id, network_lane_type_name(l->type));
}

static void link_name(Out *o, const NLink *l)
{
    if (l->kind == NLINK_ROAD)
        put(o, "road %016llx at its %s", (unsigned long long)network_road(l->index)->id, l->contact ? "end" : "start");
    else if (l->kind == NLINK_JUNCTION)
        put(o, "junction %016llx (%s)", (unsigned long long)network_junction(l->index)->id, network_junction_type_name(network_junction(l->index)->type));
    else
        put(o, "the map's edge");
}

static const char *node_kind(int node)
{
    const NNode *nd = node >= 0 ? network_node(node) : NULL;
    if (!nd)
        return "no node";
    if (nd->nin >= 2 && nd->nout == 1)
        return "a merge";
    if (nd->nin == 1 && nd->nout >= 2)
        return "a diverge";
    if (nd->nin == 0 || nd->nout == 0)
        return "a dead end of the lane graph";
    return "a node";
}

void net_pick_name(const NetPick *p, int level, char *buf, size_t n)
{
    net_pick_load(p);
    Out o  = {buf, n, 0};
    buf[0] = 0;
    if (!net_pick_has(p, level))
        return;
    switch (level)
    {
        case NPICK_LANE:
            put(&o, "lane %d, %s", (int)network_lane(p->lane)->id, network_lane_type_name(network_lane(p->lane)->type));
            break;
        case NPICK_SECTION:
            put(&o, "lane section %d of %d", section_index(p->section), network_road(p->road)->nsec);
            break;
        case NPICK_ROAD:
            {
                const NRoad *r = network_road(p->road);
                put(&o, "%s %016llx (%s)", r->junction >= 0 ? "connecting road" : "road", (unsigned long long)r->id, rcity_net_name(r->net));
                break;
            }
        case NPICK_JUNCTION:
            {
                const NJunction *j = network_junction(p->junction);
                put(&o, "%s junction %016llx", network_junction_type_name(j->type), (unsigned long long)j->id);
                break;
            }
        default:
            break;
    }
}

static void text_lane(Out *o, const NetPick *p)
{
    const NLane *l = network_lane(p->lane);
    float        t_in, t_out, ds = shape_units(p->road, p->s) - network_section(l->section)->s;
    int          nn = 0, np = 0;
    put(o, "LANE\t");
    lane_name(o, p->lane);
    put(o, "\n");
    put(o, "travels\t%s\n", network_lane_in_graph(p->lane) ? (network_lane_with_s(p->lane) ? "with s" : "against s") : "not at all: no lane of the graph");
    {
        int           n, k;
        const NWidth *w = network_lane_widths(p->lane, &n);
        put(o, "width\t");
        for (k = 0; k < n; k++)
            put(o, "%s%.3f from %.2f%s", k ? ", " : "", w[k].a, (double)w[k].s, w[k].c != 0.0 || w[k].d != 0.0 ? ", easing" : "");
        put(o, " (tiles, from the section's start in units)\n");
    }
    if (shaped(p->road))
    {
        network_lane_t(p->lane, ds, &t_in, &t_out);
        put(o, "across\tt %.3f to %.3f here, the point at t %.3f\n", (double)t_in, (double)t_out, (double)p->t);
    }
    if (l->change)
        put(o, "may change\t%s%s%s\n", l->change & NCHANGE_INNER ? "inward" : "", l->change == (NCHANGE_INNER | NCHANGE_OUTER) ? " and " : "", l->change & NCHANGE_OUTER ? "outward" : "");
    /*  A sidewalk is walked both ways, so its ends are no merge or
     *  diverge. */
    if (l->type != NLANE_SIDEWALK)
    {
        put(o, "starts at\t%s\n", node_kind(network_lane_node(p->lane, 0)));
        put(o, "ends at\t%s\n", node_kind(network_lane_node(p->lane, 1)));
    }
    /*  The lanes it goes on into, then the lanes that feed it, at either
     *  end, as the store files them. */
    {
        int pass, end, it, other, into;
        for (pass = 0; pass < 2; ++pass)
            for (end = 0; end < 2; ++end)
                for (it = network_end_link(p->lane, end, -1, &other, NULL, &into); it >= 0; it = network_end_link(p->lane, end, it, &other, NULL, &into))
                {
                    int *count = pass ? &np : &nn;
                    if (into != pass || *count >= 6)
                        continue;
                    put(o, (*count)++ ? "\t" : pass ? "fed by\t"
                                                    : "goes on into\t");
                    lane_name(o, other);
                    put(o, "\n");
                }
    }
    if (!nn)
        put(o, "goes on into\tnothing\n");
    if (!np)
        put(o, "fed by\tnothing\n");
    if (network_lane_fault(p->lane))
        put(o, "fault\t%d (NFAULT_*): the lane check fails it\n", network_lane_fault(p->lane));
}

static void text_section(Out *o, const NetPick *p)
{
    const NSection *x  = network_section(p->section);
    const NRoad    *r  = network_road(p->road);
    int             k  = section_index(p->section);
    float           u1 = k + 1 < r->nsec ? network_section(p->section + 1)->s : r->length;
    put(o, "LANE SECTION\t%d of %d\n", k, r->nsec);
    put(o, "runs\ts %.2f to %.2f units, %.2f to %.2f tiles along the line\n", (double)x->s, (double)u1, (double)shape_s(p->road, x->s), (double)shape_s(p->road, u1));
    put(o, "class\t%s\n", x->cls == 2 ? "boulevard" : x->cls == 1 ? "avenue"
                                                                  : "street");
    put(o, "laneOffset\t%.3f", (double)x->offset);
    if (x->ease > 0.0f)
        put(o, ", easing to %.3f over %.2f units", (double)x->offset1, (double)x->ease);
    put(o, "\nlanes\t");
    for (k = 0; k < x->nlane; ++k)
    {
        const NLane  *l = network_lane(x->lane0 + k);
        int           n, q;
        const NWidth *w = network_lane_widths(x->lane0 + k, &n);
        put(o, "%s%d:%s=", k ? " " : "", (int)l->id, network_lane_type_name(l->type));
        for (q = 0; q < n; q++)
            put(o, "%s%.3f", q ? ">" : "", w[q].a);
    }
    put(o, "\n");
}

static void text_road(Out *o, const NetPick *p)
{
    const NRoad *r = network_road(p->road);
    int          k, n = 0;
    put(o, "ROAD\t%016llx (%s)\n", (unsigned long long)r->id, rcity_net_name(r->net));
    if (r->junction >= 0)
        put(o, "in junction\t%016llx\n", (unsigned long long)network_junction(r->junction)->id);
    put(o, "length\t%.2f units, %.3f tiles of line\n", (double)r->length, shaped(p->road) ? (double)shape_road(p->road)->length : 0.0);
    put(o, "starts at\t");
    link_name(o, &r->pred);
    put(o, "\nends at\t");
    link_name(o, &r->succ);
    put(o, "\n");
    if (r->topo >= 0)
    {
        const TopoRoad *t    = topology_road(r->topo);
        int32_t         side = topology_side();
        put(o, "units\t%d:", t->nunit);
        for (k = 0; k < t->nunit && k < 12; ++k)
        {
            int32_t cell = topology_cell(t->unit0 + k);
            put(o, " %d,%d", (int)(cell % side), (int)(cell / side));
        }
        put(o, t->nunit > 12 ? " ...\n" : "\n");
    }
    if (shaped(p->road))
    {
        float h0 = shape_height(p->road, p->s), h1 = shape_height(p->road, fminf(p->s + 0.1f, shape_road(p->road)->length));
        put(o, "the point\ts %.3f tiles (%.3f units), t %.3f, height %.3f, grade %.2f levels a tile\n", (double)p->s, (double)shape_units(p->road, p->s), (double)p->t, (double)h0, (double)((h1 - h0) / 0.1f));
    }
    for (k = 0; k < network_ramps(); ++k)
    {
        const NRamp *rp   = network_ramp(k);
        int32_t      side = topology_side();
        if (rp->road != p->road && rp->in_road != p->road && rp->out_road != p->road && rp->band != p->road)
            continue;
        put(o, n++ ? "\t" : "ramps\t");
        put(o, "%s ramp at %d,%d: this road is its %s\n", rp->on ? "an on" : "an off", (int)(rp->cell % side), (int)(rp->cell / side), rp->road == p->road ? "own road" : rp->in_road == p->road ? "band road arriving at its gore"
                                                                                                                                                                      : rp->out_road == p->road  ? "band road leaving its gore"
                                                                                                                                                                                                 : "band road");
    }
}

static void text_junction(Out *o, int junction)
{
    const NJunction *j = network_junction(junction);
    float            h = shape_junction_height(junction);
    const float     *bxy;
    int              k;
    put(o, "JUNCTION\t%s %016llx\n", network_junction_type_name(j->type), (unsigned long long)j->id);
    if (isfinite(h))
        put(o, "height\t%.3f\n", (double)h);
    put(o, "boundary\t%d points\n", shape_boundary(junction, &bxy));
    {
        int roads[256], nr = net_pick_junction_roads(junction, roads, 256);
        put(o, "connecting roads\t%d, by each road's own junction\n", nr);
    }
    /*  The connections filed under it, each flagged where its connecting
     *  road is in another junction: the slice is not this junction's. */
    put(o, "connections\t%d filed under it\n", j->nconn);
    for (k = 0; k < j->nconn && k < 24; ++k)
    {
        const NConnection *cn  = network_connection(j->conn0 + k);
        int                own = cn->connecting >= 0 ? network_road(cn->connecting)->junction : -1;
        put(o, "\t%016llx > %016llx > %016llx", cn->incoming >= 0 ? (unsigned long long)network_road(cn->incoming)->id : 0ull, cn->connecting >= 0 ? (unsigned long long)network_road(cn->connecting)->id : 0ull, cn->outgoing >= 0 ? (unsigned long long)network_road(cn->outgoing)->id : 0ull);
        if (cn->connecting >= 0 && own != junction)
            put(o, "  FAULT: its connecting road is in junction %016llx", own >= 0 ? (unsigned long long)network_junction(own)->id : 0ull);
        put(o, "\n");
    }
}

void net_pick_text(const NetPick *p, char *buf, size_t n)
{
    net_pick_load(p);
    Out o  = {buf, n, 0};
    buf[0] = 0;
    put(&o, "point\t%.3f, %.3f, height %.3f\n", (double)p->hit[0], (double)p->hit[1], (double)p->hit[2]);
    if (net_pick_has(p, NPICK_LANE))
        text_lane(&o, p);
    if (net_pick_has(p, NPICK_SECTION))
        text_section(&o, p);
    if (net_pick_has(p, NPICK_ROAD))
        text_road(&o, p);
    if (net_pick_has(p, NPICK_JUNCTION))
        text_junction(&o, p->junction);
}
