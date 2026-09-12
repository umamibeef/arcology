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
                        --  touches a JUNCTION tile, never through another
                        --  block: one against the interchange is the loop
                        --  inside it, but a chain of them running away is
                        --  a band's corners, and swallowing the chain
                        --  merges two real edges of the node into one.
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
            --  A lane end stands on the node's own tiles or near them.
            --  A band runs its slab out half a tile past its last cell
            --  and its lanes sit up to a half width across that, so an
            --  end of the arm can land two tiles from the glob, and the
            --  approach the band leaves to the node puts it further out
            --  by that many cells; filed under a ring narrower than that,
            --  the arm reads as having no end here at all and its
            --  movements cannot be made.
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

--  How high a node's ways run between their crossings: the HIGHEST of
--  the heights its arms arrive at, and every level of the weave stands
--  interchange_over above the one below it from there.  A slab is held
--  up over what it crosses, so a way that comes down from an arm to
--  the mean of the arms passes through the railway or the road that
--  arm's slab is carried over.  From the highest arm nothing goes
--  down: a way climbs to the base, or leaves it level.
local function node_base(ms)
    local top
    for _, m in ipairs(ms) do
        top = math.max(top or m.za, m.za, m.zb)
    end
    return top
end

--  THE WEAVE PLANNER, shared: the node lays its ways with it, and the
--  matcher that chooses where the ways run scores each choice with it.
--  `ms` is the node's movements, each with its sampled points `pts`
--  (x, y, s), its length, the heights `za`, `zb` and grades `ma`, `mb`
--  at its two arms, and `tc`, `tr` to name it by.  `o` carries the
--  numbers: `grade`, `over`, `over_min`, `lift`, `round`, `crest`,
--  `stack`; `ground(x, y)` answers the ground under a point, or is
--  absent for a flat reckoning; `say` takes the report lines, `dump`
--  turns the crossing dump on.  Every movement leaves with `level`,
--  `at` (its crossings) and `along` (its height knots), and the planner
--  answers what it could not do: how many crossings were too near an
--  arm to weave, how many could not be lifted the least gap, and the
--  smallest gap it laid.
arc.weave = arc.weave or {}
arc.weave.sample = sample
function arc.weave.plan(ms, opt)
    local grade = opt.grade
    for _, m in ipairs(ms) do m.at = {} end
    --  The crossings, filed under both movements.
    for i = 1, #ms do
        for j = i + 1, #ms do
            for _, x in ipairs(crossings(ms[i], ms[j])) do
                ms[i].at[#ms[i].at + 1] = {s = x.sa, other = j, so = x.sb}
                ms[j].at[#ms[j].at + 1] = {s = x.sb, other = i, so = x.sa}
            end
        end
    end
    --  EACH WAY'S OWN PROFILE, before the weave: a ramp from the
    --  height of the arm it leaves to the height of the arm it
    --  reaches, held over the ground under it by the slab's lift, and
    --  no steeper than the grade in either direction.  The two ends
    --  are exact whatever the grade says: a way meets its arms.  Where
    --  the arms lie further apart in height than the grade can join
    --  over the way's length, the ramp between them is steeper than
    --  the grade, and the node says so.
    local lift = opt.lift
    local function baseline(m)
        local step = math.max(0.25, m.len / 60)
        local ks = {}
        local nk = math.max(2, math.ceil(m.len / step))
        for q = 0, nk do
            local at = math.min(m.len, q * m.len / nk)
            --  the sample nearest this distance along
            local best, bd = m.pts[1], 1e9
            for _, pt in ipairs(m.pts) do
                local dd = math.abs(pt.s - at)
                if dd < bd then best, bd = pt, dd end
            end
            local gz = opt.ground and opt.ground(best.x, best.y)
            local ramp = m.za + (m.zb - m.za) * (m.len > 1e-6 and at / m.len or 0.0)
            ks[#ks + 1] = {s = at, z = math.max(ramp, gz and gz + lift or ramp)}
        end
        ks[1].z, ks[#ks].z = m.za, m.zb
        --  Where the two arms lie further apart in height than the
        --  grade joins over the way's length, the envelope cannot
        --  hold both ends: the ramp is then the straight one between
        --  them, steeper than the grade by as little as it must be,
        --  and never a run at the grade with the rest in one step.
        local need = math.abs(m.zb - m.za) / math.max(1e-6, m.len)
        if need <= grade then
            for q = 2, #ks do
                ks[q].z = math.max(ks[q].z, ks[q - 1].z - grade * (ks[q].s - ks[q - 1].s))
            end
            for q = #ks - 1, 1, -1 do
                ks[q].z = math.max(ks[q].z, ks[q + 1].z - grade * (ks[q + 1].s - ks[q].s))
            end
        end
        ks[1].z, ks[#ks].z = m.za, m.zb
        return ks
    end
    local function height_at(ks, at)
        for q = 2, #ks do
            if at <= ks[q].s then
                local f = (at - ks[q - 1].s) / math.max(1e-6, ks[q].s - ks[q - 1].s)
                return ks[q - 1].z + (ks[q].z - ks[q - 1].z) * f
            end
        end
        return ks[#ks].z
    end
    for _, m in ipairs(ms) do
        m.ks = baseline(m)
        local steep = 0.0
        for q = 2, #m.ks do
            local g = math.abs(m.ks[q].z - m.ks[q - 1].z) / math.max(1e-6, m.ks[q].s - m.ks[q - 1].s)
            if g > steep then steep = g end
        end
        m.steep = steep
    end

    --  THE WEAVE over that.  At a crossing the two ways stand on a
    --  common floor, the higher of their two profiles there, and the
    --  one on the higher level stands interchange_over above it per
    --  level.  How far a way can climb from its arms by a crossing is
    --  the grade over the run from each end, less what its own
    --  profile already climbs there.
    --  A crossing neither way can be lifted at is UNWEAVABLE at the
    --  grade: it is left out, so it does not flatten every other,
    --  and it is counted and said.
    local function budget(m, at, floor_z)
        return math.min(grade * at - (floor_z - m.za),
                        grade * (m.len - at) - (floor_z - m.zb))
    end
    --  At a crossing only one of the two may have the room to be
    --  lifted: then it is the one that goes over, and that is a
    --  FORCED pair the levels must honour.  Where neither has the
    --  room the crossing is unweavable.
    local unweavable = 0
    for i = 1, #ms do
        local keep = {}
        ms[i].room, ms[i].over = 1e9, {}
        for _, x in ipairs(ms[i].at) do
            local o = ms[x.other]
            local floor_z = math.max(height_at(ms[i].ks, x.s), height_at(o.ks, x.so))
            x.floor  = floor_z
            x.budget = budget(ms[i], x.s, floor_z)
            local other_can = budget(o, x.so, floor_z) >= opt.over_min
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
                        x.budget, budget(o, x.so, floor_z)))
                end
            end
        end
        ms[i].at = keep
    end
    local steepest, steep_m = 0.0, nil
    for _, m in ipairs(ms) do
        if m.steep > steepest then steepest, steep_m = m.steep, m end
    end
    if opt.say and (unweavable > 0 or steepest > grade + 1e-3) then
        opt.say(string.format(
            "interchange %d,%d: %d crossings too near their arms to weave at the grade; the steepest way runs at %.0f%% where the grade is %.0f%%",
            ms[1].tc, ms[1].tr, unweavable, steepest * 100, grade * 100))
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
    local below = {}   -- below[i] = the ways i must lie UNDER
    for i = 1, #ms do
        below[i] = {}
        for j in pairs(ms[i].over) do below[i][#below[i] + 1] = j end
    end
    local done, left = {}, #ms
    while left > 0 do
        local pick
        for i = 1, #ms do
            if not done[i] then
                local ready = true
                for _, j in ipairs(below[i]) do
                    if done[j] then ready = false end
                end
                --  ready when every way it goes OVER is coloured
                ready = true
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

    --  THE PROFILES, level by level.  A way on level 0 runs on its
    --  own profile.  A way above that is lifted at each crossing it
    --  is the UPPER of, over the lower way's finished profile there,
    --  by interchange_over -- or by as much as the grade lets it
    --  reach from its two arms, and not at all where that is under
    --  the least gap a car clears.  The climb to each lifted knot is
    --  spread back at the grade both ways, and the arm's height and
    --  GRADE at each end are exact, so the way leaves the slab as
    --  the slab arrives and the join has no crease.  A hair per
    --  movement on the lifted knots alone: two ways at one height
    --  are one surface drawn twice where they touch, which the depth
    --  test settles differently from pixel to pixel; at the arms the
    --  hair would lift a way off the slab it leaves.
    local over, over_min = opt.over, opt.over_min
    local short, least = 0, over
    local top = 0
    for _, m in ipairs(ms) do top = math.max(top, m.level) end
    for lv = 0, top do
        for i, m in ipairs(ms) do
            if m.level == lv then
                local hair  = (i - 1) * opt.stack
                local along = {}
                for _, k in ipairs(m.ks) do along[#along + 1] = {s = k.s, z = k.z} end
                m.lifts = {}
                for _, x in ipairs(m.at) do
                    local o = ms[x.other]
                    if o.level < lv and o.along then
                        local floor_z = height_at(o.along, x.so)
                        local cap = math.min(m.za + grade * x.s, m.zb + grade * (m.len - x.s))
                        local got = math.min(floor_z + over, cap) - floor_z
                        if got >= over_min then
                            if got < least then least = got end
                            --  the nearest profile knot becomes the crossing's
                            local best, bd = nil, 1e9
                            for _, k in ipairs(along) do
                                local dd = math.abs(k.s - x.s)
                                if dd < bd then best, bd = k, dd end
                            end
                            --  held as a PLATEAU either side of the
                            --  crossing, so the vertical curves below
                            --  round its shoulders and not the clearance
                            local crest = opt.crest
                            for _, k in ipairs(along) do
                                if math.abs(k.s - x.s) <= crest and k.s > 1e-6 and k.s < m.len - 1e-6 then
                                    k.z = math.max(k.z, floor_z + got + hair)
                                end
                            end
                            m.lifts[#m.lifts + 1] = string.format("over %d by %.2f", x.other, got)
                        else
                            short = short + 1
                        end
                    end
                end
                table.sort(along, function (p, q) return p.s < q.s end)
                for q = 2, #along do
                    along[q].z = math.max(along[q].z, along[q - 1].z - grade * (along[q].s - along[q - 1].s))
                end
                for q = #along - 1, 1, -1 do
                    along[q].z = math.max(along[q].z, along[q + 1].z - grade * (along[q + 1].s - along[q].s))
                end
                --  VERTICAL CURVES.  The envelope is a chain of straight
                --  runs, and where a flat run meets a climb the way has a
                --  corner.  A few passes of averaging each knot with its
                --  neighbours round every corner into a curve, the ends
                --  pinned to the arms.  A crest is lowered a little by
                --  it, which is why a lifted crossing is held as a plateau
                --  wider than the rounding reaches.
                for _ = 1, opt.round do
                    local prev = along[1].z
                    for q = 2, #along - 1 do
                        local cur = along[q].z
                        along[q].z = 0.25 * prev + 0.5 * cur + 0.25 * along[q + 1].z
                        prev = cur
                    end
                end
                along[1].z, along[#along].z = m.za, m.zb
                --  And the slope at every knot is the slope of the chord
                --  through its neighbours, so the cubic between two knots
                --  carries the grade through them rather than flattening
                --  at each and steepening between: a stair, a quarter
                --  tile a step.  The ends keep the slab's own grade.
                for q = 2, #along - 1 do
                    along[q].dz = (along[q + 1].z - along[q - 1].z) /
                                  math.max(1e-6, along[q + 1].s - along[q - 1].s)
                end
                along[1].dz, along[#along].dz = m.ma, m.mb
                m.along = along
            end
        end
    end
    if opt.say and (short > 0 or least < over) then
        opt.say(string.format(
            "interchange %d,%d: %d crossings cannot be lifted the least gap at the grade; the smallest gap laid is %.2f of a level, %.2f asked",
            ms[1].tc, ms[1].tr, short, least, over))
    end

    return {unweavable = unweavable, short = short, least = least, steepest = steepest}
end

arc.interchange_slabs = function (w)
    local _, of_tile, n = arc.interchange_nodes()
    --  The deck height of every node, from the floors the arriving
    --  bands recorded this pass (scripts/compose/profile.lua): the next
    --  pass shapes each arm to it.  A deck already settled is kept where
    --  the floors say the same, so the second pass does not move it.
    for k, floor in pairs(arc.node_floor or {}) do
        arc.node_deck[k] = floor
    end
    arc.node_floor = {}
    --  A movement's way, painted as ONE LANE of a slab: the slab's
    --  material marks a way by where across it each point lies, and a
    --  point told it is 0.70 to 0.94 of the way across is given a solid
    --  edge line each side and nothing else (the same reading a lane
    --  drop's own strip is painted with, scripts/compose/deck.lua).
    local h = arc.geo.interchange_lane_w
    local way = {{across = -h, up = 0.0, mat = arc.mat.band, paint = 0.70},
                 {across =  h, up = 0.0, mat = arc.mat.band, paint = 0.94}}
    local grade = arc.geo.interchange_grade > 0 and arc.geo.interchange_grade
                  or arc.tune.band_grade
    --  The movements, gathered by the node each belongs to, with the
    --  height of the arm at each end.
    local by_node = {}
    for _, pc in ipairs(arc.interchange_turns or {}) do
        local a, b = pc[1], pc[#pc]
        local tc = a and math.floor(a.ax)
        local tr = a and math.floor(a.ay)
        local k  = tc and tc >= 0 and tr >= 0 and tc < n and tr < n
                   and of_tile[tr * n + tc] or nil
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
            ms[#ms + 1] = {pc = pc, pts = pts, len = len, za = za, zb = zb,
                           ma = ma, mb = mb, tc = tc, tr = tr, at = {}}
        end
    end

    for _, ms in pairs(by_node) do
        arc.weave.plan(ms, {
            grade = grade, over = arc.geo.interchange_over,
            over_min = arc.geo.interchange_over_min, lift = arc.geo.slab_lift,
            round = arc.geo.interchange_round, crest = arc.geo.interchange_crest,
            stack = arc.geo.interchange_stack,
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
            w:shape("band interchange turn", m.tc, m.tr)
            w:loft(m.pc, way, {step = 0.25, step_arc = 0.1, along = along,
                               along_at = arc.slab.lane_along})
        end
    end
end
