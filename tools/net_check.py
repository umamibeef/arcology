#!/usr/bin/env python3
"""The road network's lane check over every city the repository ships.

Each city's network is derived from its cells alone (layers 0 to 2 of
plans/network.md) and held to the check: every lane of travel goes on
and is fed, and every sidewalk has a lane at each of its ends.  Its shape
(layer 3) is held to its own check: each road's line is whole, stays in
its corridor and meets the next road across a seam, and each deck stands
clear of its ground.  Each drawn junction is held to its geometry: its
boundary and its sidewalk ring are simple, the ring lies inside the
boundary, and each ring's triangles cover it once.  The ground laid beside
each street is reported by its own check (bank.c), one line a city.  No mesh is built.  A city whose network breaks the check, or that does not run,
fails the test, and the faults are printed by road and lane.

    tools/net_check.py BINARY
"""
import glob
import os
import re
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from arcrun import world, Fault  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TOTAL = re.compile(r"^network check  (\d+) lanes", re.M)
FAULT = re.compile(r"^(?:network check  [a-z]|roads check  junction|bank: \d+ edge samples).*$", re.M)


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    binary = sys.argv[1]
    cities = sorted(glob.glob(os.path.join(ROOT, "cities", "*.sc2")) + glob.glob(os.path.join(ROOT, "cities", "*.arco")))
    lanes, bad = 0, []
    with tempfile.TemporaryDirectory() as tmp:
        for city in cities:
            dump = os.path.join(tmp, os.path.basename(city) + ".txt")
            name = os.path.basename(city)
            try:
                text = world(binary, [city, "--mute", "--net-check"], dump=dump, timeout=120)
            except Fault as f:
                bad.append(name)
                print("%s: %s" % (name, f))
                if os.path.exists(dump):
                    with open(dump, encoding="utf-8") as d:
                        for line in FAULT.findall(d.read()):
                            print("  " + line)
                continue
            m = TOTAL.search(text)
            if not m:
                bad.append(name)
                print("%s: the run said no lane count" % name)
                continue
            lanes += int(m.group(1))
    print("net check: %d lanes in %d cities, %d cities fail" % (lanes, len(cities), len(bad)))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
