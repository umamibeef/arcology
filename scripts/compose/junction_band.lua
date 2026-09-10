--  junction_band.lua -- how the margin sits on a junction's ring.
--
--  The ring is arc.rules.outline's.  Read as a margin it is three
--  things: which of its edges carry a band, which way each faces into
--  the junction, and which arm's mouth each is -- a mouth carries no
--  margin, because the line runs on through it.
--
--  The same reading gives the ring moved IN by the margin's width,
--  which is where the junction's fill stops.  Every edge is moved as
--  a LINE and the corners are where consecutive lines meet.  Offsetting
--  each vertex along a mitre instead is only right where both its edges
--  move by the same amount: at a mouth, where one moves and one does
--  not, the mitred point slides along the mouth and the ring crosses
--  itself, which puts fill back over the margin it was meant to stop
--  at.

local f32 = arc.put.f32

local function line_meet(ax, ay, ux, uy, bx, by, vx, vy)
    local d = ux * vy - uy * vx
    if math.abs(d) < 1e-6 then return nil end
    local t = ((bx - ax) * vy - (by - ay) * vx) / d
    return f32(ax + ux * t), f32(ay + uy * t)
end

arc.rules.band = function (b)
    local d = b:info()
    local n = d.n
    if n < 3 then return true end
    local mouth = arc.rules.family({name = "line"}).margin.mouth

    local p = {}
    for i = 0, n - 1 do
        local x, y = b:at(i)
        p[i] = {x = x, y = y}
    end

    --  Which side of an edge the junction is on comes from the ring's own
    --  WINDING, not from which way the middle lies.  A lip return makes
    --  the boundary concave, and there a normal aimed at the middle comes
    --  out very nearly tangential: the band then slides along the
    --  boundary and out of the junction, which leaves the corner paved
    --  with fill and the margin lying on the grass outside it.
    local area2 = 0.0
    for i = 0, n - 1 do
        local q = p[(i + 1) % n]
        area2 = area2 + p[i].x * q.y - q.x * p[i].y
    end
    local ccw = area2 > 0.0

    --  Each arm's mouth is ONE edge of the ring: the one whose two ends
    --  lie nearest the two corners of that arm's cut, whichever way round
    --  the ring happens to run.  Chosen per arm, so every arm has exactly
    --  one mouth however the points were ordered, and the margin breaks
    --  at every line rather than running over the ones whose tags did not
    --  survive.
    local edge_arm = {}
    for i = 0, n - 1 do edge_arm[i] = -1 end
    for e = 0, 3 do
        local have, ax, ay, bx, by = b:arm(e)
        if have then
            local best, bd = -1, mouth
            for i = 0, n - 1 do
                local q = p[(i + 1) % n]
                local s1 = arc.dist(p[i].x, p[i].y, ax, ay) + arc.dist(q.x, q.y, bx, by)
                local s2 = arc.dist(p[i].x, p[i].y, bx, by) + arc.dist(q.x, q.y, ax, ay)
                local sc = math.min(s1, s2)
                if sc < bd then bd, best = sc, i end
            end
            if best >= 0 then edge_arm[best] = e end
        end
    end

    --  The inward normal of each edge, and whether it carries a band.  A
    --  mouth keeps its normal -- that is the way a meet laid there
    --  runs into the junction -- but carries no margin.
    local nrm, has = {}, {}
    for i = 0, n - 1 do
        local q = p[(i + 1) % n]
        local ex, ey = q.x - p[i].x, q.y - p[i].y
        local el = math.sqrt(ex * ex + ey * ey)
        if el < 1e-4 then
            nrm[i], has[i] = {x = 0.0, y = 0.0}, false
        else
            local nx, ny = f32(-ey / el), f32(ex / el)
            if not ccw then nx, ny = -nx, -ny end
            nrm[i], has[i] = {x = nx, y = ny}, edge_arm[i] < 0
        end
    end

    --  Where the band turns a corner of the ring its offset is MITRED --
    --  the bisector of the two edges' normals, stretched by the angle --
    --  so a lip round a return is one smooth band and not a row of
    --  quads each square to its own edge, which leaves a tooth at every
    --  vertex.  Against a mouth, which carries no band, there is nothing
    --  to mitre with and the edge keeps its own normal.
    local function mitre(own, other)
        local nx, ny = nrm[own].x, nrm[own].y
        if not has[other] then return nx, ny end
        local bx, by = f32(nrm[other].x + nx), f32(nrm[other].y + ny)
        local bl = f32(math.sqrt(f32(bx * bx) + f32(by * by)))
        if bl <= 1e-4 then return nx, ny end
        bx, by = f32(bx / bl), f32(by / bl)
        local dt = f32(bx * nx + by * ny)
        if dt <= 0.5 then dt = 0.5 end
        return f32(bx / dt), f32(by / dt)
    end

    for i = 0, n - 1 do
        local ip, j = (i - 1) % n, (i + 1) % n
        local m0x, m0y = mitre(i, ip)
        local m1x, m1y = mitre(i, j)
        b:edge(i, has[i], nrm[i].x, nrm[i].y, edge_arm[i], m0x, m0y, m1x, m1y)
    end

    --  And the ring moved in, where one was asked for.
    local org, dir = {}, {}
    for i = 0, n - 1 do
        local q = p[(i + 1) % n]
        local inw = edge_arm[i] < 0 and d.width or 0.0
        org[i] = {x = f32(p[i].x + f32(nrm[i].x * inw)), y = f32(p[i].y + f32(nrm[i].y * inw))}
        dir[i] = {x = q.x - p[i].x, y = q.y - p[i].y}
    end
    for i = 0, n - 1 do
        local ip = (i - 1) % n
        local x, y = line_meet(org[ip].x, org[ip].y, dir[ip].x, dir[ip].y,
                               org[i].x, org[i].y, dir[i].x, dir[i].y)
        --  Two edges in a line: the moved point serves.
        if not x then x, y = org[i].x, org[i].y end
        b:inset(x, y)
    end
    b:close()
    return true
end
