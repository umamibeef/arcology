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

**The engine becomes much more flexible than that: huge regions, more
interesting terrain features such as real cliffs, realistic water that
flows and resettles, weather.** An existing city imports into the enhanced
world, and the simulation then runs in a more complicated sandbox.

**The original save format is therefore an import and the reference
baseline, not the limit of what the engine can hold.** Original content
round-trips exactly; the enhanced world is a superset of it.

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
