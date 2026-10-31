/*  network/topology.c: layer 1 of the road network, built from the cells
 *  through layer 0 (rcity_parts).  Three networks have roads: the road
 *  network, the railway and the highway (the band).  A power line has no
 *  roads.
 *
 *  A tunnel links its two mouths, and the road through it has the cells
 *  it passes under as its units.  Those cells are no units of the road
 *  network's own, because the cells over a tunnel can carry roads. */
#include "network/topology.h"
#include "network/state.h"

#include <stdlib.h>
#include <string.h>

#include "dump.h"
#include "log.h"

enum
{
    T_NETS = 3
};
static const int T_NET[T_NETS] = {RNET_ROAD, RNET_RAIL, RNET_BAND};

static int32_t s_n; /* cells a side */
/*  These hold one entry per network and per unit, at the unit's first
 *  cell.  They are the edges it claims, the edges it links by and the
 *  edges that leave the map.  Then the end it belongs to, and the edges a
 *  road walk has taken. */
static uint8_t *s_claim[T_NETS], *s_link[T_NETS], *s_off[T_NETS], *s_used[T_NETS];
static int     *s_endof[T_NETS];
static int     *s_seam[T_NETS]; /* per unit and edge, the seam end across it, else -1 */
static uint8_t *s_mouth;        /* per cell, a tunnel mouth's edge into the hill, else 0 */
static int32_t *s_tunnel;       /* per cell, the mouth at the far end of its tunnel, else -1 */
static uint8_t *s_ramp, *s_meet;
static uint8_t *s_joined;  /* per band unit, 1 where a ramp's band edge reaches it */
static uint8_t *s_region;  /* per end, 1 where it is an interchange's region */
static int      s_nregion; /* the ends s_region holds */
static int32_t *s_ramp_band;
static int8_t  *s_ramp_edge;
static int     *s_ramp_gore;  /* per ramp's cell, the end of its gore on the band, else -1 */
static int      s_drive_left; /* 1 where the world drives on the left */

static TopoEnd  *s_end;
static TopoRoad *s_road;
static int32_t  *s_cell;
static int       s_nend, s_cap_end, s_nroad, s_cap_road, s_ncell, s_cap_cell;

static int bit_index(int e)
{
    return e == 1 ? 0 : e == 2 ? 1
                    : e == 4   ? 2
                               : 3;
}

static int popcount4(int m)
{
    return (m & 1) + ((m >> 1) & 1) + ((m >> 2) & 1) + ((m >> 3) & 1);
}

/*  Whether edge e of unit u is a tunnel's: it leads into the hill to a
 *  far mouth. */
static int tunnel_edge(int k, int32_t u, int e)
{
    return T_NET[k] == RNET_ROAD && s_mouth && s_mouth[u] == e && s_tunnel[u] >= 0;
}

/*  The unit across edge e of unit u, -1 off the map.  A tunnel's edge
 *  leads to its far mouth. */
static int32_t across(int k, int32_t u, int e)
{
    int32_t step = T_NET[k] == RNET_BAND ? 2 : 1;
    if (tunnel_edge(k, u, e))
        return s_tunnel[u];
    int32_t row = u / s_n, col = u % s_n;
    row += e == 2 ? step : e == 8 ? -step
                                  : 0;
    col += e == 4 ? step : e == 1 ? -step
                                  : 0;
    if (row < 0 || col < 0 || row >= s_n || col >= s_n)
        return -1;
    return row * s_n + col;
}

static int is_unit(int k, int32_t u)
{
    return T_NET[k] != RNET_BAND || (((u / s_n) & 1) == 0 && ((u % s_n) & 1) == 0);
}

static int push_cell(int32_t cell)
{
    if (s_ncell == s_cap_cell)
    {
        int32_t *p = (int32_t *)realloc(s_cell, sizeof *p * (size_t)(s_cap_cell ? 2 * s_cap_cell : 4096));
        if (!p)
            return -1;
        s_cell     = p;
        s_cap_cell = s_cap_cell ? 2 * s_cap_cell : 4096;
    }
    s_cell[s_ncell++] = cell;
    return 0;
}

static int new_end(int net, int kind, int edge)
{
    TopoEnd *e;
    if (s_nend == s_cap_end)
    {
        TopoEnd *p = (TopoEnd *)realloc(s_end, sizeof *p * (size_t)(s_cap_end ? 2 * s_cap_end : 1024));
        if (!p)
            return -1;
        s_end     = p;
        s_cap_end = s_cap_end ? 2 * s_cap_end : 1024;
    }
    e        = &s_end[s_nend];
    e->id    = 0;
    e->net   = net;
    e->kind  = kind;
    e->cell0 = s_ncell;
    e->ncell = 0;
    e->edge  = (int8_t)edge;
    return s_nend++;
}

/*  An end's id is its kind, its network and its first cell by index,
 *  with the edge a seam or a map edge lies across. */
static void name_end(int i)
{
    TopoEnd *e     = &s_end[i];
    int32_t  first = s_cell[e->cell0];
    int      j;
    for (j = 1; j < e->ncell; j++)
        if (s_cell[e->cell0 + j] < first)
            first = s_cell[e->cell0 + j];
    e->id = ((uint64_t)e->kind << 60) | ((uint64_t)(e->net + 1) << 56) | ((uint64_t)first << 4) | (uint64_t)(e->edge & 15);
}

static void free_all(void)
{
    int k;
    for (k = 0; k < T_NETS; k++)
    {
        free(s_claim[k]), free(s_link[k]), free(s_off[k]), free(s_used[k]), free(s_endof[k]), free(s_seam[k]);
        s_claim[k] = s_link[k] = s_off[k] = s_used[k] = NULL;
        s_endof[k] = s_seam[k] = NULL;
    }
    free(s_ramp), free(s_meet), free(s_joined), free(s_ramp_band), free(s_ramp_edge), free(s_mouth), free(s_tunnel), free(s_ramp_gore);
    s_ramp_gore = NULL;
    free(s_region);
    s_joined = NULL, s_region = NULL, s_nregion = 0;
    s_mouth  = NULL;
    s_tunnel = NULL;
    s_ramp = s_meet = NULL;
    s_ramp_band     = NULL;
    s_ramp_edge     = NULL;
}

/*  What each unit claims, from layer 0.  A band unit claims what its
 *  four cells claim.  A span reads its axis from the unit's cell at
 *  (row, col + 1).  An interchange claims every edge, so it links
 *  wherever a unit beside it claims back. */
static void claims(const RCity *c)
{
    int32_t i, n2 = s_n * s_n;
    for (i = 0; i < n2; i++)
    {
        RPart p[2];
        int   n = rcity_parts(c->xbld[i], c->xbit[i], p), j, road = 0, rail = 0;
        for (j = 0; j < n; j++)
        {
            if (p[j].net == RNET_ROAD)
            {
                s_claim[0][i] |= p[j].edges;
                road = p[j].kind == RPART_CROSSING;
                if (p[j].kind == RPART_MOUTH)
                    s_mouth[i] = p[j].toward;
                if (p[j].kind == RPART_RAMP)
                    s_ramp[i] = 1;
            }
            else if (p[j].net == RNET_RAIL)
            {
                s_claim[1][i] |= p[j].edges;
                rail = p[j].kind == RPART_CROSSING;
            }
            else if (p[j].net == RNET_BAND && p[j].kind == RPART_RAMP)
            {
                int32_t b      = across(0, i, p[j].edges);
                s_ramp_edge[i] = (int8_t)p[j].edges;
                s_ramp_band[i] = b < 0 ? -1 : ((b / s_n) & ~1) * s_n + ((b % s_n) & ~1);
                if (b >= 0)
                    s_joined[s_ramp_band[i]] = 1;
            }
            else if (p[j].net == RNET_BAND)
            {
                int32_t row = i / s_n, col = i % s_n, u = (row & ~1) * s_n + (col & ~1);
                uint8_t edges = p[j].edges;
                RPart   q[2];
                int32_t r1 = u + 1;
                if (p[j].kind == RPART_SPAN && (u % s_n) + 1 < s_n)
                {
                    int m = rcity_parts(c->xbld[i], c->xbit[r1], q), t;
                    for (t = 0; t < m; t++)
                        if (q[t].net == RNET_BAND)
                            edges = q[t].edges;
                }
                if (p[j].kind == RPART_INTERCHANGE)
                    edges = 15;
                s_claim[2][u] |= edges;
            }
        }
        if (road && rail)
            s_meet[i] = 1;
    }
}

/*  Each tunnel mouth's far mouth: the first mouth reached stepping into
 *  the hill that faces back.  A mouth with none stays a dead end.  Its
 *  edge into the hill is then claimed, so the two mouths link. */
static void tunnels(void)
{
    int32_t i, n2 = s_n * s_n;
    for (i = 0; i < n2; i++)
    {
        int     e = s_mouth[i], dc = rcity_edge_dc(e),
                dr  = rcity_edge_dr(e);
        int32_t row = i / s_n, col = i % s_n;
        if (!e)
            continue;
        for (;;)
        {
            row += dr, col += dc;
            if (row < 0 || col < 0 || row >= s_n || col >= s_n)
                break;
            if (s_mouth[row * s_n + col] == rcity_edge_back(e))
            {
                s_tunnel[i] = row * s_n + col;
                break;
            }
        }
        if (s_tunnel[i] >= 0)
            s_claim[0][i] = (uint8_t)(s_claim[0][i] | e);
    }
}

/*  A unit links across an edge where the unit there claims the edge back
 *  and that edge leads back to it.  A mouth's edge into the hill leads
 *  to its far mouth, so the cell beside the mouth does not link into it
 *  across that edge. */
static void links(void)
{
    int     k, e;
    int32_t u, n2 = s_n * s_n;
    for (k = 0; k < T_NETS; k++)
        for (u = 0; u < n2; u++)
        {
            if (!s_claim[k][u] || !is_unit(k, u))
                continue;
            for (e = 1; e < 16; e <<= 1)
            {
                int32_t v;
                if (!(s_claim[k][u] & e))
                    continue;
                v = across(k, u, e);
                if (v < 0)
                    s_off[k][u] = (uint8_t)(s_off[k][u] | e);
                else if ((s_claim[k][v] & rcity_edge_back(e)) && across(k, v, rcity_edge_back(e)) == u)
                    s_link[k][u] = (uint8_t)(s_link[k][u] | e);
            }
        }
    /*  A ramp's cell links across its edge to the band, to the band unit
     *  there. */
    for (u = 0; u < n2; u++)
        if (s_ramp[u] && s_ramp_band[u] >= 0 && s_claim[2][s_ramp_band[u]])
            s_link[0][u] = (uint8_t)(s_link[0][u] | s_ramp_edge[u]);
}

/*  The band unit a ramp's cell links into across edge `e`, else -1. */
static int32_t ramp_band(int k, int32_t u, int e)
{
    return k == 0 && s_ramp[u] && e == s_ramp_edge[u] && (s_link[0][u] & e) ? s_ramp_band[u] : -1;
}

static int degree(int k, int32_t u)
{
    return popcount4(s_link[k][u] | s_off[k][u]);
}

/*  What KIND of unit a unit is.  The kinds are a level meet, a node, a
 *  a band unit a ramp joins beside another junction unit, and a dead end.
 *  A node has
 *  three links or more, or is a band's interchange.  0 is a plain unit of
 *  two links.  Junction units of one kind that link merge into one end,
 *  and units of two kinds do not. */
enum
{
    U_PLAIN = 0,
    U_MEET,
    U_NODE,
    U_JOINED,
    U_DEAD
};

/*  Whether band unit `u` is no plain unit between plain units: a node, a
 *  dead end, the map's edge or an interchange's unit. */
static int band_stop(const RCity *c, int32_t u)
{
    return degree(2, u) != 2 || s_off[2][u] || c->xbld[u] == 0x69;
}

/*  A band unit a ramp joins is its ramps' band junction where the ramp's
 *  taper has no unit to run into.  See appendix-road-spec, 7.3,
 *  "Fallback".  That is where the unit is no plain unit.  It is also
 *  where the unit across the border its gore would cut is none, or is
 *  such a unit.  A unit beside an interchange's unit, or beside such a
 *  unit beside one, is one as well.  The interchange's region takes it
 *  in (plans/interchanges.md, "The model"). s_joined holds 2 for each. */
static void joined_junctions(const RCity *c)
{
    int32_t u, n2 = s_n * s_n;
    int     more = 1;
    for (u = 0; u < n2; u++)
    {
        int e;
        if (!s_joined[u] || !is_unit(2, u))
            continue;
        if (band_stop(c, u))
            s_joined[u] = 2;
        for (e = 1; e < 16; e <<= 1)
        {
            int32_t v = (s_link[2][u] & e) ? across(2, u, e) : -1;
            if (v >= 0 && c->xbld[v] == 0x69)
                s_joined[u] = 3;
        }
    }
    while (more)
    {
        more = 0;
        for (u = 0; u < n2; u++)
        {
            int e;
            if (s_joined[u] != 1 || !is_unit(2, u))
                continue;
            for (e = 1; e < 16; e <<= 1)
            {
                int32_t v = (s_link[2][u] & e) ? across(2, u, e) : -1;
                if (v >= 0 && s_joined[v] == 3)
                    s_joined[u] = 3, more = 1;
            }
        }
    }
    for (u = 0; u < n2; u++)
        if (s_joined[u] == 3)
            s_joined[u] = 2;
    more = 1;
    while (more)
    {
        more = 0;
        for (u = 0; u < n2; u++)
        {
            int32_t b = s_ramp[u] ? s_ramp_band[u] : -1, v;
            int     h, on, dir;
            if (b < 0 || s_joined[b] != 1 || !is_unit(2, b) || !topology_ramp_way(u, &h, &on))
                continue;
            dir = on ? h : rcity_edge_back(h);
            v   = (s_link[2][b] & dir) ? across(2, b, dir) : -1;
            if (v < 0 || band_stop(c, v) || s_joined[v] == 2)
                s_joined[b] = 2, more = 1;
        }
    }
}

/*  THE REGIONS (plans/interchanges.md, "The model").  A band unit a ramp
 *  joins that links to an interchange's unit, directly or through other
 *  such units, is in the interchange's region.  Its entry in s_joined
 *  is 3.  It is a node's unit, so it merges with the interchange. */
static void regions(const RCity *c)
{
    int32_t u, n2 = s_n * s_n;
    int     more = 1;
    while (more)
    {
        more = 0;
        for (u = 0; u < n2; u++)
        {
            int e;
            if (s_joined[u] != 2 || !is_unit(2, u))
                continue;
            for (e = 1; e < 16; e <<= 1)
            {
                int32_t v = (s_link[2][u] & e) ? across(2, u, e) : -1;
                if (v >= 0 && (c->xbld[v] == 0x69 || s_joined[v] == 3))
                    s_joined[u] = 3, more = 1;
            }
        }
    }
}

static int unit_kind(const RCity *c, int k, int32_t u)
{
    if (k < 2 && s_meet[u])
        return U_MEET;
    if (degree(k, u) >= 3 || (T_NET[k] == RNET_BAND && (c->xbld[u] == 0x69 || s_joined[u] == 3)))
        return U_NODE;
    if (T_NET[k] == RNET_BAND && s_joined[u] == 2)
        return U_JOINED;
    return degree(k, u) <= 1 ? U_DEAD : U_PLAIN;
}

static int is_junction_unit(const RCity *c, int k, int32_t u)
{
    return unit_kind(c, k, u) != U_PLAIN;
}

/*  Which units lie INSIDE a cluster of junction units.  A unit of two
 *  links whose two neighbors are junction units of one cluster is no road
 *  out of the junction and straight back in.  Two clusters a unit joins
 *  stay apart.  `lab` is scratch, a cluster number for each junction
 *  unit. */
static void inside_units(const RCity *c, int k, int32_t *stack, int32_t *lab, uint8_t *inside)
{
    int32_t u, n2 = s_n * s_n, next = 0;
    for (u = 0; u < n2; u++)
        lab[u] = -1, inside[u] = 0;
    for (u = 0; u < n2; u++)
    {
        int sp = 0;
        if (lab[u] >= 0 || !s_claim[k][u] || !is_unit(k, u) || degree(k, u) == 0 || !is_junction_unit(c, k, u))
            continue;
        lab[u]      = next;
        stack[sp++] = u;
        while (sp)
        {
            int32_t x = stack[--sp];
            int     e;
            for (e = 1; e < 16; e <<= 1)
            {
                int32_t y = (s_link[k][x] & e) ? across(k, x, e) : -1;
                if (y >= 0 && lab[y] < 0 && unit_kind(c, k, x) != U_DEAD && unit_kind(c, k, y) == unit_kind(c, k, x))
                    lab[y] = next, stack[sp++] = y;
            }
        }
        next++;
    }
    for (u = 0; u < n2; u++)
    {
        int32_t y[2];
        int     e, m = 0;
        if (!s_claim[k][u] || !is_unit(k, u) || lab[u] >= 0 || degree(k, u) != 2 || s_off[k][u])
            continue;
        for (e = 1; e < 16 && m < 2; e <<= 1)
            if (s_link[k][u] & e)
                y[m++] = across(k, u, e);
        if (m == 2 && y[0] >= 0 && y[1] >= 0 && lab[y[0]] >= 0 && lab[y[0]] == lab[y[1]])
            inside[u] = 1;
    }
}

/*  The ends that lie on units.  Junction units of one network and of one
 *  kind that link merge into one end with the units inside them.  A unit
 *  of another kind stops the merge.  A dead end is the end of a road and
 *  no place where roads meet.  So it is an end of its own unit alone.  A
 *  level meet is a unit of two networks, so an end of meets goes on in
 *  both.  The end's kind is the kind of its units. */
static int unit_ends(const RCity *c)
{
    int      k, kk;
    int32_t  u, n2 = s_n * s_n, *stack = (int32_t *)malloc(sizeof *stack * (size_t)n2 * T_NETS);
    int32_t *lab = (int32_t *)malloc(sizeof *lab * (size_t)n2);
    uint8_t *inside[T_NETS];
    for (k = 0; k < T_NETS; k++)
        inside[k] = (uint8_t *)malloc((size_t)n2);
    if (!stack || !lab || !inside[0] || !inside[1] || !inside[2])
        goto fail;
    for (k = 0; k < T_NETS; k++)
        inside_units(c, k, stack, lab, inside[k]);
    for (k = 0; k < T_NETS; k++)
        for (u = 0; u < n2; u++)
        {
            int at, e, sp = 0, nets = 0, kind = unit_kind(c, k, u);
            if (!s_claim[k][u] || !is_unit(k, u) || s_endof[k][u] >= 0 || !is_junction_unit(c, k, u))
                continue;
            at = new_end(T_NET[k], END_DEAD, 0);
            if (at < 0)
                goto fail;
            s_endof[k][u] = at;
            stack[sp++]   = u * T_NETS + k;
            while (sp)
            {
                int32_t x  = stack[--sp] / T_NETS;
                int     xk = stack[sp] % T_NETS, meet = xk < 2 && s_meet[x];
                /*  A meet's cell is pushed once, from the road network. */
                if (!meet || xk == 0)
                {
                    if (push_cell(x) != 0)
                        goto fail;
                    s_end[at].ncell++;
                }
                if (meet && s_endof[1 - xk][x] < 0)
                {
                    s_endof[1 - xk][x] = at;
                    stack[sp++]        = x * T_NETS + (1 - xk);
                }
                nets |= 1 << xk;
                for (e = 1; e < 16; e <<= 1)
                {
                    int32_t y = (s_link[xk][x] & e) ? across(xk, x, e) : -1;
                    if (y >= 0 && s_endof[xk][y] < 0 && kind != U_DEAD && (unit_kind(c, xk, y) == kind || inside[xk][y]))
                    {
                        s_endof[xk][y] = at;
                        stack[sp++]    = y * T_NETS + xk;
                    }
                }
            }
            s_end[at].kind = kind == U_MEET ? END_MEET : kind == U_DEAD ? END_DEAD
                                                                        : END_NODE;
            for (kk = 0; kk < T_NETS; kk++)
                if (nets == 1 << kk)
                    s_end[at].net = T_NET[kk];
            if (nets & (nets - 1))
                s_end[at].net = -1;
        }
    for (k = 0; k < T_NETS; k++)
        free(inside[k]);
    free(stack), free(lab);
    return 0;
fail:
    for (k = 0; k < T_NETS; k++)
        free(inside[k]);
    free(stack), free(lab);
    return -1;
}

/*  A unit with two links is PLAIN: a road runs through it. */
static int is_plain(int k, int32_t u)
{
    return s_claim[k][u] && is_unit(k, u) && s_endof[k][u] < 0 && degree(k, u) == 2;
}

int topology_ramp_way(int32_t cell, int *heading, int *on)
{
    int eb, er, cand, k, h = 0;
    if (!s_ramp || cell < 0 || cell >= s_n * s_n || !s_ramp[cell])
        return 0;
    eb = s_ramp_edge[cell], er = s_claim[0][cell];
    /*  The band's traffic on the ramp's side heads along the band with the
     *  ramp on the side the world drives on. */
    cand = (eb == 1 || eb == 4) ? 2 : 1;
    for (k = 0; k < 2; k++, cand = rcity_edge_back(cand))
        if ((s_drive_left ? rcity_edge_back(rcity_edge_next(cand)) : rcity_edge_next(cand)) == rcity_edge_back(eb))
            h = cand;
    *heading = h, *on = er == rcity_edge_back(h);
    return er == h || er == rcity_edge_back(h);
}

int topology_ramp_gore(int32_t cell)
{
    return s_ramp_gore && cell >= 0 && cell < s_n * s_n ? s_ramp_gore[cell] : -1;
}

/*  The band cut across border `f` of unit `t`: a seam there becomes a gore,
 *  and a new gore holds `t` first and the unit across second.  Its end, or
 *  -1 out of memory. */
static int cut_band(int32_t t, int f)
{
    int32_t v = across(2, t, f);
    int     q = t * 4 + bit_index(f), at;
    if (s_seam[2][q] >= 0)
    {
        /*  The seam's units, `t` first, and its edge seen from `t`. */
        TopoEnd *e = &s_end[s_seam[2][q]];
        if (e->ncell == 2 && s_cell[e->cell0] != t)
            s_cell[e->cell0 + 1] = s_cell[e->cell0], s_cell[e->cell0] = t, e->edge = (int8_t)rcity_edge_back(e->edge);
        e->kind = END_GORE;
        return s_seam[2][q];
    }
    at = new_end(RNET_BAND, END_GORE, f);
    if (at < 0 || push_cell(t) != 0 || push_cell(v) != 0)
        return -1;
    s_end[at].ncell                                  = 2;
    s_seam[2][q]                                     = at;
    s_seam[2][v * 4 + bit_index(rcity_edge_back(f))] = at;
    return at;
}

/*  Whether the band can be cut across border `f` of unit `t`: both units
 *  plain, and no gore there yet. */
static int cuttable(int32_t t, int f)
{
    int32_t v = (s_link[2][t] & f) ? across(2, t, f) : -1;
    int     q = t * 4 + bit_index(f);
    return v >= 0 && is_plain(2, t) && is_plain(2, v) && !(s_seam[2][q] >= 0 && s_end[s_seam[2][q]].kind == END_GORE);
}

/*  Whether border `f` of plain unit `t` is a gore already, to a plain
 *  unit. */
static int shared_gore(int32_t t, int f)
{
    int32_t v = (s_link[2][t] & f) ? across(2, t, f) : -1;
    int     q = t * 4 + bit_index(f);
    return v >= 0 && is_plain(2, t) && is_plain(2, v) && s_seam[2][q] >= 0 && s_end[s_seam[2][q]].kind == END_GORE;
}

/*  Whether border `f` of plain unit `t` is a gore of ramps on the other
 *  carriageway alone: of no ramp whose band traffic heads `h`.  A taper
 *  runs on through such a gore and shares it. */
static int other_side_gore(int32_t t, int f, int h)
{
    int32_t x, n2 = s_n * s_n;
    int     end, hx, on;
    if (!shared_gore(t, f))
        return 0;
    end = s_seam[2][t * 4 + bit_index(f)];
    for (x = 0; x < n2; x++)
        if (s_ramp_gore[x] == end && topology_ramp_way(x, &hx, &on) && hx == h)
            return 0;
    return 1;
}

/*  Whether the band runs straight through the junction that holds unit
 *  `b`, along `dir`: it leaves the junction both ways along that line. */
static int runs_through(int32_t b, int dir)
{
    int k;
    for (k = 0; k < 2; k++)
    {
        int     f = k ? rcity_edge_back(dir) : dir;
        int32_t t = b;
        while (t >= 0 && s_endof[2][t] == s_endof[2][b])
            t = (s_link[2][t] & f) ? across(2, t, f) : -1;
        if (t < 0)
            return 0;
    }
    return 1;
}

/*  THE GORES (plans/network.md, layer 1; appendix-road-spec, 7.3).  A
 *  ramp's cell can stand beside a plain band unit.  The ramp's road is
 *  cut at the edge to that unit.  The band's road is cut at the GORE of
 *  the ramp.  The gore lies a taper along the band from the ramp's unit.
 *  It lies upstream for an off ramp and downstream for an on ramp.  The
 *  taper is TAPER_UNITS, fewer where the band's plain units run out, or
 *  another ramp's unit or gore comes first, and never fewer than one.
 *  Where not even one unit is free, the gore is the ramp's unit's own
 *  border.  The band's road is cut as well at the unit's other border,
 *  where the lane comes back.  Each cut is an end that holds no unit and
 *  lies across its edge, as a seam does, and a seam there becomes the
 *  gore.  A border to a junction's unit needs none: the band's road ends
 *  there. */
#define TAPER_UNITS 2
static int gores(void)
{
    int32_t u, n2 = s_n * s_n;
    if ((s_ramp_gore = (int *)malloc(sizeof(int) * (size_t)(n2 > 0 ? n2 : 1))) == NULL)
        return -1;
    for (u = 0; u < n2; u++)
        s_ramp_gore[u] = -1;
    for (u = 0; u < n2; u++)
    {
        int     e = s_ramp_edge[u], at, h, on, dir, k, node;
        int32_t b = s_ramp[u] && s_ramp_band[u] >= 0 && (s_link[0][u] & e) ? s_ramp_band[u] : -1, t, near;
        /*  A ramp beside a junction's unit, a cross street's, runs its taper
         *  along the band beyond the junction. */
        node = b >= 0 && !is_plain(2, b) && s_endof[2][b] >= 0 && s_end[s_endof[2][b]].kind == END_NODE;
        if (b < 0 || (!is_plain(2, b) && !node))
            continue;
        /*  The ramp's road is cut at the edge to a plain band unit.  Beside
         *  a junction's unit it is cut once its gore is placed, and else
         *  joins the junction. */
        if (!node && s_seam[0][u * 4 + bit_index(e)] < 0)
        {
            int rc = new_end(RNET_ROAD, END_GORE, e);
            if (rc < 0 || push_cell(u) != 0)
                return -1;
            s_end[rc].ncell                 = 1;
            s_seam[0][u * 4 + bit_index(e)] = rc;
        }
        if (!topology_ramp_way(u, &h, &on))
            continue;
        /*  The way from the ramp's unit to its gore, and the unit on the
         *  near side of the gore: as far as the taper reaches. */
        dir = on ? h : rcity_edge_back(h);
        /*  Beside a junction's unit the taper starts at the band's first
         *  unit beyond the junction, and its first unit is that one. */
        if (node)
        {
            /*  Only where the band runs straight on through the junction,
             *  a cross street's.  At a bend the ramp joins the junction. */
            if (!runs_through(b, dir))
                continue;
            for (t = b; t >= 0 && s_endof[2][t] == s_endof[2][b];)
                t = (s_link[2][t] & dir) ? across(2, t, dir) : -1;
            if (t < 0 || !is_plain(2, t) || s_joined[t])
                continue;
            b = t;
        }
        /*  A gore on the unit's own border already, another ramp's, is
         *  shared: the taper runs no farther.  Where the band leaves the
         *  map there, the gore is the map's edge. */
        if (!cuttable(b, dir) && !shared_gore(b, dir) && !(s_off[2][b] & dir))
            continue;
        for (near = b, k = node; k < TAPER_UNITS && (cuttable(b, dir) || other_side_gore(b, dir, h)); k++)
        {
            t = across(2, near, dir);
            if (s_joined[t] || !(cuttable(t, dir) || other_side_gore(t, dir, h)))
                break;
            near = t;
        }
        if (!cuttable(near, dir) && (s_off[2][near] & dir))
        {
            /*  The band leaves the map before the taper's end.  The gore is
             *  the map's edge: an end of one unit, the band's road on the
             *  map side alone.  Two ramps there share it. */
            int q = near * 4 + bit_index(dir);
            if (s_seam[2][q] >= 0 && s_end[s_seam[2][q]].kind == END_GORE)
                at = s_seam[2][q];
            else
            {
                if ((at = new_end(RNET_BAND, END_GORE, dir)) < 0 || push_cell(near) != 0)
                    return -1;
                s_end[at].ncell = 1;
                s_seam[2][q]    = at;
            }
        }
        else if ((at = cut_band(near, dir)) < 0)
            return -1;
        s_ramp_gore[u] = at;
        if (s_seam[0][u * 4 + bit_index(e)] < 0)
        {
            int rc = new_end(RNET_ROAD, END_GORE, e);
            if (rc < 0 || push_cell(u) != 0)
                return -1;
            s_end[rc].ncell                 = 1;
            s_seam[0][u * 4 + bit_index(e)] = rc;
        }
        /*  The border the lane comes back at.  Beside a junction it comes
         *  back where the junction's band road leaves on the other side. */
        if (!node && cuttable(b, rcity_edge_back(dir)) && cut_band(b, rcity_edge_back(dir)) < 0)
            return -1;
    }
    return 0;
}

/*  Whether end `i` is an interchange: an end of the band that holds an
 *  interchange's unit, and that three band links or more leave. */
static int is_interchange(const RCity *c, int i)
{
    int j, arms = 0, ix = 0;
    if (s_end[i].kind != END_NODE || s_end[i].net != RNET_BAND)
        return 0;
    for (j = 0; j < s_end[i].ncell; j++)
    {
        int32_t u = s_cell[s_end[i].cell0 + j];
        int     e;
        if (s_endof[2][u] != i)
            continue;
        ix |= c->xbld[u] == 0x69;
        for (e = 1; e < 16; e <<= 1)
        {
            int32_t v = (s_link[2][u] & e) ? across(2, u, e) : -1;
            arms += v >= 0 && s_endof[2][v] != i;
        }
    }
    return ix && arms >= 3;
}

/*  THE INTERCHANGE GORES (plans/interchanges.md, "The records").  Each
 *  band link that leaves an interchange is cut by a GORE on the edge it
 *  crosses.  The gore's first cell is the unit outside the interchange,
 *  and its edge faces the interchange.  The gore holds the interchange's
 *  side of the edge.  It holds the far side too, where that is no other
 *  interchange.  Two interchanges side by side each cut a gore of their
 *  own, and a road of no units joins the two (roads). */
static int interchange_gores(const RCity *c)
{
    int      i, n = s_nend;
    uint8_t *ix = (uint8_t *)calloc((size_t)(n > 0 ? n : 1), 1);
    if (!ix)
        return -1;
    free(s_region);
    s_region = ix, s_nregion = n;
    for (i = 0; i < n; i++)
        ix[i] = (uint8_t)is_interchange(c, i);
    for (i = 0; i < n; i++)
    {
        int j;
        if (!ix[i])
            continue;
        for (j = 0; j < s_end[i].ncell; j++)
        {
            int32_t u = s_cell[s_end[i].cell0 + j];
            int     e;
            if (s_endof[2][u] != i)
                continue;
            for (e = 1; e < 16; e <<= 1)
            {
                int32_t v = (s_link[2][u] & e) ? across(2, u, e) : -1;
                int     at;
                if (v < 0 || s_endof[2][v] == i)
                    continue;
                at = new_end(RNET_BAND, END_GORE, rcity_edge_back(e));
                if (at < 0 || push_cell(v) != 0 || push_cell(u) != 0)
                    return -1;
                s_end[at].ncell                 = 2;
                s_seam[2][u * 4 + bit_index(e)] = at;
                if (s_endof[2][v] < 0 || !ix[s_endof[2][v]])
                    s_seam[2][v * 4 + bit_index(rcity_edge_back(e))] = at;
            }
        }
    }
    /*  A ramp's road whose band unit is in a region is cut at the edge
     *  between its cell and that unit. */
    for (i = 0; i < s_n * s_n; i++)
    {
        int     e = s_ramp_edge[i], at;
        int32_t b = s_ramp[i] ? s_ramp_band[i] : -1;
        if (b < 0 || !(s_link[0][i] & e) || s_endof[2][b] < 0 || !ix[s_endof[2][b]] || s_seam[0][i * 4 + bit_index(e)] >= 0)
            continue;
        at = new_end(RNET_ROAD, END_GORE, e);
        if (at < 0 || push_cell(i) != 0)
            return -1;
        s_end[at].ncell                 = 1;
        s_seam[0][i * 4 + bit_index(e)] = at;
    }
    return 0;
}

/*  The ends that lie on edges.  A SEAM is wherever a link between two
 *  plain units crosses the border of a chunk the mesh is kept in
 *  (MESH_CHUNK cells a side).  A MAP EDGE is wherever a plain unit
 *  links off the map.  A unit that is an end already keeps its link
 *  off the map as a road of no units. */
static int edge_ends(void)
{
    int     k, e, fc = MESH_CHUNK;
    int32_t u, n2 = s_n * s_n;
    for (k = 0; k < T_NETS; k++)
        for (u = 0; u < n2; u++)
        {
            if (!s_claim[k][u] || !is_unit(k, u))
                continue;
            for (e = 1; e < 16; e <<= 1)
            {
                int at;
                if ((s_off[k][u] & e) && s_seam[k][u * 4 + bit_index(e)] < 0)
                {
                    at = new_end(T_NET[k], END_MAP_EDGE, e);
                    if (at < 0 || push_cell(u) != 0)
                        return -1;
                    s_end[at].ncell                 = 1;
                    s_seam[k][u * 4 + bit_index(e)] = at;
                    continue;
                }
                if (fc > 0 && (e == 2 || e == 4) && (s_link[k][u] & e) && !tunnel_edge(k, u, e))
                {
                    int32_t v       = across(k, u, e);
                    int     crosses = e == 4 ? (u % s_n) / fc != (v % s_n) / fc : (u / s_n) / fc != (v / s_n) / fc;
                    if (!crosses || !is_plain(k, u) || !is_plain(k, v))
                        continue;
                    at = new_end(T_NET[k], END_SEAM, e);
                    if (at < 0 || push_cell(u) != 0 || push_cell(v) != 0)
                        return -1;
                    s_end[at].ncell                                  = 2;
                    s_seam[k][u * 4 + bit_index(e)]                  = at;
                    s_seam[k][v * 4 + bit_index(rcity_edge_back(e))] = at;
                }
            }
        }
    return gores();
}

static int new_road(int k)
{
    TopoRoad *r;
    if (s_nroad == s_cap_road)
    {
        TopoRoad *p = (TopoRoad *)realloc(s_road, sizeof *p * (size_t)(s_cap_road ? 2 * s_cap_road : 1024));
        if (!p)
            return -1;
        s_road     = p;
        s_cap_road = s_cap_road ? 2 * s_cap_road : 1024;
    }
    r         = &s_road[s_nroad];
    r->id     = 0;
    r->net    = T_NET[k];
    r->unit0  = s_ncell;
    r->nunit  = 0;
    r->end[0] = r->end[1] = -1;
    r->at[0] = r->at[1] = -1;
    r->edge[0] = r->edge[1] = 0;
    return s_nroad++;
}

/*  A road's id is its network, its first unit and the edge it faces its
 *  first end by.  The road runs from whichever end gives the smaller id,
 *  so a road has the same id whichever end the walk began at. */
static uint64_t end_key(const TopoRoad *r, int c, int32_t unit)
{
    return ((uint64_t)unit << 5) | ((uint64_t)r->edge[c] << 1);
}

static void name_road(int i, int32_t first, int32_t last)
{
    TopoRoad *r  = &s_road[i];
    uint64_t  k0 = end_key(r, 0, first);
    uint64_t  k1 = end_key(r, 1, last);
    if (k1 < k0)
    {
        int    j, t;
        int8_t te;
        for (j = 0; j < r->nunit / 2; j++)
        {
            int32_t a                           = s_cell[r->unit0 + j];
            s_cell[r->unit0 + j]                = s_cell[r->unit0 + r->nunit - 1 - j];
            s_cell[r->unit0 + r->nunit - 1 - j] = a;
        }
        t = r->end[0], r->end[0] = r->end[1], r->end[1] = t;
        te = r->edge[0], r->edge[0] = r->edge[1], r->edge[1] = te;
        {
            int32_t ta = r->at[0];
            r->at[0]   = r->at[1];
            r->at[1]   = ta;
        }
        k0 = k1;
    }
    r->id = ((uint64_t)(r->net + 1) << 56) | k0;
}

/*  Walk a road from end `from`.  `x` is its first unit, and `facing` the
 *  edge of `x` toward that end.  The walk takes each plain unit's other
 *  link until it reaches an end. */
static int walk(int k, int from, int32_t x, int facing)
{
    int     i = new_road(k), e = facing;
    int32_t first = x;
    if (i < 0)
        return -1;
    s_road[i].end[0]  = from;
    s_road[i].edge[0] = (int8_t)facing;
    if (s_end[from].kind != END_SEAM && s_end[from].kind != END_MAP_EDGE && s_end[from].kind != END_GORE)
        s_road[i].at[0] = ramp_band(k, x, facing) >= 0 ? ramp_band(k, x, facing) : across(k, x, facing);
    s_used[k][x] = (uint8_t)(s_used[k][x] | facing);
    for (;;)
    {
        int     out;
        int32_t y;
        if (push_cell(x) != 0)
            return -1;
        s_road[i].nunit++;
        out          = (s_link[k][x] | s_off[k][x]) & ~e;
        s_used[k][x] = (uint8_t)(s_used[k][x] | out);
        if (s_seam[k][x * 4 + bit_index(out)] >= 0)
        {
            s_road[i].end[1]  = s_seam[k][x * 4 + bit_index(out)];
            s_road[i].edge[1] = (int8_t)out;
            break;
        }
        /*  A ramp's road ends at the band unit's end. */
        if (ramp_band(k, x, out) >= 0)
        {
            s_road[i].end[1]  = s_endof[2][ramp_band(k, x, out)];
            s_road[i].edge[1] = (int8_t)out;
            s_road[i].at[1]   = ramp_band(k, x, out);
            break;
        }
        y = across(k, x, out);
        if (tunnel_edge(k, x, out))
        {
            /*  The cells the tunnel passes under, between its mouths. */
            int     dc = rcity_edge_dc(out),
                    dr = rcity_edge_dr(out);
            int32_t c  = x + dr * s_n + dc;
            for (; c != y; c += dr * s_n + dc)
            {
                if (push_cell(c) != 0)
                    return -1;
                s_road[i].nunit++;
            }
        }
        s_used[k][y] = (uint8_t)(s_used[k][y] | rcity_edge_back(out));
        if (s_endof[k][y] >= 0)
        {
            s_road[i].end[1]  = s_endof[k][y];
            s_road[i].edge[1] = (int8_t)out;
            s_road[i].at[1]   = y;
            break;
        }
        x = y;
        e = rcity_edge_back(out);
    }
    name_road(i, first, x);
    return 0;
}

/*  A road of no units: two ends side by side, or an end that links off
 *  the map. */
static int touch(int k, int a, int32_t u, int e, int b)
{
    int     i = new_road(k);
    int32_t v = across(k, u, e);
    if (i < 0)
        return -1;
    s_used[k][u] = (uint8_t)(s_used[k][u] | e);
    if (v >= 0)
        s_used[k][v] = (uint8_t)(s_used[k][v] | rcity_edge_back(e));
    s_road[i].end[0]  = a;
    s_road[i].end[1]  = b;
    s_road[i].at[0]   = u;
    s_road[i].at[1]   = v;
    s_road[i].edge[0] = (int8_t)e;
    s_road[i].edge[1] = (int8_t)(v >= 0 ? rcity_edge_back(e) : e);
    name_road(i, u, v >= 0 ? v : u);
    return 0;
}

/*  A road of no units from end `a` to the gore on the edge `e` of its unit
 *  `u`.  A gore holds no unit, so the road touches no unit at that end. */
static int gore_touch(int k, int a, int32_t u, int e, int gore)
{
    int i = new_road(k);
    if (i < 0)
        return -1;
    s_used[k][u]      = (uint8_t)(s_used[k][u] | e);
    s_road[i].end[0]  = a;
    s_road[i].end[1]  = gore;
    s_road[i].at[0]   = u;
    s_road[i].edge[0] = (int8_t)e;
    s_road[i].edge[1] = (int8_t)e;
    name_road(i, u, u);
    return 0;
}

/*  A road of no units between gore `a` and gore `b`, on the edge `e` of
 *  unit `u` that both lie across.  It touches no unit. */
static int gore_gore(int k, int a, int32_t u, int e, int b)
{
    int i = new_road(k);
    if (i < 0)
        return -1;
    s_road[i].end[0]  = a;
    s_road[i].end[1]  = b;
    s_road[i].edge[0] = (int8_t)e;
    s_road[i].edge[1] = (int8_t)e;
    name_road(i, u, u);
    return 0;
}

/*  A road of no units of the road network joins a ramp's cell that is a
 *  dead end to the band unit beside it.  It crosses the edge between them. */
static int ramp_touch(int32_t u)
{
    int     i = new_road(0), e = s_ramp_edge[u];
    int32_t b = s_ramp_band[u];
    if (i < 0)
        return -1;
    s_road[i].end[0]  = s_endof[0][u];
    s_road[i].end[1]  = s_endof[2][b];
    s_road[i].at[0]   = u;
    s_road[i].at[1]   = b;
    s_road[i].edge[0] = (int8_t)e;
    s_road[i].edge[1] = (int8_t)rcity_edge_back(e);
    s_used[0][u]      = (uint8_t)(s_used[0][u] | e);
    name_road(i, u, b);
    return 0;
}

/*  Every road.  The roads run from each end along each link it has not
 *  walked, and from each band unit a ramp joins into the ramp's cell.  A
 *  ramp's cell that is a dead end is joined to the band unit by a road of
 *  no units.  Then a road runs around each ring of plain units that no
 *  end reaches.  A ring is cut by a seam at its first unit. */
static int roads(void)
{
    int     k, i, e;
    int32_t u, n2 = s_n * s_n;
    for (i = 0; i < s_nend; i++)
    {
        const TopoEnd *end = &s_end[i];
        for (k = 0; k < T_NETS; k++)
        {
            int c;
            if ((end->kind == END_SEAM || end->kind == END_MAP_EDGE || end->kind == END_GORE) && T_NET[k] != end->net)
                continue;
            for (c = 0; c < end->ncell; c++)
            {
                u = s_cell[end->cell0 + c];
                if (end->kind == END_SEAM || end->kind == END_MAP_EDGE || end->kind == END_GORE)
                {
                    int facing = c == 0 ? end->edge : rcity_edge_back(end->edge);
                    /*  Two interchanges side by side: the gore of the
                     *  other holds this unit's side of the edge. */
                    if (end->kind == END_GORE && s_endof[k][u] >= 0 && s_seam[k][u * 4 + bit_index(facing)] > i &&
                        gore_gore(k, i, u, facing, s_seam[k][u * 4 + bit_index(facing)]) != 0)
                        return -1;
                    if (s_endof[k][u] >= 0)
                    {
                        if (end->kind == END_MAP_EDGE && !(s_used[k][u] & facing) &&
                            touch(k, s_endof[k][u], u, facing, i) != 0)
                            return -1;
                        continue;
                    }
                    if (!(s_used[k][u] & facing) && walk(k, i, u, facing) != 0)
                        return -1;
                    continue;
                }
                /*  An end can hold units of more than one network.  A
                 *  unit walks only the networks it is the end's unit of. */
                if (s_endof[k][u] != i)
                    continue;
                for (e = 1; e < 16; e <<= 1)
                {
                    int32_t v;
                    if (!(s_link[k][u] & e) || (s_used[k][u] & e) || ramp_band(k, u, e) >= 0)
                        continue;
                    /*  A region holds no road: its gores are joined inside
                     *  it by layer 2. */
                    if (s_seam[k][u * 4 + bit_index(e)] >= 0)
                    {
                        if (!(s_region && i < s_nregion && s_region[i]) && gore_touch(k, i, u, e, s_seam[k][u * 4 + bit_index(e)]) != 0)
                            return -1;
                        continue;
                    }
                    v = across(k, u, e);
                    if (s_endof[k][v] == i)
                        continue;
                    if (s_endof[k][v] >= 0)
                    {
                        if (touch(k, i, u, e, s_endof[k][v]) != 0)
                            return -1;
                        continue;
                    }
                    s_used[k][u] = (uint8_t)(s_used[k][u] | e);
                    if (walk(k, i, v, rcity_edge_back(e)) != 0)
                        return -1;
                }
            }
        }
    }
    for (u = 0; u < n2; u++)
    {
        int32_t b = ramp_band(0, u, s_ramp_edge[u]);
        if (b < 0 || s_endof[2][b] < 0 || (s_used[0][u] & s_ramp_edge[u]))
            continue;
        /*  A ramp's cell that is a dead end, cut at its edge to a region,
         *  reaches the gore there by a road of no units. */
        if (s_endof[0][u] >= 0 && s_seam[0][u * 4 + bit_index(s_ramp_edge[u])] >= 0)
        {
            if (gore_touch(0, s_endof[0][u], u, s_ramp_edge[u], s_seam[0][u * 4 + bit_index(s_ramp_edge[u])]) != 0)
                return -1;
            continue;
        }
        if (s_endof[0][u] >= 0 ? ramp_touch(u) != 0 : walk(0, s_endof[2][b], u, s_ramp_edge[u]) != 0)
            return -1;
    }
    for (k = 0; k < T_NETS; k++)
        for (u = 0; u < n2; u++)
        {
            int     at, rest;
            int32_t v;
            if (!is_plain(k, u) || (s_used[k][u] & s_link[k][u]) == s_link[k][u])
                continue;
            rest = s_link[k][u] & ~s_used[k][u];
            e    = rest & -rest;
            v    = across(k, u, e);
            at   = new_end(T_NET[k], END_SEAM, e);
            if (at < 0 || push_cell(u) != 0 || push_cell(v) != 0)
                return -1;
            s_end[at].ncell                                  = 2;
            s_seam[k][u * 4 + bit_index(e)]                  = at;
            s_seam[k][v * 4 + bit_index(rcity_edge_back(e))] = at;
            if (walk(k, at, u, e) != 0)
                return -1;
        }
    return 0;
}

int topology_build(const RCity *c)
{
    int    k, i;
    size_t n2;
    free_all();
    s_nend = s_nroad = s_ncell = 0;
    s_n                        = c->n;
    s_drive_left               = c->drive_left != 0;
    n2                         = (size_t)s_n * (size_t)s_n;
    for (k = 0; k < T_NETS; k++)
    {
        s_claim[k] = (uint8_t *)calloc(n2, 1);
        s_link[k]  = (uint8_t *)calloc(n2, 1);
        s_off[k]   = (uint8_t *)calloc(n2, 1);
        s_used[k]  = (uint8_t *)calloc(n2, 1);
        s_endof[k] = (int *)malloc(sizeof(int) * n2);
        s_seam[k]  = (int *)malloc(sizeof(int) * n2 * 4);
        if (!s_claim[k] || !s_link[k] || !s_off[k] || !s_used[k] || !s_endof[k] || !s_seam[k])
            goto fail;
        memset(s_endof[k], 0xFF, sizeof(int) * n2);
        memset(s_seam[k], 0xFF, sizeof(int) * n2 * 4);
    }
    s_mouth     = (uint8_t *)calloc(n2, 1);
    s_tunnel    = (int32_t *)malloc(sizeof(int32_t) * n2);
    s_ramp      = (uint8_t *)calloc(n2, 1);
    s_meet      = (uint8_t *)calloc(n2, 1);
    s_joined    = (uint8_t *)calloc(n2, 1);
    s_ramp_band = (int32_t *)malloc(sizeof(int32_t) * n2);
    s_ramp_edge = (int8_t *)calloc(n2, 1);
    if (!s_mouth || !s_tunnel || !s_ramp || !s_meet || !s_joined || !s_ramp_band || !s_ramp_edge)
        goto fail;
    memset(s_tunnel, 0xFF, sizeof(int32_t) * n2);
    memset(s_ramp_band, 0xFF, sizeof(int32_t) * n2);
    claims(c);
    tunnels();
    links();
    joined_junctions(c);
    regions(c);
    if (unit_ends(c) != 0 || edge_ends() != 0 || interchange_gores(c) != 0 || roads() != 0)
        goto fail;
    for (i = 0; i < s_nend; i++)
        name_end(i);
    return 0;
fail:
    R_ERR("net", "the topology could not be built: out of memory");
    free_all();
    s_nend = s_nroad = s_ncell = 0;
    return -1;
}

int topology_ends(void)
{
    return s_nend;
}

const TopoEnd *topology_end(int i)
{
    return i >= 0 && i < s_nend ? &s_end[i] : NULL;
}

int topology_roads(void)
{
    return s_nroad;
}

const TopoRoad *topology_road(int i)
{
    return i >= 0 && i < s_nroad ? &s_road[i] : NULL;
}

int32_t topology_side(void)
{
    return s_n;
}

int topology_linked(int net, int32_t cell, int edge)
{
    int32_t u = cell;
    int     k;
    for (k = 0; k < T_NETS && T_NET[k] != net; k++)
        ;
    if (k == T_NETS || !s_link[k] || cell < 0 || cell >= s_n * s_n)
        return 0;
    if (net == RNET_BAND)
        u = ((cell / s_n) & ~1) * s_n + ((cell % s_n) & ~1);
    return ((s_link[k][u] | s_off[k][u]) & edge) != 0;
}

int topology_ramp_edges(int32_t cell, int *road_edge, int *band_edge)
{
    if (!s_ramp || cell < 0 || cell >= s_n * s_n || !s_ramp[cell])
        return 0;
    *road_edge = s_claim[0][cell];
    *band_edge = s_ramp_edge[cell];
    return 1;
}

int32_t topology_cell(int i)
{
    return i >= 0 && i < s_ncell ? s_cell[i] : -1;
}

const char *topology_end_name(int kind)
{
    static const char *const NAME[END_KINDS] = {"node", "dead end", "level meet", "seam", "map edge", "gore"};
    return kind >= 0 && kind < END_KINDS ? NAME[kind] : "?";
}

/*  An end's network.  An end two networks meet at has none of its own. */
static const char *net_name(int net)
{
    return net < 0 ? "road+rail" : rcity_net_name(net);
}

void topology_dump(void)
{
    int i, j, count[END_KINDS] = {0};
    for (i = 0; i < s_nend; i++)
        count[s_end[i].kind]++;
    dumpf("topology  %d ends  %d roads\n", s_nend, s_nroad);
    {
        int32_t c, mouths = 0, joined = 0;
        for (c = 0; s_mouth && c < s_n * s_n; c++)
            if (s_mouth[c])
                mouths++, joined += s_tunnel[c] >= 0;
        dumpf("topology  %d tunnel mouths, %d with a far mouth\n", (int)mouths, (int)joined);
    }
    for (i = 0; i < END_KINDS; i++)
        dumpf("topology  %-8s %d\n", topology_end_name(i), count[i]);
    for (i = 0; i < s_nend; i++)
    {
        const TopoEnd *e = &s_end[i];
        dumpf("end   %016llx  %-8s %-9s", (unsigned long long)e->id, topology_end_name(e->kind), net_name(e->net));
        for (j = 0; j < e->ncell; j++)
            dumpf(" %d,%d", (int)(s_cell[e->cell0 + j] % s_n), (int)(s_cell[e->cell0 + j] / s_n));
        dumpf("\n");
    }
    for (i = 0; i < s_nroad; i++)
    {
        const TopoRoad *r = &s_road[i];
        dumpf("road  %016llx  %-4s units %d  from %016llx to %016llx", (unsigned long long)r->id, net_name(r->net), r->nunit, (unsigned long long)s_end[r->end[0]].id, (unsigned long long)s_end[r->end[1]].id);
        dumpf(" ");
        for (j = 0; j < r->nunit; j++)
            dumpf(" %d,%d", (int)(s_cell[r->unit0 + j] % s_n), (int)(s_cell[r->unit0 + j] / s_n));
        dumpf("\n");
    }
}

/* ---- the state ----------------------------------------------------------- */

/*  Every variable of this file, as the store saves and loads it whole
 *  (store.h).  The release frees everything the variables own. */
/* clang-format off */
#define TOPOLOGY_STATE(X) \
    X(s_n) X(s_claim) X(s_link) X(s_off) X(s_used) X(s_endof) X(s_seam) X(s_mouth) X(s_tunnel) \
    X(s_ramp) X(s_meet) X(s_joined) X(s_region) X(s_nregion) X(s_ramp_band) X(s_ramp_edge) \
    X(s_ramp_gore) X(s_drive_left) X(s_end) X(s_road) X(s_cell) X(s_nend) X(s_cap_end) X(s_nroad) \
    X(s_cap_road) X(s_ncell) X(s_cap_cell)
/* clang-format on */
NET_STATE(topology, TOPOLOGY_STATE)

void topology_release(void)
{
    free_all();
    free(s_end), free(s_road), free(s_cell);
    s_end = NULL, s_road = NULL, s_cell = NULL;
    s_nend = s_cap_end = s_nroad = s_cap_road = s_ncell = s_cap_cell = 0;
}
