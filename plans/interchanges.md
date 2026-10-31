# Arcology — interchanges: the design

How highways meet.  This page holds the design and nothing about where the build stands; the status is
`network-status.md`.  It takes its rules from highway engineering practice and cites them, and it adds to
`network.md` only what an interchange needs: the layers, the records and the words are that page's.

## What it is for

Where two or more highways meet, the cells hold an INTERCHANGE: a block of interchange units and the arms
that reach it.  The cells say which arms meet and nothing else.  The network files every movement between
the arms, and the shape lays them out as a real interchange is laid out, compact, close round its block.  The original's art is no reference for any of it.

## The sources

* Design manuals, read in full: WSDOT Design Manual M 22-01 chapter 1360; Caltrans Highway Design Manual
  chapters 300 and 500; INDOT Design Manual chapter 48; SCDOT Roadway Design Manual chapter 10; TxDOT
  Roadway Design Manual 15.3 and 15.7; WisDOT FDM 11-30; UK DMRB CD 122; IRC:92-2017.  The AASHTO Green
  Book (2011) chapter 10 reaches this page through the tables those manuals copy.
* Design and generation: Vázquez-Méndez, Casal, Castro and Santamarina, "An algorithm for random
  generation of admissible horizontal alignments for optimum layout design", Computer-Aided Civil and
  Infrastructure Engineering 36 (2021), and Vázquez-Méndez, "Automatic Design of the Horizontal Alignment
  of a Road Interchange: The Case of a Trumpet-Type", Journal of Surveying Engineering 149 (2023); Lee and
  Cheng, "A model for calculating optimal vertical alignments of interchanges", Transportation Research B
  35 (2001); Wilkie, Sewall and Lin, "Transforming GIS Data into Functional Road Models for Large-Scale
  Traffic Simulation", IEEE TVCG (2012); Zhou et al., "FLYOVER", arXiv 2301.12738 (2023).
* Real examples: the Dallas High Five (TxDOT), the Los Angeles Four Level, the Springfield and Marquette
  interchanges, Gravelly Hill.

## The scale

A tile is 15 m (`docs/appendix-road-spec.rst`), and a level is half a tile, 7.5 m: one level of the
original is one level of a stacked interchange (5.03 m of clearance and a deck, about 6.9 m).  The manuals'
own dimensions, in tiles, are these:

| what | value | in tiles |
|---|---|---|
| a loop's radius | 45 to 60 m, never under 37 m | 3 to 4, never under 2.5 |
| a direct or semi-direct ramp's radius | 260 m or more | 17 or more |
| an exit's divergence | 2° to 5° | |
| a deceleration lane, highway at 70 mph | 105 to 170 m | 7 to 11 |
| an entrance's convergence | 50:1 to 70:1, an acceleration lane of 180 to 490 m | 12 to 33 |
| ramp noses in a row: two exits, two entrances | 300 m | 20 |
| ramp noses in a row: an entrance, then an exit | 490 to 610 m | 33 to 41 |
| a ramp's grade | 5 to 7 % at most | |
| a climb of one level, curves included | 160 to 275 m | 11 to 18 |

**An interchange is drawn COMPACT, not at those dimensions.**  A city's interchange block is two band units
a side, and the original's arms are seldom longer than a few units before the next ramp.  So every gore
stands within `IX_GORE_OUT` tiles of the interchange's border, and the radii and lengths are the widest the
solve finds inside that reach.  A ramp climbs a level a tile at most, as a ramp's own cell does.  The
manuals' table is what the solve reaches toward, never what it must meet.

## The model: bundles and strands

A highway is a BUNDLE of lanes: outside an interchange its lanes lie side by side and move as one.  Inside an
interchange the bundle is cut, and each lane that turns comes away from it as a STRAND, free to stretch, bend
and cross another strand at another level.  The lanes straight on stay a bundle, free to bend as one.  An interchange is then an exercise in splicing: each strand that arrives is joined to a lane
that leaves.  Which lane joins which is the graph (layer 2), and it follows from the cells alone.  Where each
strand runs is the shape (layer 3), and it changes no record.

* **The REGION** is the interchange's own units and every band unit a ramp joins that links to them, merged
  along the band.  Every road that reaches the region is one of its ARMS: a highway that leaves it, and
  each ramp of the city that joins it.  A city ramp is an arm like any other.
* **Each arm is cut at the region's border by a GORE** (layer 1).  Inside the border there is no `default`
  junction.  A highway's inner lanes stay one bundle, which bends across the region whole to the arm across.
  Its outermost lane leaves the bundle as the turns, and the city's ramps are strands too.
* **Every movement the cells allow is made**: each way into the region goes on into each way out of it but
  back the way it came (Caltrans 502.3(2)(c), "each of the eight basic movements").  An on ramp is a way in
  whose traffic may go wherever the highway's traffic beside it goes on to.  An off ramp is a way out that
  takes the traffic of every movement that reaches the highway beside it.

## The splicing (layer 2)

The splicing is OpenDRIVE's own (ASAM OpenDRIVE 1.7, chapter 10): a junction's connecting road is one whole
path from a road in to a road out, and "connecting roads shall not be incoming roads".  So a strand that
splits from another strand, or joins one, is not a connecting road.  It is a road of its own, and where two
strands meet is a `direct` junction, which OpenDRIVE has "to model entries and exits".

* **A strand is a road of one `connectingRamp` lane, one way,** in no junction and on no units.  It runs from
  one node to the next: a gore, a split or a join.
* **Straight on, an arm's inner lanes are one road into the arm across**, each lane into the lane of its own
  place.  The outermost lane does not go straight on.
* **The outermost lane IS the turns.**  At its last node it divides into the arm's turns, and nothing of it
  goes on straight: it does not run on beneath them (the manuals' lane drop).  Traffic straight on moves to
  the inner lanes along the arm before the region.  On the way out, the turns that reach an arm join, two at
  a time, and become its outermost lane again, beside the inner lanes (the lane gain), where traffic
  straight on may move into it.
* **A city's ramps on an arm splice into its outermost lane.**  A ramp whose traffic heads into the region
  is on the arriving outermost lane, and one whose traffic heads out is on the leaving one.  Its split or
  join is a node of that lane beside the highway, and a strand runs between that node and the ramp's gore.  Along an
  outermost lane an off ramp splits off before an on ramp joins, so their traffic never weaves.  On the way
  in the ramps come before the turns, and on the way out after them.
* **An arm with no way straight on has no bundle.**  Its inner lanes join its outermost lane after its gore,
  two at a time, and on the way out its outermost lane splits into them before its gore.
* **Where one lane carries more than one movement, it SPLITS, two at a time,** nearest turn first.
  The split is OpenDRIVE's diverge: a road of two lanes side by side, the new lane widening from nothing
  beside the strand, then a `direct` junction where the two part.  A third movement splits from one of the
  two the same way, further on.
* **Where more than one strand must end in one lane, they JOIN, two at a time,** farthest turn first: a
  `direct` junction where the two meet and go on as one road of two lanes side by side, then one lane
  narrowing to nothing, its traffic moving over.  The turn from farthest across is the lane, and each nearer
  turn joins it as the lane outside, as it arrives from outside.  So no two strands cross where they join.
  The next strand joins the one lane so made, further on.
  No node joins or splits more than two.
* **At each gore, one `direct` junction** joins the arm's lanes to the strands that leave them and to the
  strands that reach them.
* The region's junctions are one `junctionGroup` of type `highwayInterchange`.

So the lane graph is the same for a stack, a turbine and a braided cloverleaf: the forms differ in the shape
alone.

### Where two highways cross: the cloverleaf

A region whose four arms are two highways that cross is spliced as a CLOVERLEAF, the form a compact block has
room for (Caltrans 502.3, the full cloverleaf).  Each left turn is a LOOP that turns three quarters of a turn
right in the corner beyond the crossing, and each right turn leaves its arm's outermost lane near the gore.
Along each carriageway, between the loop that brings traffic onto it and the loop that takes traffic off it,
the two streams cross by a WEAVE.

* **On the way in**, an arm's outermost lane takes its city's ramps as above, then splits once: the right
  turn leaves it as the lane outside, and the lane goes on with the traffic for the left turn to the
  carriageway's weave.
* **A WEAVE is a road of two `connectingRamp` lanes,** from a `direct` junction before the crossing to a
  `direct` junction beyond it.  Its inner lane is fed by the arriving outermost lane, and its outer lane by
  the loop that brings traffic onto this carriageway.  At its end the outer lane goes on into the loop that
  takes traffic off, and the inner lane into the leaving arm's outermost lane.  Its traffic may change lanes
  along it, both ways (OpenDRIVE's `laneChange`), and that is how each stream reaches its lane: no strand
  crosses another at one level.
* **A LOOP is a strand** from the outer lane at one carriageway's weave's end to the outer lane at the start
  of the weave of the carriageway the turn joins.
* **On the way out**, the leaving arm's outermost lane begins at the weave's inner lane, takes the right turn
  that reaches it as the lane outside, then gives its city's ramps their splits as above.
* Every movement is made: a left turn rides the weave's inner lane, moves out, loops, rides the next weave's
  outer lane and moves in.  The traffic of a loop that comes onto a carriageway and goes on straight moves
  in along the weave, and that going on to the next loop moves out.

## The shape (layer 3)

The shape lays the strands out where there is ROOM.  Along an arm, the room reaches to the next junction of the
highway.  Across, every cell is room but a cell under a building with height: the highways are raised, and what
stands on the ground below never stops a strand.  Room changes the shape alone, never a record.

* **The cut reaches out along an arm where there is room.**  Each gore stands on its arm's line, out from the
  region's border by `IX_GORE_OUT` tiles at most and short of the arm's other end, or short of half way where
  another region's gore is there.  How far is a place.  The arm's line ends at the gore, and the heights along
  the arm stay where they are.  So the search scores the arm's deck that stays as it scores a strand: a stretch
  of it steeper than the greatest grade counts as grade over.  An arm with no line, a road of no units, has no room: its gore stays at the
  border, and the strands use the highway's own width.
* **The nodes of an arm's outermost lane lie along that lane, in their order from the gore**: the arriving
  lane's ahead of its gore, the leaving lane's behind it.  How far each lies from the last is one of the
  region's PLACES (below).  Each strand between two of them runs along the lane, and a pair's two lanes lie
  side by side on it.  Where the arm goes on straight, its outermost lane runs beside the bundle's line, as
  far across from it as at the gore, so the lanes straight on bend as one until a strand peels off.
* **In a cloverleaf** the weave runs along the carriageway beside the bundle, a lane farther out, from its
  node before the crossing to its node beyond it.  A loop is an oval of two circular arcs that share its turn
  equally, about three quarters of a turn, from the weave's outer lane to the next weave's outer lane, tangent to both.  The
  loop's radius is a place.  The loop places the two weaves' nodes it meets: each lies where a circle of that
  radius, in the corner between the two carriageways, touches the weave's outer lane.  The two radii of the
  oval follow from where those nodes lie, so the loop always meets both lanes, and it is round where the lanes
  run straight.  A weave's node that would lie less than `IX_GAP_LO` past the node before it on its lane
  counts against the loop as a radius short.  A loop whose radius would fall below `IX_LOOP_R_LO` is not routed.  A right
  turn is one transition curve from its split to its join, and it rides over whatever it crosses.
* **A left turn swings wide of the middle.**  A turn to the far side runs through a place beside the middle,
  out on the side away from the turn, and the two left turns of one highway pass each other side by side.  How
  far out it swings is a place, and how far that place moves along the way it turns is another.  A left
  turn rides over the highways, so it may take all the room above them for a wide curve.
* **A city ramp's strand reaches back along the road it joins.**  The strand ends a distance along the ramp's
  road from the ramp's gore, tangent to that road's line, and the road's line starts there.  That distance is a
  place.  So the ramp climbs its level over the street, never inside one cell.
* **Beside the highway a city ramp's strand runs on as a lane of its own.**  From the node where it parts
  from the outermost lane, or up to the node where it joins it, the strand runs straight on beside that
  lane, one lane out, before it bends away to its road.  This is its deceleration or acceleration lane, and
  how far it runs is a place.  It runs `IX_RUN` tiles where there is room, and the search makes it no
  shorter than it must.  It bends as the outermost lane does.  The run, a pair's least length and a turn's
  radius are wishes, not rules: where no layout with them can be routed, the region is searched again with
  no run, then with no least length, then with no radius.
* **Each strand PEELS off its lane, as a strand off a bundle.**  Its line starts on the middle of the lane it
  leaves, tangent to it, with no bend, so it lies on the highway as it starts.  It ends on the middle of the
  lane it joins the same way.
* **Each strand is the transition curve of the design standard** (Vázquez-Méndez et al. 2021): a spiral from
  the line it leaves, an arc, and a spiral onto the line it joins, the farther place's extra length a line.
  The spirals take `IX_SPIRAL` of the turn, so a bend grows from nothing and dies away again.
* **A split's or a join's two lanes ride side by side** on its road for the length the shape gives it, and the
  lane that appears or ends eases between nothing and its width over that length.  Every pair of one lane
  runs one length, `IX_PAIR_MIN` tiles at least: the city ramp's join and the turns' split on the way in,
  the turns' join and the ramp's split on the way out.  So the room along the lane is shared equally, and
  both the ramp's traffic and the turns' traffic have a lane to move into or out of.
* **The bundles straight on bend too.**  A bundle runs from its arm to the arm across even where the two do
  not line up.  Where they do not, it goes from gore to gore in two turns: it leaves its arm straight, turns
  toward the far arm's line, runs straight, and turns back onto it.  Each turn is two spirals over a
  quarter of its length, so its curvature is continuous.
* **The places are SEARCHED, one region at a time.**  Each candidate set of places gives every strand its line
  and then its heights (below).  Its score is, in order of weight: any strand against its own traffic; how far
  its least grade is over the greatest grade; how much tighter than `IX_TURN_R` each turn bends,
  so the search shortens a lane's pairs together where the turns need the room; how far a city ramp's run beside the highway falls
  short of `IX_RUN`; the length of strand that lies over or under another,
  as each such stretch is a structure, weighed lightly, as a left turn is meant to ride over the rest; its sharpest curve; and any turning a strand does beyond its own turn.  The search is differential evolution
  (Storn and Price 1997) from a fixed seed, so one city always gets one shape.  Its best is then polished by
  a pattern search (Hooke and Jeeves 1961): each place in turn tries a step either way, and the step halves.  The places it finds change the
  shape alone, never a record.
* **An interchange the search cannot route is a PLACEHOLDER.**  Where the best places found still leave a
  strand against its traffic, a grade over the greatest or a road with no line, the region's roads get no
  line, and no arm or ramp road is trimmed for it.  Its records stay whole, and the lane graph through it is
  the same.  The drawing stands a placeholder model on the region's units, and the check counts it as a
  placeholder, not as a fault.

## The levels

A level is a fact of a CROSSING, never of a road (OpenStreetMap's `layer`; Wilkie et al. 2012).

* Where two ways cross in plan, one passes over the other by a level.  Ways that do not cross need no
  order.
* Which way goes over is a fact of the two ways' kinds.  From the lowest: a city ramp, the first highway with
  its outermost lanes, a right turn, the second highway with its outermost lanes, the first highway's left
  turns, the second highway's left turns.  In a cloverleaf: a city ramp, the first highway with its
  outermost lanes and weaves, a loop, the second highway with its outermost lanes and weaves, a right turn.  Two left turns of one highway that cross each other cross where
  one of them has more room to climb: the one farther from its own ends goes over.
* A split's or a join's two strands ride side by side until they part, and a strand lies on its own lane
  where it peels off it or onto it.  Neither is a crossing.
* Then every height.  Each strand is sampled along its line, and every rule is a difference between two
  heights: one sample to the next climbs no more than the grade of its kind allows, a way over another clears it by a
  level, and nothing goes below the ground the streets stand on.  Each end is pinned to the height of the
  road it meets, but a gore.  Each arm's deck takes part in the solve over its APPROACH, `IX_APPROACH`
  tiles back from its gore: its samples keep a deck's floor and grade, the farthest is pinned to the arm's
  height there, and the gore is free.  So a carriageway is one profile from its arm into the region, with
  no kink at the gore, and the arm keeps the eased heights over its approach.  A system of differences has a solution where its graph has no negative cycle
  (Bellman-Ford), so the least grade at which every rule holds is found by halving.  At the greatest grade,
  the heights are then eased: each road's sum of the squares of its bends is made the least its rules let it,
  each sample in turn, and each node takes the mean of the samples next to it.  So a deck bends smoothly, and
  two roads that meet at a node meet in line.

## What the shape keeps

* **No two ways on one level overlap**, apart from a split's or a join's two lanes side by side, and a
  strand where it peels off a lane or onto one.  A check measures it on every interchange, and a fault is never accepted.
* **No strand runs against its own traffic**: along every strand's line, its heading stays within a
  quarter turn of the way from its start to its end.  A check counts every strand that does not.  A
  cloverleaf's loop turns three quarters of a turn, so it is not held to this.
* **Every crossing has its level of clearance.**
* **Every ramp keeps the greatest grade of its kind**: a city ramp's strand a level a tile, as a ramp's own
  cell climbs, and every other way of the region a deck's grade.  A check counts every road steeper than that.
* **Every ramp keeps the least radius of its kind.**
