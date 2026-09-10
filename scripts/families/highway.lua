--  band.lua -- the freeway slab.
--
--  The slab is walked by its own bands, not by tile family -- its tiles
--  are the line's -- so it answers for no tile family and the walk never
--  reaches it.  Only what the LOFT asks a family for is here: what the
--  strip records, whether it stands clear of the ground, how it narrows,
--  the heights along it, and where the traffic runs on it.
--
--  This is the file to copy for another way of drawing a band.  Give
--  the copy its own name, name your own rules for the stages you want to
--  answer yourself, and leave the rest naming the primitives.

arc.family.define{
    name    = "band",
    tiles   = "line", -- its tiles are the line's
    answers = false,  -- but the line, not the slab, is what a line tile means
    walk    = -1,     -- the bands reach it, not the walk

    width = "line_w",
    rmin  = "line_rmin",
    rmax  = "line_rmax",
    ref_width = 0.50,

    material = arc.mat.band,
    loft     = "slab",
    slot     = "slot_strip", -- a slab carries its own height and meets nothing on the ground
    fit      = 2,

    junc_lift   = 0.0,
    shelf_grade = 0.25,

    lips             = false,
    spurs             = false,
    ends_at_buildings = false,
    caps              = false,
    classed           = false,

    lane_paint = 7.0,     -- a slab's lane wires in light grey
    lane_ends  = "open",  -- the lanes stop
    free_reach = 0,
    turnout    = 0.0,
    slab       = true,    -- its quads carry a gore where a spur takes the outer lane, and an underside

    --  A slab files its strips in the ROAD graph -- the cars on it are
    --  the line network's -- under a class of its own, which is what
    --  keeps a slab's lanes apart from a boulevard's.
    graph        = "line",
    record_class = 3,

    --  And its lofts file their STATIONS, which is how a spur finds the
    --  slab it leaves and how high that slab stands beside it.  A
    --  structure files none: a viaduct's own slab is what a spur looks
    --  for, and its columns are not stations of it.
    stations     = true,

    stages = {
        flies   = "slab_flies",
        taper   = "slab_taper",
        profile = "slab_profile",
        traffic = "slab_lanes",
    },
}

--  Where the traffic runs across a slab, as fractions of its width: the
--  inner way's lane and the outer one.  A STAGE IN LUA -- the
--  name is no primitive's, so it is this rule -- which is what lets a
--  slab's cars be moved off the paint, or on to it, without a compile.
function arc.rules.slab_lanes(cls)
    local _ = cls
    return arc.geo.slab_traffic_in, arc.geo.slab_traffic_out
end

--  How high a slab has to stand to notch nothing.  A viaduct does not
--  grade the hillside: its columns take up the difference, and the
--  ground under and beside it keeps its shape.  A STRUCTURE always
--  stands clear, so it answers a height nothing can be below; anything
--  else stands clear once it is more than half a lift above the surface
--  beside it.  Only where the lift has tapered toward the ground -- the
--  spur cells at a band's ends -- is the slab earthworks, and shelved
--  like a line.
--
--  A STAGE IN LUA, and it answers a HEIGHT rather than a yes, so the
--  grading can compare at every station without asking again.
function arc.rules.slab_flies(structure)
    if structure then return -math.huge end
    return 0.5 * arc.geo.slab_lift
end

--  How a slab NARROWS along its length.  A spur is a strip like any
--  other, and it tapers from the slab's own half width down to the one
--  the drop was given, over the taper's length, from whichever end the
--  taper starts at.  A station already narrower than the taper asks for
--  keeps what it has: the taper only ever takes width away.
--
--  A STAGE IN LUA -- the name is no primitive's, so it is this rule --
--  and the arithmetic is done at the mesh's own precision, because a
--  width worked out to more places than the mesh can hold puts two edges
--  a few millionths apart instead of together.
local f32 = arc.put.f32

function arc.rules.slab_taper(s)
    local d = s:info()
    if not (d.taper > 0.0) or d.half <= 0.0 then return true end
    local f1 = f32(d.taper_to / d.half)
    for i = 0, d.n - 1 do
        local _, _, _, _, _, at, wl, wr = s:at(i)
        local from_end = d.taper_start and at or f32(d.len - at)
        local t = f32(from_end / d.taper)
        if t < 0.0 then t = 0.0 end
        if t > 1.0 then t = 1.0 end
        local f = f32(f1 + f32(f32(1.0 - f1) * t))
        if f < wr then wr = f end
        if f < wl then wl = f end
        s:narrow(i, wl, wr)
    end
    return true
end
