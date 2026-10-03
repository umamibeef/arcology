--  census.lua -- WHICH NODE EACH LANE END STANDS AT.
--
--  Before any movement is planned, every open end of every slab lane is
--  filed under the interchange node it reaches, and under the ARM of
--  that node it belongs to.  What is filed here is what the node
--  planner has to spend.

local M = {}

--  Every node on the map, a place to file the ends that reach each, and
--  the path of every movement a node lays: a link is a lane in the model
--  and nothing on the ground, so without the paths the node's movements
--  are there and invisible (compose/interchange/slabs.lua reads them).
M.take = function (x, n)
    local turns = {}
    arc.interchange_turns = turns

    local nodes, of_tile, size = arc.interchange_nodes()
    local at_node = {}
    for i = 1, #nodes do at_node[i] = {out = {}, inn = {}, bands = {}} end

    local band_end = arc.poses and arc.poses.band_end
    for li = 0, n - 1 do
        local l = x:lane(li)
        if l and l.slab then
            for which = 0, 1 do
                if (which == 1 and l.open1) or (which == 0 and l.open0) then
                    local px, py, dx, dy = x:pose(li, which)
                    --  WHICH NODE this end stands at is what the pose
                    --  store says of its band (scripts/compose/poses.lua):
                    --  a lane on the band's right runs with it, so its
                    --  finish is at the band's tail and its start at the
                    --  head; one on the left runs back.  A band the store
                    --  has no record of is filed by its tile, under the
                    --  node whose catchment it lies in.  The store is
                    --  exact where the catchment can only guess: an arm's
                    --  lanes end anywhere over the fan, and two nodes a
                    --  few tiles apart have catchments that overlap.
                    --  AND IT MUST ACTUALLY STAND AT THE NODE IT IS
                    --  FILED UNDER.  The store names a node for each
                    --  end of a band, and it can name one the end is
                    --  nowhere near.  An end filed at a node it does
                    --  not reach is owed a movement to that node's
                    --  other arms, and the way laid for it then runs
                    --  across the map to a highway it never meets.  So
                    --  the reach is measured.  A store answer out of
                    --  reach falls back to the catchment, which is
                    --  geometry and cannot point somewhere else, and an
                    --  end out of reach of that as well is filed
                    --  nowhere: the band ends where it ends.
                    local function reaches(kk)
                        if not kk then return false end
                        local cells = nodes[kk].cells
                        local far   = arc.geo.interchange_catchment
                                      * arc.geo.interchange_catchment
                        for ci = 1, #cells do
                            local q  = cells[ci]
                            local qx = q % size + 0.5
                            local qy = math.floor(q / size) + 0.5
                            local d  = (px - qx) * (px - qx) + (py - qy) * (py - qy)
                            if d <= far then return true end
                        end
                        return false
                    end
                    local k
                    local be = band_end and band_end[l.band]
                    if be then
                        local at_tail = (which == 1) == (l.off > 0.0)
                        local rec
                        if at_tail then rec = be.tail else rec = be.head end
                        k = rec and rec.node
                    end
                    if not reaches(k) then
                        local tc, tr = math.floor(px), math.floor(py)
                        k = (tc >= 0 and tr >= 0 and tc < size and tr < size)
                            and of_tile[tr * size + tc] or nil
                        if not reaches(k) then k = nil end
                    end
                    --  AND THE HEIGHT IT STANDS AT MUST BE SETTLED.  The
                    --  grading pass settles the height of each arm a
                    --  node expects (compose/interchange/arms.lua,
                    --  arc.arm_z).  An end that pass could not settle
                    --  has no height there.  A movement laid to it
                    --  leaves the slab at nought and dives to the
                    --  ground, and the slab is left with its end cut
                    --  square and open.  So the end is filed nowhere
                    --  and the band ends where it ends.
                    if k and arc.arm_z then
                        local at_tail = (which == 1) == (l.off > 0.0)
                        if arc.arm_z[l.band * 2 + (at_tail and 1 or 0)] == nil then
                            k = nil
                        end
                    end
                    --  AND IT MUST ACTUALLY STAND THERE.  The store
                    --  names the node a band's end belongs to, and it
                    --  can name one the end is nowhere near.  An end
                    --  filed at a node it does not reach is given a
                    --  movement to the node's far arms, and that way
                    --  then runs across the map to a highway it never
                    --  meets.  So the reach is measured, and an end out
                    --  of reach is filed nowhere: the band ends where
                    --  it ends.
                    if k then
                        local reach, cells = 1e9, nodes[k].cells
                        for ci = 1, #cells do
                            local q = cells[ci]
                            local qx = q % size + 0.5
                            local qy = math.floor(q / size) + 0.5
                            local d  = (px - qx) * (px - qx) + (py - qy) * (py - qy)
                            if d < reach then reach = d end
                        end
                        if reach > arc.geo.interchange_catchment
                                  * arc.geo.interchange_catchment then
                            k = nil
                        end
                    end
                    local g = k and at_node[k]
                    if g then
                        --  Which ARM of the node this end belongs to is
                        --  the BAND it is a lane of.  The walk splits a
                        --  slab where it meets an interchange, so each
                        --  way into a node is a band of its own and its
                        --  six lanes are the six ends of that arm.
                        --
                        --  The band is exact where geometry can only
                        --  guess.  An arm of an L-shaped node lies off
                        --  the line from the node's middle, so walking
                        --  outward through an end charges it to the arm
                        --  beside it: the node then holds one arm with
                        --  six ends arriving and another with none, and
                        --  owes movements it has no lane left to lay.
                        local e = {li = li, x = px, y = py, dx = dx, dy = dy,
                                   w = l.w, off = l.off, band = l.band, arm = l.band}
                        if which == 1 then g.out[#g.out + 1] = e
                        else g.inn[#g.inn + 1] = e end
                        g.bands[l.band] = true
                    end
                end
            end
        end
    end
    return nodes, at_node, size, turns
end
return M
