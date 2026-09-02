/*  sane.c -- the _FP68K trap ($A9EB), reduced to what SimCity 2000 uses.
 *
 *  The 68k calling convention is Pascal: push a pointer to the source,
 *  then a pointer to the destination, then the opword, then trap -- so
 *  the destination ends up NEAREST the stack pointer.  The destination
 *  is read-modify-written for the arithmetic operations and written for
 *  the conversions, so the call sites read like
 *
 *      pea  src ; pea dst ; move.w #$0804,-(sp) ; _FP68K
 *
 *  which is "dst = dst * src, operands in double".  Getting the two the
 *  wrong way round still runs, and still terminates, and quietly
 *  computes the reciprocal of what was meant.  Transcribing $34D04
 *  is then mechanical: each four-instruction group becomes one fp68k().
 *
 *  Only the 21 opwords that actually occur are handled; anything else
 *  is a bug in the transcription rather than a missing feature, so it
 *  is reported rather than silently ignored.
 */
#include "ext80.h"
#include <stdio.h>
#include <string.h>

static ext80 load_fmt(int fmt, const void *p)
{
    switch (fmt)
    {
        case FFEXT:
            return ext_load((const uint8_t *)p);
        case FFDBL:
            {
                double d;
                memcpy(&d, p, 8);
                return ext_from_double(d);
            }
        case FFSGL:
            {
                float f;
                memcpy(&f, p, 4);
                return ext_from_float(f);
            }
        case FFINT:
            {
                int16_t v;
                memcpy(&v, p, 2);
                return ext_from_i16(v);
            }
        case FFLNG:
            {
                int32_t v;
                memcpy(&v, p, 4);
                return ext_from_i32(v);
            }
        case FFCOMP:
            {
                int64_t v;
                memcpy(&v, p, 8);
                return ext_from_i32((int32_t)v);
            }
    }
    return ext_from_i32(0);
}

static void store_fmt(int fmt, void *p, ext80 a)
{
    switch (fmt)
    {
        case FFEXT:
            ext_store(a, (uint8_t *)p);
            return;
        case FFDBL:
            {
                double d = ext_to_double(a);
                memcpy(p, &d, 8);
                return;
            }
        case FFSGL:
            {
                float f = (float)ext_to_double(a);
                memcpy(p, &f, 4);
                return;
            }
        case FFINT:
            {
                int16_t v = (int16_t)ext_to_i32(a);
                memcpy(p, &v, 2);
                return;
            }
        case FFLNG:
            {
                int32_t v = ext_to_i32(a);
                memcpy(p, &v, 4);
                return;
            }
        case FFCOMP:
            {
                int64_t v = ext_to_i32(a);
                memcpy(p, &v, 8);
                return;
            }
    }
}

/*  Returns the comparison result for FOCMP, 0 otherwise. */
int fp68k(uint16_t opword, void *dst, const void *src)
{
    int   op  = opword & 0x1F;
    int   fmt = (opword >> 11) & 0x7;
    ext80 d, s;

    switch (op)
    {
        case FOZ2X: /* src in fmt -> dst extended */
            store_fmt(FFEXT, dst, load_fmt(fmt, src));
            return 0;
        case FOX2Z: /* src extended -> dst in fmt */
            store_fmt(fmt, dst, ext_load((const uint8_t *)src));
            return 0;
        case FOTTI: /* truncate extended in place  */
            d = ext_load((const uint8_t *)dst);
            store_fmt(FFEXT, dst, ext_from_i32(ext_to_i32(d)));
            return 0;
        default:
            break;
    }

    d = ext_load((const uint8_t *)dst);
    s = load_fmt(fmt, src);
    switch (op)
    {
        case FOADD:
            d = ext_add(d, s);
            break;
        case FOSUB:
            d = ext_sub(d, s);
            break;
        case FOMUL:
            d = ext_mul(d, s);
            break;
        case FODIV:
            d = ext_div(d, s);
            break;
        case FOCMP:
            return ext_cmp(d, s);
        default:
            fprintf(stderr, "fp68k: unhandled opword $%04X (op 0x%02X fmt %d)\n", opword, op, fmt);
            return 0;
    }
    store_fmt(FFEXT, dst, d);
    return 0;
}
