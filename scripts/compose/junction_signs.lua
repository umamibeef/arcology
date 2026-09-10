--  junction_signs.lua -- what stands at a junction's arms.
--
--  arc.rules.control says how each arm is controlled; this stands what
--  the driver actually sees there and puts it on the map.  A signal, a
--  stop sign, or nothing at all.
--
--  Each is placed at the TILE'S MIDDLE facing the driver it is for, with
--  the junction's half width for its size: the model steps itself out to
--  the mouth and across to that driver's own corner, and every
--  measurement it does that by is its own file's.
--
--  A signal carries the junction's phase and its arm's group so the
--  shader can run its lamps on the same cycle that holds the cars.  A
--  stop sign carries neither: it says the same thing all day.
--
--  Nothing in C stands a sign.  Take this away and no junction has one.

local DU = {[0] = 0, 1, 0, -1} -- north, east, south, west
local DV = {[0] = -1, 0, 1, 0}

arc.rules.junction_signs = function (j)
    local d = j:info()
    if not d.col then return true end
    local prop = arc.rules.control_prop
    if not prop then return true end
    for e = 0, 3 do
        local a = d.arms[e + 1]
        if a then
            local p = prop(a.control)
            if p and p.model then
                local phase, group = 0.0, 0.0
                if p.cycle then
                    local ph = arc.rules.signal_phase
                    local gr = arc.rules.signal_group
                    phase = ph and ph((d.col * 7 + d.row * 13) % 8) or 0.0
                    group = gr and gr(e) or 0.0
                end
                arc.put.model(p.model, d.col + 0.5, d.row + 0.5,
                              -DU[e], -DV[e], d.half, phase, group)
            end
        end
    end
    return true
end
