--  strip.lua -- a strip's own surface, composed station by station.
--
--  The pipeline fits the centreline, grades it against the ground and
--  cuts it into stations.  Everything after that is here: how wide the
--  band is at each station, what the material reads across and along it,
--  what height it lies at, which slot of the painter's stack it takes,
--  what it is made of and where it is in a cut.
--
--  `s` is the strip itself -- `s.kind` is "strip" -- and every face it
--  lays is attributed to it.  Take this file away and the lines, the
--  railways and the slabs have no surface at all.

--  One station, interpolated between two, where a junction's meet
--  band cuts the pair that straddles its edge.  The two then meet along
--  the line rather than lapping by the rest of a quad.
--  Every step is taken in the mesh's own precision: a station worked out
--  to more places than the mesh can keep puts this quad's end a hair
--  from where the band beside it thinks the line is.
local f32 = arc.put.f32

local function mix(a, b, t)
    return f32(a + f32(f32(b - a) * t))
end

local function lerp(a, b, at)
    local d = f32(b.s - a.s)
    local t = d > 1e-6 and f32(f32(at - a.s) / d) or 0
    t = math.max(0, math.min(1, t))
    local o = {
        x = mix(a.x, b.x, t), y = mix(a.y, b.y, t), z = mix(a.z, b.z, t),
        dx = mix(a.dx, b.dx, t), dy = mix(a.dy, b.dy, t),
        s = at,
        wl = mix(a.wl, b.wl, t), wr = mix(a.wr, b.wr, t),
        xd = mix(a.xd, b.xd, t), zorig = a.zorig,
    }
    local l = f32(math.sqrt(f32(f32(o.dx * o.dx) + f32(o.dy * o.dy))))
    if l > 1e-6 then o.dx, o.dy = f32(o.dx / l), f32(o.dy / l) end
    return o
end

local function station(s, i)
    local x, y, z, dx, dy, at, wl, wr, xd, zorig = s:at(i)
    return {x = x, y = y, z = z, dx = dx, dy = dy, s = at,
            wl = wl, wr = wr, xd = xd, zorig = zorig}
end

--  OVER 150 LINES, and one page on purpose: it is ONE SWEEP along one
--  strip.  Each station is drawn from the station before it, so the
--  loop carries the whole of the strip's state.  Cut in two, both
--  halves would walk the same stations and the second would have to be
--  handed everything the first read.
arc.rules.strip = function (s)
    local d = s:info()
    local n = s:count()
    if n < 2 then return true end
    local fam = arc.rules.family({name = d.family, width = d.half * 2})
    --  A family that carries a margin keeps only the way here:
    --  the outer share of the band is the margin's own, laid from the
    --  network, and the two meet along that line rather than one being
    --  painted over the other.
    local inset = (d.lips and fam.margin) and fam.margin.inner or 1.0
    local slot = arc.geo[d.slot]
    local cut, dip = fam.strip.cut, fam.strip.dip
    local app = fam.approach

    --  A RAMP'S STRIP IS ONE LANE, painted as a lane of the slab on its
    --  own (the slab's material marks a lane told it is 0.70 to 0.94
    --  across, past every ordinary along: scripts/compose/deck.lua).
    --  And a lane drop is drawn from its gore outward only: over the
    --  merge lane and the taper the slab itself is the surface, a lane
    --  wider there (scripts/compose/drop.lua), and the strip's own quads
    --  would lie on it.  The slab end is the end nearer the slab.
    --  And where the lane runs BESIDE ITS SLAB, over the merge lane, the
    --  climb and as the descent draws away, the lane's own strip carries
    --  no body: its inner edge lies against the slab's, widened or not,
    --  and a girder there would share the slab's fascia plane.  Beside
    --  is measured from the slab's own stations, to the merge lane's
    --  outer edge.  A lane whose gore the pose store could not hand a
    --  slab's stations (scripts/compose/profile.lua) leaves its slab
    --  through the slab's edge, and carries no body at all.
    local skip_to, skip_from, slab_of
    if d.lane_piece then
        local a, b = station(s, 0), station(s, n - 1)
        local ga = arc.merge_of(a.x, a.y, "q", 0.5)
        local gb = not ga and arc.merge_of(b.x, b.y, "q", 0.5)
        local g  = ga or gb
        if g then
            local over = g.merge.len + g.merge.taper + 0.02
            if ga then skip_to = over else skip_from = f32(d.len - over) end
        end
        --  the slab it leaves, with or without a merge lane
        local h = g or arc.gore_of(a.x, a.y) or arc.gore_of(b.x, b.y)
        slab_of = h and h.parent
    end
    --  A slab's fascia OPENS where a lane leaves it through its edge: a
    --  ramp with no room beside the slab for a merge lane
    --  (scripts/compose/drop.lua) runs its lane off the outer lane and
    --  over the edge line somewhere along its own tiles, and a fascia
    --  there is a wall across it.  The gores of this slab say where, by
    --  side and by the stations the tiles span, half a tile more each way.
    local open = {}
    if d.slab and arc.poses and arc.poses.gore then
        local s0 = station(s, 0)
        for _, g in pairs(arc.poses.gore) do
            local pa = g.parent
            if pa and g.slab_top and g.slab_at and not g.merge
               and math.abs(pa.x[0] - s0.x) < 1e-4 and math.abs(pa.y[0] - s0.y) < 1e-4 then
                for i = math.min(g.slab_at, g.slab_top) - 8, math.max(g.slab_at, g.slab_top) + 8 do
                    open[i * 2 + g.slab_side] = true
                end
            end
        end
    end

    for i = 1, n - 1 do
        local pv, cu = station(s, i - 1), station(s, i)
        --  The ground's own line at the two STATIONS, which a pair cut
        --  short at a meet band still answers to: a station below it
        --  is in a cut whatever the quad was trimmed to.
        local zo0, zo1 = pv.zorig, cu.zorig
        --  What the junction's own meet covers is left to it: the
        --  band is laid square to the mouth, on the ground this strip
        --  graded, and the way starts exactly where it ends.
        --  Every step in the mesh's own precision, the comparisons with
        --  it: a band's edge worked out to more places than the mesh can
        --  keep cuts the pair at a hair from where the band itself was
        --  laid.
        local far = f32(d.len - d.cross1)
        if (d.cross0 > 0 and cu.s <= f32(d.cross0 + 1e-4)) or
           (d.cross1 > 0 and pv.s >= f32(far - 1e-4)) then
            goto next
        end
        if d.cross0 > 0 and pv.s < d.cross0 then pv = lerp(pv, cu, d.cross0) end
        if d.cross1 > 0 and cu.s > far then cu = lerp(pv, cu, far) end
        if (skip_to and cu.s <= skip_to) or (skip_from and pv.s >= skip_from) then
            goto next
        end

        do
            local ha = arc.band_half(d.half, s:width(pv.dx, pv.dy), inset)
            local hb = arc.band_half(d.half, s:width(cu.dx, cu.dy), inset)
            --  a0/b0 the right-hand edge (across negative), a1/b1 the
            --  left.  A slab's own quad runs to the edge of the LANES IT
            --  CARRIES at the station, so the material's across stays
            --  true to them: the full width, or the edge of the lanes
            --  still on it behind a lane drop at an interchange
            --  (scripts/compose/drop.lua, arc.fan_lane_w).  Whatever the
            --  slab is beyond that is a quad of its own beside it: a
            --  merge lane's widening painted as a ramp's lane, or the
            --  exit lane tapering to nothing, painted as the lane it is,
            --  its edge line converging on the lane line.
            local function lane_w(i0, side)
                local t = d.slab and arc.fan_lane_w
                return t and t[i0 * 2 + side] or 1.0
            end
            local pwr, pwl = math.min(pv.wr, lane_w(i - 1, 0)), math.min(pv.wl, lane_w(i - 1, 1))
            local cwr, cwl = math.min(cu.wr, lane_w(i, 0)), math.min(cu.wl, lane_w(i, 1))
            local a0x, a0y = arc.band_edge(pv.x, pv.y, pv.dx, pv.dy, ha, pwr, 1)
            local a1x, a1y = arc.band_edge(pv.x, pv.y, pv.dx, pv.dy, ha, pwl, -1)
            local b0x, b0y = arc.band_edge(cu.x, cu.y, cu.dx, cu.dy, hb, cwr, 1)
            local b1x, b1y = arc.band_edge(cu.x, cu.y, cu.dx, cu.dy, hb, cwl, -1)
            --  the across at each station: the slab's edge as a fraction
            --  of its full half width, so a quad between two widths keeps
            --  the material true to the lanes at both ends
            local acr, acl = arc.f32(-pwr * inset), arc.f32(pwl * inset)
            local acr_b, acl_b = arc.f32(-cwr * inset), arc.f32(cwl * inset)
            local mx = arc.f32(0.5 * arc.f32(pv.x + cu.x))
            local my = arc.f32(0.5 * arc.f32(pv.y + cu.y))
            local order = s:order(mx, my)
            local mat, ala, alb = d.mat, pv.s, cu.s
            local cls = 0
            if d.structure then
                acr, acl, acr_b, acl_b = 0.70, 0.94, 0.70, 0.94
                ala, alb = f32(ala + arc.slab.lane_along), f32(alb + arc.slab.lane_along)
            end

            --  A line's own pair: the class it carries, and its one
            --  marking -- the approach to a level meet, whose along
            --  is the distance to the meet.
            if d.family == "line" then
                cls = d.class >= 0 and d.class
                      or s:line_class(math.floor(mx), math.floor(my))
                if app and pv.xd > app.near and 0.5 * (pv.xd + cu.xd) < app.far then
                    mat, ala, alb = arc.mat.xapproach, pv.xd, cu.xd
                end
            end

            if d.slab then arc.slab.gore(s, d, i, pv, cu, ha, hb, order, ala, alb) end

            --  A slab narrowed on a side -- a lane ended short of the
            --  band's end (scripts/compose/drop.lua) -- says so in its
            --  quad's along, in the thousands the material reads as
            --  bits: 1 the right side narrowed to two lanes, 2 the left,
            --  8 the right side narrowed to one lane, 16 the left; the
            --  edge line is then drawn where the lane was.  A quad
            --  between two widths takes the narrower.  Whole thousands
            --  leave the dashes' phase alone.
            --  No edge line where the exit lane still tapers beside the
            --  quad: the lane line there is the exit lane's own.
            local qla, qlb = ala, alb
            local taper_r = pv.wr > pwr + 1e-4 or cu.wr > cwr + 1e-4
            local taper_l = pv.wl > pwl + 1e-4 or cu.wl > cwl + 1e-4
            if d.slab and not d.structure then
                local one = arc.geo.slab_lane1 - arc.geo.interchange_lane_w / arc.tune.band_w + 1e-3
                local code = 0
                local wr, wl = math.min(pwr, cwr), math.min(pwl, cwl)
                if wr < 0.9999 and not taper_r then code = code + (wr <= one and 8000 or 1000) end
                if wl < 0.9999 and not taper_l then code = code + (wl <= one and 16000 or 2000) end
                if code > 0 then qla, qlb = f32(ala + code), f32(alb + code) end
            end

            --  A quad in a cut, the line below the ground at a corner,
            --  is flagged in its class (4 and up) so the clipping check
            --  knows the ground standing over it is meant, held back by
            --  the walls below.
            local g0, g1 = s:ground(a0x, a0y), s:ground(a1x, a1y)
            local g2, g3 = s:ground(b0x, b0y), s:ground(b1x, b1y)
            local sunk = pv.z < f32(g0 - cut) or pv.z < f32(g1 - cut)
                      or cu.z < f32(g2 - cut) or cu.z < f32(g3 - cut)
                      or pv.z < f32(zo0 - dip) or cu.z < f32(zo1 - dip)
            s:class(sunk and cls + 4 or cls)

            --  One quad, the whole band: the margin pass and the
            --  marking pass draw these same vertices again under their
            --  own pass number, so a strip costs one quad however many
            --  passes paint it.  A band that stands on the ground is
            --  drawn ON it -- handed no height, every piece it is cut
            --  into takes the drawn surface at its own corners.  A slab
            --  carries its own height and keeps it.
            local qa = d.flies and pv.z or -1.0
            local qb = d.flies and cu.z or -1.0
            s:quad(a0x, a0y, a1x, a1y, b0x, b0y, b1x, b1y,
                   qa, qb, acr, acl, qla, qlb, mat, order + slot, acr_b, acl_b)
            s:class(cls)

            if d.slab then
                --  the lane beside the slab's own, where the slab is wider
                --  than the lanes it carries on a side, and the structure
                --  under the whole of it
                local e0x, e0y, e1x, e1y, f0x, f0y, f1x, f1y = a0x, a0y, a1x, a1y, b0x, b0y, b1x, b1y
                --  a merge lane, wider than the slab, is a ramp's lane;
                --  an exit lane, within the slab's width, is the slab's
                --  outer lane running out, in the slab's own paint
                if taper_r then
                    e0x, e0y = arc.band_edge(pv.x, pv.y, pv.dx, pv.dy, ha, pv.wr, 1)
                    f0x, f0y = arc.band_edge(cu.x, cu.y, cu.dx, cu.dy, hb, cu.wr, 1)
                    if pv.wr > 1.0001 or cu.wr > 1.0001 then
                        s:quad(e0x, e0y, a0x, a0y, f0x, f0y, b0x, b0y, qa, qb, 0.70, 0.94,
                               f32(ala + arc.slab.lane_along), f32(alb + arc.slab.lane_along), mat, order + slot)
                    else
                        s:quad(e0x, e0y, a0x, a0y, f0x, f0y, b0x, b0y, qa, qb, -0.94, -pwr,
                               ala, alb, mat, order + slot, -0.94, -cwr)
                    end
                end
                if taper_l then
                    e1x, e1y = arc.band_edge(pv.x, pv.y, pv.dx, pv.dy, ha, pv.wl, -1)
                    f1x, f1y = arc.band_edge(cu.x, cu.y, cu.dx, cu.dy, hb, cu.wl, -1)
                    if pv.wl > 1.0001 or cu.wl > 1.0001 then
                        s:quad(a1x, a1y, e1x, e1y, b1x, b1y, f1x, f1y, qa, qb, 0.94, 0.70,
                               f32(ala + arc.slab.lane_along), f32(alb + arc.slab.lane_along), mat, order + slot)
                    else
                        s:quad(a1x, a1y, e1x, e1y, b1x, b1y, f1x, f1y, qa, qb, pwl, 0.94,
                               ala, alb, mat, order + slot, cwl, 0.94)
                    end
                end
                local bare = d.lane_piece and (not slab_of or arc.slab_away(slab_of, mx, my)
                                         < arc.tune.band_w * (1.0 + arc.geo.ramp_lane_w) + d.half + 0.05)
                if not bare then
                    arc.slab.under(s, d, i, n, pv, cu, e0x, e0y, e1x, e1y, f0x, f0y, f1x, f1y, order,
                                   open[(i - 1) * 2] or open[i * 2], open[(i - 1) * 2 + 1] or open[i * 2 + 1])
                end
            end
        end
        ::next::
    end
    return true
end
