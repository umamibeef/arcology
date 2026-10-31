# Arcology — a city's limits and the region's chunks

A city's limits, the map and the city as two stores, and the chunks the region is kept in.  The goals are
`CLAUDE.md`'s "City limits, not a square" and "A much larger region".  When this plan is done, this file is
deleted: the open work goes into `TODO.md`, and the facts into `docs/`.

## The map and the city

The world is two stores.

* The MAP is the land: altitude and water, as large as the region.  Only terraforming and water change it.
* The CITY is what is built on the map: lines, bands, nodes, zones and buildings.  It is sparse.  The city
  reads the map and never writes it.

A city's LIMITS are an area that glows a fixed distance outside its roads, its zoning and its buildings, so
they grow as it does.  They take any shape the city's growth gives them, never a square.  Nothing about a
city is 128 tiles a side except the window a 1995 save is imported into.

The renderer's view of the world is the same two stores: `RMap` (altitude and water, `src/cells/city.h`)
and the city's content.

## Windows of chunks

The world is built by WINDOWS of chunks.  A chunk is 32 tiles a side (`MESH_CHUNK`).  A whole build is the
window that is every chunk, and there is no other build path (`CLAUDE.md`, invariant 1).

* **A window build** lays only the chunks a window covers (`--build-window`, `incr_window_set`).  It reads
  what is built within `net_halo` tiles of them, and the whole of each road that reaches in there
  (`topology_reach`, `src/render/build.c`).  A road is followed through the chunks beyond, along its own cells,
  through every seam, gore and level meet to the junction at each end.  The junction is kept with the first
  unit of each of its arms.  So a road is shaped from the whole of it, as a whole build shapes it.  The
  build derives the network from that, shapes it and draws it, and reads the land everywhere.
* **A chunk is computed from its neighborhood alone.**  Whatever a build lays in a chunk is a function of the
  cells within `net_halo` of it and of the roads that reach in.  `tools/window_witness.py` holds that: a window built alone lays, in each of
  its chunks, the faces a whole build lays there, bit for bit.  A record is named by its id, which is its
  place, and never by its place in a list.
* **The network a window derived belongs to the chunks it laid** (`src/network/store.c`).  The state of
  every layer of the network and the city it read are kept together, and each chunk the build laid points
  at them.  A later window points its own chunks at its own, and the others stand.  A reader of a place,
  the inspector among them, loads the network of that place's chunk first.  A network no chunk points at
  is freed.  Each layer names every variable it holds in one list (`src/network/state.h`), and
  `tools/code_smells.py` fails a variable the list leaves out.
* **A window into the mesh standing** replaces its chunks whole, and every other chunk stands as it is
  (`incr_window_end`).
* **The mesh check judges the windows as one.**  Each laid chunk keeps the triangles its build dropped
  beyond the window's border (`mesh_beyond`, `src/render/mesh/emit.c`), and a build replaces only its own
  chunks' stores.  So the border of every window laid is met by the world beyond it, whatever window was
  built last.  The headless modes that read the mesh build every window `--build-window` lists.
* **The live window** builds a map no wider than `build_whole_side` tiles whole, at once.  A wider
  one it builds a block of `build_block` chunks a side at a time, the block nearest the camera first
  (`resident_aim` in `src/app/world.c`).  `--whole-build` builds any map whole.

## The chunk store

Every tile lives in one chunk store (`src/sim/tiles.h`), 32 tiles a side.  Each layer of a chunk is one
aligned run, found through a flat directory in two loads.  The simulation reaches a tile only through the
accessors in `src/sim/sim.h` and keeps no array of its own.  A city is its records, its store, the corner of
its window (a multiple of 32 tiles) and the window's extent.  Every pass, working plane, bound and
coordinate draw follows that extent.  So a city is any rectangle of whole chunks, and its limits are its
real shape inside it.

A city on the original's own 128 by 128 runs exactly as the original does, ring of 512 and column wrap
included.  A city whose limits reach its window's right or bottom edge asks for a chunk more there.  The
world widens it between phases, up to 256 tiles a side, the most a moving thing's record can hold.

The world (`src/sim/world.c`) is that store with any number of cities on it (`world_add_city`).  The
`.arco` at version 2 writes the chunks there are, every city at its origin with its window, and each tile's
owner.  `arcology --compose out.arco city.sc2@x,y[:WxH]` lays cities into one world.

The clock runs about a fifth slower than on the original's flat arrays.  The passes that walk whole rows
(land value, growth, the grids, coverage, crime, pollution) are where that is won back.

## The city's limits and the simulation

The LIMITS are a mask, one bit a tile, and they are derived, never stored: every tile within a fixed
distance (the glow) of a road, a zoned tile or a building.  So they take whatever shape the city has grown
to.  An edit changes them only within the glow of itself.  Zoning is offered only inside them, so a city
grows its limits by building toward them.

The simulation runs over the tiles inside the limits and nothing else.

* **Iteration.**  A pass walks the tiles of the mask, not the rows and columns of an array.  The original's
  sixteen growth slices become row bands of the mask's bounding box, so a mask that is the original's
  128 x 128 square walks every tile in the original's order.
* **The edge.**  A read of a tile outside the limits answers what the original answers past its map edge:
  nothing built, no power, no water, no traffic, and the land as the map has it.  A write outside the limits
  is a fault.  The original's own edge rules (a road that leaves the map counts as arriving) apply at every
  edge of the mask.
* **Safety, checked three ways.**
  * The faithful mode is the original's square, so the oracle checks (`tools/oracle_diff.py`,
    `tools/clock_check.py`) hold the original's behaviour exactly.
  * An instrumented accessor counts every read and write outside the mask during a run of a city whose
    limits are not square: reads answer the edge, writes are zero.
  * A city run inside a larger empty world gives the same result as the same city run alone: what lies
    beyond the limits cannot reach in.
* **A rule's reach.**  A rule that reads a neighbourhood (coverage, pollution, land value, the traffic's
  trips) reads tiles outside the limits as the edge.  A rule whose reach is wider than the glow is listed
  with its reach, because a city then sees less of its own surroundings than the original saw of its square.

The clock knows the limits (`City.limits_glow`, `sim_limits` in `src/sim/sim.c`, `--limits-glow N` in the
game).  A glow of 0 is the original's map, every tile inside, and every change is behind that test, so the
faithful mode runs the original's code paths.  With a glow, the limits are worked out once a month.

* The power and water grids' sweeps walk the tiles inside.
* The forest draws its dice wherever the tile lies, and grows only inside.
* The city scan keeps its half-resolution "developed" mask in a layer of its own (`developed_cells`).  The
  original keeps it in XBIT's scratch bit at half-resolution coordinates, in the map's top-left quarter, and
  leans on other passes to clear it.

`arcology --limits <city> <glow> <ticks>` derives the limits, clears the city outside them to bare land, runs
the clock and counts every write outside them, by phase and layer.  What is written outside is pollution
and the police and fire coverage spreading past the edge, and a thing's label where it arrives at the map's
own edge.  The spread is meant: pollution and coverage reach past the limits.  Reads are not counted here:
the run of a city inside a larger empty world is what checks them.

The limits follow the city: they shrink where it decays, and they grow as the player zones and builds.  The
zone tools offer a zone only inside them, and each zoning grows them at once.

The simulation's layers split along the map and city line as the renderer's do (ALTM and the water are the
map, XBLD, XZON and the rest are the city).  The limits are the simulation's own track and keep its own rules
(`CLAUDE.md`, THE SIMULATION): the original's behaviour is reproduced inside a square mask before any other
shape is trusted.

## What is left

* **Growth up and to the left.**  It moves the window's corner and every coordinate the city holds.
* **Moving things past 256 tiles.**
* **The land a growing city reaches.**  It comes in bare at altitude 0.
* **A thing that arrives at the limits' edge**, rather than at the map's.
* **The map is one array.**  `r_map` and every buffer the size of the map (the tile records, the ground
  corners) assume one square grid, whatever is built.  The renderer draws the world as one square the size
  of its bounds.  The map and the city are to be stored as chunks created where they are needed, and the
  renderer is to read the limits as a mask and build nothing outside them.
* **Nothing frees a chunk.**  A chunk once built stays in memory and on the device.  A chunk far from the
  camera is to be freed, its records with it, and built again when it comes near.  The network's records
  are then kept by the chunks they lie in, and a road is reached by its id across builds.
