--  orient.lua -- every on-spur tile on the map, and what each one becomes.
--
--  WHICH CELLS CARRY A SPUR is read here, off the map, the way bands.lua
--  reads the slabs: a tile whose building is one arc.band_tiles calls
--  an spur.  Nothing in the pipeline looks for one -- it is handed the
--  tiles this finds, in the order this finds them.
--
--  Then, for each: which side of it is the slab, and which the line.
--
--  A spur is one tile with four neighbours, and everything the spur
--  becomes follows from reading them: it climbs to a SLAB on one side
--  and comes down onto a ROAD on another.  The data says neither; it
--  only says what each neighbouring tile is.
--
--  The slab side is a slab tile whose band runs along that side's own
--  axis -- a slab to the north whose band runs east-west is the one this
--  spur climbs.  A slab tile that fails that test is a slab met END-ON:
--  the band begins at this spur rather than passing it.
--
--  The spur's line is the one ALONG the slab's axis when there is one.  A
--  line on the side opposite the slab runs parallel to the slab and is
--  the spur's only when nothing else is; taking the first line met
--  instead leaves the lane leaving from an edge with no line on it.
--
--  From those two sides EVERYTHING ELSE the spur is follows, and it is
--  worked out below rather than in the pipeline:
--
--    ALONG   the way the spur lies on the slab.  The map reaches the
--            screen through a reflection -- col down-left, row
--            down-right -- so the viewer's right hand is the map's left,
--            and a lane on the slab's south edge heads west as seen.
--    TOWARD  the way the slab is, one step.
--    OFF     which way round the spur runs: an OFF spur leaves the slab
--            downstream of its line, an ON spur joins it upstream.
--    LEN     how many tiles of slab the taper has to come down over,
--            walked along the band until it ends or reaches the
--            interchange.
--
--  WHICH SIDE THE TAPER FALLS ON.  A spur has no direction of its own in
--  the data, so the taper goes on the side away from its line: on the
--  line's side the strip would run over the line.  Where a line lies
--  opposite the slab, or none does, or one lies each way, both sides are
--  open -- and then the LONGER taper wins, because a taper with room to
--  run reads as a spur and a stubby one does not.
--
--  Answer no spur and the tile carries none, however its sides read.

local DC = {[0] = 0, 1, 0, -1}
local DR = {[0] = -1, 0, 1, 0}

--  How far the taper may reach: one gore, then the descent.
local TAPER = 4

--  Which of the four sides a unit direction points at, in the
--  pipeline's own edge order.
local function side_of(dx, dy)
    if dx == 0 then return dy < 0 and 0 or 2 end
    return dx > 0 and 1 or 3
end

--  A tile carrying an on-spur of its own.
local function spur(xbld, n, c, r)
    if c < 0 or r < 0 or c >= n or r >= n then return false end
    local t = arc.band_tiles[xbld[r * n + c]]
    return t ~= nil and t.kind == "spur"
end

--  A tile with a line on it, of any of the three kinds: a line proper, a
--  meet that is still a line to anything joining it, or a slab with
--  a line beneath it.
local function roadish(xbld, n, c, r)
    if c < 0 or r < 0 or c >= n or r >= n then return false end
    return arc.line_tiles[xbld[r * n + c]] ~= nil
end

--  The slab a taper runs back along, from the spur's own slab cell: the
--  tiles that are still band and not the interchange, up to TAPER.
local function room(xbld, n, c0, r0, dx, dy)
    for L = 0, TAPER - 1 do
        local c, r = c0 + dx * (L + 1), r0 + dy * (L + 1)
        if c < 0 or r < 0 or c >= n or r >= n then return L end
        local t = arc.band_tiles[xbld[r * n + c]]
        if not t or t.kind == "junction" then return L end
    end
    return TAPER
end

--  ONE SIDE of the spur's tile, read off the map: whether the neighbour
--  there is a SLAB tile, whether that slab's band runs along this side's
--  own axis, and whether the tile carries a ROAD.  A slab to the north
--  whose band runs east-west is the one this spur climbs; a slab that
--  fails the axis test is one met END-ON.
local function side(xbld, n, col, row, k)
    local c, r = col + DC[k], row + DR[k]
    if c < 0 or r < 0 or c >= n or r >= n then return false, false, false end
    local b = xbld[r * n + c]
    local t = arc.band_tiles[b]
    local slab = t ~= nil and t.kind == "slab"
    local ew = slab and t.axis == "ew"
    local carries = arc.line_tiles[b]
    return slab,
           slab and (ew and (k == 0 or k == 2) or (not ew and (k == 1 or k == 3))),
           carries == 1 or carries == 2
end

--  ONE TILE: everything the spur on it becomes, from the map around it.
local function orient(o, xbld, n, col, row, pass)
    local dside, rside, eside, lines

    for k = 0, 3 do
        local slab, axis, line = side(xbld, n, col, row, k)
        if slab and axis and not dside then
            dside = k
        elseif slab and not eside then
            --  A slab met end-on: the band begins here.
            eside = k
        elseif line then
            lines = (lines or 0) + 1
            if not rside then rside = k end
        end
    end

    if not dside then
        --  No band beside it at all, unless one ends here.
        o:answer(eside and 2 or 0, dside, rside, eside, lines)
        return
    end

    --  Prefer a line across the slab's axis to one opposite the slab.
    if rside then
        local back = (dside + 2) % 4
        for k = 0, 3 do
            if k ~= dside and k ~= back then
                local _, _, line = side(xbld, n, col, row, k)
                if line then
                    rside = k
                    break
                end
            end
        end
    end

    o:answer(1, dside, rside, eside, lines)

    --  And the spur itself, from those two sides and the map.
    local ax, ay = 0, 0
    if     dside == 0 then ax = -1
    elseif dside == 2 then ax =  1
    elseif dside == 1 then ay = -1
    else                   ay =  1 end
    local tx, ty = DC[dside], DR[dside]

    --  An OFF spur leaves the slab downstream of its line; only a line
    --  ACROSS the slab's axis says which way round it runs.
    local back = (dside + 2) % 4
    local off  = false
    if rside and rside ~= dside and rside ~= back then
        off = (DC[rside] * ax + DR[rside] * ay) > 0
    end

    --  How many lines lie along the slab's axis: one, and the taper has
    --  to fall away from it.
    local along_roads = 0
    for k = 0, 3 do
        if k ~= dside and k ~= back then
            local c, r = col + DC[k], row + DR[k]
            if c >= 0 and r >= 0 and c < n and r < n then
                local carries = arc.line_tiles[xbld[r * n + c]]
                if carries == 1 or carries == 2 then along_roads = along_roads + 1 end
            end
        end
    end

    local d0c, d0r = col + tx, row + ty
    local fwd  = room(xbld, n, d0c, d0r, off and -ax or ax, off and -ay or ay)
    local free = along_roads ~= 1
    local bwd  = free and room(xbld, n, d0c, d0r, off and ax or -ax, off and ay or -ay) or -1
    --  WHICH PASS.  The grading pass lays the corridors out and the
    --  building pass notches them in, and the two have never agreed
    --  about this: the grading pass takes the far side whatever the
    --  line says.  Reproduced here because the world is built on it --
    --  the grading is what the ground's shelves come from -- and making
    --  the two agree is a change to the look, not a tidy-up.
    local keep = pass == 2 and (not free or fwd >= bwd) or false

    --  `keep` says the taper stays on the side the line put it; the
    --  other side reverses the spur.  Written out rather than with
    --  `and`/`or`, which answers the wrong way round when the value it
    --  is choosing IS false.
    local side_off, side_len
    if keep then side_off, side_len = off, fwd
    else        side_off, side_len = not off, bwd end

    local opp = rside ~= nil and rside == back

    --  AND THE ROAD IT COMES DOWN TO.  `rd` is the way to the line tile:
    --  along the slab for a lane drop, and across the spur tile away from
    --  the slab where the line lies opposite it.  `pp` is the line's own
    --  axis, across that.
    local rdx, rdy
    if opp        then rdx, rdy = -tx, -ty
    elseif side_off then rdx, rdy = ax, ay
    else               rdx, rdy = -ax, -ay end
    local ppx, ppy = rdy, -rdx

    --  What lies BEYOND the line tile decides what the spur does there.
    --  A line carrying straight on through its far side takes the lane
    --  through without a turn, whatever else joins; otherwise a line
    --  running BOTH ways across is a through line the spur forks onto,
    --  one way only a stub it ends on, and neither no junction at all.
    --  The line tile's own links cannot say: a stub between two spurs is
    --  a four-way piece in the data.
    local kt = side_of(ppx, ppy)
    local ks = side_of(rdx, rdy)
    local fork, arm, mdx, mdy = 0, 0, ppx, ppy
    local tc, tr = col + DC[ks], row + DR[ks]
    if tc >= 0 and tr >= 0 and tc < n and tr < n then
        local into  = roadish(xbld, n, tc + DC[kt], tr + DR[kt])
        local away  = roadish(xbld, n, tc + DC[(kt + 2) % 4], tr + DR[(kt + 2) % 4])
        local ahead = roadish(xbld, n, tc + DC[ks], tr + DR[ks])
        if ahead then fork = 3
        elseif into and away then fork = 2
        elseif into or away then fork = 1 end
        if away then mdx, mdy = -ppx, -ppy end

        --  HOW THE SPUR MEETS THAT MEET.  What the piece beside the
        --  spur is depends on the ROADS around it, not on the spur:
        --  three or four line arms is a real junction and one is a stub
        --  the line ends in, and either takes the spur as an arm of its
        --  box that every other arm's outermost lane may use; two arms
        --  with the spur straight ahead of one is a BEND whose through
        --  movement IS the spur, so only the lane arriving opposite it
        --  may use it; two arms passing the spur at its side is an
        --  ordinary line, and the spur does not go through the box at
        --  all -- its own join peels off, or merges into, the near lane
        --  beside it.
        --
        --  A line arm is an edge the line tile's own piece carries out
        --  to, with a line on the far side of it that is not another
        --  spur.
        local lk, n_arm, road_arm = o:links(tc, tr), 0, {}
        for k = 0, 3 do
            local ac, ar = tc + DC[k], tr + DR[k]
            if lk & (1 << k) ~= 0 and roadish(xbld, n, ac, ar) and not spur(xbld, n, ac, ar) then
                road_arm[k] = true
                n_arm = n_arm + 1
            end
        end
        local e = (ks + 2) % 4          -- the line tile's edge back at the spur
        if n_arm ~= 2 then arm = 1
        elseif road_arm[(e + 2) % 4] then arm = 2
        else arm = 0 end
    end

    --  `off` is the spur's, after the taper's side was chosen;
    --  `tile_off` is the tile's own reading, which the join reads.
    o:spur{ax = ax, ay = ay, tx = tx, ty = ty,
           off = side_off, tile_off = off, len = side_len, opp = opp,
           fork = fork, arm = arm, rdx = rdx, rdy = rdy, mdx = mdx, mdy = mdy}
end

--  THE WALK.  Row by row, column by column, and each spur tile is named
--  and then answered for before the next is looked at.
arc.rules.spurs = function (o)
    local xbld, n = arc.city.plane("xbld")
    local tiles   = arc.band_tiles
    if not xbld or not tiles then return true end
    local pass = o:info().pass
    for row = 0, n - 1 do
        local base = row * n
        for col = 0, n - 1 do
            local t = tiles[xbld[base + col]]
            if t and t.kind == "spur" then
                if not o:at(col, row) then return true end
                orient(o, xbld, n, col, row, pass)
            end
        end
    end
    return true
end
