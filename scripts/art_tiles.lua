--  art_tiles.lua -- what the PICTURES say, read once when the scripts
--  are read.
--
--  Two things about the city are written nowhere but in the art itself.
--  Which edges a piece of line art joins.  And how high a structure
--  stands, which the band dump reports; whether a raised band may sweep
--  over a tile at all follows from whether the art can be measured
--  there.
--
--  Both are interpretations of the material, so both are decided here.
--  arc.art offers the pictures; this reads them and pushes a table down,
--  and the pipeline looks the answer up by byte.
--
--  The FINEST level is read whatever level is drawn.  What a piece joins
--  and how tall a building is are properties of the thing, not of how
--  large its picture is, so the mesh must not change with the art set
--  the camera is near enough for.
--
--  A run with no art reads nothing.  Every table then comes out empty,
--  no byte carries a network at all, and a slab may sweep over nothing:
--  which is what a run that cannot find its pictures should draw.

local FINE = arc.art.levels() - 1
local LV   = FINE >= 0 and arc.art.level(FINE) or nil

--  WHICH EDGES A PIECE JOINS, from the line art.
--
--  The fill (palette 0x91) or a dash (0x8B), in the three by three
--  around each edge's midpoint, a tenth of the way in from it.  Two
--  painted pixels of the nine are enough: an edge the art draws a way
--  out of has a way's width there, and one it does not is bare ground.
--
--  The fifteen layouts run from a dead end to a crossroads, and their
--  art begins at 0x1D.  The four SLOPE pieces are left out: their art
--  is tall, and they are straight along their slope, which the terrain
--  says and no picture can.  Which four they are is pushed down as
--  `slope_pieces`, so that the pipeline and this file cannot come to
--  differ about it.
local PIECE_ART   = 0x1D
local PIECE_FILL  = 0x91
local PIECE_DASH  = 0x8B
local PIECE_EDGES = 4
--  Each edge's midpoint, as a share of the diamond, pulled a tenth in.
local EDGE_U = {0.25, 0.25, 0.75, 0.75}
local EDGE_V = {0.25, 0.75, 0.75, 0.25}
local EDGE_IN = 0.9

local SLOPE_FIRST, SLOPE_LAST = 2, 5

do
    local t = {}
    for piece = SLOPE_FIRST, SLOPE_LAST do t[piece] = true end
    arc.bytes("slope_pieces", t)
end

do
    local t = {}
    for piece = 0, 14 do
        local art = PIECE_ART + piece
        --  The four SLOPE pieces answer from the terrain, not from a
        --  picture, so the art is not read for them and the table holds
        --  nothing there.
        local slope = piece >= SLOPE_FIRST and piece <= SLOPE_LAST
        local tl    = LV and not slope and arc.art.tile(FINE, art)
        if tl then
            --  The diamond fills the bottom rows of the sprite.
            local y0, links = tl.h - (LV.tile_h + 1), 0
            for e = 0, PIECE_EDGES - 1 do
                local sx = math.floor((0.5 + (EDGE_U[e + 1] - 0.5) * EDGE_IN) * LV.tile_w)
                local sy = y0 + math.floor((0.5 + (EDGE_V[e + 1] - 0.5) * EDGE_IN) * LV.tile_h)
                local n  = 0
                for dy = -1, 1 do
                    for dx = -1, 1 do
                        local v = arc.art.pixel(FINE, art, sx + dx, sy + dy)
                        if v == PIECE_FILL or v == PIECE_DASH then n = n + 1 end
                    end
                end
                if n >= 2 then links = links + (1 << e) end
            end
            t[piece] = links
        end
    end
    arc.bytes("piece_links", t, "number")
end

--  HOW HIGH A STRUCTURE STANDS, in levels of altitude, from the bulk of
--  its picture.
--
--  Not the bounding box.  A chimney, an antenna or a flag would make a
--  shed a tower.  The BULK: the first row down from the top with paint
--  across two fifths of the diamond's width.  A tree stands about a
--  level and a house one.  A warehouse stands about two levels and a
--  third to its box, and less to its roof.
--
--  The art rises `ay` above the left corner of its diamond.  The top
--  corner of that diamond is half the footprint's diamond above that.
--  The rest is building, `alt_step` pixels to the level.
--
--  A byte whose picture the art set cannot show is measured as nothing,
--  and `structure_seen` says so: a tile no measurement can be taken of
--  is out of a slab's corridor.
local BULK_SHARE = 2.0 / 5.0

do
    local rise, seen = {}, {}
    if LV and LV.alt_step > 0 then
        for b = 0, 255 do
            local tl = arc.art.tile(FINE, b)
            if tl then
                local y = 0
                while y < tl.h do
                    if arc.art.row(FINE, b, y) >= BULK_SHARE * tl.foot * LV.tile_w then break end
                    y = y + 1
                end
                local r = (tl.ay - 0.5 * tl.foot * LV.tile_h - y) / LV.alt_step
                rise[b] = r > 0 and r or 0
                seen[b] = true
            end
        end
    end
    arc.floats("structure_rise", rise)
    arc.bytes("structure_seen", seen)
end

--  WHICH SPRITES ARE WHAT, in the art set's own numbering.
--
--  A sprite id is the ART SET'S, not the city's: it says which picture,
--  and the pictures are numbered by whoever made the pack.  So which
--  run of ids is the land, the water, the lattice of an empty
--  underground tile or a train belongs here beside the rest of the art,
--  rather than in the C that draws a frame.
--
--  Each run is named by its first and its last, both counted from the
--  level's own base.  The water column is one id of its own inside the
--  water's run.
arc.numbers("art_runs", {
    land_first    = 256,
    land_last     = 269,   -- the last is also the map edge's dirt column
    water_first   = 270,
    water_last    = 290,   -- open water, the shores, and the channel pieces
    water_column  = 284,
    lattice_first = 305,   -- the wireframe of an empty underground tile
    lattice_last  = 318,
    train_first   = 374,
    train_last    = 378,
})
