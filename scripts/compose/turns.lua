--  turns.lua -- the pattern an intersection draws.
--
--  A junction's arms each carry lanes into it and lanes out of it, and
--  the PATTERN is which of them joins which.  Every pair this asks for is
--  routed between the two ports and cut like any other path; a pair it
--  does not ask for is a movement the junction does not permit.
--
--  There is no matcher in C behind this.  Take the rule away and every
--  arm of every junction meets nothing, which is what `swap_check`'s
--  `turns` entry reads.
--
--  The pattern here is the ordinary one:
--
--    * no arm joins itself, so there is no U-turn;
--    * with two lanes each way the inner lane goes to the inner and the
--      outer to the outer, as the markings would have it;
--    * a SPUR carries one lane, so one lane of each other arm feeds it or
--      leaves it -- the outermost, which is the one beside the lip;
--    * and a spur straight ahead of a bend's arm is that arm's through
--      movement and nobody else's: the other line turns, it does not
--      take the spur.
--
--  The arms are walked in their own order and the lanes in theirs,
--  because a connector is laid in the order it is asked for and the lane
--  model numbers them as they arrive.

arc.rules.turns = function (j)
    local d   = j:info()
    local arm = {}
    for e = 0, d.arms - 1 do arm[e] = j:arm(e) end

    for e = 0, d.arms - 1 do
        for e2 = 0, d.arms - 1 do
            local a, b = arm[e], arm[e2]
            if a and b and e ~= e2 and a.into and b.out then
                for k = 0, a.lanes - 1 do
                    for k2 = 0, b.lanes - 1 do
                        local take = true
                        if a.lanes == 2 and b.lanes == 2 and k ~= k2 then
                            take = false
                        end
                        if b.spur ~= 0 and k ~= a.lanes - 1 then take = false end
                        if a.spur ~= 0 and k2 ~= b.lanes - 1 then take = false end
                        if b.spur == 2 and e ~= (e2 + 2) % d.arms then take = false end
                        if a.spur == 2 and e2 ~= (e + 2) % d.arms then take = false end
                        if take then j:want(e, k, e2, k2) end
                    end
                end
            end
        end
    end
    return true
end
