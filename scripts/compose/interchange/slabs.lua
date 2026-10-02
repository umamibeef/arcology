--  slabs.lua -- the surface a node's movements are driven on.
--
--  A movement is a lane in the model and nothing on the ground.  This
--  lays the slab under it, along the very line the lane takes, so the
--  node's ways are seen as well as driven.

local plateau = require "compose.interchange.plateau"
local arms    = require "compose.interchange.arms"

local sample      = plateau.sample
local settle_arms = arms.settle

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
    local lead = arc.geo.interchange_lead
    --  The movements, gathered by the node each belongs to, with the
    --  height of the arm at each end.
    local by_node = {}
    for _, pc in ipairs(arc.interchange_turns or {}) do
        local a, b = pc[1], pc[#pc]
        local tc = a and math.floor(a.ax)
        local tr = a and math.floor(a.ay)
        --  the node the way belongs to, as the links rule filed it
        --  (scripts/compose/links), or by its first tile
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
                local hair = arc.geo.interchange_stack
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
        local nose_len = arc.geo.interchange_nose
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

