--  thread.lua -- the railway family.
--
--  A line is unclassed and unliped: no lane markings, no margins, no
--  turning head at a building.  Its junction is a TURNOUT rather than a
--  box of lip returns, so the arms' strips start a way out along each
--  straight and the box draws across the ground between them.

arc.family.define{
    name    = "thread",
    tiles   = "thread",
    answers = true,
    walk    = 1, -- the walk visits it after the line

    width = "thread_w", -- a double thread's right of way
    rmin  = "thread_rmin",
    rmax  = "thread_rmax",
    ref_width = 0.62,

    material = arc.mat.thread,
    loft     = "thread",
    slot     = "slot_rail", -- the ballast and ties lie UNDER the line works the line crosses
    fit      = 1,

    junc_lift   = 0.05, -- a thread box a hair over a line's
    shelf_grade = 0.12, -- a line climbs gently

    lips             = false,
    spurs             = false,
    ends_at_buildings = false,
    caps              = false,
    classed           = false,

    lane_paint = 1.0,         -- thread wires in white
    lane_ends  = "reverse",   -- the train reverses; the arriving thread names the leaving one
    free_reach = 2,           -- free ground beside the line the fit may use, tiles: a railway sweeps its corners across the field
    turnout    = 2.5,         -- a junction reaches two tiles and a half along each straight arm, its branch along its own path

    --  A railway files its strips in the thread graph, under the one
    --  class it has.  Saying so is all it takes: the pipeline files
    --  them, and no stage of its own is needed.
    graph        = "thread",
    record_class = 0,

    --  What stands beside a railway is arc.rules.thread_marks's, and it
    --  stands them itself: no stage of the pipeline's walks them.
    props = "thread_marks",

    --  A tile a line crosses this line on carries a level meet.
    --  What one is made of is arc.rules.lap_frame's and
    --  arc.rules.lap_marks's; the pipeline only gathers it.
    meets   = true,

    --  And its junction is a set of THREADS and nothing else --
    --  no fill, no box.  Which threads it carries is
    --  arc.rules.node_threads's.
    threads      = true,

    stages = {
        traffic   = "rail_lanes",
    },
}

--  Where a train runs across the thread: the one lane arc.rules.lanes
--  gives a railway, the same both ways, because a single thread carries
--  traffic each way over the same rails.  A STAGE IN LUA -- the name is
--  no primitive's, so it is this rule -- reading the same answer the
--  connectors are laid along.
function arc.rules.rail_lanes(cls)
    local off = arc.rules.lanes("thread", cls)
    local inner = off and off[1] or 0.0
    return inner, inner
end
