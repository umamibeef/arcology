.. _enhanced-renderer:

=================
The 2.5D renderer
=================

.. container:: eyebrow

   The enhanced world · the renderer

.. container:: lede

   What the enhanced world's renderer is, and how to run it. The direction and the decisions behind it are on the :doc:`future` page. The original's own drawing, which this is measured against, is on :doc:`rendering-pipeline`. The software rasteriser, ``arcology --soft``, is checked against the original's frame. The GPU path, ``arcology``, is checked against the software rasteriser. The script side of the pipeline is on :doc:`scripting`.

The composition
---------------

.. rubric:: ``arcology --soft --mesh`` · ``--mesh-reverse``

The software rasteriser draws the frame a second way, selected by ``--mesh``. It draws the terrain first, on its own, and everything else afterwards. The terrain pass paints the skirt columns, the ground tile (terrain, zoned-lot ground or a data-view tint) and the elevated anchor's footprint ground. It writes a depth plane that holds the painter's index of the tile that owns each pixel. The sprite pass then draws buildings, networks, traffic, the no-power marker and things in the original's anti-diagonal order. It tests each pixel against that plane and never writes it. Terrain owned by a tile later in the sweep hides a sprite pixel. Sprite-on-sprite order stays the sweep's. The underground view keeps the single sweep, because it emits ground and buried pieces together per tile.

This is the composition rule of the 2.5D renderer. The only depth the original has is "drawn later in the sweep", and a depth plane that stores exactly that reproduces the painter's algorithm tile for tile. ``--mesh-reverse`` draws the terrain pass back to front. That must change nothing, since the plane and not the loop carries the ordering.

.. list-table::
   :header-rows: 1
   :widths: auto

   * - Sweep, terrain drawn back to front
     - Frames identical to the single sweep
     - Sprite pixels the depth plane rejected
   * - every shipped city file, 8 px
     - 111 of 111
     - 2,464,940
   * - ten dense and hilly cities, 16 px
     - 11 of 11
     - 661,735
   * - the same ten, 32 px
     - 11 of 11
     - 1,253,928
   * - the same ten, 32 px, pollution view
     - 11 of 11
     - 0: a data view draws no sprites
   * - the same ten, 32 px, palette phase 3 and no-drop
     - 11 of 11 each
     -

The rejected count is what proves the test. The plane hides over a million sprite pixels behind nearer terrain at 32 px and the frames still match, so depth does the occlusion that the sweep does by ordering.

The provenance plane
--------------------

.. rubric:: ``arcology --soft --provenance out.png``

``--provenance`` writes a second plane beside the picture, one 16-bit sample per pixel. The sample is the id of the sprite that painted the pixel within its art set, with bit 15 set where the shadow pass darkened it afterwards, and 0 for the background. A check of the terrain mesh reads it: every pixel a non-terrain sprite painted must survive the mesh unchanged. The plane changes no byte of the picture. Eight renders covering the three zooms, the underground view, a data view, a palette phase, a crop and an indexed export hash the same with it and without it.

The shape table
---------------

.. rubric:: ``python3 tools/terrain_shapes.py`` · ``assets/terrain-shapes.json``

The tool builds each of the fourteen slope codes as faces from its corner mask, projects them exactly as the rasteriser projects, and compares them against the sprite at all three zooms, for both diagonals. The table it prints is the mesh's geometry. What it finds about the art is stated with the slopes on the :doc:`mechanics` page. The far edges that slope down from a raised corner are drawn at three pixels per four rows where the projection gives four per five, and code 13 is drawn as a flat tile raised one level. Code 7 reproduces its sprite only when cut from north-east to south-west. Every other code is indifferent to the cut.

Running it
----------

All from the repository root, with the game folder as installed:

.. code-block:: console

   cmake --build build

   CITY="$ARC_CITIES/Manhattan"
   build/arcology --soft assets "$CITY" plain.png --zoom 32
   build/arcology --soft assets "$CITY" mesh.png  --zoom 32 --mesh
   build/arcology --soft assets "$CITY" rev.png   --zoom 32 --mesh-reverse

All three print the same CRC. The two ``--mesh`` runs also print how many sprite pixels the depth plane hid behind nearer terrain. ``--zoom 16`` or ``8``, ``--view``, ``--phase``, ``--crop`` with ``--scale`` and ``--focus`` combine with ``--mesh``. The underground view keeps the single sweep.

.. code-block:: console

   build/arcology --soft assets "$CITY" out.png --zoom 32 --indexed --provenance prov.png
   build/arcology --soft assets "$CITY" out.png --zoom 32 --mesh --depth depth.png
   python3 tools/terrain_shapes.py

``--depth`` writes the depth plane of a ``--mesh`` render as a 16-bit greyscale PNG, full frame, before any crop. It holds the painter's index of the tile owning each pixel, and 0 where no terrain was painted. Under a zone building it is 0, because the original paints no ground there. The only terrain that owns pixels in a built-up district is the ground under networks and the open land and water.

The GPU path
------------

.. rubric:: ``arcology`` · ``soft_sweep`` · ``src/render/gpu/gpu.c`` · ``tools/gen_shaders.py``

The software rasteriser's sweep emits an op list, one entry per blit the original makes, in the original's order, and paints it afterwards. ``soft_sweep`` (``src/render/soft/soft.c``) is that list as a function. The GPU path uploads the same list as instances and draws it through SDL_GPU. It draws the terrain ops first with depth writes, the sprites after them tested against that depth and never writing it, the shadows into a mask, and a resolve onto the window. The depth is the sweep's painter's index, so the composition is the 2.5D one above. The atlases are uploaded as palette indices and resolved through a 256-entry palette texture. The app rewrites that texture when the animated runs turn, so no pixel is resolved on the CPU. Shaders are authored once in GLSL and embedded as SPIR-V and MSL, so a plain build needs no shader tools.

``arcology --check out.png`` draws one 1280 by 800 frame both ways, the GPU frame and the software frame cropped to the same canvas rectangle, and counts the pixels that agree.

.. list-table::
   :header-rows: 1
   :widths: auto

   * - City
     - 32 px
     - 16 px
     - 8 px
   * - Manhattan
     - identical
     - 1 pixel of 1,024,000
     - identical
   * - Charleston
     - identical
     - identical
     - identical
   * - Flint
     - identical
     - 9 pixels of 1,024,000
     - identical

Identical means the same CRC over the raw RGB. The pixels that differ are traffic cars, and they are the one place the GPU path approximates. ``$19004`` paints a car only where the destination still holds the road surface, index 0x91. A fragment shader cannot read its destination, so the shader tests the road sprite's own texel under the pixel instead. The two agree except where a tile before it in the sweep painted the destination. At 16 px a car's art crosses into the next road tile, and the second car is stencilled against the first car's grey. With moving things left out, all three cities are identical at all three zooms. The resolve pass applies the shadow of a flying thing by ``$19B76``'s rule on the index the sprite pass left in alpha. That is exact wherever no later sprite painted a dirt-ramp index under the silhouette.

The purpose
-----------

Lua is handed the simulation data, imagines a world on top of it, and asks C to draw that world in 3D with primitives. Lua reads the simulation and decides what the world contains. It decides which cells form a segment, where a road sweeps through its corridor, and how an on-ramp climbs to a highway. It decides what pattern an intersection draws and how many lanes a road carries. C fits curves, lofts profiles and puts geometry in 3D. The point of the split is iteration. Every one of those interpretations changes by editing a script, without a compile. A decision that lives in C is a decision that cannot be iterated on, and that is the test for every change to the renderer.

The four levels
---------------

.. rubric:: ``src/render`` · ``src/render/walk`` · ``src/render/net`` · ``src/render/mesh``

The renderer's code sits at four levels. The top runs a build. ``walk/`` and ``net/`` sit under it. ``mesh/`` is under both and knows nothing above itself, with five exceptions that are real dependencies. ``mesh/tile.c`` cuts the ground to the corridor shelves. ``mesh/loft.c`` reads the profile slots and a family's stages. ``mesh/mesh.c`` walks the bands for the per-pass body. ``mesh/shapes.c`` and ``mesh/surface.c`` read one name apiece.

**The top, src/render/.** ``build.c`` is the build from the outside: two passes over one body, handed one at a time to the script that composes them. ``incr.c`` is what a build has to redo after an edit. It diffs the tiles against a snapshot of the last city built and names the chunks the change can reach. ``geo.c`` holds the numbers a script tunes with: ``arc.geo``'s store and ``arc.tune``'s knobs. ``pipeline.h`` is the vocabulary the three lower levels share: a point, a piece, a sample, a family slot, and what a loft is asked to draw. It declares no ``net/`` name.

**A segment's stages, src/render/walk/.** ``walk.c`` runs one segment's stages from a node to the next node along the pieces a family lays: the cells, the class, the fit, the arms, the trims, the lanes and the overlay. ``cursor.c`` is the order the drive steps them in. ``cell.c`` says what a cell carries: which family a tile's piece belongs to and how it links to its neighbours, read from the byte tables the scripts push down. ``walkway.c`` is the margin network: the paths, their ports, and whether the ends meet. Nothing here decides which cells make a segment.

**The stores and the plumbing, src/render/net/.** ``family.c``, ``drive.c``, ``cut.c`` and ``strip.c`` are the plumbing that turns a Lua declaration into a family and offers a reading to a rule. ``table.c``, ``lane.c``, ``station.c``, ``shelf.c``, ``network.c`` and ``traffic.c`` are the stores, and ``report.c`` says what the city holds on a tile. ``box.c``, ``meet.c``, ``node.c`` and ``margin.c`` are one concept each. ``band.c`` reads what the map says of a band and ``band_walk.c`` walks one. ``spur.c`` holds the tiles the map carries and ``spur_build.c`` poses, routes and builds the descent. Nothing here decides anything, and nothing here knows what a family draws.

**The shape primitives, src/render/mesh/.** ``emit.c`` puts triangles, walls, tops and boxes. ``shapes.c`` puts line quads, fans, boxes, cylinders, bars, wires and ground highlights over them. ``shape.c`` is the table of what each shape is and which triangles are its own. ``mesh.c`` builds the mesh and answers the query tool. ``check.c`` is the watertightness and the lines against the ground. ``fit.c`` is the tangent fit and ``loft.c`` is the sweep. ``piece.c`` is the arithmetic every path is made of, ``surface.c`` samples the drawn ground, ``tile.c`` is what a tile is and the ground field, and ``model.c`` draws a model and caches it. A file here names no road, ramp, deck, track, junction or footway. ``ctest -R family_words`` holds that line.

The vocabulary
--------------

The pipeline names no family. What is drawn (a road, a railway, a raised highway, a power line, and everything those imply) is the scripts'. The C is written in this generic vocabulary instead. Each word is defined by what it is.

.. list-table::
   :header-rows: 1
   :widths: 18 82

   * - word
     - what it is
   * - a **family**
     - one kind of line, and everything about how it is drawn. A script declares it in
       ``scripts/families``. C keeps no list of them.
   * - a **line**, a **strip**
     - the geometry one family lays along a run of cells: one cell wide, following the ground it
       crosses.
   * - a **lane**
     - one way of travel inside a line or a slab. A mover drives along a lane, and ``net/lane.c``
       checks that every one of them can be driven.
   * - a **band**
     - a run of cells two wide, whose spine runs along the seam between the two rather than down the
       middle of either.
   * - a **slab**
     - the geometry a band carries: one raised surface on columns, three lanes each way, held a level
       of altitude over the ground it crosses (``arc.geo.slab_lift``).
   * - a **station**
     - one cross-section of a slab: a point, a direction, the distance along, and the height. The
       loft takes one every fraction of a tile. A column, a girder, a lane or a spur is placed by
       reading stations, never cells.
   * - a **node**
     - where three or four lines meet: the box drawn over that cell.
   * - an **arm**
     - one line reaching a node. Its **mouth** is where it enters the box.
   * - the **fill**
     - the surface a node lays inside its own outline, between the mouths.
   * - a **spur**
     - where a raised surface comes down to the ground. Two shapes of it: the sloped cells at a band's
       own end, and the single lane a slab drops to a line below it.
   * - a **margin**
     - a strip laid beside a line and level with it. Its **lip** is the raised edge along its inside.
   * - a **lap**
     - one cell two families share at the same level, and the panel drawn where the two cross.
   * - a **stripe**
     - a marked way across a line, laid at a node's mouth.
   * - a **thread**
     - a narrow way a family draws inside its own strip, in pairs, on ties laid across it.

A tile family is a slot, and which name sits at which slot is the script's. ``Family`` is an int. The pipeline reaches for one only through the pointers the declarations fill in.

The drive
---------

.. rubric:: ``src/render/net/drive.c`` · ``arc.rules.frame`` · ``scripts/compose/frame.lua`` · ``world.lua`` · ``beat.lua``

``net/drive.c`` is the one door. It holds the only place in ``src/render`` where C calls up into a script, ``script_rule_object("frame")``, and ``tools/call_up.py`` (``ctest -R call_up``) holds the count at one. A turn of the world is handed to ``arc.rules.frame`` (``scripts/compose/frame.lua``), which decides what a turn does. Take that file away and nothing is built and nothing moves.

Two things reach the door. A **build** hands out its passes one at a time, each as a ``world`` handle from ``f:pass()``, and the script composes each with ``arc.rules.world`` (``scripts/compose/world.lua``). A **move** hands out the world that moves as a ``moving`` handle from ``f:moving()``, with the beats its clock owes, and ``arc.rules.moving`` (``scripts/compose/beat.lua``) runs them.

A build is two passes over one body (``src/render/build.c``). The first pass **grades**: it walks every network and records the surface each corridor wants, and it draws nothing, because every emitter is a no-op while it runs. The second pass **builds** the world on those shelves. Where there are no lines, or the view is underground, there is one pass. ``arc.rules.world`` composes a pass in this order: the tops and the ground field, the ground tile by tile (``arc.rules.tile``), the zone tints, then the networks. The network step discovers the runs and the bands, then settles the lanes, the classes and every pose. It fits every path, cuts every chain and builds the lane model. It lays the outlines, the meets and the power lines, and then draws the junctions, the strips and the spurs.

The build reads more than the tiles. Its key holds the view, the knobs, the furniture switch and the stamp the scripts carry, and a different key is a full build. An edit under the same key that touches fewer than an eighth of the tiles is built as an edit (``src/render/incr.c``). The mesh is kept in chunks of 32 by 32 tiles, 16 on a map, and the rebuild replaces only the chunks the edit can reach. Which chunks those are is a closure over the tables the grading pass leaves. A segment whose tiles touch a dirty tile has a new fit, and a junction at its end has a new box. A band near the dirty tiles has a new fit, and so has every spur on it. ``ctest -R incremental_rebuild`` proves the edit's build identical to a full one.

The network is the script's
---------------------------

.. rubric:: ``scripts/compose/network.lua`` · ``bands.lua`` · ``src/render/net/network.c`` · ``src/render/walk/``

Which cells form a segment, and where it runs, is the script's. The pipeline offers two readings of every cell to ``arc.rules.network``: the links a cell returns, which both sides agree on, and the links its own art claims. ``network.lua`` walks the map with them and hands back the network: what counts as a node, every run of cells that makes a segment, every junction, and the lone pieces no run reaches. A run ends for one of five reasons, and each is a different shape downstream. The five are the next node, the map's edge, a run cut short, a cell that does not return the link, and a loop back to the first cell. The list is ordered, and a run's place in it is its identity for the rest of the build.

``bands.lua`` asks the same question of the slab. It takes four readings off the map: which cell of a pair carries the point, which way the pair lies, which corner cell a curve block belongs to, and which sides of the block carry a run. It walks each band from an end, and ``arc.rules.band_start`` says which way to leave the end it finds. A band with no end is walked both ways from any cell of it.

``net/network.c`` stores what the scripts hand back: one ordered list per family of the runs, junctions, edge pieces and lone pieces, the node plane the script settled, and the bands. Nothing downstream reads the map for the network. ``walk/walk.c`` then runs each segment's own stages once the script has said which cells make one, and ``walk/cursor.c`` is the order the drive asks for them in. ``ctest -R network_discovery`` takes the two rules away and checks that the city loses its lines.

Poses and chains
----------------

.. rubric:: ``scripts/compose/poses.lua`` · ``chain.lua`` · ``links/chain.lua`` · ``src/render/net/cut.c`` · ``src/render/mesh/fit.c`` · ``arc.fit``

A pose is a point and a direction that two things share: the mouth of a junction, the end of a band's arm, the gore where a spur leaves its slab, the middle of a level meet. ``poses.lua`` settles every one from the map and the walks before any path is fitted, into ``arc.poses``. Every fit that reaches a pose is fitted to it, so the two sides meet by construction rather than by repair.

C never answers "the points between these two poses". A pass that wants a chain queues its two poses with ``net_cut_add_poses`` and nothing else. The drive asks ``arc.rules.posed`` (``scripts/compose/chain.lua``) for the points before the cut. The chain is the equal-tangent biarc, worked at the mesh's own precision through ``arc.put.f32``, so a chain built in Lua lands where one built in C floats would. The interchange and the band ends build their chains directly through ``arc.chain_between`` (``scripts/compose/links/chain.lua``) and never queue.

``net/cut.c`` is the cut queue. A pass that wants a path cut into pieces does not cut it. It queues the chain, the drive comes round and hands each queued chain to ``arc.rules.pieces`` (``scripts/compose/pieces.lua``), and the pass reads the pieces back. One rule cuts every path in the world: a segment's corridor fit, a junction's connectors, a dead end's cap, a level meet's thread, a spur's descent. ``mesh/fit.c`` is the fit itself: the corridor a segment occupies, the taut path through it, the runs, joins, bridges and steps of the chain, and the corridor sweep at each corner. Every decision in it is offered to a rule through a fan (below).

``arc.fit(points, radii, budgets)`` (``src/script/api_fit.c``) is the same cut as a service. A script hands it a path of its own invention and gets the pieces back, at any moment, with no build running. It asks ``arc.rules.pieces`` through the same ``PieceFan`` the drive uses, so two callers share one implementation. ``ctest -R fit_service`` checks it on paths invented outside the renderer.

Every chain the scripts build leaves a pose on a straight, so the curvature is nought on both sides of it. ``pieces.lua`` counts the ends that are not, in ``arc.g2``.

The fans
--------

.. rubric:: ``src/render/pipeline.h`` · ``src/render/net/types.h``

A fan is a reading offered to a rule, and the answer taken back. The pipeline gathers what a decision needs into a struct, hands it to the script as a handle of some kind with a table of methods, and reads the decision out of it afterwards. The geometry inside a fan is the pipeline's. The decision is the script's. ``ctest -R script_api`` holds the shape of every one: a rule asks for the kind of its own name, an accessor is ``api_<kind>_<method>``, every index counts from nought, ``info`` answers a table, and the linter knows every rule.

``pipeline.h`` names eight fans of the fit:

.. list-table::
   :header-rows: 1
   :widths: 16 84

   * - fan
     - what the script decides over it
   * - ``RunFan``
     - a path's steps cut into runs: which span wins each prefix, with the geometry of a candidate
       asked through ``try``.
   * - ``ChainFan``
     - a path's runs made into the chain of lines, and whether either end needs a line of its own.
   * - ``JoinFan``
     - one meet of two lines: how far ahead and behind the meet is enough for an arc to fit there.
   * - ``BridgeFan``
     - two lines that do not cross, bridged by an S: how far back each tangent point may go, and in
       what order the placings are tried.
   * - ``StepFan``
     - two lines that neither cross nor take a biarc: which points of the gap are worth a vertex, and
       whether the gap is one diagonal.
   * - ``SweepFan``
     - one corner swept with a fillet: how the search walks between the widest arc and the narrowest,
       with whether a radius stays on the corridor asked through ``holds``.
   * - ``PieceFan``
     - a fitted path's vertices cut into straights and arcs: what radius each corner is finally given.
   * - ``GroundFan``
     - a strip's elevation between the nodes at its ends: which stations are anchors and how the
       profile runs between them.

``FitFan`` is the fitted path as the script finishes it: which vertices are idle and what radius each corner gets. The network's own fans are declared in ``net/types.h``: the discovery, the band's stair, profile, slide, drop and orientation, the shelf, the crossing lanes, the margins, the outline, the lap panel and the junction's fill.

The loft and the strip
----------------------

.. rubric:: ``src/render/mesh/loft.c`` · ``w:loft`` · ``src/render/net/strip.c`` · ``src/render/net/family.c`` · ``scripts/families/``

``w:loft(pieces, profile, how)`` is the generic sweep, in ``mesh/loft.c``. It takes a chain of pieces and a cross-section written as numbers, and nothing else, and answers one strip. A script can loft a path it invented along a cross-section it defined and see it in the world. ``ctest -R loft_service`` works the face count out independently and reads the mesh back.

``net/strip.c`` is the other half: the moments the drive stops at while one strip is lofted. Each offers a reading to the rule the family named and takes its answer back. ``world.lua`` asks them in the loft's order: the taper, the profile, the drop, the works, the record, the furniture, then the curve overlay and the slab.

A family is a file in ``scripts/families``. There is no C table of them. ``arc.family.define`` names the knobs, the material, the loft kind, the tile family and the stages, and ``net/family.c`` turns that into the ``NetFamily`` the pipeline reads. The eight stages are ``control``, ``record``, ``flies``, ``taper``, ``profile``, ``works``, ``traffic`` and ``furniture``. A stage is named, not pointed at. A name the pipeline registered as a primitive binds to that C function. Any other name binds to ``arc.rules.<name>``. A knob, a loft kind, a tile family or a lane ending the C has never heard of stops the declaration, so a family half declared never appears. The four shipped declarations are ``road.lua`` (the family named ``line``), ``rail.lua`` (``thread``), ``highway.lua`` (``band``) and ``power.lua`` (``power``, drawn tile by tile and never walked).

.. caution::

   Ask through the door, never through the pointer. ``net_family_has(fam, NH_PROFILE)`` sees a stage a script answers. A call site that tests the pointer sees only the primitive and reads a stage a script answers as absent.

``ctest -R swappable`` replaces each of the four interpretations (the sweep through a corridor, an on-ramp's climb, an intersection's pattern, the lanes a road carries) with a rule of its own making and checks that the city changes with it.

The stores
----------

.. rubric:: ``src/render/net/``

Nothing in ``net/`` decides anything. A store holds what a script handed back, and a fan offers a reading to a rule. One line each:

* ``table.c``: the segment table. Every segment and band the grading pass fitted, its pieces, nodes and corridor tiles, replayed by the building pass, with the loft's station cache.
* ``lane.c``: the lane model. The lanes, their ends, what is within reach of a point, a junction's connectors, and the band's lane ends as ``scripts/compose/links`` sees them.
* ``station.c``: the stations the loft records, filed under the band they belong to. A spur is placed by reading them.
* ``shelf.c``: the corridor field. Per grid corner, what the strips passing over it ask of the ground, each corridor tile's own four corner heights, and the graded surface every later stage reads.
* ``network.c``: the network the script discovered, and the node plane it decided from.
* ``traffic.c``: the moving world. The movers, the signals and the gates, and the mesh they are drawn into.
* ``report.c``: the debug report of an area (``--area C0,R0-C1,R1``, or Shift-drag in the map view), and the component under a tile for the inspector.

The one-concept files:

* ``box.c``: the box a node draws on its outline, the signs at its arms, and its paving.
* ``meet.c``: the thread family's junction, and the level meets with lines: the lap panel and its approaches.
* ``node.c``: a node's outline and its arms' trims. The outline must be a simple ring, and the check counts every fault.
* ``margin.c``: the margin bands, gathered one path at a time and composed by the script, and the outline a junction's fill is laid on.
* ``band.c`` and ``band_walk.c``: which cells make a band, the free air a slab may sweep and the spurs beside it. The second walks one band: its stair, its profile and its drop.
* ``spur.c`` and ``spur_build.c``: every spur tile and the pose each was given. The second routes, slides and builds the descent.

The movers
----------

.. rubric:: ``src/render/net/traffic.c`` · ``scripts/compose/beat.lua`` · ``--traffic-t``

``net/traffic.c`` keeps the movers a beat steps, the signals and gates they answer to, and the mesh they are drawn into. A beat is offered to ``arc.rules.moving`` as a reading: how far a mover may go, what stands in its way, and which lap holds it. How fast a mover runs, which way it turns, what a signal shows and when a gate falls are the scripts'. There is no beat in C behind ``beat.lua``.

In each beat the trains run on their own rails, and the beat stops at each junction a train reaches so ``arc.rules.train_turn`` can choose the arm on the heading it arrived with. Then the gates swing (``arc.rules.gate``), because a car reads the gate ahead of it. Then each car is read and moved before the next is read. ``arc.rules.car_follow`` answers for the car ahead in the same lane, ``arc.rules.car_hold`` for the control at the junction ahead and for every meet on the way, ``arc.rules.signal`` (``signal.lua``) for whether that control reads red, and ``arc.rules.car_turn`` for the arm it takes. Where the frame draws, ``arc.rules.thread_signal`` settles each thread signal's aspect, the world is built, and ``arc.rules.gate_arm`` lays each gate's moving parts. The signal's cycle, its stagger and its two groups are ``arc.rules.signal_phase`` and ``arc.rules.signal_group``, settled once in the build. Every step is taken in the world's own precision through ``arc.put.f32``.

``--traffic-t SECONDS`` advances the traffic before a headless frame. A headless shot does not draw the movers, so a view hashes the same at any traffic time. The battery's traffic section checks what the moving world does: five cities advanced 26 seconds, and the ``traffic`` line the world prints, which digests every car, gate and train and every vertex the frame uploads.

The checks and the battery
--------------------------

.. rubric:: ``--mesh-check`` · ``tools/battery.py`` · ``tools/battery_ref.json`` · ``tools/fp.sh`` · ``ctest``

``arcology CITY --mute --mesh-check --run 1`` builds a city and prints what the mesh is. Each line is a witness, and ``mesh/check.c`` writes most of them:

* ``mesh check``: triangles, free edges and free vertical spans. A surface with no free edge has no crack.
* ``line clip``: samples of a line under the terrain, and the tiles they lie on.
* ``line pieces without geometry``: tiles carrying a piece that no strip reached.
* ``shapes``: shapes, and triangles claimed by no shape.
* ``collide``: face pairs of different shapes that pass through one another. No painter's order can separate them.
* ``overlap``: coplanar face pairs, and line works buried under their own margin.
* ``outlines``: junction outlines, with their spurs and self-meets.
* ``meets``: junction mouths, and those reaching no margin.
* ``lanes``: connectors without a route, and lane ends with nowhere to go.
* ``tangent fit``: each family's fit, and its hard corners.

``tools/battery.py`` is the whole check, against one reference file, ``tools/battery_ref.json``. A number that differs from the file fails, whichever way it moved. ``--update`` rewrites the file and prints what moved, so the diff of the file is the record of every accepted change. Four sections:

* ``views``: eight fixed shots at a fixed framebuffer, hashed. Two frame a highway interchange, two have the traffic advanced, and none frames water, because the water shader moves every frame.
* ``mesh``: one ``--mesh-check`` run a city over eight cities, every line above.
* ``corners``: the hard corners and the segments without geometry over every city in ``cities/``.
* ``traffic``: five cities advanced 26 seconds and hashed.

``sh tools/fp.sh`` is the short name for the views alone. ``--gate`` runs the views and one city's mesh in about a minute, and ``--only`` runs one section. ``ctest -R battery_gate`` and ``ctest -R battery_full`` drive the same script. Every geometry change is proved output-identical against the reference before it is believed. A change that cannot show this is not verified, however sound it reads.

.. caution::

   A crashed run leaves the previous PNG in place, so a hash of it would report a stale frame as a pass. The battery deletes every shot before it takes it. The hashes are per framebuffer: a display backing-scale change makes ``--win 1280x800`` produce twice the pixels with nothing moved, so the size the shot came out at is compared too.

``ctest --test-dir build`` holds the rest. The goals have a check each: ``map_read``, ``fit_service``, ``loft_service``, ``network_discovery``, ``swappable``, ``interchange``, ``incremental_rebuild`` and ``call_up``. The house rules run without a build: ``no_printf``, ``comment_lint``, ``family_words``, ``ste_comments``, ``script_api`` and ``lua_lint``.

.. warning::

   Still the sprite's, or open. A 2x2 interchange block draws a pad over the block, and the pad is a slab with no filleted turn geometry: two turns a block, chosen straightest first, where a four-arm block wants more. Some curve blocks of a band draw nothing. A band end that finds neither a road nor another band's lane is left open. The collide count is not at zero: an off-ramp lane through a road junction, a highway deck through a road strip, a power line through the ground, a footway through the road strip beside it. The fit leaves hard corners in road and rail, and a few highway curve blocks have no geometry. Head-on ramps exist. On some cities the spur builder reaches a band record that is null, and the interchange rule refuses a node where two bands reach five edges, so the band's tiles stay undrawn. The profile's grade ceiling, what a corridor may do to the ground it crosses, is a design question with no answer yet. Roundabouts are not drawn. On-ramp sprites do not take the right art in every rotation in sprite mode. Sidewalks are not routed arm to arm round a junction. The rebuild after an edit redraws more than it must. The mesh inspector is built and judged only by pointing at things. For the larger world, the highways do not go through the station cache and the band closure is loose.
