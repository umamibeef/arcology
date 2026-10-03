--  node.lua -- WHAT ONE INTERCHANGE NODE OWES, and who gets a lane.
--
--  Every arm of a node must be able to reach every other: that is what
--  the node is for.  So the movements are laid FIRST, one lane apiece,
--  and only then are the ends left over spent on the straightest pairs.
--
--  It runs BEFORE the ordinary matcher (match.lua).  A lane end names
--  one other end and one only, so whichever pass reaches it first keeps
--  it.  A node's movements outrank a plain continuation, because
--  carrying a way straight on is only ONE of the movements the node
--  owes.  Matched the other way round the matcher spends every end at
--  the node on continuations, and the node is left with one arm and
--  nothing to join.
--
--  Where each movement RUNS is anneal.lua's.

local geom   = require "compose.links.chain"
local anneal = require "compose.links.anneal"

local f32  = geom.f32
local sub  = geom.sub
local dot  = geom.dot
local say  = arc.error or arc.log

local M = {}

M.plan = function (x, c, g, k)
    local n, nodes, size, turns = c.n, c.nodes, c.size, c.turns
    local route = arc.chain_between
    local taken, made, missed = {}, 0, {}

    --  The best unused pair carrying one named arm on to another.
    local function best(fb, tb)
        local bi, bj, bd = nil, nil, -2.0
        for i = 1, #g.out do
            if not taken["o" .. i] and g.out[i].arm == fb then
                for j = 1, #g.inn do
                    local a, b = g.out[i], g.inn[j]
                    if not taken["i" .. j] and b.arm == tb then
                        local dd = dot(a.dx, a.dy, b.dx, b.dy)
                        if dd > bd and route(a.x, a.y, a.dx, a.dy,
                                               b.x, b.y, b.dx, b.dy) then
                            bi, bj, bd = i, j, dd
                        end
                    end
                end
            end
        end
        return bi, bj
    end

    --  Which lane of its slab an end is, from the inside out: the
    --  slab lane whose offset its own lies nearest.
    local function lane_of(e)
        local best, k = 1e9, 0
        if not e.off then return 0 end
        for kk, o in ipairs {arc.geo.slab_lane0, arc.geo.slab_lane1, arc.geo.slab_lane2} do
            local dd = math.abs(math.abs(e.off) - o * e.w)
            if dd < best then best, k = dd, kk - 1 end
        end
        return k
    end
    --  How far a way runs on its slab at each end, level with it,
    --  before it is a ribbon of its own: the lane drop's taper
    --  behind the lane's end (scripts/compose/drop.lua, arc.fan_nose).
    --  Beyond the taper the slab has drawn in past the lane, and the
    --  way beside it is a ribbon of its own, free to climb.
    local function nose(e, which)
        local F = arc.fan_nose and arc.fan_nose[e.band]
        if not F or not e.off then return 0.0 end
        local at_tail = (which == 1) == (e.off > 0.0)
        local side = e.off > 0.0 and 0 or 1
        return F[((at_tail and 1 or 0) * 2 + side) * 4 + lane_of(e)] or 0.0
    end

    --  A movement through the node is only RECORDED here: which out
    --  end goes to which in end.  Where it runs is settled for the
    --  whole node at once, below, once every movement is known.
    local moves = {}
    local function lay(i, j)
        local a, b = g.out[i], g.inn[j]
        taken["o" .. i], taken["i" .. j] = true, true
        x:note("band_links")
        made = made + 1
        moves[#moves + 1] = {a = a, b = b, hold_a = nose(a, 1), hold_b = nose(b, 0)}
    end

    local bands = {}
    for band in pairs(g.bands) do bands[#bands + 1] = band end
    table.sort(bands)

    --  An arm the MAP has but no lane end reached is invisible to
    --  everything above: no band carries it, so no movement is owed
    --  to it and nothing reports it missing.  That is the one
    --  failure a count of unserved movements cannot see, so the arms
    --  are counted against the edges the node is made of.
    local eds = nodes[k].edges
    if #bands < #eds then
        --  Say what the nearest slab lane end to each edge is, and
        --  how far.  An edge with none near it is a band that does
        --  not end here, and an edge with one just out of reach is a
        --  catchment too tight.  The two want different fixes.
        local near = {}
        for ei = 1, #eds do
            local nd, no = 1e9, nil
            for lj = 0, n - 1 do
                local r = x:lane(lj)
                if r and r.slab then
                    for wh = 0, 1 do
                        local qx, qy = x:pose(lj, wh)
                        local ddx, ddy = sub(qx, eds[ei].x), sub(qy, eds[ei].y)
                        local dd = f32(f32(ddx * ddx) + f32(ddy * ddy))
                        if dd < nd then
                            nd, no = dd, string.format(
                                "lane %d %s at %.2f,%.2f open %s",
                                lj, wh == 1 and "end" or "start", qx, qy,
                                tostring(wh == 1 and r.open1 or r.open0))
                        end
                    end
                end
            end
            near[#near + 1] = string.format("%d,%d [nearest %s, %.2f away]",
                math.floor(eds[ei].x), math.floor(eds[ei].y),
                no or "none", math.sqrt(nd))
        end
        local c1 = nodes[k].cells[1]
        say(string.format(
            "interchange %d,%d: %d bands reach it but the node has %d edges, "
            .. "so an arm has nothing to join (%s)",
            c1 % size, math.floor(c1 / size), #bands, #eds, table.concat(near, " ")))
    end

    --  WHICH LANE EACH MOVEMENT TAKES, by the side it turns to.  The
    --  ways leaving one arm must not cross each other on the way
    --  out, nor the ways arriving at one arm on the way in: a left
    --  turn handed the rightmost lane cuts across its two neighbours
    --  at the arm, where no ramp has the run to climb over them.  So
    --  an arm's out lanes are ordered across the arm, its targets
    --  are ordered by the angle they lie at, and the two orders are
    --  paired: the most-left target takes the leftmost lane.  The
    --  in lanes are paired with their sources the same way.
    local function across(ends)
        local dx, dy, cx, cy = 0.0, 0.0, 0.0, 0.0
        for _, e in ipairs(ends) do
            dx, dy, cx, cy = dx + e.dx, dy + e.dy, cx + e.x, cy + e.y
        end
        local dl = math.max(1e-6, math.sqrt(dx * dx + dy * dy))
        dx, dy, cx, cy = dx / dl, dy / dl, cx / #ends, cy / #ends
        return dx, dy, cx, cy
    end
    local outs, inns = {}, {}
    for i, e in ipairs(g.out) do
        outs[e.arm] = outs[e.arm] or {}
        outs[e.arm][#outs[e.arm] + 1] = i
    end
    for j, e in ipairs(g.inn) do
        inns[e.arm] = inns[e.arm] or {}
        inns[e.arm][#inns[e.arm] + 1] = j
    end
    --  the lane an out end of `fb` takes for `tb`, and an in end of
    --  `tb` takes from `fb`
    local out_for, in_for = {}, {}
    for _, fb in ipairs(bands) do
        local ends = {}
        for _, i in ipairs(outs[fb] or {}) do ends[#ends + 1] = g.out[i] end
        if #ends > 0 then
            local dx, dy, cx, cy = across(ends)
            local lanes = {}
            for _, i in ipairs(outs[fb]) do
                local e = g.out[i]
                lanes[#lanes + 1] = {i = i, lat = (e.x - cx) * -dy + (e.y - cy) * dx}
            end
            table.sort(lanes, function (p, q) return p.lat > q.lat end)
            local targets = {}
            for _, tb in ipairs(bands) do
                if tb ~= fb and inns[tb] then
                    local tends = {}
                    for _, j in ipairs(inns[tb]) do tends[#tends + 1] = g.inn[j] end
                    local _, _, tx, ty = across(tends)
                    targets[#targets + 1] = {tb = tb, side = dx * (ty - cy) - dy * (tx - cx)}
                end
            end
            table.sort(targets, function (p, q) return p.side > q.side end)
            out_for[fb] = {}
            --  the targets spread over the lanes: three of each, one
            --  apiece; fewer targets take the lanes from the sides in,
            --  the leftmost target the leftmost lane and the rightmost
            --  target the rightmost, so a turn leaves from the side it
            --  turns to and the lanes between are the leftovers'
            for k, t in ipairs(targets) do
                local li = #targets > 1 and math.floor((k - 1) * (#lanes - 1) / (#targets - 1) + 0.5) + 1 or 1
                out_for[fb][t.tb] = lanes[li] and lanes[li].i
            end
            if arc.geo.interchange_dump > 0.5 then
                local ls, ts = {}, {}
                for _, l in ipairs(lanes) do ls[#ls + 1] = string.format("%d@%+.2f", l.i, l.lat) end
                for _, t in ipairs(targets) do ts[#ts + 1] = string.format("%d@%+.2f->lane %s", t.tb, t.side, tostring(out_for[fb][t.tb])) end
                arc.dump(string.format("LANES node %d,%d arm %d out at %.2f,%.2f heading %.2f,%.2f: lanes by left %s; targets by side %s",
                    nodes[k].cells[1] % size, math.floor(nodes[k].cells[1] / size), fb, cx, cy, dx, dy, table.concat(ls, " "), table.concat(ts, " ")))
            end
        end
    end
    for _, tb in ipairs(bands) do
        local ends = {}
        for _, j in ipairs(inns[tb] or {}) do ends[#ends + 1] = g.inn[j] end
        if #ends > 0 then
            local dx, dy, cx, cy = across(ends)
            local lanes = {}
            for _, j in ipairs(inns[tb]) do
                local e = g.inn[j]
                lanes[#lanes + 1] = {j = j, lat = (e.x - cx) * -dy + (e.y - cy) * dx}
            end
            table.sort(lanes, function (p, q) return p.lat > q.lat end)
            local sources = {}
            for _, fb in ipairs(bands) do
                if fb ~= tb and outs[fb] then
                    local fends = {}
                    for _, i in ipairs(outs[fb]) do fends[#fends + 1] = g.out[i] end
                    local _, _, fx, fy = across(fends)
                    sources[#sources + 1] = {fb = fb, side = dx * (fy - cy) - dy * (fx - cx)}
                end
            end
            table.sort(sources, function (p, q) return p.side > q.side end)
            in_for[tb] = {}
            for k, src in ipairs(sources) do
                local li = #sources > 1 and math.floor((k - 1) * (#lanes - 1) / (#sources - 1) + 0.5) + 1 or 1
                in_for[tb][src.fb] = lanes[li] and lanes[li].j
            end
        end
    end

    --  Every movement, one lane apiece: the lane the sides give it
    --  where that pair is free and can be built, the straightest free
    --  pair otherwise.
    for _, fb in ipairs(bands) do
        for _, tb in ipairs(bands) do
            if fb ~= tb then
                local i = out_for[fb] and out_for[fb][tb]
                local j = in_for[tb] and in_for[tb][fb]
                if i and j and not taken["o" .. i] and not taken["i" .. j] then
                    local a, b = g.out[i], g.inn[j]
                    if not route(a.x, a.y, a.dx, a.dy, b.x, b.y, b.dx, b.dy) then i = nil end
                else
                    i = nil
                end
                if not i then i, j = best(fb, tb) end
                if i then lay(i, j)
                else missed[#missed + 1] = fb .. "->" .. tb end
            end
        end
    end

    --  Then the ends left over, straightest first.  A pair is
    --  only taken where the two ends are on DIFFERENT arms: an out
    --  and an in of one arm face back the way they came, which is a
    --  U-turn and no movement at all.
    --
    --  Straightest first is a greedy choice, and a greedy choice can
    --  strand the last pair: with three arms left holding one out
    --  and one in apiece, taking the two straightest can leave the
    --  third arm's out facing its own in.  Both are then spent on
    --  nothing.  So an end that finds no partner looks for a pair
    --  already made that it can TAKE OVER, and hands that pair's
    --  out the end it was refused.  The search carries on through as
    --  many pairs as it must.  That is the difference between eight
    --  of a node's nine ways and all nine.
    local legal = {}
    for i = 1, #g.out do
        legal[i] = {}
        if not taken["o" .. i] then
            for j = 1, #g.inn do
                if not taken["i" .. j] then
                    local a, b = g.out[i], g.inn[j]
                    if a.arm ~= b.arm
                       and route(a.x, a.y, a.dx, a.dy, b.x, b.y, b.dx, b.dy) then
                        legal[i][j] = dot(a.dx, a.dy, b.dx, b.dy)
                    end
                end
            end
        end
    end

    --  Each out's ins, straightest first, so the search prefers the
    --  same pairs a plain sweep would have taken.
    local want = {}
    for i = 1, #g.out do
        local o = {}
        for j = 1, #g.inn do
            if legal[i][j] then o[#o + 1] = j end
        end
        table.sort(o, function (p, q)
            if legal[i][p] ~= legal[i][q] then return legal[i][p] > legal[i][q] end
            return p < q
        end)
        want[i] = o
    end

    --  The straightest pairs, taken while they are free.
    local match, held = {}, {}
    local rank = {}
    for i = 1, #g.out do
        for _, j in ipairs(want[i]) do
            rank[#rank + 1] = {i = i, j = j, dot = legal[i][j]}
        end
    end
    table.sort(rank, function (p, q)
        if p.dot ~= q.dot then return p.dot > q.dot end
        if p.i ~= q.i then return p.i < q.i end
        return p.j < q.j
    end)
    for _, pr in ipairs(rank) do
        if not held[pr.i] and not match[pr.j] then
            match[pr.j], held[pr.i] = pr.i, true
        end
    end

    --  Then the ends that got nothing take a pair over.
    local seen
    local function take(i)
        for _, j in ipairs(want[i]) do
            if not seen[j] then
                seen[j] = true
                if not match[j] or take(match[j]) then
                    match[j] = i
                    return true
                end
            end
        end
        return false
    end
    for i = 1, #g.out do
        if not taken["o" .. i] and not held[i] then
            seen = {}
            if take(i) then held[i] = true end
        end
    end

    for j = 1, #g.inn do
        if match[j] then lay(match[j], j) end
    end

    --  WHERE THE WAYS RUN is settled for the whole node at once, now
    --  that every movement is known (anneal.lua).
    local nd = nodes[k]
    local first, best, trials = anneal.settle(moves, nd, size)

    --  A WITNESS on every way laid: how many times its curvature
    --  changes sign along the pieces.  A movement is a slight turn,
    --  one big circle and a slight turn back, so two at most; a
    --  third is a wiggle the fit put in, and the node says so.
    local function wiggles(pc)
        local changes, last = 0, 0
        for _, p in ipairs(pc) do
            if p.arc then
                local sg = p.t1 > p.t0 and 1 or -1
                if last ~= 0 and sg ~= last then changes = changes + 1 end
                last = sg
            end
        end
        return changes
    end
    local function tightest(pc)
        local r = 1e9
        for _, p in ipairs(pc) do
            if p.arc and p.r < r then r = p.r end
        end
        return r
    end
    --  And on every way's ends: one that begins or ends on an arc
    --  meets its arm's straight with a curve, and the pose has a
    --  kink in curvature if not in direction.
    local wiggly, tight, curved = 0, 0, 0
    for _, mv in ipairs(moves) do
        if mv.pc and wiggles(mv.pc) > 2 then wiggly = wiggly + 1 end
        if mv.pc and tightest(mv.pc) < arc.geo.interchange_rmin then tight = tight + 1 end
        if mv.pc and (mv.pc[1].arc or mv.pc[#mv.pc].arc) then curved = curved + 1 end
    end
    if wiggly > 0 or tight > 0 or curved > 0 then
        arc.log(string.format("interchange %d,%d: of %d movements, %d change their turn more than twice, %d carry an arc under %.1f tiles and %d begin or end on an arc",
            nd.cells[1] % size, math.floor(nd.cells[1] / size), #moves, wiggly, tight, arc.geo.interchange_rmin, curved))
    end
    if arc.geo.interchange_dump > 0.5 then
        local bs = {}
        for _, mv in ipairs(moves) do
            bs[#bs + 1] = string.format("%.2f/%d", mv.bulge, mv.pc and wiggles(mv.pc) or -1)
        end
        arc.dump(string.format("ANNEAL interchange %d,%d: %d trials, cost %.2f to %.2f, bulge/turns %s",
            nd.cells[1] % size, math.floor(nd.cells[1] / size), trials, first, best, table.concat(bs, " ")))
    end
    for _, mv in ipairs(moves) do
        if mv.pc then
            x:link(mv.pc, mv.a.w, mv.a.li, mv.b.li, mv.a.band)
            --  the node, and how far each end is held on its slab,
            --  for the ways to be planned and laid by
            --  (scripts/compose/interchange)
            mv.pc.node, mv.pc.hold_a, mv.pc.hold_b = k, mv.hold_a, mv.hold_b
            mv.pc.lane_a, mv.pc.lane_b = lane_of(mv.a), lane_of(mv.b)
            turns[#turns + 1] = mv.pc
        else
            x:note("link_fail")
        end
    end

    --  What the node did, and what it could not.  A movement with no
    --  pair left is a driver who cannot leave by that arm, which is
    --  the one thing the node exists to prevent, so it is said
    --  whether anyone asked or not; the rest is a report line and
    --  waits to be asked for.
    local c0 = nodes[k].cells[1]
    local where = string.format("%d,%d", c0 % size, math.floor(c0 / size))
    if #missed > 0 then
        say(string.format(
            "interchange %s: %d tiles, %d arms -- %d movements have no lane left (%s)",
            where, #nodes[k].cells, #bands, #missed, table.concat(missed, " ")))
    end
    if arc.geo.interchange_dump > 0.5 and #g.out + #g.inn > 0 then
        local per = {}
        for _, band in ipairs(bands) do
            local o, i = 0, 0
            for _, e in ipairs(g.out) do if e.arm == band then o = o + 1 end end
            for _, e in ipairs(g.inn) do if e.arm == band then i = i + 1 end end
            per[#per + 1] = string.format("%d:%din/%dout", band, i, o)
        end
        arc.dump(string.format(
            "LINKS interchange %s: %d tiles, %d arms (%s), %d links laid, %d unserved",
            where, #nodes[k].cells, #bands, table.concat(per, " "), made, #missed))
    end
end
return M
