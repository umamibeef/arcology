.. _index:

========
Arcology
========

**Arcology** is a from-scratch reconstruction of the SimCity 2000
simulation engine, verified against the original Mac 68k binary.

These pages are the study behind it: the retail Macintosh build of
SimCity 2000 1.2 (22 June 1995), recovered from its CODE resources and
checked against the original executing under a 68000 interpreter.

Every formula in these pages was read out of the shipped binary and then
verified by running the game's own routines and the reconstruction from
byte-identical starting state, comparing the results tile by tile.  Where
something is unrecovered or uncertain, the page says so.

What this project is
--------------------

**The goal is a game that supports everything original: saves, sprites,
sounds, music. As if you were playing a port of SimCity 2000 on a modern
system.** The reconstruction documented here is that baseline, and it
stays checkable against the original.

**The engine becomes much more flexible than that.** Its goals, briefly:

- **A superset of SimCity 2000.** A whole 1995 city imports. A ``.sc2``
  is loaded and never saved, and the world saves as ``.arco``.
- **City limits, not a square.** A city's limits come from its roads,
  zoning and buildings, plus a glow around them, and they grow with it.
- **Independent cities that meet.** Cities interact, and they may grow
  into each other where their limits touch.
- **The road network from the cells.** C derives the network from the
  city's data.
- **Roads drawn better than their cells.** The renderer draws a diagonal
  instead of a staircase, and a highway bends where the original could
  only turn. The simulation's data does not change.
- **A much larger region.** One fixed, very large region, shown as a
  map, holds the cities.

**The original save format is therefore an import and the reference
baseline, not the limit of what the engine can hold.** Original content
imports exactly, and a world is saved as ``.arco``, a superset of it.

That is the simulation's aim. The renderer's is its own, and the two must
not be confused:

**C derives the road network from the city's cells and stores it, and C
draws the world.** The network follows from the tiles and the player's
street layer.

.. toctree::
   :maxdepth: 2
   :caption: The simulation

   sc2k-report
   mechanics

.. toctree::
   :maxdepth: 2
   :caption: The renderer

   rendering-pipeline
   renderer-design
   renderer-previews
   renderer-terrain
   renderer-terrain-b
   renderer-views
   renderer-sky
   renderer-trees

.. toctree::
   :maxdepth: 2
   :caption: The enhanced world

   enhanced-renderer
   future

.. toctree::
   :maxdepth: 1
   :caption: Reference

   conventions
   appendix-routines
   appendix-road-spec
   notebook
