--  geo.lua -- every number the road works are built from.
--
--  This file holds them all.  There is no list in C: a number exists
--  from the moment this file names it, the C that reads one names it
--  back, and a name nothing sets reads as zero.
--
--  A script may also make numbers of its own simply by naming them; the
--  models use those the same way (arc.model, scripts/models.lua).

arc.geo.blink_duty         = 0.3
arc.geo.buck_long          = 0.081
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
arc.geo.deck_underside     = 0
arc.geo.deck_lane0         = 0.255
arc.geo.deck_lane1         = 0.55
arc.geo.deck_lane2         = 0.82
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
arc.geo.junc_curb          = 0.12
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
--  Where one highway band's deck lane carries on into the next band's.
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

--  Where a band's six lanes become a road's two.  The inner lane routes
--  into the road; the middle and outer taper into the inner, the outer
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

--  A ramp.  The outer lane sits at 0.85 of the deck's half width; a
--  ramp end snaps to a recorded lane within 0.35; a through road is met
--  thirty degrees off its line, as a cosine and a sine so the pair is
--  exactly what the geometry uses; the road's own lanes run a quarter of
--  its width off the centreline; and the merge sits most of half a tile
--  along the road from the foot.
arc.geo.ramp_lane_off      = 0.25
arc.geo.ramp_meet_cos      = 0.866
arc.geo.ramp_meet_sin      = 0.5
arc.geo.ramp_merge_along   = 0.45
arc.geo.ramp_outer         = 0.85
arc.geo.ramp_snap          = 0.35
arc.geo.ramp_taper         = 0.6
--  How near a station must be to a tile's centre to count as standing AT
--  the level crossing on it.  The strip is sampled far more finely than
--  a tile, so this picks the one station that is really on the crossing
--  rather than the several that pass over its tile.
arc.geo.crossing_centre    = 0.07

--  The block a rail signal protects: how far BEHIND it a train still
--  holds it -- a train whose nose has just passed still occupies it --
--  and how far ahead of it the block runs.
arc.geo.rail_block_ahead   = 10.0
arc.geo.rail_block_back    = 0.6

--  Which way a run leaves a junction.
--
--  A piece long enough to have a heading of its own gives its tangent
--  straight: a fillet's straights and swept arcs are a good fraction of a
--  tile, and the tangent at the start of one IS where the road goes.
--
--  A spline fit's piece is one dense sample, and a tangent taken from a
--  few hundredths of a tile points wherever that fragment happens to.  A
--  run leaving diagonally can report due west, the junction trims its arm
--  back on the strength of it, and the road ends up under terrain the pad
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
--  quarters of a tile it would take the grade of a part of the road it
--  does not touch.
--
--  The shelf reaches a little past the band's own edge, and the ring
--  beyond that is the BATTER -- half way back to the hillside -- so the
--  shelf blends out instead of standing on one wall of its full depth.
arc.geo.shelf_along        = 0.75
arc.geo.shelf_batter       = 0.3
arc.geo.shelf_reach        = 0.8

--  A road's class, from the traffic the simulation counted on its tile.
--  Under the first it is a local road, one lane each way; at the first an
--  avenue, with a centre line; at the second a boulevard.  The count is
--  the sim's own byte, 0 to 255, read at half resolution.
arc.geo.class_avenue       = 64
arc.geo.class_boulevard    = 160

--  How far inside its tile a lone piece's ends sit.  A hair, so the end
--  stations read that tile's own surface and not the neighbour's.
arc.geo.tile_inset         = 0.49
--  How far out from the centreline a dead end's kerb runs, as a fraction
--  of the strip's half width: inside the carriageway's own edge.
arc.geo.cap_kerb           = 0.9
--  The most of an arm's road a crossing may take.  Two crossings must
--  still leave a road between the junctions they belong to.
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
arc.geo.lane_road          = 0.2
arc.geo.lane_step_arc      = 0.08
arc.geo.lane_step_ramp     = 0.1
arc.geo.lane_step_run      = 0.25
arc.geo.lane_wire          = 0.08
arc.geo.lift_xpanel        = 0.045
arc.geo.loft_cut           = 0.015
arc.geo.loft_dip           = 0.005
arc.geo.loft_lift          = 0.03
arc.geo.loft_lift_min      = 0.02
arc.geo.loft_step_arc      = 0.04
arc.geo.loft_step_run      = 0.0625
arc.geo.node_high          = 0.04
arc.geo.node_lift          = 0.08
arc.geo.node_over          = 0.1
arc.geo.node_wide          = 0.05
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
arc.geo.xing_find          = 0.25
arc.geo.xing_mast          = 0.08
arc.geo.xing_pad           = 0.06
arc.geo.xing_reach         = 0.62
arc.geo.xing_sign_in       = 0.04
arc.geo.xing_sign_out      = 0.02
arc.geo.xing_skew          = 0.45
arc.geo.xing_stop_deep     = 0.02
arc.geo.xing_stop_lane     = 0.8
arc.geo.xing_stop_min      = 0.1
arc.geo.xing_stop_pen      = 0.02
arc.geo.xing_stop_set      = 0.3

