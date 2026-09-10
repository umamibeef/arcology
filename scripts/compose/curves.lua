--  curves.lua -- the fitted line, drawn over the world it made.
--
--  When the tuning window asks to see them: a hairline down the middle
--  of the band through every station, so the curve the fit produced can
--  be read against the ribbon that came out of it; the band's two edges
--  beside it, so the way can be read against the corridor it
--  sits in; and a mark at every piece boundary -- where a straight hands
--  over to an arc and back -- so the curve's extent is visible and not
--  just its shape.  Blue where an arc begins, red where the line is
--  straight through.

local f32 = arc.put.f32

--  The hairline's own numbers: how wide it is drawn, how far it floats
--  over the band, and where it sits in the stack.  The two edges go a
--  hair behind the centreline so it wins where they cross.
local LINE_SLOT, LINE_OVER, LINE_PAINT = 0.45, 0.06, 4.0
local EDGE_SLOT, EDGE_OVER, EDGE_PAINT = 0.44, 0.05, 7.0

arc.rules.curves = function (s)
    local d = s:info()
    local n = s:count()
    local fam = arc.rules.family({name = d.family, width = d.half * 2})
    local wide = fam.strip.line_wide

    for i = 1, n - 1 do
        local ax, ay, _, _, _ = s:at(i - 1)
        local bx, by = s:at(i)
        local _, _, za = s:at(i - 1)
        local _, _, zb = s:at(i)
        local dx, dy = bx - ax, by - ay
        local dl = math.sqrt(dx * dx + dy * dy)
        if dl >= 1e-5 then
            local px, py = -dy / dl * wide, dx / dl * wide
            local order = s:order(ax, ay)
            s:quad(ax - px, ay - py, ax + px, ay + py,
                   bx - px, by - py, bx + px, by + py,
                   za + LINE_OVER, zb + LINE_OVER,
                   LINE_PAINT, LINE_PAINT, 0, 0, arc.mat.vehicle, order + LINE_SLOT)
            --  And the width it carries: the band's two edges.
            local hw = d.half
            local ox, oy = -dy / dl * hw, dx / dl * hw
            for _, sgn in ipairs({-1, 1}) do
                s:quad(ax + ox * sgn - px * 0.5, ay + oy * sgn - py * 0.5,
                       ax + ox * sgn + px * 0.5, ay + oy * sgn + py * 0.5,
                       bx + ox * sgn - px * 0.5, by + oy * sgn - py * 0.5,
                       bx + ox * sgn + px * 0.5, by + oy * sgn + py * 0.5,
                       za + EDGE_OVER, zb + EDGE_OVER,
                       EDGE_PAINT, EDGE_PAINT, 0, 0, arc.mat.vehicle, order + EDGE_SLOT)
            end
        end
    end

    --  The construction behind it: a mark at every piece boundary.  Under
    --  the spline fit the boundaries mean nothing -- every sample is its
    --  own straight -- so the marks are left off there.
    local np = s:pieces()
    for k = 0, np do
        local x, y
        if k < np then
            x, y = s:piece_at(k, 0.0)
        else
            local len = s:piece(np - 1)
            x, y = s:piece_at(np - 1, len)
        end
        local order = s:order(x, y)
        if order then
            --  An arc, or a straight.
            local paint = 5.0
            if k < np then
                local _, turns = s:piece(k)
                if turns then paint = 6.0 end
            end
            s:box(order + fam.strip.mark_slot, x, y,
                  fam.strip.mark_wide, fam.strip.mark_wide,
                  fam.strip.mark_lift, fam.strip.mark_lift + fam.strip.mark_high,
                  arc.mat.vehicle, paint)
        end
    end
    return true
end
