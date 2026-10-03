--  arms.lua -- the height every arm of a node stands at.
--
--  An arm's height is not the band's own: the ways that leave it must
--  weave inside the node, and a node whose arms all sit at grade has
--  nowhere to put the movement that goes over.  So the arms are settled
--  first, over every node at once, and the weave is then planned
--  against them.

local plateau = require "compose.interchange.plateau"
local sample  = plateau.sample

local M = {}

arc.arm_z = arc.arm_z or {}
local function settle_arms(nodes, size, grade)
    local by_node = {}
    for key, k in pairs(arc.arm_node or {}) do
        local pose  = arc.arm_pose and arc.arm_pose[key]
        local floor = arc.arm_floor and arc.arm_floor[key]
        if pose and floor and nodes[k] then
            by_node[k] = by_node[k] or {}
            local list = by_node[k]
            list[#list + 1] = {key = key, x = pose.x, y = pose.y, dx = pose.dx, dy = pose.dy,
                               away = pose.away, floor = floor}
        end
    end
    for k, list in pairs(by_node) do
        table.sort(list, function (p, q) return p.key < q.key end)
        local arms, ms = {}, {}
        local c1 = nodes[k].cells[1]
        local tc, tr = c1 % size, math.floor(c1 / size)
        --  Every arm is one group: the arms stand at one height (the
        --  highest floor among them), which leaves every way the whole
        --  grade to climb over what it crosses.  Two highways parted a
        --  level apart cost the turns between them that same grade, and
        --  the node's ways are too short to pay it twice.
        local nx, ny = nodes[k].x, nodes[k].y
        --  TWO DECKS.  The arms are tied by AXIS: the arms of one
        --  highway stand at one height, and the other highway's stand
        --  the gap over them, so the through ways of the two cross as
        --  an overpass and every turn is a ramp between the two decks.
        --  ONE CEILING per group: the lowest arm's floor and the rise
        --  over it, or the highest floor where that is more.  An arm
        --  that can rise no further holds its group with it, so the
        --  arms of a deck never part.  The group that can stand
        --  highest goes over, since it is the one with the room.
        local groups = {}
        for _, a in ipairs(list) do
            a.axis = math.abs(a.x - nx) >= math.abs(a.y - ny) and "ew" or "ns"
            groups[a.axis] = groups[a.axis] or {keys = {}, top = -math.huge, cap = math.huge}
            local grp = groups[a.axis]
            grp.keys[a.key] = true
            if a.floor > grp.top then grp.top = a.floor end
            if a.floor + arc.geo.interchange_rise < grp.cap then grp.cap = a.floor + arc.geo.interchange_rise end
        end
        local order = {}
        for axis, grp in pairs(groups) do
            if grp.cap < grp.top then grp.cap = grp.top end
            order[#order + 1] = {axis = axis, grp = grp}
        end
        table.sort(order, function (p, q)
            if p.grp.cap ~= q.grp.cap then return p.grp.cap > q.grp.cap end
            return p.axis < q.axis
        end)
        for _, a in ipairs(list) do
            arms[a.key] = {z = a.floor, floor = a.floor, ceil = groups[a.axis].cap, fixed = false}
        end
        local tie = {groups = {}, over = #order > 1 and arc.geo.interchange_over or nil}
        for _, o in ipairs(order) do tie.groups[#tie.groups + 1] = o.grp.keys end
        --  Each way runs on the traffic's own side of the arms it
        --  joins, a lane's offset from the arm's line: the way from a
        --  to b and the way back are then a slab's width apart, as the
        --  lanes are, and are not read as crossing each other.
        local off = arc.geo.interchange_lane_off
        for _, a in ipairs(list) do
            for _, b in ipairs(list) do
                if a ~= b then
                    --  into the node by a, and out of it by b
                    local adx, ady = a.dx, a.dy
                    if a.away then adx, ady = -adx, -ady end
                    local bdx, bdy = b.dx, b.dy
                    if not b.away then bdx, bdy = -bdx, -bdy end
                    local q, rad, tlim = arc.chain_between(a.x + ady * off, a.y - adx * off, adx, ady,
                                                           b.x + bdy * off, b.y - bdx * off, bdx, bdy)
                    local pc = q and arc.fit(q, rad, tlim)
                    if pc and #pc > 0 then
                        local pts, len = sample(pc, arc.geo.interchange_sample)
                        --  no hold on a proxy: a held knot is the arm's
                        --  own node here, and a crossing at it would
                        --  raise the arm instead of the way
                        ms[#ms + 1] = {pts = pts, len = len, za = a.floor, zb = b.floor,
                                       ma = 0.0, mb = 0.0, tc = tc, tr = tr,
                                       arm_a = a.key, arm_b = b.key, at = {}}
                    end
                end
            end
        end
        if #ms > 0 then
            arc.weave.plan(ms, {
                grade = grade, over = arc.geo.interchange_over,
                over_min = arc.geo.interchange_over_min, lift = arc.geo.slab_lift,
                round = 0, crest = arc.geo.interchange_crest, stack = 0.0, arms = arms, tie = tie,
                ground = function (x, y) return arc.city.at("surface", math.floor(x), math.floor(y)) end,
                say = arc.log, dump = arc.geo.interchange_dump > 0.5})
        end
        for _, a in ipairs(list) do
            arc.arm_z[a.key] = arms[a.key].z
            if arc.geo.interchange_dump > 0.5 then
                arc.dump(string.format("ARM node %d,%d arm %d (%s) at %.2f,%.2f: floor %.3f, settled %.3f",
                    tc, tr, a.key, a.axis, a.x, a.y, a.floor, arms[a.key].z))
            end
        end
    end
end

--  The seat's height at a distance along a way, between its knots.

M.settle = settle_arms
return M
