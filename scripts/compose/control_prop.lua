--  control_prop.lua -- what a controlled arm of a junction carries.
--
--  arc.rules.control says how each arm of a junction is controlled; this
--  says what the driver actually sees there.  A signal, a stop sign, or
--  nothing at all -- an arm nobody controls carries no sign.
--
--  A prop that takes the CYCLE is given the junction's phase and its
--  arm's group, which is what the shader runs its lamps on.  A stop sign
--  takes neither: it says the same thing all day.
--
--  There are four controls and the answer is settled for each of them
--  before anything is built, so a junction costs a lookup.

arc.rules.control_prop = function (ctrl)
    if ctrl == 2 then return {model = "signal", cycle = true} end
    if ctrl == 1 then return {model = "stop_sign"} end
    return nil
end
