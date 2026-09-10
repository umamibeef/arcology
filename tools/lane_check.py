#!/usr/bin/env python3
"""What a lane must be for a car to drive along it -- a RATCHET.

Four faults, counted by mesh/lane.c over every piece of every lane, and
two more over a slab lane's ends.  None of the corpus is at zero, so this
holds the numbers where they are: it fails when one RISES.

  broken   piece k ends where piece k+1 does not start: a gap
  kinked   they meet, but not tangentially: a corner of no radius
  tight    an arc under the minimum radius, which nothing can take
  stubby   a piece under the minimum length, which is how a tight turn
           HIDES -- a hair of arc swings the line through a large angle
           while showing a radius that passes
  slab     a SLAB lane's own open ends, which the connectivity check
           used to pass over

    python3 tools/lane_check.py [--binary PATH] [--update]
"""
import argparse
import json
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
REF = os.path.join(HERE, "lane_ref.json")

CURVES = re.compile(
    r"^lanes  the curves: (\d+) broken between pieces, (\d+) kinked, "
    r"(\d+) arcs under the minimum radius, (\d+) pieces under the minimum length",
    re.M)
SLAB = re.compile(
    r"^lanes  a SLAB lane's own ends: (\d+) with nowhere to go, (\d+) with nothing arriving",
    re.M)
KEYS = ("broken", "kinked", "tight", "stubby", "slab_nowhere", "slab_nothing")


def measure(binary, city):
    out = subprocess.run(
        [binary, os.path.join(ROOT, "cities", city + ".sc2"),
         "--mute", "--mesh-check", "--run", "1"],
        capture_output=True, text=True, errors="replace", cwd=ROOT).stdout
    c, d = CURVES.search(out), SLAB.search(out)
    if not c or not d:
        raise SystemExit("lane check: %s printed no lane report" % city)
    return dict(zip(KEYS, [int(x) for x in c.groups() + d.groups()]))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--binary", default=os.path.join(ROOT, "build", "arcology"))
    ap.add_argument("--update", action="store_true")
    a = ap.parse_args()
    ref = json.load(open(REF)) if os.path.exists(REF) else {}
    got, worse = {}, []
    for city in ("atlanta", "toronto", "tokyo", "flint", "babar", "maltron", "chicago"):
        got[city] = measure(a.binary, city)
        was = ref.get(city)
        for k in KEYS:
            if was is not None and got[city][k] > was[k]:
                worse.append("%s %s: %d, was %d" % (city, k, got[city][k], was[k]))
        print("%-8s %s" % (city, "  ".join("%s %d" % (k, got[city][k]) for k in KEYS)))
    if a.update or not ref:
        json.dump(got, open(REF, "w"), indent=1, sort_keys=True)
        print("lane check: the ratchet holds these numbers now")
        return 0
    if worse:
        print("lane check: WORSE --\n  " + "\n  ".join(worse))
        return 1
    print("lane check: no worse than the ratchet")
    return 0


if __name__ == "__main__":
    sys.exit(main())
