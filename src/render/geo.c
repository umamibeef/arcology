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
#include <stddef.h>
#include <string.h>

#include "log.h"
#include "pipeline.h"
#define GEO_MAX 512
/*  Where each name sits, by its hash.  A script reads a number by name
 *  through arc.geo, and a rule that runs per tile reads several of
 *  them.  A walk of the whole store at each read would cost more than
 *  the number is worth.  The table is twice the store, so a probe ends within a few
 *  steps.  A slot holds ONE PAST the place, since nought is empty.  A
 *  name is never taken away, so a probe needs no mark for a hole. */
#define GEO_SLOTS 1024
/*  A probe walks until it finds an empty slot, so the table must always
 *  hold one.  This says so at compile time: raising GEO_MAX past the
 *  slots would turn a miss into a loop that never ends. */
typedef char geo_slots_hold_the_store[GEO_SLOTS > GEO_MAX ? 1 : -1];

static char     s_name[GEO_MAX][32];
static float    s_val[GEO_MAX];
static uint16_t s_slot[GEO_SLOTS];
static int      s_n;

const char *geo_name(int i, float *v)
{
    if (i < 0 || i >= s_n)
        return NULL;
    if (v)
        *v = s_val[i];
    return s_name[i];
}

/*  FNV-1a over the name.  Any spread does: the names are a few hundred
 *  words a person wrote, and the table is half empty. */
static uint32_t geo_hash(const char *s)
{
    uint32_t h = 2166136261u;
    while (*s)
        h = (h ^ (uint32_t)(unsigned char)*s++) * 16777619u;
    return h;
}

/*  The slot a name holds, or the empty slot it would take.  Answers
 *  where, and puts the place it found in `at`, which is -1 for a name
 *  the store does not hold. */
static uint32_t geo_probe(const char *name, int *at)
{
    uint32_t h = geo_hash(name) & (GEO_SLOTS - 1u);
    for (;; h = (h + 1u) & (GEO_SLOTS - 1u))
    {
        int k = (int)s_slot[h] - 1;
        if (k < 0)
            return *at = -1, h;
        if (strcmp(s_name[k], name) == 0)
            return *at = k, h;
    }
}

static int geo_index(const char *name)
{
    int at;
    if (!name)
        return -1;
    geo_probe(name, &at);
    return at;
}

int geo_find(const char *name, float *v)
{
    int i = geo_index(name);
    if (i < 0)
        return 0;
    if (v)
        *v = s_val[i];
    return 1;
}

float geo_num(int *cache, const char *name)
{
    if (*cache < 0)
        *cache = geo_index(name);
    return *cache >= 0 ? s_val[*cache] : 0.0f;
}

int geo_set(const char *name, float v)
{
    int      at;
    uint32_t h;
    if (!name || !*name)
        return 0;
    h = geo_probe(name, &at);
    if (at >= 0)
    {
        s_val[at] = v;
        return 1;
    }
    if (s_n >= GEO_MAX)
        return 0;
    /*  A name the store cannot hold whole would never be found again,
     *  and would take a fresh place at every reading. */
    if (strlen(name) >= sizeof s_name[0])
    {
        R_WARN("geo", "the name %s is longer than %d characters and cannot be kept", name, (int)sizeof s_name[0] - 1);
        return 0;
    }
    snprintf(s_name[s_n], sizeof s_name[0], "%s", name);
    s_val[s_n] = v;
    s_slot[h]  = (uint16_t)(s_n + 1);
    ++s_n;
    return 1;
}

/*  ---- THE LOOK'S KNOBS ---------------------------------------------------
 *
 *  The store above is the line works' numbers, set by name from a
 *  script.  A KNOB is one of the handful the TUNING WINDOW moves while
 *  the city is running.  It carries more than a value: a range to hold
 *  it to, the value it starts at, and what the window calls it.
 *
 *  The scripts declare every one (scripts/tune.lua).  There is no list
 *  of them here, and none in the window either, so a knob is added in
 *  one place.
 *
 *  A knob keeps its own float at an address that never moves, because a
 *  family holds a pointer at its width and its radii.  So a strip drawn
 *  a year later reads the value the window is showing rather than the
 *  one the declaration was read at. */
static struct
{
    char  name[24];
    char  label[32];
    char  group[24];
    float v, lo, hi, def;
} s_knob[ARC_TUNE_MAX];
static int s_nknob;

static float clamp_to(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static int knob_at(const char *name)
{
    int i;
    for (i = 0; name && i < s_nknob; ++i)
        if (strcmp(s_knob[i].name, name) == 0)
            return i;
    return -1;
}

int tune_declare(const RKnob *k)
{
    int at;
    if (!k || !k->name || !*k->name)
        return 0;
    if (strlen(k->name) >= sizeof s_knob[0].name)
    {
        R_WARN("geo", "the knob %s is longer than %d characters and cannot be kept", k->name, (int)sizeof s_knob[0].name - 1);
        return 0;
    }
    at = knob_at(k->name);
    if (at < 0)
    {
        if (s_nknob >= ARC_TUNE_MAX)
        {
            R_WARN("geo", "the knob %s: the store holds only %d", k->name, ARC_TUNE_MAX);
            return 0;
        }
        at          = s_nknob++;
        s_knob[at].v = k->def; /* only the first reading sets the value */
        snprintf(s_knob[at].name, sizeof s_knob[0].name, "%s", k->name);
    }
    snprintf(s_knob[at].label, sizeof s_knob[0].label, "%s", k->label ? k->label : "");
    snprintf(s_knob[at].group, sizeof s_knob[0].group, "%s", k->group ? k->group : "");
    s_knob[at].lo  = k->lo;
    s_knob[at].hi  = k->hi;
    s_knob[at].def = clamp_to(k->def, k->lo, k->hi);
    s_knob[at].v   = clamp_to(s_knob[at].v, k->lo, k->hi);
    return 1;
}

int tune_count(void)
{
    return s_nknob;
}

int tune_knob(int i, RKnob *out)
{
    if (i < 0 || i >= s_nknob || !out)
        return 0;
    out->name  = s_knob[i].name;
    out->label = s_knob[i].label[0] ? s_knob[i].label : NULL;
    out->group = s_knob[i].group;
    out->v     = s_knob[i].v;
    out->lo    = s_knob[i].lo;
    out->hi    = s_knob[i].hi;
    out->def   = s_knob[i].def;
    return 1;
}

int tune_set_at(int i, float v)
{
    if (i < 0 || i >= s_nknob)
        return 0;
    s_knob[i].v = clamp_to(v, s_knob[i].lo, s_knob[i].hi);
    return 1;
}

int tune_set(const char *name, float v)
{
    return tune_set_at(knob_at(name), v);
}

const float *tune_at(const char *name)
{
    int i = knob_at(name);
    return i < 0 ? NULL : &s_knob[i].v;
}

int tune_find(const char *name, float *v)
{
    const float *p = tune_at(name);
    if (!p)
        return 0;
    if (v)
        *v = *p;
    return 1;
}

const char *tune_name(int i, float *v)
{
    if (i < 0 || i >= s_nknob)
        return NULL;
    if (v)
        *v = s_knob[i].v;
    return s_knob[i].name;
}

int tune_values(float *out, int max)
{
    int i, n = s_nknob < max ? s_nknob : max;
    for (i = 0; i < n; ++i)
        out[i] = s_knob[i].v;
    return n;
}

/*  What an unbound knob reads as.  A build before the scripts are read
 *  draws nothing, and this keeps such a read from being a crash. */
static const float s_nought = 0.0f;

/*  Every pointer is the same value here, so nothing is ordered by
 *  position: tune_bind names each field beside the knob it wants. */
RTuned s_tuned = {
    &s_nought, &s_nought, &s_nought, &s_nought, &s_nought, &s_nought, &s_nought, &s_nought,
    &s_nought, &s_nought, &s_nought, &s_nought, &s_nought, &s_nought, &s_nought, &s_nought,
    &s_nought,
};

int tune_bind(void)
{
    int missing = 0;
#define BIND(k)                                                                \
    do                                                                         \
    {                                                                          \
        const float *p = tune_at(#k);                                          \
        if (!p)                                                                \
        {                                                                      \
            R_ERR("geo", "the scripts declare no knob named %s, and the renderer reads it", #k); \
            ++missing;                                                         \
            p = &s_nought;                                                     \
        }                                                                      \
        s_tuned.k = p;                                                         \
    } while (0)
    BIND(line_w);
    BIND(thread_w);
    BIND(line_rmin);
    BIND(thread_rmin);
    BIND(approach);
    BIND(margin);
    BIND(trim_cap);
    BIND(show_curves);
    BIND(band_w);
    BIND(band_rmin);
    BIND(band_rmax);
    BIND(band_reach);
    BIND(band_stair);
    BIND(band_grade);
    BIND(band_stiff);
    BIND(corner_share);
    BIND(spur_merge);
#undef BIND
    return missing == 0;
}
