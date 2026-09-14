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

--  THE ARMS OF A NODE AGREE.  A band whose end meets an interchange
--  (the pose store says which, scripts/compose/poses.lua) records the
--  height its slab arrives at, stiffened over the ground, with the
--  slab's lift over it (arc.arm_floor, by band and end), and the
--  node it meets (arc.arm_node).  The grading pass settles every arm's
--  height over the node's ways, with the arms free to rise
--  (scripts/compose/interchange.lua, arc.arm_z), and the band is then
--  brought to it over its last tiles at the ramp's grade: down to it
--  where the stiff profile rides higher, never below its own envelope,
--  and up to it where it rides lower.  A slab is faded down to the
--  ground at its ends because that is where it usually has to meet
--  it; an end that meets a node is carried on at the slab's height
--  instead, and the node's ways take it from there.
arc.arm_floor = arc.arm_floor or {}
arc.arm_node  = arc.arm_node or {}
arc.arm_z     = arc.arm_z or {}
local function shape_to_node(k, key, s, z, env, n, total, lift, at_end)
    local i_end = at_end and n - 1 or 0
    --  The floor an arm records is the slab AS IT ARRIVES, stiffened
    --  over the ground, not the bare envelope under it: a node's deck
    --  is the highest arriving slab, so no arm dips into the node and
    --  climbs out of it again, and the lower arms rise to meet it.  The
    --  arm arrives over the FAN'S length (scripts/compose/drop.lua): its
    --  lanes end anywhere along that, and every way leaves at the deck's
    --  height, so the floor is the highest the slab stands over it and
    --  the arm is held level at the deck that far back.
    local reach = arc.fan_reach() + 0.125 -- and a station's step, as a lane's end snaps to one
    local floor = z[i_end]
    for i = 0, n - 1 do
        local back = at_end and f32(total - s[i]) or s[i]
        if back <= reach + 1e-4 and z[i] > floor then floor = z[i] end
    end
    floor = f32(floor + lift)
    arc.arm_floor[key], arc.arm_node[key] = floor, k
    local deck = arc.arm_z[key]
    if arc.geo.interchange_dump > 0.5 then
        arc.dump(string.format("FLOOR node %d arm %d %s end: env %.3f smoothed %.3f deck %s",
            k, key, at_end and "last" or "first", floor, z[i_end] + lift, deck and string.format("%.3f", deck) or "none"))
    end
    if not deck then return end
    deck = f32(deck - lift)
    local grade = arc.geo.interchange_grade > 0 and arc.geo.interchange_grade
                  or arc.tune.band_grade
    for i = 0, n - 1 do
        local back = at_end and f32(total - s[i]) or s[i]
        back = math.max(0.0, f32(back - reach))
        local hi   = f32(deck + f32(grade * back))
        local lo   = f32(deck - f32(grade * back))
        if z[i] > hi then z[i] = hi end
        if z[i] < lo then z[i] = lo end
        if z[i] < env[i] then z[i] = env[i] end
    end
end

--  A SPUR IS A BRANCH OF ITS SLAB.  A lane drop's descent begins on the
--  slab's stations, wherever along them the slide put it, and the slab
--  does not stand at ground plus lift there: it is stiffened over the
--  dips, and an arm of a node is brought to the node's deck.  So a slab's
--  profile, once settled, hands every gore beside it the slab's finished
--  stations (arc.poses.gore[cell].parent), and the descent reads its
--  slab end's height and grade off them.  The gore itself keeps the
--  slab's height and grade there, for whatever settles heights over the
--  poses.  A gore is beside a slab when a station stands within
--  GORE_NEAR of it: the gore is a tile from the seam, and the next slab
--  over is two or more.
local GORE_NEAR = 1.3
local function hand_gores(p, s, z, n)
    local P = arc.poses
    if not P or not P.gore or not next(P.gore) then return end
    local xs, ys, dxs, dys = {}, {}, {}, {}
    local x0, y0, x1, y1 = 1e9, 1e9, -1e9, -1e9
    for i = 0, n - 1 do
        local x, y, dx, dy = p:pose(i)
        xs[i], ys[i], dxs[i], dys[i] = x, y, dx, dy
        if x < x0 then x0 = x end
        if x > x1 then x1 = x end
        if y < y0 then y0 = y end
        if y > y1 then y1 = y end
    end
    local parent = {n = n, x = xs, y = ys, dx = dxs, dy = dys, z = z, s = s}
    for cell, g in pairs(P.gore) do
        if g.x >= x0 - GORE_NEAR and g.x <= x1 + GORE_NEAR and
           g.y >= y0 - GORE_NEAR and g.y <= y1 + GORE_NEAR then
            local best, k = GORE_NEAR * GORE_NEAR, nil
            for i = 0, n - 1 do
                local ex, ey = xs[i] - g.x, ys[i] - g.y
                local dd = ex * ex + ey * ey
                if dd < best then best, k = dd, i end
            end
            if k then
                local a, b = k > 0 and k - 1 or k, k < n - 1 and k + 1 or k
                g.parent = parent
                g.at     = k
                g.z      = z[k]
                g.grade  = s[b] > s[a] and (z[b] - z[a]) / (s[b] - s[a]) or 0.0
                if arc.geo.pose_dump > 0.5 then
                    arc.dump(string.format("GORE %d,%d: slab station %d of %d at %.2f,%.2f, %.3f high, grade %.3f",
                        cell % P.n, cell // P.n, k, n, xs[k], ys[k], z[k], g.grade))
                end
            end
        end
    end
end

--  The slab's height where the descent leaves it, and its grade along
--  the descent's way out: read off the parent's station nearest the
--  slab end, in the direction the descent runs away from it.
local function slab_end(g, x, y, ax, ay)
    local pa = g.parent
    local best, k = 1e9, nil
    for i = 0, pa.n - 1 do
        local ex, ey = pa.x[i] - x, pa.y[i] - y
        local dd = ex * ex + ey * ey
        if dd < best then best, k = dd, i end
    end
    local a, b = k > 0 and k - 1 or k, k < pa.n - 1 and k + 1 or k
    local grade = pa.s[b] > pa.s[a] and (pa.z[b] - pa.z[a]) / (pa.s[b] - pa.s[a]) or 0.0
    return pa.z[k], grade * (pa.dx[k] * ax + pa.dy[k] * ay), math.sqrt(best)
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
        --  A lane drop whose gore has its slab's stations leaves the slab
        --  at the slab's own height and grade there, and eases down to
        --  the line: a cubic between the two ends, the slab's slope at
        --  one and level at the other.  A lane with a MERGE LANE
        --  (scripts/compose/drop.lua) runs on the slab's own surface
        --  from the taper's far end to the gore, so it stands at the
        --  slab's height, station for station, that far, and the cubic
        --  runs from the gore.
        local branch, zs, ms, hold, mg
        if d.lane_piece and d.total > 1e-6 then
            local i_slab, i_line = d.lane_off and 0 or n - 1, d.lane_off and n - 1 or 0
            local g = arc.gore_of(p:pose(i_line))
            local x, y, dx, dy = p:pose(i_slab)
            mg = arc.merge_of(x, y, "q", 0.5)
            if mg then
                hold = mg.merge.len + mg.merge.taper
                if hold > f32(d.total - 0.05) then hold = f32(d.total - 0.05) end
                local zg, gg = arc.slab_z_at(mg.parent, mg.merge.s_g)
                zs, ms, branch = zg, -mg.merge.m * gg, true
                --  The slope the lane leaves with is kept to what a cubic
                --  can carry down to the line without dipping under it on
                --  the way: at most three times the mean fall, and never
                --  the other way.
                local zl  = d.lane_off and z1 or z0
                local run = math.max(0.05, d.total - hold - arc.geo.spur_taper)
                local most = 3.0 * (zl - zs) / run
                if zs > zl then ms = math.max(most, math.min(0.0, ms))
                else ms = math.min(most, math.max(0.0, ms)) end
            elseif g then
                local ax, ay = dx, dy
                if not d.lane_off then ax, ay = -dx, -dy end
                local away
                zs, ms, away = slab_end(g, x, y, ax, ay)
                branch, hold = true, 0.0
                if arc.geo.pose_dump > 0.5 then
                    arc.dump(string.format("BRANCH %s at %.2f,%.2f: slab %.3f (ground and lift %.3f), grade %.3f, station %.2f away",
                        d.lane_off and "off" or "on", x, y, zs, d.lane_off and z0 or z1, ms, away))
                end
            end
        end
        for i = 0, n - 1 do
            local t = d.total > 1e-6 and f32(s[i] / d.total) or 0.0
            local lin
            if branch then
                --  how far from the slab end this station lies
                local sf = d.lane_off and s[i] or f32(d.total - s[i])
                local zl = d.lane_off and z1 or z0
                if mg and sf <= hold then
                    lin = f32((arc.slab_z_at(mg.parent, mg.merge.s_q - mg.merge.m * sf)))
                else
                    --  down to the line's level by the foot, and level
                    --  along the join beyond it, which is the line's
                    local run = f32(d.total - hold - (mg and arc.geo.spur_taper or 0.0))
                    if run < 0.05 then run = 0.05 end
                    local ts  = f32(f32(sf - hold) / run)
                    if ts < 0.0 then ts = 0.0 end
                    if ts > 1.0 then ts = 1.0 end
                    local t2 = f32(ts * ts)
                    local t3 = f32(t2 * ts)
                    local h00 = f32(f32(2.0 * t3) - f32(3.0 * t2) + 1.0)
                    local h10 = f32(t3 - f32(2.0 * t2) + ts)
                    local h01 = f32(f32(3.0 * t2) - f32(2.0 * t3))
                    lin = f32(f32(zs * h00) + f32(f32(ms * run) * h10) + f32(zl * h01))
                end
            elseif d.lane_piece then
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
            local be = arc.band_fitting and arc.poses and arc.poses.band_end
                       and arc.poses.band_end[arc.band_fitting]
            local node0 = be and be.head and be.head.node
            local node1 = be and be.tail and be.tail.node
            for i = 0, n - 1 do
                local e0 = node0 and 1e9 or s[i]
                local e1 = node1 and 1e9 or f32(d.total - s[i])
                local edge = e0 < e1 and e0 or e1
                local fr   = f32(edge / w)
                if fr > 1.0 then fr = 1.0 elseif fr < 0.0 then fr = 0.0 end
                z[i] = f32(z[i] + f32(f32(zsm[i] - z[i]) * fr))
            end
            if node0 then shape_to_node(node0, arc.band_fitting * 2, s, z, env, n, d.total, d.lift, false) end
            if node1 then shape_to_node(node1, arc.band_fitting * 2 + 1, s, z, env, n, d.total, d.lift, true) end
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
        z[i] = f32(z[i] + f32(d.lift * lift))
        p:set(i, z[i])
    end

    --  A slab's finished stations, to the gores beside it.
    if not d.spur and n > 2 then hand_gores(p, s, z, n) end
    return true
end
