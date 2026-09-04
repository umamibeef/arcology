/*  tables.h -- tables lifted out of the binary's own global image.
 *  See tools/gen_render_tables.py; never hand-type a constant here.
 */
#ifndef R_TABLES_H
#define R_TABLES_H

#include <stdint.h>

/*  XTER -> terrain shape id.  Zero means "no art, draw nothing". */
extern const uint16_t R_XTER_TILE[256];

/*  Things: base shape per type, the zoom below which a type is not drawn,
 *  and the heading -> sprite-frame / mirror pair. */
extern const uint16_t R_THING_SHAPE[17];
extern const uint16_t R_THING_MINZOOM[17];
extern const uint8_t  R_DIR_FRAME[8];
extern const uint8_t  R_DIR_FLIP[8];

/*  Road vehicles (thing types 10 and 11), $A7E0. */
extern const uint8_t R_VEH_SLOT[28];
extern const uint8_t R_VEH_SLOT8[8];
extern const uint8_t R_VEH_SHAPE[20];
extern const uint8_t R_VEH_FLIP[20];
extern const uint8_t R_VEH_DX[20];
extern const int16_t R_VEH_DY[20];

/*  Traffic: XBLD-29 -> car sprite index, and the heavy-traffic variant. */
extern const uint16_t R_UGND_GROUND[128];
extern const uint8_t  R_UGND_ZONE[16];
extern const uint8_t  R_THING_DIVX[4];
extern const uint8_t  R_THING_DIVY[4];
extern const uint8_t  R_DIR_FRAME4[4];
extern const uint8_t  R_DIR_FLIP4[4];
extern const uint8_t  R_ANIM_PERM_A[49];
extern const uint8_t  R_ANIM_PERM_B[15];
extern const uint8_t R_TRAFFIC_CAR[80];
extern const uint8_t R_TRAFFIC_HEAVY[28];

#endif /* R_TABLES_H */
