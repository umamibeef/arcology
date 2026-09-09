/*  The power family: lines drawn tile by tile. */
#include <math.h>

#include <string.h>

#include "mesh/internal.h"
#include "net/internal.h"
#include "script.h"
#include "geo/model.h"

/*  The numbers this file reads, each remembering where the store
 *  put it (net/geo.c).  A name here is a name a script must set. */


/*  ==================================================================
 *  The other things this file builds
 *
 *  Power lines and the clipped prisms they and the rails stand on.
 *  ================================================================== */
/*  power.c -- the power line's family.
 *
 *  A line is drawn TILE BY TILE rather than walked, and the composing
 *  script asks for each tile itself (scripts/compose/world.lua): the
 *  pylon it stands and the wires it spans to each joined edge, and over
 *  a road or a railway the span alone.  Nothing of it is drawn here.
 *
 *  So the family lends no stages at all.  Everything a strip or a
 *  junction asks of a family is beside the point for a line that is
 *  neither. */
void power_primitives(void)
{
}
