--  power.lua -- the power line.
--
--  Drawn tile by tile rather than walked, so nothing a strip or a
--  junction asks of a family applies to it: one stage, the tile, and
--  every flag off.

arc.family.define{
    name    = "power",
    tiles   = "power",
    answers = true,
    walk    = -1, -- the tile pass reaches it, not the walk

    width = "line_w",
    rmin  = "line_rmin",
    rmax  = "line_rmax",
    ref_width = 0.50,

    material = arc.mat.line,
    loft     = "line",
    slot     = "slot_strip",
    fit      = 0,

    shelf_grade = 0.25,
    lane_ends   = "open",

    --  No stages: a line is drawn tile by tile by the composing script
    --  (scripts/compose/world.lua), not walked, so nothing a strip or a
    --  junction asks of a family applies to it.
}
