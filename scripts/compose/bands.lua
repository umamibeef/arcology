--  bands.lua -- which cells form a band, and where it runs.
--
--  The same question scripts/compose/network.lua asks of the lines,
--  asked of the slab.  Four readings of every cell are taken off the
--  map here and the walk below runs on them; take the file away and the
--  city has no bands at all.
--
--  A slab tile stands in a PAIR, two tiles across the band, and only the
--  primary of the pair carries a point: the spine runs along the seam
--  between them.  Where the art draws a corner it draws a 2x2 curve
--  BLOCK instead, whose centre lies on both seams at once, so the band
--  turns there and may leave by any side the block offers.
--
--  A band is walked from an END, away from it.  Which way that is --
--  when there is a neighbour behind, on, both or neither -- is
--  arc.rules.band_start's; a band with no end at all is walked both
--  ways from any cell of it, which is what the second sweep does.

local DEPTH = 512   -- a band that turns this many times is a fault, not a band

--  The four sides of a cell or a block, in the pipeline's own edge
--  order, and the step each takes.
local ODX = {[0] = 0, 1, 0, -1}
local ODY = {[0] = -1, 0, 1, 0}

--  THE FOUR READINGS, off the map.
--
--  PRIM is the cell of a pair that carries the point -- the lower of the
--  two across the band -- filed under both cells of the pair.  A slab
--  tile with no partner across it is malformed data and carries none, so
--  it is left to the sprites.  AXIS is which way that pair lies, true
--  for east-west.
--
--  BLOCK is the corner cell of the curve block a cell belongs to, filed
--  under all four of its cells, and SIDE -- on the corner cell alone --
--  is which of the block's four sides carry a run it joins.  The ids are
--  not trusted for a block's orientation: it is read off the runs that
--  touch it, the way the meet table was read off the shipped cities.
--  Any slab piece beside the block counts, the meets included --
--  they are band cells like the plain ones, and taking only the plain
--  ones leaves a block beside one with no side there, so the walk stops
--  inside it and the band breaks into pieces.
--
--  Only a band cell carries any of the four, which is what keeps the
--  reading to the few hundred cells that have one.
local function readings(xbld, n, tiles)
    local prim, axis, block, side = {}, {}, {}, {}

    --  A cell of the band itself -- a slab tile or a spur of one --
    --  and which way it lies.  An on-spur, a curve block and the
    --  interchange are band and are not the band.
    local function band(c, r)
        if c < 0 or r < 0 or c >= n or r >= n then return nil end
        local t = tiles[xbld[r * n + c]]
        if not t or (t.kind ~= "slab" and t.kind ~= "incline") then return nil end
        return t.axis == "ew"
    end

    for row = 0, n - 1 do
        for col = 0, n - 1 do
            local at = row * n + col
            local t  = tiles[xbld[at]]
            if t then
                local ew = band(col, row)
                if ew ~= nil then
                    --  across the band: north-south for an east-west slab
                    local oc = ew and col or col - 1
                    local orr = ew and row - 1 or row
                    if band(oc, orr) == ew then
                        prim[at], axis[at] = orr * n + oc, ew
                    else
                        oc, orr = ew and col or col + 1, ew and row + 1 or row
                        if band(oc, orr) == ew then prim[at], axis[at] = at, ew end
                    end
                elseif t.kind == "curve" then
                    local b, bc, br = xbld[at], col, row
                    for _ = 1, 2 do
                        if bc > 0 and xbld[br * n + bc - 1] == b then bc = bc - 1 end
                        if br > 0 and xbld[(br - 1) * n + bc] == b then br = br - 1 end
                    end
                    local bi = br * n + bc
                    block[at] = bi
                    if bi == at then
                        local sd = 0
                        for k = 0, 1 do
                            local x, y = bc + k, br + k
                            if band(x, br - 1) == false then sd = sd | 1 end
                            if band(x, br + 2) == false then sd = sd | 4 end
                            if band(bc + 2, y) == true  then sd = sd | 2 end
                            if band(bc - 1, y) == true  then sd = sd | 8 end
                        end
                        side[at] = sd
                    end
                end
            end
        end
    end
    return prim, axis, block, side
end

--  And the same four for anything that wants to check a band against
--  what it was read off: tools/network_check.py audits every cell of
--  every band with them.
arc.band_readings = readings

--  THE CORRIDOR a slab may sweep over, beside the cells of its own band.
--
--  Free air is free in height too.  A slab a level over its own ground
--  may sweep over ground no higher than that, and a hill beside the
--  band stands in its way as surely as another slab does.  So the
--  corridor is the free tiles within REACH of the band and no more than
--  CLIMB levels above the band's own ground next to them.
--
--  Three tiles is the most an arc cuts inside a corner at the widest
--  sweep, so it is the reach; nothing further can be reached and letting
--  the corridor run further only costs the sweep time.  A profile that
--  climbed over roofs spread each climb along the grade until every slab
--  rode two levels up, which is why the climb is nought and a structure
--  on level ground is in the corridor whatever its height.
--
--  SPURS at 1 puts the band's own on-spur tiles in as well, the ones
--  standing beside its cells: a slab's edge over a spur tile is exactly
--  where the spur's lane leaves it, and kept out, a straight line past a
--  spur is refused for the third of a tile its edge takes.  0 leaves
--  them out.
--
--  Push nothing and a slab may sweep over its own cells and no others,
--  which is a band that cannot be fitted at all.  It is PUSHED rather
--  than asked, like the byte tables: a build reaching up to ask what its
--  corridor is would be C driving the answer, the contract the other way
--  round.
arc.numbers("corridor", {reach = 3, climb = 0, spurs = 1})


arc.rules.bands = function (o)
    local d = o:info()
    local n = d.size
    local xbld              = arc.city.plane("xbld")
    local prim, axis, block, side = readings(xbld, n, arc.band_tiles)
    local start = arc.rules.band_start
    local seen, e = {}, {}
    --  Which node each band END meets, filed in the order the bands are
    --  handed over: the fit reads it (scripts/compose/runs.lua) and aims
    --  that end at the node.  An end that meets none is nil.
    local _, of_tile = arc.interchange_nodes()
    arc.band_ends = {}

    --  THE APPROACH to a node is the node's: the cells of a band within
    --  arc.geo.interchange_approach steps of an interchange tile are
    --  walked but not handed over, so the slab and its lanes end that
    --  far out and the ways through the node begin there, with that run
    --  to climb in.  Walked, so a band is found and run the same way it
    --  is without an approach; trimmed after, so nothing is laid on
    --  them.  Found by stepping out from every interchange tile through
    --  band cells, a curve block counting as one step for all four of
    --  its cells.
    local near = {}
    do
        local depth = math.floor(arc.geo.interchange_approach + 0.5)
        local tiles = arc.band_tiles
        local front = {}
        for at = 0, n * n - 1 do
            local t = tiles[xbld[at]]
            if t and t.kind == "junction" then front[#front + 1] = at end
        end
        for _ = 1, depth do
            local next_ = {}
            for _, at in ipairs(front) do
                local c, r = at % n, at // n
                for _, s in ipairs {{1, 0}, {-1, 0}, {0, 1}, {0, -1}} do
                    local cc, cr = c + s[1], r + s[2]
                    if cc >= 0 and cr >= 0 and cc < n and cr < n then
                        local p = cr * n + cc
                        local u = tiles[xbld[p]]
                        if u and u.kind ~= "junction" and not near[p] and (prim[p] or block[p]) then
                            near[p] = true
                            next_[#next_ + 1] = p
                            if block[p] then
                                local bi = block[p]
                                local bc, br = bi % n, bi // n
                                for _, q in ipairs {bi, bi + 1, bi + n, bi + n + 1} do
                                    if q // n <= br + 1 and not near[q] then
                                        near[q] = true
                                        next_[#next_ + 1] = q
                                    end
                                end
                            end
                        end
                    end
                end
            end
            front = next_
        end
    end
    local walked = {}

    --  A cell the band may stand on: the primary of its own pair, lying
    --  the way the walk is going, and not already taken.
    local function open_cell(cc, cr, ew)
        if cc < 0 or cr < 0 or cc >= n or cr >= n then return nil end
        local at = cr * n + cc
        if prim[at] ~= at or axis[at] ~= ew or seen[at] then return nil end
        return at
    end

    --  The block a chain carries on into: never back the way it came, a
    --  step across first, so a diagonal is followed block by block.
    local OFF = {{2, 0}, {-2, 0}, {0, 2}, {0, -2}, {2, 2}, {2, -2}, {-2, 2}, {-2, -2}}

    local function walk(col, row, ew, sign)
        local k = 0
        local cc, cr, cew = col, row, ew
        local dx, dy = ew and sign or 0, ew and 0 or sign
        for _ = 1, DEPTH do
            --  the cell the walk stands on
            local at = open_cell(cc, cr, cew)
            if not at or k + 2 >= d.max_cells then break end
            seen[at] = true
            e[k] = {cell = at, block = false, ew = cew}
            k = k + 1
            local nc, nr = cc + dx, cr + dy
            --  on along the band
            local nx = open_cell(nc, nr, cew)
            if nx then cc, cr = nc, nr goto next end
            --  or through a curve block, and every block chained to it
            do
                local tc = cew and (dx > 0 and cc + 1 or cc - 1) or cc
                local tr = cew and cr or (dy > 0 and cr + 1 or cr - 1)
                local sx, sy, chained = dx, dy, false
                while tc >= 0 and tr >= 0 and tc < n and tr < n and k + 2 < d.max_cells do
                    local bi = block[tr * n + tc]
                    if not bi or seen[bi] then break end
                    local bc, br = bi % n, bi // n
                    seen[bi] = true
                    seen[br * n + bc + 1] = true
                    seen[(br + 1) * n + bc] = true
                    seen[(br + 1) * n + bc + 1] = true
                    e[k] = {cell = bi, block = true, ew = false}
                    k = k + 1
                    chained = true
                    --  the next block of the chain, if there is one
                    local have = false
                    for _, off in ipairs(OFF) do
                        local qc, qr = bc + off[1], br + off[2]
                        if off[1] * sx + off[2] * sy >= 0 and
                           qc >= 0 and qr >= 0 and qc < n and qr < n then
                            local mi = block[qr * n + qc]
                            if mi and not seen[mi] then
                                tc, tr = mi % n, mi // n
                                sx, sy = off[1], off[2]
                                have = true
                                break
                            end
                        end
                    end
                    if have then goto chain end
                    --  else leave the block by a side it offers, forward
                    --  if it can, and otherwise not backward
                    do
                        local sd, out = side[bi], nil
                        for j = 0, 3 do
                            if sd & (1 << j) ~= 0 and ODX[j] * sx + ODY[j] * sy > 0 then out = j end
                        end
                        if not out then
                            for j = 0, 3 do
                                if sd & (1 << j) ~= 0 and not (ODX[j] * sx + ODY[j] * sy < 0) then out = j end
                            end
                        end
                        if not out then break end
                        cew = out == 1 or out == 3
                        dx, dy = ODX[out], ODY[out]
                        cc = bc + (out == 1 and 2 or out == 3 and -1 or 0)
                        cr = br + (out == 2 and 2 or out == 0 and -1 or 0)
                    end
                    break
                    ::chain::
                end
                if chained then goto next end
            end
            --  or one cell across, where the band steps sideways
            do
                local found = false
                for _, j in ipairs{-1, 1} do
                    local sc = nc + (cew and 0 or j)
                    local sr = nr + (cew and j or 0)
                    if not found and open_cell(sc, sr, cew) then
                        cc, cr, found = sc, sr, true
                    end
                end
                if found then goto next end
            end
            break
            ::next::
        end
        --  The approach trimmed from each end.  The cells stay taken, so
        --  no other walk starts inside one.  A curve block keeps the
        --  cell it exits through: trimmed away, the band turns in the
        --  block and ends at its middle facing along the block rather
        --  than out of it toward the node, and every way leaving it
        --  has to swing round first.
        local function keep(i)
            local c = e[i]
            return not near[c.cell] or c.block
                   or (i > 0 and e[i - 1].block) or (i < k - 1 and e[i + 1].block)
        end
        local lo, k0 = 0, k
        while lo < k and not keep(lo) do lo = lo + 1 end
        while k > lo and not keep(k - 1) do k = k - 1 end
        local head = lo > 0 and of_tile[e[0].cell] or nil
        local tail = k < k0 and of_tile[e[k0 - 1].cell] or nil
        if lo > 0 then
            for j = lo, k - 1 do e[j - lo] = e[j] end
            k = k - lo
        end
        for j = 0, k - 1 do walked[e[j].cell] = true end
        if k > 0 then
            arc.band_ends[#arc.band_ends + 1] = {head = head, tail = tail}
            if arc.geo.interchange_dump > 0.5 then
                arc.dump(string.format("ENDS band %d from %d,%d: trimmed %d/%d, head %s tail %s",
                    #arc.band_ends, e[0].cell % n, e[0].cell // n, lo, k0 - k - lo, tostring(head), tostring(tail)))
            end
            o:band(e, k, e[0].cell, e[0].ew, sign)
        end
    end

    --  Every cell that could start a band, walked away from its end.
    for row = 0, n - 1 do
        for col = 0, n - 1 do
            local at = row * n + col
            if prim[at] == at and not seen[at] then
                local ew = axis[at]
                local back, on = 0, 0
                local bc = ew and col - 1 or col
                local br = ew and row or row - 1
                if bc >= 0 and br >= 0 then
                    local bi = br * n + bc
                    if (prim[bi] == bi and axis[bi] == ew) or block[bi] then back = 1 end
                end
                bc = ew and col + 1 or col
                br = ew and row or row + 1
                if bc < n and br < n then
                    local bi = br * n + bc
                    if (prim[bi] == bi and axis[bi] == ew) or block[bi] then on = 1 end
                end
                --  The rule answers a direction by name, as it does for
                --  the drive: "forward", "backward", or nothing for a
                --  cell that starts no band.
                local way = start and start{back = back == 1, on = on == 1}
                if way == "forward" then walk(col, row, ew, 1)
                elseif way == "backward" then walk(col, row, ew, -1) end
            end
        end
    end

    --  And the bands with no end at all: any cell of one, walked both
    --  ways.  The start cell's own half is lofted twice, once per
    --  direction, which is rare enough to bear.
    for row = 0, n - 1 do
        for col = 0, n - 1 do
            local at = row * n + col
            if prim[at] == at and not seen[at] then
                local ew = axis[at]
                walk(col, row, ew, 1)
                seen[at] = nil
                walk(col, row, ew, -1)
            end
        end
    end

    --  THE CELLS THE WALKS HANDED OVER, for anything that must not draw
    --  over a slab a band lays itself.  The approach cells were trimmed
    --  and are not in it.  A node's own pad is the one that must:
    --  a curve block beside an interchange belongs to that node, and
    --  where a band turns through the block the band's slab already
    --  covers it.  Two surfaces over one cell at two heights pass
    --  through one another, and no painter's order separates them.
    arc.band_walked = walked
    --  And the approach cells, for the profile: a band end beside one of
    --  them is an end that meets a node.
    arc.band_approach = near
    return true
end
