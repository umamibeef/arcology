/*  station.c: THE SLAB'S STATIONS, and the store they are kept in.
 *
 *  Every station a slab's loft passes through is filed here under the
 *  band it belongs to: where it is.  This way it runs, how far along it
 *  stands and how high.  A spur reads them to find the slab it leaves
 *  and how high that slab is beside it.  The lane model reads them for a
 *  slab lane's height.
 *
 *  Nothing here decides anything.  Which strips file themselves is the
 *  family's own declaration.  A family that says `stations = true` has
 *  its loft filed, and one that does not is not.  So no family lends a
 *  stage for it.  A STRUCTURE files none: a viaduct's own slab is what a
 *  spur looks for, and its columns are not stations of it. */
#include <math.h>
#include <string.h>

#include "mesh/internal.h"
#include "pipeline.h"

#include "net/net.h"
BandSt s_band_st[BAND_MAX_ST];
int  s_band_nst, s_band_st_at;

void net_station_reset(void)
{
    s_band_nst  = 0;
    s_band_st_at = 0;
}

/*  One loft's stations filed under its band.  Answers 0 always: a slab
 *  that fills the store draws no fewer strips for it. */
int net_station_record(const Loft *x)
{
    const Sample *smp = x->smp;
    int           ns = x->ns, i;
    if (x->d->struct_)
        return 0;
    for (i = 0; i < ns && s_band_nst < BAND_MAX_ST; ++i)
    {
        s_band_st[s_band_nst].pos  = smp[i].pos;
        s_band_st[s_band_nst].dir  = smp[i].dir;
        s_band_st[s_band_nst].s    = smp[i].s;
        s_band_st[s_band_nst].z    = smp[i].z;
        s_band_st[s_band_nst].band = x->d->band;
        ++s_band_nst;
    }
    return 0;
}

/*  The nearest station to a point, whatever band laid it: how high the
 *  slab stands there, and how far away that station is.  Answers 0 where
 *  the store holds none at all, and then writes nothing.
 *
 *  This is how a place that joins several slabs finds the height they
 *  arrive at.  The store is the only record of it.  A lane carries no
 *  height.  The ground under the point says how high the land is, not
 *  how far over the land the slab was carried. */
int net_station_near(V2 p, float *z, float *away)
{
    float best = 1e9f, bz = 0.0f;
    int   k, have = 0;
    for (k = 0; k < s_band_nst; ++k)
    {
        float dx = s_band_st[k].pos.x - p.x;
        float dy = s_band_st[k].pos.y - p.y;
        float d  = dx * dx + dy * dy;
        if (d < best)
            best = d, bz = s_band_st[k].z, have = 1;
    }
    if (!have)
        return 0;
    if (z)
        *z = bz;
    if (away)
        *away = sqrtf(best);
    return 1;
}
