--  match.lua -- WHERE A SLAB LANE GOES WHEN ITS SLAB RUNS OUT.
--
--  A slab lane's end is one of four situations, and telling them apart
--  is the whole of this file.  The pipeline offers the lanes as they
--  stand and lays what it is told to lay.
--
--    the inner lane, with a line ahead
--        the slab has come down to grade: the lane goes into the line
--        lane whose open end faces it, nearest first.
--    the inner lane, with no line
--        the slab goes on as ANOTHER BAND -- the walk splits a slab at
--        an interchange or a corner and leaves the lanes either side
--        facing each other -- so the lane looks for that band's lane.
--        Failing that the band simply ends open.
--    a middle or outer lane, with a band ahead
--        the same continuation, at its own offset: this is what carries
--        a three-lane way round a loop without dropping a lane.
--    a middle or outer lane, with nothing ahead
--        it tapers into the inner lane of its own band, leaving a
--        taper's length back from the end, so the way narrows
--        instead of stopping in mid-air.
--
--  The ends an interchange node has already spent are gone before this
--  runs (node.lua).

local geom = require "compose.links.chain"

local f32  = geom.f32
local sqrt = geom.sqrt
local sub  = geom.sub
local dot  = geom.dot
local dist = geom.dist

local M = {}

M.run = function (x, d, n)
    local route = arc.chain_between
--  One link laid: the chain the router answers, cut into pieces the
--  same way every other path in the city is.
local function join(ax, ay, adx, ady, bx, by, bdx, bdy, w, from, to, band)
    local q, rad, tlim = route(ax, ay, adx, ady, bx, by, bdx, bdy)
    if not q then
        x:note("link_fail")
        return
    end
    local pc = arc.fit(q, rad, tlim)
    if #pc < 1 then
        x:note("link_fail")
        return
    end
    x:link(pc, w, from, to, band)
    return pc
end

--  The lane of ANOTHER band whose open end faces this one: ahead of
--  it, or level with it round a corner, within reach, running on or
--  turning up to a right angle; the nearest wins.  The chain must
--  also be one the fit can cut -- the window admits a band end lying
--  BESIDE this one, two ways of an interchange abreast, and
--  that is not a continuation at all.
local function continuation(li, which, pdx, pdy, ddx, ddy)
    local best, ba = nil, d.band_reach
    local bx, by, bdx, bdy
    for lj = 0, n - 1 do
        if lj ~= li then
            local r = x:lane(lj)
            if r and r.slab and (which == 1 and r.open0 or which == 0 and r.open1) then
                local p2x, p2y, d2x, d2y = x:pose(lj, which == 1 and 0 or 1)
                if dot(d2x, d2y, ddx, ddy) >= d.band_dot then
                    local vx, vy
                    if which == 1 then vx, vy = sub(p2x, pdx), sub(p2y, pdy)
                    else vx, vy = sub(pdx, p2x), sub(pdy, p2y) end
                    local ahead = dot(vx, vy, ddx, ddy)
                    local aside = math.abs(f32(f32(vx * ddy) - f32(vy * ddx)))
                    local dd = sqrt(f32(f32(vx * vx) + f32(vy * vy)))
                    --  The far end must LEAD AWAY along the way the
                    --  traffic travels.  `ahead` says the two ends
                    --  lie the right way round; this says the far one
                    --  faces on rather than back.  Without it a band
                    --  that turns a corner matches its own lanes
                    --  across the turn: the way then leaves one lane,
                    --  swings through half a circle and arrives in
                    --  another lane of the same band, which is a
                    --  U-turn and reads as a kink of 180 degrees.
                    local lead = dot(d2x, d2y, vx, vy)
                    if ahead >= d.band_ahead and dd <= ba and aside <= d.band_aside
                       and dd >= d.band_apart and lead > arc.put.f32(arc.geo.band_lead) then
                        --  the router must be able to build it
                        local q
                        if which == 1 then q = route(pdx, pdy, ddx, ddy, p2x, p2y, d2x, d2y)
                        else q = route(p2x, p2y, d2x, d2y, pdx, pdy, ddx, ddy) end
                        if q then
                            ba, best = dd, lj
                            bx, by, bdx, bdy = p2x, p2y, d2x, d2y
                        end
                    end
                end
            end
        end
    end
    return best, bx, by, bdx, bdy
end
for li = 0, n - 1 do
    local l = x:lane(li)
    if l and l.slab then
        for which = 0, 1 do
            --  which 1: the lane's travel leaves the slab here; 0: it
            --  arrives.
            if (which == 1 and l.open1) or (which == 0 and l.open0) then
                local pdx, pdy, ddx, ddy = x:pose(li, which)
                --  Only the INNER lane -- the smallest offset of its
                --  band and side -- looks for the line; the others go
                --  to it.
                local inner = true
                for lj = 0, n - 1 do
                    if lj ~= li then
                        local r = x:lane(lj)
                        if r and r.slab and r.band == l.band
                           and (r.off > 0.0) == (l.off > 0.0)
                           and math.abs(r.off) < math.abs(l.off) then
                            local p2x, p2y = x:pose(lj, which)
                            if f32(math.abs(sub(p2x, pdx)) + math.abs(sub(p2y, pdy))) < d.abreast then
                                inner = false
                            end
                        end
                    end
                end

                if not inner then
                    --  A middle or outer lane: on to the next band's
                    --  lane of its own offset where the slab goes on,
                    --  else into the inner lane's station up the band.
                    local nx, px, py, nx2, ny2 = continuation(li, which, pdx, pdy, ddx, ddy)
                    if nx then
                        x:note("band_links")
                        if which == 1 then join(pdx, pdy, ddx, ddy, px, py, nx2, ny2, l.w, li, nx, l.band)
                        else join(px, py, nx2, ny2, pdx, pdy, ddx, ddy, l.w, nx, li, l.band) end
                    else
                        local back = math.abs(l.off) > f32(d.outer * l.w) and d.taper_far or d.taper_near
                        if l.len >= f32(back + d.taper_room) then
                            local in_li, in_off = nil, 1e9
                            for lj = 0, n - 1 do
                                local r = x:lane(lj)
                                if r and r.slab and r.band == l.band
                                   and (r.off > 0.0) == (l.off > 0.0)
                                   and math.abs(r.off) < in_off then
                                    in_off, in_li = math.abs(r.off), lj
                                end
                            end
                            if in_li then
                                local psx, psy, dsx, dsy = x:station(in_li, which, back)
                                local pax, pay, dax, day = x:station(li, which, f32(back + d.taper_gap))
                                --  The taper claims THIS lane's end and
                                --  nothing of the inner one.  It meets
                                --  the inner lane at a STATION along it,
                                --  not at either of its ends, and an end
                                --  a join does not touch is passed as -1.
                                --
                                --  Claiming it costs the band both ways.
                                --  A lane end names one other and one
                                --  only, so the middle lane and the outer
                                --  lane, tapering into the same inner
                                --  lane, would ask for the same end and
                                --  the second would be refused.  And the
                                --  end asked for is at the FAR side of
                                --  the band, where a node has usually
                                --  spent it already, so the first is
                                --  refused as well and both lanes are
                                --  left stopping in mid-air.
                                if which == 1 then
                                    join(pax, pay, dax, day, psx, psy, dsx, dsy, l.w, li, -1, l.band)
                                else
                                    join(psx, psy, dsx, dsy, pax, pay, dax, day, l.w, -1, li, l.band)
                                end
                            end
                        end
                    end
                else
                    x:note("band_ends")
                    --  The inner lane: the line lane whose open end
                    --  faces this one, nearest first.
                    local best, bd = nil, d.reach
                    local brx, bry, bdx2, bdy2
                    for lj = 0, n - 1 do
                        local r = x:lane(lj)
                        if r and r.line
                           and (which == 1 and r.open0 or which == 0 and r.open1) then
                            local p2x, p2y, d2x, d2y = x:pose(lj, which == 1 and 0 or 1)
                            local dd = dist(pdx, pdy, p2x, p2y)
                            if dd < bd and dot(d2x, d2y, ddx, ddy) > d.line_dot then
                                bd, best = dd, lj
                                brx, bry, bdx2, bdy2 = p2x, p2y, d2x, d2y
                            end
                        end
                    end
                    if best then
                        if which == 1 then join(pdx, pdy, ddx, ddy, brx, bry, bdx2, bdy2, l.w, li, best, 0)
                        else join(brx, bry, bdx2, bdy2, pdx, pdy, ddx, ddy, l.w, best, li, 0) end
                    else
                        --  No line: the slab may go on as another band.
                        local nx, px, py, nx2, ny2 = continuation(li, which, pdx, pdy, ddx, ddy)
                        if nx then
                            x:note("band_links")
                            if which == 1 then join(pdx, pdy, ddx, ddy, px, py, nx2, ny2, l.w, li, nx, l.band)
                            else join(px, py, nx2, ny2, pdx, pdy, ddx, ddy, l.w, nx, li, l.band) end
                        else
                            x:note("band_open")
                        end
                    end
                end
            end
        end
    end
end
end
return M
