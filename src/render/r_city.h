/*  r_city.h -- a city, as the renderer is allowed to see it.
 *
 *  This deliberately does NOT include the simulation's sc2k.h.  The design
 *  boundary is that the renderer reads and never writes, and the cheapest
 *  way to guarantee that is for the renderer to have its own read-only view
 *  type.  When the two are linked together, an adapter fills an RCity from
 *  a `const City *` and the renderer still cannot reach the simulation's
 *  state.  Until then this loads a .SC2 file directly, which also lets the
 *  renderer be built and tested on its own.
 */
#ifndef R_CITY_H
#define R_CITY_H

#include <stdint.h>

#define R_MAP  128
#define R_HALF 64
#define R_QTR  32
#define R_MAX_THINGS 64 /* the fullest shipped city has 40 */

typedef struct
{
    /* full resolution, 128x128 */
    uint16_t altm[R_MAP * R_MAP]; /* low 5 bits are the altitude level */
    uint8_t  xbld[R_MAP * R_MAP];
    uint8_t  xzon[R_MAP * R_MAP];
    uint8_t  xter[R_MAP * R_MAP];
    uint8_t  xund[R_MAP * R_MAP];
    uint8_t  xtxt[R_MAP * R_MAP];
    uint8_t  xbit[R_MAP * R_MAP];

    /* half resolution, 64x64 */
    uint8_t xtrf[R_HALF * R_HALF];
    uint8_t xplt[R_HALF * R_HALF];
    uint8_t xval[R_HALF * R_HALF];
    uint8_t xcrm[R_HALF * R_HALF];

    /* quarter resolution, 32x32 */
    uint8_t xplc[R_QTR * R_QTR];
    uint8_t xfir[R_QTR * R_QTR];
    uint8_t xpop[R_QTR * R_QTR];
    uint8_t xrog[R_QTR * R_QTR];

    /*  XTHG: twelve bytes per record.  +0 type, +1 heading, +3 y, +4 x --
     *  the field order settled by which reading puts things on real
     *  infrastructure (98.5% vs 93.6% across the shipped cities). */
    uint8_t xthg[R_MAX_THINGS * 12];
    int32_t n_things;

    int32_t misc[1200];
    int32_t rotation; /* misc[2] & 3 */
    char    name[64];
    char    err[192];
} RCity;

/*  Read a .SC2 file.  Returns 0, or -1 with a reason in c->err.
 *  `c` is caller-allocated; it is about 156 KB, so allocate it, do not put
 *  it on the stack. */
int r_city_load(RCity *c, const char *path);

/*  The XZON corner bit that is nearest the viewer at this rotation.
 *  A multi-tile building is drawn once, at the tile carrying this bit.  */
uint8_t r_city_corner_mask(int32_t rotation);

#endif /* R_CITY_H */
