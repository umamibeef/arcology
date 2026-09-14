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

--  THE MERGE LANE'S CHAIN (scripts/compose/drop.lua): the lane's points
--  from the far end of its taper to the gore (arc.merge_points), and
--  the descent from there to the line, all from the slab's own stations
--  so the lane lies on the slab however it curves.  The lane a car
--  merges on is the slab's own surface, a lane wider there; the ramp's
--  strip is drawn from the gore outward (scripts/compose/strip.lua).
--  Answers the chain, or nothing where the descent folds.
local function merge_chain(g, px, py, tbx, tby)
    local q, gdx, gdy = arc.merge_points(g)
    local q2 = arc.chain_between(q[#q].x, q[#q].y, gdx, gdy, px, py, tbx, tby)
    if not q2 then return nil end
    for i = 2, #q2 do q[#q + 1] = q2[i] end
    local rad, tlim = {}, {}
    for i = 1, #q do
        rad[i] = arc.geo.interchange_radius
        local d1 = i > 1 and arc.dist(q[i - 1].x, q[i - 1].y, q[i].x, q[i].y) or 1e9
        local d2 = i < #q and arc.dist(q[i].x, q[i].y, q[i + 1].x, q[i + 1].y) or 1e9
        tlim[i] = 0.5 * math.min(d1, d2)
    end
    return q, rad, tlim
end

arc.rules.slide = function (sl)
    local d = sl:info()

    --  How far along the slab the descent may start.
    local reach = 0.0
    if d.reach then
        reach = f32(d.reach - SLAB_BACK)
        if reach > SLAB_CAP then reach = SLAB_CAP end
    end
    --  A ramp with a merge lane starts its descent where the lane says,
    --  and slides along the line alone.
    local gore

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
                if gore == nil then
                    local g, away = arc.merge_of(qx, qy, "top", 0.3)
                    gore = g or false
                    if not gore and arc.geo.pose_dump > 0.5 then
                        local h, far = arc.merge_of(qx, qy, "top", 1e9)
                        arc.dump(string.format("SLIDE at %.2f,%.2f: no merge lane begins near; the nearest top is %s's at %.2f,%.2f, %.2f away", qx, qy,
                            h and string.format("%d,%d", math.floor(h.x), math.floor(h.y)) or "none",
                            h and h.merge.top.x or 0, h and h.merge.top.y or 0, far or -1))
                    end
                    if gore then
                        reach = 0.0
                        if arc.geo.pose_dump > 0.5 then
                            local _, _, gdx, gdy = arc.slab_at(gore.parent, gore.merge.s_g)
                            arc.dump(string.format("SLIDE %d,%d: merge lane %.2f to %.2f, top %.2f from the slab's, way at the gore %+.2f,%+.2f against the slab's %+.2f,%+.2f (%s)",
                                math.floor(gore.x), math.floor(gore.y), gore.merge.s_p, gore.merge.s_q, away, tax, tay,
                                -gore.merge.m * gdx, -gore.merge.m * gdy,
                                (tax * gdx + tay * gdy) * -gore.merge.m > 0.0 and "agree" or "DISAGREE"))
                        end
                    end
                end
                local q, rad, tlim
                if gore then q, rad, tlim = merge_chain(gore, px, py, tbx, tby)
                else q, rad, tlim = arc.chain_between(qx, qy, tax, tay, px, py, tbx, tby) end
                if q then
                    local pc = arc.fit(q, rad, tlim)
                    if gore and arc.geo.pose_dump > 0.5 then
                        local function dir_at(pcs, t)
                            if pcs.arc then
                                local th = pcs.t0 + (pcs.t1 - pcs.t0) * t
                                local sg = pcs.t1 > pcs.t0 and 1.0 or -1.0
                                return -sg * math.sin(th), sg * math.cos(th)
                            end
                            local l = math.max(1e-9, pcs.len)
                            return (pcs.bx - pcs.ax) / l, (pcs.by - pcs.ay) / l
                        end
                        local worst, where = 1.0, 0
                        for k = 1, #pc - 1 do
                            local ax, ay = dir_at(pc[k], 1.0)
                            local bx, by = dir_at(pc[k + 1], 0.0)
                            local dt = ax * bx + ay * by
                            if dt < worst then worst, where = dt, k end
                        end
                        arc.dump(string.format("FITTED %d,%d at %.2f: %d pieces, worst joint %d at %.1f degrees (%s)",
                            math.floor(gore.x), math.floor(gore.y), at, #pc, where, math.deg(math.acos(math.max(-1, math.min(1, worst)))),
                            where > 0 and (pc[where].arc and "arc" or "straight") .. " to " .. (pc[where + 1].arc and "arc" or "straight") or "-"))
                        arc.fitted_shown = arc.fitted_shown or 0
                        if worst < 0.99 and where >= 2 and arc.fitted_shown < 2 and at < 0.01 then
                            arc.fitted_shown = arc.fitted_shown + 1
                            local qs = {}
                            for _, pt in ipairs(q) do qs[#qs + 1] = string.format("%.3f,%.3f", pt.x, pt.y) end
                            arc.dump("  q: " .. table.concat(qs, " "))
                            for k, pcs in ipairs(pc) do
                                if pcs.arc then
                                    arc.dump(string.format("  %d arc c %.3f,%.3f r %.3f t0 %.4f t1 %.4f a %.3f,%.3f b %.3f,%.3f len %.3f", k, pcs.cx, pcs.cy, pcs.r, pcs.t0, pcs.t1, pcs.ax, pcs.ay, pcs.bx, pcs.by, pcs.len))
                                else
                                    arc.dump(string.format("  %d run a %.3f,%.3f b %.3f,%.3f len %.3f", k, pcs.ax, pcs.ay, pcs.bx, pcs.by, pcs.len))
                                end
                            end
                        end
                    end
                    r = sl:routed(pc)
                end
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
    if arc.geo.pose_dump > 0.5 then
        arc.dump(string.format("SLID pass %d %s: %d tried, %d off the lane, %d unroutable, %d miss the line edge, best %.2f",
            arc.current_pass or 0, gore and string.format("%d,%d", math.floor(gore.x), math.floor(gore.y)) or "no merge",
            tried, off, unroutable, missed, best))
    end
    return true
end
