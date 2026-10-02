.. _scripting:

Scripting
=========

.. rubric:: ``src/script``, ``scripts/``

The world is drawn from numbers and from rules. A script holds both, so
a change to either is a file save and not a compile.

.. important::

   **The scripts are where the world is imagined.** Lua reads the
   simulation, decides what the world contains, and asks C to draw it in
   3D with primitives. C offers the simulation to be read, fits curves,
   lofts profiles and puts geometry. It decides nothing about the look.

   A decision that lives in C is a decision nobody can iterate on. The
   purpose at the top of ``CLAUDE.md`` states that division, and the
   goals under it record how far it holds.

The scripts in ``scripts/`` are read at startup, with no flag, before
the first mesh is built. Every folder under it is read too. They are the
only copy. There is no ladder in C behind any of them. Take the folder
away and the city has no signals, no stop signs, no stripes and no
street furniture at all.

.. code-block:: console

   scripts/geo.lua              the numbers the line works share
   scripts/rules.lua            the decisions
   scripts/families/road.lua    one family: how a line is drawn
   scripts/families/highway.lua
   scripts/families/rail.lua
   scripts/families/power.lua
   scripts/compose/world.lua    the drive: what a pass composes, in what order
   scripts/compose/...          one rule a file: what each thing is made of
   scripts/models/signal.lua    one prop: its numbers and its shape
   scripts/models/...
   scripts/net_tiles.lua        what a building byte means to the networks
   scripts/ground_tiles.lua     what a building byte means to the ground
   scripts/incr.lua             how far an edit reaches

The program watches them. It looks once a frame, reads them again when
a file changes, and builds the mesh again on the reload. Edit a rule,
save, and the city redraws. ``--lua`` names a file or a folder of your
own. It is read after the shipped scripts, so it may change what they
set.

.. code-block:: console

   arcology atlanta --lua my-scripts/
   arcology atlanta --lua-eval 'arc.model.build("signal", 0.25)'

The Script window runs a line against the live state and shows what it
answers. ``--scriptwin`` opens it at start.

What a script may reach
-----------------------

Everything is under one global, ``arc``. :ref:`appendix-script-api`
lists every function, every kind of handle with its methods, and every
rule. That page is generated from ``src/script``, so a name on it is a
name the program has. This page says what each part is for.

.. list-table::
   :header-rows: 1
   :widths: 30 70

   * - Name
     - What it is
   * - ``arc.tune``
     - The look's knobs, by name: the width and the radii each family is
       drawn with, and the band's own.
   * - ``arc.geo``
     - The line works' numbers, and any number a script makes for
       itself.
   * - ``arc.settings()``
     - Both of the above as plain tables, for printing or walking.
   * - ``arc.rules``
     - The decisions. A script sets each one as a function.
   * - ``arc.city``
     - The simulation, offered to be read: any layer at any cell, its
       four neighbors, or a whole plane.
   * - ``arc.mesh``
     - The last build: its level meets, its walkways, its faults, and
       every surface over one point.
   * - ``arc.fit``
     - The tangent fit as a service: points and radii in, pieces out.
   * - ``arc.put``, ``arc.mat``
     - The primitives a script draws with, and the materials by name.
   * - ``arc.model``
     - The props: ``define``, ``names``, ``params`` and ``build``.
   * - ``arc.family``
     - ``define{...}`` declares a family. ``list()`` names the families
       declared.
   * - ``arc.bytes``, ``arc.numbers``, ``arc.pieces``, ``arc.bands``
     - What a script pushes down once, when it is read: what a byte of
       the save means, and the numbers a build reads without asking a
       rule.
   * - ``arc.log``, ``arc.warn``, ``arc.error``, ``arc.dump``
     - A message at each of the log's three levels, and a report line.
   * - ``arc.rebuild``, ``arc.stale()``, ``arc.reload()``
     - A request to draw the world again, and the watch from the
       script's own side.

The numbers
-----------

There is no list of the line works' numbers in C. ``scripts/geo.lua``
sets every one, and a name exists from the moment a script sets it. The
C that reads a number names it back and keeps the place the store gave
it:

.. code-block:: c

   static int gix_weld_grid = -1;
   ... geo_num(&gix_weld_grid, "weld_grid") ...

So a number can be invented, renamed or removed without a compile. A
name nothing sets reads as nought. ``arcology --lua-lint`` reads the
scripts together, in load order, and says whether every name one of
them reads is a name another one sets.

``arc.tune`` and ``arc.geo`` answer from the running program. A read is
the value the build uses, however it was last moved. A write to a knob
is held to the range the pipeline can draw with, and asks for a
rebuild. Neither table holds anything itself, so ``pairs`` cannot walk
them. ``arc.settings()`` answers a plain snapshot of both.

The pushed data
---------------

What a byte of the city's save means is the script's to say. It says
so once, when it is read, and the pipeline reads the answer straight
out of a table of 256. No build asks a rule what a byte means.

``scripts/net_tiles.lua`` hands ``arc.pieces`` a table keyed by the
building byte: the family the byte carries, its place in the shared
fifteen-piece layout, and the second family a meet carries on its other
axis. ``scripts/ground_tiles.lua`` hands ``arc.bytes`` one table for
each question the ground asks: whether a byte is water, whether
anything stands on it, what slope it carries. ``scripts/incr.lua``
hands ``arc.numbers`` how far an edit reaches.

.. caution::

   A byte no family claims falls through to the building path and is
   given a leveled pad. A network id left out of
   ``scripts/net_tiles.lua`` does not go undrawn. It becomes a raised
   slab with the network on top of it and the ground either side
   untouched.

With no pushed table nothing is water, nothing is built and nothing
slopes. That is what a run that cannot find the scripts draws.

The rules
---------

A rule is a function on ``arc.rules``. It decides, and there is no
ladder in C behind it. Take a rule away and the thing it decides is not
there at all. That is what keeps one path through the pipeline rather
than two. :ref:`appendix-script-api` lists every rule the lint knows
and the shape of its answer.

The pipeline hands a rule the thing it has built **as an object**, never
as a window on "the one being drawn":

.. code-block:: lua

   arc.rules.strip = function (s)
       for i = 1, s:count() - 1 do
           local x, y, z, dx, dy, at, wl, wr = s:at(i)
           ...
           s:quad(a0x, a0y, a1x, a1y, b0x, b0y, b1x, b1y,
                  za, zb, acr, acl, ala, alb, mat, order)
       end
       return true
   end

``s.kind`` says what the handle is. ``s.alive`` says whether the record
behind it is still the pipeline's current one. The methods a handle
answers are its kind's: a strip and a junction both answer ``info``, and
each answers its own. A handle is dead the moment the rule that was
given it returns. A method on a dead handle answers nothing rather than
reading a record the pipeline has moved past.

Every index the API takes or answers counts from nought. A sequence a
rule returns is Lua's own and counts from one, because that is what
``{...}`` and ``ipairs`` mean.

**The drive.** The renderer runs a pass and hands it to
``arc.rules.world(w)``. What happens in that pass is decided in Lua:
which tiles are composed, in what order, and which network passes run
at all. There is no loop in C behind it. ``scripts/compose/world.lua``
names the steps, and ``w:info()`` gives ``size``, ``pass``, ``lines``
and ``underground``.

**The beat.** ``gate``, ``car_follow``, ``car_hold``, ``car_turn`` and
``train_turn`` are asked on the world's beat, one call for each gate and
each mover. Every other rule is called while the mesh is built, a few
thousand times a build, and never once a frame.

.. caution::

   **Nothing a rule answers may depend on a frame.** The ``step`` a beat
   rule is handed is the world's own beat, a sixtieth of a second. A
   frame adds the real time it took to what the world owes and runs
   whole beats for it. So how often the picture is drawn changes nothing
   a car or a gate does.

A rule that cannot end is stopped after thirty seconds and reported like
any other fault. It is not asked again until the scripts are read again.
So a loop saved by mistake costs a message rather than the program.

.. code-block:: lua

   arc.geo.signal_green = 0.45

   arc.rules.control = function (at)
       local c = {0, 0, 0, 0}
       for e = 1, 4 do if at.arms[e] then c[e] = 2 end end
       return c
   end

.. caution::

   A rule may change what is drawn. It may not make the mesh unsound.
   Every answer is held to what the pipeline can build. ``ctest -R
   lua_rules`` (``tools/lua_check.py``) proves five things at once:
   taking a rule away takes the thing away, a constant changes the
   build, a rule changes the build, the build with that rule still
   passes its own checks, and a rule that raises abandons the build and
   says which rule.

Composing the world
-------------------

The renderer draws triangles and runs shaders. **What triangles there
are is composed by the scripts.** The pipeline solves: it fits a
centerline, grades it against the ground, cuts it into stations, and
works out a junction's outline from the arms that meet there. That is a
solver's work. What is laid over the result is not. The rules that
compose live in ``scripts/compose/``, one rule a file.

.. caution::

   **Compose in the mesh's own precision.** Vertices are floats in the
   mesh and numbers are doubles in a script. A line worked out to more
   places than the mesh can keep lands a few millionths from where its
   neighbor thinks it is. The two then overlap by that hair instead of
   meeting along it. ``arc.put.f32`` narrows a number to what the mesh
   holds, and a shared edge is worked out through it. That is why
   ``arc.band_edge`` (``scripts/compose/band.lua``) is one function
   that both the way and the margin beside it call.

   ``ctest -R lua_compose`` (``tools/strip_check.sh``) composes every
   strip of a city through the script and checks each station pair
   against the pipeline's own numbers.

The models
----------

A prop is a file under ``scripts/models``. It names itself, holds its
own measurements, and answers with the pieces it is made of when it is
asked for a prop of a given size. Nothing outside the file names its
numbers, so changing one moves that prop and nothing else.

.. code-block:: lua

   arc.model.define("signal", {
       p = {out = 0.06, post = 0.025, tall = 0.88, arm = 0.14, ...},

       build = function (p, at)
           local corner = p.out + at.size
           return {
               slot = 0,           -- its place in the painter's stack
               ax = -corner,       -- its own origin from where it was put
               parts = {
                   {kind = "box", ac = corner, w = p.post, d = p.post,
                    z0 = 0, z1 = p.tall, mat = arc.mat.prop},
                   ...
               },
           }
       end,
   })

``at.size`` is the thing the prop belongs to, across: a junction's half
width for a signal. That is what makes a model **parametric** rather
than merely stored. The same file answers a mast further out at a wider
junction, so a signal stands at the lip of whatever junction it is on.

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

``arc.model.names()`` lists the models. ``arc.model.params(name)`` is a
model's own numbers, to read or to change. ``arc.model.build(name,
size)`` answers what it is made of at that size, with every number
worked out, which is what the renderer walks.

The linter reads every model file, runs every build function, and
reports a piece whose measurement is not a number.

Families
--------

A **family** is one kind of line and everything about how it is drawn:
how wide it is, which material it wears, which radii the fit may use,
what it builds where two of its lines meet, and which of the loft's
stages it supplies. The line, the thread, the band and the power line
are the four the repository ships, and there is no table of them in C.
Each is a file in ``scripts/families``:

.. code-block:: lua

   arc.family.define{
       name    = "line",
       tiles   = "line",       -- the tile family it answers for
       answers = true,         -- and it is the family that tile family means
       walk    = 0,            -- the walk visits it first

       width = "line_w",       -- the live knobs, BY NAME, so a strip
       rmin  = "line_rmin",    -- reads the value the window is showing
       rmax  = "line_rmax",

       material = arc.mat.line,
       loft     = "line",      -- line, thread, slab or spur
       slot     = "slot_strip",

       lips = true, spurs = true, caps = true, classed = true,
       lane_paint = 5.0, lane_ends = "cap",
       ...

       stages = {
           control = "node_control",
           traffic = "road_lanes",
       },
   }

The eight stages are ``control``, ``record``, ``flies``, ``taper``,
``profile``, ``works``, ``traffic`` and ``furniture``. Every stage is
**named**, not pointed at. A name the pipeline has registered as a
primitive binds to that C function. Any other name binds to
``arc.rules.<name>``, which is handed the thing the stage works on. So a
stage can be moved into Lua one at a time, and the line's ``traffic``
stage is one:

.. code-block:: lua

   function arc.rules.road_lanes(cls)
       local off = arc.rules.lanes("line", cls)
       local inner = off and off[1] or 0.0
       return inner, off and off[2] or inner
   end

Another way of drawing a band is therefore another file in
``scripts/families``. Give it a name of its own, name your own rules for
the stages you want to answer yourself, and leave the rest naming the
primitives. Nothing is compiled.

.. caution::

   A name nothing answers to is a **fault**, not a default. A knob, a
   loft kind, a tile family or a lane ending the C has never heard of
   stops the declaration, and the family never appears. A family half
   declared would draw a city half wrong and say nothing about it.

The linter
----------

.. code-block:: console

   arcology --lua-lint scripts

A generic Lua checker knows the language. This one knows the
**vocabulary**, which is where the time goes. ``arc.geo.cros_deep``
reads as nil, and ``arc.rules.crosing = f`` is a rule nothing will ever
call. Neither says anything at all: the city comes out unchanged and
the next half hour goes on wondering why.

It makes four passes over each script, each catching what the one
before it cannot.

1. It parses.
2. It runs, in an environment where ``arc.tune`` and ``arc.geo`` refuse
   a name that is not a setting, and ``arc.rules`` refuses a name that
   is not a rule or a value that is not a function. A write to a global
   that was never declared is reported, which is nearly always a
   missing ``local``.
3. Every rule it sets is **called once**, with an argument of the shape
   the pipeline hands it, so a typo inside a rule body is found here
   rather than at the next build.
4. What each rule answered is the shape its caller reads.

A rule a family's declaration names for a stage is a name of the
script's own invention. The linter learns it from ``arc.family.define``
and settles it against the rules once every file is read.

Nothing it does touches the world: there is no city, no mesh and no
window, and the settings it writes go to a table of its own. ``ctest -R
lua_lint`` reads every script the repository ships.

The house comment rules hold for a script as they do for the code it
drives, and ``tools/comment_lint.py`` reads ``.lua`` along with the
rest.

There is no building without it
-------------------------------

Lua 5.4 is fetched by CMake, as SDL3, spdlog and Dear ImGui are, and
there is no switch to leave it out. The geometry, the decisions, the
families and the drive are all in the scripts. A build without them
would not be a smaller city. It would be no city. ``src/script/script.h``
is the whole of the layer's face.
