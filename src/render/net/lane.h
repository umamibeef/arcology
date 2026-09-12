/*  net/lane.h: what lane.c answers for.
 *
 *  the lane model: the lanes, their ends, and what is within reach of a point.
 *
 *  Nothing declared here decides anything. */
#ifndef ARC_NET_LANE_H
#define ARC_NET_LANE_H

#include "net/types.h"

float arm_cut(Family f, int col, int row, int e); /* how far from its mouth an arm's strip is cut, as the segment is cut (lane.c) */
/*  A lane's open end carried on into the facing lane across a meet.  The
 *  ends that could take one, how a candidate lies, and the join. */
int   xlane_end(const XLaneFan *x, int i);
int   xlane_measure(const XLaneFan *x, int la, int lb, float *off, float *dot, float *ahead, float *aside, float *dist);
void  xlane_merge(XLaneFan *x, int la, int lb);
int   xlane_link(XLaneFan *x, int la, int lb);
/*  THE BAND'S LANE ENDS, as the script that joins them sees them
 *  (net/lane.c).  Which slab lane goes on to which is the script's.
 *  This offers the lanes, their end poses, a station back along one, the
 *  biarc between two poses, and the laying of a link. */
void *net_links_fan(RMesh *m, const RCity *c, uint8_t mask_bit);
int   net_links_count(void);
int   net_links_lane(int i, int *slab, int *line, int *band, float *off, float *w, float *len, int *open0, int *open1);
int   net_links_pose(int i, int which, float *x, float *y, float *dx, float *dy);
int   net_links_station(int i, int which, float back, float *x, float *y, float *dx, float *dy);
int   net_links_add(const Piece *pc, int np, float w, int from, int to, int band);
void  net_links_note(const char *what, int n);
/*  lane.c: lanes as primitives: the router, and the lanes at an
 *  intersection (docs/future.rst, "Lanes as primitives"). */
void lane_reset(void);
/*  A junction's connectors, in the drive's two halves.  The chains run
 *  from every inbound lane to every outbound lane of the other arms,
 *  queued for the cut.  Laid once the script has cut them. */
void lane_junction_ask(RMesh *m, const RCity *c, uint8_t mask_bit, Family f, int32_t col, int32_t row, int links);
int  lane_junction_take(void);
/*  The arms of the junction in hand, for arc.rules.turns: which of them
 *  joins which is the rule's.  There is no matcher in C behind it. */
int  lane_turns_ask(void);
void lane_turns_info(int *col, int *row, int *arms, const char **fam);
int  lane_turns_arm(int e, int *lanes, int *into, int *out, int *spur);
int  lane_turns_want(int e, int k, int e2, int k2);
/*  A segment's dead ends' caps, once the drive has cut the chains
 *  lane_segment queued for them. */
int  lane_segment_caps(void);
/*  A segment's dead ends, for arc.rules.cap: the lane arriving at each
 *  and the lane leaving it, and the two ways they may be joined. */
int  lane_caps_ask(void);
void lane_caps_info(int *n, const char **ends, const char **fam);
int  lane_caps_at(int i, int *end, int *lane, int *from, int *to);
int  lane_caps_merge(int i);
int  lane_caps_link(int i);
int  lane_segment(RMesh *m, const RCity *c, uint8_t mask_bit, Family f, const Piece *pc, int np, int32_t col, int32_t row, int e, int kind0, int32_t cc, int32_t cr, int back, int kind1, float hw, int cls);
void lane_stats_print(void);
void lane_dump_pieces(const Piece *pc, int np);
int  lane_slab(RMesh *m, const RCity *c, uint8_t mask_bit, const Piece *pc, int np, float hw, int band);
int  lane_snap_ask(V2 p, V2 dir, float maxd);
int  lane_snap_count(void);
int  lane_snap_at(int i, int *lane, int *cls, int *band, float *off, float *dist, float *dot, float *x, float *y, float *dx, float *dy);
void lane_snap_is(int i);
int  lane_snap_take(V2 *pos, V2 *odir, float *dist);
/*  Where a family's lanes run at that class, from the centerline, inner
 *  first: arc.rules.lanes's answer.  Answers how many were written. */
void net_lane_runs_reset(void);
int  net_lane_runs(void);
void net_lane_run_at(int i, const char **fam, int *cls);
void net_lane_run_is(int i, const float *off, int n);
/*  The slab's own surface near a point, for a line that belongs to a
 *  band rather than to the ground (net/lane.c). */
float slab_z_near(const RCity *c, uint8_t mask_bit, int band, V2 p);
const BandSpur *lane_spur_tile(int32_t col, int32_t row); /* the spur at a tile, or NULL */
int           lane_spur(RMesh *m, const RCity *c, uint8_t mask_bit, const Piece *pc, int np, float hw, int slab_lane, int line_lane, int line_port, int off);
int           lane_port_id(int col, int row, int e, int out, int k);
int           lane_table_count(void);                                                       /* the lanes as built: spurs, connectors, slab lanes */
int           lane_table_get(int i, int *cls, int *fam, const Piece **pc, int *np, float *w);
void          lane_check_ends(void);
XLaneFan     *lane_cross_ask(RMesh *m, const RCity *c, uint8_t mask_bit);
int           lane_cross_take(void);
int           net_wires(void);
LaneFan      *net_wire_at(int i);
void          net_wire_done(int i);
int   lane_port(Family f, int col, int row, int e, int out, V2 *pos, V2 *dir);      /* a junction port's pose (lane.c port_pose) */

#endif /* ARC_NET_LANE_H */
