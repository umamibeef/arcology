#!/usr/bin/env python3
"""Check that .arco carries a city without losing anything.

Not "does the round trip reproduce the original file" -- the .sc2 writer
already answers that, and its RLE differs from the 1995 encoder on most
cities.  The question here is narrower and is the one that matters for a
new format: does going THROUGH .arco change the answer?

    city.sc2 -> city.sc2          the baseline the .sc2 writer produces
    city.sc2 -> .arco -> city.sc2 the same trip with .arco in the middle

The two must be byte-identical.  If they are, .arco carried everything
the .sc2 writer knew how to write, which is everything the City struct
holds.

usage: arco_check.py [<city>...]        (default: every city in cities/)
"""
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BIN = ROOT / "build" / "arcology"


def convert(src, dst):
    r = subprocess.run([str(BIN), "--convert", str(src), str(dst)],
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    return r.returncode == 0


def main():
    if not BIN.exists():
        raise SystemExit("build it first: cmake --build build")
    args = sys.argv[1:]
    cities = [Path(a) for a in args] if args else sorted(
        (ROOT / "cities").glob("*.sc2"))
    if not cities:
        raise SystemExit("no cities to check")

    tmp = Path(tempfile.mkdtemp())
    same = bad = 0
    for c in cities:
        direct, arco, via = tmp / "d.sc2", tmp / "w.arco", tmp / "v.sc2"
        if not (convert(c, direct) and convert(c, arco) and convert(arco, via)):
            print("  %-24s conversion failed" % c.name)
            bad += 1
            continue
        if direct.read_bytes() == via.read_bytes():
            same += 1
        else:
            bad += 1
            print("  %-24s .arco lost something" % c.name)
    print("arco round-trip: %d / %d carry everything the .sc2 writer knows"
          % (same, same + bad))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
