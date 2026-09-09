--  junction.lua -- a junction's asphalt.
--
--  The outline is a solver's work: which arms leave, at what angle, how
--  far each mouth is held back, and how round each corner comes out.
--  What is laid over it is here -- a fan from the middle, every point at
--  its own ground and split half way out.
--
--  The junction's own tile is a levelled pad, so inside it every height
--  is that flat one; where the outline reaches past the tile the asphalt
--  has to follow the ground, and a triangle running from the middle
--  straight to the rim would cut under it on the way.  Hence the split:
--  the fan is two rings, not one.
--
--  The polygon it is given already stops on the footway's inner edge
--  where there is a band, so the asphalt is laid INSIDE the pavement and
--  the two meet edge to edge rather than one lying over the other.

arc.rules.junction = function (j)
    local d = j:info()
    local n = j:count()
    --  An outline that came to nothing: the square the box always was.
    --  The fan is still laid over whatever polygon there is, so a box
    --  can carry both.
    if d.square then j:quad() end
    if n < 3 then return true end
    local cx, cy, zj = d.x, d.y, d.z
    for i = 0, n - 1 do
        local p0x, p0y = j:at(i)
        local p1x, p1y = j:at((i + 1) % n)
        if math.abs(p0x - p1x) >= 1e-5 or math.abs(p0y - p1y) >= 1e-5 then
            local m0x, m0y = arc.f32(0.5 * arc.f32(cx + p0x)), arc.f32(0.5 * arc.f32(cy + p0y))
            local m1x, m1y = arc.f32(0.5 * arc.f32(cx + p1x)), arc.f32(0.5 * arc.f32(cy + p1y))
            local z0, z1 = j:surface(p0x, p0y), j:surface(p1x, p1y)
            local zm0, zm1 = j:surface(m0x, m0y), j:surface(m1x, m1y)
            --  On the ground, like the bands that meet it: these heights
            --  are a guide, and each piece takes the drawn surface where
            --  it lands, so a fan across a tile's fold does not cut
            --  under the terrain.
            j:tri(cx, cy, zj, m0x, m0y, zm0, m1x, m1y, zm1)
            j:tri(m0x, m0y, zm0, p0x, p0y, z0, p1x, p1y, z1)
            j:tri(m0x, m0y, zm0, p1x, p1y, z1, m1x, m1y, zm1)
        end
    end
    return true
end
