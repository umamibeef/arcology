/*  city.c -- SimCity 2000 city file (.SC2) reader and writer.
 *
 *  The file is IFF: "FORM" <len> "SCDH", then a sequence of chunks, each
 *  a 4-byte tag, a big-endian length, and that many bytes.  There is no
 *  pad byte between chunks -- the game writes them back to back.
 *
 *  Every chunk except CNAM and ALTM is run-length encoded.  The codec
 *  below is a direct port of the encoder at $293EC and the decoder it
 *  implies; see the comments on sc2_rle_encode for the exact rules,
 *  which matter because we want to reproduce Maxis's byte stream and
 *  not merely an equivalent one.
 */
#include "arco.h"
#include "sc2k.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------------- big-endian helpers ------------------------------ */
static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
static uint16_t rd16(const uint8_t *p) { return (uint16_t)((p[0] << 8) | p[1]); }
static void     wr32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

/* ------------------------------------------------------------------ *
 *  Decoder.  A count byte below 128 introduces that many literal
 *  bytes; 128 or above means the next byte repeats (count - 127) times.
 * ------------------------------------------------------------------ */
size_t sc2_rle_decode(const uint8_t *in, size_t in_len, uint8_t *out, size_t out_cap)
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
 *  Encoder -- a faithful port of $293EC.
 *
 *  The original is not a textbook RLE and the differences are load
 *  bearing if you want identical bytes back:
 *
 *    - A run is emitted only when two adjacent bytes already match, and
 *      it is capped at 128 ($2946A: cmpi.l #$80).
 *    - The literal branch ($294A2) writes bytes one ahead of the count
 *      slot and stops as soon as it sees a byte equal to its
 *      predecessor -- so a literal chunk always ends just before a run
 *      begins, and the byte that ended it is left for the next chunk.
 *    - The main loop runs while i < size-1, so a final odd byte is
 *      emitted afterwards as a one-byte literal ($29514).
 *
 *  Worst case output is size * 3 / 2, which is what the original
 *  allocates at $2940C.
 * ------------------------------------------------------------------ */
size_t sc2_rle_encode(const uint8_t *src, size_t size, uint8_t *dst)
{
    size_t i = 0, o = 0;

    while (size >= 1 && i < size - 1)
    {
        uint8_t v = src[i];

        if (src[i + 1] == v)
        {                 /* --- run --- */
            size_t n = 2; /* $29452      */
            while (src[i + n] == v && n < 0x80 && i + n < size)
                n++;
            dst[o++] = (uint8_t)((n - 1) | 0x80);
            dst[o++] = v;
            i += n;
        }
        else
        {                    /* --- literal --- */
            size_t  n   = 1; /* $294A2      */
            uint8_t cur = v;
            dst[o + 1]  = cur;
            for (;;)
            {
                uint8_t nxt = (i + n < size) ? src[i + n] : 0;
                if (nxt == cur)
                    break; /* a run starts here */
                if (n >= 0x80)
                    break;
                if (i + n >= size)
                    break;
                cur = nxt;
                n++;
                dst[o + n] = cur;
            }
            dst[o] = (uint8_t)(n - 1); /* count, $294E8 */
            i += n - 1;
            o += n;
        }
    }

    if (size >= 1 && i == size - 1)
    { /* trailing byte, $29514 */
        dst[o++] = 1;
        dst[o++] = src[i];
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
                c->altm[r][x] = rd16(d + (r * MAP_W + x) * 2);
    }
    else if (!memcmp(tag, "XBLD", 4))
        memcpy(c->xbld, d, n < sizeof c->xbld ? n : sizeof c->xbld);
    else if (!memcmp(tag, "XZON", 4))
        memcpy(c->xzon, d, n < sizeof c->xzon ? n : sizeof c->xzon);
    else if (!memcmp(tag, "XTER", 4))
        memcpy(c->xter, d, n < sizeof c->xter ? n : sizeof c->xter);
    else if (!memcmp(tag, "XUND", 4))
        memcpy(c->xund, d, n < sizeof c->xund ? n : sizeof c->xund);
    else if (!memcmp(tag, "XTXT", 4))
        memcpy(c->xtxt, d, n < sizeof c->xtxt ? n : sizeof c->xtxt);
    else if (!memcmp(tag, "XBIT", 4))
        memcpy(c->xbit, d, n < sizeof c->xbit ? n : sizeof c->xbit);
    else if (!memcmp(tag, "XTRF", 4))
        memcpy(c->xtrf, d, n < sizeof c->xtrf ? n : sizeof c->xtrf);
    else if (!memcmp(tag, "XPLT", 4))
        memcpy(c->xplt, d, n < sizeof c->xplt ? n : sizeof c->xplt);
    else if (!memcmp(tag, "XVAL", 4))
        memcpy(c->xval, d, n < sizeof c->xval ? n : sizeof c->xval);
    else if (!memcmp(tag, "XCRM", 4))
        memcpy(c->xcrm, d, n < sizeof c->xcrm ? n : sizeof c->xcrm);
    else if (!memcmp(tag, "XPLC", 4))
        memcpy(c->xplc, d, n < sizeof c->xplc ? n : sizeof c->xplc);
    else if (!memcmp(tag, "XFIR", 4))
        memcpy(c->xfir, d, n < sizeof c->xfir ? n : sizeof c->xfir);
    else if (!memcmp(tag, "XPOP", 4))
        memcpy(c->xpop, d, n < sizeof c->xpop ? n : sizeof c->xpop);
    else if (!memcmp(tag, "XROG", 4))
        memcpy(c->xrog, d, n < sizeof c->xrog ? n : sizeof c->xrog);
    else if (!memcmp(tag, "XLAB", 4))
        keep_raw(&c->xlab, &c->xlab_len, d, n);
    else if (!memcmp(tag, "XMIC", 4))
        keep_raw(&c->xmic, &c->xmic_len, d, n);
    else if (!memcmp(tag, "XTHG", 4))
        keep_raw(&c->xthg, &c->xthg_len, d, n);
    else if (!memcmp(tag, "XGRP", 4))
    {
        /*  Sixteen series of 52 big-endian longs, in series order --
         *  the block $2D52E allocates, written out whole. */
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

/* gather a city member back into a flat chunk; returns length, 0 if unknown */
static size_t fetch_chunk(const City *c, const char *tag, uint8_t *out)
{
    size_t r, x;
    if (!memcmp(tag, "MISC", 4))
    {
        for (r = 0; r < MISC_LONGS; r++)
            wr32(out + r * 4, (uint32_t)c->misc[r]);
        return MISC_LONGS * 4;
    }
    if (!memcmp(tag, "ALTM", 4))
    {
        for (r = 0; r < MAP_H; r++)
            for (x = 0; x < MAP_W; x++)
            {
                out[(r * MAP_W + x) * 2]     = (uint8_t)(c->altm[r][x] >> 8);
                out[(r * MAP_W + x) * 2 + 1] = (uint8_t)(c->altm[r][x]);
            }
        return MAP_W * MAP_H * 2;
    }
#define GATHER(T, M)                    \
    if (!memcmp(tag, T, 4))             \
    {                                   \
        memcpy(out, c->M, sizeof c->M); \
        return sizeof c->M;             \
    }
    GATHER("XBLD", xbld)
    GATHER("XZON", xzon)
    GATHER("XTER", xter)
    GATHER("XUND", xund)
    GATHER("XTXT", xtxt)
    GATHER("XBIT", xbit)
    GATHER("XTRF", xtrf)
    GATHER("XPLT", xplt)
    GATHER("XVAL", xval)
    GATHER("XCRM", xcrm)
    GATHER("XPLC", xplc)
    GATHER("XFIR", xfir)
    GATHER("XPOP", xpop)
    GATHER("XROG", xrog)
#undef GATHER
#define RAW(T, M)                      \
    if (!memcmp(tag, T, 4))            \
    {                                  \
        if (!c->M)                     \
            return 0;                  \
        memcpy(out, c->M, c->M##_len); \
        return c->M##_len;             \
    }
    RAW("XLAB", xlab)
    RAW("XMIC", xmic)
    RAW("XTHG", xthg)
    if (!memcmp(tag, "XGRP", 4))
    {
        /*  written from graph[] rather than the loaded blob, so a
         *  history the simulation has advanced is what lands on disk */
        size_t i, k;
        for (i = 0; i < N_GRAPH; i++)
            for (k = 0; k < GRAPH_SAMPLES; k++)
                wr32(out + (i * GRAPH_SAMPLES + k) * 4,
                     (uint32_t)c->graph[i][k]);
        return N_GRAPH * GRAPH_SAMPLES * 4;
    }
    RAW("CNAM", cnam)
#undef RAW
    return 0;
}

/* pull the named scalars out of raw MISC.  Indices 2..13 are the ones
 * the MISC builder at $2A186 emits from straight-line code, so they are
 * unambiguous; anything past 26 sits behind counted loops and is not
 * decoded here yet. */
void city_misc_to_scalars(City *c)
{
    int i;

    c->rotation       = (int16_t)c->misc[2];
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
     *  out/miscload.json -- the game's own unpacker at $295D6 with
     *  every read tagged.  Leaving them out started a loaded city
     *  with less state than the original has, which the clock
     *  comparison shows on the very first tick. */
    c->unemployment  = c->misc[1001];          /* A5+0x2C82 */
    c->power_pct     = c->misc[1048];          /* A5+0x1E86 */
    c->water_pct     = c->misc[1049];          /* A5+0x1E8A */
    c->developed     = (int16_t)c->misc[1067]; /* A5+0x11D0 */
    c->disasters_off = (int16_t)c->misc[1024]; /* A5+0x13AA */
    c->disaster_kind = (int16_t)c->misc[28];   /* A5+0x13A0 */
    c->centre_y      = (int16_t)c->misc[1030]; /* A5+0x867E */
    c->centre_x      = (int16_t)c->misc[1031]; /* A5+0x8680 */
    /*  A5+0x1EFA -- sixteen counters, one per id $DD..$EC, saved as
     *  MISC[1002..1017].  The growth scan only ever adjusts them by
     *  one as buildings come and go, so a loaded city that starts
     *  them at zero has the military and airport ladders reading
     *  nothing and asking for the wrong building. */
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
     *  every read tagged by its MISC index (tools/miscload.py); the
     *  layout below is what that produced, not a guess at a stride. */
    /*  Unsigned: a big map can hold more than 32767 trees, and several
     *  shipped cities do.  The game zero-extends every read of it. */
    for (i = 0; i < 256; i++)
        c->census[i] = (uint16_t)c->misc[MISC_CENSUS + i];

    /*  Population per zone kind, MISC[380..387] -- the city scan
     *  rebuilds it, but the economy reads accum8[7] (the industrial
     *  total) before any scan has run, in the pollution-per-head term
     *  at $35FBA.  Loading it is what takes Flint and Oakland exact. */
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

/*  and back the other way, so a re-save carries what we changed */
static void scalars_to_misc(City *c)
{
    int i;

    for (i = 0; i < 256; i++)
        c->misc[MISC_CENSUS + i] = c->census[i];

    for (i = 0; i < N_DEPT; i++)
    {
        int32_t *d = &c->misc[MISC_BUDGET + i * 27];
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
    c->misc[MISC_YEAR_END]     = c->year_end;
    c->misc[MISC_TRANSIT_BUS]  = c->transit_bus;
    c->misc[MISC_TRANSIT_RAIL] = c->transit_rail;
    c->misc[MISC_TRANSIT_SUB]  = c->transit_subway;
    for (i = 0; i < 3; i++)
        c->misc[MISC_RCI_DEMAND + i] = c->rci_demand[i];
}

int city_load(const char *path, City *c)
{
    /*  .arco and the 1995 .sc2 are both cities as far as a caller is
     *  concerned.  Which one this is, is a question the file answers.
     *
     *  The two report differently and always will: city_load answers 1
     *  for success because its callers read it as a predicate, and
     *  arco_load answers 0 because its callers read it as an errno.
     *  Converting here is the whole of the reconciliation. */
    if (arco_is_arco(path))
        return arco_load(path, c) == 0;
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

        /*  $2D594 -- the graph scales come up as one, not zero.  They are
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
        city_misc_to_scalars(c);
        return 1;
    }
}

int city_save(const char *path, const City *c)
{
    FILE    *f;
    City    *w      = (City *)c; /* the two derived blocks are folded back in */
    uint8_t *flat   = (uint8_t *)malloc(1 << 20);
    uint8_t *packed = (uint8_t *)malloc(1 << 21);
    uint8_t *out    = (uint8_t *)malloc(1 << 21);
    size_t   o      = 12;
    int      i, ok;

    scalars_to_misc(w);

    memcpy(out, "FORM", 4);
    memcpy(out + 8, "SCDH", 4);

    for (i = 0; i < c->n_chunks; i++)
    {
        const char *tag = c->order[i];
        size_t      n   = fetch_chunk(c, tag, flat), m;
        if (!n)
            continue;
        if (is_uncompressed(tag))
        {
            memcpy(packed, flat, n);
            m = n;
        }
        else
        {
            m = sc2_rle_encode(flat, n, packed);
        }
        memcpy(out + o, tag, 4);
        wr32(out + o + 4, (uint32_t)m);
        memcpy(out + o + 8, packed, m);
        o += 8 + m;
    }
    wr32(out + 4, (uint32_t)(o - 8));

    f  = fopen(path, "wb");
    ok = f && fwrite(out, 1, o, f) == o;
    if (f)
        fclose(f);
    free(flat);
    free(packed);
    free(out);
    return ok;
}

void city_free(City *c)
{
    free(c->xlab);
    free(c->xmic);
    free(c->xthg);
    free(c->xgrp);
    free(c->cnam);
    c->xlab = c->xmic = c->xthg = c->xgrp = c->cnam = NULL;
}
