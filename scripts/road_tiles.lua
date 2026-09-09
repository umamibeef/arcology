--  road_tiles.lua -- which of the city's building bytes carry a road.
--
--  Three answers, because three different things want to know.
--
--  "road" is a road piece proper, the fifteen layouts from a dead end to
--  a crossroads.  "crossing" is a road running under a power line or
--  over a railway, which is still a road to anything that joins it.
--  "under" is an elevated deck standing on a tile that has a road
--  beneath it: nothing about the tile's surface is road, but a ramp
--  coming down on it lands on one.
--
--  A ramp looks for all three; a lane looking for something to connect
--  to along the deck looks only at the first two.

arc.rules.road_tiles = function ()
    local t = {}
    for b = 0x1D, 0x2B do t[b] = "road" end
    for b = 0x43, 0x48 do t[b] = "crossing" end
    for _, b in ipairs {0x4B, 0x4C, 0x4F, 0x50} do t[b] = "under" end
    return t
end

--  Ground a fitted line may sweep across, when its family is allowed to
--  leave its own cells at all.  A railway sweeps its corners across the
--  field and runs a staircase as one line; held to its tiles it turns
--  inside each of them at a radius of 0.4, and a branch comes out as a
--  chain of hooks with the track broken where two of them meet.
--
--  Bare ground, rubble and trees are free.  Anything built or laid is
--  not, and water is refused by the terrain layer rather than here.
arc.rules.open_tiles = function ()
    local t = {}
    for b = 0x00, 0x0D do t[b] = true end
    return t
end

--  A building that STANDS UP, as a raised highway sees the map.
--
--  A surface network is not in the way of a viaduct: the deck crosses
--  over it on a straddle bent.  Anything from the first structure id
--  onward is a building, a park or the like, and the deck's own free-air
--  measurement treats it separately -- the deck takes the air and the
--  building renderer keeps buildings off it afterwards.
--
--  The highway ids themselves are neither: another deck IS in the way,
--  being at the same height, and arc.rules.hiway_tiles names those.
arc.rules.standing_tiles = function ()
    local t = {}
    for b = 0x69, 0xFF do t[b] = true end
    return t
end

--  A CARRIER: a tile a road runs ON INTO rather than stopping at.
--
--  A footway ends where its road does, so a road whose last tile is
--  followed by a bridge, a tunnel end, a crossing or a highway has not
--  ended at all -- the carriageway carries on over or under, and the
--  pavement should not put a kerb across it.
--
--  The run covers the bridges and tunnel ends, the six crossings, and
--  every highway id up to and including the interchange.
arc.rules.carrier_tiles = function ()
    local t = {}
    for b = 0x3B, 0x69 do t[b] = true end
    return t
end

--  A road crossing a RAILWAY, which is a node of both networks: the two
--  are pinned to one altitude there, so a road and a railway meeting at
--  a crossing agree about its height without anything being solved
--  between them.
--
--  The road-under-a-power-line crossings are not this: a power line has
--  no altitude of its own to agree about.
arc.rules.rail_crossing_tiles = function ()
    return {[0x45] = true, [0x46] = true}
end
