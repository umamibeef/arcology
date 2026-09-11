--  signal.lua -- whether the signal a car faces reads red.
--
--  A junction with lights runs a fixed cycle and the two groups of arms
--  take it in turn: twenty seconds round, the green held for most of a
--  group's half of it, then an all-red clearance before the other group
--  starts.  How much of the cycle is green is arc.geo.signal_green.
--
--  Neighbouring junctions must not all go green together, so each takes
--  a PHASE of its own from where it stands -- a stagger the grid decides
--  rather than a wave anyone laid out.
--
--  The two groups are told apart by the way the car faces the junction:
--  a car facing along the map's columns is in one, along its rows the
--  other, and the second runs half a cycle behind the first.
--
--  There is no clock in C behind this.  Take the rule away and every
--  signal stays green.

local f32 = arc.put.f32

arc.rules.signal = function (s)
    --  The stagger is the world's reading; what phase it means is
    --  arc.rules.signal_phase's, and the shader that lights the lamps
    --  reads the same answer.  Two copies of it would show a driver
    --  green at a lamp that is red.
    local phase = f32(arc.rules.signal_phase(s.k))
    local t     = f32(f32(s.t / 20.0) + phase)
    t = f32(t - math.floor(t))
    if math.abs(s.hx) > math.abs(s.hy) then
        t = f32(t + 0.5)
        t = f32(t - math.floor(t))
    end
    return t >= arc.geo.signal_green
end
