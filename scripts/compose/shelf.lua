--  shelf.lua -- making the corridors agree about the corners they share.
--
--  The corridors are the edges of a graph, and these are the graph's own
--  two rules.
--
--  Every tile keeps its OWN copy of each of its four corners.  That is
--  what lets two corridors lie side by side as two shelves with a wall
--  between them, and it is why the copies can disagree: each was written
--  by whichever of the corridor's stations happened to be nearest, so
--  one corridor's tiles can take the corner they share from different
--  stations and come out half a level apart.
--
--  ALONG AN EDGE the tiles are continuous.  Every copy of a corner
--  written by one corridor takes the height of the nearest station that
--  corridor wrote any of them with -- the nearest station knows best,
--  and one answer per corridor per corner is what makes the shelf
--  continuous along it.
--
--  AT A NODE every corridor that meets there is at one level.  The
--  copies of the node tile's own corners, whichever corridor wrote them,
--  are averaged and handed back to all of them.
--
--  Two corridors that merely run side by side share no node, so nothing
--  here touches them and the wall between their shelves stands.

--  The heights are the mesh's own floats, and a mean taken in more
--  places than they hold lands a shelf a hair off where the corridor
--  either side of it sits.
local f32 = arc.put.f32

arc.rules.shelf = function (s)
    local d = s:info()

    --  Along the edges: one height per corridor per corner, the nearest
    --  station's.
    for gy = 0, d.n do
        for gx = 0, d.n do
            local c = {s:copies(gx, gy)}
            if #c > 3 then
                --  More than one copy: the lowest distance per corridor.
                local best = {}
                for i = 1, #c, 3 do
                    local owner, dist, z = c[i], c[i + 1], c[i + 2]
                    if not best[owner] or dist < best[owner].dist then
                        best[owner] = {dist = dist, z = z}
                    end
                end
                for owner, b in pairs(best) do s:set(gx, gy, owner, b.z) end
            end
        end
    end

    --  At the nodes: one level for everything that meets there.
    for i = 0, d.nodes - 1 do
        local col, row = s:node(i)
        if col then
            local z = {s:heights(col, row)}
            if #z > 0 then
                local sum = 0.0
                for k = 1, #z do sum = f32(sum + z[k]) end
                s:node_set(col, row, f32(sum / #z))
            end
        end
    end
    return true
end
