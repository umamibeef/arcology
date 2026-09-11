/*  net/station.h: what station.c answers for.
 *
 *  a slab's stations, filed under the band they belong to.
 *
 *  Nothing declared here decides anything. */
#ifndef ARC_NET_STATION_H
#define ARC_NET_STATION_H

#include "net/types.h"

extern BandSt s_band_st[BAND_MAX_ST];
void net_station_reset(void);
int  net_station_record(const Loft *x);

#endif /* ARC_NET_STATION_H */
