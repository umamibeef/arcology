/*  draw/rails.h: the railways drawn from the road network's records.
 *
 *  A railway the road network draws (laid_road) sits on a pile of
 *  gravel, and carries two tracks, one for each of its rail lanes.  A track
 *  is sleepers across it and two steel rails along it.  A turnout's gravel
 *  is its junction's fill (roads.c), and each of its branches carries its
 *  own track.  Across a level crossing the rails lie in the street. */
#ifndef ARC_RAILS_H
#define ARC_RAILS_H

#include <stdint.h>

#include "city.h"
#include "mesh/mesh.h"

/*  Lay every drawn railway's gravel, sleepers and rails into the mesh. */
void rails_draw(RMesh *m, const RCity *c, uint8_t mask_bit);

#endif /* ARC_RAILS_H */
