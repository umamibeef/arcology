--  thread_signal.lua -- what aspect a thread signal shows.
--
--  A signal governs a BLOCK of thread and shows red while a train stands
--  in it.  The world measures how near the nearest car is each way along
--  that thread -- ahead of the signal in the direction it faces, and
--  behind it -- and this decides how much of the block has to be clear.
--
--  Behind matters as well as ahead: a train that has just passed is
--  still in the block it is leaving, and a signal that went green the
--  moment the engine cleared it would show green at a train's flank.
--
--  This is the division the level meet's gate is under too: the
--  world says how near, the rule says what to do about it.
--
--  The answer is the MODEL the signal's head shows, so a set with three
--  aspects needs no C at all: nothing in the pipeline knows a red from a
--  green.  Answer nothing and the signal's head is dark.
--
--  There is no block test in C behind this, and no aspect either: take
--  the rule away and every signal's head is dark.

arc.rules.thread_signal = function (s)
    if s.ahead < arc.geo.rail_block_ahead or s.back < arc.geo.rail_block_back then
        return "rail_aspect_red"
    end
    return "rail_aspect_green"
end
