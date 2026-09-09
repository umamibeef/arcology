--  incr.lua -- how far an edit reaches.
--
--  An edit changes a few tiles.  The mesh is kept in chunks, so only the
--  chunks whose geometry changed are built again, and which those are is
--  a CLOSURE over what depends on what: a segment through the edit's
--  neighbourhood has a new fit, a junction at its end has a new box, a
--  band near it has a new fit, a ramp beside a changed tile has a new
--  join.  These are the distances that closure reaches over, in tiles.
--
--  They are a floor, not a fit.  Too far costs build time and draws the
--  same thing twice; too NEAR leaves the last build's triangles standing
--  in a chunk that should have been redrawn, and nothing in the build
--  says so -- which is what ctest `incremental_rebuild` exists to catch.
--
--  At the chunk size the mesh uses, most of these are swallowed whole: a
--  margin of one tile or two only changes the answer where it crosses a
--  chunk boundary.  They earn their keep as the chunk gets smaller.

arc.numbers("incr_reach", {
    --  How far out a band's free-air check reads, so a band that far
    --  from the edit has a new fit.
    band_fit = 2,

    --  A ramp this near a changed tile has a new join, and the band
    --  it climbs to carries the lane drop.
    ramp = 2,

    --  A hot band whose fit came out as before changes geometry only
    --  where the ground under it moved -- its profile follows the
    --  ground -- or where a lane drop did.
    band_ground = 8,
    band_ramp   = 6,

    --  The margin round a band tile and round a segment's tiles that
    --  is drawn again with them.
    band_margin = 2,
    segment     = 1,
})
