.. _enhanced-renderer:

=================
The 2.5D renderer
=================

.. container:: eyebrow

   The enhanced world · the renderer

.. container:: lede

   What the enhanced world's renderer is, and how to run it. The direction and the decisions behind it are on the :doc:`future` page. The original's own drawing, which this is measured against, is on :doc:`rendering-pipeline`. The software rasteriser, ``arcology --soft``, is checked against the original's frame. ``arcology --compare-renderers`` compares the GPU frame with the software frame. That shows where the two backends part, and proves neither one right.

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

The tool builds each of the fourteen slope codes as faces from its corner mask, projects them exactly as the rasteriser projects, and compares them against the sprite at all three zooms, for both diagonals. The table it prints is the geometry of the ``--soft --mesh`` replica of the sprites. The 3D ground is the dual ground of ``plans/terrain.md``, and it does not read this table. What it finds about the art is stated with the slopes on the :doc:`mechanics` page. The far edges that slope down from a raised corner are drawn at three pixels per four rows where the projection gives four per five, and code 13 is drawn as a flat tile raised one level. Code 7 reproduces its sprite only when cut from north-east to south-west. Every other code is indifferent to the cut.

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

``arcology --compare-renderers out.png`` draws one 1280 by 800 frame both ways, the GPU frame and the software frame cropped to the same canvas rectangle, and counts the pixels that agree.

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

What the code is
----------------

The city's cells say what the world contains. The road network follows from them: which cells form a road, which lanes it carries, how roads join, which way a ramp runs. The cells include the player's own street data, each street's class and direction. The code is C. It derives the network and stores it, gives each road its shape, and draws the world in 3D. Every number the world is built from is set in C (``geo_defaults``, ``src/render/geo.c``). The network is derived in C (``plans/network.md``, layers 0 to 2) and shaped in C (layer 3), and it is verified as records before anything is laid on it. How it is drawn is ``plans/drawing.md``, and what is drawn is ``plans/network-status.md``.

The levels
----------

.. rubric:: ``src/render`` · ``src/render/draw`` · ``src/render/mesh``

The renderer's code sits at three levels. The top runs a build. ``draw/`` sits under it, and ``mesh/`` is under both. The road network is derived under all three, in ``src/network/``, from the city's cells in ``src/cells/``. ``tools/layering.py`` holds the order of every directory under ``src/``.

**The top, src/render/.** ``build.c`` is the build from the outside, and the one door a build comes through: it derives the road network and sets the world up once. ``compose.c`` composes it: the ground, the zone tints over it, the drawing and the power lines. ``incr.c`` is what a build has to redo after an edit, and ``show.c`` what an edit's build shows. ``geo.c`` holds every number the world is built from, and the knobs the tuning window moves. ``material.c`` holds the materials named at run time. ``turn.c`` is the view from another side.

**The cells, src/cells/.** ``city.c`` is the read-only view of the city's cells, ``adapt.c`` fills it from the simulation's city, and ``terrain.c`` is what a tile's terrain byte means to the land. The network and the renderer read the cells through them.

**The road network, src/network/.** ``topology.c`` (layer 1) and ``network.c`` (layer 2, with the lane check) derive the road network from the cells, and ``street.c`` imports the street layer once. ``shape.c`` (layer 3) gives each road its line and its height, and ``bank.c`` works out the ground beside each street. Layer 0, what each cell carries, is the simulation's ``src/sim/net_cell.c``, since it reads the original's own tables. ``src/render/draw/net_graph.c`` lays the lane graph out as a diagram, which ``src/dev/net_svg.c`` writes to a file and ``src/render/draw/net_overlay.c`` lays in the outline view.

**The drawing, src/render/draw/.** ``roads.c`` draws the streets, their junctions, crossings and dead ends, ``rails.c`` the railways, turnouts and buffer stops, and ``bands.c`` the highways' decks, columns and caps. Each reads the network's records and lays them through ``mesh/``. Every shape it opens names the record it draws, and a strip of a lane names its lane, so the inspector finds a record from the triangle under the pointer.

**The shape primitives, src/render/mesh/.** ``emit.c`` puts triangles, walls, tops and boxes. ``shapes.c`` puts line quads, fans and wires over them, and ``slab.c`` lays a closed slab. ``shape.c`` is the table of what each shape is and which triangles are its own. ``mesh.c`` builds the mesh and answers the query tool. ``check.c`` is the watertightness. ``poly.c`` and ``poly.cpp`` are a polygon's area, its triangles and its union. ``surface.c`` samples the drawn ground, ``tile.c`` is what a tile is and the ground field, ``ground_bytes.c`` what a tile's building byte means to the ground, ``ground.c`` draws the ground tile by tile, and ``model.c`` holds the models, the power line's pylon among them, and draws them.

The vocabulary
--------------

The network's words are ASAM OpenDRIVE's and the original game's own: a road, a ramp, a highway, a crossing, a bridge, a crosswalk. The words below are the ones this code adds. Each is defined by what it is.

.. list-table::
   :header-rows: 1
   :widths: 18 82

   * - word
     - what it is
   * - the **road network**
     - the records of everything travelled, in ASAM OpenDRIVE's structure: roads with lane sections
       and lanes, junctions with connections and lane links, ``default`` junctions through connecting
       roads, ``direct`` junctions where a road divides or two join, a contact point at a road's start
       or end, and ``s`` along a road and ``t`` across it. ``plans/network.md`` is its design.
   * - a **lane**
     - an OpenDRIVE lane: one lane of one lane section of a road, with a signed id, a type and a
       width.
   * - a **link**
     - a lane's end carried on into a lane's start, at one node of the lane graph. A merge is a node
       where two lanes link into one, and a diverge a node where one links into two.
   * - a **band**
     - a run of cells two wide, whose spine runs along the seam between the two rather than down the
       middle of either.
   * - a **node**
     - where three or four lines meet: a ``default`` junction.
   * - an **arm**
     - one road reaching a node.
   * - a **connecting road**
     - a road inside a junction, from the lane arriving by one arm to the lane
       leaving by another.
   * - a **spur**
     - where a raised road comes down to the ground: the sloped cells at a band's own end. The road
       of one lane between a band and a line below it is a road of its own, joined to the band's road
       at a ``direct`` junction.

The drive
---------

.. rubric:: ``src/render/build.c`` · ``src/render/compose.c``

A build composes the world once. ``build.c`` hands the world out, and ``compose_world`` (``src/render/compose.c``) composes it in this order: the tops and the ground field, the ground tile by tile, the zone tints, the streets and railways, then the power lines. It names each step to whatever is showing the loading bar. When it ends, the C lays the outline view's lane graph where the view is on, and cuts the mesh into its chunks. An edit's build then offers the pieces it changed to the edit show. The ground beside a street (``src/network/bank.c``) grades the ground to the streets the mesh draws.

The build reads more than the tiles. Its key holds the view and the knobs, and a different key is a full build. An edit under the same key that touches fewer than an eighth of the tiles is built as an edit (``src/render/incr.c``). The mesh is kept in chunks of 32 by 32 tiles, and an edit rebuilds the chunks within ``net_halo`` tiles of its own, as a window. The windows of chunks, the chunk store and a city's limits are in ``plans/limits.md``.

The network
-----------

.. rubric:: ``src/network/topology.c`` · ``src/network/network.c`` · ``--net-check`` · ``--net-svg``

``plans/network.md`` is the design. The network is built from the map and the street layer, in layers, each from the layers above it. The C derives every layer, and ``plans/drawing.md`` is how the shaped network is drawn. The layers are in these files:

.. list-table::
   :header-rows: 1
   :widths: 34 66

   * - file
     - what it settles
   * - ``src/sim/net_cell.c``
     - layer 0: what each cell carries, as parts. A part names its network, the edges it joins and its
       kind: a slope, a crossing, a ramp, a tunnel mouth, a slab.
   * - ``src/network/topology.c``
     - layer 1: the junctions (nodes, dead ends, ramps, level meets, map edges and seams) and the roads
       between them. A road that crosses a chunk's border between two straight units is cut there at a
       seam. Each pair of tunnel mouths is one road.
   * - ``src/network/network.c``
     - layer 2: the road network, before any fit. It holds roads and their lane sections, lanes,
       junctions, connections, controls, the lane links and the lane graph's nodes. Each ramp's
       junction is the band unit beside it, with its entry or exit lane. The sidewalks are ``sidewalk`` lanes, and a node's corners and crosswalks are
       connecting roads. ``--net-dump`` prints it, ``--net-check`` holds it to the lane check, and
       ``--net-svg FILE`` draws its lanes and sidewalks as a diagram.
   * - ``src/network/shape.c``
     - layer 3: each road's line from above (``planView``) and its height along it
       (``elevationProfile``), and each junction's boundary. ``--net-check`` holds it to its check.

``arcology CITY --mute --net-check`` builds the topology and the road network alone, with no mesh, and holds them to the lane check. Every lane of travel goes on into a lane at the end it runs to, and a lane feeds it at the end it comes from. A stop needs no link: the map's edge, the dead end of a band or a railway, and the dead end a one-way street runs from or to. A sidewalk needs a lane at each end. ``ctest -R net_check`` (``tools/net_check.py``) runs it over every city the repository ships. ``--net-svg FILE`` writes the lane graph as a diagram, and the outline view lays the same diagram over the bare ground. One layout draws both (``src/render/draw/net_graph.c``), from the records alone. Each lane and sidewalk crosses every cell border at a port of its own, in the color of its type, with an arrow the way it runs. Each node of the lane graph is placed: at a port where it lies at a road's contact, and a step into the junction where an exit lane divides or lanes join an entry lane. Each lane of a connecting road runs rounded from the node at its start to the node at its end. A link between two ends that do not meet is gray, and a lane the check fails is red.

The checks
----------

.. rubric:: ``--mesh-check`` · ``--tile-dump`` · ``tools/architecture.py`` · ``ctest``

``arcology CITY --mute --mesh-check --run 1`` builds a city and prints what the mesh is. Each line is a witness, and ``mesh/check.c`` writes most of them:

* ``mesh check``: triangles, free edges and free vertical spans. A surface with no free edge has no crack.
* ``line clip``: samples of a line under the terrain, and the tiles they lie on.
* ``shapes``: shapes, and triangles claimed by no shape.
* ``collide``: face pairs of different shapes that pass through one another. No painter's order can separate them.
* ``overlap``: coplanar face pairs, and line works buried under their own margin.

A change is proved by a property measured against a whole build of the same city: the mesh check's counts, the dump of the build (``--tile-dump all``) compared before and after, and the lane check. ``tools/architecture.py`` counts the gaps between the network and its design, and each count may only fall.

.. caution::

   No frame is a reference while the network is being built. A frame is for a person to judge, never a measure.

``ctest --test-dir build`` holds the rest. ``ctest -L lint`` runs without a build: ``no_printf``, ``comment_lint``, ``ste_comments``, ``architecture`` and ``layering``, with the format and the generated files' checks. No test builds a city: ``--mesh-check`` on a whole build is run by hand.

.. warning::

   Open. What the mesh draws of the network, and what it leaves as a gap, is on ``plans/network-status.md``. A junction's signals and stop signs are filed as controls, and nothing reads them. Roundabouts are not in the network. On-ramp sprites do not take the right art in every rotation in sprite mode. ``TODO.md`` holds the rest.
