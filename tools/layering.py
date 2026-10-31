#!/usr/bin/env python3
"""layering.py -- the levels of the tree, held by their includes.

    python3 tools/layering.py            # the rule, and every include that breaks it
    python3 tools/layering.py --graph    # the include edges between the levels, counted

Every directory under src/ is a level, and an include may only run DOWN
the levels:

    app        the game: the loop, the clock, the world, the glue    may include any level
    dev        the developer modes                                   may include render and below
    render     build.c, compose.c, incr.c, geo.c: runs a build
      draw     the drawing of the shaped network                     may include mesh, render, network, cells
      mesh     the shape primitives                                  may include render, cells, atlas
      gpu, soft, atlas, generated: the frame, beside the three
    network    the road network, derived from the cells              may include cells
    cells      the city's cells, as the network and the renderer read them
    sim        the simulation                                        includes nothing above it
    ui, audio  the interface and the sound, each behind a C face     may include util
    util       the log, the dump, the options

The top's headers that every renderer level reads, geo.h and material.h
among them, count as `render`, so a level under it may name them.
`network/` and `draw/` cannot read the ART, so nothing there has a sprite
to look at.  A directory's own `internal.h` is what its own files share:
no file outside that directory includes it.

An include is resolved as the compiler resolves it: the includer's own
directory first, then the build's include path.  Every include that runs
the wrong way is listed in EXCEPTIONS with the reason it is still there.
An include the list does not name fails, and so does a listed include
that has gone, so the list and the tree cannot drift apart.
"""
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
SRC = os.path.join(ROOT, "src")

#  The build's include path, under src/, in the order the compiler reads it.
PATH = ["render", "cells", "util", "audio", "ui", "app", "render/generated", "sim", "", "dev", "sim/generated"]

#  The renderer's own levels, under src/render.
RENDER_SUB = {"draw", "mesh", "gpu", "soft", "atlas", "generated"}

RENDERER = {"render", "draw", "mesh", "gpu", "soft", "atlas", "generated"}

#  What each level may include, besides itself.
MAY = {
    "util":      set(),
    "sim":       set(),
    "cells":     {"sim", "util"},
    "network":   {"cells", "util"},
    "mesh":      {"render", "cells", "util", "generated", "atlas"},
    "draw":      {"mesh", "render", "network", "cells", "util", "generated"},
    "render":    {"draw", "mesh", "network", "cells", "util", "gpu", "soft", "atlas", "generated"},
    "gpu":       {"mesh", "render", "util", "generated", "atlas", "soft"},
    "soft":      {"render", "cells", "util", "generated", "atlas"},
    "atlas":     {"render", "util", "generated"},
    "generated": {"render"},
    "ui":        {"util"},
    "audio":     {"util"},
    "dev":       RENDERER | {"network", "cells", "sim", "util"},
    "app":       RENDERER | {"network", "cells", "sim", "util", "dev", "ui", "audio"},
}

#  (from file, included header) -> why it is still there.  Paths are
#  relative to src/.
EXCEPTIONS = {
}


def level_of(path):
    """The level a file under src/ belongs to."""
    rel = os.path.relpath(path, SRC).split(os.sep)
    if rel[0] == "render" and len(rel) > 2 and rel[1] in RENDER_SUB:
        return rel[1]
    return rel[0]


def resolve(frm, inc):
    """The file an include names, as the compiler finds it, or None for a
    header outside the tree (a vendor's, a system's or a generated one)."""
    for d in [os.path.dirname(frm)] + [os.path.join(SRC, x) for x in PATH]:
        p = os.path.normpath(os.path.join(d, inc))
        if os.path.isfile(p):
            return p
    return None


def edges():
    for dirpath, dirnames, names in os.walk(SRC):
        dirnames[:] = [d for d in dirnames if d not in ("vendor", "shaders")]
        for n in sorted(names):
            if not n.endswith((".c", ".h", ".cpp", ".hpp")):
                continue
            p = os.path.join(dirpath, n)
            for i, line in enumerate(open(p, errors="replace"), 1):
                m = re.match(r'\s*#include\s+"([^"]+)"', line)
                if m:
                    q = resolve(p, m.group(1))
                    if q:
                        yield p, i, m.group(1), q


def main():
    graph = "--graph" in sys.argv
    counts, bad, seen = {}, [], set()
    for p, line, inc, q in edges():
        frm, to = level_of(p), level_of(q)
        rel = os.path.relpath(p, SRC)
        counts[(frm, to)] = counts.get((frm, to), 0) + 1
        why = None
        if os.path.basename(q).startswith("internal.") and os.path.dirname(q) != os.path.dirname(p):
            why = "another directory's internal header"
        elif frm != to and to not in MAY.get(frm, set()):
            why = "%s -> %s" % (frm, to)
        if not why:
            continue
        key = (rel, inc)
        seen.add(key)
        if key in EXCEPTIONS:
            continue
        bad.append("%s:%d includes %s (%s): not allowed and not on the list" % (rel, line, inc, why))
    for key in sorted(set(EXCEPTIONS) - seen):
        bad.append("%s no longer includes %s; take it off the list" % key)
    if graph:
        for (a, b), n in sorted(counts.items()):
            if a != b:
                print("%4d  %-9s -> %s" % (n, a, b))
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
