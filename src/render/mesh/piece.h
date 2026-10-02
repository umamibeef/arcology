/*  mesh/piece.h: the arithmetic over one piece, and over a chain of
 *  them.  Nothing here knows what a piece is for. */
#ifndef ARC_MESH_PIECE_H
#define ARC_MESH_PIECE_H

#include "pipeline.h"

/*  The chain the router lays between two poses.  Two points where B is
 *  dead ahead of A, else the equal-tangent biarc's four.  Cutting it
 *  into pieces is arc.rules.pieces's and happens elsewhere. */
/*  ---- THE PIECE (mesh/piece.c) ------------------------------------------
 *
 *  A straight or an arc, and the general arithmetic over one.  Nothing
 *  here knows what a piece is for. */
V2   l_right(V2 d);

void piece_offset(const Piece *p, float off, Piece *o);

void pieces_reverse(Piece *p, int np);

int  pieces_cut(const Piece *pc, int np, float s0, float s1, Piece *out);

void extent_add(float x, float y, float *x0, float *y0, float *x1, float *y1);

void piece_extent(const Piece *pc, float *x0, float *y0, float *x1, float *y1);

void piece_near(const Piece *pc, V2 p, V2 *q, V2 *dq);

void  piece_at(const Piece *p, float t, V2 *pos, V2 *dir);

void  pieces_at(const Piece *pc, int np, float t, V2 *pos, V2 *dir); /* along a chain, clamped */

#endif /* ARC_MESH_PIECE_H */
