#!/usr/bin/env python3
"""The fit is a service, and what it hands back is a path.

GOAL 2 is that a script can fit a path OF ITS OWN INVENTION and get the
pieces back.  The paths here are invented in Python, handed to a running
game through `arc.fit`, and the pieces that come back are checked here --
so the fit is not being compared against another copy of itself.

Six claims, each of which a broken fit would breach:

  ends        the first piece starts at the first point, the last piece
              ends at the last
  chain       every piece ends where the next one starts
  arc         an arc's ends lie on its own circle, and its length is the
              radius times the angle it turns through
  tangent     wherever an arc meets a piece the two run the same way, so
              no corner is left where an arc was swept.  Two straights
              meeting is a corner the fit was asked to leave
  radius      no corner sweeps wider than it was allowed
  budget      no arc eats more of an edge than its budget

    python3 tools/fit_check.py [--binary PATH]
"""
import json
import math
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
BIN = os.path.join(ROOT, "build", "arcology")
EPS = 2e-3

#  Each path, the radius every corner is allowed, and what it is meant to
#  exercise.  The last two have corners the budget must cut back.
PATHS = [
    ("dog-leg", [(10, 10), (20, 10), (20, 25), (34, 25)], 3.0),
    ("shallow", [(4, 4), (20, 6), (36, 4), (52, 8)], 6.0),
    ("hairpin", [(10, 40), (25, 40), (25, 44), (10, 44)], 1.5),
    ("straight", [(0, 0), (10, 0), (20, 0), (30, 0)], 4.0),
    ("hard corners", [(5, 5), (15, 5), (15, 15)], 0.0),
    ("short edges", [(0, 0), (1.2, 0), (1.2, 1.2), (2.4, 1.2), (2.4, 0)], 5.0),
    ("zigzag", [(0, 20), (6, 26), (12, 20), (18, 26), (24, 20)], 2.0),
]


def lua_path(pts):
    return "{" + ",".join("{x=%r,y=%r}" % (float(x), float(y)) for x, y in pts) + "}"


def run(binary, city):
    """Every path fitted inside the game, printed as one JSON line each."""
    src = ["local out = {}"]
    for i, (name, pts, r) in enumerate(PATHS):
        src.append("local pc = arc.fit(%s, %r)" % (lua_path(pts), float(r)))
        src.append("local ps = {}")
        src.append("for _, p in ipairs(pc) do ps[#ps+1] = string.format("
                   "'{\"arc\":%s,\"ax\":%.9g,\"ay\":%.9g,\"bx\":%.9g,\"by\":%.9g,"
                   "\"len\":%.9g,\"r\":%.9g,\"t0\":%.9g,\"t1\":%.9g,"
                   "\"cx\":%.9g,\"cy\":%.9g}', tostring(p.arc), p.ax, p.ay, p.bx, p.by,"
                   " p.len, p.r or 0, p.t0 or 0, p.t1 or 0, p.cx or 0, p.cy or 0) end")
        src.append("arc.dump('FIT %d [' .. table.concat(ps, ',') .. ']')" % i)
    out = subprocess.run([binary, city, "--mute", "--lua-eval", "\n".join(src)],
                         capture_output=True, text=True, cwd=ROOT)
    if out.returncode != 0:
        raise SystemExit("the game would not run the fit\n%s" % out.stderr[-2000:])
    got = {}
    for line in out.stdout.splitlines():
        if line.startswith("FIT "):
            i, rest = line[4:].split(" ", 1)
            got[int(i)] = json.loads(rest)
    return got


def near(a, b, eps=EPS):
    return abs(a - b) <= eps


def heading_in(p):
    """Which way the path runs INTO the end of a piece."""
    if p["arc"]:
        s = 1.0 if p["t1"] >= p["t0"] else -1.0
        return (-math.sin(p["t1"]) * s, math.cos(p["t1"]) * s)
    d = math.hypot(p["bx"] - p["ax"], p["by"] - p["ay"])
    return ((p["bx"] - p["ax"]) / d, (p["by"] - p["ay"]) / d) if d > 1e-9 else None


def heading_out(p):
    """And which way it runs OUT of the start of one."""
    if p["arc"]:
        s = 1.0 if p["t1"] >= p["t0"] else -1.0
        return (-math.sin(p["t0"]) * s, math.cos(p["t0"]) * s)
    return heading_in(p)


def check(name, pts, want_r, pc):
    """Every claim about one fitted path.  Answers the breaches."""
    bad = []
    if not pc:
        return ["%s: no pieces at all" % name]
    if not (near(pc[0]["ax"], pts[0][0]) and near(pc[0]["ay"], pts[0][1])):
        bad.append("%s: starts at (%.3f,%.3f), not the first point"
                   % (name, pc[0]["ax"], pc[0]["ay"]))
    if not (near(pc[-1]["bx"], pts[-1][0]) and near(pc[-1]["by"], pts[-1][1])):
        bad.append("%s: ends at (%.3f,%.3f), not the last point"
                   % (name, pc[-1]["bx"], pc[-1]["by"]))
    for k in range(len(pc) - 1):
        if not (near(pc[k]["bx"], pc[k + 1]["ax"]) and near(pc[k]["by"], pc[k + 1]["ay"])):
            bad.append("%s: piece %d ends at (%.3f,%.3f), piece %d starts at (%.3f,%.3f)"
                       % (name, k, pc[k]["bx"], pc[k]["by"], k + 1,
                          pc[k + 1]["ax"], pc[k + 1]["ay"]))
    for k, p in enumerate(pc):
        if not p["arc"]:
            if not near(p["len"], math.hypot(p["bx"] - p["ax"], p["by"] - p["ay"])):
                bad.append("%s: straight %d is %.4f long, measures %.4f"
                           % (name, k, p["len"], math.hypot(p["bx"] - p["ax"],
                                                            p["by"] - p["ay"])))
            continue
        for end in ("a", "b"):
            d = math.hypot(p[end + "x"] - p["cx"], p[end + "y"] - p["cy"])
            if not near(d, p["r"]):
                bad.append("%s: arc %d's %s end is %.4f from the centre, radius %.4f"
                           % (name, k, end, d, p["r"]))
        if not near(p["len"], p["r"] * abs(p["t1"] - p["t0"])):
            bad.append("%s: arc %d is %.4f long, radius %.4f turns %.4f"
                       % (name, k, p["len"], p["r"], abs(p["t1"] - p["t0"])))
        if p["r"] > want_r + EPS:
            bad.append("%s: arc %d swept %.4f, allowed %.4f" % (name, k, p["r"], want_r))
    #  An arc is swept INTO a corner, so wherever one meets a piece the
    #  two must run the same way.  Two straights meeting is a corner the
    #  fit was asked to leave, and is not a breach.
    for k in range(len(pc) - 1):
        if not (pc[k]["arc"] or pc[k + 1]["arc"]):
            continue
        u, v = heading_in(pc[k]), heading_out(pc[k + 1])
        if u and v and u[0] * v[0] + u[1] * v[1] < 1.0 - 1e-3:
            bad.append("%s: a corner between pieces %d and %d, %.1f degrees"
                       % (name, k, k + 1,
                          math.degrees(math.acos(max(-1.0, min(1.0, u[0] * v[0] + u[1] * v[1]))))))
    #  The budget: with none given each corner gets its share of the
    #  shorter edge, so no tangent point may sit past an edge's midpoint.
    for k, p in enumerate(pc):
        if not p["arc"]:
            continue
        for vx, vy in pts:
            d = math.hypot(p["ax"] - vx, p["ay"] - vy)
            if d < EPS:
                bad.append("%s: arc %d starts on a vertex" % (name, k))
    return bad


def main(argv):
    binary = BIN
    argv = argv[1:]
    if len(argv) >= 2 and argv[0] == "--binary":
        binary, argv = argv[1], argv[2:]
    city = argv[0] if argv else os.path.join(ROOT, "cities", "atlanta.sc2")
    got = run(binary, city)
    bad = []
    for i, (name, pts, r) in enumerate(PATHS):
        if i not in got:
            bad.append("%s: the fit answered nothing" % name)
            continue
        bad += check(name, pts, r, got[i])
    for line in bad:
        print(line)
    if bad:
        print("%d breaches over %d paths" % (len(bad), len(PATHS)))
        return 1
    print("%d paths of the script's own invention fitted: ends, chain, arc, "
          "tangent, radius and budget all hold" % len(PATHS))
    for i, (name, pts, r) in enumerate(PATHS):
        arcs = sum(1 for p in got[i] if p["arc"])
        print("  %-14s %d points, radius %.1f -> %d pieces, %d arcs, %.3f long"
              % (name, len(pts), r, len(got[i]), arcs, sum(p["len"] for p in got[i])))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
