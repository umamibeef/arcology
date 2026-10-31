# Arcology — what is open

Everything here is open.  Finished work is not repeated: the rules are in `CLAUDE.md`, the facts in
`docs/`, and the plans in progress in `plans/` (`plans/README.md`).  What proves a change today is
`CLAUDE.md`'s "The constraint on all of it".

## Editing a city: the player's tools

The action layer is `src/sim/sim_tool.c`: a gesture is begun, stepped from tile to tile and ended, and the app
drives it from the mouse (`src/app/uiglue.c`) or from `--gesture`.  Only the simulation has to match the
original, so the tools still missing are written as plain interface code, not reconstructed from the listing.
What they write must be what the simulation reads.  The bulldozer's Demolish and Raise and the network fitters
are reconstructions, and `tools/tool_check.py` holds them to the original.  The zones, the ports and De-zone
are plain code: a rectangle dragged and paid for whole, offered only inside the city's limits.

Still to write:

- **The bulldozer's** Level and Lower.
- **Buildings**: power plants, pumps, schools, parks and rewards, with the power and water reflood after them.
- **The line tools**: the drag and the placers for road, rail, power, pipe and subway, bridges, highway,
  tunnel, onramp, and the map-edge links.  Each road tool lays its own class into the street layer.
- **Landscape**: trees, water, the terrain editor, signs.
- **The interface**: the drag preview for lines, the tool cursors, and which sub-tools are offered.  The
  menus, the sub-tool popup and the tooltips draw in the Kaleidoscope theme; scrollbars and the other window
  controls do not yet.

What an edit shows (`src/render/show.c`, `src/render/compose.c`): the pieces near the edit sink and
dissolve while the world is rebuilt, and the pieces the build changed rise back, both as a wave out from the
edited tiles.  The look is the `edit_show_*` numbers (`src/render/geo.c`); it is unjudged in a window.  Open:

- The build itself (about 280 ms) still freezes input and the clock; only the pieces move.  The going wave's tail
  after the build, and the coming wave, run in ordinary frames.  `--show-edits --show-at S --shot` draws the
  pieces as they stand S seconds into the going wave, which is how the look is checked without a window.
- A road an edit lays has no street-layer edges, so it carries class 0, two-way.  The import fills the layer
  once, and the street tools that are to set it are open (above).

## Performance

- The live window builds on a thread of its own (`src/app/world.c`, `remesh_begin`), so frames, the
  simulation and the edit show go on during a build.
- The simulation is polled once a frame (`step_clock`): at most one phase a frame at speeds 2 to 4,
  and speed 5 spends 4 ms of every frame.  It has no file-scope state and could run on a thread.
- `static_analysis` wants the build configured with `-DCMAKE_EXPORT_COMPILE_COMMANDS=ON`.

## The network

`plans/network.md` is the design, and `plans/network-status.md` where the build stands and what is left.
No ctest builds a city: `net_check` holds the lane check on the records alone.  The mesh draws streets and
railways (`src/render/draw/roads.c`, `rails.c`).  A highway, a bridge and a meet where one family turns have no
drawing, and the geometry view hides their art, so each is a gap.  Nothing moves on any road.  Open on it, besides that list:

- **The street import's thresholds are first values.**  `STREET_POP_AVENUE`, `STREET_POP_BOULEVARD` and
  the two traffic thresholds (`src/network/street.c`) were set by hand, not measured.
- **Two streets side by side (a proposal, not settled).**  The original's fitter joins neighbouring road
  cells sideways, so two parallel streets and a grid of junctions are the same cells, and the topology makes
  touching node cells one junction.  Keeping two streets apart needs the player's own data: a bit per edge
  of the street layer saying the two cells do not join, set by a road tool that lays a street beside
  another.  An import keeps the original's joins.
- The interchange's movements are connecting roads of one lane each, on two levels and a third for a left turn.
  Which movements a node carries is to be judged on the diagram (`--net-svg`).
- A junction's signals and stop signs are filed on the network (`network_controls`), and nothing reads them.
- Open questions on lanes: the turning radius a single lane may take, against its road's, and how a car
  picks at a diverge once there is a trip model (a fair random pick until then).
- **A railway junction's gravel leaves free edges** where it meets the railways it joins (`--mesh-check`):
  15 on Atlanta at five junctions, and 3 more at 63,50 and on `test.arco` at 152,3.  The gravel's foot lies
  at the outer border of a railway's `border` lanes (layer 2), and the three more appear with it there
  rather than 0.01 tiles nearer the line.
- **Most goals have no witness.**  `net_check` holds "The network is derived from the cells, in C", and the
  oracle checks hold the simulation under "A superset of SimCity 2000".  "City limits, not a square",
  "Independent cities that meet", "Road networks drawn better than their cells" and "A much larger region"
  have no check that fails when they break.

## The geometry libraries

Clipper2 (polygon union, difference, intersection and offsetting) and earcut (triangulation) are vendored
(`src/vendor`) and reached through `src/render/mesh/poly.cpp`.  Today they triangulate a junction's fill
and join a railway junction's gravel.  Hand-written geometry that they answer better, each a candidate to
replace:

- **A junction's sidewalk ring** (`fill_rings`, `roads.c`) is the boundary inset by the sidewalk's width,
  worked out by winding and a mitre at each reflex corner.  Clipper2's offset with a negative delta is that
  inset, joins included.
- **A street giving way to a railway** (`gives_way`, `foot_hit`, `roads.c`) casts a ray from each point of
  the street's end onto the gravel's foot.  The street's top less the gravel's footprint is one polygon
  difference, whatever the railway's curve.
- **The tiles a surface covers** (`cover_quad`, `roads.c`) walks each strip in steps.  Clipping each
  strip's polygon to the tile grid answers it exactly.
- **A railway junction's gravel seams** (`weld`, `rails.c`) put one ring's points into another's edges by
  hand.  One union of every piece of gravel meeting there leaves no seam to weld.
- **A polygon that crosses itself** (`poly_simple`, `mesh/poly.c`, read by `fill_fault`) is tested pair by
  pair.  A union of the polygon with itself that changes it says the same.
- **The corridor a road is fitted in** (`outside`, `corner_out`, `path_clear`, `shape.c`) tests a point
  against each unit's square in turn.  The corridor as one union, tested once, says the same.  Layer 3
  is in `src/network`, under `src/render`, so it can reach `poly.cpp` only once the polygon calls move
  to a level both may include.
- **Overlap in the mesh check** (`mesh_check_overlap`, `check.c`) measures shared area pair by pair.  An
  intersection of the two faces' polygons is the shared area.

## The mesh inspector

Built and unproven: it can only be judged by pointing at things with the app up (`--inspect` or ⇧⌘I).  The
inspector is `src/app/inspect.c` (the pick, the components to outline, the remark, the keys and clicks) and
`src/ui/inspect.cpp` (its window and the outlines), with `src/app/net_pick.c` for the records; the UI reads
it from `RUiState.insp`.  The `part of` row names the shape that holds the component.

Over the road network it shows the RECORD the mesh was drawn for (`src/app/net_pick.c`).  Every shape the
drawing lays names its record (`shape_ref`): a road, a junction, or, for a strip of a deck's top, its lane
(`NREF_LANE`, under the road's shape).  The pick is the triangle under the pointer, read through its shape.  The
road under the pointer is outlined as its components, each the triangles of one shape group: every lane, the
structure drawn beside them (sides, columns, caps), and the junction at each end; in a junction, the junction and
its connecting roads the same way.  Each is outlined whole from a mask in window points, in the color of its lane's
type, steel blue for structure and orange for a junction.  The record the window describes, the lane under the
pointer until Shift-Enter steps to its section, road or junction, is bright.  A click selects the object, and
Escape or a click on nothing clears it.  A second click in the same place selects the next road under the point,
Enter selects the child and Shift-Enter the parent, as a design tool's layers do, and a click on a level in the
window shows it.  A remark typed in the window goes to `debug/remarks.md` and the clipboard, with the records and
the camera.  `--pick X,Y --inspect` dumps the same rows.  Open: a street is drawn as five strips, not a strip a
lane (`roads.c`, `street_stretch`), so its lanes are not shapes of their own and the street is one component.

## Placing a city in any orientation

A city can be placed in the world in any of four orientations.  Its placement is a map from the city's
cells onto the map's cells, chosen when it is placed.  Nothing turns: the simulation, the city's data and
the network have no orientation, and only the rendering turns (`CLAUDE.md`, "Only the rendering turns").
`world_add_city` (`src/sim/world.c`) places a city as it is.  Open:

- **The orientation is an argument** of `world_add_city` and of `--compose`.
- **The bytes that name a direction** (a piece's id, a ramp's `XBIT` bit `0x02`, a zone's corner bit) are
  written for the map's cells as the city is placed, through the original's own tables.
- **The street layer's edges go with their cells.**  An edge is stored on the cell with the lower column or
  row, so a placement files each edge on whichever of its two cells then has it.  A one-way still runs
  from the same cell to the same cell.
- **A save's `rotation` is decoded once, on import.**  `misc[2]` is how a `.sc2` encodes the zone corner
  bits.  `city_load` rewrites them into the grid's own terms.  A `.sc2` is only ever loaded, never saved.
  The simulation and the renderer read no rotation.  The oracle checks
  give the original the same decoded city, and agree on every layer.  Against the shipped saves, `--verify`
  agrees less: a city saved at a turn other than 0 was scanned by the original at that turn, and some of
  its reads test a fixed corner bit (the station coverage stamp, ``$23DEE``).

## A much larger region

The plan and where it stands are `plans/limits.md`.  A window build and a world built block by block are
each meant to lay what a whole build lays.  No check holds it (`CLAUDE.md`, invariant 2).  Open:

- **The city's limits.**  The clock keeps to them with `--limits-glow N` (`plans/limits.md`).  Pollution and
  the service coverage spread past them.  Zoning is offered only inside them once the city holds anything.
  An empty city zones anywhere, and each zoning grows them.  Open: a thing arriving at the limits' edge
  rather than the map's, and the renderer reading them.

## Loose ends

- **A saved world forgets its date, funds and population.**  The `.arco` writer folds the census, the
  budget, the year-end values, transit and demand back into `MISC` (`city_scalars_to_misc`), but not the
  date, funds, population, bonds or the other named scalars.  A world saved after play reloads with the
  values it was loaded with.  The fix is the MISC builder (``$2A186``) ported whole, with an audit of which
  copy of each value is the live one.
- **The checkers need fixing.**  An audit (static, nothing run) found:
  - stale docstrings: `tools/frame.py` and `tools/highway_map.py`;
  - `blit_check.py` passes a failed emulator run as a skip;
- **The house rules could be shorter.**  Much of `CLAUDE.md` restates rules a checker enforces; once the
  checkers are trusted, those rules can shrink to a pointer each.

- **The city's name carries its Pascal length byte.**  CNAM is copied raw into the view's `name`, so the
  title shows a control character before the name (`adapt.c`, `uiglue.c`, `soft/main.c`).
- **Three-finger drag** on macOS does not pan the map.  Needs a session with `--input-log` to see what the
  trackpad sends.
- **`src/dev/dev.c`** carries the developer modes' `printf` calls outside the ban's fence.  The modes belong
  outside the reconstruction, with their own output rules.
- **The docs' preview pages** need the game folder to render their pictures (`tools/gen_previews.py`); a
  build without them stands a note in for each picture.
