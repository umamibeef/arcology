#!/usr/bin/env python3
"""The moving world, hashed.

A headless frame does not draw the movers, so the pixel battery says
nothing at all about the traffic: `--traffic-t 0` and `--traffic-t 26`
hash the same.  This is the check that does not.  It advances the world
a fixed distance and hashes what moved and what was drawn -- every car's
segment, direction, lane, distance, speed and waiting, every gate's
angle, every train's place, and every vertex of the scratch mesh the
frame uploads.

It is a RATCHET against a recorded reference, like the six views: a
change to the beat that moves a car is a change that has to be looked
at, and one that moves none costs nothing to prove.

    python3 tools/traffic_check.py                 # against the reference
    python3 tools/traffic_check.py --update        # record a new one
"""
import argparse
import json
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
REF = os.path.join(HERE, "traffic_ref.json")

#  Five cities, each with railways and level meets, advanced far
#  enough that a car has met a signal, a gate and the car ahead of it.
CITIES = ["atlanta", "toronto", "flint", "tokyo", "chicago"]
SECONDS = "26"


def digest(binary, city):
    p = subprocess.run([binary, os.path.join(ROOT, "cities", city + ".sc2"),
                        "--mute", "--win", "1280x800", "--centre", "63,50",
                        "--zoomf", "26", "--traffic-t", SECONDS,
                        "--shot", os.path.join(os.environ.get("TMPDIR", "/tmp"), "traffic.png")],
                       capture_output=True, text=True, cwd=ROOT)
    m = re.search(r"^traffic (\d+) cars, (\d+) trains, (\d+) vertices, digest ([0-9a-f]+)",
                  p.stdout, re.M)
    if not m:
        raise SystemExit("%s: the world said nothing about its traffic\n%s"
                         % (city, p.stdout[-1500:] + p.stderr[-1500:]))
    return {"cars": int(m.group(1)), "trains": int(m.group(2)),
            "vertices": int(m.group(3)), "digest": m.group(4)}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--binary", default=os.path.join(ROOT, "build", "arcology"))
    ap.add_argument("--update", action="store_true")
    a = ap.parse_args()
    if not os.path.exists(a.binary):
        print("traffic: no binary at", a.binary)
        return 0
    got = {}
    for city in CITIES:
        if not os.path.exists(os.path.join(ROOT, "cities", city + ".sc2")):
            print("traffic: no city", city)
            return 0
        got[city] = digest(a.binary, city)
    if a.update:
        json.dump(got, open(REF, "w"), indent=1, sort_keys=True)
        print("traffic: recorded", len(got), "cities")
        return 0
    if not os.path.exists(REF):
        print("traffic: no reference; run with --update")
        return 1
    want = json.load(open(REF))
    bad = [c for c in CITIES if want.get(c) != got[c]]
    for c in bad:
        print("traffic: %-8s %s\n         wanted %s" % (c, got[c], want.get(c)))
    if bad:
        print("%d of %d cities moved" % (len(bad), len(CITIES)))
        return 1
    print("the moving world is unchanged on %d cities after %s seconds:" % (len(CITIES), SECONDS))
    for c in CITIES:
        print("  %-8s %5d cars, %2d trains, %6d vertices  %s"
              % (c, got[c]["cars"], got[c]["trains"], got[c]["vertices"], got[c]["digest"]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
