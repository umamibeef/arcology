--  path.lua -- the tangent fit walked boundary by boundary.
--
--  The fit lays a line through every run of the corridor, and then has
--  to get from each line on to the next.  There are three ways: an arc
--  at the meet, a biarc between them where they are parallel, and
--  the join walked tile by tile, which always works and always looks
--  like it.  Which to try, and in what order, is arc.rules.join's; what
--  lies after the far line, for the budget the join is given, is
--  arc.rules.after's.
--
--  Everything the fit MEASURES is the pipeline's -- the corridor, the
--  lines through it, the arcs and the biarcs -- and everything it
--  DECIDES is here.
local function stage(name, o)
    local fn = name and arc.rules[name]
    if fn then fn(o) end
end

arc.rules.path = function (p)
    local after, join = arc.rules.after, arc.rules.join
    --  The runs the corridor lets be straight, and the chain of lines
    --  they become with the ends the fit starts and finishes at.
    stage(p:runs())
    stage(p:chain())
    local n = p:lined()
    for k = 0, n - 1 do
        local a, j = p:pair(k)
        if a then
            if a.has_after and after then p:after_is(after(a)) end
            local how = join and join(j)
            for i = 1, how and #how or 0 do
                --  Each way is a reading of the boundary handed to a
                --  rule of its own; the first that holds wins it.
                local name, o = p:try(how[i])
                local fn      = name and arc.rules[name]
                if fn then fn(o) end
                if p:held() then break end
            end
        end
    end
    --  And the path it came to: the idle vertices dropped, and the
    --  radius at each of the rest.
    stage(p:ending())
end
