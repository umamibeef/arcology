--  car.lua -- a road car.
--
--  A body the length of the run it sits on and a cabin over the middle
--  of it.  Drawn every frame from this, so it is the shape and nothing
--  else: where a car is and which way it points is the traffic's, and
--  `phase` is its paint.

arc.model.define("car", {
    p = {
        long = 0.15,    --  the body
        wide = 0.065,
        high = 0.045,
        sit  = 0.005,   --  how far its floor stands over the lane
        back = 0.012,   --  the cabin sits this far back of the middle
        clong = 0.07,
        cwide = 0.055,
        chigh = 0.085,
        f0   = 0.27,    --  and spans this much of the run under it
        f1   = 0.73,
    },

    build = function (p)
        return {
            parts = {
                {kind = "prism", d = p.long, w = p.wide, ac = p.sit, ac2 = p.sit,
                 z0 = 0, z1 = p.high, mat = arc.mat.vehicle},
                {kind = "prism", ax = -p.back, d = p.clong, w = p.cwide,
                 ac = p.sit, ac2 = p.sit, f0 = p.f0, f1 = p.f1,
                 z0 = p.high, z1 = p.chigh, mat = arc.mat.vehicle},
            },
        }
    end,
})
