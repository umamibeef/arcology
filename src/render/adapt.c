/*  adapt.c -- see adapt.h. */
#include "adapt.h"

#include <string.h>

void adapt_city(RCity *v, const City *c)
{
    size_t n, k;

    /*  The layer arrays have the same element type and count on both
     *  sides; only their shape differs (rows and columns against a flat
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
    v->n_things = (int32_t) n;

    for (k = 0; k < 1200u && k < (size_t) MISC_LONGS; ++k)
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
