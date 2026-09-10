--  frame.lua -- one turn of the world.
--
--  This is the ONE DOOR: the only rule the pipeline asks for of itself.
--  Everything else a script decides, it decides under this.
--
--  A turn is one of two things.  A BUILD hands out its passes one at a
--  time -- the grading pass that lays the networks out, then the
--  building pass that notches their corridors into the world -- and each
--  is composed by arc.rules.world.  A MOVE hands out the world that
--  moves, and arc.rules.moving runs the beats its clock owes and lays
--  the geometry of what moved.
--
--  Take this file away and nothing happens at all: no world is built and
--  nothing in it stirs.

arc.rules.frame = function (f)
    local d = f:info()
    if d.build then
        local world = arc.rules.world
        local w = f:pass()
        while w do
            if world then world(w) end
            w = f:pass()
        end
    end
    if d.moving then
        local moving = arc.rules.moving
        local b = f:moving()
        if b and moving then moving(b) end
    end
    return true
end
