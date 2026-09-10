--  street_lamp.lua -- a line's cobra-head luminaire (spec 1.6, 6.4).
--
--  A pole on the margin with a davit arm in over the line and the head
--  hung at its end.  It stands on the strip's own height, not on the
--  ground under the lip, and every piece is cut on the tile folds since
--  the arm reaches into the next tile.

arc.model.define("street_lamp", {
    p = {
        post   = 0.02,    --  the pole, round
        tall   = 1.35,    --  ten metres
        arm    = 0.2,     --  the davit arm, three metres over the line
        armw   = 0.014,
        armz   = 1.33,
        rise   = 0.03,    --  how far the arm lifts as it reaches in
        head   = 0.055,
        headw  = 0.025,
        headz  = 1.3,
        headz1 = 1.345,
        headat = 0.19,    --  where along the arm the head hangs
        slot   = "slot_furn",
    },

    build = function (p)
        return {
            slot = arc.geo[p.slot],
            parts = {
                {kind = "prism", d = p.post, w = p.post, z1 = p.tall,
                 mat = arc.mat.prop},
                {kind = "prism", ax = p.arm * 0.5, ac2 = p.rise, d = p.arm,
                 w = p.armw, z0 = p.armz, z1 = p.tall, mat = arc.mat.prop},
                {kind = "prism", ax = p.headat, ac = p.rise, ac2 = p.rise,
                 d = p.head, w = p.headw, z0 = p.headz, z1 = p.headz1,
                 mat = arc.mat.prop},
            },
        }
    end,
})
