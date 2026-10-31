/*  ext80.h: the 80-bit extended arithmetic SimCity 2000's economic model
 *  runs on.  The game reaches it through the SANE trap _FP68K ($A9EB):
 *  push two operand pointers and an opword, trap, read the result back.
 *  The opword is the operation in bits 0-4 and the operand format in
 *  bits 11-13.  Across the whole program only 21 distinct opwords
 *  appear, so this implements exactly the operations that are actually
 *  used.  Apple Silicon has no hardware 80-bit type, and long double
 *  there is 53 bits, so the faithful build is a software implementation.
 *  The native build (ARC_NATIVE_FLOAT, the default) runs the same calls
 *  on doubles. */
#ifndef EXT80_H
#define EXT80_H
#include <stdint.h>

#ifndef ARC_NATIVE_FLOAT
    #define ARC_NATIVE_FLOAT 0
#endif

#if !ARC_NATIVE_FLOAT
/*  value = (-1)^sign * sig * 2^(exp-63), with bit 63 of sig set when
 *  normal.  Sig == 0 means zero.  The game's model never produces inf or
 *  nan, so those are represented but not elaborated. */
typedef struct
{
    uint64_t sig;
    int32_t  exp;
    int      sign;
} ext80;
#else
/*  THE NATIVE BUILD (ARC_NATIVE_FLOAT, the CMake option of that name).
 *  Each number is the machine's own double and each operation one
 *  instruction.  It answers as the original does where the original
 *  makes a choice.  A quotient by zero is zero, and a number too large
 *  for 32 bits truncates to the nearest end of the range.  It rounds to
 *  53 bits, not 64, so its results drift from the original's in the last
 *  places.  It is the default.  The faithful build is
 *  ARC_NATIVE_FLOAT=OFF. */
typedef struct
{
    double v;
} ext80;
#endif

#if !ARC_NATIVE_FLOAT
ext80   ext_from_i32(int32_t v);
ext80   ext_from_i16(int16_t v);
ext80   ext_from_double(double v);
ext80   ext_from_float(float v);
double  ext_to_double(ext80 a);
int32_t ext_to_i32(ext80 a); /* FOTTI: truncate toward zero */

ext80 ext_add(ext80 a, ext80 b);
ext80 ext_sub(ext80 a, ext80 b);
ext80 ext_mul(ext80 a, ext80 b);
ext80 ext_div(ext80 a, ext80 b);
#else
static inline ext80 ext_make(double v)
{
    ext80 r;
    r.v = v;
    return r;
}
static inline ext80   ext_from_i32(int32_t v) { return ext_make((double)v); }
static inline ext80   ext_from_i16(int16_t v) { return ext_make((double)v); }
static inline ext80   ext_from_double(double v) { return ext_make(v); }
static inline ext80   ext_from_float(float v) { return ext_make((double)v); }
static inline double  ext_to_double(ext80 a) { return a.v; }
static inline int32_t ext_to_i32(ext80 a) /* FOTTI: truncate toward zero */
{
    if (!(a.v == a.v))
        return 0;
    if (a.v >= 2147483647.0)
        return INT32_MAX;
    if (a.v <= -2147483648.0)
        return INT32_MIN;
    return (int32_t)a.v;
}
static inline ext80 ext_add(ext80 a, ext80 b) { return ext_make(a.v + b.v); }
static inline ext80 ext_sub(ext80 a, ext80 b) { return ext_make(a.v - b.v); }
static inline ext80 ext_mul(ext80 a, ext80 b) { return ext_make(a.v * b.v); }
static inline ext80 ext_div(ext80 a, ext80 b) { return ext_make(b.v == 0.0 ? 0.0 : a.v / b.v); }
#endif

/* read/write the 10-byte in-memory form the 68k code passes around */
ext80 ext_load(const uint8_t p[10]);
void  ext_store(ext80 a, uint8_t p[10]);

#endif
