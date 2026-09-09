/*  The power family: lines drawn tile by tile. */
#include <math.h>

#include <string.h>

#include "mesh/internal.h"
#include "net/internal.h"
#include "script.h"
#include "net/model.h"

/*  The numbers this file reads, each remembering where the store
 *  put it (net/geo.c).  A name here is a name a script must set. */


/*  ==================================================================
 *  The other things this file builds
 *
 *  Power lines and the clipped prisms they and the rails stand on.
 *  ================================================================== */
/*  A power line on one tile: the pole at the centre with its crossbar,
 *  and a wire from its top to each joined edge's midpoint, where the
 *  neighbour's wire meets it.  On a crossing over a road or rail there
 *  is no pole: the wire spans from edge to edge. */
int build_power_tile(RMesh *m, const RCity *c, int32_t col, int32_t row, uint8_t mask_bit, int links, float order, int crossing)
{
    /*  The whole tile is the SCRIPT'S (scripts/rules.lua): on its own
     *  ground the pylon it stands and the wires it spans to each joined
     *  edge, and over a road or a railway the span alone.  No rule is no
     *  power line. */
    ScriptProp at;
    memset(&at, 0, sizeof at);
    at.col = (int)col, at.row = (int)row, at.arm = -1, at.links = links;
    at.x = (float)col + 0.5f, at.y = (float)row + 0.5f;
    at.z = surface_at_world(c, mask_bit, at.x, at.y);
    at.fx = 1.0f, at.fy = 0.0f;
    script_emit_open(m, c, mask_bit, order);
    script_rule_prop(crossing ? "power_crossing" : "power_tile", &at);
    script_emit_close();
    return 0;
}

/*  Power lines are drawn tile by tile, not walked: nothing a strip or a
 *  junction asks of a family applies. */
const NetFamily net_power = {"power", F_POWER, &s_tune.road_w, &s_tune.road_rmin, &s_tune.road_rmax, 0.50f, MAT_ROAD, LOFT_ROAD, 0, 0.0f, 0.25f, 0, 0, 0, 0, 0, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, build_power_tile, NULL, 0.0f, NET_LANE_ENDS_OPEN, 0 /* keeps to its tiles */, 0.0f, "slot_strip", 0 /* not a deck */};
