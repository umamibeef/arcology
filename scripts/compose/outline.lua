--  outline.lua -- the shape of a junction.
--
--  A junction is where two or more arms of a network meet.  Which arms
--  those are, and where each one's own path starts and which way it
--  leaves, is read off the map: that is the fit's answer and the
--  pipeline's.  Everything worked out FROM them is here.
--
--    the ORDER    the arms by angle, so "the next arm round" means what
--                 it says;
--    the CORNERS  where one arm's left edge meets the next arm's right,
--                 held within a junction's own reach;
--    the TRIMS    how far along each arm its strip starts, which is the
--                 further of that arm's two corners;
--    the RETURNS  the kerb returns, which push the mouths further out
--                 still so a car has room to turn;
--    the RING     the boundary itself, mouth to corner to mouth.
--
--  Every measurement is taken in the mesh's own precision (arc.f32): the
--  ring is what the asphalt, the footway and every strip that meets this
--  junction are cut against, so a boundary worked out to more places
--  than the mesh can keep is a boundary they disagree about.

local f32 = arc.put.f32
local sqrt, sin, cos, atan2 = arc.put.sqrt, arc.put.sin, arc.put.cos, arc.put.atan2

--  Where two lines meet, or nothing where they run parallel.  `a` and
--  `b` are points, `u` and `v` the directions.
local function line_meet(ax, ay, ux, uy, bx, by, vx, vy)
    local d = ux * vy - uy * vx
    if math.abs(d) < 1e-6 then return nil end
    local t = ((bx - ax) * vy - (by - ay) * vx) / d
    return f32(ax + ux * t), f32(ay + uy * t)
end

arc.rules.outline = function (o)
    local d = o:info()
    local n = d.n
    if n < 1 then return true end

    --  The arms, in the order the tile's edges gave them.
    local arm = {}
    for i = 0, n - 1 do
        local ox, oy, dx, dy, ang, e = o:arm(i)
        arm[i] = {ox = ox, oy = oy, dx = dx, dy = dy, ang = ang, e = e}
    end

    --  By angle, so "the next arm round" means what it says.  An
    --  insertion sort: there are never more than four.
    for i = 1, n - 1 do
        local j, t = i, arm[i]
        while j > 0 and arm[j - 1].ang > t.ang do
            arm[j] = arm[j - 1]
            j = j - 1
        end
        arm[j] = t
    end
    o:order(arm, n)

    --  The corner between each arm and the next: where one's left edge
    --  meets the other's right, held within the tile.
    local corner, met = {}, {}
    for i = 0, n - 1 do
        local a, b = arm[i], arm[(i + 1) % n]
        --  arm i's left edge, and arm j's right
        local a0x = f32(a.ox + f32(-a.dy * d.half))
        local a0y = f32(a.oy + f32(a.dx * d.half))
        local b0x = f32(b.ox - f32(-b.dy * d.half))
        local b0y = f32(b.oy - f32(b.dx * d.half))
        local x, y
        met[i] = true
        if n >= 2 then x, y = line_meet(a0x, a0y, a.dx, a.dy, b0x, b0y, b.dx, b.dy) end
        if not x then
            --  One arm, or two facing each other: the edges never meet,
            --  and the corner is then the corner of the strip itself.
            x, y = f32(a0x + a.dx * d.half), f32(a0y + a.dy * d.half)
            met[i] = false
        end
        local vx, vy = f32(x - d.x), f32(y - d.y)
        local len = sqrt(f32(f32(vx * vx) + f32(vy * vy)))
        --  A pair of arms that leave almost together sends their corner
        --  to infinity; a junction is a tile wide and no more, so a
        --  corner further out than that is pulled in and gets no kerb
        --  return.
        if len > d.far then
            o:clamped(len)
            x, y = f32(d.x + f32(vx / len * d.far)), f32(d.y + f32(vy / len * d.far))
            met[i] = false
        end
        corner[i] = {x = x, y = y}
    end

    --  How far along each arm its strip starts: the further of its two
    --  corners, measured along the arm's own path from where that path
    --  begins, so the number handed back is an arc length the strip can
    --  simply be cut at.  Never past the cap: the junction tile is a
    --  levelled pad, and a strip that starts beyond it starts on ground
    --  the pad's height does not describe.
    local trim = {}
    for e = 0, 3 do trim[e] = d.half end
    local cap = f32(d.cap * d.grow)
    for i = 0, n - 1 do
        local a = arm[i]
        local ca, cb = corner[(i - 1) % n], corner[i]
        local ta = f32(f32(ca.x - a.ox) * a.dx + f32(ca.y - a.oy) * a.dy)
        local tb = f32(f32(cb.x - a.ox) * a.dx + f32(cb.y - a.oy) * a.dy)
        local t = math.max(0, math.min(cap, math.max(ta, tb)))
        trim[a.e] = t
    end

    --  The kerb returns push the mouths out.  At a right angle the
    --  corner where two arms' edges meet IS the mouth line, so a lane
    --  turning there pivots on a point; a car needs room to sweep, which
    --  puts the kerb at the return's tangent distance beyond the corner
    --  along each edge.  Each arm's mouth moves out to its corner's
    --  tangent point, under the same cap, and the ring below rounds the
    --  corner with the arc between them.
    for i = 0, n - 1 do
        local a, b = arm[i], arm[(i + 1) % n]
        local x = corner[i]
        if d.curbs and n >= 2 and met[i] then
            local phi = arc.corner_angle(a.dx, a.dy, b.dx, b.dy)
            --  Asked with only the angle: there is no ring yet to
            --  measure against, and a corner the rule does not round
            --  asks for no room at all.
            local k = arc.rules.corner({col = d.col, row = d.row, phi = phi,
                                        grow = d.grow, width = d.half})
            if type(k) == "table" then
                local L = k.tangent
                local ti = f32(f32(x.x - a.ox) * a.dx + f32(x.y - a.oy) * a.dy + L)
                local tj = f32(f32(x.x - b.ox) * b.dx + f32(x.y - b.oy) * b.dy + L)
                --  The cap says how far an arm may be cut back for the
                --  junction's own sake.  It never holds a mouth SHORT OF
                --  ITS OWN CORNER: a mouth inside the corner leaves a
                --  spike beyond the boundary, and a circle tangent to
                --  both edges then has nowhere to touch.
                --  The cap only ever holds a mouth BACK; it never
                --  pushes one out that did not ask to go.
                local hard = f32(d.far + L)
                local lim = math.min(math.max(cap, ti), hard)
                if ti > lim then ti = lim end
                lim = math.min(math.max(cap, tj), hard)
                if tj > lim then tj = lim end
                if ti > trim[a.e] then trim[a.e] = ti end
                if tj > trim[b.e] then trim[b.e] = tj end
            end
        end
    end
    for e = 0, 3 do o:trim(e, trim[e]) end

    --  The ring: each arm's mouth, right hand first, then whatever the
    --  boundary does at the corner beyond it.
    for i = 0, n - 1 do
        local a, b = arm[i], arm[(i + 1) % n]
        local t = trim[a.e]
        local pix, piy = -a.dy, a.dx
        local mrx = f32(a.ox + f32(a.dx * t) - f32(pix * d.half))
        local mry = f32(a.oy + f32(a.dy * t) - f32(piy * d.half))
        local mlx = f32(a.ox + f32(a.dx * t) + f32(pix * d.half))
        local mly = f32(a.oy + f32(a.dy * t) + f32(piy * d.half))
        local x = corner[i]
        local pjx, pjy = -b.dy, b.dx
        --  The next arm's own right-hand mouth corner: how much room
        --  that side leaves for the return.
        local nrx = f32(b.ox + f32(b.dx * trim[b.e]) - f32(pjx * d.half))
        local nry = f32(b.oy + f32(b.dy * trim[b.e]) - f32(pjy * d.half))
        --  The mouth, right hand first: the road runs on through it and
        --  no footway crosses it.
        o:point(mrx, mry, 1 + a.e)
        o:point(mlx, mly, 1 + a.e)

        local phi = arc.corner_angle(a.dx, a.dy, b.dx, b.dy)
        local ti = f32(f32(mlx - x.x) * a.dx + f32(mly - x.y) * a.dy)
        local tj = f32(f32(nrx - x.x) * b.dx + f32(nry - x.y) * b.dy)
        --  Written out: a corner that falls exactly on the last point
        --  is nought away, and `a and b or c` would read that as no
        --  answer at all.
        local back = 1e9
        if o:count() > 0 then back = o:back(x.x, x.y) end
        local vx, vy = f32(x.x - nrx), f32(x.y - nry)
        local fwd = sqrt(f32(f32(vx * vx) + f32(vy * vy)))
        local k = (d.curbs and n >= 2 and met[i])
                  and arc.rules.corner({col = d.col, row = d.row, phi = phi,
                                        grow = d.grow, width = d.half,
                                        room = math.min(ti, tj), back = back, fwd = fwd})
                  or false
        if type(k) == "table" then
            --  The ring runs along each edge from the mouth to the
            --  tangent point, outward or back as the case needs, and the
            --  arc between them.
            local bsx, bsy = f32(a.dx + b.dx), f32(a.dy + b.dy)
            local bl = sqrt(f32(f32(bsx * bsx) + f32(bsy * bsy)))
            if bl >= 1e-5 then
                local dc = f32(k.radius / sin(f32(0.5 * phi)))
                local steps = math.floor(k.steps + 0.5)
                local ccx, ccy = f32(x.x + f32(bsx / bl * dc)), f32(x.y + f32(bsy / bl * dc))
                local t0x, t0y = f32(x.x + f32(a.dx * k.tangent)), f32(x.y + f32(a.dy * k.tangent))
                local t1x, t1y = f32(x.x + f32(b.dx * k.tangent)), f32(x.y + f32(b.dy * k.tangent))
                local g0 = atan2(f32(t0y - ccy), f32(t0x - ccx))
                local g1 = atan2(f32(t1y - ccy), f32(t1x - ccx))
                local sw = g1 - g0
                while sw > 3.14159265 do sw = sw - 6.2831853 end
                while sw < -3.14159265 do sw = sw + 6.2831853 end
                o:point(t0x, t0y, 0)
                for kk = 1, steps - 1 do
                    local th = g0 + sw * kk / steps
                    o:point(f32(ccx + f32(k.radius * cos(f32(th)))),
                            f32(ccy + f32(k.radius * sin(f32(th)))), 0)
                end
                o:point(t1x, t1y, 0)
            end
        elseif k == false then
            o:point(x.x, x.y, 0)
        end
    end
    o:close()
    return true
end
