--  geo.lua -- every number the line works are built from.
--
--  This file holds them all.  There is no list in C: a number exists
--  from the moment this file names it, the C that reads one names it
--  back, and a name nothing sets reads as zero.
--
--  A script may also make numbers of its own simply by naming them; the
--  models use those the same way (arc.model, scripts/models.lua).

arc.geo.blink_duty         = 0.3
--  The share of a signal's cycle one group of arms holds green.  The two
--  groups take the cycle in turn, so what is left over at each changeover
--  is the all-red clearance: at 0.45 that is nine seconds of green and one
--  of clearance in a twenty second cycle.  It must stay under 0.5, or the
--  two groups are green together.
arc.geo.signal_green       = 0.45
--  How nearly the two arms of a junction must agree before a car crosses
--  it straight through instead of bending round the corner: about
--  forty-five degrees.
arc.geo.box_straight_dot   = 0.7
arc.geo.car_creep          = 0.02
arc.geo.car_density        = 1.5
arc.geo.car_gap_free       = 0.8
arc.geo.car_gap_stop       = 0.42
arc.geo.car_len            = 0.556
arc.geo.car_probe          = 0.075
arc.geo.car_stop_hold      = 0.45
arc.geo.car_stop_junc      = 0.47
arc.geo.cross_deep         = 0.2
arc.geo.cross_par          = 0.985
arc.geo.slab_underside     = 0
arc.geo.slab_lane0         = 0.255
arc.geo.slab_lane1         = 0.55
arc.geo.slab_lane2         = 0.82
--  And where its CARS run, which is not where its paint is: the two
--  numbers the slab's traffic stage answers with.
arc.geo.slab_traffic_in    = 0.185
arc.geo.slab_traffic_out   = 0.681
--  How far into a tile a slab's point must lie before the cell reads as
--  east-west rather than north-south, and how nearly two spurs must lie
--  on one line along a slab to be a pair.
arc.geo.slab_cell_axis     = 0.25
arc.geo.slab_pair_line     = 0.5
--  How long a level meet's gate arm is: it reaches across the
--  line it stops, so a line that carries more lanes needs a longer
--  one.  Which lines are which is arc.rules.line_class's.
arc.geo.gate_arm           = 0.29
arc.geo.gate_arm_wide      = 0.36
arc.geo.gate_arm_d         = 0.02
arc.geo.gate_arm_w         = 0.01
arc.geo.gate_pivot         = 0.13
arc.geo.gate_raise         = 30
arc.geo.gate_rate          = 44
arc.geo.gate_rise          = 0.53
arc.geo.gate_shaft         = 0.045
arc.geo.gate_up            = 88
arc.geo.gate_warn          = 8
arc.geo.gate_watch         = 0.6
arc.geo.junc_arc           = 16
arc.geo.junc_lip          = 0.12
arc.geo.junc_far           = 0.85
arc.geo.junc_flat          = 2.9
arc.geo.junc_inset         = 0.01
arc.geo.junc_near          = 0.2
arc.geo.junc_sharp         = 0.2
arc.geo.junc_tan           = 0.005
arc.geo.junc_walk          = 0.2
arc.geo.lamp_every         = 2
arc.geo.lamp_first         = 1.25
arc.geo.lamp_in            = 0.9
arc.geo.lamp_min           = 2.5
arc.geo.lane_aim           = 0.99
arc.geo.lane_av_in         = 0.115
arc.geo.lane_av_out        = 0.306
arc.geo.lane_bd_in         = 0.162
arc.geo.lane_bd_out        = 0.317
--  Where one band's slab lane carries on into the next band's.
--  The window is wide -- six tiles of reach, four aside, and a lane end
--  a little BEHIND still counts -- because two bands meeting at an
--  interchange are not neatly end to end.  It is deliberately wider than
--  a continuation really is: the router is asked to build each candidate
--  before it is taken, and what it refuses is not a continuation.
arc.geo.band_ahead         = -0.6
arc.geo.band_apart         = 0.05
arc.geo.band_aside         = 4.0
arc.geo.band_dot           = -0.2
arc.geo.band_off           = 0.05
arc.geo.band_reach         = 6.0
--  A continuation's far end must lead AWAY from the near one by at
--  least this much, along the way the traffic travels.  At nought a
--  band that turns a corner stops matching its own lanes across the
--  turn, which is a U-turn and not a continuation at all.
arc.geo.band_lead          = 0.0

--  Where a band's six lanes become a line's two.  The inner lane routes
--  into the line; the middle and outer taper into the inner, the outer
--  starting further back so the two tapers do not cross.  A lane counts
--  as the outer one when it sits more than 0.7 of its own width off the
--  centreline, and a band needs the taper's length and two tiles more
--  before it is worth tapering at all.
arc.geo.band_abreast       = 2.5
arc.geo.band_outer         = 0.7
arc.geo.band_line_dot      = 0.7
arc.geo.band_taper_far     = 3.0
arc.geo.band_taper_gap     = 1.5
arc.geo.band_taper_near    = 1.5
arc.geo.band_taper_room    = 2.0

--  A spur.  The outer lane sits at 0.85 of the slab's half width; a
--  spur end snaps to a recorded lane within 0.35; a through line is met
--  thirty degrees off its line, as a cosine and a sine so the pair is
--  exactly what the geometry uses; the line's own lanes run a quarter of
--  its width off the centreline; and the merge sits most of half a tile
--  along the line from the foot.
arc.geo.spur_lane_off      = 0.25
arc.geo.spur_meet_cos      = 0.866
arc.geo.spur_meet_sin      = 0.5
arc.geo.spur_merge_along   = 0.45
arc.geo.spur_outer         = 0.85
arc.geo.spur_snap          = 0.35
arc.geo.spur_taper         = 0.6
--  A RAMP MERGES.  The slab keeps its lanes, and the ramp's lane runs
--  beside it as a MERGE LANE for ramp_merge tiles from the gore, joined
--  to the outer lane over ramp_taper tiles at its far end, where the
--  slab's edge runs out to take it; ramp_lane_w is the lane's width as
--  a fraction of the slab's half width.  The far end keeps ramp_clear
--  tiles from an interchange's arm, and the lane is shortened to no
--  less than ramp_merge_min to keep clear (scripts/compose/drop.lua).
arc.geo.ramp_merge         = 2.0
arc.geo.ramp_taper         = 1.0
arc.geo.ramp_merge_min     = 1.0
arc.geo.ramp_lane_w        = 0.30
arc.geo.ramp_clear         = 4.0
--  How far a ramp's lane may run on beside the slab past its tiles'
--  top, climbing, so the lane holds the grade (arc.geo.interchange_grade)
--  where the tiles alone are too short for the height it climbs, and
--  only as far as the room beside the slab allows (a road, a wire or
--  a building beside the highway stops it).  0 keeps the climb to the
--  tiles the map gave the ramp.
arc.geo.ramp_climb         = 6.0
--  The gap a ramp's lane keeps from the next ramp's tiles on the same
--  side of the slab, in tiles; two ramps whose lanes run toward each
--  other share the room between them.
arc.geo.ramp_gap           = 1.0
--  How near a station must be to a tile's centre to count as standing AT
--  the level meet on it.  The strip is sampled far more finely than
--  a tile, so this picks the one station that is really on the meet
--  rather than the several that pass over its tile.
arc.geo.lap_centre    = 0.07

--  How far from a tile's own middle the line's and the railway's
--  centrelines may meet and still be taken as the middle of the
--  meet's panel.  Further off than this and the tile's middle is
--  used instead: two lines that nearly parallel meet a long way away,
--  and a panel laid there is not on the meet at all.
arc.geo.lap_point     = 0.6

--  The block a thread signal protects: how far BEHIND it a train still
--  holds it -- a train whose nose has just passed still occupies it --
--  and how far ahead of it the block runs.
arc.geo.rail_block_ahead   = 10.0
arc.geo.rail_block_back    = 0.6

--  Which way a run leaves a junction.
--
--  A piece long enough to have a heading of its own gives its tangent
--  straight: a fillet's straights and swept arcs are a good fraction of a
--  tile, and the tangent at the start of one IS where the line goes.
--
--  A piece that is one dense sample gives a tangent taken from a few
--  hundredths of a tile, which points wherever that fragment happens
--  to.  A
--  run leaving diagonally can report due west, the junction trims its arm
--  back on the strength of it, and the line ends up under terrain the pad
--  does not cover.  Those are measured half a tile along the run instead
--  -- far enough that the run means it, and taken as the TANGENT there
--  rather than the chord to there, since a chord over half a tile turns a
--  curving approach into a straight one and moves the junction with it.
arc.geo.arm_own            = 0.25
arc.geo.arm_base           = 0.5

--  The shelf a corridor cuts for itself out of the terrain.
--
--  A corner of a tile takes its height from the centreline, and how far
--  ALONG the line it may reach for that height is capped: past three
--  quarters of a tile it would take the grade of a part of the line it
--  does not touch.
--
--  The shelf reaches a little past the band's own edge, and the ring
--  beyond that is the BATTER -- half way back to the hillside -- so the
--  shelf blends out instead of standing on one wall of its full depth.
arc.geo.shelf_along        = 0.75
arc.geo.shelf_batter       = 0.3
arc.geo.shelf_reach        = 0.8

--  A line's class, from the traffic the simulation counted on its tile.
--  Under the first it is a local line, one lane each way; at the first an
--  avenue, with a centre line; at the second a boulevard.  The count is
--  the sim's own byte, 0 to 255, read at half resolution.
arc.geo.class_avenue       = 64
arc.geo.class_boulevard    = 160
--  And how busy a junction's own tile must be before its control counts
--  it a busy one: an all-way stop between two avenues becomes a signal.
arc.geo.junction_busy      = 170

--  How far inside its tile a lone piece's ends sit.  A hair, so the end
--  stations read that tile's own surface and not the neighbour's.
arc.geo.tile_inset         = 0.49
--  How far out from the centreline a dead end's lip runs, as a fraction
--  of the strip's half width: inside the way's own edge.
arc.geo.cap_lip           = 0.9
--  The most of an arm's line a meet may take.  Two meets must
--  still leave a line between the junctions they belong to.
arc.geo.cross_share        = 0.45

--  How nearly a lane must run the way asked for to be the lane meant.
--  Half is a right angle's worth of slack: a lane at more than sixty
--  degrees to the direction wanted is a different lane, not this one seen
--  askew.
arc.geo.lane_pick_dot      = 0.5

arc.geo.lane_cross_ahead   = 0.2
arc.geo.lane_cross_aside   = 0.3
arc.geo.lane_cross_dot     = 0.9
arc.geo.lane_cross_off     = 0.03
arc.geo.lane_cross_reach   = 2.0
arc.geo.lane_cross_spot    = 0.05
arc.geo.lane_edge          = 0.6
arc.geo.lane_join          = 0.03
arc.geo.lane_lift          = 0.08
arc.geo.lane_reach         = 1.2
arc.geo.lane_rmin          = 0.25
arc.geo.lane_line          = 0.2
--  A connector's two inner vertices are limited by their tangent alone,
--  so the cap is only there to keep an all but straight one finite.
--  Lowering it holds every connector to a tighter arc than the tangent
--  would allow, which reads as a squarer junction.
arc.geo.lane_route_rmax    = 1000000.0
--  The tightest an arc may become once it is offset across to a lane.
--  The inner lane of a sharp turn offsets toward the centre, and without
--  a floor it would pass through it and turn inside out.
arc.geo.lane_arc_min       = 0.02
arc.geo.lane_step_arc      = 0.08
arc.geo.lane_step_spur     = 0.1
arc.geo.lane_step_run      = 0.25
arc.geo.lane_wire          = 0.08
--  THE FIT'S OWN TOLERANCES.  These do not shape a curve directly; they
--  decide whether one is accepted, so they show up as shape all the
--  same.
--
--  How far inside the band's edge the corridor is sampled: a band is
--  refused where its edge lands on a tile it may not have, and sampling
--  the edge itself refuses it wherever it merely touches one.
arc.geo.fit_edge           = 0.002
--  And the same for a slab, whose corridor is the air beside it rather
--  than its own tiles, so it may sample further in.
arc.geo.fit_edge_slab      = 0.05
--  How closely the corridor is sampled along a straight and round an
--  arc.  Coarser is faster and lets a band cut a corner it should not.
arc.geo.fit_probe_run      = 0.25
arc.geo.fit_probe_arc      = 0.05
--  How nearly two runs must line up before the corner between them is
--  no corner at all.  A vertex under this gets no arc and no straight of
--  its own: the path runs through it.
arc.geo.fit_straight_dot   = 0.9999
--  The straight reserved at an end that meets no junction.  A junction's
--  own approach is arc.tune.approach; this is what a free end keeps.
arc.geo.fit_free_end       = 0.05
arc.geo.lift_xpanel        = 0.045
arc.geo.loft_cut           = 0.015
arc.geo.loft_dip           = 0.005
arc.geo.loft_lift          = 0.03
arc.geo.loft_lift_min      = 0.02
arc.geo.loft_step_arc      = 0.04
arc.geo.loft_step_run      = 0.0625
--  THE SLAB'S OWN PARTS.
--  The parapet's thickness, and how far beyond its edges a slab reads
--  the ground it stands over.
--  How far a slab rides over the ground it crosses, in levels of
--  altitude.  The 2x2 interchange's own slab is invented from the map
--  (scripts/compose/invent.lua) and has to meet the bands that arrive on
--  it, so this is the height those stand at.
arc.geo.slab_lift          = 1.0
--  Say what every interchange node did: its tiles, its arms, the lane
--  ends that reached it and the movements it served.  Off by default,
--  because it is one line a node on every build; a movement it could NOT
--  serve is reported whatever this says.
arc.geo.interchange_dump      = 0
arc.geo.pose_dump          = 0    -- the gores' slab readings and the branches from them

--  WHAT A LANE MUST BE for a car to drive it, checked over every piece
--  of every lane (net/lane.c lane_check_curves).  The shortest piece
--  that is a real piece rather than a hair; how far two pieces may end
--  and start apart before the line is broken; and how nearly their
--  headings must agree before the join is a corner rather than a curve.
arc.geo.lane_min_len       = 0.02
arc.geo.lane_join_gap      = 0.002
arc.geo.lane_join_dot      = 0.999

--  THE FLOW ARROWS on the outline view: a chevron along each lane every
--  so many tiles, its half length and how far its tails spread.  The way
--  a lane runs is otherwise unreadable -- a hairline looks the same from
--  both ends -- so this is what tells a reader which way the traffic
--  goes through a junction or round an interchange.
arc.geo.lane_arrow_every   = 2.0
arc.geo.lane_arrow_len     = 0.16
arc.geo.lane_arrow_wing    = 0.10

--  Half the width of one movement's way across an interchange: one lane
--  of the slab, and a hair under it.  A slab's lane ends stand about
--  0.17 of a tile apart where they leave the slab, and two ways out of
--  one arm run abreast from lanes side by side, so a way wider than
--  that pitch overlaps the next one's and the two pass through each
--  other wherever their heights differ.
arc.geo.interchange_lane_w = 0.07
--  And the depth of the girder under it, in tiles: the slab's own is
--  0.11, and a one-lane way carries a lighter one.
arc.geo.interchange_girder = 0.08

--  THE KEEP-OUT.  Over every surface a car drives on stands an
--  invisible tunnel a car's height tall, in levels of altitude, that
--  nothing may be built into: where one movement crosses over another
--  its girder's underside stands that far above the lower way's
--  surface.  The whole of what makes a node an interchange rather than
--  a crossing.  A crossing the grade cannot lift that far is lifted as
--  far as it can and said; one it cannot lift even interchange_over_min
--  is not lifted at all, since two ways that close are one surface, and
--  it is said too.
arc.geo.keep_out              = 0.30
--  HOW FAR FROM A NODE A LANE END MAY STAND and still be an end AT
--  that node.  A slab runs half a tile past its last cell and its
--  lanes sit up to a half width across that, so a true end lands
--  within about three tiles of the nearest cell of the glob.  An end
--  further out is not at the node, whatever any store says of it: the
--  band simply ends there, and a movement invented to it would reach
--  across the map to a way it never meets.
arc.geo.interchange_catchment = 4.0

arc.geo.interchange_over      = arc.geo.keep_out + arc.geo.interchange_girder
arc.geo.interchange_over_min  = 0.20
--  The steepest a movement may climb or fall, in levels of altitude per
--  tile.  A node's ways are as short as the map's tiles make them, and
--  a way has to lift a car's keep-out over what it crosses within that
--  length: at forty in a hundred every crossing in a node two tiles
--  across is lifted, at fifteen most are not.  A ramp's tiles are as
--  steep.  0 takes the slab's own grade (arc.tune.band_grade) instead.
arc.geo.interchange_grade     = 0.40
--  How far the arms of an interchange may be RAISED above the highest
--  of their floors (the slabs' envelopes where they arrive), in levels
--  of altitude, so a way between them clears the ground it crosses.
--  The arms are settled together in the grading pass, and rise only as
--  far as a way asks (scripts/compose/interchange).
arc.geo.interchange_rise      = 1.0
--  How far off an arm's line the way the grading pass lays between two
--  arms runs, in tiles, on the traffic's side: a slab lane's offset
--  from the spine, so the ways stand where the lanes' ways will.
arc.geo.interchange_lane_off  = 0.33
--  How many cells short of an interchange a band STOPS, so that its
--  lanes end there and the node's ways have those cells to climb in.
--  A way that must cross another a level higher needs the run to get
--  there, and a node four tiles across has none to spare: the approach
--  is where an interchange's ramps part from the deck, and here they
--  do.  0 runs the slab to the node's edge.
arc.geo.interchange_approach  = 2
--  THE FAN: at an arm each lane ends its own distance short of the
--  band's end, this many tiles per lane from the inside out -- the
--  inner lane at the end, the middle lane this far before it, the outer
--  twice as far.  A way that leaves from further back is longer, and a
--  longer way has more run to climb over what it crosses; the outer
--  lane, which turns off the arm, gains the most.  0 ends every lane at
--  the band's end.
arc.geo.interchange_fan       = 0.5
--  THE LANE DROP behind a lane's end: the slab's edge tapers to the next
--  lane line over this many tiles toward the node, never more than a
--  step of the fan, and the lane's way runs on that taper, level, before
--  its own ribbon begins at the nose.  Nought is a step: the way's
--  ribbon begins at the lane's end, as wide as the lane, and narrows to
--  a ramp's width over its lead (scripts/compose/interchange).
arc.geo.interchange_gore      = 0

--  THE NOSE past a lane's end: a way leaves the slab as wide as its
--  lane, its edge touching the ribbon beside it, and once that one has
--  parted the way narrows to a ramp's width over this many tiles.
arc.geo.interchange_nose      = 0.5

--  Whether a segment's end at a junction takes its pose from the pose
--  store -- the node's middle, along the arm's axis -- rather than the
--  fit's own end.  1 does, 0 leaves the fit to its own.
arc.geo.pose_mouths           = 1

--  How far a band's end is carried on toward the node it meets, along
--  the line from that end to the middle of the node's mass, in tiles.
--  The end's line and the run behind it then meet a tile back, and the
--  slab's last stretch runs at the node's own angle; carried nowhere,
--  the aimed line passes through the end and bends nothing.
arc.geo.interchange_aim       = 1.0

--  A movement whose two end lines meet is a CORNER and is laid as one
--  arc: the lines must cross at more than this sine of the angle between
--  them (about ten degrees), and the meet must lie at least this far
--  ahead of the one pose and behind the other, in tiles.
arc.geo.interchange_corner_sin = 0.17
arc.geo.interchange_corner_leg = 0.3

--  How far a movement runs STRAIGHT off the slab before it bends, and
--  straight into the slab it reaches after its last bend, in tiles.  A
--  left turn that bends at once crosses its own arm's opposing lanes
--  within the first half tile, where no ramp has climbed anything; a
--  tile of straight run puts that crossing where the grade has bought
--  the clearance.  The fit rounds the corner at the lead's end with a
--  tangent of up to half the lead.
arc.geo.interchange_lead      = 1.0

--  WHERE THE WAYS RUN is searched for, not taken from a router.  Each
--  movement is a chain the script builds: its straight leads, and a
--  middle waypoint bowed sideways from the chord by a BULGE, out from
--  the node's middle.  The bulges are annealed: this many trials per
--  node, each moving one way's bulge by a step that shrinks as the
--  search cools, kept when the node weaves better -- fewer crossings
--  that cannot be lifted at the grade, less climb, no ground it may
--  not cross -- and sometimes when it does not, while the search is
--  hot.  0 trials lays every way on its chord.  The search is seeded
--  by the node's own cells, so a build is the same build every time.
arc.geo.interchange_anneal      = 600
arc.geo.interchange_anneal_heat = 3.0
arc.geo.interchange_anneal_step = 0.3
arc.geo.interchange_bulge       = 2.5
--  What one change of a way's turning sign costs the search, in the
--  same coin as a radian of turning beyond the unavoidable: none is the
--  ideal, one is borne, and two is the most a way may carry.
arc.geo.interchange_sign_cost   = 0.5
arc.geo.interchange_bulge_in    = 0.5

--  A way's VERTICAL CURVES: how many passes of rounding its profile
--  takes, each a knot averaged with its neighbours a quarter tile
--  either side, and how far either side of a crossing the way it is
--  lifted over is held flat, so the rounding leaves the clearance whole.
arc.geo.interchange_round     = 3
arc.geo.interchange_crest     = 0.5
--  LEVELS AS PLATEAUS (1) or lifts at each crossing (0).  As plateaus,
--  a way leaves its deck, climbs once at the grade to its level, holds
--  it and descends once: the levels are whole steps of interchange_over
--  above the upper deck and below the lower one, and a through way is
--  flat at its own deck.  The two decks stand the gap apart.
arc.geo.interchange_plateau   = 1

--  How far back along a slab its grade is read where a way leaves it,
--  so the way leaves at that grade and the profile has no crease.
arc.geo.interchange_tangent   = 0.25

--  How closely a movement is sampled when the crossings are looked for.
arc.geo.interchange_sample = 0.1

--  How far from a 2x2 interchange's middle a lane end still belongs to
--  it: the block is two tiles across and a band runs its slab out to the
--  edge, so an end of one lies about a tile and a half out.
arc.geo.interchange_reach  = 1.75

--  How wide a movement's turns sweep, in tiles.  A movement is three
--  corners at most -- off the slab, at its bowed middle, onto the far
--  slab -- so its curvature changes sign twice at most: a slight turn,
--  one big circle, a slight turn back.  Each corner may sweep this
--  radius but no more than half its legs allow, so with a radius wider
--  than any leg the arcs take the whole of their legs and meet, and the
--  way is one continuous sweep with no straight between tight corners.
arc.geo.interchange_radius = 12.0
--  And the tightest arc a movement may carry.  A bowed midpoint on a
--  short chord bends the middle corner through most of a half turn on a
--  radius of nothing -- a hairpin, two sign changes and all -- so a
--  chain the fit cuts with an arc under this is no way the search may
--  keep.  The way on its chord is laid whatever its arcs, so no
--  movement is lost to it.
arc.geo.interchange_rmin   = 0.5

--  How far one movement across an interchange is stacked over the one
--  before it.  Two movements of a node cross, and two ribbons at one
--  height are one surface drawn twice: the depth test then settles them
--  differently from pixel to pixel and the node speckles.  A driver
--  crossing on one goes over or under the other, so the step is what the
--  world does, taken as small as the mesh can hold and still tell two
--  surfaces apart.
arc.geo.interchange_stack  = 0.006

arc.geo.slab_parapet       = 0.035
arc.geo.slab_ground_margin = 0.06
--  How far back from a tile's middle a head-on spur starts its climb,
--  and how closely a spur's lane is sampled along its own pieces.
arc.geo.spur_head_back     = 0.47
arc.geo.spur_probe_step    = 0.05
--  THE JUNCTION AND ITS MARGIN.
--  The turn past which a ring's corner is reported a spur rather than a
--  lip return.  It decides what counts as a fault, not what is drawn.
arc.geo.junc_spur_angle    = 150.0
--  How much wider than the box an avenue's mouth may be before the
--  margin stops treating the two as the same edge.
arc.geo.walk_mouth_eps     = 0.12
--  The margin carried across a strip's own end, as a share of the
--  line's half width.  A junction's is arc.geo.junc_walk; this is the
--  cap at an end with no junction, which is a different question.
arc.geo.walk_cap_w         = 0.2
arc.geo.node_high          = 0.04
arc.geo.node_lift          = 0.08
arc.geo.node_over          = 0.1
arc.geo.node_wide          = 0.05
--  THE MESH'S OWN WELD.
--  The grid two vertices must land on together to be welded into one: a
--  four-thousandth of a tile.  Coarser welds seams that should stay
--  apart; finer leaves a crack the eye catches as a dark line.
arc.geo.weld_grid          = 4096.0
--  And the coarser grid a simplified edge's ends are welded on, since a
--  cut piece's end is an interpolation rather than a vertex it stands
--  for, with the tolerance the edge itself is simplified to.  The
--  tolerance is what turns a thousand stations along a band into the few
--  edges the eye can tell apart.
arc.geo.thin_grid          = 1024.0
arc.geo.thin_tol           = 0.02
--  How near an edge's own end a T-junction split is refused, as a share
--  of the edge.  A split at the very end is the end.
arc.geo.weld_split_end     = 0.002
--  How far a round foot sinks into the ground it stands on, so its rim
--  is never left hanging over a slope.
arc.geo.foot_sink          = 0.01
--  A wire's half width across.
arc.geo.wire_w             = 0.02
--  How far either side of a split station the ground is read.  The two
--  readings are what let a strip stand on the higher of two surfaces
--  that meet under it.
arc.geo.loft_split_probe   = 0.004
arc.geo.rail_gauge         = 0.005
arc.geo.rail_lane          = 0.133
arc.geo.rail_thru          = 0.02
arc.geo.rail_thru2         = 0.03
arc.geo.rmark_end          = 0.3
arc.geo.rmark_out          = 0.3
arc.geo.rsig_every         = 10
arc.geo.rsig_first         = 5
arc.geo.rsig_out           = 0.33
arc.geo.slot_crosswalk     = 0.002
arc.geo.slot_furn          = 0.3
arc.geo.slot_junction      = 0.001
arc.geo.slot_lane          = 0.6
arc.geo.slot_node          = 0.47
arc.geo.slot_rail          = 0
arc.geo.slot_rfurn         = 0.3
arc.geo.slot_strip         = 0.003
arc.geo.slot_walk_junction = 0.15
arc.geo.slot_walk_strip    = 0.152
arc.geo.slot_wire          = 0.46
arc.geo.slot_xpanel        = 0.06
arc.geo.slot_xstop         = 0.25
arc.geo.step_max           = 0.1
arc.geo.trail_step         = 0.03
arc.geo.train_len          = 0.42
arc.geo.train_speed        = 2.4
arc.geo.train_spread       = 0.6
arc.geo.walk_edge          = 0.9
arc.geo.walk_inner         = 0.8
arc.geo.walk_look          = 0.3
arc.geo.walk_mouth         = 0.25
arc.geo.walk_narrow        = 4
arc.geo.wire_span_sag      = 0.1
arc.geo.wire_hang          = 1.35
arc.geo.wire_sag           = 0.05
arc.geo.wire_wide          = 0.02
arc.geo.xapp_far           = 1.55
arc.geo.xapp_near          = 0.45
arc.geo.lap_find          = 0.25
arc.geo.meet_mast          = 0.08
arc.geo.meet_pad           = 0.06
arc.geo.meet_reach         = 0.62
arc.geo.meet_sign_in       = 0.04
arc.geo.meet_sign_out      = 0.02
arc.geo.meet_skew          = 0.45
arc.geo.meet_stop_deep     = 0.02
arc.geo.meet_stop_lane     = 0.8
arc.geo.meet_stop_min      = 0.1
arc.geo.meet_stop_pen      = 0.02
arc.geo.meet_stop_set      = 0.3


--  ---- THE OVERLAY'S COLOURS ---------------------------------------------
--
--  "Show curves" draws the fit over a bare world: the tiles a fit was
--  allowed to use, the lanes as wires a hair over the ground, and a mark
--  at every vertex of the path.  Each number is a PAINT, which the
--  shader turns into a colour.  They are here so that the overlay can be
--  recoloured without a compile.

--  The ground under the overlay: the tiles a fit may use, the free air
--  beside a band, and a tile an on-spur takes.
arc.geo.mark_corridor      = 3
arc.geo.mark_free          = 6
arc.geo.mark_spur          = 8
--  And the first paint drawn as a LID: over what it marks rather than
--  under it, at the top of the tile's own slot.  It is a threshold, not
--  a colour, so recolouring a mark does not move any geometry.
arc.geo.mark_lid           = 8

--  The lanes, as wires: one joined to another lane, one carried through
--  a junction, and one dropping off a slab.
arc.geo.wire_link          = 5
arc.geo.wire_connector     = 6
arc.geo.wire_spur          = 5

--  And the mark at each vertex of the fitted path: where it starts and
--  finishes, a corner swept into an arc, and a corner left as one.
arc.geo.mark_end           = 7
arc.geo.mark_arc           = 3
arc.geo.mark_corner        = 1

--  A CAR'S COLOUR, and a train's.  The first paint of the run, and how
--  many there are: a car takes one of the run at random, a train's
--  engine takes its own and every carriage one of the rest.
arc.geo.car_paint_first    = 4
arc.geo.car_paint_n        = 5
arc.geo.train_paint_engine = 0
arc.geo.train_paint_first  = 1
arc.geo.train_paint_n      = 3

--  ---- THREE TOLERANCES THAT WERE WRITTEN TWICE ---------------------------

--  How nearly two lines must head the same way for the step between them
--  to read as a sideways jog rather than a turn.
arc.geo.fit_jog_dot        = 0.999
--  How close in height two faces must lie before the check calls them
--  one surface and asks which of the two is meant to show.
arc.geo.coplanar_z         = 0.004
--  How far ahead of the surface it lies on a marking's painter's slot
--  sits.  A slot's fraction outranks any height term.
arc.geo.decal_slot         = 0.004

--  ---- THE GROUND'S OWN COLOUR ------------------------------------------
--
--  Which sprite the mesh takes the ground's mean colour from, and what
--  to use where the art set has no such sprite.  The mean itself is the
--  pipeline's arithmetic; WHICH picture stands for the ground, and what
--  a run without it falls back on, are decided here.
arc.geo.ground_art         = 256
arc.geo.ground_red         = 0.45
arc.geo.ground_green       = 0.62
arc.geo.ground_blue        = 0.30

--  ---- WHAT AN EDIT SHOWS -----------------------------------------------
--
--  What an edit was drawn on goes while the world is built again, the
--  other pieces the build changed leave once it is built, and the pieces
--  it made come after them (scripts/compose/edit_show.lua).  A piece
--  breaks into fragments as it goes and is put together from them as it
--  comes, and every wave runs from the edited tiles.
--
--  How far from them a changed piece may leave, in tiles, and how far one
--  may come: a change further out takes its place at once, since it would
--  wait hidden until the wave reached it.
arc.geo.edit_show_reach       = 3.0
arc.geo.edit_show_come        = 6.0
--  For the going and the coming waves, how fast each runs, in tiles a
--  second, and how long one fragment takes, in seconds.  A time of 0
--  shows nothing.
arc.geo.edit_show_go_speed    = 12.0
arc.geo.edit_show_go_time     = 0.35
arc.geo.edit_show_come_speed  = 15.0
arc.geo.edit_show_come_time   = 0.35
--  The coming wave runs in from its far edge to the edit, 1, or out from
--  the edit as the going does, 0.
arc.geo.edit_show_come_inward = 1
--  A fragment is the triangles whose middles fall in one cell of an
--  uneven grid this many tiles wide.  It waits up to this many seconds
--  past the wave, drifts this far in tiles, and dissolves this far as it
--  goes, 1 to nothing.
arc.geo.edit_show_piece       = 0.4
arc.geo.edit_show_jitter      = 0.2
arc.geo.edit_show_scatter     = 0.3
arc.geo.edit_show_dissolve    = 0.0
--  Going, a fragment is thrown this many levels up and falls this many
--  below its place.  Coming, it drops in from this many levels above it.
arc.geo.edit_show_hop         = 0.5
arc.geo.edit_show_depth       = 1.0
arc.geo.edit_show_drop        = 1.5
