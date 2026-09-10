--  train_turn.lua -- the arm a train takes at a thread junction.
--
--  The beat stops where a train reaches a node and offers every arm of
--  it except the one the train arrived by, each with the way it heads
--  AWAY from the node, and the heading the train arrived on.
--
--  A railway is not a line: a train does not pick a direction, it
--  follows the line it is on.  So this takes the arm that continues
--  STRAIGHTEST -- the one whose heading lies nearest the train's -- and a
--  wye or a spur is taken only where nothing carries straight on.
--
--  There is no chooser in C behind this.  Take the rule away and a train
--  reaching a junction runs on to the end of its own thread and reverses
--  there, which is what a terminus does anyway.

arc.rules.train_turn = function (t)
    local best, bd
    for k = 1, #t.arms do
        local a = t.arms[k]
        local dot = a.dx * t.hx + a.dy * t.hy
        if not bd or dot > bd then bd, best = dot, k - 1 end
    end
    return best
end
