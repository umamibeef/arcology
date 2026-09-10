--  node_threads.lua -- which threads a thread junction carries.
--
--  A railway junction is not paved like a line's: it is made of THREADS,
--  each routed from one arm's rails to another's, and what the junction
--  is depends entirely on which pairs are laid.
--
--    A THROUGH LINE, where both arms of an axis are there: the line runs
--      straight across.  Where both axes are through lines the second
--      lies over the first, and the two cross as a diamond.
--    A WYE, where an arm has no opposite: the branch runs into every
--      other arm of the junction, its threads a gauge apart so they read
--      as separate rails rather than one wide one.
--
--  The raise comes with the pair because how the threads stack is part of
--  the same answer -- a diamond is a through line lifted over another,
--  not a different kind of thread.
--
--  There is no pattern in C behind this.  Take the rule away and a thread
--  junction carries no thread at all.

local f32 = arc.put.f32

arc.rules.node_threads = function (j)
    local d = j:info()
    local function linked(e) return d.links & (1 << e) ~= 0 end

    --  the through lines first, each raised over the last
    local second = 0
    for e = 0, 1 do
        if linked(e) and linked(e + 2) then
            j:thread(e, e + 2, f32(arc.geo.rail_thru + f32(arc.geo.rail_thru2 * second)))
            second = second + 1
        end
    end

    --  then a wye for every arm with no opposite
    for eb = 0, d.arms - 1 do
        if linked(eb) and not linked((eb + 2) % d.arms) then
            local k = 0
            for e = 0, d.arms - 1 do
                if e ~= eb and linked(e) then
                    j:thread(eb, e, f32(-arc.geo.rail_gauge * k))
                    k = k + 1
                end
            end
        end
    end
    return true
end
