--  crossbuck.lua -- a level crossing's crossbuck (spec 6.1).
--
--  A post with two blades crossed on it, facing the road.  Each blade
--  carries its own two heights: reading one from the other moves both
--  when only one was meant to move.

arc.model.define("crossbuck", {
    p = {
        post = 0.012,  --  100 mm, square
        tall = 0.54,   --  4.3 metres
        long = 0.081,  --  the blades, 1.22 by 0.23 metres
        wide = 0.02,
        hi0  = 0.325,  --  the upper blade
        hi1  = 0.355,
        lo0  = 0.3,    --  and the lower
        lo1  = 0.33,
    },

    build = function (p)
        return {
            parts = {
                {kind = "box", w = p.post, d = p.post, z0 = 0, z1 = p.tall,
                 mat = arc.mat.prop},
                {kind = "box", w = p.long, d = p.wide, z0 = p.hi0, z1 = p.hi1,
                 mat = arc.mat.lamp},
                {kind = "box", w = p.long, d = p.wide, z0 = p.lo0, z1 = p.lo1,
                 mat = arc.mat.lamp},
            },
        }
    end,
})
