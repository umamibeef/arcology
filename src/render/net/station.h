/*  net/station.h: what station.c answers for.
 *
 *  A STATION is one cross-section of a slab: a point, a direction, the
 *  distance along and the height.  The loft records them here, filed
 *  under the band they belong to.  A spur is placed by reading stations
 *  rather than cells.
 *
 *  Nothing declared here decides anything. */
#ifndef ARC_NET_STATION_H
#define ARC_NET_STATION_H

#include "net/types.h"

extern BandSt s_band_st[BAND_MAX_ST];
void net_station_reset(void);
int  net_station_record(const Loft *x);

#endif /* ARC_NET_STATION_H */
