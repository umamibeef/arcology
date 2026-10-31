/*  grow.h: room in a growing array.  An array of `size`-byte items in *p,
 *  with room for *cap of them, gets room for at least `need`.  The room
 *  doubles, from 1024.  0 on success, -1 with the array as it was where
 *  there is no memory. */
#ifndef ARC_GROW_H
#define ARC_GROW_H

#include <stdlib.h>

static inline int arc_grow(void **p, int *cap, int need, size_t size)
{
    int   n = *cap ? *cap : 1024;
    void *q;
    if (need <= *cap)
        return 0;
    while (n < need)
        n *= 2;
    q = realloc(*p, size * (size_t)n);
    if (!q)
        return -1;
    *p   = q;
    *cap = n;
    return 0;
}

#endif /* ARC_GROW_H */
