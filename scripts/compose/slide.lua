--  slide.lua -- where a ramp's join meets the road.
--
--  A ramp is two legs: the descent from the deck, and the join into the
--  road.  Neither end is fixed.  The descent may START further along the
--  deck than the gore, and the join may SIT further along the road than
--  the point it aimed at, and every pairing of the two is a different
--  ramp.
--
--  So all of them are routed and the WIDEST radius wins.  A ramp that
--  turns tightly reads as a hairpin; one that starts a tile earlier
--  along the deck has the whole tile to turn in.
--
--  Three things end a placing.  It may land on a different lane from the
--  one it aimed at, and since sliding further along the road only takes
--  it further away, that ends the slide along the road entirely.  It may
--  not route at all.  Or it may leave the ramp tile by some other edge
--  than its road edge -- the ramp tile is the hard rule, and a lane that
--  misses that edge is not this ramp's.

local f32 = arc.put.f32

--  How far the descent may start along the deck, and in what steps.  Six
--  tiles is further than any ramp needs and keeps the search bounded;
--  half a tile back from the lane line leaves the join room to turn.
local DECK_STEP = 0.5
local DECK_CAP  = 6.0
local DECK_BACK = 0.5

--  And along the road.
local ROAD_STEP = f32(0.2)

--  A placing has to be more than two per cent wider to be worth taking:
--  the sampling is the dear part, and a hair more radius is not a
--  better ramp.
local MARGIN = 1.02

--  The float slack the two loops end on, so a step that lands exactly on
--  the limit is walked.
local SLACK = f32(1e-4)

arc.rules.ramp_slide = function (sl)
    local d = sl:info()

    --  How far along the deck the descent may start.
    local reach = 0.0
    if d.reach then
        reach = f32(d.reach - DECK_BACK)
        if reach > DECK_CAP then reach = DECK_CAP end
    end

    local best = 0.0
    local tried, off, unroutable, missed = 0, 0, 0, 0
    local limit = f32(d.merge + SLACK)

    local u = 0.0
    while u <= f32(reach + SLACK) do
        local at = 0.0
        while at <= limit do
            local r, why = sl:route(u, at)
            if not r and why == "off" then
                --  Off the lane aimed at: no further along the road.
                off = off + 1
                break
            end
            if not r then
                unroutable = unroutable + 1
            elseif r <= f32(best * MARGIN) then
                --  No wider than what is held: not worth the sampling.
            elseif not sl:exits() then
                missed = missed + 1
            else
                best = r
                sl:keep(d.taper)
            end
            tried = tried + 1
            at    = f32(at + ROAD_STEP)
        end
        u = f32(u + DECK_STEP)
    end

    if not (best > 0.0) then sl:note(tried, off, unroutable, missed) end
    return true
end
