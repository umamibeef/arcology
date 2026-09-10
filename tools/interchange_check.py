#!/usr/bin/env python3
"""The band's interchanges are joined by a script, and by nothing else.

A slab lane's end is one of four situations, and telling them apart is
the whole of `scripts/compose/links.lua`:

  * the inner lane with a line ahead -- the slab has come down to grade
  * the inner lane with no line -- the slab goes on as another band, so
    the lane looks for that band's lane round the interchange
  * a middle or outer lane with a band ahead -- the same continuation at
    its own offset, which is what carries a three-lane way round
    a loop without dropping a lane
  * a middle or outer lane with nothing ahead -- it tapers into the
    inner lane of its own band

This checks three things:

  the script's    taking arc.rules.links away leaves every band end
                  unjoined.  If any matcher were left in C the links
                  would survive it.  The meets' own links are a
                  different rule and stay
  connected       with it, every lane end goes somewhere and every start
                  has something arriving, and every band end is either
                  joined or reported as meeting nothing
  spurs           every spur's two ends fasten to a lane, and clearing
                  arc.rules.spur_lane leaves them fastened to nothing
  sound           the mesh the loops are drawn into still checks out

    python3 tools/interchange_check.py [--binary PATH]
"""
import argparse
import os
import re
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)

#  Cities with real interchanges: a slab that splits, loops between
#  bands, and spurs down to the lines.
CITIES = ["tokyo", "atlanta", "chicago", "flint"]

NONE = "arc.rules.links = function (x) return true end\n"

#  And the spur's own ends: which lane each fastens to is a rule too.
NOSNAP = "arc.rules.spur_lane = function (s) s:is(nil) return true end\n"

SPURS = re.compile(r"^lanes.*?(\d+) spurs, (\d+) spur ends on no lane", re.M | re.S)

LANES = re.compile(
    r"^lanes.*?(\d+) band ends \((\d+) meeting no line, (\d+) on to another band\), "
    r"(\d+) links \((\d+) failed\).*?(\d+) ends with nowhere to go, "
    r"(\d+) starts with nothing arriving", re.M | re.S)


def run(binary, city, script=None):
    name = None
    if script:
        with tempfile.NamedTemporaryFile("w", suffix=".lua", delete=False,
                                         dir=os.environ.get("TMPDIR")) as f:
            f.write(script)
            name = f.name
    cmd = [binary, os.path.join(ROOT, "cities", city + ".sc2"), "--mute", "--mesh-check"]
    if name:
        cmd += ["--lua", name]
    try:
        p = subprocess.run(cmd, capture_output=True, text=True, cwd=ROOT)
    finally:
        if name:
            os.unlink(name)
    return p.stdout


def read(out):
    m = LANES.search(out)
    if not m:
        return None
    k = [int(x) for x in m.groups()]
    return dict(zip(("ends", "no_line", "on_band", "links", "failed",
                     "nowhere", "nothing"), k))


def sound(out):
    return (re.search(r"^mesh check: \d+ triangles, 0 free edges, 0 free vertical spans",
                      out, re.M) is not None
            and re.search(r"^shapes\s+\d+ shapes; 0 triangles claimed by none", out, re.M)
            is not None)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--binary", default=os.path.join(ROOT, "build", "arcology"))
    a = ap.parse_args()
    if not os.path.exists(a.binary):
        print("interchange: no binary at", a.binary)
        return 0
    bad, rows = [], []
    for city in CITIES:
        if not os.path.exists(os.path.join(ROOT, "cities", city + ".sc2")):
            print("interchange: no city", city)
            return 0
        full, none = run(a.binary, city), run(a.binary, city, NONE)
        nosnap = run(a.binary, city, NOSNAP)
        rf, rn = SPURS.search(full), SPURS.search(nosnap)
        if rf and int(rf.group(1)) > 0:
            if int(rf.group(2)) != 0:
                bad.append("%s: %s of %s spur ends fasten to no lane"
                           % (city, rf.group(2), rf.group(1)))
            if not rn or int(rn.group(2)) == 0:
                bad.append("%s: clearing arc.rules.spur_lane still fastened every spur: "
                           "something behind the script is still picking the lane" % city)
        g, n = read(full), read(none)
        if g is None or n is None:
            bad.append("%s: the lane report says nothing" % city)
            continue
        if not sound(full):
            bad.append("%s: the mesh the loops are drawn into does not check out" % city)
        if g["links"] == 0 and g["ends"] == 0:
            bad.append("%s: no band ends at all -- nothing to join here" % city)
        #  The meets' own links are a different rule (arc.rules.cross)
        #  and stay; what must go is every band end and the links this
        #  rule laid for them.
        if n["ends"] != 0 or n["links"] >= g["links"]:
            bad.append("%s: clearing arc.rules.links left %d band ends and %d links "
                       "against %d: something behind the script is still joining them"
                       % (city, n["ends"], n["links"], g["links"]))
        if g["failed"] != 0:
            bad.append("%s: %d links the router could not build" % (city, g["failed"]))
        if g["on_band"] + g["no_line"] * 0 == 0 and g["ends"] > 0:
            bad.append("%s: %d band ends and not one carried on to another band"
                       % (city, g["ends"]))
        g["spurs"] = int(rf.group(1)) if rf else 0
        rows.append((city, g))
    for line in bad:
        print("interchange:", line)
    if bad:
        return 1
    print("the band's interchanges are the script's, on %d cities:" % len(CITIES))
    for city, g in rows:
        print("  %-8s %3d band ends, %3d carried on to another band, %3d meeting no line; "
              "%3d links, %d failed; %2d spurs all fastened; %d ends nowhere, %d starts with nothing"
              % (city, g["ends"], g["on_band"], g["no_line"], g["links"], g["failed"],
                 g["spurs"], g["nowhere"], g["nothing"]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
