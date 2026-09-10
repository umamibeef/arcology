#!/usr/bin/env python3
"""What a script reads off the map is what is in the city file.

GOAL 1 is that Lua can read the simulation: any layer at any cell, with
neighbours, fast enough for a script to walk the whole map.  This is the
check behind it, and it is deliberately NOT the renderer reading itself.
The city file is decoded here, in Python, straight out of its IFF
chunks; the same cells are read from a Lua walk over `arc.city`; and the
two digests have to agree.

Three things are compared, because they are three different claims:

  plane   every cell of every layer, through arc.city.plane -- the whole
          layer handed over at once, which is what a walk uses
  at      the same cells through arc.city.at, one call each, including
          the half- and quarter-resolution planes scaled to a full cell
  near    the four neighbours of a sample of cells, off the map included

    python3 tools/map_read.py [--binary PATH] [city.sc2 ...]
"""
import os
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import sc2

BIN = os.environ.get("ARCOLOGY", os.path.join(ROOT, "build", "arcology"))

#  Every layer, by the name a script reads it under, with the edge it is
#  stored at and the chunk it comes out of.  The three derived ones are
#  computed from the altitude word and XTER, as the renderer's own
#  rcity_alt_* do.
LAYERS = [
    ("altm", 128, "ALTM"), ("xbld", 128, "XBLD"), ("xzon", 128, "XZON"),
    ("xter", 128, "XTER"), ("xund", 128, "XUND"), ("xtxt", 128, "XTXT"),
    ("xbit", 128, "XBIT"),
    ("xtrf", 64, "XTRF"), ("xplt", 64, "XPLT"), ("xval", 64, "XVAL"),
    ("xcrm", 64, "XCRM"),
    ("xplc", 32, "XPLC"), ("xfir", 32, "XFIR"), ("xpop", 32, "XPOP"),
    ("xrog", 32, "XROG"),
    ("ground", 128, None), ("water_table", 128, None), ("surface", 128, None),
]

MASK = (1 << 32) - 1


def digest(values):
    """A rolling hash, so a layer read in the wrong ORDER differs too."""
    h = 2166136261
    for v in values:
        h = (h * 31 + v) & MASK
    return h


def planes(path):
    """Every layer of one city file, as a flat list at its own edge."""
    ch, _ = sc2.load(path)
    out = {}
    altm = ch.get(b"ALTM", b"")
    alt = [(altm[i * 2] << 8) | altm[i * 2 + 1] for i in range(len(altm) // 2)]
    alt += [0] * (128 * 128 - len(alt))
    xter = list(ch.get(b"XTER", b"")) + [0] * (128 * 128)
    for name, edge, tag in LAYERS:
        if name == "altm":
            out[name] = alt[:128 * 128]
        elif name == "ground":
            out[name] = [a & 0x1F for a in alt[:128 * 128]]
        elif name == "water_table":
            out[name] = [(a >> 5) & 0x1F for a in alt[:128 * 128]]
        elif name == "surface":
            out[name] = [(a & 0x1F) if xter[i] < 0x10 else ((a >> 5) & 0x1F)
                         for i, a in enumerate(alt[:128 * 128])]
        else:
            b = list(ch.get(tag.encode(), b""))
            b += [0] * (edge * edge - len(b))
            out[name] = b[:edge * edge]
    return out


#  The walk, run inside the game.  It reads every layer three ways and
#  prints one line each, so a mismatch names the layer and the way.
WALK = r"""
local M = 4294967296
local names = {}
for _, ly in ipairs(arc.city.layers) do names[#names + 1] = ly.name end
local n = arc.city.size
local t0 = os.clock()
for _, name in ipairs(names) do
    local p, edge = arc.city.plane(name)
    local h = 2166136261
    for i = 0, edge * edge - 1 do h = (h * 31 + p[i]) % M end
    arc.dump("plane " .. name .. " " .. edge .. " " .. h)
end
local t1 = os.clock()
for _, name in ipairs(names) do
    local h = 2166136261
    for row = 0, n - 1 do
        for col = 0, n - 1 do
            h = (h * 31 + arc.city.at(name, col, row)) % M
        end
    end
    arc.dump("at " .. name .. " " .. h)
end
local t2 = os.clock()
--  The neighbours, on a lattice that reaches all four edges of the map.
local h = 2166136261
for row = 0, n - 1, 7 do
    for col = 0, n - 1, 5 do
        for _, name in ipairs(names) do
            local a, b, c, d = arc.city.near(name, col, row)
            h = (h * 31 + (a or 999)) % M
            h = (h * 31 + (b or 999)) % M
            h = (h * 31 + (c or 999)) % M
            h = (h * 31 + (d or 999)) % M
        end
    end
end
arc.dump("near " .. h)
arc.dump(string.format("walked %d layers: plane %.0f ms, at %.0f ms",
                       #names, (t1 - t0) * 1000, (t2 - t1) * 1000))
"""


def run(path):
    out = subprocess.run([BIN, path, "--mute", "--lua-eval", WALK],
                         capture_output=True, text=True, cwd=ROOT)
    if out.returncode != 0:
        raise SystemExit("%s: the game would not run it\n%s" % (path, out.stderr[-2000:]))
    got = {}
    timing = ""
    for line in out.stdout.splitlines():
        f = line.split()
        if len(f) == 4 and f[0] == "plane":
            got[("plane", f[1])] = (int(f[2]), int(f[3]))
        elif len(f) == 3 and f[0] == "at":
            got[("at", f[1])] = int(f[2])
        elif len(f) == 2 and f[0] == "near":
            got[("near",)] = int(f[1])
        elif line.startswith("walked "):
            timing = line
    if not got:
        raise SystemExit("%s: the walk printed nothing\n%s" % (path, out.stdout[-2000:]))
    return got, timing


def want(p):
    """The same three digests, off the file."""
    out = {}
    for name, edge, _ in LAYERS:
        v = p[name]
        out[("plane", name)] = (edge, digest(v))
        shift = {128: 0, 64: 1, 32: 2}[edge]
        out[("at", name)] = digest(v[(row >> shift) * edge + (col >> shift)]
                                   for row in range(128) for col in range(128))
    h = 2166136261
    for row in range(0, 128, 7):
        for col in range(0, 128, 5):
            for name, edge, _ in LAYERS:
                shift = {128: 0, 64: 1, 32: 2}[edge]
                for dc, dr in ((0, -1), (1, 0), (0, 1), (-1, 0)):
                    c, r = col + dc, row + dr
                    if 0 <= c < 128 and 0 <= r < 128:
                        h = (h * 31 + p[name][(r >> shift) * edge + (c >> shift)]) & MASK
                    else:
                        h = (h * 31 + 999) & MASK
    out[("near",)] = h
    return out


def main(argv):
    global BIN
    argv = argv[1:]
    if len(argv) >= 2 and argv[0] == "--binary":
        BIN, argv = argv[1], argv[2:]
    cities = argv or [os.path.join(ROOT, "cities", c) for c in
                          ("atlanta.sc2", "toronto.sc2", "flint.sc2")]
    bad = 0
    for path in cities:
        got, timing = run(path)
        exp = want(planes(path))
        miss = [k for k in exp if got.get(k) != exp[k]]
        name = os.path.basename(path)
        if miss:
            bad += 1
            for k in sorted(miss, key=str):
                print("%-14s %-24s file %s  script %s"
                      % (name, " ".join(k), exp[k], got.get(k)))
        else:
            print("%-14s %d layers, plane, at and near all agree with the file -- %s"
                  % (name, len(LAYERS), timing))
    if bad:
        print("%d of %d cities differ" % (bad, len(cities)))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
