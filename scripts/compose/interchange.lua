--  interchange.lua -- the 2x2 interchange, and the globs of them.
--
--  An interchange tile is a NODE that band segments meet at.  A 2x2
--  is the smallest one; tiles touching each other make ONE node with a
--  larger area, not several -- twelve tiles in an L are a single place
--  where four ways meet, and cutting them into three 2x2 blocks
--  invents nodes the map never had and leaves the arms hanging.
--
--  So a node is a connected GLOB of them, and its ARMS are the band
--  edges that touch its boundary.  Every arm must be able to reach every
--  other: that is what the node is for.
--
--  This file finds them and lays the slab they meet on.  Which lane goes
--  on to which across it is scripts/compose/links.lua's.

local STEP = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}}

--  Is this byte a band that a node may have as an arm?
local function is_way(t)
    return t ~= nil and t.kind ~= nil
end

--  Every node on the map: its cells, the tile it is centred on, and the
--  arm cells that touch it.  Answers a list, and a lookup from a tile to
--  the node it belongs to or touches.
function arc.interchange_nodes()
    local xbld, n = arc.city.plane("xbld")
    local tiles   = arc.band_tiles
    local nodes, of_tile = {}, {}
    if not tiles then return nodes, of_tile, n end
    local seen = {}
    for at = 0, n * n - 1 do
        local t = tiles[xbld[at]]
        if t and t.kind == "junction" and not seen[at] then
            --  The glob, and the CURVE BLOCKS that touch it.  A curve
            --  block against an interchange is not an arm arriving at
            --  it: it is part of the same place, the loop that carries
            --  one way round inside the node.  Counted as an arm
            --  it becomes a fifth edge that nothing can ever join, and
            --  the real arm behind it -- the spurs it leads to -- is
            --  hidden.  A block only joins the glob through a junction
            --  tile or through another block already in it, so a corner
            --  out on a band stays a corner.
            local cells, stack = {}, {at}
            seen[at] = true
            while #stack > 0 do
                local q = table.remove(stack)
                cells[#cells + 1] = q
                local qc, qr = q % n, q // n
                for _, s in ipairs(STEP) do
                    local cc, cr = qc + s[1], qr + s[2]
                    if cc >= 0 and cr >= 0 and cc < n and cr < n then
                        local p = cr * n + cc
                        local u = tiles[xbld[p]]
                        local me = tiles[xbld[q]]
                        --  A curve block joins the node only where it
                        --  touches a JUNCTION tile and leads NOWHERE
                        --  ELSE: a block wedged against the interchange
                        --  with no band beyond it is the loop inside the
                        --  node, and counted as an arm it is an edge
                        --  nothing can ever join.  A block that carries a
                        --  band on -- away from the node, or round to
                        --  another arm -- is that band's own corner: the
                        --  band turns through it and arrives at the
                        --  interchange's face, and the arm is there.
                        --  Swallowed instead, the node's arms stand at
                        --  the far ends of the turns and its ways run
                        --  straight across whatever lies between.
                        if u and not seen[p] and u.kind == "junction" then
                            seen[p] = true
                            stack[#stack + 1] = p
                        elseif u and u.kind == "curve" and me and me.kind == "junction" then
                            --  A curve block is a BLOCK: four tiles of
                            --  one id.  Touch any of them and the whole
                            --  block joins, or the corner that touches
                            --  the glob only diagonally is left behind
                            --  as a one-cell edge nothing can reach.
                            local b2 = xbld[p]
                            local bc, br = cc, cr
                            if bc > 0 and xbld[br * n + bc - 1] == b2 then bc = bc - 1 end
                            if br > 0 and xbld[(br - 1) * n + bc] == b2 then br = br - 1 end
                            --  whether any band tile lies beyond the block
                            local beyond = false
                            for _, q2 in ipairs {br * n + bc, br * n + bc + 1,
                                                 (br + 1) * n + bc, (br + 1) * n + bc + 1} do
                                local c2, r2 = q2 % n, q2 // n
                                for _, s2 in ipairs(STEP) do
                                    local c3, r3 = c2 + s2[1], r2 + s2[2]
                                    if c3 >= 0 and r3 >= 0 and c3 < n and r3 < n then
                                        local p3 = r3 * n + c3
                                        local u3 = tiles[xbld[p3]]
                                        if u3 and u3.kind ~= "junction" and xbld[p3] ~= b2
                                           and (c3 < bc or c3 > bc + 1 or r3 < br or r3 > br + 1) then
                                            beyond = true
                                        end
                                    end
                                end
                            end
                            if not beyond then
                                for _, q2 in ipairs {br * n + bc, br * n + bc + 1,
                                                     (br + 1) * n + bc, (br + 1) * n + bc + 1} do
                                    if not seen[q2] and xbld[q2] == b2 then
                                        seen[q2] = true
                                        stack[#stack + 1] = q2
                                    end
                                end
                            end
                        end
                    end
                end
            end
            --  The arms: every band cell touching the glob that is
            --  not part of it.
            --  The boundary cells, each with the SIDE of the glob it
            --  lies off: an arm is a side, and two arms whose cells
            --  happen to touch -- one leaving north, one leaving east,
            --  meeting at a corner -- are two arms and not one.  Grouped
            --  by adjacency alone they merge, and every movement between
            --  them is skipped as a movement from an arm to itself.
            local inside, arms, side = {}, {}, {}
            for _, q in ipairs(cells) do inside[q] = true end
            for _, q in ipairs(cells) do
                local qc, qr = q % n, q // n
                for si, s in ipairs(STEP) do
                    local cc, cr = qc + s[1], qr + s[2]
                    if cc >= 0 and cr >= 0 and cc < n and cr < n then
                        local p = cr * n + cc
                        if not inside[p] and is_way(tiles[xbld[p]]) then
                            arms[p] = true
                            side[p] = side[p] or si
                        end
                    end
                end
            end
            --  The arms as EDGES, not as cells: boundary cells touching
            --  each other are one edge of the node, and each is a place a
            --  way arrives.  A band is not an arm -- one band can
            --  enter by one edge and leave by another, and counting by
            --  band collapses those two into one and skips every
            --  movement between them.
            local edges, taken = {}, {}
            for q in pairs(arms) do
                if not taken[q] then
                    local group, st = {}, {q}
                    taken[q] = true
                    while #st > 0 do
                        local w = table.remove(st)
                        group[#group + 1] = w
                        local wc, wr = w % n, w // n
                        for _, sp in ipairs(STEP) do
                            local cc, cr = wc + sp[1], wr + sp[2]
                            if cc >= 0 and cr >= 0 and cc < n and cr < n then
                                local pp = cr * n + cc
                                if arms[pp] and not taken[pp]
                                   and side[pp] == side[q] then
                                    taken[pp] = true
                                    st[#st + 1] = pp
                                end
                            end
                        end
                    end
                    --  A band is TWO tiles across, so an arm is two
                    --  boundary cells.  A longer run on one side is two
                    --  ways arriving abreast, and counting it as
                    --  one arm hides every movement between them.  The
                    --  run is ordered along the side and cut in twos.
                    table.sort(group)
                    local pairs_ = {}
                    for gi = 1, #group, 2 do
                        local part = {group[gi]}
                        if group[gi + 1] then part[2] = group[gi + 1] end
                        pairs_[#pairs_ + 1] = part
                    end
                    for _, part in ipairs(pairs_) do
                        local px2, py2 = 0.0, 0.0
                        for _, w in ipairs(part) do
                            px2 = px2 + (w % n) + 0.5
                            py2 = py2 + (w // n) + 0.5
                        end
                        edges[#edges + 1] = {cells = part,
                                             x = px2 / #part, y = py2 / #part}
                    end
                end
            end
            local sx, sy = 0.0, 0.0
            for _, q in ipairs(cells) do
                sx = sx + (q % n) + 0.5
                sy = sy + (q // n) + 0.5
            end
            local k = #nodes + 1
            nodes[k] = {cells = cells, inside = inside, arms = arms,
                        edges = edges, n = n,
                        x = sx / #cells, y = sy / #cells}
            --  A band's cell against the node stands on the node's own
            --  tiles or near them.  A band runs its slab out half a tile
            --  past its last cell and its lanes sit up to a half width
            --  across that, so an end of the arm can land two tiles from
            --  the glob, and the approach the band leaves to the node
            --  puts it further out by that many cells; filed under a
            --  ring narrower than that, the band reads as ending at no
            --  node.  The ring reaches no further: two nodes a few tiles
            --  apart would each file the other's bands.  A lane's end,
            --  which the fan can put further out still, is filed by its
            --  band's record and not by this ring
            --  (scripts/compose/links.lua).
            local halo = 2 + math.floor(arc.geo.interchange_approach + 0.5)
            for _, q in ipairs(cells) do
                of_tile[q] = k
                local qc, qr = q % n, q // n
                for dc = -halo, halo do
                    for dr = -halo, halo do
                        local cc, cr = qc + dc, qr + dr
                        if cc >= 0 and cr >= 0 and cc < n and cr < n then
                            local p = cr * n + cc
                            if of_tile[p] == nil then of_tile[p] = k end
                        end
                    end
                end
            end
        end
    end
    return nodes, of_tile, n
end

--  WHAT A NODE IS MADE OF.
--
--  Not a paved square.  A node is the MOVEMENTS through it, and each
--  one is a way of its own, one lane wide, following the very line
--  links.lua fitted for it.  Between them the ground shows, which is
--  what the loops of an interchange have inside them.  Paved as one
--  slab over every cell of the glob, the node reads as a gigantic road
--  and nothing of its shape survives.
--
--  And the ways WEAVE.  Where two movements cross, one is carried over
--  the other: every movement is given a LEVEL such that no two that
--  cross share one, and at each crossing the way on the higher level
--  stands arc.geo.interchange_over above the lower.  Away from its
--  crossings a movement runs at the height of the arm it leaves and
--  comes down to the height of the arm it reaches, both read off the
--  stations the lofts filed, since the arms of a node do not stand at
--  one height.  Laid at one height instead, every movement is one
--  plane, and the ways cross inside it rather than over one another.
local function arm_height(w, x, y)
    local z, away = w:slab_near(x, y)
    if z and away <= arc.geo.interchange_reach then return z end
    local gz = arc.city.at("surface", math.floor(x), math.floor(y))
    return gz and gz + arc.geo.slab_lift or nil
end

--  The slab's grade where a way leaves it or joins it, as the change
--  of height per tile ALONG THE WAY: read back into the slab from the
--  end by the tangent's length, so the way leaves at the slab's own
--  slope and the profile has no crease at the join.  `dx, dy` is the
--  way's direction at that end, into the node; the slab lies the other
--  way from the start and the same way from the end.
local function arm_slope(w, x, y, dx, dy, into)
    local t  = arc.geo.interchange_tangent
    local z0 = arm_height(w, x, y)
    local z1
    if into then z1 = arm_height(w, x - dx * t, y - dy * t)
    else z1 = arm_height(w, x + dx * t, y + dy * t) end
    if not z0 or not z1 then return 0.0 end
    return into and (z0 - z1) / t or (z1 - z0) / t
end

--  A chain of pieces as points along it, each with its distance along.
--  A piece's `b` is its end whether it runs straight or turns: an arc's
--  centre is `c`, and its angles run t0 to t1.
arc.weave = arc.weave or {}
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
local function crossings(ma, mb)
    local out = {}
    local pa, pb = ma.pts, mb.pts
    if pa.x0 and pb.x0 and (pa.x1 < pb.x0 or pb.x1 < pa.x0 or pa.y1 < pb.y0 or pb.y1 < pa.y0) then
        return out
    end
    for i = 1, #ma.pts - 1 do
        for j = 1, #mb.pts - 1 do
            local sa, sb = step_cross(ma.pts[i], ma.pts[i + 1], mb.pts[j], mb.pts[j + 1])
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
    local girder = arc.geo.interchange_girder or 0.0
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

arc.weave.sample = sample
function arc.weave.plan(ms, opt)
    local grade  = opt.grade
    local arms   = opt.arms or {}
    local sweeps = opt.sweeps or 64
    for _, m in ipairs(ms) do m.at = {} end
    --  The crossings, filed under both movements, each knowing its
    --  pair.  Two ways out of one arm, or into one, meet at that arm
    --  and part: that is no crossing, and neither is one within a tile
    --  of the arm they share.
    local function near_arm(m, s, key)
        return key ~= nil and (m.arm_a == key and s < 1.0 or m.arm_b == key and m.len - s < 1.0)
    end
    local function file(a, b, i, j, sa, sb)
        local xa = {s = sa, other = j, so = sb}
        local xb = {s = sb, other = i, so = sa}
        xa.pair, xb.pair = xb, xa
        a.at[#a.at + 1] = xa
        b.at[#b.at + 1] = xb
    end
    for i = 1, #ms do
        for j = i + 1, #ms do
            local a, b = ms[i], ms[j]
            for _, x in ipairs(crossings(a, b)) do
                local shared = near_arm(a, x.sa, a.arm_a) and near_arm(b, x.sb, a.arm_a)
                            or near_arm(a, x.sa, a.arm_b) and near_arm(b, x.sb, a.arm_b)
                if not shared then file(a, b, i, j, x.sa, x.sb) end
            end
            --  AND THE NEAR MISSES.  Two ways whose bodies would touch
            --  without their lines crossing -- side by side over one
            --  spot, or crossing so shallowly that the bodies overlap for
            --  a stretch -- are a crossing all the same.  Every run of
            --  samples of the one within a body's width of the other is
            --  filed as crossings every half tile along it, so the lift
            --  covers the whole of the overlap.
            if opt.near and a.pts.x0 and b.pts.x0
               and not (a.pts.x1 + opt.near < b.pts.x0 or b.pts.x1 + opt.near < a.pts.x0
                        or a.pts.y1 + opt.near < b.pts.y0 or b.pts.y1 + opt.near < a.pts.y0) then
                local run
                local function flush()
                    if not run then return end
                    local n = math.max(1, math.floor((run.s1 - run.s0) / 0.5 + 0.5))
                    for q = 1, n do
                        local f  = (q - 0.5) / n
                        local sa = run.s0 + (run.s1 - run.s0) * f
                        local sb = run.b0 + (run.b1 - run.b0) * f
                        local dup = false
                        for _, x in ipairs(a.at) do
                            if x.other == j and math.abs(x.s - sa) < 0.3 then dup = true break end
                        end
                        if not dup then file(a, b, i, j, sa, sb) end
                    end
                    run = nil
                end
                for _, pt in ipairs(a.pts) do
                    local best, bs = opt.near * opt.near, nil
                    for q = 1, #b.pts - 1 do
                        local p0, p1 = b.pts[q], b.pts[q + 1]
                        local ex, ey = p1.x - p0.x, p1.y - p0.y
                        local ll = ex * ex + ey * ey
                        local tt = ll > 1e-12 and ((pt.x - p0.x) * ex + (pt.y - p0.y) * ey) / ll or 0.0
                        if tt < 0.0 then tt = 0.0 elseif tt > 1.0 then tt = 1.0 end
                        local dx, dy = pt.x - (p0.x + ex * tt), pt.y - (p0.y + ey * tt)
                        local dd = dx * dx + dy * dy
                        if dd < best then best, bs = dd, p0.s + (p1.s - p0.s) * tt end
                    end
                    if bs then
                        if run then run.s1, run.b1 = pt.s, bs
                        else run = {s0 = pt.s, s1 = pt.s, b0 = bs, b1 = bs} end
                    else
                        flush()
                    end
                end
                flush()
            end
        end
    end

    --  Levels as plateaus, where the caller asks: no graph, no lifts
    --  at the crossings (plateau_plan).
    if opt.plateau then return plateau_plan(ms, opt, grade) end

    --  THE GRAPH.  An arm named is one node, shared by every way that
    --  leaves or reaches it; an end with no arm named is a node of its
    --  own, pinned at the height given.
    local g = {nodes = {}, ways = {}, overs = {}}
    local nodes = g.nodes
    local function node(z, floor, ceil, fixed)
        nodes[#nodes + 1] = {z = z, z0 = z, floor = floor, ceil = ceil, fixed = fixed}
        return #nodes
    end
    local arm_id = {}
    local function arm_node(key, z)
        local a = key and arms[key]
        if not a then return node(z, z, z, true) end
        if not arm_id[key] then
            arm_id[key] = node(a.z, a.floor, a.ceil or math.huge, a.fixed)
        end
        return arm_id[key]
    end
    --  A knot every quarter tile or so along each way, standing on the
    --  ground under it by the lift, and each crossing at the knot
    --  nearest it.
    local lift = opt.lift or 0.0
    for _, m in ipairs(ms) do
        local step = math.max(0.25, m.len / 60)
        local nk = math.max(2, math.ceil(m.len / step))
        local ks = {}
        m.ida, m.idb = arm_node(m.arm_a, m.za), arm_node(m.arm_b, m.zb)
        local ids, ss = {}, {}
        for q = 0, nk do
            local at = math.min(m.len, q * m.len / nk)
            local best, bd = m.pts[1], 1e9
            for _, pt in ipairs(m.pts) do
                local dd = math.abs(pt.s - at)
                if dd < bd then best, bd = pt, dd end
            end
            local k = {s = at, x = best.x, y = best.y}
            --  A knot within the HOLD of an arm is the arm's own node:
            --  the way runs on the slab's flare there, level with it,
            --  and climbs only past the nose (scripts/compose/drop.lua).
            if q == 0 or at <= (m.hold_a or 0.0) + 1e-6 then k.id = m.ida
            elseif q == nk or m.len - at <= (m.hold_b or 0.0) + 1e-6 then k.id = m.idb
            else
                local gz = opt.ground and opt.ground(best.x, best.y)
                local floor = gz and gz + lift or -math.huge
                k.id = node(floor, floor, math.huge, false)
            end
            ks[#ks + 1] = k
            ids[#ids + 1], ss[#ss + 1] = k.id, at
        end
        for _, x in ipairs(m.at) do
            local best, bd = nil, 1e9
            for _, k in ipairs(ks) do
                local dd = math.abs(k.s - x.s)
                if dd < bd then best, bd = k, dd end
            end
            x.knot = best
        end
        --  The grade holds along the way, unless both its arms are
        --  built and lie further apart than the grade joins over the
        --  way's length: then the way is the straight ramp between
        --  them, steeper than the grade by as little as it must be,
        --  and never a run at the grade with the rest in one step.
        local na, nb = nodes[m.ida], nodes[m.idb]
        local need = math.abs(nb.z - na.z) / math.max(1e-6, m.len)
        m.ks  = ks
        m.way = {ids = ids, s = ss, chord = true,
                 graded = not (na.fixed and nb.fixed and need > grade)}
        g.ways[#g.ways + 1] = m.way
    end

    --  THE ARMS AGREE.  Where the arms are free they are tied at one
    --  height: a way's knots along it, all at no distance from each
    --  other, which the grade holds level.  Four arms meeting a level
    --  apart leave no grade for one way to climb over another; four
    --  meeting at one height do.  So the arms stand at the highest
    --  floor among them, and rise together only where a way between
    --  them has to clear the ground it crosses.
    --  Tied per GROUP where the caller names groups: the arms of one
    --  highway are tied, and where two highways meet, the arms of one
    --  stand `opt.tie.over` above the other's, so the through ways of
    --  the two never cross at one height and the turns climb between
    --  the levels over their whole length.  The group named first
    --  goes over.
    local fixed_overs = {}
    if opt.tie then
        local groups = type(opt.tie) == "table" and opt.tie.groups or {arm_id}
        local tied = {}
        for gi, grp in ipairs(groups) do
            local ids, ss = {}, {}
            for key in pairs(grp) do
                if arm_id[key] then ids[#ids + 1] = arm_id[key] end
            end
            table.sort(ids)
            for q = 1, #ids do ss[q] = 0.0 end
            if #ids > 1 then g.ways[#g.ways + 1] = {ids = ids, s = ss, graded = true} end
            tied[gi] = ids
        end
        if type(opt.tie) == "table" and opt.tie.over and #tied >= 2 then
            for _, up in ipairs(tied[1]) do
                for _, low in ipairs(tied[2]) do
                    fixed_overs[#fixed_overs + 1] = {up = up, low = low, gap = opt.tie.over}
                end
            end
        end
    end
    g.overs = fixed_overs

    --  EACH WAY'S OWN PROFILE, before the weave: the relaxation with no
    --  crossing in it.
    arc.relax(g, grade, sweeps)

    --  How far a way can be lifted at a point along it: the grade over
    --  the run from each arm, as the arm stands.  A lift never raises
    --  an arm: two ways out of one arm rise with it and are no further
    --  apart, so a crossing the grade cannot clear from the arms as
    --  they are is one it cannot clear at all.
    --  How high a way can stand at a distance along it: the grade
    --  climbed from the nearer end, counted from the end of its hold
    --  and not from the arm, since the way is level over the hold.
    local function cap_at(m, s)
        local fa = math.max(0.0, s - (m.hold_a or 0.0))
        local fb = math.max(0.0, m.len - s - (m.hold_b or 0.0))
        return math.min(nodes[m.ida].z + grade * fa, nodes[m.idb].z + grade * fb)
    end
    --  At a crossing only one of the two may have the room to be
    --  lifted the least gap over the other: then it is the one that
    --  goes over, and that is a FORCED pair the levels must honour.
    --  Where neither has the room the crossing is UNWEAVABLE at the
    --  grade: it is left out, so it does not flatten every other, and
    --  it is counted and said.
    local unweavable = 0
    for i = 1, #ms do
        local keep = {}
        ms[i].room, ms[i].over = 1e9, {}
        for _, x in ipairs(ms[i].at) do
            local o = ms[x.other]
            local floor_z = math.max(nodes[x.knot.id].z, nodes[x.pair.knot.id].z)
            x.floor  = floor_z
            x.budget = cap_at(ms[i], x.s) - floor_z
            local other_can = cap_at(o, x.so) - floor_z >= opt.over_min
            local self_can  = x.budget >= opt.over_min
            if self_can or other_can then
                keep[#keep + 1] = x
                if x.budget < ms[i].room then ms[i].room = x.budget end
                --  the other must go over this one: this one is below it
                if other_can and not self_can then ms[i].over[x.other] = true end
            elseif x.other > i then
                unweavable = unweavable + 1
                if opt.dump then
                    arc.dump(string.format(
                        "NEAR %d,%d: ways %d and %d cross at %.2f of %.2f and %.2f of %.2f, floor %.2f, room %.2f and %.2f",
                        ms[1].tc, ms[1].tr, i, x.other, x.s, ms[i].len, x.so, o.len, floor_z,
                        x.budget, cap_at(o, x.so) - floor_z))
                end
            end
        end
        ms[i].at = keep
    end

    --  A level for each: the lowest colour no movement it crosses has
    --  taken yet, so two that cross never share one.  A colour is a
    --  level UP from the floor at each crossing, and only a way ABOVE
    --  its floor is lifted, so the ways with the least room to climb
    --  are coloured first and take the low levels; ties go in the
    --  order the movements were laid, so the answer is the same every
    --  build.
    --  The forced pairs first: a way is coloured only once every way
    --  it must lie under has its level, and takes a level above them
    --  all.  Among the ways ready to colour, the one with the least
    --  room goes first.  A ring of forced pairs -- A under B under C
    --  under A -- cannot be honoured, and its ways are coloured in
    --  the order they were laid.
    local done, left = {}, #ms
    while left > 0 do
        local pick
        for i = 1, #ms do
            if not done[i] then
                --  ready when every way it goes OVER is coloured
                local ready = true
                for _, x in ipairs(ms[i].at) do
                    if ms[x.other].over[i] and not done[x.other] then ready = false end
                end
                if ready and (not pick or ms[i].room < ms[pick].room) then pick = i end
            end
        end
        if not pick then
            for i = 1, #ms do
                if not done[i] then pick = i break end
            end
        end
        local used, floor_lv = {}, 0
        for _, x in ipairs(ms[pick].at) do
            local oc = ms[x.other].level
            if oc then
                used[oc] = true
                if ms[x.other].over[pick] and oc + 1 > floor_lv then floor_lv = oc + 1 end
            end
        end
        local c = floor_lv
        while used[c] do c = c + 1 end
        ms[pick].level = c
        done[pick], left = true, left - 1
    end

    --  THE WEAVE: at each crossing the way on the higher level stands
    --  the gap over the other's knot there, and so does every knot of
    --  it within `crest` of the crossing, so the clearance is a
    --  plateau.  A hair per movement on the lifted knots alone: two
    --  ways at one height are one surface drawn twice where they
    --  touch, which the depth test settles differently from pixel to
    --  pixel; at the arms the hair would lift a way off the slab it
    --  leaves.
    local over, over_min = opt.over, opt.over_min
    local crest = opt.crest or 0.0
    local xs = {}
    for i, m in ipairs(ms) do
        m.lifts = {}
        for _, x in ipairs(m.at) do
            if ms[x.other].level < m.level then
                x.gap   = over + (i - 1) * opt.stack
                x.overs = {}
                for _, k in ipairs(m.ks) do
                    if math.abs(k.s - x.s) <= crest and k.s > 1e-6 and k.s < m.len - 1e-6 then
                        x.overs[#x.overs + 1] = {up = k.id, low = x.pair.knot.id,
                                                 gap = x.gap, cap = cap_at(m, k.s)}
                    end
                end
                xs[#xs + 1] = {m = m, x = x, i = i}
            end
        end
    end
    --  The heights settled over all of it.  A crossing lifted short of
    --  the least gap a car clears is no crossing to lift: it is
    --  dropped, and counted, and the heights are settled again without
    --  it.
    local short = 0
    local function settle()
        g.overs = {}
        for _, ov in ipairs(fixed_overs) do g.overs[#g.overs + 1] = ov end
        for _, e in ipairs(xs) do
            if not e.x.dropped then
                for _, ov in ipairs(e.x.overs) do g.overs[#g.overs + 1] = ov end
            end
        end
        arc.relax(g, grade, sweeps)
    end
    local rounds = 0
    repeat
        settle()
        local dropped = false
        for _, e in ipairs(xs) do
            if not e.x.dropped then
                local got = nodes[e.x.knot.id].z - nodes[e.x.pair.knot.id].z
                if got < over_min - 1e-6 then
                    e.x.dropped, dropped = true, true
                    short = short + 1
                end
            end
        end
        if dropped then
            for _, nd in ipairs(nodes) do
                if not nd.fixed then nd.z = nd.z0 end
            end
        end
        rounds = rounds + 1
    until not dropped or rounds > #xs

    --  THE PROFILES, from the knots: with VERTICAL CURVES.  The
    --  envelope is a chain of straight runs, and where a flat run
    --  meets a climb the way has a corner.  A few passes of averaging
    --  each knot with its neighbours round every corner into a curve,
    --  the ends pinned to the arms.  A crest is lowered a little by
    --  it, which is why a lifted crossing is held as a plateau wider
    --  than the rounding reaches; and a sag under a crossing is raised
    --  a little by it, so the gap is measured again on the rounded
    --  ways and the plateau over it asked higher by what it lost.
    --  And the slope at every knot is the slope of the chord through
    --  its neighbours, so the cubic between two knots carries the grade
    --  through them rather than flattening at each and steepening
    --  between: a stair, a quarter tile a step.  The ends keep the
    --  slab's own grade.
    local function height_at(along, at)
        for q = 2, #along do
            if at <= along[q].s then
                local f = (at - along[q - 1].s) / math.max(1e-6, along[q].s - along[q - 1].s)
                return along[q - 1].z + (along[q].z - along[q - 1].z) * f
            end
        end
        return along[#along].z
    end
    local function shape(m)
        local along = {}
        for _, k in ipairs(m.ks) do along[#along + 1] = {s = k.s, z = nodes[k.id].z} end
        m.za, m.zb = nodes[m.ida].z, nodes[m.idb].z
        --  The rounding stops at the holds: a way held on its arm's
        --  slab is level there to the nose, and the corner at the nose
        --  is rounded from the free side alone.
        local ha, hb = m.hold_a or 0.0, m.hold_b or 0.0
        for _ = 1, opt.round do
            local prev = along[1].z
            for q = 2, #along - 1 do
                local cur = along[q].z
                if along[q].s > ha + 1e-6 and along[q].s < m.len - hb - 1e-6 then
                    along[q].z = 0.25 * prev + 0.5 * cur + 0.25 * along[q + 1].z
                end
                prev = cur
            end
        end
        along[1].z, along[#along].z = m.za, m.zb
        for q = 1, #along do
            if along[q].s <= ha + 1e-6 then along[q].z = m.za end
            if along[q].s >= m.len - hb - 1e-6 then along[q].z = m.zb end
        end
        for q = 2, #along - 1 do
            along[q].dz = (along[q + 1].z - along[q - 1].z) /
                          math.max(1e-6, along[q + 1].s - along[q - 1].s)
        end
        along[1].dz, along[#along].dz = m.ma, m.mb
        m.along = along
    end
    local least = over
    for _ = 1, 3 do
        for _, m in ipairs(ms) do shape(m) end
        local lost = false
        least = over
        for _, e in ipairs(xs) do
            if not e.x.dropped then
                local m, x = e.m, e.x
                local got  = height_at(m.along, x.s) - height_at(ms[x.other].along, x.so)
                local laid = nodes[x.knot.id].z - nodes[x.pair.knot.id].z
                if got < laid - 1e-4 then
                    lost = true
                    for _, ov in ipairs(x.overs) do ov.gap = ov.gap + (laid - got) end
                elseif got < least then
                    least = got
                end
            end
        end
        if not lost then break end
        settle()
    end
    for _, e in ipairs(xs) do
        if not e.x.dropped then
            local got = nodes[e.x.knot.id].z - nodes[e.x.pair.knot.id].z
            e.m.lifts[#e.m.lifts + 1] = string.format("over %d by %.2f", e.x.other, got)
        end
    end

    --  The steepest run laid, and the arms as they settled.
    local steepest, steep_m = 0.0, nil
    for i, m in ipairs(ms) do
        local steep, at = 0.0, 0
        for q = 2, #m.ks do
            local g2 = math.abs(nodes[m.ks[q].id].z - nodes[m.ks[q - 1].id].z) /
                       math.max(1e-6, m.ks[q].s - m.ks[q - 1].s)
            if g2 > steep then steep, at = g2, q end
        end
        m.steep = steep
        if steep > steepest then steepest, steep_m = steep, m end
        if opt.dump and steep > grade + 1e-3 then
            local ka, kb = m.ks[at - 1], m.ks[at]
            arc.dump(string.format("STEEP %d,%d way %d runs at %.4f between %.2f (z %.3f, floor %.3f) and %.2f (z %.3f, floor %.3f) of %.2f",
                ms[1].tc, ms[1].tr, i, steep, ka.s, nodes[ka.id].z, nodes[ka.id].floor,
                kb.s, nodes[kb.id].z, nodes[kb.id].floor, m.len))
        end
    end
    for key, id in pairs(arm_id) do arms[key].z = nodes[id].z end
    if opt.say and (unweavable > 0 or steepest > grade + 1e-3) then
        opt.say(string.format(
            "interchange %d,%d: %d crossings too near their arms to weave at the grade; the steepest way runs at %.0f%% where the grade is %.0f%%",
            ms[1].tc, ms[1].tr, unweavable, steepest * 100, grade * 100))
    end
    if opt.say and (short > 0 or least < over) then
        opt.say(string.format(
            "interchange %d,%d: %d crossings cannot be lifted the least gap at the grade; the smallest gap laid is %.2f of a level, %.2f asked",
            ms[1].tc, ms[1].tr, short, least, over))
    end
    return {unweavable = unweavable, short = short, least = least, steepest = steepest}
end

--  THE ARMS OF A NODE, SETTLED IN THE GRADING PASS.  Each band end
--  that meets a node records the lowest height it can stand at, its
--  ground envelope there with the slab's lift over it
--  (scripts/compose/profile.lua, arc.arm_floor), and the pose it
--  arrives on (scripts/compose/runs.lua, arc.arm_pose).  Between every
--  two arms the way a movement would take is laid from the two poses
--  (arc.chain_between), and the weave is planned over those ways with
--  the arms tied at one height and free to rise together: they stand
--  at the highest floor among them, and rise only where a way between
--  them cannot otherwise clear the ground it crosses, and by no more
--  than interchange_rise over the lowest of them, so that no arm is
--  left behind.  The building pass shapes every arm to the
--  height settled here (arc.arm_z), and plans the weave again over the
--  lanes' real ways with the arms as built.
arc.arm_z = arc.arm_z or {}
local function settle_arms(nodes, size, grade)
    local by_node = {}
    for key, k in pairs(arc.arm_node or {}) do
        local pose  = arc.arm_pose and arc.arm_pose[key]
        local floor = arc.arm_floor and arc.arm_floor[key]
        if pose and floor and nodes[k] then
            by_node[k] = by_node[k] or {}
            local list = by_node[k]
            list[#list + 1] = {key = key, x = pose.x, y = pose.y, dx = pose.dx, dy = pose.dy,
                               away = pose.away, floor = floor}
        end
    end
    for k, list in pairs(by_node) do
        table.sort(list, function (p, q) return p.key < q.key end)
        local arms, ms = {}, {}
        local c1 = nodes[k].cells[1]
        local tc, tr = c1 % size, c1 // size
        --  Every arm is one group: the arms stand at one height (the
        --  highest floor among them), which leaves every way the whole
        --  grade to climb over what it crosses.  Two highways parted a
        --  level apart cost the turns between them that same grade, and
        --  the node's ways are too short to pay it twice.
        local nx, ny = nodes[k].x, nodes[k].y
        --  TWO DECKS.  The arms are tied by AXIS: the arms of one
        --  highway stand at one height, and the other highway's stand
        --  the gap over them, so the through ways of the two cross as
        --  an overpass and every turn is a ramp between the two decks.
        --  ONE CEILING per group: the lowest arm's floor and the rise
        --  over it, or the highest floor where that is more.  An arm
        --  that can rise no further holds its group with it, so the
        --  arms of a deck never part.  The group that can stand
        --  highest goes over, since it is the one with the room.
        local groups = {}
        for _, a in ipairs(list) do
            a.axis = math.abs(a.x - nx) >= math.abs(a.y - ny) and "ew" or "ns"
            groups[a.axis] = groups[a.axis] or {keys = {}, top = -math.huge, cap = math.huge}
            local grp = groups[a.axis]
            grp.keys[a.key] = true
            if a.floor > grp.top then grp.top = a.floor end
            if a.floor + arc.geo.interchange_rise < grp.cap then grp.cap = a.floor + arc.geo.interchange_rise end
        end
        local order = {}
        for axis, grp in pairs(groups) do
            if grp.cap < grp.top then grp.cap = grp.top end
            order[#order + 1] = {axis = axis, grp = grp}
        end
        table.sort(order, function (p, q)
            if p.grp.cap ~= q.grp.cap then return p.grp.cap > q.grp.cap end
            return p.axis < q.axis
        end)
        for _, a in ipairs(list) do
            arms[a.key] = {z = a.floor, floor = a.floor, ceil = groups[a.axis].cap, fixed = false}
        end
        local tie = {groups = {}, over = #order > 1 and arc.geo.interchange_over or nil}
        for _, o in ipairs(order) do tie.groups[#tie.groups + 1] = o.grp.keys end
        --  Each way runs on the traffic's own side of the arms it
        --  joins, a lane's offset from the arm's line: the way from a
        --  to b and the way back are then a slab's width apart, as the
        --  lanes are, and are not read as crossing each other.
        local off = arc.geo.interchange_lane_off
        for _, a in ipairs(list) do
            for _, b in ipairs(list) do
                if a ~= b then
                    --  into the node by a, and out of it by b
                    local adx, ady = a.dx, a.dy
                    if a.away then adx, ady = -adx, -ady end
                    local bdx, bdy = b.dx, b.dy
                    if not b.away then bdx, bdy = -bdx, -bdy end
                    local q, rad, tlim = arc.chain_between(a.x + ady * off, a.y - adx * off, adx, ady,
                                                           b.x + bdy * off, b.y - bdx * off, bdx, bdy)
                    local pc = q and arc.fit(q, rad, tlim)
                    if pc and #pc > 0 then
                        local pts, len = sample(pc, arc.geo.interchange_sample)
                        --  no hold on a proxy: a held knot is the arm's
                        --  own node here, and a crossing at it would
                        --  raise the arm instead of the way
                        ms[#ms + 1] = {pts = pts, len = len, za = a.floor, zb = b.floor,
                                       ma = 0.0, mb = 0.0, tc = tc, tr = tr,
                                       arm_a = a.key, arm_b = b.key, at = {}}
                    end
                end
            end
        end
        if #ms > 0 then
            arc.weave.plan(ms, {
                grade = grade, over = arc.geo.interchange_over,
                over_min = arc.geo.interchange_over_min, lift = arc.geo.slab_lift,
                round = 0, crest = arc.geo.interchange_crest, stack = 0.0, arms = arms, tie = tie,
                ground = function (x, y) return arc.city.at("surface", math.floor(x), math.floor(y)) end,
                say = arc.log, dump = arc.geo.interchange_dump > 0.5})
        end
        for _, a in ipairs(list) do
            arc.arm_z[a.key] = arms[a.key].z
            if arc.geo.interchange_dump > 0.5 then
                arc.dump(string.format("ARM node %d,%d arm %d (%s) at %.2f,%.2f: floor %.3f, settled %.3f",
                    tc, tr, a.key, a.axis, a.x, a.y, a.floor, arms[a.key].z))
            end
        end
    end
end

--  The seat's height at a distance along a way, between its knots.
local function seat_at(along, at)
    for q = 2, #along do
        if at <= along[q].s then
            local f = (at - along[q - 1].s) / math.max(1e-6, along[q].s - along[q - 1].s)
            return along[q - 1].z + (along[q].z - along[q - 1].z) * f
        end
    end
    return along[#along].z
end

--  The part of a chain of pieces between two arc lengths along it, as
--  pieces of their own: a piece inside the window copied, one an end
--  falls in shortened to that end, one outside left out.
local function cut_pieces(pc, s0, s1)
    local out, s = {}, 0.0
    for _, p in ipairs(pc) do
        local a, b = s0 - s, s1 - s
        s = s + p.len
        if b > 1e-6 and a < p.len - 1e-6 then
            if a < 0.0 then a = 0.0 end
            if b > p.len then b = p.len end
            local q = {}
            for name, v in pairs(p) do q[name] = v end
            if a > 0.0 or b < p.len then
                local function at(t)
                    local f = p.len > 1e-6 and t / p.len or 0.0
                    if p.arc then
                        local th = p.t0 + (p.t1 - p.t0) * f
                        return p.cx + p.r * math.cos(th), p.cy + p.r * math.sin(th), th
                    end
                    return p.ax + (p.bx - p.ax) * f, p.ay + (p.by - p.ay) * f, nil
                end
                local x0, y0, th0 = at(a)
                local x1, y1, th1 = at(b)
                q.ax, q.ay, q.bx, q.by = x0, y0, x1, y1
                if p.arc then q.t0, q.t1 = th0, th1 end
                q.len = b - a
            end
            out[#out + 1] = q
        end
    end
    return out
end

arc.interchange_slabs = function (w, pass)
    local nodes, of_tile, n = arc.interchange_nodes()
    local grade = arc.geo.interchange_grade > 0 and arc.geo.interchange_grade
                  or arc.tune.band_grade
    if pass == 1 then
        settle_arms(nodes, n, grade)
        return
    end
    --  A movement's way, painted as ONE LANE of a slab: the slab's
    --  material marks a way by where across it each point lies, and a
    --  point told it is 0.70 to 0.94 of the way across is given a solid
    --  edge line each side and nothing else (the same reading a lane
    --  drop's own strip is painted with, scripts/compose/deck.lua).
    --  Over its first and last lead, though, the way IS the lane it
    --  leaves or joins, still running beside the slab's other lanes, so
    --  it is painted as that lane: the lane's own place across the
    --  slab, the outer edge line solid on its right and the lane line
    --  dashed on its left, and the dashes in the slab's own phase.  The
    --  ramp's paint begins where the way has parted from the slab.
    --  And the way has a BODY: a girder's depth of concrete under the
    --  lane, closed at both sides, so a way carried over another is
    --  seen to be carried, and one that climbs is seen to climb.
    local h, g = arc.geo.interchange_lane_w, arc.geo.interchange_girder
    local function section(right, left, bare)
        if bare then
            return {{across = -h, up = 0.0, mat = arc.mat.band, paint = right},
                    {across =  h, up = 0.0, mat = arc.mat.band, paint = left}}
        end
        return {{across = -h, up = 0.0, mat = arc.mat.band, paint = right},
                {across =  h, up = 0.0, mat = arc.mat.band, paint = left},
                {across =  h, up = -g,  mat = arc.mat.pier},
                {across = -h, up = -g,  mat = arc.mat.pier},
                {across = -h, up = 0.0, mat = arc.mat.pier}}
    end
    local way = section(0.70, 0.94)
    --  the lane's place across the slab, from the inside out: its right
    --  edge and its left
    local LANE = {[0] = {0.40, 0.12}, [1] = {0.70, 0.40}, [2] = {0.94, 0.70}}
    local lead = arc.geo.interchange_lead or 0.0
    --  The movements, gathered by the node each belongs to, with the
    --  height of the arm at each end.
    local by_node = {}
    for _, pc in ipairs(arc.interchange_turns or {}) do
        local a, b = pc[1], pc[#pc]
        local tc = a and math.floor(a.ax)
        local tr = a and math.floor(a.ay)
        --  the node the way belongs to, as the links rule filed it
        --  (scripts/compose/links.lua), or by its first tile
        local k  = pc.node or (tc and tc >= 0 and tr >= 0 and tc < n and tr < n
                   and of_tile[tr * n + tc] or nil)
        local za = k and arm_height(w, a.ax, a.ay)
        local zb = k and arm_height(w, b.bx, b.by)
        if za and zb then
            local pts, len = sample(pc, arc.geo.interchange_sample)
            --  The way's direction at each end, from its first and last
            --  steps, for the grade it must leave and arrive at.
            local p0, p1, q0, q1 = pts[1], pts[2] or pts[1], pts[#pts - 1] or pts[#pts], pts[#pts]
            local d0 = math.max(1e-6, arc.dist(p0.x, p0.y, p1.x, p1.y))
            local d1 = math.max(1e-6, arc.dist(q0.x, q0.y, q1.x, q1.y))
            local ma = arm_slope(w, a.ax, a.ay, (p1.x - p0.x) / d0, (p1.y - p0.y) / d0, true)
            local mb = arm_slope(w, b.bx, b.by, (q1.x - q0.x) / d1, (q1.y - q0.y) / d1, false)
            by_node[k] = by_node[k] or {}
            local ms = by_node[k]
            --  a lane that drops: the way is level over the taper and
            --  its ribbon begins at the taper's end, the nose
            local hold_a, hold_b = pc.hold_a or 0.0, pc.hold_b or 0.0
            ms[#ms + 1] = {pc = pc, pts = pts, len = len, za = za, zb = zb,
                           ma = ma, mb = mb, tc = tc, tr = tr, at = {},
                           hold_a = hold_a, hold_b = hold_b, nose_a = hold_a, nose_b = hold_b}
        end
    end

    for _, ms in pairs(by_node) do
        arc.weave.plan(ms, {
            grade = grade, over = arc.geo.interchange_over,
            over_min = arc.geo.interchange_over_min, lift = arc.geo.slab_lift,
            round = arc.geo.interchange_round, crest = arc.geo.interchange_crest,
            stack = arc.geo.interchange_stack, near = 2.0 * h + 0.01,
            plateau = arc.geo.interchange_plateau > 0.5,
            ground = function (x, y) return arc.city.at("surface", math.floor(x), math.floor(y)) end,
            say = arc.log, dump = arc.geo.interchange_dump > 0.5})

        for i, m in ipairs(ms) do
            local along = m.along
            if arc.geo.interchange_dump > 0.5 then
                local xs = {}
                for _, x in ipairs(m.at) do xs[#xs + 1] = string.format("%d@%.2f", x.other, x.s) end
                local p0, p1 = m.pts[1], m.pts[#m.pts]
                arc.dump(string.format(
                    "WEAVE %d,%d movement %d level %d len %.2f steep %.0f%% from %.3f,%.3f (z %.3f) to %.3f,%.3f (z %.3f) slopes %.2f/%.2f crosses %s | lifts %s",
                    m.tc, m.tr, i, m.level, m.len, m.steep * 100, p0.x, p0.y, m.za,
                    p1.x, p1.y, m.zb, m.ma, m.mb, table.concat(xs, " "), table.concat(m.lifts, ", ")))
            end
            if arc.geo.interchange_dump > 0.5 then
                --  THE SEAMS, measured: the way's height against the
                --  slab's beside it over its first and last tile, and
                --  how far off the slab's spine each end stands.
                local function z_at(s) return seat_at(along, s) end
                local function pt_at(s)
                    local best, bd = m.pts[1], 1e9
                    for _, pt in ipairs(m.pts) do
                        local dd = math.abs(pt.s - s)
                        if dd < bd then best, bd = pt, dd end
                    end
                    return best
                end
                for _, e in ipairs {{"start", 0.0, 1.0}, {"end", m.len, -1.0}} do
                    local cols = {}
                    for _, d in ipairs {0.0, 0.25, 0.5, 1.0} do
                        local s = e[2] + e[3] * d
                        local pt = pt_at(s)
                        local dz, away = w:slab_near(pt.x, pt.y)
                        cols[#cols + 1] = string.format("%.2f: way %.3f slab %s (%.2f off)", d, z_at(s),
                            dz and string.format("%.3f", dz) or "none", away or -1)
                    end
                    arc.dump(string.format("SEAM %d,%d way %d %s at %.2f,%.2f held %.2f | %s",
                        m.tc, m.tr, i, e[1], pt_at(e[2]).x, pt_at(e[2]).y,
                        e[1] == "start" and (m.hold_a or 0.0) or (m.hold_b or 0.0), table.concat(cols, " | ")))
                end
            end
            --  The way's own ribbon begins at the NOSE at each end, the
            --  taper's end behind its lane's end (scripts/compose/
            --  drop.lua): over the taper the slab's own quad carries the
            --  lane, and a ribbon laid there would lie on it.  The
            --  pieces are cut to the noses and the seat is moved with
            --  them, level at each cut.
            local pc2, along2 = m.pc, along
            local ha, hb = m.nose_a or 0.0, m.nose_b or 0.0
            if ha > 1e-4 or hb > 1e-4 then
                pc2 = cut_pieces(m.pc, ha, m.len - hb)
                along2 = {{s = 0.0, z = seat_at(along, ha), dz = 0.0}}
                for _, kn in ipairs(along) do
                    if kn.s > ha + 1e-4 and kn.s < m.len - hb - 1e-4 then
                        along2[#along2 + 1] = {s = kn.s - ha, z = kn.z, dz = kn.dz}
                    end
                end
                along2[#along2 + 1] = {s = m.len - hb - ha, z = seat_at(along, m.len - hb), dz = 0.0}
                local hair = arc.geo.interchange_stack or 0.0
                local sa, sb = (m.hold_a or 0.0) - ha, m.len - hb - ha - ((m.len - (m.hold_b or 0.0)) - ha)
                for _, kn in ipairs(along2) do
                    if kn.s <= sa + 1e-4 or kn.s >= (m.len - hb - ha) - sb - 1e-4 then kn.z = kn.z + hair end
                end
            end
            m.pc2, m.along2 = pc2, along2
        end

        --  Every ribbon's centreline, sampled finely with its height,
        --  for the ribbons beside it to be measured against.
        local rows = {}
        for i, m in ipairs(ms) do
            local pts = #m.pc2 > 0 and sample(m.pc2, 1.0 / 32.0) or {}
            for q, pt in ipairs(pts) do
                pt.z = seat_at(m.along2, pt.s)
                local p0, p1 = pts[math.max(1, q - 1)], pts[math.min(#pts, q + 1)]
                local dx, dy = p1.x - p0.x, p1.y - p0.y
                local l = math.sqrt(dx * dx + dy * dy)
                pt.dx, pt.dy = l > 1e-9 and dx / l or 1.0, l > 1e-9 and dy / l or 0.0
            end
            rows[i] = pts
        end
        --  How far the nearest other ribbon's centreline stands to the
        --  right and to the left of a point on this one: abreast of it,
        --  running the same way or the opposite, within half a level of
        --  its height and within reach, or nothing on that side.  A way
        --  crossing this one is not beside it, whatever its height.
        local function beside(i, x, y, dx, dy, z, reach)
            local dr, dl
            for j, pts in ipairs(rows) do
                if j ~= i then
                    for _, pt in ipairs(pts) do
                        local vx, vy = pt.x - x, pt.y - y
                        local fwd = vx * dx + vy * dy
                        if fwd > -0.03 and fwd < 0.03 and math.abs(pt.z - z) < 0.5
                           and math.abs(pt.dx * dx + pt.dy * dy) > 0.95 then
                            local lat = vy * dx - vx * dy
                            if lat < 0.0 then
                                if -lat <= reach and (not dr or -lat < dr) then dr = -lat end
                            elseif lat <= reach and (not dl or lat < dl) then dl = lat end
                        end
                    end
                end
            end
            return dr, dl
        end

        --  THE WIDTH a way leaves and joins the slab at.  At its start
        --  the way is as wide as the slab's lane it leaves: its outer
        --  edge stands at the inner edge of the next lane's ribbon, or
        --  at the slab's own edge for the outer lane, and the inner
        --  lane's inner edge stands at the slab's spine.  So the slab's
        --  end is tiled by its ways, with no slit of ground between two
        --  of them and none between the two inner lanes.  The edge then
        --  FOLLOWS the ribbon beside it, touching, for as long as that
        --  one runs within a lane's pitch: two ways abreast off one
        --  slab are one surface until they part.  Once the neighbour has
        --  parted, or where there is none, the edge narrows to a ramp's
        --  width over the nose (arc.geo.interchange_nose), and it is
        --  the ramp's own width from the end of the lead on.  The same
        --  at its end, for the lane it joins.
        local bw = arc.tune.band_w
        local nose_len = arc.geo.interchange_nose or 0.5
        local HAIR = 0.001
        local lanes = {[0] = arc.geo.slab_lane0, arc.geo.slab_lane1, arc.geo.slab_lane2}
        local function full(k)
            local outer = k < 2 and lanes[k + 1] * bw - h or bw
            return (outer - lanes[k] * bw) / h, k == 0 and lanes[0] * bw / h or 1.0
        end
        local function chain_at(pc, s)
            local run = 0.0
            for q, p in ipairs(pc) do
                if s <= run + p.len + 1e-6 or q == #pc then
                    local t = math.max(0.0, math.min(p.len, s - run))
                    local f = p.len > 1e-6 and t / p.len or 0.0
                    if p.arc then
                        local th = p.t0 + (p.t1 - p.t0) * f
                        local sg = p.t1 >= p.t0 and 1.0 or -1.0
                        return p.cx + p.r * math.cos(th), p.cy + p.r * math.sin(th),
                               -sg * math.sin(th), sg * math.cos(th)
                    end
                    local dx, dy = p.bx - p.ax, p.by - p.ay
                    local l = math.sqrt(dx * dx + dy * dy)
                    if l < 1e-9 then dx, dy, l = 1.0, 0.0, 1.0 end
                    return p.ax + dx * f, p.ay + dy * f, dx / l, dy / l
                end
                run = run + p.len
            end
        end

        for i, m in ipairs(ms) do
            local pc2, along2 = m.pc2, m.along2
            local ha, hb = m.nose_a or 0.0, m.nose_b or 0.0
            if #pc2 > 0 then
                w:shape("band interchange turn", m.tc, m.tr)
                local len2 = m.len - ha - hb
                local la, lb = LANE[m.pc.lane_a or 2], LANE[m.pc.lane_b or 2]
                --  the lane's paint runs to the end of the straight lead
                --  off the slab, measured from the way's start on the
                --  slab, and the ramp's from there
                local pa, pb = math.max(0.0, lead - ha), math.max(0.0, lead - hb)
                local fa_r, fa_l = full(m.pc.lane_a or 2)
                local fb_r, fb_l = full(m.pc.lane_b or 2)
                --  The width at s, worked out in order from each face
                --  inward, the last width on that side carried: the edge
                --  is at the neighbour's while one is beside it within
                --  the lane's width, and narrows no faster than the nose
                --  allows once it has gone.  The lane's full width is
                --  allowed over the lead and the nose past it, and no
                --  further.  `side` 0 is the start, 1 the end.
                local state = {[0] = {}, [1] = {}}
                local function width_at(s, side)
                    local st = state[side]
                    local fr, fl = fa_r, fa_l
                    local pl, off = math.min(pa, 0.5 * len2), s
                    if side == 1 then fr, fl, pl, off = fb_r, fb_l, math.min(pb, 0.5 * len2), len2 - s end
                    if pl < 1e-6 or off > pl + nose_len + 1e-6 then return 1.0, 1.0 end
                    local ds = st.off and math.abs(off - st.off) or 0.0
                    local x, y, dx, dy = chain_at(pc2, s)
                    local reach = math.max((fr + 1.0) * h, 2.0 * fl * h) + 0.01
                    local dr, dl = beside(i, x, y, dx, dy, seat_at(along2, s), reach)
                    local function one(f, have, prev)
                        local nominal = off <= pl and f
                                        or 1.0 + (f - 1.0) * math.max(0.0, 1.0 - (off - pl) / nose_len)
                        if have and have <= nominal + 0.05 then return math.max(1.0, math.min(nominal, have)) end
                        return math.min(nominal, math.max(1.0, (prev or f) - (f - 1.0) / nose_len * ds))
                    end
                    --  a hair short of the neighbour's edge: its own
                    --  stations do not fall on this one's, so an edge laid
                    --  exactly on a bending neighbour's would cross it by
                    --  the bend between two stations
                    local wr = one(fr, dr and (dr - h - HAIR) / h, st.wr)
                    local wl = one(fl, dl and (0.5 * dl - HAIR) / h, st.wl)
                    st.off, st.wr, st.wl = off, wr, wl
                    if arc.geo.interchange_dump > 0.5 then
                        --  the width each side, and the gap left to the
                        --  ribbon beside it there
                        arc.dump(string.format("WIDE %d,%d way %d lane %d/%d s %.3f at %.3f,%.3f: right %.3f gap %s, left %.3f gap %s",
                            m.tc, m.tr, i, m.pc.lane_a or -1, m.pc.lane_b or -1, s, x, y,
                            wr, dr and string.format("%.4f", dr - h - wr * h) or "-",
                            wl, dl and string.format("%.4f", 0.5 * dl - wl * h) or "-"))
                    end
                    return wr, wl
                end
                --  the way in three parts: the lane it leaves, the ramp,
                --  the lane it joins; one part where the leads meet.  A
                --  part whose width varies is stationed finely, so its
                --  edge follows the ribbon beside it.
                local function part(s0, s1, sec, at, fine)
                    if s1 - s0 < 1e-3 then return end
                    local pcp = cut_pieces(pc2, s0, s1)
                    if #pcp < 1 then return end
                    local alp = {{s = 0.0, z = seat_at(along2, s0), dz = 0.0}}
                    for _, kn in ipairs(along2) do
                        if kn.s > s0 + 1e-4 and kn.s < s1 - 1e-4 then
                            alp[#alp + 1] = {s = kn.s - s0, z = kn.z, dz = kn.dz}
                        end
                    end
                    alp[#alp + 1] = {s = s1 - s0, z = seat_at(along2, s1), dz = 0.0}
                    local wide, step = {}, 0.25
                    if fine then
                        step = 1.0 / 16.0
                        local n = math.max(1, math.ceil((s1 - s0) / step - 1e-6))
                        --  a part at the end is walked from the end face
                        local side = s1 > 0.5 * len2 + 1e-6 and 1 or 0
                        for q = 0, n do
                            local k = side == 0 and q or n - q
                            local s = s0 + (s1 - s0) * k / n
                            local wr, wl = width_at(s, side)
                            wide[k + 1] = {s = s - s0, wr = wr, wl = wl}
                        end
                    end
                    w:loft(pcp, sec, {step = step, step_arc = math.min(step, 0.1), along = alp,
                                      along_at = at, width = wide})
                end
                --  Over the straddle, to a hair past where the way is
                --  clear of the slab, the ribbon is the top alone, so
                --  its body does not cut the slab's sheet.  From each
                --  face inward: the straddle, the lane, then the ramp
                --  over the nose, each stationed finely for its width;
                --  the ramp between them is stationed as a ramp.
                local mid = 0.5 * len2
                local ca = math.min(len2, (m.hold_a or 0.0) - ha + 0.02)
                local cb = math.min(len2, (m.hold_b or 0.0) - hb + 0.02)
                local s0, s1 = math.min(ca, pa), len2 - math.min(cb, pb)
                local ramp_at = arc.slab.lane_along + lead
                if pa + pb < len2 - 1e-3 then
                    local na, nb = math.min(pa + nose_len, mid), math.max(len2 - pb - nose_len, mid)
                    if ca > 1e-3 then part(0.0, s0, section(la[1], la[2], true), ha, true) end
                    part(s0, pa, section(la[1], la[2]), ha + s0, true)
                    part(pa, na, way, ramp_at, true)
                    if cb > 1e-3 then part(s1, len2, section(lb[1], lb[2], true), 0.0, true) end
                    part(len2 - pb, s1, section(lb[1], lb[2]), 0.0, true)
                    part(nb, len2 - pb, way, ramp_at + (nb - pa), true)
                    part(na, nb, way, ramp_at + (na - pa))
                else
                    if ca > 1e-3 then part(0.0, s0, section(la[1], la[2], true), ha, true) end
                    part(s0, mid, section(la[1], la[2]), ha + s0, true)
                    if cb > 1e-3 then part(s1, len2, section(lb[1], lb[2], true), 0.0, true) end
                    part(mid, s1, section(lb[1], lb[2]), 0.0, true)
                end
            end
        end
    end
end
