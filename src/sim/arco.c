/* ==================================================================== *
 *  .arco -- the reader and writer.  See arco.h for what the format is
 *  and why it is a ZIP.
 *
 *  The ZIP here is deliberately the smallest thing that a real unzip
 *  will open: local headers, a central directory, an end record, and
 *  deflate from lodepng, which the renderer already carries for PNG.
 *  No zip64, no encryption, no directory entries.  A file this reader
 *  writes opens in Finder, Explorer, `unzip`, and Python's `zipfile`.
 *
 *  What is NOT here, on purpose: a schema.  The manifest is written by
 *  hand and read by a parser that looks for the keys it wants and
 *  ignores the rest.  That is what makes an older reader survive a
 *  newer file, and it is the same bargain PNG makes with its ancillary
 *  chunks.
 * ==================================================================== */
#include "arco.h"
#include "lodepng.h"

#include <stdarg.h>
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
    if (want <= b->cap)
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
    /*  Measure the central directory BEFORE the end record starts, or
     *  the twelve bytes written just below land inside the size and
     *  every reader computes a negative prefix. */
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
/*  Every grid the world holds, with its element size and resolution
 *  divisor.  Adding a layer is a line here and a file in the archive;
 *  a reader that does not know the name simply does not ask for it. */
typedef struct
{
    const char *name;
    void       *base;
    size_t      elem; /* 1 or 2 bytes */
    int         div;  /* 1 full res, 2 half, 4 quarter */
} Layer;

static void layers_of(City *c, Layer *L, int *n)
{
    int i = 0;
#define ADD(nm, arr, sz, dv)       \
    do                             \
    {                              \
        L[i].name = nm;            \
        L[i].base = (void *)(arr); \
        L[i].elem = (sz);          \
        L[i].div  = (dv);          \
        i++;                       \
    } while (0)
    ADD("altm", c->altm, 2, 1);
    ADD("xbld", c->xbld, 1, 1);
    ADD("xzon", c->xzon, 1, 1);
    ADD("xter", c->xter, 1, 1);
    ADD("xund", c->xund, 1, 1);
    ADD("xbit", c->xbit, 1, 1);
    ADD("xtxt", c->xtxt, 1, 1);
    ADD("xtrf", c->xtrf, 1, 2);
    ADD("xplt", c->xplt, 1, 2);
    ADD("xval", c->xval, 1, 2);
    ADD("xcrm", c->xcrm, 1, 2);
    ADD("xplc", c->xplc, 1, 4);
    ADD("xfir", c->xfir, 1, 4);
    ADD("xpop", c->xpop, 1, 4);
    ADD("xrog", c->xrog, 1, 4);
#undef ADD
    *n = i;
}

/*  A layer, chunk by chunk, little-endian.
 *
 *  Little-endian is the one place this format breaks with the original
 *  on purpose: the 1995 file is big-endian because a 68000 was, and
 *  every machine that will ever open a .arco is not. */
static int put_layer(Zip *z, int cx, int cy, const Layer *l)
{
    int      side = ARCO_CHUNK / l->div;
    int      w    = MAP_W / l->div;
    size_t   n    = (size_t)side * (size_t)side * l->elem;
    uint8_t *tile = (uint8_t *)malloc(n);
    char     path[160];
    int      y, x, rc;

    if (!tile)
        return -1;
    for (y = 0; y < side; y++)
        for (x = 0; x < side; x++)
        {
            size_t src = (size_t)(cy * side + y) * (size_t)w +
                         (size_t)(cx * side + x);
            size_t dst = ((size_t)y * (size_t)side + (size_t)x) * l->elem;
            if (l->elem == 2)
            {
                uint16_t v    = ((const uint16_t *)l->base)[src];
                tile[dst]     = (uint8_t)(v & 0xFF);
                tile[dst + 1] = (uint8_t)(v >> 8);
            }
            else
                tile[dst] = ((const uint8_t *)l->base)[src];
        }
    snprintf(path, sizeof path, "chunks/%d_%d/%s.bin", cx, cy, l->name);
    rc = zip_add(z, path, tile, n);
    free(tile);
    return rc;
}

static int get_layer(const Ar *a, int cx, int cy, const Layer *l)
{
    int      side = ARCO_CHUNK / l->div;
    int      w    = MAP_W / l->div;
    size_t   want = (size_t)side * (size_t)side * l->elem, got = 0;
    char     path[160];
    uint8_t *tile;
    int      y, x;

    snprintf(path, sizeof path, "chunks/%d_%d/%s.bin", cx, cy, l->name);
    tile = ar_get(a, path, &got);
    if (!tile)
        return 0; /* a layer the file does not carry stays as it was */
    if (got != want)
    {
        free(tile);
        return -1;
    }
    for (y = 0; y < side; y++)
        for (x = 0; x < side; x++)
        {
            size_t dst = (size_t)(cy * side + y) * (size_t)w +
                         (size_t)(cx * side + x);
            size_t src = ((size_t)y * (size_t)side + (size_t)x) * l->elem;
            if (l->elem == 2)
                ((uint16_t *)l->base)[dst] =
                    (uint16_t)(tile[src] | ((uint16_t)tile[src + 1] << 8));
            else
                ((uint8_t *)l->base)[dst] = tile[src];
        }
    free(tile);
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

/*  The city's name.  It is not a field on City -- it is the CNAM chunk,
 *  a Pascal string in a 32-byte slot -- so it is decoded here rather
 *  than assumed. */
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

static int write_manifest(Zip *z, const City *c)
{
    Buf  b = {0};
    char name[128];
    int  chunks = MAP_W / ARCO_CHUNK;
    int  rc;

    {
        char raw[64];
        city_name(c, raw, sizeof raw);
        json_escape(name, sizeof name, raw[0] ? raw : "Untitled");
    }
    buf_fmt(&b, "{\n");
    buf_fmt(&b, "  \"format\": \"arcology-world\",\n");
    buf_fmt(&b, "  \"version\": %d,\n", ARCO_VERSION);
    buf_fmt(&b, "\n");
    buf_fmt(&b, "  \"_\": \"Grids live in chunks/<cx>_<cy>/<layer>.bin, "
                "little-endian, row-major, chunk_size square. A layer file "
                "that is absent is simply not carried.\",\n\n");
    buf_fmt(&b, "  \"chunk_size\": %d,\n", ARCO_CHUNK);
    buf_fmt(&b, "  \"chunks\": { \"x\": %d, \"y\": %d },\n", chunks, chunks);
    buf_fmt(&b, "  \"tiles\":  { \"w\": %d, \"h\": %d },\n", MAP_W, MAP_H);
    buf_fmt(&b, "\n");
    /*  The list is one long today.  It is a LIST because the format
     *  exists to hold a region, and a region is many. */
    buf_fmt(&b, "  \"cities\": [\n");
    buf_fmt(&b, "    { \"id\": \"0\", \"name\": \"%s\",\n", name);
    buf_fmt(&b, "      \"origin\": { \"x\": 0, \"y\": 0 },\n");
    buf_fmt(&b, "      \"state\": \"cities/0.json\" }\n");
    buf_fmt(&b, "  ],\n");
    buf_fmt(&b, "\n");
    buf_fmt(&b, "  \"origin\": {\n");
    buf_fmt(&b, "    \"_\": \"Where this world came from. A world imported "
                "from a 1995 save keeps its MISC block in cities/<id>/"
                "misc.bin, which is what makes the round trip exact while "
                "fields are still being named.\",\n");
    buf_fmt(&b, "    \"imported_from\": \"sc2\",\n");
    /*  The order the 1995 file listed its chunks in.  It has no meaning
     *  to this format -- .arco stores layers by name -- but writing a
     *  .sc2 back out in a different order would change the bytes, and a
     *  round trip that is only ALMOST exact is not worth having. */
    buf_fmt(&b, "    \"sc2_chunk_order\": \"");
    {
        int k;
        for (k = 0; k < c->n_chunks; k++)
            buf_fmt(&b, "%s%s", k ? "," : "", c->order[k]);
    }
    buf_fmt(&b, "\"\n");
    buf_fmt(&b, "  }\n");
    buf_fmt(&b, "}\n");
    rc = zip_add(z, "world.json", b.p, b.n);
    free(b.p);
    return rc;
}

static int write_city(Zip *z, const City *c)
{
    Buf  b = {0};
    char name[128];
    int  rc;

    {
        char raw[64];
        city_name(c, raw, sizeof raw);
        json_escape(name, sizeof name, raw[0] ? raw : "Untitled");
    }
    buf_fmt(&b, "{\n");
    buf_fmt(&b, "  \"name\": \"%s\",\n", name);
    buf_fmt(&b, "  \"founded\": %d,\n", (int)c->year_founded);
    buf_fmt(&b, "  \"date\": %d,\n", (int)c->date);
    buf_fmt(&b, "  \"rotation\": %d,\n", (int)c->rotation);
    buf_fmt(&b, "  \"difficulty\": %d,\n", (int)c->difficulty);
    buf_fmt(&b, "  \"funds\": %d,\n", (int)c->funds);
    buf_fmt(&b, "  \"bonds\": %d,\n", (int)c->bonds);
    buf_fmt(&b, "  \"population\": %d,\n", (int)c->population);
    buf_fmt(&b, "\n");
    buf_fmt(&b, "  \"_\": \"These are the fields the reconstruction has "
                "named. They are written so the file can be read, but "
                "misc.bin is what is loaded -- see world.json.\",\n\n");
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
    buf_fmt(&b, "  \"state\": {\n");
    buf_fmt(&b, "    \"misc\": \"cities/0/misc.bin\",\n");
    buf_fmt(&b, "    \"signs\": \"cities/0/xlab.bin\",\n");
    buf_fmt(&b, "    \"microsim\": \"cities/0/xmic.bin\",\n");
    buf_fmt(&b, "    \"things\": \"cities/0/xthg.bin\",\n");
    buf_fmt(&b, "    \"graphs\": \"cities/0/graphs.bin\"\n");
    buf_fmt(&b, "  }\n");
    buf_fmt(&b, "}\n");
    rc = zip_add(z, "cities/0.json", b.p, b.n);
    free(b.p);
    return rc;
}

/* ---- the public pair ----------------------------------------------- */
int arco_save(const char *path, const City *c)
{
    Zip     z = {{0}, NULL, 0, 0};
    Layer   L[24];
    int     n, i, cx, cy, chunks = MAP_W / ARCO_CHUNK;
    int32_t misc_le[MISC_LONGS];

    if (write_manifest(&z, c) || write_city(&z, c))
        goto fail;

    layers_of((City *)c, L, &n);
    for (cy = 0; cy < chunks; cy++)
        for (cx = 0; cx < chunks; cx++)
            for (i = 0; i < n; i++)
                if (put_layer(&z, cx, cy, &L[i]))
                    goto fail;

    /*  MISC little-endian, and the four chunks the reconstruction has
     *  not taken apart yet, verbatim.  Between them these are what make
     *  a converted save convert back byte for byte. */
    for (i = 0; i < MISC_LONGS; i++)
    {
        uint32_t v                  = (uint32_t)c->misc[i];
        ((uint8_t *)&misc_le[i])[0] = (uint8_t)(v & 0xFF);
        ((uint8_t *)&misc_le[i])[1] = (uint8_t)((v >> 8) & 0xFF);
        ((uint8_t *)&misc_le[i])[2] = (uint8_t)((v >> 16) & 0xFF);
        ((uint8_t *)&misc_le[i])[3] = (uint8_t)((v >> 24) & 0xFF);
    }
    if (zip_add(&z, "cities/0/misc.bin", misc_le, sizeof misc_le))
        goto fail;
    if (c->xlab && zip_add(&z, "cities/0/xlab.bin", c->xlab, c->xlab_len))
        goto fail;
    if (c->xmic && zip_add(&z, "cities/0/xmic.bin", c->xmic, c->xmic_len))
        goto fail;
    if (c->xthg && zip_add(&z, "cities/0/xthg.bin", c->xthg, c->xthg_len))
        goto fail;
    /*  The graph history is NOT passed through as the original's blob.
     *  XGRP is just graph[16][52] written big-endian, so .arco stores
     *  the numbers themselves, little-endian, under a name that says
     *  what they are.  city_save rebuilds the chunk from them. */
    {
        static int32_t g[N_GRAPH * GRAPH_SAMPLES];
        int            gi, gk;
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
        if (zip_add(&z, "cities/0/graphs.bin", g, sizeof g))
            goto fail;
    }
    if (c->cnam && zip_add(&z, "cities/0/cnam.bin", c->cnam, c->cnam_len))
        goto fail;

    if (zip_finish(&z, path))
        goto fail;
    zip_free(&z);
    return 0;
fail:
    zip_free(&z);
    return -1;
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

int arco_load(const char *path, City *c)
{
    Ar                       a  = {NULL, 0};
    FILE                    *f  = fopen(path, "rb");
    uint8_t                 *js = NULL, *mb = NULL;
    size_t                   jn = 0, mn = 0;
    Layer                    L[24];
    int                      n, i, cx, cy, chunks, rc = -1;
    long                     side;
    static const char *const RAW[] = {"xlab", "xmic", "xthg", "cnam", NULL};

    if (!f)
        return -1;
    fseek(f, 0, SEEK_END);
    a.len = (size_t)ftell(f);
    fseek(f, 0, SEEK_SET);
    a.raw = (uint8_t *)malloc(a.len ? a.len : 1);
    if (!a.raw || fread(a.raw, 1, a.len, f) != a.len)
    {
        fclose(f);
        free(a.raw);
        return -1;
    }
    fclose(f);

    js = ar_get(&a, "world.json", &jn);
    if (!js)
        goto done;
    /*  NUL-terminate before any strstr touches it. */
    {
        uint8_t *t = (uint8_t *)realloc(js, jn + 1);
        if (!t)
            goto done;
        js     = t;
        js[jn] = 0;
    }
    side = json_int((const char *)js, "chunk_size", ARCO_CHUNK);
    if (side != ARCO_CHUNK)
        goto done; /* a world this build cannot lay out */
    chunks = MAP_W / ARCO_CHUNK;

    memset(c, 0, sizeof *c);
    layers_of(c, L, &n);
    for (cy = 0; cy < chunks; cy++)
        for (cx = 0; cx < chunks; cx++)
            for (i = 0; i < n; i++)
                if (get_layer(&a, cx, cy, &L[i]))
                    goto done;

    mb = ar_get(&a, "cities/0/misc.bin", &mn);
    if (!mb || mn != sizeof c->misc)
        goto done;
    for (i = 0; i < MISC_LONGS; i++)
        c->misc[i] = (int32_t)((uint32_t)mb[i * 4] |
                               ((uint32_t)mb[i * 4 + 1] << 8) |
                               ((uint32_t)mb[i * 4 + 2] << 16) |
                               ((uint32_t)mb[i * 4 + 3] << 24));

    for (i = 0; RAW[i]; i++)
    {
        char      p[64];
        size_t   *plen;
        uint8_t **slot = slot_for(c, RAW[i], &plen);
        size_t    got  = 0;
        uint8_t  *d;
        snprintf(p, sizeof p, "cities/0/%s.bin", RAW[i]);
        d = ar_get(&a, p, &got);
        if (d && slot)
        {
            *slot = d;
            *plen = got;
        }
        else
            free(d);
    }

    /*  The scalars come from MISC, exactly as they do for a 1995 save,
     *  so there is one path that turns MISC into a City and not two
     *  that can disagree. */
    /*  The graph history, back into graph[][].  city_save writes XGRP
     *  from there, so nothing has to keep the original blob. */
    {
        size_t   gn = 0;
        uint8_t *g  = ar_get(&a, "cities/0/graphs.bin", &gn);
        if (g && gn == (size_t)N_GRAPH * GRAPH_SAMPLES * 4)
        {
            int gi, gk;
            for (gi = 0; gi < N_GRAPH; gi++)
                for (gk = 0; gk < GRAPH_SAMPLES; gk++)
                {
                    const uint8_t *o = g + (gi * GRAPH_SAMPLES + gk) * 4;
                    c->graph[gi][gk] =
                        (int32_t)((uint32_t)o[0] | ((uint32_t)o[1] << 8) |
                                  ((uint32_t)o[2] << 16) |
                                  ((uint32_t)o[3] << 24));
                }
        }
        free(g);
    }

    /*  Restore the chunk order, so converting back to .sc2 reproduces
     *  the original file and not merely an equivalent one. */
    {
        const char *q = strstr((const char *)js, "\"sc2_chunk_order\"");
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
    }

    city_misc_to_scalars(c);
    rc = 0;
done:
    free(js);
    free(mb);
    free(a.raw);
    return rc;
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
