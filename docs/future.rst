.. _future:

============
Future plans
============

.. container:: eyebrow

   The enhanced world · decided in conversation, not built

.. container:: lede

   The front page states the goal: a game that supports everything original, and an engine far more flexible than that. This page is where the second half lives. Everything on it was settled or raised in conversation on 1 September 2026, and nothing on it exists in code. Each item carries its status, and where a decision rests on a measured fact the fact is stated with it.

.. warning::

   Nothing on this page is implemented. A step marked *decided* is a direction the project has chosen; it is not a description of the reconstruction. The checked facts that the decisions rest on are on the Mechanics and renderer pages.

Two modes
---------

.. container:: eyebrow

   Decided

.. container:: col

   **The faithful mode** is the port: original saves, sprites, sounds and music, and a simulation checked against the original by every oracle check the project has. It is the baseline and it stays checkable forever.

   **The enhanced mode** is the sandbox: cliffs of any height, water as a body with physics, weather, regions beyond one city. Original content imports into it exactly and lives under the richer rules. The original save format is therefore an import and the reference baseline, not the limit of what the engine holds. A city that never used an enhanced feature still saves as a 1995 file; one that did has outgrown it.

The renderer becomes 2.5D
-------------------------

.. container:: eyebrow

   Decided

.. container:: col

   The terrain is drawn as geometry. Everything on it stays a sprite, drawn where it is drawn today, in the order it is drawn today. Only the terrain sprites are replaced: the fourteen slope shapes and their submerged, shore and channel variants, the flat ground diamond, and the map-edge skirt.

   **The camera reproduces the original projection exactly.** The projection is linear in column, row and altitude, and its vertical scale of 0.75 tile heights per level is not what a tilted orthographic camera produces: it is an oblique projection, one matrix, whose depth axis is free to choose. Depth is chosen as the sweep order, ``row + col``, so a depth buffer reproduces the painter's algorithm tile for tile. The mesh is drawn with depth writes; the sprites follow in the anti-diagonal order, tested against the terrain and writing no depth. Every tile corner lands on an integer pixel at every zoom, so exact silhouettes are attainable rather than approximate.

   **The four snap views are the only views for now.** Expressive views come later, and nothing built now may foreclose them: the projection matrix is the only thing that changes.

   **At the snap views the frame is identical to today's**, under five conditions, each of which is a place it could silently stop being true:

   - corner heights are ``ALTM`` plus the lifted corner bits, one level per lift;
   - the side faces under a raised edge are part of the geometry, because the sprite paints them;
   - the diagonal that splits a non-planar tile is read from its sprite, code by code, and so is each face's coverage, because the sloped far edges are drawn at 3:4 where the projection gives 4:5;
   - each face is painted with the sprite's own pixels;
   - depth equals sweep order, so the nearer tile overpaints exactly where the sprite did.

   Two facts from the shipped cities decide the mesh's topology. **A lifted corner is one level, never two**: the art confirms it, sprite 257 raising two corners by twelve pixels and sprite 263 three. **Shared corners disagree in the data**: across all 103 cities, under that reading, neighbouring tiles agree on a shared corner's height 97.0% of the time, and no other reading of the corner bits or of ``ALTM`` comes close. The remaining 3% are diagonal ramps, where altitude climbs one level per tile in both directions and no shape spans the two levels a plane would need; the high-side tile puts the corner one level above the low-side tile, and the nearer sprite overpaints the mismatch. So the terrain is separate per-tile quads overpainted in sweep order, because that is what the art contains. A joined surface would draw a ramp the game never drew.

What is done first
~~~~~~~~~~~~~~~~~~

.. container:: eyebrow

   Done · 1 September 2026

.. container:: col

   - **The provenance plane** is in the software rasteriser, written by ``--provenance``: the sprite that painted each pixel, with a flag for the shadow pass. Adding it changed no pixel of any existing output.
   - **The per-code table** is ``tools/terrain_shapes.py`` and ``assets/terrain-shapes.json``. It found two things the mechanics page now states: the far edges that slope down from a raised corner are drawn at three pixels per four rows where the projection gives four per five, so a polygon filled from the true geometry differs from the sprite by one pixel on about half the rows of such an edge, and code 13 is drawn as a flat tile raised one level, not as a saddle. Code 7 fixes the diagonal at north-east to south-west; every other code is indifferent.
   - **The 2.5D composition** is ``arcology --soft --mesh`` (see :doc:`enhanced-renderer`): the terrain drawn first as its own pass writing a depth plane, everything else after it in the sweep's order and tested against the plane. With the terrain drawn back to front it reproduces every shipped city file at 8 px and the ten dense and hilly cities at 16 and 32 px, pixel for pixel, while rejecting over a million sprite pixels behind nearer terrain at 32 px. The plate-and-step test was superseded by that, since a whole city is a harder plate. The :doc:`enhanced-renderer` page carries the table.

   What the first finding changes: at the snap views the coverage of each terrain face comes from the sprite's own mask, and polygon rasterisation of the geometry is the off-snap path, one pixel different along those edges by the art's choice.

Later, in this order
~~~~~~~~~~~~~~~~~~~~

.. container:: eyebrow

   Open

.. container:: col

   - **A material per terrain band lit by a fitted sun**: built, one material for all land on a mesh that replicates the sprites' faces, and a sun fitted to the four plain slopes (:doc:`enhanced-renderer`). Measured against the land art on one frame: 1,894 of 204,000 pixels uncovered, the diamonds' edge rows. Relighting at the snap angle has no stated error.
   - **A water plane**: built the other way round, as a shader painted inside the water sprites' own outline, so the shore is the art's and only the blue moves (:doc:`enhanced-renderer`). A plane over a continuing bed is not needed for this.
   - **The GPU path**: built, and identical to the software rasteriser at 32 and 8 px on three cities, within ten pixels at 16 px.
   - Buildings as geometry rather than billboards: open, and not foreclosed by billboards.

Terrain in the enhanced mode
----------------------------

.. container:: eyebrow

   Decided

.. container:: col

   **The one-level rule is the original's, and the enhanced mode breaks it on purpose.** The fourteen shapes express a lift of one level, and two pieces of code keep the world inside that: the raise and lower cascade (``$8758``, ``$896C``), which pushes neighbours along so no edge exceeds a level, and the pit rule in ``fixTerrain`` (:ref:`$128DE <rt-128DE>`), which raises a tile lower than all eight neighbours rather than leave a hole.

   The extension keeps ``ALTM`` as the tile's base and keeps ``XTER`` derived. The lift predicate changes from *a higher neighbour lifts the corner* to *a neighbour exactly one level higher lifts the corner*; anything steeper is a cliff, the low tile stays flat on that side, and the mesh draws a vertical face of however many levels. The cascade survives as a brush, not a law. The pit rule goes, because a pit is a crater floor.

   What that buys: the save format is untouched, since ``ALTM`` already holds 32 levels and adjacent tiles differing by six is data the original never wrote; ``XTER`` stays derivable, so the sculptor and the disasters only produce ``ALTM``; every tile the original could make is unchanged, so the identity check still holds on all shipped cities; rotation stays free, since walls derive from altitudes like everything else; and the map-edge skirt (:ref:`$17018 <rt-17018>`) already carries the game's own dirt-cliff art, the natural paint for an interior cliff.

   .. admonition:: To be read, not assumed
      :class: warning

      Placement rules that read the slope code, roads in particular, must refuse a cliff edge, and those routines have not been read for it. A city with cliffs loaded into the 1995 game draws a flat tile beside a tall one with nothing between them; that is accepted. The extension is a mode, so the oracle checks against the original keep their meaning.

   **Deformation is a brush plus the rules.** A crater or a cone is a profile, naturally a signed-distance shape, sampled onto ``ALTM``; then the rules run and the mesh is rebuilt from the grid. The mesh never has state of its own. Whether ordinary edits keep the cascade as default behaviour, so that everyday terrain stays inside the sprite vocabulary and only disasters and an explicit cliff tool break the level rule, is open.

Water as a body
---------------

.. container:: eyebrow

   Decided

.. container:: col

   The land heightfield is the bottom of a dish and the water is one continuous body resting in it, with a depth and a horizontal velocity everywhere; the tiles only report where it currently is. The model is the shallow-water equations: depth-averaged, one column per cell, which is the physics of a long wave in a dish at city scale and the standard model of a tsunami. It is a real fluid simulation in two dimensions, not a volumetric one; breaking crests and spray are rendering effects on top of the depth field.

   - **Resolution**: about four cells per tile, floating-point depth and velocity, with the bathymetry sampled from the tile quads including their steps.
   - **The two truths**: the fluid state is the physics and render state during an event; at rest it quantises back into the integer water level in ``ALTM`` bits 5 to 9, from which the shore and submerged codes are derived exactly as today. Any state the water reaches is drawable at the snaps with existing sprites.
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

   A region is a Game-of-Life grid: conceptually infinite, rules functions of a cell's neighbourhood, and undeveloped terrain the quiescent state that costs nothing. A chunk is the storage and streaming unit, and its size is an implementation choice, not a rule: the original's data layers run at half and quarter resolution, so a chunk is a multiple of 4 tiles, and a multiple of 8 keeps the growth-slice arithmetic clean; beyond that the size is a performance question. A 1995 save is a 128 by 128 patch of tiles, and importing it writes that patch into whatever chunks it spans. Active chunks are those with development or water in motion; dormant chunks hold a heightfield and are never ticked. Region terrain is generated from a continuous field so chunk borders do not show, and an imported city's edge is stitched to the land around it.

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

   **What it costs in the reconstruction.** The sim has 128 baked into its ring loops, its layer sizes and its rotation (:ref:`$3AECA <rt-3AECA>`). It is not forked: every tile access goes through a world accessor, and the faithful mode runs the original's loops over exactly one 128 by 128 window with the original's edge behaviour, so every oracle check runs on the same code. That window exists only in the faithful mode; it is what the original's sim iterates, not a unit the world stores and not a limit on any city. The renderer's "draw the whole map every frame" becomes "draw the visible chunks".

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

   A city is a mask over the world grid, an owner id per tile, and its extent is what it can reach: its developed and zoned tiles plus everything within service reach of its road network. The player zones only at that frontier, so the limits move as the roads and services do. Between cities a tile in reach of both goes to the stronger pull, population over distance squared, which is Reilly's breaking point; a strip where neither pull crosses a threshold stays unclaimed, the gap that tree canopies leave between them, and it closes as a city grows. The models behind this are the urban-growth literature's: diffusion-limited aggregation and correlated percolation for where development attaches, constrained cellular automata for local rules, weighted Voronoi and Reilly's law for territory. They decide extent; the original's growth engine already decides, tile by tile, where development happens, and is not replaced.

   The box in the original is an array bound, not a rule, and three places it leaks into semantics have answers: the sixteen growth slices become row bands of the mask's bounding box, so a box city gets the original's slices exactly; the map-edge rule becomes a mask-edge rule, arriving only where another city is; and the half- and quarter-resolution data layers become world fields rather than per-city arrays, which is a correction, since a neighbour's pollution should drift across a border. The owner id is recomputed only at the frontier when zones or roads change.

   .. admonition:: Open
      :class: warning

      Whether two cities that meet may coalesce into one economy, as the percolation model says real ones do. The default is to keep them separate and let them trade.

Two seats, one action layer
---------------------------

.. container:: eyebrow

   Decided

.. container:: col

   Growth is driven by either seat. The sim never grows a city by itself: every change to the world is an action, and the human issues actions through the interface. A virtual player issues the same actions through the same interface, so a city can be placed and watched growing over the years, and the human can sit down in any city and take over. Nothing in the simulation loop knows which seat an action came from, so the faithful mode stays checkable, and the growth models above are the virtual player's policy rather than a rule of the world. An action layer both seats drive is also what a replay file and a headless test need.

The minimum playable game
-------------------------

.. container:: eyebrow

   Decided · 1 September 2026

.. container:: col

   The first thing to play is the original city size, simulated at the original's speed steps, drawn by the 2.5D renderer with the original sprites, with the terrain switchable to geometry and the water switchable from its sprites to a shader. All four parts exist (:doc:`enhanced-renderer`); what the page above lists as not built is what separates them from a game.

The interface
-------------

.. container:: eyebrow

   Decided · 1 September 2026

.. container:: col

   **The canon interface is the original's, and its port has begun.** The game's interface (:doc:`enhanced-renderer`) is the original's tool palette from its own picture, its menus from its own menu resources, and System 7 chrome, with Dear ImGui as the engine underneath and behind a C interface (``src/render/ui.h``). What remains: the sub-tool menus and the tools themselves, which need the original's placement routines reconstructed; the map, ordinances, industry, neighbours and newspaper windows from their pictures; the shift-click help from ``TEXT`` 1100 onward; and rotation.

   **The original's interface is to be ported from its own resources.** The survey of 1 September 2026 found the art intact in the application's resource fork: the tool palette, status bar, demand bars, newspaper mastheads, budget panels and growth icons are ``PICT`` resources, the thirty tool cursors ``CURS`` resources, the menus ``MENU`` resources, and the shift-click help texts, ``TEXT`` 1100 to 1133, enumerate the toolbar's thirty-four buttons in order. There is no dialog resource: every window is a hand-drawn ``NewCWindow`` over a ``PICT`` with its own hit-testing. The fonts are the system's (Chicago, Geneva, Monaco), not the game's. The decoder for the QuickDraw ``PICT`` opcodes now exists (``tools/pict.py``); the fork reader existed.

.. container:: eyebrow

   Built · 1 September 2026

.. container:: col

   **Kaleidoscope schemes as themes.** Built the day it was earmarked: ``tools/scheme.py`` reads a scheme into a theme pack and ``--theme`` dresses the menu bar, the menus and every title bar in it (:doc:`enhanced-renderer`); scroll bars, buttons, patterns and the inactive states remain. The earmark as it was: the user wants the interface themable with Kaleidoscope schemes, the classic Mac OS 8 and 9 theming extension whose scheme files (Kaleidoscope 2.x, resource-fork based: window frames, buttons, scrollbars, menus, patterns and colours as ``PICT``, ``ppat`` and colour resources) are still archived, for instance the mass:werk collection at https://www.masswerk.at/schemes.php. The consequences: theming stays in one place (``apply_theme`` in ``ui.cpp`` today); a scheme is textured frames, not colours alone, so an ImGui theme can take its palette from a scheme while the frames wait for the vintage interface, which draws its own; and the scheme reader is the same resource-fork and ``PICT`` work the port needs.

Still open
----------

.. container:: eyebrow

   Open

.. container:: col

   - What "more flexibility" in sculpting means on screen: freeform terrain quantised and shown, or a surface that stays continuous on screen while the sim reads the grid.
   - The chunk size, and at what city size the centrality field replaces the original's single centre.
   - Weather, named in the goal and not yet discussed.
   - Sound and music support, named in the goal and not yet discussed.
   - The original's terrain generator, for the faithful mode's new cities, against a field-based generator for region land.

The road system spec, part by part
----------------------------------

.. container:: eyebrow

   Open · for review together

.. container:: col

   The user's road system specification is the reference for the road renderer (:doc:`appendix-road-spec`, brought in whole on 2 September 2026). What follows is its feasibility against what the original game and its save format provide, part by part, with what is built so far. The save stores one road family (``XBLD`` ``0x1D`` to ``0x2B``), one highway family, rails and power lines, each with fifteen pieces on a four-neighbour grid; it stores no road class, no one-way flag, no lane count and no true diagonal. Everything the spec keys off the class must therefore be derived, or belong to the enhanced mode's own data.

.. list-table::
   :header-rows: 1
   :widths: 18 34 30 18

   * - Spec part
     - What the data and the original give
     - Feasibility
     - Built so far
   * - 1.1 Road classes
     - No class in the save. One road family, one highway family. Traffic density per half-tile (``XTRF``) and the zoning around a tile are the only signals.
     - Derive it: LOCAL, COLLECTOR and ARTERIAL by traffic thresholds with hysteresis, HIGHWAY from the highway pieces. ALLEY, FREEWAY, RAMP, ONEWAY and PEDESTRIAN need enhanced-mode fields in the save.
     - Three classes by traffic, one per segment (the median of its tiles): road, avenue, boulevard.
   * - 1.2 Cross-section
     - Nothing in the data beyond the tile.
     - Every element is an offset of the centreline: feasible in full once the pipeline of 3.10 is in.
     - Asphalt, curb and sidewalk each side inside the 0.71 band; a planted median between curbs on boulevards; embankment walls where the band stands above the ground.
   * - 1.3 Markings
     - None in the data.
     - Longitudinal markings by class and transverse markings at legs are decals on the strip; arrows and stencils are small textured quads.
     - Dashes, double yellow, lane dashes, crosswalk bars, stop line, at the art's width and never wider than a fixed fraction of the strip.
   * - 3.1 Connectivity and shape
     - Four neighbours, read from each piece's own art. No diagonal connections exist in the data; a diagonal is a staircase of corners.
     - Shape classes fall out of the links. Diagonals come from staircase straightening (3.10 step 4), not from the data.
     - Links from the art, counted only where the neighbour links back or the map ends; END, STRAIGHT, CORNER, TEE, CROSS; a lone piece as an island; staircases straightened.
   * - 3.2 Roles
     - Nothing in the data.
     - Segment, intersection, approach and transition are derivable from the links and the class.
     - Segments walked from node to node; junction tiles; a one-tile approach for crosswalks.
   * - 3.3 Intersection footprint
     - The original's junctions are one tile; the sim places one piece per tile.
     - One-by-one is native. Multi-tile footprints for arterials are a renderer-side union of neighbouring junction tiles, feasible where the player has laid them.
     - One-by-one.
   * - 3.4 Control selection
     - The art puts a signal at every T and crossing.
     - By class as the spec tables it: stop signs and uncontrolled legs on locals, signals on arterials. Straightforward once classes exist.
     - By class as the spec tables it: two-way and all-way stops with STOP signs on locals and avenues, signals where a boulevard meets or an avenue junction is busy; the cars obey both.
   * - 3.5 Corner returns
     - Nothing in the data.
     - Fillet arcs at the box corners by class radius; the sidewalk follows the offset. Feasible now.
     - Curb returns on a fillet of the sidewalk's width where two arms meet; sidewalk and curb along free sides.
   * - 3.6 Lane assembly
     - Nothing in the data.
     - Markings only, since the original's traffic model has no lanes: turn pockets and arrows as decals on approach tiles.
     - Not started.
   * - 3.7 Straight segments
     - The tile is the unit; the original restarts its art per tile.
     - Phase continuity needs arc-length per segment from the nearest stop bar. Furniture placement is one walk per segment.
     - Arc length per segment from the junction; dashes and crosswalks placed by it; signals, power poles and crossbucks as props.
   * - 3.8 Right-angle corners
     - One corner piece per tile.
     - The tight fillet is native. The wide sweep consumes the inside tiles, which is an editor rule for the enhanced mode.
     - Fillet of up to a tile, clamped to the legs; one arc per corner.
   * - 3.9 Diagonals
     - Staircases only.
     - The 45-degree band is built. The 45-degree merge should be a fillet arc rather than a mitre and fan. Skew and five-way intersections cannot occur from the data.
     - The 45-degree and 2:1 and 3:1 staircases become lines on the exact midline of their two rows of corners; the band stays 0.71 in every direction (FIT for 1:1, ENCROACH on the shallower stairs); joins to the legs by intersection and fillet; skew intersections cannot occur from the data.
   * - 3.10 Geometry pipeline
     - Not applicable to the data.
     - The right architecture, and the one the code is moving to: segments between intersections, straightening, fillets, offsets, arc-length, loft, clip per tile, intersection patches, end caps.
     - Built: segments between nodes, straightening, fillets, arc-length loft with a station at every tile edge, clipping per tile with each piece's own painter's order, junction boxes with curb returns, end caps on local roads with room. Not built: offsets beyond the band, intersection patch unions.
   * - 3.11 Elevation, bridges, tunnels
     - Slope pieces, bridges and tunnels are pieces in the data.
     - Slopes are native to the mesh. Bridges and tunnels as geometry are feasible; their sprites are kept until then.
     - Slopes follow the terrain plane; the profile is capped at a level per tile and rides up over cuts on an embankment; bridges, tunnels and highways keep their sprites and the band runs square onto them.
   * - 3.12 Transitions and one-way
     - No one-way in the data.
     - Enhanced-mode data only.
     - Not started.
   * - 3.13 Layer order
     - The sweep's painter order, which the mesh reproduces as depth.
     - Each layer a small depth bias over the ground, as the strips and props already have.
     - Ground, strip, sidewalk corner fans, props and signals, each a depth bias over the ground.
   * - 3.14 Validation
     - The sim's placement rules are the original's.
     - Placement rules belong to the enhanced mode's editor, not the renderer.
     - Not started.
   * - 3.16 Signal operation
     - The art puts a light at every T and crossing; the original has no phases.
     - NEMA rings and barriers, actuation and coordination are a simulation feature; the renderer needs a phase state per movement to draw.
     - A two-phase twelve-second cycle per junction, on a hash of the tile, which the cars obey.
   * - 3.15 Railway crossings
     - Six crossing pieces in the data, ``0x43`` to ``0x48``: a road under a power line, over a rail, a rail under a power line, each on both axes. No control kind, no angle but square.
     - The crossing object falls out of the crossing tile. Control by road class as the spec tables it: crossbucks, flashers, gates as props; the RXR stencil and stop line as decals. Skew crossings cannot occur.
     - The panel surface across both tracks, rails alone through it; gates on every approach with crossbuck, plaque, flashers and striped arm, down with a train within three tiles; stop line, second-train signs, solid approach lines and the X stencil.
   * - 5.1 Rail classes
     - One rail family, one piece layout; no class, no track count, no traffic count for rails in the save.
     - One class for the shipped cities, chosen as the reference look (MAINLINE, one or two tracks); the others need enhanced-mode fields.
     - One class, a double-track mainline: two tracks 0.38 of a tile apart on one ballast strip, two light rails on ties each.
   * - 5.4 Placement and connectivity
     - The data has T and cross rail pieces and one-tile corners, which the spec forbids; the original built them.
     - Render what the player laid: a T as a turnout, a crossing as a diamond, a one-tile corner as the tightest arc; the placement rules belong to the enhanced mode's editor.
     - T and cross pieces carry each arm's rails through to the centre; corners on the road's fillet.
   * - 5.5 Curve geometry
     - Staircases and one-tile corners only.
     - Larger fillets than a road's where the legs allow, claiming the inside tiles as the spec's arc does (rail encroaches by default); spirals are a refinement.
     - The road's fillet, up to a tile.
   * - 5.6 Signalling
     - Nothing in the data.
     - Block signals every eight to twelve tiles, absolute signals at turnouts and diamonds, whistle posts before crossings: props on an arc-length walk, as the road's furniture pass.
     - Block signals every ten tiles per track on the right of its direction, absolute signals before junctions, whistle posts before crossings; every aspect follows the block's occupancy by the moving trains.
   * - 5.7 Stations and 5.8 yards
     - Rail stations are buildings in the data; no yards.
     - A station's platform beside the track where a rail station building adjoins it; yards need the enhanced mode.
     - Not started.
   * - 5.9 Rail layer order
     - As 3.13.
     - Ballast, ties, rails, hardware, signals, wire: depth biases as the road's.
     - Ballast, ties, rails.
   * - 5.10 Rail validation
     - The original's placement rules.
     - The enhanced mode's editor.
     - Not started.
   * - 6 Hardware
     - Nothing in the data; the sprites' props are a few pixels.
     - Every dimension is given in metres; at the road's scale, about seventeen metres to the tile and seven to the level, they are props of the mesh, and the state hooks of 6.7 are lamp codes and box angles.
     - Crossbuck masts, plaques, flasher bars, gate mechanisms and arms, signal poles and heads, second-train signs, whistle posts and block signals at part 6's metres; the base junction box at every mast; the tracks at 6.1's gauge, spacing and tie pitch. No bells, bungalows, cabinets, street lights or furniture yet.
   * - Traffic
     - The original places a car sprite on a tile whose traffic passes a threshold; cars do not move.
     - Cars as small meshes in the right-hand lane, moving along the strip and holding at red, in a per-frame buffer; the same threshold keeps the original's density.
     - Cars move on the lofted network in the lanes their class draws, hold at red and behind the car ahead, turn at junctions and dead ends, seeded by the original's density; trains run the rail network as coupled chains behind their engine, straightest arm at turnouts; crossing gates follow them; every vehicle pitches with the grade.

Part 7, the raised highways: what the save file gives us
--------------------------------------------------------

.. container:: lede

   Part 7 of the :doc:`road spec <appendix-road-spec>` is engine-agnostic:
   it describes decks, ramps and interchanges without saying which byte
   is which. This is that bridge, and it is read off the shipped cities
   rather than off the sprite sheet — a sprite says what a tile looks
   like, its neighbours say what it *is*. ``tools/highway_map.py``
   regenerates the whole table.

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
     - **Ramp runs.** One tile wide, so these are the 1×1 ramp data of
       7.3 — including ``0x5A``/``0x5B`` over rail and ``0x5C`` under a
       power line.
   * - ``0x5D``–``0x60``
     - 1642
     - 17–19 of ~400
     - one-sided; road 470–561
     - **Ramp touchdowns**, one id per direction, meeting the surface
       road at the T of 7.3 step 5.

Two consequences for the implementation.

**The deck really is a band, and the id says which way it runs.** An
interior tile of a two-wide band has two neighbours along it and one
across, which is why every deck id reports a three-neighbour mask.
``0x49`` reports ``{E,W,N}`` and ``{E,W,S}``; ``0x4A`` reports
``{N,S,E}`` and ``{N,S,W}``. Nothing has to be inferred from the art.

**The crossings are the deck, not the thing underneath.** ``0x4D`` is a
deck tile in an east–west band with a north–south railway beneath it,
which is why the renderer's ``piece_family`` must answer *rail,
north–south* for it: the rail is what the mesh has to draw through the
tile, and the highway above is drawn by the highway family. The same
holds for ``0x4B``/``0x4C`` over road and ``0x4F``/``0x50`` over power.

**What is not in the data at all.** The save file has no elevation for a
highway tile, no ramp form, no interchange type, and no air claims.
Part 7's whole apparatus — ``D`` and ``U``, the helix, the quadrant
tests, the stack — is therefore *derived on every edit* from the tile
layout alone, exactly as 7.3's closing note says it should be. There is
nothing to load and nothing to migrate; an imported city gets its ramp
forms computed the first time it is drawn.

