/*  net/cut.h: what cut.c answers for.
 *
 *  the cut queue: the chains a pass hands over, and the pieces that come back.
 *
 *  Nothing declared here decides anything. */
#ifndef ARC_NET_CUT_H
#define ARC_NET_CUT_H

#include "net/types.h"

/*  THE CUT QUEUE (mesh/fit.c).  Cutting a path into pieces is
 *  arc.rules.pieces's and only the drive may ask for it.  So a pass that
 *  needs one queues the chain and reads the pieces back after the drive
 *  has been round. */
void net_cut_reset(void);
int  net_cut_add(const V2 *q, int n, const float *rad, const float *tlim);
int  net_cut_add_poses(V2 A, V2 tA, V2 B, V2 tB);
int  net_cut_points(void *fan, const V2 *q, int n, const float *rad, const float *tlim);
int  net_cuts(void);
int  net_cut_full(void);
void *net_cut_at(int i);
void net_cut_done(int i);
int  net_cut_pieces(int i, Piece *out, int cap, int *count);

#endif /* ARC_NET_CUT_H */
