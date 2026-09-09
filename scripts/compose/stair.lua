--  stair.lua -- the points a highway band's fit is given.
--
--  A band is a run of cells, and most of them become a point of the
--  chain the fit works on.  The exception is a STAIRCASE.
--
--  The game has no diagonal highway.  It lays one as curve blocks
--  turning alternately -- left, right, left -- with a straight cell or
--  two between them, and that is what a diagonal looks like in the data.
--  Given every block as a point, the fit sees a polyline of right-angle
--  corners two tiles apart and fillets each at a tile's radius: a
--  serpentine.  Given none of them, it joins the runs at either end with
--  an L across the block beside it.
--
--  So a staircase is ONE point, at the centre of all the cells it is
--  made of.  The run before it, the diagonal through its middle and the
--  run after it are three legs, and the fit fillets the two bends
--  between them.  The coverage rule keeps the deck over the short
--  straights it skipped.
--
--  A run of blocks is a staircase when it turns at least twice and turns
--  the OTHER WAY each time.  The same way twice is a spiral, not a
--  diagonal.  And it may not step over a cell an on-ramp pins: a ramp
--  joins the deck cell it touches, so the deck has to pass over that
--  cell rather than sliding its diagonal off it.

arc.rules.stair = function (s)
    local d = s:info()
    local n = d.n

    local block = {}
    for i = 0, n - 1 do block[i] = s:block(i) end

    --  Which cells belong to which staircase.
    local stair, count = {}, 0
    local i = 0
    while i < n do
        local last = i
        if block[i] then
            local j, nb, prev = i, 0, 0
            while j < n and block[j] do
                local turn = s:turn(j)
                --  The same way again, or an end: not a stair.
                if nb > 0 and (turn == 0 or turn == prev) then break end
                prev, last, nb = turn, j, nb + 1

                --  How far to the next block, over straight cells only,
                --  and none of them pinned.
                local k = j + 1
                while k < n and not block[k] and k - j <= d.gap + 1 and not s:pinned(k) do
                    k = k + 1
                end
                --  A long straight, a pinned one, or the end.
                if k < n and block[k] and k - j - 1 <= d.gap then j = k else break end
            end
            if nb >= 2 then
                count = count + 1
                for j2 = i, last do stair[j2] = count end
            end
            i = last
        end
        i = i + 1
    end

    --  The chain: every loose cell, and one point for each staircase.
    for k = 0, n - 1 do
        if not stair[k] then
            s:point(k)
        elseif k == 0 or stair[k - 1] ~= stair[k] then
            local j = k
            while j < n and stair[j] == stair[k] do j = j + 1 end
            s:centre(k, j - 1)
        end
    end
    return true
end
