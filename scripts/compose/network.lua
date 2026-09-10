--  network.lua -- which cells form a segment, and where it runs.
--
--  This is the DISCOVERY.  The pipeline offers two readings of every
--  cell -- the links it RETURNS, which both sides agree on, and the
--  links its own art claims -- and this walks the map with them and
--  hands back the network: what counts as a node, every run of cells
--  that makes a segment, every junction, and the lone pieces no run
--  reaches.
--
--  There is no walk in C behind it.  Take this file away and the city
--  has no lines, no railways and no power lines, and the build says so
--  rather than drawing an empty map.
--
--  The list is ORDERED, and the order is the one the whole pipeline
--  reads it in: the class pass, the measuring walk and the drawing walk
--  all step through it. So a run's place in the list is its identity for
--  the rest of the build.
--
--  A run ends for one of five reasons, and they are not the same shape
--  downstream:
--
--    node   the next node: the last cell IS it
--    edge   the map's edge, leaving the last cell by `exit`
--    cut    the run outgrew what one run may hold
--    stuck  the next cell does not return the link -- a guard
--    loop   round to the cell it started from

--  The four edges, in the pipeline's own order: 0 north, 1 east,
--  2 south, 3 west, and the step each one takes.
local DC = {[0] = 0, 1, 0, -1}
local DR = {[0] = -1, 0, 1, 0}

--  How many links a mask holds, and which edge a single-link mask is.
local COUNT, ONLY = {}, {}
for m = 0, 15 do
    local n, last = 0, 0
    for e = 0, 3 do
        if m & (1 << e) ~= 0 then n = n + 1; last = e end
    end
    COUNT[m], ONLY[m] = n, last
end

arc.rules.network = function (o)
    local d = o:info()
    local n = d.size
    local links, art = o:plane()
    local cells, seen = {}, {}

    --  What makes a cell a NODE, which is what says where a run ends.  A
    --  junction piece is one whatever its neighbours return: its box is
    --  drawn and its dangling arms end at the box.  Otherwise one
    --  returned link makes an end, and anything else is a cell a run
    --  passes through.  The whole plane goes back, because every pass
    --  after this reads it.
    local node = {}
    for i = 0, n * n - 1 do
        node[i] = COUNT[art[i]] >= 3 and 2 or COUNT[links[i]] == 1 and 1 or 0
    end
    o:nodes(node)

    --  One run, from a cell out along an edge: cell by cell to the next
    --  node, and the cells and the marks it leaves behind.  Every edge
    --  the run passes over is marked, from both sides, so the same
    --  segment is never found twice.
    local function run(col, row, e)
        local cc, cr, ee, steps, k = col, row, e, 0, 0
        seen[(row * n + col) * 4 + e] = true
        cells[0] = row * n + col
        k = 1
        while true do
            cc, cr = cc + DC[ee], cr + DR[ee]
            local back = (ee + 2) % 4
            if cc < 0 or cr < 0 or cc >= n or cr >= n then
                return k, "edge", ee
            end
            local at = cr * n + cc
            local lk = links[at]
            if lk & (1 << back) == 0 then return k, "stuck", ee end
            seen[at * 4 + back] = true
            steps = steps + 1
            if k + 2 >= d.max_cells or steps > d.max_steps then
                return k, "cut", ee
            end
            if node[at] ~= 0 or COUNT[lk] ~= 2 then
                cells[k] = at
                return k + 1, "node", -1
            end
            cells[k] = at
            k = k + 1
            ee = ONLY[lk & ~(1 << back)]
            seen[at * 4 + ee] = true
            if cc == col and cr == row then return k, "loop", ee end
        end
    end

    local function keep(col, row, e)
        if seen[(row * n + col) * 4 + e] then return end
        local k, stop, exit = run(col, row, e)
        o:segment(cells, k, stop, exit)
    end

    --  From every node first, one run an edge.  A junction is handed
    --  over as it is met, because everything that shapes one -- its
    --  control, its outline, the trims it gives its arms, the box it
    --  draws -- walks that list.  A lone piece -- one whose art claims
    --  links and whose neighbours return none -- is its own short band
    --  and goes over where it stands, so the drawing meets it in the
    --  same place the runs are met.
    for row = 0, n - 1 do
        for col = 0, n - 1 do
            local at = row * n + col
            if art[at] ~= 0 then
                if node[at] == 0 then
                    if links[at] == 0 then o:island(at, false) end
                else
                    if node[at] == 2 then o:junction(at) end
                    for e = 0, 3 do
                        if links[at] & (1 << e) ~= 0 then keep(col, row, e) end
                    end
                end
            end
        end
    end

    --  Then the loops, which have no node to start from: any cell of one
    --  no run has already covered.
    for row = 0, n - 1 do
        for col = 0, n - 1 do
            local at = row * n + col
            if COUNT[links[at]] == 2 then
                for e = 0, 3 do
                    if links[at] & (1 << e) ~= 0 then keep(col, row, e) end
                end
            end
        end
    end

    --  And last the pieces whose every link leaves the map.  One of
    --  these keeps a link, so a run reaches it and comes straight back
    --  with one cell and nothing drawn; it is drawn as its own band, but
    --  only where no run covered the tile after all, which is why these
    --  come at the end.
    for row = 0, n - 1 do
        for col = 0, n - 1 do
            local at = row * n + col
            if art[at] ~= 0 and links[at] ~= 0 and node[at] ~= 2 then
                local off = true
                for e = 0, 3 do
                    if links[at] & (1 << e) ~= 0 then
                        local nc, nr = col + DC[e], row + DR[e]
                        if nc >= 0 and nr >= 0 and nc < n and nr < n then off = false end
                    end
                end
                if off then o:island(at, true) end
            end
        end
    end
    return true
end
