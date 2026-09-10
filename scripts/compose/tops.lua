--  tops.lua -- where every tile's top comes from.
--
--  Six places, and this reads the map to say which of them each tile
--  draws from.  Every later pass -- the ground faces, the walls, a
--  strip's own samples of the surface under it -- reads the answer, so
--  it is settled first and once, before anything looks at a tile.
--
--    FIELD   the terrain's own heightfield, four corners shared with the
--            tiles around it.  Open land, and anything with nothing to
--            say for itself.
--    PAD     a flat pad at the tile's own level: a structure stands on
--            level ground however the ground under it falls.
--    ANCHOR  the pad of the tile the footprint belongs to.  A building
--            covers up to sixteen tiles and every one of them stands at
--            the anchor's level, or the footprint would be a staircase.
--    WATER   the water's table, which is what a body of water draws.
--    SHELF   the shelf a corridor graded for itself in the first pass.
--            The corridor's tile IS the corridor's surface, so it draws
--            its own four corners and comes out as one continuous run.
--    PLANE   the tile's own slope plane, from ALTM's level and the slope
--            code's lifts.  A network piece whose sprite fits a slope
--            keeps the slope rather than being levelled.
--
--  THE ORDER OF THE LADDER IS THE RULE.  A building answers first,
--  whatever else the tile is; then water; then the corridors, but only
--  in the building pass, because nothing has graded anything before it;
--  then a sloped piece; then any other structure; and open land last.
--
--  AND EVERY TILE ANSWERS TWICE.  A corridor's shelf is the BUILDING
--  PASS's reading of the tile -- it is what the ground is composed from
--  while the world is being made.  Once the pass is over and something
--  asks what stands at a tile, a corridor answers as the tile itself:
--  the pad or the plane its own byte says, not the shelf the pass drew.
--  So the ladder is run twice, once with the corridors in it and once
--  without, and the pipeline reads whichever the moment calls for.
--
--  WHY A CORRIDOR HAS TWO ANSWERS.  Its own shelf where one was written,
--  and the field where the corridor covers all four corners but wrote no
--  shelf of its own -- the corners were lowered to the corridor's height
--  in place, so the field already IS the corridor there.  The terrain
--  field is never notched: a corner belongs to the tile next door as
--  well, and cutting through it would drag the ground outside the
--  corridor down with it.  The wall rule closes the step, which is what
--  a notch looks like.
--
--  AND WHOSE PLACE IN THE PAINTER'S STACK.  A tile is swept in the order
--  the original sweeps it, so the mesh composes with the sprites.  A
--  footprint takes its anchor's place, so the art stays in front of its
--  own pad; an elevated piece takes the place of the neighbour that owns
--  its span, so a raised slab and the tile it crosses do not each claim
--  one.  Everything else takes its own.

local FIELD, ANCHOR, WATER, SHELF, PLANE, PAD = 0, 1, 2, 3, 4, 5

--  AND HOW A TILE VOTES for the four corners it shares with the tiles
--  around it, which is what the terrain field is averaged from.  A tile
--  that draws its own pad or plane does not vote with it: LAND votes its
--  own plane, BASE votes the tile's base alone -- so a corner every tile
--  around is a structure falls back to that average, the land keeps its
--  shape, and the wall rule closes the whole difference -- and WATER
--  votes the bed and holds the shore up to the table.
local LAND, BASE, SEA = 0, 1, 2

--  A footprint's ANCHOR: the tile of it that carries the rotation's
--  corner bit, up to three tiles north and east of this one.  The
--  nearest such tile wins, and a tile carrying the bit itself is its
--  own.
local function anchor_of(xbld, xzon, n, col, row, corner, buildings)
    local at = row * n + col
    local b  = xbld[at]
    if not buildings[b] or xzon[at] & corner ~= 0 then return at end
    local best, ai = 99, at
    for dr = 0, -3, -1 do
        for dc = 0, 3 do
            local ar, ac = row + dr, col + dc
            if ar >= 0 and ac < n and -dr + dc < best then
                local i = ar * n + ac
                if xbld[i] == b and xzon[i] & corner ~= 0 then
                    best, ai = -dr + dc, i
                end
            end
        end
    end
    return ai
end

--  The three tiles an elevated piece may take its place from: east,
--  north-east, north.  The first that is elevated and carries the corner
--  bit owns the span.
local QDR = {0, -1, -1}
local QDC = {1, 1, 0}

arc.rules.terrain = function (o)
    local d       = o:info()
    local n       = d.size
    local corner  = d.corner
    local xbld    = arc.city.plane("xbld")
    local xter    = arc.city.plane("xter")
    local xzon    = arc.city.plane("xzon")
    local graded  = o:graded()
    local building  = arc.building_tiles
    local water     = arc.water_tiles
    local built     = arc.built_tiles
    local levelling = arc.levelling_tiles
    local sloped    = arc.sloped_tiles
    local elevated  = arc.elevated_tiles
    local slope     = arc.slope_codes
    local top, anchor, order, vote, rest = {}, {}, {}, {}, {}

    for row = 0, n - 1 do
        local base = row * n
        for col = 0, n - 1 do
            local at = base + col
            local b, x = xbld[at], xter[at]
            local place
            vote[at] = water[x] and SEA or built[b] and BASE or LAND

            if building[b] then
                local a = anchor_of(xbld, xzon, n, col, row, corner, building)
                place, anchor[at], order[at] = ANCHOR, a, a
            elseif water[x] then
                place = WATER
            elseif built[b] then
                --  The tile as it stands, whatever the corridors did:
                --  a sloped piece keeps its slope, and anything else
                --  takes a level pad.  A band spur carries its own
                --  slab, lofted down to the ground by the band walk, so
                --  the tile under it is flat; given its own plane on a
                --  saddle it draws a pair of twisted wedges at the foot
                --  of the elevated band instead.
                if (slope[x] or 0) ~= 0 and sloped[b] and not elevated[b] then
                    place = PLANE
                else
                    place = PAD
                end
                rest[at] = place
                --  And what the building pass composes it from, where a
                --  corridor graded it.
                local g = levelling[b] and graded and graded[at] or 0
                if g == 1 then
                    place = SHELF
                elseif g == 2 then
                    place = FIELD
                end
            else
                place = FIELD
            end

            --  An elevated piece that does not carry the corner bit
            --  itself takes the place of the neighbour that does.
            if not order[at] and elevated[b] and xzon[at] & corner == 0 then
                for q = 1, 3 do
                    local ar, ac = row + QDR[q], col + QDC[q]
                    if ar >= 0 and ac < n then
                        local ai = ar * n + ac
                        if elevated[xbld[ai]] and xzon[ai] & corner ~= 0 then
                            order[at] = ai
                            break
                        end
                    end
                end
            end

            top[at] = place
        end
    end

    o:tops(top, anchor, order, vote, rest)
    return true
end
