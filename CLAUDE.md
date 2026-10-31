# Arcology — house rules

THE GOALS and THE ARCHITECTURE come first: they are why the rest exists.  Then how a change is
proved, where the code lives, and the rules a change is held to.  THE DOCUMENTS says how the pages are
written, and THE SIMULATION is the reconstruction of the original game from its binary, with its own goal
and rules.  The work in progress is `plans/`.

# THE GOALS

The user sets these; do not add, reorder or reinterpret them.

**A superset of SimCity 2000.**  Arcology is meant to be a superset of the original SC2K spec, enhancing on it in
a huge number of ways.  So a whole city made in SC2K imports into an Arcology region.  A `.sc2` is loaded, never
saved: the world is saved as `.arco`.

**City limits, not a square.**  The original simulation is adapted to run inside a city's LIMITS instead of the
original's square map.  A city's limits follow from its road network, its zoning and its buildings, with a glow
round them: every tile within a fixed distance of them, so the limits grow as the city does.  The simulation reads
and writes only inside them, and what lies outside answers as the original's map edge does.

**Independent cities that meet.**  Cities are independent, and they interact with each other.  Where their limits
come to touch, they can, if the player wants, grow into each other.

**The network is derived from the cells, in C.**  Which cells form a road, which lanes it carries and how it
joins the others follow from the city's data -- the simulation's tiles, and the player's own street data (each
street's class and direction, per tile edge, in the `.arco`, inferred once on import) -- and are derived in C
and kept by `src/network/`.  MET for the records.  `src/network/topology.c` and `network.c` derive them.
`ctest -R net_check` (`tools/net_check.py`) holds every lane of
every shipped city to the check that it goes on and is fed, on the network's records.
No road is drawn until the records are verified, and `--net-svg` draws the lanes as a diagram.  *Met when the network is built
in C from cell data alone, and a check that every lane goes on and is fed
holds on the filed store.*

**Road networks drawn better than their cells.**  The cells say what connects to what, and the renderer chooses
how to draw it, using the space it has to draw the original more elegantly.  A staircase of road cells becomes
a clean diagonal instead of harsh steps, a highway bends where the original could only turn, and a ramp takes
the best path to the highway.  What stands near a road gives way: a building beside a highway is drawn smaller
or shifted within its own cells, so it never sits in the best path for a ramp.  All of it is worked out
procedurally from how everything is laid out in the cells, and none of it changes the simulation's data.

**A much larger region.**  Arcology supports a MUCH larger region than the original's 128 by 128 map.

* A new world is a REGION: a fixed area, very large, shown as a map the player picks a site from.  Its size is
  a parameter, and it can be made larger later without any land already made moving.
* The world is stored and streamed in CHUNKS of 32 tiles a side (`MESH_CHUNK`).  A multiple of 8 keeps the
  original's half- and quarter-resolution layers and its growth slices whole.  A chunk with development or
  water in motion is active, and the rest hold only their land and are never ticked.  The renderer builds the
  world a window of chunks at a time, and draws the chunks in view.
* A 1995 save is a 128 by 128 patch.  Importing it writes the patch into whatever chunks it spans and stitches
  its edge to the land round it.  Nothing else about a city is 128: only the faithful mode runs the original's
  loops over one 128 window, so every check against the original runs on the same code.
* Region land is generated from a seed and a few sliders (sea level, mountains, rivers), from a continuous
  field so chunk borders do not show.  `plans/terrain.md` is the generator, and the original's own generator
  is ported beside it as a classic choice.
* The land's heights are not capped at the 32 levels a 1995 save holds.

## THE ARCHITECTURE

Four invariants.  Each has a WITNESS, a check that fails when it is broken, and a change that cannot keep
its witness green is not taken.  `ctest -R architecture` (`tools/architecture.py`, lint tier, no build
needed) holds the mechanical ones.  Invariant 2's witness is `tools/window_witness.py`, run on demand.  No
ctest builds a mesh.  A session that finds itself arguing with one of these has strayed: stop, and say so.

**1.  One build path.**  A whole build, a block of the world and an edit are the SAME path: a window of
chunks laid into the mesh standing, and a whole build is the window that is every chunk.  No second
mechanism keeps, diffs, matches or replays pieces beside it.  *Witness:* `architecture.py` counts the call
sites of the per-piece edit path by file, and the count may only fall.  It is zero when the invariant holds.

**2.  A store is keyed by place.**  Everything a chunk holds -- its triangles, its ways, lanes and links,
its shapes, its tile records, its room on the device -- is reached by WHERE it is, never by its place in a
list, and a window build REPLACES its chunks' stores.  It never merges into a global list, never resets a
global list, and never leaves a global list to grow.  *Witness:* a chunk built alone, a
city built window by window, and an edit each lay what a whole build lays, triangle for triangle in draw order,
way for way, link for link.  `tools/window_witness.py` holds a window built alone to it, face for face.  A store that cannot pass that when built in any order is not keyed by place.

**3.  A fault is never accepted.**  A proof that runs the renderer runs it through `tools/arcrun.py`, which
raises on a crash, an abandoned build or an incomplete one.  Two faults can never compare "the same".
*Witness:* `architecture.py` fails on a proof tool that runs the binary through `subprocess` itself.

**4.  The network is the source of every roadway.**  Everything travelled -- a road, a ramp, a track,
a sidewalk, a crosswalk -- is a record of ONE road network, in ASAM OpenDRIVE's structure: roads with lane
sections and lanes, junctions with connections and lane links, `default` junctions through connecting
roads and `direct` junctions where a road divides or two join.  It is derived from the cells -- the
simulation's tiles and the player's street layer -- straight after the topology.  It is filed before any
shape is given to it, and it can be checked and seen in that state.  Everything in it is placed by its road
and by `s` along the road and `t` across it, and by nothing else.  Nothing that shapes or draws the network
adds, removes or relinks anything in it.  Every surface, deck, column, marking, mover, check and
outline is read from what was filed, never from a plan made to file it.  The LANE GRAPH is the lanes and their
links: a lane runs from one section boundary to the next, a merge or a diverge is a node of it (a section
boundary or a `direct` junction), and no lane joins or parts from another partway along.  No link is found by
matching where two lanes touch.  A reader that needs what the store does not answer is a finding about the
store, never a reason to read a plan.  `plans/network.md` is the design.  *Witness:* `architecture.py` counts, by
file, the sites of a link partway along a lane and the sites of a link found by place.  Each count may only
fall, and the invariant holds when both are zero.  No roadway is drawn from records the check does not hold.

### Standing rules

These rules are not invariants.  No witness holds them.

**Only the rendering turns.**  There is no rotation in the simulation, in the city's data or in the network.
A turn of the view is perspective: the renderer draws the same cells from another side, and picks each tile's
art for that side through the original's own tables.  Placing a city in the world maps its cells onto the
map's cells, and nothing is turned (`TODO.md`, "Placing a city in any orientation").

**A measure is a witness, never a goal.**  Every measure in this repository is a witness that a goal is met.  A
change that improves a measure and nothing a person can see is not progress.

**Code is C.**  There are no scripts.  Every number the world is built from, and every knob the tuning window
moves, is set in C (`geo_defaults`, `src/render/geo.c`).  The build order, the ground, the zone tints and the
power line are C (`src/render/compose.c`).

What this forbids, so it is said once: a switch that keeps an old path alive "for comparison"; a helper
borrowed from the old path because it is to hand; a bound hard-coded where a knob should hold it; a
tool that compares two outputs and does not first ask whether either was built.

**Before any change to the network, or to a page or comment about it**, say which layer of `plans/network.md` it
belongs to and which OpenDRIVE element it files or reads.  If the answer is "none", or "a layer
below reads it from the plan", the change is wrong.  Four mistakes keep recurring, and each is forbidden:

* **Writing what the code does as the design.**  The design is `plans/network.md`'s layers.  Where the build
  differs, the difference goes on `plans/network-status.md` as a breach, never into the design as a fact.
* **Coining a term the standard already has.**  A road, a lane section, a lane, a junction, a connecting
  road, a contact point, `s` and `t` are OpenDRIVE's, and a new word for any of them is a second model in
  waiting.
* **Giving the network a frame.**  The network and the street layer are a graph of cells, indexed by column
  and row.  A graph has no orientation, so nothing in them is named by a compass direction and nothing in
  them is turned.  Orientation exists only where cells are MAPPED: onto the map as a city is placed, or onto
  the screen as a view draws.  The original's bytes that name a direction are its own encoding, read through
  its tables.  Before writing about a turn, a placement or a direction, ask what maps onto what.  If the
  answer turns data, the change is wrong.  `architecture.py` counts the compass words and rotation reads in
  the network's code and plans (`FRAME`) and the calls of the original's map turn (`TURNS`), and each count
  may only fall.
* **Repairing a symptom outside the network.**  A gap seen in a surface, a car or a check is a fault in the
  network's records or in a reader of them.  It is fixed there, never with a shape, a link or a rule beside
  the network.

`tools/architecture.py` runs after every edit (`.claude/settings.json`) and refuses one that raises a count.

## The constraint on all of it

Every stage is proved before it is believed.  A geometry change that cannot show what it moved, and that
nothing else moved, has not been verified, however sound it reads.

**There is no reference frame.**  The render is built ahead of its design (`plans/network-status.md`), so no
frame of it can become a reference.  A change is proved by a PROPERTY measured against a whole
build of the same city: the mesh check's own counts (`--mesh-check`: free edges, collisions), the dump of
the build (`--tile-dump all`) compared before and after, and the lane check on the network's records
(`--net-check`).  A proof that runs the binary goes through `tools/arcrun.py`.

**A dump compared before and after proves one thing only: that a change meant to move nothing moved
nothing.**  A trim, a rename or a move is proved that way.  Anything that builds something, a port among
them, is proved by the DESIGN's properties checked against the cells (`plans/network.md`), never by agreeing
with the code it replaces.  No code in this tree is a reference: matching code copies its faults
and makes the code the design.

A world that was not built -- a crash, a rule that runs away, a build abandoned -- is a FAULT, and a fault is
never accepted.  A frame is for the user to judge (`tools/frame.py` frames a place), never a measure.

`ctest` runs in tiers, and the tier is the whole choice a session makes:

    ctest --test-dir build -L lint       # no build needed, a second: the style and layer checkers
    ctest --test-dir build -L gate       # the lint tier, the atlas load, the docs build: no city
    ctest --test-dir build -L full       # everything the repository alone can run
    ctest --test-dir build -L oracle     # what needs the game's own files

The simulation's three checks (`verify`, `microsim`, `allocmicro`) are behind `-DARC_SIM_TESTS=ON`: the
simulation is verified against the original and the renderer cannot move a simulation layer.

## Where the code lives

Every directory under `src/` is a level, and the includes hold the order: `tools/layering.py` (`ctest -R layering`)
fails on any include that runs up a level and is not on its list of exceptions, each with its reason.  From the
bottom: `util`, then `sim`, then `cells` (the city's cells as the network and the renderer read them), then
`network`, then `render`, then `dev`, then `app`.  `ui` and `audio` sit beside them and read only `util`.  Inside
`render` the top runs a build, `draw/` sits under it, and `mesh/` is under both.  A directory's own `internal.h` is
what its own files share, and no file outside it includes that header.

`tools/code_smells.py` (`ctest -R code_smells`, lint tier) reads the call graph and fails on a call up a level, a
copied function body, one enum's names written in two tables, a function nothing reaches, an export only its own file
calls, files that call round a loop, and a function that lives away from the one file that calls it.  Each place a rule
allows is on its list of exceptions with the reason, and the list can only shrink.

**`network/` and `draw/` cannot see the ART.**  Not a sprite, not a palette index, not an atlas level.  The checker
holds that line too.

The arithmetic is compiled with `-ffp-contract=off`.  With contraction on, a function inlined into its caller rounds
differently from the same function called across a translation unit, so moving a function between files moved the
mesh; off, a move is bit-identical and a whole build can prove it.

### `src/cells/` — the city's cells

`city` is the read-only view of the cells, `RCity`, with the map's size, its buffers and the chunk size.  `adapt`
fills the view from the simulation's `City`, and it is the one place both sides meet (`tools/seam_check.py`).
`terrain` is what a tile's terrain byte means to the land.

### `src/network/` — the road network

`topology.c` (layer 1) and `network.c` (layer 2, with the lane check) derive the road network from the cells,
and `street.c` imports the street layer.  `shape.c` (layer 3) gives each road its line and its height, and
`bank.c` works out the ground drawn beside each street.  `store.c` keeps what a build derived by place: each
chunk points at the network of the build that laid it, and a reader of a place loads that one.  Each layer
names its variables in one list for it (`state.h`).  `src/render/draw/net_graph.c` lays its lanes out as a diagram,
which `src/dev/net_svg.c` writes to a file (`--net-svg`) and `src/render/draw/net_overlay.c` lays over the ground in
the outline view.

### `src/render/` — one build, and the city it is built from

* `build` — one build of the world, and the one door a build comes through.  `build_world_next` empties the stores
  and hands the world over; `build_world_field` builds the field once the tops are settled; `build_world_done`
  finishes the mesh when the turn ends.
* `compose` — the composition of one build: the tops and the field, the ground, the zone tints, the power lines,
  and the drawing.
* `incr` — what a build has to redo after an edit.
* `show` — what an edit's build shows going and coming.
* `geo` — every number the world is built from, and the knobs the tuning window moves, set by `geo_defaults`.
* `material` — the materials named at run time.
* `prof` — where a build's time goes, stage by stage.
* `turn` — the view from another side: each cell of the one grid read from that side, and each id that encodes a
  direction passed through the original's tables.  `project.h` is the one projection.

### `src/render/draw/` — the drawing

The shaped network laid as surfaces (`plans/drawing.md`): `roads` the streets, their junctions, crossings and
dead ends; `rails` the railways, turnouts and buffer stops; `bands` the highways' decks, columns and caps.
`rail_shape` is the railway's footprint (its way, its foot, a junction's gravel) that the tracks and the streets both
read, so a level crossing has one owner, and `laid` is the record of what each drawing laid.
`net_graph` lays the lane graph out, and `net_overlay` draws it over the bare ground in the outline view.  It reads
the network's records and lays them through `mesh/`.  Every shape it opens names the record it draws (`shape_ref`),
and a strip of a lane names its lane.

### `src/render/mesh/` — EVERY SHAPE PRIMITIVE, and nothing else

A file here names no road, ramp, deck, track, junction or footway.  `emit` puts triangles, walls, tops and boxes,
`shapes` puts line quads and fans over them, `slab` lays a closed slab, and `shape` is the table of what each shape is
and which triangles are its own.  `mesh` builds the mesh and answers the query tool, and `check` is the
watertightness and the lines against the ground.  `poly` is a polygon's area and simplicity, its triangles and its
union.  `surface` samples the drawn ground, `tile` is what a tile is and the ground field (`mesh/tile.h` is what the build
composes the ground from), `ground_bytes` what a
tile's building byte means to the ground, `ground` draws the ground tile by tile, and `model` holds the models, the pylon among
them, and draws them.  `list` keeps each chunk's triangle lists, and `lod` simplifies a piece of mesh into clusters
for drawing at a distance.

## The vocabulary

The network's words are ASAM OpenDRIVE's and the original game's own: a road, a ramp, a highway, a crossing, a
bridge, a crosswalk.  Name a thing by what it is.  The words below are the ones this code adds,  Each is defined by
WHAT IT IS: a row that needs another row to make sense is not a definition, so name the geometry, the cells or the
travel the word stands for.

| word | what it is |
|---|---|
| a **line**, a **strip** | the geometry one family lays along a run of cells: one cell wide, following the ground it crosses |
| a **lane** | an OpenDRIVE lane: one lane of one lane section of a road, with a signed id, a type and a width.  A mover drives along a lane |
| a **band** | a run of cells TWO wide, whose spine runs along the SEAM between the two rather than down the middle of either |
| the **street layer** | the player's data each street carries, per tile: its edges to the next column and the next row, each with a class (street, avenue, boulevard) and a direction (two-way, or one way either way).  Saved in the `.arco`, inferred once on import from the built population along the street and its traffic, and never changed by the simulation.  The road network reads it: a road's class and its one-way stretches come from it |
| the **road network** | the network's records, in ASAM OpenDRIVE's terms and structure: a **road** with its **lane sections** and **lanes**, a **junction** (`default` through **connecting roads**, `direct` where a road divides or two join) with its **connections** and **lane links**, a **contact point** at a road's start or end, and `s` along a road and `t` across it.  Everything is placed by its road, `s` and `t` |
| a **lane graph** node | where lanes end and begin: a road's end at a junction or a seam, a lane section's start, or a node inside a junction where an exit lane splits or lanes join an entry lane.  A MERGE is a node where two lanes link into one, a DIVERGE where one links into two.  A lane runs from one node to the next and nothing joins it between |
| an **arm** | one road reaching a junction.  Its **mouth** is where it crosses the junction's border |
| a **margin** | the `sidewalk` lane outermost on each side of a road, level with it.  A junction's corners and crosswalks are connecting roads of one `sidewalk` lane (`plans/network.md`) |

A tile family is a SLOT: the power line, the line and the thread.  `Family` is an int.

## What a byte means: the network's, in C

**What a byte of the city means to the NETWORK is C's.**  Which bytes carry a piece and of which family,
which edges a piece joins, a band tile's kind and axis, the two edges a ramp's piece joins, a level meet:
each is a fact the network is derived from (THE GOALS, "The network is derived from the cells"), so it is read in C, from the original's own data --
its tables and its code -- and never from a picture.  Layer 0 (`src/sim/net_cell.c`) reads it, and the
renderer reaches it through `rcity_parts`.  The network never reads the art.

## Output: never printf

`printf` is not used anywhere in `src/render`, `src/network`, `src/ui` or `src/app`.

* A **message** goes through the log ("The log", below).
* A **developer dump or a report line** goes through `dumpf` from `src/util/dump.h`. It writes to the file `--dump-to`
  names and to stdout when none is named, so the same line can be read by a person or captured by a script.

A bare `printf` obeys neither switch: it cannot be redirected, cannot be levelled, and is invisible to the tooling that
reads everything else. `ctest -R no_printf` enforces this (`tools/no_printf.sh`); it needs no build and runs in a
moment.

A log inside a per-frame path says its line **once, when what it is
saying changes** — never once a frame.

### The log

`src/util/log.h` is a C face over spdlog: `R_ERR/R_WARN/R_NOTE/R_DBG(source, ...)`.  A message carries a source
tag and a level, and it can be filtered.  Diagnostics go to
**stderr** and stay there: stdout carries measured output (`--run`, `--compare-renderers`, `--pick`) that people pipe.
A line is `<ISO 8601 timestamp> <level> <source> <message>`.  spdlog colours the **level** by severity; the
**source** gets a colour of its own from `source_flag` in `src/util/log.cpp`, because `"%^%-9n%$"` would wrap
it in the LEVEL's markers.  The source palette is blues, purples and magentas only, since spdlog paints the
severities cyan, green, yellow and red, and colours are handed out **in the order sources first appear**,
never hashed: a hash collides with a dozen sources whatever the palette.
Colour is decided once, by spdlog's own terminal detection plus the `NO_COLOR` family it does not read: a
refusal (`NO_COLOR` non-empty, `CLICOLOR=0`, `--no-colour`) beats a request (`FORCE_COLOR`, `CLICOLOR_FORCE`)
beats the guess.  `SPDLOG_LEVEL` sets levels globally or per source (`SPDLOG_LEVEL=gpu=debug`), which works
only because loggers are created with `spdlog::initialize_logger`.

## Comments: Simplified Technical English

A comment is read by someone whose first language may not be English, so it is written in ASD-STE100.  `ctest -R
ste_comments` (`tools/ste_lint.py`, no build needed) holds the mechanical rules over every C and H file under `src`,
less `src/vendor`.

| rule | what it means |
|---|---|
| `long` | a sentence over 25 words.  Split it |
| `semicolon` | write two sentences |
| `dash` | no em dash, and no `--` standing in for one |
| `short` | no contraction |
| `word` | the short common word: use, help, make sure, before, get, show, also |
| `spell` | American spelling |
| `header` | the first comment names the file and says what it holds |

Two tools go with it.  `tools/ste_fix.py` does the sweeps a machine can do and wraps a paragraph again.
`tools/ste_edit.py` applies one rewrite and wraps the paragraph it is in.  Neither touches `generated/`: a generated
file is rewritten by rewriting its generator and running it again.

## Comments: what the code does, once

**A comment is a statement of current fact.**  It says what the code does, and nothing about how it came to
do it.  `tools/comment_lint.py` enforces the rules below: a hook runs it on every file that is written, and
`ctest -R comment_lint` runs it over the tree (`python3 tools/comment_lint.py [path]`, `--stats` for a count
per rule).  It reads C, C++, GLSL, Lua, Python, the CMake files, the `.md` pages and the `.rst` under `docs/`,
and skips `src/vendor` and `docs/.venv`.  A comment that names a file, a function or a switch the tree does
not have breaks rule 1 as surely as a date does: check the name.

1. **Current fact, not history.**  A defect that has been repaired leaves no trace, and neither does the
   shape the code had before.  **Keep the reason, drop the chronology.**  The give-away words: *used to*,
   *had been*, *it once*, *before this*, *no longer*, *an earlier*, *reverted*, *refactored*, *the bug*, *the
   fix*.  The checker knows their innocent senses ("the earlier stages", "can no longer stand") and passes
   those.  The rule holds in `src/sim` as well: what the ORIGINAL 68k game does is its subject matter and is
   written in the present ("the original subtracts `a2*300`"), never as a history of the port.
2. **No conversation, ever.**  Not a request, not a complaint, not a screenshot, not a date on which
   something was asked for, and not an assistant.  Where a quote carried the only statement of a fact, write
   the fact in the code's own terms.  `the user` alone is the player; a colon, a comma or a verb of speech
   after it makes it a transcript.  The docs are held to this too: a page explains, it does not minute who
   asked for what and when.
3. **No data-centric examples.**  No one city, one tile or one measurement as the illustration: a per-city
   coordinate states a symptom where a rule belongs.  A constant the code uses stays, named as a constant; a
   corpus-wide count stays.
4. **No dates.**  A date inside the project's own working period is a work log.  A date from the game's own
   era, or a citation of something published earlier, keeps its date.  A date in a page's own eyebrow or
   rubric is the page's own, and stays.
5. **Never stack.**  Two multi-line block comments must never sit back to back with no code between them.
   That shape means a change wrote a new comment and left the old one standing: the two then disagree and the
   reader cannot tell which one the code obeys.  The rule reads every stacked pair, in the first column as
   well as indented; a comment drawn as a BOX, or opened by a heading with a rule beside it, is a section
   marker and is passed over.  When it fires, the second comment nearly always describes the code below and
   the first describes something that MOVED: find what it describes and put it there.
6. **No progress.**  A comment or a page states what is, never how far along the work is.  What is built,
   what is missing and what a check finds change with every commit, so a sentence that records them is
   wrong the day after.  Progress has one home an area, its status page (`plans/*-status.md`, and
   `docs/sc2k-report.rst` for the simulation), and open items have `TODO.md`.  Everywhere else, say the
   design or the standing fact, and point to the status page for how far the work has got.  Rule 1 bars
   the past and this rule bars the progress: no history, no progress.  The give-away words on a page:
   *yet*, *for now*, *at present*, *currently*, *still not*, *until it is built*, *today*.  In code,
   where *a chunk not yet built* is the running program's state and stays, the checker looks only for the
   work's own words: *not reconstructed yet*, *draws no ... yet*, *as of this writing*.

The checker joins each comment block before matching, so a citation that wraps across two lines is still
found.  A line carrying `comment-lint: allow` is passed over.  It exists for a scenario's own briefing quoted
verbatim, works per LINE, and **never keeps a comment you have not rewritten.**

So when you change code, **rewrite the comment beside it**.  Do not add a second one; do not explain what you changed.
If the old comment is still true, leave it alone; if it is not, replace it.

## Renaming: never by substring, never through a string

A rename across this tree is a scripted edit, and two shapes of it break things silently.

**A word boundary is not enough.**  Renaming the rule `walk` to `step` also hits `walk_curves` and `walks`, and the
footway disappears.  Match the whole identifier, and read the list of what will change before changing it.

**A rename must not enter a string literal.**  The names a reader looks a field up by are C strings: rewriting
`"lane_piece"` to `"api_lane_piece"` compiles cleanly, and the field simply reads as nothing from then on. Rename
declarations and call sites; leave every quoted string alone.

**And each step of a cascade is a fresh input.**  Rewriting `at(4)` to `at(3)`, then `at(3)` to `at(2)`, walks every
index down to nought. Compute the whole mapping first, then apply it once.

# THE DOCUMENTS

`docs/` is a Sphinx project and the `.rst` files there are the only source.  Nothing else builds a page.

| page | covers |
|---|---|
| `sc2k-report.rst`, `mechanics.rst`, `notebook.rst` | the disassembly: what reproduces, against what; the game's rules as the binary implements them; the findings as they were written, exempt from the current-fact rule |
| `rendering-pipeline.rst`, `renderer-design.rst` | how the ORIGINAL draws a frame, by address; how OURS is built: assets, zoom, build order |
| `renderer-previews.rst`, `-views`, `-terrain`, `-terrain-b` | what it draws; `tools/gen_previews.py` builds them |
| `enhanced-renderer.rst` | the 2.5D renderer: the facts, the keys, the one approximation, and where its code lives |
| `future.rst`, `conventions.rst` | long-range ideas (the work in progress is `plans/`, one page an area); the house style for these pages |
| `appendix-routines.rst`, `appendix-road-spec.rst` | generated: every address the pages cite.  Hand-written: the road system spec, a real-world reference |

## Writing them

Edit the `.rst` files directly.  **Read `docs/conventions.rst` first**: it says which box means what (`note`
an aside, `warning` something unresolved, `caution` a trap, `tip` a shortcut), when a `card` or a `grid` is
allowed, and that a `rubric` is the address strapline.  A box that opens with a bold lead-in keeps the lead-in
as its title (`.. admonition:: A calling-convention trap` with `:class: caution`).  Anything that calls itself
a trap is a `caution` whatever else it says.

    cd docs
    python3 -m venv .venv && .venv/bin/pip install -r requirements.txt
    make html                 # -> docs/_build/html/index.html

**The build must finish with no warnings.**  A Sphinx warning here almost always means malformed markup that
renders as visible garbage, so treat one as a failure.  To add a page: write `docs/<name>.rst`, give it a
`.. _<name>:` label and a title, and add it to a `toctree` in `index.rst`.  Addresses are inline literals --
``` ``$128DE`` ``` -- and `default_role` is `literal`, so single backticks do the same thing.  A bare `$128DE`
in running text is a mistake.  **Two rules reStructuredText will not bend on.**  No section inside a
directive body: a mechanism is a section, not a box, and inside a `tab-item` a heading has to become a
`.. rubric::`.  No nested inline markup: a literal inside bold renders as raw backticks, a link inside bold
breaks the link.  Pick one.

## Three hard rules for the prose, all enforced

**Write the current fact, not the story of how it got there.**  The pages describe what the binary does and
what the reconstruction reproduces.  They are not a changelog and not a lab notebook, and a bug that has been
fixed leaves no trace in the prose.  Words that usually signal a breach: *now*, *previously*, *used to*, *no
longer*, *turned out*, *for a while*, *at first*, *it was also*, and any first-person account of reading the
disassembly.  `tools/comment_lint.py` reads every `.rst` under `docs/` for them; `conventions.rst` and
`notebook.rst` are the two exemptions, and the notebook is where a finding lives until it is rewritten as a
standing fact.  The exception is a trap a future reader can still fall into.  Keep it, in a `.. caution::`,
phrased as a standing fact -- "a rect drawn before its neighbours keeps only two of its four sides", never
"this was drawn in the wrong order".

**State what is, never how far along the work is.**  A page holds the design or the standing fact.  What is
built and what is not belongs to the area's status page alone (`plans/*-status.md`, and `sc2k-report.rst`
for the simulation), and every other page points there.  This is rule 6 of the comment rules, and
`tools/comment_lint.py` holds every page to it but the status pages and `TODO.md`.

**Write ASD-STE100 Simplified Technical English.**  One idea per sentence, 20 words for an instruction and 25
for a description.  Active voice with a named actor.  No semicolons.  American spelling.  One name for one
thing.  Run the `/ste-writing` self-lint over anything before it goes in.

## Addresses and figures

`docs/appendix-routines.rst` is generated by `tools/gen_routines.py` and is the index of every address the
documents cite.  Re-run it after adding or removing citations.  Every ``$xxxxx`` in the prose is a `:ref:`
into it, and the generator refuses to list anything that is not an instruction address in CODE 2, so a bit
mask like `$FC1F` is never linked as a routine.  Names come from the banner comments in `src/sim/*.c`, the
symbols `tools/symbols.json` puts into the listing, and a curated table at the top of the generator: **add a
name there rather than leaving an entry bare.**

Diagrams are SVG files in `docs/img/`, shown with `.. figure::` and a normal caption.  Each file carries its
own `<style>` block with the palette variables **and the class rules it uses**, plus a `prefers-color-scheme`
block: a standalone SVG cannot see the page stylesheet.  `numfig` is on, so do not write "Figure 1" into a
caption.  **A rect drawn before its neighbours keeps only two of its four sides**: draw the emphasised cell
last, or append a `fill="none"` copy at the end.  Type is Helvetica, monospace only where columns line up,
and each label's width is estimated from its font size and character count.  Anything wider than about
900 px is a `.. thumbnail::` with a `:group:`.

# THE SIMULATION

This part holds the simulation's rules, and how to build and check it.  The findings that produced them, kept
as they were written, are `docs/notebook.rst`.  A sentence in this part that begins "the goal" is the
simulation's.

## The goal

Reconstruct SimCity 2000's simulation from the retail Macintosh binary so that it is **behaviourally
identical** to the original, and prove it against the city files shipped in `~/Downloads/SimCity 2000®
Collection/`.

## What the game is

**The faithful port is the baseline.**  It supports everything original: saves, sprites, sounds and music.
The behavioural reconstruction is that baseline, and it stays checkable against the original forever.  THE
GOALS say what the enhanced world adds to it.

## The approach, and why the obvious one is wrong

**Do not hand-transcribe 68k into idiomatic C.**  Every defect that hand transcription produces is a reading
error, not a thinking error: four bytes skipped in a dispatch, so a case that is free is charged to the
budget; a routine read only to its first `bra.w`, so a whole second pass is missing; a push condition
inverted, so a flood dies on its first tile; three stages run in the wrong order in the VERIFIER, which
flatters a result instead of breaking it.  None of those is possible for a translator that does not decide
what matters.  So:

1. **Prefer emulation and mechanical translation.**  `tools/m68kemu.py` is a 68k interpreter that executes
   the disassembly text: real `jsr`/`rts`, `movem`, the CODE 1 arithmetic helpers by interception.
2. **Use it as ground truth.**  `tools/runsim.py` sets up the A5 world from a city file, runs the real
   routine, and reads the layers back.  That is the oracle.  Hand-written C is a hypothesis checked against
   it, never the source of truth; it is what makes the result readable, and nothing more.
3. **Check against the oracle, not the save file.**  `oracle_diff.py` gives both sides the same starting
   city, so any disagreement is a transcription error with the snapshot skew removed.  A save records a city
   mid-cycle and conflates a misread instruction with a snapshot taken at another moment.

## Building and running

The project is **Arcology** and it builds ONE binary from the root:

    cmake -B build -DARC_CITIES="$HOME/Downloads/SimCity 2000® Collection"
    cmake --build build
    ./build/arcology                                   # the game, load menu
    ./build/arcology bayview                           # cities/ ships with the repo
    ./build/arcology cities/bayview.sc2 --mute --run 1 # a file; --mute when headless
    ./build/arcology --modes                           # the developer modes
    ./build/arcology --verify "$ARC_CITIES" "$ARC_CITIES/Cities"
    ./build/arcology --convert a.sc2 b.arco            # a 1995 save becomes a world
    ctest --test-dir build                             # the tiers: "The constraint on all of it"
    cmake --build build --target format                # apply the house style
    python3 tools/oracle_diff.py "$ARC_CITIES"         # the C against the original
    python3 tools/clock_check.py cities/bayview.sc2    # the whole clock

`src/app/main.c` and `src/app/modes.cpp` decide which mode the arguments name, from one table a mode:
`src/dev/dev.h` for the developer modes and modes.cpp for the four programs of their own.  `src/sim/` is the
simulation, the `arc_core` library; `sim_tick` is one phase of the clock as `$21EDE` dispatches it.  It has
NO file-scope mutable state and it prints nothing: the dice, the ring and the scratch plane belong to the
`City`, and `src/dev/` holds the tooling that drives it.  `src/network/` is the road network,
`src/render/` the renderer, `src/util/` the log, `src/vendor/` lodepng, `cities/` the collection as lowercase `.sc2`.
`src/render/soft/soft.c` reproduces the original's tile renderer and emits the op list the GPU uploads.
`--compare-renderers` shows where the two backends part: it is a drift alarm, not a proof, because only the software path is
checked against the original.  A generated source is never edited:
`src/sim/generated/tables.c`, `src/render/generated/tables.c` and `src/render/generated/shaders.c` come from
`tools/gen_tables.py`, `tools/gen_render_tables.py` and `tools/gen_shaders.py`.  Each takes `--check`, and a
ctest runs it, so a hand edit or a stale copy is caught.  **The prose in one of those files belongs to its
generator.**  Editing the file is reverted the next time it is run.  SDL3 is found or fetched
(`-DARC_FETCH_SDL3=ON`), Dear ImGui fetched on first configure (`-DARC_IMGUI=OFF` skips it); the interface art
is `assets/ui.png` from `tools/pict.py`, the font `assets/fonts/ChicagoKare-Regular.ttf`, a `--theme` pack
from `tools/scheme.py`, the sound effects from `tools/snd.py`.

Zero warnings under `-Wall -Wextra -Wshadow -Wconversion`: a silent integer conversion here is a behavioural
difference.  `.clang-format` is the house style and `ctest` fails if any source drifts from it.  The
simulation's own checks (`verify`, `microsim`, `allocmicro`, the `oracle` label) are behind
`-DARC_SIM_TESTS=ON`.  What reproduces, and against what, is `docs/sc2k-report.rst`; the rules of the game as
the binary implements them are `docs/mechanics.rst`.

## Toolchain

| tool | does |
|---|---|
| `tools/rezfork.py`, `tools/dumpcode.py` | resource fork reader; the CODE resources out of it, one file each: the first step of the listing |
| `tools/m68kdis.py`, `analyze.py`, `dumpall.py`, `annotate.py`, `symbols.json` | capstone front end (A-traps, both switch idioms); function/xref database; the listing `out/CODE_2.asm`; symbol substitution into it |
| `tools/thinkdata.py`, `tools/gen_tables.py` | decompresses DATA 0 into the A5 global image; A5 image → `src/sim/generated/tables.c`, so no constant is hand-typed |
| `tools/m68kemu.py`, `runsim.py`, `oracle_diff.py` | the 68k interpreter, with provenance tracking; **the oracle** — the A5 world from a city, then the real routines; the diff of the C against it, the regression test that matters |
| `tools/miscmap2.py`, `tools/miscload.py` | run the MISC builder (all 1200 field slots) and the MISC **unpacker** (the full index → destination map) |
| `tools/gen_coverage.py`, `gen_budget.py`, `gen_ordinance.py` | run `$24232` for the coverage diamond, `$263C8` for the tile → department table, probe `$41368` for the ordinance cost formulas |
| `tools/rng_diff.py`, `tools/clock_check.py` | both random streams draw by draw, first divergence printed; `sim_tick` against `$21EDE`, every layer, scalar and die |
| `tools/trace_growth.py`, `tools/*_check.py` | the growth pass traced tile by tile on both sides, to a named tile; the oracle checks, one a ported routine, run by `ctest -L oracle` where the game folder is |
| `tools/scen.py`, `tools/tset.py`, `tools/walk_deps.py` | a scenario's `SCEN` resource; the `TSET` shape descriptor the aeroplane reads; the sweep that names every walk mode reading more than one layer |
| `tools/sc2.py` | reads a 1995 save, and decodes its zone corner bits into the grid's own terms |
| `tools/artpack.py`, `tools/gen_previews.py` | art -> indexed PNG atlases + JSON, SCURK import; every preview rendered and the preview pages under `docs/` built |
| `tools/render_oracle.py`, `render_diff.py` | the game's own per-tile renderer, blitter stubbed -> a blit list; the diff of ours against it, blind to anything that is not a blit |
| `tools/render_pixels.py`, `pixel_diff.py`, `pixel_scan.py`, `pixel_sbs.py` | the game's renderer with its REAL blitter, into a framebuffer; pixels vs the original: measure, locate, show |
| `tools/blit_check.py`, `tools/shapedec.py` | our `blit()` vs `$18E96`, every sprite, both mirrors; the tileset decoded by running the game's blitter |
| `tools/gpu_diff.py` | where a GPU frame and a software frame differ, and why |
| `tools/gen_render_tables.py`, `tools/gen_shaders.py` | A5 image -> `src/render/generated/tables.c`; GLSL -> SPIR-V + MSL as `src/render/generated/shaders.c` |

`out/CODE_2.asm` is the listing.  Work from it, not from memory of it.  **Never check a renderer against
another renderer.**  A check that shares an assumption with the thing it checks agrees with it forever, and a
check with a bug of its own invents defects that are not there.  The renderer's checks run the original.

## Working rules

- **An oracle run that stopped early is not the original's answer.**  `Emu.run` returns a string on any
  failure (out of steps, or an unknown trap); `Sim.run` and every `*_check.py` raise on it.  Never read a
  truncated run as ground truth.
- **Stubs have to answer, not just return.**  A skipped call leaves D0 holding whatever came before; where
  the caller tests the result, give the stub the value the real routine leaves there (`Emu.run` takes `stubs`
  as a dict).  Stub whole routines, never traps: a guessed Pascal stack effect corrupts the frame silently.
  Write down why each stub is there, and never add one because it makes a run finish: a stubbed routine that
  drew dice takes those draws away, and the streams line up for the wrong reason.
- **Seed both sides.**  A checker that compares dice sets `A5+0x11DC = 1` and `e.tb_seed = 1` on the oracle
  and `rng_seed(1, 1)` on the C.  A divergence at draw 0 is the harness, not the model.
- **When a comparison fails on something the save does not store, suspect the harness first.**  Unset oracle
  state -- a NULL pointer, an unseeded generator, a derived season left at zero -- reads as a model bug.  If a
  number against the oracle is worse than the same number against the save files, the harness is wrong.
- **Drive the passes in the clock's order.**  A pass that reads state another phase writes is checked after
  that phase has run, or the comparison certifies a state the game never has.
- **A guard that skips work when a value is zero also skips a die.**  The guard belongs on the division
  alone, where the original would trap.  **Drawing code still rolls dice**: "it only draws" is not a reason
  to leave a call out; only "it draws no numbers" is.
- **A block that draws no dice is invisible to a dice check.**  Identical dice prove that everything which
  consumes randomness is right, and nothing else.  Run the layer-and-scalar diff as well.  And run the dice
  comparison even when you expect it to be clean: `tools/rng_diff.py` matching on every draw is a RESULT
  that rules out every RNG-shaped bug at once and points at the logic.
- **Reading the listing.**  A `(d16,An)` displacement is a signed word: any offset shown as `$8000` or above
  is negative, and a value that seems missing from the tag map is filed under its signed form.  Absolute
  `jsr` targets are ambiguous between CODE 1 and CODE 2: `link.w a6` (`4E56`) at the target makes it CODE 2,
  otherwise it is a CODE 1 runtime helper.  A "no code at" stop is a hole in the disassembly, not a wrong
  branch: read the bytes, add the jump-table entry to `m68kemu`, say where it came from.  A byte argument
  sits in the high half of its word: push `0xFF00`, not `0x00FF`, or `tst.b $c(a6)` reads zero.
- **Before porting a routine, grep the C for its address.**  A routine can be ported and sitting at the wrong
  position in its pass.  **Read a routine to its `rts`, not to its first branch**: a ported routine can still
  be missing its tail.  Grep for a switch before assuming its scope: `A5+0x13AA` guards more than the
  disaster trigger.
- **A correct port that seems to make a metric worse is usually breaking a pair of cancelling errors.**
  Check what it depends on and what it exposes before reverting it.
- **When a table is indexed by something wider than the table, do not clamp and do not assume zero.**  Read
  what the original reads, and let `gen_tables.py` read that far.  **Transcribe the original's defects as
  written**: a wrong register, a loop unrolled in an order no loop produces.  The comparison is exact with
  them and not without.  Say so in the comment, in the present.
- **A name in `tools/symbols.json` can be wrong.**  Check for a second entry for the same address first.
  **When two sources of truth disagree, look for a THIRD.**  A self-describing artifact -- a ResEdit `TMPL`
  that names every field -- is still a CLAIM.  The code is what runs.
- **Always print the denominator.**  Count non-completions and say why before quoting a ratio.  Never quote
  an accuracy figure without its denominator, and count only cells non-zero in one or both: all-cell scoring
  is inflated by empty countryside.  Do not fit a constant to make a number look better; report the miss.
- **Extract data.  Do not generate behavior.**  A table with an A5 address in its comment is data the game
  indexes at run time: read it out.  A table that flattens CONTROL FLOW records a plausible answer for every
  input you did not vary and never says which.  Run `tools/walk_deps.py` after any change to
  `tools/gen_walk.py`.  **Sweep a word layer as a word**: ALTM is two bytes per cell.
- Values computed in the same pass are mutually consistent in a save and reproduce exactly; anything compared
  across a pass boundary may not.  The game folder is read-only.
