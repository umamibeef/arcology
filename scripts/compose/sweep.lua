--  sweep.lua -- the widest fillet a corner will take.
--
--  A corner of a fitted path is given a tangent length, and a fillet of
--  radius r needs r * tan(turn / 2) of it.  That fixes the widest arc
--  the corner could have; whether an arc that wide actually FITS is
--  another matter, because the band it carries has to stay on the
--  corridor and still cover the tiles the corner was drawn for.
--
--  So the search runs downward: start at the widest the tangent allows,
--  narrow by six per cent at a time, and take the first that holds.
--  Six per cent is fine enough that the answer is not visibly coarse and
--  coarse enough that a corner costs a few dozen samples rather than a
--  few hundred.
--
--  It stops at a floor, and the floor is the interesting part.  An arc
--  narrower than the band's own half width folds its inner edge through
--  itself -- a slab a tile wide each side has no arc at all under a tile
--  of radius -- and an arc narrower than the kink stops reading as an
--  arc and is better drawn as a corner.  Below the floor the answer is
--  no arc, and the corner is reported tight so the fit can say so.

local f32 = arc.put.f32

--  An arc under this reads as a corner.
local KINK  = 0.30
--  What each step of the search narrows the radius by.
local DECAY = f32(0.94)

arc.rules.sweep = function (s)
    local d = s:info()
    if d.straight then
        s:answer(0.0, false)
        return true
    end

    --  The widest the tangent allows, and never wider than the family's
    --  own limit.
    local cap = f32(d.tangent / d.tan_half)
    if cap > d.rmax then cap = d.rmax end

    --  The band's own half width, not the padded one.
    local floor = d.padded and f32(d.half - d.margin) or d.half
    if floor < KINK then floor = KINK end
    if cap < floor then cap = 0.0 end

    local r = cap
    while r >= floor do
        if s:holds(r) then
            s:answer(r, r < d.rmin)
            return true
        end
        r = f32(r * DECAY)
    end

    s:answer(0.0, true)
    return true
end
