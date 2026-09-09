--  ground.lua -- how high a strip rides over the ground it crosses.
--
--  A strip does not follow the ground.  It RAMPS between the nodes at
--  its two ends, and through any level crossing on the way.
--
--  A node -- a junction, a dead end, a level crossing -- stands at its
--  own tile's levelled height, and every corridor that reaches it ramps
--  to that one number.  Two segments meeting at a junction therefore
--  agree without anything being solved between them; a road and a
--  railway crossing agree because the crossing is a node they share;
--  and an edit moves only the segments whose anchors moved.
--
--  An end the strip does not reach a node for keeps the height the
--  ground gave it, and the ramp runs from there.
--
--  An end is pinned when it actually reaches its node's own tile.  A
--  turnout's arm does not: it ends at its port, tiles away, and takes
--  the shelf there as the box's own tracks do.  Pinned to its node
--  instead it ramps a level down into the hillside it crosses and draws
--  itself under the ground.  A dead end is pinned whatever the family,
--  because it ends on its own tile.
--
--  The ramp is EASED at both ends, so a corridor leaves a node level and
--  picks up its grade in between rather than kinking at the join.

local f32 = arc.put.f32

--  Two anchors closer together than this are one anchor: a crossing a
--  hair from the end it follows would make a step, not a ramp.
local APART = 0.5

--  As many anchors as a strip may have.  A strip with more level
--  crossings than this on it takes the first of them.
local MAX = 64

arc.rules.ground = function (g)
    local d = g:info()
    local n = d.n
    if n < 1 then return true end

    --  The stations, and the anchors along them.
    local at, z = {}, {}
    for i = 0, n - 1 do at[i], z[i] = g:at(i) end

    local ax, az = {}, {}
    local na = 0

    local function anchor(s, z)
        na = na + 1
        ax[na], az[na] = s, z
    end

    --  The near end: its node's altitude where the strip reaches it.
    local z0 = z[0]
    if (d.pin0 and d.reaches_node) or d.dead0 then z0 = g:node("start") end
    anchor(0.0, z0)

    --  Every level crossing along the way is a node of both networks.
    for i = 1, n - 2 do
        if na < MAX then
            local z = g:crossing(i)
            if z and (na < 2 or f32(at[i] - ax[na]) >= APART) then anchor(at[i], z) end
        end
    end

    --  And the far end.
    local z1 = z[n - 1]
    if (d.pin1 and d.reaches_node) or d.dead1 then z1 = g:node("goal") end
    anchor(d.total, z1)

    --  The ramp, eased at each anchor.
    local k = 1
    for i = 0, n - 1 do
        while k + 2 <= na and at[i] > ax[k + 1] do k = k + 1 end
        local span = f32(ax[k + 1] - ax[k])
        local u    = span > 1e-4 and f32(f32(at[i] - ax[k]) / span) or 0.0
        if u < 0.0 then u = 0.0 elseif u > 1.0 then u = 1.0 end
        u = f32(f32(u * u) * f32(3.0 - f32(2.0 * u)))
        g:set(i, f32(az[k] + f32(f32(az[k + 1] - az[k]) * u)))
    end
    return true
end
