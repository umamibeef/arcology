--  node_control.lua -- what controls a line junction's arms.
--
--  The reading and the decision are both here.  What a junction is told
--  about itself is the classes of the lines meeting it and how busy they
--  are, and both come from the city the script can read for itself --
--  arc.city.line_class and the traffic layer -- so nothing in the
--  pipeline has to gather them first.
--
--  A junction counts as BUSY when the traffic on its own tile passes the
--  mark, which is what turns an all-way stop between two avenues into a
--  signal.
--
--  What is done with that reading is arc.rules.control's, which is
--  shared: a railway's junction and a line's are told the same shape of
--  thing.  This is the line's own gathering of it.
--
--  An arm off the map, or one the junction has no link along, is no arm
--  at all and is left out rather than given a class of nothing.

local DU = {[0] = 0, 1, 0, -1} -- north, east, south, west
local DV = {[0] = -1, 0, 1, 0}

arc.rules.node_control = function (at)
    local arms = {}
    for e = 0, 3 do
        if at.links & (1 << e) ~= 0 then
            local c, r = at.col + DU[e], at.row + DV[e]
            local tv = arc.city.at("xtrf", c, r)
            if tv then
                arms[e + 1] = {class = arc.city.line_class(c, r), traffic = tv}
            end
        end
    end
    --  The decision itself is shared with the railway's junction, and a
    --  rule may be absent: no answer is a junction nothing controls,
    --  which is what a run that cannot find the scripts draws.
    local control = arc.rules.control
    if not control then return nil end
    local here = arc.city.at("xtrf", at.col, at.row) or 0
    return control{col = at.col, row = at.row, links = at.links,
                   arms = arms, busy = here > arc.geo.junction_busy}
end
