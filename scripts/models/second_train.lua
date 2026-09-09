--  second_train.lua -- a level crossing's second-train sign (spec 3.15).
--
--  A post with a yellow diamond on it, one facing each of the crossing's
--  four footway corners where the line carries two tracks or more.

arc.model.define("second_train", {
    p = {
        post = 0.008,
        tall = 0.33,
        size = 0.02,   --  the diamond, half its span
        z    = 0.3,
        code = 10,     --  the face the lamp material paints
    },

    build = function (p)
        return {
            parts = {
                {kind = "box", w = p.post, d = p.post, z0 = 0, z1 = p.tall,
                 mat = arc.mat.prop},
                {kind = "face", w = p.size, z0 = p.z, mat = arc.mat.lamp,
                 code = p.code},
            },
        }
    end,
})
