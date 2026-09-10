#!/usr/bin/env python3
"""buried_check.py -- nothing of a line may be laid under its own margin.

A margin is the one surface a person stands on.  Where a junction's own
fill, or a strip's own way, is drawn beneath the margin that
belongs to it, the two were never made to meet: the surface was not drawn
back to its own lip.  Both faces then lie at one height, and which one a
pixel shows rests on the painter's slot alone.

`arcology --mesh-check` counts those pairs (mesh_check_overlap).  The
corpus is not at zero, so this is a ratchet: each city is held against
tools/buried_baseline.json and a city that gets worse fails.  `--update`
rewrites the baseline after a fix, never to hide a regression.

    tools/buried_check.py [--binary build/arcology] [--update]
"""
import argparse
import json
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
BASELINE = os.path.join(HERE, "buried_baseline.json")
CITIES = ["atlanta", "toronto", "manhattan"]


def measure(binary, city):
    """The buried count for one city, or None when it could not be run."""
    path = os.path.join(ROOT, "cities", city + ".sc2")
    if not os.path.exists(path):
        return None
    out = subprocess.run([binary, path, "--mute", "--mesh-check"],
                         capture_output=True, text=True, cwd=ROOT).stdout
    m = re.search(r"(\d+) of line works buried under a margin", out)
    return int(m.group(1)) if m else None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--binary", default=os.path.join(ROOT, "build", "arcology"))
    ap.add_argument("--update", action="store_true")
    a = ap.parse_args()
    if not os.path.exists(a.binary):
        print("buried: no binary at", a.binary)
        return 0
    base = {}
    if os.path.exists(BASELINE):
        with open(BASELINE) as f:
            base = json.load(f)
    now, worse = {}, []
    for city in CITIES:
        n = measure(a.binary, city)
        if n is None:
            continue
        now[city] = n
        was = base.get(city)
        if was is not None and n > was:
            worse.append((city, was, n))
    if a.update:
        with open(BASELINE, "w") as f:
            json.dump(now, f, indent=2, sort_keys=True)
            f.write("\n")
        print("buried: baseline written:", now)
        return 0
    for city, was, n in worse:
        print(f"buried: {city} got worse: {was} -> {n} faces under their own margin")
    total = sum(now.values())
    print(f"buried: {len(now)} cities, {total} faces under their own margin"
          f"{' -- WORSE' if worse else ''}")
    return 1 if worse else 0


if __name__ == "__main__":
    sys.exit(main())
