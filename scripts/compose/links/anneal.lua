--  anneal.lua -- WHERE THE WAYS RUN inside one node.
--
--  Which out end goes to which in end is settled before this
--  (node.lua).  This decides the LINE each of those movements takes.
--
--  A movement is a chain of the script's own: the out end, a straight
--  lead off the slab, a middle waypoint bowed out from the node's
--  middle, a straight lead into the far slab, the in end.  A movement
--  between two arms whose lines meet is a CORNER instead, and a corner
--  is one arc.
--
--  The bulges are then ANNEALED: one way's bulge moved at a time, the
--  node re-planned with the weave planner (compose/interchange), and
--  the move kept when the node weaves better, or sometimes when it does
--  not while the search is hot.  The search is seeded by the node's
--  cells, so it answers the same every build.

local geom = require "compose.links.chain"
local dist = geom.dist

local M = {}

M.settle = function (moves, nd, size)
    local lead   = arc.geo.interchange_lead
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
    --  Every way LEAVES ITS ARM ON A STRAIGHT, as the slab arrives
    --  on one: the curvature is nought on both sides of the pose,
    --  so the way is smooth through it and not merely tangent.  A
    --  corner's arc is kept `lead` short of each pose where the
    --  legs have the room, half the shorter leg where they do not,
    --  and never so short that the arc falls under the least
    --  radius: a legal arc comes before the straight.  A bowed
    --  chain's leads are straights of their own.
    local function chain(mv, bulge, strict)
        local a, b = mv.a, mv.b
        local q, rad, tlim
        local px, py, da, db = corner(a, b)
        if px then
            --  the share of the legs the arc takes: the whole at the
            --  inmost bulge, less as the bulge grows
            local f = 1.0 - (bulge + arc.geo.interchange_bulge_in) /
                            (arc.geo.interchange_bulge + arc.geo.interchange_bulge_in) * 0.7
            local leg  = math.min(da, db)
            local cosn = math.max(-1.0, math.min(1.0, a.dx * b.dx + a.dy * b.dy))
            local tan_half = math.sqrt(math.max(0.0, (1.0 - cosn) / math.max(1e-6, 1.0 + cosn)))
            local keep = math.min(lead, 0.5 * leg, leg - arc.geo.interchange_rmin * tan_half / f)
            if keep < 0.0 then keep = 0.0 end
            q    = {{x = a.x, y = a.y}, {x = px, y = py}, {x = b.x, y = b.y}}
            rad  = {0.0, radius, 0.0}
            tlim = {0.0, f * (leg - keep), 0.0}
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
    --  The deck an end stands on, as the grading pass settled it
    --  (scripts/compose/interchange/arms.lua, arc.arm_z): the plateau
    --  model plans from the two decks.
    local function deck_of(e, which)
        if not arc.arm_z or not e.off then return 0.0 end
        local at_tail = (which == 1) == (e.off > 0.0)
        return arc.arm_z[e.band * 2 + (at_tail and 1 or 0)] or 0.0
    end
    local plateau = arc.geo.interchange_plateau > 0.5
    local function cost_of()
        local ms = {}
        for _, mv in ipairs(moves) do
            if mv.pc then
                local pts, len = arc.weave.sample(mv.pc, step)
                ms[#ms + 1] = {pts = pts, len = len, za = deck_of(mv.a, 1), zb = deck_of(mv.b, 0),
                               ma = 0.0, mb = 0.0, tc = 0, tr = 0,
                               hold_a = mv.hold_a, hold_b = mv.hold_b}
            end
        end
        local st = arc.weave.plan(ms, {grade = grade, over = over, over_min = over_min,
                                       lift = 0.0, round = 0, stack = 0.0,
                                       crest = arc.geo.interchange_crest, plateau = plateau,
                                       ground = plateau and function (x, y)
                                           return arc.city.at("surface", math.floor(x), math.floor(y)) end or nil,
                                       near = 2.0 * arc.geo.interchange_lane_w + 0.01})
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
        --  and, as plateaus, a crossing on a climb and a way that
        --  cannot reach its level
        return 10.0 * st.unweavable + 4.0 * st.short + 2.0 * (over - st.least)
               + 2.0 * (st.knee or 0) + 4.0 * (st.low or 0)
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
    return first, best, trials
end
return M
