/*  tiles.c: the chunk store and its directory.  See tiles.h. */
#include "tiles.h"

#include <stdlib.h>
#include <string.h>

static TileChunk *chunk_new(int32_t cx, int32_t cy)
{
    TileChunk *k = NULL;
    if (posix_memalign((void **)&k, 64, sizeof *k) != 0)
        return NULL;
    memset(k, 0, sizeof *k);
    k->cx = cx;
    k->cy = cy;
    return k;
}

int tilemap_init(TileMap *m)
{
    memset(m, 0, sizeof *m);
    m->bare = chunk_new(0, 0);
    return m->bare ? 0 : -1;
}

void tilemap_free(TileMap *m)
{
    int32_t i;
    for (i = 0; i < m->n; i++)
        free(m->all[i]);
    free(m->all);
    free(m->dir);
    free(m->bare);
    memset(m, 0, sizeof *m);
}

/*  Floor division by the chunk's side, for a tile below nought. */
static int32_t chunk_of(int32_t t)
{
    return t >> TILE_SHIFT;
}

/*  The directory widened to the chunks cx0..cx1, cy0..cy1, inclusive. */
static int dir_cover(TileMap *m, int32_t cx0, int32_t cy0, int32_t cx1, int32_t cy1)
{
    int32_t     nx0, ny0, nx1, ny1, nw, nh, x, y;
    TileChunk **d;
    if (m->w)
    {
        if (cx0 >= m->cx0 && cy0 >= m->cy0 && cx1 < m->cx0 + m->w && cy1 < m->cy0 + m->h)
            return 0;
        nx0 = cx0 < m->cx0 ? cx0 : m->cx0;
        ny0 = cy0 < m->cy0 ? cy0 : m->cy0;
        nx1 = cx1 > m->cx0 + m->w - 1 ? cx1 : m->cx0 + m->w - 1;
        ny1 = cy1 > m->cy0 + m->h - 1 ? cy1 : m->cy0 + m->h - 1;
    }
    else
    {
        nx0 = cx0, ny0 = cy0, nx1 = cx1, ny1 = cy1;
    }
    nw = nx1 - nx0 + 1;
    nh = ny1 - ny0 + 1;
    d  = (TileChunk **)malloc(sizeof *d * (size_t)nw * (size_t)nh);
    if (!d)
        return -1;
    for (y = 0; y < nh; y++)
        for (x = 0; x < nw; x++)
        {
            int32_t cx = nx0 + x, cy = ny0 + y;
            int     in    = m->w && cx >= m->cx0 && cy >= m->cy0 && cx < m->cx0 + m->w && cy < m->cy0 + m->h;
            d[y * nw + x] = in ? m->dir[(cy - m->cy0) * m->w + (cx - m->cx0)] : m->bare;
        }
    free(m->dir);
    m->dir = d;
    m->cx0 = nx0;
    m->cy0 = ny0;
    m->w   = nw;
    m->h   = nh;
    return 0;
}

TileChunk *tilemap_chunk(TileMap *m, int32_t cx, int32_t cy, int make)
{
    TileChunk **slot, *k;
    if (m->w && cx >= m->cx0 && cy >= m->cy0 && cx < m->cx0 + m->w && cy < m->cy0 + m->h)
    {
        k = m->dir[(cy - m->cy0) * m->w + (cx - m->cx0)];
        if (k != m->bare)
            return k;
    }
    if (!make || dir_cover(m, cx, cy, cx, cy))
        return NULL;
    if (m->n == m->cap)
    {
        int32_t     cap = m->cap ? m->cap * 2 : 64;
        TileChunk **p   = (TileChunk **)realloc(m->all, sizeof *p * (size_t)cap);
        if (!p)
            return NULL;
        m->all = p;
        m->cap = cap;
    }
    k = chunk_new(cx, cy);
    if (!k)
        return NULL;
    m->all[m->n++] = k;
    slot           = &m->dir[(cy - m->cy0) * m->w + (cx - m->cx0)];
    *slot          = k;
    return k;
}

int tilemap_reserve(TileMap *m, int32_t x0, int32_t y0, int32_t x1, int32_t y1)
{
    int32_t cx, cy, cx0 = chunk_of(x0), cy0 = chunk_of(y0), cx1 = chunk_of(x1 - 1), cy1 = chunk_of(y1 - 1);
    if (x1 <= x0 || y1 <= y0)
        return 0;
    if (dir_cover(m, cx0, cy0, cx1, cy1))
        return -1;
    for (cy = cy0; cy <= cy1; cy++)
        for (cx = cx0; cx <= cx1; cx++)
            if (!tilemap_chunk(m, cx, cy, 1))
                return -1;
    return 0;
}

int tilemap_cover(TileMap *m, int32_t x0, int32_t y0, int32_t x1, int32_t y1)
{
    if (x1 <= x0 || y1 <= y0)
        return 0;
    return dir_cover(m, chunk_of(x0), chunk_of(y0), chunk_of(x1 - 1), chunk_of(y1 - 1));
}

int tilemap_copy(TileMap *dst, const TileMap *src)
{
    int32_t i;
    for (i = 0; i < src->n; i++)
    {
        const TileChunk *s = src->all[i];
        TileChunk       *d = tilemap_chunk(dst, s->cx, s->cy, 1);
        if (!d)
            return -1;
        memcpy(d, s, sizeof *d);
    }
    dst->has_streets = src->has_streets;
    dst->drive_left  = src->drive_left;
    return 0;
}
