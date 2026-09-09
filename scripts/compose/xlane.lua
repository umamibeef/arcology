--  xlane.lua -- carrying a lane on across a crossing.
--
--  A rail crossing a road, or a road running under a deck, ENDS the
--  segments on both sides of the crossing tile.  Their lanes are left
--  facing each other open, with nothing between them, and a car reaching
--  one turns round rather than crossing.
--
--  So every open lane end looks for the lane facing it: the same family,
--  the same offset from the centreline, running the same way, and near
--  enough to be the other half of one road.  The nearest such lane wins.
--
--  Two that meet ON THE SPOT are one lane the walk broke at a node in the
--  middle of it: each end simply names the other and nothing is drawn.
--  Two a little apart get a straight link between them.
--
--  An end at the map's edge faces nothing and is left alone.

local geo = arc.geo

arc.rules.cross = function (x)
    local d = x:info()

    for la = 0, d.n - 1 do
        if x:open(la) then
            local best, bd

            for lb = 0, d.n - 1 do
                local off, dot, ahead, aside, dist = x:measure(la, lb)
                if off and off <= geo.lane_cross_off and dot >= geo.lane_cross_dot then
                    --  Behind the end, off to one side, or further than
                    --  the best so far: not this one.
                    local behind = ahead < geo.lane_cross_ahead and dist > geo.lane_cross_spot
                    if not behind and aside <= geo.lane_cross_aside and dist < (bd or geo.lane_cross_reach) then
                        bd, best = dist, lb
                    end
                end
            end

            if best then
                if bd <= geo.lane_cross_spot then x:merge(la, best) else x:link(la, best) end
            end
        end
    end
    return true
end
