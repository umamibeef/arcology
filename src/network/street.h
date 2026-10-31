/*  network/street.h: the street layer's import (plans/network.md, layer 0b).
 *
 *  A city that holds no street layer gets one once, when it is loaded.
 *  Each STREET is a road of the road network's topology, with the roads it
 *  goes on into across seams.  It takes the higher of two classes.  One is
 *  by the traffic on its units, and the other by the population of the
 *  lots that reach it, per unit of its length.  Every edge of the street
 *  takes that class, two-way.  This file works the edges out and writes
 *  nothing.  The app writes them into the city. */
#ifndef ARC_NET_STREET_H
#define ARC_NET_STREET_H

#include <stdint.h>

#include "city.h"

/*  One edge of the street layer.  It holds the two cells side by side,
 *  by their index in the view, and the edge's class and direction
 *  (sim.h, STREET_EDGE). */
typedef struct
{
    int32_t a, b;
    uint8_t edge;
} StreetEdge;

/*  The street layer a city's view should hold.  It builds the topology of
 *  the view first.  The edges are handed back in `*out`, which the caller
 *  frees.  The answer is how many, or -1. */
int street_infer(const RCity *v, StreetEdge **out);

#endif /* ARC_NET_STREET_H */
