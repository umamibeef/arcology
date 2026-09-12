--  slab.lua -- what a freeway slab carries beside its way.
--
--  A slab is a slab on columns, nothing under or beside it: the side
--  faces show through the way from the far edge.  Two things are
--  built with each of its quads, and the strip's own rule calls them --
--  the gore before the quad, the underside after it.
--
--  THE GORE (spec 7.3): where the slab's outer lane parts from the
--  way, the lane's own slab beside the narrowing slab, level,
--  from the slab's edge out to the slab's -- a sliver widening to a
--  lane.  The slab alone; the spur itself is a piece of its own from
--  here to the line.  Laid BEFORE the slab's own slab: at the game's
--  pitch the painter's slot orders the two and the later wins.
--
--  THE UNDERSIDE: the soffit, the fascias and the end walls, kept behind
--  arc.geo.slab_underside until they are a pass of their own.

local f32 = arc.put.f32

--  Concrete, the piers' material: it carries a depth bias of its own so
--  the structure stays behind the slab it holds up.
local CONC = {1.0, 0.0, arc.mat.pier}
--  The along a gore's slab is marked at, which puts it past every
--  ordinary station and out of the marking material's way.
local GORE_ALONG = 4000.0

arc.slab = {}
--  And the same reading for any other way painted as one lane of a
--  slab, an interchange's movements among them.
arc.slab.lane_along = GORE_ALONG

function arc.slab.gore(s, d, i, pv, cu, ha, hb, order, ala, alb)
    if d.structure then return end
    local pl, pzr0, pzr1 = s:lane(i - 1)
    local cl, czr0, czr1 = s:lane(i)
    if (pl | cl) == 0 then return end
    for sd = 0, 1 do
        local bit = sd == 1 and 2 or 1
        local sg = sd == 1 and -1.0 or 1.0
        local wa = sd == 1 and pv.wl or pv.wr
        local wb = sd == 1 and cu.wl or cu.wr
        local za = sd == 1 and pzr1 or pzr0
        local zb = sd == 1 and czr1 or czr0
        if (pl | cl) & bit ~= 0 and 1.0 - 0.5 * (wa + wb) >= 0.01 then
            if cl & bit == 0 then zb = za end
            if pl & bit == 0 then za = zb end
            local i0x, i0y = arc.band_edge(pv.x, pv.y, pv.dx, pv.dy, ha, wa, sg)
            local o0x, o0y = arc.band_edge(pv.x, pv.y, pv.dx, pv.dy, ha, 1.0, sg)
            local i1x, i1y = arc.band_edge(cu.x, cu.y, cu.dx, cu.dy, hb, wb, sg)
            local o1x, o1y = arc.band_edge(cu.x, cu.y, cu.dx, cu.dy, hb, 1.0, sg)
            s:quad(o0x, o0y, i0x, i0y, o1x, o1y, i1x, i1y, za, zb,
                   -sg, f32(-sg * f32(0.5 * f32(wa + wb))),
                   ala + GORE_ALONG, alb + GORE_ALONG, arc.mat.band, order)
        end
    end
end

function arc.slab.under(s, d, i, n, pv, cu, a0x, a0y, a1x, a1y, b0x, b0y, b1x, b1y, order)
    if d.flat or arc.geo.slab_underside < 0.5 then return end
    --  Behind the slab in the painter's order: depth here is the sweep's
    --  slot, not a height, so a soffit at the same order as the line it
    --  hangs under would draw through it in stripes.  Every face goes
    --  through the tile clipper, since a soffit triangle straddling two
    --  tiles with one painter's order was drawn over the next tile's
    --  way.
    local gd = d.lane_piece and 0.5 * d.girder or d.girder
    local u0z, v0z = pv.z - gd, cu.z - gd
    s:tri_n(a0x, a0y, u0z, b1x, b1y, v0z, a1x, a1y, u0z, 0, 0, -1,
            CONC[1], CONC[2], CONC[3], order)
    s:tri_n(a0x, a0y, u0z, b0x, b0y, v0z, b1x, b1y, v0z, 0, 0, -1,
            CONC[1], CONC[2], CONC[3], order)
    --  The ends: where the slab begins and ends in the air, a wall
    --  across it from the line down to the soffit.
    if (i == 2 or i == n) and not d.lane_piece then
        local first = i == 2
        local e0x, e0y = first and a0x or b0x, first and a0y or b0y
        local e1x, e1y = first and a1x or b1x, first and a1y or b1y
        local zt = first and pv.z or cu.z
        local ax, ay = e1y - e0y, e0x - e1x
        local al = math.sqrt(ax * ax + ay * ay)
        local nx = al > 1e-6 and ax / al or 1.0
        local ny = al > 1e-6 and ay / al or 0.0
        if zt - d.girder > s:ground(e0x, e0y) then
            s:wall(e0x, e0y, e1x, e1y, zt, zt, zt - d.girder, zt - d.girder,
                   nx, ny, 0, order, CONC[1], CONC[2], CONC[3])
        end
    end
    --  The girder's fascia down from the way and the parapet up
    --  from it, on each side; a turn-out on the ground has neither.
    if not d.lane_piece then
        for side = 0, 1 do
            local eax, eay = side == 1 and a1x or a0x, side == 1 and a1y or a0y
            local ebx, eby = side == 1 and b1x or b0x, side == 1 and b1y or b0y
            local nx = side == 1 and -cu.dy or cu.dy
            local ny = side == 1 and cu.dx or -cu.dx
            s:edge(eax, eay, ebx, eby, pv.z, cu.z, nx, ny, d.girder, false, order)
        end
    end
end
