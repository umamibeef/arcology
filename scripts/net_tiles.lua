--  net_tiles.lua -- which of the city's building bytes carry a NETWORK
--  piece, which family it belongs to, and which of the shared layouts it
--  is.
--
--  Every surface network -- power lines, lines, railways -- is stored as
--  a run of fifteen ids, one per way the tile is joined: a dead end,
--  four straights and bends, three-way and four-way.  The three runs sit
--  one after another, so the layout is the byte's place in its own run.
--
--  A MEET carries two families at once, one on each axis: it answers
--  for the one whose surface it is, and `second` names the other.  What
--  runs UNDER a viaduct is a network too -- the slab's tile carries no
--  surface of its own, so a line or a line left out here vanishes at
--  every elevated meet, and worse: a tile no family claims falls
--  through to the BUILDING path and is given a levelled pad, so a thread
--  tile missing from this table becomes a raised slab with the thread on
--  top of it and the ground either side untouched.
--
--  The ids below were read off the shipped cities by counting which way
--  each tile's neighbours run.

do
    local t = {}

    --  The three runs of fifteen, in the order the save format lays them.
    for i = 0, 14 do
        t[0x0E + i] = {family = "power", piece = i}
        t[0x1D + i] = {family = "line", piece = i}
        t[0x2C + i] = {family = "thread", piece = i}
    end

    --  Four more thread straights, and they are NOT in the run above --
    --  two ids to an axis, because the art has two elevations of
    --  trestle.
    t[0x3B] = {family = "thread", piece = 1} -- north-south
    t[0x3C] = {family = "thread", piece = 0} -- east-west
    t[0x3D] = {family = "thread", piece = 1}
    t[0x3E] = {family = "thread", piece = 0}

    --  The six meets.  A line under a power line, a line over a
    --  thread, a thread under a power line; each in both axes.
    t[0x43] = {family = "line", piece = 0, second = {family = "power", piece = 1}}
    t[0x44] = {family = "line", piece = 1, second = {family = "power", piece = 0}}
    t[0x45] = {family = "line", piece = 0, second = {family = "thread", piece = 1}}
    t[0x46] = {family = "line", piece = 1, second = {family = "thread", piece = 0}}
    t[0x47] = {family = "thread", piece = 0, second = {family = "power", piece = 1}}
    t[0x48] = {family = "thread", piece = 1, second = {family = "power", piece = 0}}

    --  What a viaduct spans: the surface under the slab's own tile.
    t[0x4B] = {family = "line", piece = 1} -- a north-south line
    t[0x4C] = {family = "line", piece = 0} -- an east-west one
    t[0x4D] = {family = "thread", piece = 1}
    t[0x4E] = {family = "thread", piece = 0}

    arc.pieces(t)
end
