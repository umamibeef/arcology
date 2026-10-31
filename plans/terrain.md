# Arcology — the terrain

The land the world stands on: what it IS, how it is drawn, and how a large region of it is made.  Where the
build stands is the last section.

## What the land is

The land is a HEIGHT for each tile, a whole number of levels, and nothing else.

* There is no slope code.  A tile's slope is not stored: the ground between two tiles follows from their two
  heights.
* There is no cap.  A height is not limited to the 32 levels a 1995 save can hold.
* Two tiles side by side may differ by any number of levels.  A difference of more than one is a cliff
  (below), and what may be built on it is the simulation's.
* Water is a TABLE over the land: a tile is under water where the table stands above its height.

This is the same land the original keeps.  Its altitude layer (`ALTM`) holds a level for each tile, and its
slope codes (`XTER`) are worked out from the neighbours' levels (``$129E6``), as a picture to paste.  So an
original city maps in one to one: each tile's height is its `ALTM` level, and its water table is the one it
stores.  Its slope codes are not read.  A 1995 save is only ever loaded: the world is saved as `.arco`.

## The dual ground

The ground is drawn on the DUAL grid: between the middles of the tiles, not on the tiles themselves.

* A tile's MIDDLE is at its height.
* The middle of an EDGE between two tiles is at the mean of their two heights.
* A CORNER, where four tiles meet, is at the mean of their four heights.

A tile's ground is the eight triangles between its middle, its four edge middles and its four corners.  Each
height is a function of the grid point alone, so the two tiles that share an edge, and the four that share
a corner, agree on it.  The ground is continuous by construction: there is no average of disagreeing
planes and no slope code to misread.

Three kinds of tile draw a top of their own.  The ground MEETS them: a corner or an edge middle such a top
touches is at that top's height there, the mean where several touch it, and the open land between slopes
to it.  A WALL is left only where two such tops disagree.

| tile | its top | why |
|---|---|---|
| a structure's pad | flat at its level | a building stands on level ground |
| water | flat at the table, over its bed | a body of water is level |
| a sloped network piece | its own plane | where the original's road and rail pictures stand in for the network, each picture is drawn for that plane |

At a shore the ground is never below the water it touches: an edge middle or a corner that touches water is
held up to the table.

Every top, its own or the dual ground, is drawn as the same eight triangles through the middle of each
edge.  A flat or sloped top lies in its own planes, with its middle on the diagonal it is folded on.  So the
tiles on the two sides of every edge share its three points, and the mesh has no free edge.  The seabed is
drawn the same way, and every wall and rim runs in two halves through the edge's middle.

## Cliffs

A CLIFF lies between two land tiles side by side whose heights differ by more than one level: the
original's own slope limit.  It is derived from the heights, as the network is derived from the cells, and
no flag is stored.  Across a cliff the ground does not join.  The middle of the edge between the two tiles
is each tile's own, and at a corner the four tiles round it fall into groups joined by slopes, each with
its own height there.  C draws the face between the two sides, and gives it its look:

* **The lean.**  Open land at the lip sags toward the step below, and land at the foot rises toward the
  step above, as scree, each by a part of the step (the numbers `cliff_lip` and `cliff_toe`, `src/render/geo.c`).  C applies it
  where each tile's corners and edge middles are worked out, so everything that stands on the ground
  reads the same surface.
* **A broken face.**  C lays the face as a grid of facets: a ledge a level, each set
  in or out, and a jag at each point inside.  The rim lies on the tile's edge and the corners stay still,
  so the face meets the ground and the faces beside it.
* **The layer cake.**  Every cut shows the same land: the face, the lip the lean cut away, and the map's
  own cut, which is a section through a larger world and adds nothing of its own.  The strata are runs of
  height, the same on every cut, and the topsoil is the top layer, a fixed depth under the surface as it
  stood before the lean.  C records how far the lean moved each point (`tile_lean`), and the ground shows
  the cake where it was cut and scree where it was filled.

Water meets land as a shore and never forms a cliff.  The shipped cities hold 27 cliff edges in all;
generated land and the terraforming tools make the rest.

## The ground beside a road

A road's height is its own (`plans/network.md`, layer 3), so the ground drawn beside it moves to meet it.
The land is never written: taking the road away gives back the land as it was.

* **Under a road or a junction** the drawn ground meets its surface.
* **Beside a street**, within a quarter tile of its edge, the ground is GRADED: it blends from the road's
  height at the edge to the land's own height at the end of that reach.
* **On both sides of every street**, whatever stands beside it.  Where several roads or junctions reach
  the same ground, it takes the lowest of their grades, so it is continuous from tile to tile.
* **Walls** on steep slopes are wanted, and not designed: when a slope is steep enough for one is not
  decided, and no wall is drawn.

Tested in `tools/roadfit` (`ground.py`, and `ground3d.py` in 3D) on the test world: a street's edge never
parts from its land by more than half a level there, so every bank is graded and no wall is needed.
`src/network/bank.c` works it out in C, `--net-check` reports it against this rule, and the mesh grades
the ground to the streets it draws from it.  The report does not fail the check.  The rule has no answer
where the reaches of two streets overlap, or where one street bends back near itself
(`plans/network-status.md`).  A street junction lies on its own tile: every street ends level at its end tile's
edge, which it shares with the junction.

## Who decides what

The ground is C's, the look included (`CLAUDE.md`, "Code is C").
C stores the tile heights and the water table, works out the dual ground, the cliffs and their lean, and
draws every top, wall, rim and face (`src/render/mesh/tile.c`, `ground.c`, `ground_bytes.c`).  Its three
numbers are `cliff_lip`, `cliff_toe` and `map_floor` (`geo_defaults`, `src/render/geo.c`).

Everything that stands on the ground reads the one surface, through `surface_at_*`: what is drawn and what
stands on it agree everywhere.

## The region (not built)

How large the region is, and how it is stored, is a goal (`CLAUDE.md`, THE GOALS: "A much larger region").
This is how its land is made.  OpenTTD's TerraGenesis is the model:

1. **A coarse layer over the whole extent**, about one cell for sixteen tiles: the land and the sea from
   warped low-frequency noise, the sea level as a share of the land; uplift for the mountains; erosion by
   the stream power law to steady state, in fixed point; the lakes by Priority-Flood; the rivers where the
   flow passes a threshold.
2. **Each chunk of 32 tiles**, from the seed and the coarse layer alone: the coarse heights interpolated,
   warped detail damped near the rivers, and a VARIETY curve for each coarse cell, so one area is flat,
   another plateaus, another hills.
3. **Heights and water**: rounded to whole levels; the river and lake beds carved, each only stepping
   down along its flow; the water tables from the lakes and the sea.

The original's own generator (``$11A92``) is ported too, as a CLASSIC choice for a single city, and is
checked against the original in the emulator.

## Where the build stands

* The land is the original's: `ALTM`, with no tile height beyond 31.  Its slope codes are read only for a
  sloped network piece's own plane and for the seabed.
* The ground is the dual ground (`src/render/mesh/tile.c`, `ground.c`).  Everything that
  stands on it reads the same surface (`surface_at_*`), and a face laid on it is cut on its eight
  triangles.  Every shipped city builds with no free edge.  The ground carries about four times the
  triangles of a top cut on one diagonal.
* Cliffs are drawn: the lean, a broken face, and the layer cake on every cut, the map's own among them.

What is left, in order:

1. **The land as tile heights in the store**, past 31 levels, saved in the `.arco`.
2. **The region**: the generator, the region map, and a new game that starts on it.
3. **The classic generator.**
