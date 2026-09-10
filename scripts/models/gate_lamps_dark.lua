--  gate_lamps_dark.lua -- a level meet's flashers, both out.
--
--  The same pair as gate_lamps_lit in the same places, drawn while the
--  arm stands at its rest and no train is near.

arc.model.define("gate_lamps_dark", {
    p = {
        out  = 0.012,
        side = 0.045,
        size = 0.011,
        z    = 0.293,
        dark = 12,     --  the code the material paints an unlit lens
    },

    build = function (p)
        return {
            parts = {
                {kind = "face", ax = p.out, ac = p.side, w = p.size, z0 = p.z,
                 mat = arc.mat.lamp, code = p.dark},
                {kind = "face", ax = p.out, ac = -p.side, w = p.size, z0 = p.z,
                 mat = arc.mat.lamp, code = p.dark},
            },
        }
    end,
})
