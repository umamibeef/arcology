--  fit.lua -- how a fitted path finishes.
--
--  The tangent fit finds the straight runs of a road and puts a vertex
--  where two of them meet.  What is left is two decisions, and they are
--  here: which of those vertices say nothing and can go, and what radius
--  each of the rest is given.
--
--  A vertex says nothing when it stands on top of its neighbour, or when
--  the path runs straight through it.  A vertex a biarc placed is never
--  idle: its tangent length is what the biarc was built with, and moving
--  it would break the pair.
--
--  How the edges are shared out is arc.end_budget's and arc.demand's,
--  in tangent.lua, because the join stage settles the same question at a
--  crossing and the two must answer it the same way.

--  Every step in the mesh's own precision: the fit works in floats, and
--  a budget worked out to more places than it can hold splits an edge a
--  hair differently and moves the arc that sits on it.
local f32 = arc.put.f32
local sqrt, tan, acos = arc.put.sqrt, arc.put.tan, arc.put.acos

local function len(ax, ay, bx, by)
    local dx, dy = f32(bx - ax), f32(by - ay)
    return sqrt(f32(f32(dx * dx) + f32(dy * dy)))
end

arc.rules.fit = function (q)
    local d = q:info()

    --  The idle vertices.  Walked from the second: the first and the
    --  last are the path's own ends and stay whatever they do.  Vertices
    --  count from nought, as every index the pipeline hands out does.
    local n = d.n
    local k = 1
    while k < n do
        local ax, ay = q:at(k - 1)
        local bx, by, fixed = q:at(k)
        local drop = len(ax, ay, bx, by) < 1e-3
        if not drop and k + 1 < n and fixed < 0.0 then
            local cx, cy = q:at(k + 1)
            local la, lb = len(ax, ay, bx, by), len(bx, by, cx, cy)
            if la > 1e-5 and lb > 1e-5 then
                local ux, uy = f32(bx - ax), f32(by - ay)
                local vx, vy = f32(cx - bx), f32(cy - by)
                local dot = f32(f32(f32(ux * vx) + f32(uy * vy)) / f32(la * lb))
                if dot > 0.9999 then drop = true end
            end
        end
        if drop then n = q:drop(k) else k = k + 1 end
    end

    --  Every vertex starts with no arc at all.
    for i = 0, n - 1 do q:corner(i, 0.0, 0.0) end

    for i = 1, n - 2 do
        local ax, ay = q:at(i - 1)
        local bx, by, fixed = q:at(i)
        local cx, cy = q:at(i + 1)
        local lin, lout = len(ax, ay, bx, by), len(bx, by, cx, cy)
        if fixed >= 0.0 then
            --  A biarc vertex: its radius follows from the geometry it
            --  was built with.
            local ux, uy = f32(f32(bx - ax) / lin), f32(f32(by - ay) / lin)
            local vx, vy = f32(f32(cx - bx) / lout), f32(f32(cy - by) / lout)
            local dot = f32(f32(ux * vx) + f32(uy * vy))
            if dot <= 0.9999 then
                local theta = acos(math.max(-1, dot))
                local r = f32(fixed / tan(f32(0.5 * theta)))
                q:corner(i, r, fixed)
                q:tally(r < d.rmin and "tight" or "swept")
            end
        else
            local wk = q:demand(ax, ay, bx, by, cx, cy)
            local bin, bout
            --  The edge behind: the end's own budget where it is the
            --  path's, the biarc's remainder where the vertex behind was
            --  built by one, and otherwise this corner's share of it.
            local _, _, prevf = q:at(i - 1)
            local _, _, nextf = q:at(i + 1)
            if i - 1 == 0 then
                bin = arc.end_budget(lin, d.reserve0, q:need(ax, ay, bx, by, cx, cy), d.trim_cap)
            elseif prevf >= 0.0 then
                bin = f32(lin - prevf)
            else
                local px, py = q:at(i - 2)
                local wo = q:demand(px, py, ax, ay, bx, by)
                bin = f32(wk + wo) > 1e-6 and f32(f32(lin * wk) / f32(wk + wo)) or f32(d.share * lin)
            end
            if i + 1 == n - 1 then
                bout = arc.end_budget(lout, d.reserve1, q:need(ax, ay, bx, by, cx, cy), d.trim_cap)
            elseif nextf >= 0.0 then
                bout = f32(lout - nextf)
            else
                local nx, ny = q:at(i + 2)
                local wo = q:demand(bx, by, cx, cy, nx, ny)
                bout = f32(wk + wo) > 1e-6 and f32(f32(lout * wk) / f32(wk + wo)) or f32(d.share * lout)
            end
            local tl = math.max(0.0, math.min(bin, bout))
            local s = q:sweep(ax, ay, bx, by, cx, cy, tl)
            if s then arc.rules.sweep(s) end
            local r, tight = q:swept()
            q:corner(i, r, tl)
            if r <= 0.0 then
                --  A real turn with no arc is a corner; a straight run
                --  through is neither.
                local dot = f32(f32(f32(f32(bx - ax) * f32(cx - bx)) + f32(f32(by - ay) * f32(cy - by))) / f32(lin * lout))
                if dot < 0.9999 then q:tally("corner") end
            else
                q:tally(tight and "tight" or "swept")
            end
        end
    end
    return true
end
