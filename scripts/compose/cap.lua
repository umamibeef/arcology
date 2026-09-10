--  cap.lua -- what a lane does where its segment simply stops.
--
--  A segment that ends on open land leaves its lanes facing each other:
--  one arrives at the end, one leaves it, and without something between
--  them a car reaching the end has nowhere to be next.
--
--  The FAMILY says which kind of ending it has and this decides the
--  shape:
--
--    a line CAPS ROUND.  The lane arriving is drawn round to the lane
--      leaving, lane for lane -- the biarc between two facing poses a
--      lane's offset apart, which is a semicircle of that offset, and it
--      sits inside the round cap the loft draws there.
--    a railway REVERSES.  The train runs back the way it came, so the
--      arriving thread simply names the leaving one and no piece is drawn
--      between them.  Only the first thread does it: a terminus reverses
--      the train, it does not shunt it across the platforms.
--
--  Anything else leaves the lanes as they are, which reads as a lane
--  that stops.

arc.rules.cap = function (x)
    local d = x:info()
    for i = 0, d.n - 1 do
        local e = x:at(i)
        if d.ends == "reverse" then
            if e.lane == 0 then x:merge(i) end
        elseif d.ends == "cap" then
            x:link(i)
        end
    end
    return true
end
