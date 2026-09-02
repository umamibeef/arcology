.. _renderer-previews:

=================
Renderer previews
=================

.. grid:: 2 2 4 4
   :gutter: 2
   :class-container: stats

   .. grid-item-card:: 100.0000%

      pixels matching the original renderer

   .. grid-item-card:: 3,973,352

      surface pixels compared, nine cities

   .. grid-item-card:: 498/498

      sprites decoded by the game's own blitter

   .. grid-item-card:: 3

      renderers driven: surface, underground, data views

.. rubric:: Every picture here is rendered by tools/gen_previews.py against the original, and the command that made it is printed above it. Each note gives the address of the rule its panel illustrates.

.. tab-set::

   .. tab-item:: Against the original

      .. rubric:: Traffic beneath the power lines

      .. code-block:: console

         python3 tools/pixel_sbs.py 'Manhattan' out.png --crop 2018,1634,90,70 --scale 5 --whole

      .. thumbnail:: img/preview/verify-1.gif
         :group: renderer-previews
         :alt: Traffic beneath the power lines
         :show_caption: True

         ours | the game's own renderer | any differing pixel in magenta · 1410x350

      .. admonition:: What the middle panel is
         :class: note

         The middle panel is the original game's renderer: its own sweep (``$16B74``), its own per-tile routine, its own blitter, run under the 68k interpreter. Nothing about tile order or compositing is assumed by the measurement. Over a 900x700 window of Manhattan that is 441178 pixels compared and 0 differing. Traffic is the case a blit list cannot judge: cars do not go through the ordinary blitter at all. All five call sites use ``$19004``, whose inner loops read the destination and write only where it is index 0x91, the road surface (``$1987E``). A car is stencilled onto asphalt rather than layered over it, so a power line, a building or a bridge rail already covering the road erases the car underneath it. The blit list is identical either way; only the write differs.

      .. rubric:: Downtown blocks

      .. code-block:: console

         python3 tools/pixel_sbs.py 'Manhattan' out.png --crop 1762,1570,90,70 --scale 5 --whole

      .. thumbnail:: img/preview/verify-2.gif
         :group: renderer-previews
         :alt: Downtown blocks
         :show_caption: True

         ours | the game's own renderer | any differing pixel in magenta · 1410x350

      .. rubric:: Waterfront and rail

      .. code-block:: console

         python3 tools/pixel_sbs.py 'Manhattan' out.png --crop 1570,1506,90,70 --scale 5 --whole

      .. thumbnail:: img/preview/verify-3.gif
         :group: renderer-previews
         :alt: Waterfront and rail
         :show_caption: True

         ours | the game's own renderer | any differing pixel in magenta · 1410x350

   .. tab-item:: Manhattan

      .. rubric:: Whole map

      .. code-block:: console

         arcology --soft assets 'Manhattan' out.png --zoom 32

      .. thumbnail:: img/preview/manhattan-full.gif
         :group: renderer-previews
         :alt: Whole map
         :show_caption: True

         whole map at maximum zoom, 1:1 — click to open it full size · 4224x2468

      .. rubric:: Downtown, 2×

      .. code-block:: console

         arcology --soft assets 'Manhattan' out.png --zoom 32 --focus 119,119,330 --scale 2

      .. thumbnail:: img/preview/manhattan-detail.gif
         :group: renderer-previews
         :alt: Downtown, 2&times;
         :show_caption: True

         2× · 1320x1320

      .. rubric:: Empty zoned lots

      .. code-block:: console

         arcology --soft assets 'Manhattan' out.png --zoom 32 --focus 18,10,120 --scale 4

      .. thumbnail:: img/preview/manhattan-zones.gif
         :group: renderer-previews
         :alt: Empty zoned lots
         :show_caption: True

         zoned land with nothing built on it yet · 960x960

      .. admonition:: The zoned-lot ground
         :class: note

         Shape 290 + zone, drawn where the tile is zoned, flat and has no building (``$17192``).

   .. tab-item:: Rules in detail

      .. rubric:: Palette animation, running

      .. code-block:: console

         python3 tools/gen_anim.py 'Bayview' out.gif --crop 1380,1300,180,140 --scale 2 --frames 16

      .. figure:: img/preview/judge-anim.gif
         :alt: Palette animation, running

         16 frames at the game's own 5 steps a second, 2× — the pixels never change, only the palette · 360x280

      .. admonition:: Animation is a palette permutation
         :class: note

         The art is completely static; only the palette moves, and it is a permutation rather than a rotation. ``$9750`` keeps TWO colour tables, swaps them every 12 ticks and rebuilds one from the other through a permutation (``$9770``, ``$97FA``). The 49-entry run is three eight-cycles, a four-cycle, a fixed point, then an eight-cycle, a four-cycle and an eight-cycle running the OTHER way; the 15-entry run is seven swaps, a blink rather than a flow. Turning all 49 as one block mixed ramps that have nothing to do with each other. The timing is the game's own: 12 ticks, 5 steps a second. A palette-only effect leaves every index identical, so the pixel comparison has nothing to compare; the permutation and the cadence are read from the listing.

      .. rubric:: A tall building, anchored

      .. code-block:: console

         arcology --soft assets 'Manhattan' out.png --zoom 32 --focus 119,119,150 --scale 3

      .. thumbnail:: img/preview/judge-drop-a.gif
         :group: renderer-previews
         :alt: A tall building, anchored
         :show_caption: True

         the tallest building in Manhattan, 3× · 900x900

      .. admonition:: There is no footprint drop
         :class: note

         There is no separate “footprint drop”. The game positions every sprite by subtracting the ``$1226`` descriptor's +4, which ``$18E96`` uses as the sprite's height, so a taller sprite reaches further down by construction.

      .. rubric:: B — the same tile with the height subtraction removed

      .. code-block:: console

         arcology --soft assets 'Manhattan' out.png --zoom 32 --no-drop --focus 119,119,150 --scale 3

      .. thumbnail:: img/preview/judge-drop-b.gif
         :group: renderer-previews
         :alt: B - the same tile with the height subtraction removed
         :show_caption: True

         same tile, drop disabled, 3× · 900x900

      .. admonition:: The same tile without the height subtraction
         :class: note

         The same tile with that subtraction removed, which is what a zeroed ``$1226`` gives on both sides. The table carries the real sprite sizes, so the anchor is inside the comparison.

      .. rubric:: An aircraft at its own altitude

      .. code-block:: console

         arcology --soft assets 'Manhattan' out.png --zoom 32 --focus 95,101,90,0,-72 --scale 6

      .. thumbnail:: img/preview/judge-plane.gif
         :group: renderer-previews
         :alt: An aircraft at its own altitude
         :show_caption: True

         type 1 at row 95 col 101, 6× · 1080x1080

      .. admonition:: A thing carries its own altitude
         :class: note

         A thing is not pinned to its tile. ``$A232`` reads an altitude from the XTHG record at +5 and scales it by ``2 << zoom``, half the terrain step; this aircraft carries 9, so it belongs 72 px up. The sub-tile position at +6 and +7 (``$A2F6``, ``$A322``) is what lets cars and boats sit between tiles rather than snapping to them.

      .. rubric:: Subway junction that turns and changes altitude

      .. code-block:: console

         arcology --soft assets 'Bayview' out.png --zoom 32 --underground --focus 3,28,150 --scale 3

      .. thumbnail:: img/preview/judge-underground.gif
         :group: renderer-previews
         :alt: Subway junction that turns and changes altitude
         :show_caption: True

         Bayview, row 3 col 28, 3× · 900x900

      .. admonition:: The underground renderer
         :class: note

         A subway junction that both turns and changes altitude, drawn by ``$161DC``.

      .. rubric:: Road, rail and power crossing

      .. code-block:: console

         python3 tools/pixel_sbs.py 'Manhattan' out.png --crop 1920,1620,120,110 --scale 5 --whole

      .. thumbnail:: img/preview/judge-hotspot.gif
         :group: renderer-previews
         :alt: Road, rail and power crossing
         :show_caption: True

         ours | the game's own renderer | any differing pixel in magenta · 1860x550

      .. admonition:: Traffic is stencilled, not layered
         :class: note

         The game writes a car pixel only where the destination is still index 0x91, the road surface (``$1987E``), so a power line or a building already covering the road erases the car underneath. That is how the original gets correct occlusion for traffic with no depth information, and it is invisible to a blit list — the list is identical either way.

      .. rubric:: Subway bend — the notch is the original's

      .. code-block:: console

         arcology --soft assets 'Bayview' out.png --zoom 32 --underground --crop 480,1350,240,180 --scale 4

      .. thumbnail:: img/preview/judge-bend.gif
         :group: renderer-previews
         :alt: Subway bend - the notch is the original's
         :show_caption: True

         Bayview, 4× · 960x720

      .. admonition:: The notch is the original's
         :class: note

         The notch at the bend is the original's. Rendering this rectangle through the game's own renderer produces the same notch, so it is a property of the art rather than of the reconstruction.

      .. rubric:: Subway corner, 5×

      .. code-block:: console

         arcology --soft assets 'Bayview' out.png --zoom 32 --underground --focus 0,127,520,470,120

      .. thumbnail:: img/preview/judge-corner.gif
         :group: renderer-previews
         :alt: Subway corner, 5&times;
         :show_caption: True

         Bayview west corner, underground, 1:1 · 1040x1040

      .. admonition:: Where the underground view meets the map edge
         :class: note

         Tile (0,127), where the underground view meets the map edge. Shapes 1319-1333 are the green tunnels, 1334-1350 the blue pipes, 1354-1358 the zone markers.

      .. rubric:: Map edge

      .. code-block:: console

         arcology --soft assets 'Bayview' out.png --zoom 32 --focus 120,127,170 --scale 3

      .. thumbnail:: img/preview/judge-edge.gif
         :group: renderer-previews
         :alt: Map edge
         :show_caption: True

         the far corner, 3× · 1020x1020

      .. admonition:: The skirt is drawn on two edges only
         :class: note

         ``$17008`` gates the whole skirt on col == 127 || row == 127, so only these two edges get the dirt cliff and the water side.
