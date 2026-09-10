--  thread_marks.lua -- what stands beside a family's threads, and where.
--
--  Right-hand running (spec 5.6).  A BLOCK SIGNAL every rsig_every along
--  each thread, on that thread's own outer side, facing back down it; an
--  ABSOLUTE one a tile before the junction the thread runs toward; and a
--  WHISTLE POST two tiles before every level meet, each way.
--
--  A signal wants its tile clear of a meet, which has its own
--  protection.  A whistle post is placed against one on purpose, and
--  faces along the map's own axis rather than back down the thread.
--
--  A signal is registered as well as drawn: the block it watches lights
--  it, so it is stood through the strip's own thread_signal rather than as
--  a plain model.
--
--  Nothing in C spaces or stands any of these.

local geo = arc.geo
local f32 = arc.put.f32

--  the station at or past a distance along the strip
local function station(s, n, at)
    local j = 1
    while j < n - 1 do
        local _, _, _, _, _, sj = s:at(j)
        if sj >= at then break end
        j = j + 1
    end
    return j
end

local function stand(s, d, at, side, out, model, signal, to_map)
    local j = station(s, d.n, at)
    local sx, sy, _, hx, hy, sj = s:at(j)
    if signal and s:near_lap(sx, sy) then return end
    local rx = f32(f32(-hy * side) * out)
    local ry = f32(f32(hx * side) * out)
    local px, py = f32(sx + rx), f32(sy + ry)
    --  a mark whose place is off the map is not stood at all
    if not s:on_map(px, py) then return end
    local order = s:order(px, py)
    if signal then
        s:thread_signal(model, order, px, py, f32(-hx * side), f32(-hy * side), signal, sj, side)
    else
        local fx, fy = 1.0, 0.0
        if not to_map then fx, fy = f32(-hx * side), f32(-hy * side) end
        s:prop_flat(model, order, px, py, fx, fy)
    end
end

arc.rules.thread_marks = function (s)
    local d = s:info()
    if d.n <= 2 then return true end

    for side = 1, -1, -2 do
        local at = geo.rsig_first
        while at < d.len - 0.5 do
            stand(s, d, at, side, geo.rsig_out, "thread_signal", 0, false)
            at = at + geo.rsig_every
        end
        if ((side == 1 and d.ahead) or (side == -1 and d.behind)) and d.len > 2.5 then
            stand(s, d, side == 1 and d.len - 1.0 or 1.0, side,
                  geo.rsig_out, "thread_signal", 1, false)
        end
    end
    for _, at0 in ipairs(s:meets()) do
        for side = 1, -1, -2 do
            local at = at0 - side * 2.0
            if at >= geo.rmark_end and at <= d.len - geo.rmark_end then
                stand(s, d, at, side, geo.rmark_out, "milepost", nil, true)
            end
        end
    end
    return true
end
