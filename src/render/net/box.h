/*  net/box.h: what box.c answers for.
 *
 *  the box a node draws on its outline, and the edge a strip records.
 *
 *  Nothing declared here decides anything. */
#ifndef ARC_NET_BOX_H
#define ARC_NET_BOX_H

#include "net/types.h"

float line_class(const RCity *c, int32_t col, int32_t row);
/*  A junction's arms, for the rule that stands its signs. */
int   net_junction_signs_ask(void);
int   net_junction_signs_at(int e, int *ctrl, float *h, int32_t *col, int32_t *row);
float net_junction_signs_order(void);
void  net_junction_signs_taken(void);
void  net_junction_signs_enter(void);
void  net_junction_signs_leave(void);
/*  How many cars a tile's traffic is worth, for every byte there is. */
void net_line_class_is(int tv, int cls);
/*  The same, for the box the drive is drawing: the ring, the mouths off
 *  it, and the fill laid inside the answer. */
void *net_junction_band(void);
int   net_junction_mouths(void);
int   net_junction_band_done(void);
JuncFan *net_junction_fan(void); /* the outline the drive lays a junction's fill on (line.c) */
int  net_box_paving_ask(void);
int  net_box_paving_done(void);

#endif /* ARC_NET_BOX_H */
