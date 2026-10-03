# Arcology — what is open

Everything here is open.  Finished work is not repeated: the rules are in `CLAUDE.md`, the facts in
`docs/`, and the refactor in progress in `REFACTOR.md`, phase by phase.  The proofs are
`python3 tools/battery.py` against `tools/battery_ref.json` and `ctest -L gate`.

## What the battery refuses

`python3 tools/battery.py` fails on this tree, and the faults are the interchange work in flight:

- `hi-loh20`, `london` and `simcity` build, where the reference has them crashing: the three carry a band's
  head-on END TILE, which used to leave the spur in hand stale and send `spur_route` at a null band record.
  `tools/battery_ref.json` still records the crash, so the battery reports the three as differing until somebody
  accepts them.
- `keith` builds in 4.5 seconds, and no node lays a crossing under the least gap.  Two guards in
  `compose/links/census.lua` hold an end to the node it is filed at.  It must REACH the node
  (`arc.geo.interchange_catchment`, 4 tiles to the nearest cell of the glob), and the grading pass must have
  settled a height for it (`arc.arm_z`).  An end that fails either is filed nowhere and the band ends there.

  Both guards read a store that is WRONG, and neither repairs it.  `arc.poses.band_end` names a node an end
  is nowhere near, and `arc.arm_z` has no height at all for 30 of keith's lane ends while every other city in
  `cities/` has none missing.  `scripts/compose/poses.lua` and `compose/interchange/arms.lua` fill those two.
  The suspect they share is `(which == 1) == (off > 0.0)`, the test for WHICH END OF A BAND an open lane end
  is, written out three times: `census.lua`, `node.lua` and `anneal.lua`.  Where that test is wrong all three
  read the far end of the band.  A city where a store is wrong now loses the movement instead of misplacing
  it, so the guards hide the fault they work around.

  Four nodes report an arm with nothing to join (86,80 / 42,88 / 60,90 / 88,102), which does not stop the
  build.

- `flint` has an on-spur lane under the terrain on three tiles.
- `tokyo`, `babar` and `manhattan` carry more colliding face pairs than the commit before.

## Editing a city: the player's tools

The action layer is `src/sim/sim_tool.c`: a gesture is begun, stepped from tile to tile and ended, and the app
drives it from the mouse (`src/app/uiglue.c`) or from `--gesture`.  `tools/tool_check.py` runs the original's
own tool loop under the interpreter with a scripted mouse and compares every layer, the funds, the census, the
military tallies and the transit count.  The bulldozer's Demolish and Raise, and the five network fitters and
the water edge fitter they share, are exact on every city in `cities/`.

Not yet reconstructed, from the decoding of `$2512` (the cost table is `TOOL_COST`, A5+0x616):

- **The bulldozer's other sub-tools:** Level `$8008`, Lower `$8B04`, De-zone `$1E782`.
- **Zones** `$1E782` with its rectangle drag `$1DAB2`, and Ports through the same routine.
- **Buildings** `$3212`: one routine for every power plant, pump, school, park and reward, with the NIMBY roll,
  the department counts and the power and water reflood after it.
- **The line tools:** the drag `$1DDFE`, the walks `$1E046` and `$1E2EC`, and the per-tile placers road `$1F4AC`,
  rail `$1F9DA`, power `$1F196`, pipe `$1FEB0` and subway `$20252`.  Bridges `$1A820`, and the map-edge links
  that write XTXT `$FA`.
- **Highway** `$1BF18`, **tunnel** `$20622`, **onramp** `$3B98`, **sub to rail** `$3E9C`, hydro `$3972`.
- **Landscape** trees `$2C86` and water `$2EF0`, the terrain editor's tools under `$78F6`, dispatch
  `$11466`..`$11736`, and signs `$F8A0`.
- **The interface:** the drag preview for lines and rectangles, the thirty tool cursors (`CURS` 1000..1029), the
  sub-tool availability masks at A5+0x5CE, and a repeating bulldozer sound.

What an edit shows (`src/render/show.c`, `scripts/compose/edit_show.lua`): the pieces near the edit sink and
dissolve while the world is rebuilt, and the pieces the build changed rise back, both as a wave out from the
edited tiles.  The look is the `arc.geo.edit_show_*` knobs and the rule; it is unjudged in a window.  Open:

- The build itself (about 280 ms) still freezes input and the clock; only the pieces move.  The going wave's tail
  after the build, and the coming wave, run in ordinary frames.  `--show-edits --show-at S --shot` draws the
  pieces as they stand S seconds into the going wave, which is how the look is checked without a window.
- A one-tile bulldoze changes the line class of strips 30 to 50 tiles away (seen as `nrm.w` 1 before, 0 after on
  `line strip 60,88 to 65,88` in Atlanta).  Either the class reads something the edit moves, or the first build
  read it stale.
- The family-word checker reads `.glsl` but not `.vert`/`.frag`, and the terrain shaders' text carries the words.

## Performance

- The live window builds on a thread of its own (`src/app/world.c`, `remesh_begin`), so frames, the
  simulation and the edit show go on during a build.  The cars stand still while it runs and are
  respawned after it (`traffic_init`), which shows as a jump.
- Cars, trains and signals are rebuilt as triangles on the CPU every frame (`traffic_build`), about
  1 ms a frame.  GPU instancing would draw them from models uploaded once; it changes what the
  battery's traffic digest hashes, so the reference moves with it.
- The simulation is polled once a frame (`step_clock`): at most one phase a frame at speeds 2 to 4,
  and speed 5 spends 4 ms of every frame.  It has no file-scope state and could run on a thread.
- `static_analysis` wants the build configured with `-DCMAKE_EXPORT_COMPILE_COMMANDS=ON`.

## The highway interchange

- The pad over a 2x2 block is a slab with no filleted turn geometry.  Two turns a block, and "straightest
  first" is a weak rule for which lanes turn; a four-arm block wants more than two.
- A band end that finds neither a line nor another band's lane is left open.
- The annealer in `scripts/compose/links/anneal.lua` and the weave planner in `scripts/compose/interchange/weave.lua` are
  the two largest rules in the tree; the refactor plan's phase 5 splits them by concept.

## Network geometry

- Broken segments to zero: the battery's `corners` section counts the hard corners and the segments
  without geometry in every city, and the corpus is not at zero.
- Head-on spurs across the corpus.
- The profile and the grade ceiling: what a corridor is allowed to do to the ground it crosses.
- Roundabouts where a junction suits one.
- On-spur sprites must take the right art in every rotation, in sprite mode.
- Line and band build tools on the test city.

## The mesh inspector

Built and unproven: it can only be judged by pointing at things with the app up (`--inspect` or ⌘I).  The
outline is a welded silhouette that carries its own height; the pick walks the projection ray; one spur
highlights per hover; furniture is pointable; the `generated by` row names the function that built the
component.

## Scale

The hundred-times-larger world.  Chunked builds, the rebuild after an edit and the station cache in the
segment table are done.  Next: bands through the cache, and tighter band closure.  The ground is never
cached.

## What the battery costs

A whole `python3 tools/battery.py` is about forty seconds on eighteen cores, and the corner sweep is most
of it: a full mesh build and watertightness check on all 102 cities in `cities/`.  The rule watchdog
(`api_rule_watch`, `src/script/script.c`) traps on a Lua CALL rather than counting instructions, so the VM
keeps its fast path.

Still open: the gate builds the world once per shot.  A multi-shot gate that builds once and moves the
camera is worth about eleven seconds of the twenty a `--gate` run takes.

## Loose ends

- **The city's name carries its Pascal length byte.**  CNAM is copied raw into the view's `name`, so the
  title shows a control character before the name (`adapt.c`, `uiglue.c`, `soft/main.c`).
- **Three-finger drag** on macOS does not pan the map.  Needs a session with `--input-log` to see what the
  trackpad sends.
- **`src/dev/dev.c`** carries the developer modes' `printf` calls outside the ban's fence; the refactor
  plan's phase 6 moves the modes out of the reconstruction.
- **The docs' preview pages** need the game folder to render their pictures (`tools/gen_previews.py`); a
  build without them stands a note in for each picture.
