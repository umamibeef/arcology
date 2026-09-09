--  profile.lua -- how high a highway strip rides, station by station.
--
--  Two quite different things use this.
--
--  A RAMP is one straight line in elevation, from the ground at its road
--  end to the ground at its deck end.  It does NOT follow the bumps
--  under it, and the deck's grade limiter below would let one dip under
--  a rising verge.  A lane drop's turn-out carries the last of the
--  descent instead: its deck end sits where the strip left off, the
--  deck's own height above the ground there, and eases down to the road.
--  Either way a ramp is never under the ground it crosses -- a bump
--  lifts it, a hollow does not drop it.
--
--  A DECK is stiff.  The ground's upper envelope may rise or fall no
--  faster than the deck grade; then a closing over the stiffness window
--  -- the running greatest height over the window, and the running mean
--  of that over the same window -- which holds the deck level across
--  dips shorter than the window and rounds every crest and sag while
--  never dipping below the envelope.  It is faded out over a window's
--  length at each end, where the deck has to meet the ground.
--
--  Both then take the lift, tapered over the ramp cells at each end so a
--  ramp is a ramp and not a carriageway ending in mid-air.

local f32 = arc.put.f32

arc.rules.profile = function (p)
    local d = p:info()
    local n = d.n

    --  The stations as they stand: how far along each is, and the ground
    --  under it.
    local s, z = {}, {}
    for i = 0, n - 1 do s[i], z[i] = p:at(i) end

    if d.ramp then
        --  A lane drop's turn-out meets the deck where the strip left
        --  off; a plain ramp meets the ground at both ends.
        local z0 = f32(z[0] + ((d.lane_piece and not d.lane_off) and 0.0 or d.deck_above))
        local z1 = f32(z[n - 1] + ((d.lane_piece and d.lane_off) and 0.0 or d.deck_above))
        for i = 0, n - 1 do
            local t = d.total > 1e-6 and f32(s[i] / d.total) or 0.0
            local lin
            if d.lane_piece then
                if d.lane_off then
                    lin = f32(z1 + f32(f32(z0 - z1) * p:ease(f32(1.0 - t))))
                else
                    lin = f32(z0 + f32(f32(z1 - z0) * p:ease(t)))
                end
            else
                lin = f32(z0 + f32(f32(z1 - z0) * t))
            end
            if lin > z[i] or d.lane_piece then z[i] = lin end
        end
    else
        --  The envelope: no faster up or down than the grade, both ways.
        for i = 1, n - 1 do
            local lim = f32(z[i - 1] - f32(d.grade * f32(s[i] - s[i - 1])))
            if z[i] < lim then z[i] = lim end
        end
        for i = n - 2, 0, -1 do
            local lim = f32(z[i + 1] - f32(d.grade * f32(s[i + 1] - s[i])))
            if z[i] < lim then z[i] = lim end
        end

        local w = d.stiff
        if w > 1e-3 then
            --  The running greatest height over the window.
            local zmax = {}
            local a, b = 0, 0
            for i = 0, n - 1 do
                local m = -1e9
                while a < i and s[a] < f32(s[i] - w) do a = a + 1 end
                while b < n and s[b] <= f32(s[i] + w) do b = b + 1 end
                for k = a, b - 1 do if z[k] > m then m = z[k] end end
                zmax[i] = m
            end
            --  And the running mean of that, over the same window.
            local zsm = {}
            a, b = 0, 0
            for i = 0, n - 1 do
                local sum = 0.0
                while a < i and s[a] < f32(s[i] - w) do a = a + 1 end
                while b < n and s[b] <= f32(s[i] + w) do b = b + 1 end
                for k = a, b - 1 do sum = sum + zmax[k] end
                zsm[i] = f32(sum / (b - a))
            end
            --  Faded in over a window at each end, where the deck has to
            --  meet the ground.
            for i = 0, n - 1 do
                local edge = s[i] < f32(d.total - s[i]) and s[i] or f32(d.total - s[i])
                local fr   = f32(edge / w)
                if fr > 1.0 then fr = 1.0 elseif fr < 0.0 then fr = 0.0 end
                z[i] = f32(z[i] + f32(f32(zsm[i] - z[i]) * fr))
            end
        end
    end

    --  The lift, tapered over the ramp cells at each end.
    for i = 0, n - 1 do
        local lift = d.flat and 0.0 or 1.0
        if d.taper0 > 0.0 and s[i] < d.taper0 then lift = f32(s[i] / d.taper0) end
        if d.taper1 > 0.0 and f32(d.total - s[i]) < d.taper1 then
            local back = f32(f32(d.total - s[i]) / d.taper1)
            if back < lift then lift = back end
        end
        p:set(i, f32(z[i] + f32(d.lift * lift)))
    end
    return true
end
