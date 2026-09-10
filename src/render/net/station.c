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
#include <string.h>

#include "mesh/internal.h"
#include "pipeline.h"

HwSt s_hw_st[HW_MAX_ST];
int  s_hw_nst, s_hw_band;

void net_station_reset(void)
{
    s_hw_nst  = 0;
    s_hw_band = 0;
}

/*  One loft's stations filed under its band.  Answers 0 always: a slab
 *  that fills the store draws no fewer strips for it. */
int net_station_record(const Loft *x)
{
    const Sample *smp = x->smp;
    int           ns = x->ns, i;
    if (x->d->struct_)
        return 0;
    for (i = 0; i < ns && s_hw_nst < HW_MAX_ST; ++i)
    {
        s_hw_st[s_hw_nst].pos  = smp[i].pos;
        s_hw_st[s_hw_nst].dir  = smp[i].dir;
        s_hw_st[s_hw_nst].s    = smp[i].s;
        s_hw_st[s_hw_nst].z    = smp[i].z;
        s_hw_st[s_hw_nst].band = x->d->band;
        ++s_hw_nst;
    }
    return 0;
}
