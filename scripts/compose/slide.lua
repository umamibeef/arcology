--  slide.lua -- where a spur's join meets the line.
--
--  A spur is two legs: the descent from the slab, and the join into the
--  line.  Neither end is fixed.  The descent may START further along the
--  slab than the gore, and the join may SIT further along the line than
--  the point it aimed at, and every pairing of the two is a different
--  spur.
--
--  So all of them are routed and the WIDEST radius wins.  A spur that
--  turns tightly reads as a hairpin; one that starts a tile earlier
--  along the slab has the whole tile to turn in.
--
--  Three things end a placing.  It may land on a different lane from the
--  one it aimed at, and since sliding further along the line only takes
--  it further away, that ends the slide along the line entirely.  It may
--  not route at all.  Or it may leave the spur tile by some other edge
--  than its line edge -- the spur tile is the hard rule, and a lane that
--  misses that edge is not this spur's.

local f32 = arc.put.f32

--  How far the descent may start along the slab, and in what steps.  Six
--  tiles is further than any spur needs and keeps the search bounded;
--  half a tile back from the lane line leaves the join room to turn.
local SLAB_STEP = 0.5
local SLAB_CAP  = 6.0
local SLAB_BACK = 0.5

--  And along the line.
local ROAD_STEP = f32(0.2)

--  A placing has to be more than two per cent wider to be worth taking:
--  the sampling is the dear part, and a hair more radius is not a
--  better spur.
local MARGIN = 1.02

--  The float slack the two loops end on, so a step that lands exactly on
--  the limit is walked.
local SLACK = f32(1e-4)

arc.rules.slide = function (sl)
    local d = sl:info()

    --  How far along the slab the descent may start.
    local reach = 0.0
    if d.reach then
        reach = f32(d.reach - SLAB_BACK)
        if reach > SLAB_CAP then reach = SLAB_CAP end
    end

    local best = 0.0
    local tried, off, unroutable, missed = 0, 0, 0, 0
    local limit = f32(d.merge + SLACK)

    local u = 0.0
    while u <= f32(reach + SLACK) do
        local at = 0.0
        while at <= limit do
            --  Which lane the join lands on here.  The pick is the
            --  same one the spur's foot made -- arc.lip_lane -- and
            --  the placing is off the moment it answers another lane.
            local sn = sl:snap(at)
            if sn then sn:is(arc.lip_lane(sn, sn:info())) end
            --  The chain this placing is cut from is the script's: the
            --  two poses are read, the chain is built between them
            --  (arc.chain_between, scripts/compose/links.lua), and the
            --  cut is arc.rules.pieces's like every other path's.  The
            --  placing is built from the pieces it answers.
            local qx, qy, tax, tay, px, py, tbx, tby = sl:poses(u, at)
            local r
            if not qx and qy == "off" then
                --  Off the lane aimed at: no further along the line.
                off = off + 1
                break
            end
            if qx then
                local q, rad, tlim = arc.chain_between(qx, qy, tax, tay, px, py, tbx, tby)
                if q then r = sl:routed(arc.fit(q, rad, tlim)) end
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
        u = f32(u + SLAB_STEP)
    end

    if not (best > 0.0) then sl:note(tried, off, unroutable, missed) end
    return true
end
