/*  city.c: what the view of the cells holds on its own.  The reader is
 *  the simulation's, and adapt.c fills the view from it.  So this holds
 *  only what depends on nothing but the view.  That is the map's size
 *  and the buffers of that size, and the view's layers.  It also holds
 *  the corner mask a view from each quarter reads the corner nibble
 *  under. */
#include "city.h"

#include "log.h"

#include <stdlib.h>
#include <string.h>

int32_t r_map = 128;

/*  Every buffer the size of the map, and the bytes it holds per tile. */
#define MAP_BUFS 256
static struct
{
    void **buf;
    size_t per;
    int    kind;  /* each tile, each corner, or a count that grows with the map */
    size_t count; /* the count on a map 128 tiles a side, for the third kind */
} s_bufs[MAP_BUFS];
static int s_nbufs;

enum
{
    BUF_TILES,
    BUF_CORNERS,
    BUF_SCALED
};

static size_t cells(int32_t n, int i)
{
    size_t e = (size_t)n + (s_bufs[i].kind == BUF_CORNERS ? 1u : 0u);
    if (s_bufs[i].kind == BUF_SCALED)
        return (s_bufs[i].count * (size_t)n * (size_t)n + 128u * 128u - 1u) / (128u * 128u);
    return e * e;
}

static void add(void **buf, size_t per, int kind, size_t count)
{
    if (s_nbufs == MAP_BUFS)
        abort(); /* a constant too small for the program itself */
    s_bufs[s_nbufs].buf   = buf;
    s_bufs[s_nbufs].per   = per;
    s_bufs[s_nbufs].kind  = kind;
    s_bufs[s_nbufs].count = count;
    *buf                  = calloc(cells(r_map, s_nbufs), per);
    if (!*buf)
        abort();
    ++s_nbufs;
}

void r_map_register(void **buf, size_t bytes_per_tile)
{
    add(buf, bytes_per_tile, BUF_TILES, 0);
}

void r_grid_register(void **buf, size_t bytes_per_corner)
{
    add(buf, bytes_per_corner, BUF_CORNERS, 0);
}

void r_scaled_register(void **buf, size_t bytes, size_t count)
{
    add(buf, bytes, BUF_SCALED, count);
}

size_t r_map_bytes(const void *buf)
{
    int i;
    for (i = 0; i < s_nbufs; ++i)
        if (*s_bufs[i].buf == buf)
            return cells(r_map, i) * s_bufs[i].per;
    return 0;
}

int r_map_set(int32_t n)
{
    int i;
    if (n == r_map)
        return 0;
    /*  The mesh cuts the map into chunks 32 tiles a side, and holds at
     *  most 64 of them a side (mesh/mesh.h). */
    if (n < 32 || n > 2048 || n % 32)
    {
        R_ERR("render", "a map of %d tiles a side: the size is a multiple of 32, up to 2048", (int)n);
        return -1;
    }
    for (i = 0; i < s_nbufs; ++i)
    {
        free(*s_bufs[i].buf);
        *s_bufs[i].buf = calloc(cells(n, i), s_bufs[i].per);
        if (!*s_bufs[i].buf)
        {
            R_ERR("render", "no memory for a map of %d tiles a side", (int)n);
            abort();
        }
    }
    r_map = n;
    return 0;
}

/*  The bytes of each layer for a map `n` tiles a side, in the order the
 *  block holds them.  The word layer comes first, so it is aligned. */
static size_t layout(RCity *c, int32_t n, unsigned char *b)
{
    size_t full = (size_t)n * (size_t)n, half = full / 4u, qtr = full / 16u, at = 0;
#define LAYER(f, cnt)                \
    do                               \
    {                                \
        if (b)                       \
            c->f = (void *)(b + at); \
        at += sizeof *c->f * (cnt);  \
    } while (0)
    LAYER(map.altm, full);
    LAYER(xbld, full);
    LAYER(xzon, full);
    LAYER(map.xter, full);
    LAYER(xund, full);
    LAYER(xtxt, full);
    LAYER(xbit, full);
    LAYER(street, full);
    LAYER(xtrf, half);
    LAYER(xplt, half);
    LAYER(xval, half);
    LAYER(xcrm, half);
    LAYER(xplc, qtr);
    LAYER(xfir, qtr);
    LAYER(xpop, qtr);
    LAYER(xrog, qtr);
#undef LAYER
    return at;
}

int rcity_size(RCity *c, int32_t n)
{
    void *b;
    if (c->block && c->n == n)
        return 0;
    b = calloc(1, layout(c, n, NULL));
    if (!b)
        return -1;
    free(c->block);
    c->block = b;
    c->n     = n;
    layout(c, n, (unsigned char *)b);
    return 0;
}

int rcity_copy(RCity *dst, const RCity *src)
{
    void *keep;
    if (rcity_size(dst, src->n) != 0)
        return -1;
    keep = dst->block;
    memcpy(keep, src->block, layout(dst, src->n, NULL));
    memcpy(dst, src, sizeof *dst);
    dst->block = keep;
    layout(dst, dst->n, (unsigned char *)keep);
    return 0;
}

static void tile_layer(unsigned char *dst, const unsigned char *src, int32_t n, size_t per, int32_t k)
{
    int32_t r, i, j;
    size_t  row = (size_t)n * per;
    for (i = 0; i < k; ++i)
        for (r = 0; r < n; ++r)
            for (j = 0; j < k; ++j)
                memcpy(dst + (((size_t)i * (size_t)n + (size_t)r) * (size_t)k + (size_t)j) * row, src + (size_t)r * row, row);
}

int rcity_tile(RCity *dst, const RCity *src, int32_t k)
{
    int32_t n = src->n;
    void   *keep;
    if (rcity_size(dst, n * k) != 0)
        return -1;
    keep = dst->block;
    memcpy(dst, src, sizeof *dst);
    dst->block = keep;
    dst->n     = n * k;
    layout(dst, dst->n, (unsigned char *)keep);
#define TILE(f, edge) tile_layer((unsigned char *)dst->f, (const unsigned char *)src->f, edge, sizeof *src->f, k)
    TILE(map.altm, n);
    TILE(xbld, n);
    TILE(xzon, n);
    TILE(map.xter, n);
    TILE(xund, n);
    TILE(xtxt, n);
    TILE(xbit, n);
    TILE(street, n);
    TILE(xtrf, n / 2);
    TILE(xplt, n / 2);
    TILE(xval, n / 2);
    TILE(xcrm, n / 2);
    TILE(xplc, n / 4);
    TILE(xfir, n / 4);
    TILE(xpop, n / 4);
    TILE(xrog, n / 4);
#undef TILE
    return 0;
}

uint64_t rcity_hash(const RCity *c)
{
    RCity          v;
    size_t         n = layout(&v, c->n, NULL), k;
    const uint8_t *b = (const uint8_t *)c->block;
    uint64_t       h = 14695981039346656037ull ^ (uint64_t)c->n ^ ((uint64_t)c->drive_left << 32);
    for (k = 0; k + 8 <= n; k += 8)
    {
        uint64_t w;
        memcpy(&w, b + k, 8);
        h = (h ^ w) * 0x100000001B3ull;
        h ^= h >> 29;
    }
    for (; k < n; ++k)
        h = (h ^ b[k]) * 0x100000001B3ull;
    return h;
}

void rcity_free(RCity *c)
{
    free(c->block);
    memset(c, 0, sizeof *c);
}

uint8_t city_corner_mask(int32_t quarter)
{
    /*  ROT_CORNER_MASK.  A turned view moves the bytes but leaves XZON's
     *  high nibble alone.  So the bit that means "nearest the viewer"
     *  changes with the quarter. */
    static const uint8_t mask[4] = {0x80, 0x10, 0x20, 0x40};
    return mask[quarter & 3];
}
