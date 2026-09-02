/*  ext80.h -- the 80-bit extended arithmetic SimCity 2000's economic
 *  model runs on.
 *
 *  The game reaches it through the SANE trap _FP68K ($A9EB): push two
 *  operand pointers and an opword, trap, read the result back.  The
 *  opword is the operation in bits 0-4 and the operand format in bits
 *  11-13.  Across the whole program only 21 distinct opwords appear, so
 *  this implements exactly the operations that are actually used.
 *
 *  Apple Silicon has no hardware 80-bit type -- long double here is
 *  53-bit -- so this is a software implementation rather than a wrapper.
 */
#ifndef EXT80_H
#define EXT80_H
#include <stdint.h>

/*  value = (-1)^sign * sig * 2^(exp-63), with bit 63 of sig set when
 *  normal.  sig == 0 means zero.  The game's model never produces inf
 *  or nan, so those are represented but not elaborated. */
typedef struct
{
    uint64_t sig;
    int32_t  exp;
    int      sign;
} ext80;

/* SANE operation codes, bits 0-4 of the opword */
enum
{
    FOADD = 0x00,
    FOSUB = 0x02,
    FOMUL = 0x04,
    FODIV = 0x06,
    FOCMP = 0x08,
    FOZ2X = 0x0E,
    FOX2Z = 0x10,
    FOTTI = 0x16
};

/* SANE operand formats, bits 11-13 */
enum
{
    FFEXT  = 0,
    FFDBL  = 1,
    FFSGL  = 2,
    FFINT  = 4,
    FFLNG  = 5,
    FFCOMP = 6
};

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
int   ext_cmp(ext80 a, ext80 b); /* -1, 0, +1 */

/* read/write the 10-byte in-memory form the 68k code passes around */
ext80 ext_load(const uint8_t p[10]);
void  ext_store(ext80 a, uint8_t p[10]);

#endif
