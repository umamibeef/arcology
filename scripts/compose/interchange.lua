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
            --  end of the arm can land two tiles from the glob; filed
            --  under a ring narrower than that, the arm reads as having
            --  no end here at all and its movements cannot be made.
            for _, q in ipairs(cells) do
                of_tile[q] = k
                local qc, qr = q % n, q // n
                for dc = -2, 2 do
                    for dr = -2, 2 do
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

--  The slab a node is crossed on: its own tiles, covered.  A glob is any
--  shape, so it is laid as one strip per RUN of cells along a row -- the
--  runs tile the glob exactly and no two of them overlap, which a sweep
--  per pair of arms would not.
arc.interchange_slabs = function (w)
    local nodes, _, n = arc.interchange_nodes()
    local sec = {{across = -0.5, up = 0.0, mat = arc.mat.line},
                 {across =  0.5, up = 0.0, mat = arc.mat.line}}
    --  The movements themselves, each swept a lane wide along the pieces
    --  links.lua fitted for it, a hair over the pad so the two do not
    --  settle it between them.
    local h = arc.geo.interchange_lane_w
    local turn = {{across = -h, up = 0.0, mat = arc.mat.band_lane},
                  {across =  h, up = 0.0, mat = arc.mat.band_lane}}
    for _, pc in ipairs(arc.interchange_turns or {}) do
        local p0 = pc[1]
        if p0 then
            local gz = arc.city.at("surface", math.floor(p0.ax), math.floor(p0.ay))
            if gz then
                w:shape("band interchange turn",
                        math.floor(p0.ax), math.floor(p0.ay))
                w:loft(pc, turn, {step = 0.25, step_arc = 0.1,
                                  z = gz + arc.geo.slab_lift + arc.geo.loft_lift})
            end
        end
    end

    for _, nd in ipairs(nodes) do
        local rows = {}
        for _, q in ipairs(nd.cells) do
            local qc, qr = q % n, q // n
            rows[qr] = rows[qr] or {}
            rows[qr][qc] = true
        end
        for r, cols in pairs(rows) do
            local c = 0
            while c < n do
                if cols[c] then
                    local c0 = c
                    while cols[c + 1] do c = c + 1 end
                    local gz = arc.city.at("surface", c0, r)
                    if gz then
                        local pc = arc.fit({{x = c0, y = r + 0.5},
                                            {x = c + 1.0, y = r + 0.5}}, 0.0)
                        if #pc > 0 then
                            w:shape("band interchange", c0, r)
                            w:loft(pc, sec, {step = 0.25, step_arc = 0.1,
                                             z = gz + arc.geo.slab_lift})
                        end
                    end
                end
                c = c + 1
            end
        end
    end
end
