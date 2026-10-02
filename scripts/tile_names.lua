--  tile_names.lua -- what a report calls each of the city's building
--  bytes.
--
--  The report of an area says what the city holds on every tile.  What
--  a byte means is the script's, so what the report calls it is the
--  script's too: a second list in C would say one thing where the rest
--  of the scripts say another.
--
--  Two answers.  A MARK is two characters, for the grid the report
--  draws.  A KIND is a word, for the lines under it.

--  THE MARK a band piece carries: its own id.  The report is read to
--  tell one band piece from another, so the id itself is the mark, and
--  it stands whatever the terrain under it.
do
    local t = {}
    for b = 0x49, 0x6B do t[b] = string.format("%02x", b) end
    arc.names("band_marks", t)
end

--  THE MARK THE TERRAIN carries, by its own byte.  It wins over what
--  stands on the tile: a line under water is drawn as water.  Only the
--  water has a mark; every other terrain byte leaves the tile to what
--  stands on it.
do
    local t = {}
    for b = 0x10, 0xFF do t[b] = "~~" end
    arc.names("terrain_marks", t)
end

--  AND THE MARK every other byte carries, by what stands on the tile.
do
    local t = {}
    for b = 0x00, 0xFF do t[b] = ",," end
    t[0x00] = ".."
    for b = 0x1D, 0x2B do t[b] = "rr" end
    for b = 0x2C, 0x3A do t[b] = "tt" end
    for b = 0x3B, 0x42 do t[b] = "pp" end
    for b = 0x43, 0x48 do t[b] = "xx" end
    for b = 0x6C, 0xFF do t[b] = "bb" end
    arc.names("tile_marks", t)
end

--  AND THE WORD for each, which the report prints beside a tile it is
--  asked about.
do
    local t = {}
    for b = 0x00, 0xFF do t[b] = "building" end
    t[0x00] = "ground"
    for b = 0x01, 0x1C do t[b] = "trees or rubble" end
    for b = 0x1D, 0x2B do t[b] = "line" end
    for b = 0x2C, 0x3A do t[b] = "thread" end
    for b = 0x3B, 0x42 do t[b] = "power line" end
    for b = 0x43, 0x48 do t[b] = "level meet" end
    t[0x49] = "band straight, east-west"
    t[0x4A] = "band straight, north-south"
    for b = 0x4B, 0x50 do t[b] = "band over a line or thread" end
    for b = 0x51, 0x5C do t[b] = "band piece" end
    for b = 0x5D, 0x60 do t[b] = "on-spur" end
    for b = 0x61, 0x64 do t[b] = "band section" end
    for b = 0x65, 0x68 do t[b] = "band curve block" end
    t[0x69] = "band interchange"
    for b = 0x6A, 0x6B do t[b] = "band piece" end
    arc.names("tile_kinds", t)
end
