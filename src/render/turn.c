/*  turn.c: see turn.h. */
#include "turn.h"

#include "tables.h"

#include <stdio.h>
#include <string.h>

void adapt_turned_dst(int32_t sx, int32_t sy, int q, int32_t n, int32_t *x, int32_t *y)
{
    int k;
    for (k = 0; k < q; ++k)
    {
        int32_t px = n - 1 - sy, py = sx;
        sx = px;
        sy = py;
    }
    *x = sx;
    *y = sy;
}

void adapt_turned_src(int32_t x, int32_t y, int q, int32_t n, int32_t *sx, int32_t *sy)
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

/*  One XTHG record, 12 bytes, as the view a quarter turn on shows it.
 *  The original turns its records with the same arithmetic at $3B96E.
 *  +3 is y and +4 is x, and the pair shows at (127 - y, x).  The type at
 *  +0 selects one of three arms.  Type 1 ($3B9A8) turns its
 *  eight-direction heading at +1, the pair at +8 and +9, and the nibble
 *  at +2.  Types 10 to 13 ($3BA54) turn a four-direction heading at +1,
 *  the eight-direction value at +8, and the pair at +6 and +7.  All other
 *  types ($3BAFC) turn the heading at +1 and the pair at +8 and +9.  An
 *  empty record, type 0, stays as it is. */
static void turned_thing(uint8_t *r)
{
    uint8_t t = r[0], y;
    if (t == 0)
        return;
    y    = r[3];
    r[3] = r[4];
    r[4] = (uint8_t)((127 - y) & 0xFF);
    if (t >= 10 && t <= 13)
    {
        r[1] = (uint8_t)((r[1] - 1) & 3);
        r[8] = (uint8_t)((r[8] - 2) & 7);
        y    = r[6]; /* $3BAC2 */
        r[6] = r[7];
        r[7] = (uint8_t)((127 - y) & 0xFF);
        return;
    }
    r[1] = (uint8_t)((r[1] - 2) & 7);
    y    = r[8];
    r[8] = r[9];
    r[9] = (uint8_t)((127 - y) & 0xFF);
    if (t == 1)
        r[2] = (uint8_t)(((((r[2] >> 4) - 2) & 7) << 4) | (r[2] & 0x0F));
}

/*  The XBLD id and the XBIT byte of one cell, as the view a quarter turn
 *  on shows them.  The id goes through R_TURN_XBLD.  XBIT bit 0x02 goes
 *  with the direction of some ids.  So the original sets it again
 *  ($3B3A8..$3B4BE), and the view does the same.
 *
 *  Ids 0x51..0x5C, 0x6A and 0x6B toggle the bit.  When the bit goes from
 *  clear to set, the id goes through R_TURN_XBLD a second time ($3B422).
 *  When the bit goes from set to clear, the id stays.
 *
 *  Ids 0x5D..0x60 read two tables at one index: the id less 0x5D, plus 4
 *  when the bit is set.  R_TURN_5D_ID gives the new id and
 *  R_TURN_5D_BIT gives the new bit. */
static void turned_cell(uint8_t *id, uint8_t *bit)
{
    int b = R_TURN_XBLD[*id];
    if ((b >= 0x51 && b < 0x5D) || b == 0x6A || b == 0x6B)
    {
        if (*bit & 0x02)
            *bit &= (uint8_t)~0x02; /* $3B404 */
        else
        {
            *bit |= 0x02;       /* $3B41C */
            b = R_TURN_XBLD[b]; /* $3B432 */
        }
    }
    else if (b >= 0x5D && b < 0x61)
    {
        int k = b - 0x5D + ((*bit & 0x02) ? 4 : 0);
        b     = R_TURN_5D_ID[k]; /* $3B474 */
        *bit &= (uint8_t)~0x02;  /* $3B48C */
        if (R_TURN_5D_BIT[k])
            *bit |= 0x02; /* $3B4A4 */
    }
    *id = (uint8_t)b;
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
            adapt_turned_src(x, y, q, n, &sx, &sy);
            b = src[sy * n + sx];
            if (table)
                for (k = 0; k < q; ++k)
                    b = table[b];
            dst[y * n + x] = b;
        }
}

/*  The renderer's view of the city seen from a quarter turn, 1 to 3,
 *  filled from the city as it is.  Every cell of the turned view reads
 *  its own cell of the grid.  The ids that encode a direction pass
 *  through the original's tables for that view, as do the moving things'
 *  headings.  Nothing of the city is written: a turn is a change of
 *  perspective, and the grid stays one grid of data.  With no turn it is
 *  adapt_city. */
void adapt_view_turned(RCity *v, const RCity *u, int q)
{
    int32_t n = u->n, x, y;
    size_t  k;
    q &= 3;
    if (rcity_size(v, n) != 0)
    {
        snprintf(v->err, sizeof v->err, "out of memory");
        return;
    }
    for (y = 0; y < n; ++y)
        for (x = 0; x < n; ++x)
        {
            int32_t sx, sy;
            adapt_turned_src(x, y, q, n, &sx, &sy);
            v->map.altm[y * n + x] = u->map.altm[sy * n + sx];
        }
    for (y = 0; y < n; ++y)
        for (x = 0; x < n; ++x)
        {
            int32_t sx, sy;
            int     t;
            uint8_t id, bit;
            adapt_turned_src(x, y, q, n, &sx, &sy);
            id  = u->xbld[sy * n + sx];
            bit = u->xbit[sy * n + sx];
            for (t = 0; t < q; ++t)
                turned_cell(&id, &bit);
            v->xbld[y * n + x] = id;
            v->xbit[y * n + x] = bit;
        }
    turned_layer8(v->xzon, u->xzon, n, q, NULL);
    turned_layer8(v->map.xter, u->map.xter, n, q, R_TURN_XTER);
    turned_layer8(v->xund, u->xund, n, q, R_TURN_XUND);
    /*  The street layer names its edges by column and row, and a turned
     *  view draws sprites alone, so it carries none. */
    memset(v->street, 0, (size_t)n * (size_t)n);
    turned_layer8(v->xtxt, u->xtxt, n, q, NULL);
    turned_layer8(v->xtrf, u->xtrf, n / 2, q, NULL);
    turned_layer8(v->xplt, u->xplt, n / 2, q, NULL);
    turned_layer8(v->xval, u->xval, n / 2, q, NULL);
    turned_layer8(v->xcrm, u->xcrm, n / 2, q, NULL);
    turned_layer8(v->xplc, u->xplc, n / 4, q, NULL);
    turned_layer8(v->xfir, u->xfir, n / 4, q, NULL);
    turned_layer8(v->xpop, u->xpop, n / 4, q, NULL);
    turned_layer8(v->xrog, u->xrog, n / 4, q, NULL);
    memcpy(v->xthg, u->xthg, sizeof v->xthg);
    v->n_things = u->n_things;
    for (k = 0; k < (size_t)u->n_things; ++k)
    {
        int t;
        for (t = 0; t < q; ++t)
            turned_thing(v->xthg + k * 12u);
    }
    memcpy(v->misc, u->misc, sizeof v->misc);
    v->view_quarter = (u->view_quarter + q) & 3;
    v->x0           = u->x0;
    v->y0           = u->y0;
    memcpy(v->name, u->name, sizeof v->name);
    v->err[0] = '\0';
}
