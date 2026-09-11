#!/usr/bin/env python3
"""The pipeline knows no families.

C holds the primitives and the accounting.  WHAT is drawn -- a road, a
railway, a raised band, a power line, and everything those imply: a
deck, a ramp, a sidewalk, a level crossing -- is the scripts'.  So none
of those words may appear under the pipeline: not in a filename, not in
an identifier, not in a comment.

The generic vocabulary they are written in instead:

    a family        one kind of line and everything about how it is drawn
    a line, a strip what a family lays along a run of cells
    a band          a line two cells wide, whose spine runs on the seam
    a node          where three or four lines meet, and the box it draws
    a spur          a short line joining a band to a line below it
    a margin        the band beside a line, and the kerb along its inside
    a meeting       two families sharing one cell
    a way across    a marked path over a line at a node's mouth
    a surface       what a node lays inside its outline

Two areas are exempt and say why:

  src/render/soft   reproduces the ORIGINAL's tile renderer, address by
                    address.  Its subject is the 1995 game's own drawing,
                    the way src/sim's subject is the 68k simulation, and
                    naming what the original draws is describing the
                    material rather than deciding anything.
  src/render/generated  is generated from the game's data.

    python3 tools/family_words.py            # the whole pipeline
    python3 tools/family_words.py --list     # every line, to work from
"""
import argparse
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
TREE = os.path.join(ROOT, "src", "render")

EXEMPT = ("soft", "generated")

WORDS = [
    "road", "roads", "roadway", "roadways",
    "rail", "rails", "railway", "railways",
    "hiway", "hiways", "highway", "highways",
    "deck", "decks", "ramp", "ramps", "onramp", "onramps",
    "sidewalk", "sidewalks", "footway", "footways", "pavement", "pavements",
    "kerb", "kerbs", "curb", "curbs", "verge", "verges",
    "crossing", "crossings", "crosswalk", "crosswalks",
    "track", "tracks", "asphalt", "carriageway", "carriageways",
    "tarmac", "motorway", "motorways", "freeway", "freeways",
    "F_ROAD", "F_RAIL", "F_HIWAY", "F_POWER",
]
#  A family word stands alone, or sits inside a snake_case name:
#  `node_control` in a comment is the pipeline naming a family too.
PAT = re.compile(r"(?<![A-Za-z])(" + "|".join(WORDS) + r")(?![A-Za-z])", re.I)

#  The files still to convert.  A file leaves this list when it holds not
#  one of the words above, and it may never go back on it.
ALLOW = set()  # every file is clean: a word coming back is a fault

#  A family word also gets in ABBREVIATED, where the list above cannot
#  see it.  `hw` is the one that did: `HwSpur`, `s_hw_st`, `HW_MAX_ST`
#  and `s_hwfit_q` all named the same family in two letters.
#
#  A bare `hw` is a HALF WIDTH, which every fit and loft takes, so the
#  test is the SHAPE of the name rather than the letters.  An identifier
#  of one part named `hw` is a half width and passes.  A part of a longer
#  name that starts with `hw` is the family and is a fault.
IDENT = re.compile(r"[A-Za-z_][A-Za-z0-9_]*")
HALF_WIDTH = {"hw", "hw_end", "SPUR_HW"}  # the half widths, each read as one


def parts(name):
    """The name in its parts, over both underscores and case changes."""
    out = []
    for chunk in name.split("_"):
        out += re.findall(r"[A-Z]+(?![a-z])|[A-Z][a-z0-9]*|[a-z0-9]+", chunk) or [chunk]
    return out


def abbreviated(name):
    """True where a part of a longer name abbreviates a family."""
    if name in HALF_WIDTH:
        return False
    p = parts(name)
    return len(p) > 1 and any(q.lower().startswith("hw") for q in p)


def files():
    for dirpath, dirnames, names in os.walk(TREE):
        dirnames[:] = [d for d in dirnames if d not in EXEMPT]
        for n in sorted(names):
            if n.endswith((".c", ".h", ".cpp", ".glsl")):
                yield os.path.relpath(os.path.join(dirpath, n), TREE)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--list", action="store_true")
    a = ap.parse_args()
    bad_name, counts, clean_but_listed, nfiles = [], {}, [], 0
    for rel in files():
        nfiles += 1
        base = os.path.basename(rel)
        if PAT.search(base) or abbreviated(os.path.splitext(base)[0]):
            bad_name.append(rel)
        n = 0
        with open(os.path.join(TREE, rel), errors="replace") as f:
            for i, line in enumerate(f, 1):
                hits = [m.group(0) for m in PAT.finditer(line)]
                hits += [t for t in IDENT.findall(line) if abbreviated(t)]
                n += len(hits)
                if hits and a.list and rel not in ALLOW:
                    print("%s:%d: %s" % (rel, i, line.rstrip()[:120]))
        if n:
            counts[rel] = n
        elif rel in ALLOW:
            clean_but_listed.append(rel)

    live = {k: v for k, v in counts.items() if k not in ALLOW}
    total = sum(counts.values())
    if not total and not bad_name:
        print("family words: none, in %d files: the pipeline names no family" % nfiles)
        return 0
    print("family words: %d in %d files" % (total, len(counts)))
    for rel in sorted(counts, key=lambda k: -counts[k])[:12]:
        print("  %-24s %4d" % (rel, counts[rel]))
    if clean_but_listed:
        print("family words: these are clean and may leave ALLOW: "
              + ", ".join(sorted(clean_but_listed)))
    if bad_name:
        print("family words: a FILENAME names a family: " + ", ".join(bad_name))
    if live:
        print("family words: a file not in ALLOW carries them: "
              + ", ".join("%s (%d)" % (k, v) for k, v in sorted(live.items())))
    return 1 if (bad_name or live) else 0


if __name__ == "__main__":
    sys.exit(main())
