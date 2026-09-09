--  gate.lua -- a level crossing's gate, standing parts (spec 3.15).
--
--  The base junction box, the flasher bar, the "2 TRACKS" plaque and the
--  mechanism's case beside the mast on the road side.  The flashers'
--  lamps and the arm itself are the traffic's, rebuilt every frame from
--  gate_lamps_lit, gate_lamps_dark and arc.rules.gate_arm.

arc.model.define("gate", {
    p = {
        base   = 0.03,   --  the junction box at the foot
        base_z = 0.075,
        flash  = 0.046,  --  the flasher bar
        flashw = 0.015,
        flash0 = 0.25,
        flash1 = 0.31,
        plaque = 0.1,    --  the "2 TRACKS" plaque above it
        plaq0  = 0.285,
        plaq1  = 0.3,
        case   = 0.03,   --  the mechanism's case, beside the mast
        case0  = 0.1,
        case1  = 0.16,
        shaft  = 0.045,  --  how far to the side the arm pivots
    },

    build = function (p)
        return {
            parts = {
                {kind = "box", w = p.base, d = p.base, z0 = 0, z1 = p.base_z,
                 mat = arc.mat.prop},
                {kind = "box", w = p.flash, d = p.flashw, z0 = p.flash0,
                 z1 = p.flash1, mat = arc.mat.lamp},
                {kind = "box", w = p.plaque, d = p.flashw, z0 = p.plaq0,
                 z1 = p.plaq1, mat = arc.mat.prop},
                {kind = "box", ac = p.shaft, w = p.case, d = p.case,
                 z0 = p.case0, z1 = p.case1, mat = arc.mat.prop},
            },
        }
    end,
})
