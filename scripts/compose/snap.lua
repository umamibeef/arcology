--  snap.lua -- which lane a spur's end fastens to.
--
--  A spur has two ends and each must find a lane.  The pipeline measures
--  every lane within reach -- one candidate a PIECE, because a lane's
--  nearest station may run the wrong way where one further along runs
--  the right way -- and this picks among them.  There is no pick in C
--  behind it: answer nothing and a spur's ends fasten to nothing.
--
--  Two situations, and they want different lanes:
--
--    the TOP, on the slab: the nearest station of the spur's own band,
--      whichever lane of it that is -- a spur meets the way it
--      belongs to and no other.
--    the FOOT, on the line: the LIP-SIDE lane, whatever lies nearer.
--      A spur joins the outside lane of the line it comes down to, and
--      only where there is none does it take the nearest turn inside a
--      junction's box instead.
--
--  Every station is measured in the world's own precision; the
--  comparisons are made in it too, or a lane a hair beyond reach would
--  be taken and a spur would fasten across a line.
local f32 = arc.put.f32

--  The LIP-SIDE lane among the candidates: the one furthest from its
--  line's own centreline, and among equals the nearer.  A spur's FOOT
--  asks for it, and so does the JOIN sliding along the line, which must
--  stay on the lane the foot picked -- so it is written once here and
--  slide.lua calls it.  Answers the candidate's index, or nothing.
function arc.lip_lane(s, d)
    local best, bd, boff
    for i = 0, d.n - 1 do
        local c = s:at(i)
        --  Running the spur's way, and near enough to fasten to.
        if c.line and c.dot > d.dot and c.dist < d.reach then
            local off = math.abs(c.off)
            if not boff or off > f32(boff + 1e-4)
               or (off > f32(boff - 1e-4) and c.dist < bd) then
                bd, boff, best = c.dist, off, i
            end
        end
    end
    return best
end

arc.rules.spur_lane = function (s)
    local d = s:info()
    local best, bd

    if d.what == "slab" then
        --  the spur's own band, nearest station
        for i = 0, d.n - 1 do
            local c = s:at(i)
            if c.dot > d.dot and c.dist < d.reach
               and c.slab and c.band == d.band and (not bd or c.dist < bd) then
                bd, best = c.dist, i
            end
        end
    else
        best = arc.lip_lane(s, d)
    end

    --  No line lane at all: the nearest turn inside a junction's box.
    if not best and d.what == "line" then
        for i = 0, d.n - 1 do
            local c = s:at(i)
            if c.turn and c.dot > d.dot and c.dist < d.reach
               and (not bd or c.dist < bd) then
                bd, best = c.dist, i
            end
        end
    end

    s:is(best)
    return true
end
