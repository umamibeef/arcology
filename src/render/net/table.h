/*  net/table.h: what table.c answers for.
 *
 *  the segment table: every segment the grading pass fitted, replayed by the building pass.
 *
 *  Nothing declared here decides anything. */
#ifndef ARC_NET_TABLE_H
#define ARC_NET_TABLE_H

#include "net/types.h"

void seg_table_reset(void); /* net/table.c: the segment table, kept across the two passes */
void net_table_free(void); /* net/table.c: the sample arenas, once, at exit */
int  seg_table_count(void);
int  seg_table_replay(RMesh *m, const RCity *c, const RAtlasLevel *l, uint8_t mask_bit, int comp, uint8_t *visited);
int  seg_table_get(int i, int32_t *col, int32_t *row, int32_t *cc, int32_t *cr, const int32_t **tcol, const int32_t **trow, int *nt);
int  seg_table_nodes(int i, const V2 **q, const float **rad, int *nk); /* net/table.c: a segment's or band's fitted nodes and radii */
/*  A band in the table.  The grading pass stores it from its walk and
 *  fit.  Its own tiles are its corridor, and its start cell and way its
 *  key.  It is replayed by the building pass (band.c). */
int         seg_store_band(int32_t col, int32_t row, int ew, int sign, const Piece *pc, int np, const V2 *q, const float *rad, const float *tlim, int nk, const int32_t *own, int n_own);
const RSeg *seg_table_entry(int i);
int         seg_table_unchanged(int i);   /* its pieces hash as the previous build's did */
void        seg_table_misses_print(void); /* the station cache's misses this pass, by reason, under --times */
int         seg_table_band_index(int k);  /* the k-th band's entry, in walk order */
void        seg_table_arenas(const RSeg *r, const Piece **pc, const V2 **q, const float **rad, const float **tlim, const int32_t **tcol, const int32_t **trow);
/* ---- net/table.c: the segment table and the sample cache */
extern RSeg    s_segs[];
extern int32_t s_seg_at[];
uint64_t       pieces_hash(const Piece *pc, int np, float total);
int            loft_cached(Loft *x);
void           loft_keep(const Loft *x);
int            seg_store(const Seg *x);
void           seg_load(Seg *x, const RSeg *r);
int   seg_table_pieces_from(int32_t col, int32_t row, int e, Piece *out, int cap, int *np); /* a segment's fitted pieces from one end (table.c) */
int    seg_class(Seg *x);
void   seg_class_counts(const Seg *x, int cnt[3]);

#endif /* ARC_NET_TABLE_H */
