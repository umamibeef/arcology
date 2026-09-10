--  lane.lua -- the fitted line, drawn over the world it made.
--
--  The outline view draws these in place of the lines: a hairline down
--  each lane, each connector and each band edge, so the curve the fit
--  produced can be read against the ribbon that came out of it.  Where
--  the line runs is the fit's work; the hairline over it is not.
--
--  A piece is cut as finely as a strip is: an arc every lane.step_arc
--  and a straight every lane.step_run, fine enough that a curve reads as
--  a curve at the closest zoom.
--
--  And a CHEVRON every arc.geo.lane_arrow_every tiles, pointing the way
--  the lane runs.  A hairline reads the same from both ends, so without
--  these the overlay says where the lanes are and nothing about which
--  way anything travels -- which is the whole question at a junction or
--  an interchange.  It is drawn from the same wire the line is: two
--  strokes back from a tip, so it needs no primitive of its own.

arc.rules.lane = function (n)
    local d = n:info()
    local lane = arc.rules.family({name = "line"}).lane
    --  A spur's own line is cut at one step whatever it does, and its
    --  height eases from the slab at the gore to the ground at the line,
    --  so the overlay shows the lane meeting the slab's where it does.
    local total, s0 = 0.0, 0.0
    local every = arc.geo.lane_arrow_every
    local alen, awing = arc.geo.lane_arrow_len, arc.geo.lane_arrow_wing
    local next_arrow = every * 0.5
    if d.spur then
        for k = 0, d.n - 1 do total = total + (n:piece(k)) end
        if total < 1e-6 then return true end
    end
    for k = 0, d.n - 1 do
        local len, turns = n:piece(k)
        local step = d.spur and d.step or (turns and lane.step_arc or lane.step_run)
        local cuts = math.max(1, math.ceil(len / step))
        for i = 0, cuts - 1 do
            local ax, ay = n:at(k, len * i / cuts)
            local bx, by = n:at(k, len * (i + 1) / cuts)
            local fa, fb = 0.0, 0.0
            if d.spur then
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
        --  The chevrons on this piece, at whatever distances along the
        --  whole lane fall inside it.
        while next_arrow < s0 + len do
            local sa = next_arrow - s0
            local px, py = n:at(k, sa)
            local qx, qy = n:at(k, math.min(sa + alen, len))
            local ux, uy = qx - px, qy - py
            local ul = math.sqrt(ux * ux + uy * uy)
            if ul > 1e-6 then
                ux, uy = ux / ul, uy / ul
                local tipx, tipy = px + ux * alen, py + uy * alen
                local nx, ny = -uy * awing, ux * awing
                local fa = d.spur and (d.off and 1.0 - next_arrow / total or next_arrow / total) or 0.0
                local tz = n:height(tipx, tipy, fa) + d.lift
                local order = n:order(tipx, tipy)
                if order then
                    n:wire(tipx, tipy, tz, px + nx, py + ny,
                           n:height(px + nx, py + ny, fa) + d.lift,
                           order + lane.slot, d.paint)
                    n:wire(tipx, tipy, tz, px - nx, py - ny,
                           n:height(px - nx, py - ny, fa) + d.lift,
                           order + lane.slot, d.paint)
                end
            end
            next_arrow = next_arrow + every
        end
        s0 = s0 + len
    end
    return true
end
