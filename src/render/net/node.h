/*  net/node.h: what node.c answers for.
 *
 *  a node's outline and its arms' trims.
 *
 *  Nothing declared here decides anything. */
#ifndef ARC_NET_NODE_H
#define ARC_NET_NODE_H

#include "net/types.h"

/*  The context walk_segment sets for the segment it is drawing, read by
 *  the loft, the junction box and the passes (walk/walk.c owns them). */
extern uint8_t        s_junc_ctrl[R_MAP * R_MAP]; /* net/node.c: per junction tile, two bits per arm: 0 none, 1 stop, 2 signal */
/*  A junction's outline must be a simple ring: no vertex where it
 *  doubles back, no two edges meet.  Counted for every junction a build
 *  lays and reported by the mesh check.  Neither fault may exist. */
void junction_outline_reset(void);
/*  A corner the outline pulled in for standing too far out, counted. */
void junction_outline_clamped(float len);
void junction_outline_print(void);
int  junction_outline_faults(void);
int junction_poly(const RCity *c, Family f, int32_t col, int32_t row, int links, V2 *out, uint8_t *mouth, int max, float trim[4], JuncArm arms[4]);
int build_junction_at(const char *where, const char *who, RMesh *m, const RCity *c, uint8_t mask_bit, Family f, int32_t col, int32_t row, int links, float order);
int            build_junction_done(void);
float               junc_surface(Family f, const RCity *c, uint8_t mask_bit, int col, int row, float x, float y, float zj);
int            net_box_loft_add(const JBox *jb, const RLoft *d, const Piece *pc, int np, float total);
int            net_box_lofts(void);
int            net_box_loft(int i);
JBox *net_junction_box_now(void);
int  net_junction_composing(void);
int  net_junction_finishing(void);
int            build_junction_lanes(void);
void           junction_rings_reset(void);
void           junction_ring_keep(const OutlineFan *o);
int            junction_ask(const RCity *c, Family f, int32_t col, int32_t row, int links, OutlineFan *o, V2 *out, uint8_t *mouth, float *trim);
/*  What stage three measured and left for the drive to have answered:
 *  each junction's control, and each mouth's stripe depth. */
void           net_control_asks_reset(void);
void           net_xwalk_asks_reset(void);
void           net_control_ask(const char *rule, int32_t col, int32_t row, int links, const int *cls, const int *traf, int busy);
int            net_control_asked(void);
const char    *net_control_ask_at(int i, int32_t *col, int32_t *row, int *links, const int **cls, const int **traf, int *busy);
void           net_control_is(int i, int ctrl);
void           net_xwalk_ask(int32_t col, int32_t row, int e, int fx, int ctrl, float want, float room, float straight, float cap);
int            net_xwalk_asked(void);
int            net_xwalk_ask_at(int i, int32_t *col, int32_t *row, int *e, int *ctrl, float *want, float *room, float *straight);
void           net_xwalk_deep(int i, float d);
/* ---- net/node.c: the junction outline and box */
void arm_heading(const Piece *pc, int np, float total, int from_end, V2 *pos, V2 *dir);

#endif /* ARC_NET_NODE_H */
