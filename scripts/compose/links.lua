--  links.lua -- where a band's lanes go when its slab runs out.
--
--  A slab lane's end is one of four situations, and telling them apart
--  is the whole of this file.  The pipeline offers the lanes as they
--  stand and lays what it is told to lay; which lane goes on to which is
--  decided here, and nowhere else.
--
--    the inner lane, with a line ahead
--        the slab has come down to grade: the lane goes into the line
--        lane whose open end faces it, nearest first.
--    the inner lane, with no line
--        the slab goes on as ANOTHER BAND -- the walk splits a slab at
--        an interchange or a corner and leaves the lanes either side
--        facing each other -- so the lane looks for that band's lane.
--        Failing that the band simply ends open.
--    a middle or outer lane, with a band ahead
--        the same continuation, at its own offset: this is what carries
--        a three-lane way round a loop without dropping a lane.
--    a middle or outer lane, with nothing ahead
--        it tapers into the inner lane of its own band, leaving a
--        taper's length back from the end, so the way narrows
--        instead of stopping in mid-air.
--
--  Every step is taken in the world's own precision.  The lane model
--  holds its poses as floats, and a comparison made to more places than
--  it keeps answers differently at a boundary -- a link that should be
--  laid would not be.  The numbers each test is measured against come
--  with the reading for the same reason.
--  A fault is said at ERROR level where the pipeline offers one, and
--  through the plain log where it does not: a script that calls a name
--  the running program has never heard of faults the rule and abandons
--  the build, so what is only sometimes there is reached through a local
--  and never by name.
local say = arc.error or arc.log

--  How far the far end of a continuation must lead away from the near
--  one, along the way the traffic travels.  It is a number of the
--  scripts' like every other (scripts/geo.lua).
local band_lead = arc.put.f32(arc.geo.band_lead or 0.0)

local f32 = arc.put.f32
local sqrt = arc.put.sqrt

local function sub(a, b) return f32(a - b) end
local function dot(ax, ay, bx, by) return f32(f32(ax * bx) + f32(ay * by)) end
local function dist(ax, ay, bx, by)
    local dx, dy = sub(bx, ax), sub(by, ay)
    return sqrt(f32(f32(dx * dx) + f32(dy * dy)))
end

--  THE CHAIN BETWEEN TWO POSES is the script's, never a router's: the
--  first pose, a straight lead along it, a straight lead back along the
--  second pose, the second pose.  The lead is the node's, cut down for
--  a short link so the two leads never meet.  Every corner may sweep
--  the node's turn radius and spend half the shorter leg beside it.
--  arc.fit cuts it into pieces, and a chain it can cut into none is a
--  link that cannot be laid, so nothing is answered for one.  The spur's
--  slide builds its placings with this too (scripts/compose/slide.lua).
function arc.chain_between(ax, ay, adx, ady, bx, by, bdx, bdy)
    local far  = dist(ax, ay, bx, by)
    local lead = math.min(arc.geo.interchange_lead or 0.0, 0.3 * far)
    local q = {{x = ax, y = ay}}
    if lead > 1e-3 then
        q[#q + 1] = {x = ax + adx * lead, y = ay + ady * lead}
        q[#q + 1] = {x = bx - bdx * lead, y = by - bdy * lead}
    end
    q[#q + 1] = {x = bx, y = by}
    local rad, tlim = {}, {}
    for i = 1, #q do
        rad[i] = arc.geo.interchange_radius
        local d1 = i > 1 and dist(q[i - 1].x, q[i - 1].y, q[i].x, q[i].y) or 1e9
        local d2 = i < #q and dist(q[i].x, q[i].y, q[i + 1].x, q[i + 1].y) or 1e9
        tlim[i] = 0.5 * math.min(d1, d2)
    end
    if #arc.fit(q, rad, tlim) < 1 then return nil end
    return q, rad, tlim
end

arc.rules.links = function (x)
    local d = x:info()
    local n = d.n
    local pieces = arc.rules.pieces

    local route = arc.chain_between

    --  One link laid: the chain the router answers, cut into pieces the
    --  same way every other path in the city is.
    local function join(ax, ay, adx, ady, bx, by, bdx, bdy, w, from, to, band)
        local q, rad, tlim = route(ax, ay, adx, ady, bx, by, bdx, bdy)
        if not q then
            x:note("link_fail")
            return
        end
        local pc = arc.fit(q, rad, tlim)
        if #pc < 1 then
            x:note("link_fail")
            return
        end
        x:link(pc, w, from, to, band)
        return pc
    end

    --  The lane of ANOTHER band whose open end faces this one: ahead of
    --  it, or level with it round a corner, within reach, running on or
    --  turning up to a right angle; the nearest wins.  The chain must
    --  also be one the fit can cut -- the window admits a band end lying
    --  BESIDE this one, two ways of an interchange abreast, and
    --  that is not a continuation at all.
    local function continuation(li, which, pdx, pdy, ddx, ddy)
        local best, ba = nil, d.band_reach
        local bx, by, bdx, bdy
        for lj = 0, n - 1 do
            if lj ~= li then
                local r = x:lane(lj)
                if r and r.slab and (which == 1 and r.open0 or which == 0 and r.open1) then
                    local p2x, p2y, d2x, d2y = x:pose(lj, which == 1 and 0 or 1)
                    if dot(d2x, d2y, ddx, ddy) >= d.band_dot then
                        local vx, vy
                        if which == 1 then vx, vy = sub(p2x, pdx), sub(p2y, pdy)
                        else vx, vy = sub(pdx, p2x), sub(pdy, p2y) end
                        local ahead = dot(vx, vy, ddx, ddy)
                        local aside = math.abs(f32(f32(vx * ddy) - f32(vy * ddx)))
                        local dd = sqrt(f32(f32(vx * vx) + f32(vy * vy)))
                        --  The far end must LEAD AWAY along the way the
                        --  traffic travels.  `ahead` says the two ends
                        --  lie the right way round; this says the far one
                        --  faces on rather than back.  Without it a band
                        --  that turns a corner matches its own lanes
                        --  across the turn: the way then leaves one lane,
                        --  swings through half a circle and arrives in
                        --  another lane of the same band, which is a
                        --  U-turn and reads as a kink of 180 degrees.
                        local lead = dot(d2x, d2y, vx, vy)
                        if ahead >= d.band_ahead and dd <= ba and aside <= d.band_aside
                           and dd >= d.band_apart and lead > band_lead then
                            --  the router must be able to build it
                            local q
                            if which == 1 then q = route(pdx, pdy, ddx, ddy, p2x, p2y, d2x, d2y)
                            else q = route(p2x, p2y, d2x, d2y, pdx, pdy, ddx, ddy) end
                            if q then
                                ba, best = dd, lj
                                bx, by, bdx, bdy = p2x, p2y, d2x, d2y
                            end
                        end
                    end
                end
            end
        end
        return best, bx, by, bdx, bdy
    end

    --  THE INTERCHANGE NODES.  A connected glob of interchange tiles is
    --  ONE node however large it grows, and its arms are the band
    --  edges that touch it (scripts/compose/interchange.lua).  Every arm
    --  must be able to reach every other -- that is what the node is for
    --  -- so the movements are laid FIRST, one lane apiece, and only
    --  then are the ends left over spent on the straightest pairs.
    --
    --  It runs BEFORE the ordinary matcher.  A lane end names one other
    --  end and one only, so whichever pass reaches it first keeps it --
    --  and a node's movements outrank a plain continuation, because
    --  carrying a way straight on is only ONE of the movements
    --  the node owes.  Matched the other way round the matcher spends
    --  every end at the node on continuations and the node is left with
    --  one arm and nothing to join.
    --  The path of every movement kept, so the slab it is driven on can
    --  be drawn along the very line the lane takes: a link is a lane in
    --  the model and nothing on the ground, so without this the node's
    --  movements are there and invisible.
    local turns = {}
    arc.interchange_turns = turns

    local nodes, of_tile, size = arc.interchange_nodes()
    local at_node = {}
    for i = 1, #nodes do at_node[i] = {out = {}, inn = {}, bands = {}} end

    for li = 0, n - 1 do
        local l = x:lane(li)
        if l and l.slab then
            for which = 0, 1 do
                if (which == 1 and l.open1) or (which == 0 and l.open0) then
                    local px, py, dx, dy = x:pose(li, which)
                    local tc, tr = math.floor(px), math.floor(py)
                    local k = (tc >= 0 and tr >= 0 and tc < size and tr < size)
                              and of_tile[tr * size + tc] or nil
                    local g = k and at_node[k]
                    if g then
                        --  Which ARM of the node this end belongs to is
                        --  the BAND it is a lane of.  The walk splits a
                        --  slab where it meets an interchange, so each
                        --  way into a node is a band of its own and its
                        --  six lanes are the six ends of that arm.
                        --
                        --  The band is exact where geometry can only
                        --  guess.  An arm of an L-shaped node lies off
                        --  the line from the node's middle, so walking
                        --  outward through an end charges it to the arm
                        --  beside it: the node then holds one arm with
                        --  six ends arriving and another with none, and
                        --  owes movements it has no lane left to lay.
                        local e = {li = li, x = px, y = py, dx = dx, dy = dy,
                                   w = l.w, band = l.band, arm = l.band}
                        if which == 1 then g.out[#g.out + 1] = e
                        else g.inn[#g.inn + 1] = e end
                        g.bands[l.band] = true
                    end
                end
            end
        end
    end

    for k, g in ipairs(at_node) do
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

        --  A movement through the node is only RECORDED here: which out
        --  end goes to which in end.  Where it runs is settled for the
        --  whole node at once, below, once every movement is known.
        local moves = {}
        local function lay(i, j)
            local a, b = g.out[i], g.inn[j]
            taken["o" .. i], taken["i" .. j] = true, true
            x:note("band_links")
            made = made + 1
            moves[#moves + 1] = {a = a, b = b}
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
                c1 % size, c1 // size, #bands, #eds, table.concat(near, " ")))
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
                --  apiece; fewer targets take the lanes from the sides in
                for k, t in ipairs(targets) do
                    local li = math.floor((k - 1) * #lanes / math.max(1, #targets)) + 1
                    out_for[fb][t.tb] = lanes[li] and lanes[li].i
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
                    local li = math.floor((k - 1) * #lanes / math.max(1, #sources)) + 1
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

        --  WHERE THE WAYS RUN.  A chain of the script's own for each
        --  movement: the out end, a straight lead off the slab, a middle
        --  waypoint bowed out from the node's middle by the movement's
        --  bulge, a straight lead into the far slab, the in end; the
        --  turn radius and the tangent each corner may spend are the
        --  script's numbers.  Nothing of the way's shape is asked of a
        --  router.  The bulges are then ANNEALED: one way's bulge moved
        --  at a time, the node re-planned flat with the weave planner
        --  (scripts/compose/interchange.lua), and the move kept when the
        --  node weaves better, or sometimes when it does not while the
        --  search is hot.  The search is seeded by the node's cells, so
        --  it answers the same every build.
        local nd     = nodes[k]
        local lead   = arc.geo.interchange_lead or 0.0
        local radius = arc.geo.interchange_radius
        --  A movement between two arms whose lines MEET ahead of the one
        --  and behind the other is a CORNER, and a corner is one arc: the
        --  way runs straight from its pose to a tangent point, sweeps the
        --  fillet of the two lines, and runs straight into the far pose.
        --  The arc is as wide as the shorter leg allows, and the search
        --  varies how much of the legs it takes -- the whole of them for
        --  the widest arc, less to pull the arc in toward the corner --
        --  where a bowed chain would fold on so short a chord.
        local function corner(a, b)
            local den = a.dx * b.dy - a.dy * b.dx
            if math.abs(den) < arc.geo.interchange_corner_sin then return nil end
            local rx, ry = b.x - a.x, b.y - a.y
            local t = (rx * b.dy - ry * b.dx) / den
            local u = (rx * a.dy - ry * a.dx) / den
            if t < arc.geo.interchange_corner_leg or -u < arc.geo.interchange_corner_leg then return nil end
            return a.x + a.dx * t, a.y + a.dy * t, t, -u
        end
        local function chain(mv, bulge, strict)
            local a, b = mv.a, mv.b
            local q, rad, tlim
            local px, py, da, db = corner(a, b)
            if px then
                --  the share of the legs the arc takes: the whole at the
                --  inmost bulge, less as the bulge grows
                local f = 1.0 - (bulge + arc.geo.interchange_bulge_in) /
                                (arc.geo.interchange_bulge + arc.geo.interchange_bulge_in) * 0.7
                q    = {{x = a.x, y = a.y}, {x = px, y = py}, {x = b.x, y = b.y}}
                rad  = {0.0, radius, 0.0}
                tlim = {0.0, f * math.min(da, db), 0.0}
            else
                local ax2, ay2 = a.x + a.dx * lead, a.y + a.dy * lead
                local bx2, by2 = b.x - b.dx * lead, b.y - b.dy * lead
                local mx, my = 0.5 * (ax2 + bx2), 0.5 * (ay2 + by2)
                local cx, cy = bx2 - ax2, by2 - ay2
                local cl = math.max(1e-6, math.sqrt(cx * cx + cy * cy))
                local nx, ny = -cy / cl, cx / cl
                if (mx - nd.x) * nx + (my - nd.y) * ny < 0.0 then nx, ny = -nx, -ny end
                q = {{x = a.x, y = a.y}, {x = ax2, y = ay2},
                     {x = mx + nx * bulge, y = my + ny * bulge},
                     {x = bx2, y = by2}, {x = b.x, y = b.y}}
                rad, tlim = {}, {}
                for i = 1, #q do
                    rad[i] = radius
                    local d1 = i > 1 and dist(q[i - 1].x, q[i - 1].y, q[i].x, q[i].y) or 1e9
                    local d2 = i < #q and dist(q[i].x, q[i].y, q[i + 1].x, q[i + 1].y) or 1e9
                    tlim[i] = 0.5 * math.min(d1, d2)
                end
            end
            local pc = arc.fit(q, rad, tlim)
            if #pc < 1 then return nil end
            --  no hairpin: a proposal with an arc under the least radius
            --  is no way the search may keep
            if strict then
                for _, p in ipairs(pc) do
                    if p.arc and p.r < arc.geo.interchange_rmin then return nil end
                end
            end
            return pc
        end
        local grade = arc.geo.interchange_grade > 0 and arc.geo.interchange_grade
                      or arc.tune.band_grade
        local over, over_min = arc.geo.interchange_over, arc.geo.interchange_over_min
        local step = arc.geo.interchange_anneal_step
        local xbld = arc.city.plane("xbld")
        local function cost_of()
            local ms = {}
            for _, mv in ipairs(moves) do
                if mv.pc then
                    local pts, len = arc.weave.sample(mv.pc, step)
                    ms[#ms + 1] = {pts = pts, len = len, za = 0.0, zb = 0.0,
                                   ma = 0.0, mb = 0.0, tc = 0, tr = 0}
                end
            end
            local st = arc.weave.plan(ms, {grade = grade, over = over, over_min = over_min,
                                           lift = 0.0, round = 0, stack = 0.0,
                                           crest = arc.geo.interchange_crest})
            --  What a layout costs.  A crossing that cannot be weaved is
            --  the worst thing; then one lifted short of the gap; then
            --  ground a way may not cross.  BENDING costs more than
            --  CLIMBING: the turning a way does beyond what its two end
            --  poses demand is charged, and its height is charged only a
            --  little, so the search separates ways by lifting them, which
            --  the grade already bounds, and keeps their lines gentle.
            local climb, bad, bend = 0.0, 0, 0.0
            for i, m in ipairs(ms) do
                local top = 0.0
                for _, kn in ipairs(m.along or {}) do
                    if kn.z > top then top = kn.z end
                end
                climb = climb + top
                for _, pt in ipairs(m.pts) do
                    local c, r = math.floor(pt.x), math.floor(pt.y)
                    if c >= 0 and r >= 0 and c < size and r < size and xbld[r * size + c] > 0x69 then
                        bad = bad + 1
                    end
                end
                --  the turning beyond the unavoidable: the sweep of every
                --  arc, less the angle between the way's two end poses
                local mv = moves[i]
                local turned = 0.0
                for _, p in ipairs(mv.pc) do
                    if p.arc then turned = turned + math.abs(p.t1 - p.t0) end
                end
                local need = math.acos(math.max(-1.0, math.min(1.0,
                    mv.a.dx * mv.b.dx + mv.a.dy * mv.b.dy)))
                bend = bend + math.max(0.0, turned - need)
                --  and every change of the turn's sign: one arc is the
                --  ideal, a bow is borne, two changes is the most allowed
                local last = 0
                for _, p in ipairs(mv.pc) do
                    if p.arc then
                        local sg = p.t1 > p.t0 and 1 or -1
                        if last ~= 0 and sg ~= last then bend = bend + arc.geo.interchange_sign_cost end
                        last = sg
                    end
                end
            end
            return 10.0 * st.unweavable + 4.0 * st.short + 2.0 * (over - st.least)
                   + 0.1 * climb + 2.0 * bad + 3.0 * bend
        end
        for _, mv in ipairs(moves) do
            mv.bulge = 0.0
            mv.pc    = chain(mv, 0.0)
            mv.best  = 0.0
        end
        local trials = math.floor(arc.geo.interchange_anneal + 0.5)
        local seed = ((nd.cells[1] or 1) * 7919 + 17) % 2147483648
        local function rnd()
            seed = (seed * 1103515245 + 12345) % 2147483648
            return seed / 2147483648
        end
        local cur = #moves > 0 and cost_of() or 0.0
        local first, best = cur, cur
        if #moves > 1 then
            local heat = arc.geo.interchange_anneal_heat
            for it = 1, trials do
                local t   = it / trials
                local T   = heat * (0.02 ^ t)
                local sz  = 0.8 * (0.2 ^ t)
                local mv  = moves[math.floor(rnd() * #moves) + 1]
                local ob, opc = mv.bulge, mv.pc
                local nb = ob + (2.0 * rnd() - 1.0) * sz
                nb = math.max(-arc.geo.interchange_bulge_in, math.min(arc.geo.interchange_bulge, nb))
                local pc = chain(mv, nb, true)
                if pc then
                    mv.bulge, mv.pc = nb, pc
                    local c = cost_of()
                    if c <= cur or rnd() < math.exp((cur - c) / T) then
                        cur = c
                        if c < best then
                            best = c
                            for _, m in ipairs(moves) do m.best = m.bulge end
                        end
                    else
                        mv.bulge, mv.pc = ob, opc
                    end
                end
            end
            for _, mv in ipairs(moves) do
                if mv.bulge ~= mv.best then
                    mv.bulge = mv.best
                    mv.pc    = chain(mv, mv.best) or mv.pc
                end
            end
        end
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
        local wiggly, tight = 0, 0
        for _, mv in ipairs(moves) do
            if mv.pc and wiggles(mv.pc) > 2 then wiggly = wiggly + 1 end
            if mv.pc and tightest(mv.pc) < arc.geo.interchange_rmin then tight = tight + 1 end
        end
        if wiggly > 0 or tight > 0 then
            arc.log(string.format("interchange %d,%d: of %d movements, %d change their turn more than twice and %d carry an arc under %.1f tiles",
                nd.cells[1] % size, nd.cells[1] // size, #moves, wiggly, tight, arc.geo.interchange_rmin))
        end
        if arc.geo.interchange_dump > 0.5 then
            local bs = {}
            for _, mv in ipairs(moves) do
                bs[#bs + 1] = string.format("%.2f/%d", mv.bulge, mv.pc and wiggles(mv.pc) or -1)
            end
            arc.dump(string.format("ANNEAL interchange %d,%d: %d trials, cost %.2f to %.2f, bulge/turns %s",
                nd.cells[1] % size, nd.cells[1] // size, trials, first, best, table.concat(bs, " ")))
        end
        for _, mv in ipairs(moves) do
            if mv.pc then
                x:link(mv.pc, mv.a.w, mv.a.li, mv.b.li, mv.a.band)
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
        local where = string.format("%d,%d", c0 % size, c0 // size)
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
    for li = 0, n - 1 do
        local l = x:lane(li)
        if l and l.slab then
            for which = 0, 1 do
                --  which 1: the lane's travel leaves the slab here; 0: it
                --  arrives.
                if (which == 1 and l.open1) or (which == 0 and l.open0) then
                    local pdx, pdy, ddx, ddy = x:pose(li, which)
                    --  Only the INNER lane -- the smallest offset of its
                    --  band and side -- looks for the line; the others go
                    --  to it.
                    local inner = true
                    for lj = 0, n - 1 do
                        if lj ~= li then
                            local r = x:lane(lj)
                            if r and r.slab and r.band == l.band
                               and (r.off > 0.0) == (l.off > 0.0)
                               and math.abs(r.off) < math.abs(l.off) then
                                local p2x, p2y = x:pose(lj, which)
                                if f32(math.abs(sub(p2x, pdx)) + math.abs(sub(p2y, pdy))) < d.abreast then
                                    inner = false
                                end
                            end
                        end
                    end

                    if not inner then
                        --  A middle or outer lane: on to the next band's
                        --  lane of its own offset where the slab goes on,
                        --  else into the inner lane's station up the band.
                        local nx, px, py, nx2, ny2 = continuation(li, which, pdx, pdy, ddx, ddy)
                        if nx then
                            x:note("band_links")
                            if which == 1 then join(pdx, pdy, ddx, ddy, px, py, nx2, ny2, l.w, li, nx, l.band)
                            else join(px, py, nx2, ny2, pdx, pdy, ddx, ddy, l.w, nx, li, l.band) end
                        else
                            local back = math.abs(l.off) > f32(d.outer * l.w) and d.taper_far or d.taper_near
                            if l.len >= f32(back + d.taper_room) then
                                local in_li, in_off = nil, 1e9
                                for lj = 0, n - 1 do
                                    local r = x:lane(lj)
                                    if r and r.slab and r.band == l.band
                                       and (r.off > 0.0) == (l.off > 0.0)
                                       and math.abs(r.off) < in_off then
                                        in_off, in_li = math.abs(r.off), lj
                                    end
                                end
                                if in_li then
                                    local psx, psy, dsx, dsy = x:station(in_li, which, back)
                                    local pax, pay, dax, day = x:station(li, which, f32(back + d.taper_gap))
                                    --  The taper claims THIS lane's end and
                                    --  nothing of the inner one.  It meets
                                    --  the inner lane at a STATION along it,
                                    --  not at either of its ends, and an end
                                    --  a join does not touch is passed as -1.
                                    --
                                    --  Claiming it costs the band both ways.
                                    --  A lane end names one other and one
                                    --  only, so the middle lane and the outer
                                    --  lane, tapering into the same inner
                                    --  lane, would ask for the same end and
                                    --  the second would be refused.  And the
                                    --  end asked for is at the FAR side of
                                    --  the band, where a node has usually
                                    --  spent it already, so the first is
                                    --  refused as well and both lanes are
                                    --  left stopping in mid-air.
                                    if which == 1 then
                                        join(pax, pay, dax, day, psx, psy, dsx, dsy, l.w, li, -1, l.band)
                                    else
                                        join(psx, psy, dsx, dsy, pax, pay, dax, day, l.w, -1, li, l.band)
                                    end
                                end
                            end
                        end
                    else
                        x:note("band_ends")
                        --  The inner lane: the line lane whose open end
                        --  faces this one, nearest first.
                        local best, bd = nil, d.reach
                        local brx, bry, bdx2, bdy2
                        for lj = 0, n - 1 do
                            local r = x:lane(lj)
                            if r and r.line
                               and (which == 1 and r.open0 or which == 0 and r.open1) then
                                local p2x, p2y, d2x, d2y = x:pose(lj, which == 1 and 0 or 1)
                                local dd = dist(pdx, pdy, p2x, p2y)
                                if dd < bd and dot(d2x, d2y, ddx, ddy) > d.road_dot then
                                    bd, best = dd, lj
                                    brx, bry, bdx2, bdy2 = p2x, p2y, d2x, d2y
                                end
                            end
                        end
                        if best then
                            if which == 1 then join(pdx, pdy, ddx, ddy, brx, bry, bdx2, bdy2, l.w, li, best, 0)
                            else join(brx, bry, bdx2, bdy2, pdx, pdy, ddx, ddy, l.w, best, li, 0) end
                        else
                            --  No line: the slab may go on as another band.
                            local nx, px, py, nx2, ny2 = continuation(li, which, pdx, pdy, ddx, ddy)
                            if nx then
                                x:note("band_links")
                                if which == 1 then join(pdx, pdy, ddx, ddy, px, py, nx2, ny2, l.w, li, nx, l.band)
                                else join(px, py, nx2, ny2, pdx, pdy, ddx, ddy, l.w, nx, li, l.band) end
                            else
                                x:note("band_open")
                            end
                        end
                    end
                end
            end
        end
    end

    return true
end
