# Arcology — what is open

Everything listed here is open; finished work is not repeated. This is
the starting point for the next run of work.

## The five goals

All five of `CLAUDE.md`'s goals are met, each with a check of its own in
`ctest` and each proved output-identical against the reference battery.

| goal | how it is met | the check |
|---|---|---|
| 1 Lua reads the simulation | `arc.city.plane/at/near/tile/layers`: 18 layers, any cell, four neighbours, the whole map in 3 ms | `map_read`, against a second decode of the city file in Python |
| 2 the fit is a service | `arc.fit(points, radii, budgets) -> pieces`, at any moment | `fit_service`, seven paths invented in Python and checked geometrically |
| 3 the loft is a service | `w:loft(pieces, profile, how)`: a cross-section written as numbers, swept along any path | `loft_service`, the face count worked out independently and the mesh read back |
| 4 the network is discovered in Lua | `scripts/compose/network.lua` produces every run, every junction and what a node is; `scripts/compose/bands.lua` does the same for the highway's decks; `net/network.c` keeps them, and `walk/` no longer sweeps the map at all | `network_discovery`, and taking the two rules away leaves 83712 triangles of 1068927 |
| 5 the interpretations are swappable | the sweep, an on-ramp's climb, an intersection's pattern and what a road carries, each a different algorithm in a Lua file | `swappable`, four distinct meshes from four scripts |

What is left of them is reach rather than principle.  Three sweeps in
`net/hiway.c` still enumerate the on-ramp cells, and the byte that makes
one is already the script's (`arc.highways`), so they find cells rather
than deciding anything; and `net/hiway.c` still wants cutting in two,
the deck's geometry from the ramps'.

## The highway's interchanges

Which deck lane goes on to which is `scripts/compose/links.lua`'s, and
nothing else's.  It tells four situations apart:

* the inner lane with a road ahead -- the deck has come down to grade,
  and the lane goes into the road lane facing it;
* the inner lane with no road -- the deck goes on as ANOTHER BAND round
  the interchange, so the lane looks for that band's lane;
* a middle or outer lane with a band ahead -- the same continuation at
  its own offset, which is what carries a three-lane carriageway round a
  loop without dropping a lane;
* a middle or outer lane with nothing ahead -- it tapers into the inner
  lane of its own band.

`ctest -R interchange` proves it: clearing `arc.rules.links` leaves every
band end unjoined, and with it every lane end goes somewhere.  The
supports under the loops go the same way -- a family declaration naming
its own `works` rule takes the piers off without a compile.

The RAMPS go the same way.  Which lane each of a ramp's two ends fastens
to -- a station of the deck's own band at the top, the kerb-side lane of
the road at the foot, or the nearest turn inside a junction's box -- is
`scripts/compose/snap.lua`'s.  The lanes within reach are measured for
it, one candidate a PIECE, because a lane's nearest station may run the
wrong way where one further along runs the right way; finding the
nearest point on an arc is geometry and stays in C.  Clear
`arc.rules.ramp_lane` and 102 of Atlanta's 108 ramp ends fasten to
nothing.

## Where C enters Lua

**One place, and it is the drive.**  `src/render/net/drive.c` holds the
only `script_rule_object` in `src/render`: `arc.rules.frame`, entered
once a frame, with the build under `arc.rules.world` and the world that
moves under `arc.rules.moving` hanging off it.  `tools/call_up.py` is the
ratchet and its ceiling is 1.

Every path the renderer draws is cut by `arc.rules.pieces`, and every one
of them goes through the CUT QUEUE in `geo/fit.c`: a pass that wants a
path cut puts the chain in the queue, the drive comes round and hands
each queued chain to the rule, and the pass reads the pieces back.  What
queues:

| the path | queued by | taken by |
|---|---|---|
| a segment's corridor fit | `walk/walk.c` `seg_fit` | the same |
| a junction's connectors | `lane_junction_ask` | `w:junction_lanes` |
| a rail junction's tracks | `rail_box_ask` (the box's asking half) | `rail_box` |
| a segment's dead-end caps | `lane_segment` | `w:segment_done` |
| the links across a crossing | `xlane_link` | `w:lane_cross_done` |
| a highway band's spine | `hiway.c` `hw_chain` | `w:hiway_band_cut` |
| a ramp's descent | `ramp_route` | `w:ramp_joined` |
| a ramp's slide | `hiway_slide_chain` | `sl:routed` |
| the two legs a join falls back on | `ramp_legs_queue` | `w:ramp_done` |

`arc.fit` cuts through the same rule from `src/script/api_fit.c`, which
is a SCRIPT asking, not the renderer: the question travels back out to
the script that started it.

A stage that wants a path of its own declares an ASKING HALF the same way
`box_done` declares a second half -- `net_hook_add_asking` -- so the
family declaration names one primitive and the pipeline finds both.

## The 2x2 interchange

Four tiles of one id (`xbld` 0x69) where up to four highway bands meet.
It draws as MESH now, and the pieces are all the script's:

* `scripts/compose/links.lua` gathers the deck lane ends standing on a
  block AFTER the ordinary matcher has run, and joins what it left open:
  each leaving end takes one arriving end of ANOTHER BAND, straightest
  first, so the through movement is kept and the leftovers turn.  That is
  the switch from one highway to another.  Atlanta gains six.
* `scripts/compose/interchange.lua` sweeps the deck they cross on: one
  pad over the block, swept ALONG the arms it serves, since the deck
  material paints the median and the lane lines across the sweep.
* `scripts/ground_tiles.lua`'s `meshed_tiles` is what stops the original's
  cloverleaf sprite being painted over all of it.

**The sprite is the gate, and it was a C range.**  `K_ROAD_ART` in
`gpu/gpu.c` is not a label, it is the suppression list: with the mesh on,
a sprite in it is dropped because geometry stands in for it.  It was
`0x0E..0x50` and `0x5D..0x68` in the source -- one byte short of the
interchange -- so a mesh drawn there was invisible under the art whatever
a script did.  It reads `script_bytes("meshed_tiles")` now, so a piece the
mesh learns to draw is turned on in Lua.

**And the battery could not see any of it.**  `fp.sh`'s six views never
framed an interchange, so every claim about one rested on a screenshot
somebody chose.  Views seven and eight frame Atlanta's three blocks and
Tokyo's lone one.

Still open: the pad is a slab with no filleted turn geometry; two turns a
block, and "straightest first" is a weak rule for deciding which lanes
turn; a four-arm block wants more than two.

## What COLLIDES

`mesh_check_collide` (`mesh/check.c`, driven from `--mesh-check`): two
faces of DIFFERENT shapes that pass through one another -- one surface
driven through another, which no painter's order can separate.  A pair
collides when an edge of one crosses the other's plane strictly between
its own ends and lands strictly inside it, so touching along an edge,
meeting at a corner and lying coplanar all pass.

The corpus is not at zero and these are not new:

| city | pairs | tiles |
|---|---|---|
| atlanta | 738 | 81 |
| toronto | 907 | 93 |
| tokyo | 346 | 38 |
| flint | 2370 | 359 |
| babar | 279 | 37 |
| maltron | 2261 | 271 |
| chicago | 1001 | 162 |

What it names: an off ramp lane through a road junction, a highway deck
through a road strip, a power line through the ground, a footway through
the road strip beside it.

## The highway interchange, and what is still the sprite's

Goal-C work, and the gap is measured rather than guessed:

- the 2x2 INTERCHANGE block (`xbld` 0x69) draws **no mesh at all**.  A
  tile dump of one of Atlanta's twelve reports two triangles, both
  ground; the cloverleaf a player sees there is the original's sprite
  standing on it.  Its loops are not lofted, so they carry no deck, no
  parapet and no piers.
- some CURVE blocks (0x65..0x68) draw nothing either -- one of Atlanta's
  0x65 blocks is bare where a 0x66 block two bands away carries 29
  faces.
- a band end that finds neither a road nor another band's lane is left
  OPEN: 8 of Atlanta's 16, 20 of Tokyo's 68.

The lane links across an interchange ARE laid (`links.lua`), so the
traffic crosses; what is missing is the slab under them.  `w:loft(pieces,
profile, how)` is the service that would draw it, and the pieces are
already in the rule's hand -- `join()` fits them.  What the rule still
lacks is the deck's HEIGHT at a lane end: the lane model is flat, so a
loop lofted from it would have to be draped or pinned.

## Where the proofs stand

All four ran in this location and passed:

- `sh tools/fp.sh` -- the six views, `12f28cc62f e664211a6f e43c1d3e03
  582be65e62 c8ff0ea62c b303e7b5b4` at 1280x800, and all seven cities at
  their reference triangle and shape counts.
- `ctest` -- every test but the two that need the 1995 game folder
  (`no_broken_segments`, `blit_matches_game`); both fail only on
  `PermissionError` reaching into `~/Downloads`, not on anything in the
  code.  Point `ARC_CITIES` at a readable copy of the game folder and
  they register and run.
- `tools/incr_check.py` (ctest `incremental_rebuild`) -- five edits on Atlanta, incremental against full:
  **SAME** on all five, including the ramp at 72,88, the deck over a road
  at 105,84 and the junction at 90,81.
- `tools/sweep3.sh` -- 102 cities against a build of the tree as it stood
  before the comment sweep and the inspector rewrite: **0 differing**.
  So neither changed the mesh.
- `arcology <city> --check` -- software against GPU: **1024000 of
  1024000 pixels identical**, both CRCs `0xf1030905`.

Atlanta itself reports 549757 triangles, 0 free edges, 0 free vertical
spans, 0 samples under the terrain, 0 shared corners disagreeing, 0 hard
corners in road, rail or highway, and 0 lane ends with nowhere to go.

What is still unproven is the inspector, because it can only be judged by
pointing at things.

## The mesh inspector

Built but **never seen running** — all of this needs a look with the app
up, `--inspect` or ⌘I:

- the outline is a welded 3D silhouette now (`mesh_comp_edges`), so a
  vertical face and a component standing over itself should both outline
  correctly, and a T left by the tile clip should close
- the outline carries its own height, so a deck's outline should sit on
  the deck and not on the ground beneath it
- the pick walks the projection ray instead of a fixed patch of tiles,
  so an elevated deck and its ramps should be reachable and should not
  lose to the road underneath
- one ramp should highlight per hover, not three
- furniture — poles, signals — should be pointable
- the `generated by` row should name the function that built the
  component, not the emitter

## Network geometry

- **Broken segments to zero.** Hard corners: 41 road across 29 cities,
  2 rail, 0 highway. Segments without geometry: 5 highway curve blocks.
  `tools/corner_check.py --update` moves the ratchet.
- **Head-on ramps**: 55 across 13 cities.
- **Lane ends with nowhere to go**: 0 on Atlanta, Tokyo, Flint and
  Toronto; 2 on Chicago.
- **The profile and the grade ceiling** — a design question, not a bug:
  what a corridor is allowed to do to the ground it crosses.
- **Roundabouts** where a junction suits one: a circulating lane with
  entries and exits.
- **On-ramp sprites** must take the right art in every rotation, in
  sprite mode.
- **Road and highway build tools** on the test city.

## Pipeline work in flight

- **Lanes as primitives.** The router between lane ends is in and the
  intersections use it. Still to come: ramps, rail, lane counts, and
  traffic last of all.
- **Sidewalks as a primitive**, routed arm to arm round a junction the
  way lanes are, never across a mouth.
- **Incremental rebuild** — the second half of the stage-three work. The
  computation is a pure pass in the grading walk now; the rebuild after
  an edit still redraws more than it must.
- **Static geometry across perspectives**: the mesh is built once and a
  turn is only the camera.
- **Road pipeline restructuring**: the loft description struct, one
  grading pass, geometry reading the lane model, the loft split by
  responsibility.

## Scale

The hundred-times-larger world. Chunked builds, rebuild-after-edit and
the station cache in the segment table are done. Next: highways through
the cache, and tighter band closure. Ground is never cached.

## Loose ends

- **Three-finger drag** on macOS does not pan the map. Needs a session
  with `--input-log` to see what the trackpad actually sends.
- **`printf`** is banned in `src/render` and `src/app` and enforced by
  `ctest -R no_printf`. `src/sim/dev.c` (82 calls) and
  `src/sim/testcity.c` (3) are outside that fence — decide whether the
  rule should cover them too.

## House rules

`CLAUDE.md` holds them. Two are enforced and run without a build:

    ctest -R no_printf        messages go through the log, dumps through dumpf
    ctest -R comment_style    no attribution, no history, never stacked
