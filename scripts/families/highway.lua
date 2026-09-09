--  highway.lua -- the freeway deck.
--
--  The deck is walked by its own bands, not by tile family -- its tiles
--  are the road's -- so it answers for no tile family and the walk never
--  reaches it.  Only what the LOFT asks a family for is here: what the
--  strip records, whether it stands clear of the ground, how it narrows,
--  the heights along it, and where the traffic runs on it.
--
--  This is the file to copy for another way of drawing a highway.  Give
--  the copy its own name, name your own rules for the stages you want to
--  answer yourself, and leave the rest naming the primitives.

arc.family.define{
    name    = "highway",
    tiles   = "road", -- its tiles are the road's
    answers = false,  -- but the road, not the deck, is what a road tile means
    walk    = -1,     -- the bands reach it, not the walk

    width = "road_w",
    rmin  = "road_rmin",
    rmax  = "road_rmax",
    ref_width = 0.50,

    material = arc.mat.hiway,
    loft     = "deck",
    slot     = "slot_strip", -- a deck carries its own height and meets nothing on the ground
    fit      = 2,

    junc_lift   = 0.0,
    shelf_grade = 0.25,

    curbs             = false,
    ramps             = false,
    ends_at_buildings = false,
    caps              = false,
    classed           = false,

    lane_paint = 7.0,     -- a deck's lane wires in light grey
    lane_ends  = "open",  -- the lanes stop
    free_reach = 0,
    turnout    = 0.0,
    deck       = true,    -- its quads carry a gore where a ramp takes the outer lane, and an underside

    stages = {
        record  = "deck_record",
        flies   = "deck_flies",
        taper   = "deck_taper",
        profile = "deck_profile",
        works   = "deck_works",
        traffic = "deck_traffic",
    },
}
