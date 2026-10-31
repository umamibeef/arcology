/*  city.c: the SimCity 2000 city file (.SC2) reader.  The file is IFF:
 *  "FORM" <len> "SCDH", then a sequence of chunks, each a 4-byte tag, a
 *  big-endian length, and that many bytes.  There is no pad byte between
 *  chunks: the game writes them back to back.  Every chunk except CNAM
 *  and ALTM is run-length encoded.  The decoder below reads what the
 *  encoder at $293EC writes.  A .sc2 is loaded and never written: the
 *  world is saved as .arco (arco.c). */
#include "arco.h"
#include "sim.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------------- big-endian helpers ------------------------------ */
static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
static uint16_t rd16(const uint8_t *p) { return (uint16_t)((p[0] << 8) | p[1]); }

/* ------------------------------------------------------------------ *
 *  Decoder.  A count byte below 128 introduces that many literal
 *  bytes.  128 or above means the next byte repeats (count - 127) times.
 * ------------------------------------------------------------------ */
static size_t sc2_rle_decode(const uint8_t *in, size_t in_len, uint8_t *out, size_t out_cap)
{
    size_t i = 0, o = 0;
    while (i < in_len)
    {
        uint8_t c = in[i++];
        if (c < 128)
        { /* literal run */
            if (i + c > in_len || o + c > out_cap)
                break;
            memcpy(out + o, in + i, c);
            o += c;
            i += c;
        }
        else
        { /* repeat run */
            size_t n = (size_t)c - 127;
            if (i >= in_len || o + n > out_cap)
                break;
            memset(out + o, in[i++], n);
            o += n;
        }
    }
    return o;
}

/* ------------------------------------------------------------------ *
 *  Chunk plumbing
 * ------------------------------------------------------------------ */
static int is_uncompressed(const char *tag)
{
    return !memcmp(tag, "CNAM", 4) || !memcmp(tag, "ALTM", 4);
}

static void keep_raw(uint8_t **dst, size_t *dlen, const uint8_t *p, size_t n)
{
    free(*dst);
    *dst = (uint8_t *)malloc(n ? n : 1);
    memcpy(*dst, p, n);
    *dlen = n;
}

const char *city_layer_tag(int layer)
{
    static const char *const TAG[CL_N] = {"ALTM", "XBLD", "XZON", "XTER", "XUND", "XTXT", "XBIT", "XTRF", "XPLT", "XVAL", "XCRM", "XPLC", "XFIR", "XPOP", "XROG"};
    return layer >= 0 && layer < CL_N ? TAG[layer] : "?";
}

/* scatter a flat expanded chunk into the right city member */
static void store_chunk(City *c, const char *tag, const uint8_t *d, size_t n)
{
    size_t r, x;
    if (!memcmp(tag, "MISC", 4))
    {
        for (r = 0; r < MISC_LONGS && (r + 1) * 4 <= n; r++)
            c->misc[r] = (int32_t)rd32(d + r * 4);
    }
    else if (!memcmp(tag, "ALTM", 4))
    {
        for (r = 0; r < MAP_H; r++)
            for (x = 0; x < MAP_W; x++)
                T_ALTM(c, (int32_t)r, (int32_t)x) = rd16(d + (r * MAP_W + x) * 2);
    }
    else
    {
        /*  A tile layer: as many of its bytes as the chunk carries, over
         *  what the window holds. */
        static uint8_t buf[MAP_W * MAP_H * 2];
        int            l;
        for (l = CL_XBLD; l < CL_N; l++)
            if (!memcmp(tag, city_layer_tag(l), 4))
            {
                size_t m = city_layer_bytes(c, l);
                city_layer_get(c, l, buf);
                memcpy(buf, d, n < m ? n : m);
                city_layer_set(c, l, buf);
                return;
            }
    }
    if (!memcmp(tag, "XLAB", 4))
        keep_raw(&c->xlab, &c->xlab_len, d, n);
    else if (!memcmp(tag, "XMIC", 4))
        keep_raw(&c->xmic, &c->xmic_len, d, n);
    else if (!memcmp(tag, "XTHG", 4))
        keep_raw(&c->xthg, &c->xthg_len, d, n);
    else if (!memcmp(tag, "XGRP", 4))
    {
        /*  Sixteen series of 52 big-endian longs, in series order: the
         *  block $2D52E allocates, written out whole. */
        size_t i, k;
        keep_raw(&c->xgrp, &c->xgrp_len, d, n);
        for (i = 0; i < N_GRAPH; i++)
            for (k = 0; k < GRAPH_SAMPLES; k++)
            {
                size_t o = (i * GRAPH_SAMPLES + k) * 4;
                if (o + 4 <= n)
                    c->graph[i][k] = (int32_t)rd32(d + o);
            }
    }
    else if (!memcmp(tag, "CNAM", 4))
        keep_raw(&c->cnam, &c->cnam_len, d, n);
}

/* pull the named scalars out of raw MISC.  The MISC builder at $2A186
 * emits indices 2..13 from straight-line code, so they are unambiguous.
 * Anything past 26 sits behind counted loops, and this does not decode
 * it. */
void city_misc_to_scalars(City *c)
{
    int i;

    c->year_founded   = (int16_t)c->misc[3];
    c->date           = c->misc[4];
    c->funds          = c->misc[5];
    c->bonds          = c->misc[6];
    c->difficulty     = (int16_t)c->misc[7];
    c->land_value_tot = c->misc[10];
    c->crime_tot      = c->misc[11];
    c->traffic_tot    = c->misc[12];
    c->pollution_tot  = c->misc[13];
    c->population     = c->misc[MISC_POPULATION];
    c->ordinances     = c->misc[MISC_ORDINANCES];
    /*  These four are saved, and the MISC indices come from
     *  out/miscload.json: the game's own unpacker at $295D6 with every
     *  read tagged.  Leaving them out started a loaded city with less
     *  state than the original has, which the clock comparison shows on
     *  the very first tick. */
    c->unemployment  = c->misc[1001];          /* A5+0x2C82 */
    c->power_pct     = c->misc[1048];          /* A5+0x1E86 */
    c->water_pct     = c->misc[1049];          /* A5+0x1E8A */
    c->developed     = (int16_t)c->misc[1067]; /* A5+0x11D0 */
    c->disasters_off = (int16_t)c->misc[1024]; /* A5+0x13AA */
    c->disaster_kind = (int16_t)c->misc[28];   /* A5+0x13A0 */
    c->centre_y      = (int16_t)c->misc[1030]; /* A5+0x867E */
    c->centre_x      = (int16_t)c->misc[1031]; /* A5+0x8680 */
    /*  A5+0x1EFA: sixteen counters, one per id $DD..$EC, saved as
     *  MISC[1002..1017].  The growth scan only ever adjusts them by one
     *  as buildings come and go.  So a loaded city that starts them at
     *  zero has the military and airport ladders reading nothing and
     *  asking for the wrong building. */
    {
        int k;
        for (k = 0; k < 16; k++)
            c->infra[k] = (int16_t)c->misc[1002 + k];
    }
    c->weather1      = (int16_t)c->misc[MISC_WEATHER1];
    c->weather2      = (int16_t)c->misc[MISC_WEATHER2];
    c->temperature   = (int16_t)c->misc[24]; /* A5+0x1F00 */
    c->weather_state = (int16_t)c->misc[27]; /* A5+0x1F03 */
    c->water_level   = (int16_t)c->misc[MISC_2C86];
    c->raise_cost    = 25;               /* A5+0x61E, a constant in the DATA image */
    c->city_mode     = 1;                /* A5-0x7DE6, a saved city is never in the
                                          * terrain editor */
    c->worst_problem  = (int16_t)0xFFFF; /* A5-0x1254 starts empty */
    c->police_term    = (int16_t)c->misc[MISC_POLICE_TERM];
    c->transit_term   = (int16_t)c->misc[MISC_1EFE];
    c->year_end       = (uint8_t)c->misc[MISC_YEAR_END];
    c->transit_bus    = c->misc[MISC_TRANSIT_BUS];
    c->transit_rail   = c->misc[MISC_TRANSIT_RAIL];
    c->transit_subway = c->misc[MISC_TRANSIT_SUB];
    for (i = 0; i < 3; i++)
        c->rci_demand[i] = (int16_t)c->misc[MISC_RCI_DEMAND + i];

    /*  The month is not stored: $1523E derives it from the date, which
     *  runs at 25 days to the month and 300 to the year. */
    c->month = (int16_t)((c->date / 25) % 12);
    c->years = c->date / 300; /* $15268 */
    for (i = 0; i < 11; i++)
        c->industry_level[i] = (int16_t)c->misc[MISC_IND_LEVEL + 3 * i];
    if (c->years > 0x3E80)
        c->years = 10000;

    /*  Two blocks rather than scalars.  Both were located by running the
     *  game's own MISC unpacker at $295D6 under the interpreter with
     *  every read tagged by its MISC index (tools/miscload.py).  The
     *  layout below is what that produced, not a guess at a stride.  The
     *  census is unsigned: a big map can hold more than 32767 trees, and
     *  several shipped cities do, so the game zero-extends every read. */
    for (i = 0; i < 256; i++)
        c->census[i] = (uint16_t)c->misc[MISC_CENSUS + i];

    /*  Population per zone kind, MISC[380..387]: the city scan rebuilds
     *  it.  But the economy reads accum8[7] (the industrial total)
     *  before any scan has run, in the pollution-per-head term at
     *  $35FBA.  Loading it is what takes Flint and Oakland exact. */
    for (i = 0; i < 8; i++)
        c->accum8[i] = c->misc[MISC_ACCUM8 + i];

    for (i = 0; i < N_DEPT; i++)
    {
        const int32_t *d = &c->misc[MISC_BUDGET + i * 27];
        int            m;
        c->dept[i].amount  = d[0];
        c->dept[i].funding = d[1];
        c->dept[i].accrued = d[2];
        for (m = 0; m < 12; m++)
        {
            c->dept[i].history_amount[m]  = d[3 + m * 2];
            c->dept[i].history_funding[m] = d[4 + m * 2];
        }
    }
}

/*  And back the other way, into a copy of MISC: the named scalars the
 *  simulation changes, so a saved world carries what it changed. */
void city_scalars_to_misc(const City *c, int32_t *misc)
{
    int i;

    for (i = 0; i < 256; i++)
        misc[MISC_CENSUS + i] = c->census[i];

    for (i = 0; i < N_DEPT; i++)
    {
        int32_t *d = &misc[MISC_BUDGET + i * 27];
        int      m;
        d[0] = c->dept[i].amount;
        d[1] = c->dept[i].funding;
        d[2] = c->dept[i].accrued;
        for (m = 0; m < 12; m++)
        {
            d[3 + m * 2] = c->dept[i].history_amount[m];
            d[4 + m * 2] = c->dept[i].history_funding[m];
        }
    }
    misc[MISC_YEAR_END]     = c->year_end;
    misc[MISC_TRANSIT_BUS]  = c->transit_bus;
    misc[MISC_TRANSIT_RAIL] = c->transit_rail;
    misc[MISC_TRANSIT_SUB]  = c->transit_subway;
    for (i = 0; i < 3; i++)
        misc[MISC_RCI_DEMAND + i] = c->rci_demand[i];
}

void city_cell_unturn(uint8_t *xzon, uint8_t *xbit, uint8_t xbld, int turn)
{
    /*  $3AECA turns the map and moves XZON as a plain byte.  So at turn
     *  r, corner i of a footprint carries ROT_CORNER_4[r * 4 + i] (top
     *  left, bottom left, bottom right, top right).  Each bit goes to the
     *  bit that corner carries at turn 0. */
    const int16_t *at = &ROT_CORNER_4[(turn & 3) * 4];
    uint8_t        hi = 0;
    int            i;
    for (i = 0; i < 4; i++)
        if (*xzon & at[i])
            hi = (uint8_t)(hi | ROT_CORNER_4[i]);
    *xzon = (uint8_t)((*xzon & 0x0F) | hi);
    /*  The turn also moves XBIT as a plain byte for ids 0x70 and up.  The
     *  original reads their mirror bit against the parity of the turn:
     *  $178C8 inverts it to draw, and $3391C, $33C64 and $322CC to lay a
     *  runway or a pier and to launch a plane. */
    if ((turn & 1) && xbld >= 0x70)
        *xbit = (uint8_t)(*xbit ^ 0x02);
}

int city_load_sc2(const char *path, City *c)
{
    {
        FILE    *f = fopen(path, "rb");
        uint8_t *buf, *exp;
        long     len;
        size_t   o;

        if (!f)
            return 0;
        fseek(f, 0, SEEK_END);
        len = ftell(f);
        fseek(f, 0, SEEK_SET);
        buf = (uint8_t *)malloc((size_t)len);
        if (fread(buf, 1, (size_t)len, f) != (size_t)len)
        {
            fclose(f);
            free(buf);
            return 0;
        }
        fclose(f);

        if (len < 12 || memcmp(buf, "FORM", 4) || memcmp(buf + 8, "SCDH", 4))
        {
            free(buf);
            return 0;
        }

        memset(c, 0, sizeof *c);
        if (city_extent(c, MAP_W, MAP_H) || city_own_map(c))
        {
            free(buf);
            return 0;
        }

        /*  The dice come up at one, like the graph scales below and for
         *  the same reason.  The seeds are not in the save file, and a
         *  shift register at nought never leaves it. */
        rng_seed(c, 1, 1);

        /*  $2D594: the graph scales come up as one, not zero.  They are
         *  not in the save file, so a loaded city starts here and the
         *  running maximum climbs from the samples. */
        {
            int g;
            for (g = 0; g < N_GRAPH; g++)
                c->graph_max[g] = 1;
        }

        exp = (uint8_t *)malloc(1 << 20);

        o = 12;
        while (o + 8 <= (size_t)len && c->n_chunks < 24)
        {
            char   tag[5];
            size_t n, m;
            memcpy(tag, buf + o, 4);
            tag[4] = 0;
            n      = rd32(buf + o + 4);
            if (o + 8 + n > (size_t)len)
                break;

            if (is_uncompressed(tag))
            {
                memcpy(exp, buf + o + 8, n);
                m = n;
            }
            else
            {
                m = sc2_rle_decode(buf + o + 8, n, exp, 1 << 20);
            }

            store_chunk(c, tag, exp, m);
            memcpy(c->order[c->n_chunks++], tag, 5);
            o += 8 + n;
        }

        free(exp);
        free(buf);
        /*  MISC_ROTATION is the map turn the file was saved at.  The
         *  city is decoded into the grid's own terms once, here, and
         *  nothing reads the turn again. */
        {
            int     turn = (int)(c->misc[MISC_ROTATION] & 3);
            int32_t y, x;
            if (turn)
                for (y = 0; y < c->h; y++)
                    for (x = 0; x < c->w; x++)
                        city_cell_unturn(&T_XZON(c, y, x), &T_XBIT(c, y, x), T_XBLD(c, y, x), turn);
            c->misc[MISC_ROTATION] = 0;
        }
        city_misc_to_scalars(c);
        return 1;
    }
}

void city_free(City *c)
{
    rng_log_free(c);
    if (c->own_map && c->map)
    {
        tilemap_free(c->map);
        free(c->map);
    }
    c->map     = NULL;
    c->own_map = 0;
    free(c->pad);
    free(c->limits);
    free(c->developed_cells);
    free(c->scan_mask);
    free(c->scan_acc);
    free(c->q);
    c->pad = NULL, c->limits = c->developed_cells = c->scan_mask = NULL;
    c->scan_acc = NULL;
    c->q        = NULL;
    free(c->xlab);
    free(c->xmic);
    free(c->xthg);
    free(c->xgrp);
    free(c->cnam);
    c->xlab = c->xmic = c->xthg = c->xgrp = c->cnam = NULL;
}

/* ---- the city's window, a layer at a time ------------------------- */
static const struct
{
    size_t off;
    int    shift; /* 0 full resolution, 1 half, 2 quarter */
    int    wide;  /* two bytes a cell */
} LAYER[CL_N] = {
    {offsetof(TileChunk, altm), 0, 1},
    {offsetof(TileChunk, xbld), 0, 0},
    {offsetof(TileChunk, xzon), 0, 0},
    {offsetof(TileChunk, xter), 0, 0},
    {offsetof(TileChunk, xund), 0, 0},
    {offsetof(TileChunk, xtxt), 0, 0},
    {offsetof(TileChunk, xbit), 0, 0},
    {offsetof(TileChunk, xtrf), 1, 0},
    {offsetof(TileChunk, xplt), 1, 0},
    {offsetof(TileChunk, xval), 1, 0},
    {offsetof(TileChunk, xcrm), 1, 0},
    {offsetof(TileChunk, xplc), 2, 0},
    {offsetof(TileChunk, xfir), 2, 0},
    {offsetof(TileChunk, xpop), 2, 0},
    {offsetof(TileChunk, xrog), 2, 0},
};

size_t city_layer_bytes(const City *c, int layer)
{
    size_t w = (size_t)(c->w >> LAYER[layer].shift), h = (size_t)(c->h >> LAYER[layer].shift);
    return w * h * (LAYER[layer].wide ? 2u : 1u);
}

/*  One row of a layer's window at a time: a row lies along its chunks,
 *  so each chunk's stretch of it is one copy. */
static void layer_rows(const City *c, int layer, uint8_t *buf, int out)
{
    const int s = LAYER[layer].shift, side = c->w >> s, rows = c->h >> s, csz = TILE_CHUNK >> s;
    const int el = LAYER[layer].wide ? 2 : 1;
    int       y, x;
    for (y = 0; y < rows; y++)
        for (x = 0; x < side;)
        {
            int32_t    X = x, Y = y; /* the window's own cell: it starts on a chunk */
            TileChunk *k  = city_chunk(c, X << s, Y << s);
            int        in = X & (csz - 1), run = csz - in;
            uint8_t   *p = (uint8_t *)k + LAYER[layer].off + (size_t)(((Y & (csz - 1)) * csz + in) * el);
            uint8_t   *q = buf + (size_t)((y * side + x) * el);
            if (run > side - x)
                run = side - x;
            if (out)
                memcpy(q, p, (size_t)(run * el));
            else
                memcpy(p, q, (size_t)(run * el));
            x += run;
        }
}

void city_layer_get(const City *c, int layer, void *out)
{
    layer_rows(c, layer, (uint8_t *)out, 1);
}

void city_layer_set(City *c, int layer, const void *in)
{
    layer_rows(c, layer, (uint8_t *)(uintptr_t)in, 0);
}

int city_own_map(City *c)
{
    TileMap *m = (TileMap *)calloc(1, sizeof *m);
    if (!m || tilemap_init(m) || tilemap_reserve(m, c->ox, c->oy, c->ox + c->w, c->oy + c->h) ||
        tilemap_cover(m, c->ox - CITY_MARGIN, c->oy - CITY_MARGIN, c->ox + c->w + CITY_MARGIN, c->oy + c->h + CITY_MARGIN))
    {
        if (m)
            tilemap_free(m);
        free(m);
        return -1;
    }
    c->map     = m;
    c->own_map = 1;
    city_bind(c);
    return 0;
}

void city_bind(City *c)
{
    const TileMap *m = c->map;
    c->cw            = m->w;
    c->cdir          = m->dir + ((c->oy >> TILE_SHIFT) - m->cy0) * m->w + ((c->ox >> TILE_SHIFT) - m->cx0);
}

int city_copy(City *dst, const City *src)
{
    TileMap         *keep = dst->own_map ? dst->map : NULL;
    int16_t         *pad = dst->pad, *acc = dst->scan_acc;
    struct CityRing *q   = dst->q;
    uint8_t         *lim = dst->limits, *dev = dst->developed_cells, *msk = dst->scan_mask;
    int              same = dst->w == src->w && dst->h == src->h && pad;
    *dst                  = *src;
    dst->map              = keep;
    dst->own_map          = keep != NULL;
    /*  The working planes are the copy's own. */
    dst->pad = pad, dst->limits = lim, dst->developed_cells = dev;
    dst->scan_mask = msk, dst->scan_acc = acc, dst->q = q;
    if (!same)
    {
        free(pad), free(lim), free(dev), free(msk), free(acc), free(q);
        dst->pad = NULL, dst->limits = dst->developed_cells = dst->scan_mask = NULL;
        dst->scan_acc = NULL;
        dst->q        = NULL;
        if (city_extent(dst, src->w, src->h))
            return -1;
    }
    memcpy(dst->pad, src->pad, sizeof *src->pad * (size_t)src->w * (size_t)src->h);
    memcpy(dst->limits, src->limits, (size_t)src->w * (size_t)src->h);
    memcpy(dst->developed_cells, src->developed_cells, (size_t)(src->w / 2) * (size_t)(src->h / 2));
    memcpy(dst->q, src->q, sizeof *src->q * (size_t)(src->qmask + 1));
    if (!keep && city_own_map(dst))
        return -1;
    city_bind(dst);
    {
        uint8_t *buf = (uint8_t *)malloc((size_t)src->w * (size_t)src->h * 2u);
        int      l;
        if (!buf)
            return -1;
        for (l = 0; l < CL_N; l++)
        {
            city_layer_get(src, l, buf);
            city_layer_set(dst, l, buf);
        }
        free(buf);
    }
    return 0;
}

int city_grow(City *c, int32_t w, int32_t h)
{
    int16_t *pad = c->pad;
    uint8_t *lim = c->limits;
    int32_t  ow = c->w, oh = c->h, y;
    if (w < ow || h < oh || (w & TILE_MASK) || (h & TILE_MASK))
        return -1;
    if (tilemap_reserve(c->map, c->ox, c->oy, c->ox + w, c->oy + h) ||
        tilemap_cover(c->map, c->ox - CITY_MARGIN, c->oy - CITY_MARGIN, c->ox + w + CITY_MARGIN, c->oy + h + CITY_MARGIN))
        return -1;
    /*  The planes made again at the new size, with the old rows kept. */
    c->pad    = NULL;
    c->limits = NULL;
    {
        int16_t *keep_pad = pad;
        uint8_t *keep_lim = lim;
        c->pad            = NULL;
        if (city_extent(c, w, h))
            return -1;
        for (y = 0; y < oh; y++)
        {
            memcpy(c->pad + (size_t)y * (size_t)w, keep_pad + (size_t)y * (size_t)ow, sizeof *keep_pad * (size_t)ow);
            memcpy(c->limits + (size_t)y * (size_t)w, keep_lim + (size_t)y * (size_t)ow, (size_t)ow);
        }
        free(keep_pad);
        free(keep_lim);
    }
    city_bind(c);
    return 0;
}

int city_extent(City *c, int32_t w, int32_t h)
{
    free(c->pad);
    free(c->limits);
    free(c->developed_cells);
    free(c->scan_mask);
    free(c->scan_acc);
    free(c->q);
    {
        size_t n = 512;
        if (!(w == MAP_W && h == MAP_H))
            while (n < (size_t)w * (size_t)h)
                n *= 2;
        c->q     = (struct CityRing *)calloc(n, sizeof *c->q);
        c->qmask = (int)n - 1;
    }
    c->scan_mask       = (uint8_t *)calloc((size_t)(w / 2) * (size_t)(h / 2), 1);
    c->scan_acc        = (int16_t *)calloc((size_t)(w / 4) * (size_t)(h / 4), sizeof *c->scan_acc);
    c->w               = w;
    c->h               = h;
    c->flat            = w == MAP_W && h == MAP_H;
    c->pad             = (int16_t *)calloc((size_t)w * (size_t)h, sizeof *c->pad);
    c->limits          = (uint8_t *)calloc((size_t)w * (size_t)h, 1);
    c->developed_cells = (uint8_t *)calloc((size_t)(w / 2) * (size_t)(h / 2), 1);
    return c->pad && c->limits && c->developed_cells && c->scan_mask && c->scan_acc && c->q ? 0 : -1;
}
