#!/usr/bin/env python3
"""The architecture's mechanical witnesses (THE ARCHITECTURE, in the house rules at the root).

Three rules, no build needed:

1.  ONE BUILD PATH.  The per-piece edit path (its prints, records,
    answers, skips, closure and the twin matching of the edit show) is
    gone, and no file may name it again.

2.  A FAULT IS NEVER ACCEPTED.  A tool that proves a property of a built
    world runs the renderer through tools/arcrun.py, never through
    subprocess itself.  PROOFS names them.  UNCONVERTED names the ones that
    still run it themselves, each with its reason, and that list may only
    shrink.

3.  THE NETWORK IS THE SOURCE.  A lane never joins or parts from another
    partway along: every merge, diverge and lane gained or dropped is a
    node of the lane graph, and every link runs from a lane's end to a
    lane's start.
    Every link is filed from the lane graph, never found by matching where
    two lanes touch.  PARTWAY counts the sites of the partway link by file
    and BY_PLACE the sites of linking by place.  A count may only fall,
    and the invariant holds when both are empty.

    No tuned number decides what the network holds.  SCRIPT_READS
    counts, by file, the reads of the store of tuned numbers and knobs, and
    the includes of its header, in the C that derives the network.  None is
    allowed: a number the network is derived by is a constant of that C.

    The network has no orientation.  It is a graph of cells, indexed by
    column and row, and nothing turns it: a turn of the view and the
    placing of a city map cells, they rotate nothing.  FRAME counts, by
    file, the compass words and the reads of a rotation in what derives,
    stores or designs the network.  TURNS counts the calls of a
    turn of the map by file, and none is allowed: the simulation never
    turns.  Both may only fall.

    python3 tools/architecture.py
"""
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)

#  The names of the per-piece edit path.
PIECE_PATH = ["incr_rec_", "incr_group", "incr_piece_", "incr_answer_", "incr_may_keep", "incr_closure",
              "incr_near", "incr_draw_near", "show_meet", "edit_pieces", "incr_print", "piece_was_skipped",
              "incr_editing"]

#  The tools that prove a property of a built world.
PROOFS = ["window_witness.py"]
#  Those that still run the renderer through subprocess, and why.
UNCONVERTED = {}

#  The partway link: a lane joined or parted from another partway along,
#  by the names such a link and a merge node partway along are written
#  with.
PARTWAY_NAMES = re.compile(r"\b(from_s|to_s|sides_link|merges_link|WAY_NODE_MERGE|link_add_at|ways_merge_missed)\b")
#  Its sites by file, as they stand.  A count may only fall.
PARTWAY = {}

#  A link found by matching where two lane ends touch, after the fit,
#  rather than filed from the lane graph.
BY_PLACE_NAMES = re.compile(r"\b(ends_link|ends_index)\b")
BY_PLACE = {}

#  The compass words and rotation reads in what derives, stores or
#  designs the network.  A count may only fall.
FRAME_NAMES = re.compile(r"\b(?:[Nn]orth|[Ss]outh|[Ee]ast|[Ww]est)(?:ward|ern|-south|-west)?\b|\brotat\w*|\bROT_\w+")
FRAME_SCOPE = ["src/network", "plans/network.md", "plans/network-status.md"]
FRAME = {}
TURN_NAMES = re.compile(r"\bsim_rotate\s*\(")
DERIVERS = ["src/network", "src/sim/net_cell.c", "src/sim/net_cell.h"]
SCRIPT_NAMES = re.compile(r"\b(?:geo_num|geo_get|geo_at|tune_at|TUNE)\s*\("
                          r"|#\s*include\s*\"geo\.h\"")
SCRIPT_READS = {}
TURNS = {}


SOURCES = (".c", ".h", ".cpp")


def sources():
    for top in ("src",):
        for dirpath, _, files in os.walk(os.path.join(ROOT, top)):
            if "vendor" in dirpath or "generated" in dirpath:
                continue
            for f in files:
                if f.endswith(SOURCES):
                    yield os.path.relpath(os.path.join(dirpath, f), ROOT)


def piece_sites():
    pat = re.compile("|".join(re.escape(n) for n in PIECE_PATH))
    counts = {}
    for path in sources():
        with open(os.path.join(ROOT, path), encoding="utf-8", errors="replace") as f:
            n = sum(len(pat.findall(line)) for line in f)
        if n:
            counts[path] = n
    return counts


def check_one_path():
    return ["%s: %d sites of the per-piece edit path" % (path, n) for path, n in sorted(piece_sites().items())]


def check_faults():
    bad = []
    for name in PROOFS:
        path = os.path.join(HERE, name)
        if not os.path.exists(path):
            continue
        with open(path, encoding="utf-8") as f:
            text = f.read()
        runs_itself = re.search(r"^\s*import subprocess|^\s*from subprocess", text, re.M)
        if runs_itself and name not in UNCONVERTED:
            bad.append("tools/%s runs the renderer through subprocess; a proof runs it through arcrun.world" % name)
        if not runs_itself and name in UNCONVERTED:
            bad.append("tools/%s no longer runs subprocess itself: take it out of UNCONVERTED" % name)
    return bad


def count(path, pat):
    with open(os.path.join(ROOT, path), encoding="utf-8", errors="replace") as f:
        return sum(len(pat.findall(line)) for line in f)


def ratchet(what, now, was):
    bad = []
    for path, n in sorted(now.items()):
        if n > was.get(path, 0):
            bad.append("%s: %d %s, %d allowed" % (path, n, what, was.get(path, 0)))
    for path, n in sorted(was.items()):
        if now.get(path, 0) < n:
            bad.append("%s: %d %s, down from %d: lower its count in tools/architecture.py" % (path, now.get(path, 0), what, n))
    return bad


def check_network_source():
    partway, by_place = {}, {}
    for path in sources():
        n = count(path, PARTWAY_NAMES)
        if n:
            partway[path] = n
        n = count(path, BY_PLACE_NAMES)
        if n:
            by_place[path] = n
    return ratchet("sites of a link partway along a lane", partway, PARTWAY) + \
        ratchet("sites of a link found by place", by_place, BY_PLACE)


def frame_files():
    for top in FRAME_SCOPE:
        full = os.path.join(ROOT, top)
        if os.path.isfile(full):
            yield top
        for dirpath, _, files in os.walk(full):
            for f in sorted(files):
                if f.endswith(SOURCES + (".md",)):
                    yield os.path.relpath(os.path.join(dirpath, f), ROOT)


def check_no_frame():
    frame, turns = {}, {}
    for path in frame_files():
        n = count(path, FRAME_NAMES)
        if n:
            frame[path] = n
    for path in sources():
        if path.endswith(".h"):
            continue
        n = count(path, TURN_NAMES)
        if n:
            turns[path] = n
    return ratchet("compass words and rotation reads in the network", frame, FRAME) + \
        ratchet("calls of the original's map turn", turns, TURNS)


def check_no_script_reads():
    reads = {}
    for top in DERIVERS:
        if os.path.isfile(os.path.join(ROOT, top)):
            n = count(top, SCRIPT_NAMES)
            if n:
                reads[top] = n
            continue
        for dirpath, _, files in os.walk(os.path.join(ROOT, top)):
            for f in sorted(files):
                if f.endswith(SOURCES):
                    path = os.path.relpath(os.path.join(dirpath, f), ROOT)
                    n = count(path, SCRIPT_NAMES)
                    if n:
                        reads[path] = n
    return ratchet("reads of a tuned number, in the C that derives the network", reads, SCRIPT_READS)


def main(argv):
    bad = check_one_path() + check_faults() + check_network_source() + check_no_frame() + check_no_script_reads()
    for b in bad:
        print(b)
    if bad:
        print("\n%d breach(es) of THE ARCHITECTURE, in the house rules at the root." % len(bad))
        return 1
    print("architecture: the witnesses hold")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
