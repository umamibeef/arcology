/*  shore.h: the shore field the GPU draws water and sand by, four bytes a
 *  tile (shore.c).  They hold a water tile's distance to the nearest land,
 *  the water's depth, and a land tile's distance to the nearest water.  The
 *  fourth is the share of the sky the tile sees past the land round it. */
#ifndef ARC_SHORE_H
#define ARC_SHORE_H

#include <stdint.h>

#include "city.h"

void shore_field(const RCity *c, uint8_t *out);

#endif /* ARC_SHORE_H */
