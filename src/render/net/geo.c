/*  geo.c -- the road works' numbers, and the one place they are written.
 *
 *  Every number is the SCRIPTS' (scripts/geo.lua).  There is no list
 *  here and no struct of fields: a name exists from the moment something
 *  sets it, and the C that reads one names it and keeps the place it was
 *  given.
 *
 *  A read is an index into this store, cached at the site that reads it,
 *  so a number costs a bounds test and a load even in a per-frame loop.
 *  That is what lets the traffic and the loft keep their numbers here
 *  rather than in fields of their own, and it is why nothing has to be
 *  declared before a script can invent it. */
#include <stdio.h>
#include <string.h>

#include "mesh/internal.h"
#include "net/internal.h"

#define GEO_MAX 512

static char  s_name[GEO_MAX][32];
static float s_val[GEO_MAX];
static int   s_n;

int net_geo_count(void)
{
    return s_n;
}

const char *net_geo_name(int i, float *v)
{
    if (i < 0 || i >= s_n)
        return NULL;
    if (v)
        *v = s_val[i];
    return s_name[i];
}

int net_geo_index(const char *name)
{
    int i;
    for (i = 0; name && i < s_n; ++i)
        if (strcmp(s_name[i], name) == 0)
            return i;
    return -1;
}

float net_geo_value(int i)
{
    return i >= 0 && i < s_n ? s_val[i] : 0.0f;
}

float net_geo(int *cache, const char *name)
{
    if (*cache < 0)
        *cache = net_geo_index(name);
    return *cache >= 0 ? s_val[*cache] : 0.0f;
}

int net_geo_set(const char *name, float v)
{
    int i;
    if (!name || !*name)
        return 0;
    i = net_geo_index(name);
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
