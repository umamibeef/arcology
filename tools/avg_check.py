#!/usr/bin/env python3
"""Diff sim_overlay_averages against $224BA under the interpreter.

The four averages live behind pointers, so runsim has to allocate them
before the routine runs -- see the note on unset oracle state in
the project brief.  Both sides are given the same totals and the same developed
count, then the four results are compared.

usage: avg_check.py <city> [<city>...]
"""
import os
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from runsim import Sim, A5

SIMBIN = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                      "..", "build", "arcology")
PTR = {"traffic": 0x2BEC, "pollution": 0x2BF0,
       "land_value": 0x2BF4, "crime": 0x2BF8}


def oracle(path):
    s = Sim(path)
    #  run the city scan first so the totals and the developed count are
    #  the ones the averages are meant to divide
    s.run(0x2317E, stubs={0x9728, 0x15A54, 0x36EA6}, limit=200000000)
    #  $224BA is mid-function.  Entering there skips the prologue, so
    #  the epilogue pops a frame that was never pushed and the return
    #  lands nowhere.  Drive the whole routine instead.
    s.run(0x22330, stubs={0x9728, 0x15A54}, limit=60000000)
    out = {}
    for k, off in PTR.items():
        p = s.e.rd(A5 + off, 4)
        out[k] = s.e.rd(p, 4) if p else None
    out["developed"] = s.e.rd(A5 + 0x11D0, 2)
    return out


def main():
    bad = tot = 0
    for path in sys.argv[1:]:
        want = oracle(path)
        r = subprocess.run([SIMBIN, "--averages", path],
                           capture_output=True, text=True)
        got = {}
        for line in r.stdout.split("\n"):
            f = line.split()
            if len(f) == 2:
                got[f[0]] = int(f[1])
        for k in sorted(want):
            tot += 1
            if want[k] != got.get(k):
                bad += 1
                print("  %-14s %-12s want %-12s got %s"
                      % (os.path.basename(path), k, want[k], got.get(k)))
    print("overlayAverages: %d/%d values exact" % (tot - bad, tot))
    return 1 if bad else 0


sys.exit(main())
