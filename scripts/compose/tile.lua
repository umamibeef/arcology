--  tile.lua -- the ground itself.
--
--  The heightfield, the slope codes and the pads a network cut into the
--  hill are settled before this.  What is drawn over them is here, and
--  it is four things:
--
--    the TOP     the tile's own face, cut by the slope code its art
--                carries, or the water surface where a body of water
--                lies on it;
--    the SEABED  under that water, and the land going on below the
--                surface down to it on every side that is not water,
--                both a quarter step behind the tile so the surface wins
--                inside the diamond and they show through the glass;
--    the WALLS   down to the neighbour over each edge, done on the east
--                and south only so an edge between two tiles is drawn
--                once;
--    the RIM     at the map's own cut edges: through land the layers of
--                sediment from what the tile draws down to the base, and
--                the foundation of a pad or a spur as blocks above the
--                ground; through water the glass from the surface to the
--                seabed and the sediment under it.
--
--  Corners run 1 to 4 -- north-west, north-east, south-east, south-west
--  -- and edges 1 to 4, north, east, south, west.

local NW, NE, SE, SW = 0, 1, 2, 3
local E_N, E_E, E_S, E_W = 0, 1, 2, 3

--  A tile's kind, as the pipeline settles it: the field cut on the
--  sprite's diagonal, water flat at the table over a seabed, a flat pad
--  at a building's or a flat piece's level, and a sloped network piece
--  on its own plane.
local T_LAND, T_WATER, T_PAD, T_PLANE = 0, 1, 2, 3

--  How far behind the tile the seabed and the land under water sit.
local UNDER = 0.25
--  And how far in front of it a wall between two tiles does.
local FACING = 0.5

arc.rules.tile = function (t)
    local d = t:info()

    --  The underground view: the ground itself, the seabed under water,
    --  cut by its own slope code, and nothing else.
    --  A colour is three numbers, so it is taken into locals: used in
    --  the middle of an argument list only its first would arrive.
    local lr, lg, lb = t:colour("land")
    local er, eg, eb = t:colour("earth")
    local wr, wg, wb = t:colour("wall")
    local sr, sg, sb = t:colour("sediment")

    if d.underground then
        t:top(true, d.code, d.order, lr, lg, lb, false)
        return true
    end

    --  The top.  A tile with water over it draws its surface instead of
    --  its ground -- calm where it is a marina's or a stream's and alive
    --  elsewhere -- and is still cut on its own slope where the ground
    --  under the water is a field rather than a pad or a plane.
    local tr, tg, tb = lr, lg, lb
    if d.wet then
        tr = d.surface
        tg = (d.kind == T_PAD or d.xter >= 0x30) and 1.0 or 0.0
        tb = arc.mat.surface
    end
    t:top(false, d.kind == T_LAND and d.code or 0, d.order, tr, tg, tb, d.kind == T_PAD)

    --  Under a water body: the seabed, and the land going on below the
    --  surface down to it on every side that is not water.
    if d.wet then
        local br, bg, bb = t:colour("seabed")
        t:top(true, 0, d.order - UNDER, br, bg, bb, false)
        for e = E_N, E_W do
            local there, _, _, _, _, _, ia, ib, _, sea = t:edge(e)
            --  Water beside water shares the bed.
            if there and not sea then
                local _, _, za, bza = t:at(ia)
                local _, _, zb, bzb = t:at(ib)
                if bza < za - 1e-4 or bzb < zb - 1e-4 then
                    --  Seen from inside the water, so the face turns in.
                    local nx, ny = t:normal(e)
                    t:wall(ia, ib, false, za, zb, bza, bzb, -nx, -ny,
                           d.order - UNDER, er, eg, eb)
                end
            end
        end
    end

    --  The walls, on the east and south edges, so each edge between two
    --  tiles is done once.
    for _, e in ipairs({E_E, E_S}) do
        local there, na, nb, water, nxbld, corridor, ia, ib = t:edge(e)
        if there then
            local _, _, za = t:at(ia)
            local _, _, zb = t:at(ib)
            if math.abs(za - na) >= 1e-4 or math.abs(zb - nb) >= 1e-4 then
                --  A wall of coursed blocks where the step is
                --  ENGINEERED, earth where it is the hill's own.  A
                --  building's foundation is engineered; so is the side of
                --  a network tile that stands ABOVE its neighbour, which
                --  is a line or a line built up on fill.  A network tile
                --  that sits BELOW its neighbour is a cut into the slope,
                --  and the slope's face is earth, which is what the art
                --  shows.
                local r, g, b
                local built = d.xbld >= 0x0E and d.xbld < 0x49
                local nbuilt = nxbld >= 0x0E and nxbld < 0x49
                if d.xbld >= 0x70 or nxbld >= 0x70 or d.corridor or corridor then
                    r, g, b = wr, wg, wb
                elseif za + zb > na + nb + 1e-4 and built then
                    r, g, b = wr, wg, wb   -- this tile is built up over its neighbour
                elseif na + nb > za + zb + 1e-4 and nbuilt then
                    r, g, b = wr, wg, wb   -- the neighbour is
                elseif d.wet and water then
                    --  A drop between two waters: a face of water, a cascade.
                    r, g, b = math.max(za, zb, na, nb), 1.0, arc.mat.surface
                else
                    r, g, b = er, eg, eb
                end
                --  The face is seen from the lower side.
                local nx, ny = t:normal(e)
                if za + zb < na + nb then nx, ny = -nx, -ny end
                t:wall(ia, ib, false, za, zb, na, nb, nx, ny, d.order + FACING, r, g, b)
                t:walled()
            end
        end
    end

    --  The map's cut edges, all four of them always: the mesh is built
    --  once and the camera may look at it from any side.
    for e = E_N, E_W do
        local _, _, _, _, _, _, ia, ib, rim = t:edge(e)
        if rim then
            local nx, ny = t:normal(e)
            local _, _, za, bza, pa = t:at(ia)
            local _, _, zb, bzb, pb = t:at(ib)
            if d.wet then
                local gr, gg, gb2 = t:colour("glass")
                t:glass(ia, ib, nx, ny, d.order, gr, gg, gb2)
                t:wall_r(ia, ib, bza, bzb, 0, 0, nx, ny, d.order, sr, sg, sb, bza, bzb)
            else
                local ga, gb = za, zb
                if d.kind == T_PAD or d.kind == T_PLANE then
                    ga, gb = pa, pb
                    t:wall(ia, ib, false, za, zb, ga, gb, nx, ny, d.order, wr, wg, wb)
                end
                t:wall_r(ia, ib, ga, gb, 0, 0, nx, ny, d.order, sr, sg, sb, bza, bzb)
            end
        end
    end
    return true
end
