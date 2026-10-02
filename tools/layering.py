#!/usr/bin/env python3
"""layering.py -- the renderer's four levels, held by their includes.

    python3 tools/layering.py            # the rule, and every include that breaks it
    python3 tools/layering.py --graph    # the include edges between the levels, counted

The renderer is four levels, and an include may only run DOWN them:

    render     build.c, incr.c, geo.c, pipeline.h: runs a build, and the vocabulary
    walk       a segment's stages                   may include net, mesh, render, util
    net        the stores and the plumbing          may include mesh, render, util
    mesh       the shape primitives                 may include render (the vocabulary), util, atlas

Neither `walk/` nor `net/` may read the ART at all.  What a picture
means is decided in a script (arc.art, scripts/art_tiles.lua) and
pushed down as a table, so nothing there has a sprite to look at.

`src/script` is the door between the scripts and the renderer: it may
include anything under the renderer, and nothing under the renderer may
include it except through the one door named below.  `gpu/`, `soft/`
and `atlas/` draw the frame and are outside the four levels: they may
include mesh and the top, and nothing includes them but the top and the
app.

Every include that runs the wrong way is listed in EXCEPTIONS with the
reason it is still there.  An include the list does not name fails, and
so does a listed include that has gone, so the list and the tree cannot
drift apart.  Taking a line off the list is what progress looks like.
"""
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
RENDER = os.path.join(ROOT, "src", "render")
SCRIPT = os.path.join(ROOT, "src", "script")

#  What each level may include.  A level not listed as a key is outside
#  the rule (gpu, soft, atlas, generated).
MAY = {
    "render": {"render", "walk", "net", "mesh", "util", "gpu", "soft", "atlas", "generated", "sim", "script", "vendor"},
    "walk":   {"walk", "net", "mesh", "render", "util"},
    "net":    {"net", "mesh", "render", "util"},
    "mesh":   {"mesh", "render", "util", "generated", "atlas"},
    "gpu":    {"gpu", "mesh", "render", "util", "generated", "atlas", "soft", "script"},
    "soft":   {"soft", "render", "util", "generated", "atlas", "vendor"},
    "atlas":  {"atlas", "render", "util", "generated", "vendor"},
    "script": {"script", "render", "walk", "net", "mesh", "util", "generated", "atlas", "lua"},
}

#  (from file, included header) -> why it is still there.  Paths are
#  relative to src/.
EXCEPTIONS = {
    # mesh/ reaching up into net/: each one a name, and the design question
    # whether mesh/ should know a family has stages is a design question,
    # not a file move
    ("render/mesh/loft.c", "net/family.h"): "the loft asks a family which stages it has, and its rules",
    ("render/mesh/loft.c", "net/margin.h"): "the loft asks whether a margin stands beside it",
    ("render/mesh/loft.c", "net/strip.h"): "the loft reads the slab's width factor and readiness",
    ("render/mesh/loft.c", "net/table.h"): "the loft reads and keeps the station cache",
    ("render/mesh/loft.c", "net/cell.h"): "the loft reads whether a tile is lapped",
    ("render/mesh/mesh.c", "net/shelf.h"): "the ground fan says whether a tile is a corridor's",
    ("render/mesh/shapes.c", "net/meet.h"): "the lap panel is drawn on a meet",
    ("render/mesh/shapes.c", "net/cell.h"): "the lap panel reads whether a tile is lapped",
    ("render/mesh/surface.c", "net/margin.h"): "the drawn ground reads a family's rules",
    ("render/mesh/tile.c", "net/shelf.h"): "the ground is cut to the corridor shelves",
    ("render/mesh/check.c", "net/cell.h"): "the checks read the city the piece scan saw",
    ("render/mesh/check.c", "net/network.h"): "the checks read which tiles a line serves",
    # mesh/ reaching up into the script API
    ("render/mesh/emit.c", "script.h"): "the emitter reads the incremental key's script-side data",
    ("render/mesh/fit.c", "script.h"): "the fit names the rule that cuts it",
    ("render/mesh/loft.c", "script.h"): "the loft reads pushed data",
    ("render/mesh/check.c", "script.h"): "the checks read which bytes owe geometry",
    ("render/mesh/mesh.c", "script.h"): "the driver reads the tints and the meshed tiles",
    ("render/mesh/model.c", "script.h"): "a model is a script's",
    ("render/mesh/piece.c", "script.h"): "the piece scan reads a byte set",
    ("render/mesh/surface.c", "script.h"): "the ground reads a byte set",
    ("render/mesh/tile.c", "script.h"): "the ground reads the meshed tiles",
    # net/ reaching sideways into walk/: the cycle the plan's phase 3 breaks
    # net/ and walk/ reaching into the script API: pushed data and the one door
    ("render/net/box.c", "script.h"): "reads pushed data",
    ("render/net/drive.c", "script.h"): "THE ONE DOOR: arc.rules.frame",
    ("render/net/family.c", "script.h"): "reads a family declaration",
    ("render/net/lane.c", "script.h"): "reads pushed data",
    ("render/net/margin.c", "script.h"): "reads pushed data",
    ("render/net/meet.c", "script.h"): "reads pushed data",
    ("render/net/meet.h", "script.h"): "the meet's readings are script records",
    ("render/net/margin.h", "script.h"): "the margin's readings are script records",
    ("render/net/node.c", "script.h"): "reads pushed data",
    ("render/net/strip.c", "script.h"): "reads pushed data",
    ("render/net/traffic.c", "script.h"): "reads pushed data",
    ("render/net/band.c", "script.h"): "reads pushed data",
    ("render/net/band_walk.c", "script.h"): "reads pushed data",
    ("render/net/report.c", "script.h"): "reads pushed data",
    ("render/net/shelf.c", "script.h"): "reads pushed data",
    ("render/net/spur_build.c", "script.h"): "reads pushed data",
    ("render/net/station.c", "script.h"): "reads pushed data",
    ("render/net/cell.c", "script.h"): "the piece tables are byte sets the script pushed",
    ("render/net/walkway.c", "script.h"): "reads pushed data",
    ("render/walk/cursor.c", "script.h"): "reads pushed data",
    ("render/walk/walk.c", "script.h"): "reads pushed data",
}


def level_of(rel):
    """The level a file under src/ belongs to, or None outside the rule."""
    parts = rel.split("/")
    if parts[0] == "script":
        return "script"
    if parts[0] != "render":
        return None
    if len(parts) == 2:
        return "render"
    return parts[1]


def level_of_include(rel_from, inc):
    """The level an include names, resolved as the compiler would."""
    if inc.startswith("../app/"):
        return "app"
    if inc.startswith("../util/") or inc in ("log.h", "dump.h", "opt.h", "fs.h"):
        return "util"
    if inc in ("script.h",):
        return "script"
    if inc in ("sim.h", "arco.h"):
        return "sim"
    if inc.startswith(("lua", "lauxlib")):
        return "lua"
    head = inc.split("/")[0]
    if head in ("mesh", "net", "walk", "gpu", "soft", "atlas", "generated"):
        return head
    if inc in ("materials.h", "shaders.h", "tables.h"):
        return "generated"
    if inc in ("lodepng.h", "jsmn.h", "CLI11.hpp"):
        return "vendor"
    if inc == "internal.h":
        lvl = level_of(rel_from)
        return lvl if lvl else "render"
    #  a bare name: the same directory as the includer, or the top
    same = os.path.join(os.path.dirname(os.path.join(ROOT, "src", rel_from)), inc)
    if os.path.exists(same):
        return level_of(rel_from) or "render"
    return "render"


def edges():
    out = []
    for base in (RENDER, SCRIPT):
        for dirpath, dirnames, names in os.walk(base):
            dirnames[:] = [d for d in dirnames if d not in ("generated", "shaders")]
            for n in sorted(names):
                if not n.endswith((".c", ".h", ".cpp")):
                    continue
                p = os.path.join(dirpath, n)
                rel = os.path.relpath(p, os.path.join(ROOT, "src"))
                for i, line in enumerate(open(p, errors="replace"), 1):
                    m = re.match(r'\s*#include\s+"([^"]+)"', line)
                    if m:
                        out.append((rel, i, m.group(1)))
    return out


def main():
    graph = "--graph" in sys.argv
    counts, bad, seen = {}, [], set()
    for rel, line, inc in edges():
        frm = level_of(rel)
        to = level_of_include(rel, inc)
        counts[(frm, to)] = counts.get((frm, to), 0) + 1
        if frm is None or frm not in MAY:
            continue
        if to in MAY[frm]:
            continue
        key = (rel, inc)
        seen.add(key)
        if key in EXCEPTIONS:
            continue
        bad.append("%s:%d includes %s (%s -> %s): not allowed and not on the list" % (rel, line, inc, frm, to))
    for key in sorted(set(EXCEPTIONS) - seen):
        bad.append("%s no longer includes %s; take it off the list" % key)
    if graph:
        for (a, b), n in sorted(counts.items(), key=lambda kv: (str(kv[0][0]), str(kv[0][1]))):
            print("%4d  %-8s -> %s" % (n, a, b))
        print()
    if bad:
        print("layering: %d include%s run the wrong way and are not on the list:" % (len(bad), "" if len(bad) == 1 else "s"))
        for b in bad:
            print("   ", b)
        return 1
    print("layering: every include runs down the levels, or is on the list of %d exceptions" % len(EXCEPTIONS))
    return 0


if __name__ == "__main__":
    sys.exit(main())
