/*  net/network.h: what network.c answers for.
 *
 *  the network the script discovered, and the reading it decided from.
 *
 *  Nothing declared here decides anything. */
#ifndef ARC_NET_NETWORK_H
#define ARC_NET_NETWORK_H

#include "net/types.h"

void net_band_disc_reset(void);
int  net_band_disc_add(const BandRun *r, int32_t col, int32_t row, int ew, int sign);
int  net_band_disc_get(int i, const BandRun **r, int32_t *col, int32_t *row, int *ew, int *sign);
void    net_disc_reset(void);
int     net_disc_run_add(int fk, const int32_t *cells, int n, int stop, int exit);
int     net_disc_island_add(int fk, int32_t cell, int edge);
int     net_disc_junction_add(int fk, int32_t cell);
int     net_disc_junctions(int fk);
int32_t net_disc_junction(int fk, int k);
int     net_disc_count(int fk);
int     net_disc_kind(int fk, int i);
int32_t net_disc_cell(int fk, int i);
int     net_disc_run_get(int fk, int i, NetRun *out);
void    net_disc_planes(const RCity *c, const RAtlasLevel *l, Family f, uint8_t *links, uint8_t *art);
/*  What counts as a NODE says where a segment ends.  The script works it
 *  out off the two planes and hands the whole plane back.  Node_kind
 *  reads that and nothing else. */
void    net_disc_nodes_set(Family f, const uint8_t *plane);
/*  The edge a run leaves its first cell by, which with that cell names
 *  the segment for the rest of the build.  A run of one cell left the map
 *  or was cut, and leaves by the edge it was heading for. */
int     net_run_edge(const NetRun *r);
int node_kind(const RCity *c, const RAtlasLevel *l, Family f, int32_t col, int32_t row);

#endif /* ARC_NET_NETWORK_H */
