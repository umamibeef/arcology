--  rail.lua -- the railway family.
--
--  A line is unclassed and uncurbed: no lane markings, no sidewalks, no
--  turning head at a building.  Its junction is a TURNOUT rather than a
--  box of curb returns, so the arms' strips start a way out along each
--  straight and the box draws across the ground between them.

arc.family.define{
    name    = "rail",
    tiles   = "rail",
    answers = true,
    walk    = 1, -- the walk visits it after the road

    width = "rail_w", -- a double track's right of way
    rmin  = "rail_rmin",
    rmax  = "rail_rmax",
    ref_width = 0.62,

    material = arc.mat.rail,
    loft     = "rail",
    slot     = "slot_rail", -- the ballast and ties lie UNDER the road works the line crosses
    fit      = 1,

    junc_lift   = 0.05, -- a rail box a hair over a road's
    shelf_grade = 0.12, -- a line climbs gently

    curbs             = false,
    ramps             = false,
    ends_at_buildings = false,
    caps              = false,
    classed           = false,

    lane_paint = 1.0,         -- track wires in white
    lane_ends  = "reverse",   -- the train reverses; the arriving track names the leaving one
    free_reach = 2,           -- free ground beside the line the fit may use, tiles: a railway sweeps its corners across the field
    turnout    = 2.5,         -- a junction reaches two tiles and a half along each straight arm, its branch along its own path

    stages = {
        box       = "rail_box",
        record    = "rail_record",
        crossing  = "level_crossing",
        traffic   = "rail_traffic",
        furniture = "rail_signals",
    },
}
