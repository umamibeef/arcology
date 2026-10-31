/*  world.c: the cities on the world's chunk store.  See world.h. */
#include "world.h"

#include <stdlib.h>
#include <string.h>

int world_init(World *w)
{
    memset(w, 0, sizeof *w);
    return tilemap_init(&w->map);
}

void world_free(World *w)
{
    int32_t i;
    for (i = 0; i < w->n_city; i++)
        if (w->city[i].city)
        {
            city_free(w->city[i].city);
            free(w->city[i].city);
        }
    free(w->city);
    tilemap_free(&w->map);
    memset(w, 0, sizeof *w);
}

TileChunk *world_chunk(World *w, int32_t cx, int32_t cy, int make)
{
    return tilemap_chunk(&w->map, cx, cy, make);
}

TileChunk *world_at(World *w, int32_t x, int32_t y, int make, int32_t *i)
{
    *i = tile_index(x, y);
    return tilemap_chunk(&w->map, x >> TILE_SHIFT, y >> TILE_SHIFT, make);
}

int world_street_edge(World *w, int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint8_t edge)
{
    int32_t    i, kx = x0 < x1 || y0 < y1 ? x0 : x1, ky = x0 < x1 || y0 < y1 ? y0 : y1;
    int        along_col = y0 == y1;
    TileChunk *k;
    if (!((y0 == y1 && (x1 - x0 == 1 || x0 - x1 == 1)) || (x0 == x1 && (y1 - y0 == 1 || y0 - y1 == 1))))
        return -1;
    k = world_at(w, kx, ky, 0, &i);
    if (!k)
        return -1;
    k->street[i] = along_col ? STREET_BYTE(edge, STREET_NEXT_ROW(k->street[i])) : STREET_BYTE(STREET_NEXT_COL(k->street[i]), edge);
    return 0;
}

int world_claims(uint8_t xzon, uint8_t xbld)
{
    return (xzon & 0x0F) != 0 || xbld >= 0x0E;
}

/*  Every city's cached directory, after the store has been widened. */
static void bind_all(World *w)
{
    int32_t i;
    for (i = 0; i < w->n_city; i++)
        if (w->city[i].city)
            city_bind(w->city[i].city);
}

int world_place_city(World *w, City *c, int32_t x0, int32_t y0)
{
    if ((x0 & TILE_MASK) || (y0 & TILE_MASK) || w->n_city >= WORLD_MAX_CITIES)
        return -1;
    if (tilemap_reserve(&w->map, x0, y0, x0 + c->w, y0 + c->h) ||
        tilemap_cover(&w->map, x0 - CITY_MARGIN, y0 - CITY_MARGIN, x0 + c->w + CITY_MARGIN, y0 + c->h + CITY_MARGIN))
        return -1;
    if (w->n_city == w->cap_city)
    {
        int32_t    cap = w->cap_city ? w->cap_city * 2 : 4;
        WorldCity *p   = (WorldCity *)realloc(w->city, sizeof *p * (size_t)cap);
        if (!p)
            return -1;
        w->city     = p;
        w->cap_city = cap;
    }
    c->map                    = &w->map;
    c->own_map                = 0;
    c->ox                     = x0;
    c->oy                     = y0;
    w->city[w->n_city++].city = c;
    bind_all(w);
    return w->n_city - 1;
}

int world_add_city(World *w, City *c, int32_t x0, int32_t y0)
{
    TileMap *own = c->own_map ? c->map : NULL;
    int32_t  y, x, slot;
    for (y = 0; y < c->h; y++)
        for (x = 0; x < c->w; x++)
        {
            int32_t    i;
            TileChunk *k = world_at(w, x0 + x, y0 + y, 0, &i);
            if (k && k->owner[i] && world_claims(T_XZON(c, y, x), T_XBLD(c, y, x)))
                return -1;
        }
    if ((x0 & TILE_MASK) || (y0 & TILE_MASK) || tilemap_reserve(&w->map, x0, y0, x0 + c->w, y0 + c->h))
        return -1;
    bind_all(w);
    /*  The window, tile by tile.  The land is the map's and is laid
     *  whatever stands there.  What is built goes where nobody owns the
     *  tile. */
    for (y = 0; y < c->h; y++)
        for (x = 0; x < c->w; x++)
        {
            int32_t    i;
            TileChunk *k = world_at(w, x0 + x, y0 + y, 1, &i);
            k->altm[i]   = T_ALTM(c, y, x);
            k->xter[i]   = T_XTER(c, y, x);
            if (k->owner[i])
                continue;
            k->xbld[i]  = T_XBLD(c, y, x);
            k->xzon[i]  = T_XZON(c, y, x);
            k->xund[i]  = T_XUND(c, y, x);
            k->xbit[i]  = T_XBIT(c, y, x);
            k->xtxt[i]  = T_XTXT(c, y, x);
            k->owner[i] = world_claims(T_XZON(c, y, x), T_XBLD(c, y, x)) ? (uint8_t)(w->n_city + 1) : 0;
        }
    for (y = 0; y < c->h / 2; y++)
        for (x = 0; x < c->w / 2; x++)
        {
            int32_t    i;
            TileChunk *k = world_at(w, x0 + 2 * x, y0 + 2 * y, 1, &i);
            int32_t    h = tile_half_index((x0 >> 1) + x, (y0 >> 1) + y);
            k->xtrf[h]   = T_XTRF(c, y, x);
            k->xplt[h]   = T_XPLT(c, y, x);
            k->xval[h]   = T_XVAL(c, y, x);
            k->xcrm[h]   = T_XCRM(c, y, x);
        }
    for (y = 0; y < c->h / 4; y++)
        for (x = 0; x < c->w / 4; x++)
        {
            int32_t    i;
            TileChunk *k = world_at(w, x0 + 4 * x, y0 + 4 * y, 1, &i);
            int32_t    q = tile_qtr_index((x0 >> 2) + x, (y0 >> 2) + y);
            k->xplc[q]   = T_XPLC(c, y, x);
            k->xfir[q]   = T_XFIR(c, y, x);
            k->xpop[q]   = T_XPOP(c, y, x);
            k->xrog[q]   = T_XROG(c, y, x);
        }
    slot = world_place_city(w, c, x0, y0);
    if (slot < 0)
        return -1;
    if (own)
    {
        tilemap_free(own);
        free(own);
    }
    return slot;
}

int world_city_grow(World *w, int i, int32_t cw, int32_t ch)
{
    int rc = city_grow(w->city[i].city, cw, ch);
    bind_all(w);
    return rc;
}

void world_bounds(const World *w, int32_t *x0, int32_t *y0, int32_t *x1, int32_t *y1)
{
    int32_t i;
    *x0 = *y0 = *x1 = *y1 = 0;
    for (i = 0; i < w->map.n; i++)
    {
        int32_t ax = w->map.all[i]->cx * TILE_CHUNK, ay = w->map.all[i]->cy * TILE_CHUNK;
        if (i == 0 || ax < *x0)
            *x0 = ax;
        if (i == 0 || ay < *y0)
            *y0 = ay;
        if (i == 0 || ax + TILE_CHUNK > *x1)
            *x1 = ax + TILE_CHUNK;
        if (i == 0 || ay + TILE_CHUNK > *y1)
            *y1 = ay + TILE_CHUNK;
    }
}
