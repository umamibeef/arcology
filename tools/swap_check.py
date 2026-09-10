#!/usr/bin/env python3
"""Each interpretation can be replaced by editing a script alone.

GOAL 5 names four of them, and this replaces each with a DIFFERENT
ALGORITHM -- not a different number -- and shows the world change.  No
compile: every one of these is a Lua file read at startup.

  sweep     lines through the grid corridors: a Chaikin corner-cutting
            of the corridor's own cells, in place of the tangent fit.
            Nothing the fit does is used -- no runs, no chain, no joins
  climb     an on-spur raising a smooth climb to a band: a cosine
            ease over the whole length, in place of the straight line
  turns     the pattern an intersection draws: which arm's lane joins
            which, replaced by one that permits the through movement and
            no turn at all
  lanes     what a line carries: the mean population density over the
            segment's cells and the eight around each of them, in place
            of the median of the classes its tiles were drawn with

and one more that is not a goal of its own but is easy to mistake for
the third, so it is named apart:

  paving    the fill an intersection LAYS, which is a different rule
            from the pattern its lanes make
  cap       what a lane does where its segment simply stops
  arm       how a spur joins the meet piece the data puts beside it
  target    where a spur's foot aims on the line it comes down to
  lip      which lane of the line it fastens to -- asked once, by
            the foot and by every placing the join slides through
  corridor  where a raised band may sweep: what stands in its way,
            and how far beside its own cells it may reach
  tops      where every tile's top comes from -- the whole terrain read
            as one flat field, with no pads, no water and no shelves in
            it, in place of the six-way ladder

TWO WITNESSES, because a swap can change the world without changing a
triangle.  The geometry is a hash of the mesh's triangles, SORTED, so a
change that moves a surface without changing how many there are is still
caught -- which is what the spur's climb does.  The LANE MODEL is the
report's own counts, because a connector is a lane and not a triangle: a
rule that decides which lane joins which moves not one vertex until the
overlay is switched on, and a check that reads triangles alone cannot
see it at all.

Every swap declares which witness its rule is supposed to move.  A swap
whose witness does not move has found a copy of the decision somewhere a
script cannot reach, and that is the whole point of the check.

    python3 tools/swap_check.py [--binary PATH] [--city NAME]
"""
import argparse
import hashlib
import os
import re
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)

SWEEP = """
--  Roads swept a different way: a Chaikin smoothing of the corridor's
--  own cell centres, in place of the pipeline's tangent fit.
arc.rules.path = function (p)
    local d = p:corridor()
    if not d or d.n < 2 then return end
    local q = {}
    q[0] = d.start
    for k = 1, d.n - 2 do
        q[#q + 1] = {x = d.cells[k].col + 0.5, y = d.cells[k].row + 0.5}
    end
    q[#q + 1] = d.goal
    for _ = 1, 2 do
        local o, m = {}, #q
        o[0] = q[0]
        for k = 0, m - 1 do
            local a, b = q[k], q[k + 1]
            o[#o + 1] = {x = a.x + 0.25 * (b.x - a.x), y = a.y + 0.25 * (b.y - a.y)}
            o[#o + 1] = {x = a.x + 0.75 * (b.x - a.x), y = a.y + 0.75 * (b.y - a.y)}
        end
        o[#o + 1] = q[m]
        q = o
    end
    local rad = {}
    for k = 0, #q do rad[k] = 0.5 * d.half end
    rad[0], rad[#q] = 0.0, 0.0
    p:answer(q, rad, nil, #q + 1)
end
"""

CLIMB = """
--  An on-spur raising a different climb: a cosine ease over the whole
--  length in place of the straight line.
local base = arc.rules.profile
arc.rules.profile = function (p)
    local d = p:info()
    if not d.spur then return base(p) end
    local n = d.n
    local s, z = {}, {}
    for i = 0, n - 1 do s[i], z[i] = p:at(i) end
    local z0, z1 = z[0] + d.slab_above, z[n - 1] + d.slab_above
    for i = 0, n - 1 do
        local t = d.total > 1e-6 and s[i] / d.total or 0.0
        local lin = z0 + (z1 - z0) * (0.5 - 0.5 * math.cos(t * math.pi))
        if lin > z[i] then z[i] = lin end
        p:set(i, z[i])
    end
    return true
end
"""

TURNS = """
--  An intersection that permits the through movement and nothing else:
--  every arm's lanes cross to the arm opposite it and turn nowhere.
arc.rules.turns = function (j)
    local d = j:info()
    for e = 0, d.arms - 1 do
        local a = j:arm(e)
        if a and a.into then
            local e2 = (e + 2) % d.arms
            local b = j:arm(e2)
            if b and b.out then
                for k = 0, a.lanes - 1 do
                    for k2 = 0, b.lanes - 1 do
                        if a.lanes ~= 2 or b.lanes ~= 2 or k == k2 then
                            j:want(e, k, e2, k2)
                        end
                    end
                end
            end
        end
    end
    return true
end
"""

PAVING = """
--  An intersection that lays no fill at all.
arc.rules.junction = function (j) return true end
"""

CAP = """
--  A segment that does nothing at all where it stops: no lane drawn
--  round the cap, no thread naming the one beside it.
arc.rules.cap = function (x) return true end
"""

SPUR_ARM = """
--  Every spur an arm of the box that only the lane opposite it may use,
--  whatever the lines around the meet say.  How a spur meets its
--  meet is one of the things arc.rules.spurs reads off the map, so
--  this wraps that rule and overrides the one field.
do
    local base = arc.rules.spurs
    arc.rules.spurs = function (o)
        local shim = setmetatable({}, {__index = function (_, k)
            if k == "spur" then
                return function (_, r)
                    r.arm = 2
                    return o:spur(r)
                end
            end
            return function (_, ...) return o[k](o, ...) end
        end})
        return base(shim)
    end
end
"""

TARGET = """
--  Every spur carried STRAIGHT ON across the line tile, whatever its
--  meet turned out to be: no merge and no turn anywhere.
arc.rules.spur_target = function (t)
    local d = t:info()
    local cx, cy = d.x + d.rdx * 0.5, d.y + d.rdy * 0.5
    local tvx, tvy = d.rdx, d.rdy
    if not d.off then tvx, tvy = -tvx, -tvy end
    local rx, ry = tvy, -tvx
    t:is(cx + rx * d.lane_off + d.rdx * 0.5, cy + ry * d.lane_off + d.rdy * 0.5,
         d.rdx, d.rdy, tvx, tvy)
    return true
end
"""

LIP = """
--  A spur fastening to the lane nearest the line's own CENTRELINE
--  rather than the lip-side one -- the same comparison the other way
--  about.  Both its foot and every placing the join slides through ask
--  this, so one edit moves them together.
local f32 = arc.put.f32
function arc.lip_lane(s, d)
    local best, bd, boff
    for i = 0, d.n - 1 do
        local c = s:at(i)
        if c.line and c.dot > d.dot and c.dist < d.reach then
            local off = math.abs(c.off)
            if not boff or off < f32(boff - 1e-4)
               or (off < f32(boff + 1e-4) and c.dist < bd) then
                bd, boff, best = c.dist, off, i
            end
        end
    end
    return best
end
"""

CORRIDOR = """
--  Where a band may sweep, both halves of it, changed together: a slab
--  refused the air over anything that stands up, as a wall would refuse
--  it, and held to one tile either side of its own cells.  What a slab
--  may sweep over is a table of the city's own bytes and how far it may
--  reach is a pushed number, so these two are the whole of it.
do
    local air = {}
    for b = 0x00, 0xFF do air[b] = b >= 0x69 and 0 or 1 end
    for b in pairs(arc.band_tiles) do air[b] = 0 end
    arc.bytes("slab_air", air, "number")
    arc.numbers("corridor", {reach = 1, climb = 0, spurs = 1})
end
"""

LANES = """
--  What a line carries, from the density and the neighbourhood rather
--  than from the class its own tiles were drawn with.
arc.rules.seg_class = function (at)
    local sum, n = 0, 0
    for k = 0, at.n - 1 do
        local c, r = at.cells[k].col, at.cells[k].row
        for dr = -1, 1 do
            for dc = -1, 1 do
                local v = arc.city.at("xpop", c + dc, r + dr)
                if v then sum, n = sum + v, n + 1 end
            end
        end
    end
    local mean = n > 0 and sum / n or 0
    return mean > 96 and 2 or mean > 32 and 1 or 0
end
"""

#  name, what it replaces, the script, and the witness its rule must move.
TOPS = """
--  The ground read flat: every tile's top comes from the terrain field
--  and nowhere else, so nothing stands on a pad, no water draws its
--  table and no corridor draws the shelf it was graded.
do
    local base = arc.rules.terrain
    local _ = base
    arc.rules.terrain = function (o)
        local d = o:info()
        local n = d.size
        local top = {}
        for at = 0, n * n - 1 do top[at] = 0 end
        o:tops(top, {}, {}, {}, top)
        return true
    end
end
"""

SWAPS = [
    ("sweep",  "lines through the grid corridors",      SWEEP,  "tris"),
    ("climb",  "an on-spur's climb to the band",     CLIMB,  "tris"),
    ("turns",  "the pattern an intersection draws",     TURNS,  "lanes"),
    ("lanes",  "what a line carries, from density",     LANES,  "tris"),
    ("paving", "the fill an intersection lays",      PAVING, "tris"),
    ("cap",    "what a lane does where a line stops",   CAP,    "lanes"),
    ("arm",    "how a spur meets the meet beside it", SPUR_ARM, "lanes"),
    ("target", "where a spur aims on the line below",  TARGET, "tris"),
    ("lip",   "which lane of a line a spur joins",    LIP,   "tris"),
    ("corridor", "where a band band may sweep",     CORRIDOR, "tris"),
    ("tops",   "where every tile's top comes from",   TOPS,   "tris"),
]

WITNESS = {"tris": "the geometry", "lanes": "the lane model"}


class Built:
    """One build: the two witnesses, and the lines the checks reported."""

    def __init__(self, tris, ntris, lanes, checks):
        self.tris, self.ntris, self.lanes, self.checks = tris, ntris, lanes, checks

    def of(self, witness):
        return self.tris if witness == "tris" else self.lanes


def build(binary, city, script=None):
    with tempfile.TemporaryDirectory() as d:
        out = os.path.join(d, "dump.txt")
        cmd = [binary, os.path.join(ROOT, "cities", city + ".sc2"), "--mute",
               "--mesh-check", "--tile-dump", "all", "--dump-to", out]
        if script:
            name = os.path.join(d, "swap.lua")
            open(name, "w").write(script)
            cmd += ["--lua", name]
        subprocess.run(cmd, capture_output=True, text=True, cwd=ROOT)
        if not os.path.exists(out):
            return None
        lines = open(out, errors="replace").read().splitlines()
    tris = sorted(l for l in lines if l.startswith("tri "))
    #  The lane model as the report counts it: junctions, ports,
    #  connectors, segments, links, caps and every end that goes nowhere.
    lanes = [l for l in lines if l.startswith("lanes ")]
    return Built(hashlib.md5("\n".join(tris).encode()).hexdigest()[:12], len(tris),
                 hashlib.md5("\n".join(lanes).encode()).hexdigest()[:12],
                 "\n".join(l for l in lines if l.startswith(("mesh check", "shapes"))))


def sound(checks):
    return (re.search(r"^mesh check: \d+ triangles, 0 free edges, 0 free vertical spans",
                      checks, re.M) is not None
            and re.search(r"^shapes\s+\d+ shapes; 0 triangles claimed by none",
                          checks, re.M) is not None)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--binary", default=os.path.join(ROOT, "build", "arcology"))
    ap.add_argument("--city", default="atlanta")
    a = ap.parse_args()
    if not os.path.exists(a.binary) or not os.path.exists(
            os.path.join(ROOT, "cities", a.city + ".sc2")):
        print("swap: no binary or no city")
        return 0
    base = build(a.binary, a.city)
    if base is None:
        print("swap: the city would not build at all")
        return 1
    if not sound(base.checks):
        print("swap: the city is not sound before anything is swapped")
        return 1
    bad, rows = [], []
    seen = {}
    for name, what, src, witness in SWAPS:
        got = build(a.binary, a.city, src)
        if got is None:
            bad.append("%s: the build would not run" % name)
            continue
        if got.of(witness) == base.of(witness):
            bad.append("%s: %s -- %s did not change, so the rule is not the "
                       "only copy of the decision"
                       % (name, what, WITNESS[witness]))
        elif (witness, got.of(witness)) in seen:
            bad.append("%s: drew the same thing as %s"
                       % (name, seen[(witness, got.of(witness))]))
        if not sound(got.checks):
            bad.append("%s: the swapped build is not sound" % name)
        seen[(witness, got.of(witness))] = name
        rows.append((name, what, witness, got))
    for line in bad:
        print("swap:", line)
    if bad:
        return 1
    print("%d interpretations, each replaced by a script and nothing else:" % len(rows))
    print("  %-7s %-36s %-14s %8d tri  %s  %s"
          % ("(base)", "the reference", "", base.ntris, base.tris, base.lanes))
    for name, what, witness, got in rows:
        print("  %-7s %-36s %-14s %8d tri  %s  %s"
              % (name, what, WITNESS[witness], got.ntris, got.tris, got.lanes))
    return 0


if __name__ == "__main__":
    sys.exit(main())
