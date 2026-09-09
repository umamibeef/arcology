--  road.lua -- the road family.
--
--  A family says how one kind of line is drawn: how wide it is, what it
--  is made of, which of the loft's stages it supplies and what it builds
--  where two of its lines meet.  This is the whole of it -- there is no
--  C table behind it -- so a road drawn another way is a change here.
--
--  A stage names either one of the pipeline's own primitives or a rule
--  of this folder's; net/family.c looks the name up in the primitives
--  first and takes arc.rules[name] when none answers.

arc.family.define{
    name    = "road",
    tiles   = "road",  -- the tile family it answers for
    answers = true,    -- and it is the family that tile family means
    walk    = 0,       -- the walk visits it first, before the rail

    width = "road_w",  -- the live knobs, by name, so a strip reads the
    rmin  = "road_rmin", -- value the tuning window is showing
    rmax  = "road_rmax",
    ref_width = 0.50,  -- the width the junction outline's numbers were tuned at

    material = arc.mat.road,
    loft     = "road",
    slot     = "slot_strip", -- the carriageway, over the junction it runs into
    fit      = 0,            -- the tangent fit's family code

    junc_lift   = 0.0,
    shelf_grade = 0.25, -- the grading's ceiling on the profile's own grade

    curbs             = true, -- the outline has curb returns and hands trims back
    ramps             = true, -- a ramp may attach beside a junction
    ends_at_buildings = true, -- a building tile ends a segment with a turning head
    caps              = true, -- a dead end gets a round cap
    classed           = true, -- segments carry a class from their tiles

    lane_paint = 5.0,   -- lane wires in red
    lane_ends  = "cap", -- round the cap, lane for lane
    free_reach = 0,     -- it keeps to its own tiles
    turnout    = 0.0,   -- no turnout: its junctions hand back curb trims

    stages = {
        control   = "road_control",
        box       = "road_box",
        record    = "road_record",
        traffic   = "road_lanes",
        furniture = "road_lamps",
    },
}

--  Where the traffic runs across a road, as fractions of a tile: the
--  inner lane each way and the outer one, or the inner again where the
--  class has only one.  A STAGE IN LUA -- the name is no primitive's, so
--  it is this rule -- reading the same arc.rules.lanes the paint and the
--  connectors are laid along, which is what keeps a lane the cars run on
--  and a lane the paint marks from parting company.
function arc.rules.road_lanes(cls)
    local off = arc.rules.lanes("road", cls)
    local inner = off and off[1] or 0.0
    return inner, off and off[2] or inner
end
