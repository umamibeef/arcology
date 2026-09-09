--  walk_curves.lua -- the footway network's own outline.
--
--  Drawn as the fitted curves are, so what the network HOLDS can be seen
--  rather than inferred from the bands it lays: a hairline down the
--  middle of each run, and a join drawn end to end where the band is one
--  -- a crossing or a cap.  Blue for a footway, tan for a crossing, red
--  for a cap.
--
--  A crossing is drawn as what it IS in the network: the line from one
--  pavement to the other.  Its cross-sections run the other way, along
--  the arm, and a wire down them would read as a footway going the wrong
--  way across the road.

--  The hairline stands a hair over the ground so it reads over whatever
--  it crosses, and takes its own place in the stack, over the bands.
local OVER, SLOT = 0.07, 0.46

local PAINT = {side = 6.0, corner = 6.0, crossing = 3.0, cap = 5.0}

arc.rules.walk_curves = function (b)
    local d = b:info()
    local paint = PAINT[d.band] or 6.0
    local wide = arc.rules.family({name = "road"}).strip.line_wide
    local n = b:count()
    if n >= 2 and d.band ~= "crossing" then
        for k = 1, n - 1 do
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
