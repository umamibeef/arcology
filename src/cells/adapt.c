/*  adapt.c: see adapt.h. */
#include "adapt.h"

#include "net_cell.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*  Fill `v` from `c`.  About 156 KB of copying, which is cheap next to a
 *  sweep.  Call it whenever the simulation has run. */
static void adapt_city(RCity *v, const City *c);

static void adapt_city(RCity *v, const City *c)
{
    size_t n, k;

    /*  The layer arrays have the same element type and count on both
     *  sides.  Only their shape differs (rows and columns against a flat
     *  run), which memcpy does not care about. */
    if (rcity_size(v, MAP_W) != 0)
    {
        snprintf(v->err, sizeof v->err, "out of memory");
        return;
    }
    city_layer_get(c, CL_ALTM, v->map.altm);
    city_layer_get(c, CL_XBLD, v->xbld);
    city_layer_get(c, CL_XZON, v->xzon);
    city_layer_get(c, CL_XTER, v->map.xter);
    city_layer_get(c, CL_XUND, v->xund);
    city_layer_get(c, CL_XTXT, v->xtxt);
    city_layer_get(c, CL_XBIT, v->xbit);
    {
        int32_t y, x;
        for (y = 0; y < MAP_H; y++)
            for (x = 0; x < MAP_W; x++)
                v->street[y * MAP_W + x] = T_STREET(c, y, x);
    }
    city_layer_get(c, CL_XTRF, v->xtrf);
    city_layer_get(c, CL_XPLT, v->xplt);
    city_layer_get(c, CL_XVAL, v->xval);
    city_layer_get(c, CL_XCRM, v->xcrm);
    city_layer_get(c, CL_XPLC, v->xplc);
    city_layer_get(c, CL_XFIR, v->xfir);
    city_layer_get(c, CL_XPOP, v->xpop);
    city_layer_get(c, CL_XROG, v->xrog);

    memset(v->xthg, 0, sizeof v->xthg);
    n = c->xthg ? c->xthg_len / 12u : 0u;
    if (n > R_MAX_THINGS)
        n = R_MAX_THINGS;
    if (n)
        memcpy(v->xthg, c->xthg, n * 12u);
    v->n_things = (int32_t)n;

    for (k = 0; k < 1200u && k < (size_t)MISC_LONGS; ++k)
        v->misc[k] = c->misc[k];
    v->view_quarter = 0;
    v->drive_left   = c->map ? c->map->drive_left : 0;

    v->name[0] = '\0';
    if (c->cnam && c->cnam_len)
    {
        size_t len = c->cnam_len < sizeof v->name - 1u ? c->cnam_len
                                                       : sizeof v->name - 1u;
        memcpy(v->name, c->cnam, len);
        v->name[len] = '\0';
    }
    v->err[0] = '\0';
}

int32_t adapt_world_side(const World *w)
{
    int32_t x0, y0, x1, y1, side;
    world_bounds(w, &x0, &y0, &x1, &y1);
    side = x1 - x0 > y1 - y0 ? x1 - x0 : y1 - y0;
    return side < MAP_W ? MAP_W : (side + 3) & ~3;
}

void adapt_world(RCity *v, const World *w, int active)
{
    int32_t     x0, y0, x1, y1, n = adapt_world_side(w), i, k;
    const City *c = w->n_city ? w->city[active].city : NULL;

    world_bounds(w, &x0, &y0, &x1, &y1);
    if (rcity_size(v, n) != 0)
    {
        snprintf(v->err, sizeof v->err, "out of memory");
        return;
    }
    v->x0 = x0;
    v->y0 = y0;
    memset(v->map.altm, 0, sizeof *v->map.altm * (size_t)n * (size_t)n);
    memset(v->map.xter, 0, (size_t)n * (size_t)n);
    memset(v->xbld, 0, (size_t)n * (size_t)n);
    memset(v->xzon, 0, (size_t)n * (size_t)n);
    memset(v->xund, 0, (size_t)n * (size_t)n);
    memset(v->xtxt, 0, (size_t)n * (size_t)n);
    memset(v->xbit, 0, (size_t)n * (size_t)n);
    /*  Every chunk at its place in the view, the view's first tile the
     *  world's first. */
    for (k = 0; k < w->map.n; k++)
    {
        const TileChunk *ch = w->map.all[k];
        int32_t          ox = ch->cx * TILE_CHUNK - x0, oy = ch->cy * TILE_CHUNK - y0, y, x;
        for (y = 0; y < TILE_CHUNK; y++)
            for (x = 0; x < TILE_CHUNK; x++)
            {
                int32_t s = y * TILE_CHUNK + x, d = (oy + y) * n + ox + x;
                v->map.altm[d] = ch->altm[s];
                v->map.xter[d] = ch->xter[s];
                v->xbld[d]     = ch->xbld[s];
                v->xzon[d]     = ch->xzon[s];
                v->xund[d]     = ch->xund[s];
                v->xtxt[d]     = ch->xtxt[s];
                v->xbit[d]     = ch->xbit[s];
                v->street[d]   = ch->street[s];
            }
        for (y = 0; y < TILE_HALF_CHUNK; y++)
            for (x = 0; x < TILE_HALF_CHUNK; x++)
            {
                int32_t s = y * TILE_HALF_CHUNK + x, d = (oy / 2 + y) * (n / 2) + ox / 2 + x;
                v->xtrf[d] = ch->xtrf[s];
                v->xplt[d] = ch->xplt[s];
                v->xval[d] = ch->xval[s];
                v->xcrm[d] = ch->xcrm[s];
            }
        for (y = 0; y < TILE_QTR_CHUNK; y++)
            for (x = 0; x < TILE_QTR_CHUNK; x++)
            {
                int32_t s = y * TILE_QTR_CHUNK + x, d = (oy / 4 + y) * (n / 4) + ox / 4 + x;
                v->xplc[d] = ch->xplc[s];
                v->xfir[d] = ch->xfir[s];
                v->xpop[d] = ch->xpop[s];
                v->xrog[d] = ch->xrog[s];
            }
    }
    /*  The moving things of every city, each at its place in the view.
     *  A record carries its tile in a byte each way, so a thing past the
     *  view's 256th tile is not shown. */
    memset(v->xthg, 0, sizeof v->xthg);
    v->n_things = 0;
    for (i = 0; i < w->n_city; i++)
    {
        const City *ci = w->city[i].city;
        size_t      m  = ci->xthg ? ci->xthg_len / 12u : 0u, j;
        for (j = 0; j < m && v->n_things < R_MAX_THINGS; j++)
        {
            uint8_t *t  = v->xthg + (size_t)v->n_things * 12u;
            int32_t  ty = ci->xthg[j * 12u + 3] + w->city[i].city->oy - y0;
            int32_t  tx = ci->xthg[j * 12u + 4] + w->city[i].city->ox - x0;
            if (tx > 255 || ty > 255)
                continue;
            memcpy(t, ci->xthg + j * 12u, 12u);
            t[3] = (uint8_t)ty;
            t[4] = (uint8_t)tx;
            v->n_things++;
        }
    }
    /*  The records of the city the player is in. */
    memset(v->misc, 0, sizeof v->misc);
    v->view_quarter = 0;
    v->drive_left   = w->map.drive_left;
    v->name[0]      = '\0';
    if (c)
    {
        for (k = 0; k < 1200 && k < MISC_LONGS; ++k)
            v->misc[k] = c->misc[k];
        if (c->cnam && c->cnam_len)
        {
            size_t len = c->cnam_len < sizeof v->name - 1u ? c->cnam_len : sizeof v->name - 1u;
            memcpy(v->name, c->cnam, len);
            v->name[len] = '\0';
        }
    }
    v->err[0] = '\0';
}

/*  The view read straight from a file.  One reader for the simulation
 *  and the renderer, so the view cannot decode a file differently from
 *  the simulation it draws. */
int rcity_load(RCity *v, const char *path)
{
    City *c = (City *)calloc(1, sizeof *c);
    if (!c)
    {
        snprintf(v->err, sizeof v->err, "out of memory");
        return -1;
    }
    if (!city_load(path, c))
    {
        snprintf(v->err, sizeof v->err, "%s is not a city this reader knows", path);
        free(c);
        return -1;
    }
    adapt_city(v, c);
    city_free(c);
    free(c);
    return 0;
}

/*  Layer 0, in the renderer's own terms.  The values are the
 *  simulation's, one for one. */
_Static_assert(RNET_ROAD == NETW_ROAD && RNET_RAIL == NETW_RAIL && RNET_BAND == NETW_BAND && RNET_PIPE == NETW_PIPE,
               "the networks are numbered as the simulation numbers them");
_Static_assert(RPART_FITTED == NETP_FITTED && RPART_CROSSING == NETP_CROSSING && RPART_SLOPE_FOOT == NETP_SLOPE_FOOT &&
                   RPART_MOUTH == NETP_TUNNEL_MOUTH && RPART_BRIDGE == NETP_BRIDGE && RPART_RAMP == NETP_RAMP &&
                   RPART_TO_SUBWAY == NETP_TO_SUBWAY && RPART_STATION == NETP_STATION && RPART_SLAB == NETP_SLAB &&
                   RPART_INCLINE == NETP_INCLINE && RPART_CURVE == NETP_CURVE &&
                   RPART_INTERCHANGE == NETP_INTERCHANGE && RPART_SPAN == NETP_SPAN,
               "the kinds of part are numbered as the simulation numbers them");

int rcity_parts(uint8_t xbld, uint8_t xbit, RPart out[2])
{
    NetPart p[2];
    int     n = net_cell_parts(xbld, xbit, p), i;
    for (i = 0; i < n; i++)
    {
        out[i].net    = p[i].net;
        out[i].edges  = p[i].edges;
        out[i].kind   = p[i].kind;
        out[i].toward = p[i].toward;
    }
    return n;
}

int rcity_lot_population(uint8_t xbld)
{
    return BUILDING[xbld].population;
}

const char *rcity_net_name(int net)
{
    return net_cell_net_name(net);
}
