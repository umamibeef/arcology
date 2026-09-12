--  profile.lua -- how high a band strip rides, station by station.
--
--  Two quite different things use this.
--
--  A SPUR is one straight line in elevation, from the ground at its line
--  end to the ground at its slab end.  It does NOT follow the bumps
--  under it, and the slab's grade limiter below would let one dip under
--  a rising verge.  A lane drop's turn-out carries the last of the
--  descent instead: its slab end sits where the strip left off, the
--  slab's own height above the ground there, and eases down to the line.
--  Either way a spur is never under the ground it crosses -- a bump
--  lifts it, a hollow does not drop it.
--
--  A SLAB is stiff.  The ground's upper envelope may rise or fall no
--  faster than the slab grade; then a closing over the stiffness window
--  -- the running greatest height over the window, and the running mean
--  of that over the same window -- which holds the slab level across
--  dips shorter than the window and rounds every crest and sag while
--  never dipping below the envelope.  It is faded out over a window's
--  length at each end, where the slab has to meet the ground.
--
--  A strip of two stations or fewer is TOO SHORT TO SHAPE: neither
--  reading has anything to work on, so it takes the lift alone.
--
--  All three then take the lift, tapered over the spur cells at each end
--  so a spur is a spur and not a way ending in mid-air.

local f32 = arc.put.f32

--  Whether the strip meets an interchange beyond this end, and which:
--  the node whose halo the end station stands in, where the end's tile
--  or one beside it is a cell the band walk left to that node as its
--  approach (scripts/compose/bands.lua).  Those cells lie only between
--  a band's end and the node it meets, so an end inside a halo with
--  none beside it is a band passing by.  Neither the strip's direction
--  at the end nor its distance to the node is a guide: a band that
--  begins in a curve block turns there.  A slab is faded down to the
--  ground at its ends because that is where it usually has to meet it;
--  an end that meets a node is carried on at the slab's height instead,
--  and the node's ways take it from there.
local function meets_node(x, y)
    local near = arc.band_approach
    if not arc.band_tiles or not near then return nil end
    local _, of_tile, n = arc.interchange_nodes()
    local c, r = math.floor(x), math.floor(y)
    if c < 0 or r < 0 or c >= n or r >= n then return nil end
    local k = of_tile[r * n + c]
    if not k then return nil end
    for _, s in ipairs {{0, 0}, {1, 0}, {-1, 0}, {0, 1}, {0, -1}} do
        local cc, cr = c + s[1], r + s[2]
        if cc >= 0 and cr >= 0 and cc < n and cr < n and near[cr * n + cc] then return k end
    end
    return nil
end

--  THE ARMS OF A NODE AGREE.  A band reaching a node records the lowest
--  height its end can stand at, its ground envelope there, under the
--  node (arc.node_floor); the node's deck is the highest of those
--  (arc.node_deck, set from the floors when the node is laid, so the
--  pass after reads it).  A band whose end has a deck is then brought
--  to it over its last tiles, at the ramp's grade: down to it where the
--  stiff profile rides higher, never below its own envelope, and up to
--  it where it rides lower.  Four ways meeting a level apart leave no
--  grade for one to climb over another; four meeting at one height do.
arc.node_floor = arc.node_floor or {}
arc.node_deck  = arc.node_deck or {}
local function shape_to_node(k, s, z, env, n, total, at_end)
    if k == nil or k == true then return end
    local i_end = at_end and n - 1 or 0
    local floor = env[i_end]
    if not arc.node_floor[k] or floor > arc.node_floor[k] then arc.node_floor[k] = floor end
    local deck = arc.node_deck[k]
    if arc.geo.interchange_dump > 0.5 then
        arc.dump(string.format("FLOOR node %d %s end: env %.3f smoothed %.3f deck %s",
            k, at_end and "last" or "first", floor, z[i_end], deck and string.format("%.3f", deck) or "none"))
    end
    if not deck then return end
    local grade = arc.geo.interchange_grade > 0 and arc.geo.interchange_grade
                  or arc.tune.band_grade
    for i = 0, n - 1 do
        local back = at_end and f32(total - s[i]) or s[i]
        local hi   = f32(deck + f32(grade * back))
        local lo   = f32(deck - f32(grade * back))
        if z[i] > hi then z[i] = hi end
        if z[i] < lo then z[i] = lo end
        if z[i] < env[i] then z[i] = env[i] end
    end
end

arc.rules.profile = function (p)
    local d = p:info()
    local n = d.n

    --  The stations as they stand: how far along each is, and the ground
    --  under it.
    local s, z = {}, {}
    for i = 0, n - 1 do s[i], z[i] = p:at(i) end

    if n <= 2 then
        --  Too short to shape: the lift below is the whole of it.
    elseif d.spur then
        --  A lane drop's turn-out meets the slab where the strip left
        --  off; a plain spur meets the ground at both ends.
        local z0 = f32(z[0] + ((d.lane_piece and not d.lane_off) and 0.0 or d.slab_above))
        local z1 = f32(z[n - 1] + ((d.lane_piece and d.lane_off) and 0.0 or d.slab_above))
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

        --  The envelope, kept: what an end can never stand below.
        local env = {}
        for i = 0, n - 1 do env[i] = z[i] end

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
            --  Faded in over a window at each end, where the slab has to
            --  meet the ground; not at an end that meets a node.
            local x0, y0 = p:pose(0)
            local x1, y1 = p:pose(n - 1)
            local node0 = x0 and meets_node(x0, y0)
            local node1 = x1 and meets_node(x1, y1)
            for i = 0, n - 1 do
                local e0 = node0 and 1e9 or s[i]
                local e1 = node1 and 1e9 or f32(d.total - s[i])
                local edge = e0 < e1 and e0 or e1
                local fr   = f32(edge / w)
                if fr > 1.0 then fr = 1.0 elseif fr < 0.0 then fr = 0.0 end
                z[i] = f32(z[i] + f32(f32(zsm[i] - z[i]) * fr))
            end
            if node0 then shape_to_node(node0, s, z, env, n, d.total, false) end
            if node1 then shape_to_node(node1, s, z, env, n, d.total, true) end
        end
    end

    --  The lift, tapered over the spur cells at each end.
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
