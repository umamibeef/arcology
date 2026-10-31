/*  solved.c: each interchange's search, kept by its place (solved.h). */
#include "network/solved.h"

#include <SDL3/SDL.h>

#include <stdlib.h>
#include <string.h>

typedef struct
{
    uint64_t id, key;
    int      n;
    double  *data;
} Solved;

static Solved      *s_solved;
static int          s_nsolved, s_capsolved;
static SDL_SpinLock s_lock; /* the interchanges are searched side by side (shape.c) */

int solved_get(uint64_t id, uint64_t key, double *out, int n)
{
    int i, got = 0;
    SDL_LockSpinlock(&s_lock);
    for (i = 0; i < s_nsolved; ++i)
        if (s_solved[i].id == id)
        {
            if (s_solved[i].key == key && s_solved[i].n == n)
                memcpy(out, s_solved[i].data, sizeof *out * (size_t)n), got = 1;
            break;
        }
    SDL_UnlockSpinlock(&s_lock);
    return got;
}

void solved_put(uint64_t id, uint64_t key, const double *data, int n)
{
    int     i;
    double *d = (double *)malloc(sizeof *d * (size_t)(n > 0 ? n : 1));
    if (!d)
        return;
    memcpy(d, data, sizeof *d * (size_t)n);
    SDL_LockSpinlock(&s_lock);
    for (i = 0; i < s_nsolved && s_solved[i].id != id; ++i)
        ;
    if (i == s_nsolved)
    {
        if (s_nsolved == s_capsolved)
        {
            int     cap = s_capsolved ? 2 * s_capsolved : 16;
            Solved *ns  = (Solved *)realloc(s_solved, sizeof *ns * (size_t)cap);
            if (!ns)
            {
                SDL_UnlockSpinlock(&s_lock);
                free(d);
                return;
            }
            s_solved = ns, s_capsolved = cap;
        }
        s_solved[s_nsolved++].data = NULL;
    }
    free(s_solved[i].data);
    s_solved[i].id = id, s_solved[i].key = key, s_solved[i].n = n, s_solved[i].data = d;
    SDL_UnlockSpinlock(&s_lock);
}
