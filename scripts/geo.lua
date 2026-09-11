--  geo.lua -- every number the line works are built from.
--
--  This file holds them all.  There is no list in C: a number exists
--  from the moment this file names it, the C that reads one names it
--  back, and a name nothing sets reads as zero.
--
--  A script may also make numbers of its own simply by naming them; the
--  models use those the same way (arc.model, scripts/models.lua).

arc.geo.blink_duty         = 0.3
--  How nearly the two arms of a junction must agree before a car crosses
--  it straight through instead of bending round the corner: about
--  forty-five degrees.
arc.geo.box_straight_dot   = 0.7
arc.geo.buck_long          = 0.081
arc.geo.car_creep          = 0.02
arc.geo.car_density        = 1.5
arc.geo.car_gap_free       = 0.8
arc.geo.car_gap_stop       = 0.42
arc.geo.car_len            = 0.556
arc.geo.car_probe          = 0.075
arc.geo.car_stop_hold      = 0.45
--  The braking curve: a car a twentieth of a tile from the line it must
--  stop at may still be doing three tiles a second, and nought at the
--  line itself.  Distance and speed, with no time in them: how often the
--  picture is drawn has nothing to do with how a car stops.
arc.geo.hold_over          = 0.05
arc.geo.hold_speed         = 3.0
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
arc.geo.band_road_dot      = 0.7
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
--  A spline fit's piece is one dense sample, and a tangent taken from a
--  few hundredths of a tile points wherever that fragment happens to.  A
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
arc.geo.interchange_dump   = 0

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

--  Half the width of a movement's own ribbon across an interchange: one
--  lane of the slab, since a movement carries one lane from one arm to
--  another.
arc.geo.interchange_lane_w = 0.30

--  How near two lane ends at an interchange must be to belong to the
--  same arm.  A way is two tiles across and its ends stand
--  within that; the next arm along is further off.
arc.geo.interchange_arm_apart = 1.6

--  How far from a 2x2 interchange's middle a lane end still belongs to
--  it: the block is two tiles across and a band runs its slab out to the
--  edge, so an end of one lies about a tile and a half out.
arc.geo.interchange_reach  = 1.75

--  And how wide the turn through an interchange sweeps: the corner at
--  the block's middle, where one arm is carried round into another.
arc.geo.interchange_radius = 0.9

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
arc.geo.slot_xrail         = 0.061
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

