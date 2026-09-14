--  relax.lua -- heights settled over a graph of poses, by relaxation.
--
--  Every height the network shares is a NODE of one graph: an arm of an
--  interchange where a slab arrives, a knot along a way through it, and
--  the knot of each of two ways where they cross.  What a height may
--  not go under is its FLOOR: the ground and the lift over it, or an
--  arm's own envelope.  Along one way two knots may not lie further
--  apart in height than the grade climbs between them; a way between
--  two arms keeps every knot on or over the straight line between
--  them; and where one way crosses over another its knot stands the
--  gap above the other's.  Each of those says only how LOW a height may
--  not be.  So the lowest heights that obey them all are found by
--  RAISING: every rule lifts what stands under it, and the sweep is
--  run again until nothing moves.
--
--  A height may also have a CEILING.  An arm is raised only so far
--  above its floor, and one already built is not raised at all.  A rule
--  that asks for more than a ceiling allows is left unmet, and the
--  caller reads that back as a way steeper than the grade or a gap
--  short of what was asked.
--
--  The graph handed in:
--    nodes[i] = {z, floor, ceil, fixed}
--    ways[w]  = {ids = {the node of each knot, in order},
--                s = {how far along each knot lies},
--                chord = the knots stay over the line between the ends,
--                graded = the grade holds between neighbours}
--    overs[o] = {up, low, gap, cap}: nodes[up] stands gap over
--               nodes[low], or at cap where that is lower

local EPS = 1e-6

--  Node `nd` raised to `z`, within its ceiling.  Answers whether it moved.
local function lift(nd, z)
    if nd.fixed then return false end
    if z > nd.ceil then z = nd.ceil end
    if z > nd.z + EPS then
        nd.z = z
        return true
    end
    return false
end

--  One sweep over every rule.  Answers whether anything moved.
local function sweep(g, grade)
    local nodes = g.nodes
    local moved = false
    for _, w in ipairs(g.ways) do
        local ids, s = w.ids, w.s
        local k = #ids
        if w.chord and k > 2 then
            local za, zb = nodes[ids[1]].z, nodes[ids[k]].z
            local len = s[k] - s[1]
            for q = 2, k - 1 do
                local t = len > EPS and (s[q] - s[1]) / len or 0.0
                if lift(nodes[ids[q]], za + (zb - za) * t) then moved = true end
            end
        end
        if w.graded then
            for q = 2, k do
                if lift(nodes[ids[q]], nodes[ids[q - 1]].z - grade * (s[q] - s[q - 1])) then moved = true end
            end
            for q = k - 1, 1, -1 do
                if lift(nodes[ids[q]], nodes[ids[q + 1]].z - grade * (s[q + 1] - s[q])) then moved = true end
            end
        end
    end
    for _, o in ipairs(g.overs) do
        local want = nodes[o.low].z + o.gap
        if o.cap and want > o.cap then want = o.cap end
        if lift(nodes[o.up], want) then moved = true end
    end
    return moved
end

--  The graph settled: swept until nothing moves, or `most` times.
--  Answers the sweeps it took and whether it settled.
function arc.relax(g, grade, most)
    local n = 0
    while n < most do
        n = n + 1
        if not sweep(g, grade) then return n, true end
    end
    return n, false
end
