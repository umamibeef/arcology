--  pieces.lua -- a fitted path cut into the pieces a strip is lofted from.
--
--  The fit leaves a chain of vertices, each with a radius and a tangent
--  budget.  This turns that into straights and arcs laid end to end,
--  which is what everything downstream draws along.
--
--  Each corner is given the radius the sweep found for it, but three
--  things can still cut it down, and all three are the same rule: an arc
--  may not eat an edge that is not there.
--
--    * its own tangent budget, which is its share of the two edges;
--    * what the piece already laid on the way in has left of the
--      incoming edge -- a tangent point behind where the last piece
--      ended would fold the strip back through itself;
--    * the edge it leaves along, which the next corner has not taken its
--      share of yet.
--
--  A corner the corridor gave no room to sweep is left as a corner: the
--  line runs into the vertex and turns.  Dropping the vertex instead
--  loses the path's shape -- with every such corner dropped a segment
--  becomes one straight line between its two ends, and the tiles it
--  should have run through come out bare.

local f32 = arc.put.f32

--  No arc is drawn tighter than this.  Below it the two tangent points
--  are the same point and the arc is a rounding error.
local FLOOR = 0.04

arc.rules.pieces = function (p)
    local d = p:info()

    for i = 1, d.n - 2 do
        local c = p:corner(i)
        --  Nothing for a vertex the path runs straight through.
        if c then
            if c.radius <= 0.0 then
                p:straight(i)
            else
                local r   = c.radius
                local lim = c.tangent
                if lim > c.room    then lim = c.room end
                if lim > c.leaving then lim = c.leaving end
                if f32(r * c.tan_half) > lim then r = f32(lim / c.tan_half) end
                if r < FLOOR then r = FLOOR end
                p:arc(i, r)
            end
        end
    end

    p:tail()
    return true
end
