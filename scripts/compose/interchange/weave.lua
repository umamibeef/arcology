--  weave.lua -- WHICH MOVEMENT GOES OVER WHICH, and at what height.
--
--  One relaxation over one graph of heights.  Every movement of a node
--  is a chain of knots, every crossing ties two of them a level apart,
--  and every arm pins the movements that leave it to the height the arm
--  already stands at.  The planner settles the graph and answers the
--  height of each knot.
--
--  Where a crossing falls, and how long a flat the upper way needs for
--  it, is plateau.lua's.

local plateau = require "compose.interchange.plateau"

local sample       = plateau.sample
local crossings    = plateau.crossings
local plateau_plan = plateau.plateau_plan

arc.weave = arc.weave or {}
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
    --  What two ways file against each other: where their lines cross,
    --  and where their bodies come near.  Put files one crossing.
    local function pair(a, b, j, put)
        for _, x in ipairs(crossings(a, b)) do
            local shared = near_arm(a, x.sa, a.arm_a) and near_arm(b, x.sb, a.arm_a)
                        or near_arm(a, x.sa, a.arm_b) and near_arm(b, x.sb, a.arm_b)
            if not shared then put(x.sa, x.sb) end
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
                    if not dup then put(sa, sb) end
                end
                run = nil
            end
            --  A point further than the near from the other way's box
            --  is further from every step of it.  The margin is far
            --  wider than any rounding, so the test only ever passes
            --  over points the steps would all have passed over.
            local bx0, by0 = b.pts.x0 - opt.near - 1e-6, b.pts.y0 - opt.near - 1e-6
            local bx1, by1 = b.pts.x1 + opt.near + 1e-6, b.pts.y1 + opt.near + 1e-6
            for _, pt in ipairs(a.pts) do
                local best, bs = opt.near * opt.near, nil
                local far = pt.x < bx0 or pt.x > bx1 or pt.y < by0 or pt.y > by1
                for q = 1, far and 0 or #b.pts - 1 do
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
    --  A caller that plans one node many times over, moving one way at a
    --  time, keeps opt.pairs from one call to the next.  What two ways file
    --  against each other depends on their samples alone.  So a pair whose
    --  samples are the very tables of an earlier call files what it filed
    --  then, in the same order.  A way that names an arm is planned afresh.
    local seen_pairs = opt.pairs
    for i = 1, #ms do
        for j = i + 1, #ms do
            local a, b = ms[i], ms[j]
            local keep = seen_pairs and not (a.arm_a or a.arm_b or b.arm_a or b.arm_b)
            local row  = keep and seen_pairs[a.pts]
            local seen = row and row[b.pts]
            if seen and seen.near == opt.near then
                for q = 1, #seen, 2 do file(a, b, i, j, seen[q], seen[q + 1]) end
            else
                local filed = keep and {near = opt.near} or nil
                pair(a, b, j, function (sa, sb)
                    file(a, b, i, j, sa, sb)
                    if filed then
                        local n = #filed
                        filed[n + 1] = sa
                        filed[n + 2] = sb
                    end
                end)
                if filed then
                    if not row then
                        row = {}
                        seen_pairs[a.pts] = row
                    end
                    row[b.pts] = filed
                end
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
