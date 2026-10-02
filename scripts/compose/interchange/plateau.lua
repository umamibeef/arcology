--  plateau.lua -- where two movements cross, and the flat they share.
--
--  A movement through a node is a way in three dimensions.  Two of them
--  that cross must not do it at the same height, and the one that goes
--  over needs a length of LEVEL running to cross on.  This file measures
--  both: where along each movement a crossing falls, and the flat the
--  upper one is given for it.
--
--  It decides nothing about which movement goes over.  That is the
--  weave planner's (weave.lua), which reads these.

local M = {}

local function sample(pc, step)
    local pts, s = {}, 0.0
    local x0, y0, x1, y1 = 1e9, 1e9, -1e9, -1e9
    for _, p in ipairs(pc) do
        local nd = math.max(1, math.ceil(p.len / step))
        for i = (#pts == 0) and 0 or 1, nd do
            local f = i / nd
            local x, y
            if p.arc then
                local th = p.t0 + (p.t1 - p.t0) * f
                x, y = p.cx + p.r * math.cos(th), p.cy + p.r * math.sin(th)
            else
                x, y = p.ax + (p.bx - p.ax) * f, p.ay + (p.by - p.ay) * f
            end
            pts[#pts + 1] = {x = x, y = y, s = s + p.len * f}
            if x < x0 then x0 = x end
            if x > x1 then x1 = x end
            if y < y0 then y0 = y end
            if y > y1 then y1 = y end
        end
        s = s + p.len
    end
    --  and the box the points lie in, so two ways whose boxes do not
    --  meet are known not to cross without a step of either compared
    pts.x0, pts.y0, pts.x1, pts.y1 = x0, y0, x1, y1
    return pts, s
end

--  Where two straight steps cross, as the distance along each, or
--  nothing where they do not.
local function step_cross(a0, a1, b0, b1)
    local rx, ry = a1.x - a0.x, a1.y - a0.y
    local sx, sy = b1.x - b0.x, b1.y - b0.y
    local den = rx * sy - ry * sx
    if math.abs(den) < 1e-9 then return nil end
    local qx, qy = b0.x - a0.x, b0.y - a0.y
    local t = (qx * sy - qy * sx) / den
    local u = (qx * ry - qy * rx) / den
    if t < 0.0 or t > 1.0 or u < 0.0 or u > 1.0 then return nil end
    return a0.s + (a1.s - a0.s) * t, b0.s + (b1.s - b0.s) * u
end

--  Every crossing of two movements, each as the distance along both.
--  Two ways out of one arm run abreast and never cross: their
--  centrelines are what is tested, and a way narrower than the lanes'
--  pitch touches nothing beside it.
--  The second way's steps are FILED BY THE CELL each one lies in, and
--  a step of the first is then tested against the few that share a
--  cell with it.  Two ways through one node have boxes that overlap
--  almost always, so the box above rejects little and the count of
--  step against step is the whole cost of the search.  A cell a tile
--  across holds a handful of steps, because the sampler puts one every
--  third of a tile.
--
--  The answer is the same as a test of every step against every other.
--  The candidates are read out in ascending order, so a crossing is
--  recorded where it always was, and the arithmetic is untouched.
local CELL = 1.0
local GRID_ROW = 4096

local function file_steps(pb, nb)
    local grid = {}
    for j = 1, nb do
        local q0, q1 = pb[j], pb[j + 1]
        local cx0 = math.floor(math.min(q0.x, q1.x) / CELL)
        local cx1 = math.floor(math.max(q0.x, q1.x) / CELL)
        local cy0 = math.floor(math.min(q0.y, q1.y) / CELL)
        local cy1 = math.floor(math.max(q0.y, q1.y) / CELL)
        for cy = cy0, cy1 do
            for cx = cx0, cx1 do
                local key = cy * GRID_ROW + cx
                local cell = grid[key]
                if not cell then cell = {}; grid[key] = cell end
                cell[#cell + 1] = j
            end
        end
    end
    return grid
end

local function crossings(ma, mb)
    local out = {}
    local pa, pb = ma.pts, mb.pts
    if pa.x0 and pb.x0 and (pa.x1 < pb.x0 or pb.x1 < pa.x0 or pa.y1 < pb.y0 or pb.y1 < pa.y0) then
        return out
    end
    local na, nb = #pa - 1, #pb - 1
    if na < 1 or nb < 1 then return out end
    local grid = file_steps(pb, nb)
    --  `seen` marks a step already gathered for THIS step of the first
    --  way, so a step filed under two cells is tested once.
    local seen, cand = {}, {}
    for i = 1, na do
        local p0, p1 = pa[i], pa[i + 1]
        local cx0 = math.floor(math.min(p0.x, p1.x) / CELL)
        local cx1 = math.floor(math.max(p0.x, p1.x) / CELL)
        local cy0 = math.floor(math.min(p0.y, p1.y) / CELL)
        local cy1 = math.floor(math.max(p0.y, p1.y) / CELL)
        local n = 0
        for cy = cy0, cy1 do
            for cx = cx0, cx1 do
                local cell = grid[cy * GRID_ROW + cx]
                if cell then
                    for ci = 1, #cell do
                        local j = cell[ci]
                        if seen[j] ~= i then
                            seen[j] = i
                            n = n + 1
                            cand[n] = j
                        end
                    end
                end
            end
        end
        --  The list is held between steps to save making one every
        --  time, so the tail of a longer step before this one is cut
        --  off.  A sort reads the whole table.
        for z = n + 1, #cand do cand[z] = nil end
        if n > 1 then table.sort(cand) end
        for ci = 1, n do
            local j = cand[ci]
            local sa, sb = step_cross(p0, p1, pb[j], pb[j + 1])
            if sa then out[#out + 1] = {sa = sa, sb = sb} end
        end
    end
    return out
end

--  THE WEAVE PLANNER, shared: the node lays its ways with it, the
--  matcher that chooses where the ways run scores each choice with it,
--  and the grading pass settles the arms' heights with it over the
--  ways it expects.  `ms` is the node's movements, each with its
--  sampled points `pts` (x, y, s), its length, the heights `za`, `zb`
--  and grades `ma`, `mb` at its two arms, `tc`, `tr` to name it by,
--  and `arm_a`, `arm_b` naming its arms in `opt.arms` where the arms
--  are heights to settle rather than heights given.  `opt` carries the
--  numbers: `grade`, `over`, `over_min`, `lift`, `round`, `crest`,
--  `stack`, `sweeps`; `ground(x, y)` answers the ground under a point,
--  or is absent for a flat reckoning; `arms[key]` is {z, floor, ceil,
--  fixed} for each arm named; `say` takes the report lines, `dump`
--  turns the crossing dump on.  Every movement leaves with `level`,
--  `at` (its crossings) and `along` (its height knots), every arm
--  named leaves with its settled `z`, and the planner answers what it
--  could not do: how many crossings were too near an arm to weave, how
--  many could not be lifted the least gap, the smallest gap it laid,
--  and the steepest run.
--
--  All of it is ONE RELAXATION over the graph of heights
--  (scripts/compose/relax.lua): the arms, and the knots along each way,
--  a crossing at the knot nearest it.  A way's knots stay over the line
--  between its arms, held over the ground under them by the slab's
--  lift, and within the grade of each other; a way crossing over
--  another stands the gap above it, as a plateau either side of the
--  crossing so the vertical curves round its shoulders and not the
--  clearance.  Which of two ways goes over is a LEVEL: the lowest
--  colour no way it crosses has, taken by the ways with the least room
--  to climb first.  The heights are then the lowest that obey every
--  rule, and an arm free to rise rises only as far as a rule asks.
--  LEVELS AS PLATEAUS.  Every way has ONE shape: it leaves its arm at
--  that deck's height, climbs once at the grade to its level, holds
--  it, and descends once to the deck it arrives at.  A through way,
--  whose two arms stand at one deck, is flat.  A way that crosses
--  nothing is a plain ramp between the two decks.  The levels are whole
--  steps of the gap: the two decks, then flyovers above the upper deck
--  and an underpass beneath the lower one, where the ground leaves the
--  room.  A way takes the first level at which every way it crosses,
--  among those already given theirs, stands the gap from it at the
--  crossing; the ways with the least length are given theirs first.
--  And the crossing must lie ON BOTH PLATEAUS, not on a climb: a way
--  crossing another on its knee is counted, and a level where every
--  crossing lies on the flat is taken before one where some do not.
--  That puts the knees at the arms and the crossings in the core.  A
--  way too short to reach its level at the grade climbs as far as it
--  can, is counted short of its level, and is never bent into a hump.
local function plateau_plan(ms, opt, grade)
    local over, over_min = opt.over, opt.over_min
    local girder = arc.geo.interchange_girder
    local lo, hi = math.huge, -math.huge
    for _, m in ipairs(ms) do
        lo = math.min(lo, m.za, m.zb)
        hi = math.max(hi, m.za, m.zb)
    end
    --  the knots: a quarter tile apart, each with the ground under it
    for _, m in ipairs(ms) do
        local step = math.max(0.25, m.len / 60)
        local nk = math.max(2, math.ceil(m.len / step))
        m.ks = {}
        for q = 0, nk do
            local at = math.min(m.len, q * m.len / nk)
            local best, bd = m.pts[1], 1e9
            for _, pt in ipairs(m.pts) do
                local dd = math.abs(pt.s - at)
                if dd < bd then best, bd = pt, dd end
            end
            local gz = opt.ground and opt.ground(best.x, best.y) or -math.huge
            m.ks[#m.ks + 1] = {s = at, x = best.x, y = best.y, ground = gz}
        end
    end
    --  a way's profile at a level: the envelope of the two climbs and
    --  the plateau, the knees rounded, the plateau and the ends held
    local function profile(m, zl)
        local z, flat = {}, {}
        local n = #m.ks
        --  level over the holds at each end, where the way still runs
        --  on or beside its slab; the climb and the ramp begin past them
        local ha, hb = m.hold_a or 0.0, m.hold_b or 0.0
        local run = math.max(0.05, m.len - ha - hb)
        if zl == nil then
            --  a ramp or a flat through is on its own plateau throughout
            for q = 1, n do flat[q] = true end
        end
        for q, k in ipairs(m.ks) do
            local sa = math.max(0.0, k.s - ha)
            local sb = math.max(0.0, m.len - k.s - hb)
            if zl == nil then
                local t = math.min(1.0, sa / run)
                z[q] = m.za + (m.zb - m.za) * t
            elseif zl >= math.max(m.za, m.zb) then
                local up, dn = m.za + grade * sa, m.zb + grade * sb
                z[q] = math.min(up, zl, dn)
                flat[q] = up >= zl and dn >= zl
            else
                local up, dn = m.za - grade * sa, m.zb - grade * sb
                z[q] = math.max(up, zl, dn)
                flat[q] = up <= zl and dn <= zl
            end
        end
        for _ = 1, opt.round or 0 do
            local prev = z[1]
            for q = 2, n - 1 do
                local cur = z[q]
                z[q] = 0.25 * prev + 0.5 * cur + 0.25 * z[q + 1]
                prev = cur
            end
        end
        z[1], z[n] = m.za, m.zb
        for q = 1, n do
            if flat[q] and zl then z[q] = zl end
        end
        return z, flat
    end
    --  whether a point along a way lies on its plateau: both knots
    --  about it are flat
    local function on_flat(m, s)
        local ks = m.ks
        for q = 2, #ks do
            if s <= ks[q].s then return m.flat_k[q - 1] and m.flat_k[q] end
        end
        return m.flat_k[#ks]
    end
    local function z_at(m, s)
        local ks = m.ks
        for q = 2, #ks do
            if s <= ks[q].s then
                local f = (s - ks[q - 1].s) / math.max(1e-6, ks[q].s - ks[q - 1].s)
                return m.z[q - 1] + (m.z[q] - m.z[q - 1]) * f
            end
        end
        return m.z[#ks]
    end
    --  whether a profile keeps its body over the ground
    local function grounded(m, z)
        for q, k in ipairs(m.ks) do
            if z[q] - girder - 0.05 < k.ground then return false end
        end
        return true
    end
    --  the order: the throughs, flat at their deck, then the rest by
    --  length, the shortest first
    local order = {}
    for i, m in ipairs(ms) do
        m.flat = math.abs(m.za - m.zb) < 1e-3
        order[#order + 1] = i
    end
    table.sort(order, function (p, q)
        local a, b = ms[p], ms[q]
        if a.flat ~= b.flat then return a.flat end
        if a.len ~= b.len then return a.len < b.len end
        return p < q
    end)
    local short, least, knees, low = 0, over, 0, 0
    for _, i in ipairs(order) do
        local m = ms[i]
        local cands
        if m.flat then
            cands = {{nil, m.za <= lo + 1e-3 and 0 or 1}}
        else
            cands = {{nil, 0}, {hi + over, 2}, {lo - over, -1}, {hi + 2 * over, 3}, {lo - 2 * over, -2}}
        end
        local best_z, best_flat, best_lv, best_bad, best_knee = nil, nil, 0, math.huge, math.huge
        for _, c in ipairs(cands) do
            local z, flat = profile(m, c[1])
            local ok = c[1] == nil or c[1] > lo or grounded(m, z)
            --  a level the way cannot reach has no flat to stand on
            local reached = c[1] == nil
            for q = 1, #flat do if flat[q] then reached = true end end
            if ok and reached then
                m.z, m.flat_k = z, flat
                local bad, knee = 0, 0
                for _, x in ipairs(m.at) do
                    local o = ms[x.other]
                    if o.z then
                        local got = math.abs(z_at(m, x.s) - z_at(o, x.so))
                        if got < over_min then bad = bad + 1 end
                        if not (on_flat(m, x.s) and on_flat(o, x.so)) then knee = knee + 1 end
                    end
                end
                if bad < best_bad or (bad == best_bad and knee < best_knee) then
                    best_z, best_flat, best_lv, best_bad, best_knee = z, flat, c[2], bad, knee
                end
                if bad == 0 and knee == 0 then break end
            end
        end
        if not best_z then
            --  no level within reach: the way climbs to the first as
            --  far as it can
            local c = cands[math.min(2, #cands)]
            best_z, best_flat, best_lv = profile(m, c[1])
            best_lv = c[2]
            low = low + 1
        end
        m.z, m.flat_k, m.level = best_z, best_flat, best_lv
        m.lifts = {}
        for _, x in ipairs(m.at) do
            local o = ms[x.other]
            if o.z then
                local got = z_at(m, x.s) - z_at(o, x.so)
                local knee = not (on_flat(m, x.s) and on_flat(o, x.so))
                m.lifts[#m.lifts + 1] = string.format("%s %d by %.2f%s", got >= 0 and "over" or "under",
                                                      x.other, math.abs(got), knee and " on a climb" or "")
                if math.abs(got) < over_min then short = short + 1
                elseif math.abs(got) < least then least = math.abs(got) end
                if knee then knees = knees + 1 end
            end
        end
    end
    --  the seat for the loft, and the steepest run
    local steepest = 0.0
    for _, m in ipairs(ms) do
        local along = {}
        for q, k in ipairs(m.ks) do along[q] = {s = k.s, z = m.z[q]} end
        for q = 2, #along - 1 do
            along[q].dz = (along[q + 1].z - along[q - 1].z) / math.max(1e-6, along[q + 1].s - along[q - 1].s)
        end
        along[1].dz, along[#along].dz = m.ma, m.mb
        m.along = along
        local steep = 0.0
        for q = 2, #along do
            local g2 = math.abs(along[q].z - along[q - 1].z) / math.max(1e-6, along[q].s - along[q - 1].s)
            if g2 > steep then steep = g2 end
        end
        m.steep = steep
        if steep > steepest then steepest = steep end
    end
    if opt.say and (short > 0 or least < over or knees > 0 or low > 0) then
        opt.say(string.format(
            "interchange %d,%d: %d crossings stand less than the least gap apart, %d lie on a climb, %d ways cannot reach their level; the smallest gap laid is %.2f of a level, %.2f asked",
            ms[1].tc, ms[1].tr, short, knees, low, least, over))
    end
    return {unweavable = 0, short = short, least = least, steepest = steepest, knee = knees, low = low}
end


M.sample       = sample
M.crossings    = crossings
M.plateau_plan = plateau_plan
return M
