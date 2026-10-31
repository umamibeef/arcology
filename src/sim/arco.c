/* ==================================================================== *
 *  .arco: the reader and writer.  See arco.h for what the format is
 *  and why it is a ZIP.
 *
 *  The ZIP here is deliberately the smallest thing that a real unzip
 *  will open.
 *
 *      Local headers.
 *      A central directory.
 *      An end record.
 *      Deflate from lodepng.
 *
 *  This the renderer already carries for PNG.  No zip64, no encryption,
 *  no directory entries.  A file this reader writes opens in Finder,
 *  Explorer, `unzip`, and Python's `zipfile`.
 *
 *  What is NOT here, on purpose: a schema.  The manifest is written by
 *  hand and read by a parser that looks for the keys it wants and
 *  ignores the rest.  That is what makes an older reader survive a
 *  newer file, and it is the same bargain PNG makes with its ancillary
 *  chunks.
 * ==================================================================== */
#include "arco.h"
#include "lodepng.h"
#include "world.h"

#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- a growable buffer -------------------------------------------- */
typedef struct
{
    uint8_t *p;
    size_t   n, cap;
} Buf;

static int buf_need(Buf *b, size_t extra)
{
    size_t   want = b->n + extra;
    uint8_t *q;
    /*  The buffer and its size come together: a size with no buffer
     *  behind it is one every put would write through nothing. */
    if (b->p && want <= b->cap)
        return 0;
    while (b->cap < want)
        b->cap = b->cap ? b->cap * 2 : 4096;
    q = (uint8_t *)realloc(b->p, b->cap);
    if (!q)
        return -1;
    b->p = q;
    return 0;
}

static int buf_put(Buf *b, const void *d, size_t n)
{
    if (!n)
        return 0; /* nothing to put, and no buffer needed to put it in */
    if (buf_need(b, n))
        return -1;
    memcpy(b->p + b->n, d, n);
    b->n += n;
    return 0;
}

static int buf_u16(Buf *b, unsigned v)
{
    uint8_t t[2];
    t[0] = (uint8_t)(v & 0xFF);
    t[1] = (uint8_t)((v >> 8) & 0xFF);
    return buf_put(b, t, 2);
}

static int buf_u32(Buf *b, uint32_t v)
{
    uint8_t t[4];
    t[0] = (uint8_t)(v & 0xFF);
    t[1] = (uint8_t)((v >> 8) & 0xFF);
    t[2] = (uint8_t)((v >> 16) & 0xFF);
    t[3] = (uint8_t)((v >> 24) & 0xFF);
    return buf_put(b, t, 4);
}

static int buf_str(Buf *b, const char *s) { return buf_put(b, s, strlen(s)); }

/*  printf into a Buf.  The manifest is the only thing that needs it and
 *  every line is short. */
static int buf_fmt(Buf *b, const char *fmt, ...)
{
    char    t[512];
    va_list ap;
    int     n;
    va_start(ap, fmt);
    n = vsnprintf(t, sizeof t, fmt, ap);
    va_end(ap);
    if (n < 0 || (size_t)n >= sizeof t)
        return -1;
    return buf_put(b, t, (size_t)n);
}

/* ---- the ZIP ------------------------------------------------------- */
/*  One entry, remembered until the central directory is written. */
typedef struct
{
    char     name[128];
    uint32_t crc, csize, usize, offset;
    int      stored; /* 1 if deflate did not help, so method 0 */
} Entry;

typedef struct
{
    Buf    out;
    Entry *e;
    size_t n, cap;
} Zip;

static int zip_add(Zip *z, const char *name, const void *data, size_t len)
{
    uint8_t *comp = NULL;
    size_t   clen = 0;
    Entry   *e;
    unsigned err;
    int      stored = 0;

    if (z->n == z->cap)
    {
        size_t c = z->cap ? z->cap * 2 : 32;
        Entry *q = (Entry *)realloc(z->e, c * sizeof *q);
        if (!q)
            return -1;
        z->e   = q;
        z->cap = c;
    }
    if (len)
    {
        err = lodepng_deflate(&comp, &clen, (const unsigned char *)data, len, &lodepng_default_compress_settings);
        if (err)
            return -1;
    }
    /*  An already-dense layer can deflate LARGER than it started.  Store
     *  it raw when that happens, which is what method 0 is for. */
    if (!len || clen >= len)
    {
        free(comp);
        comp   = NULL;
        clen   = len;
        stored = 1;
    }

    e = &z->e[z->n++];
    memset(e, 0, sizeof *e);
    snprintf(e->name, sizeof e->name, "%s", name);
    e->crc    = lodepng_crc32((const unsigned char *)data, len);
    e->usize  = (uint32_t)len;
    e->csize  = (uint32_t)clen;
    e->offset = (uint32_t)z->out.n;
    e->stored = stored;

    buf_u32(&z->out, 0x04034B50);
    buf_u16(&z->out, 20);             /* version needed  */
    buf_u16(&z->out, 0);              /* flags           */
    buf_u16(&z->out, stored ? 0 : 8); /* method          */
    buf_u16(&z->out, 0);              /* time            */
    buf_u16(&z->out, 0x21);           /* date: 1980-01-01, so a file is
                                       * reproducible byte for byte    */
    buf_u32(&z->out, e->crc);
    buf_u32(&z->out, e->csize);
    buf_u32(&z->out, e->usize);
    buf_u16(&z->out, (unsigned)strlen(e->name));
    buf_u16(&z->out, 0);
    buf_str(&z->out, e->name);
    buf_put(&z->out, stored ? data : comp, clen);
    free(comp);
    return 0;
}

static int zip_finish(Zip *z, const char *path)
{
    size_t start = z->out.n, cd_size, i;
    FILE  *f;

    for (i = 0; i < z->n; i++)
    {
        Entry *e = &z->e[i];
        buf_u32(&z->out, 0x02014B50);
        buf_u16(&z->out, 20); /* made by */
        buf_u16(&z->out, 20); /* needed  */
        buf_u16(&z->out, 0);
        buf_u16(&z->out, e->stored ? 0 : 8);
        buf_u16(&z->out, 0);
        buf_u16(&z->out, 0x21);
        buf_u32(&z->out, e->crc);
        buf_u32(&z->out, e->csize);
        buf_u32(&z->out, e->usize);
        buf_u16(&z->out, (unsigned)strlen(e->name));
        buf_u16(&z->out, 0);
        buf_u16(&z->out, 0);
        buf_u16(&z->out, 0);
        buf_u16(&z->out, 0);
        buf_u32(&z->out, 0);
        buf_u32(&z->out, e->offset);
        buf_str(&z->out, e->name);
    }
    /*  Measure the central directory BEFORE the end record starts.
     *  Otherwise the twelve bytes written just below land inside the
     *  size and every reader computes a negative prefix. */
    cd_size = z->out.n - start;
    buf_u32(&z->out, 0x06054B50);
    buf_u16(&z->out, 0); /* this disk               */
    buf_u16(&z->out, 0); /* disk with the directory */
    buf_u16(&z->out, (unsigned)z->n);
    buf_u16(&z->out, (unsigned)z->n);
    buf_u32(&z->out, (uint32_t)cd_size);
    buf_u32(&z->out, (uint32_t)start);
    buf_u16(&z->out, 0); /* archive comment length  */

    f = fopen(path, "wb");
    if (!f)
        return -1;
    if (fwrite(z->out.p, 1, z->out.n, f) != z->out.n)
    {
        fclose(f);
        return -1;
    }
    fclose(f);
    return 0;
}

static void zip_free(Zip *z)
{
    free(z->out.p);
    free(z->e);
    memset(z, 0, sizeof *z);
}

/* ---- reading ------------------------------------------------------- */
typedef struct
{
    uint8_t *raw;
    size_t   len;
} Ar;

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}
static unsigned rd16(const uint8_t *p)
{
    return (unsigned)p[0] | ((unsigned)p[1] << 8);
}

/*  Find one entry and give back its bytes, inflated.  Linear over the
 *  local headers: a world holds tens of files, not thousands, and
 *  walking them needs no index. */
static uint8_t *ar_get(const Ar *a, const char *name, size_t *out_len)
{
    size_t p = 0;
    while (p + 30 <= a->len && rd32(a->raw + p) == 0x04034B50)
    {
        unsigned method = rd16(a->raw + p + 8);
        uint32_t csize = rd32(a->raw + p + 18), usize = rd32(a->raw + p + 22);
        unsigned nlen = rd16(a->raw + p + 26), elen = rd16(a->raw + p + 28);
        size_t   dat = p + 30 + nlen + elen;
        if (dat + csize > a->len)
            return NULL;
        if (nlen == strlen(name) &&
            memcmp(a->raw + p + 30, name, nlen) == 0)
        {
            uint8_t *out;
            if (method == 0)
            {
                out = (uint8_t *)malloc(csize ? csize : 1);
                if (!out)
                    return NULL;
                memcpy(out, a->raw + dat, csize);
                *out_len = csize;
                return out;
            }
            out = NULL;
            {
                size_t   n   = 0;
                unsigned err = lodepng_inflate(&out, &n, a->raw + dat, csize, &lodepng_default_decompress_settings);
                if (err || n != usize)
                {
                    free(out);
                    return NULL;
                }
                *out_len = n;
                return out;
            }
        }
        p = dat + csize;
    }
    return NULL;
}

/*  The manifest reader.  It looks for `"key":` and takes the number
 *  after it, anywhere in the document.  That is enough for a flat
 *  manifest and it means an unknown key costs nothing. */
static long json_int(const char *js, const char *key, long dflt)
{
    char        pat[64];
    const char *p;
    snprintf(pat, sizeof pat, "\"%s\"", key);
    p = strstr(js, pat);
    if (!p)
        return dflt;
    p = strchr(p, ':');
    if (!p)
        return dflt;
    return strtol(p + 1, NULL, 10);
}

/* ---- layers -------------------------------------------------------- */
/*  Every grid a chunk holds, with its element size and its side.  Adding
 *  a layer is a line here and a file in the archive.  A reader that does
 *  not know the name simply does not ask for it. */
typedef struct
{
    const char *name;
    size_t      off;  /* where it lies in a TileChunk */
    size_t      elem; /* 1 or 2 bytes */
    int         side; /* cells a side */
} Layer;

#define LAYER(nm, sz, sd) {#nm, offsetof(TileChunk, nm), (sz), (sd)}
static const Layer LAYERS[] = {
    LAYER(altm, 2, TILE_CHUNK),
    LAYER(xbld, 1, TILE_CHUNK),
    LAYER(xzon, 1, TILE_CHUNK),
    LAYER(xter, 1, TILE_CHUNK),
    LAYER(xund, 1, TILE_CHUNK),
    LAYER(xbit, 1, TILE_CHUNK),
    LAYER(xtxt, 1, TILE_CHUNK),
    LAYER(owner, 1, TILE_CHUNK),
    LAYER(street, 1, TILE_CHUNK),
    LAYER(xtrf, 1, TILE_HALF_CHUNK),
    LAYER(xplt, 1, TILE_HALF_CHUNK),
    LAYER(xval, 1, TILE_HALF_CHUNK),
    LAYER(xcrm, 1, TILE_HALF_CHUNK),
    LAYER(xplc, 1, TILE_QTR_CHUNK),
    LAYER(xfir, 1, TILE_QTR_CHUNK),
    LAYER(xpop, 1, TILE_QTR_CHUNK),
    LAYER(xrog, 1, TILE_QTR_CHUNK),
};
#undef LAYER
#define N_LAYERS ((int)(sizeof LAYERS / sizeof LAYERS[0]))

/*  A chunk's layers, little-endian.  Little-endian is the one place this
 *  format breaks with the original on purpose: the 1995 file is
 *  big-endian because a 68000 was.  Every machine that will ever open a
 *  .arco is not.  The street layer is written only where the world
 *  has one.  A world imported from a .sc2 has none until it is worked
 *  out.  A file without it is worked out again when it is read. */
static int put_chunk(Zip *z, const TileChunk *k, int with_street)
{
    int l;
    for (l = 0; l < N_LAYERS; l++)
    {
        const Layer *ly = &LAYERS[l];
        if (!with_street && !strcmp(ly->name, "street"))
            continue;
        size_t         n = (size_t)ly->side * (size_t)ly->side * ly->elem;
        const uint8_t *s = (const uint8_t *)k + ly->off;
        uint8_t       *d = (uint8_t *)malloc(n);
        char           path[160];
        size_t         i;
        int            rc;
        if (!d)
            return -1;
        if (ly->elem == 2)
            for (i = 0; i < n / 2; i++)
            {
                uint16_t v   = ((const uint16_t *)(const void *)s)[i];
                d[2 * i]     = (uint8_t)(v & 0xFF);
                d[2 * i + 1] = (uint8_t)(v >> 8);
            }
        else
            memcpy(d, s, n);
        snprintf(path, sizeof path, "chunks/%d_%d/%s.bin", (int)k->cx, (int)k->cy, ly->name);
        rc = zip_add(z, path, d, n);
        free(d);
        if (rc)
            return -1;
    }
    return 0;
}

/*  A layer the file does not carry stays as it was, which is 0.  The
 *  caller learns whether the owner and the street layer were there, so
 *  either can be worked out from the tiles where it was not. */
static int get_chunk(const Ar *a, TileChunk *k, int *had_owner, int *had_street)
{
    int l;
    *had_owner = *had_street = 0;
    for (l = 0; l < N_LAYERS; l++)
    {
        const Layer *ly   = &LAYERS[l];
        size_t       want = (size_t)ly->side * (size_t)ly->side * ly->elem, got = 0, i;
        uint8_t     *d = (uint8_t *)k + ly->off;
        char         path[160];
        uint8_t     *s;
        snprintf(path, sizeof path, "chunks/%d_%d/%s.bin", (int)k->cx, (int)k->cy, ly->name);
        s = ar_get(a, path, &got);
        if (!s)
            continue;
        if (got != want)
        {
            free(s);
            return -1;
        }
        if (ly->elem == 2)
            for (i = 0; i < want / 2; i++)
                ((uint16_t *)(void *)d)[i] = (uint16_t)(s[2 * i] | ((uint16_t)s[2 * i + 1] << 8));
        else
            memcpy(d, s, want);
        if (!strcmp(ly->name, "owner"))
            *had_owner = 1;
        if (!strcmp(ly->name, "street"))
            *had_street = 1;
        free(s);
    }
    return 0;
}

/* ---- the manifest -------------------------------------------------- */
static void json_escape(char *out, size_t n, const char *in)
{
    size_t o = 0;
    for (; *in && o + 2 < n; in++)
    {
        if (*in == '"' || *in == '\\')
        {
            out[o++] = '\\';
            out[o++] = *in;
        }
        else if ((unsigned char)*in < 0x20)
            out[o++] = ' ';
        else
            out[o++] = *in;
    }
    out[o] = 0;
}

/*  The city's name.  It is not a field on City.  It is the CNAM chunk, a
 *  Pascal string in a 32-byte slot: so it is decoded here rather than
 *  assumed. */
static void city_name(const City *c, char *out, size_t n)
{
    size_t len = 0;
    out[0]     = 0;
    if (!c->cnam || c->cnam_len < 1)
        return;
    len = c->cnam[0];
    if (len > c->cnam_len - 1)
        len = c->cnam_len - 1;
    if (len > n - 1)
        len = n - 1;
    memcpy(out, c->cnam + 1, len);
    out[len] = 0;
}

static int write_manifest(Zip *z, const World *w)
{
    Buf     b = {0};
    int32_t i, x0, y0, x1, y1;
    int     rc;

    world_bounds(w, &x0, &y0, &x1, &y1);
    buf_fmt(&b, "{\n");
    buf_fmt(&b, "  \"format\": \"arcology-world\",\n");
    buf_fmt(&b, "  \"version\": %d,\n", ARCO_VERSION);
    buf_fmt(&b, "\n");
    buf_fmt(&b, "  \"_\": \"The map is in chunks/<cx>_<cy>/<layer>.bin, little-endian, row-major, chunk_size "
                "square, one directory for each chunk listed. A chunk not listed is bare land. The world is as "
                "large as its chunks and grows with them. A layer file that is absent is simply not carried.\",\n\n");
    buf_fmt(&b, "  \"chunk_size\": %d,\n", TILE_CHUNK);
    buf_fmt(&b, "  \"drive_left\": %d,\n", w->map.drive_left ? 1 : 0);
    buf_fmt(&b, "  \"bounds\": { \"x0\": %d, \"y0\": %d, \"x1\": %d, \"y1\": %d },\n", (int)x0, (int)y0, (int)x1, (int)y1);
    buf_fmt(&b, "  \"chunk_list\": \"");
    for (i = 0; i < w->map.n; i++)
        buf_fmt(&b, "%s%d_%d", i ? "," : "", (int)w->map.all[i]->cx, (int)w->map.all[i]->cy);
    buf_fmt(&b, "\",\n\n");
    buf_fmt(&b, "  \"_cities\": \"Each city runs the window its origin and extent give, and owns the tiles "
                "chunks/*/owner.bin gives its number plus one.\",\n");
    buf_fmt(&b, "  \"_streets\": \"chunks/*/street.bin is the player's street data, a byte a tile: the edge to the "
                "tile in the next column in the low four bits and the edge to the tile in the next row in the high "
                "four. In an edge, bits 0-1 are the class (0 a street, 1 an avenue, 2 a boulevard) and bits 2-3 the "
                "direction (0 two-way, 1 one way from this tile to the next, 2 one way from the next tile to this "
                "one). An edge counts only while the two tiles join across it.\",\n");
    buf_fmt(&b, "  \"cities\": [\n");
    for (i = 0; i < w->n_city; i++)
    {
        char raw[64], name[128];
        city_name(w->city[i].city, raw, sizeof raw);
        json_escape(name, sizeof name, raw[0] ? raw : "Untitled");
        buf_fmt(&b, "    { \"id\": \"%d\", \"name\": \"%s\", \"origin\": { \"x\": %d, \"y\": %d }, ", (int)i, name, (int)w->city[i].city->ox, (int)w->city[i].city->oy);
        buf_fmt(&b, "\"window\": { \"w\": %d, \"h\": %d }, ", (int)w->city[i].city->w, (int)w->city[i].city->h);
        buf_fmt(&b, "\"state\": \"cities/%d.json\" }%s\n", (int)i, i + 1 < w->n_city ? "," : "");
    }
    buf_fmt(&b, "  ]\n");
    buf_fmt(&b, "}\n");
    rc = zip_add(z, "world.json", b.p, b.n);
    free(b.p);
    return rc;
}

static int write_city(Zip *z, const City *c, int id)
{
    Buf  b = {0};
    char name[128], p[64];
    int  rc, k;

    {
        char raw[64];
        city_name(c, raw, sizeof raw);
        json_escape(name, sizeof name, raw[0] ? raw : "Untitled");
    }
    buf_fmt(&b, "{\n");
    buf_fmt(&b, "  \"name\": \"%s\",\n", name);
    buf_fmt(&b, "  \"founded\": %d,\n", (int)c->year_founded);
    buf_fmt(&b, "  \"date\": %d,\n", (int)c->date);
    buf_fmt(&b, "  \"difficulty\": %d,\n", (int)c->difficulty);
    buf_fmt(&b, "  \"funds\": %d,\n", (int)c->funds);
    buf_fmt(&b, "  \"bonds\": %d,\n", (int)c->bonds);
    buf_fmt(&b, "  \"population\": %d,\n", (int)c->population);
    buf_fmt(&b, "\n");
    buf_fmt(&b, "  \"_\": \"These are the fields the reconstruction has "
                "named. They are written so the file can be read, but "
                "misc.bin is what is loaded.\",\n\n");
    buf_fmt(&b, "  \"indicators\": {\n");
    buf_fmt(&b, "    \"land_value\": %d,\n", (int)c->land_value_tot);
    buf_fmt(&b, "    \"crime\": %d,\n", (int)c->crime_tot);
    buf_fmt(&b, "    \"traffic\": %d,\n", (int)c->traffic_tot);
    buf_fmt(&b, "    \"pollution\": %d,\n", (int)c->pollution_tot);
    buf_fmt(&b, "    \"unemployment\": %d,\n", (int)c->unemployment);
    buf_fmt(&b, "    \"approval\": %d\n", (int)c->approval);
    buf_fmt(&b, "  },\n");
    buf_fmt(&b, "  \"utilities\": {\n");
    buf_fmt(&b, "    \"power_supplied_pct\": %d,\n", (int)c->power_pct);
    buf_fmt(&b, "    \"water_supplied_pct\": %d\n", (int)c->water_pct);
    buf_fmt(&b, "  },\n");
    /*  Where the world came from, and the order the 1995 file listed its
     *  chunks in.  Both are a record for a reader.  This format stores
     *  layers by name, and nothing is built from the order. */
    buf_fmt(&b, "  \"imported_from\": \"%s\",\n", c->n_chunks ? "sc2" : "arcology");
    buf_fmt(&b, "  \"sc2_chunk_order\": \"");
    for (k = 0; k < c->n_chunks; k++)
        buf_fmt(&b, "%s%s", k ? "," : "", c->order[k]);
    buf_fmt(&b, "\",\n");
    buf_fmt(&b, "  \"state\": {\n");
    buf_fmt(&b, "    \"misc\": \"cities/%d/misc.bin\",\n", id);
    buf_fmt(&b, "    \"signs\": \"cities/%d/xlab.bin\",\n", id);
    buf_fmt(&b, "    \"microsim\": \"cities/%d/xmic.bin\",\n", id);
    buf_fmt(&b, "    \"things\": \"cities/%d/xthg.bin\",\n", id);
    buf_fmt(&b, "    \"graphs\": \"cities/%d/graphs.bin\"\n", id);
    buf_fmt(&b, "  }\n");
    buf_fmt(&b, "}\n");
    snprintf(p, sizeof p, "cities/%d.json", id);
    rc = zip_add(z, p, b.p, b.n);
    free(b.p);
    return rc;
}

/*  A city's own state.  MISC is written little-endian, with the named
 *  scalars folded back in.  The graph history follows, and then every
 *  chunk the reconstruction does not take apart, verbatim. */
static int write_state(Zip *z, const City *c, int id)
{
    static int32_t misc[MISC_LONGS];
    static int32_t misc_le[MISC_LONGS];
    static int32_t g[N_GRAPH * GRAPH_SAMPLES];
    char           p[64];
    int            i, gi, gk;

    memcpy(misc, c->misc, sizeof misc);
    city_scalars_to_misc(c, misc);
    for (i = 0; i < MISC_LONGS; i++)
    {
        uint32_t v                  = (uint32_t)misc[i];
        ((uint8_t *)&misc_le[i])[0] = (uint8_t)(v & 0xFF);
        ((uint8_t *)&misc_le[i])[1] = (uint8_t)((v >> 8) & 0xFF);
        ((uint8_t *)&misc_le[i])[2] = (uint8_t)((v >> 16) & 0xFF);
        ((uint8_t *)&misc_le[i])[3] = (uint8_t)((v >> 24) & 0xFF);
    }
    snprintf(p, sizeof p, "cities/%d/misc.bin", id);
    if (zip_add(z, p, misc_le, sizeof misc_le))
        return -1;
#define RAW(f)                                             \
    do                                                     \
    {                                                      \
        snprintf(p, sizeof p, "cities/%d/" #f ".bin", id); \
        if (c->f && zip_add(z, p, c->f, c->f##_len))       \
            return -1;                                     \
    } while (0)
    RAW(xlab);
    RAW(xmic);
    RAW(xthg);
    RAW(cnam);
#undef RAW
    /*  The graph history is NOT passed through as the original's blob.
     *  XGRP is graph[16][52] written big-endian.  So .arco stores the
     *  numbers themselves, little-endian, under a name that says what
     *  they are. */
    for (gi = 0; gi < N_GRAPH; gi++)
        for (gk = 0; gk < GRAPH_SAMPLES; gk++)
        {
            uint32_t v = (uint32_t)c->graph[gi][gk];
            uint8_t *o = (uint8_t *)&g[gi * GRAPH_SAMPLES + gk];
            o[0]       = (uint8_t)(v & 0xFF);
            o[1]       = (uint8_t)((v >> 8) & 0xFF);
            o[2]       = (uint8_t)((v >> 16) & 0xFF);
            o[3]       = (uint8_t)((v >> 24) & 0xFF);
        }
    snprintf(p, sizeof p, "cities/%d/graphs.bin", id);
    return zip_add(z, p, g, sizeof g);
}

/* ---- the public calls ---------------------------------------------- */
int arco_save_world(const char *path, const World *w, const ArcoFile *extra, int n_extra)
{
    Zip     z = {{0}, NULL, 0, 0};
    int32_t i;

    if (write_manifest(&z, w))
        goto fail;
    for (i = 0; i < w->map.n; i++)
        if (put_chunk(&z, w->map.all[i], w->map.has_streets))
            goto fail;
    for (i = 0; i < w->n_city; i++)
        if (write_city(&z, w->city[i].city, (int)i) || write_state(&z, w->city[i].city, (int)i))
            goto fail;
    /*  The caller's own files, as they were handed over. */
    for (i = 0; i < n_extra; i++)
        if (extra[i].name && zip_add(&z, extra[i].name, extra[i].data, extra[i].len))
            goto fail;
    if (zip_finish(&z, path))
        goto fail;
    zip_free(&z);
    return 0;
fail:
    zip_free(&z);
    return -1;
}

/*  One city as a world of its own: its tiles and its records, lent to a
 *  world for the writing and never taken. */
static int arco_save_with(const char *path, const City *c, const ArcoFile *extra, int n_extra)
{
    World     w;
    WorldCity one;
    int       rc = -1;
    if (world_init(&w) == 0 && tilemap_copy(&w.map, c->map) == 0)
    {
        one.city = (City *)c;
        w.city   = &one;
        w.n_city = 1;
        rc       = arco_save_world(path, &w, extra, n_extra);
        w.city   = NULL;
        w.n_city = 0;
    }
    world_free(&w);
    return rc;
}

int arco_save(const char *path, const City *c)
{
    return arco_save_with(path, c, NULL, 0);
}

static uint8_t **slot_for(City *c, const char *k, size_t **len)
{
    if (!strcmp(k, "xlab"))
    {
        *len = &c->xlab_len;
        return &c->xlab;
    }
    if (!strcmp(k, "xmic"))
    {
        *len = &c->xmic_len;
        return &c->xmic;
    }
    if (!strcmp(k, "xthg"))
    {
        *len = &c->xthg_len;
        return &c->xthg;
    }
    if (!strcmp(k, "xgrp"))
    {
        *len = &c->xgrp_len;
        return &c->xgrp;
    }
    if (!strcmp(k, "cnam"))
    {
        *len = &c->cnam_len;
        return &c->cnam;
    }
    *len = NULL;
    return NULL;
}

/*  The archive's bytes, whole.  0 on success. */
static int ar_open(const char *path, Ar *a)
{
    FILE *f = fopen(path, "rb");
    a->raw  = NULL;
    a->len  = 0;
    if (!f)
        return -1;
    fseek(f, 0, SEEK_END);
    a->len = (size_t)ftell(f);
    fseek(f, 0, SEEK_SET);
    a->raw = (uint8_t *)malloc(a->len ? a->len : 1);
    if (!a->raw || fread(a->raw, 1, a->len, f) != a->len)
    {
        fclose(f);
        free(a->raw);
        a->raw = NULL;
        return -1;
    }
    fclose(f);
    return 0;
}

/*  A file of the archive as a NUL-terminated string, which the caller
 *  frees.  NULL where there is none. */
static char *ar_text(const Ar *a, const char *name)
{
    size_t   n = 0;
    uint8_t *s = ar_get(a, name, &n), *t;
    if (!s)
        return NULL;
    t = (uint8_t *)realloc(s, n + 1);
    if (!t)
    {
        free(s);
        return NULL;
    }
    t[n] = 0;
    return (char *)t;
}

/*  One city's state, `cities/<id>/...`, into a City whose tile layers
 *  the window fills.  0 on success. */
static int read_city(const Ar *a, City *c, int id, int32_t ww, int32_t wh)
{
    static const char *const RAWS[] = {"xlab", "xmic", "xthg", "cnam", NULL};
    char                     p[64];
    size_t                   mn = 0, gn = 0;
    uint8_t                 *mb, *g;
    char                    *js;
    int                      i;

    memset(c, 0, sizeof *c);
    if (city_extent(c, ww, wh))
        return -1;
    /*  The dice come up at one: the seeds are not in the file, and a
     *  shift register at nought never leaves it. */
    rng_seed(c, 1, 1);
    snprintf(p, sizeof p, "cities/%d/misc.bin", id);
    mb = ar_get(a, p, &mn);
    if (!mb || mn != sizeof c->misc)
    {
        free(mb);
        return -1;
    }
    for (i = 0; i < MISC_LONGS; i++)
        c->misc[i] = (int32_t)((uint32_t)mb[i * 4] | ((uint32_t)mb[i * 4 + 1] << 8) |
                               ((uint32_t)mb[i * 4 + 2] << 16) | ((uint32_t)mb[i * 4 + 3] << 24));
    free(mb);
    for (i = 0; RAWS[i]; i++)
    {
        size_t   *plen;
        uint8_t **slot = slot_for(c, RAWS[i], &plen);
        size_t    got  = 0;
        uint8_t  *d;
        snprintf(p, sizeof p, "cities/%d/%s.bin", id, RAWS[i]);
        d = ar_get(a, p, &got);
        if (d && slot)
        {
            *slot = d;
            *plen = got;
        }
        else
            free(d);
    }
    /*  The scalars come from MISC, exactly as they do for a 1995 save.
     *  So there is one path that turns MISC into a City and not two that
     *  can disagree.  The graph history goes back into graph[][]. */
    snprintf(p, sizeof p, "cities/%d/graphs.bin", id);
    g = ar_get(a, p, &gn);
    if (g && gn == (size_t)N_GRAPH * GRAPH_SAMPLES * 4)
    {
        int gi, gk;
        for (gi = 0; gi < N_GRAPH; gi++)
            for (gk = 0; gk < GRAPH_SAMPLES; gk++)
            {
                const uint8_t *o = g + (gi * GRAPH_SAMPLES + gk) * 4;
                c->graph[gi][gk] = (int32_t)((uint32_t)o[0] | ((uint32_t)o[1] << 8) | ((uint32_t)o[2] << 16) |
                                             ((uint32_t)o[3] << 24));
            }
    }
    free(g);
    /*  The chunk order of the 1995 file the world came from, which the
     *  load log prints.  A world written at version 1 keeps it in
     *  world.json. */
    snprintf(p, sizeof p, "cities/%d.json", id);
    js = ar_text(a, p);
    {
        char       *wj = js && strstr(js, "\"sc2_chunk_order\"") ? NULL : ar_text(a, "world.json");
        const char *q  = strstr(wj ? wj : (js ? js : ""), "\"sc2_chunk_order\"");
        if (q && (q = strchr(q, ':')) && (q = strchr(q, '"')))
        {
            q++;
            while (*q && *q != '"' && c->n_chunks < 24)
            {
                int k = 0;
                while (k < 4 && q[k] && q[k] != ',' && q[k] != '"')
                    k++;
                if (k == 4)
                {
                    memcpy(c->order[c->n_chunks], q, 4);
                    c->order[c->n_chunks][4] = 0;
                    c->n_chunks++;
                }
                q += k;
                if (*q == ',')
                    q++;
            }
        }
        free(wj);
    }
    free(js);
    city_misc_to_scalars(c);
    return 0;
}

int arco_load_world(const char *path, World *w)
{
    Ar          a  = {NULL, 0};
    char       *js = NULL;
    const char *q;
    int         rc = -1, version, id = 0, any_owner = 0, any_street = 0;

    if (world_init(w))
        return -1;
    if (ar_open(path, &a))
    {
        world_free(w);
        return -1;
    }
    js = ar_text(&a, "world.json");
    if (!js || json_int(js, "chunk_size", TILE_CHUNK) != TILE_CHUNK)
        goto done; /* a world this build cannot lay out */
    version           = (int)json_int(js, "version", 1);
    w->map.drive_left = json_int(js, "drive_left", 0) != 0;

    /*  The chunks: the list a world names, or the four by four of a world
     *  written at version 1, one city's square. */
    if (version >= 2)
    {
        q = strstr(js, "\"chunk_list\"");
        if (!q || !(q = strchr(q, ':')) || !(q = strchr(q, '"')))
            goto done;
        q++;
        while (*q && *q != '"')
        {
            char      *e;
            long       cx = strtol(q, &e, 10), cy;
            TileChunk *k;
            int        had, had_street;
            if (*e != '_')
                goto done;
            cy = strtol(e + 1, &e, 10);
            k  = world_chunk(w, (int32_t)cx, (int32_t)cy, 1);
            if (!k || get_chunk(&a, k, &had, &had_street))
                goto done;
            any_owner |= had;
            any_street |= had_street;
            q = *e == ',' ? e + 1 : e;
        }
    }
    else
    {
        int cx, cy, had, had_street;
        for (cy = 0; cy < MAP_H / TILE_CHUNK; cy++)
            for (cx = 0; cx < MAP_W / TILE_CHUNK; cx++)
            {
                TileChunk *k = world_chunk(w, cx, cy, 1);
                if (!k || get_chunk(&a, k, &had, &had_street))
                    goto done;
                any_street |= had_street;
            }
    }

    /*  The cities, in the order the list gives them, each at its origin. */
    q = strstr(js, "\"cities\"");
    {
        const char *end = q ? strchr(q, ']') : NULL;
        while (q && (q = strstr(q, "\"origin\"")) && q < end)
        {
            long        x0 = json_int(q, "x", 0), y0 = json_int(q, "y", 0);
            const char *wn = strstr(q, "\"window\"");
            const char *nx = strstr(q + 1, "\"origin\"");
            /*  The window's extent, where this city's entry gives one: a
             *  world written before windows holds 128 by 128 cities. */
            long  ww = wn && (!nx || wn < nx) ? json_int(wn, "w", MAP_W) : MAP_W;
            long  wh = wn && (!nx || wn < nx) ? json_int(wn, "h", MAP_H) : MAP_H;
            City *c  = (City *)calloc(1, sizeof *c);
            if (!c || read_city(&a, c, id, (int32_t)ww, (int32_t)wh) || world_place_city(w, c, (int32_t)x0, (int32_t)y0) < 0)
            {
                if (c)
                    city_free(c);
                free(c);
                goto done;
            }
            id++;
            q++;
        }
    }
    if (w->n_city == 0)
        goto done;
    /*  A world written before owners: each city owns what it zoned or
     *  built inside its window. */
    if (!any_owner)
    {
        int32_t ci, y, x;
        for (ci = 0; ci < w->n_city; ci++)
            for (y = 0; y < w->city[ci].city->h; y++)
                for (x = 0; x < w->city[ci].city->w; x++)
                {
                    int32_t    i;
                    TileChunk *k = world_at(w, w->city[ci].city->ox + x, w->city[ci].city->oy + y, 0, &i);
                    if (k && !k->owner[i] && world_claims(k->xzon[i], k->xbld[i]))
                        k->owner[i] = (uint8_t)(ci + 1);
                }
    }
    /*  A city whose misc.bin still holds a map turn was written with its
     *  tiles in the terms of that turn.  Its own tiles are decoded as
     *  city_load decodes a 1995 file, and the turn goes to 0.  A tile
     *  nobody owns is the city's when the world holds one city. */
    {
        int32_t ci, y, x;
        for (ci = 0; ci < w->n_city; ci++)
        {
            City *cc   = w->city[ci].city;
            int   turn = (int)(cc->misc[MISC_ROTATION] & 3);
            if (!turn)
                continue;
            for (y = 0; y < cc->h; y++)
                for (x = 0; x < cc->w; x++)
                {
                    int32_t    i;
                    TileChunk *k = world_at(w, cc->ox + x, cc->oy + y, 0, &i);
                    if (k && (k->owner[i] == (uint8_t)(ci + 1) || (!k->owner[i] && w->n_city == 1)))
                        city_cell_unturn(&k->xzon[i], &k->xbit[i], k->xbld[i], turn);
                }
            cc->misc[MISC_ROTATION] = 0;
        }
    }
    w->map.has_streets = any_street;
    rc                 = 0;
done:
    if (rc)
        world_free(w);
    free(js);
    free(a.raw);
    return rc;
}

int arco_load(const char *path, City *c)
{
    World    w;
    City    *one;
    TileMap *m = (TileMap *)malloc(sizeof *m);
    if (!m)
        return -1;
    if (arco_load_world(path, &w))
    {
        free(m);
        return -1;
    }
    /*  The first city, and the world's store as its own: the city keeps
     *  its tiles, and the world is freed without them. */
    one = w.city[0].city;
    memcpy(c, one, sizeof *c);
    free(one);
    w.city[0].city = NULL;
    *m             = w.map;
    memset(&w.map, 0, sizeof w.map);
    c->map     = m;
    c->own_map = 1;
    city_bind(c);
    world_free(&w);
    return 0;
}

int arco_is_arco(const char *path)
{
    uint8_t h[4];
    FILE   *f = fopen(path, "rb");
    size_t  n;
    if (!f)
        return 0;
    n = fread(h, 1, 4, f);
    fclose(f);
    return n == 4 && h[0] == 'P' && h[1] == 'K' && h[2] == 3 && h[3] == 4;
}
