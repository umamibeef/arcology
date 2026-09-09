# Arcology — house rules

# THE PURPOSE

> **Lua is handed the simulation data, imagines a world on top of it, and
> asks C to draw that world in 3D with primitives.**

The point of it is **iteration**.  How the renderer interprets the
simulation — how roads sweep through the original grid corridors, how a
cell naming an on-ramp raises a smooth climb to a highway, what pattern
an intersection draws, how many lanes a road carries for its density and
its neighbourhood — must be changeable by **editing a script**.  Not by
compiling.  Not by reading C.

| Lua | C |
|---|---|
| reads the simulation | offers the simulation to be read |
| decides what the world contains | draws what it is asked for |
| invents networks, paths, patterns, lane counts | fits curves, lofts profiles, puts geometry in 3D |

**A decision that lives in C is a decision that cannot be iterated on.**
That is the test for every change: afterwards, can the look be changed
from a script?  If not, the change was not progress, whatever else it
improved.

## Never take a proxy for the purpose

A measure that stands in for the purpose becomes the goal, and then the
purpose is gone.  This has already happened: a call-up counter with a
ceiling and a test behind it drove a whole session of work that moved not
one number a person could reach.  Every measure in this repository is a
**witness** that a goal is met.  None of them is a goal.  A change that
improves a measure and leaves the look no easier to change is not
progress.

# THE GOALS

In order.  Each depends on the one above it, so nothing lower is worth
starting first.  The user sets these; do not add, reorder or reinterpret
them.

**1.  Lua can read the simulation.**  Any layer at any cell, with
neighbours, fast enough for a script to walk the whole map.  Today it
cannot: `arc.bytes` lets a script declare in advance what each of 256
bytes means, and then C walks the map on the script's behalf.  Until this
is met, nothing else on this list is expressible.
*Met when a script can walk the map itself and read what is on a cell.*

**2.  The fit is a service a script calls.**  `fit(points, radii,
budgets) -> pieces`, at any moment the script likes.  Today the fit is
entered from inside C's own passes and a script may only shape a fit that
C already chose.
*Met when a script can fit a path of its own invention and get the pieces
back.*

**3.  The loft is a service a script calls.**  `loft(pieces, profile) ->
geometry`.  Today the loft is a stage of C's walk with eleven fixed
hooks.
*Met when a script can loft a path it invented, along a cross-section it
defined, and see it in the world.*

**4.  The network is discovered in Lua.**  Which cells form a segment,
and where it runs, is the script's.  `walk/` stops walking.
*Met when C offers cells and neighbours and the script produces the
segments.*

**5.  The interpretations are swappable.**  Each of these can be changed
to a different algorithm by editing a script alone:
roads sweeping through the grid corridors; an on-ramp cell raising a
smooth climb to a highway; the pattern an intersection draws; the lanes a
road carries, from density and neighbourhood.
*Met when each one can be replaced without a compile.*

## The constraint on all of it

Every stage is proved output-identical before it is believed.  A geometry
change that cannot show this has not been verified, however sound it
reads.

    sh tools/fp.sh                       # the six views; the only exact check

    for c in atlanta toronto tokyo flint babar maltron chicago; do
        ./build/arcology cities/$c.sc2 --mute --mesh-check --run 1 2>&1 |
            grep -aE "^mesh check|road clip|claimed by none"
    done

    ctest --test-dir build               # 29 tests

The reference, which every one of these must still print:

    12f28cc62f e664211a6f e43c1d3e03 582be65e62 c8ff0ea62c b303e7b5b4  at 1280x800

| city | triangles | shapes |
|---|---|---|
| atlanta | 1068927 | 33397 |
| toronto | 776689 | 29458 |
| tokyo | 832168 | 29509 |
| flint | 817221 | 30485 |
| babar | 671535 | 21045 |
| maltron | 827675 | 30066 |
| chicago | 1069533 | 35090 |

with `road clip  0 samples` and `0 triangles claimed by none` on every
one, and `ctest` at 29 of 29.  The four simulation checks (`verify`,
`microsim`, `allocmicro`, `arco_roundtrip`) are behind
`-DARC_SIM_TESTS=ON` and off by default: the simulation is verified
against the original and the renderer cannot move a simulation layer.

Two traps in the battery itself.  A crashed run leaves the previous PNGs
in place, so `fp.sh` would report stale hashes as a pass -- it deletes
them first for that reason.  And the hashes are per FRAMEBUFFER: a
display backing-scale change makes `--win 1280x800` produce 2560x1600 and
every hash differs with nothing moved, which is why the script prints the
size it got.


## Where the renderer's code lives

    src/render/walk/    reading the map: the network walk, the pieces a
                        tile carries, the footway network.  GOAL 4 empties
                        this: what it does belongs in a script.
    src/render/geo/     making geometry: the corridor fit, the loft, the
                        lane router, junctions, footways, furniture,
                        models, grading.  GOALS 2 and 3 turn these into
                        services a script calls.
    src/render/net/     the four families (road, rail, highway, power),
                        the plumbing that turns a Lua declaration into
                        one, the segment table, the number store, the
                        running world, the debug report.
    src/render/mesh/    chunks, shapes, emission, the incremental rebuild.
    scripts/            the numbers, the rules, the families, the models
                        and the drive.

`net/hiway.c` is the outlier: 3469 lines that walk the map, make geometry
and declare a family all at once.  It is the largest single obstacle to
goals 2 to 4 and wants cutting three ways.

`net/internal.h` is shared by all three of `walk/`, `geo/` and `net/`, so
the boundary between them is a convention and not yet enforced.

## Traps when extending the script API

Goals 1 to 3 all add API, and these four cost real time every time.

* **`src/script/lint.c` carries a stub `world` handle** as a
  `luaL_dostring` string, and it must list **every** `w:` method or
  `lua_lint` fails.  `RULES[]` gives each rule's answer shape and
  `rule_args` supplies an argument that exercises its body.  Add all three
  when adding a rule.
* **`arc.rules.<name>` may be nil.**  Index it through a local --
  `local fn = arc.rules[rule]; w:control_is(i, fn and fn(at))`.
* **A static holding a pass's state must own its data**, never point at a
  caller's stack local.
* **`arc.put.f32` truncates a float in Lua**, which is what makes
  script-side geometry bit-identical to the C.  A number worked out to
  more places than the mesh can hold lands a few millionths off and two
  edges overlap instead of meeting.

## Output: never printf

`printf` is not used anywhere in `src/render` or `src/app`.

* A **message** goes through the log — `R_ERR`, `R_WARN`, `R_NOTE`,
  `R_DBG` from `src/util/log.h`. It carries a source tag and a level, and
  it can be filtered.
* A **developer dump or a report line** goes through `dumpf` from
  `src/util/dump.h`. It writes to the file `--dump-to` names and to
  stdout when none is named, so the same line can be read by a person or
  captured by a script.

A bare `printf` obeys neither switch: it cannot be redirected, cannot be
levelled, and is invisible to the tooling that reads everything else.
`ctest -R no_printf` enforces this (`tools/no_printf.sh`); it needs no
build and runs in a moment.

A log inside a per-frame path says its line **once, when what it is
saying changes** — never once a frame.

## Comments: what the code does, once

A comment describes the code **as it stands**.  Three rules, checked by
`ctest -R comment_style` (`tools/comment_style.py`, no build needed):

* **No attribution.**  Not "the user asked for", not a quoted request,
  not a date from this project's own era.  Who wanted the code and when
  is the repository's history; the source is not the place for it.  A
  date from the game's own era is provenance of the material and stays.
* **No history.**  In `src/render`, `src/app` and `src/ui`, no account
  of what the code used to do -- no "used to", "had been", "it once",
  "before this", "no longer".  A reader needs the rule that holds now.
  `src/sim` is exempt because there "the old value" and "used to"
  describe the ORIGINAL 68k game, which is the subject matter.
* **Never stack.**  Two multi-line block comments must never sit back to
  back with no code between them.  That shape means a change wrote a new
  comment and left the old one standing: the two then disagree and the
  reader cannot tell which one the code obeys.

The **docs** are held to the attribution rule too: a page explains the
renderer, it does not minute who asked for what and when.  A date in a
page's own eyebrow or rubric is the page's own, and stays.

So when you change code, **rewrite the comment beside it**.  Do not add
a second one; do not explain what you changed.  If the old comment is
still true, leave it alone; if it is not, replace it.

## The script API: one shape, checked

The pipeline hands a script an **object**: a handle of some *kind*, with
a table of methods.  `ctest -R script_api` (`tools/script_api.py`, no
build needed) holds five rules over it, and every one was broken at least
once while the API was written -- each time silently, geometry moving
with nothing said about it.

* **A rule asks for the kind of its own name.**  `arc.rules.slide` asks
  for a `slide`.  A kind that answers several rules -- a strip is asked
  three things, a footway two, a tile two -- is listed in the checker's
  `SHARED` table with the reason.
* **An accessor is `api_<kind>_<method>`.**  The pipeline's own functions
  are named for what they do, so `shelf_copies` and `xlane_measure` are
  already taken: the `api_` prefix is what tells the script's face apart
  from the thing behind it.
* **Every index counts from NOUGHT** -- the ones the API takes and the
  ones it hands back.  A `- 1` on an argument means a script somewhere
  counts from one, and the two conventions cannot both be right.  A
  script-local constant that mirrors the pipeline's own numbering counts
  from nought too: edges and corners are `0, 1, 2, 3`.
* **`info` answers a table.**  A rule reads it by name, so a plain list
  makes every field positional and a field added in the middle silently
  renames the rest.
* **The linter knows every rule.**  A rule nothing exercises is a rule
  nothing checks.

Two more that no checker can see.

**A knob is read through the family in hand.**  `net_family_rules(f)`,
never `net_family_rules(F_ROAD)` in code that runs for a railway or a
highway.  `arc.rules.family` is handed the family and may answer
differently for each; a hardcoded read ignores that and the script's
own answer is silently dropped.

**A Lua sequence a rule RETURNS is Lua's own** -- one-based and `#`-able,
because that is what `{...}` and `ipairs` mean.  A table the script hands
BACK to the pipeline is not a sequence: it carries the API's own indices,
so it counts from nought and its length is passed with it.  `o:order`
takes the arm table and its count for exactly this reason.

## A family is declared, not written

A family -- one kind of line and everything about how it is drawn -- is a
file in `scripts/families`.  There is no C table of them.
`arc.family.define` names the knobs, the material, the loft kind and the
eleven stages, and `net/family.c` turns that into the `NetFamily` the
pipeline reads.

* **A stage is NAMED, not pointed at.**  A name the pipeline registered
  as a primitive binds to that C function; any other name binds to
  `arc.rules.<name>`.  A module keeps its own stage functions `static`
  and lends them through `net_hook_add`, so the list of what a
  declaration may name is the list of what the pipeline can do.
* **Ask through the door, never through the pointer.**
  `net_family_has(fam, NH_PROFILE)` and `net_family_profile(fam, x)`.  A
  call site that tests `fam->profile` sees only the primitive and reads a
  stage a script answers as absent.
* **A name nothing answers to is a fault.**  A knob, a loft kind, a tile
  family or a lane ending the C has never heard of stops the declaration
  and the family never appears.  A family half declared draws a city half
  wrong and says nothing.
* **The reading is one expression.**  `api_family_read` turns the Lua
  table into a declaration, and the running program and `--lua-lint` both
  call it.  Two readings drift, and then the lint passes what the program
  refuses.
* **The lint learns a family's rule names from its declaration.**  A
  stage answered by a rule invents a name the fixed `RULES` list in
  `lint.c` cannot hold, so an unknown name is kept and settled against
  the declarations once every file is read.

## Renaming: never by substring, never through a string

A rename across this tree is a scripted edit, and two shapes of it break
things silently.

**A word boundary is not enough.**  Renaming the rule `walk` to `step`
also hits `walk_curves` and `walks`, and the footway disappears.  Match
the whole identifier, and read the list of what will change before
changing it.

**A rename must not enter a string literal.**  The names a script reads
fields by are C strings: rewriting `"lane_piece"` to `"api_lane_piece"`
compiles cleanly, and the field simply reads as nothing from then on.
Rename declarations and call sites; leave every quoted string alone.

**And each step of a cascade is a fresh input.**  Rewriting `at(4)` to
`at(3)`, then `at(3)` to `at(2)`, walks every index down to nought.
Compute the whole mapping first, then apply it once.
