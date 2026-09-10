--  car_turn.lua -- the arm a car takes at a junction.
--
--  A car reaching a node has to go somewhere.  The arms are offered as
--  the world found them -- every arm of the node except the one the car
--  arrived by, unless that is the only one there is -- each with the way
--  it heads AWAY from the node, so a rule may prefer one over another by
--  where it goes rather than by its number.
--
--  This one takes any of them, evenly.  The draw comes with the car: the
--  world made it from that car's own generator, so a city replays the
--  same traffic from the same save whatever this rule does with it.
--
--  There is no chooser in C behind this.  Take the rule away and a car
--  reaching a junction turns nowhere and stops there.

arc.rules.car_turn = function (c)
    local n = #c.arms
    if n < 1 then return nil end
    return c.draw % n
end
