--  bridge.lua -- the S between two of a path's lines that never cross.
--
--  Two parallel lines, or two that cross too far away to be one corner,
--  are joined by a biarc: two arcs of the same radius meeting back to
--  back, tangent to one line where it ends and to the other where it
--  begins.  The tangent points are drawn BACK along each line, and the
--  farther back they go the wider the arcs are, so the first placing
--  that holds inside the band is the widest that does.
--
--  How far back a tangent point may go is the edge behind it.  Half of
--  that edge, the other half belonging to whatever turns at its far end
--  -- unless the vertex there was built by a biarc of its own, which
--  leaves only its remainder, or unless it is the path's own end, which
--  turns nothing and needs only its reserve.  A line that IS the path's
--  end line draws nothing back at all.
--
--  Both points are drawn back the same distance, by the smaller of the
--  two budgets, so neither arc is left long enough to leave the
--  corridor.  When nothing symmetric holds the same chord is slid along
--  by quarter tiles either way -- one tangent point further back and the
--  other past its line's start, over the corner blocks, which are the
--  band's own.  A band whose first cells have a building beside them has
--  every S that ends at their centre overhanging it, and the S that ends
--  half a tile sooner does not.

local f32 = arc.put.f32

--  An arc under this reads as a corner.
local KINK = 0.30

--  The placings: seventeen fractions of the budget, widest first, and
--  seventeen slides of a quarter tile either way.
local STEPS  = 16
local SLIDES = 16
local SLIDE  = 0.25

--  The room test is a hair generous: a tangent that fits to within this
--  is a tangent that fits.
local SLACK = f32(1e-4)

--  ONE PLACING, and whether it stands.
--
--  The biarc is solved first, because where its two tangent points fall
--  decides how much edge is left outside them.  Those OUTER edges have
--  to hold the tangents too -- half an edge each way, the other half
--  belonging to the neighbouring vertex, unless that neighbour is the
--  path's own end, which turns nothing and needs only its reserve, or a
--  biarc's own vertex, which has already spent what it needs.  A jog
--  right off an interchange comes out as two quarter circles when the S
--  is asked to leave half of the tiles before it to a start that has no
--  arc at all.
--
--  Then the band has to hold it, and the radius it holds at may not be
--  under the band's own half width: below that the inner edge of the
--  turn folds through itself.
--
--  nil for a placing that does not stand.
local function place(b, d, a, c, f, shift)
    local s = b:solve(a, c, f, shift)
    if not s then return nil end

    local room_in, room_out
    if d.first then
        room_in = f32(s.len_in - d.reserve0)
    elseif d.fixed_prev >= 0.0 then
        room_in = f32(s.len_in - d.fixed_prev)
    else
        room_in = f32(s.len_in - f32(d.share * s.len_in))
    end
    if d.last then
        room_out = f32(s.len_out - d.reserve1)
    else
        room_out = f32(s.len_out - f32(d.share * s.len_out))
    end

    if s.tangent > f32(room_in + SLACK) then
        b:refuse("in", room_in, s.len_in)
        return nil
    end
    if s.tangent > f32(room_out + SLACK) then
        b:refuse("out", room_out, s.len_out)
        return nil
    end

    local r = b:holds()
    local floor = d.padded and f32(d.band - d.margin) or d.band
    if r > 0.0 and r < floor then r = -1.0 end
    b:result(r)
    return r
end

arc.rules.bridge = function (b)
    local d = b:info()

    --  What each line may draw back.
    local ba
    if d.head then
        ba = 0.0
    elseif d.fixed_prev >= 0.0 then
        ba = f32(d.len_in - d.fixed_prev)
    else
        ba = f32(0.5 * d.len_in)
    end
    local bc = d.tail and 0.0 or f32(0.5 * d.len_out)
    if d.first then ba = f32(d.len_in - d.reserve0) end
    if d.last  then bc = f32(d.len_out - d.reserve1) end
    if ba < 0.0 then ba = 0.0 end
    if bc < 0.0 then bc = 0.0 end
    b:note(ba, bc)

    --  Each side's own budget still bounds a slid S; the symmetric one
    --  is the smaller, and at a band's start that is nothing, which is
    --  exactly where sliding is needed.
    local ba0, bc0 = ba, bc
    local both = ba < bc and ba or bc
    ba, bc = both, both

    local floor = f32(-0.5 * d.gap)
    local best  = -1.0

    for si = 0, SLIDES do
        --  Something symmetric held: the slid ones are for when nothing
        --  did.
        if si == 1 and best >= 0.0 then break end
        local shift = (si + 1) // 2 * ((si % 2 == 1) and -1 or 1)
        local sa    = f32(SLIDE * shift)
        for f = STEPS, 0, -1 do
            local a = f32(f32(f32(ba * f) / STEPS) + sa)
            local c = f32(f32(f32(bc * f) / STEPS) - sa)
            if not (a > ba0 or c > bc0 or a < floor or c < floor) then
                local r = place(b, d, a, c, f, shift)
                --  Nothing at all: no biarc, or no room for its tangents.
                if r and r > best then
                    best = r
                    b:keep()
                    --  The widest that holds: done.
                    if r > 0.0 then break end
                end
            end
        end
    end

    if best >= KINK then b:place() end
    return true
end
