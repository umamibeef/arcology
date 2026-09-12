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
--  (scripts/compose/links.lua) and never queue.
--
--  The chain is the equal-tangent BIARC: two points where B lies dead
--  ahead of A, else four -- A, A + d tA, B - d tB, B -- with the same
--  tangent length d at both ends, the two corners given the widest
--  radius the fit may sweep and d to spend.  Nothing where there is no
--  such lane, which is B behind A.  Worked at the mesh's own precision,
--  so a chain built here lands where one built in floats would.

local f32  = arc.put.f32
local sqrt = arc.put.sqrt

function arc.biarc_chain(ax, ay, tax, tay, bx, by, tbx, tby)
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

arc.rules.posed = function (p)
    local d = p:info()
    if not d.posed then return true end
    local q, rad, tlim = arc.biarc_chain(d.ax, d.ay, d.adx, d.ady, d.bx, d.by, d.bdx, d.bdy)
    if q then p:points(q, rad, tlim) end
    return true
end
