.. _scripting:

Scripting
=========

.. rubric:: ``src/script``, ``scripts/``

The road works are drawn from numbers and from rules. A script holds both,
so a change to either is a file save and not a compile.

.. important::

   **The scripts are where the world is imagined.** Lua reads the
   simulation, decides what the world contains, and asks C to draw it in
   3D with primitives. C offers the simulation to be read, fits curves,
   lofts profiles and puts geometry; it decides nothing about the look.

   A decision that lives in C is a decision nobody can iterate on. This
   page describes how much of that division holds today, which is less
   than the whole: the network walk, the corridor fit and the loft are
   still entered from C rather than called from a script. The purpose at
   the top of ``CLAUDE.md`` states the division that is being built
   toward, and the goals under it say in what order.

The scripts in ``scripts/`` are read at startup, with no flag, before the
first mesh is built, and every folder under it is read too. They are not
decoration and not a fallback: they are the only copy. ``geo.lua`` holds
the road works' shared numbers, ``rules.lua`` every decision, and
``models/`` one file per prop, each carrying its own measurements and the
build function that turns them into the pieces the prop is made of.
There is no ladder in C behind any of it. Take the folder away and the
city has no signals, no stop signs, no crosswalks and no street furniture
at all.

.. code-block:: console

   scripts/geo.lua              the numbers the road works share
   scripts/rules.lua            every decision
   scripts/families/road.lua    one family: how a road is drawn
   scripts/families/highway.lua
   scripts/families/...
   scripts/models/signal.lua    one prop, its numbers and its shape
   scripts/models/gate.lua
   scripts/models/...

The program watches them. It looks once a frame, reads them again when
anything changes, and builds the mesh again on the reload. Edit a rule in an
editor, save, and the city redraws. ``--lua`` names a file or folder of your
own, read after the shipped ones so it may change what they set.

.. code-block:: console

   arcology atlanta --lua my-scripts/
   arcology atlanta --lua-eval 'arc.model.build("signal", 0.25)'

The Script window on the Windows menu, or ``--scriptwin``, runs a line
against the live state and shows what it answers.

What a script may reach
-----------------------

Everything is under one global, ``arc``.

.. list-table::
   :header-rows: 1
   :widths: 30 70

   * - Name
     - What it is
   * - ``arc.tune``
     - The look's nineteen knobs, by name: the two carriageway widths, the
       radii each family may be drawn with, the highway's own.
   * - ``arc.geo``
     - The road works' numbers, and any a script makes for itself: the
       painter's slot stack, lowest first --
       a railway's ballast, a junction's asphalt, a crosswalk's band, a
       carriageway, a level crossing's surface and the rails in it, then
       the footways -- the footway's share of a band, a crossing's depth
       and how nearly parallel its two pavements must run, and the kerb
       return's radius and smoothness.
   * - ``arc.rules``
     - Every decision the road works make. See below.
   * - ``arc.family``
     - ``define{...}`` declares a family -- one kind of line, and how it is
       drawn. ``list()`` names the ones declared. See below.
   * - ``arc.city``
     - ``size``, ``tile(col, row)``, ``road_class(col, row)``.
   * - ``arc.mesh``
     - ``crossings()``, ``walkways()``, ``faults()``, ``probe(x, y)``.
   * - ``arc.settings()``
     - Both of the above as plain tables, for printing or walking.
   * - ``arc.log``, ``arc.dump``, ``arc.rebuild``
     - A message, a report line, and a request to draw the world again.
   * - ``arc.stale()``, ``arc.reload()``
     - The watch, from the script's own side: whether the file has changed,
       and reading it again without waiting.

There is no list of these numbers in C at all. ``scripts/geo.lua`` sets every
one, a name exists from the moment a script names it, and the C that reads
one names it back and keeps the place the store gave it:

.. code-block:: c

   static int gix_junc_curb = -1;
   ... net_geo(&gix_junc_curb, "junc_curb") ...

So a number can be invented, renamed or removed without a compile, and a name
nothing sets reads as zero. ``arcology --lua-lint`` reads the scripts
together, in load order, and says whether every name one of them uses is a
name another one sets.

``arc.tune`` and ``arc.geo`` answer from the running program, so a read is
the value the build is using however it was last moved, and a write is held
to a range the rest of the pipeline can still draw with and asks for a
rebuild. They hold nothing themselves, so ``pairs`` cannot walk them;
``arc.settings()`` answers a plain snapshot of both.

The rules
---------

A rule is a function on ``arc.rules``. It decides, and there is no ladder
in C behind it: take a rule away and the thing it decides is not there at
all. That is what keeps one path through the pipeline rather than two.

``family(f)``
   What is true of every strip and every junction of one family, asked
   once for each and used all through the build: ``footway`` (how the
   band divides across the carriageway's half width, and where its bands
   sit in the painter's stack), ``junction`` (the box every junction is
   built in), ``strip`` (how finely the ribbon is cut, how far it stands
   over the ground, and the marks the curve overlay draws it with),
   ``lane`` (how tight a connector may turn, how it is drawn, when two
   join), ``track`` (a railway's gauge and through offsets) and
   ``approach`` (the band a road fades over into a level crossing). A
   family that answers no footway has none.

``traffic()``
   How the world that moves behaves: the trains, the cars that follow one
   another, the gates they wait at and the signals that blink. Asked once,
   so a car costs no call of its own.

``control(at)``
   A junction's control, one code an arm: 0 none, 1 stop, 2 signal. ``at``
   carries ``col``, ``row``, ``links``, ``busy``, and ``arms`` where the
   family measured its own: ``at.arms[e + 1]`` is ``{class=, traffic=}``
   for an arm that is there and ``nil`` for one that is not.

``crossing_at(mouth)``
   Whether one arm's mouth carries a crosswalk and how deep a band it asks
   for, in tiles, or 0 for none. ``mouth`` carries ``col``, ``row``,
   ``arm``, ``control``, ``pavement`` -- a footway each side of the mouth
   -- ``cos``, how the two run against one another with -1 squarely
   facing, and ``span``, how wide the mouth is in footway widths.

``crossing(mouth)``
   How much of that depth the arm can spare. ``mouth`` carries the three
   measurements the junction made: ``want``, the band the crossing asked
   for; ``room``, the road there is to give up; and ``straight``, how much
   of that road runs straight from the mouth.

``corner(at)``
   What a junction's outline does where two arms meet, turning through
   ``at.phi`` radians. A table ``{tangent=, radius=, steps=}`` rounds the
   corner off with a kerb return, ``false`` leaves it square, and nothing
   runs the boundary straight past it. ``at`` also carries ``col``,
   ``row``, ``grow`` and ``width``, and, once there is an outline to
   measure against, ``room``, ``back`` and ``fwd``.

``lanes(fam, cls)``
   Where a family's lanes run, as distances from the centreline, inner
   first.

``lamps(cls, len)``
   Where the lamps stand along one road strip: a list of
   ``{at=, side=, ["in"]=}``, the distance along it, which hand of it the
   pole is on and how far in from the kerb it stands.

``rail_marks(m)``
   What stands beside one railway strip: a list of
   ``{at=, side=, out=, model=, signal=, face=, clear=}``. ``signal`` 0 is
   a block signal and 1 an absolute one, both registered with the traffic
   so their block can light them; anything else is the named model
   standing there. ``m`` carries ``len``, ``ahead`` and ``behind`` -- a
   junction each way -- and ``crossings``, where the roads cross.

``crossing_frame(x)``
   How big a level crossing is, from ``x.sin`` -- the sine of the angle
   the road and the railway cross at -- and their two widths: ``reach``
   along the road, ``mast`` out to the gate, ``bed`` across the rail, and
   the panel's own ``lift`` and ``slot``.

``crossing_marks(x)``
   What one road approach to a level crossing carries, in the order it is
   laid: a list of ``{model=, out=, across=, deep=, slot=}``, ``out``
   along the road from the crossing's middle and ``across`` from its
   centreline. An entry with no model is the stop line, and asking for no
   span leaves it off.

``gate_arm(at)``, ``power_tile(at)``, ``power_crossing(at)``
   A prop, built whole rather than merely placed. ``at`` carries where it
   stands, the ground there, which way it faces, its tile, the edges the
   tile joins, and for a moving part ``angle`` and ``len``. Answer true
   when you drew it. The primitives are ``arc.put.box``, ``arc.put.bar``,
   ``arc.put.lamp``, ``arc.put.wire``, ``arc.put.model`` and
   ``arc.put.ground``, with the materials under ``arc.mat``; they are the
   same ones every other prop is built from, so a scripted gate goes
   through the same emitter, the shape layer and the checks included, and
   they draw only while a prop rule is running.

``piece_tiles()``
   What the city's save FILE means. Answers a table keyed by the building
   byte: ``{family=, piece=}`` names the network a byte carries and its
   place in the shared fifteen-piece layout, and ``second`` names the
   other family a crossing carries on the other axis. Asked once a
   reading and kept, since every tile is looked up in it twice.

   .. caution::

      A byte no family claims falls through to the BUILDING path and is
      given a levelled pad. So a rail id left out here does not merely go
      undrawn: it becomes a raised slab with the track on top of it and
      the ground either side untouched.

   Its neighbours are the same shape and answer the same kind of
   question: ``road_tiles`` (which bytes a ramp or a lane may join),
   ``hiway_tiles`` (which are deck, ramp, on-ramp, curve, interchange or
   crossing, and which way each runs), ``open_tiles`` (ground a fit may
   sweep across), ``standing_tiles`` (what is in a viaduct's way),
   ``carrier_tiles`` and ``rail_crossing_tiles``.

``water_tiles()``, ``slope_codes()``, ``built_tiles()``, ``sloped_tiles()``, ``building_tiles()``, ``elevated_tiles()``, ``levelling_tiles()``, ``saddle_tiles()``, ``structure_tints()``
   What a tile's own two bytes mean to the GROUND
   (``scripts/ground_tiles.lua``), each keyed by the byte and each looked
   up at every tile of the map: whether the terrain byte is water, what
   slope it carries, whether anything stands on the tile, whether the
   piece on it follows the slope, whether it is a building with a
   footprint and an anchor, whether it is a raised piece ordered by its
   neighbour, whether a corridor may level it, whether the saddle lift
   applies, and what the map view tints a placed structure.

   ``slope_codes`` and ``structure_tints`` answer a NUMBER for each byte;
   the rest answer yes or no.

   .. note::

      There is no ladder in C behind any of them. With no rule nothing is
      water, nothing is built and nothing slopes, which is what a run
      that cannot find the scripts draws.

``world(w)``
   **The drive.** The renderer runs a pass and hands it here, and what
   happens in that pass is decided in Lua: which tiles are composed, in
   what order, and which of the network passes run at all. There is no
   loop in C behind it.

   ``w:info()`` gives ``size``, ``pass`` (1 lays the networks out and
   records the surface their corridors want, 2 builds the world with
   those corridors notched in), ``roads`` and ``underground``. The
   primitives are ``w:wanted(col, row)`` -- whether this build wants the
   tile at all, which an edit's build answers false for most of the map
   -- then ``w:ground(col, row)``, ``w:tint(col, row)``, and the three
   network passes ``w:lanes()``, ``w:networks()`` and ``w:highways()``.

   Answer true when you composed the world. A build whose ``world`` rule
   is missing, or answers false, is abandoned and says so: there is
   nothing behind it to fall back to, and an empty city reported as a
   success would be worse than no city at all.

``incr_reach()``
   How far an EDIT reaches, in tiles (``scripts/incr.lua``). The mesh is
   kept in chunks, so a rebuild after an edit replaces only the chunks
   whose geometry changed, and which those are is a closure over what
   depends on what: ``band_fit``, ``ramp``, ``band_ground``,
   ``band_ramp``, ``band_margin`` and ``segment``. Asked once a build.

   .. caution::

      These are a floor, not a fit. Too far costs build time and draws
      the same thing twice. Too NEAR leaves the last build's triangles
      standing in a chunk that should have been redrawn, and nothing in
      the build says so -- the mesh is sound, the counts are plausible,
      and the city is simply wrong where the edit reached and the closure
      did not. ``ctest -R incremental_rebuild`` is what catches it.

   With no rule at all every chunk is built again: slow and correct,
   rather than fast and wrong.

``gate(g)``
   Where a level crossing's gate arm stands after ``g.dt`` seconds:
   ``g.angle`` is where it is now, 0 flat across the road, and ``g.near``
   how far along the rail's axis the nearest train car is.

``car_follow(c)``, ``car_hold(c)``
   How fast a car may go. ``car_follow`` answers for the ``gap`` to the
   car ahead of it; ``car_hold`` for whatever holds it -- a signal, a
   stop sign or a crossing's gates -- ``c.ahead`` away, with ``c.line``
   the distance short of it that a car stops at.

.. caution::

   **Nothing a rule answers may depend on a frame.** ``g.dt`` and
   ``c.step`` are the world's own beat, a sixtieth of a second. A frame
   adds the real time it took to what the world owes and runs whole beats
   for it, at most eight after a stall, so how often the picture is drawn
   changes nothing a car or a gate does.

Every rule but ``gate``, ``car_follow``, ``car_hold`` and ``gate_arm`` is
called while the mesh is built, a few thousand times a build. The first
three are asked on the world's beat, one for each crossing and one for
each car; ``gate_arm`` draws the arm where the beat has left it, once a
frame for each crossing, which is a few dozen.

A rule that cannot end is stopped after two hundred thousand steps and
reported like any other fault, so a loop saved by mistake costs a message
rather than the program.

.. code-block:: lua

   arc.geo.cross_deep = 0.30

   arc.rules.control = function (at)
       local c = {0, 0, 0, 0}
       for e = 1, 4 do if at.arms[e] then c[e] = 2 end end
       return c
   end

.. caution::

   A rule may change what is drawn; it may not make the mesh unsound. Every
   answer is held to what the pipeline can build: a crossing to the road the
   arm has to give up, a kerb return to the room the two mouths leave, a
   knob and a constant to their own ranges. ``ctest -R lua_rules`` proves
   the lot at once -- that the reference script changes nothing, that a
   constant and a rule each change the build, and that the build with that
   rule still passes its own checks.

Composing the world
-------------------

The renderer draws triangles and runs shaders. **What triangles there
are is composed by the scripts.** The pipeline still solves: it fits a
road's centreline, grades it against the ground, cuts it into stations,
and works out a junction's outline from the arms that meet there. That
is a solver's work. What is laid over the result is not.

A thing the pipeline has built is handed to a rule **as an object**, not
as a window on to "the one being drawn":

.. code-block:: lua

   arc.rules.strip = function (s)
       for i = 2, s:count() do
           local x, y, z, dx, dy, at, wl, wr = s:at(i)
           ...
           s:quad(a0x, a0y, a1x, a1y, b0x, b0y, b1x, b1y,
                  za, zb, acr, acl, ala, alb, mat, order)
       end
       return true
   end

``s.kind`` says what it is, ``s.alive`` whether the record it points at
is still the pipeline's current one, and the methods it answers are its
kind's -- a strip and a junction both answer ``info``, and each answers
its own. A handle is dead the moment the rule that was given it returns:
a method on a dead one answers nothing rather than reading a record the
pipeline has moved past.

.. list-table::
   :header-rows: 1
   :widths: 22 78

   * - kind
     - what it is, and what it answers
   * - ``strip``
     - one run of road, railway or deck: ``info``, ``count``, ``at``,
       ``width``, ``order``, ``ground``, ``road_class``, ``class``,
       ``extras``, ``quad``, ``pair``, ``walk_at``, ``walk_ends``
   * - ``junction``
     - the asphalt inside one junction's outline: ``info``, ``count``,
       ``at``, ``surface``, ``tri``, ``quad``
   * - ``footway``
     - one band of pavement or one crosswalk: ``info``, ``count``,
       ``at``, ``ends``, ``quad``, ``wire``
   * - ``tile``
     - one tile of the ground: ``info``, ``at``, ``colour``, ``edge``,
       ``normal``, ``top``, ``wall``, ``wall_r``, ``glass``, ``tri``,
       ``walled``
   * - ``lane``
     - one fitted line the outline view draws: ``info``, ``piece``,
       ``at``, ``height``, ``order``, ``wire``
   * - ``panel``
     - a level crossing's panel: ``info``, ``at``, ``quad``

The rules that compose live in ``scripts/compose/``:

.. list-table::
   :header-rows: 1
   :widths: 24 76

   * - rule
     - what it lays
   * - ``tile``
     - the ground: the top face, the seabed under water, the wall down to
       each neighbour, the sediment at the map's cut edges
   * - ``zone_tint``
     - the map view's tint over a zoned tile
   * - ``strip``
     - a run of road, railway or deck: its own surface
   * - ``walks``
     - the pavements beside a strip, station for station with it
   * - ``junction``
     - a junction's asphalt, as a fan over its outline
   * - ``footway``
     - every band the network holds: pavements and crosswalks
   * - ``crossing_panel``
     - a level crossing's panel over the road it interrupts
   * - ``lane``, ``curves``, ``walk_curves``
     - what the outline view draws in place of the roads
   * - ``deck``
     - a freeway's gore where a ramp takes its outer lane, and its
       underside

.. caution::

   **Compose in the mesh's own precision.** Vertices are floats in the
   mesh and numbers are doubles in a script. A line worked out to more
   places than the mesh can keep lands a few millionths from where its
   neighbour thinks it is, and the two then overlap by that hair instead
   of meeting along it. ``arc.put.f32`` narrows a number to what the mesh
   holds, and a shared edge is worked out through it -- which is why
   ``arc.band_edge`` is one function that both the carriageway and the
   pavement beside it call.

   ``ctest -R lua_compose`` proves it: every station pair of every strip
   in a city, composed by the script and by the pipeline's own stages,
   compared to the bit.

The models
----------

A prop is a file under ``scripts/models``. It names itself, holds its own
measurements, and answers with the pieces it is made of when it is asked
for a prop of a given size. Nothing outside the file names its numbers,
so changing one moves that prop and nothing else.

.. code-block:: lua

   arc.model.define("signal", {
       p = {out = 0.06, post = 0.025, tall = 0.88, arm = 0.14, ...},

       build = function (p, at)
           local corner = p.out + at.size
           return {
               slot = 0,           -- its place in the painter's stack
               lift = 0,           -- how far over the surface it stands
               ax = -corner,       -- its own origin from where it was put
               ac = 0,
               parts = {
                   {kind = "box", ac = corner, w = p.post, d = p.post,
                    z0 = 0, z1 = p.tall, mat = arc.mat.prop},
                   ...
               },
           }
       end,
   })

``at.size`` is the thing the prop belongs to, across -- a junction's half
width for a signal. That is what makes a model **parametric** rather than
merely stored: the same file answers a mast 0.31 out at one junction and
0.46 at a wider one, so a signal stands at the kerb of whatever junction
it is on.

A piece is one of five kinds, in the prop's own frame: ``ax`` along the
way it faces, ``ac`` across it, ``z0`` to ``z1`` above the ground under
it, ``w`` across the facing and ``d`` along it.

.. list-table::
   :header-rows: 1
   :widths: 20 80

   * - ``kind``
     - what it is
   * - ``box``
     - a box, square to the map
   * - ``arm``
     - a box spanning across from ``ac2`` to ``ac``, ``d`` of it lapping
   * - ``lens``
     - three lamp faces down from ``z0``, one every ``z1``
   * - ``face``
     - one lamp face, ``w`` its half span and ``z0`` its middle
   * - ``prism``
     - a box along the facing, cut on the tile folds, its two feet at
       ``ac`` and ``ac2`` over the surface and at ``f0`` and ``f1`` along
       the run between the heights it is put on

``arc.model.names()`` lists them, ``arc.model.params(name)`` is a model's
own numbers to read or to change, and ``arc.model.build(name, size)``
answers what it is made of at that size, with every number worked out --
which is exactly what the renderer walks.

The linter reads every model file, runs every build function, and reports
a piece whose measurement is not a number.

Families
--------

A **family** is one kind of line and everything about how it is drawn: how
wide it is, which material it wears, which radii the fit may use, what it
builds where two of its lines meet, and which of the loft's stages it
supplies. The road, the railway, the freeway deck and the power line are
four of them, and there is no table of them in C. Each is a file:

.. code-block:: lua

   arc.family.define{
       name    = "road",
       tiles   = "road",       -- the tile family it answers for
       answers = true,         -- and it is what a road tile means
       walk    = 0,            -- the walk visits it first

       width = "road_w",       -- the live knobs, BY NAME, so a strip
       rmin  = "road_rmin",    -- reads the value the window is showing
       rmax  = "road_rmax",

       material = arc.mat.road,
       loft     = "road",      -- road, rail, deck or ramp
       slot     = "slot_strip",

       curbs = true, ramps = true, caps = true, classed = true,
       lane_paint = 5.0, lane_ends = "cap",

       stages = {
           control   = "road_control",
           box       = "road_box",
           record    = "road_record",
           traffic   = "road_lanes",
           furniture = "road_lamps",
       },
   }

Every stage is **named**, not pointed at. A name the pipeline has registered
as a primitive binds to that C function; any other name binds to
``arc.rules.<name>``, which is handed the thing the stage works on. So a
stage can be moved into Lua one at a time, and the road's ``traffic`` stage
already is:

.. code-block:: lua

   function arc.rules.road_lanes(cls)
       local off = arc.rules.lanes("road", cls)
       local inner = off and off[1] or 0.0
       return inner, off and off[2] or inner
   end

The eleven stages are ``control``, ``box``, ``record``, ``crossing``,
``flies``, ``taper``, ``profile``, ``works``, ``traffic``, ``furniture``
and ``tile``.

.. note::

   Four of them must name a primitive. ``box``, ``crossing`` and ``tile``
   work on a mesh a rule has no handle on; ``flies`` is asked at every
   station of every strip, in the middle of the grading, where the drive
   cannot stand between. A declaration that names anything else there is
   refused and says so.

Another way of drawing a highway is therefore another file in
``scripts/families``: give it a name of its own, name your own rules for the
stages you want to answer yourself, and leave the rest naming the
primitives. Nothing is compiled.

.. caution::

   A name nothing answers to is a **fault**, not a default. A knob, a loft
   kind, a tile family or a lane ending the C has never heard of stops the
   declaration and the family never appears. A family half declared would
   draw a city half wrong and say nothing about it.

The linter
----------

.. code-block:: console

   arcology --lua-lint scripts/*.lua

A generic Lua checker knows the language. This one knows the **vocabulary**,
which is where the time goes. ``arc.geo.cros_deep`` reads as nil,
``arc.rules.crosing = f`` is a rule nothing will ever call, and neither says
anything at all: the city comes out unchanged and the next half hour goes on
wondering why.

It makes four passes over each script, each catching what the one before it
cannot.

1. It parses.
2. It runs, in an environment where ``arc.tune`` and ``arc.geo`` refuse a
   name that is not a setting, ``arc.rules`` refuses a name that is not a
   rule or a value that is not a function, and a write to a global that was
   never declared is reported, which is nearly always a missing ``local``.
3. Every rule it sets is **called once**, with an argument of the shape the
   pipeline hands it, so a typo inside a rule body is found here rather than
   at the next build.
4. What each rule answered is the shape its caller reads.

Nothing it does touches the world: there is no city, no mesh and no window,
and the settings it writes go to a table of its own. ``ctest -R lua_lint``
reads every script the repository ships.

The house comment rules hold for a script as they do for the code it drives,
and ``tools/comment_lint.py`` reads ``.lua`` along with the rest.

There is no building without it
-------------------------------

Lua 5.4 is fetched by CMake, as SDL3, spdlog and Dear ImGui are, and
there is no switch to leave it out. The geometry, the decisions, the
families and the drive are all in the scripts: a build without them would
not be a smaller city, it would be no city. Nothing calls into the layer
through a stub any more, and `script.h` is the whole of its face.
