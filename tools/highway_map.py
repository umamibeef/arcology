#!/usr/bin/env python3
"""What each highway tile id actually is, read off the shipped cities.

Part 7 of the road spec is engine-agnostic: it describes decks, ramps and
interchanges without saying which XBLD byte is which.  This answers that,
and it answers it from the shipped cities rather than from the sprite sheet,
because a sprite tells you what a tile LOOKS like and the neighbours tell
you what it IS.

For every id in 0x49..0x60 it reports:

  n          how many tiles carry it
  block      how often the tile sits in a 2x2 square of highway -- the
             spec's segment; a deck tile is always in one
  axis       which orthogonal neighbours are highway, as a mask: a deck
             tile running north-south joins N and S
  joins      the OTHER families it touches: road, rail, power.  This is
             what separates a ramp (touches road) from a crossing
             (touches rail) from plain deck (touches neither)
  altm       the ALTM high byte, which carries the bridge/tunnel flags

usage: highway_map.py [--csv]
"""
import collections
import glob
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from runsim import Sim  # noqa: E402

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
HW = set(range(0x49, 0x61))
FAM = {"road": set(range(0x1D, 0x2C)) | {0x43, 0x44, 0x45, 0x46},
       "rail": set(range(0x2C, 0x3F)) | {0x47, 0x48},
       "power": set(range(0x0E, 0x1D))}


def family(v):
    for k, s in FAM.items():
        if v in s:
            return k
    return "highway" if v in HW else None


def main():
    n = collections.Counter()
    inblock = collections.Counter()
    axis = collections.defaultdict(collections.Counter)
    joins = collections.defaultdict(collections.Counter)
    altm = collections.defaultdict(collections.Counter)
    for p in sorted(glob.glob(os.path.join(ROOT, "cities", "*.sc2"))):
        try:
            s = Sim(p)
        except Exception:
            continue
        b, a = s.layer("XBLD"), s.layer("ALTM")
        for i, v in enumerate(b):
            if v not in HW:
                continue
            r, c = divmod(i, 128)
            n[v] += 1
            altm[v][((a[i * 2] << 8) | a[i * 2 + 1]) >> 8 & 0xFF] += 1
            #  in a 2x2 square of highway, any of the four positions
            for dr in (-1, 0):
                for dc in (-1, 0):
                    q = [(r + dr + y, c + dc + x) for y in (0, 1) for x in (0, 1)]
                    if all(0 <= yy < 128 and 0 <= xx < 128 and
                           b[yy * 128 + xx] in HW for yy, xx in q):
                        inblock[v] += 1
                        break
                else:
                    continue
                break
            m = 0
            for bit, (dr, dc) in enumerate(((-1, 0), (0, 1), (1, 0), (0, -1))):
                rr, cc = r + dr, c + dc
                if not (0 <= rr < 128 and 0 <= cc < 128):
                    continue
                f = family(b[rr * 128 + cc])
                if f == "highway":
                    m |= 1 << bit
                elif f:
                    joins[v][f] += 1
            axis[v][m] += 1
    NAME = {0: "-", 1: "N", 2: "E", 4: "S", 8: "W", 5: "N-S", 10: "E-W",
            3: "NE", 6: "ES", 12: "SW", 9: "WN", 15: "all", 7: "NES",
            11: "NEW", 13: "NSW", 14: "ESW"}
    print("id    n     in 2x2   axis (top two)        joins            altm-hi")
    for v in sorted(n):
        top = "  ".join("%s x%d" % (NAME.get(m, bin(m)), k)
                        for m, k in axis[v].most_common(2))
        print("0x%02X %5d  %5d    %-22s %-16s %s"
              % (v, n[v], inblock[v], top,
                 ",".join("%s:%d" % kv for kv in joins[v].most_common(2)) or "-",
                 ",".join(str(k) for k, _ in altm[v].most_common(2))))
    return 0


if __name__ == "__main__":
    sys.exit(main())
