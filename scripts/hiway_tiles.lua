--  band_tiles.lua -- which of the city's building bytes are a band.
--
--  An elevated band is stored as a run of ids that alternate between
--  the two axes, and the band a slab tile belongs to runs whichever way
--  its id says.
--
--  The spurs belong to the band too.  Without them the slab stops at the
--  last elevated tile, a way ending in mid-air over the field.
--  Their axis is that of the slab they carry, which the ids do not state
--  and which is read off the shipped cities instead: 0x62 and 0x64 join
--  an east-west band, 0x61 and 0x63 a north-south one.  Two of the 200
--  spur tiles in the collection disagree.

do
    local t = {}
    for _, b in ipairs {0x49, 0x4B, 0x4D, 0x4F} do t[b] = {kind = "slab", axis = "ew"} end
    for _, b in ipairs {0x4A, 0x4C, 0x4E, 0x50} do t[b] = {kind = "slab", axis = "ns"} end
    for _, b in ipairs {0x62, 0x64}             do t[b] = {kind = "incline", axis = "ew"} end
    for _, b in ipairs {0x61, 0x63}             do t[b] = {kind = "incline", axis = "ns"} end
    --  A band meet something at its own level: a line, a railway
    --  or a power line, each way about.  Part of the band's run, but not
    --  a slab tile of it.
    for b = 0x51, 0x5C do t[b] = {kind = "over", axis = "ns"} end
    --  The on-spurs, one id per direction.  Each is one tile beside the
    --  slab cell it climbs to, and is no part of the band itself.
    for b = 0x5D, 0x60 do t[b] = {kind = "spur", axis = "ns"} end
    --  The curve blocks: four tiles of one id carrying a band through a
    --  right angle.  The id is not trusted for the orientation, which is
    --  read off the runs that touch the block instead.
    for b = 0x65, 0x68 do t[b] = {kind = "curve", axis = "ns"} end
    --  The interchange, a 2x2 where four bands meet.
    t[0x69] = {kind = "junction", axis = "ns"}
    arc.bands(t)
    --  And the same table for the scripts that walk the map themselves:
    --  bands.lua reads the interchange off `xbld` rather than off the
    --  pipeline's planes, and the two must agree about which byte is
    --  which.
    arc.band_tiles = t
end
