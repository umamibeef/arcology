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
        --  And what an edit's build shows once its passes are done: the
        --  pieces it changed leaving, then the pieces it made coming.
        local show = arc.rules.edit_show
        if show then
            for _, phase in ipairs({"leaving", "coming"}) do
                local e = f:edit_show(phase)
                if e then show(e) end
            end
        end
    end
    if d.moving then
        local moving = arc.rules.moving
        local b = f:moving()
        if b and moving then moving(b) end
    end
    return true
end
