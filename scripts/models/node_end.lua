--  node_end.lua -- a mark on the fitted curve, where a fitted line ends.
--
--  The outline view draws these in place of the lines, so the shape the
--  fit actually chose can be read off the map.  `phase` is the paint the
--  vehicle material reads.

arc.model.define("node_end", {
    p = {
        size = 0.05,
        high = 0.04,
        slot = "slot_node",
        lift = "node_over",
    },

    build = function (p)
        return {
            slot = arc.geo[p.slot],
            lift = arc.geo[p.lift],
            parts = {
                {kind = "box", w = p.size, d = p.size, z0 = 0, z1 = p.high,
                 mat = arc.mat.vehicle},
            },
        }
    end,
})
