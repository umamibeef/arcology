--  train_car.lua -- One wagon of a train.
--
--  The roof is what tells the two apart, which is why each is a model of
--  its own; which one a place in the train gets is the traffic's.

arc.model.define("train_car", {
    p = {
        len  = 0.42,
        wid  = 0.1,
        high = 0.24,
        sit  = 0.02,   --  how far its floor stands over the thread
    },

    build = function (p)
        return {
            parts = {
                {kind = "prism", d = p.len, w = p.wid, ac = p.sit, ac2 = p.sit,
                 z0 = 0, z1 = p.high, mat = arc.mat.vehicle},
            },
        }
    end,
})
