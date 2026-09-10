--  gate_lamps_lit.lua -- a level meet's flashers, alternating.
--
--  The pair either side of the mast, drawn while the arm is off its
--  rest.  The two lamp codes are the two halves of the alternation the
--  material runs; gate_lamps_dark is the same pair, both out.

arc.model.define("gate_lamps_lit", {
    p = {
        out  = 0.012,  --  proud of the flasher bar
        side = 0.045,  --  either side of the mast
        size = 0.011,  --  the lens, half its span
        z    = 0.293,  --  the height of its middle
        left = 8,      --  the two halves of the alternation
        right = 9,
    },

    build = function (p)
        return {
            parts = {
                {kind = "face", ax = p.out, ac = p.side, w = p.size, z0 = p.z,
                 mat = arc.mat.lamp, code = p.left},
                {kind = "face", ax = p.out, ac = -p.side, w = p.size, z0 = p.z,
                 mat = arc.mat.lamp, code = p.right},
            },
        }
    end,
})
