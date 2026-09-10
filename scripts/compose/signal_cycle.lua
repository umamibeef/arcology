--  signal_cycle.lua -- how a junction's lights are staggered and grouped.
--
--  Neighbouring junctions must not all go green together, so each takes
--  one of eight STAGGERS from where it stands.  What phase of the cycle a
--  stagger means is here: evenly spread, so eight junctions in a row hand
--  the green along.
--
--  And the two GROUPS of arms take the cycle in turn.  A group is a
--  number the signal's shader reads to know which half of the cycle its
--  lamps run on, and the arms of one axis share it.
--
--  Both are settled before anything is built, and both are read by the
--  shader that lights the lamps AND by the rule that holds a car at the
--  line -- so a signal cannot show green to a car it is stopping.

arc.rules.signal_phase = function (k)
    return k / 8.0
end

arc.rules.signal_group = function (e)
    --  north and south on one, east and west on the other
    return (e == 0 or e == 2) and 0.0 or 3.0
end
