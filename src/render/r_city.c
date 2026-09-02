/*  r_city.c -- the .SC2 reader, independent of the simulation's own.
 *
 *  IFF: "FORM" <len> "SCDH", then chunks of <tag><len><payload>.  Every
 *  chunk is RLE'd except CNAM and ALTM.  The codec is the one at $293EC:
 *  a byte under 128 introduces that many literal bytes, a byte of 128 or
 *  more repeats the next byte (c - 127) times.
 */
#include "r_city.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*  Read big-endian a byte at a time.  Never cast a struct over the buffer:
 *  the file is big-endian and the host may not be. */
static uint32_t be32(const uint8_t *p)
{
    return ((uint32_t) p[0] << 24) | ((uint32_t) p[1] << 16) |
           ((uint32_t) p[2] << 8) | (uint32_t) p[3];
}

static uint16_t be16(const uint8_t *p)
{
    return (uint16_t) (((uint16_t) p[0] << 8) | (uint16_t) p[1]);
}

static size_t unrle(const uint8_t *in, size_t n, uint8_t *out, size_t cap)
{
    size_t i = 0, o = 0;
    while (i < n)
    {
        uint8_t c = in[i++];
        if (c < 128)
        {
            size_t k = c;
            if (i + k > n)
                k = n - i;
            if (o + k > cap)
                k = cap - o;
            memcpy(out + o, in + i, k);
            o += k;
            i += c;
        }
        else
        {
            size_t k = (size_t) (c - 127);
            if (i >= n)
                break;
            if (o + k > cap)
                k = cap - o;
            memset(out + o, in[i], k);
            o += k;
            i += 1;
        }
        if (o >= cap)
            break;
    }
    return o;
}

static int is_raw(const uint8_t *tag)
{
    return memcmp(tag, "CNAM", 4) == 0 || memcmp(tag, "ALTM", 4) == 0;
}

int r_city_load(RCity *c, const char *path)
{
    FILE    *f;
    uint8_t *buf;
    long     flen;
    size_t   got, off;

    memset(c, 0, sizeof *c);

    f = fopen(path, "rb"); /* binary mode: a text read corrupts every offset */
    if (!f)
    {
        snprintf(c->err, sizeof c->err, "cannot open %s", path);
        return -1;
    }
    if (fseek(f, 0, SEEK_END) != 0 || (flen = ftell(f)) < 12)
    {
        fclose(f);
        snprintf(c->err, sizeof c->err, "%s is too short to be a city", path);
        return -1;
    }
    rewind(f);
    buf = (uint8_t *) malloc((size_t) flen);
    if (!buf)
    {
        fclose(f);
        snprintf(c->err, sizeof c->err, "out of memory");
        return -1;
    }
    got = fread(buf, 1, (size_t) flen, f);
    fclose(f);
    if (got != (size_t) flen)
    {
        free(buf);
        snprintf(c->err, sizeof c->err, "short read on %s", path);
        return -1;
    }
    if (memcmp(buf, "FORM", 4) != 0 || memcmp(buf + 8, "SCDH", 4) != 0)
    {
        free(buf);
        snprintf(c->err, sizeof c->err, "%s is not a SimCity 2000 city", path);
        return -1;
    }

    {
        size_t total = (size_t) be32(buf + 4);
        size_t limit = total + 8u < (size_t) flen ? total + 8u : (size_t) flen;
        off = 12;
        while (off + 8 <= limit)
        {
            const uint8_t *tag = buf + off;
            size_t         n   = (size_t) be32(buf + off + 4);
            const uint8_t *pay = buf + off + 8;
            uint8_t        tmp[40960];
            const uint8_t *data;
            size_t         dlen;

            if (off + 8 + n > (size_t) flen)
                break;
            if (is_raw(tag))
            {
                data = pay;
                dlen = n;
            }
            else
            {
                dlen = unrle(pay, n, tmp, sizeof tmp);
                data = tmp;
            }

#define TAKE(NAME, FIELD)                                                     \
    else if (memcmp(tag, NAME, 4) == 0)                                       \
    {                                                                         \
        size_t k = dlen < sizeof c->FIELD ? dlen : sizeof c->FIELD;           \
        memcpy(c->FIELD, data, k);                                            \
    }

            if (memcmp(tag, "ALTM", 4) == 0)
            {
                size_t k = dlen / 2u;
                size_t i;
                if (k > R_MAP * R_MAP)
                    k = R_MAP * R_MAP;
                for (i = 0; i < k; ++i)
                    c->altm[i] = be16(data + i * 2u);
            }
            else if (memcmp(tag, "MISC", 4) == 0)
            {
                size_t k = dlen / 4u;
                size_t i;
                if (k > 1200)
                    k = 1200;
                for (i = 0; i < k; ++i)
                    c->misc[i] = (int32_t) be32(data + i * 4u);
            }
            else if (memcmp(tag, "XTHG", 4) == 0)
            {
                size_t recs = dlen / 12u;
                if (recs > R_MAX_THINGS)
                    recs = R_MAX_THINGS;
                memcpy(c->xthg, data, recs * 12u);
                c->n_things = (int32_t) recs;
            }
            else if (memcmp(tag, "CNAM", 4) == 0)
            {
                size_t k = dlen < sizeof c->name - 1u ? dlen : sizeof c->name - 1u;
                memcpy(c->name, data, k);
                c->name[k] = '\0';
            }
            TAKE("XBLD", xbld)
            TAKE("XZON", xzon)
            TAKE("XTER", xter)
            TAKE("XUND", xund)
            TAKE("XTXT", xtxt)
            TAKE("XBIT", xbit)
            TAKE("XTRF", xtrf)
            TAKE("XPLT", xplt)
            TAKE("XVAL", xval)
            TAKE("XCRM", xcrm)
            TAKE("XPLC", xplc)
            TAKE("XFIR", xfir)
            TAKE("XPOP", xpop)
            TAKE("XROG", xrog)
#undef TAKE

            off += 8u + n;
        }
    }
    free(buf);
    c->rotation = c->misc[2] & 3;
    return 0;
}

uint8_t r_city_corner_mask(int32_t rotation)
{
    /*  ROT_CORNER_MASK.  The array turns under the corner nibble -- rotating
     *  a city moves the bytes but leaves XZON's high nibble alone -- so the
     *  bit that means "nearest the viewer" changes with the rotation. */
    static const uint8_t mask[4] = {0x80, 0x10, 0x20, 0x40};
    return mask[rotation & 3];
}
