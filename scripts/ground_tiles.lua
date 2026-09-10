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
    arc.water_tiles = t
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
    arc.slope_codes = t
end

--  WHICH CORNERS A SLOPE CODE LIFTS, as a mask -- bit 0 NW, 1 SW, 2 SE,
--  3 NE.  A set bit lifts that corner one level, and a lift is never
--  more than one.  On screen NW is the diamond's top vertex, NE its
--  left, SW its right and SE its bottom.  The fourteen masks were read
--  off the sprites by tools/terrain_shapes.py.
do
    local m = {[0] = 0, 9, 3, 6, 12, 11, 7, 14, 13, 1, 2, 4, 8, 5}
    arc.bytes("corner_lifts", m, "number")

    --  AND THE DIAGONAL a tile's top is folded on, which follows from
    --  the same masks.  A tile with one odd corner keeps a flat triangle
    --  on the other three, so the fold avoids the odd corner; a saddle
    --  is folded NE-SW whatever its corners say; a plane is planar
    --  either way.
    local NW, SW, SE, NE = 0, 1, 2, 3
    local _ = SW + NE
    local f = {}
    for code = 0, 13 do
        local mask, raised, odd = m[code], 0, nil
        for k = 0, 3 do
            if mask & (1 << k) ~= 0 then raised = raised + 1 end
        end
        if raised == 1 or raised == 3 then
            local want = raised == 1 and 1 or 0
            for k = 0, 3 do
                if (mask >> k) & 1 == want then odd = k end
            end
        end
        f[code] = code == 13 or odd == NW or odd == SE
    end
    arc.bytes("fold_ne_sw", f)
end

--  THE GROUND A SADDLE LIFT APPLIES TO: terrain code 13 on bare land.
--  The original draws a network piece there one step up ($17528:
--  `cmpi.w #$d`, then -12), and only there.
arc.bytes("saddle_terrain", {[0x0D] = true})

--  SOMETHING STANDS HERE: anything but bare ground and trees.  A tile
--  that carries a structure is given a levelled pad rather than the
--  terrain's own surface.
do
    local t = {}
    for b = 0x0E, 0xFF do t[b] = true end
    arc.bytes("built_tiles", t)
    arc.built_tiles = t
end

--  A PIECE DRAWN BY A SLOPED SPRITE.  Across the shipped cities these
--  stand on a sloped terrain code every time and the others on flat
--  ground, so the ground under one keeps its slope instead of being
--  levelled: power lines, lines, thread, band, and the elevated pieces.
do
    local t = {}
    for _, r in ipairs {{0x10, 0x13}, {0x1F, 0x22}, {0x2E, 0x31},
                        {0x3F, 0x42}, {0x61, 0x64}} do
        for b = r[1], r[2] do t[b] = true end
    end
    arc.bytes("sloped_tiles", t)
    arc.sloped_tiles = t
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
    arc.building_tiles = t
end

--  A TILE WHOSE ART THE MESH DRAWS.  With the mesh on the sprite is
--  dropped and the geometry stands in for it; a tile left out keeps its
--  sprite, which is then drawn over the mesh at the sprite's own height
--  -- a piece of thread hanging in the air over a surface already there.
--  So this names exactly what the mesh draws as a strip, and a piece the
--  mesh learns to draw is turned on here.
do
    local t = {}
    --  The line, thread and meet pieces, and the eight band slab ids.
    for b = 0x0E, 0x50 do t[b] = true end
    --  The on-spurs, the four spur ids, the four curve blocks, and the
    --  2x2 interchange the band walk carries a slab across
    --  (scripts/compose/bands.lua).
    for b = 0x5D, 0x69 do t[b] = true end
    --  What lies between, 0x51 to 0x5C -- the long inclines, the bridges
    --  and the corner fills beside a curve -- is still sprites.
    arc.bytes("meshed_tiles", t)
end

--  AN ELEVATED PIECE, which takes its order from the neighbour that owns
--  the span rather than from its own tile, so a raised slab and the tile
--  it crosses do not each claim the painter's place.
do
    local t = {}
    for b = 0x61, 0x6B do t[b] = true end
    arc.bytes("elevated_tiles", t)
    arc.elevated_tiles = t
end

--  A tile a CORRIDOR MAY LEVEL.  A slab levels nothing -- it stands
--  clear and its columns take up the difference -- but what runs under
--  one does, or a line climbing beneath a viaduct steps where the shelf
--  asked for a slope and the ground was drawn flat.
do
    local t = {}
    for b = 0x00, 0x48 do t[b] = true end
    for b = 0x4B, 0x4E do t[b] = true end
    arc.bytes("levelling_tiles", t)
    arc.levelling_tiles = t
end

--  A TILE WHOSE GRADED CORNERS ARE FILLED IN from the ones it has.  A
--  corridor tile graded on some of its corners but not all would tilt
--  its surface through the line, so the rest take the mean of those it
--  has.  Only a surface network piece: a building has a pad of its own
--  and a slab stands clear.
do
    local t = {}
    for b = 0x0E, 0x48 do t[b] = true end
    arc.bytes("corridor_fill", t)
end

--  A piece the SADDLE LIFT applies to: a network piece on terrain code
--  13 is drawn one step up, as the original draws it, and an elevated
--  piece never is.
do
    local t = {}
    for b = 0x0E, 0x60 do t[b] = true end
    arc.bytes("saddle_tiles", t)
end

--  WHAT OUTLINE MODE PAINTS A NETWORK TILE.  Show Curves tints the
--  ground under every network so a person can see where the pipeline
--  thinks each one runs: lines grey, railways and the level meets
--  between them yellow, an elevated band purple.  A spur tile is
--  painted orange instead, and that is not a byte -- a spur is one the
--  pipeline found, so it is decided where the tint is laid.
--
--  0 leaves a tile untinted, which is what open ground gets.
do
    local t = {}
    for b = 0x1D, 0x2B do t[b] = 9 end       -- lines
    for b = 0x2C, 0x3A do t[b] = 10 end      -- railways
    for b = 0x43, 0x48 do t[b] = 10 end      -- the meets between them
    for b = 0x49, 0x69 do t[b] = 11 end      -- every part of a band
    arc.bytes("outline_tint", t, "number")
end

--  GROUND A TURNING HEAD MAY BE DRAWN ON.  A line that dies against a
--  building ends inside its own tile and gets a round cap there, and
--  the cap is a flat fan at the end's own height: on anything but level
--  ground it cuts under the surface, so a sloped end stays square
--  instead.  Only bare flat land qualifies -- a terrain byte carrying a
--  stream, a shore or a slope does not, whatever its slope code says.
arc.bytes("cap_ground", {[0x00] = true})
