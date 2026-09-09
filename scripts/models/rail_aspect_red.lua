--  rail_aspect_red.lua -- a rail signal's lit aspect.
--
--  One lamp face on the signal's head, red on an absolute one at a junction.  Which
--  of the two aspects is drawn is the traffic's; the shape is the same
--  either way.

arc.model.define("rail_aspect_red", {
    p = {
        face = 0.015,  --  proud of the head
        size = 0.014,  --  the lens, half its span
        z    = 0.56,   --  the height of its middle
        code = 7,
        slot = "slot_rfurn",
    },

    build = function (p)
        return {
            slot = arc.geo[p.slot],
            parts = {
                {kind = "face", ax = p.face, w = p.size, z0 = p.z,
                 mat = arc.mat.lamp, code = p.code},
            },
        }
    end,
})
