/*  net/strip.h: what loft.c answers for.
 *
 *  the eleven moments the drive stops at while one strip is lofted.
 *
 *  Nothing declared here decides anything. */
#ifndef ARC_NET_STRIP_H
#define ARC_NET_STRIP_H

#include "net/types.h"

float width_factor(float dx, float dy, int compensate);
/*  Answers how many faces it laid, or -1 where the mesh would not take
 *  them. */
int loft_sweep(RMesh *m, const RCity *c, uint8_t mask_bit, const Piece *pc, int np, const LoftRung *sec, int nsec, const LoftSweep *how);
/*  The strip the loft worked out, and the slab laid over it.  The loft
 *  stops before the slab, so what is drawn over the stations is settled
 *  outside it.  Net_loft_compose is that, for the callers still in C. */
Loft       *net_loft_strip(void);
const char *net_loft_taper(void);
extern int     s_slab_ready;
extern uint32_t s_slab_sh;
void  net_stage_hand(void *obj, const char *kind);
void *net_stage_taken(const char **kind);
const char *net_loft_profile(GroundFan **g);
const char *net_loft_dropped(void);
const char *net_loft_works(void);
const char *net_loft_record(void);
const char *net_loft_furniture(void);
int         net_loft_recorded(void);
Loft       *net_loft_working(void);
Loft       *net_loft_curves(void);
int         net_loft_close(void);
void net_strip_laps(Loft *x);
int                     loft_furniture(Loft *x); /* the furniture pass: the family's, under the switch (net/strip.c) */

#endif /* ARC_NET_STRIP_H */
