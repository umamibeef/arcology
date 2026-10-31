/*  solved.h: each interchange's search, kept by its place once it is
 *  solved.  The search of an interchange is a function of what it starts
 *  from: its places, their bounds and the samples of its roads.  So a
 *  build that meets an interchange it has met before, starting from the
 *  same, takes the layout found then instead of searching again.  There
 *  is one entry an interchange, by its id, and a search from anything
 *  else replaces it. */
#ifndef NET_SOLVED_H
#define NET_SOLVED_H

#include <stdint.h>

/*  The layout kept for interchange `id` searched from `key`, `n` numbers
 *  into `out`.  1 where it is kept, else 0. */
int solved_get(uint64_t id, uint64_t key, double *out, int n);

/*  Keep the layout of interchange `id`, searched from `key`. */
void solved_put(uint64_t id, uint64_t key, const double *data, int n);

#endif
