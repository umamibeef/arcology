--  car_density.lua -- how many cars a tile's traffic is worth.
--
--  The cars are not a simulation of anybody's journey: they are what the
--  TRAFFIC LAYER looks like when you draw it.  A segment carries the sum
--  of what its tiles are worth, capped so a short street cannot fill
--  with them.
--
--  This is the original's own step: a tile busier than a third carries
--  one car, one busier than two thirds carries two.  A smooth curve, or
--  a reading of some other layer entirely, is this file and nothing
--  else -- the pipeline asks for all two hundred and fifty-six answers
--  before it builds anything, so a different curve costs nothing.
--
--  There is no density in C behind this.  Take the rule away and the
--  city has no cars.

arc.rules.car_density = function (tv)
    local n = 0
    if tv > 0x55 then n = n + 1 end
    if tv > 0xAA then n = n + 1 end
    return n
end
