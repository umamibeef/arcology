/*  adapt.c: see adapt.h. */
#include "adapt.h"

#include <string.h>

void adapt_city(RCity *v, const City *c)
{
    size_t n, k;

    /*  The layer arrays have the same element type and count on both
     *  sides.  Only their shape differs (rows and columns against a flat
     *  run), which memcpy does not care about. */
    memcpy(v->altm, c->altm, sizeof v->altm);
    memcpy(v->xbld, c->xbld, sizeof v->xbld);
    memcpy(v->xzon, c->xzon, sizeof v->xzon);
    memcpy(v->xter, c->xter, sizeof v->xter);
    memcpy(v->xund, c->xund, sizeof v->xund);
    memcpy(v->xtxt, c->xtxt, sizeof v->xtxt);
    memcpy(v->xbit, c->xbit, sizeof v->xbit);
    memcpy(v->xtrf, c->xtrf, sizeof v->xtrf);
    memcpy(v->xplt, c->xplt, sizeof v->xplt);
    memcpy(v->xval, c->xval, sizeof v->xval);
    memcpy(v->xcrm, c->xcrm, sizeof v->xcrm);
    memcpy(v->xplc, c->xplc, sizeof v->xplc);
    memcpy(v->xfir, c->xfir, sizeof v->xfir);
    memcpy(v->xpop, c->xpop, sizeof v->xpop);
    memcpy(v->xrog, c->xrog, sizeof v->xrog);

    memset(v->xthg, 0, sizeof v->xthg);
    n = c->xthg ? c->xthg_len / 12u : 0u;
    if (n > R_MAX_THINGS)
        n = R_MAX_THINGS;
    if (n)
        memcpy(v->xthg, c->xthg, n * 12u);
    v->n_things = (int32_t)n;

    for (k = 0; k < 1200u && k < (size_t)MISC_LONGS; ++k)
        v->misc[k] = c->misc[k];
    v->rotation = c->rotation & 3;

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

/*  The renderer's view of the city seen from a quarter turn, 1 to 3,
 *  filled from the city as it is.  Every cell of the turned view reads
 *  its own cell of the grid.  The ids that encode a direction pass
 *  through the original's art table for that orientation, as do the
 *  moving things' headings.  Nothing of the city is rewritten: the grid
 *  is one grid of data, and a turn is a change of perspective.  The
 *  original gets the same art by rotating its map, and this reads it off
 *  the unturned one.  With no turn it is adapt_city. */
static void turned_src(int32_t x, int32_t y, int q, int32_t n, int32_t *sx, int32_t *sy)
{
    int k;
    for (k = 0; k < q; ++k)
    {
        int32_t nx = y, ny = n - 1 - x;
        x = nx;
        y = ny;
    }
    *sx = x;
    *sy = y;
}

static void turned_layer8(uint8_t *dst, const uint8_t *src, int32_t n, int q, const uint8_t *table)
{
    int32_t x, y;
    for (y = 0; y < n; ++y)
        for (x = 0; x < n; ++x)
        {
            int32_t sx, sy;
            uint8_t b;
            int     k;
            turned_src(x, y, q, n, &sx, &sy);
            b = src[sy * n + sx];
            if (table)
                for (k = 0; k < q; ++k)
                    b = table[b];
            dst[y * n + x] = b;
        }
}

void adapt_city_turned(RCity *v, const City *c, int q)
{
    size_t  n, k;
    int32_t x, y;
    q &= 3;
    if (q == 0)
    {
        adapt_city(v, c);
        return;
    }
    for (y = 0; y < MAP_H; ++y)
        for (x = 0; x < MAP_W; ++x)
        {
            int32_t sx, sy;
            turned_src(x, y, q, MAP_W, &sx, &sy);
            v->altm[y * R_MAP + x] = c->altm[sy][sx];
        }
    turned_layer8(v->xbld, &c->xbld[0][0], MAP_W, q, sim_rot_table(0));
    turned_layer8(v->xzon, &c->xzon[0][0], MAP_W, q, NULL);
    turned_layer8(v->xter, &c->xter[0][0], MAP_W, q, sim_rot_table(1));
    turned_layer8(v->xund, &c->xund[0][0], MAP_W, q, sim_rot_table(2));
    turned_layer8(v->xtxt, &c->xtxt[0][0], MAP_W, q, NULL);
    turned_layer8(v->xbit, &c->xbit[0][0], MAP_W, q, NULL);
    turned_layer8(v->xtrf, &c->xtrf[0][0], HALF_W, q, NULL);
    turned_layer8(v->xplt, &c->xplt[0][0], HALF_W, q, NULL);
    turned_layer8(v->xval, &c->xval[0][0], HALF_W, q, NULL);
    turned_layer8(v->xcrm, &c->xcrm[0][0], HALF_W, q, NULL);
    turned_layer8(v->xplc, &c->xplc[0][0], QTR_W, q, NULL);
    turned_layer8(v->xfir, &c->xfir[0][0], QTR_W, q, NULL);
    turned_layer8(v->xpop, &c->xpop[0][0], QTR_W, q, NULL);
    turned_layer8(v->xrog, &c->xrog[0][0], QTR_W, q, NULL);

    memset(v->xthg, 0, sizeof v->xthg);
    n = c->xthg ? c->xthg_len / 12u : 0u;
    if (n > R_MAX_THINGS)
        n = R_MAX_THINGS;
    if (n)
    {
        memcpy(v->xthg, c->xthg, n * 12u);
        for (k = 0; k < n; ++k)
        {
            int t;
            for (t = 0; t < q; ++t)
                sim_rotate_thing(v->xthg + k * 12u);
        }
    }
    v->n_things = (int32_t)n;

    for (k = 0; k < 1200u && k < (size_t)MISC_LONGS; ++k)
        v->misc[k] = c->misc[k];
    v->rotation = (c->rotation + q) & 3; /* the corner the art anchors on, for that orientation */

    v->name[0] = '\0';
    if (c->cnam && c->cnam_len)
    {
        size_t len = c->cnam_len < sizeof v->name - 1u ? c->cnam_len : sizeof v->name - 1u;
        memcpy(v->name, c->cnam, len);
        v->name[len] = '\0';
    }
    v->err[0] = '\0';
}
