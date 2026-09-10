--  join.lua -- the arc where two of a path's lines cross.
--
--  Two lines that cross take one vertex at the meet, and a fillet is
--  swept into the corner there.  Three things have to be true of it.
--
--  The meet has to be AHEAD of the line behind and BEHIND the line
--  in front, or the path would double back on itself to reach it.  How
--  far outside that a meet may still sit depends on what the lines
--  are.  A run ends on its own last tile, so a meet more than three
--  quarters of a tile outside it is not this corner.  A free line ends
--  where its last point projects, which at a shallow angle can be a long
--  way past the meet: it needs only to be half a tile ahead of the
--  vertex behind and half a tile short of its own far end, and the slab
--  it draws to get there has to hold on the corridor like any other.
--
--  The corner has to leave nothing bare.  The points between the two
--  lines are the line's own tiles, and a corner that cuts inside them
--  abandons the very cells it was drawn for.
--
--  And an arc has to fit that reads as an arc.  A radius under the kink
--  is a corner drawn as a V, and the join is walked tile by tile
--  instead, whose own fillets are local and gentle.  The straights that
--  run from each line's end into the arc must hold too: two 45 degree
--  arms meeting at a right angle cross beyond the end of one of them.

local f32 = arc.put.f32

--  An arc under this reads as a corner.
local KINK = 0.30

--  How far outside its line a meet may sit: three quarters of a tile
--  for a run, and for a free line the whole of its own reach less half a
--  tile at each end.
local RUN_SLACK  = 0.75
local FREE_SLACK = 0.5

arc.rules.meet = function (j)
    local d = j:info()

    if d.free then
        if d.ahead <= -(d.reach - FREE_SLACK) then return true end
        if d.behind <= -(d.reach_on - FREE_SLACK) then return true end
        if d.ahead < 0.0 and not j:holds("behind") then return true end
        if d.behind < 0.0 and not j:holds("ahead") then return true end
    else
        if d.ahead <= -RUN_SLACK or d.behind <= -RUN_SLACK then return true end
    end

    if not j:covers() then return true end

    --  The tangent the meet may take: the edge behind is the end's
    --  own budget where it is the path's, the biarc's remainder where the
    --  vertex behind was built by one, and otherwise this corner's share.
    local bin, bout
    if d.first then
        bin = arc.end_budget(d.len_in, d.reserve0, d.need, d.trim_cap)
    elseif d.fixed_prev >= 0.0 then
        bin = f32(d.len_in - d.fixed_prev)
    else
        bin = f32(d.share * d.len_in)
    end
    if d.last then
        bout = arc.end_budget(d.len_out, d.reserve1, d.need, d.trim_cap)
    else
        bout = f32(d.share * d.len_out)
    end

    --  The radius the corridor allows there: the sampling is the
    --  pipeline's, the search over it arc.rules.sweep's.
    local s = j:arc(bin < bout and bin or bout)
    if s then arc.rules.sweep(s) end
    local r = j:swept()
    if r < KINK then return true end
    if not j:legs(r) then return true end

    j:place()
    return true
end
