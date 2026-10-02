#!/usr/bin/env python3
"""Goal 6's witness: no path is built outside the shape primitives.

C is for rendering.  The chain of points a way runs between two poses is
built by a script and handed to `arc.fit`, and C never answers "the
points between these two poses".  A router in C fixes every movement as
one shape, and the look can then only be changed by compiling.

The shape of that fault is always the same: a `Piece` filled in by hand.
So this reads every C file under `src/render` and `src/script`, finds
what is declared a `Piece` there, and fails on any line that WRITES one
of a piece's geometry fields.

`src/render/mesh/` is exempt and says why: the fit and the piece
arithmetic ARE the primitives a script's chain is cut into, so a piece
is what they are for.

Anything else is on the list below, one entry a function, each with the
reason it is not a route.  Add to it only for something that is not a
chain between two poses, and say so.

    python3 tools/no_routing.py
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
READ = ("src/render", "src/script")
#  The primitives themselves: a piece is what they make.
EXEMPT_DIRS = ("src/render/mesh/",)

#  The geometry of a piece: where it starts, where it ends, the arc it
#  sweeps.  A write to one of these is a point somebody decided.
FIELDS = ("arc", "a", "b", "c", "r", "t0", "t1", "len")

#  A script's own chain arriving.  Under src/script a piece is READ off a
#  Lua table, field by field, which is the chain coming IN rather than one
#  being decided: the right-hand side is Lua's.
FROM_LUA = re.compile(r"=\s*[^=].*\b(?:lua_|api_field_)")

#  What is not a route, and why.  Keyed by the function that writes it.
ALLOWED = {
    ("src/render/walk/walk.c", "build_island"):
        "A LONE TILE, not a route between two poses.  An island is one cell no "
        "neighbor joins.  Its piece IS the cell: the middle, the axis the art "
        "links, and the family's own inset each side.  There is no second pose, "
        "and no path to choose between.",
    ("src/render/net/spur_build.c", "spur_reverse"):
        "TURNING A CHAIN ROUND, not building one.  An ON spur is built slab to "
        "line and then reversed, so each piece's two ends swap.  Every point is "
        "the script's own, in the other order.",
    ("src/render/net/table.c", "seg_table_pieces_from"):
        "THE SAME CHAIN, READ FROM THE OTHER END.  A segment is kept once and "
        "reached from either of its two nodes, so the copy a caller at the far "
        "node gets has each piece's ends swapped and the order turned round.",
}


def files():
    for top in READ:
        for root, _, names in os.walk(os.path.join(ROOT, top)):
            for n in sorted(names):
                if not n.endswith((".c", ".h")):
                    continue
                p = os.path.relpath(os.path.join(root, n), ROOT)
                if any(p.startswith(d) for d in EXEMPT_DIRS):
                    continue
                yield p


def strip_comments(text):
    return re.sub(r"/\*.*?\*/", lambda m: "\n" * m.group(0).count("\n"), text, flags=re.S)


def pieces_of(text):
    """The names declared a Piece in this file, plus the members of a
    struct that holds one: both are written the same way."""
    out = set()
    for m in re.finditer(r"\bPiece\s+\**\s*([A-Za-z_]\w*)", text):
        out.add(m.group(1))
    return out


def function_at(lines, i):
    """The name of the function line `i` sits in: the last line at column
    nought that opens one."""
    for k in range(i, -1, -1):
        m = re.match(r"^[A-Za-z_][\w \*]*?([A-Za-z_]\w*)\s*\([^;]*$", lines[k])
        if m:
            return m.group(1)
    return "?"


def main():
    bad = []
    for path in files():
        text = strip_comments(open(os.path.join(ROOT, path)).read())
        names = pieces_of(text)
        if not names:
            continue
        lines = text.split("\n")
        who = "|".join(re.escape(n) for n in sorted(names))
        fld = "|".join(FIELDS)
        #  name.field =, name[i].field =, name->field =
        rx = re.compile(r"\b(?:%s)\s*(?:\[[^\]]*\])?\s*(?:\.|->)\s*(?:%s)\s*=(?!=)" % (who, fld))
        for i, line in enumerate(lines):
            if not rx.search(line):
                continue
            if path.startswith("src/script/") and FROM_LUA.search(line):
                continue
            fn = function_at(lines, i)
            if (path, fn) in ALLOWED:
                continue
            bad.append((path, i + 1, fn, line.strip()))
    if not bad:
        print("no routing: every chain of points is a script's (goal 6)")
        return 0
    for path, n, fn, line in bad:
        print("%s:%d: %s builds a piece by hand: %s" % (path, n, fn, line[:90]))
    print()
    print("%d place(s) answer the points between two poses in C.  Goal 6 (CLAUDE.md):" % len(bad))
    print("the chain is the script's.  Queue the two poses with net_cut_add_poses and")
    print("take the pieces the drive cut, or say in tools/no_routing.py why it is no route.")
    return 1


if __name__ == "__main__":
    sys.exit(main())
