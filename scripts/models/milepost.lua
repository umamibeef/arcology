--  milepost.lua -- a whistle post beside the line.
--
--  A slim post two tiles before every level crossing, each way, telling
--  the driver to sound the horn.

arc.model.define("milepost", {
    p = {
        wide = 0.012,
        high = 0.19,
        slot = "slot_rfurn",
    },

    build = function (p)
        return {
            slot = arc.geo[p.slot],
            parts = {
                {kind = "box", w = p.wide, d = p.wide, z0 = 0, z1 = p.high,
                 mat = arc.mat.lamp},
            },
        }
    end,
})
