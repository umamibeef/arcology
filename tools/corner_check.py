#!/usr/bin/env python3
"""corner_check.py -- no segment may be broken.

A broken segment is one the fit could not draw whole: a corner where no
legal radius fitted and the line simply turns (the red node in show
curves, the "corners" count on the tangent-fit line), or a segment that
produced no geometry at all.  The user, 5 September 2026: "A test should
be that we never have broken segments (the brown marker)."

The corpus is not at zero yet, so the test is a ratchet: every city's
counts are held against tools/corner_baseline.json and a city that gets
worse fails the test; the cities still above zero are listed so the
number can be worked down.  `--update` rewrites the baseline from the
current counts (after a fix, never to hide a regression).

    tools/corner_check.py [--binary build/arcology] [--update]
"""
import argparse
import concurrent.futures
import json
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
BASELINE = os.path.join(HERE, "corner_baseline.json")
COLLECTION = os.environ.get("ARC_CITIES") or os.path.expanduser(
    "~/Downloads/SimCity 2000® Collection")


def cities():
    out = []
    for folder in [os.path.join(ROOT, "cities"), os.path.join(COLLECTION, "Cities"), COLLECTION]:
        if not os.path.isdir(folder):
            continue
        for name in sorted(os.listdir(folder)):
            if name.lower().endswith(".sc2"):
                out.append(os.path.join(folder, name))
    return out


def measure(binary, city):
    try:
        text = subprocess.run([binary, city, "--mute", "--mesh-check", "--dump-to", "-"], capture_output=True, text=True, errors="replace", timeout=120).stdout
    except (subprocess.TimeoutExpired, OSError) as e:
        return {"corners": {}, "nogeom": None, "error": str(e)}
    corners = {}
    for fam, n in re.findall(r"^tangent fit  (road|rail|highway): .*?(\d+) corners", text, re.M):
        corners[fam] = int(n)
    m = re.search(r"^road pieces without geometry: (\d+)", text, re.M)
    return {"corners": corners, "nogeom": int(m.group(1)) if m else None}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--binary", default=os.path.join(ROOT, "build", "arcology"))
    ap.add_argument("--update", action="store_true", help="rewrite the baseline from the current counts")
    args = ap.parse_args()
    files = cities()
    if not files:
        print("corner check: no cities found")
        return 1
    with concurrent.futures.ThreadPoolExecutor(max_workers=os.cpu_count() or 4) as pool:
        results = dict(zip([os.path.basename(f) for f in files], pool.map(lambda f: measure(args.binary, f), files)))
    baseline = {}
    if os.path.exists(BASELINE):
        with open(BASELINE) as fp:
            baseline = json.load(fp)
    worse, remaining, totals = [], [], {"road": 0, "rail": 0, "highway": 0, "nogeom": 0}
    for name, r in sorted(results.items()):
        if "error" in r:
            worse.append("%s: could not run (%s)" % (name, r["error"]))
            continue
        c = sum(r["corners"].values())
        g = r["nogeom"] or 0
        for fam, n in r["corners"].items():
            totals[fam] += n
        totals["nogeom"] += g
        b = baseline.get(name, {"corners": 0, "nogeom": 0})
        if c > b["corners"] or g > b["nogeom"]:
            worse.append("%s: %d corners (baseline %d), %d without geometry (baseline %d)" % (name, c, b["corners"], g, b["nogeom"]))
        if c or g:
            remaining.append("%s: %s%s" % (name, ", ".join("%d %s" % (n, fam) for fam, n in r["corners"].items() if n), (", %d without geometry" % g) if g else ""))
    if args.update:
        with open(BASELINE, "w") as fp:
            json.dump({n: {"corners": sum(r["corners"].values()), "nogeom": r["nogeom"] or 0} for n, r in sorted(results.items()) if "error" not in r}, fp, indent=1)
        print("corner check: baseline written for %d cities" % len(results))
    print("corner check: %d cities; hard corners road %d rail %d highway %d; segments without geometry %d; %d cities still above zero" % (len(results), totals["road"], totals["rail"], totals["highway"], totals["nogeom"], len(remaining)))
    for line in remaining:
        print("  " + line)
    if worse and not args.update:
        print("corner check: WORSE than the baseline:")
        for line in worse:
            print("  " + line)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
