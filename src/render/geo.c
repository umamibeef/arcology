/*  geo.c: the numbers a script tunes the world with, and the one
 *  place they are written.
 *
 *  Every number is the SCRIPTS' (scripts/geo.lua).  There is no list
 *  here and no struct of fields: a name exists from the moment something
 *  sets it.  The C that reads one names it and keeps the place it was
 *  given.
 *
 *  A read is an index into this store, cached at the site that reads it.
 *  So a number costs a bounds test and a load even in a per-frame loop.
 *  That is what lets the traffic and the loft keep their numbers here
 *  rather than in fields of their own.  It is why nothing has to be
 *  declared before a script can invent it. */
#include <stdio.h>
#include <string.h>

#include "pipeline.h"
#define GEO_MAX 512

static char  s_name[GEO_MAX][32];
static float s_val[GEO_MAX];
static int   s_n;

const char *geo_name(int i, float *v)
{
    if (i < 0 || i >= s_n)
        return NULL;
    if (v)
        *v = s_val[i];
    return s_name[i];
}

static int geo_index(const char *name)
{
    int i;
    for (i = 0; name && i < s_n; ++i)
        if (strcmp(s_name[i], name) == 0)
            return i;
    return -1;
}

float geo_num(int *cache, const char *name)
{
    if (*cache < 0)
        *cache = geo_index(name);
    return *cache >= 0 ? s_val[*cache] : 0.0f;
}

int geo_set(const char *name, float v)
{
    int i;
    if (!name || !*name)
        return 0;
    i = geo_index(name);
    if (i >= 0)
    {
        s_val[i] = v;
        return 1;
    }
    if (s_n >= GEO_MAX)
        return 0;
    snprintf(s_name[s_n], sizeof s_name[0], "%s", name);
    s_val[s_n++] = v;
    return 1;
}

/*  ---- THE LOOK'S KNOBS ---------------------------------------------------
 *
 *  The store above is the line works' numbers, written by name from a
 *  script.  These are the handful the TUNING WINDOW moves while the city
 *  is running: a family holds a pointer at its width and its radii.  So
 *  each has to keep its own float rather than a slot in a table that may
 *  move.  Both are numbers and both live here.  Nothing about them is
 *  the line's, which is why neither is in a family's file. */
/*  The look's knobs.  Named, not counted, for the reason the line works'
 *  numbers are.  A list of nineteen bare floats orders itself by
 *  position, and a field moved hands every later number to its neighbor
 *  without a word. */
RTune s_tune = {
    .line_w       = 0.50f,
    .thread_w       = 0.62f,
    .line_rmin    = 0.90f,
    .thread_rmin    = 3.00f,
    .line_rmax    = 6.00f,
    .thread_rmax    = 8.00f,
    .approach     = 0.80f,
    .margin       = 0.04f,
    .trim_cap     = 0.32f, /* a box is the meet and its lip returns, not the tile */
    .show_curves  = 0.00f,
    .band_w      = 0.60f,
    .band_rmin   = 2.50f,
    .band_rmax   = 8.00f,
    .band_reach  = 1.60f,
    .band_stair  = 2.00f,
    .corner_share = 0.50f,
    .spur_merge   = 2.50f,
    .band_grade  = 0.10f,
    .band_stiff  = 2.00f,
};

/*  And by name, so a script and a report reach them the way the line
 *  works' numbers are reached.  Each says where it sits rather than
 *  being counted into place. */
static const struct
{
    const char *name;
    size_t      at;
    float       lo, hi;
} TUNE[] = {
    {"line_w",        offsetof(RTune, line_w), 0.15f, 1.00f},
    {"thread_w",        offsetof(RTune, thread_w), 0.15f, 1.00f},
    {"line_rmin",     offsetof(RTune, line_rmin), 0.05f, 4.00f},
    {"thread_rmin",     offsetof(RTune, thread_rmin), 0.05f, 8.00f},
    {"line_rmax",     offsetof(RTune, line_rmax), 0.50f, 12.00f},
    {"thread_rmax",     offsetof(RTune, thread_rmax), 0.50f, 16.00f},
    {"approach",      offsetof(RTune, approach), 0.00f, 2.00f},
    {"margin",        offsetof(RTune, margin), 0.00f, 0.30f},
    {"trim_cap",      offsetof(RTune, trim_cap), 0.10f, 0.60f},
    {"show_curves",   offsetof(RTune, show_curves), 0.00f, 1.00f},
    {"band_w",       offsetof(RTune, band_w), 0.30f, 1.00f},
    {"band_rmin",    offsetof(RTune, band_rmin), 0.50f, 6.00f},
    {"band_rmax",    offsetof(RTune, band_rmax), 1.00f, 16.00f},
    {"band_reach",   offsetof(RTune, band_reach), 0.50f, 3.00f},
    {"band_stair",   offsetof(RTune, band_stair), 0.00f, 6.00f},
    {"corner_share",  offsetof(RTune, corner_share), 0.25f, 1.00f},
    {"spur_merge",    offsetof(RTune, spur_merge), 0.00f, 4.00f},
    {"band_grade",   offsetof(RTune, band_grade), 0.02f, 0.50f},
    {"band_stiff",   offsetof(RTune, band_stiff), 0.00f, 8.00f},
};

int tune_set(const char *name, float v)
{
    size_t i;
    for (i = 0; i < sizeof TUNE / sizeof TUNE[0]; ++i)
    {
        if (!name || strcmp(name, TUNE[i].name) != 0)
            continue;
        if (v < TUNE[i].lo)
            v = TUNE[i].lo;
        if (v > TUNE[i].hi)
            v = TUNE[i].hi;
        *(float *)((char *)&s_tune + TUNE[i].at) = v;
        return 1;
    }
    return 0;
}

/*  A knob's own float, to keep.  A family holds pointers at its width
 *  and its radii.  So a strip drawn a year later reads the value the
 *  window is showing rather than the one the declaration was read at. */
const float *tune_at(const char *name)
{
    size_t i;
    for (i = 0; name && i < sizeof TUNE / sizeof TUNE[0]; ++i)
        if (strcmp(name, TUNE[i].name) == 0)
            return (const float *)((const char *)&s_tune + TUNE[i].at);
    return NULL;
}

const char *tune_name(int i, float *v)
{
    if (i < 0 || (size_t)i >= sizeof TUNE / sizeof TUNE[0])
        return NULL;
    if (v)
        *v = *(const float *)((const char *)&s_tune + TUNE[i].at);
    return TUNE[i].name;
}

float *tune_array(void)
{
    return &s_tune.line_w; /* nineteen floats, in the struct's own order */
}
