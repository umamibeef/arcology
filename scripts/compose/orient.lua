--  orient.lua -- which side of an on-ramp's tile is the deck, and which
--  the road.
--
--  A ramp is one tile with four neighbours, and everything the ramp
--  becomes follows from reading them: it climbs to a DECK on one side
--  and comes down onto a ROAD on another.  The data says neither; it
--  only says what each neighbouring tile is.
--
--  The deck side is a deck tile whose band runs along that side's own
--  axis -- a deck to the north whose band runs east-west is the one this
--  ramp climbs.  A deck tile that fails that test is a deck met END-ON:
--  the band begins at this ramp rather than passing it.
--
--  The ramp's road is the one ALONG the deck's axis when there is one.  A
--  road on the side opposite the deck runs parallel to the deck and is
--  the ramp's only when nothing else is; taking the first road met
--  instead leaves the lane leaving from an edge with no road on it.

arc.rules.ramp_orient = function (o)
    local dside, rside, eside, roads

    for k = 1, 4 do
        local deck, axis, road = o:side(k)
        if deck and axis and not dside then
            dside = k
        elseif deck and not eside then
            --  A deck met end-on: the band begins here.
            eside = k
        elseif road then
            roads = (roads or 0) + 1
            if not rside then rside = k end
        end
    end

    if not dside then
        --  No band beside it at all, unless one ends here.
        o:answer(eside and 2 or 0, dside, rside, eside, roads)
        return true
    end

    --  Prefer a road across the deck's axis to one opposite the deck.
    if rside then
        local back = (dside + 1) % 4 + 1
        for k = 1, 4 do
            if k ~= dside and k ~= back then
                local _, _, road = o:side(k)
                if road then
                    rside = k
                    break
                end
            end
        end
    end

    o:answer(1, dside, rside, eside, roads)
    return true
end
