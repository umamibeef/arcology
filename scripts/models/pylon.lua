--  pylon.lua -- a power line's pylon.
--
--  A pole with a crossarm turned to the way the line runs.  The wires
--  between pylons are the line's, not the model's: arc.rules.power_tile
--  hangs them.

arc.model.define("pylon", {
    p = {
        wide = 0.04,
        tall = 1.45,
        arm  = 0.24,   --  the crossarm, across the way the line runs
        arm0 = 1.33,
        arm1 = 1.37,
    },

    build = function (p)
        return {
            parts = {
                {kind = "box", w = p.wide, d = p.wide, z0 = 0, z1 = p.tall,
                 mat = arc.mat.prop},
                {kind = "box", w = p.arm, d = p.wide, z0 = p.arm0, z1 = p.arm1,
                 mat = arc.mat.prop},
            },
        }
    end,
})
