# Arcology — the network: the design

What the road network IS, layer by layer.  This page holds the design and nothing about where the build
stands: that is `plans/network-status.md`, and a difference between the two is written there as a breach,
never here as a fact.  When the network is done, this page goes into `docs/` and is deleted.

## What it is for

The NETWORK is the one source of truth for everything that is travelled: roads, highways, ramps, railways,
the sidewalks and crosswalks beside and across them, and the junctions, interchanges, turnouts and level
crossings between them.  Every surface, every marking,
every car and train, the outline view and every check is DERIVED from it.  Nothing outside it holds a path,
a height or a width of its own, so nothing can disagree with it.

The network's records are the one model: the cars drive its lanes and links, and the checks and the
outline view read it.

The model is ASAM OpenDRIVE's, which simulators use for exactly this: roads with a reference line, an
elevation and lane sections, and junctions whose connecting roads are roads.  Its structure and terms are used here.  A network
can then be written out as OpenDRIVE and read by a standard viewer, which is a check that shares no code
with ours.

## The layers

Each layer is derived from the layers above it and from nothing else.  The order is the point: WHAT is
travelled, and how it joins, is settled as a graph before anything is given a shape, and the shape never
changes the graph.

Layers 0 to 2 follow from the city's cells, so they are derived in C and stored in C (`CLAUDE.md`, THE GOALS: "The network is derived from the cells, in C").  How the network is drawn is not
designed.

### Layer 0a: the map

The cells: what is built on each, its terrain, its height.  The simulation's, and never written.  What a
byte means to the network (a piece and its family, the edges it joins, a band tile's kind and axis, a
ramp's two edges) is read in C from the original's own data, never from the art.  It lives in `src/sim`, beside the fitters it is read from, as a function of a cell's own
bytes (`XBLD`, `XTER`, `XBIT`).

* **A cell carries up to two pieces.**  A crossing carries a piece of each of two networks.  For each piece,
  layer 0 answers its network and the edges it joins, in the fitters' own bits: `col - 1`, `row + 1`,
  `col + 1`, `row - 1`.
* **A fitted piece** is one a fitter chooses: power from `0x0E`, roads from `0x1D`, rail from `0x2C`, each
  as `NET_PIECE` maps a join mask to it.  Its edges are that mask.  A piece on a slope takes its piece from
  the terrain alone (`NET_SLOPE_PIECE`), and its edges are the two along the slope.
* **A fixed piece** is one a placer writes and no fitter changes: a crossing, a tunnel end, the end of a
  slope, a highway over a line, a ramp.  Its edges are the ones its placer gives it, read from that placer.
* **A band cell** answers its kind (slab, incline, curve, interchange, span over water) and the edges its
  unit's arms leave by, from the original's highway placer (`$1C826`, kinds chosen at `$1C3A8`).  An
  interchange's arms are not in its byte: layer 1 reads them off the units beside it.  The single-cell
  bridges `0x51` to `0x5C` are a road's, a railway's or a power line's, never a band's.
* **An edge of a network is LINKED** where the pieces of that network on both its cells join it.

### Layer 0b: the street layer

The player's own data about the streets, which the original game never held.  It is part of the city, saved
in the `.arco` beside the tiles, and the faithful simulation never reads it.

* **One byte a tile: the edges to the next column and to the next row.**  Every edge between two cells is
  stored once, by the cell with the lower column or row, so two neighbors can never disagree.  An edge holds
  4 bits:
  * the CLASS, 2 bits: 0 a street, 1 an avenue, 2 a boulevard;
  * the DIRECTION, 2 bits: 0 two-way, 1 one way from the cell that keeps the edge to the next, 2 one way
    from the next cell to it.
* **An edge counts only while the two cells join across it.**  An edge with no link across it reads as
  nothing, so a road the simulation takes away (a fire, a flood, a zone that grows over it) leaves no stale
  data that matters, and a save writes such edges as nought.
* **Streets only.**  A highway carries three lanes each way, a railway two tracks, and a ramp one lane one
  way, whatever the layer says.
* **Imported once.**  A city read from an `.sc2`, or from an `.arco` that has no street layer, gets one on
  load: each street, junction to junction through its seams, takes the higher of a class by the population
  of the buildings standing on the lots that reach it, per tile of its length, and a class by its traffic
  (`XTRF`).  Every edge of the street takes that class, two-way.  The thresholds are import knobs.  Once
  stored, the layer never follows the traffic again.
* **Changed by the player alone.**  A road tool lays its own class.  A tool to upgrade a street sets the
  class of the whole street or of a stretch dragged over, and a tool for one-way streets sets the direction.
  Bulldozing a tile clears its two edges and the edges its neighbors hold toward it.
* **It has no orientation.**  Each entry is an edge between two cells, and a one-way runs from one of them
  to the other.  A turn of the view changes nothing in it.  A city placed in another orientation maps its
  cells onto the map's, and each edge goes with its two cells.
* **It is the `.arco`'s alone.**  A `.sc2` is only loaded, and holds no street layer.

The graph built from it is as simple as the data allows: one road a run between two junctions, and one lane
section a stretch of one class and one direction.  A long straight of edges that agree is one road with one
section.  A class or a direction that changes partway along a road is a section boundary there.  A street
whose edges disagree on its one-way direction is a fault, and said.

### Layer 1: topology

From the map alone, before any geometry.

* A ROAD: an id, its family and class, its cells in order, and its two ends.
* An END: an id from its cells, its kind, and its cells.  Every cell is read the same way.  A unit of two
  links is PLAIN, and a run of plain units is a road.  Every other unit is a JUNCTION unit, of one of five
  kinds: a node (three links or more, or a band's interchange), a dead end (one link, or none: a unit alone), a level meet,
  and a band unit a ramp joins whose taper has no unit to run into (below).  Junction units of one network and one kind that link MERGE into one end,
  until a unit of another kind stops the merge: an interchange of many units is one end.  A dead end is the end of a road and no place where roads meet, so it
  never merges: two dead ends that link are two ends.  A meet's cell is a unit of two networks, so a merge
  of meets goes on in both.  Two ends side by side that do not merge are joined by a road of no units, with a node on each
  side of the edge they share.  Layer 2 makes every end that holds units a `default` junction.  A
  seam, a gore and a map edge hold none and are plain road ends.

A RAMP's piece joins two families: one edge to a line cell, and one edge to a cell of a band.  Which two
edges is the original's: the ramp id (`0x5D` to `0x60`) together with the cell's `XBIT` bit `0x02`, as the
placer at ``$3B98`` writes them.  The two edges are always
perpendicular, so the road runs beside the band.  The id holds nothing else.  A ramp's cell is a unit of
the road network linked across both its edges, so where both link it is a plain unit: the first unit of
the RAMP'S ROAD, which ends at a GORE across the edge to the band.

| id | `XBIT & 0x02` clear: road / band | set: road / band |
|---|---|---|
| `0x5D` | row + 1 / col - 1 | col + 1 / row - 1 |
| `0x5E` | row - 1 / col - 1 | col - 1 / row - 1 |
| `0x5F` | row - 1 / col + 1 | col - 1 / row + 1 |
| `0x60` | row + 1 / col + 1 | col + 1 / row + 1 |

It holds on all 1642 ramp tiles of the shipped cities.  Its direction
follows from the side of the band it stands on and the world's DRIVING SIDE (right, unless the world says
otherwise): the band's lanes on that side run one way, and the ramp's lane runs with them.  So the
direction comes from the cells and the driving side, never from a fitted line.

A TUNNEL is a road under the ground.  A tunnel mouth's piece joins the surface by one edge, and its other
edge leads into the hill.  Stepping that way from the mouth reaches the mouth that faces back, as the
original's placer lays them (``$20622``).  The road runs from one mouth to the other, and the cells it
passes under are its units.  Those cells keep whatever the surface carries.

A road always ends at an end of one of those kinds.  A road is cut at a SEAM with no choice in it, wherever it crosses the border of a chunk of
`MESH_CHUNK` tiles between two units of two links, on a bend too.  A seam never falls in a node, a
level meet or a ramp, so a road runs on past a border there.  A band's road is cut as well at a ramp's GORE
(`appendix-road-spec`, 7.3), and the ramp's road at the edge between its cell and the plain band unit it
stands beside.  The gore lies a TAPER along the band from that unit: upstream of it for an off ramp, and
downstream for an on ramp, by the band's traffic on the ramp's side.  The taper is two units, four tiles.
It is fewer where the band's plain units run out first, or another ramp's unit or cut comes first, and
never fewer than one.  Where not even one unit is free, the gore is the ramp's unit's own border.  The
band's road is cut as well at the ramp's unit's other border, the border its lane comes back at (layer
2).  A band unit a ramp joins is a junction unit instead, the ramp's band junction, only where the ramp's
taper has no unit to run into: the unit across the border its gore would cut is a node's, a dead end's,
the map's edge or such a unit (`appendix-road-spec`, 7.3, "Fallback").  Its ramps join there (below).  An interchange of three band arms or more is a
REGION: its units and every band unit a ramp joins that links to them, merged into one end along the band
(`interchanges.md`, "The model").  A band's road is cut at each border of a region where a band arm leaves
it, and a band road of no units joins that cut to the region.  A ramp's road whose band unit is in a region
is cut at the edge between its cell and that unit.  Each such cut is a GORE: an end that holds no unit, lying across the edge, as a seam does.

A road's ids come from its ends, so cutting a road in the middle renames only the two halves.  Two
roads that leave one cluster of node units by the same side and reach the next by the same side are named as
well by their first and last unit.

### Layer 2: the road network, in OpenDRIVE's structure

Built straight after the topology, from the topology and the families alone, before anything is fitted.
Its elements and their names are ASAM OpenDRIVE's, so writing the network out as `.xodr` and reading it in
a standard viewer is a check that shares no code with ours.  It is THE network: every layer below gives it
a shape or reads it, and none of them adds, removes or relinks anything in it.

* A ROAD: an id named by place (from its topology road, its ends' junctions and edges), its family and
  class, `junction` (none, or the junction it is a connecting road of), and its `link`: a PREDECESSOR and a
  SUCCESSOR, each another road at a `contactPoint` (its `start` or its `end`) or a junction.  Its `lanes`
  are a list of LANE SECTIONS, each starting at an `s` along the road, each holding the lanes left and
  right of the reference line, lane 0.
* A LANE: a signed id (right of the reference line negative, counted out from it), a `type` (`driving`,
  `entry`, `exit`, `onRamp`, `offRamp`, `connectingRamp`, `sidewalk`, `median`, `border`, `shoulder`, `rail`), a `width`, and its own
  `link`: the lane it comes from and the lane it goes on into.  Inside a road that is a lane of the section
  before or after.  At a road's end it is a lane of the road or the connecting road linked there.
* A JUNCTION: an id named by place, a `type`, and its CONNECTIONS.  Each connection names an incoming road,
  the road it goes on into and the `contactPoint` there, and a `laneLink` from each incoming lane to the
  lane it goes on into.
  * A `default` junction joins its roads through CONNECTING ROADS, each a road of its own with its own
    lanes: a road node's movements, an interchange's highway straight on, a turnout's branches, the turn at a dead
    end, and a level meet's two ways across each other.  No lane of
    an arm links straight to another arm's lane or to a node inside the junction.
  * A `direct` junction joins its roads with no connecting road: their lanes link straight across.  It is
    where a road divides or two join: a highway's exit and entrance, at a gore (ASAM OpenDRIVE 1.8.1
    §12.6, "Direct junctions are intended to model entries and exits without adding additional
    connecting roads").  It joins one road on one side to the roads on the other.
* A SEAM is no junction: the two roads either side of it are each other's predecessor and successor, and
  their lanes link one for one.

THE LANE GRAPH follows from those records, with nothing added: its edges are the lanes of each section,
and its nodes are where lanes end and begin.  That is a road's end at a junction or a seam, a lane
section's start, and a direct junction.  A lane runs from one section boundary to the next and does nothing
between them: it never joins or parts from another lane partway along.  So:

* a lane GAINED is a lane whose section starts at an `s` where the lane beside it goes on, and a lane
  DROPPED one whose section ends there.  Where two lanes of one section both link into one lane of the
  next, that boundary is a MERGE node, and where one lane links into two, a DIVERGE node;
* a lane that widens from nought at its section's start has no predecessor, and a lane that narrows to
  nought at its section's end has no successor (OpenDRIVE 1.8.1 §11.5): an `exit` lane appears so, and an
  `entry` lane ends so, its traffic merging by a lane change;
* a lane of a junction may begin or end at a node inside the junction rather than at an arm: an `exit` lane ends at the node where it splits, and
  an `entry` lane begins at the node where lanes join it;
* A NODE LIES IN ONE PLACE.  A road's contact is a node on its junction's border, and a node inside a
  junction joins that junction's connecting roads alone.  So no node is both;
* LANE CHANGES are OpenDRIVE's laneChange and are no links: along a road, a traffic lane may change into
  the traffic lane beside it that runs the same way.  Inside a junction no lane may.

THE ANCHORING is OpenDRIVE's own: everything in the network is placed by its ROAD and by `s` along the
road's reference line and `t` across it, and by nothing else.  A lane section starts at an `s`, a lane is
a width at a `t`, a junction meets a road at a `contactPoint`, which is that road's `s` of nought or its
length.  Nothing in the network has a place of its own, so nothing in it can drift off its roads.

Before any shape, `s` is counted along the road's own units, from its first end, so a section can start
at a unit's first boundary, its middle or its last boundary.  The reference line is the line through the
units' points.  How the drawing maps `s` onto a fitted line is not designed.

The network can be read, checked and SEEN before any fit, and a check holds its rules over it alone.  The INITIAL VIEW draws it from its records alone: each lane along its road's provisional reference
line at its `t`, each connecting road as a straight segment between its two contact points, and each node
as a mark of its kind.  A dump prints it for a reader without a window.

Which lanes a road carries follows from its family and, for a street, from the street layer's class and
direction.  Which connections a junction carries, where each section starts and what each lane links to
follow from the topology.  All of it is derived in C, and C files the roads, sections, lanes, junctions and
links as records named by place, and answers what each holds.  It never links a lane partway along
another, and it never finds a link by matching positions.

### Layer 2: what each network's roads carry

Each road's lanes follow from its network and, for a street, from the street layer.  The network holds
each lane's id, type, direction and width.

| network | lanes each way | sidewalks | a dead end | a node |
|---|---|---|---|---|
| the road network | 1 for a street, 2 for an avenue, 2 for a boulevard (a `median` lane between) | one each side | turns round | every arm's traffic goes on into every other arm (below) |
| the rail network | 1 `rail` lane, so a pair of tracks, with a `border` lane of gravel outside each | none | a stop: the train reverses on its own track | a single cell of four arms carries its straights alone; any other node is a turnout and carries every way but back |
| the band | 3, with a `median` lane next to the line and a `shoulder` lane outermost | none | a stop | an interchange (below) |

A ramp's road carries one lane, one way, and no sidewalk.

**A movement at a street's node takes as many lanes as the narrower of its two arms holds**, and no two
lanes of one arm go on into one lane inside the junction (Muktadir et al., "Realistic Road Generation:
Intersections", the link configuration).  Straight on, each lane goes on into the lane of its own place.
A turn toward the side the world drives on is made from the outermost arriving lanes into the outermost
leaving ones, and a turn away from it from the innermost into the innermost.  Where the arm reached has
more lanes, the outermost arriving lane straight on also feeds the lanes past its own.  An arriving lane
that so makes no movement at all goes on into the leaving lane of its own place, or the nearest, of the
movement that turns least.

A boulevard's MEDIAN is a lane of type `median` on each side of the reference line, ids 1 and -1, each
half the median's width.  Nothing travels it and it links to nothing, so the lane graph does not hold it.

**A lane's width** is the network's, filed with the lane as OpenDRIVE's `width`: a cubic in `ds` from
its section's start.  It follows from the road's family and class and from the lanes its section holds:

* A road's family and class give its whole WIDTH: a street half a tile, an avenue 0.7, a boulevard 0.9, a
  railway's two tracks and their gravel 0.2, a highway a tile and a half.  A highway's units are two tiles
  wide (`appendix-road-spec`, 7.1, bakes its deck to them).  It is drawn narrower than its units so it can
  fit and curve within them, and its ramps' runs and its interchanges' strands have room beside it.
* A `sidewalk`, a `median`, a `shoulder` and a `rail` lane each take a fixed width: a rail lane's is the
  spacing of its track.  The lanes of the type that FILLS its road share what is left equally: a street's
  and a highway's lanes of travel, and a railway's `border` lanes, its gravel outside each track.  An
  `exit`, `entry` or auxiliary lane is a lane's width of its own, outside them (below).
* A road's sections are centered on its reference line: the section's `laneOffset` puts lane 0 where the
  lanes either side of it balance.
* Where a lane of a section after the first links with lanes of the section before, it starts at the sum
  of their widths, each split among the lanes of this section it links with, and eases to its own over the
  section's first unit: the cubic, with no slope at either end.  So a road's width runs on unbroken where
  its class or its lanes change.  A lane of travel that links with no lane of the section before, where a
  one-way street turns the other way, starts at its own width.  A median, which links with nothing, eases
  in from none.
* A connecting road's lanes ease along its whole length by the same rule: from their shares of the lanes
  they link from to their shares of the lanes they link into.  Its line runs along its lanes (layer 3), so
  its `laneOffset` is nought.  A level meet's connecting roads run along the road's own line across the
  meet, and their `laneOffset` eases from the edge, nearer the line, of the lanes they link from to the same
  edge of the lanes they link into.
* At a node inside a junction every lane has the width and place of the THROUGH lane there.  An `exit`
  lane keeps its arm's width and place to its node.  An `entry` lane that goes on from an `exit` lane
  starts at that lane's and eases to its arm's, and one that does not keeps its arm's.  Every other lane
  at an interchange's node, a turn, takes the `exit` or `entry` lane's there.
* **A ramp's road** carries its one lane one way, as wide as a highway lane, from its gore to the first
  junction its line reaches: an `offRamp` lane, or an `onRamp` lane (OpenDRIVE 1.8.1 §11.7).  The ramp's
  lane is as wide as the highway's outermost driving lane, which it leaves at the gore or joins there, so
  its width does not change on the way: a lane's width changes only where its road's class does.  The
  street junction at the ramp road's far end eases it into the street's lanes, as every connecting road
  eases (above), and there one way meets two.  A ramp's road two ramps want one way each, in opposite
  ways, stays a street.
* **A ramp IS the highway's outermost lane** (`appendix-road-spec`, 7.3), the same for an off and an on
  ramp, mirrored.  The lane leaves the carriageway, and nothing goes on beneath it.
  * **An exit is a lane drop.**  At an off ramp's gore the band road arriving ends at the ramp's `direct`
    junction.  Its outermost driving lane on the ramp's side goes on into the ramp's `offRamp` lane and
    nowhere else.  Every other lane goes on into its own lane of the band road beyond, which carries one
    driving lane fewer on that side, and no shoulder there: the deck narrows to its second lane, and the
    ramp's lane runs on in the strip outside it, where the lane and the shoulder were.
  * **An entrance is a lane gain.**  At an on ramp's gore the band road arriving carries one driving lane
    fewer on the ramp's side.  The ramp's `onRamp` lane goes on, across the ramp's `direct` junction, into
    the outermost driving lane of the band road beyond on its side, and nothing else goes on into it.
  * **The lane comes back.**  Between an off ramp's gore and the gore of an on ramp on the same side, the
    band's roads carry one driving lane fewer on that side.  That is the two ramps of a cross street,
    where the lane leaving and the lane entering are one place on the deck.  An off ramp with no on ramp's
    gore on its side within three units beyond its unit has its lane back at its unit's far border: the
    band road beyond carries the lane again, and it widens from nothing over one unit, fed by the lane
    beside it.  An on ramp with no off ramp's gore on its side within three units before its unit drops
    the lane at its unit's near border: the band road before it carries the lane, and it narrows to
    nothing over one unit, going on into the lane beside it.
  * **No auxiliary lane.**  Two crossings close together on one side share the units between them, each
    ramp's taper taking the units left to it, and the band carries no lane at deck level between them.  A
    gore drops or gains the lane only where the band's roads either side say so, by one driving lane
    fewer or more on its side.  Where they carry as many, the lane comes back and goes again at one place:
    an off ramp's lane divides from the outermost driving lane there, and an on ramp's merges into it.
  * **A road of no units has no length to taper over.**  Where an `exit` or `entry` lane reaches one, it is
    at its full width from its start, and the junction at its other end feeds it from the lane beside it,
    as a section boundary feeds a lane gained.  An `entry` lane goes on there into the lane beside it.
  * An `exit` or `entry` lane adds its width to its road's: the road's own width is its lanes' but those.
  * The taper's length is named once, in `src/network/network.c`.
* A lane's place across its road, `t`, is its section's `laneOffset` and the widths of the lanes between it
  and lane 0.  Nothing else lays a lane across its road.
* The check holds that each section of a road the topology found has lanes that add up to its road's
  width.

Every width is named once, in `src/network/network.c`.

* **A junction acts by its number of arms.**  Two or more are joined by connecting roads.  An interchange
  of two arms is a bend: each lane goes on into the lane of its own place on the other arm.
* **No arm is stranded.**  Where an arm's arriving lanes go on into nothing, or its leaving lanes are fed
  by nothing, its traffic turns round where its network turns (the table above) and stops where it does
  not.  That is one rule for a dead end, a network that reaches a junction by one arm alone, and a street
  whose every other arm is one way out.  A one-way arm has no lane to turn into, and its lanes stop.  A
  lane that stops ends at the junction's edge with no connecting road beyond it: that end is a dead end
  of the lane graph, and every connection the junction files has a road in and a road out.
* **A ramp is one way, as a real one is.**  Every road from the ramp's cell to the first junction its line
  reaches, through any seam or level meet, carries its one lane the ramp's way.  Where that stretch ends at
  a dead end, its lane stops there.
* **A ramp joins the band at its gore** (layer 1), a taper along the band from its unit.  A `direct`
  junction there joins the band's road arriving, the band's road leaving and the ramp's road.  The ramp's
  unit's other border, where the lane comes back, joins its two band roads as a seam does.  Every ramp
  the cells hold is joined.
* **A ramp's side.**  The direction a ramp's lane runs comes from the side of the band its cell stands on
  and the world's driving side: the lanes right of a road's line, as the store places them, travel with
  `s` where the world drives on the right.  Which roads carry one lane fewer follows from the gores alone;
  the network checks no space.
* **A band can end in ramps.**  The unit where a band ends is the band junction of every ramp beside it
  or beyond it, and the band's road ends there.  A ramp beside that unit is read the way that has deck
  behind it (`appendix-road-spec`, 7.3, "Termini"): an off ramp on the side whose traffic arrives at the
  end, and an on ramp on the side whose traffic leaves it.  So no ramp's lanes cross the band.  A ramp beyond the end, in line with one of the band's two halves, is END-ON: the
  half whose traffic arrives at the end, by the driving side, has the off ramp, and the other the on ramp.
  At a band's end, beside it or beyond it, the off ramps' connecting roads share the arriving lanes, and the on ramps' share the
  lanes leaving: each lane links into one ramp or is fed by one, never two.  A ramp beside the carriageway on its own side
  takes the outer lanes, a ramp in line with it the next, and a ramp across the band the inner ones.  An odd lane goes to a ramp
  in line, which goes straight on, so no lane crosses another to reach its ramp.
* **An interchange's movements** are strands: inside its region the highways' bundles are cut, and every
  lane is a road of its own that splits and joins two at a time at `direct` junctions.  The city's ramps
  that join the region are arms of it.  `interchanges.md`, "The splicing", is the design.
* **A lane carried past a seam** merges or diverges there, like any other lane.

* **A street junction's CONTROL**, one an arm (`appendix-road-spec`, 3.4), from the classes of the arms
  meeting there and the traffic (`XTRF`).  Anything against a boulevard: a signal on every arm.  An avenue
  against an avenue: an all-way stop, or a signal where the traffic on the junction's own cell passes
  `JUNCTION_BUSY` (`src/network/network.c`).  A street against an avenue: a stop on the street's arms.  Three arms of streets: a stop
  on the stem, the arm with no arm across from it.  Otherwise a stop on the arms of the axis whose busiest
  arm carries less traffic.  An arm's traffic is the traffic on its road's unit next to the junction.  A
  crosswalk crosses each mouth a stop or a signal controls.
* **A level crossing's CONTROL.**  Where a road crosses a railway at grade, the railway has priority:
  each road arm of the level meet carries a control that yields to it.  It is a record of the network,
  derived from the crossing's cell, and changes no lane or link.  How the crossing is guarded, gates,
  flashers or signs by the road's class (`appendix-road-spec`, 3.15), is the drawing's.

### Layer 2: the sidewalks

The SIDEWALKS are `sidewalk` lanes, built in the same step and held to the same checks.  They form a graph
of their own inside the network: a sidewalk lane links only to a sidewalk lane, never to a lane a car
drives.

* A road that has sidewalks carries a `sidewalk` lane outermost on each side, in every one of its
  sections.  A section boundary cuts it as it cuts every lane, and it links on into the sidewalk of the
  next section.
* A `default` junction carries a CORNER between each two neighbouring arms: a connecting road with one
  `sidewalk` lane, from the outer sidewalk of one arm's road to the facing sidewalk of the next.  The arms are
  taken in their order around the junction, and an arm with no sidewalk is passed over.
* A CROSSWALK is a connecting road with one `sidewalk` lane across an arm's mouth, from the sidewalk on one
  side to the sidewalk on the other.  The junction's control says which mouths carry one, and its marking
  is a `crosswalk` object on that road.
* A dead end's sidewalk runs round its cap: a corner from one side's sidewalk to the other's.
* At a level meet, each sidewalk goes on across the other family's way, as a connecting road of the meet's
  junction.

Every sidewalk lane's end links on into another sidewalk lane or leaves the map.  Layer 2 says which roads
carry sidewalks (`src/network/network.c`), and how wide each is (above).  A raised way, a ramp and a
track carry none.

A power line is not a road.  It is its own network of spans, which needs only the cells and the ground.

### Layer 2: the bridges

A bridge is OpenDRIVE's `bridge`, in the `objects` of the road that runs over it: the `s` it starts at and the
length it spans, in the road's units.  Layer 2 files one for each run of a road's units whose cells carry a bridge
piece of the road's own network, straight after the sidewalks.  The road, its lane sections and its lanes run
on across the bridge unchanged: a bridge is where a road is carried, never a road of its own.  The drawing reads
the bridges as it reads every record, and lays a street everywhere along its line but over its bridges.

### Layer 2: the street lamps

A street lamp is an OpenDRIVE `object` of its road: type `pole`, name `streetLamp`, placed by `s` and `t`
and by nothing else.  A run of lamps along a road is one object with a `repeat`: the `s` it starts at, the
length it runs, and the `distance` between two lamps.  Layer 2 files them from the road's network, class
and lanes (`appendix-road-spec`, 1.6), straight after the sidewalks.  They are records like any other:
filed before any shape, seen in the outline view, and read by the drawing, never placed by it.

| road | the lamp | where across the road (`t`) | the repeat |
|---|---|---|---|
| a street | a cobra head on a davit arm, 9 m | in each sidewalk, at its border with the road | a lamp every 1.5 units, the two sides staggered by half that |
| an avenue | a cobra head on a davit arm, 10 m | in each sidewalk, at its border with the road | every unit, the two sides staggered |
| a boulevard | a twin-arm cobra head, 10 m | in the median, at its middle | every unit |
| a band | a twin-arm cobra head, 12 m | at the reference line, between the two directions | every 1.5 units |
| a ramp, a track, a connecting road | none | | |

A lamp's light falls on the road it stands on, at that road's height, and on nothing above or below it.

* A repeat runs over the whole of each lane section, from half its distance in from each end.  So no lamp stands
  in a junction, and the lamps of two roads meeting at a seam keep their rhythm.
* A lamp's `zOffset` is nought: it stands on its lane.  Its `orientation` faces the road from the side it
  stands on, and a lamp in a median faces both ways.
* At a `default` junction each street or avenue arm carries one lamp at its end, in the sidewalk on the
  right as traffic leaves the junction, so each corner has one.
* A ramp and each strand of an interchange carry one row of cobra heads on the outer edge of the outermost
  lane, every 1.5 units.  A strand a unit long carries one, at its middle.
* The network tests no room or clearance for a lamp: a lamp stands where its record says, and what stands
  near it is the drawing's to give way.

At night a lamp's head is lit, and its light falls on the ground round it.  The ground's light is read
from the records, by place: each chunk bakes the light of the lamps that stand in it, and on the chunks
round it, into a field of its own.  So an edit lights again only the chunks it reached.

### Layer 2: the check

The check reads two things: the store, and the cells through the same readings the network is derived from.
It shares nothing else with what built the network, so it cannot share a mistake with it.  It holds every
rule below on every city, and it is the witness that the network is derived from the cells (`CLAUDE.md`,
THE GOALS).

The unit it counts is an EDGE, never a cell.  An edge of a family is the side two neighbouring cells share,
and it is LINKED where both cells' pieces of that family join it.  A cell is owned by no road.  A cell at
a junction is where several roads end and its connecting roads run.  A cell at a level meet carries a road
of each of two families.  A cell under a slab carries the slab's road over the road on the ground.  For a
band, the edge is between two UNITS: the original lays a band in units of two cells by two, at an even row
and col, and a unit's arms reach the units two cells away.  A RAMP's piece is the one piece that joins two
families: it joins one edge to a line cell and one edge to a cell of the band.  Each is a linked edge, of the line and of the band, and rule 1 holds for each.  The ramp's cell is
the first unit of the ramp's road, and that road ends at a gore on the edge to the band.

1. **Every linked edge is carried once.**  A road's cells are never a junction's.  Each linked edge of a
   family is carried by exactly one of these:
   * a road of that family, between two of its cells next to each other in order;
   * a road's end: between its end cell and a cell of the junction it reaches, across a seam to the next
     road's end cell, or off the map;
   * a junction, between two of its own cells;
   * a road with no cells of its own, between the cells of two junctions side by side.

   No road crosses an edge that is not linked.
2. **Every road ends where its cells end.**  Each end of a road is a junction, a seam, the map's edge, or
   A road and its predecessor or successor name each other, each at the right contact point.
3. **A junction's arms are its edges.**  The arms of a junction are the linked edges that cross the border
   of its cells, and each arm is the contact point of exactly one road.  At a level meet, no connecting
   road joins two families.
4. **Every lane goes on and is fed.**  Each lane's end links on into a lane, and each lane's start is fed
   by one.  Only the map's edge, and a dead end of a family that stops there, take a lane end with nothing
   beyond it.
5. **Every link names real lanes, end to start.**  A lane link runs from a lane's end to a lane's start.
   Inside a road it runs between two sections next to each other.  At a road's end it reaches a lane of
   the road or the connecting road linked there.  Two lanes of one section linking into one is a MERGE, and
   one linking into two a DIVERGE.  Nothing links partway along a lane.
6. **The lanes follow the data.**  A street's driving lanes each way follow the class of the street
   layer's edges along it, and its directions follow theirs.  A road whose edges disagree is a fault.  A
   slab carries three lanes each way, a track two `rail` lanes, and a ramp one lane, one way, from its tile
   to the first junction.
7. **The sidewalks form a graph of their own.**  A `sidewalk` lane links only to a `sidewalk` lane, and
   every one goes on and is fed by rule 4.  Each `default` junction holds a corner between each two
   neighbouring arms that carry sidewalks.

A fault names the rule, the family, and the edge, lane or junction it is found at.

### Layer 3: the shape

Each road's reference line seen from above, OpenDRIVE's `planView`, and its height along it, its
`elevationProfile`, worked out in C from the network and the land alone.  It adds, removes and relinks
nothing.  Every number it is tuned by is named once, in `src/network/shape.c`.  `tools/roadfit` is the
prototype it was tested with.

* **A run.**  Roads linked across a seam, and a band's roads linked across a gore, are shaped as one RUN, from the junction at one end to the
  junction at the other: a seam is a chunk's border, and the shape never sees it.  A run that comes back to
  itself across seams alone is a ring, opened in the middle of its longest straight.  The run's cells are
  its roads' units, with the unit of the junction it touches at each end.
* **The line from above: the tangent fit.**  On the grid of the network's units, from the run's first unit,
  the longest run of units that is a STRAIGHT, then the next from its last unit.  A run of units is a
  straight when one line passes through every unit's square, and its steps keep a straight line's rhythm:
  every step across goes the same way, two never come together, the runs between them differ by at most
  one, and it steps across never or at least three times (one step is a corner and two a jog).  A run of
  four moves or more that alternates its two steps every time is a 45° diagonal, however often it steps
  across.  Two
  straights meet at a vertex, and each vertex is an arc: the largest radius up to the network's own at
  which both edges of the road stay inside its CORRIDOR, its units widened by a quarter tile, and whose
  tangents take at most half of each straight beside it.  A road keeps its angle into a junction.
* **The widths it is fitted to.**  The fit keeps the road's whole width (layer 2: the sum of its lanes'
  widths) inside its corridor, with corners up to a tile and a half for a street and four tiles for a
  highway.  It reads the widths and never sets one.
* **Mapping `s`.**  Each unit boundary of a road maps to the nearest point of its line, in order, so a lane
  section keeps its place inside its unit and `s` along the line grows with `s` along the units.
* **A junction's boundary** is the outline round its arms' mouths, where each arm's line leaves the
  junction's units.  Each arm's road ends at its mouth.  A dead end's boundary runs on from its one mouth,
  the road's own width, across its whole unit: past the end of the road's line, where traffic turns
  round, to the unit's far edge.
* **A connecting road's line runs along its lanes**: through the inner edge of its lanes, from the place
  where they leave to the place where they arrive, heading the way its traffic goes and tangent to the way
  at both.  So a bend need only be wider than its own lanes.  The inner edge at a place is the inner edge
  of the lane it links with there.  Where several connecting roads part from one lane side by side, or
  join into one side by side, each takes its place across that lane in turn, the way straight on nearer
  the road's line and an `exit` or `entry` lane outside it.  A level meet's connecting roads are the
  exception: they run along the road's own line across the meet.  A place is one of two kinds:
  * **An arm's mouth**: the end of the arm road's line.  A road of no units has no line, and its mouth is
    the middle of the edge its two units share, heading out across it.
  * **A node inside the junction.**  It lies along the way THROUGH the junction that it splits or joins,
    heading along it, at the inner edge of the lane that splits or joins there.  So the lanes that part there leave the through lanes as a lane drop does,
    and nothing turns sharp at the node.
  * **The way through** a junction from an arm is the connecting road straight on from it, where the
    junction carries one, and else the arm's line carried straight on.
  * Every place in a junction with one height stands at that height.
* **A ramp's line is the highway's outer lane, carried on** (`appendix-road-spec`, 7.3).  At a gore the
  band's roads either side are one line, as at a seam, and the ramp's road is a road of its own that leaves
  it or joins it there.  From the gore it runs along the band on the middle of the outermost driving lane
  it was, where the band before the gore lays that lane, tangent to the band, over the taper to the near
  border of the ramp's unit.  There it TURNS OUT: one S across the ramp's unit and its cell, to the line
  fitted through its cell, by the transition curve (`interchanges.md`, "The shape").  It meets its street
  at its cell's far edge head-on: the last half tile into the street runs along the fitted line.  An on ramp's line is the mirror.
* **A ramp's height.**  Along the taper the ramp's road comes down in a straight line from the deck's
  height at the gore to the land at the near border of the ramp's unit.  Its turn-out lies on the land,
  and so does its cell.  An on ramp climbs the same way to the deck at its gore.
* **The height: one solver.**  A road's height is the smoothest profile (the least squared change of
  grade) between a floor and a ceiling, its ends fixed, drawn gently toward a target.  It is solved
  directly along the road's band of equations.
  * A STREET's target is its own tiles' heights under its line.  It stays within a level of them, never
    above the highest or below the lowest of them within two tiles, and comes level into each junction.
  * A HIGHWAY's deck stands a level above the highest ground under each unit, over the whole of the unit,
    from the center of the unit at its run's first end to the center of the one at its last: a junction's
    unit, where the run ends at one, so each road lies over its floor to its end.  A highway JUNCTION
    has one height: the highest floor any of its arms needs at its center.  A dead end that no run reaches
    stands a level above the highest ground under its unit, and across a road of no units it takes the
    height of the junction beside it where that is higher.  Every arm's deck ends at that
    height there, and the junction's surface rises from its center to each arm's deck edge at its mouth.  It never climbs steeper than
    18.4°, a level for each tile and a half: where the ground steps up it starts climbing early, and it is
    only ever lifted to keep the grade, never lowered.  It holds a level across a
    dip narrower than eight tiles and no more than a level deep, and may rise a quarter level above a level
    it holds, so it eases onto it.  Its ends are free in grade.
  * A RAMP's height is its own rule (above).
* **The land is never written.**  A road's height is its own, so taking a road away leaves its tiles as
  they were.
* **It can be built progressively.**  A run reads only its own units, its two junctions' units and the
  land under them.  An edit shapes again the runs whose units changed and the junctions they reach, and the
  result does not depend on the order.  Nothing of it is saved: it follows from what the `.arco` holds.

### The drawing

How the shaped network is laid as surfaces is `plans/drawing.md`.

### Traffic

On the lanes alone.  A car is a lane and an s.  At a lane's end it follows a link at the node there, and it
moves from one lane to another only at a node.  Its height and heading are evaluated from the lane, never
stored.
