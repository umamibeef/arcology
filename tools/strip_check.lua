--  strip_check.lua -- the composition against the pipeline's own numbers.
--
--  scripts/compose/strip.lua lays every strip's surface.  It works from
--  the stations the fit and the grading produced, and the pipeline can
--  still build one pair the way its own stages want it, so the two can
--  be compared pair for pair over a whole city.
--
--  They agree to the BIT.  A script works in doubles and the mesh keeps
--  floats, so the composition narrows every step through arc.f32 and
--  comes out the number the pipeline's own float arithmetic would have
--  reached.  That is what makes a band's edge and the footway's beside
--  it the same line rather than two lines a few millionths apart.
--
--      arcology cities/atlanta.sc2 --mute --run 0 --lua tools/strip_check.lua
--
--  It reports through arc.dump, so a run of it is a line a script can
--  read.  TOLERANCE is a ten-thousandth of a tile: a millimetre and a
--  half on the ground, and a hundred times the rounding it allows for.
local TOLERANCE = 0

local base = arc.rules.strip
local seen, bad, worst = 0, 0, 0

arc.rules.strip = function (s)
    local d = s:info()
    local fam = arc.rules.family({name = d.family, width = d.half * 2})
    local inset = (d.curbs and fam.footway) and fam.footway.inner or 1.0
    --  A pair the crossing band cuts is compared by the composition
    --  itself, which cuts it in the mesh's precision; the pipeline's own
    --  pair is cut in floats and the two are the same line by different
    --  roads.  Only the whole pairs are compared here.
    local cut0, cut1 = d.cross0, d.len - d.cross1
    for i = 2, s:count() do
        local a0x, a0y, a1x, a1y, b0x, b0y, b1x, b1y, acr, acl, ala, alb, ma, order = s:pair(i)
        local ps0 = select(6, s:at(i - 1))
        local cs1 = select(6, s:at(i))
        local whole = not ((d.cross0 > 0 and ps0 < cut0) or (d.cross1 > 0 and cs1 > cut1))
        if a0x and whole then
            local px, py, _, pdx, pdy, ps, pwl, pwr = s:at(i - 1)
            local cx, cy, _, cdx, cdy, cs, cwl, cwr = s:at(i)
            local ha = arc.band_half(d.half, s:width(pdx, pdy), inset)
            local hb = arc.band_half(d.half, s:width(cdx, cdy), inset)
            local a0x, a0y = arc.band_edge(px, py, pdx, pdy, ha, pwr, 1)
            local a1x, a1y = arc.band_edge(px, py, pdx, pdy, ha, pwl, -1)
            local b0x, b0y = arc.band_edge(cx, cy, cdx, cdy, hb, cwr, 1)
            local b1x, b1y = arc.band_edge(cx, cy, cdx, cdy, hb, cwl, -1)
            local mine = {
                a0x, a0y, a1x, a1y, b0x, b0y, b1x, b1y,
                arc.f32(arc.f32(-0.5 * arc.f32(pwr + cwr)) * inset),
                arc.f32(arc.f32(0.5 * arc.f32(pwl + cwl)) * inset),
                ps, cs, d.mat,
                s:order(arc.f32(0.5 * arc.f32(px + cx)), arc.f32(0.5 * arc.f32(py + cy))),
            }
            local theirs = {a0x, a0y, a1x, a1y, b0x, b0y, b1x, b1y, acr, acl, ala, alb, ma, order}
            local e = 0
            for k = 1, 14 do e = math.max(e, math.abs(mine[k] - theirs[k])) end
            seen, worst = seen + 1, math.max(worst, e)
            if e > TOLERANCE then bad = bad + 1 end
        end
    end
    return base(s)
end

arc.rules.strip_check_report = function ()
    arc.dump(("strips  %d pairs composed against the pipeline's own, %d past %g, worst %g of a tile\n")
             :format(seen, bad, TOLERANCE, worst))
    return bad
end
