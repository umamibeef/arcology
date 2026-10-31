.. _renderer-trees:

=========
The trees
=========

.. container:: eyebrow

   The enhanced world · the renderer

.. container:: lede

   The forest the simulation grows, drawn as trees. Each forest tile holds one to five trees of a species, and the species comes from the city's biome and the land under the tile. A species is one row of numbers, as Weber and Penn describe an archetype (1995). One builder turns a row into a tree. The trees replace the tree art in the geometry view.

In the game
-----------

Each picture is a frame of the game, made by ``tools/gen_previews.py`` from the current build with the command above it. ``--trees`` sets the biome for one run, as **View → Trees** does.

.. tab-set::

   .. tab-item:: Tropical

      .. code-block:: console

         arcology bayview --shot out.png --centre 118,118 --zoom 2 --trees tropical

      .. thumbnail:: img/preview/trees-tropical.png
         :group: renderer-trees
         :alt: Tropical
         :show_caption: True

         Bayview's forest, grown as the tropical biome
   .. tab-item:: Boreal

      .. code-block:: console

         arcology bayview --shot out.png --centre 118,118 --zoom 2 --trees boreal

      .. thumbnail:: img/preview/trees-boreal.png
         :group: renderer-trees
         :alt: Boreal
         :show_caption: True

         the same forest, boreal
   .. tab-item:: Arid

      .. code-block:: console

         arcology bayview --shot out.png --centre 118,118 --zoom 2 --trees arid

      .. thumbnail:: img/preview/trees-arid.png
         :group: renderer-trees
         :alt: Arid
         :show_caption: True

         the same forest, arid

What the trees are built on
---------------------------

.. rubric:: ``sim_forest`` · ``tree_byte`` · ``trees_draw``

The answers below are what the rest of the page rests on.

.. list-table::
   :header-rows: 1
   :widths: 30 70

   * - Question
     - Answer
   * - The forest in the simulation
     - The building bytes ``0x06`` to ``0x0D`` are the forest's stages, from the sapling up. ``sim_forest`` (``$34792``) picks a tile, advances a forest tile one stage up to ``0x0C``, and grows or plants the tile next to it. The census of the forest's stages moves in most phases of the clock, a tile or two at a time. The map generator alone places ``0x0D``, old growth.
   * - How a shape is laid
     - A tree is triangles in the mesh, as every other shape is. ``put_frustum`` (``mesh/shapes.c``) lays a trunk. ``put_tri_n3`` (``mesh/emit.c``) lays a triangle with a normal at each corner, which the crowns use. A vertex carries its place, a normal, a material, and two numbers for the material to read.
   * - Where the trees are placed
     - ``trees_draw`` (``src/render/trees.c``) runs in the build after the streets and the power lines, on every forest tile the build lays. It reads the ground's height with ``surface_at_world``.
   * - How the mesh is lit
     - ``terrain_body.frag`` lights the mesh by the sun, the sky's ambient light, the sun's shadow map and the lamps. A cut-out is a ``discard``, and the leaf material (``MAT_LEAF``) cuts its edges that way. The material is a number in the vertex, a sixteenth a species.
   * - How the mesh is kept and drawn
     - The mesh is kept by chunk, and each chunk holds four land ranges: the ground, what stands on it, the trees, and their simple crowns (``MESH_RANGES``). The frame draws the chunks in view. An edit lays again only the chunks near the tiles it changed (``incr.c``).
   * - What the art does in the geometry view
     - The tree art does not draw where the mesh lays the tile. The shaped mask hides the art of every tree byte, as it hides the art of every network tile.
   * - What a city carries that picks a biome
     - Nothing in the simulation names a climate. The file's name is the one hook. ``CITY_BIOME`` maps each shipped city to a biome, and a city not in it is temperate mixed.
   * - How a choice is kept
     - ``prefs.c`` saves the biome the player picks as ``biome``: a biome's name, or ``city`` for the city's own.

The species
-----------

.. rubric:: ``SPECIES`` · ``canopy`` · ``CROWN_CONES`` · ``CROWN_CARDS`` · ``CROWN_FRONDS``

Eight species make three kinds of crown.

.. list-table::
   :header-rows: 1
   :widths: 20 20 60

   * - Crown
     - Species
     - How the builder lays it
   * - Cones
     - fir, pine, cypress
     - A stack of tiers. Each tier has 7 to 11 branch tips, each with its own reach and droop, and the tiers turn by the golden angle so no two line up. The material cuts the edge of each tier ragged and mottles it in two greens.
   * - Cards
     - oak, maple, birch, acacia
     - Standing cards that cross at the trunk, and two caps across them, the lower cap the wider. The camera from above sees the caps, and from the side it sees the cards. The crossed card is a degenerate billboard cloud (Décoret et al., 2003). The material cuts each card to a ragged round crown with holes in it.
   * - Fronds
     - palm
     - Fronds from the top of the trunk, each two strips that rise and then droop. The material cuts each frond into a fringe of leaflets that narrows to the tip.

Each tree takes its own height, width, lean, turn and shade of green from its seed. The crown's normals are the normals of its envelope, not of its faces. So a crown is lit as one rounded mass. The shadow pass cuts the same shapes, so a crown's shadow has holes too.

The biomes
----------

.. rubric:: ``BIOMES`` · ``CITY_BIOME`` · ``trees_set_biome``

A biome is a mix of the species by weight, a share of the trees a stage holds, and a tint of the leaves. There are eight: temperate mixed, northeast hardwood, southeast pine, boreal, pacific northwest, mediterranean, tropical and arid. Atlanta grows southeast pine, Manhattan northeast hardwood, Hawaii tropical and Las Vegas arid. **View → Trees** sets another biome for the city, or gives it back its own. ``--trees BIOME`` sets one for one run and does not keep it.

Where a tree stands
-------------------

.. rubric:: ``place_trees`` · ``land_at`` · ``STAGE``

``place_trees`` is pure. It reads the tables, the tile's stage and the land under the tile, and it gives the same trees for the same inputs.

* **The stage.** The stage table gives a stage its count of trees and their size, from one sapling at a quarter of full size to five old trees at 1.1.
* **The tile.** Each tree takes a cell of a grid in the tile, off its middle by up to 0.35 of a cell, and 0.1 of a tile in from the edge.
* **The clumps.** A slow noise over the map gathers the trees into clumps a few tiles across, with clearings between them.
* **The land.** ``land_at`` reads the tile as the ground's shading does: its height, its slope over its four corners, and how near the water is. High ground weights the alpine species up, and water the wet ones. So a slope grades from one mix to the other with no line between them. Past a slope term of 0.5 a tile keeps half its trees, and past 0.8 it keeps none. So no tree stands on a cliff face.

The levels of detail
--------------------

.. rubric:: ``tree_levels`` · ``MESH_RANGE_TREES`` · ``MESH_RANGE_CROWNS`` · ``forest_floor``

The build lays every tree twice: whole, and as a simple crown in the same place. The simple crown is the same builder at its least detail: one tier, a crossed pair of cards, or four fronds, on a trunk of three sides. The frame picks the level by the pixels a tile spans on the screen.

.. list-table::
   :header-rows: 1
   :widths: 30 70

   * - A tile spans
     - The frame draws
   * - over ``trees_near_px`` (48)
     - every tree whole
   * - between the two
     - every tree as its simple crown
   * - under ``trees_far_px`` (12)
     - no tree. The ground takes the canopy instead.

The canopy is two bytes a tile that the build lays beside the trees: how much of the tile the crowns cover, and the species of its largest tree. The ground reads them in ``forest_floor``. Under the trees the floor is darker at every distance. Where no tree draws, the ground takes the main species' leaves, mottled a crown wide.

Each change of level is a band ``trees_band`` (0.15) either side. In a band both levels draw, and each keeps the pixels of the ordered dither that the other discards. So a tree does not pop, and a camera at rest in a band shows a still mix. The simple crown stands where its whole tree stands, so in a band the two cover the same pixels.

.. note::

   The view is a parallel projection, so a tile spans the same pixels in every chunk. The level that a chunk's size on the screen picks is therefore one level for the whole frame.

The sun's map draws the level that holds most of the pixels, and no tree past the crowns.

The forest as it grows
----------------------

.. rubric:: ``run_phase`` · ``trees_stale`` · ``DIRTY_NEAR``

The clock compares the census of the forest's stages before and after each phase. When it moves, the trees are out of date, and the build lays them again when the month turns. An edit's window reaches ``net_halo`` tiles round each tile it changed. A tile whose only change is between bare land, rubble and a forest stage reaches its own chunk alone, because no reader but the trees tells those bytes apart.

.. caution::

   The build at the month's turn also takes in everything the city grew since the last build. Each built tile it finds reaches ``net_halo`` tiles. So one month's build can lay most of the city again.

The wind
--------

.. rubric:: ``tree_sway`` · ``trees_sway``

The crowns sway on the wind the clouds move on (``sky_wind_x``, ``sky_wind_y``). ``tree_sway`` in ``terrain_body.vert`` moves each leaf vertex downwind. The gusts run across the map along the wind, and each tree is a little out of step by its shade. A crown's tips move more than its middle, and a palm's fronds more than a crown. The trunk does not move. The sway is on while the clouds are on. The sun's map draws the crowns at rest.

The settings
------------

.. list-table::
   :header-rows: 1
   :widths: 35 15 50

   * - Setting
     - Value
     - What it sets
   * - ``trees_near_px``
     - 48
     - the pixels a tile spans above which every tree draws whole
   * - ``trees_far_px``
     - 12
     - the pixels below which no tree draws
   * - ``trees_band``
     - 0.15
     - the half width of each band, a share of its threshold
   * - ``trees_sway``
     - 0.012
     - how far a crown sways on the wind, in tiles
   * - ``TREE_EDGE_INSET``
     - 0.1
     - how far in from the tile's edge a tree stands, in tiles
   * - ``TREE_JITTER``
     - 0.35
     - how far off its cell a tree stands, a share of the cell
   * - ``TREE_CLUMP_TILES``
     - 4
     - the tiles across a clump
   * - ``TREE_SLOPE_HALF``, ``TREE_SLOPE_NONE``
     - 0.5, 0.8
     - the slope terms past which a tile keeps half its trees, and none

The settings of the levels and the sway are in ``geo_defaults`` (``src/render/geo.c``), and Debug → Shading sets them. The rest are in ``src/render/trees.c`` beside the tables.
