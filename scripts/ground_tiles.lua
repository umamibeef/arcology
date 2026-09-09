--  ground_tiles.lua -- what a tile's own two bytes mean to the GROUND.
--
--  Nine answers, all of them by the byte alone and every one looked up
--  at every tile of the map: whether the terrain byte is water, what
--  slope it carries, whether anything stands on the tile, whether the
--  piece on it follows the slope, whether it is a building with a
--  footprint and an anchor, whether it is a raised piece ordered by its
--  neighbour, whether a corridor may level it, whether the saddle lift
--  applies, and what the map view tints a placed structure.
--
--  They are PUSHED, not asked for: the pipeline reads a table of 256 and
--  never calls back up to find out what a byte means.

--  WATER: terrain 0x10 and up.  Submerged and shore slopes, 0x10..0x2F,
--  are a body at ALTM's table over a bed at ALTM's level; streams,
--  canals and the waterfall, 0x30 on, have their table at their level.
do
    local t = {}
    for b = 0x10, 0xFF do t[b] = true end
    arc.bytes("water_tiles", t)
end

--  THE SLOPE a terrain byte carries, 0 flat.  The low nibble says which
--  way the tile falls, and only below 0x40: a stream with a slope nibble
--  among flat neighbours would otherwise draw as a bump.  Fourteen and
--  fifteen are no slope the art has a shape for.
do
    local t = {}
    for b = 0x00, 0x3F do
        local code = b % 16
        if code <= 13 then t[b] = code end
    end
    arc.bytes("slope_codes", t, "number")
end

--  SOMETHING STANDS HERE: anything but bare ground and trees.  A tile
--  that carries a structure is given a levelled pad rather than the
--  terrain's own surface.
do
    local t = {}
    for b = 0x0E, 0xFF do t[b] = true end
    arc.bytes("built_tiles", t)
end

--  A PIECE DRAWN BY A SLOPED SPRITE.  Across the shipped cities these
--  stand on a sloped terrain code every time and the others on flat
--  ground, so the ground under one keeps its slope instead of being
--  levelled: power lines, roads, rail, highway, and the elevated pieces.
do
    local t = {}
    for _, r in ipairs {{0x10, 0x13}, {0x1F, 0x22}, {0x2E, 0x31},
                        {0x3F, 0x42}, {0x61, 0x64}} do
        for b = r[1], r[2] do t[b] = true end
    end
    arc.bytes("sloped_tiles", t)
end

--  THE MAP VIEW'S TINT for a tile something was PLACED on, as against
--  one a zone grew: everything from the power plants up is the player's.
--  0 leaves the tile to its zone.
do
    local t = {}
    for b = 0xC6, 0xFF do t[b] = 10 end
    for b = 0xC6, 0xCF do t[b] = 11 end -- the ten power plants
    t[0xD5] = 12                        -- a park
    arc.bytes("structure_tints", t, "number")
end

--  A BUILDING PROPER: one that occupies a footprint and hangs its whole
--  order on the anchor tile, rather than a network piece drawn tile by
--  tile.  The buildings begin where the networks and the structures end.
do
    local t = {}
    for b = 0x70, 0xFF do t[b] = true end
    arc.bytes("building_tiles", t)
end

--  AN ELEVATED PIECE, which takes its order from the neighbour that owns
--  the span rather than from its own tile, so a raised deck and the tile
--  it crosses do not each claim the painter's place.
do
    local t = {}
    for b = 0x61, 0x6B do t[b] = true end
    arc.bytes("elevated_tiles", t)
end

--  A tile a CORRIDOR MAY LEVEL.  A deck levels nothing -- it stands
--  clear and its columns take up the difference -- but what runs under
--  one does, or a road climbing beneath a viaduct steps where the shelf
--  asked for a slope and the ground was drawn flat.
do
    local t = {}
    for b = 0x00, 0x48 do t[b] = true end
    for b = 0x4B, 0x4E do t[b] = true end
    arc.bytes("levelling_tiles", t)
end

--  A piece the SADDLE LIFT applies to: a network piece on terrain code
--  13 is drawn one step up, as the original draws it, and an elevated
--  piece never is.
do
    local t = {}
    for b = 0x0E, 0x60 do t[b] = true end
    arc.bytes("saddle_tiles", t)
end
