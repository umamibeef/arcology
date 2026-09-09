--  tangent.lua -- how much of an edge a corner may take for its arc.
--
--  Every corner of a fitted path needs tangent: a fillet of radius r
--  takes r * tan(turn / 2) of the edge on each side of it.  The edges
--  are finite, so the corners along a path are competing for them, and
--  these are the two rules that settle the competition.
--
--  Between two corners the edge is split by DEMAND, not in half.  A
--  right angle beside a slight bend takes most of the edge and the two
--  then come out with the same radius; half each would leave the gentle
--  bend wasting its share.
--
--  At an END the edge is shared with the junction rather than another
--  corner, and the junction wants the arm to leave it straight for a
--  while -- the approach, which scales with the road's width.  But not
--  at the price of a legal arc.  Where both fit, the approach is kept.
--  Where they do not, the arc comes first and the straight is cut back
--  to what the junction's own trim needs and no more.  And where even
--  that leaves nothing, the corner takes half the edge, so a corner next
--  to a junction still sweeps with whatever there is.

local f32 = arc.put.f32

--  `need` is the tangent an arc of the smallest legal radius would take
--  here, or nil at a corner with no turn to it.
function arc.end_budget(len, reserve, need, trim_cap)
    local half = f32(0.5 * len)
    if not need then return half end
    local approach = f32(len - reserve)
    if approach >= need then return approach end
    local keep = f32(len - trim_cap)
    return keep > half and keep or half
end
