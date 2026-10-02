--  poses.lua -- every POSE the network meets at, from the map alone.
--
--  A pose is a point and a direction that two things share: the mouth
--  of a junction, where a segment ends and the box begins; the end of a
--  band's arm, where the slab stops and the node's ways begin; the gore
--  where a spur leaves its slab; the middle of a level meet.  Every
--  fit that reaches one is fitted TO it, so the two sides meet by
--  construction rather than by repair.  So the poses are settled here,
--  from the map and the walks alone, before any path is fitted.
--
--    arc.poses.mouth[fk][cell * 4 + edge]   a node cell's arm, per family
--    arc.poses.band_end[i].head / .tail     a band's end that meets a node
--    arc.poses.gore[cell]                   a spur tile's leaving point
--    arc.poses.meet[cell]                   a level meet's two axes
--    arc.poses.n                            the map's side, the cells' stride
--
--  Each carries x, y, dx, dy: the point, and the direction of travel
--  INTO the thing it names.  A band end carries only its direction and
--  the node it meets: its point is the fit's own.  A gore is handed
--  more once its slab is shaped (scripts/compose/profile.lua): the
--  slab's height and grade there, and the slab's stations (`parent`);
--  and where its merge lane lies along them (`merge`,
--  scripts/compose/drop.lua).

local DC = {[0] = 0, 1, 0, -1}
local DR = {[0] = -1, 0, 1, 0}

arc.poses        = arc.poses or {}
arc.net_planes   = arc.net_planes or {}
arc.spur_records = arc.spur_records or {}

--  What a spur's orientation found, kept for the gore
--  (scripts/compose/orient.lua calls this as it answers).
function arc.poses_gore(col, row, side, ax, ay, off)
    arc.spur_records[#arc.spur_records + 1] = {col = col, row = row, side = side,
                                               ax = ax, ay = ay, off = off}
end

function arc.poses_build()
    local xbld, n = arc.city.plane("xbld")
    local P = {mouth = {}, band_end = {}, gore = {}, meet = {}, n = n}
    arc.poses = P
    if not xbld then return P end

    --  THE MOUTHS: every node cell of every family, one pose per arm.
    --  The point is the port, a quarter tile short of the node's middle
    --  along the arm, which is where a segment's fit ends; the direction
    --  is the arm's axis, into the node.  Filed per
    --  family, and once more over all of them by cell and edge: a meet
    --  is a node of two families, and both arms on one edge share the
    --  edge's axis, so the first filed answers for either.
    P.mouth_at = {}
    for fk, pl in ipairs(arc.net_planes) do
        local mouths = {}
        P.mouth[fk] = mouths
        local node, links = pl.node, pl.links
        for i = 0, n * n - 1 do
            if node[i] ~= 0 then
                local lk = links[i]
                for e = 0, 3 do
                    if lk & (1 << e) ~= 0 then
                        local m = {x = i % n + 0.5 + 0.25 * DC[e], y = i // n + 0.5 + 0.25 * DR[e],
                                   dx = -DC[e], dy = -DR[e], kind = node[i]}
                        mouths[i * 4 + e] = m
                        if not P.mouth_at[i * 4 + e] then P.mouth_at[i * 4 + e] = m end
                    end
                end
            end
        end
    end

    --  THE ARMS: a band end that meets a node faces the middle of the
    --  node's mass (scripts/compose/interchange), so every arm
    --  arrives on a line through one point.  A head is the band's
    --  start and runs away from the node; a tail runs toward it.
    local nodes = arc.interchange_nodes()
    for i, be in ipairs(arc.band_ends or {}) do
        local rec = {}
        for _, which in ipairs {"head", "tail"} do
            local k = be[which]
            if k and nodes[k] then
                rec[which] = {node = k, away = which == "head",
                              nx = nodes[k].x, ny = nodes[k].y}
            end
        end
        P.band_end[i] = rec
    end

    --  THE GORES: where a spur leaves its slab, at the middle of the
    --  slab-side edge of the spur tile, running the slab's own way.
    for _, s in ipairs(arc.spur_records) do
        local cell = s.row * n + s.col
        P.gore[cell] = {x = s.col + 0.5 + 0.5 * DC[s.side], y = s.row + 0.5 + 0.5 * DR[s.side],
                        dx = s.ax, dy = s.ay, off = s.off, side = s.side}
    end

    --  THE MEETS: a line over a thread, pinned to one altitude there:
    --  the tile's middle, and the axis of each.  Piece 0 runs east and
    --  west, piece 1 north and south.
    local lap = arc.lap_tiles or {}
    for i = 0, n * n - 1 do
        local t = lap[xbld[i]]
        if t then
            P.meet[i] = {x = i % n + 0.5, y = i // n + 0.5,
                         line   = t.line == 0 and {dx = 1, dy = 0} or {dx = 0, dy = 1},
                         thread = t.line == 0 and {dx = 0, dy = 1} or {dx = 1, dy = 0}}
        end
    end
    return P
end

--  The gore beside a point: the spur tile nearest it among the cells
--  around it, once its slab has handed it its stations.
function arc.gore_of(x, y)
    local P = arc.poses
    if not P or not P.gore or not P.n or not x then return nil end
    local best, g = 4.0, nil
    local c, r = math.floor(x), math.floor(y)
    for dr = -1, 1 do
        for dc = -1, 1 do
            local cc, rr = c + dc, r + dr
            if cc >= 0 and rr >= 0 and cc < P.n and rr < P.n then
                local h = P.gore[rr * P.n + cc]
                if h and h.parent then
                    local ex, ey = h.x - x, h.y - y
                    local dd = ex * ex + ey * ey
                    if dd < best then best, g = dd, h end
                end
            end
        end
    end
    return g
end

--  Where a slab stands `s` along the stations handed to a gore, and
--  which way it runs there: between the two stations either side.
function arc.slab_at(pa, s)
    local n = pa.n
    if s <= pa.s[0] then return pa.x[0], pa.y[0], pa.dx[0], pa.dy[0] end
    if s >= pa.s[n - 1] then return pa.x[n - 1], pa.y[n - 1], pa.dx[n - 1], pa.dy[n - 1] end
    local lo, hi = 0, n - 1
    while hi - lo > 1 do
        local mid = (lo + hi) // 2
        if pa.s[mid] <= s then lo = mid else hi = mid end
    end
    local t = (s - pa.s[lo]) / math.max(1e-6, pa.s[hi] - pa.s[lo])
    local dx = pa.dx[lo] + (pa.dx[hi] - pa.dx[lo]) * t
    local dy = pa.dy[lo] + (pa.dy[hi] - pa.dy[lo]) * t
    local l = math.max(1e-6, math.sqrt(dx * dx + dy * dy))
    return pa.x[lo] + (pa.x[hi] - pa.x[lo]) * t, pa.y[lo] + (pa.y[hi] - pa.y[lo]) * t, dx / l, dy / l
end

--  The distance from a point to the nearest of a gore's slab stations.
function arc.slab_away(pa, x, y)
    local best = 1e9
    for i = 0, pa.n - 1 do
        local ex, ey = pa.x[i] - x, pa.y[i] - y
        local dd = ex * ex + ey * ey
        if dd < best then best = dd end
    end
    return math.sqrt(best)
end

--  A point beside the slab: `o` half widths out from the stations on
--  the side `sg` (1 the stations' right, -1 their left), `s` along.
function arc.slab_beside(pa, s, o, sg, hw)
    local x, y, dx, dy = arc.slab_at(pa, s)
    return x + sg * dy * o * hw, y - sg * dx * o * hw
end

--  The gore whose merge lane's point `which` ("top", where the ramp's
--  lane leaves the slab, or "q", where the lane joins the outer lane
--  at the taper's far end) lies within `near` of a point.  A ramp's
--  top lies a ramp's length from the tile the gore is filed under, so
--  a ramp is found by its points and not by its cell.
function arc.merge_of(x, y, which, near)
    local P = arc.poses
    if not P or not P.gore then return nil end
    local best, g = near * near, nil
    for _, h in pairs(P.gore) do
        local pt = h.merge and h.merge[which]
        if pt then
            local ex, ey = pt.x - x, pt.y - y
            local dd = ex * ex + ey * ey
            if dd < best then best, g = dd, h end
        end
    end
    return g, g and math.sqrt(best) or nil
end

--  THE MERGE LANE'S POINTS, from the far end of its taper to its gore
--  (scripts/compose/drop.lua): on the outer lane at the taper's far
--  end, still on it a little further so the way leaves the lane
--  straight and the taper's turn is a fillet, out on the merge lane at
--  its far start, and on the merge lane beside the gore.  Answers the
--  points, and the way's direction at the gore, heading on along the
--  slab past it.
function arc.merge_points(g)
    local mg, pa = g.merge, g.parent
    local hw    = arc.tune.band_w
    local outer = arc.geo.slab_lane2
    local aux   = 1.0 + 0.5 * arc.geo.ramp_lane_w
    local function at_s(s, o)
        local x, y = arc.slab_beside(pa, s, o, mg.sg, hw)
        return {x = x, y = y}
    end
    --  Two points that fall together -- a lane of no length, a taper
    --  of none, no climb -- are one: the fit reads a run of no length
    --  as no corner, and the chain then cuts the corner with a kink.
    local q = {}
    local function add(pt)
        local last = q[#q]
        if last and math.abs(last.x - pt.x) < 1e-4 and math.abs(last.y - pt.y) < 1e-4 then return end
        q[#q + 1] = pt
    end
    add(at_s(mg.s_q, outer))
    add(at_s(mg.s_q - mg.m * 0.3 * mg.taper, outer))
    add(at_s(mg.s_p, aux))
    add(at_s(mg.s_g, aux))
    --  and on beside the slab to the ramp tiles' top, where the lane
    --  climbs past them to hold the grade
    if mg.climb > 1e-3 then add(at_s(mg.s_top, aux)) end
    local _, _, gdx, gdy = arc.slab_at(pa, mg.s_top)
    return q, -mg.m * gdx, -mg.m * gdy
end

--  How high a slab stands `s` along the stations handed to a gore, and
--  its grade there: between the two stations either side.
function arc.slab_z_at(pa, s)
    local n = pa.n
    if n < 2 then return pa.z[0], 0.0 end
    if s <= pa.s[0] then s = pa.s[0] end
    if s >= pa.s[n - 1] then s = pa.s[n - 1] end
    local lo, hi = 0, n - 1
    while hi - lo > 1 do
        local mid = (lo + hi) // 2
        if pa.s[mid] <= s then lo = mid else hi = mid end
    end
    local ds = math.max(1e-6, pa.s[hi] - pa.s[lo])
    local t  = (s - pa.s[lo]) / ds
    return pa.z[lo] + (pa.z[hi] - pa.z[lo]) * t, (pa.z[hi] - pa.z[lo]) / ds
end
