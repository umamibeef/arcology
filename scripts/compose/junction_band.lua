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
    local mouth = arc.family_of("line").margin.mouth

    --  Every vertex, normal and moved edge is held as a pair of arrays
    --  rather than a table a point: a ring has dozens of points, and a
    --  city has hundreds of rings.
    local px, py = {}, {}
    for i = 0, n - 1 do
        px[i], py[i] = b:at(i)
    end

    --  Which side of an edge the junction is on comes from the ring's own
    --  WINDING, not from which way the middle lies.  A lip return makes
    --  the boundary concave, and there a normal aimed at the middle comes
    --  out very nearly tangential: the band then slides along the
    --  boundary and out of the junction, which leaves the corner paved
    --  with fill and the margin lying on the grass outside it.
    local area2 = 0.0
    for i = 0, n - 1 do
        local j = (i + 1) % n
        area2 = area2 + px[i] * py[j] - px[j] * py[i]
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
                local j = (i + 1) % n
                local s1 = arc.dist(px[i], py[i], ax, ay) + arc.dist(px[j], py[j], bx, by)
                local s2 = arc.dist(px[i], py[i], bx, by) + arc.dist(px[j], py[j], ax, ay)
                local sc = math.min(s1, s2)
                if sc < bd then bd, best = sc, i end
            end
            if best >= 0 then edge_arm[best] = e end
        end
    end

    --  The inward normal of each edge, and whether it carries a band.  A
    --  mouth keeps its normal -- that is the way a meet laid there
    --  runs into the junction -- but carries no margin.
    local nrx, nry, has = {}, {}, {}
    for i = 0, n - 1 do
        local j = (i + 1) % n
        local ex, ey = px[j] - px[i], py[j] - py[i]
        local el = math.sqrt(ex * ex + ey * ey)
        if el < 1e-4 then
            nrx[i], nry[i], has[i] = 0.0, 0.0, false
        else
            local nx, ny = f32(-ey / el), f32(ex / el)
            if not ccw then nx, ny = -nx, -ny end
            nrx[i], nry[i], has[i] = nx, ny, edge_arm[i] < 0
        end
    end

    --  Where the band turns a corner of the ring its offset is MITRED --
    --  the bisector of the two edges' normals, stretched by the angle --
    --  so a lip round a return is one smooth band and not a row of
    --  quads each square to its own edge, which leaves a tooth at every
    --  vertex.  Against a mouth, which carries no band, there is nothing
    --  to mitre with and the edge keeps its own normal.
    local function mitre(own, other)
        local nx, ny = nrx[own], nry[own]
        if not has[other] then return nx, ny end
        local bx, by = f32(nrx[other] + nx), f32(nry[other] + ny)
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
        b:edge(i, has[i], nrx[i], nry[i], edge_arm[i], m0x, m0y, m1x, m1y)
    end

    --  And the ring moved in, where one was asked for.
    local ox, oy, dx, dy = {}, {}, {}, {}
    for i = 0, n - 1 do
        local j = (i + 1) % n
        local inw = edge_arm[i] < 0 and d.width or 0.0
        ox[i], oy[i] = f32(px[i] + f32(nrx[i] * inw)), f32(py[i] + f32(nry[i] * inw))
        dx[i], dy[i] = px[j] - px[i], py[j] - py[i]
    end
    for i = 0, n - 1 do
        local ip = (i - 1) % n
        local x, y = line_meet(ox[ip], oy[ip], dx[ip], dy[ip], ox[i], oy[i], dx[i], dy[i])
        --  Two edges in a line: the moved point serves.
        if not x then x, y = ox[i], oy[i] end
        b:inset(x, y)
    end
    b:close()
    return true
end
