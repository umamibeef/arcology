/*  api_rules.c: `arc.rules`: the decisions the C hands to the script.
 *
 *  A rule is a plain function on this table.  Setting one takes the
 *  decision over.  Leaving it unset, or answering nil, leaves the C's
 *  own answer standing, which is the only reason there is one path here
 *  and not two.  Every rule is called while the mesh is being built, a
 *  few thousand times a build, never once a frame.
 *
 *      arc.rules.control(col, row, arms, busy) -> {n, e, s, w}
 *          A junction's control: 0 none, 1 stop, 2 signal, one an arm.
 *          `arms[e + 1]` is {class=, traffic=} for an arm that is there
 *          and nil for one that is not.
 *
 *      arc.rules.lap_at(mouth) -> depth in tiles, or 0
 *          Whether one arm's mouth carries a crosswalk and how deep a
 *          band it asks for.  `mouth` carries col, row, arm, control,
 *          margin (a margin each side of the mouth), cos (how the two
 *          run against one another, -1 squarely facing) and span (how
 *          wide the mouth is, in margin widths).
 *
 *      arc.rules.meet(mouth) -> depth in tiles, or false
 *          Whether one arm's mouth carries a crosswalk and how deep its
 *          band runs.  `mouth` carries col, row, arm, control, want (the
 *          band the C would give it), room (the line it may give up) and
 *          straight (how far that line runs straight from the mouth).
 *
 *      arc.rules.corner(at) -> {tangent=, radius=, steps=} | false | nil
 *          What a junction's outline does where two arms meet, turning
 *          through `at.phi` radians: a table rounds the corner off with
 *          a lip return, false leaves it square, nothing runs the
 *          boundary straight past it.  `at` also carries col, row, grow
 *          (how far the junction is let out), width (its margin's) and,
 *          once there is an outline to measure, room, back and fwd.
 *
 *  The call itself is script.c's.  This puts the table there and answers
 *  what is on it. */
#include "script.h"


#include "internal.h"

void api_rules_open(lua_State *L)
{
    lua_newtable(L);
    lua_setfield(L, -2, "rules");
}

