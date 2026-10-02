--  interchange -- the 2x2 interchange, and the globs of them.
--
--  An interchange tile is a NODE that band segments meet at.  A node is
--  a connected GLOB of such tiles, and its ARMS are the band edges that
--  touch its boundary.  Every arm must be able to reach every other:
--  that is what the node is for.
--
--  The parts, in the order one reads them:
--
--    nodes     every node on the map, its cells and its arm cells
--    plateau   where two movements cross, and the flat the upper needs
--    weave     which movement goes over which, and at what height
--    arms      the height each arm of a node stands at
--    slabs     the surface the movements are driven on
--
--  Which lane goes on to which across a node is compose/links's.
require "compose.interchange.nodes"
require "compose.interchange.weave"
require "compose.interchange.slabs"
