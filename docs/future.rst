.. _future:

============
Future plans
============

.. container:: eyebrow

   The enhanced world · decided, not built

.. container:: lede

  The front page states the goal: a game that supports everything original, and an engine far more flexible than that. This page is where the second half lives. Everything on it is decided and not built. Each item carries its status, and where a decision rests on a measured fact the fact is stated with it.

.. warning::

   This page holds long-range ideas. A step marked *decided* is a direction the project has chosen, not a description of the reconstruction. A step marked *built* is described on the renderer pages. The work in progress is in ``plans/``. The checked facts that the decisions rest on are on the Mechanics and renderer pages.

Two modes
---------

.. container:: eyebrow

   Decided

.. container:: col

   **The faithful mode** is the port: original saves, sprites, sounds and music, and a simulation checked against the original by every oracle check the project has. It is the baseline and it stays checkable forever.

   **The enhanced mode** is the sandbox: cliffs of any height, water as a body with physics, weather, regions beyond one city. Original content imports into it exactly and lives under the richer rules. The original save format is therefore an import and the reference baseline, not the limit of what the engine holds. Every world saves as ``.arco``. A ``.sc2`` is only loaded.

The renderer becomes 2.5D
-------------------------

.. container:: eyebrow

   Decided

.. container:: col

   The terrain is drawn as geometry. Everything on it stays a sprite, drawn where the original draws it, in the original's order. Only the terrain sprites are replaced: the fourteen slope shapes and their submerged, shore and channel variants, the flat ground diamond, and the map-edge skirt.

   **The camera reproduces the original projection exactly.** The projection is linear in column, row and altitude, and its vertical scale of 0.75 tile heights per level is not what a tilted orthographic camera produces: it is an oblique projection, one matrix, whose depth axis is free to choose. Depth is chosen as the sweep order, ``row + col``, so a depth buffer reproduces the painter's algorithm tile for tile. The mesh is drawn with depth writes. The sprites follow in the anti-diagonal order, tested against the terrain and writing no depth. Every tile corner lands on an integer pixel at every zoom, so exact silhouettes are attainable rather than approximate.

   **The four snap views are the only views in this phase.** Expressive views follow it, and nothing built in it may foreclose them: the projection matrix is the only thing that changes.

   **The ground is the dual ground** (``plans/terrain.md``). The land is a height per tile. A tile's middle stands at its height, an edge middle at the mean of the two tiles that share it, and a corner at the mean of the four. So each tile is eight triangles, and neighbours always agree on the points they share. A top of its own, such as a pad, a sloped network piece or water, holds the points it touches. The dual ground does not copy the sprites' faces. The ``--soft --mesh`` replica of the sprites does that (:doc:`enhanced-renderer`).

Later, in this order
~~~~~~~~~~~~~~~~~~~~

.. container:: eyebrow

   Open

.. container:: col

   - **A material per terrain band lit by a fitted sun**: built: one material for all land, and a sun fitted to the four plain slopes (:doc:`enhanced-renderer`). On the replica of the sprites' faces, measured against the land art on one frame, 1,894 of 204,000 pixels are uncovered: the diamonds' edge rows. The material lights the dual ground the same way. Relighting at the snap angle has no stated error.
   - **A water plane**: built the other way round, as a shader painted inside the water sprites' own outline, so the shore is the art's and only the blue moves (:doc:`enhanced-renderer`). A plane over a continuing bed is not needed for this.
   - **The GPU path**: built, and identical to the software rasteriser at 32 and 8 px on three cities, within ten pixels at 16 px.
   - Buildings as geometry rather than billboards: open, and not foreclosed by billboards.

Terrain in the enhanced mode
----------------------------

.. container:: eyebrow

   Decided

.. container:: col

   **The one-level rule is the original's, and the enhanced mode breaks it on purpose.** The fourteen shapes express a lift of one level, and two pieces of code keep the world inside that: the raise and lower cascade (``$8758``, ``$896C``), which pushes neighbours along so no edge exceeds a level, and the pit rule in ``fixTerrain`` (:ref:`$128DE <rt-128DE>`), which raises a tile lower than all eight neighbours rather than leave a hole.

   The enhanced mode does not keep that rule. Its land and its cliffs are set out in ``plans/terrain.md``:

   - **The land is a height per tile.** The slope codes (``XTER``) are not read for the land. Only a sloped network piece and the seabed read them.
   - **The ground is the dual ground**, as above.
   - **A cliff** lies between two land tiles side by side that are more than one level apart. Water is never a cliff: it meets land as a shore.
   - **The lean.** The lip of a cliff sags toward the step below, and a small foot rises at its base.
   - **A broken face.** The face is laid as facets, a ledge a level.
   - **One layer cake.** Every cut shows the same strata, as runs of height, with the topsoil 0.2 levels under the surface. The foot shows scree, and patchy scree lies at the foot of the face.
   - **The map's edge** is a section through a larger world. Grass runs to the edge with no wear, and its cut goes down to a floor below the lowest land.
   - **No height cap is planned.** The store holds a height up to 31 (``plans/terrain.md``).

   The cascade survives as a brush, not a law. The pit rule goes, because a pit is a crater floor.

   .. admonition:: To be read, not assumed
      :class: warning

      Placement rules that read the slope code, roads in particular, must refuse a cliff edge. Those routines have not been read for it.

   **Deformation is a brush plus the rules.** A crater or a cone is a profile, naturally a signed-distance shape, sampled onto the tile heights. Then the rules run, and the mesh is rebuilt from the heights. The mesh never has state of its own. Whether ordinary edits keep the cascade as default behavior is open. With it, everyday terrain stays within one level, and only disasters and an explicit cliff tool break the rule.

   **Land is generated from a seed and sliders.** OpenTTD's TerraGenesis is the model. The original's own generator is ported as a classic choice for a single city.

Water as a body
---------------

.. container:: eyebrow

   Decided

.. container:: col

   The land heightfield is the bottom of a dish and the water is one continuous body resting in it, with a depth and a horizontal velocity everywhere; the tiles only report where it is. The model is the shallow-water equations: depth-averaged, one column per cell, which is the physics of a long wave in a dish at city scale and the standard model of a tsunami. It is a real fluid simulation in two dimensions, not a volumetric one; breaking crests and spray are rendering effects on top of the depth field.

   - **Resolution**: about four cells per tile, floating-point depth and velocity, with the bathymetry sampled from the tile quads including their steps.
   - **The two truths**: the fluid state is the physics and render state during an event; at rest it quantises back into the integer water level in ``ALTM`` bits 5 to 9, from which the shore and submerged codes are derived exactly as the original derives them. Any state the water reaches is drawable at the snaps with existing sprites.
   - **A tsunami** is an event that displaces the water or the floor, then propagation, shoaling as the floor rises, and run-up onto land tiles, which is the flooding. The original's flood disaster becomes a special case.
   - **Containment** is a property of the equations, not a feature. Cliffs are what make real dishes: a crater becomes a lake, a breach a channel, a drop of several levels a waterfall.
   - **The map edge** is open sea: waves leave rather than reflect.

   .. admonition:: Open
      :class: warning

      Whether a wave over a tile floods it live during the event or from the settled state at its end; this decides how destructive a wave is. And the internal water state is richer than the save file, the first place the sim and the render would hold different truths.

Regions
-------

.. container:: eyebrow

   Decided

.. container:: col

   A region is a Game-of-Life grid. Its rules are functions of a cell's neighborhood, and undeveloped terrain is the quiescent state that costs nothing. The region is fixed and very large, and the game shows it as a map. Its size is a parameter, so it can grow later without moving land. A chunk is 32 by 32 tiles, the storage and streaming unit. The original's data layers run at half and quarter resolution, and 32 is a multiple of 8, so the growth-slice arithmetic stays clean. A 1995 save is a 128 by 128 patch of tiles. Importing it writes that patch into the chunks it spans, and its edge is stitched to the land around it. Active chunks hold development or water in motion. Dormant chunks hold heights and are never ticked.

   **Most of the original's rules are already local**: the terrain rule reads eight neighbours; pollution, land value, coverage, crime and density are bounded kernels; trips walk a bounded distance; power and water are flood fills, global in extent and local in rule; the growth scan already visits the map in sixteen slices on sixteen days. The original's "neighbour connection" at the map edge becomes a real neighbour, which makes that rule more faithful, not less.

   **Three rules read the whole board and must generalise:**

   .. list-table::
      :header-rows: 1
      :widths: auto

      * - Rule
        - In the original
        - In a region
      * - The city centre
        - one centre per city, land value falls off with distance to it (:ref:`$2317E <rt-2317E>`)
        - a centrality field, a smoothed density every tile reads locally; must reproduce the original inside a single chunk
      * - Aggregates
        - population, economy, budget and demand summed over one city, in counters sized for it
        - sums over the active set are well defined; the counters overflow and are widened in the enhanced mode only
      * - The random generator
        - one stream consumed in scan order
        - a stream per city, so which parts of the world are loaded does not change what grows

   **What it costs in the reconstruction.** The sim has 128 baked into its ring loops, its layer sizes and its rotation (:ref:`$3AECA <rt-3AECA>`). It is not forked: every tile access goes through a world accessor, and the faithful mode runs the original's loops over exactly one 128 by 128 window with the original's edge behaviour, so every oracle check runs on the same code. That window exists only in the faithful mode; it is what the original's sim iterates, not a unit the world stores and not a limit on any city. The renderer draws the visible chunks, not the whole map every frame.

Cities as economic units
------------------------

.. container:: eyebrow

   Decided

.. container:: col

   A city owns what the original gives it: a budget, an economy, a population, a 25-phase clock, its centre. That model runs unchanged, one instance per city. A region is many cities plus unclaimed land. An imported save contributes its 128 by 128 patch of land and content to the world; the city's limits are then computed by the organic rule below like any other city's, so they can be smaller than the patch where its corners are undeveloped and they grow past it as the city does. Nothing about a city is 128 in the enhanced mode. The only thing that depends on a city's size is when the original's single city centre stops being adequate and the centrality field is needed.

   **Trade is the original's abstract neighbour made real.** The original keeps a neighbours table of six bytes per neighbour at ``A5+0x1EDA``, saved through ``MISC`` and drawn by the Neighbors window, and asks "Do you want to build a connection to your neighbor for $1000?" or $1500 at the map edge. In the commute model a road that leaves the edge counts as arriving. In a region:

   - connections are real road, rail, power and water tiles crossing the shared boundary, placed on both sides, with the original's prompt and cost in the faithful mode;
   - a trip leaving a city arrives in the neighbour, whose size can weight it;
   - the flood fills cross the boundary through a connection, so a utility surplus really flows next door, and pricing it is the deal the panes imply, landing in both budgets;
   - migration, which the original already models in both directions from "outside", becomes exchange between cities, so the region conserves people.

   **Time**: each city keeps its own phase counter, as the original saves it, on one shared region day. Distant cities may pause; a paused neighbour is a frozen trading partner, which is what a far-away city looks like from here anyway.

   .. admonition:: To be read before trade is designed
      :class: warning

      How the original evolves its neighbours table is not reconstructed. It is read in the Neighbors window and in a menu handler at ``$3BC2C`` and nowhere in the documented passes. Trade should extend that rule, not replace it.

Organic city boundaries
-----------------------

.. container:: eyebrow

   Decided

.. container:: col

   A city is a mask over the world grid, an owner id per tile. Its limits are its roads, zoning and buildings, plus a glow around them, so they grow as the city does. The player zones inside the limits. Cities are independent, and they interact. Where the limits of two cities touch, the cities may grow into each other. The urban-growth literature has models for where development attaches: diffusion-limited aggregation, correlated percolation and constrained cellular automata. They are candidates for the virtual player's policy. The original's growth engine decides, tile by tile, where development happens, and it is not replaced.

   The box in the original is an array bound, not a rule, and three places it leaks into semantics have answers: the sixteen growth slices become row bands of the mask's bounding box, so a box city gets the original's slices exactly; the map-edge rule becomes a mask-edge rule, arriving only where another city is; and the half- and quarter-resolution data layers become world fields rather than per-city arrays, which is a correction, since a neighbour's pollution should drift across a border. The owner id is recomputed only at the frontier when roads, zones or buildings change.

   .. admonition:: Open
      :class: warning

      Whether two cities that grow into each other also merge into one economy, as the percolation model says real ones do.

Road networks drawn better than their cells
-------------------------------------------

.. container:: eyebrow

   Decided

.. container:: col

   The renderer draws a road network better than the cells it stands on. A diagonal is drawn as a diagonal, not as a staircase. A highway bends where the original could only turn. A ramp takes its best path. A building near a highway is drawn smaller, or shifted within its cells, so it never blocks a ramp. The simulation's data never changes: only the drawing departs from the cells. How it is drawn is not designed (``plans/network.md``).

Two seats, one action layer
---------------------------

.. container:: eyebrow

   Decided

.. container:: col

   Growth is driven by either seat. The sim never grows a city by itself: every change to the world is an action, and the human issues actions through the interface. A virtual player issues the same actions through the same interface, so a city can be placed and watched growing over the years, and the human can sit down in any city and take over. Nothing in the simulation loop knows which seat an action came from, so the faithful mode stays checkable, and the growth models above are the virtual player's policy rather than a rule of the world. An action layer both seats drive is also what a replay file and a headless test need.

The minimum playable game
-------------------------

.. container:: eyebrow

   Decided

.. container:: col

   The first thing to play is one imported 1995 city, simulated at the original's speed steps. The 2.5D renderer draws it with the original sprites over the dual ground, and the water switches from its sprites to a shader. All of these parts exist (:doc:`enhanced-renderer`). What this page lists as not built separates them from a game.

The interface
-------------

.. container:: eyebrow

   Decided

.. container:: col

   **The canon interface is the original's, and its port has begun.** The game's interface (:doc:`enhanced-renderer`) is the original's tool palette from its own picture, its menus from its own menu resources, and System 7 chrome, with Dear ImGui as the engine underneath and behind a C interface (``src/ui/ui.h``). What remains: the sub-tool menus and the tools themselves, which need the original's placement routines reconstructed; the map, ordinances, industry, neighbours and newspaper windows from their pictures; the shift-click help from ``TEXT`` 1100 onward; and rotation.

  **The original's interface is to be ported from its own resources.** The art is intact in the application's resource fork: the tool palette, status bar, demand bars, newspaper mastheads, budget panels and growth icons are ``PICT`` resources, the thirty tool cursors ``CURS`` resources, the menus ``MENU`` resources, and the shift-click help texts, ``TEXT`` 1100 to 1133, enumerate the toolbar's thirty-four buttons in order. There is no dialog resource: every window is a hand-drawn ``NewCWindow`` over a ``PICT`` with its own hit-testing. The fonts are the system's (Chicago, Geneva, Monaco), not the game's. ``tools/pict.py`` decodes the QuickDraw ``PICT`` opcodes, and ``tools/rezfork.py`` reads the fork.

Open questions
--------------

.. container:: eyebrow

   Open

.. container:: col

   - What "more flexibility" in sculpting means on screen: freeform terrain quantised and shown, or a surface that stays continuous on screen while the sim reads the grid.
   - At what city size the centrality field replaces the original's single centre.
   - Weather, named in the goal, with no design.
   - Sound and music support, named in the goal, with no design.

The road system spec, part by part
----------------------------------

.. container:: eyebrow

   Open

.. container:: col

  The road system specification (:doc:`appendix-road-spec`) is a real-world reference for classes, dimensions, markings and hardware. It is not the design of the road network: that is ``plans/network.md``, and ``plans/network-status.md`` says what is built. The table below is reference, not the design. It sets out the specification's feasibility against what the original game and its save format provide, part by part. The save stores one road family (``XBLD`` ``0x1D`` to ``0x2B``), one highway family, rails and power lines, each with fifteen pieces on a four-neighbour grid; it stores no road class, no one-way flag, no lane count and no true diagonal. Everything the spec keys off the class must therefore be derived, or belong to the enhanced mode's own data.

.. list-table::
   :header-rows: 1
   :widths: 20 40 40

   * - Spec part
     - What the data and the original give
     - Feasibility
   * - 1.1 Road classes
     - No class in the save. One road family, one highway family. Traffic density per half-tile (``XTRF``) and the zoning around a tile are the only signals.
     - The street layer: the player's class and direction for each street, per tile edge, in the ``.arco``. An imported city gets it once, from the built population along each street and its traffic, and it never follows the traffic after. HIGHWAY from the highway pieces. ALLEY, FREEWAY and PEDESTRIAN need enhanced-mode fields of their own.
   * - 1.2 Cross-section
     - Nothing in the data beyond the tile.
     - Every element is a lane of its road, at its class's width: a ``sidewalk`` lane outermost each side, a median, the driving lanes. Feasible in full.
   * - 1.3 Markings
     - None in the data.
     - Longitudinal markings by class and transverse markings at legs are decals on the strip; arrows and stencils are small textured quads.
   * - 3.1 Connectivity and shape
     - Four neighbours, read from each piece's own art. No diagonal connections exist in the data; a diagonal is a staircase of corners.
     - Shape classes fall out of the links. Diagonals come from staircase straightening (3.10 step 4), not from the data.
   * - 3.2 Roles
     - Nothing in the data.
     - Roads, junctions and lane sections are derivable from the links and the class.
   * - 3.3 Intersection footprint
     - The original's junctions are one tile; the sim places one piece per tile.
     - One-by-one is native. Multi-tile footprints for arterials are a renderer-side union of neighbouring junction tiles, feasible where the player has laid them.
   * - 3.4 Control selection
     - The art puts a signal at every T and crossing.
     - By class as the spec tables it: stop signs and uncontrolled legs on locals, signals on arterials. Straightforward once classes exist.
   * - 3.5 Corner returns
     - Nothing in the data.
     - A corner's sidewalk is a connecting road of the junction with one ``sidewalk`` lane, swept at the class radius. Feasible now.
   * - 3.6 Lane assembly
     - Nothing in the data.
     - A turn pocket is a lane gained at a lane section's start, a node of the lane graph; its arrows are markings.
   * - 3.7 Straight segments
     - The tile is the unit; the original restarts its art per tile.
     - Phase continuity reads ``s`` along the road. Furniture is placed by its road, ``s`` and ``t``, on the filed sidewalk lane.
   * - 3.8 Right-angle corners
     - One corner piece per tile.
     - The tight fillet is native. The wide sweep consumes the inside tiles, which is an editor rule for the enhanced mode.
   * - 3.9 Diagonals
     - Staircases only.
     - Nothing draws a diagonal: how the network is drawn is not designed. A 45-degree merge is an idea for it: a fillet arc rather than a mitre and fan. Skew and five-way intersections cannot occur from the data.
   * - 3.10 Geometry pipeline
     - Not applicable to the data.
     - Not the network's design. ``plans/network.md`` derives the topology and then the road network. How it is drawn is not designed, and it is to be designed in C. Straightening and fillets belong to the drawing.
   * - 3.11 Elevation, bridges, tunnels
     - Slope pieces, bridges and tunnels are pieces in the data.
     - A slope piece is a top of its own on the dual ground. Nothing draws a road, a bridge or a tunnel as geometry, and their sprites stand in.
   * - 3.12 Transitions and one-way
     - No one-way in the data.
     - Enhanced-mode data only.
   * - 3.13 Layer order
     - The sweep's painter order, which the mesh reproduces as depth.
     - Each layer is a pass of one surface, a step nearer the camera than the one under it, and paints only its own pixels (``plans/network.md``, "What the drawing keeps").
   * - 3.14 Validation
     - The sim's placement rules are the original's.
     - Placement rules belong to the enhanced mode's editor, not the renderer.
   * - 3.16 Signal operation
     - The art puts a light at every T and crossing; the original has no phases.
     - NEMA rings and barriers, actuation and coordination are a simulation feature; the renderer needs a phase state per movement to draw.
   * - 3.15 Railway crossings
     - Six crossing pieces in the data, ``0x43`` to ``0x48``: a road under a power line, over a rail, a rail under a power line, each on both axes. No control kind, no angle but square.
     - A level crossing is a junction of the road network where each network's lanes go straight across, found by the topology, and its road arms yield to the railway. Control by road class as the spec tables it: crossbucks, flashers, gates as props; the RXR stencil and stop line as decals. Skew crossings cannot occur.
   * - 5.1 Rail classes
     - One rail family, one piece layout; no class, no track count, no traffic count for rails in the save.
     - One class for the shipped cities, chosen as the reference look (MAINLINE, one or two tracks); the others need enhanced-mode fields.
   * - 5.4 Placement and connectivity
     - The data has T and cross rail pieces and one-tile corners, which the spec forbids; the original built them.
     - Render what the player laid: a T as a turnout, a crossing as a diamond, a one-tile corner as the tightest arc; the placement rules belong to the enhanced mode's editor.
   * - 5.5 Curve geometry
     - Staircases and one-tile corners only.
     - Larger fillets than a road's where the legs allow, claiming the inside tiles as the spec's arc does (rail encroaches by default); spirals are a refinement.
   * - 5.6 Signalling
     - Nothing in the data.
     - Block signals every eight to twelve tiles, absolute signals at turnouts and diamonds, whistle posts before crossings: props on an arc-length walk, as the road's furniture pass.
   * - 5.7 Stations and 5.8 yards
     - Rail stations are buildings in the data; no yards.
     - A station's platform beside the track where a rail station building adjoins it; yards need the enhanced mode.
   * - 5.9 Rail layer order
     - As 3.13.
     - Ballast, ties, rails, hardware, signals, wire: as 3.13.
   * - 5.10 Rail validation
     - The original's placement rules.
     - The enhanced mode's editor.
   * - 6 Hardware
     - Nothing in the data; the sprites' props are a few pixels.
     - Every dimension is given in metres; at the road's scale, about seventeen metres to the tile and seven to the level, they are props of the mesh, and the state hooks of 6.7 are lamp codes and box angles.
   * - Traffic
     - The original places a car sprite on a tile whose traffic passes a threshold; cars do not move.
     - Cars as small meshes on the filed lanes, following lane links at the nodes and holding at red, in a per-frame buffer; the same threshold keeps the original's density.


The road network and the street layer
-------------------------------------

.. container:: col

   The road network, its lanes and the street layer are designed in
   ``plans/network.md``.

Part 7, the raised highways: what the save file gives us
--------------------------------------------------------

.. container:: lede

   Part 7 of the :doc:`road spec <appendix-road-spec>` is engine-agnostic:
   it describes decks, ramps and interchanges without saying which byte
   is which. This is that bridge, and it is read off the shipped cities
   rather than off the sprite sheet — a sprite says what a tile looks
   like, its neighbours say what it *is*. ``tools/highway_map.py``
   regenerates the whole table. This section is reference: the save
   file's facts, not the network's design (``plans/network.md``).

**The spec's 2×2 premise holds.** Across the 101 cities there are 3547
squares of four adjacent highway tiles, and a straight run is a solid
block of one id. The family divides exactly where Part 7 needs it to:
eight ids that are *always* part of a 2×2 block, and twelve that almost
never are.

.. list-table::
   :header-rows: 1
   :widths: 12 10 12 30 36

   * - XBLD
     - Tiles
     - In a 2×2
     - Neighbours
     - What it is
   * - ``0x49``
     - 6905
     - 6905 / 6905
     - E+W along, one of N/S across
     - **Deck, east–west.** The two-tile band of Part 7.1.
   * - ``0x4A``
     - 7182
     - 7179 / 7182
     - N+S along, one of E/W across
     - **Deck, north–south.**
   * - ``0x4B`` ``0x4C``
     - 1729
     - all
     - road, 784 and 880
     - Deck over a **road** — the grade separation of 7.2.
   * - ``0x4D`` ``0x4E``
     - 131
     - all
     - rail, 61 of 62 and 64 of 69
     - Deck over a **rail** line, in pairs across the band's width.
   * - ``0x4F`` ``0x50``
     - 1022
     - all
     - power, 133 and 99
     - Deck over a **power line**.
   * - ``0x51``–``0x5C``
     - 3592
     - 10–243 of 300–911
     - single-width N–S or E–W runs
     - **Bridges** — the road and rail spans over water, single-width
       runs (the deck ``0x57``, the lift span ``0x58``, the raised span
       ``0x59``, with ``0x5A``/``0x5B`` over rail and ``0x5C`` under a
       power line).  They are bridges, not highway tiles, and stay the
       sprites the bridge code draws.
   * - ``0x5D``–``0x60``
     - 1642
     - 17–19 of ~400
     - one-sided; road 470–561
     - **On-ramps**, one id per direction: one tile beside a deck and
       beside a surface road, on two adjacent sides — exactly 7.3's
       "one 1×1 tile adjacent to a deck edge, connected to a surface
       road".  In the shipped cities they flank each road that crosses
       under a deck, one on either side.  Layer 0 reads each as a ramp
       piece that joins its two edges.
   * - ``0x61``–``0x64``
     - 508
     - 20 cities
     - a band's last two cells
     - **End pieces**, one id per direction: where a band stops.  They
       stay at deck height — a deck is raised end to end.
   * - ``0x65``–``0x68``
     - 2216
     - 25 cities
     - 2×2, between two bands at right angles
     - **Curve blocks**, one id per quadrant: the corner a band turns
       through into another.  A curve block is a unit of a band's road:
       the road arrives through it from the band before it and leaves
       along the band after; its four tiles are covered by the arc, less
       what the arc cuts inside.
   * - ``0x69``
     - 284
     - 20 cities
     - 2×2, four bands meeting
     - **The interchange**: where two decks cross (Toronto 4,40).  Deck
       family, not a building — its sprite, the cloverleaf, measures 2.4
       levels, and read as a structure it lifted every deck through it.
       The road network builds it as a ``default`` junction of the
       band, its movements connecting roads.

Two consequences for the implementation.

**The deck really is a band, and the id says which way it runs.** An
interior tile of a two-wide band has two neighbours along it and one
across, which is why every deck id reports a three-neighbour mask.
``0x49`` reports ``{E,W,N}`` and ``{E,W,S}``; ``0x4A`` reports
``{N,S,E}`` and ``{N,S,W}``. Nothing has to be inferred from the art.

**The crossings are the deck, not the thing underneath.** ``0x4D`` is a
deck tile in an east–west band with a north–south railway beneath it,
which is why layer 0 (``src/sim/net_cell.c``) reads two parts from it:
the band, and the rail that runs on under it. The same
holds for ``0x4B``/``0x4C`` over road and ``0x4F``/``0x50`` over power.

**What is not in the data at all.** The save file has no elevation for a
highway tile, no ramp form, no interchange type, and no air claims.
Part 7's whole apparatus — ``D`` and ``U``, the helix, the quadrant
tests, the stack — is therefore *derived on every edit* from the tile
layout alone, exactly as 7.3's closing note says it should be. There is
nothing to load and nothing to migrate; an imported city gets its ramp
forms computed the first time it is drawn.


.. _fit-safe-corridor:

An idea for the shape: safe-corridor optimization
--------------------------------------------------

.. container:: eyebrow

   Open

How the road network is drawn is not designed. It is to be designed in
C, and this is an idea for it, not a decision. A fit that
decides curvature locally and after the fact asks, at each corner, for
the widest arc that fits between its two edges. A corner with no legal
arc is then drawn hard. Nodes can lie too close to sweep, and a radius
floor that rises with the band leaves a corner with no legal arc at all.

The standard method solves it a different way. Put the path in a basis
whose CONTROL POINTS bound the curve, by the convex hull property of a
uniform cubic B-spline. Box each control point into the corridor, then
minimize curvature. Containment stops being a test applied afterwards
and becomes a constraint on the points. Curvature is continuous by
construction, so there is no such thing as a hard corner in the result.
Quadrotor and self-driving planners fit paths this way, and the civil
engineering equivalent is the tangent, spiral and arc alignment that
line design has used for a century.

Minimizing the discrete bending energy
:math:`\sum |p_{i-1} - 2p_i + p_{i+1}|^2` makes the stationary condition
a fourth difference of zero, so one sweep of projected Gauss-Seidel is

.. code-block:: text

    p[i] <- box( (4(p[i-1] + p[i+1]) - (p[i-2] + p[i+2])) / 6 )

which is a few dozen iterations of arithmetic per segment. It needs no
solver, no global system and no per-corner search.

.. caution::

   **Boxing each control point into its own tile proves nothing about the
   curve between them.** The convex hull property bounds a span by its
   four control points, so a span is safe exactly when all four lie in
   one CONVEX piece of the corridor. The corridor is a run of unit tiles,
   so its convex pieces are the solid rectangles it contains: the four
   tiles' bounding rectangle where the corridor fills it, and otherwise
   the middle tile alone, which forces the span's points together and
   turns the corner tightly but legally.

.. note::

   A fit whose pieces are dense samples has no fillets. A reader of piece
   boundaries must allow for that. A tangent read from a piece a few
   hundredths of a tile long points wherever that fragment happens to.
