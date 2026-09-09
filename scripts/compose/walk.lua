--  walk.lua -- going between two of a path's lines the plain way.
--
--  Where two lines neither cross near enough for one corner nor leave
--  room for a biarc, the path goes between them the way it did before
--  there were lines at all: the first line's end, every point of the gap
--  between them, and the next line's start.
--
--  An end that the gap point beside it already lies in line with is left
--  out.  As a vertex it turns nothing, and it only shortens the edge
--  that the real corner beside it may take its tangent from.
--
--  One gap has a shape of its own.  A single point, square to two
--  parallel lines heading the same way, one tile across, is the data
--  drawing a SIDEWAYS STEP -- and as two vertices that is two elbows a
--  tile apart.  Drawn instead as one diagonal it is a 45 degree line
--  between the middles of the two tile edges it crosses, with a 45
--  degree corner at each end.  A road's band holds on that line; a wider
--  band does not, so only a narrow one is stepped this way.

--  Half a tile of straight stays either side of the diagonal: a corner a
--  quarter tile from a junction's port turns the port itself.  Where a
--  neighbour is closer than that the corners slide toward the step's own
--  tiles, but never past them.
local STRAIGHT = 0.5
local SLIDE    = 0.4

--  A band wider than this cannot hold the diagonal: the corner the line
--  passes lies 0.354 off it.
local STEPPABLE = 0.36

arc.rules.walk = function (w)
    local d = w:info()
    local keep_head, keep_tail = d.head, d.tail

    if d.gap > 0 then
        if w:inline("behind") then keep_head = false end
        if w:inline("ahead")  then keep_tail = false end
    end

    if not keep_head and keep_tail and d.gap == 1 and d.half <= STEPPABLE and w:jog() then
        if w:diagonal(STRAIGHT, SLIDE) then return true end
    end

    if keep_head then w:place("behind") end
    for t = 1, d.gap do w:point(t) end
    if keep_tail then w:place("ahead") end
    return true
end
