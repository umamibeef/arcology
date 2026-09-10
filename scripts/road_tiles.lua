--  line_tiles.lua -- which of the city's building bytes carry a line.
--
--  Three answers, because three different things want to know.
--
--  "line" is a line piece proper, the fifteen layouts from a dead end to
--  a crossroads.  "meet" is a line running under a power line or
--  over a railway, which is still a line to anything that joins it.
--  "under" is an elevated slab standing on a tile that has a line
--  beneath it: nothing about the tile's surface is line, but a spur
--  coming down on it lands on one.
--
--  A spur looks for all three; a lane looking for something to connect
--  to along the slab looks only at the first two.

--  The three are numbered, and the numbers are the vocabulary the
--  pipeline reads: 1 a line proper, 2 a meet that is still a line to
--  anything joining it, 3 a slab with a line beneath it.
do
    local t = {}
    for b = 0x1D, 0x2B do t[b] = 1 end
    for b = 0x43, 0x48 do t[b] = 2 end
    for _, b in ipairs {0x4B, 0x4C, 0x4F, 0x50} do t[b] = 3 end
    arc.bytes("line_tiles", t, "number")
    --  And the same table for the scripts that walk the map themselves:
    --  orient.lua counts the lines beside a spur off `xbld` rather than
    --  off the pipeline's planes, and the two must agree.
    arc.line_tiles = t
end

--  Ground a fitted line may sweep across, when its family is allowed to
--  leave its own cells at all.  A railway sweeps its corners across the
--  field and runs a staircase as one line; held to its tiles it turns
--  inside each of them at a radius of 0.4, and a branch comes out as a
--  chain of hooks with the thread broken where two of them meet.
--
--  Bare ground, rubble and trees are free.  Anything built or laid is
--  not, and water is refused by the terrain layer rather than here.
do
    local t = {}
    for b = 0x00, 0x0D do t[b] = true end
    arc.bytes("open_tiles", t)
end

--  WHAT A RAISED SLAB MAY SWEEP OVER, by the byte the city stores:
--
--    0  in the way          1  free air          2  free air whose
--                                                   height is measured
--
--  A viaduct is raised, so most of the map is air to it.  A surface
--  network it straddles on a bent is air.  So is a structure -- a
--  building, a park, anything from the first structure id onward: the
--  slab takes the air and the building renderer keeps buildings off it
--  afterwards.  Its height is measured all the same, since what a slab
--  clears is the sort of thing a rule may want to change its mind about.
--
--  Another slab IS in the way, being at the same height, and
--  arc.band_tiles names every byte that is one.
do
    local air = {}
    for b = 0x00, 0x68 do air[b] = 1 end
    for b = 0x69, 0xFF do air[b] = 2 end
    for b in pairs(arc.band_tiles) do air[b] = 0 end
    arc.bytes("slab_air", air, "number")
end

--  A CARRIER: a tile a line runs ON INTO rather than stopping at.
--
--  A margin ends where its line does, so a line whose last tile is
--  followed by a bridge, a tunnel end, a meet or a band has not
--  ended at all -- the way carries on over or under, and the
--  margin should not put a lip across it.
--
--  The run covers the bridges and tunnel ends, the six meets, and
--  every band id up to and including the interchange.
do
    local t = {}
    for b = 0x3B, 0x69 do t[b] = true end
    arc.bytes("carrier_tiles", t)
end

--  WHAT A SEGMENT'S LAST TILE ENDS AGAINST, on the dead side its art
--  points at.  1 is a CARRIER the line runs on into -- a bridge, a
--  tunnel end, a meet, a band -- and the segment stops square to
--  the tile's edge, as the original draws the straight piece whole and
--  the carrier's sprite goes on from there.  2 is a BUILDING, and a line
--  whose family ends at one gets its turning head instead.  Nothing else
--  ends a segment: against open land it stops at the tile's centre.
--
--  This reaches further than `carrier_tiles` at one end and further than
--  `building_tiles` at the other: the six ids between the interchange
--  and the buildings proper end a line as a building does.
do
    local t = {}
    for b = 0x3B, 0x69 do t[b] = 1 end
    for b = 0x6A, 0xFF do t[b] = 2 end
    arc.bytes("end_against", t, "number")
end

--  A line meet a RAILWAY, which is a node of both networks: the two
--  are pinned to one altitude there, so a line and a railway meeting at
--  a meet agree about its height without anything being solved
--  between them.
--
--  The line-under-a-power-line meets are not this: a power line has
--  no altitude of its own to agree about.
arc.bytes("lap_tiles", {[0x45] = true, [0x46] = true})
