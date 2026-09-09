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

That is the simulation's aim. The renderer's is its own, and the two must
not be confused:

**Lua is handed the simulation data, imagines a world on top of it, and
asks C to draw that world in 3D with primitives.** How the renderer reads
the simulation — how a road sweeps through its corridor, how an on-ramp
cell raises a climb to a highway, what an intersection draws, how many
lanes a road carries — is changed by editing a script. See
:ref:`scripting`.

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
   scripting
   future

.. toctree::
   :maxdepth: 1
   :caption: Reference

   conventions
   appendix-routines
   appendix-road-spec
