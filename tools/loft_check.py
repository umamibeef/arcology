#!/usr/bin/env python3
"""The loft is a service, and the cross-section is the script's.

GOAL 3 is that a script can loft a path IT INVENTED, along a
cross-section IT DEFINED, and see it in the world.  The path here is
invented in a script the pipeline has never heard of, fitted with
arc.fit and swept with w:loft; what the build then contains is read back
out of the mesh and checked here.

Five claims:

  drawn       the mesh gains triangles, in a shape of the script's own
              naming, and still passes its own checks
  faces       the sweep stationed the path exactly as it was asked to --
              the count is worked out here from the pieces and the steps
  height      a section pinned to a height is at that height, in the
              material the section named, under the script's shape name
  section     a different cross-section draws a different thing: a wider
              one reaches a point a narrower one does not
  ground      a section given a lift follows the ground rather than
              lying flat

    python3 tools/loft_check.py [--binary PATH] [--city NAME]
"""
import argparse
import json
import math
import os
import re
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)

#  The path the script invents, and the two sections it lofts along.  A
#  sine so the fit has real corners in it, and the sections differ only
#  in how far they reach across.
PATH = "\n".join([
    "    local pts = {}",
    "    for k = 0, 12 do",
    "        pts[#pts + 1] = {x = 30 + k * 4, y = 40 + 6 * math.sin(k * 0.5)}",
    "    end",
    "    local pieces = arc.fit(pts, 3.0)",
])

STEP, STEP_ARC, RADIUS, HEIGHT = 0.25, 0.1, 3.0, 12.0

NARROW = [(-0.30, 0.0), (0.30, 0.0)]
WIDE = [(-1.20, 0.30), (-1.00, 0.0), (1.00, 0.0), (1.20, 0.30)]


def invent(section, opts):
    sec = ",".join("{across=%r,up=%r,mat=arc.mat.line}" % (a, u) for a, u in section)
    return """
arc.rules.invent = function (w)
%s
    w:shape("invented", 30, 40)
    local faces = w:loft(pieces, {%s}, {%s})
    arc.dump("invent " .. #pieces .. " " .. faces)
    local ls = {}
    for _, p in ipairs(pieces) do
        ls[#ls + 1] = string.format("%%s %%.9g", tostring(p.arc), p.len)
    end
    arc.dump("pieces " .. table.concat(ls, ";"))
end
""" % (PATH, sec, opts)


def run(binary, city, script, extra=()):
    path = os.path.join(ROOT, "cities", city + ".sc2")
    with tempfile.NamedTemporaryFile("w", suffix=".lua", delete=False,
                                     dir=os.environ.get("TMPDIR")) as f:
        f.write(script or "")
        name = f.name
    cmd = [binary, path, "--mute", "--mesh-check"]
    if script:
        cmd += ["--lua", name]
    cmd += list(extra)
    try:
        p = subprocess.run(cmd, capture_output=True, text=True, cwd=ROOT)
    finally:
        os.unlink(name)
    return p.returncode, p.stdout


def tris(out):
    m = re.search(r"^mesh check: (\d+) triangles", out, re.M)
    return int(m.group(1)) if m else -1


def shapes(out):
    m = re.search(r"^shapes\s+(\d+) shapes; (\d+) triangles claimed by none", out, re.M)
    return (int(m.group(1)), int(m.group(2))) if m else (-1, -1)


def said(out, tag):
    for line in out.splitlines():
        if line.startswith(tag + " "):
            return line[len(tag) + 1:]
    return None


def want_faces(pieces, rungs):
    """The faces the sweep must lay: a station every `step` along a
    straight and every `step_arc` along an arc, two triangles a rung."""
    ns = 0
    for is_arc, length in pieces:
        step = STEP_ARC if is_arc else STEP
        nd = max(1, math.ceil(length / step - 1e-9))
        ns += nd + (1 if ns == 0 else 0)
    return (ns - 1) * rungs * 2


def probe(binary, city, script, points):
    """Every face over each point, as the mesh reports them."""
    ev = " ".join("arc.mesh.probe(%r, %r)" % p for p in points)
    rc, out = run(binary, city, script, ["--lua-eval", ev])
    hits, at = {}, None
    for line in out.splitlines():
        m = re.match(r"^probe   ([-\d.]+),([-\d.]+): (\d+) faces", line)
        if m:
            at = (float(m.group(1)), float(m.group(2)))
            hits[at] = []
            continue
        m = re.match(r"^probe     z ([-\d.]+)\s+slot\s+([-\d.]+)\s+material\s+(\S+)\s+(.*)$", line)
        if m and at:
            hits[at].append((float(m.group(1)), float(m.group(3)), m.group(4)))
    return rc, hits


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--binary", default=os.path.join(ROOT, "build", "arcology"))
    ap.add_argument("--city", default="atlanta")
    a = ap.parse_args()
    if not os.path.exists(a.binary) or not os.path.exists(
            os.path.join(ROOT, "cities", a.city + ".sc2")):
        print("loft: no binary or no city")
        return 0
    bad = []

    pinned = "step=%r,step_arc=%r,z=%r,slot=0.02" % (STEP, STEP_ARC, HEIGHT)
    bare_rc, bare = run(a.binary, a.city, None)
    if bare_rc != 0:
        print("loft: the city does not check out without a script")
        return 1
    wide_rc, wide = run(a.binary, a.city, invent(WIDE, pinned))
    if wide_rc != 0:
        print("loft: the build with an invented strip in it does not check out")
        print(wide[-2000:])
        return 1

    #  drawn
    if tris(wide) <= tris(bare):
        bad.append("nothing was drawn: %d triangles either way" % tris(bare))
    if shapes(wide)[0] != shapes(bare)[0] + 1:
        bad.append("the script's shape is missing: %d shapes against %d"
                   % (shapes(wide)[0], shapes(bare)[0]))
    if shapes(wide)[1] != 0:
        bad.append("%d triangles belong to no shape" % shapes(wide)[1])

    #  faces
    line = said(wide, "invent")
    plist = said(wide, "pieces")
    if not line or not plist:
        bad.append("the invent rule said nothing")
    else:
        npieces, faces = (int(x) for x in line.split())
        pieces = [(f.split()[0] == "true", float(f.split()[1])) for f in plist.split(";")]
        if len(pieces) != npieces:
            bad.append("%d pieces listed, %d fitted" % (len(pieces), npieces))
        exp = want_faces(pieces, len(WIDE) - 1)
        if faces != exp:
            bad.append("the sweep laid %d faces over %d pieces; the steps ask for %d"
                       % (faces, npieces, exp))

    #  height, section, ground
    on = (30.5, 40.24)
    off = (30.5, 41.5)
    away = (20.0, 40.0)
    rc, hits = probe(a.binary, a.city, invent(WIDE, pinned), [on, off, away])
    mine = [h for h in hits.get(on, []) if "invented" in h[2]]
    if not mine:
        bad.append("no invented face over %r at all" % (on,))
    elif abs(mine[0][0] - HEIGHT) > 1e-3:
        bad.append("the invented face over %r is at %.4f, pinned to %.4f"
                   % (on, mine[0][0], HEIGHT))
    if [h for h in hits.get(away, []) if "invented" in h[2]]:
        bad.append("an invented face over %r, where the script lofted nothing" % (away,))
    wide_off = [h for h in hits.get(off, []) if "invented" in h[2]]

    rc, nhits = probe(a.binary, a.city, invent(NARROW, pinned), [on, off])
    if not [h for h in nhits.get(on, []) if "invented" in h[2]]:
        bad.append("the narrow section drew nothing over its own centreline")
    if [h for h in nhits.get(off, []) if "invented" in h[2]]:
        bad.append("the narrow section reached %r, which only the wide one covers" % (off,))
    if not wide_off:
        bad.append("the wide section did not reach %r" % (off,))

    #  ground: a lifted section follows the terrain, so two points with
    #  different ground under them are at different heights
    lift = "step=%r,step_arc=%r,lift=1.0,slot=0.02" % (STEP, STEP_ARC)
    pa, pb = (30.2, 40.0), (66.0, 34.14)
    rc, lhits = probe(a.binary, a.city, invent(WIDE, lift), [pa, pb])
    za = [h[0] for h in lhits.get(pa, []) if "invented" in h[2]]
    zb = [h[0] for h in lhits.get(pb, []) if "invented" in h[2]]
    ga = [h[0] for h in lhits.get(pa, []) if h[2].startswith("ground")]
    gb = [h[0] for h in lhits.get(pb, []) if h[2].startswith("ground")]
    if not za or not zb:
        bad.append("the lifted section is missing over %r or %r" % (pa, pb))
    elif not ga or not gb:
        bad.append("no ground under the lifted section to measure it against")
    else:
        for z, g, at in ((za[0], ga[0], pa), (zb[0], gb[0], pb)):
            if abs(z - g - 1.0) > 0.02:
                bad.append("the lifted section over %r is %.4f, the ground %.4f: "
                           "it is not following the terrain" % (at, z, g))

    for line in bad:
        print("loft:", line)
    if bad:
        return 1
    npieces, faces = (int(x) for x in said(wide, "invent").split())
    print("a path the script invented, fitted into %d pieces and swept along a "
          "section of its own:" % npieces)
    print("  %d faces, %d triangles over the bare build, one new shape, mesh check clean"
          % (faces, tris(wide) - tris(bare)))
    print("  pinned at %.1f and read back at %.1f; a lifted section follows the ground; "
          "a narrower section draws a narrower thing" % (HEIGHT, mine[0][0]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
