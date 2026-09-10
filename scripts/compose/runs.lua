--  runs.lua -- cutting a path's steps into the runs it is made of.
--
--  A fitted line is a chain of straight lines with arcs between them,
--  and this is where the lines come from.  The steps between the path's
--  points are walked once, and every span of them is asked what sort of
--  line it could be:
--
--    a straight -- the same step over and over, so the line is that
--                  step through the points themselves;
--    a slope    -- two perpendicular steps in a regular pattern, the
--                  minority one every `period` steps, whose line is the
--                  midline of the staircase;
--    a free line -- any chord at all, sat midway across the points it
--                  covers.  Only a slab asks for this: it rides free
--                  air and is not held to the tiles under it.
--
--  The score of a span is the steps it covers less half a point for the
--  run itself, so a slope beats the short straights inside it and one
--  long straight beats two.  A step in no run at all is a gap and costs
--  nothing, which is what lets a path bend without a line for the bend.
--  Best prefix wins, walked back at the end to name the runs in order.

--  A straight needs a straight tile inside it, so two steps.  A stair is
--  only a line once it has repeated: two full periods (spec 3.10).
local MIN_STRAIGHT = 2
local MIN_PERIODS  = 2

--  THE SLOPE REACHING FROM A STEP.
--
--  A slope is two perpendicular steps in a regular pattern: the minority
--  one every `period` steps and the majority the rest, so a 45 degree
--  stair has period 2 and a 2:1 stair period 3.  Both assignments have
--  to be tried, because a run may open with its minority step -- E,N,N,
--  E,N,N is the same stair as N,N,E,N,N,E and only one reading of it
--  starts at the first step.
--
--  Steps are named by the first step they are the same as, so the whole
--  pattern is read without comparing one vector to another.
local function slope_from(x, code, moves, ns, i)
    local a = code[i]
    if not moves[i] then return 0 end

    --  The other direction: the first step that is not this one.
    local b
    for k = i + 1, ns - 1 do
        if code[k] ~= a then
            b = code[k]
            break
        end
    end
    --  One direction only, or not a right-angle step: not a slope.
    if not b or not x:perp(a, b) then return 0 end

    local best, bper, bmaj, bmin = 0, 0, 0, 0
    for pass = 0, 1 do
        local maj = pass == 1 and b or a
        local min = pass == 1 and a or b

        --  The period is the gap between the first two minority steps.
        local first, second
        for k = i, ns - 1 do
            if code[k] == min then
                if not first then
                    first = k
                else
                    second = k
                    break
                end
            elseif code[k] ~= maj then
                break
            end
        end

        if first and second and second - first >= 2 then
            local p = second - first
            --  How far the pattern holds.
            local len = 0
            while i + len < ns do
                local want = (i + len - first) % p == 0 and min or maj
                if code[i + len] ~= want then break end
                len = len + 1
            end
            if len >= MIN_PERIODS * p and len > best then
                best, bper, bmaj, bmin = len, p, maj, min
            end
        end
    end
    return best, bper, bmaj, bmin
end

--  THE CHORD A FREE SPAN IS GIVEN.
--
--  A slab is a raised thing and rides where its corridor lets it, so its
--  line need not pass through the chain's points at all.  It is the
--  span's chord, moved across to sit midway between the outermost of the
--  points it covers -- so a line that misses every point by a little is
--  still a line.  Held to its points instead, a slab turns 45 degrees
--  one way and 31 back at a meet where the whole move is a bend of
--  fourteen.
--
--  A span that reaches the chain's own end is the chord itself, through
--  that end: moved across, it leaves a jog where a band starts.
--
--  And a span whose covered points spread wider than the band is no line
--  at all -- no single chord can carry them.
local f32 = arc.put.f32

local function chord(x, d, i, j, ns)
    local lo, hi = x:spread(i, j)
    if not lo then
        --  No covered point, or no length: the chord itself.
        lo, hi = 0.0, 0.0
    end
    if i == 0 or j == ns then lo, hi = 0.0, 0.0 end
    if f32(0.5 * f32(hi - lo)) > d.band then
        x:note(i, j, "spread")
        return false
    end
    x:chord(i, j, f32(0.5 * f32(lo + hi)))
    return true
end

arc.rules.runs = function (x)
    local d  = x:info()
    local ns = d.ns
    if ns < 1 then return true end

    --  The steps, and the slope reaching from each.
    local code, moves = {}, {}
    for k = 0, ns - 1 do code[k], moves[k] = x:step(k) end

    local slen, sper = {}, {}
    for i = 0, ns - 1 do
        local len, per, maj, min = slope_from(x, code, moves, ns, i)
        slen[i], sper[i] = len, per
        if len > 0 then x:slope(i, len, per, maj, min) end
    end

    --  best[j] is the score of the first j steps, from[j] where the run
    --  ending there began, and kind[j] what it is -- nothing, for a gap.
    local best, from, kind = {}, {}, {}
    best[0], from[0] = 0.0, -1

    for j = 1, ns do
        best[j], from[j], kind[j] = best[j - 1], j - 1, nil

        --  A straight: back as far as the step repeats.
        local i = j - 1
        while i >= 0 and moves[i] and code[i] == code[j - 1] do
            local len = j - i
            local sc  = best[i] + len - 0.5
            if len >= MIN_STRAIGHT and sc > best[j] then
                best[j], from[j], kind[j] = sc, i, 0
            end
            i = i - 1
        end

        --  A slope, where one reaches this far and has repeated enough.
        for i2 = j - 1, 0, -1 do
            local len = j - i2
            if slen[i2] >= len and len >= MIN_PERIODS * sper[i2] then
                local sc = best[i2] + len - 0.5
                if x:try(i2, j, 1) and sc > best[j] then
                    best[j], from[j], kind[j] = sc, i2, 1
                    x:keep(j)
                end
            end
        end

        --  A free line, for a slab.  The corridor sampling is the dear
        --  part of the whole cut, so a span that cannot beat what is
        --  already held is never sampled.
        if d.free then
            for i2 = j - 2, 0, -1 do
                local len = j - i2
                local sc  = best[i2] + len - 0.5
                --  A junction's mouth is left along its arm: the end
                --  line and a join, not a chord off at an angle.
                local mouth = (d.ex0 and i2 == 0) or (d.ex1 and j == ns)
                if sc > best[j] and not mouth and chord(x, d, i2, j, ns) and x:try(i2, j, 2) then
                    best[j], from[j], kind[j] = sc, i2, 2
                    x:keep(j)
                end
            end
        end
    end

    local j = ns
    while j > 0 do
        if kind[j] then x:emit(from[j], j, kind[j]) end
        j = from[j]
    end
    x:order()
    return true
end

--  The chain of lines a fitted path is made of: the start's line, every
--  run in order, and the goal's.  Two things are decided here.
--
--  A slope that owns a junction's tile arrives at its own angle rather
--  than the tile's axis, so the end is pulled onto the slope's line and
--  the junction shapes a skew arm.
--
--  An end that already lies on the line of the run beside it needs no
--  line of its own: the chain would hold the same line twice, and the
--  join stage would look for a vertex between a line and itself.

arc.rules.chain = function (c)
    local d = c:info()
    local head = d.nr > 0 and c:run(0) or nil
    local tail = d.nr > 0 and c:run(d.nr - 1) or nil

    if head and d.ex0 and head.kind == 1 and head.first then c:aim("start") end
    if tail and d.ex1 and tail.kind == 1 and tail.last  then c:aim("goal")  end

    local same0 = head ~= nil and head.first and c:on_line("start")
    local same1 = tail ~= nil and tail.last  and c:on_line("goal")

    if not same0 then c:add_end("start") end
    for i = 0, d.nr - 1 do c:add(i) end
    if not same1 then c:add_end("goal") end
    return true
end
