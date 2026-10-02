--  chain.lua -- the points a way runs through between two poses.
--
--  A pass that wants a path between two poses -- a junction's connector
--  from an in lane to an out lane, a segment's lane on to the next
--  segment's, a dead end's cap, a level meet's thread, a spur's foot and
--  its descent -- queues the two POSES and nothing else.  The points
--  between them are arc.rules.posed's, asked by the drive before the
--  cut.  (arc.rules.chain is the fit's own stage, scripts/compose/
--  runs.lua: the chain of lines a fitted path is made of.)
--  The interchange and the band ends build theirs directly
--  (scripts/compose/links/chain.lua) and never queue.
--
--  The chain is the equal-tangent BIARC: two points where B lies dead
--  ahead of A, else four -- A, A + d tA, B - d tB, B -- with the same
--  tangent length d at both ends, the two corners given the widest
--  radius the fit may sweep and d to spend.  Nothing where there is no
--  such lane, which is B behind A.  Worked at the mesh's own precision,
--  so a chain built here lands where one built in floats would.

local f32  = arc.put.f32
local sqrt = arc.put.sqrt

local function biarc_chain(ax, ay, tax, tay, bx, by, tbx, tby)
    local vx, vy = f32(bx - ax), f32(by - ay)
    local vv = f32(f32(vx * vx) + f32(vy * vy))
    local vl = sqrt(vv)
    local cr = f32(f32(tax * vy) - f32(tay * vx))
    local dt = f32(f32(tax * tbx) + f32(tay * tby))
    local dv = f32(f32(tax * vx) + f32(tay * vy))
    if vv < 1e-10 then return nil end
    if math.abs(cr) < f32(1e-4 * vl) and dt > 0.9999 and dv > 0.0 then
        return {{x = ax, y = ay}, {x = bx, y = by}}, {0.0, 0.0}, {0.0, 0.0}
    end
    local sx, sy = f32(tax + tbx), f32(tay + tby)
    local vs = f32(f32(vx * sx) + f32(vy * sy))
    local kk = f32(2.0 * f32(1.0 - dt))
    local d
    if kk < 1e-5 then
        if vs <= 1e-6 then return nil end
        d = f32(vv / f32(2.0 * vs))
    else
        d = f32(f32(-vs + sqrt(f32(f32(vs * vs) + f32(kk * vv)))) / kk)
    end
    if d <= 1e-4 then return nil end
    local cap = arc.geo.lane_route_rmax
    return {{x = ax, y = ay},
            {x = f32(ax + f32(tax * d)), y = f32(ay + f32(tay * d))},
            {x = f32(bx - f32(tbx * d)), y = f32(by - f32(tby * d))},
            {x = bx, y = by}},
           {0.0, cap, cap, 0.0}, {0.0, d, d, 0.0}
end

--  A chain that begins at a ramp's top begins with its MERGE LANE
--  (scripts/compose/drop.lua, arc.merge_points): the lane's points
--  from the far end of its taper to the gore, and the biarc from the
--  gore on to the far pose.  A ramp whose line is a junction's arm
--  queues its descent this way rather than sliding it
--  (scripts/compose/slide.lua), and the two lay the same lane.
arc.rules.posed = function (p)
    local d = p:info()
    if not d.posed then return true end
    local g = arc.merge_of(d.ax, d.ay, "top", 0.3)
    local q, rad, tlim
    if g then
        local gdx, gdy
        q, gdx, gdy = arc.merge_points(g)
        local q2, rad2, tlim2 = biarc_chain(q[#q].x, q[#q].y, gdx, gdy, d.bx, d.by, d.bdx, d.bdy)
        if not q2 then return true end
        rad, tlim = {}, {}
        for i = 1, #q do
            rad[i] = arc.geo.interchange_radius
            local d1 = i > 1 and arc.dist(q[i - 1].x, q[i - 1].y, q[i].x, q[i].y) or 1e9
            local d2 = arc.dist(q[i].x, q[i].y, i < #q and q[i + 1].x or q2[2].x, i < #q and q[i + 1].y or q2[2].y)
            tlim[i] = 0.5 * math.min(d1, d2)
        end
        for i = 2, #q2 do
            q[#q + 1], rad[#rad + 1], tlim[#tlim + 1] = q2[i], rad2[i], tlim2[i]
        end
        if arc.geo.pose_dump > 0.5 then
            local qs = {}
            for _, pt in ipairs(q) do qs[#qs + 1] = string.format("%.2f,%.2f", pt.x, pt.y) end
            arc.dump(string.format("POSED merge %d,%d: A %.2f,%.2f dir %.2f,%.2f to B %.2f,%.2f dir %.2f,%.2f | %s",
                math.floor(g.x), math.floor(g.y), d.ax, d.ay, d.adx, d.ady, d.bx, d.by, d.bdx, d.bdy, table.concat(qs, " ")))
        end
    else
        q, rad, tlim = biarc_chain(d.ax, d.ay, d.adx, d.ady, d.bx, d.by, d.bdx, d.bdy)
    end
    if q then p:points(q, rad, tlim) end
    return true
end
