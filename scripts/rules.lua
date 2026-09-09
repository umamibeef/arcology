--  rules.lua -- the decisions the road works are drawn by.
--
--  These are the judgements, as opposed to the shapes in models.lua and
--  the numbers in arc.geo: which of a junction's arms are controlled and
--  how, and whether one of its mouths carries a crosswalk.
--
--  They are the ONLY copy.  There is no ladder in C behind them to fall
--  back on: take a rule out and no junction is controlled, take the
--  folder away and the city has no signals, no stop signs, no crosswalks
--  and no street furniture at all.  This file drives the game.

--  A junction's control, one code an arm: 0 none, 1 stop, 2 signal
--  (spec 3.4).  `arms[e + 1]` is {class = , traffic = } for an arm that
--  is there and nil for one that is not; e is 0 north, 1 east, 2 south,
--  3 west, and the class is 0 a road, 1 an avenue, 2 a boulevard.
--
--  The ladder, from the classes meeting there: anything against a
--  boulevard gets a signal; avenue against avenue an all-way stop, or a
--  signal where the junction is busy; a local against an avenue puts the
--  stop on the local legs; three legs with a local stem, a stop on the
--  stem; four local legs, a two-way stop on the quieter axis.
arc.rules.control = function (col, row, arms, busy)
    local ctrl = {0, 0, 0, 0}
    local n, hi, lo = 0, 0, 9
    for e = 1, 4 do
        if arms[e] then
            n = n + 1
            if arms[e].class > hi then hi = arms[e].class end
            if arms[e].class < lo then lo = arms[e].class end
        end
    end
    if hi >= 2 then
        for e = 1, 4 do if arms[e] then ctrl[e] = 2 end end
    elseif hi == 1 and lo == 1 then
        for e = 1, 4 do if arms[e] then ctrl[e] = busy and 2 or 1 end end
    elseif hi == 1 then
        for e = 1, 4 do if arms[e] and arms[e].class == 0 then ctrl[e] = 1 end end
    elseif n == 3 then
        --  the stem: the arm whose opposite is missing
        for e = 1, 4 do
            local opp = (e + 1) % 4 + 1
            if arms[e] and not arms[opp] then ctrl[e] = 1 end
        end
    else
        --  four local legs: the stop goes on the axis carrying less
        local ns = math.max(arms[1] and arms[1].traffic or 0, arms[3] and arms[3].traffic or 0)
        local ew = math.max(arms[2] and arms[2].traffic or 0, arms[4] and arms[4].traffic or 0)
        for e = 1, 4 do
            local quiet = (e == 1 or e == 3) and ns <= ew or (e == 2 or e == 4) and ew < ns
            if arms[e] and quiet then ctrl[e] = 1 end
        end
    end
    return ctrl
end

--  Whether one arm's mouth carries a crosswalk, and how deep a band it
--  asks for.  A crosswalk runs between two pavements, so the mouth needs
--  a controlled arm and a footway on each side of it, those two must
--  face one another across the road -- `cos` is how their headings run
--  against each other, and -1 is squarely opposed -- and the mouth must
--  be wider than `span` footway widths, since a band shorter than that
--  is a mark on the kerb rather than a way across.  It answers the depth
--  the band wants before the road it has to give up is known; how much
--  of that it gets is arc.rules.crossing's.
arc.rules.crossing_at = function (m)
    if m.control == 0 then return 0 end
    if not m.pavement then return 0 end
    if m.cos > -arc.geo.cross_par then return 0 end
    if m.span <= arc.geo.walk_narrow then return 0 end
    return arc.geo.cross_deep
end

--  How deep a crossing's band runs at one arm's mouth, in tiles, or 0
--  for none.  `m` carries the junction's own reading of the mouth:
--  `want`, the band a crosswalk asks for; `room`, the road there is to
--  give up; and `straight`, how much of that road runs straight from the
--  mouth.  A crosswalk is painted between two parallel pavements, so it
--  reaches only as far as the road stays straight, and two of them leave
--  a road between the junctions they belong to.  A band too shallow to
--  read as a crossing is not marked at all.
arc.rules.crossing = function (m)
    if m.control == 0 then return 0 end
    local d = math.min(m.want, m.straight, 0.35 * m.room)
    if d < 0.5 * arc.geo.cross_deep then return 0 end
    return d
end

--  Where a family's lanes run, as distances from the centreline, inner
--  first: a road's one each way at half the carriageway, an avenue's and
--  a boulevard's two, a railway's single track to the right of travel.
--  At most two each way, which is what the pipeline has room for.
arc.rules.lanes = function (fam, cls)
    if fam == "rail" then return {arc.geo.rail_lane} end
    if cls == 1 then return {arc.geo.lane_av_in * arc.tune.road_w,
                             arc.geo.lane_av_out * arc.tune.road_w} end
    if cls == 2 then return {arc.geo.lane_bd_in * arc.tune.road_w,
                             arc.geo.lane_bd_out * arc.tune.road_w} end
    return {arc.geo.lane_road * arc.tune.road_w}
end

--  A power line's tile: a pylon at the middle with a wire out to each
--  edge it joins, where the neighbour's wire meets it.  `at.links` is a
--  bit an edge, 0 north, 1 east, 2 south, 3 west.
local MU = {0.5, 1.0, 0.5, 0.0}   -- the middle of each edge
local MV = {0.0, 0.5, 1.0, 0.5}
arc.rules.power_tile = function (at)
    --  A line running only north and south turns its crossarm the other
    --  way; the wires' heights are over the ground, which the wire
    --  itself adds.
    local ns = (at.links & 5) == 5 and (at.links & 10) == 0
    arc.put.model("pylon", at.x, at.y, ns and 0 or 1, ns and 1 or 0)
    for e = 0, 3 do
        if at.links & (1 << e) ~= 0 then
            arc.put.wire(at.x, at.y, arc.geo.wire_hang,
                         at.col + MU[e + 1], at.row + MV[e + 1], arc.geo.wire_hang,
                         arc.geo.wire_sag)
        end
    end
    return true
end

--  A level crossing's gate on one approach (spec 3.15): the flashers,
--  lit while the arm is off its rest, and the striped arm itself,
--  swung about the mechanism's shaft beside the mast.  `at.angle` is
--  where the traffic has swung it -- 0 down across the lane, 88 up --
--  and `at.len` how far it reaches; `at.fx, at.fy` face the driver it
--  stops.  The arm rises as it lifts, so its tip is higher than its
--  pivot by the sine of the angle.
arc.rules.gate_arm = function (at)
    local wu, wv = -at.fy, at.fx
    local px, py = at.x + wu * arc.geo.gate_shaft, at.y + wv * arc.geo.gate_shaft
    local ca, sa = math.cos(math.rad(at.angle)), math.sin(math.rad(at.angle))
    local pz = at.z + arc.geo.gate_pivot
    arc.put.model(at.angle < arc.geo.gate_up - 0.5 and "gate_lamps_lit" or "gate_lamps_dark",
                  at.x, at.y, at.fx, at.fy, 0, at.phase, 0, at.z)
    arc.put.bar(px, py, pz,
                px + wu * at.len * ca, py + wv * at.len * ca,
                pz + at.len * sa * arc.geo.gate_rise,
                at.fx, at.fy, arc.geo.gate_arm_w, arc.geo.gate_arm_d,
                arc.mat.lamp, 11)
    return true
end

--  One corner of a junction's outline: what the boundary does where two
--  arms meet, turning through `at.phi` radians.  A table rounds the
--  corner off with a kerb return, false leaves it square, and nothing
--  runs the boundary straight past it.
--
--  The TANGENT DISTANCE is the constant -- a driver leaves the kerb that
--  far before the corner whatever the angle -- and the radius follows
--  from it, so a shallow corner, which sticks out further and turns
--  through less, comes out generously round instead of a point.  Holding
--  the radius constant instead gives a 135 degree corner an arc a tenth
--  of a tile long, which reads as no corner at all.
--
--  The rule is asked twice about the same corner.  The first time only
--  the angle is known and the answer says how much room to hold back
--  along each arm; the second time `room` says how much room the mouths
--  left, and `back` and `fwd` how far the outline's neighbouring points
--  are, so a corner that would double the ring back on itself is
--  dropped instead.
arc.rules.corner = function (at)
    if at.phi > arc.geo.junc_sharp and at.phi < arc.geo.junc_flat then
        local t = arc.geo.junc_curb * at.grow
        if at.room then
            t = math.min(t, at.room)
            t = math.max(t, arc.geo.junc_tan)
        end
        return {tangent = t, radius = t * math.tan(0.5 * at.phi), steps = arc.geo.junc_arc}
    end
    --  Two arms leaving in opposite directions have parallel edges and no
    --  corner between them, and a point nearer to the mouth on either
    --  side than the footway is wide is not one either: the spur's own
    --  inward side points OUT of the junction, so the asphalt laid inside
    --  the footway covers the band it was meant to stop at.
    if at.phi >= arc.geo.junc_flat then return nil end
    local near = at.width * arc.geo.junc_near
    if at.back and (at.back <= near or at.fwd <= near) then return nil end
    return false
end

--  Where the lamps stand along one strip, in tiles from its start: on an
--  avenue or a boulevard a cobra-head luminaire every lamp_every,
--  staggered side to side so each lights the far kerb, none in the first
--  or last tile where a junction or an end takes the room.  A local road
--  goes unlit -- the spec's post-tops are a downtown's -- and so is a
--  strip too short to carry a pair.
arc.rules.lamps = function (cls, len)
    local out = {}
    if cls < 0.5 or len <= arc.geo.lamp_min then return out end
    local side = 1
    local at = arc.geo.lamp_first
    while at < len - 1.0 do
        out[#out + 1] = {at = at, side = side, ["in"] = arc.geo.lamp_in}
        at, side = at + arc.geo.lamp_every, -side
    end
    return out
end

--  What stands beside a railway (spec 5.6), right-hand running: a block
--  signal every rsig_every along each track on its own outer side facing
--  back down it, an absolute one a tile before the junction that track
--  runs toward, and a whistle post two tiles before every level crossing
--  each way.  A signal wants its tile clear of a crossing, which has its
--  own protection; a whistle post is placed against one on purpose.
--
--  `m.len` is how long the strip runs, `m.ahead` and `m.behind` whether
--  it ends at a junction each way, and `m.crossings` where along it the
--  roads cross.  Side 1 is the track to the right of the walk, which
--  carries traffic forward.
arc.rules.rail_marks = function (m)
    local out = {}
    for side = 1, -1, -2 do
        local at = arc.geo.rsig_first
        while at < m.len - 0.5 do
            out[#out + 1] = {at = at, side = side, out = arc.geo.rsig_out,
                             model = "rail_signal", signal = 0, clear = true}
            at = at + arc.geo.rsig_every
        end
        if (side == 1 and m.ahead or side == -1 and m.behind) and m.len > 2.5 then
            out[#out + 1] = {at = side == 1 and m.len - 1.0 or 1.0, side = side,
                             out = arc.geo.rsig_out, model = "rail_signal",
                             signal = 1, clear = true}
        end
    end
    for _, s in ipairs(m.crossings) do
        for side = 1, -1, -2 do
            local at = s - side * 2.0
            if at >= arc.geo.rmark_end and at <= m.len - arc.geo.rmark_end then
                out[#out + 1] = {at = at, side = side, out = arc.geo.rmark_out,
                                 model = "milepost", face = "map"}
            end
        end
    end
    return out
end

--  A level crossing's gate, once a frame (spec 3.15): the arm falls
--  while a train stands within gate_warn tiles of the crossing on either
--  track -- about three seconds at the speed a train runs -- and rises
--  again once the approach is clear.  `g.angle` is where the arm is now,
--  0 flat across the road and gate_up its rest; `g.near` how far along
--  the rail's axis the nearest train car is.
arc.rules.gate = function (g)
    local rate = g.near < arc.geo.gate_warn and -arc.geo.gate_rate or arc.geo.gate_raise
    local a = g.angle + rate * g.dt
    return math.max(0, math.min(arc.geo.gate_up, a))
end

--  How big a level crossing is (spec 3.15).  Every measurement follows
--  from the angle the road and the railway cross at: the track bed
--  reaches along the road by its own half width over the sine of that
--  angle, held to xing_reach where the two cross so obliquely that the
--  quotient runs away, and the masts stand a road's half width out plus
--  their own clearance.  The panel is the surface that replaces the
--  asphalt, so it carries its own lift and its own place in the stack.
arc.rules.crossing_frame = function (x)
    local sn = math.max(x.sin, arc.geo.xing_skew)
    local bed = x.rail * 0.5 + arc.geo.xing_pad
    return {reach = math.min(bed / sn, arc.geo.xing_reach),
            mast  = x.road * 0.5 + arc.geo.xing_mast,
            bed   = bed,
            lift  = arc.geo.lift_xpanel,
            slot  = arc.geo.slot_xpanel}
end

--  What one road approach to a level crossing carries, in the order it
--  is laid: the gate's mast at the driver's right, the stop line
--  xing_stop_set before the panel, and a second-train sign facing each
--  footway.  `out` is along the road from the crossing's middle and
--  `across` from its centreline, the driver's right being positive.
--
--  The stop line is held inside the road's own end -- `limit` is how far
--  the road runs before a junction owns the surface -- and left off
--  entirely, by asking for no span at all, where the road left is too
--  short to read as a line before the crossing rather than a mark in a
--  junction.
arc.rules.crossing_marks = function (x)
    --  The stop line is laid here rather than described: it is a bar
    --  across the approach lane, and the approach's own frame -- the
    --  middle of the panel, the way the driver faces, the driver's right
    --  -- is all it needs.  Held inside the road's own end, and left off
    --  where the road left is too short to read as a line before the
    --  crossing rather than a mark in a junction.
    local stop = x.reach + arc.geo.xing_stop_set
    if stop + arc.geo.xing_stop_pen > x.limit then
        stop = x.limit - arc.geo.xing_stop_pen
    end
    if stop >= x.reach + arc.geo.xing_stop_min then
        local deep, span = arc.geo.xing_stop_deep, x.road * 0.5 * arc.geo.xing_stop_lane
        local sx, sy = x.x - x.fx * (stop + deep), x.y - x.fy * (stop + deep)
        local tx, ty = x.x - x.fx * (stop - deep), x.y - x.fy * (stop - deep)
        arc.put.quad(sx, sy, sx + x.gx * span, sy + x.gy * span,
                     tx, ty, tx + x.gx * span, ty + x.gy * span,
                     -1, -1, 0.0, 0.79, 0.17, 0.17,
                     arc.mat.zebra, arc.geo.slot_xstop)
    end
    local marks = {{model = "gate", out = x.reach, across = x.mast}}
    for side = -1, 1, 2 do
        marks[#marks + 1] = {model = "second_train",
                             out = x.reach + arc.geo.xing_sign_out,
                             across = side * (x.road * 0.5 - arc.geo.xing_sign_in)}
    end
    return marks
end

--  What is true of every strip and every junction of one family, asked
--  once for each and used all through the build.
--
--  `footway` divides the band across the carriageway's half width from
--  the centreline outward: where the pavement starts, where its outer
--  edge runs, how wide it is round a junction, and how nearly two
--  pavements must face one another across the road for a crosswalk to
--  run between them -- a cosine, 1 being squarely opposed.  `look` is
--  how far past its end a footway looks for what it joins and `mouth`
--  the slack it is allowed at a junction's, and the three slots are
--  where its bands sit in the painter's stack.  A family that answers no
--  footway has none: the asphalt is laid on its outline and no crosswalk
--  is marked anywhere on it.
--
--  `junction` is the box every junction is built in: `inset` how far
--  inside the tile its surface is read, `far` how far out its outline
--  may reach.  A junction of a diagonal road wants the room, and the
--  widest corner any shipped city asks for is under 0.8 of a tile.
--
--  `track` is a railway's own: the gauge between two tracks through a
--  junction, and where a through line runs, the second of two lying
--  over the first as a diamond.
--
--  `approach` is the band a road fades over on its way into a level
--  crossing, from `near` of it to `far`.
--
--  `strip` is the ribbon itself: how finely it is cut along its length,
--  how far its surface stands over the ground it was graded into, how
--  far under that ground a station has to fall to count as a cut, and
--  the marks the curve overlay draws it with.
--
--  `lane` is the traffic's own lines over it: how tight a connector may
--  turn, how finely one is cut, how it is drawn, when two of them join,
--  how near the map's edge one may run, and where a deck's three lanes
--  sit across it.
arc.rules.family = function (f)
    local junction = {inset = arc.geo.junc_inset, far = arc.geo.junc_far}
    local strip = {
        step_run  = arc.geo.loft_step_run,
        step_arc  = arc.geo.loft_step_arc,
        lift      = arc.geo.loft_lift,
        lift_min  = arc.geo.loft_lift_min,
        cut       = arc.geo.loft_cut,
        dip       = arc.geo.loft_dip,
        mark_wide = arc.geo.node_wide,
        mark_lift = arc.geo.node_lift,
        mark_high = arc.geo.node_high,
        mark_slot = arc.geo.slot_wire,
        line_wide = arc.geo.wire_wide,
    }
    local lane = {
        rmin      = arc.geo.lane_rmin,
        step_run  = arc.geo.lane_step_run,
        step_arc  = arc.geo.lane_step_arc,
        step_ramp = arc.geo.lane_step_ramp,
        slot      = arc.geo.slot_lane,
        wire      = arc.geo.lane_wire,
        lift      = arc.geo.lane_lift,
        join      = arc.geo.lane_join,
        aim       = arc.geo.lane_aim,
        edge      = arc.geo.lane_edge,
        reach     = arc.geo.lane_reach,
        --  Carrying on into the facing lane across a crossing.
        crossing_centre = arc.geo.crossing_centre,
        arm_own         = arc.geo.arm_own,
        arm_base        = arc.geo.arm_base,
        shelf_along     = arc.geo.shelf_along,
        shelf_reach     = arc.geo.shelf_reach,
        shelf_batter    = arc.geo.shelf_batter,
        class_avenue    = arc.geo.class_avenue,
        class_boulevard = arc.geo.class_boulevard,
        tile_inset  = arc.geo.tile_inset,
        cap_kerb    = arc.geo.cap_kerb,
        cross_share = arc.geo.cross_share,
        pick_dot    = arc.geo.lane_pick_dot,
        cross_reach = arc.geo.lane_cross_reach,
        cross_ahead = arc.geo.lane_cross_ahead,
        cross_aside = arc.geo.lane_cross_aside,
        cross_dot   = arc.geo.lane_cross_dot,
        cross_spot  = arc.geo.lane_cross_spot,
        cross_off   = arc.geo.lane_cross_off,
        deck      = {arc.geo.deck_lane0, arc.geo.deck_lane1, arc.geo.deck_lane2},
        --  A ramp: where the deck's outer lane sits, how near a recorded
        --  lane must be to snap to, the angle a through road is met at,
        --  and where the road's own lane and the merge point sit.
        ramp_outer       = arc.geo.ramp_outer,
        ramp_snap        = arc.geo.ramp_snap,
        ramp_meet_cos    = arc.geo.ramp_meet_cos,
        ramp_meet_sin    = arc.geo.ramp_meet_sin,
        ramp_lane_off    = arc.geo.ramp_lane_off,
        ramp_merge_along = arc.geo.ramp_merge_along,
        ramp_taper       = arc.geo.ramp_taper,
        --  A deck lane carrying on into the next band's.
        band_reach  = arc.geo.band_reach,
        band_off    = arc.geo.band_off,
        band_dot    = arc.geo.band_dot,
        band_ahead  = arc.geo.band_ahead,
        band_aside  = arc.geo.band_aside,
        band_apart  = arc.geo.band_apart,
        --  And the taper from its middle and outer lanes into the inner.
        band_abreast    = arc.geo.band_abreast,
        band_outer      = arc.geo.band_outer,
        band_taper_far  = arc.geo.band_taper_far,
        band_taper_near = arc.geo.band_taper_near,
        band_taper_gap  = arc.geo.band_taper_gap,
        band_taper_room = arc.geo.band_taper_room,
        band_road_dot   = arc.geo.band_road_dot,
    }
    if f.name == "rail" then
        return {junction = junction, strip = strip, lane = lane,
                track = {gauge   = arc.geo.rail_gauge,
                         through = arc.geo.rail_thru,
                         second  = arc.geo.rail_thru2}}
    end
    if f.name ~= "road" then
        return {junction = junction, strip = strip, lane = lane}
    end
    return {
        junction = junction,
        strip    = strip,
        lane     = lane,
        approach = {near = arc.geo.xapp_near, far = arc.geo.xapp_far},
        footway  = {inner         = arc.geo.walk_inner,
                    edge          = arc.geo.walk_edge,
                    at_junction   = arc.geo.junc_walk,
                    parallel      = arc.geo.cross_par,
                    look          = arc.geo.walk_look,
                    mouth         = arc.geo.walk_mouth,
                    slot_strip    = arc.geo.slot_walk_strip,
                    slot_junction = arc.geo.slot_walk_junction,
                    slot_cross    = arc.geo.slot_crosswalk},
    }
end

--  A power line crossing a road or a railway: no pole on the tile, since
--  the road owns the ground, and the wire spanning it edge to edge where
--  the neighbours' wires meet it.
arc.rules.power_crossing = function (at)
    if (at.links & 5) == 5 then
        arc.put.wire(at.x, at.row, arc.geo.wire_hang,
                     at.x, at.row + 1, arc.geo.wire_hang, arc.geo.wire_span_sag)
    else
        arc.put.wire(at.col, at.y, arc.geo.wire_hang,
                     at.col + 1, at.y, arc.geo.wire_hang, arc.geo.wire_span_sag)
    end
    return true
end

--  How the world that moves behaves: the trains, the cars that follow
--  one another, the gates they wait at and the signals that blink.  None
--  of it varies from one thing to the next, so it is asked once and the
--  frame loop reads the answer -- a car costs no call of its own.  With
--  no rule nothing moves at all.
arc.rules.traffic = function ()
    return {
        blink        = arc.geo.blink_duty,
        train_speed  = arc.geo.train_speed,
        train_spread = arc.geo.train_spread,
        train_len    = arc.geo.train_len,
        trail_step   = arc.geo.trail_step,
        xing_find    = arc.geo.xing_find,
        gate_up      = arc.geo.gate_up,
        gate_watch   = arc.geo.gate_watch,
        density      = arc.geo.car_density,
        car_len      = arc.geo.car_len,
        gap_stop     = arc.geo.car_gap_stop,
        gap_free     = arc.geo.car_gap_free,
        stop_junc    = arc.geo.car_stop_junc,
        stop_hold    = arc.geo.car_stop_hold,
        creep        = arc.geo.car_creep,
        probe        = arc.geo.car_probe,
        step_max     = arc.geo.step_max,
        slot         = arc.geo.slot_furn,
        block_back   = arc.geo.rail_block_back,
        block_ahead  = arc.geo.rail_block_ahead,
    }
end

--  What to try where two of a fitted path's straight lines meet, and in
--  what order.  Lines that cross take one arc at the crossing; lines
--  that run parallel take a biarc between them.  Two FREE lines -- ones
--  a corridor let be straight wherever they liked -- that cross a long
--  way off are very nearly parallel, so when the arc is refused they
--  take the biarc too, since that is what they are.  Only when nothing
--  holds is the join walked tile by tile, which always works and always
--  looks like it.
arc.rules.join = function (j)
    if not j.cross then return {"biarc", "walk"} end
    if j.free then return {"arc", "biarc", "walk"} end
    return {"arc", "walk"}
end

--  What lies after a line, for the budget the far end of a join is given.
--
--  Normally it is where that line crosses the line after it -- the next
--  corner, which will want its own share of the edge between them.
--
--  A free line is different.  Its crossing with the next may lie beyond
--  its own end, or behind its start, when the two are nearly parallel,
--  and a budget measured to a point the line never reaches is no budget.
--  Then its own far end is what comes after.
arc.rules.after = function (a)
    if not a.met then return "end" end
    if not a.free then return "crossing" end
    if a.ahead > a.reach or a.ahead < 0.5 then return "end" end
    return "crossing"
end

--  Which of a segment's two fits to keep.
--
--  Every segment is fitted twice: once with a corridor that lets its runs
--  leave its own cells -- a railway sweeping across the field, a deck as
--  straight as the ground allows -- and once held to them.
--
--  Fewer hard corners wins first, because a corner with no arc at all is
--  the worst thing a line can have.  Then fewer arcs under the minimum
--  radius.  Then the straighter of two equals, by vertex count.  A tie
--  goes to the free fit, so no segment comes out worse than it would
--  have without free lines at all.
--
--  One rule for every family, because the question is the same one.
arc.rules.fit_choice = function (f)
    if f.free.corners ~= f.held.corners then
        return f.free.corners < f.held.corners and "free" or "held"
    end
    if f.free.tight ~= f.held.tight then
        return f.free.tight < f.held.tight and "free" or "held"
    end
    return f.free.nodes <= f.held.nodes and "free" or "held"
end

--  How a ramp's foot meets the road it lands on.
--
--  Three things are known about the road tile the ramp comes down beside:
--  whether a road carries straight on through its far side, and whether
--  one runs each way across it.
--
--  Straight on wins outright: the ramp's lane runs through without a
--  turn, whatever else also joins there.  Otherwise a road running BOTH
--  ways across is a through road the ramp forks onto, one way only is a
--  stub it ends on, and neither is no junction at all.
arc.rules.ramp_fork = function (r)
    if r.straight then return 3 end
    if r.along and r.against then return 2 end
    if r.along or r.against then return 1 end
    return 0
end

--  Which way an on-ramp's taper lies along the deck.
--
--  A ramp has no direction of its own in the data, so the taper goes on
--  the side away from its road: on the road's side the strip would run
--  over the road.  Where a road lies opposite the deck, or none does, or
--  one lies each way, both sides are open -- and then the longer taper
--  wins, because a taper with room to run reads as a ramp and a stubby
--  one does not.
arc.rules.ramp_side = function (r)
    if not r.free then return true end
    return r.room >= r.room_back
end

--  Two ramps on one side of a deck whose tapers FACE each other -- each
--  with its road on its far side -- would run their strips into one
--  another.  They share the tiles between them: each taper takes half,
--  so the lane rises from one, runs as the deck's outer lane, and comes
--  down to the other, which reads as one ramp with a merge lane into the
--  highway.
--
--  Two ramps further apart than a pair of full tapers are not a pair at
--  all and are left alone.
arc.rules.ramp_share = function (s)
    if s.gap > 2 * s.cap then return nil end
    return math.floor(s.gap * 0.5)
end

--  One class for a whole segment, from how many of its tiles read as
--  each: the MEDIAN of them.
--
--  Taken per tile instead, an avenue's centre line starts and stops
--  mid-block wherever the traffic count crosses a threshold, which is a
--  property of the simulation's own smoothing and not of the road.  The
--  median gives the whole segment one answer and holds it.
arc.rules.seg_class = function (n)
    local half = (n[1] + n[2] + n[3] + 1) // 2
    local acc  = 0
    for cls = 1, 2 do
        acc = acc + n[cls]
        if acc >= half then return cls - 1 end
    end
    return 2
end

--  Where a ramp's descent runs along the deck.
--
--  It reaches from the GORE -- the far end of the taper, where the deck
--  has given up its outer lane and the ramp's own sliver begins -- down
--  to the ramp tile's road-side edge, which is where the lane lands on
--  the road.  The taper's length is counted in deck tiles, so the gore
--  sits half a tile plus the rest of the taper from the ramp's own
--  station.
--
--  Which way along the band that is depends on both the ramp and the
--  band: an OFF ramp's taper runs against the band's own direction and
--  an ON ramp's with it.
--
--  However short, the ramp is a strip, so it is never given no length at
--  all -- a zero-length strip has no direction and nothing to loft.
local f32 = arc.put.f32

local HALF = 0.5
local LEAST = 0.4

arc.rules.ramp_span = function (r)
    local ds   = r.leaves and -r.sgn or r.sgn
    local reach = f32(HALF + (r.len > 0 and r.len - 1 or 0))
    local top  = f32(r.at + f32(ds * reach))
    local foot = f32(r.at - f32(ds * HALF))
    local total = math.abs(f32(top - foot))
    if total < LEAST then total = LEAST end
    return {top = top, foot = foot, total = total, along = ds}
end

--  Where a highway band's walk begins, and which way it runs.
--
--  A band is walked from an END, away from it.  So a cell with nothing
--  behind it is walked forward, and a cell with nothing ahead is walked
--  backward.
--
--  A curve block counts as the band carrying on: it means the band turns
--  a corner there into another, and the walk has to ARRIVE through the
--  block.  Started beside a block and walking away from it, the block's
--  four tiles belong to nobody and nothing renders there.
--
--  A cell with band both ways is no end at all.  Those are left to the
--  sweep that walks a band with no end -- one between two blocks, or a
--  loop -- both ways from wherever it is found.
arc.rules.band_start = function (b)
    if not b.back then return "forward" end
    if not b.on then return "backward" end
    return nil
end

--  How fast a car may go for the car ahead of it.
--
--  Inside the stopping distance it stops dead; beyond the free distance
--  it ignores the car ahead entirely; between the two it takes the share
--  of its own speed that the gap has closed.  Linear, which is not how a
--  driver behaves but is what reads correctly at this scale -- a queue
--  that packs tight at a light and pulls away in order.
arc.rules.car_follow = function (c)
    if c.gap < c.stop then return 0.0 end
    if c.gap >= c.free then return c.speed end
    return c.speed * (c.gap - c.stop) / (c.free - c.stop)
end

--  How fast a car may go for what holds it ahead: a junction's signal or
--  stop sign, or a level crossing's gates.
--
--  It stops with its front on the LINE -- short of the thing itself, not
--  on top of it.  Past the line it is already committed and stops dead.
--  Approaching it, a car whose next step would carry it over the line
--  gets exactly the speed that reaches the line instead, so it arrives
--  stopped rather than overshooting and jerking back.
--
--  Nothing holding it means nothing to slow for.
arc.rules.car_hold = function (c)
    if not c.held then return c.speed end
    if c.ahead <= c.line then return 0.0 end
    if c.speed * c.step > c.ahead - c.line then return (c.ahead - c.line) / c.step end
    return c.speed
end
