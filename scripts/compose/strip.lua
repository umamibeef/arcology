--  strip.lua -- a strip's own surface, composed station by station.
--
--  The pipeline fits the centreline, grades it against the ground and
--  cuts it into stations.  Everything after that is here: how wide the
--  band is at each station, what the material reads across and along it,
--  what height it lies at, which slot of the painter's stack it takes,
--  what it is made of and where it is in a cut.
--
--  `s` is the strip itself -- `s.kind` is "strip" -- and every face it
--  lays is attributed to it.  Take this file away and the roads, the
--  railways and the decks have no surface at all.

--  One station, interpolated between two, where a junction's crossing
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

arc.rules.strip = function (s)
    local d = s:info()
    local n = s:count()
    if n < 2 then return true end
    local fam = arc.rules.family({name = d.family, width = d.half * 2})
    --  A family that carries a footway keeps only the carriageway here:
    --  the outer share of the band is the footway's own, laid from the
    --  network, and the two meet along that line rather than one being
    --  painted over the other.
    local inset = (d.curbs and fam.footway) and fam.footway.inner or 1.0
    local slot = arc.geo[d.slot]
    local cut, dip = fam.strip.cut, fam.strip.dip
    local app = fam.approach

    for i = 1, n - 1 do
        local pv, cu = station(s, i - 1), station(s, i)
        --  The ground's own line at the two STATIONS, which a pair cut
        --  short at a crossing band still answers to: a station below it
        --  is in a cut whatever the quad was trimmed to.
        local zo0, zo1 = pv.zorig, cu.zorig
        --  What the junction's own crossing covers is left to it: the
        --  band is laid square to the mouth, on the ground this strip
        --  graded, and the carriageway starts exactly where it ends.
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

        do
            local ha = arc.band_half(d.half, s:width(pv.dx, pv.dy), inset)
            local hb = arc.band_half(d.half, s:width(cu.dx, cu.dy), inset)
            --  a0/b0 the right-hand edge (across negative), a1/b1 the
            --  left; a deck's may be narrowed where a ramp took a lane
            local a0x, a0y = arc.band_edge(pv.x, pv.y, pv.dx, pv.dy, ha, pv.wr, 1)
            local a1x, a1y = arc.band_edge(pv.x, pv.y, pv.dx, pv.dy, ha, pv.wl, -1)
            local b0x, b0y = arc.band_edge(cu.x, cu.y, cu.dx, cu.dy, hb, cu.wr, 1)
            local b1x, b1y = arc.band_edge(cu.x, cu.y, cu.dx, cu.dy, hb, cu.wl, -1)
            local acr = arc.f32(arc.f32(-0.5 * arc.f32(pv.wr + cu.wr)) * inset)
            local acl = arc.f32(arc.f32(0.5 * arc.f32(pv.wl + cu.wl)) * inset)
            local mx = arc.f32(0.5 * arc.f32(pv.x + cu.x))
            local my = arc.f32(0.5 * arc.f32(pv.y + cu.y))
            local order = s:order(mx, my)
            local mat, ala, alb = d.mat, pv.s, cu.s
            local cls = 0

            --  A road's own pair: the class it carries, and its one
            --  marking -- the approach to a level crossing, whose along
            --  is the distance to the crossing.
            if d.family == "road" then
                cls = d.class >= 0 and d.class
                      or s:road_class(math.floor(mx), math.floor(my))
                if app and pv.xd > app.near and 0.5 * (pv.xd + cu.xd) < app.far then
                    mat, ala, alb = arc.mat.xapproach, pv.xd, cu.xd
                end
            end

            if d.deck then arc.deck.gore(s, d, i, pv, cu, ha, hb, order, ala, alb) end

            --  A quad in a cut, the road below the ground at a corner,
            --  is flagged in its class (4 and up) so the clipping check
            --  knows the ground standing over it is meant, held back by
            --  the walls below.
            local g0, g1 = s:ground(a0x, a0y), s:ground(a1x, a1y)
            local g2, g3 = s:ground(b0x, b0y), s:ground(b1x, b1y)
            local sunk = pv.z < f32(g0 - cut) or pv.z < f32(g1 - cut)
                      or cu.z < f32(g2 - cut) or cu.z < f32(g3 - cut)
                      or pv.z < f32(zo0 - dip) or cu.z < f32(zo1 - dip)
            s:class(sunk and cls + 4 or cls)

            --  One quad, the whole band: the sidewalk pass and the
            --  marking pass draw these same vertices again under their
            --  own pass number, so a strip costs one quad however many
            --  passes paint it.  A band that stands on the ground is
            --  drawn ON it -- handed no height, every piece it is cut
            --  into takes the drawn surface at its own corners.  A deck
            --  carries its own height and keeps it.
            local qa = d.flies and pv.z or -1.0
            local qb = d.flies and cu.z or -1.0
            s:quad(a0x, a0y, a1x, a1y, b0x, b0y, b1x, b1y,
                   qa, qb, acr, acl, ala, alb, mat, order + slot)
            s:class(cls)

            if d.deck then
                arc.deck.under(s, d, i, n, pv, cu, a0x, a0y, a1x, a1y, b0x, b0y, b1x, b1y, order)
            end
        end
        ::next::
    end
    return true
end
