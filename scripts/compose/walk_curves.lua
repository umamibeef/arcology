--  walk_curves.lua -- the margin network's own outline.
--
--  Drawn as the fitted curves are, so what the network HOLDS can be seen
--  rather than inferred from the bands it lays: a hairline down the
--  middle of each run, and a join drawn end to end where the band is one
--  -- a meet or a cap.  Blue for a margin, tan for a meet, red
--  for a cap.
--
--  A meet is drawn as what it IS in the network: the line from one
--  margin to the other.  Its cross-sections run the other way, along
--  the arm, and a wire down them would read as a margin going the wrong
--  way across the line.

--  The hairline stands a hair over the ground so it reads over whatever
--  it crosses, and takes its own place in the stack, over the bands.
local OVER, SLOT = 0.07, 0.46

local PAINT = {side = 6.0, corner = 6.0, meet = 3.0, cap = 5.0}

arc.rules.walk_curves = function (b)
    local d = b:info()
    local paint = PAINT[d.band] or 6.0
    local wide = arc.rules.family({name = "line"}).strip.line_wide
    local n = b:count()
    if n >= 2 and d.band ~= "meet" then
        for k = 0, n - 2 do
            local a0x, a0y, a1x, a1y, za = b:at(k)
            local b0x, b0y, b1x, b1y, zb = b:at(k + 1)
            b:wire(0.5 * (a0x + a1x), 0.5 * (a0y + a1y),
                   0.5 * (b0x + b1x), 0.5 * (b0y + b1y),
                   za, zb, paint, wide, OVER, SLOT)
        end
        return true
    end
    local ax, ay, bx, by, za, zb = b:ends()
    b:wire(ax, ay, bx, by, za, zb, paint, wide, OVER, SLOT)
    return true
end
