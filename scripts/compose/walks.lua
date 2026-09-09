--  walks.lua -- the footway beside a strip.
--
--  Two bands, one each hand of the carriageway, station for station with
--  it: the outer edge at the carriageway's own full width and the inner
--  one where the footway's share of the band begins.  That inner line is
--  where the carriageway stops, and both are laid by arc.band_edge, so
--  the two cannot disagree about it.
--
--  What is composed here is the geometry alone.  Which node and arm each
--  band belongs to, and which ports it names, is the strip's record: a
--  footway is one walk with the junction's corner at each end, and that
--  is the network's bookkeeping rather than a shape.

arc.rules.walks = function (s)
    local d = s:info()
    if not d.curbs then return false end
    local fam = arc.rules.family({name = d.family, width = d.half * 2})
    if not fam.footway then return false end
    local n = s:count()
    if n < 2 then return false end

    for side = 0, 1 do
        local sgn = side == 0 and 1 or -1
        for k = 0, n - 1 do
            local x, y, z, dx, dy, _, wl, wr = s:at(k)
            local w = side == 0 and wr or wl
            local wf = s:width(dx, dy)
            local ox, oy = arc.band_edge(x, y, dx, dy, arc.band_half(d.half, wf, 1), w, sgn)
            local ix, iy = arc.band_edge(x, y, dx, dy,
                                         arc.band_half(d.half, wf, fam.footway.inner), w, sgn)
            s:walk_at(side, ox, oy, ix, iy, z)
        end
        --  The band's two ends reach the strip's own, and stand out at
        --  the footway's outer edge rather than the carriageway's: that
        --  is the point the network joins to what comes next.
        local x0, y0, _, dx0, dy0, _, wl0, wr0 = s:at(0)
        local x1, y1, _, dx1, dy1, _, wl1, wr1 = s:at(n - 1)
        local h0 = arc.band_half(d.half, s:width(dx0, dy0), fam.footway.edge)
        local h1 = arc.band_half(d.half, s:width(dx1, dy1), fam.footway.edge)
        local ax, ay = arc.band_edge(x0, y0, dx0, dy0, h0, side == 0 and wr0 or wl0, sgn)
        local bx, by = arc.band_edge(x1, y1, dx1, dy1, h1, side == 0 and wr1 or wl1, sgn)
        s:walk_ends(side, ax, ay, bx, by)
    end
    return true
end
