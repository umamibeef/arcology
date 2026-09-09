/*  marking.c -- the road-marking pass.  A road is lofted BLANK: asphalt,
 *  curb and sidewalk.  What is painted on it -- the centre line by class,
 *  the lane dashes, the crosswalk bands at a controlled mouth with their
 *  stop lines, the crossing's approach with its X -- is the marking pass's,
 *  decided here from the road's attributes and drawn by the road material
 *  in terrain.frag on the marking PASS: the frame draws the road and deck
 *  strips again under pass 2 (gpu/frame.c) and the road material paints the
 *  lines alone on that draw, asphalt alone on the base draw -- the same
 *  vertices, no overlay geometry.  Off, the draw is skipped and the road is
 *  blank.  What the loft has to know at build time -- how long a crosswalk
 *  band is, whether a station approaches a crossing -- it asks here. */
#include <math.h>

#include "mesh/internal.h"
#include "net/internal.h"

static int s_marking = 1; /* View > Road markings, --no-markings */

void marking_enable(int on)
{
    s_marking = on ? 1 : 0;
}

int marking_on(void)
{
    return s_marking;
}

/*  The ground under a sunken road follows it: where a vertical curve sinks
 *  a road below the ground, the corners of the tiles it passes through are
 *  capped to a hair under the road and the mesh is built again on that
 *  field, so the terrain bends into the cut instead of being carved out of
 *  it.  One height per grid corner, shared by the four tiles around it, so
 *  the bend is continuous and the walls close what is left. */
