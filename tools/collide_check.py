#!/usr/bin/env python3
"""Geometry that COLLIDES -- a RATCHET.

mesh/check.c counts pairs of faces from different shapes that pass
through one another: one surface driven through another, which no
painter's order can separate.  The corpus is not at zero, so this holds
the counts where they are and fails when one RISES.

    python3 tools/collide_check.py [--binary PATH] [--update]
"""
import argparse
import json
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
REF = os.path.join(HERE, "collide_ref.json")
LINE = re.compile(r"^collide  (\d+) face pairs pass through one another on (\d+) tiles", re.M)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--binary", default=os.path.join(ROOT, "build", "arcology"))
    ap.add_argument("--update", action="store_true")
    a = ap.parse_args()
    ref = json.load(open(REF)) if os.path.exists(REF) else {}
    got, worse = {}, []
    for city in ("atlanta", "toronto", "tokyo", "flint", "babar", "maltron", "chicago"):
        out = subprocess.run(
            [a.binary, os.path.join(ROOT, "cities", city + ".sc2"),
             "--mute", "--mesh-check", "--run", "1"],
            capture_output=True, text=True, errors="replace", cwd=ROOT).stdout
        m = LINE.search(out)
        if not m:
            raise SystemExit("collide check: %s printed no collide line" % city)
        got[city] = int(m.group(1))
        if city in ref and got[city] > ref[city]:
            worse.append("%s: %d pairs, was %d" % (city, got[city], ref[city]))
        print("%-8s %d pairs" % (city, got[city]))
    if a.update or not ref:
        json.dump(got, open(REF, "w"), indent=1, sort_keys=True)
        print("collide check: the ratchet holds these numbers now")
        return 0
    if worse:
        print("collide check: WORSE --\n  " + "\n  ".join(worse))
        return 1
    print("collide check: no worse than the ratchet")
    return 0


if __name__ == "__main__":
    sys.exit(main())
