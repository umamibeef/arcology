/*  net/spur.h: what spur.c answers for.
 *
 *  the spurs: every spur tile, and the pose each was given.
 *
 *  Nothing declared here decides anything. */
#ifndef ARC_NET_SPUR_H
#define ARC_NET_SPUR_H

#include "net/types.h"

/*  One placing of a spur's join, as far as the CHAIN it is cut from,
 *  and the placing built from the pieces the script cut. */
const char *band_slide_chain(SlideFan *s, float u, float at, V2 *q, float *rad, float *tlim, int *n);
float       band_slide_routed(SlideFan *s, const Piece *pc, int np);
int         band_slide_exits(SlideFan *s);
void        band_slide_keep(SlideFan *s, float taper);
void        band_slide_note(const SlideFan *s, int tried, int off, int unroutable, int missed);
/*  How an on-spur reads the four sides around it: what each neighbor is,
 *  and the sides it settles on. */
int   band_orient_links(const OrientFan *o, int32_t col, int32_t row);
void  band_orient_answer(OrientFan *o, int kind, int dside, int rside, int eside, int off, int lines);
void  band_orient_spur(OrientFan *o, const OrientFan *r);
void spur_stats(void);
int   net_spurs_begin(const RCity *c, const RAtlasLevel *l);
int   net_spur_at(int32_t col, int32_t row);
OrientFan *net_spur_current(void);
int  net_spur_spans(void);
int  net_spur_span_at(int i, float *at, int *len, int *leaves, int *sgn);
void net_spur_span_is(int i, int have, float top, float foot, float total, float ds);
int  net_spur_shares(void);
int  net_spur_share_at(int k, float *gap, int *cap);
void net_spur_share_is(int k, int half);
void build_spurs_begin(RMesh *m, const RCity *c, const RAtlasLevel *l, uint8_t mask_bit, int comp);
int  build_spur_next(void);
int  build_spur_routed(void);
int  build_spur_joined(void);
int  build_spur_slid(void);
int  build_spur_done(void);
SlideFan *net_spur_slide(void);
int  build_spur_lofts(void);
int  build_spur_loft(int i);
int  band_slide_snap(SlideFan *s, float at);
/*  Where a spur aims on the line it comes down to, for arc.rules.spur_target:
 *  the reading, and the point, tangent and travel it answers with. */
int  build_spur_target(void);
int  net_spur_target_at(int *fork, int *off, float *rdx, float *rdy, float *mdx, float *mdy,
                        float *fx, float *fy, float *lane_off, float *merge_along);
void net_spur_target_is(float bx, float by, float tbx, float tby, float tvx, float tvy);
void          band_lanes(const RCity *c);

#endif /* ARC_NET_SPUR_H */
