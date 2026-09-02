#include "ext80.h"
#include <stdint.h>
#include <string.h>

/* ---- construction ------------------------------------------------ */
static ext80 normalize(int sign, uint64_t sig, int32_t exp)
{
    ext80 r;
    if (sig == 0)
    {
        r.sig  = 0;
        r.exp  = 0;
        r.sign = sign;
        return r;
    }
    while (!(sig & 0x8000000000000000ULL))
    {
        sig <<= 1;
        exp--;
    }
    r.sig  = sig;
    r.exp  = exp;
    r.sign = sign;
    return r;
}

ext80 ext_from_i32(int32_t v)
{
    int      sign = v < 0;
    uint64_t m    = (uint64_t)(sign ? -(int64_t)v : (int64_t)v);
    return normalize(sign, m, 63);
}
ext80 ext_from_i16(int16_t v) { return ext_from_i32(v); }

ext80 ext_from_double(double v)
{
    uint64_t b;
    int      sign;
    int32_t  e;
    uint64_t frac;
    memcpy(&b, &v, 8);
    sign = (int)(b >> 63);
    e    = (int32_t)((b >> 52) & 0x7FF);
    frac = b & 0xFFFFFFFFFFFFFULL;
    if (e == 0)
    { /* zero or subnormal */
        if (frac == 0)
        {
            ext80 z;
            z.sig  = 0;
            z.exp  = 0;
            z.sign = sign;
            return z;
        }
        return normalize(sign, frac << 11, -1022);
    }
    return normalize(sign, (frac | 0x10000000000000ULL) << 11, e - 1023);
}
ext80 ext_from_float(float v) { return ext_from_double((double)v); }

double ext_to_double(ext80 a)
{
    double   d;
    uint64_t b;
    int32_t  e;
    if (a.sig == 0)
    {
        b = (uint64_t)a.sign << 63;
        memcpy(&d, &b, 8);
        return d;
    }
    e = a.exp + 1023;
    if (e <= 0)
    {
        b = (uint64_t)a.sign << 63;
        memcpy(&d, &b, 8);
        return d;
    }
    if (e >= 2047)
    {
        b = ((uint64_t)a.sign << 63) | 0x7FF0000000000000ULL;
        memcpy(&d, &b, 8);
        return d;
    }
    /* round the 64-bit significand to 53 bits, nearest-even */
    {
        uint64_t sig = a.sig, half = 1ULL << 10, low = sig & ((1ULL << 11) - 1);
        sig >>= 11;
        if (low > half || (low == half && (sig & 1)))
        {
            sig++;
            if (sig == (1ULL << 53))
            {
                sig >>= 1;
                e++;
            }
        }
        b = ((uint64_t)a.sign << 63) | ((uint64_t)e << 52) | (sig & 0xFFFFFFFFFFFFFULL);
    }
    memcpy(&d, &b, 8);
    return d;
}

int32_t ext_to_i32(ext80 a) /* FOTTI, truncates */
{
    int32_t  sh;
    uint64_t m;
    if (a.sig == 0)
        return 0;
    sh = 63 - a.exp;
    if (sh >= 64)
        return 0;
    if (sh <= 0)
        return a.sign ? INT32_MIN : INT32_MAX; /* overflow */
    m = a.sig >> sh;
    if (m > 0x7FFFFFFFULL)
        return a.sign ? INT32_MIN : INT32_MAX;
    return a.sign ? -(int32_t)m : (int32_t)m;
}

/* ---- arithmetic -------------------------------------------------- */
static ext80 round128(int sign, unsigned __int128 p, int32_t exp)
{
    /* p holds the product/quotient with 64 guard bits below the point */
    uint64_t hi, lo;
    if (p == 0)
    {
        ext80 z;
        z.sig  = 0;
        z.exp  = 0;
        z.sign = sign;
        return z;
    }
    while (!((p >> 64) & 0x8000000000000000ULL))
    {
        p <<= 1;
        exp--;
    }
    hi = (uint64_t)(p >> 64);
    lo = (uint64_t)p;
    if (lo > 0x8000000000000000ULL || (lo == 0x8000000000000000ULL && (hi & 1)))
    {
        hi++;
        if (hi == 0)
        {
            hi = 0x8000000000000000ULL;
            exp++;
        }
    }
    {
        ext80 r;
        r.sig  = hi;
        r.exp  = exp;
        r.sign = sign;
        return r;
    }
}

ext80 ext_mul(ext80 a, ext80 b)
{
    if (a.sig == 0 || b.sig == 0)
    {
        ext80 z;
        z.sig  = 0;
        z.exp  = 0;
        z.sign = a.sign ^ b.sign;
        return z;
    }
    return round128(a.sign ^ b.sign,
                    (unsigned __int128)a.sig * b.sig,
                    a.exp + b.exp + 1);
}

ext80 ext_div(ext80 a, ext80 b)
{
    unsigned __int128 num;
    if (a.sig == 0 || b.sig == 0)
    {
        ext80 z;
        z.sig  = 0;
        z.exp  = 0;
        z.sign = a.sign ^ b.sign;
        return z;
    }
    num = (unsigned __int128)a.sig << 64;
    return round128(a.sign ^ b.sign, num / b.sig, a.exp - b.exp + 63);
}

static ext80 addsub(ext80 a, ext80 b, int subtract)
{
    int32_t           d;
    unsigned __int128 ma, mb;
    int               sign;

    if (subtract)
        b.sign ^= 1;
    if (a.sig == 0)
        return b;
    if (b.sig == 0)
        return a;

    if (a.exp < b.exp)
    {
        ext80 t = a;
        a       = b;
        b       = t;
    }
    d = a.exp - b.exp;
    if (d > 80)
        return a;

    ma = (unsigned __int128)a.sig << 64;
    mb = ((unsigned __int128)b.sig << 64) >> d;

    if (a.sign == b.sign)
    {
        unsigned __int128 s = ma + mb;
        sign                = a.sign;
        if (s < ma)
        {
            s = (s >> 1) | ((unsigned __int128)1 << 127);
            a.exp++;
        }
        return round128(sign, s, a.exp);
    }
    if (ma >= mb)
    {
        sign = a.sign;
        return round128(sign, ma - mb, a.exp);
    }
    sign = b.sign;
    return round128(sign, mb - ma, a.exp);
}

ext80 ext_add(ext80 a, ext80 b) { return addsub(a, b, 0); }
ext80 ext_sub(ext80 a, ext80 b) { return addsub(a, b, 1); }

int ext_cmp(ext80 a, ext80 b)
{
    ext80 d = ext_sub(a, b);
    if (d.sig == 0)
        return 0;
    return d.sign ? -1 : 1;
}

/* ---- the 10-byte memory form ------------------------------------- */
ext80 ext_load(const uint8_t p[10])
{
    ext80   r;
    int32_t e = ((p[0] & 0x7F) << 8) | p[1];
    r.sign    = p[0] >> 7;
    r.sig     = 0;
    {
        int i;
        for (i = 0; i < 8; i++)
            r.sig = (r.sig << 8) | p[2 + i];
    }
    r.exp = e - 16383;
    if (r.sig == 0)
    {
        r.exp = 0;
    }
    return r;
}

void ext_store(ext80 a, uint8_t p[10])
{
    int32_t e = a.sig ? a.exp + 16383 : 0;
    int     i;
    p[0] = (uint8_t)((a.sign << 7) | ((e >> 8) & 0x7F));
    p[1] = (uint8_t)e;
    for (i = 0; i < 8; i++)
        p[2 + i] = (uint8_t)(a.sig >> (56 - 8 * i));
}
