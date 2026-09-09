--  stop_sign.lua -- a road junction's stop sign.
--
--  A post with an octagon on it, standing back from the junction's mouth
--  on the driver's own side of the road.  Put at the tile's middle
--  facing the driver, with `at.size` the junction's half width, it steps
--  itself out and across.

arc.model.define("stop_sign", {
    p = {
        out  = 0.32,   --  back from the junction's middle
        side = 0.03,   --  and out past the carriageway
        post = 0.008,
        tall = 0.31,   --  two and a half metres to the sign's top
        size = 0.025,  --  the octagon, half its span
        z    = 0.29,   --  the height of its middle
        code = 13,     --  the face the lamp material paints
    },

    build = function (p, at)
        return {
            ax = -(p.out + at.size),
            ac = p.side + at.size,
            parts = {
                {kind = "box", w = p.post, d = p.post, z0 = 0, z1 = p.tall,
                 mat = arc.mat.prop},
                {kind = "face", w = p.size, z0 = p.z, mat = arc.mat.lamp,
                 code = p.code},
            },
        }
    end,
})
