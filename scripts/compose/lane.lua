--  lane.lua -- the fitted line, drawn over the world it made.
--
--  The outline view draws these in place of the roads: a hairline down
--  each lane, each connector and each band edge, so the curve the fit
--  produced can be read against the ribbon that came out of it.  Where
--  the line runs is the fit's work; the hairline over it is not.
--
--  A piece is cut as finely as a strip is: an arc every lane.step_arc
--  and a straight every lane.step_run, fine enough that a curve reads as
--  a curve at the closest zoom.

arc.rules.lane = function (n)
    local d = n:info()
    local lane = arc.rules.family({name = "road"}).lane
    --  A ramp's own line is cut at one step whatever it does, and its
    --  height eases from the deck at the gore to the ground at the road,
    --  so the overlay shows the lane meeting the deck's where it does.
    local total, s0 = 0.0, 0.0
    if d.ramp then
        for k = 1, d.n do total = total + (n:piece(k)) end
        if total < 1e-6 then return true end
    end
    for k = 1, d.n do
        local len, turns = n:piece(k)
        local step = d.ramp and d.step or (turns and lane.step_arc or lane.step_run)
        local cuts = math.max(1, math.ceil(len / step))
        for i = 0, cuts - 1 do
            local ax, ay = n:at(k, len * i / cuts)
            local bx, by = n:at(k, len * (i + 1) / cuts)
            local fa, fb = 0.0, 0.0
            if d.ramp then
                local sa, sb = s0 + len * i / cuts, s0 + len * (i + 1) / cuts
                fa = d.off and 1.0 - sa / total or sa / total   -- 1 at the gore
                fb = d.off and 1.0 - sb / total or sb / total
            end
            local az = n:height(ax, ay, fa) + d.lift
            local bz = n:height(bx, by, fb) + d.lift
            local order = n:order(0.5 * (ax + bx), 0.5 * (ay + by))
            if order then
                n:wire(ax, ay, az, bx, by, bz, order + lane.slot, d.paint)
            end
        end
        s0 = s0 + len
    end
    return true
end
