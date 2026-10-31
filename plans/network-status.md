# Arcology — the network: where it stands

Where the build stands against `plans/network.md`, and what is left to build.  When the network is done,
this page is deleted: the open work goes back into `TODO.md`.

## The build

The road network is derived in C from the cells and the street layer (layers 0 to 2), and shaped in C
(layer 3).  The drawing (`plans/drawing.md`) draws from it the streets whose every tile is a street piece,
their junctions, the railways and their turnouts, and the level crossings between them.  It draws each
highway road with a line as a deck (`src/render/draw/bands.c`), the strands of a routed interchange among them,
a box girder deep, and their tiles' art is not drawn.  Each stands on columns, one a tile under each way's traffic, kept clear of the
roads below, an interchange's strands among them.  Where strands peel off a lane, and where a ramp's
lane merges into the band at a gore whose roads carry as many lanes, the two decks overlap; where a city
ramp's strand comes down to its street its deck passes through the ground; and a column on sloping
ground passes into it, as a pylon does.  A highway's dead end is capped, and a carriageway keeps one height profile from its
arm into an interchange.  An interchange's exits and entrances peel off at their gore with no taper (spec 7.3).  A rail or street
dead end beside a level meet is capped across its cell, through the road of no units between them.  A street's
bridges are filed as objects of its road (`bridges`, `src/network/network.c`), and the street is laid along
its line everywhere but over them.  Nothing draws the bridge itself, a tunnel, a meet where one family turns,
an interchange that is a placeholder, two railway junctions whose outline fails, a railway over a bridge, or
a road any of whose other tiles carries another piece.  The geometry view hides
the art of every tile that carries a road, railway, highway or power line (`src/app/world.c`, `a->shaped`),
drawn or not, so each of them is a gap: a street's bridge span is water with the street's lamps standing
over it.  Nothing moves on any road.

| file | layer | what it is |
|---|---|---|
| `src/sim/net_cell.c` | 0 | each cell's parts, from the original's fitters and placers; `ctest -R net_cells` holds it |
| `src/network/topology.c` | 1 | the units, their links, the ends (node, dead end, level meet, seam, map edge) and the roads between |
| `src/network/network.c` | 2 | THE ROAD NETWORK: roads, lane sections, lanes, junctions, connections, controls, ramps, sidewalks, lane links |
| `src/network/street.c` | - | the street layer's import: each street's class, once, when a world has none |
| `src/network/shape.c`, `bank.c` | 3 | each road's line and height, each junction's outline; the ground beside a street |
| `src/render/draw/roads.c`, `rails.c` | drawing | the streets, junctions, crossings and dead ends; the railways, turnouts and buffer stops |
| `src/render/draw/net_graph.c` | - | the lane graph laid out as a diagram, from the records |
| `src/dev/net_svg.c`, `net_overlay.c` | - | that diagram written to a file, and laid in the outline view |

`arcology CITY --net-check` builds layers 1 and 2 alone, with no mesh, and holds them to the lane check:
every lane of travel goes on and is fed, every sidewalk has a lane at each end, and no road's contact is
one node with a node inside a junction.  An end needs no link at a STOP: the map's edge, and a stranded
arm whose network does not turn round (a band or a railway) or that is one way.  `ctest -R net_check` runs it over every shipped
city, and every lane holds.  `--net-dump` prints layers 1 and 2, and `--net-svg FILE` draws the lanes: each by
its type with an arrow the way it runs, each link dashed, each junction's cells, and each fault in red.
The network files the lane graph's nodes: every lane end is at one node, and
two ends a link joins are at the same node, by the contact point each link names.  The outline view
(`--outline`, or the View menu) lays the lane graph in the world, and `--net-svg` writes it, from one layout
(`src/render/draw/net_graph.c`): each lane and sidewalk at a port of its own on every cell border it crosses,
each node placed, each connecting road's lane rounded from the node at its start to the node at its end,
with an arrow the way it runs.  The three counts of invariant 4 are zero.

`--net-check` also holds every lane link in the world (`shape_lane_check`): at each lane end the lanes
it links with span what it spans, at one height.  A link to a road with no line is counted as not
measured.  It finds one fault across the corpus's junctions: a corner sidewalk lies a sidewalk's width
outside the arm's sidewalk it links with, because layer 3 lays the corner's line along the sidewalk's
outer edge.  It also finds six nodes of three lanes where one lane stands a level or more from the
other two: five on test.arco and one on Atlanta, at gores and in an interchange.

### Where it breaks the design

Nothing the records hold breaks invariant 4.  A node makes no movement that runs back on itself
(`network.c`), and a unit of two links between two units of one cluster of node units is inside that
junction (`topology.c`).  What the check does not see is whether a movement is the RIGHT one: that is for
the diagram to show and the user to judge.

Each junction's connections are one run of the list, `conn0` and `nconn`, in the order they were filed: they
are grouped by junction once every connection is filed (`group_connections`, `network.c`).  The lane check holds
it: a connection filed under a junction whose connecting road is in another counts as a fault.

The network is kept by place (`src/network/store.c`).  Each build keeps what it derived, every layer's state
and the city it read, as one stored network, and each chunk it laid points at it.  A reader of a place loads
the network of that place's chunk.  `tools/window_witness.py` holds a window built alone to what the whole
build lays in each of its chunks.  A window build traces the topology over the whole map once, to follow
each road that reaches into its margin to its ends (`topology_reach`).  That trace reads every cell of the map.

Where the build breaks invariant 2, the store keyed by place:

* **An interchange numbers its parts in the order the topology found them.**  The ids of its connecting
  roads mix in the number of each arm and ramp, taken from the order of the topology's ends.  That order
  follows the roads the topology traced, not where the ends lie.  A window whose margin is under
  `net_halo` gives the same roads other ids (`tools/window_witness.py`, test.arco, chunk 9,1).
* **An interchange's shape depends on the order of its arms.**  Taking the arms in the order of the places
  their gores name moves faces of the interchange in a whole build.  The same arms in another order are to
  give the same interchange.

Where the build breaks the design on lane widths:

* **The fit leaves room for a family, not a road.**  Layer 3 fits every road to its family's width
  (`network_width(net, 0)`): an avenue and a boulevard are fitted as a street is, though their lanes are
  wider.
* **No view shows the lanes along the shaped line.**  The initial view draws the provisional line through
  the cells, as designed, so it parts from the render wherever the fit straightens a staircase.
* **Two ramp tapers that overlap on a short highway road file its outer lane at twice its width.**  Its
  records run at the double width from start to end, so the lane check finds two sections of test.arco
  whose lanes add up to more than their road.

Where the build breaks the design on heights:

* **A crowded region's highway floor gives way.**  An interchange that cannot be routed with every
  highway strand on its deck's floor is searched again with the floor at half its clearance, then on the
  ground.  The pair of interchanges a unit apart in test.arco routes only at half its clearance.  Its
  strands then pass through one another: about two thousand face pairs of the mesh check.
* **A highway dips under a ramp that crosses it near the gore it leaves.**  The ramp cannot climb a level
  over the highway within the grade in that distance, so the highway drops to pass under it.  The search
  places the crossing and does not weigh the dip.  In test.arco, interchange roads climb about 170 levels
  that they then come down again.
* **A road's profile is smooth inside the road, not across its ends.**  Each change of grade inside a road
  is a vertical curve.  Where two roads meet, the grade can change at once.

## What the network holds

The street layer is a layer of the city's tile store, saved in the `.arco`, cleared by the bulldozer and
imported once.  The street tools are built: Upgrade Street and One-way Street, the road tool's sixth and
seventh sub-tools (`src/sim/sim_tool.c`).  A click takes the whole street through the tile, and a drag the
edges it crosses.  Left in it: a class laid by each road tool, which waits for the road placer (`TODO.md`,
"The line tools").

Every cell is read the same way: junction units of one network and one kind that link merge into one
junction, and two junctions of different kinds side by side are joined by a road of no units.  A ramp's
cell is the first unit of the ramp's road.  Beside a plain band unit the ramp is the band's outermost
lane (`appendix-road-spec`, 7.3).  Its gore lies up to two units along the band from its unit, a `direct`
junction there joins the band's roads to the ramp's road, and the band carries one driving lane fewer on
that side until the partner ramp's gore or the lane's return at the ramp's unit.  Where the band's roads
either side of a gore carry as many lanes, the ramp's lane divides from or merges into the outermost
lane there.  Layer 3 runs the ramp's line along the band from the gore and turns it out to its cell.
Two ramps on the two carriageways can share one gore.  Breaches: the ramp's run is drawn as a street,
with no girder, parapet or column, and the decks have no lines.  A ramp whose taper has no unit to run
into joins its unit as a band junction of its own, through an auxiliary lane and a node per ramp inside
it, as the design's fallback does not have it: ten on test.arco and one on Atlanta.  A ramp at a band's
end takes or feeds every lane, where the spec reads it as the ramp that leaves a taper along the deck.
Where two ramps' gores fall on one border, the one direct junction joins two roads on each side, which
OpenDRIVE's direct junction does not allow.  Where the band ends at the
ramp's band unit, the ramp takes or feeds every lane.  An interchange is filed as `interchanges.md`'s strands: a region of the
interchange's units and the ramp units beside it, a gore on every highway arm and every city ramp at its
border, a bundle of the inner lanes straight on from gore to gore, and each arm's outermost lane a chain of
strands that takes the city's ramps and splits into the turns, or takes the turns and the ramps, two at a
time.  A join is a `direct` junction and a road of two lanes after it, the joining lane an `entry` lane
narrowing to nothing; a split is a road of two lanes, an `exit` lane widening from nothing, then a
`direct` junction.  Layer 3 searches each region's places (`interchanges.md`, "The shape"): the gaps
between the nodes of each outermost lane, each left turn's swing and each city ramp's reach back along its
road.  For each candidate it lays the lines and solves the heights as a system of differences, and it files
the best.  An interchange whose best layout still breaks a rule is a placeholder: its roads get no line.
On test.arco five of eleven interchanges are routed and six are placeholders, and Atlanta's one is
routed.  Three placeholders are cloverleafs whose gores lie too close to the crossing for a loop of the
least radius: the circle touches the lanes 0.5 to 3 tiles behind the nodes before it.  One has two left
turns against their traffic, and two are over the greatest grade.  The original's sprites stand as every
placeholder's model.  Each gore lies out along its arm by a searched distance where the arm has a line.
A highway road that curves just before a region's gore can climb steeper than the grade, its height spread
by its units over a shorter line.  A
lane on a road records where its traffic may change lanes (`change`).  Each road arm of a level
crossing carries a control that yields to the railway, and each way crosses the meet by a connecting
road.  Every dead end is a junction of its own unit.  A lane carried past a seam merges
or diverges there.  The network checks no space: every ramp the cells hold is joined, and no band road
carries an extra lane but the auxiliary lane between an on and an off ramp.  A
junction acts by its number of arms: two or more are joined, and an interchange of two arms is a bend.
No arm is stranded: an arm whose lanes have nowhere else to go turns round where its network turns (a
street) and stops where it does not (a highway, a railway).  A lane that stops ends at the junction's
edge, a dead end of the lane graph, and the diagram draws it on across the junction to show it.  A ramp is one way: every
road from its tile to the first junction its line reaches, through any seam or level meet, carries its
one lane the ramp's way, and where that stretch ends at a dead end its lane stops there.  Where the street
layer's one-way stretches meet head on or part tail to tail, the road has a section boundary there: each
stretch's lanes end at terminal nodes of their own, nothing links them, and only the sidewalks go on
across.

## What is left

Bottom up, because each layer reads the one under it.

1. **The graph (layers 1 and 2).**  Judged in the diagram over every case of the test world, and
   accepted.  It is extracted from the cells and follows them.  A new case the cells hold is added to the
   test world and judged in the diagram before the graph changes for it.
2. **The shape (layer 3).**  Written in C (`src/network/shape.c`) from the design tested in
   `tools/roadfit`.  Every road of the road network, the band and the railways has its `planView` and its
   `elevationProfile`, every traffic connecting road its line, ramps' and interchanges' lanes to and from
   the nodes inside their junctions among them, and every junction its boundary.
   `--net-check` reports the shape's check, and `--net-dump` prints it.  The shape's check reports three
   highway decks steeper than 18.4° across the shipped cities, not explained.  `tools/roadfit/c_view.py`
   draws what the C filed.  Not shaped: a crosswalk, a sidewalk across a level meet, a dead end's
   sidewalk round its cap, a corner sidewalk where its corner has no curve, a U-turn, a tunnel (its height
   follows the land over it), and a bridge (a road over water stands on the water's surface).  A
   ramp beside the band, not at an interchange, peels off the highway from its node, but its height is a
   street's, from its own cells, and no check holds it to the deck it leaves.  A ramp at the band's end whose street turns back beside the band keeps that turn.  The ground beside a street (`src/network/bank.c`)
   breaches `plans/terrain.md`: the drawn ground takes the lowest of the streets' grades AND the land, so
   beside a street above its land the ground stays at the land.  The report, read on the drawn ground
   (`bank_ground`), finds that at 6,175 street edges in Atlanta, and in 81 of the 102 shipped cities.
   Taking the lowest of the grades alone, as the page says, closes those and leaves the edges where two
   streets' banks overlap at different heights, 15,176 across the cities.  It also raises the ground into
   highway decks and columns beside a ramp: 2,356 more colliding face pairs in the test world.  Which
   surface yields there, a wall or the deck, is the open question the page names.
3. **The drawing** (`plans/drawing.md`).  Built ahead of its design, so where the two differ is a breach
   of that page.  Not drawn, and a gap in the geometry view: the roads listed under "The build".
4. **Traffic** on the filed lanes: the cars and the trains.

## The test world

`cities/test.arco`, written by `tools/test_city.py`: one world with a city for each kind of way (`roads`,
`railways`, `highways` with their ramps and interchanges, `power`), one for where two kinds meet
(`crossings`), and one for `heights`, each kind over hills, valleys, terraces and cliffs.  The cities lie
side by side on the one map, packed by what each holds.  Every block is one case, drawn as text with its
ground as a second picture, and fitted as the game's own tools fit it.  The road and highway curves are
there with the land beside them open and hemmed in by buildings.  `python3 tools/test_city.py --list` prints
where each block is in the world.  A case is added there, in the block's own picture.  A block may also
name runs of street with a class and a direction, written into the world's street layer, so one-way
streets, avenues and boulevards are cases like any other.
