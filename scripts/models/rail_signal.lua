--  rail_signal.lua -- a wayside colour-light signal (spec 5.6).
--
--  A mast with a hooded head on it, facing the train it stops.  The
--  aspect it shows is the traffic's, drawn over this from
--  rail_aspect_green or rail_aspect_red.

arc.model.define("rail_signal", {
    p = {
        post  = 0.012,
        tall  = 0.62,
        head  = 0.03,
        head0 = 0.5,    --  the head hangs from here to the mast's top
        slot  = "slot_rfurn",
    },

    build = function (p)
        return {
            slot = arc.geo[p.slot],
            parts = {
                {kind = "box", w = p.post, d = p.post, z0 = 0, z1 = p.tall,
                 mat = arc.mat.prop},
                {kind = "box", w = p.head, d = p.head, z0 = p.head0,
                 z1 = p.tall, mat = arc.mat.prop},
            },
        }
    end,
})
