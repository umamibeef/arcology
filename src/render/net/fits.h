/*  net/fits.h: THE FIT ARENA, a store.
 *  Everything the drive fits is an ASK: a segment's corridor of tiles, or a
 *  chain of points with a corridor of the caller's own.  An ask that may
 *  leave its cells is put twice, once HELD to them and once FREE.  Which of
 *  the two is kept is arc.rules.fit_choice's.  The arena holds the asks and
 *  runs each through the fit when the drive says.  It keeps the two
 *  candidates and their scores while the choice waits, and hands the kept
 *  path back.
 *
 *  Two arenas: the lines' (every segment of every line family, asked before
 *  anything is fitted) and the band's (one band at a time).  One
 *  implementation serves both.  The lines' kept paths are filed by segment
 *  and queued to be cut here as well.  So a segment reads its path and its
 *  pieces back by its own cell. */
#ifndef ARC_NET_FITS_H
#define ARC_NET_FITS_H

#include "pipeline.h"
#include "mesh/mesh.h"
#include "net/types.h"

typedef struct FitArena FitArena;

FitArena *fits_lines(void);
FitArena *fits_band(void);

/*  Empty the arena.  The lines' also empties the kept store. */
void fits_reset(FitArena *a);
/*  One segment's corridor of tiles, put once (cand 0) or as the held
 *  (1) and the free (2) way.  Answers 0, or -1 with the reason reported
 *  where the arena is full. */
int  fits_add_tiles(FitArena *a, const NetFamily *fam, Family f, int32_t col, int32_t row, int e, const Seg *x, int cand, int free_reach);
/*  A chain of points with the band's corridor, as the held (1) or the
 *  free (2) way.  `bucket` is the tally the fit counts into. */
int  fits_add_points(FitArena *a, const RCity *c, const int32_t *own, int n_own, const uint8_t *own_mask, const V2 *chain, int nc, V2 b0, V2 b1, int cand, int bucket, const char *name);
int  fits_count(const FitArena *a);
/*  One ask set up and its lines found, ready for the drive to walk its
 *  boundaries.  Answers how many boundaries there are. */
int  fits_begin(FitArena *a, const RCity *c, int i);
/*  The path the ask settled, kept or held aside.  Answers 1 when a
 *  choice between the two ways now waits. */
int  fits_done(FitArena *a, int i);
/*  The choice waiting, if any: the two scores and the name the rule is
 *  handed. */
const char *fits_choice(const FitArena *a, const int **free_, const int **held);
void fits_choice_is(FitArena *a, int keep_free);
/*  The kept way of a points ask, into the caller's arrays.  Answers how
 *  many points, 0 where nothing was kept. */
int  fits_take(const FitArena *a, V2 *q, float *rad, float *tlim);

/*  The lines' kept store, read by the segment's own cell. */
int  net_seg_fit_of(Family f, int32_t col, int32_t row, int e, V2 *q, float *rad, float *tlim, int cap);
int  net_seg_pieces_of(Family f, int32_t col, int32_t row, int e, Piece *out, int cap, int *count);

/*  The lines' arena, as the script drives it. */
int         net_fits(void);
int         net_fit_begin(const RCity *c, int i);
int         net_fit_done(int i);
const char *net_fit_choice(const int **free_, const int **held);
void        net_fit_choice_is(int keep_free);

#endif /* ARC_NET_FITS_H */
