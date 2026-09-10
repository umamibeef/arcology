--  spur_target.lua -- where a spur aims on the line it comes down to.
--
--  A spur's foot has to end ON a lane of the line, not on its surface.
--  Which lane, and where along it, follows from the FORK -- what the
--  meet beside the spur is, which arc.rules.spur_arm answers, and
--  what the lines beyond it are, which arc.rules.spur_fork answers:
--
--    fork 3, STRAIGHT ON.  A line carries through the tile's far side,
--      so the spur's lane runs on along it with no turn at all: the lane
--      on the right hand of the spur's own travel, out to the far edge.
--    fork 1, A THROUGH ROAD ACROSS.  The lane on the spur's side, out to
--      the far edge.  An off spur's traffic runs away from the slab and
--      takes the lane on that direction's right hand; an on spur's runs
--      toward it and takes the other -- the side is the LANE'S, not the
--      spur tile's.
--    anything else, THE MERGE.  Into the through line's near lane, the
--      one on the spur's side running the way whose right hand it is,
--      a little off the centreline and most of half a tile along.  An
--      off spur merges in downstream of the foot; an on spur peels off
--      upstream, with the construction heading against its travel.
--
--  The viewer's right of a direction d is (d.y, -d.x) -- the map is drawn
--  reflected, and cars drive on the viewer's right.
--
--  Answers the point the join is drawn to, the tangent it is drawn with,
--  and the way the line's lane travels there, which is what the lane
--  lookup is made against.

local function right(x, y) return y, -x end

arc.rules.spur_target = function (t)
    local d  = t:info()
    local lo = d.lane_off
    --  the line tile's centre, half a tile on from the foot
    local cx, cy = d.x + d.rdx * 0.5, d.y + d.rdy * 0.5

    if d.fork == 3 then
        local tvx, tvy = d.rdx, d.rdy
        if not d.off then tvx, tvy = -tvx, -tvy end
        local rx, ry = right(tvx, tvy)
        t:is(cx + rx * lo + d.rdx * 0.5, cy + ry * lo + d.rdy * 0.5,
             d.rdx, d.rdy, tvx, tvy)

    elseif d.fork == 1 then
        local tvx, tvy = d.mdx, d.mdy
        if not d.off then tvx, tvy = -tvx, -tvy end
        local rx, ry = right(tvx, tvy)
        t:is(cx + rx * lo + d.mdx * 0.5, cy + ry * lo + d.mdy * 0.5,
             d.mdx, d.mdy, tvx, tvy)

    else
        local dmx, dmy = right(d.rdx, d.rdy)
        local tr = d.off and 1.0 or -1.0
        local al = d.merge_along
        t:is(cx - d.rdx * lo + dmx * al * tr, cy - d.rdy * lo + dmy * al * tr,
             dmx * tr, dmy * tr, dmx, dmy)
    end
    return true
end
