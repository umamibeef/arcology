--  lamps.lua -- the street lighting, stood on the line it lights.
--
--  Which strips are lit and how the lamps are spaced along them is here,
--  and so is the standing of them: a lamp is a model placed beside the
--  strip, on the strip's OWN height rather than on the ground under the
--  lip, facing in over the line.
--
--  A lamp that would stand at a level meet is dropped: the meet
--  has its own protection and its own furniture.  One that would stand
--  off the map is dropped too.
--
--  The across offset is worked out at the mesh's own precision, because
--  a lamp a few millionths from where the line thinks it is reads as a
--  lamp leaning.
--
--  Nothing in C spaces or stands a lamp.  Take this away and no line is
--  lit.

local geo = arc.geo
local f32 = arc.put.f32

arc.rules.lamps = function (s)
    local d = s:info()
    if d.class < 0.5 or d.len <= geo.lamp_min then return true end

    local side = 1
    local at   = geo.lamp_first
    while at < d.len - 1.0 do
        --  the first station at or past this lamp's distance along
        local j = 1
        while j < d.n - 1 do
            local _, _, _, _, _, sj = s:at(j)
            if sj >= at then break end
            j = j + 1
        end
        local sx, sy, sz, hx, hy = s:at(j)
        if not s:near_lap(sx, sy) then
            --  across, toward the pole's side; the model faces back in
            local ax, ay = f32(-hy * side), f32(hx * side)
            local px = f32(sx + f32(f32(ax * d.half) * geo.lamp_in))
            local py = f32(sy + f32(f32(ay * d.half) * geo.lamp_in))
            --  a lamp whose place is off the map is not stood at all
            if s:on_map(px, py) then
                s:prop("street_lamp", s:order(px, py), px, py, -ax, -ay, sz)
            end
        end
        at, side = at + geo.lamp_every, -side
    end
    return true
end
