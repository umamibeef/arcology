--  drop.lua -- the lane a ramp MERGES on, station by station (spec 7.3).
--
--  A ramp is not a lane cut out of the slab.  The slab keeps its lanes,
--  and the ramp's lane runs BESIDE it as a lane of its own -- the merge
--  lane -- for arc.geo.ramp_merge tiles from the gore, and joins the
--  outer lane over arc.geo.ramp_taper tiles at its far end.  An OFF
--  ramp's merge lane lies upstream of its gore, where the traffic slows
--  and leaves; an ON ramp's downstream, where it comes up to speed and
--  merges.  Over the merge lane the slab's edge on that side stands a
--  lane's width out, and over the taper it runs out to it, so the slab
--  itself draws the lane a driver merges on and the ramp's own strip
--  takes over at the gore.  What this works out, station by station,
--  is how wide the slab is at that station on each side, and where each
--  gore's merge lane lies along the slab, which the ramp's own chain is
--  then built from (scripts/compose/slide.lua) and its strip drawn
--  from (scripts/compose/strip.lua): arc.poses.gore[cell].merge.
--
--  A MERGE LANE KEEPS CLEAR OF AN INTERCHANGE.  A ramp that merges at
--  the arm of a node leaves a driver no length to change lanes for the
--  way they need.  So the far end of a merge lane stands
--  arc.geo.ramp_clear tiles from a band end that meets a node, and the
--  lane is shortened, to no less than arc.geo.ramp_merge_min, to keep
--  that clear.  A ramp that cannot is laid short and SAID, whether
--  anyone asked or not: a ramp beside an interchange is a fault in the
--  map, and a fault the network hides is one nobody fixes.
--
--  Every spur asks, and each station keeps the WIDEST width anyone
--  asked for, so two merge lanes overlapping do not undo each other.
--
--  AN ARM FANS.  At an end that meets an interchange each lane ends its
--  own distance short of the band's end, the inner lane at the end and
--  each lane out one step of arc.geo.interchange_fan further back, on
--  both sides alike: the ways leaving by the arm and the ways arriving
--  at it fan the same.  A way that leaves from further back is longer,
--  and has more run to climb over what it crosses.  A lane that ends
--  is a lane drop: the slab's edge tapers in behind it and the way is
--  a ribbon of its own from the taper's end.  The lane model lays each
--  lane as far as it is told here (dr:cut) and no further.

local f32 = arc.put.f32

--  How far before a band's end the fan's furthest lane ends: the outer
--  of three lanes, two steps back.  The profile holds an arm level over
--  this length (scripts/compose/profile.lua), and a node's catchment
--  reaches this much further out (scripts/compose/interchange).
function arc.fan_reach()
    return 2.0 * arc.geo.interchange_fan
end

--  Which side of the slab a point lies on, from the station's heading:
--  0 is the viewer's right of the way it runs, 1 the left.
local function side_of(st, x, y)
    return f32(f32(f32(x - st.x) * st.dy) - f32(f32(y - st.y) * st.dx)) > 0.0 and 0 or 1
end

--  OVER 150 LINES, and one page on purpose: it is ONE construction.
--  A spur's taper is settled from the stations, the gores and the
--  widths together, and every step reads the arrays the step before it
--  filled.  Cut in two, the two halves would hand the same arrays back
--  and forth and nothing would be simpler.
arc.rules.drop = function (dr)
    local d = dr:info()
    dr:clear()
    if d.n < 1 then return true end

    --  The stations, and each spur's nearest one on this band.
    local st = {}
    for i = 0, d.n - 1 do st[i] = dr:station(i) end
    local total = st[d.n - 1].at

    local P  = arc.poses
    local be = arc.band_fitting and P and P.band_end and P.band_end[arc.band_fitting]
    local xbld = arc.city.plane("xbld")
    local L, T   = arc.geo.ramp_merge, arc.geo.ramp_taper
    local wide   = 1.0 + arc.geo.ramp_lane_w
    local clear  = arc.geo.ramp_clear
    local least  = arc.geo.ramp_merge_min
    local widths = {}

    --  Each spur's place on the band first: its nearest station, its
    --  side, the way its lane runs beside the slab and where its tiles'
    --  TOP stands, so each can keep clear of the others.
    local sp = {}
    for r = 0, d.spurs - 1 do
        local spur = dr:spur(r)
        local i0, best = nil, d.reach
        for i = 0, d.n - 1 do
            local dx, dy = f32(st[i].x - spur.x), f32(st[i].y - spur.y)
            local dist = arc.put.sqrt(f32(f32(dx * dx) + f32(dy * dy)))
            if dist < best then best, i0 = dist, i end
        end
        --  A spur the grading pass has no length for is left alone: the
        --  building pass reads the ramp's tiles and lays the lane.
        if arc.geo.pose_dump > 0.5 and not (i0 and spur.len >= 0) then
            arc.dump(string.format("SPUR pass %d %d,%d %s: c0 %.2f,%.2f len %d, nearest station %s",
                arc.current_pass or 0, math.floor(spur.tx), math.floor(spur.ty), spur.leaves and "off" or "on",
                spur.x, spur.y, spur.len, i0 and string.format("%d at %.2f", i0, best) or "none within reach"))
        end
        if i0 and spur.len >= 0 then
            local here = st[i0]
            local side = side_of(here, spur.tx, spur.ty)
            --  Whether the slab's way on this side runs with the stations:
            --  the spur's own axis runs with the traffic it serves.
            local traffic = f32(f32(here.dx * spur.ax) + f32(here.dy * spur.ay)) > 0.0 and 1 or -1
            --  Which way the merge lane lies from the gore: upstream for
            --  a ramp that leaves, downstream for one that joins.  The
            --  ramp's tiles run from the spur's point in that direction,
            --  and their TOP is at their far end.
            local m = spur.leaves and -traffic or traffic
            sp[#sp + 1] = {spur = spur, here = here, i0 = i0, side = side, traffic = traffic, m = m,
                           s_top = here.at + m * (spur.len - 0.5), s_c0 = here.at}
        end
    end

    for _, me in ipairs(sp) do
        do
            local spur, here, side, traffic, m, s_top = me.spur, me.here, me.side, me.traffic, me.m, me.s_top
            --  HOW FAR THE LANE MAY RUN beside the slab past its top: to
            --  the next ramp on this side ahead of it, less a gap, and
            --  half way where that ramp's lane runs back toward this one.
            local limit = 1e9
            for _, o in ipairs(sp) do
                if o ~= me and o.side == side then
                    local ahead = (o.s_top - s_top) * m
                    local tiles = (o.s_c0 - s_top) * m
                    local near  = math.min(ahead, tiles)
                    if near > 0.0 then
                        local room = near - arc.geo.ramp_gap
                        if o.m == -m then room = 0.5 * (ahead - arc.geo.ramp_gap) end
                        if room < limit then limit = math.max(0.0, room) end
                    end
                end
            end
            --  And no further than the ROOM beside the slab: the merge
            --  lane takes the tiles beside the highway, and where those
            --  carry a road, a line, a wire or a building it stops short
            --  of them.  Bare ground, trees and the highway's own tiles
            --  are room.
            local g = P and P.gore and P.n and P.gore[math.floor(spur.ty) * P.n + math.floor(spur.tx)]
            if g and not g.parent then g = nil end
            --  Where the lane LEAVES the slab, whether or not there is
            --  room for a merge lane: the side and the station of the
            --  tiles' top.  A lane without a merge lane leaves through
            --  the slab's edge there, and the slab's fascia opens for it
            --  (scripts/compose/strip.lua).
            if g then
                g.slab_side, g.slab_at, g.merge = side, me.i0, nil
                local j, bd = me.i0, 1e9
                for i = 0, d.n - 1 do
                    local dd = math.abs(st[i].at - s_top)
                    if dd < bd then bd, j = dd, i end
                end
                g.slab_top = j
            end
            if not g and arc.geo.pose_dump > 0.5 then
                local h = P and P.gore and P.n and P.gore[math.floor(spur.ty) * P.n + math.floor(spur.tx)]
                arc.dump(string.format("NOGORE pass %d %d,%d %s: c0 %.2f,%.2f len %d, gore %s, station at %.2f along",
                    arc.current_pass or 0, math.floor(spur.tx), math.floor(spur.ty), spur.leaves and "off" or "on",
                    spur.x, spur.y, spur.len, h and string.format("%.2f,%.2f side %d", h.x, h.y, h.side) or "none",
                    here.at))
            end
            if g then
                local hw   = arc.tune.band_w
                local sg   = side == 0 and 1.0 or -1.0
                local out  = 1.0 + arc.geo.ramp_lane_w
                local most = arc.geo.ramp_climb + L + T
                local at   = 0.25
                while at < most and at < limit do
                    local x, y = arc.slab_beside(g.parent, s_top + m * at, out, sg, hw)
                    local c, r = math.floor(x), math.floor(y)
                    local b = (c >= 0 and r >= 0 and c < P.n and r < P.n) and xbld[r * P.n + c] or 0
                    local free = b == 0 or (b >= 0x06 and b <= 0x0D) or arc.band_tiles[b] ~= nil
                    if not free then
                        limit = math.max(0.0, at - 0.25)
                        break
                    end
                    at = at + 0.25
                end
            end
            --  The ramp's lane holds the grade: where the tiles are too
            --  short for the height it has to climb, the lane runs on
            --  beside the slab past their top, climbing, and leaves the
            --  slab further along -- the gore -- so the merge lane begins
            --  there.
            --  The band end the lane runs toward, and how much slab
            --  there is before it: the climb, the lane and then the
            --  taper are all cut to that.  A top at the band's own end
            --  has none, and no merge lane.
            local end_at = m > 0 and total or 0.0
            local ahead  = math.max(0.0, (end_at - s_top) * m)
            local u = 0.0
            if g then
                local zs = arc.slab_z_at(g.parent, s_top)
                local zl = arc.city.at("surface", math.floor(spur.tx), math.floor(spur.ty)) or zs
                local grade = arc.geo.interchange_grade > 0 and arc.geo.interchange_grade or arc.tune.band_grade
                local need = math.abs(zs - zl) / grade - (spur.len + 1.0)
                u = math.max(0.0, math.min(arc.geo.ramp_climb, need, limit, ahead))
            end
            local s_g = s_top + m * u
            local room = ahead - u
            --  Whether that end meets a node: then the lane's far end
            --  keeps clear of it.
            local node   = be and (m > 0 and be.tail or be.head)
            local len, taper = L, T
            if node then
                local most = room - clear - T
                if most < len then len = math.max(least, most) end
            end
            if len + taper > room then
                taper = math.min(taper, room)
                len   = math.max(0.0, room - taper)
            end
            --  and short of the next ramp on this side, or of the room
            local left = limit - u
            if len + taper > left then
                taper = math.max(0.0, math.min(taper, left))
                len   = math.max(0.0, left - taper)
            end
            --  Under half a taper of room there is no merge lane to lay:
            --  the ramp keeps its own lane and the slab its edge.
            if g and len + taper < 0.5 * T then g = nil end
            local s_p = s_g + m * len
            local s_q = s_g + m * (len + taper)
            local short = node and math.abs(end_at - s_q) < clear - 1e-3
            if short then
                arc.log(string.format(
                    "ramp at %d,%d: its merge lane ends %.1f tiles from the interchange's arm, under the %.1f clear",
                    math.floor(spur.tx), math.floor(spur.ty), math.abs(end_at - s_q), clear))
            end

            --  The merge lane, for the ramp's chain and strip: only where
            --  the gore has its slab's stations to be laid from.  A ramp
            --  with none keeps its old lane, and the slab is not widened
            --  for it.
            if g then
                --  The slab's edge on the ramp's side: a lane out over
                --  the merge lane, running out to it over the taper.
                for i = 0, d.n - 1 do
                    local at = f32(st[i].at - s_g) * m
                    local w  = 1.0
                    if at >= 0.0 and at <= len then
                        w = wide
                    elseif at > len and at < len + taper then
                        w = f32(1.0 + f32(f32(wide - 1.0) * f32(1.0 - f32(at - len) / taper)))
                    end
                    local key = i * 2 + side
                    if w > (widths[key] or 1.0) then widths[key] = w end
                end
                do
                    --  The lane's two points on the outer lane, for the
                    --  ramp's chain and strip to be found by, on the
                    --  ramp's side of the stations: the tiles' top, where
                    --  the ramp's own lane stands, and the taper's far
                    --  end.
                    local hw, outer = arc.tune.band_w, arc.geo.slab_lane2
                    local sg = side == 0 and 1.0 or -1.0
                    local tx, ty = arc.slab_beside(g.parent, s_top, outer, sg, hw)
                    local qx, qy = arc.slab_beside(g.parent, s_q, outer, sg, hw)
                    g.merge = {s_g = s_g, s_top = s_top, climb = u, s_p = s_p, s_q = s_q, m = m, len = len, taper = taper,
                               side = side, sg = sg, short = short, band = arc.band_fitting,
                               top = {x = tx, y = ty}, q = {x = qx, y = qy}}
                    if arc.geo.pose_dump > 0.5 then
                        arc.dump(string.format(
                            "MERGE pass %d %d,%d %s: c0 %.2f,%.2f len %d, top %.2f, climbs %.2f to the gore at %.2f of %.2f, lane to %.2f, taper to %.2f, side %d, traffic %+d%s",
                            arc.current_pass or 0, math.floor(spur.tx), math.floor(spur.ty), spur.leaves and "off" or "on",
                            spur.x, spur.y, spur.len, s_top, u,
                            s_g, total, s_p, s_q, side, traffic, short and ", SHORT of the arm" or ""))
                    end
                end
            end
        end
    end
    --  THE FAN, at each end that meets a node, side by side: each lane
    --  of the slab ENDS its own distance short of the band's end, the
    --  inner lane at the end and each lane out one step of
    --  arc.geo.interchange_fan further back, on both sides alike.  A
    --  lane that ends is a LANE DROP: behind its end the slab's edge on
    --  that side TAPERS from where it stood to the next lane line in,
    --  over arc.geo.interchange_gore tiles toward the node and never
    --  past the next lane's end, and over the taper the slab's own quad
    --  still carries the lane, narrowing to nothing (scripts/compose/
    --  strip.lua reads arc.fan_lane_w, the edge of the lanes still on
    --  the slab, station by station).  The lane's way is a ribbon of
    --  its own from the taper's end, the NOSE, and level with the slab
    --  until then; every nose is published by band, end, side and lane
    --  (arc.fan_nose) for the node's ways (scripts/compose/links,
    --  scripts/compose/interchange).  A ramp within the fan on a
    --  side keeps the outer lane it leaves or joins: that side's fan
    --  stops at the ramp's own tiles and the far end of its merge lane,
    --  whichever is nearer the arm.  A narrowing wins over a merge
    --  lane's widening at the same station.  The lane model lays each
    --  lane as far as it is told here (dr:cut) and no further.
    arc.fan_lane_w = {}
    arc.fan_nose   = arc.fan_nose or {}
    local nose = {}
    local fan = arc.geo.interchange_fan
    if be and fan > 0.0 and d.n > 1 then
        local lanes = {arc.geo.slab_lane0, arc.geo.slab_lane1, arc.geo.slab_lane2}
        --  the slab's edge with lanes k and out gone: the inner edge of
        --  lane k's own ribbon, so the two meet with no sliver of ground
        --  between them (scripts/compose/interchange lays a way
        --  interchange_lane_w to each side of its lane's centre)
        local rib = arc.geo.interchange_lane_w / arc.tune.band_w
        local edge = {lanes[2] - rib, lanes[3] - rib}
        local gore  = math.min(arc.geo.interchange_gore, fan)
        local cut = {[0] = {}, [1] = {}}
        for side = 0, 1 do
            for k = 0, #lanes - 1 do cut[side][k] = {0.0, total} end
        end
        for ei, e in ipairs {"head", "tail"} do
            if be[e] then
                for side = 0, 1 do
                    local room = 0.5 * total
                    for _, me in ipairs(sp) do
                        if me.side == side then
                            local g = P.gore[math.floor(me.spur.ty) * P.n + math.floor(me.spur.tx)]
                            local far = g and g.merge and g.merge.s_q or me.s_top
                            for _, at in ipairs {me.s_top, far} do
                                local back = e == "tail" and (total - at) or at
                                if back < room then room = back end
                            end
                        end
                    end
                    --  each lane's end from the outside in, as a distance
                    --  back from the band's end: the station nearest its
                    --  pull, and never past the band's middle
                    local ck = {[0] = 0.0}
                    for k = #lanes - 1, 1, -1 do
                        local pull = math.min(k * fan, math.max(0.0, room))
                        local want = e == "tail" and total - pull or pull
                        local j, bd = 0, 1e9
                        for i = 0, d.n - 1 do
                            local dd = math.abs(st[i].at - want)
                            if dd < bd then bd, j = dd, i end
                        end
                        local s_cut = st[j].at
                        ck[k] = e == "tail" and (total - s_cut) or s_cut
                        if e == "tail" then cut[side][k][2] = math.min(cut[side][k][2], s_cut)
                        else cut[side][k][1] = math.max(cut[side][k][1], s_cut) end
                    end
                    --  the taper behind each: as long as the gore, or as
                    --  the run to the next lane's end where that is less
                    local g = {}
                    local at_e = (ei - 1) * 2 + side
                    for k = #lanes - 1, 1, -1 do
                        g[k] = math.max(0.0, math.min(gore, ck[k] - ck[k - 1]))
                        nose[at_e * 4 + k] = g[k]
                    end
                    nose[at_e * 4] = 0.0
                    --  the slab's edge at every station behind a lane's
                    --  end: the lanes still on it, and the exit lane
                    --  tapering in to them
                    for i = 0, d.n - 1 do
                        local back = e == "tail" and (total - st[i].at) or st[i].at
                        local lane_w, flare = 1.0, 1.0
                        for k = #lanes - 1, 1, -1 do
                            if back < ck[k] then
                                lane_w = edge[k]
                                local above = k == #lanes - 1 and 1.0 or edge[k + 1]
                                local t = g[k] > 1e-6 and (back - (ck[k] - g[k])) / g[k] or 0.0
                                if t < 0.0 then t = 0.0 elseif t > 1.0 then t = 1.0 end
                                local w = edge[k] + (above - edge[k]) * t
                                if w < flare then flare = w end
                            end
                        end
                        if lane_w < 1.0 then
                            local key = i * 2 + side
                            if flare < (widths[key] or 1.0) then widths[key] = flare end
                            arc.fan_lane_w[key] = lane_w
                        end
                    end
                    if arc.geo.pose_dump > 0.5 then
                        local c = cut[side]
                        local w = e == "tail" and 2 or 1
                        arc.dump(string.format("FAN pass %d band %d %s side %d: lanes end at %.2f, %.2f, %.2f of %.2f, room %.2f, tapers %.2f %.2f",
                            arc.current_pass or 0, arc.band_fitting or 0, e, side,
                            c[0][w], c[1][w], c[2][w], total, room, g[1] or 0, g[2] or 0))
                    end
                end
            end
        end
        for side = 0, 1 do
            for k = 0, #lanes - 1 do dr:cut(side, k, cut[side][k][1], cut[side][k][2]) end
        end
    end
    if arc.band_fitting then arc.fan_nose[arc.band_fitting] = nose end
    for key, w in pairs(widths) do dr:width(key // 2, key % 2, w) end
    return true
end
