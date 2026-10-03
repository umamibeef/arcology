--  memo.lua -- WHAT A UNIT OF THE COMPOSITION ANSWERED, kept for the
--  next edit's build.
--
--  An edit changes a few tiles, and most of what a build composes reads
--  none of them.  So a unit -- a node's ways, a junction's outline --
--  keeps what it read and what it answered.  The next edit's build hands
--  it the same readings, and it answers the same, without working the
--  answer out again.  One reading that differs, and the unit works its
--  answer out afresh and keeps that.
--
--  Only an edit's build reads what is kept.  A whole build -- a city
--  loaded, a script changed, a knob moved -- starts every store empty,
--  so the knobs and the scripts never have to be part of what a unit
--  compares: within an edit's build they cannot change.  tools/incr_check.py
--  holds the line, building every edit both ways and comparing them.

local M = {}

local stores = {}

--  A store for one kind of unit, kept across builds.
function M.store(name)
    local s = stores[name]
    if not s then
        s = {}
        stores[name] = s
    end
    return s
end

--  Every store emptied, as a whole build begins.
function M.reset()
    for _, s in pairs(stores) do
        for k in pairs(s) do s[k] = nil end
    end
end

--  Whether what is kept may be read: only in an edit's build.
function M.on()
    return arc.editing == true
end

--  A copy of a plain value: a number, a string, a boolean, or a table of
--  them, which may refer to itself.
function M.copy(v, seen)
    if type(v) ~= "table" then return v end
    seen = seen or {}
    if seen[v] then return seen[v] end
    local c = {}
    seen[v] = c
    for k, w in pairs(v) do c[M.copy(k, seen)] = M.copy(w, seen) end
    return c
end

--  Whether two plain values are the same, down to the sign of nought,
--  which a rule could tell apart.
function M.same(a, b, seen)
    local ta = type(a)
    if ta ~= type(b) then return false end
    if ta == "number" then
        if a ~= a then return b ~= b end
        if a == 0 and b == 0 then return 1 / a == 1 / b end
        return a == b
    end
    if ta ~= "table" then return a == b end
    seen = seen or {}
    if seen[a] == b then return true end
    seen[a] = b
    for k, v in pairs(a) do
        if not M.same(v, b[k], seen) then return false end
    end
    for k in pairs(b) do
        if a[k] == nil then return false end
    end
    return true
end

return M
