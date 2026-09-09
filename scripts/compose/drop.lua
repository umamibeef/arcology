--  drop.lua -- the lane a ramp takes from the deck it leaves (spec 7.3).
--
--  A deck is two lanes wide.  Where a ramp leaves it, the deck gives up
--  its outer lane on the ramp's side over the ramp's taper, and takes it
--  back afterwards.  What this works out, station by station, is how
--  wide the deck is at that station on each side.
--
--  The taper, measured from the ramp's own point on the centreline and
--  running TOWARD THE ROAD:
--
--    the deck tile and the descent   narrow, the whole way
--    the gore, the taper's last tile widening back, and the ramp's own
--                                    sliver of lane sits beside it here
--    before the ramp                 full width, unless a partner ramp
--                                    lies ahead on the same side
--
--  The partner rule is what keeps a pair of ramps from widening the deck
--  back to two lanes for a tile and narrowing it again.  Toward the road
--  the deck stays narrow as far as a partner's point within seven tiles;
--  with no partner it widens back over one tile.
--
--  Every ramp asks, and each station keeps the NARROWEST width anyone
--  asked for, so two ramps overlapping do not undo each other.

local f32 = arc.put.f32

--  A partner counts from half a tile out to seven and a half.
local PARTNER_NEAR = 0.5
local PARTNER_FAR  = 7.5

--  The taper's boundaries, in tiles from the ramp's point.
local HALF = 0.5

--  How far back the deck widens over when there is no partner.
local WIDEN = 1.5

--  Which side of the deck a point lies on, from the station's heading:
--  0 is the viewer's right of the way it runs, 1 the left.
local function side_of(st, x, y)
    return f32(f32(f32(x - st.x) * st.dy) - f32(f32(y - st.y) * st.dx)) > 0.0 and 0 or 1
end

arc.rules.lane_drop = function (dr)
    local d = dr:info()
    dr:clear()
    if d.n < 1 or d.ramps < 1 then return true end

    --  The stations, and each ramp's nearest one on this band.  Asking
    --  again per ramp inside the partner search below was most of a
    --  pass's profile stage.
    local st = {}
    for i = 1, d.n do st[i] = dr:station(i) end

    local rp, iref = {}, {}
    for r = 1, d.ramps do
        rp[r] = dr:ramp(r)
        local best = d.reach
        for i = 1, d.n do
            local dx, dy = f32(st[i].x - rp[r].x), f32(st[i].y - rp[r].y)
            local dist = arc.put.sqrt(f32(f32(dx * dx) + f32(dy * dy)))
            if dist < best then best, iref[r] = dist, i end
        end
    end

    for r = 1, d.ramps do
        local i0 = iref[r]
        if i0 then
            local ramp = rp[r]
            local here = st[i0]
            local side = side_of(here, ramp.tx, ramp.ty)
            --  Whether the deck's way runs with the ramp's.
            local with = f32(f32(here.dx * ramp.ax) + f32(here.dy * ramp.ay)) > 0.0 and 1 or -1
            --  +1: the taper lies at increasing distance along the deck.
            local ds = ramp.leaves and -with or with

            --  A partner on the road's side: another ramp's point on this
            --  band, the same side, within seven tiles.
            local partner
            for r2 = 1, d.ramps do
                local i2 = iref[r2]
                if r2 ~= r and i2 then
                    local s2 = side_of(st[i2], rp[r2].tx, rp[r2].ty)
                    local dd = f32(f32(st[i2].at - here.at) * -ds)
                    if s2 == side and dd > PARTNER_NEAR and dd < PARTNER_FAR and
                       (not partner or dd < partner) then
                        partner = dd
                    end
                end
            end

            local gore0 = f32(HALF + ramp.len - 1.0)
            local gore1 = f32(HALF + ramp.len)
            for i = 1, d.n do
                --  Positive into the taper.
                local at = f32(f32(st[i].at - here.at) * ds)
                local w  = 1.0
                if at >= -HALF and at < gore0 then
                    --  The deck tile and the descent.
                    w = d.narrow
                elseif ramp.len > 0 and at >= gore0 and at < gore1 then
                    --  The gore, where the deck widens back and the ramp's
                    --  own sliver sits level beside it.
                    w = f32(d.narrow + f32(f32(1.0 - d.narrow) * f32(at - gore0)))
                    dr:gore(i, side)
                elseif at < -HALF then
                    if partner and f32(-at) < f32(partner - HALF) then
                        --  Two lanes as far as the partner.
                        w = d.narrow
                    elseif not partner and at >= -WIDEN then
                        --  Widening back.
                        w = f32(d.narrow + f32(f32(1.0 - d.narrow) * f32(f32(-HALF) - at)))
                    end
                end
                dr:width(i, side, w)
            end
        end
    end
    return true
end
