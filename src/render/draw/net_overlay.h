/*  draw/net_overlay.h: the lane graph laid in the world while the outline
 *  view is on. */
#ifndef ARC_NET_OVERLAY_H
#define ARC_NET_OVERLAY_H

#include <stdint.h>

#include "city.h"
#include "mesh/mesh.h"

/*  Lay the lane graph of the network last built into the mesh, over the
 *  drawn surface.  0, or -1 out of memory. */
int net_overlay(RMesh *m, const RCity *c, uint8_t mask_bit);

#endif /* ARC_NET_OVERLAY_H */
