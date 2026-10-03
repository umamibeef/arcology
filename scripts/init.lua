--  init.lua -- WHAT THE SCRIPT LAYER IS, and the order it is read in.
--
--  C reads this file and nothing else.  Everything below is required
--  from here, so this is the index of the layer: a file no line names
--  is not part of it and is not read.  A name that is a FOLDER is read
--  through that folder's own init.lua, which is its index in the same
--  way.
--
--  Only two orderings are real.  `geo` and `tune` are first, because
--  they declare every number the rest read.  And `hiway_tiles` is before
--  `road_tiles`, which reads `arc.band_tiles` as it loads.  Nothing
--  else reads another file while it loads: a family, a model and a
--  composing rule each declare what they are and read nothing until
--  the pipeline calls them.  They are listed in their own order all
--  the same, so a reader can see the whole layer here.

--  ---- THE NUMBERS AND WHAT A BYTE MEANS ----------------------------
--
--  The arithmetic first, since every file below takes arc.put.f32 as it
--  loads (arith.lua).  Then the numbers.  Then what each of the city's
--  bytes means, which is pushed down once and read out of a table for
--  the rest of a build.
require "arith"
require "geo"
require "tune"
require "hiway_tiles"  -- sets arc.band_tiles
require "road_tiles"   -- reads it while it loads
require "ground_tiles"
require "net_tiles"
require "art_tiles"
require "tile_names"
require "incr"
require "rules"

--  ---- THE FAMILIES -------------------------------------------------
--
--  One kind of line each, and everything about how it is drawn.
require "families.highway"
require "families.power"
require "families.rail"
require "families.road"

--  ---- THE MODELS ---------------------------------------------------
--
--  A prop's own shape, built from the mesh primitives.
require "models.car"
require "models.crossbuck"
require "models.gate"
require "models.gate_lamps_dark"
require "models.gate_lamps_lit"
require "models.milepost"
require "models.node_end"
require "models.node_mid"
require "models.pylon"
require "models.rail_signal"
require "models.second_train"
require "models.signal"
require "models.stop_sign"
require "models.street_lamp"
require "models.thread_aspect_green"
require "models.thread_aspect_red"
require "models.train_car"
require "models.train_engine"

--  ---- THE COMPOSITION ----------------------------------------------
--
--  The rules the pipeline asks as it builds.  `world` is the drive the
--  renderer enters through; the rest answer one question each.
require "compose.band"
require "compose.bands"
require "compose.beat"
require "compose.bridge"
require "compose.cap"
require "compose.car_density"
require "compose.car_turn"
require "compose.chain"
require "compose.control_prop"
require "compose.crossing_panel"
require "compose.curves"
require "compose.deck"
require "compose.drop"
require "compose.edit_show"
require "compose.fit"
require "compose.footway"
require "compose.frame"
require "compose.ground"
require "compose.interchange"
require "compose.invent"
require "compose.join"
require "compose.junction"
require "compose.junction_band"
require "compose.junction_signs"
require "compose.lamps"
require "compose.lane"
require "compose.links"
require "compose.network"
require "compose.node_control"
require "compose.node_threads"
require "compose.orient"
require "compose.outline"
require "compose.path"
require "compose.pieces"
require "compose.poses"
require "compose.profile"
require "compose.ramp_target"
require "compose.relax"
require "compose.road_class"
require "compose.runs"
require "compose.shelf"
require "compose.signal"
require "compose.signal_cycle"
require "compose.slide"
require "compose.snap"
require "compose.stair"
require "compose.strip"
require "compose.sweep"
require "compose.tangent"
require "compose.thread_marks"
require "compose.thread_signal"
require "compose.tile"
require "compose.tops"
require "compose.train_turn"
require "compose.turns"
require "compose.walk"
require "compose.walk_curves"
require "compose.walks"
require "compose.world"
require "compose.xlane"
require "compose.zone_tint"

