#!/usr/bin/env python3
"""Ask the original's $763A about every tile of a city and compare.

$763A(&y, &x, bld) returns the building's size and moves y and x to its
origin.  C calling convention, arguments pushed right to left.

usage: footprint_check.py <city> [<city>...]
"""
import subprocess, sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from runsim import Sim, A5


def _chk(r):
    """An oracle run that stopped early -- out of steps, or on a trap the
    interpreter does not implement -- did not produce the original's
    answer.  Treating one as ground truth invents a result."""
    if r is not None:
        raise RuntimeError("oracle stopped early: %s" % r)
    return r

SIMBIN = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                      "..", "build", "arcology")
SCRATCH = 0x00280000


def oracle(path):
    s = Sim(path)
    bld = s.layer("XBLD")
    out = {}
    for y in range(128):
        for x in range(128):
            s.e.wr(SCRATCH, 2, y)
            s.e.wr(SCRATCH + 2, 2, x)
            s.e.a[7] = 0x00300000
            s.e.a[7] -= 2; s.e.wr(s.e.a[7], 2, bld[y * 128 + x])
            s.e.a[7] -= 4; s.e.wr(s.e.a[7], 4, SCRATCH + 2)
            s.e.a[7] -= 4; s.e.wr(s.e.a[7], 4, SCRATCH)
            s.e.a[7] -= 4; s.e.wr(s.e.a[7], 4, 0xDEAD0000)
            _chk(s.e.run(0x763A, 0xDEAD0000, limit=200000,
                         real_calls=True, stubs=set()))
            n = s.e.d[0] & 0xFFFF
            oy, ox = s.e.rd(SCRATCH, 2), s.e.rd(SCRATCH + 2, 2)
            out[(y, x)] = (n, oy - 0x10000 if oy >= 0x8000 else oy,
                           ox - 0x10000 if ox >= 0x8000 else ox)
    return out


def main():
    bad = tot = 0
    for path in sys.argv[1:]:
        want = oracle(path)
        got = {}
        for line in subprocess.run([SIMBIN, "--footprint", path],
                                   capture_output=True, text=True).stdout.splitlines():
            f = line.split()
            if f and f[0] == "f":
                got[(int(f[1]), int(f[2]))] = (int(f[3]), int(f[4]), int(f[5]))
        shown = 0
        for k in sorted(want):
            tot += 1
            if want[k] != got.get(k):
                bad += 1
                if shown < 6:
                    print(f"  {os.path.basename(path)} {k}: want {want[k]} got {got.get(k)}")
                    shown += 1
    print(f"footprint: {tot - bad}/{tot} tiles exact")
    return 1 if bad else 0


sys.exit(main())
