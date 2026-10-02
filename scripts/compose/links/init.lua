--  links -- where a band's lane goes when its slab runs out.
--
--  The pipeline offers the lanes as they stand and lays what it is told
--  to lay.  Which lane goes on to which is decided here, and nowhere
--  else.
--
--  The parts, in the order the rule runs them:
--
--    chain    the chain between two poses, and the arithmetic of it
--    census   which interchange node each open lane end stands at
--    node     what one node owes, and who gets a lane for it
--    anneal   where each of a node's movements runs
--    match    every end the nodes did not spend
--
--  A fault is said at ERROR level where the pipeline offers one, and
--  through the plain log where it does not: a script that calls a name
--  the running program has never heard of faults the rule and abandons
--  the build.  So what is only sometimes there is reached through a
--  local and never by name.
require "compose.links.chain"

local census = require "compose.links.census"
local node   = require "compose.links.node"
local match  = require "compose.links.match"

arc.rules.links = function (x)
    local d = x:info()
    local n = d.n
    local nodes, at_node, size, turns = census.take(x, n)
    for k, g in ipairs(at_node) do
        node.plan(x, {n = n, nodes = nodes, size = size, turns = turns}, g, k)
    end
    match.run(x, d, n)
    return true
end
