--  signal.lua -- a road junction's traffic signal (spec 6.2).
--
--  A mast-arm pole at the driver's right-hand corner of the junction, an
--  arm from its top out over the road, and a three-section head hanging
--  from the arm's end with its lamps facing the approaching traffic.
--
--  It is put at the tile's middle facing the driver it is for, and steps
--  ITSELF out to the mouth and across to the corner: `at.size` is the
--  junction's half width, so the same model stands at the kerb of
--  whatever junction it is on.

arc.model.define("signal", {
    --  The signal's own measurements, in tiles across and levels up.
    p = {
        out    = 0.06,   --  clear of the junction's own corner
        post   = 0.025,  --  the mast, square
        tall   = 0.88,   --  seven metres
        arm    = 0.14,   --  how far the arm reaches over the road
        armlap = 0.02,   --  and how far it laps on to the mast
        armw   = 0.016,
        armz0  = 0.735,
        armz1  = 0.75,
        head0  = 0.6,    --  the head hangs from the arm to here
        headw  = 0.02,
        headd  = 0.023,
        face   = 0.012,  --  the lenses stand proud of the head by this
        lens   = 0.011,  --  300 mm lenses
        lens0  = 0.712,  --  the topmost, and one every `lensd` below it
        lensd  = 0.045,
    },

    build = function (p, at)
        local corner = p.out + at.size
        return {
            slot = 0,
            --  Put at the tile's middle, it walks itself out to the mouth.
            ax = -corner,
            parts = {
                {kind = "box", ac = corner, w = p.post, d = p.post,
                 z0 = 0, z1 = p.tall, mat = arc.mat.prop},
                {kind = "arm", ac = p.arm, ac2 = corner, w = p.armw, d = p.armlap,
                 z0 = p.armz0, z1 = p.armz1, mat = arc.mat.prop},
                {kind = "box", ac = p.arm, w = p.headw, d = p.headd,
                 z0 = p.head0, z1 = p.armz0, mat = arc.mat.prop},
                {kind = "lens", ax = p.face, ac = p.arm, w = p.lens,
                 z0 = p.lens0, z1 = p.lensd, mat = arc.mat.lamp},
            },
        }
    end,
})
