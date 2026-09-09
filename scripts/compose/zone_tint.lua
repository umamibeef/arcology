--  zone_tint.lua -- the zone tints, for the map view.
--
--  One flat quad per zoned tile, a hair over its ground, carrying the
--  zone in the material's own channel.  The vertex shader drops them
--  unless the camera is looking down, so they cost nothing in the
--  oblique view they would spoil.
--
--  A placed structure is tinted too: what the player put there rather
--  than what a zone grew carries its own code, and that decision is
--  arc.rules.zone_of's.

--  How far over the ground the tint floats, and where it sits in the
--  stack: over the ground and under everything built on it.
local OVER, SLOT = 0.02, 0.4

arc.rules.zone_tint = function (t)
    local d = t:info()
    if d.zone == 0 then return true end
    local x0, y0, z0 = t:at(0)
    local x1, y1, z1 = t:at(1)
    local x2, y2, z2 = t:at(2)
    local x3, y3, z3 = t:at(3)
    local order = d.order + SLOT
    t:tri(x0, y0, z0 + OVER, x1, y1, z1 + OVER, x2, y2, z2 + OVER,
          d.zone, 0.0, arc.mat.zone, order)
    t:tri(x0, y0, z0 + OVER, x2, y2, z2 + OVER, x3, y3, z3 + OVER,
          d.zone, 0.0, arc.mat.zone, order)
    return true
end
