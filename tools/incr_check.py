#!/usr/bin/env python3
"""incr_check.py -- an incremental rebuild draws what a full build draws.

An edit changes a few tiles, and the mesh is kept in chunks, so only the
chunks whose geometry changed are built again.  Which those are is a
CLOSURE over the tables the grading pass leaves behind -- a prediction,
made before the building pass runs.  A closure that predicts too little
leaves stale triangles in the chunks it did not rebuild, and nothing in
the build says so: the mesh is sound, the counts are plausible, and the
city is simply wrong in the places the edit reached and the closure did
not.

So every edit is built twice, once incrementally and once with
--no-incr, and the two are compared as a HASH OF THE SORTED TILE DUMP --
sorted, so it does not matter what order the triangles came out in.  The
check lines are compared as well, since a closure can be wrong in what
it records rather than in what it draws.

    tools/incr_check.py [--binary build/arcology] [--city atlanta]
"""
import argparse
import hashlib
import os
import re
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)

#  Five places on the map, chosen to reach different work: a junction, a
#  highway band, a ramp's foot, a railway, and open ground away from any
#  of it.
EDITS = ["66,113", "72,88", "90,81", "105,84", "20,20"]

CHECK = re.compile(r"^(mesh check|road clip|lanes|sidewalks|on-ramps|tangent|chunks|road pieces)")


def build(binary, city, out, extra=()):
    """One build, dumping every triangle by tile.  Answers its lines."""
    cmd = [binary, os.path.join(ROOT, "cities", city + ".sc2"), "--mute",
           "--mesh-check", "--tile-dump", "all", "--dump-to", out] + list(extra)
    subprocess.run(cmd, capture_output=True, text=True, cwd=ROOT)
    with open(out, errors="replace") as f:
        return f.read().splitlines()


def digest(lines):
    """The mesh, order-independent, and the check lines beside it."""
    tris = sorted(l for l in lines if l.startswith("tri "))
    checks = [l for l in lines if CHECK.match(l)]
    return (hashlib.md5("\n".join(tris).encode()).hexdigest()[:12],
            hashlib.md5("\n".join(checks).encode()).hexdigest()[:8],
            len(tris))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--binary", default=os.path.join(ROOT, "build", "arcology"))
    ap.add_argument("--city", default="atlanta")
    a = ap.parse_args()
    if not os.path.exists(a.binary):
        print("incr: no binary at", a.binary)
        return 1
    if not os.path.exists(os.path.join(ROOT, "cities", a.city + ".sc2")):
        print("incr: no city", a.city)
        return 0

    bad = 0
    with tempfile.TemporaryDirectory() as tmp:
        one, two = os.path.join(tmp, "a.txt"), os.path.join(tmp, "b.txt")
        full = digest(build(a.binary, a.city, one))
        if full[2] == 0:
            print("incr: the plain build drew nothing; the mesh check is what to read first")
            return 1
        for e in EDITS:
            inc = digest(build(a.binary, a.city, one, ("--edit", e)))
            all_ = digest(build(a.binary, a.city, two, ("--edit", e, "--no-incr")))
            if inc == all_:
                continue
            bad += 1
            print("incr: the edit at %s does not build the same both ways" % e)
            if inc[0] != all_[0]:
                print("      the mesh differs: %s incrementally, %s whole (%d and %d triangles)"
                      % (inc[0], all_[0], inc[2], all_[2]))
                print("      the closure predicted too few chunks: what it left out kept")
                print("      the triangles the last build put there")
            if inc[1] != all_[1]:
                print("      the check lines differ: %s incrementally, %s whole" % (inc[1], all_[1]))
    if bad:
        return 1
    print("incr: %d edits on %s build the same incrementally as whole, mesh and checks"
          % (len(EDITS), a.city))
    return 0


if __name__ == "__main__":
    sys.exit(main())
