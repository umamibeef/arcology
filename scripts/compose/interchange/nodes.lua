--  nodes.lua -- every interchange node on the map, and its arms.
--
--  An interchange tile is a NODE that band segments meet at.  A 2x2 is
--  the smallest one.  Tiles touching each other make ONE node with a
--  larger area, not several: twelve tiles in an L are a single place
--  where four ways meet.  Cutting them into three 2x2 blocks invents
--  nodes the map never had and leaves the arms hanging.
--
--  So a node is a connected GLOB of them.  Its ARMS are the band edges
--  that touch its boundary.  Every arm must be able to reach every
--  other: that is what the node is for.

local STEP = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}}

--  Is this byte a band that a node may have as an arm?
local function is_way(t)
    return t ~= nil and t.kind ~= nil
end

--  A STUB: a short band that leaves a node and stops at nothing.  The
--  original map carries these where a way was laid and never finished.
--  They lead nowhere, so the node owes a movement to a mouth no
--  traffic can arrive at, and every way through the node is planned
--  around it.
--
--  A run that reaches ANOTHER NODE is an arm, whatever its length:
--  traffic crosses the node to get to it.  A short run that reaches no
--  other node is not.  It either stops at nothing, or it stops at a
--  ramp, and a ramp is a way on to the node rather than a way through
--  it.  Sixteen cells is the line, and a longer run is a highway that
--  simply ends: the node keeps that one, because cars drive it.
local STUB_CELLS = 16

local function stub(xbld, tiles, inside, n, q)
    local run, st, count = {[q] = true}, {q}, 1
    local leaves = false
    while #st > 0 do
        local w = table.remove(st)
        local wc, wr = w % n, w // n
        for _, s in ipairs(STEP) do
            local cc, cr = wc + s[1], wr + s[2]
            if cc >= 0 and cr >= 0 and cc < n and cr < n then
                local p = cr * n + cc
                local u = tiles[xbld[p]]
                --  THE RUN FOLLOWS THE BAND'S BODY, never a ramp.  A
                --  ramp stands beside the band, and it touches the arm
                --  on the other side of the node as well.  Stepped
                --  through, the walk leaves this arm, comes back along
                --  the next one, and every arm of the node reads as one
                --  way that goes everywhere.
                local body = u ~= nil and u.kind ~= "spur" and u.kind ~= "incline"
                if u and is_way(u) and body and not run[p] and not inside[p] then
                    if u.kind == "junction" then leaves = true end
                    run[p] = true
                    count  = count + 1
                    if count > STUB_CELLS then return false end
                    st[#st + 1] = p
                end
            end
        end
    end
    return not leaves
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
                        if not inside[p] and is_way(tiles[xbld[p]])
                           and not stub(xbld, tiles, inside, n, p) then
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
            --  (scripts/compose/links).
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
