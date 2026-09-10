--  line.lua -- the line family.
--
--  A family says how one kind of line is drawn: how wide it is, what it
--  is made of, which of the loft's stages it supplies and what it builds
--  where two of its lines meet.  This is the whole of it -- there is no
--  C table behind it -- so a line drawn another way is a change here.
--
--  A stage names either one of the pipeline's own primitives or a rule
--  of this folder's; net/family.c looks the name up in the primitives
--  first and takes arc.rules[name] when none answers.

arc.family.define{
    name    = "line",
    tiles   = "line",  -- the tile family it answers for
    answers = true,    -- and it is the family that tile family means
    walk    = 0,       -- the walk visits it first, before the thread

    width = "line_w",  -- the live knobs, by name, so a strip reads the
    rmin  = "line_rmin", -- value the tuning window is showing
    rmax  = "line_rmax",
    ref_width = 0.50,  -- the width the junction outline's numbers were tuned at

    material = arc.mat.line,
    loft     = "line",
    slot     = "slot_strip", -- the way, over the junction it runs into
    fit      = 0,            -- the tangent fit's family code

    junc_lift   = 0.0,
    shelf_grade = 0.25, -- the grading's ceiling on the profile's own grade

    lips             = true, -- the outline has lip returns and hands trims back
    spurs             = true, -- a spur may attach beside a junction
    ends_at_buildings = true, -- a building tile ends a segment with a turning head
    caps              = true, -- a dead end gets a round cap
    classed           = true, -- segments carry a class from their tiles

    lane_paint = 5.0,   -- lane wires in red
    lane_ends  = "cap", -- round the cap, lane for lane
    free_reach = 0,     -- it keeps to its own tiles
    turnout    = 0.0,   -- no turnout: its junctions hand back lip trims

    --  A line files its strips in the line graph, under the class the
    --  strip itself carries.  Saying so is all it takes: the pipeline
    --  files them, and no stage of its own is needed.
    graph        = "line",
    record_class = -1,

    --  Its strips are CROSSED at grade, so every station measures how
    --  far the nearest level meet is: the approach markings on a
    --  line run between one distance and another either side of one.
    crossed      = true,

    --  And each carries a MARGIN either side, laid by arc.rules.walks
    --  from the same stations the way's own edge is composed
    --  from, and filed in the walk network so a junction's corners join
    --  it.  With no rule a line has no margin at all.
    margin      = "walks",

    --  What stands beside a line is arc.rules.lamps's, and it stands
    --  them itself: no stage of the pipeline's walks them.
    props = "lamps",

    --  Its junction is a PAVED BOX: an outline with lip returns,
    --  the fill inside it laid by arc.rules.junction and the
    --  signs at its arms by arc.rules.junction_signs.
    paved       = true,

    stages = {
        control   = "node_control",
        traffic   = "road_lanes",
    },
}

--  Where the traffic runs across a line, as fractions of a tile: the
--  inner lane each way and the outer one, or the inner again where the
--  class has only one.  A STAGE IN LUA -- the name is no primitive's, so
--  it is this rule -- reading the same arc.rules.lanes the paint and the
--  connectors are laid along, which is what keeps a lane the cars run on
--  and a lane the paint marks from parting company.
function arc.rules.road_lanes(cls)
    local off = arc.rules.lanes("line", cls)
    local inner = off and off[1] or 0.0
    return inner, off and off[2] or inner
end
