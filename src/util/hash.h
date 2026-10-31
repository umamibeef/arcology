/*  hash.h: one 64-bit hash of a run of bytes, for a NAME that says where
 *  a thing is.  A way, a piece and a sweep are named by the bytes that
 *  place them.  So the same place gives the same name in every build.
 *  Start from HASH_SEED, and hand the last answer on to hash the next
 *  run after it. */
#ifndef ARC_HASH_H
#define ARC_HASH_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define HASH_SEED 14695981039346656037ull

static inline uint64_t hash_bytes(uint64_t h, const void *p, size_t n)
{
    const uint8_t *b = (const uint8_t *)p;
    size_t         i = 0;
    /*  Eight bytes at a time, each word mixed in by a multiply and a
     *  shift, and the bytes left over one at a time. */
    for (; i + 8u <= n; i += 8u)
    {
        uint64_t w;
        memcpy(&w, b + i, sizeof w);
        h ^= w;
        h *= 0x9E3779B97F4A7C15ull;
        h ^= h >> 29;
    }
    for (; i < n; ++i)
        h = (h ^ b[i]) * 1099511628211ull;
    return h;
}

#endif /* ARC_HASH_H */
