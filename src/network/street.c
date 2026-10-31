/*  network/street.c: the street layer's import, worked out from a city's view
 *  through the road network's topology (network/topology.c). */
#include "network/street.h"

#include <stdlib.h>

#include "network/store.h"
#include "network/topology.h"

static int find(int *up, int i)
{
    while (up[i] != i)
        i = up[i] = up[up[i]];
    return i;
}

/*  A street's class from a figure and two steps: 0 below the first, 1 an
 *  avenue at it, 2 a boulevard at the second. */
static int step_class(float v, float avenue, float boulevard)
{
    return v >= boulevard ? 2 : v >= avenue ? 1
                                            : 0;
}

static int push(StreetEdge **out, int *n, int *cap, int32_t a, int32_t b, uint8_t edge)
{
    if (a < 0 || b < 0)
        return 0;
    if (*n == *cap)
    {
        StreetEdge *p = (StreetEdge *)realloc(*out, sizeof *p * (size_t)(*cap ? 2 * *cap : 1024));
        if (!p)
            return -1;
        *out = p, *cap = *cap ? 2 * *cap : 1024;
    }
    (*out)[*n].a    = a;
    (*out)[*n].b    = b;
    (*out)[*n].edge = edge;
    (*n)++;
    return 0;
}

/*  A STREET'S CLASS on import, once, for a city that holds no street
 *  layer.  It is the higher of two classes.  One is a class by its
 *  traffic.  The other is a class by the population of the lots that
 *  reach it, per tile of its length.  The traffic is the simulation's
 *  own byte, 0 to 255, read at half resolution.  A lot's population is
 *  the original's own figure for its building, 1 to 8.  The steps are
 *  the network's, so they are constants here and not tuned. */
#define STREET_TRAFFIC_AVENUE    64.0f
#define STREET_TRAFFIC_BOULEVARD 160.0f
#define STREET_POP_AVENUE        4.0f
#define STREET_POP_BOULEVARD     8.0f

int street_infer(const RCity *v, StreetEdge **out)
{
    float   pop_av = STREET_POP_AVENUE, pop_bd = STREET_POP_BOULEVARD;
    float   trf_av = STREET_TRAFFIC_AVENUE, trf_bd = STREET_TRAFFIC_BOULEVARD;
    int     nt, i, c, k, n = 0, cap = 0, *up = NULL, *units = NULL, *cls = NULL, *seen_at = NULL;
    float  *pop = NULL, *trf = NULL;
    int32_t side = v->n, half = side / 2;
    *out = NULL;
    net_store_begin();
    if (topology_build(v) != 0)
    {
        net_store_discard();
        return -1;
    }
    nt      = topology_roads();
    up      = (int *)malloc(sizeof(int) * (size_t)(nt + 1));
    units   = (int *)calloc((size_t)(nt + 1), sizeof(int));
    cls     = (int *)calloc((size_t)(nt + 1), sizeof(int));
    pop     = (float *)calloc((size_t)(nt + 1), sizeof(float));
    trf     = (float *)calloc((size_t)(nt + 1), sizeof(float));
    seen_at = (int *)malloc(sizeof(int) * (size_t)side * (size_t)side);
    if (!up || !units || !cls || !pop || !trf || !seen_at)
        goto fail;
    for (i = 0; i < side * side; i++)
        seen_at[i] = -1;
    for (i = 0; i < nt; i++)
        up[i] = i;
    /*  The roads one street is made of: joined across each seam. */
    {
        int  ne    = topology_ends();
        int *first = (int *)malloc(sizeof(int) * (size_t)(ne + 1));
        if (!first)
            goto fail;
        for (i = 0; i < ne; i++)
            first[i] = -1;
        for (i = 0; i < nt; i++)
            for (c = 0; c < 2 && topology_road(i)->net == RNET_ROAD; c++)
            {
                int e = topology_road(i)->end[c];
                if (topology_end(e)->kind != END_SEAM)
                    continue;
                if (first[e] < 0)
                    first[e] = i;
                else
                    up[find(up, i)] = find(up, first[e]);
            }
        free(first);
    }
    /*  Each street's units, the traffic on them and the population of the
     *  lots that reach them, each lot once. */
    for (i = 0; i < nt; i++)
    {
        const TopoRoad *t = topology_road(i);
        int             s = find(up, i);
        if (t->net != RNET_ROAD)
            continue;
        for (k = 0; k < t->nunit; k++)
        {
            int32_t cell = topology_cell(t->unit0 + k), col = cell % side, row = cell / side;
            int     d;
            units[s]++;
            trf[s] += (float)v->xtrf[(row / 2) * half + col / 2];
            for (d = 0; d < 4; d++)
            {
                int32_t nc = col + (d == 0 ? -1 : d == 2 ? 1
                                                         : 0),
                        nr = row + (d == 1 ? 1 : d == 3 ? -1
                                                        : 0),
                        lot;
                if (nc < 0 || nr < 0 || nc >= side || nr >= side)
                    continue;
                lot = nr * side + nc;
                if (seen_at[lot] == s)
                    continue;
                seen_at[lot] = s;
                pop[s] += (float)rcity_lot_population(v->xbld[lot]);
            }
        }
    }
    for (i = 0; i < nt; i++)
        if (find(up, i) == i && units[i])
        {
            int by_traffic = step_class(trf[i] / (float)units[i], trf_av, trf_bd);
            int by_pop     = step_class(pop[i] / (float)units[i], pop_av, pop_bd);
            cls[i]         = by_traffic > by_pop ? by_traffic : by_pop;
        }
    /*  Every edge of each street: to its ends, and between its units. */
    for (i = 0; i < nt; i++)
    {
        const TopoRoad *t = topology_road(i);
        uint8_t         e;
        if (t->net != RNET_ROAD)
            continue;
        e = (uint8_t)(cls[find(up, i)] & 0x03);
        if (!t->nunit)
        {
            if (push(out, &n, &cap, t->at[0], t->at[1], e) != 0)
                goto fail;
            continue;
        }
        for (c = 0; c < 2; c++)
            if (push(out, &n, &cap, t->at[c], topology_cell(c ? t->unit0 + t->nunit - 1 : t->unit0), e) != 0)
                goto fail;
        for (k = 1; k < t->nunit; k++)
            if (push(out, &n, &cap, topology_cell(t->unit0 + k - 1), topology_cell(t->unit0 + k), e) != 0)
                goto fail;
    }
    free(up), free(units), free(cls), free(pop), free(trf), free(seen_at);
    net_store_discard();
    return n;
fail:
    free(up), free(units), free(cls), free(pop), free(trf), free(seen_at), free(*out);
    *out = NULL;
    net_store_discard();
    return -1;
}
