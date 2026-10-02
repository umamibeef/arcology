# Arcology — house rules

# THE PURPOSE

> **Lua is handed the simulation data, imagines a world on top of it, and asks C to draw that world in 3D with
> primitives.**

The point of it is **iteration**.  How the renderer interprets the simulation — how roads sweep through the original
grid corridors, how a cell naming an on-ramp raises a smooth climb to a highway, what pattern an intersection draws, how
many lanes a road carries for its density and its neighbourhood — must be changeable by **editing a script**.  Not by
compiling.  Not by reading C.

| Lua | C |
|---|---|
| reads the simulation | offers the simulation to be read |
| decides what the world contains | draws what it is asked for |
| invents networks, paths, patterns, lane counts | fits curves, lofts profiles, puts geometry in 3D |

**A decision that lives in C is a decision that cannot be iterated on.** That is the test for every change: afterwards,
can the look be changed from a script?  If not, the change was not progress, whatever else it improved.

## Never take a proxy for the purpose

A measure that stands in for the purpose becomes the goal, and then the purpose is gone.  This has already happened: a
call-up counter with a ceiling and a test behind it drove a whole session of work that moved not one number a person
could reach.  Every measure in this repository is a **witness** that a goal is met.  None of them is a goal.  A change
that improves a measure and leaves the look no easier to change is not progress.

# THE GOALS

In order.  Each depends on the one above it, so nothing lower is worth starting first.  The user sets these; do not add,
reorder or reinterpret them.

**1.  Lua can read the simulation.**  Any layer at any cell, with neighbours, fast enough for a script to walk the whole
map.  Until this is met, nothing else on this list is expressible. MET.  `arc.city.at(layer, col, row)`, `.near` and
`.plane` read any layer at any cell, and `scripts/compose/bands.lua` walks whole planes with them.  `ctest -R map_read`
decodes the city a second time in Python and checks every layer three ways, so it is not the renderer agreeing with
itself. *Met when a script can walk the map itself and read what is on a cell.*

**2.  The fit is a service a script calls.**  `fit(points, radii, budgets) -> pieces`, at any moment the script likes.
MET.  `arc.fit` (`src/script/api_fit.c`) reads no pass state, so there need be no build running at all.  It asks
`arc.rules.pieces`, which is the same cut the drive queues, through the same `PieceFan` and the same four accessors in
`mesh/fit.c`: two callers, one implementation. `ctest -R fit_service`. *Met when a script can fit a path of its own
invention and get the pieces back.*

**3.  The loft is a service a script calls.**  `loft(pieces, profile) -> geometry`. MET.  `w:loft` is the generic sweep
in `mesh/loft.c`: a chain of pieces and a cross-section, and nothing else.  The eleven moments in `net/strip.c` are the
other half, and each offers a reading to the rule the family named.  `ctest -R loft_service`. *Met when a script can
loft a path it invented, along a cross-section it defined, and see it in the world.*

**4.  The network is discovered in Lua.**  Which cells form a segment, and where it runs, is the script's.  `walk/`
stops walking. MET as the criterion below has it.  `scripts/compose/network.lua` produces every run, and there is no
walk in C behind it.  `walk/` has not gone, though: it still runs a segment's own stages once the script has said which
cells make one.  `ctest -R network_discovery`. *Met when C offers cells and neighbours and the script produces the
segments.*

**5.  The interpretations are swappable.**  Each of these can be changed to a different algorithm by editing a script
alone: roads sweeping through the grid corridors; an on-ramp cell raising a smooth climb to a highway; the pattern an
intersection draws; the lanes a road carries, from density and neighbourhood. MET.  `ctest -R swappable` replaces each
with a rule of its own making and checks the city changes with it. *Met when each one can be replaced without a
compile.*

**6.  All routing and every pose is out of the C code.**  C is for rendering.  The chain of points a way runs
between two poses -- an interchange movement, a band's continuation, a taper, a spur's foot and descent, a junction's
connectors, a segment's lane joins, a dead end's cap, a level meet's crossing -- is built by a script and handed to
`arc.fit`; C never answers "the points between these two poses", not even as a test of what can be built.  MET, and
`ctest -R no_routing` (`tools/no_routing.py`, no build needed) holds the line: it fails on any `Piece` filled in by
hand outside `src/render/mesh`, which is the shape every router in C has had.  Its list of what is NOT a route is
two entries long, each with its reason.  No `pose_chain` exists.  A pass that wants a chain queues its two POSES (`net_cut_add_poses`) and the
drive asks `arc.rules.posed` (`scripts/compose/chain.lua`, the biarc at float precision) for the points before the
cut; the interchange and the band ends build theirs directly (`arc.chain_between`, `scripts/compose/links/chain.lua`; the
node's are annealed there).  The poses are the script's own: `scripts/compose/poses.lua` settles every one from the
map before any fit -- a junction's mouths, a band's arms, a spur's gore, a level meet -- and the fits are fitted TO
them (`c:end_at`).  Heights ride on the same poses: a slab hands its gore its finished stations and the ramp leaves
at the slab's height and grade (`scripts/compose/profile.lua`); a node's arms and the knots of its ways are one
relaxation over one graph of heights (`scripts/compose/relax.lua`, `arc.weave.plan`), settled in the grading pass
over the ways the node expects and again in the building pass over the lanes' real ways.  Every chain the scripts
build leaves a pose on a straight, so the curvature is nought on both sides of it; the cut counts the ends that are
not (`arc.g2`, the `G2` lines under `pose_dump`).  C evaluates where a laid piece ends and hands that back, and
decides nothing.  A SPUR'S FOOT is the orient rule's too (`scripts/compose/orient.lua`): where the descent stops at
the line and the way it heads there come back with the rest of what the rule read.  *Met when `pose_chain` has no
caller, `no_routing` passes, and every chain is a script's.*

## The constraint on all of it

Every stage is proved output-identical before it is believed.  A geometry change that cannot show this has not been
verified, however sound it reads.  The proof is `tools/battery.py`, and its reference is `tools/battery_ref.json`:
the file holds every number the renderer is held to, and nothing in this document or any other repeats them.

    python3 tools/battery.py --gate      # the eight views and Atlanta's mesh check: about a minute
    python3 tools/battery.py             # the mesh check over eight cities, every city's corners, the moving world
    python3 tools/battery.py --update    # record an accepted change; the diff of the reference is the record

A value that differs from the reference fails whichever way it moved: an improvement that is not recorded is a
regression waiting to pass.  A world that was not built -- a crash, a rule that runs away, a build abandoned -- is a
FAULT, and a fault is never accepted, whatever the reference says.  The reference moves only after somebody has
looked (`tools/frame.py` frames a difference) and only in the same change as the geometry that moved it.

`ctest` runs in tiers, and the tier is the whole choice a session makes:

    ctest --test-dir build -L lint       # no build needed, a second: the style and layer checkers
    ctest --test-dir build -L gate       # one build of Atlanta, about a minute: what runs between edits
    ctest --test-dir build -L full       # everything the repository alone can run
    ctest --test-dir build -L oracle     # what needs the game's own files

The views frame no water, because the water shader moves every frame and a hashed river cries wolf.  They frame the
2x2 highway INTERCHANGE twice (views seven and eight), because nothing else does.  They say nothing about the traffic:
a headless frame draws no movers, so the battery's `traffic` section hashes every car, gate and train instead.  The
simulation's four checks (`verify`, `microsim`, `allocmicro`, `arco_roundtrip`) are behind `-DARC_SIM_TESTS=ON`: the
simulation is verified against the original and the renderer cannot move a simulation layer.

## Where the renderer's code lives

Four levels, and the includes hold the order: `tools/layering.py` (`ctest -R layering`) fails on any include that
runs up a level and is not on its list of exceptions, each with its reason.  The top runs a build.  `walk/` and
`net/` sit under it, and `net/` includes nothing from `walk/`.  `mesh/` is under all three and includes nothing
from `walk/`; the handful of `net/` names it still reads are the exceptions the checker lists, and whether `mesh/`
should know a family HAS stages is a design question for the user, not a file move.

**Neither `walk/` nor `net/` can see the ART.**  Not a sprite, not a palette index, not an atlas level.  What a
picture MEANS is decided in a script and pushed down as a table, so there is nothing there to look at.  The
checker holds that line too.

The arithmetic is compiled with `-ffp-contract=off`.  With contraction on, a function inlined into its caller rounds
differently from the same function called across a translation unit, so moving a function between files moved the
mesh; off, a move is bit-identical and the battery can prove it.

### `src/render/` — the build, and what all three below it share

* `build` — two passes over one body, handed one at a time to the script that composes them.  `build_pass_begin`
  empties the stores and hands the pass over; `build_pass_field` builds the field when the script says the tops are
  settled; `build_world_nets` runs the network passes by name.  The door itself is `net/drive.c`.
* `incr` — what a build has to redo after an edit.
* `geo` — the numbers a script tunes with: `arc.geo`'s store and `arc.tune`'s knobs.
* `prof` — where a build's time goes, stage by stage.
* `pipeline.h` — the vocabulary the three share, and nothing more: a point, a piece, a sample, a family slot,
  what a loft is asked to draw.  What one directory answers is declared in that directory's own headers.

### `src/render/walk/` — a segment's stages

* `walk` — one segment's stages, from a node to the next node along the pieces a family lays, and the replay of
  the table's segments in the building pass.
* `cursor` — the order the drive steps them in, and the class each segment asks for before anything is fitted.

### `src/render/net/` — the stores and the plumbing

The plumbing that turns a Lua declaration into a family; the fans that offer a reading to a rule and take its answer
back; and the stores.  Nothing here decides anything, and nothing here knows what any family draws.

`family`, `drive`, `cut` and `strip` are the plumbing.  `table` (the segments, their classes and the station
cache), `fits` (the fit arena: every ask, its two ways and the choice, serving the lines and the band alike),
`lane`, `station`, `shelf`, `network` (the runs, and the tiles a drawn segment serves), `cell` (what a cell carries:
the piece tables read off the byte sets the script pushed and the art), `walkway` (the margin network) and
`traffic` are the stores, and `report` says what the city holds on a tile.  `box`, `meet`, `node` (with the arms'
trims) and `margin` are one concept each.  Two concepts are a pair of files: `band` reads what the map says and
`band_walk` walks one, `spur` holds the tiles the map carries and `spur_build` poses, routes and builds the descent.

### `src/render/mesh/` — EVERY SHAPE PRIMITIVE, and nothing else

A file here names no road, ramp, deck, track, junction or footway, and a script inventing something the renderer has
never heard of can build it out of what these offer.  One header a file: `mesh/fit.h` declares the fit and the fans
it offers a rule, `mesh/loft.h` the loft, `mesh/piece.h` the piece arithmetic, `mesh/shapes.h` the shapes,
`mesh/surface.h` the drawn ground, `mesh/tile.h` and `mesh/emit.h` the rest.

`emit` puts triangles, walls, tops and boxes, `shapes` puts line quads and fans over them, and `shape` is the table of
what each shape is and which triangles are its own.  `mesh` builds the mesh and answers the query tool, and `check` is
the watertightness and the lines against the ground.  `fit` is the tangent fit (`arc.fit`) and `loft` is the sweep
(`w:loft`).  `piece` is the arithmetic every path is made of, `surface` samples the drawn ground, `tile` is what a tile
is and the ground field, and `model` draws a model and caches it.

### `scripts/` — the decisions

The numbers, the rules, the families, the models and the drive.

## The pipeline names no family

C holds the primitives and the accounting.  WHAT is drawn -- a road, a railway, a raised highway, a power line, and
everything those imply: a deck, a ramp, a sidewalk, a level crossing -- is the scripts'.  So none of those words appears
under `src/render`: not in a filename, not in an identifier, not in a comment.  `ctest -R family_words`
(`tools/family_words.py`, no build needed) holds the line.

**And not abbreviated either.**  A word list cannot see `hw`, so the checker tests the SHAPE of a name as well: a part
of a longer name that starts with `hw` is the family, and is a fault.  `HwSpur`, `s_hw_st`, `HW_MAX_ST` and `s_hwfit_q`
all named the same family in two letters. A bare `hw` is a HALF WIDTH, which every fit and loft takes, so it passes, and
so do `hw_end` and `SPUR_HW` -- the three are named in `HALF_WIDTH` in the checker.  Add to that list only for another
half width, and say so.

The generic vocabulary they are written in instead.  Each word is
defined by WHAT IT IS: a row that needs another row to make sense is not
a definition, so name the geometry, the cells or the travel the word
stands for.

| word | what it is |
|---|---|
| a **family** | one kind of line, and everything about how it is drawn.  A script declares it in `scripts/families`; C keeps no list of them |
| a **line**, a **strip** | the geometry one family lays along a run of cells: one cell wide, following the ground it crosses |
| a **lane** | one way of travel inside a line or a slab.  A mover drives along a lane, and `net/lane.c` checks that every one of them can be driven |
| a **band** | a run of cells TWO wide, whose spine runs along the SEAM between the two rather than down the middle of either |
| a **slab** | the geometry a band carries: one raised surface on columns, three lanes each way, held a level of altitude over the ground it crosses (`arc.geo.slab_lift`) |
| a **station** | ONE CROSS-SECTION of a slab -- a point, a direction, the distance along, and the height.  The loft takes one every sixteenth of a tile along a straight run and every twenty-fifth around a curve, as the family's `step_run` and `step_arc` ask.  A column, a girder, a lane or a spur is then placed by reading STATIONS, never cells |
| a **node** | where three or four lines meet: the box drawn over that cell |
| an **arm** | one line reaching a node.  Its **mouth** is where it enters the box |
| the **fill** | the surface a node lays inside its own outline, between the mouths |
| a **spur** | where a raised surface comes down to the ground.  Two shapes of it: the sloped cells at a band's own end, and the single lane a slab drops to a line below it |
| a **margin** | a strip laid beside a line and level with it.  Its **lip** is the raised edge along its inside |
| a **lap** | one cell two families share at the same level, and the panel drawn where the two cross |
| a **stripe** | a marked way across a line, laid at a node's mouth |
| a **thread** | a narrow way a family draws inside its own strip, in pairs, on ties laid across it |

A tile family is a SLOT, and which name sits at which slot is the script's.  `Family` is an int; the pipeline reaches
for one only through the pointers the declarations fill in -- `net_line`, `net_thread`, `net_band`, `net_power` -- and
`net_line_rules()` is the knobs of the family the scripts named `line`.

`src/render/soft` is exempt, and says why: it reproduces the ORIGINAL's tile renderer address by address, so naming what
the 1995 game draws is describing the material rather than deciding anything -- the same reason `src/sim` is exempt from
the no-history rule.

## A reading is PUSHED, never asked for

What a byte of the city means, what a picture shows, which rule settles a
situation: every one of those is a decision, and every one is a script's.  C
never calls up to ask.  The script says it once when it is read, and the
pipeline reads the answer out of a table.

| what a script pushes | the table C reads |
|---|---|
| `arc.bytes(name, t [, "number"])` | `script_bytes(name)[b]`: a flag or a small number, per byte |
| `arc.floats(name, t)` | `script_floats(name)[b]`: a number no byte could hold |
| `arc.names(name, t)` | `script_name(name, b)`: a word, for a report to print |
| `arc.numbers(name, t)` | `script_numbers(name, keys, out, n)`: a handful of named numbers |
| `arc.pieces`, `arc.bands` | the piece and band tables, with a shape of their own |

`arc.art` is the other half: the pictures, offered so a script can READ them.
`arc.art.levels`, `.level`, `.tile`, `.pixel` and `.row` say what the art holds
and nothing about what it means.  `scripts/art_tiles.lua` reads them, decides
which edges a piece joins and how high a structure stands, and pushes both
down.  **A reading of the art belongs there, never in C.**

A table nothing pushed reads as nought at every byte.  That is deliberate: a
run that cannot find its scripts draws no network at all rather than a city
half right.

## Traps when extending the script API

Goals 1 to 3 all add API, and these five cost real time every time.

* **The lint's `world` handle is GENERATED** from `WORLD[]` by `tools/gen_lint_stub.py` into
  `src/script/generated/lint_stub.h`, so a new `w:` method needs no edit: it is on the handle, answering the empty
  value of what the accessor pushes.  Run the generator and `ctest -R lint_stub_fresh` holds it.  A method that must
  answer otherwise, to open a pass or to name what the drive reads, is written over in `lint.c` with its reason.
  **A new RULE is still two edits**: `RULES[]` names it, and `rule_args` supplies an argument that exercises its body.
* **A new `arc.*` function needs a stub in `lint.c` too**, beside the one the program registers.  The lint runs with no
  city, no mesh and no art, so the stub answers what a run with none of those would.
* **`arc.rules.<name>` may be nil.**  Index it through a local -- `local fn = arc.rules[rule]; w:control_is(i, fn and
  fn(at))`.
* **A static holding a pass's state must own its data**, never point at a caller's stack local.
* **`arc.put.f32` truncates a float in Lua**, which is what makes script-side geometry bit-identical to the C.  A number
  worked out to more places than the mesh can hold lands a few millionths off and two edges overlap instead of meeting.

## Output: never printf

`printf` is not used anywhere in `src/render` or `src/app`.

* A **message** goes through the log — `R_ERR`, `R_WARN`, `R_NOTE`, `R_DBG` from `src/util/log.h`. It carries a source
  tag and a level, and it can be filtered.
* A **developer dump or a report line** goes through `dumpf` from `src/util/dump.h`. It writes to the file `--dump-to`
  names and to stdout when none is named, so the same line can be read by a person or captured by a script.

A bare `printf` obeys neither switch: it cannot be redirected, cannot be levelled, and is invisible to the tooling that
reads everything else. `ctest -R no_printf` enforces this (`tools/no_printf.sh`); it needs no build and runs in a
moment.

A log inside a per-frame path says its line **once, when what it is
saying changes** — never once a frame.

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

A comment describes the code **as it stands**.  Three rules, checked by
`ctest -R comment_lint` (`tools/comment_lint.py`, no build needed, and the same checker is the write hook):

* **No attribution.**  Not "the user asked for", not a quoted request, not a date from this project's own era.  Who
  wanted the code and when is the repository's history; the source is not the place for it.  A date from the game's own
  era is provenance of the material and stays.
* **No history.**  No account of what the code used to do -- no "used to", "had been", "it once", "before this",
  "no longer".  A reader needs the rule that holds now.  This holds in `src/sim` too: what the ORIGINAL 68k game
  does is its subject matter and is written in the present ("the original divides by zero here"), never as a
  history of the port.
* **Never stack.**  Two multi-line block comments must never sit back to back with no code between them.  That shape
  means a change wrote a new comment and left the old one standing: the two then disagree and the reader cannot tell
  which one the code obeys.

The **docs** are held to the attribution rule too: a page explains the renderer, it does not minute who asked for what
and when.  A date in a page's own eyebrow or rubric is the page's own, and stays.

So when you change code, **rewrite the comment beside it**.  Do not add a second one; do not explain what you changed.
If the old comment is still true, leave it alone; if it is not, replace it.

## The script API: one shape, checked

The pipeline hands a script an **object**: a handle of some *kind*, with a table of methods.  `ctest -R script_api`
(`tools/script_api.py`, no build needed) holds five rules over it, and every one was broken at least once while the API
was written -- each time silently, geometry moving with nothing said about it.

* **A rule asks for the kind of its own name.**  `arc.rules.slide` asks for a `slide`.  A kind that answers several
  rules -- a strip is asked three things, a footway two, a tile two -- is listed in the checker's `SHARED` table with
  the reason.
* **An accessor is `api_<kind>_<method>`.**  The pipeline's own functions are named for what they do, so `shelf_copies`
  and `xlane_measure` are already taken: the `api_` prefix is what tells the script's face apart from the thing behind
  it.
* **Every index counts from NOUGHT** -- the ones the API takes and the ones it hands back.  A `- 1` on an argument means
  a script somewhere counts from one, and the two conventions cannot both be right.  A script-local constant that
  mirrors the pipeline's own numbering counts from nought too: edges and corners are `0, 1, 2, 3`.
* **`info` answers a table.**  A rule reads it by name, so a plain list makes every field positional and a field added
  in the middle silently renames the rest.
* **The linter knows every rule.**  A rule nothing exercises is a rule nothing checks.

Two more that no checker can see.

**A knob is read through the family in hand.**  `net_family_rules(f)`, never `net_family_rules(F_ROAD)` in code that
runs for a railway or a highway.  `arc.rules.family` is handed the family and may answer differently for each; a
hardcoded read ignores that and the script's own answer is silently dropped.

**A Lua sequence a rule RETURNS is Lua's own** -- one-based and `#`-able, because that is what `{...}` and `ipairs`
mean.  A table the script hands BACK to the pipeline is not a sequence: it carries the API's own indices, so it counts
from nought and its length is passed with it.  `o:order` takes the arm table and its count for exactly this reason.

## A family is declared, not written

A family -- one kind of line and everything about how it is drawn -- is a file in `scripts/families`.  There is no C
table of them. `arc.family.define` names the knobs, the material, the loft kind and the eleven stages, and
`net/family.c` turns that into the `NetFamily` the pipeline reads.

* **A stage is NAMED, not pointed at.**  A name the pipeline registered as a primitive binds to that C function; any
  other name binds to `arc.rules.<name>`.  A module keeps its own stage functions `static` and lends them through
  `net_hook_add`, so the list of what a declaration may name is the list of what the pipeline can do.
* **Ask through the door, never through the pointer.** `net_family_has(fam, NH_PROFILE)` and `net_family_profile(fam,
  x)`.  A call site that tests `fam->profile` sees only the primitive and reads a stage a script answers as absent.
* **A name nothing answers to is a fault.**  A knob, a loft kind, a tile family or a lane ending the C has never heard
  of stops the declaration and the family never appears.  A family half declared draws a city half wrong and says
  nothing.
* **The reading is one expression.**  `api_family_read` turns the Lua table into a declaration, and the running program
  and `--lua-lint` both call it.  Two readings drift, and then the lint passes what the program refuses.
* **The lint learns a family's rule names from its declaration.**  A stage answered by a rule invents a name the fixed
  `RULES` list in `lint.c` cannot hold, so an unknown name is kept and settled against the declarations once every file
  is read.

## Renaming: never by substring, never through a string

A rename across this tree is a scripted edit, and two shapes of it break things silently.

**A word boundary is not enough.**  Renaming the rule `walk` to `step` also hits `walk_curves` and `walks`, and the
footway disappears.  Match the whole identifier, and read the list of what will change before changing it.

**A rename must not enter a string literal.**  The names a script reads fields by are C strings: rewriting
`"lane_piece"` to `"api_lane_piece"` compiles cleanly, and the field simply reads as nothing from then on. Rename
declarations and call sites; leave every quoted string alone.

**And each step of a cascade is a fresh input.**  Rewriting `at(4)` to `at(3)`, then `at(3)` to `at(2)`, walks every
index down to nought. Compute the whole mapping first, then apply it once.
