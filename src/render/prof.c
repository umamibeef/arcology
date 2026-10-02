/*  prof.c: see prof.h. */
#include <string.h>
#include <time.h>

#include "prof.h"
#include "dump.h"
#include "opt.h"

static double s_prof[PROF_N];

void prof_add(int stage, double amount)
{
    s_prof[stage] += amount;
}

double prof_now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1e3 + (double)ts.tv_nsec / 1e6;
}

void prof_reset(void)
{
    memset(s_prof, 0, sizeof s_prof);
}

void prof_print(void)
{
    if (!g_dev.times)
        return;
    dumpf("time    segments: walk+fit %.1f, trims %.1f, lanes %.1f, overlay %.1f, caps %.1f | loft: sample %.1f (%d stations, %d lofts from the cache, %d sampled), ground %.1f, profile %.1f, slab works %.1f, record %.1f, slab %.1f (of which slabs and spurs %.1f) | junctions: lanes %.1f, box %.1f ms\n",
          s_prof[0],
          s_prof[1],
          s_prof[2],
          s_prof[3],
          s_prof[4],
          s_prof[5],
          (int)s_prof[13],
          (int)s_prof[14],
          (int)s_prof[15],
          s_prof[16],
          s_prof[6],
          s_prof[7],
          s_prof[8],
          s_prof[9],
          s_prof[10],
          s_prof[11],
          s_prof[12]);
    dumpf("time    bands: free air %.1f, bands %.1f, tints %.1f, spurs %.1f (of which their lofts %.1f), transitions %.1f, lane check %.1f ms\n",
          s_prof[PROF_BAND_AIR],
          s_prof[PROF_BANDS],
          s_prof[PROF_BAND_TINT],
          s_prof[PROF_BAND_SPURS],
          s_prof[PROF_SPUR_LOFT],
          s_prof[PROF_BAND_TRANS],
          s_prof[PROF_BAND_CHECK]);
}
