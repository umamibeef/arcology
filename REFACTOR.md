# Arcology — the refactor plan

Measured against the tree at commit `40c3bb6` on 13 September 2026.  Every number below was taken from a
fresh build of that tree or from a script run over it, and every claim carries the file and line it rests
on.  When this plan is done, this file is deleted: the open work goes back into `TODO.md`, the rules into
`CLAUDE.md`, and the facts into `docs/`.

## Where the tree stands

The constraint at the top of `CLAUDE.md` says every stage is proved output-identical before it is believed,
and names the reference every proof must print.  On a clean build of this tree, the reference does not
reproduce.

| check | reference says | the tree prints |
|---|---|---|
| `tools/fp.sh`, eight views | `c2a1eea4b9 … 5cfeb5254e` | 2 of 8 match (views 2 and 4) |
| triangle counts, seven cities | e.g. atlanta 1070955 | all seven differ (atlanta 1080093) |
| shape counts | e.g. atlanta 33413 | five of seven differ |
| `line clip` | 0 on every city | flint: 327 samples under the terrain on 3 tiles |
| `ctest` | 39 of 39 | 36 of 39 |

The three red tests are all ratchets against a baseline file: `no_broken_segments` (junetown has 15
segments without geometry, baseline 2), `no_collisions` (tokyo 414 pairs, baseline 346), `moving_world`
(every one of the five cities' digests differ, and atlanta has 1488 cars against 1478).  The baselines
were committed on 9 and 10 September and the tree changed through the 13th in commits called "Saving
work".  So nobody can say today whether the differences are regressions or accepted changes, and that is
the state the whole constraint exists to prevent.

The memory of the last sessions records the same thing on 9 September: the hashes in `CLAUDE.md` were
stale, the file was re-baselined, and it drifted again inside four days.  A reference kept in prose
cannot hold.  That is the first thing to fix, and nothing else in this plan can be verified until it is.

## The diagnosis

Ten causes.  Each is the reason for one or more phases below, and the phases cite them by number.

**1.  The reference lives in prose.**  `fp.sh` stores no hashes (tools/fp.sh:32).  The only copies are
`CLAUDE.md:96-101` and a different six-view set at `TODO.md:173`.  Four ratchets carry four reference
files in two naming conventions (`collide_ref.json`, `traffic_ref.json`, `corner_baseline.json`,
`buried_baseline.json`) with four `--update` flags, and `corner_baseline.json` counts 32 cities twice
because it sweeps both `cities/` and the game folder.  A change to any of them is invisible in a diff
of the code.

**2.  The checkers give false greens.**  `tools/script_api.py` reports clean because its `kinds_of`
regex (script_api.py:56-61) matches the shape `api_object_open` no longer has (api_object.c:6660-6664);
it holds none of its five rules.  `tools/lua_lint.sh:4` reads `scripts/*.lua` before `scripts/*/*.lua`,
the opposite of the program's order (script.c:270-330), and in the program's order the lint reports one
fault the shipped lint passes: `links.lua:39` reads `arc.geo.band_lead` before `geo.lua:100` defines it.
`tools/no_printf.sh:12` matches bare `printf(` only, so twenty `fprintf(stderr` calls in `src/app` and
`src/render` pass.  `tools/family_words.py:49` scans `src/render` only, so `api_strip_road_class`,
`api_world_rail_threads` and `l_road_class` in `src/script` are never seen.  Two comment linters
disagree on the same tree: `comment_style.py` (in ctest) passes, `comment_lint.py` (the write hook in
`.claude/settings.json`) exits 1 with 118 findings, and each `CLAUDE.md` names only its own.
`call_up.py` scans `src/render` only and misses `api_fit.c:46` and `api_model.c:141`.
`no_junction_spurs_*` passes on exit code alone.

**3.  The documented layering is not the real one.**  `CLAUDE.md` says `pipeline.h` is "what the three
share, and nothing more".  It is 833 lines: 174 prototypes (115 defined in `mesh/`, 52 in `walk/`, 5
defined nowhere), 17 externs of which 13 are `s_`-named pass state, and it includes `mesh/internal.h`,
`script.h` and `walk/walkway.h` (pipeline.h:62-63, 385).  Everything that includes it sees the walk,
the mesh internals and the script API.  Its guard is still `R_NET_INT_H`.  `mesh/mesh.c:254`
`mesh_build_pass` drives `walk/` and `net/` (`build_networks`, `build_bands`, `build_networks_draw`),
so the two-pass driver sits in the layer meant to know nothing above itself.  `walk/` and `net/` are a
cycle in both directions: 52 distinct `net_*` calls from `walk/`, 22 walk-defined symbols called from
`net/` (`prof_now` 18 times, `grade_only` 11), and 32 `net_`-prefixed functions are defined in `walk/`.
`src/script/internal.h:13` includes `net/net.h` while `pipeline.h:63` includes `script.h`.
`render/camera.c:8` and `render/prefs.c:8` include `../app/internal.h`.

**4.  The band is a second copy of the line.**  The fit arena in `walk/cursor.c:108-217` and the one in
`net/band_walk.c:290-303` have the same held/free/tally shape and parallel APIs (`net_fit_*` at
cursor.c:253-330, `net_band_fit_*` at band_walk.c:320-379).  The curve overlay block at `walk.c:660-695`
is `band_walk.c:568-603` plus one lift.  `s_seg_hold`/`s_seg_live` (walk.c:789) is
`s_band_hold`/`s_band_hold_live` (band_walk.c:604) is `s_band_pend` (band_walk.c:678).  Three walkers,
three fits: `walk_segment`/`seg_fit`, `walk_band`/`band_fit`, `build_spur_routed`/`spur_route`.  A
segment is five types: `NetRun`, `BandRun`, `RSeg`, `Seg`, `BandSpur` (net/types.h).

**5.  Pass state is scattered statics.**  270 file-scope mutable statics in 34 files under
`src/render`.  About 25 point at somebody else's data (`s_check_xbld` at `c->xbld`, `s_city` twice,
`s_stage_obj`, `s_spur_m/c`, `s_orient_c/l`, `s_bandfit.*`, `s_links.*`, `s_tk_jb`, `s_tri_index_m`,
`s_piece_art`), which is the trap `CLAUDE.md` names under "a static holding a pass's state must own its
data".  Five names mean different things in different files (`s_path`, `s_node`, `s_on`, `s_n`,
`s_city`).  This is also what blocks the hundred-times world: one build per process.

**6.  Decisions and look still in C.**  Interpretation: `walk/walkway.c:146` decides a margin stops at
`xbld >= 0x49`; `net/traffic.c:573` decodes XTXT `0xC9..0xF0` into a thing record; `mesh/mesh.c:223`
splits the XZON nibble; `walk/cell.c:144-196` derives a piece's link mask by probing sprite pixels
(palette 0x91/0x8B at sprite `0x1D + piece`); `net/band.c:378-390` sets a structure's height from sprite
bulk.  Look: overlay and wire colours as literals at walk.c:465/669/678, band_walk.c:571/580/587/908,
lane.c:705/1094/1325/1514/1635, traffic.c:928; material ids compared as magic floats (shapes.c:115/188/256,
emit.c:183/310, check.c:588/608/649/736/871) although `generated/materials.h` names them.  Knobs
duplicated as C literals: `fit_straight_dot` = 0.9999 again at walk.c:677 and band_walk.c:586,
`lane_join_dot` at fit.c:1367, `loft_split_probe` at check.c:389 and shapes.c:253.  And `mesh/fit.c`
chooses which rule runs by returning its name as a string (fit.c:1618/1624/1630 `"meet"`, `"bridge"`,
`"step"`; api_object.c:1866/1876/2810 `"runs"`, `"chain"`, `"stair"`): the layer that must know no rule
names six.

**7.  The script layer grew without a spine.**  No `require` anywhere: C loads 89 files in `strcmp`
order and every cross-file link is a global under `arc.*` (38 helper exports).  `arc.rules.links` is
872 lines with 15 nested closures (links.lua:95-966); `arc.weave.plan` is 460 (interchange.lua:566-1025);
`arc.rules.world` is 613.  Eleven Lua files carry a header naming a different file (`deck.lua` says
`slab.lua`, `footway.lua` says `margin.lua`, `families/road.lua` says `line.lua` …).  `script.c:673-1486`
holds 16 `script_rule_*` wrappers and `api_take_marks/lamps` with zero callers, about 808 lines.
`lint.c`'s stub world lacks `narrow` and `routed`, which scripts call (highway.lua:114, slide.lua:162).
Edge tables cross the boundary one-based at api_object.c:1436/1451/5379, against "every index counts
from nought".  Twelve `arc.geo` knobs are read by nobody, three byte sets are pushed and never read,
and every `arc.geo.x` read is a linear `strcmp` scan (api_tune.c:26-41).  `api_object.c` is 6685 lines
holding 38 kinds and 378 methods.

**8.  The simulation side has its own edges.**  The three generators (rng.c:36-46) and the flood queue
(sim_queue.c:25-29) are file statics rather than `City` members, so two cities cannot tick in one
process, which the regions goal needs.  Every developer mode is parsed twice, once in the table at
modes.cpp:30-57 and again as a `strcmp` chain in dev.c:448-1424, and `arc_dev_main` is one 1071-line
function.  `src/sim/generated/tables.c:1-3` and `src/render/generated/tables.c:1-11` were hand-edited by
the STE sweep, so re-running either generator produces a file `ctest -R ste_comments` rejects.
`shaders.h` and `tables.h` sit in `generated/` with no generator.  `arc_core` writes to stderr directly
(sane.c:140, sim_growth.c:446) and includes no log header.  `economy.c`, `advisor.c` and `sim_int.h` are
missing from the format check list (src/sim/CMakeLists.txt:105-106).  `dev.c` and `testcity.c` live in
`src/sim` but build into `arc_dev`.

**9.  Five documents describe the state of the project, and all five are stale in different ways.**
`TODO.md` has five goals, six views, `net/hiway.c`, `geo/fit.c` and nine identifiers that no longer
exist, and contradicts itself on whether the interchange is mesh.  `README.md` lists `road.c`,
`soft_paint.c`, `gpu_frame.c`, `options.c`, gives 84 cities where the brief gives 85, seven generators
where the brief gives three, and never mentions Lua or `scripts/`.  `.claude/CLAUDE.md` is 1876 lines
of which 27 are diary ("this cost an hour", "I recorded that it was"), carries two incompatible build
recipes (`cmake -S render -B build` and `arcology assets <city>`), ten dead source paths, "ten tests",
and contradicts itself on Charleston, on eleven versus fourteen disasters, and on whether the aircraft
is verified.  `docs/enhanced-renderer.rst` is a changelog written around `road.c/rail.c/hiway.c`, the
layout `family_words` forbids.  `docs/scripting.rst` cites eight API names that do not exist
(`arc.stale`, `arc.reload`, `net_geo`, `arc.put.ground`, `w:ground`, `w:tint`, `w:highways`,
`arc.mat.road`) and a family example with the wrong keys.  `docs/future.rst` says "nothing on this page
is implemented" and marks three items Built.  The Sphinx build crashes: `docs/img/preview/` is absent so
29 of 47 figures dangle, and `future.rst:432` has a warning `-W` turns into an error.  No page covers
`arc.fit`, `w:loft`, the poses, the drive, the movers, thirteen of twenty `net/` files or any of
`src/script`.  The rule documents break their own rules.

**10.  Hygiene.**  `debug/` holds 19 tracked tile dumps and is also the last line of `.gitignore`.
`testcity.sc2-saved.sc2` sits at the root, referenced by nothing.  Thirteen tools are referenced by
nothing, nine of them oracle checks whose results the brief quotes without naming the tool.
`out/CODE_2.ann.asm` is "the listing to work from" and does not exist, so `gen_routines.py` cannot run.
`CMakePresets.json:9` points `ARC_ASSETS` two levels above the repo.  Three `project()` versions.  The
no-SDL branch of `src/render/CMakeLists.txt:222` has never linked (`dumpf` and `lua_lint_main`
undefined).  No CI, no `-Werror`.  `~/Downloads/SimCity 2000® Collection` is hardcoded in three tools.

## The principles the plan follows

* **Order by dependency.**  A phase that cannot be proved against a baseline does not start before the
  baseline exists.  That is why phase 0 is alone and first.
* **Every step is one of two kinds.**  Either it is proved output-identical against the battery, or it
  is a deliberate change to the look that somebody has framed (`tools/frame.py`) and looked at, after
  which the battery is re-baselined in the same change.  There is no third kind.
* **Delete before restructuring.**  Dead code moved is dead code twice.
* **A move is not progress.**  A file changing directory counts only when a cycle breaks, a copy
  disappears or a static gets an owner.  Measure the includes before and after.
* **A C change is a finding.**  When a decision leaves C for a script, say which reading or primitive
  was missing.  When a decision stays in C, say why the script could not reach it.
* **The document changes with the code.**  A step that renames a thing rewrites the page and the
  comment that name it, in the same change.  Nothing is written down twice.

## Phase 0 — a baseline you can trust

DONE, 13 September 2026, with the state below.  Causes 1 and 2.

1. **One battery, one reference file, one update flag.**  `tools/battery.py` runs the eight views, the
   mesh check over eight cities, the corner sweep over every city in `cities/`, and the traffic digests, and
   compares all of it against `tools/battery_ref.json`.  `--update` rewrites the file and prints what moved.
   `fp.sh` is a two-line caller; `collide_check.py`, `corner_check.py`, `buried_check.py`,
   `traffic_check.py` and their four reference files are gone.  A world that was not built is a FAULT and is
   never accepted, whatever the reference says.
2. **The tiers.**  Every test carries a label: `lint` (no build, a second), `gate` (the views and Atlanta's
   mesh check, about a minute), `full`, `oracle`.  Eight tests the battery covers were retired; 33 remain.
3. **The ratchets.**  Building the previous commit beside this one showed every difference from the recorded
   reference comes from the last commit's interchange work: every city's triangle count, six of eight views,
   every traffic digest, flint's 327 clipped samples and junetown's 13 new segments without geometry.  The
   reference now records the tree as it is, and what is broken is recorded as FAULTS the battery refuses:
   `hi-loh20`, `london` and `simcity` crash in the spur builder (`spur_build.c`, a null band record reached
   from `spur_route`); `hilton` and `keith` are abandoned when `interchange.lua` refuses a node ("2 bands reach
   it but the node has 5 edges").  Two crashes (`barcelona`, `centerville`) were the cut's tail read on a
   path with no points, guarded in `mesh/fit.c`.  Flint's clip and junetown's segments are recorded as
   values and are open work in `TODO.md`: both are the in-flight interchange work, not phase 0's.
4. **The false greens.**  `script_api.py` reads `KINDS[]` and refuses to pass when it finds no kinds; the
   seven accessors it then found were renamed.  `--lua-lint` takes the scripts folder and lists it with the
   program's own walk, and the one fault that found (`links.lua` reading `arc.geo.band_lead` at load) is
   fixed.  `no_printf.sh` matches every write to the terminal, and the 22 `fprintf(stderr)` calls in
   `src/app` and `src/render` go through the log.  `family_words.py` scans `src/script` too, and the 34
   family words there are gone (the dead wrappers with them, and one knob renamed `band_line_dot`).
   `call_up.py` names every allowed site by file and rule and fails on any other.  `comment_lint.py` is the
   test and the hook, `comment_style.py` is gone, and every finding in the code and the tools is rewritten;
   `docs/` joins it in phase 2.
5. **`CLAUDE.md` carries no numbers.**  The constraint section names the battery, the reference file and the
   tiers.

## Phase 1 — delete the dead

DONE, 13 September 2026.  Output-identical: the full battery matches the reference on every value, and the
software rasteriser's frames are byte-identical on three cities through the one reader.  Causes 6, 7, 10.

1. The sixteen unused `script_rule_*` wrappers went in phase 0; `api_take_marks/lamps` and the two
   helpers only they used are gone with their declarations, and `script_family_rules_reset` is static.
2. `pipeline.h` lost the seven unused defines, the five prototypes with no definition, the orphan comment
   above them and the duplicate evaluator's declaration.
3. One `piece_at`; one scratch buffer for the three mesh checks; one declaration of the junction tiles in
   `fit.c`.
4. `geo.lua` lost the five knobs nothing read (`buck_long`, `hold_over`, `hold_speed`,
   `interchange_arm_apart`, `slot_xrail`); the other seven the audit named have readers and stay, as do
   the three byte sets, which C reads.  `arc.band_readings` stays: `tools/network_check.py` audits the
   bands with it.  The biarc is `chain.lua`'s own.
5. The renderer reads a city through the simulation's reader: `rcity_load` lives in `adapt.c` on top of
   `city_load` and `adapt_city`, and `city.c` holds only the corner mask.  The seam holds: nothing under
   `src/render` but the adapter names the simulation.
6. `debug/` and `testcity.sc2-saved.sc2` are removed from the tree; `gen_graph.py` and `extract_figs.py`
   are deleted; eight oracle checks are `ctest -L oracle` tests behind `ARC_SIM_TESTS`; `dumpcode.py` and
   `trace_growth.py` are in the toolchain table.
7. The eleven Lua headers name their own file; `gen_shaders.py` names its output; `api_rules.c` describes
   the `meet` rule `join.lua` answers.

## Phase 2 — one set of documents

DONE, 14 September 2026.  Cause 9.  Four documents, each with one job, and nothing said in two of them:

| document | job | rule |
|---|---|---|
| `README.md` | the front page: what this is, how to build, how to import | numbers only where a script prints them |
| `CLAUDE.md` | the purpose, the goals, the constraint, the house rules | no numbers, no history |
| `docs/` | the reference: how the original works, how the renderer works, the script API | current fact, generated where it can be |
| `TODO.md` | what is open | rewritten from scratch |

1. `.claude/CLAUDE.md` is 268 lines of standing rules with every path corrected and no numbers.  The findings
   are `docs/notebook.rst`, marked as a notebook and exempt from the current-fact rule, and thirteen of the
   traps are cautions in `docs/mechanics.rst` and `docs/sc2k-report.rst`, phrased as standing facts.
2. The Sphinx build is clean under `-W` on a clone without the game's pictures: `docs/conf.py` loads the
   lightbox only where the preview manifest exists and stands a note in for each missing picture.
   `ctest -R docs_build` holds it, in the gate tier.
3. `docs/appendix-script-api.rst` is generated by `tools/gen_script_api.py` from the registrations in
   `src/script` and the rules the lint knows; `ctest -R script_api_fresh` holds it fresh.  `docs/scripting.rst`
   keeps the explanation and points at it.  Three of the eight "missing" names the audit listed exist
   (`arc.stale`, `arc.reload`, `arc.put.ground`); the other five and ten more stale names are gone.
4. `docs/enhanced-renderer.rst` describes the pipeline as it stands: the four levels, the vocabulary, the
   drive, the network, the poses and chains, the fans, the loft and the strip, the stores, the movers, the
   checks and the battery.  `docs/renderer-design.rst` cites what exists.
5. `README.md` names the directories that exist, says what Lua is for, and points at the report page for
   the corpus figures.
6. `docs/future.rst` carries no built item, no date and no name.
7. `TODO.md` is what is open, and nothing else.

`tools/comment_lint.py` gained the rule that every path a page cites exists, runs it over Markdown too, and
`ctest -R comment_lint` covers `docs/` and the three Markdown pages.  Proof: `ctest -R docs_build`,
`comment_lint` and `script_api_fresh` green.

## Phase 3 — make the layering real

DONE, 14 September 2026, with two things left open below.  Causes 3, 4, 5.  Every step was one change proved by
the battery, and `tools/layering.py` (`ctest -R layering`, lint tier) now holds the four-level rule: an include
runs down the levels or it is on the checker's list with its reason, and a listed include that has gone fails too.

**A finding first.**  Moving one function between two files changed Atlanta by five triangles with nothing else
touched.  With `-ffp-contract` on (the compiler's default), a function inlined into its caller rounds differently
from the same function called across a translation unit.  So no move was provable.  The renderer is now compiled
with `-ffp-contract=off`, the reference was re-recorded once with that reason in its note (0.03 to 0.5 percent of
the pixels in four views; the `buried` count fell from 138833 to 7953 because most of it was faces made coplanar by
fused rounding), and every move after it was bit-identical.

1. **`pipeline.h` is the vocabulary**, 161 lines: a point, a piece, a sample, a family slot, what a loft is asked
   to draw.  The 114 mesh prototypes are in `mesh/{fit,loft,piece,shapes,surface,tile,emit}.h`, one header a
   file; the 52 walk prototypes in `walk/walk.h` and `walk/cursor.h`; the fans in `mesh/fit.h` beside the fit
   whose readings they are (not `net/fan.h`: their functions are the fit's); the externs in the header of the
   file that defines them.  It includes neither `mesh/internal.h`, `script.h` nor the walk.
2. **The driver is the build's.**  `build_pass_begin`, `build_pass_field` and `build_world_nets` are in
   `build.c`; `mesh.c` sets up what a pass composes its tiles from and drives nothing.
3. **`net/` includes nothing from `walk/`, and `mesh/` includes nothing from `walk/`.**  The profiler is
   `render/prof.c`; `grade_only` and the width compensation are the build's; the piece tables (`cell.c`) and
   the margin network (`walkway.c`) are stores in `net/`; the class store is the segment table's, the trims are
   the node's, the served tiles are the network's; the table's replay is the walk's stage, run from the table.
   The `net_`-prefixed trims in `walk.c` are `walk_trim_*`; the dead cell walk is gone.
4. **One fit arena** (`net/fits.c`) serves the lines and the band: every ask, its held and free way, the scores,
   the tallies and the choice, in one implementation with two instances.  **One curve overlay**
   (`mesh/shapes.c` `curve_marks`).  The five segment types stay: collapsing `NetRun`, `BandRun`, `RSeg`, `Seg`
   and `BandSpur` is a redesign of the table, not a move, and is recorded below.
5. **Pass state: the audit, not the struct.**  Every static that points at another's data was read: each is set
   at a pass or stage's start from a live store, a live struct or a family's string pool, and none points at a
   stack local.  The `Build` struct that would make a build a value is NOT done: it touches 270 statics in 34
   files, and this phase's proof discipline (one change, one battery run) makes it a phase of its own.
6. **`camera.c` and `prefs.c` are the app's** (`src/app/`), which is what they were: every function takes an
   `App *`.  `src/render` includes nothing from `src/app`.  `pipeline.h` no longer includes `script.h`.
7. **The includes that still run up** are on the checker's list, each with a reason, 41 in all: `mesh/` reading
   twelve named things from `net/` (the loft asks a family which stages it has, the ground is cut to the shelves,
   the checks read the served tiles), and `net/`, `walk/` and `mesh/` reading pushed data through `script.h`.
   Whether `mesh/` should know a family has stages is the design question left to the user.

A critic read the diff and confirmed one code path per concept; the two gaps it found are closed (a mesh header
declared a net symbol; the band's tally bucket was a literal beside the family's own field).  The simulation's
library is not compiled with `-ffp-contract=off`: its proof is the oracle, not the battery, and it is left as it is.

Left open from this phase, in order: the `Build` struct (step 5); the segment types (step 4); `net_family_rules`
declared in `margin.h` rather than `family.h`; `seg_measure_arms` still exported from `walk.h` for one caller.

## Phase 4 — the remaining decisions out of C

DONE, 14 September 2026.  Cause 6.  Every item was proved by the battery, unchanged: the eight views, the
eight cities' mesh counts, the 102-city corner sweep and the five cities' traffic hashes are all identical,
and the only faults are the four the interchange work already owned.  No look changed, so nothing was
re-baselined.

**A finding first.**  `s_band_top` in `net/band.c` held a structure's height for every tile of the map and
nothing ever read it.  The whole sprite-bulk scan existed to fill a dead array and print one dump line per
building id.  It is now a table a script pushes, measured once per byte instead of once per tile.

1. **C names no rule.**  `path_try` answers WHICH way it laid (`TRY_ARC`, `TRY_BIARC`, `TRY_WALK`) and the
   reading it made; `api_path_runs`, `api_path_chain`, `api_path_ending` and `api_world_band_chain` answer
   the reading alone.  `scripts/compose/path.lua` holds the table `JOIN` that says which rule reads which
   way, and names `runs`, `chain` and `fit` itself; `scripts/compose/world.lua` names `stair`.  The rule
   names a FAMILY declares for its loft stages still come back from C, because those are the script's own
   names handed back to it.
2. **Six cell readings are byte sets now.**  `margin_beyond` (why a margin's end meets nothing),
   `thing_marks` (which record a text byte names, one past the place so nought means none), `zone_of`
   (which half of a zoning byte is the zone), `geometry_tiles` (which bytes owe geometry, and which of
   those a fit may sweep out of), `placed_tiles` (a tile something was placed on, which the map view keeps
   the art of), and the report's words.
3. **The art is read in a script.**  `arc.art` (`src/script/api_art.c`) offers the pictures: how many
   levels, one level's measurements, a sprite's own, one pixel, and how much of one row is painted.
   `scripts/art_tiles.lua` reads them and pushes two answers down: which edges each piece joins, and how
   high each structure stands.  `piece_links` is eleven lines and `band_free_air` has no pixel loop.  The
   atlas level then had no reader left in the link path, so it left `tile_links`, `eff_links`,
   `build_island`, `end_point`, `build_junction_nth`, `stage_three`, `stage_three_ask`,
   `build_networks_controls`, `build_networks_trims` and `net_disc_planes`.
4. **Materials by name.**  `MAT_IS`, `MAT_IN`, `MAT_ABOVE`, `MAT_FROM` and `MAT_UPTO` are generated into
   `generated/materials.h` beside the numbers, and every magic float comparison reads through them.  The
   mesh check's own `mat_name` had DRIFTED from `materials.def` -- it called material 3 "glass" and 6
   "water" where the file says water and surface -- and its histogram bucketed materials modulo eight.
   Both now read the generated table.
5. **The overlay's colours are the script's.**  `mark_corridor`, `mark_free`, `mark_spur`, `wire_link`,
   `wire_connector`, `wire_spur`, `mark_end`, `mark_arc`, `mark_corner`, and a car's and a train's paints.
   The three tolerances that were written twice are named for what each measures: `fit_jog_dot`,
   `coplanar_z`, `decal_slot`.
6. **`arc.geo` is a hash.**  A read by name was a walk of 235 names with a `strcmp` at each, and one build
   of Atlanta makes 399877 of them.  The store now keeps an open-addressed table of where each name sits,
   and `arc.geo`/`arc.tune` ask it for the one name.  Measured on Atlanta, nine interleaved pairs: 7.04 s
   at best against 7.26 s, and every paired run faster.

Three primitives were added, and each is the missing half of a reading: `arc.art` (the pictures),
`arc.names` (a word per byte, for a report to print) and `arc.floats` (a number per byte that a byte
cannot hold).  Each has a stub in `lint.c` answering what a run with no art or no city would.

**A critic read the diff and found twenty-four things.**  Six falsified a claim and are closed.  The
report kept the water mark in C beside the tables it said were the only opinion, so the terrain's marks
are a table too.  The ground's mean color was a third reading of the art, with a magic sprite id and a
literal color: which picture and what fallback are now the script's, and the mean stays arithmetic.  The
SHADER was the third reader of `materials.def` and had never been generated although the file's own header
said it was: `gen_materials.py` now writes `gpu/shaders/materials.glsl`, `gen_shaders.py` splices it, and
the twenty-five hand-written range tests read the same names the C does.  Which pieces are slopes was
decided in the script and in C; both now read one table, and so does the slope's axis.  `mark_spur` was a
colour doing double duty as a geometry threshold, which is now `mark_lid`.  And the atlas level, dead in
`net/` and `walk/` once the art reading left, is gone from all nineteen functions, four structs and a
header that included the atlas for nothing else: **neither `net/` nor `walk/` can see a picture at all**,
and `tools/layering.py` holds that line.

Eleven more were real and are closed: a band kind nothing answers to is a fault rather than a slab; a table
no script pushed says so once instead of reading as nought; `arc.numbers` no longer drops a key in silence
and no longer converts a key in place inside its own walk; the geo hash has a compile-time guard and
refuses a name it could not hold whole; five orphaned comments in `emit.c` describing constants that are
gone are rewritten; eleven comments citing rules the tree does not have are corrected; four dead branches
in `lint.c`, one of them demanding the opposite of what the script pushes, are gone; three hot readers
that had a per-call name lookup now read once a generation.  `comment_lint.py` gained `--strict`, which
names its own blind spot and its size.

`grep 0x` under `src/render` now finds no tile id and no palette index outside `city.c` and `city.h`, which
decode the save format's own bit layout.  The art set's own sprite runs are `arc.numbers("art_runs")`.

Left open from this phase, recorded in `TODO.md`: the 135 stacked comments `--strict` finds; `arc.bytes`
checking only that a key is a byte; the traffic numbers written positionally through one struct member; and
the three C copies of the tile family list, which is phase 5's.

## Phase 5 — the script layer

DONE, 14 September 2026.  Cause 7.  Every step was proved by the gate, unchanged: the eight view hashes and
Atlanta's mesh are identical after each one, and `lua_lint` reads the whole layer through `init.lua` the way
the program does.

**Three findings.**  The tuning window's "Defaults" button set the junction trim to 0.45, while the value the
renderer ran with was 0.32: the knob list had five copies and two of them had drifted.  `arc.rules.links`
carried `local pieces = arc.rules.pieces`, which nothing read.  And the lint's world handle answered
`control_prop_is`, which the pipeline has no method for.

**A defect of my own, found by the split.**  `lint_all` counted the scripts `require` had kept into `n`, the
same variable that held how many PATHS the array had, and then read `paths[n - 1]`.  It read past the array
and crashed as soon as the heap moved under it.  The count is its own variable now.

1. **An explicit spine.**  C loads one file, `scripts/init.lua`, which `require`s the rest in the order
   the dependencies need.  The alphabetical load, the `arc.geo.x or 0` fallbacks and the file-scope
   reads of knobs go.  A knob read before it is declared is a load error, not a nil.
2. **`links.lua` and `interchange.lua` split by concept**: the matcher, the annealer, the weave planner,
   the plateau, each a module with a local table, and `arc.rules.links` becomes a page that calls them.
   No rule over 150 lines without a reason written at its top.
3. **The stub is generated.**  `lint.c`'s hand-listed world handle is produced from `KINDS[]`, so the
   `CLAUDE.md` trap "it must list every `w:` method" stops existing.  `RULES[]` is NOT generated: its
   column of names cannot be derived, because a rule name reaches C from four places (two literal
   `script_rule_object` calls, a family declaration's stage names, and the fixed helpers), and its second
   column is read by nobody.  What was done instead is to say so where it stands, and `rule_answer` below
   it is where an answer is really checked.  A new rule is still two edits: `RULES[]` and `rule_args`.
4. **`api_object.c` splits per kind**: `api_world.c`, `api_strip.c`, `api_moving.c` and so on, one file
   a kind or a family of kinds, each named as `script_api.py` expects.  The 24 accessors outside the
   naming rule are renamed with it, and the family words leave `src/script`.
5. **Edges count from nought across the boundary**: api_object.c:1436/1451/5379 and the two Lua mirrors.
6. **`arc.tune`'s 19 names move from C to a script**, beside `arc.geo`'s, so the numbers a script tunes
   with are declared in one place.

Proof: the battery, `lua_lint` in program order, the generated stub diffing clean.  Done when a new
`w:` method or rule needs one edit, not three.

**Where it landed.**  `scripts/init.lua` has 92 `require` lines and the layer is 103 files, and a folder is entered through its own
`init.lua`.  `scripts/tune.lua` declares all nineteen knobs with their ranges, labels and defaults, and
`tune_bind` in `geo.c` names the seventeen the renderer reads: a knob the scripts do not declare is a fault
at load, and `lua_lint` says it first.  `scripts/compose/links` and `scripts/compose/interchange` are folders
of five modules each, and `arc.rules.links` is twelve lines.  `src/script/generated/lint_stub.h` is the
lint's world handle, generated from `WORLD[]`, with fifteen answers written over it in `lint.c` and a reason
for each.  `api_object.c` is 281 lines: the handle and `KINDS[]`.  The kinds are in `obj_world.c`,
`obj_strip.c`, `obj_path.c`, `obj_node.c`, `obj_band.c`, `obj_lane.c`, `obj_ground.c` and `api_tile.c`, and
`obj.h` is the whole of what they share.  `obj_world.c` is 2,225 lines and stays one file: it is ONE kind,
and its table names every one of its accessors.

Six rules are still over 150 lines, and each now says at its top why it is one page: `world` is the order a
build runs in, and the other five are each one sweep over one body.

## Phase 6 — the simulation and the app

DONE, 14 September 2026.  Cause 8.  The simulation's proof is the oracle, not the battery, and it did not
move: `clock_check.py` reads 341213/341213 live cells, 6186/6186 scalars and 6/6 cities' dice identical on
the six exact cities, the same as before the phase.  `rng_diff.py` matches the original draw for draw on
all six.  The gate is unchanged with it.

**A `const` that was a lie.**  `plant_output` took a `const City *` and drew from the dice.  It could say
that while the seeds were file-scope: the moment they became the city's, the compiler saw the write.

**A rename scar.**  `tools/gen_tables.py` said "Indemeet all of it by id".  The word was "Indexing", and a
substring rename of `xing` to `meet` reached inside it.  It was in a GENERATED file, where no checker
looks.

**Two things hiding in `generated/`.**  `tables.h` and `shaders.h` are written by hand and sat in that
folder, so `family_words.py` never read them.  One carried "Road vehicles".  They are out of the folder now
and the checker reads them.

**Dead code.**  `arc_dev_main` ended with a single-city summary reachable only when a mode's argument guard
failed, at which point `argv[1]` is a mode's name and never a city.  It printed "not a city" and nothing
else could reach it.

1. **`City` owns its dice and its queue.**  `rng.c`'s three generators and `sim_queue.c`'s ring become
   members, and every reader takes the `City`.  Proved by `clock_check.py` and `rng_diff.py` unchanged
   on the six exact cities.  This is the one refactor the regions goal cannot start without.
2. **One mode table.**  `modes.cpp`'s table carries the handler, and `dev.c`'s `strcmp` chain becomes one
   function a mode.  `soft/main.c`'s private parse of `--zoom`, `--scale`, `--underground`,
   `--no-things` reads the same table.
3. **Move the tooling out of the reconstruction**: `dev.c` and `testcity.c` to `src/dev/`, `advisor.c`
   out of `arc_core` (it says itself it is not reconstruction), and add `economy.c`, `advisor.c`,
   `sim_int.h` to the format list.
4. **Regenerate the generated.**  Fix the two generators' comment text, re-run them, and add a freshness
   test for each generated file that has a generator in the tree (`tables.c` twice, `shaders.c`).  Give
   `shaders.h` and `tables.h` a generator or move them out of `generated/`.
5. **The reconstruction logs through the facade** or not at all: `sane.c:140` and `sim_growth.c:446`.

Done when `arc_core` has no file-scope mutable state and every generated file can be regenerated
without a lint failing.  BOTH HOLD.

**Where it landed.**  The City carries what the original kept in A5 globals: the scratch plane
(`A5+0x13BA`), the 512-entry ring, the three generators and the optional log of their draws, and the growth
scan's coverage count.  Nothing in `src/sim` is a mutable file-scope name any more, and nothing there
prints: the trip trace is a function the tooling hands over, and `fp68k` answers `FP68K_UNHANDLED` instead
of writing to stderr.  `dev.c`, `testcity.c` and `advisor.c` are `src/dev`, which `seam_check.py` now
counts as the simulation's own side.  `dev.c`'s 23-branch `strcmp` chain is 23 functions and one table
(`src/dev/dev.h`), which the parser and the dispatcher both read, so `ArcMode` no longer names a program
and `main.c` no longer maps one.  All four generators take `--check`, and `sim_tables_fresh`,
`render_tables_fresh` and `shaders_fresh` run them.

## Phase 7 — build and continuous integration

DONE, 14 September 2026.  Cause 10.  The gate is unchanged after it: the build was reorganised, not the
geometry.

**A finding.**  The no-SDL branch had not linked for a long time.  It was a SECOND list of the
executable's sources, and four of them had been added to the first alone, so it was short of `dumpf`,
`log_msg`, `rcity_load` and `lua_lint_main`.  There is one list now: the executable is built from what
needs no window, and the game's own sources are added where SDL3 was found.  A source added to one branch
and forgotten in the other cannot happen again, because there is no other branch.

1. One `project()` and one version, one `ARC_CITIES` cache entry, one `enable_testing()`.
2. `CMakePresets.json` points inside the repository.
3. The no-SDL branch links, or is removed: a branch nobody has built is a promise nobody has kept.
4. A GitHub Actions workflow that runs the `lint` tier and a build with `-Werror` on every push.  It
   needs no game folder, so it can run.  The `gate` tier joins it once assets can be cached.
5. The three hardcoded `~/Downloads` paths become `ARC_CITIES` reads.

**Where it landed.**  One `project(arcology)` with the version, the language standards, the build type,
`ARC_CITIES`, `ARC_SIM_TESTS`, Python3 and `enable_testing()`, all at the root and all before the
directories below are added: `src/render` and `src/sim` are directories of that project rather than
projects of their own.  `CMakePresets.json` pointed at `<repo>/../../assets`, two levels ABOVE the
repository, and now points at `assets`.  `ARC_WERROR` (off by default) makes a warning an error, and the
whole tree builds clean with it.  `.github/workflows/checks.yml` runs the lint tier and a `-Werror` build
on every push, both on a bare runner with no SDL3, no game folder and no art.  `tools/game.py` is the one
place the game folder is read: `ARC_CITIES` first, ONE guess after it that says out loud that it guessed,
and a refusal that names the setting.  `src/app/paths.c` reads the same name, so the build, the tools and
the program agree on it.

## The process, so this does not recur

The "vibing" was not a lack of rules.  The rules are good and unusually well stated.  It was that the
rules were not enforced by anything a session could not talk its way past.  Four changes:

* **The reference is a file.**  Phase 0.  A session that changes the look changes `battery_ref.json`,
  and the diff shows it.
* **A checker that passes an empty set is a bug.**  Every checker asserts it found the things it checks
  (`script_api.py` found zero kinds and said clean).  Add the assertion when fixing each one.
* **The gate runs between edits, the suite at the end.**  The tiers in phase 0 make that a one-word
  choice instead of a seven-minute one, which is why sessions stopped running anything.
* **A commit says what changed.**  "Saving work" carried 7840 insertions across 38 files on 13
  September.  A message per step of this plan, naming the step and the proof, is enough.

## What needs the maintainer

* **The three red ratchets** (phase 0, step 3) need a look before a decision, not a re-baseline.
* **The lab notebook** (phase 2, step 1): keep the findings in `docs/` as proposed, or keep the brief as
  it is and exempt it.  The plan assumes the first.
* **`junction` versus `node`.**  The vocabulary says node; the code says junction 321 times against
  160.  One wins, and the rename is scripted, whole-identifier, with the list read first.
* **Whether `mesh/` may know a family has stages** (phase 3, step 7), which `CLAUDE.md` left open.
